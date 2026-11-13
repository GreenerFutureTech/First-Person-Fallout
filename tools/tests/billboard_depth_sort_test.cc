#include "billboard_depth_sort.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>

struct Billboard { double depth; int identity; };
int main()
{
    std::vector<fallout::BillboardDepthRun> runs;
    std::vector<Billboard> scratch;
    std::mt19937 random(417);
    for (int trial = 0; trial < 500; ++trial) {
        std::vector<Billboard> actual;
        const int count = trial % 100;
        for (int group = 0; group < count; ++group) {
            double depth = random() % 10;
            if (group % 23 == 0) depth = std::numeric_limits<double>::infinity();
            int length = trial % 2 ? 1 : 1 + random() % 300;
            for (int i = 0; i < length; ++i) actual.push_back({ depth, int(actual.size()) });
        }
        auto expected = actual;
        std::stable_sort(expected.begin(), expected.end(),
            [](const auto& a, const auto& b) { return a.depth > b.depth; });
        for (int repeat = 0; repeat < 2; ++repeat) {
            fallout::billboardSortDepth(actual, runs, scratch);
            if (actual.size() != expected.size()) std::abort();
            for (size_t i = 0; i < actual.size(); ++i) {
                if (actual[i].identity != expected[i].identity) std::abort();
            }
        }
    }
    std::puts("Billboard depth sort tests passed");
}
