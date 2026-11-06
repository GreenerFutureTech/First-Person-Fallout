#include "billboard_clip_bands.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

struct Column {
    int top;
    int bottom;
    double capDepth;
    double wallDepth;
};

static void require(bool condition)
{
    if (!condition) {
        std::fputs("Billboard clipping band test failed\n", stderr);
        std::exit(1);
    }
}

static bool visible(const Column& column, int y, double actorDepth)
{
    double depth = column.wallDepth;
    if (y >= column.top && y < column.bottom) {
        depth = std::min(depth, column.capDepth);
    }
    return actorDepth < depth;
}

static void check(fallout::BillboardClipBands& bands, int top, int bottom,
    double actorDepth, const std::vector<Column>& columns)
{
    bands.reset(top, bottom);
    for (const Column& column : columns) {
        if (actorDepth >= column.capDepth) {
            bands.addOccluder(column.top, column.bottom);
        }
    }
    const auto& boundaries = bands.boundaries();
    require(boundaries.front() == top && boundaries.back() == bottom);
    int visitedRows = 0;
    for (size_t index = 0; index + 1 < boundaries.size(); index++) {
        require(boundaries[index] < boundaries[index + 1]);
        for (int y = boundaries[index]; y < boundaries[index + 1]; y++) {
            visitedRows++;
            for (const Column& column : columns) {
                // Compare band sampling with the old per-pixel depth test.
                require(visible(column, y, actorDepth)
                    == visible(column, boundaries[index], actorDepth));
            }
        }
    }
    require(visitedRows == bottom - top);
}

int main()
{
    fallout::BillboardClipBands bands;
    // Includes empty, reversed, offscreen, touching, overlapping, and fully
    // covering caps, with actor depth before, equal to, and behind each cap.
    for (int top = -2; top <= 8; top++) {
        for (int bottom = -2; bottom <= 8; bottom++) {
            for (double actorDepth : { 1.0, 2.0, 3.0 }) {
                check(bands, 0, 6, actorDepth,
                    { { top, bottom, 2.0, 4.0 }, { 2, 4, 2.0, 1.0 }, { -4, 10, 3.0, 5.0 } });
            }
        }
    }

    // Vary cap edges across columns, including patterns around the horizon.
    std::mt19937 random(12345);
    for (int scene = 0; scene < 500; scene++) {
        std::vector<Column> columns;
        for (int x = 0; x < 32; x++) {
            columns.push_back({ int(random() % 96) - 16, int(random() % 96) - 16,
                double(random() % 8), double(random() % 8) });
        }
        const double actorDepth = double(random() % 8);
        check(bands, 0, 32, actorDepth, columns);
        check(bands, 32, 64, actorDepth, columns);
    }

    // A flat cap across a wide sprite needs three width scans instead of 600.
    check(bands, 0, 600, 3.0, std::vector<Column>(640, { 100, 150, 2.0, 10.0 }));
    require(bands.boundaries().size() - 1 == 3);
    // Reused storage must not retain boundaries from a previous sprite.
    check(bands, 10, 20, 1.0, {});
    require(bands.boundaries().size() == 2);
    std::puts("Billboard clipping band tests passed");
}
