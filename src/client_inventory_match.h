#ifndef FALLOUT_CLIENT_INVENTORY_MATCH_H_
#define FALLOUT_CLIENT_INVENTORY_MATCH_H_

#include <vector>

namespace fallout {

// Reconcile authoritative instances without stealing a different bound item's
// identity. PID fallback is only for legacy rows or unbound baseline objects.
template <typename InventoryType>
inline int clientInventoryMatchSlot(const InventoryType* inventory, int length,
    const std::vector<char>& claimed, int netId, int pid)
{
    if (netId != 0) {
        for (int i = 0; i < length; ++i) {
            const auto* item = inventory->items[i].item;
            if (!claimed[i] && item != nullptr && item->netId == netId) return i;
        }
    }
    for (int i = 0; i < length; ++i) {
        const auto* item = inventory->items[i].item;
        if (!claimed[i] && item != nullptr && item->pid == pid
            && (netId == 0 || item->netId == 0)) return i;
    }
    return -1;
}

} // namespace fallout

#endif
