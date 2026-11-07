#ifndef FALLOUT_TRANSPARENT_WINDOW_RECTS_H_
#define FALLOUT_TRANSPARENT_WINDOW_RECTS_H_

#include <stddef.h>
#include <vector>

#include "geometry.h"

namespace fallout {

// Append only nonzero pixels from a transparent window, using screen-space
// inclusive rectangles. Merge matching runs vertically to limit draw calls.
inline void appendTransparentWindowRects(std::vector<Rect>& rects,
    const unsigned char* pixels, int pitch, int originX, int originY, const Rect& clip)
{
    std::vector<size_t> previous;
    std::vector<size_t> current;
    for (int y = clip.top; y <= clip.bottom; y++) {
        current.clear();
        size_t previousIndex = 0;
        const unsigned char* row = pixels + (y - originY) * pitch;
        int x = clip.left;
        while (x <= clip.right) {
            if (row[x - originX] == 0) {
                x++;
                continue;
            }
            const int left = x;
            while (x <= clip.right && row[x - originX] != 0) {
                x++;
            }
            const int right = x - 1;
            while (previousIndex < previous.size()
                && rects[previous[previousIndex]].left < left) {
                previousIndex++;
            }
            if (previousIndex < previous.size()
                && rects[previous[previousIndex]].left == left
                && rects[previous[previousIndex]].right == right) {
                const size_t index = previous[previousIndex];
                rects[index].bottom = y;
                current.push_back(index);
            } else {
                current.push_back(rects.size());
                rects.push_back({ left, y, right, y });
            }
        }
        previous.swap(current);
    }
}

} // namespace fallout

#endif // FALLOUT_TRANSPARENT_WINDOW_RECTS_H_
