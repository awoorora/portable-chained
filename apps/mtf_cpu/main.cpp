// CPU reference for MTF. First, self-tests whose expected values come from your
// own reasoning, not from serialMtf. Then serial MTF over every test input. Then the
// tiled CPU model over every input and tile size, checked against serial MTF, with
// look-back depth histograms.
//
// Usage: mtf_cpu [options] [file ...]
//   Each file is BWT-transformed with libsais before MTF (e.g. enwik8, samba).
//   --tiles 256,1024,4096   tile sizes in bytes for the tiled model (this is the default)
//   --size N                bytes per generated input (default 16777216, i.e. 16 MiB)
//   --csv PATH              also write the depth histograms to PATH as CSV
//   --no-histograms         print the summary table only, not one histogram per run
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS // fopen is fine here; MSVC would otherwise warn (C4996)
#endif

#include "inputs.h"
#include "serial_mtf.h"
#include "tiled_mtf.h"
#include "word_encoding.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct Options
{
    std::vector<size_t> tileSizes = {256, 1024, 4096};
    size_t generatedBytes = size_t(1) << 24;
    const char* csvPath = nullptr;
    bool printHistograms = true;
    std::vector<const char*> files;
};

static void printUsage()
{
    printf("Usage: mtf_cpu [--tiles 256,1024,4096] [--size N] [--csv PATH] [--no-histograms] [file ...]\n");
}

// A positive decimal integer, nothing else.
static bool parsePositive(const char* text, size_t& value)
{
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = strtoull(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || v == 0 || text[0] == '-')
    {
        return false;
    }
    value = size_t(v);
    return true;
}

// "256,1024,4096" -> {256, 1024, 4096}
static bool parseTileSizes(const char* text, std::vector<size_t>& sizes)
{
    sizes.clear();
    const std::string s(text);
    size_t pos = 0;
    while (true)
    {
        size_t comma = s.find(',', pos);
        if (comma == std::string::npos)
        {
            comma = s.size();
        }
        size_t value = 0;
        if (!parsePositive(s.substr(pos, comma - pos).c_str(), value))
        {
            return false;
        }
        sizes.push_back(value);
        if (comma == s.size())
        {
            return true;
        }
        pos = comma + 1;
    }
}

static bool parseOptions(int argc, char** argv, Options& opt)
{
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--tiles" || arg == "--size" || arg == "--csv")
        {
            if (i + 1 >= argc)
            {
                printf("%s needs a value\n", argv[i]);
                printUsage();
                return false;
            }
            const char* value = argv[++i];
            if (arg == "--tiles" && !parseTileSizes(value, opt.tileSizes))
            {
                printf("Bad --tiles \"%s\": expected positive integers separated by commas, e.g. 256,1024,4096\n",
                       value);
                return false;
            }
            if (arg == "--size" && (!parsePositive(value, opt.generatedBytes) || opt.generatedBytes > kMaxInputBytes))
            {
                printf("Bad --size \"%s\": expected 1..%zu\n", value, kMaxInputBytes);
                return false;
            }
            if (arg == "--csv")
            {
                opt.csvPath = value;
            }
        }
        else if (arg == "--no-histograms")
        {
            opt.printHistograms = false;
        }
        else if (arg == "--help" || arg == "-h")
        {
            printUsage();
            std::exit(0);
        }
        else if (arg.rfind("--", 0) == 0)
        {
            printf("Unknown option %s\n", argv[i]);
            printUsage();
            return false;
        }
        else
        {
            opt.files.push_back(argv[i]);
        }
    }
    return true;
}

static std::vector<uint8_t> runMtf(const std::vector<uint8_t>& in)
{
    std::vector<uint8_t> out(in.size(), 0xAB); // poisoned, so an output serialMtf skips shows up
    serialMtf(in.data(), out.data(), in.size());
    return out;
}

static std::vector<uint8_t> runTiled(const std::vector<uint8_t>& in, size_t tileSize)
{
    std::vector<uint8_t> out(in.size(), 0xAB); // poisoned, as above
    tiledMtf(in.data(), out.data(), in.size(), tileSize);
    return out;
}

