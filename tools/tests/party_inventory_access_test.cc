#include "party_inventory_access.h"

#include <cstdio>
#include <cstdlib>

using namespace fallout;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "Party inventory access regression: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    require(partyInventoryAccess(true, true, true, true, true, true, true),
        "self can manage their own inventory");
    require(partyInventoryAccess(false, false, true, true, true, true, true),
        "friendly NPC companions can be managed");
    require(!partyInventoryAccess(false, false, false, true, true, true, true),
        "non-critter subjects are rejected");
    require(!partyInventoryAccess(false, true, true, true, true, true, true),
        "other human actors remain blocked even if party members");
    require(!partyInventoryAccess(false, false, true, false, true, true, true),
        "nonparty NPCs are denied access");
    require(!partyInventoryAccess(false, false, true, true, false, true, true),
        "inactive or dead critter targets are rejected");
    require(!partyInventoryAccess(false, false, true, true, true, false, true),
        "elevation mismatches are rejected");
    require(!partyInventoryAccess(false, false, true, true, true, true, false),
        "range mismatches are rejected");

    std::puts("Party inventory access regressions passed");
}
