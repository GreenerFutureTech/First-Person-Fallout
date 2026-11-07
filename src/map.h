#ifndef MAP_H
#define MAP_H

#include "combat_defs.h"
#include "db.h"
#include "geometry.h"
#include "interpreter.h"
#include "map_defs.h"
#include "message.h"
#include "platform_compat.h"
#include "worldmap.h"

namespace fallout {

#define ORIGINAL_ISO_WINDOW_WIDTH 640
#define ORIGINAL_ISO_WINDOW_HEIGHT 380

// TODO: Probably not needed -> replace with array?
struct TileData {
    int tileFid[SQUARE_GRID_SIZE]; // contains two shortened 16 bit fids within (12 bit FrameId + 4 bit flags), lower half is floor tile fid and upper half is roof tile fid
};

typedef struct MapHeader {
    // map_ver
    int version;

    // map_name
    char name[16];

    // map_ent_tile
    int enteringTile;

    // map_ent_elev
    MapElevation enteringElevation;

    // map_ent_rot
    Rotation enteringRotation;

    // map_num_loc_vars
    int localVariablesCount;

    // 0map_script_idx
    int scriptIndex;

    // map_flags
    MapHeaderFlags flags;

    // map_darkness
    int darkness;

    // map_num_glob_vars
    int globalVariablesCount;

    // map_number
    Map index;

    // Time in game ticks when PC last visited this map.
    unsigned int lastVisitTime;
    int field_3C[44];
} MapHeader;

typedef struct MapTransition {
    Map map;
    MapElevation elevation;
    int tile;
    Rotation rotation;
} MapTransition;

typedef void IsoWindowRefreshProc(Rect* rect);

extern int gMapSid;
extern int* gMapLocalVars;
extern int* gMapGlobalVars;
extern int gMapLocalVarsLength;
extern int gMapGlobalVarsLength;
extern MapElevation gElevation;

extern MessageList gMapMessageList;
extern MapHeader gMapHeader;
extern TileData* _square[ELEVATION_COUNT];
extern int gIsoWindow;

int isoInit();
void isoReset();
void isoExit();
void mapInit();
void mapExit();
void mapVariablesFree();
void isoEnable();
bool isoDisable();
bool isoIsDisabled();
int mapSetElevation(MapElevation elevation);
int mapSetGlobalVar(int var, ProgramValue& value);
int mapGetGlobalVar(int var, ProgramValue& value);
int mapSetLocalVar(int var, ProgramValue& value);
int mapGetLocalVar(int var, ProgramValue& value);
int mapAllocLocalVars(int numNewVars);
void mapSetStart(int tile, MapElevation elevation, Rotation rotation);
char* mapGetName(Map map_num, MapElevation elev);
bool mapAreSameArea(Map map_num1, Map map_num2);
int _get_map_idx_same(Map map_num1, Map map_num2);
char* mapGetCityName(Map map_num);
char* mapDescriptionById(Map map_index);
Map mapGetCurrentMap();

// Monotonic count of map-load ATTEMPTS (bumped at mapLoad's ENTRY, the universal
// choke — NOT on success: a FAILED load still runs mapNewMap()/_obj_remove_all() and
// so still destroys the world a consumer was tracking; bumping regardless is what
// makes the rebaseline unconditional).
// THE map-change signal for anything that must rebaseline per loaded world.
//
// Use this, NOT mapGetCurrentMap(): the map INDEX is not a change signal, because
// re-entering the SAME map (leave to the worldmap and come back, or any same-index
// reload) tears down and rebuilds the entire object set while the index stays put.
// An index-equality check silently misses it — and the objects are freshly allocated
// with netId 0 (objectAllocate, object.cc:3444), so a consumer keyed on netId gets a
// world where every object collides on 0. Measured: matched=2 not_in_dump=2864.
// ⚠ mapNewMap() (map.cc) also tears the world down via _obj_remove_all() WITHOUT
// bumping this. Unreachable under the serve loop today (mapLoad is the only in-game
// path), but if that changes, it must bump too.
unsigned int mapGetLoadGeneration();
int mapScroll(int dx, int dy);
int mapSetEnteringLocation(MapElevation elevation, int tile, Rotation rotation);
void mapNewMap();
int mapLoadByName(char* fileName);
int mapLoadById(Map map_index);
const char* mapBuildPath(const char* name);
// Resolves a VFS-relative data path (e.g. "MAPS\\ARROYO.MAP") to a writable path, creating its directory.
// Save root is the validated mapper dev_path when set, otherwise the master patches path.
const char* mapBuildDataSavePath(const char* relativePath);
// Convenience wrapper around mapBuildDataSavePath for files under MAPS\, mirroring
// mapBuildPath semantics: name is the bare filename, e.g. "ARROYO.MAP".
const char* mapBuildSavePath(const char* name);
int mapLoadSaved(char* fileName);
int mapGetLoadedAreaId();
// Inner stream loader + join-blob serializer (STEP 4, CLIENT_JOIN_DESIGN.md §B).
int mapLoad(File* stream);
int mapSaveToStream(File* stream, int* mapBodyLenOut = nullptr);
// Null dangling combat back-pointers on freshly-loaded critters (whoHitMe when
// whoHitMeCid == -1). mapLoad runs this only for first-run maps; a saved-bit
// join blob must run it explicitly, or reading whoHitMe->id (state_dump) faults.
void _map_fix_critter_combat_data();
// STEP-4 viewer join: suppress mapLoad's map-enter/update procs (the viewer is a
// puppet, CLIENT_JOIN_DESIGN.md §E). Set true before loading a blob, false after.
void mapSetViewerLoad(bool viewerLoad);
int mapSetTransition(MapTransition* transition);
// Same transition latch with an explicit player initiator. Elevator requests are
// drained after their ServerActorScope has ended, so they must carry the rider here.
int mapSetTransitionForActor(MapTransition* transition, Object* actor);
// Non-zero only while destination map-enter procs are executing for a co-op
// transition. Bits are stable player slots and form the audience for story XP.
unsigned int mapTransitionXpAudienceMask();
void mapSetTransitionSuppressed(bool suppressed);
bool mapTransitionPending();
int mapHandleTransition();
int _map_save_in_game(bool isLeavingMap);
int _map_save(bool isInGame = false);

// Delete a file under the patches dir (discards a random encounter map's .SAV).
// Homed here, not in loadsave.cc: pure filesystem, and map.cc is its only caller,
// so the core no longer reaches a client-only symbol. See the definition.
int _MapDirEraseFile_(const char* a1, const char* a2);

} // namespace fallout

#endif /* MAP_H */
