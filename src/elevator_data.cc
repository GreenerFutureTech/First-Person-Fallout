#include <ctype.h>
#include "art.h"
#include "cycle.h"
#include "dbox.h"
#include "delay.h"
#include "draw.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "geometry.h"
#include "input.h"
#include "interface.h"
#include "kb.h"
#include "map.h"
#include "pipboy.h"
#include "scripts.h"
#include "settings.h"
#include "svga.h"
#include "touch.h"
#include "window_manager.h"
#include "worldmap.h"
#include "elevator.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "animation.h" // reg_anim_clear — an elevator ride cancels a walk in progress
#include "config.h"
#include "debug.h"
#include "game.h" // gDude
#include "light.h" // _obj_rebuild_all_light — the closing doors change the lighting
#include "map.h" // gMapHeader / mapSetTransition
#include "object.h"
#include "proto_types.h" // PROTO_ID_* — the three elevator-door scenery pids
#include "proto_instance.h" // objectAttemptPlacement
#include "server_players.h" // playerActorAt/Count/Online — the party rides together
#include "sfall_config.h"
#include "tile.h" // tileDistanceBetween

namespace fallout {

// ─── ELEVATOR DATA, split out of the client's elevator.cc ─────────────────────
// The DEDICATED SERVER has to resolve an elevator ride itself, so the tables and
// the two resolvers live here in f2_core while the picker SCREEN stays in
// elevator.cc (f2_client) — the character_editor_state.cc precedent: move the
// state, not the projector.
//
// ►► THIS SPLIT IS A TRUST BOUNDARY, not tidiness. The client owns the screen and
// therefore knows which BUTTON was pressed; it must never be the thing that says
// which map/elevation/tile that button means. If the destination came off the wire,
// any client could ride to any coordinate in the game. The server takes the level
// index and resolves it against ITS OWN copy of these tables.

// The maximum number of elevator levels.
#define ELEVATOR_LEVEL_MAX (4)

// Max number of elevators that can be loaded from elevators.ini. This limit is
// emposed by Sfall.
#define ELEVATORS_MAX 50

typedef struct ElevatorDescription {
    Map map;
    int elevation;
    int tile;
} ElevatorDescription;

// Number of levels for eleveators.
//
// 0x43EA1C
static int gElevatorLevels[ELEVATORS_MAX] = {
    4,
    2,
    3,
    2,
    3,
    2,
    3,
    3,
    3,
    3,
    3,
    2,
    4,
    2,
    3,
    3,
    3,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
};

// 0x43EA7C
static ElevatorDescription gElevatorDescriptions[ELEVATORS_MAX][ELEVATOR_LEVEL_MAX] = {
    {
        { MAP_KLAMATH_GRAZE, ELEVATION_FIRST, 18940 },
        { MAP_KLAMATH_GRAZE, ELEVATION_SECOND, 18936 },
        { MAP_VAULTCITY_COURTYARD, ELEVATION_FIRST, 21340 },
        { MAP_VAULTCITY_COURTYARD, ELEVATION_SECOND, 21340 },
    },
    {
        { MAP_KLAMATH_TRAPCAVES, ELEVATION_FIRST, 20502 },
        { MAP_KLAMATH_GRAZE, ELEVATION_FIRST, 14912 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_GECKO_JUNKYARD, ELEVATION_FIRST, 12498 },
        { MAP_GECKO_JUNKYARD, ELEVATION_SECOND, 20094 },
        { MAP_GECKO_ACCESS_TUNNELS, ELEVATION_FIRST, 17312 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_GECKO_ACCESS_TUNNELS, ELEVATION_FIRST, 16140 },
        { MAP_GECKO_ACCESS_TUNNELS, ELEVATION_SECOND, 16140 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_MILITARY_BASE_12, ELEVATION_FIRST, 14920 },
        { MAP_MILITARY_BASE_12, ELEVATION_SECOND, 15120 },
        { MAP_MILITARY_BASE_34, ELEVATION_FIRST, 12944 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_MILITARY_BASE_34, ELEVATION_FIRST, 24520 },
        { MAP_MILITARY_BASE_34, ELEVATION_SECOND, 25316 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_NCR_DOWNTOWN, ELEVATION_FIRST, 22526 },
        { MAP_NCR_DOWNTOWN, ELEVATION_SECOND, 22526 },
        { MAP_NCR_DOWNTOWN, ELEVATION_THIRD, 22526 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_NCR_DOWNTOWN, ELEVATION_THIRD, 14086 },
        { MAP_NCR_COUNCIL_1, ELEVATION_FIRST, 14086 },
        { MAP_NCR_COUNCIL_1, ELEVATION_THIRD, 14086 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_VAULT_13, ELEVATION_FIRST, 14104 },
        { MAP_VAULT_13, ELEVATION_SECOND, 22504 },
        { MAP_VAULT_13, ELEVATION_THIRD, 17312 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_KLAMATH_1, ELEVATION_FIRST, 13704 },
        { MAP_KLAMATH_1, ELEVATION_SECOND, 23302 },
        { MAP_KLAMATH_1, ELEVATION_THIRD, 17308 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_SIERRA_123, ELEVATION_FIRST, 19300 },
        { MAP_SIERRA_123, ELEVATION_SECOND, 19300 },
        { MAP_SIERRA_123, ELEVATION_THIRD, 20110 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_SIERRA_123, ELEVATION_THIRD, 20118 },
        { MAP_SIERRA_4, ELEVATION_FIRST, 21710 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_SIERRA_123, ELEVATION_FIRST, 20122 },
        { MAP_SIERRA_123, ELEVATION_SECOND, 20124 },
        { MAP_SIERRA_123, ELEVATION_THIRD, 20940 },
        { MAP_SIERRA_4, ELEVATION_FIRST, 22540 },
    },
    {
        { MAP_KLAMATH_TOXICCAVES, ELEVATION_SECOND, 16052 },
        { MAP_KLAMATH_TOXICCAVES, ELEVATION_THIRD, 14480 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_DEN_ENTRANCE, ELEVATION_FIRST, 14104 },
        { MAP_DEN_ENTRANCE, ELEVATION_SECOND, 22504 },
        { MAP_DEN_ENTRANCE, ELEVATION_THIRD, 17312 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_VAULT_CITY_VAULT, ELEVATION_FIRST, 14104 },
        { MAP_VAULT_CITY_VAULT, ELEVATION_SECOND, 22504 },
        { MAP_VAULT_CITY_VAULT, ELEVATION_THIRD, 17312 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_VAULT_15, ELEVATION_FIRST, 13704 },
        { MAP_VAULT_15, ELEVATION_SECOND, 23302 },
        { MAP_VAULT_15, ELEVATION_THIRD, 17308 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_VAULT_15_EAST_ENTRANCE, ELEVATION_FIRST, 17285 },
        { MAP_VAULT_15, ELEVATION_FIRST, 19472 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_NAVARRO_ENTRANCE, ELEVATION_THIRD, 10701 },
        { MAP_NAVARRO_ENTRANCE, ELEVATION_SECOND, 10705 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_NAVARRO_ENTRANCE, ELEVATION_THIRD, 14697 },
        { MAP_NAVARRO_ENTRANCE, ELEVATION_SECOND, 15099 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_NAVARRO_ENTRANCE, ELEVATION_THIRD, 23877 },
        { MAP_NAVARRO_ENTRANCE, ELEVATION_SECOND, 25481 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_NAVARRO_ENTRANCE, ELEVATION_THIRD, 26130 },
        { MAP_NAVARRO_ENTRANCE, ELEVATION_SECOND, 24721 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_SAN_FRAN_CHINATOWN, ELEVATION_FIRST, 23953 },
        { MAP_SHI_TEMPLE, ELEVATION_SECOND, 16526 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
    {
        { MAP_REDDING_WANAMINGO_ENT, ELEVATION_FIRST, 13901 },
        { MAP_REDDING_WANAMINGO_12, ELEVATION_SECOND, 17923 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
        { MAP_RND_DESERT_1, ELEVATION_FIRST, -1 },
    },
};

// The number of buttons this elevator's panel has — the range a level index from a
// client must fall inside. Reads the same (possibly sfall-overridden) table the
// resolvers use, so the check can never disagree with the resolution.
int elevatorLevelCount(int elevator)
{
    if (elevator < 0 || elevator >= ELEVATORS_MAX) {
        return 0;
    }

    return gElevatorLevels[elevator];
}

// Ledger H-57 (extracted from the elevator UI): normalize the player's
// current map/elevation into the elevator's gauge level index, including the
// Sierra Army Depot / Military Base remap math.
int elevatorResolveStartLevel(int elevator, int map, int elevation)
{
    if (elevatorLevelCount(elevator) < 2) return 0;
    const ElevatorDescription* elevatorDescription = gElevatorDescriptions[elevator];

    int index;
    for (index = 0; index < ELEVATOR_LEVEL_MAX; index++) {
        if (elevatorDescription[index].map == map) {
            break;
        }
    }

    if (index < ELEVATOR_LEVEL_MAX) {
        if (elevation + index >= 0 && elevation + index < ELEVATOR_LEVEL_MAX && elevatorDescription[elevation + index].tile != -1) {
            elevation += index;
        }
    }

    if (elevator == ELEVATOR_SIERRA_2) {
        if (elevation <= 2) {
            elevation -= 2;
        } else {
            elevation -= 3;
        }
    } else if (elevator == ELEVATOR_MILITARY_BASE_LOWER) {
        if (elevation >= 2) {
            elevation -= 2;
        }
    } else if (elevator == ELEVATOR_MILITARY_BASE_UPPER && elevation == 4) {
        elevation -= 2;
    }

    if (elevation > 3) {
        elevation -= 3;
    }

    return std::clamp(elevation, 0, elevatorLevelCount(elevator) - 1);
}

// Ledger H-57: resolve a chosen elevator level into its {map, elevation,
// tile} destination from the elevator table.
void elevatorResolveDestination(int elevator, int level, Map* mapPtr, int* elevationPtr, int* tilePtr)
{
    if (elevator < 0 || elevator >= ELEVATORS_MAX || level < 0 || level >= elevatorLevelCount(elevator)) {
        *tilePtr = -1;
        return;
    }
    const ElevatorDescription* description = &(gElevatorDescriptions[elevator][level]);
    *mapPtr = description->map;
    *elevationPtr = description->elevation;
    *tilePtr = description->tile;
}

// The DATA half of the sfall elevators.ini override (elevator.cc keeps the art
// half). Each side reads the file for its own tables: the alternative is one
// reader that reaches across the core/client line, which is the coupling this
// split exists to remove. Init-time, once, and the file is usually absent.
void elevatorsDataInit()
{
    char* elevatorsFileName;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_ELEVATORS_FILE_KEY, &elevatorsFileName);
    if (elevatorsFileName == nullptr || *elevatorsFileName == '\0') {
        return;
    }

    Config elevatorsConfig;
    if (!configInit(&elevatorsConfig)) {
        return;
    }

    if (configRead(&elevatorsConfig, elevatorsFileName, false)) {
        char sectionKey[4];
        char key[32];
        for (int index = 0; index < ELEVATORS_MAX; index++) {
            snprintf(sectionKey, sizeof(sectionKey), "%d", index);

            if (index >= ELEVATOR_COUNT) {
                int levels = 0;
                configGetInt(&elevatorsConfig, sectionKey, "ButtonCount", &levels);
                gElevatorLevels[index] = std::clamp(levels, 2, ELEVATOR_LEVEL_MAX);
            }

            for (int level = 0; level < ELEVATOR_LEVEL_MAX; level++) {
                snprintf(key, sizeof(key), "ID%d", level + 1);
                configGetEnum<Map>(&elevatorsConfig, sectionKey, key, &(gElevatorDescriptions[index][level].map));

                snprintf(key, sizeof(key), "Elevation%d", level + 1);
                configGetInt(&elevatorsConfig, sectionKey, key, &(gElevatorDescriptions[index][level].elevation));

                snprintf(key, sizeof(key), "Tile%d", level + 1);
                configGetInt(&elevatorsConfig, sectionKey, key, &(gElevatorDescriptions[index][level].tile));
            }
        }

        // The "Image" remap: an elevator can borrow another's panel, and with it
        // that panel's BUTTON COUNT. Applied here for the level count (data) and
        // again in elevator.cc for the art.
        for (int index = 0; index < ELEVATORS_MAX; index++) {
            snprintf(sectionKey, sizeof(sectionKey), "%d", index);

            int type;
            if (configGetInt(&elevatorsConfig, sectionKey, "Image", &type)) {
                type = std::clamp(type, 0, ELEVATORS_MAX - 1);
                if (index != type) {
                    gElevatorLevels[index] = gElevatorLevels[type];
                }
            }
        }
    }

    configFree(&elevatorsConfig);
}

// The elevator doors the rider just stepped through: nearest matching scenery
// within 4 tiles, on the rider's own elevation. Vanilla's search, verbatim.
static Object* elevatorFindDoors(Object* rider)
{
    Object* doors = objectFindFirstAtElevation(rider->elevation);
    while (doors != nullptr) {
        int pid = doors->pid;
        if (ProtoId(pid).objectType() == OBJ_TYPE_SCENERY
            && (pid == 0x2000099 || pid == 0x20001A5 || pid == 0x20001D6)
            && tileDistanceBetween(doors->tile, rider->tile) <= 4) {
            break;
        }
        doors = objectFindNextAtElevation();
    }

    return doors;
}

static void elevatorCloseDoors(Object* doors)
{
    if (doors == nullptr) {
        debugPrint("\nWarning: Elevator: Couldn't find old elevator doors!");
        return;
    }

    objectSetFrame(doors, 0, nullptr);
    objectSetLocation(doors, doors->tile, doors->elevation, nullptr);
    doors->flags &= ~OBJECT_OPEN_DOOR;
    doors->data.scenery.door.openFlags &= ~0x01;
    _obj_rebuild_all_light();
}

// THE RIDE. Extracted from the SCRIPT_REQUEST_ELEVATOR drain (scripts.cc) so the
// single-player path and the dedicated server run the same code — the server has no
// picker screen, so it reaches this from its own wire verb instead of from the drain.
//
// ►► THE WHOLE PARTY RIDES, not just the rider. Co-op players are an SP-style party
// (the project's standing group-effects ruling), and there is a hard reason here as
// well as a design one: one gMap, one camera elevation, and every combat/AI roster
// filters on gElevation — so leaving one player on another floor puts the server in a
// state nothing else supports. Cross-map rides were already global (mapSetTransition
// moves the world); this makes the same-map case agree with them.
//
// The vanilla animated gauge is deliberately NOT reproduced: the ride is instant, on
// the owner's call ("ease on the animated stuff; just make it a map transition").
void elevatorRideApply(Object* rider, int map, int elevation, int tile, bool closeDoorsBeforeMapTransition)
{
    if (rider == nullptr) {
        return;
    }

    if (map != gMapHeader.index) {
        // Another map: close the doors behind, then let the ordinary transition
        // machinery carry everyone. Placement of the other players is mapLoad's
        // business (it re-plants player actors beside the host), not ours.
        if (closeDoorsBeforeMapTransition) elevatorCloseDoors(elevatorFindDoors(rider));

        MapTransition transition;
        memset(&transition, 0, sizeof(transition));
        transition.map = static_cast<Map>(map);
        transition.elevation = static_cast<MapElevation>(elevation);
        transition.tile = tile;
        transition.rotation = ROTATION_SE;
        mapSetTransitionForActor(&transition, rider);
        return;
    }

    // Same map. Find the doors BEFORE moving anyone — the search is relative to the
    // rider's current tile and elevation, both of which the placement changes.
    Object* doors = elevation != rider->elevation ? elevatorFindDoors(rider) : nullptr;

    reg_anim_clear(rider);
    objectSetRotation(rider, ROTATION_SE, nullptr);
    objectAttemptPlacement(rider, tile, static_cast<MapElevation>(elevation), 0);

    // Everyone else who is online and not the rider, onto free tiles beside it. A
    // radius of 1 lets objectAttemptPlacement itself spread them if the free-tile
    // search comes up empty; co-locating is better than stranding a player on a
    // floor the party has left (map.cc's own rule for reattach).
    for (int slot = 0; slot < playerActorCount(); slot++) {
        Object* actor = playerActorAt(slot);
        if (actor == nullptr || actor == rider || !playerActorOnline(slot)) {
            continue;
        }

        int near = playerActorFindFreeTileNear(tile, elevation);
        reg_anim_clear(actor);
        objectSetRotation(actor, ROTATION_SE, nullptr);
        objectAttemptPlacement(actor, near != -1 ? near : tile, static_cast<MapElevation>(elevation), 1);
    }

    if (doors != nullptr) {
        elevatorCloseDoors(doors);
    }
}

int elevatorEscapeLevel(int elevator, int map)
{
    const ElevatorDescription* exitLevels = gElevatorDescriptions[elevator];
    for (int index = 0; index < ELEVATOR_LEVEL_MAX; index++) {
        if (exitLevels[index].map == map && exitLevels[index].elevation == gElevation) {
            return index;
        }
    }
    return -1;
}

} // namespace fallout
