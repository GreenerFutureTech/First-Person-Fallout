#include "client_inventory_match.h"

#include <cstdio>
#include <cstdlib>

using namespace fallout;

struct TestObject { int netId = 0; int pid = 0; };
struct TestEntry { TestObject* item; int quantity; };
struct TestInventory { TestEntry* items; int length; };

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "Inventory reconcile regression: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    TestObject first {}, second {}, baseline {};
    first.netId = 10;
    second.netId = 11;
    first.pid = second.pid = baseline.pid = 42;
    TestEntry entries[] = { { &first, 1 }, { &second, 1 }, { &baseline, 1 } };
    TestInventory inventory {};
    inventory.items = entries;
    inventory.length = 3;
    std::vector<char> claimed(3, 0);

    require(clientInventoryMatchSlot(&inventory, 3, claimed, 11, 42) == 1,
        "same-model guns retain their own netId regardless of row order");
    require(clientInventoryMatchSlot(&inventory, 3, claimed, 10, 209) == 0,
        "an armed explosive retains its instance when its PID changes");
    require(clientInventoryMatchSlot(&inventory, 3, claimed, 12, 42) == 2,
        "an unbound baseline item can adopt an authoritative identity");
    claimed[2] = 1;
    require(clientInventoryMatchSlot(&inventory, 3, claimed, 12, 42) == -1,
        "a new bound instance cannot overwrite another same-model item");
    claimed[1] = 1;
    require(clientInventoryMatchSlot(&inventory, 3, claimed, 11, 42) == -1,
        "a claimed item cannot satisfy a second wire row");
    require(clientInventoryMatchSlot(&inventory, 3, claimed, 0, 42) == 0,
        "legacy zero-netId rows retain PID fallback");
    require(clientInventoryMatchSlot(&inventory, 0, claimed, 99, 42) == -1,
        "empty inventories require a new instance");
    std::puts("Inventory identity regressions passed");
}