// Index of the first difference, or SIZE_MAX if equal. Sizes must match.
static size_t firstMismatch(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want)
{
    for (size_t i = 0; i < got.size(); i++)
    {
        if (got[i] != want[i])
        {
            return i;
        }
    }
    return SIZE_MAX;
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

// Placeholder in a want[] that hasn't been filled in from your hand derivation yet.
constexpr uint32_t kFillIn = 0xF1111111u; // no real word can be this large

// Compares words for a few named symbols, printing them all on failure.
static bool expectWords(const char* name, const uint32_t* words, const uint8_t* symbols, const uint32_t* want,
                        int count)
{
    for (int k = 0; k < count; k++)
    {
        if (want[k] == kFillIn)
        {
            printf("TODO %s: fill in want from your hand derivation\n", name);
            return false;
        }
    }
    bool ok = true;
    for (int k = 0; k < count; k++)
    {
        ok = ok && words[symbols[k]] == want[k];
    }
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
    {
        for (int k = 0; k < count; k++)
        {
            printf("     symbol %3u: got %10u (0x%08X), expected %10u (0x%08X)\n", symbols[k], words[symbols[k]],
                   words[symbols[k]], want[k], want[k]);
        }
    }
    return ok;
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
        for (size_t i = 256; i < in.size(); i++)
        {
            want.push_back(255);
        }
        ok = expectEqual("cycle 0..255", runMtf(in), want) && ok;
    }

    return ok;
}

// Step 2's literal look-back, as the GPU does it: from tile t - 1 backwards, stop at the
// first tile that contains s. Its word is final, because every later position beats
// every earlier one. If no tile does, tile -1's word, which is always final. Compares
// that with tiledMtf's start words and reports the first difference.
constexpr size_t kMaxWalkTiles = 1000;

static bool startWordsMatchWalk(const char* name, const std::vector<uint8_t>& in, size_t tileSize)
{
    const size_t tiles = (in.size() + tileSize - 1) / tileSize;
    std::vector<uint32_t> local(tiles * 256);
    for (size_t t = 0; t < tiles; t++)
    {
        const size_t begin = t * tileSize;
        tileLastSeen(in.data(), begin, std::min(begin + tileSize, in.size()), &local[t * 256]);
    }

    std::vector<uint8_t> out(in.size(), 0xAB);
    std::vector<uint32_t> start;
    tiledMtf(in.data(), out.data(), in.size(), tileSize, nullptr, &start);

    for (size_t t = 0; t < tiles; t++)
    {
        for (int s = 0; s < 256; s++)
        {
            uint32_t want = initialWord(s);
            for (size_t u = t; u-- > 0;) // u = t - 1 down to 0
            {
                if (local[u * 256 + s] != kNotSeen)
                {
                    want = local[u * 256 + s];
                    break;
                }
            }
            if (start[t * 256 + s] != want)
            {
                printf("FAIL step 2: %s, tile size %zu: tile %zu, symbol %d: start word %u, the walk gives %u\n",
                       name, tileSize, t, s, start[t * 256 + s], want);
                return false;
            }
        }
    }
    return true;
}

