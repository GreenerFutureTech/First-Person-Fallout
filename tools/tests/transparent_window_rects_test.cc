#include "transparent_window_rects.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

static void require(bool value)
{
    if (!value) {
        std::fputs("Transparent window coverage test failed\n", stderr);
        std::exit(1);
    }
}

static void check(const std::vector<unsigned char>& pixels, int pitch,
    int originX, int originY, const fallout::Rect& clip)
{
    // Existing opaque windows must survive appending a transparent overlay.
    std::vector<fallout::Rect> rects = { { -100, -100, -90, -90 } };
    fallout::appendTransparentWindowRects(rects, pixels.data(), pitch, originX, originY, clip);
    require(rects[0].bottom == -90);
    for (int y = clip.top; y <= clip.bottom; y++) {
        for (int x = clip.left; x <= clip.right; x++) {
            int coverage = 0;
            for (size_t i = 1; i < rects.size(); i++) {
                const auto& rect = rects[i];
                require(rect.left >= clip.left && rect.right <= clip.right);
                require(rect.top >= clip.top && rect.bottom <= clip.bottom);
                if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom) {
                    coverage++;
                }
            }
            // Zero coverage preserves the GPU scene; one copy overlays the UI.
            require(coverage == (pixels[(y - originY) * pitch + x - originX] != 0 ? 1 : 0));
        }
    }
}

int main()
{
    std::vector<unsigned char> menu(32 * 24, 0);
    for (int y = 8; y < 16; y++) {
        for (int x = 12; x < 20; x++) {
            menu[y * 32 + x] = 42;
        }
    }
    const fallout::Rect screen { 0, 0, 31, 23 };
    check(menu, 32, 0, 0, screen);
    std::vector<fallout::Rect> rects;
    fallout::appendTransparentWindowRects(rects, menu.data(), 32, 0, 0, screen);
    require(rects.size() == 1); // Solid menu block coalesces into one draw.

    std::mt19937 random(6789);
    for (int frame = 0; frame < 200; frame++) {
        for (auto& pixel : menu) {
            pixel = random() % 3 == 0 ? 0 : 1;
        }
        check(menu, 32, 0, 0, screen);
        // Partially offscreen window and nonzero source offset/pitch.
        check(menu, 32, -5, -3, { 0, 0, 20, 15 });
    }
    menu.assign(menu.size(), 0);
    check(menu, 32, 0, 0, screen); // Cleared/moved menu must leave no coverage.
    rects.clear();
    fallout::appendTransparentWindowRects(rects, menu.data(), 32, 0, 0, screen);
    require(rects.empty());
    std::puts("Transparent window coverage tests passed");
}
