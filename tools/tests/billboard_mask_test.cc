#include "billboard_mask.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>

struct Rect { int x, y, w, h; };
static void require(bool condition) { if (!condition) std::abort(); }

int main()
{
    const unsigned char sprite[] = { 1, 0, 2, 3 };
    const unsigned char outline[] = { 9, 9, 9, 9 };
    const Rect destination { -2, -2, 4, 4 };
    const Rect clip { 0, 0, 2, 2 };
    std::vector<unsigned char> output;
    // Clipping must preserve source coordinates for a partly offscreen sprite.
    require(fallout::billboardMask(sprite, 2, 2, nullptr, 0, 0,
        destination, clip, [](int x, int y) { return x == y; }, output));
    require(output == std::vector<unsigned char>({ 3, 0, 0, 3 }));
    const Rect full { 0, 0, 2, 2 };
    require(fallout::billboardMask(sprite, 2, 2, outline, 2, 2,
        full, full, [](int, int) { return true; }, output));
    require(output == std::vector<unsigned char>({ 1, 9, 2, 3 }));
    require(!fallout::billboardMask(sprite, 2, 2, outline, 2, 2,
        full, full, [](int, int) { return false; }, output));
    require(output == std::vector<unsigned char>(4, 0));
    // Fractional reduction uses the same pixel centers as GPU nearest sampling.
    const unsigned char strip[] = { 4, 5, 6 };
    const Rect reduced { 0, 0, 2, 1 };
    require(fallout::billboardMask(strip, 3, 1, nullptr, 0, 0,
        reduced, reduced, [](int, int) { return true; }, output));
    require(output == std::vector<unsigned char>({ 4, 6 }));

    // Compare to an independent per-pixel depth oracle, including multiple
    // fences, solid walls behind their holes, foreground and equal-depth actors.
    constexpr int width = 97, height = 71;
    const Rect screen { 0, 0, width, height };
    std::mt19937 random(109);
    std::vector<double> depths(width * height);
    for (double& depth : depths) {
        const unsigned choice = random() % 4;
        depth = choice == 0 ? std::numeric_limits<double>::infinity() : choice * 2.0;
    }
    const unsigned char solid[] = { 5 };
    for (double actor : { 1.0, 2.0, 3.0, 5.0, 7.0 }) {
        size_t checks = 0;
        fallout::billboardMask(solid, 1, 1, nullptr, 0, 0, screen, screen,
            [&](int x, int y) {
                ++checks;
                return actor < depths[x * height + y];
            }, output);
        require(checks == width * height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                require(output[y * width + x] == (actor < depths[x * height + y] ? 5 : 0));
            }
        }
    }
    // Reused output must not retain a previous frame's opaque pixels.
    const unsigned char clear[] = { 0 };
    require(!fallout::billboardMask(clear, 1, 1, nullptr, 0, 0, screen, screen,
        [](int, int) { std::abort(); return false; }, output));
    for (unsigned char color : output) require(color == 0);
    std::puts("billboard mask tests passed");
}
