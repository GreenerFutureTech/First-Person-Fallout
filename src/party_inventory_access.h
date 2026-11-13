#ifndef FALLOUT_PARTY_INVENTORY_ACCESS_H_
#define FALLOUT_PARTY_INVENTORY_ACCESS_H_

namespace fallout {

// Membership never grants control of another human player's inventory.
constexpr bool partyInventoryAccess(bool self, bool playerActor, bool critter,
    bool partyMember, bool active, bool sameElevation, bool inRange)
{
    return critter && active && sameElevation && inRange
        && (self || (!playerActor && partyMember));
}

} // namespace fallout
#endif
