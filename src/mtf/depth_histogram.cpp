#include "depth_histogram.h"

#include <bit>

void DepthHistogram::record(uint64_t distance)
{
    counts[bucketOf(distance)]++;
    total++;
    sumFound += distance;
    if (distance > maxFound)
    {
        maxFound = distance;
    }
}

void DepthHistogram::recordStart(uint64_t tilesWalked)
{
    start++;
    total++;
    sumStartWalk += tilesWalked;
}

void DepthHistogram::notePresent()
{
    present++;
}

int DepthHistogram::bucketOf(uint64_t distance)
{
    if (distance == 0)
    {
        return 0;
    }
    // 1 -> 1, 2 -> 2, 3..4 -> 3, 5..8 -> 4, 9..16 -> 5, ...
    const int b = 1 + int(std::bit_width(distance - 1));
    return b < kBuckets ? b : kBuckets - 1;
}

void DepthHistogram::bucketRange(int bucket, uint64_t& lo, uint64_t& hi)
{
    if (bucket <= 1)
    {
        lo = hi = uint64_t(bucket);
        return;
    }
    lo = (uint64_t(1) << (bucket - 2)) + 1;
    hi = uint64_t(1) << (bucket - 1);
}

static void rangeLabel(int bucket, char* label, size_t size)
{
    uint64_t lo, hi;
    DepthHistogram::bucketRange(bucket, lo, hi);
    if (lo == hi)
    {
        snprintf(label, size, "%llu", (unsigned long long)lo);
    }
    else
    {
        snprintf(label, size, "%llu-%llu", (unsigned long long)lo, (unsigned long long)hi);
    }
}

void printDepthHistogram(const char* input, size_t tileSize, size_t tiles, const char* kind, const DepthHistogram& h)
{
    printf("\n%s, tile %zu B, %s: %zu tiles, %llu values recorded\n", input, tileSize, kind, tiles,
           (unsigned long long)h.total);
    if (h.total == 0)
    {
        return;
    }

    int first = DepthHistogram::kBuckets;
    int last = -1;
    for (int b = 0; b < DepthHistogram::kBuckets; b++)
    {
        if (h.counts[b] != 0)
        {
            first = b < first ? b : first;
            last = b;
        }
    }

    printf("  %-15s %14s %8s %8s\n", "tiles back", "count", "%", "cum %");
    const double total = double(h.total);
    uint64_t cumulative = 0;
    for (int b = first; b <= last; b++)
    {
        char label[48];
        rangeLabel(b, label, sizeof(label));
        cumulative += h.counts[b];
        printf("  %-15s %14llu %8.3f %8.3f\n", label, (unsigned long long)h.counts[b],
               100.0 * double(h.counts[b]) / total, 100.0 * double(cumulative) / total);
    }
    cumulative += h.start;
    printf("  %-15s %14llu %8.3f %8.3f\n", "start", (unsigned long long)h.start, 100.0 * double(h.start) / total,
           100.0 * double(cumulative) / total);
    if (h.present != 0)
    {
        printf("  %-15s %14llu %8.3f   (in their own tile; not part of the total)\n", "present",
               (unsigned long long)h.present, 100.0 * double(h.present) / total);
    }
}

double fractionAtMost(const DepthHistogram& h, uint64_t distance)
{
    if (h.total == 0)
    {
        return 0.0;
    }
    uint64_t count = 0;
    for (int b = 0; b < DepthHistogram::kBuckets; b++)
    {
        uint64_t lo, hi;
        DepthHistogram::bucketRange(b, lo, hi);
        if (hi <= distance)
        {
            count += h.counts[b];
        }
    }
    return double(count) / double(h.total);
}

void writeDepthCsvHeader(FILE* f)
{
    fprintf(f, "input,bytes,tile_size,tiles,kind,bucket,lo,hi,count\n");
}

void writeDepthCsvRows(FILE* f, const char* input, size_t bytes, size_t tileSize, size_t tiles, const char* kind,
                       const DepthHistogram& h)
{
    for (int b = 0; b < DepthHistogram::kBuckets; b++)
    {
        if (h.counts[b] == 0)
        {
            continue;
        }
        char label[48];
        rangeLabel(b, label, sizeof(label));
        uint64_t lo, hi;
        DepthHistogram::bucketRange(b, lo, hi);
        fprintf(f, "%s,%zu,%zu,%zu,%s,%s,%llu,%llu,%llu\n", input, bytes, tileSize, tiles, kind, label,
                (unsigned long long)lo, (unsigned long long)hi, (unsigned long long)h.counts[b]);
    }
    if (h.start != 0)
    {
        fprintf(f, "%s,%zu,%zu,%zu,%s,start,,,%llu\n", input, bytes, tileSize, tiles, kind,
                (unsigned long long)h.start);
    }
    if (h.present != 0)
    {
        fprintf(f, "%s,%zu,%zu,%zu,%s,present,,,%llu\n", input, bytes, tileSize, tiles, kind,
                (unsigned long long)h.present);
    }
}