#ifndef FALLOUT_BILLBOARD_CLIP_BANDS_H_
#define FALLOUT_BILLBOARD_CLIP_BANDS_H_

#include <algorithm>
#include <vector>

namespace fallout {

// Within one horizon band, a column's visibility can change only at the
// exclusive bounds of a door cap. Reuse storage across sprites and frames.
class BillboardClipBands {
public:
    void reset(int top, int bottom)
    {
        top_ = top;
        bottom_ = bottom;
        boundaries_.clear();
        boundaries_.push_back(top);
        boundaries_.push_back(bottom);
    }

    void addOccluder(int top, int bottom)
    {
        if (top >= bottom || top >= bottom_ || bottom <= top_) {
            return;
        }
        if (top > top_) {
            boundaries_.push_back(top);
        }
        if (bottom < bottom_) {
            boundaries_.push_back(bottom);
        }
    }

    const std::vector<int>& boundaries()
    {
        std::sort(boundaries_.begin(), boundaries_.end());
        boundaries_.erase(std::unique(boundaries_.begin(), boundaries_.end()), boundaries_.end());
        return boundaries_;
    }

private:
    int top_ = 0;
    int bottom_ = 0;
    std::vector<int> boundaries_;
};

} // namespace fallout

#endif // FALLOUT_BILLBOARD_CLIP_BANDS_H_
