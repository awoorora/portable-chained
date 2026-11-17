#include "inputs.h"

#include <libsais.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace
{
// splitmix64: a tiny generator whose output is fully specified, unlike
// std::uniform_int_distribution, which differs between MSVC and the Mac's libc++.
uint64_t splitmix64(uint64_t& state)
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
}

std::vector<uint8_t> makeRandom(size_t n, uint64_t seed)
{
    std::vector<uint8_t> out(n);
    uint64_t state = seed;
    for (size_t i = 0; i < n; i += 8)
    {
        uint64_t r = splitmix64(state);
        for (size_t b = 0; b < 8 && i + b < n; b++)
        {
            out[i + b] = uint8_t(r >> (8 * b));
        }
    }
    return out;
}

std::vector<uint8_t> makeSmallAlphabet(size_t n, uint32_t alphabetSize, uint64_t seed)
{
    if (alphabetSize < 1 || alphabetSize > 256)
    {
        std::fprintf(stderr, "makeSmallAlphabet: alphabetSize must be 1..256, got %u\n", alphabetSize);
        std::exit(1);
    }
    std::vector<uint8_t> out(n);
    uint64_t state = seed;
    for (size_t i = 0; i < n; i++)
    {
        uint32_t index = uint32_t(splitmix64(state) % alphabetSize);
        out[i] = uint8_t((index * 256u) / alphabetSize); // 16 symbols -> 0, 16, 32, ..., 240
    }
    return out;
}

std::vector<uint8_t> makeConstant(size_t n, uint8_t value)
{
    return std::vector<uint8_t>(n, value);
}

std::vector<uint8_t> makeCycle(size_t n)
{
    std::vector<uint8_t> out(n);
    for (size_t i = 0; i < n; i++)
    {
        out[i] = uint8_t(i); // wraps at 256
    }
    return out;
}

bool loadFile(const char* path, std::vector<uint8_t>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        std::fprintf(stderr, "Can't open \"%s\"\n", path);
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || uint64_t(size) > kMaxInputBytes)
    {
        std::fprintf(stderr, "\"%s\" is %lld bytes; the limit is %zu (2^30)\n", path, (long long)size,
                     kMaxInputBytes);
        return false;
    }
    out.resize(size_t(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(out.data()), size))
    {
        std::fprintf(stderr, "Reading \"%s\" failed\n", path);
        return false;
    }
    return true;
}

std::vector<uint8_t> bwt(const std::vector<uint8_t>& text, int32_t& primaryIndex)
{
    if (text.size() > kMaxInputBytes)
    {
        std::fprintf(stderr, "bwt: input of %zu bytes is over the 2^30 limit\n", text.size());
        std::exit(1);
    }
    const int32_t n = int32_t(text.size());
    std::vector<uint8_t> out(text.size());
    std::vector<int32_t> temp(text.size()); // libsais's work array, 4 bytes per input byte
    const int32_t result = libsais_bwt(text.data(), out.data(), temp.data(), n, 0, nullptr);
    if (result < 0)
    {
        std::fprintf(stderr, "libsais_bwt failed with %d\n", result);
        std::exit(1);
    }
    primaryIndex = result;
    return out;
}

std::string baseName(const char* path)
{
    std::string s(path);
    const size_t slash = s.find_last_of("/\\");
    return slash == std::string::npos ? s : s.substr(slash + 1);
}