// Checks of the tiled model on small inputs. Failures are reported but don't stop
// the program, so the later steps can still run while an earlier check is open.
static bool tiledSelfTests()
{
    bool ok = true;
    const std::vector<uint8_t> banana = {'b', 'a', 'n', 'a', 'n', 'a'};
    const uint8_t named[5] = {'a', 'n', 'b', 0, 255};

    // Step 1 checkpoint. banana with tile size 2: tiles "ba", "na", "na".
    // On paper first: tile 2's words for 'a', 'n' and 'b', in your encoding.
    //
    // Encoding (word_encoding.h): position p -> p + 257; not seen -> 0.
    // Tile 2 is in[4, 6) = "na":
    //   a: last at position 5 -> 262
    //   n: last at position 4 -> 261
    //   b: not in this tile   -> 0
    {
        uint32_t words[256];
        tileLastSeen(banana.data(), 4, 6, words);
        const uint32_t want[3] = {262, 261, 0};
        ok = expectWords("step 1: banana, tile size 2, tile 2's own words (a, n, b)", words, named, want, 3) && ok;
    }

    // Step 2 checkpoint. Same tiles. On paper first: tile 2's start words for 'a', 'n',
    // 'b', 0 and 255, in your encoding.
    //
    // Tile 2 starts after tiles 0 and 1, "bana" (positions 0-3). Symbol s never seen
    // there keeps its tile -1 word, 256 - s (it sits at position -1 - s).
    //   a:   last at position 3      -> 260
    //   n:   last at position 2      -> 259
    //   b:   last at position 0      -> 257
    //   0:   only in tile -1, at -1  -> 256
    //   255: only in tile -1, at -256 -> 1
    // Sorted by word, that is the list [a, n, b, 0, 1, ...], which serial MTF has after "bana".
    {
        std::vector<uint8_t> out(banana.size(), 0xAB);
        std::vector<uint32_t> start;
        tiledMtf(banana.data(), out.data(), banana.size(), 2, nullptr, &start);
        const uint32_t want[5] = {260, 259, 257, 256, 1};
        ok = expectWords("step 2: banana, tile size 2, tile 2's start words (a, n, b, 0, 255)", &start[2 * 256],
                         named, want, 5) && ok;
    }

    // Small inputs at awkward tile sizes, for steps 2 and 3: 1 (every byte its own tile),
    // sizes that don't divide n (a partial last tile), and sizes larger than n (a single tile).
    struct Small
    {
        const char* name;
        std::vector<uint8_t> bytes;
    };
    const std::vector<Small> small = {
        {"banana", banana},
        {"random 5000", makeRandom(5000, 7)},
        {"alphabet16 5000", makeSmallAlphabet(5000, 16, 8)},
        {"constant 1000", makeConstant(1000, 200)},
        {"cycle 1000", makeCycle(1000)},
    };
    const size_t tileSizes[] = {1, 2, 3, 7, 255, 256, 300, 1024, 6000};

    // Step 2 check. tiledMtf's start words must equal the literal look-back, the one the
    // GPU does. The walk is quadratic, so it skips runs with more than kMaxWalkTiles tiles.
    {
        for (const Small& input : small)
        {
            bool inputOk = true;
            int runs = 0;
            for (size_t tileSize : tileSizes)
            {
                if ((input.bytes.size() + tileSize - 1) / tileSize > kMaxWalkTiles)
                {
                    continue;
                }
                runs++;
                if (!startWordsMatchWalk(input.name, input.bytes, tileSize))
                {
                    inputOk = false;
                    break; // the first failing tile size is enough
                }
            }
            if (inputOk)
            {
                printf("PASS step 2: %s, start words equal the literal look-back (%d tile sizes)\n", input.name,
                       runs);
            }
            ok = ok && inputOk;
        }
    }

    // Step 3 checkpoint. The model must equal serialMtf on the small inputs.
    {
        for (const Small& input : small)
        {
            const std::vector<uint8_t> want = runMtf(input.bytes);
            bool inputOk = true;
            for (size_t tileSize : tileSizes)
            {
                const std::vector<uint8_t> got = runTiled(input.bytes, tileSize);
                const size_t i = firstMismatch(got, want);
                if (i != SIZE_MAX)
                {
                    printf("FAIL step 3: %s, tile size %zu: out[%zu] (tile %zu, offset %zu) = %u, expected %u\n",
                           input.name, tileSize, i, i / tileSize, i % tileSize, got[i], want[i]);
                    inputOk = false;
                    break; // the first failing tile size is enough
                }
            }
            if (inputOk)
            {
                printf("PASS step 3: %s, tile sizes 1 to 6000\n", input.name);
            }
            ok = ok && inputOk;
        }
    }

    return ok;
}

