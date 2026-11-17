// CPU reference for MTF. First, self-tests whose expected values come from your
// own reasoning, not from serialMtf. Then serial MTF over every test input.
//
// Usage: mtf_cpu [file ...]
//   Each file is BWT-transformed with libsais before MTF (e.g. enwik8, samba).
#include "inputs.h"
#include "serial_mtf.h"

#include <chrono>
#include <cstdio>
#include <vector>

static std::vector<uint8_t> runMtf(const std::vector<uint8_t>& in)
{
    std::vector<uint8_t> out(in.size(), 0xAB); // poisoned, so an output serialMtf skips shows up
    serialMtf(in.data(), out.data(), in.size());
    return out;
}

static bool expectEqual(const char* name, const std::vector<uint8_t>& got, const std::vector<uint8_t>& want)
{
    if (got.size() != want.size())
    {
        printf("FAIL %s: %zu outputs, expected %zu\n", name, got.size(), want.size());
        return false;
    }
    for (size_t i = 0; i < got.size(); i++)
    {
        if (got[i] != want[i])
        {
            printf("FAIL %s: out[%zu] = %u, expected %u\n", name, i, got[i], want[i]);
            return false;
        }
    }
    printf("PASS %s\n", name);
    return true;
}

static bool selfTests()
{
    bool ok = true;

    // 1. Work this one out on paper first. 'a' = 97, 'b' = 98, 'n' = 110.

    // alphabet - [..., 97:a, 98:b, ..., 110:n, ...]
    // text to encode - "banana"
    //
    // | Symbol | List before | Output | List after |
    // | ------ | ----------- | ------ | ---------- |
    // | b      | [a, b, n]   | 98     | [b, a, n]  |
    // | a      | [b, a, n]   | 98     | [a, b, n]  |
    // | n      | [a, b, n]   | 110    | [n, a, b]  |
    // | a      | [n, a, b]   | 1      | [a, n, b]  |
    // | n      | [a, n, b]   | 1      | [n, a, b]  |
    // | a      | [n, a, b]   | 1      | [a, n, b]  |
    // Result: 98, 98, 110, 1, 1, 1

    {
        const std::vector<uint8_t> in = {'b', 'a', 'n', 'a', 'n', 'a'};
        const std::vector<uint8_t> want = { 98, 98, 110, 1, 1, 1};
        ok = expectEqual("banana (by hand)", runMtf(in), want) && ok;
    }

    // 2. One byte repeated. The value is nonzero on purpose: with 0, a wrong
    //    initial list could still pass. Work out the rule, then fill want with it.

    // Result: [byte_value], 0, 0, 0, ...
    {
        const std::vector<uint8_t> in = makeConstant(1000, 200);
        std::vector<uint8_t> want;
        want.push_back(in[0]);
        for (size_t i = 1; i < in.size(); i++)
        {
            want.push_back(0);
        }
        ok = expectEqual("constant 200", runMtf(in), want) && ok;
    }

    // 3. 0, 1, ..., 255, 0, 1, ... Long enough to see what happens after the first lap.

    // Smaller example
    // alphabet - [1, 2, 3]
    // text to encode - "123123"
    //
    // | Symbol | List before | Output | List after |
    // | ------ | ----------- | ------ | ---------- |
    // | 1      | [1, 2, 3]   | 0      | [1, 2, 3]  |
    // | 2      | [1, 2, 3]   | 1      | [2, 1, 3]  |
    // | 3      | [2, 1, 3]   | 2      | [3, 2, 1]  |
    // | 1      | [3, 2, 1]   | 2      | [1, 3, 2]  |
    // | 2      | [1, 3, 2]   | 2      | [2, 1, 3]  |
    // | 3      | [2, 1, 3]   | 2      | [3, 2, 1]  |
    // Result: `0, 1, 2, 2, 2, 2`

    // So for the big list:
    // in:  0, 1, 2, ... , 254, 255, 0,   1,   2,   ...
    // out: 0, 1, 2, ... , 254, 255, 255, 255, 255, ...

    {
        const std::vector<uint8_t> in = makeCycle(1000);
        std::vector<uint8_t> want;
        for (int i = 0; i < 256; i++)
        {
            want.push_back(uint8_t(i));
        }
        for (int i = 0; i < in.size() - 256; i++)
        {
            want.push_back(255);
        }
        ok = expectEqual("cycle 0..255", runMtf(in), want) && ok;
    }

    return ok;
}

int main(int argc, char** argv)
{
    printf("\n");

    if (!selfTests())
    {
        printf("\nSelf-tests failed.\n");
        return 1;
    }

    constexpr size_t n = size_t(1) << 24; // 16 MiB per generated input
    std::vector<Input> inputs;
    inputs.push_back({"random", makeRandom(n, 1)});
    inputs.push_back({"alphabet16", makeSmallAlphabet(n, 16, 2)});
    inputs.push_back({"constant200", makeConstant(n, 200)});
    inputs.push_back({"cycle256", makeCycle(n)});

    for (int i = 1; i < argc; i++)
    {
        std::vector<uint8_t> text;
        if (!loadFile(argv[i], text))
        {
            return 1;
        }
        const auto t0 = std::chrono::steady_clock::now();
        int32_t primaryIndex = 0;
        std::vector<uint8_t> transformed = bwt(text, primaryIndex);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("BWT of %s: %zu bytes in %.2f s, primary index %d\n", argv[i], text.size(), s, primaryIndex);
        inputs.push_back({baseName(argv[i]) + ".bwt", std::move(transformed)});
    }

    // Distinct symbols matter for the per-symbol design: a symbol that never appears
    // has to be found by looking back, all the way to the start.
    printf("\n%-16s %12s %10s %10s %8s %8s %9s\n", "input", "bytes", "ms", "MB/s", "zeros", "mean", "distinct");
    for (const Input& input : inputs)
    {
        std::vector<uint8_t> out(input.bytes.size());
        const auto t0 = std::chrono::steady_clock::now();
        serialMtf(input.bytes.data(), out.data(), input.bytes.size());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        size_t zeros = 0;
        uint64_t sum = 0;
        bool seen[256] = {};
        for (size_t i = 0; i < out.size(); i++)
        {
            zeros += (out[i] == 0);
            sum += out[i];
            seen[input.bytes[i]] = true;
        }
        int distinct = 0;
        for (bool b : seen)
        {
            distinct += b;
        }
        const double count = double(out.size());
        printf("%-16s %12zu %10.2f %10.1f %7.1f%% %8.2f %9d\n", input.name.c_str(), out.size(), ms,
               count / (ms * 1e3), 100.0 * double(zeros) / count, double(sum) / count, distinct);
    }

    return 0;
}
