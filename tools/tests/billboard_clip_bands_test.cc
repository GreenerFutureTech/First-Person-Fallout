#include "billboard_clip_bands.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <limits>
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
        if (!fallout::billboardColumnHidden(actorDepth, column.wallDepth,
                column.wallDepth, false, false) && actorDepth >= column.capDepth) {
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

static void checkRunMatching()
{
    struct Rect { int x, y, w, h; };
    std::mt19937 random(4817);
    // Compare against the old exhaustive search, including changed widths,
    // gaps, empty bands, and runs that do not touch vertically.
    for (int trial = 0; trial < 1000; ++trial) {
        std::vector<Rect> previous;
        for (int x = 0; x < 1024;) {
            const int width = 1 + random() % 8;
            if (random() % 3 != 0) {
                previous.push_back({ x, 0, width, 1 + int(random() % 2) });
            }
            x += width + 1 + random() % 4;
        }
        size_t cursor = 0;
        for (int x = 0; x < 1024;) {
            const int width = 1 + random() % 8;
            const Rect run { x, 2, width, 3 };
            size_t expected = previous.size();
            for (size_t i = 0; i < previous.size(); ++i) {
                const Rect& candidate = previous[i];
                if (candidate.x == run.x && candidate.w == run.w
                    && candidate.y + candidate.h == run.y) {
                    expected = i;
                    break;
                }
            }
            require(fallout::billboardFindPreviousRun(previous, cursor, run) == expected);
            x += width + 1 + random() % 4;
        }
    }
    // Dense fence gaps: every strip must merge, with one forward traversal.
    std::vector<Rect> previous;
    for (int x = 0; x < 4096; x += 2) previous.push_back({ x, 0, 1, 5 });
    size_t cursor = 0;
    for (size_t i = 0; i < previous.size(); ++i) {
        const Rect run { previous[i].x, 5, 1, 2 };
        require(fallout::billboardFindPreviousRun(previous, cursor, run) == i);
    }
}

static void checkClipLayerOrder()
{
    struct Rect { int x, y, w, h; };
    std::vector<Rect> clips;
    for (int y = 0; y < 64; y += 2) {
        for (int x = 0; x < 64; x += 2) clips.push_back({ x, y, 1, 1 });
    }
    for (bool outline : { false, true }) {
        std::vector<int> reference(64 * 64, 0), batched(reference);
        const auto paint = [](std::vector<int>& pixels, const Rect& clip, bool edge) {
            for (int y = clip.y; y < clip.y + clip.h; ++y) {
                for (int x = clip.x; x < clip.x + clip.w; ++x) {
                    // Transparent sprite samples leave the outline visible.
                    if (edge || (x + y) % 3 != 0) pixels[y * 64 + x] = edge ? 1 : 2;
                }
            }
        };
        for (const auto& clip : clips) {
            if (outline) paint(reference, clip, true);
            paint(reference, clip, false);
        }
        int transitions = 0;
        int previousLayer = -1;
        fallout::billboardSubmitClipLayers(clips, outline, [&](const Rect& clip, bool edge) {
            if (previousLayer != int(edge)) ++transitions;
            previousLayer = int(edge);
            paint(batched, clip, edge);
        });
        require(reference == batched);
        require(transitions == (outline ? 2 : 1));
    }
    int calls = 0;
    fallout::billboardSubmitClipLayers(std::vector<Rect>{}, true,
        [&](const Rect&, bool) { ++calls; });
    require(calls == 0);
}

int main()
{
    checkClipLayerOrder();
    // Preserve equality, horizon/sill, infinity, and critter exceptions.
    const double infinity = std::numeric_limits<double>::infinity();
    for (double actor : { 1.0, 2.0, 3.0 }) {
        for (double wall : { 1.0, 2.0, infinity }) {
            for (double sill : { 1.0, 2.0, infinity }) {
                for (bool below : { false, true }) {
                    require(fallout::billboardColumnHidden(actor, wall, sill, below, false)
                        == (actor >= wall || (below && actor >= sill)));
                    require(!fallout::billboardColumnHidden(actor, wall, sill, below, true));
                }
            }
        }
    }

    checkRunMatching();
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
    // An exposed column beside many hidden fence columns needs no alpha
    // bands from those hidden columns, while per-pixel visibility stays equal.
    std::vector<Column> hiddenFence;
    hiddenFence.push_back({ 0, 0, 10.0, 10.0 });
    for (int x = 1; x < 1920; ++x) {
        hiddenFence.push_back({ x % 590, x % 590 + 8, 1.0, 2.0 });
    }
    check(bands, 0, 600, 3.0, hiddenFence);
    require(bands.boundaries().size() == 2);
    // Reused storage must not retain boundaries from a previous sprite.
    check(bands, 10, 20, 1.0, {});
    require(bands.boundaries().size() == 2);
    // Dense repeated fence gaps must produce exactly the same boundary set
    // as collecting every edge and sorting/uniquing afterward.
    for (int scene = 0; scene < 100; ++scene) {
        const int top = int(random() % 100) - 50;
        const int bottom = top + 1 + int(random() % 1080);
        bands.reset(top, bottom);
        std::vector<int> reference { top, bottom };
        for (int column = 0; column < 1920; ++column) {
            for (int gap = 0; gap < 20; ++gap) {
                const int edge = top - 10 + gap * 20 + column % 3;
                const int end = edge + 8;
                bands.addOccluder(edge, end);
                if (edge < bottom && end > top) {
                    if (edge > top) reference.push_back(edge);
                    if (end < bottom) reference.push_back(end);
                }
            }
        }
        std::sort(reference.begin(), reference.end());
        reference.erase(std::unique(reference.begin(), reference.end()), reference.end());
        require(bands.boundaries() == reference);
        require(bands.boundaries() == reference);
        // New edges after a read must invalidate the sorted result.
        bands.addOccluder(top, top + 1);
        if (top + 1 < bottom) reference.push_back(top + 1);
        std::sort(reference.begin(), reference.end());
        reference.erase(std::unique(reference.begin(), reference.end()), reference.end());
        require(bands.boundaries() == reference);
    }
    std::puts("Billboard clipping band tests passed");
}