int main(int argc, char** argv)
{
    Options opt;
    if (!parseOptions(argc, argv, opt))
    {
        return 1;
    }

    printf("\n");

    if (!selfTests())
    {
        printf("\nSelf-tests failed.\n");
        return 1;
    }

    printf("\n");
    bool allOk = tiledSelfTests();

    const size_t n = opt.generatedBytes; // per generated input
    std::vector<Input> inputs;
    inputs.push_back({"random", makeRandom(n, 1)});
    inputs.push_back({"alphabet16", makeSmallAlphabet(n, 16, 2)});
    inputs.push_back({"constant200", makeConstant(n, 200)});
    inputs.push_back({"cycle256", makeCycle(n)});

    for (const char* path : opt.files)
    {
        std::vector<uint8_t> text;
        if (!loadFile(path, text))
        {
            return 1;
        }
        const auto t0 = std::chrono::steady_clock::now();
        int32_t primaryIndex = 0;
        std::vector<uint8_t> transformed = bwt(text, primaryIndex);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("BWT of %s: %zu bytes in %.2f s, primary index %d\n", path, text.size(), s, primaryIndex);
        inputs.push_back({baseName(path) + ".bwt", std::move(transformed)});
    }

    // Distinct symbols matter for the per-symbol design: a symbol that never appears
    // has to be found by looking back, all the way to the start.
    std::vector<std::vector<uint8_t>> reference; // serial outputs, same order as inputs
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

        reference.push_back(std::move(out));
    }

    // The tiled model over every input and tile size. "found" counts the (tile, symbol)
    // pairs whose look-back found an earlier tile containing the symbol (or the tile
    // itself); "steps" averages tiles walked over all pairs, "start" ones included.
    FILE* csv = nullptr;
    if (opt.csvPath != nullptr)
    {
        csv = fopen(opt.csvPath, "w");
        if (csv == nullptr)
        {
            printf("Can't write \"%s\"\n", opt.csvPath);
            return 1;
        }
        writeDepthCsvHeader(csv);
    }

    struct Run
    {
        const char* name;
        size_t tileSize;
        size_t tiles;
        DepthHistogram depth;
    };
    std::vector<Run> runs;

    printf("\nTiled model, checked against serial MTF\n");
    printf("%-16s %6s %9s %9s %8s %8s %8s %10s %10s %10s %6s\n", "input", "tile", "tiles", "ms", "present", "1 back",
           "start", "mean found", "mean steps", "max found", "match");
    for (size_t k = 0; k < inputs.size(); k++)
    {
        const Input& input = inputs[k];
        for (size_t tileSize : opt.tileSizes)
        {
            Run run{input.name.c_str(), tileSize, (input.bytes.size() + tileSize - 1) / tileSize, {}};

            std::vector<uint8_t> out(input.bytes.size(), 0xAB); // poisoned
            const auto t0 = std::chrono::steady_clock::now();
            tiledMtf(input.bytes.data(), out.data(), out.size(), tileSize, &run.depth);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

            const size_t bad = firstMismatch(out, reference[k]);
            const DepthHistogram& h = run.depth;
            const double total = h.total != 0 ? double(h.total) : 1.0;
            const uint64_t found = h.total - h.start;
            printf("%-16s %6zu %9zu %9.1f %7.2f%% %7.2f%% %7.2f%% %10.2f %10.1f %10llu %6s\n", run.name, tileSize,
                   run.tiles, ms, 100.0 * double(h.present) / total, 100.0 * double(h.counts[1]) / total,
                   100.0 * double(h.start) / total, found != 0 ? double(h.sumFound) / double(found) : 0.0,
                   double(h.sumFound + h.sumStartWalk) / total, (unsigned long long)h.maxFound,
                   bad == SIZE_MAX ? "yes" : "NO");
            if (bad != SIZE_MAX)
            {
                printf("    first mismatch: out[%zu] (tile %zu, offset %zu) = %u, expected %u\n", bad, bad / tileSize,
                       bad % tileSize, out[bad], reference[k][bad]);
                allOk = false;
            }

            if (csv != nullptr)
            {
                writeDepthCsvRows(csv, run.name, input.bytes.size(), tileSize, run.tiles, run.depth);
            }
            runs.push_back(run);
        }
    }

    if (csv != nullptr)
    {
        fclose(csv);
        printf("\nDepth histograms written to %s\n", opt.csvPath);
    }

    if (opt.printHistograms)
    {
        for (const Run& run : runs)
        {
            printDepthHistogram(run.name, run.tileSize, run.tiles, run.depth);
        }
    }

    if (!allOk)
    {
        printf("\nThe tiled model does not match serial MTF yet (see FAIL and NO above).\n");
        return 1;
    }
    return 0;
}