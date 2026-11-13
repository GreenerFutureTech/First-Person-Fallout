#ifndef FALLOUT_BILLBOARD_DEPTH_SORT_H_
#define FALLOUT_BILLBOARD_DEPTH_SORT_H_

#include <algorithm>
#include <cstddef>
#include <vector>

namespace fallout {
struct BillboardDepthRun {
    size_t first;
    size_t end;
};

// Preserve exactly the stable back-to-front order, sorting one descriptor
// per contiguous equal-depth run rather than every wall-clipped rectangle.
template <typename Billboard>
void billboardSortDepth(std::vector<Billboard>& items,
    std::vector<BillboardDepthRun>& runs, std::vector<Billboard>& scratch)
{
    const auto farther = [](const Billboard& a, const Billboard& b) {
        return a.depth > b.depth;
    };
    if (items.size() < 256) {
        std::stable_sort(items.begin(), items.end(), farther);
        return;
    }
    runs.clear();
    bool sorted = true;
    for (size_t first = 0; first < items.size();) {
        size_t end = first + 1;
        while (end < items.size() && items[end].depth == items[first].depth) ++end;
        if (end < items.size() && items[end].depth > items[first].depth) sorted = false;
        runs.push_back({ first, end });
        first = end;
    }
    if (sorted) return;
    if (runs.size() > items.size() / 4) {
        std::stable_sort(items.begin(), items.end(), farther);
        return;
    }
    std::stable_sort(runs.begin(), runs.end(), [&](const auto& a, const auto& b) {
        return items[a.first].depth > items[b.first].depth;
    });
    scratch.clear();
    scratch.reserve(items.size());
    for (const auto& run : runs) {
        scratch.insert(scratch.end(), items.begin() + run.first, items.begin() + run.end);
    }
    items.swap(scratch);
}
} // namespace fallout
#endif
