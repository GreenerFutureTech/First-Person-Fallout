#include "first_person_texture_runs.h"
#include <cstdio>
#include <cstdlib>
#include <random>

static void check(double top, double height, int bottom)
{
    int start = std::max(0, static_cast<int>(std::ceil(top)));
    for (int y = start; y < bottom;) {
        int end = fallout::firstPersonTextureRunEnd(y, bottom, top, height, 64);
        if (end <= y || end > bottom) std::abort();
        int expected = static_cast<int>((y - top) * 64 / height);
        for (int row = y; row < end; ++row) {
            // Match both the texture color and alpha lookup at every pixel,
            // including coordinates spanning multiple texture repetitions.
            if (static_cast<int>((row - top) * 64 / height) != expected) std::abort();
        }
        if (end < bottom && static_cast<int>((end - top) * 64 / height) == expected) std::abort();
        y = end;
    }
}

int main()
{
    for (double height : { 64.0, 255.999, 256.0, 256.001, 512.0, 4096.0, 20000.0 }) {
        for (double top : { -10000.0, -13.000000001, -13.0, -12.999999999, 0.0, 12.5 })
            check(top, height, 2160);
    }
    std::mt19937 random(871);
    for (int trial = 0; trial < 10000; ++trial) {
        double height = 256.0 + random() % 200000 / 10.0;
        double top = -double(random() % 200000) / 10.0;
        check(top, height, 1 + random() % 2160);
    }
    std::puts("Projected texture run tests passed");
}
