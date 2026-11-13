#ifndef FALLOUT_BILLBOARD_CLIP_BANDS_H_
#define FALLOUT_BILLBOARD_CLIP_BANDS_H_

#include <algorithm>
#include <cstdint>
#include <vector>

namespace fallout {

// A range never crosses the horizon. A solid wall/sill therefore hides
// this entire column, regardless of finer alpha spans. Critters deliberately
// use pixel depths instead of these conservative wall/sill depths.
inline bool billboardColumnHidden(double actorDepth, double wallDepth,
    double sillDepth, bool belowHorizon, bool pixelDepthOnly)
{
    return !pixelDepthOnly
        && actorDepth >= (belowHorizon ? std::min(wallDepth, sillDepth) : wallDepth);
}

// Both bands contain disjoint runs ordered by x. Advance monotonically
// instead of searching the entire previous band for every new run.
template <typename Rect>
size_t billboardFindPreviousRun(const std::vector<Rect>& previous,
    size_t& cursor, const Rect& run)
{
    while (cursor < previous.size() && previous[cursor].x < run.x) {
        ++cursor;
    }
    if (cursor < previous.size()) {
        const Rect& candidate = previous[cursor];
        if (candidate.x == run.x && candidate.w == run.w
            && candidate.y + candidate.h == run.y) {
            return cursor;
        }
    }
    return previous.size();
}

// Visibility clips for a sprite are disjoint. Submit each texture layer
// together so outlines do not break GPU batching at every wall gap.
template <typename Rect, typename Submit>
void billboardSubmitClipLayers(const std::vector<Rect>& clips, bool outline, Submit submit)
{
    if (outline) {
        for (const Rect& clip : clips) submit(clip, true);
    }
    for (const Rect& clip : clips) submit(clip, false);
}

// Within one horizon band, a column's visibility can change only at the
// exclusive bounds of a door cap or opaque wall span. Reuse storage across
// sprites and frames, admitting each screen-row boundary only once.
class BillboardClipBands {
public:
    void reset(int top, int bottom)
    {
        top_ = top;
        bottom_ = bottom;
        boundaries_.clear();
        boundaries_.push_back(top);
        boundaries_.push_back(bottom);
        sorted_ = false;
        const size_t rowCount = static_cast<size_t>(std::max(0, bottom - top)) + 1;
        if (rowStamps_.size() < rowCount) rowStamps_.resize(rowCount, 0);
        if (++stamp_ == 0) {
            std::fill(rowStamps_.begin(), rowStamps_.end(), 0);
            ++stamp_;
        }
    }

    void addOccluder(int top, int bottom)
    {
        if (top >= bottom || top >= bottom_ || bottom <= top_) {
            return;
        }
        if (top > top_) {
            addBoundary(top);
        }
        if (bottom < bottom_) {
            addBoundary(bottom);
        }
    }

    const std::vector<int>& boundaries()
    {
        if (!sorted_) {
            std::sort(boundaries_.begin(), boundaries_.end());
            sorted_ = true;
        }
        return boundaries_;
    }

private:
    void addBoundary(int row)
    {
        uint32_t& seen = rowStamps_[static_cast<size_t>(row - top_)];
        if (seen != stamp_) {
            seen = stamp_;
            boundaries_.push_back(row);
            sorted_ = false;
        }
    }

    // Generation stamps avoid clearing a screen-height array for each sprite.
    std::vector<uint32_t> rowStamps_;
    uint32_t stamp_ = 0;
    bool sorted_ = false;
    int top_ = 0;
    int bottom_ = 0;
    std::vector<int> boundaries_;
};

} // namespace fallout

#endif // FALLOUT_BILLBOARD_CLIP_BANDS_H_
