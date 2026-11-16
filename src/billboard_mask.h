#ifndef FALLOUT_BILLBOARD_MASK_H_
#define FALLOUT_BILLBOARD_MASK_H_

#include <cstdint>
#include <cstddef>
#include <vector>

namespace fallout {

// Rasterize only the on-screen rectangle. Work and output size depend on
// covered pixels, never on the number of holes in a fence. Index zero is clear.
// Rects use exclusive right/bottom bounds; clip must be inside destination.
template <typename Rect, typename Visible>
bool billboardMask(const unsigned char* pixels, int width, int height,
    const unsigned char* outline, int outlineWidth, int outlineHeight,
    const Rect& destination, const Rect& clip, Visible visible,
    std::vector<unsigned char>& output)
{
    output.resize(static_cast<size_t>(clip.w) * clip.h);
    // Scale once per screen column, not with an integer division per pixel.
    static std::vector<int> sourceColumns;
    static std::vector<int> outlineColumns;
    sourceColumns.resize(clip.w);
    if (outline != nullptr) outlineColumns.resize(clip.w);
    for (int column = 0; column < clip.w; ++column) {
        // Match nearest GPU texture sampling at screen-pixel centers.
        const int64_t relativeX = 2LL * (clip.x + column - destination.x) + 1;
        sourceColumns[column] = static_cast<int>(relativeX * width / (2LL * destination.w));
        if (outline != nullptr) {
            outlineColumns[column] = static_cast<int>(relativeX * outlineWidth / (2LL * destination.w));
        }
    }
    bool any = false;
    for (int row = 0; row < clip.h; ++row) {
        const int y = clip.y + row;
        const int64_t relativeY = 2LL * (y - destination.y) + 1;
        const int sourceY = static_cast<int>(relativeY * height / (2LL * destination.h));
        const int outlineY = outline != nullptr
            ? static_cast<int>(relativeY * outlineHeight / (2LL * destination.h)) : 0;
        for (int column = 0; column < clip.w; ++column) {
            const int x = clip.x + column;
            const int sourceX = sourceColumns[column];
            unsigned char color = pixels[sourceY * width + sourceX];
            if (color == 0 && outline != nullptr) {
                const int outlineX = outlineColumns[column];
                color = outline[outlineY * outlineWidth + outlineX];
            }
            if (color != 0 && !visible(x, y)) color = 0;
            output[static_cast<size_t>(row) * clip.w + column] = color;
            any |= color != 0;
        }
    }
    return any;
}

} // namespace fallout

#endif
