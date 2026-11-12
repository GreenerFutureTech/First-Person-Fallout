#include "platform_compat.h"
#include "map_render.h"
#include "palette.h"
#include "pipboy.h"
#include "presenter.h"
#include "player_sheet.h"
#include "script_request_handler.h"
#include "server_loop.h"
#include "server_players.h"
#include "geometry.h"
#include "object_render.h"
#include "map.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "animation.h"
#include "art.h"
#include "automap.h"
#include "character_editor.h"
#include "color.h"
#include "combat.h"
#include "content_config.h"
#include "critter.h"
#include "cycle.h"
#include "debug.h"
#include "draw.h"
#include "elevator.h"
#include "game.h"
#include "game_mouse.h"
#include "game_movie.h"
#include "game_sound.h"
#include "input.h"
#include "interface.h"
#include "item.h"
#include "light.h"
#include "loadsave.h"
#include "map_defs.h"
#include "map_edge.h"
#include "memory.h"
#include "object.h"
#include "party_member.h"
#include "proto.h"
#include "proto_instance.h"
#include "queue.h"
#include "random.h"
#include "script_sound.h"
#include "scripts.h"
#include "settings.h"
#include "sfall_callbacks.h"
#include "svga.h"
#include "text_object.h"
#include "tile.h"
#include "tile_hires_stencil.h"
#include "window_manager.h"
#include "window_manager_private.h"
#include "worldmap.h"

namespace fallout {

static bool gMapViewerLoad = false;

static unsigned int gMapLoadGeneration = 0;

static bool gMapTransitionSuppressed = false;

static int gMapTransitionInitiatorSlot = -1;

static unsigned int gMapTransitionParticipantMask = 0;

static unsigned int gMapActiveTransitionXpAudienceMask = 0;

void mapSetViewerLoad(bool viewerLoad);

void mapVariablesFree();

unsigned int mapGetLoadGeneration();

void mapSetTransitionSuppressed(bool suppressed);

static int mapSetTransitionImpl(MapTransition* transition, Object* explicitActor);

int mapSetTransitionForActor(MapTransition* transition, Object* actor);

unsigned int mapTransitionXpAudienceMask();

bool mapTransitionPending();

int _MapDirEraseFile_(const char* a1, const char* a2);

int mapSaveToStream(File* stream, int* mapBodyLenOut);


const char* mapBuildPath(const char* name);

static int _map_age_dead_critters();
void _map_fix_critter_combat_data();
static int _map_save_file(File* stream);
int _map_save(bool isInGame);
static int replaceDeadCritter(Object* critter);




static int mapGlobalVariablesInit(int count);
static void mapGlobalVariablesFree();
static int mapGlobalVariablesLoad(File* stream);
static int mapLocalVariablesInit(int count);
static void mapLocalVariablesFree();
static int mapLocalVariablesLoad(File* stream);
static void _map_place_dude_and_mouse();

static void _square_reset();
static int _square_load(File* stream, MapHeaderFlags flags);
static int mapHeaderWrite(MapHeader* ptr, File* stream);
static int mapHeaderRead(MapHeader* ptr, File* stream);
static void mapLoadTimerStart();
static void mapLoadTimerFinish(const char* fileName, int rc);

// 0x50B058
static char byte_50B058[] = "";

// 0x50B30C aErrorF2
static char _aErrorF2[] = "ERROR! F2";

// 0x519540 map_scroll_refresh
IsoWindowRefreshProc* _map_scroll_refresh = isoWindowRefreshRectGame;

// 0x519544 map_data_elev_flags
static const MapHeaderFlags _map_data_elev_flags[ELEVATION_COUNT] = {
    MAP_HEADER_ELEVATION_0,
    MAP_HEADER_ELEVATION_1,
    MAP_HEADER_ELEVATION_2,
};

// 0x519550 map_last_scroll_time


// 0x519554 map_bk_enabled
static bool gIsoEnabled = false;

// 0x519558 mapEntranceElevation
static MapElevation gEnteringElevation = ELEVATION_FIRST;

// 0x51955C mapEntranceTileNum
static int gEnteringTile = -1;

// 0x519560 mapEntranceRotation
static Rotation gEnteringRotation = ROTATION_NE;

// 0x519564 map_script_id
int gMapSid = -1;

// local_vars
// 0x519568 map_local_vars
int* gMapLocalVars = nullptr;

// map_vars
// 0x51956C map_global_vars
int* gMapGlobalVars = nullptr;

// local_vars_num
// 0x519570 num_map_local_vars
int gMapLocalVarsLength = 0;

// map_vars_num
// 0x519574 num_map_global_vars
int gMapGlobalVarsLength = 0;

// Current elevation.
//
// 0x519578 map_elevation
MapElevation gElevation = ELEVATION_FIRST;

// 0x51957C errMapName
static char* _errMapName = byte_50B058;

// 0x519584 wmMapIdx
static Map _wmMapIdx = MAP_INVALID;

// 0x614868 square_data
static TileData _square_data[ELEVATION_COUNT];

// 0x631D28 map_state
static MapTransition gMapTransition;
static bool disableSpecialMapIds;

// 0x631D38 map_display_rect


// map.msg
//
// map_msg_file
// 0x631D48 map_msg_file
MessageList gMapMessageList;

// 0x631D50 display_buf


// 0x631D54 map_data
MapHeader gMapHeader;

// 0x631E40 square
TileData* _square[ELEVATION_COUNT];

// 0x631E4C display_win


// 0x631E50 scratchStr
static char _scratchStr[40];

static int mapLoadTimerDepth = 0;
static unsigned int mapLoadStartTime = 0;

// CE: Basically the same problem described in |gMapLocalPointers|, but this
// time Olympus folks use global map variables to store objects (looks like
// only `self_obj`).
static std::vector<void*> gMapGlobalPointers;

// CE: There is a bug in the user-space scripting where they want to store
// pointers to |Object| instances in local vars. This is obviously wrong as it's
// meaningless to save these pointers in file. As a workaround use second array
// to store these pointers.
static std::vector<void*> gMapLocalPointers;

// iso_init
// 0x481CA0


// 0x481ED4
void isoReset()
{
    // NOTE: Uninline.
    mapGlobalVariablesFree();

    // NOTE: Uninline.
    mapLocalVariablesFree();

    artReset();
    tileReset();
    objectsReset();
    colorCycleReset();
    interfaceReset();

    // NOTE: Uninline.
    mapSetEnteringLocation(ELEVATION_INVALID, -1, ROTATION_INVALID);
}

// 0x481F48


// 0x481FB4
void mapInit()
{
    configGetBool(&gContentConfig, CONTENT_CONFIG_MAPS_SECTION, "disable_special_map_ids", &disableSpecialMapIds, false);

    if (settings.system.executableIsMapper()) {
        _map_scroll_refresh = isoWindowRefreshRectMapper;
    }

    if (messageListInit(&gMapMessageList)) {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%smap.msg", gGameMsgPath);

        if (!messageListLoad(&gMapMessageList, path)) {
            debugPrint("\nError loading map_msg_file!");
        }
    } else {
        debugPrint("\nError initing map_msg_file!");
    }

    mapNewMap();
    tickersAdd(gameMouseRefresh);
    _gmouse_disable(0);
    windowShow(gIsoWindow);

    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_MAP, &gMapMessageList);
}

// 0x482084 map_enable_bk_processes
void mapExit()
{
    windowHide(gIsoWindow);
    presenter()->cursorSet(MOUSE_CURSOR_ARROW);
    tickersRemove(gameMouseRefresh);

    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_MAP, nullptr);
    if (!messageListFree(&gMapMessageList)) {
        debugPrint("\nError exiting map_msg_file!");
    }
}

// 0x4820C0
void isoEnable()
{
    if (!gIsoEnabled) {
        // Chrome (float-text / mouse / frame-animation tickers) via the presenter
        // seam; the critter freeze + gIsoEnabled are sim authority and stay core.
        // Order matches the legacy body exactly. See Presenter::worldEnable.
        presenter()->worldEnable();
        _scr_enable_critters();
        gIsoEnabled = true;
    }
}

// 0x482104 map_disable_bk_processes
bool isoDisable()
{
    if (!gIsoEnabled) {
        return false;
    }

    _scr_disable_critters();
    presenter()->worldDisable();

    gIsoEnabled = false;

    return true;
}

// 0x482148 map_bk_processes_are_disabled
bool isoIsDisabled()
{
    return gIsoEnabled == false;
}

// 0x482158 map_set_elevation
int mapSetElevation(MapElevation elevation)
{
    if (!elevationIsValid(elevation)) {
        return -1;
    }

    bool gameMouseWasVisible = false;
    if (gameMouseGetCursor() != MOUSE_CURSOR_WAIT_PLANET) {
        gameMouseWasVisible = gameMouseObjectsIsVisible();
        presenter()->mouseObjectsHide();
        presenter()->cursorSet(MOUSE_CURSOR_NONE);
    }

    if (elevation != gElevation) {
        wmMapMarkMapEntranceState(gMapHeader.index, elevation, 1);
    }

    gElevation = elevation;

    reg_anim_clear(gDude);
    _dude_stand(gDude, gDude->rotation, FrmId(gDude));
    _partyMemberSyncPosition();

    if (gMapSid != -1) {
        scriptsExecMapUpdateProc();
    }

    if (gameMouseWasVisible) {
        presenter()->mouseObjectsShow();
    }

    tile_hires_stencil_on_center_tile_or_elevation_change();

    return 0;
}

// 0x482220
int mapSetGlobalVar(int var, ProgramValue& value)
{
    if (var < 0 || var >= gMapGlobalVarsLength) {
        debugPrint("ERROR: attempt to reference map var out of range: %d", var);
        return -1;
    }

    if (value.opcode == VALUE_TYPE_PTR) {
        gMapGlobalVars[var] = 0;
        gMapGlobalPointers[var] = value.pointerValue;
    } else {
        gMapGlobalVars[var] = value.integerValue;
        gMapGlobalPointers[var] = nullptr;
    }

    return 0;
}

// 0x482250
int mapGetGlobalVar(int var, ProgramValue& value)
{
    if (var < 0 || var >= gMapGlobalVarsLength) {
        debugPrint("ERROR: attempt to reference map var out of range: %d", var);
        return -1;
    }

    if (gMapGlobalPointers[var] != nullptr) {
        value.opcode = VALUE_TYPE_PTR;
        value.pointerValue = gMapGlobalPointers[var];
    } else {
        value.opcode = VALUE_TYPE_INT;
        value.integerValue = gMapGlobalVars[var];
    }

    return 0;
}

// 0x482280
int mapSetLocalVar(int var, ProgramValue& value)
{
    if (var < 0 || var >= gMapLocalVarsLength) {
        debugPrint("ERROR: attempt to reference local var out of range: %d", var);
        return -1;
    }

    if (value.opcode == VALUE_TYPE_PTR) {
        gMapLocalVars[var] = 0;
        gMapLocalPointers[var] = value.pointerValue;
    } else {
        gMapLocalVars[var] = value.integerValue;
        gMapLocalPointers[var] = nullptr;
    }

    return 0;
}

// 0x4822B0
int mapGetLocalVar(int var, ProgramValue& value)
{
    if (var < 0 || var >= gMapLocalVarsLength) {
        debugPrint("ERROR: attempt to reference local var out of range: %d", var);
        return -1;
    }

    if (gMapLocalPointers[var] != nullptr) {
        value.opcode = VALUE_TYPE_PTR;
        value.pointerValue = gMapLocalPointers[var];
    } else {
        value.opcode = VALUE_TYPE_INT;
        value.integerValue = gMapLocalVars[var];
    }

    return 0;
}

// Make a room to store more local variables.
//
// 0x4822E0
int mapAllocLocalVars(const int numNewVars)
{
    int oldMapLocalVarsLength = gMapLocalVarsLength;
    gMapLocalVarsLength += numNewVars;

    int* vars = (int*)internal_realloc(gMapLocalVars, sizeof(*vars) * gMapLocalVarsLength);
    if (vars == nullptr) {
        debugPrint("\nError: Ran out of memory!");
        gMapLocalVarsLength = oldMapLocalVarsLength;
        return -1;
    }

    gMapLocalVars = vars;
    memset((unsigned char*)vars + sizeof(*vars) * oldMapLocalVarsLength, 0, sizeof(*vars) * numNewVars);

    gMapLocalPointers.resize(gMapLocalVarsLength);

    return oldMapLocalVarsLength;
}

// 0x48234C
void mapSetStart(int tile, MapElevation elevation, Rotation rotation)
{
    gMapHeader.enteringTile = tile;
    gMapHeader.enteringElevation = elevation;
    gMapHeader.enteringRotation = rotation;
}

// 0x4824CC
char* mapGetName(Map map, MapElevation elevation)
{
    if (!mapIsValid(map)) {
        return nullptr;
    }

    if (!elevationIsValid(elevation)) {
        return nullptr;
    }

    MessageListItem messageListItem;
    return getmsg(&gMapMessageList, &messageListItem, map * 3 + elevation + 200);
}

// TODO: Check, probably returns true if map1 and map2 represents the same city.
//
// 0x482528
bool mapAreSameArea(Map map1, Map map2)
{
    if (!mapIsValid(map1)) {
        return false;
    }

    if (!mapIsValid(map2)) {
        return false;
    }

    if (!wmMapIdxIsSaveable(map1)) {
        return false;
    }

    if (!wmMapIdxIsSaveable(map2)) {
        return false;
    }

    City city1;
    if (wmMatchAreaContainingMapIdx(map1, &city1) == -1) {
        return false;
    }

    City city2;
    if (wmMatchAreaContainingMapIdx(map2, &city2) == -1) {
        return false;
    }

    return city1 == city2;
}

// TODO: probably can be replaced with mapAreSameArea
// 0x4825CC
int _get_map_idx_same(Map map1, Map map2)
{
    City city1 = CITY_INVALID;
    if (wmMatchAreaContainingMapIdx(map1, &city1) == -1) {
        return -1;
    }

    City city2 = CITY_INVALID;
    if (wmMatchAreaContainingMapIdx(map2, &city2) == -1) {
        return -1;
    }

    if (city1 != city2) {
        return -1;
    }

    return city1;
}

// 0x48261C
char* mapGetCityName(Map map)
{
    City city;
    if (wmMatchAreaContainingMapIdx(map, &city) == -1) {
        return _aErrorF2;
    }

    MessageListItem messageListItem;
    char* name = getmsg(&gMapMessageList, &messageListItem, 1500 + city);
    return name;
}

// 0x48268C
char* mapDescriptionById(Map map)
{
    City city;
    if (wmMatchAreaContainingMapIdx(map, &city) == 0) {
        wmGetAreaIdxName(city, _scratchStr);
    } else {
        strcpy(_scratchStr, _errMapName);
    }

    return _scratchStr;
}

// 0x4826B8
Map mapGetCurrentMap()
{
    return gMapHeader.index;
}

// 0x4826C0


// 0x482900
const char* mapBuildPath(const char* name)
{
    // 0x631E78
    static char map_path[COMPAT_MAX_PATH];

    if (*name != '\\') {
        // NOTE: Uppercased from "maps".
        snprintf(map_path, sizeof(map_path), "MAPS\\%s", name);
        return map_path;
    }
    return name;
}

const char* mapBuildDataSavePath(const char* relativePath)
{
    static char path[COMPAT_MAX_PATH];

    // Save root: the validated mapper dev_path when set (mapper only), otherwise the master patches path.
    const std::string& devPath = settings.mapper.dev_path;
    const char* root = (settings.system.executableIsMapper() && !devPath.empty()) ? devPath.c_str() : settings.system.master_patches_path.c_str();
    // Join root + relativePath, tolerating a trailing separator on the root.
    size_t rootLen = strlen(root);
    bool rootHasSeparator = rootLen > 0 && (root[rootLen - 1] == '\\' || root[rootLen - 1] == '/');
    snprintf(path, sizeof(path), "%s%s%s", root, rootHasSeparator ? "" : "\\", relativePath);

    // Ensure the directory portion exists.
    char dir[COMPAT_MAX_PATH];
    snprintf(dir, sizeof(dir), "%s", path);
    char* separator = strrchr(dir, '\\');
    if (separator != nullptr) {
        *separator = '\0';
        compat_mkdir_recursive(dir);
    }

    return path;
}

const char* mapBuildSavePath(const char* name)
{
    char relativePath[COMPAT_MAX_PATH];
    snprintf(relativePath, sizeof(relativePath), "MAPS\\%s", name);
    return mapBuildDataSavePath(relativePath);
}

// 0x482924
int mapSetEnteringLocation(MapElevation elevation, int tile_num, Rotation rotation)
{
    gEnteringElevation = elevation;
    gEnteringTile = tile_num;
    gEnteringRotation = rotation;
    return 0;
}

// 0x482938
void mapNewMap()
{
    ++gMapLoadGeneration;
    mapEdgeFree();
    mapSetElevation(ELEVATION_FIRST);
    tileSetCenter(20100, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);
    memset(&gMapTransition, 0, sizeof(gMapTransition));
    gMapHeader.enteringElevation = ELEVATION_FIRST;
    gMapHeader.enteringRotation = ROTATION_NE;
    gMapHeader.localVariablesCount = 0;
    gMapHeader.version = 20;
    gMapHeader.name[0] = '\0';
    gMapHeader.enteringTile = 20100;
    _obj_remove_all();
    animationStop();

    // NOTE: Uninline.
    mapGlobalVariablesFree();

    // NOTE: Uninline.
    mapLocalVariablesFree();

    _square_reset();
    _map_place_dude_and_mouse();
    presenter()->worldInvalidate();
}

// 0x482A68 map_load
int mapLoadByName(char* fileName)
{
    mapLoadTimerStart();

    int rc;

    compat_strupr(fileName);

    rc = -1;

    if (!settings.system.executableIsMapper()) {
        char* extension = strstr(fileName, ".MAP");
        if (extension != nullptr) {
            strcpy(extension, ".SAV");

            const char* filePath = mapBuildPath(fileName);

            File* stream = fileOpen(filePath, "rb");

            strcpy(extension, ".MAP");

            if (stream != nullptr) {
                fileClose(stream);
                rc = mapLoadSaved(fileName);
                wmMapMusicStart();
            }
        }
    }

    if (rc == -1) {
        const char* filePath = mapBuildPath(fileName);
        File* stream = fileOpen(filePath, "rb");
        if (stream != nullptr) {
            rc = mapLoad(stream);
            fileClose(stream);
        }

        if (rc == 0) {
            strcpy(gMapHeader.name, fileName);
            gDude->data.critter.combat.whoHitMe = nullptr;
        }
    }

    mapLoadTimerFinish(fileName, rc);

    return rc;
}

static void mapLoadTimerStart()
{
    if (mapLoadTimerDepth == 0) {
        mapLoadStartTime = compat_timeGetTime();
    }

    mapLoadTimerDepth++;
}

static void mapLoadTimerFinish(const char* fileName, int rc)
{
    assert(mapLoadTimerDepth > 0);

    mapLoadTimerDepth--;
    if (mapLoadTimerDepth != 0) {
        return;
    }

    unsigned int elapsed = getTicksBetween(compat_timeGetTime(), mapLoadStartTime);
    debugPrint("\nMAP LOAD: %s rc=%d total=%ums",
        fileName != nullptr ? fileName : "<null>",
        rc,
        elapsed);
}

// 0x482B34
int mapLoadById(Map map)
{
    scriptSetFixedParam(gMapSid, map);

    char name[16];
    if (wmMapIdxToName(map, name, sizeof(name)) == -1) {
        return -1;
    }

    _wmMapIdx = map;

    int rc = mapLoadByName(name);

    wmMapMusicStart();

    return rc;
}

// 0x482B74 map_load_file
int mapLoad(File* stream)
{
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: begin\n");
    }

    presenterSetEmissionsSuppressed(true);
    gMapLoadGeneration++;
    int mapLoadSoundId = 0;
    if (!settings.system.executableIsMapper()) {
        _map_save_in_game(true);
        if (!serverDedicatedActive() && backgoundSoundIsPlaying()) {
            // Use the sfall sound path so the map-loading ambience does not depend
            // on the native background music loader.
            const char* mapLoadingSound = gameSoundGetMusicOverride("map_loading_sound", "wind2");
            char mapLoadingSoundPath[COMPAT_MAX_PATH];
            snprintf(mapLoadingSoundPath, sizeof(mapLoadingSoundPath), "sound\\music\\%s.ACM", mapLoadingSound);
            mapLoadSoundId = scriptSoundPlay(mapLoadingSoundPath, SCRIPT_SOUND_MODE_LOOP);
        }
    }
    isoDisable();
    _partyMemberPrepLoad();
    presenter()->scrollDisable();

    MouseCursorType savedMouseCursorId = gameMouseGetCursor();
    if (savedMouseCursorId >= MOUSE_CURSOR_SCROLL_NW && savedMouseCursorId <= MOUSE_CURSOR_SCROLL_W_INVALID) {
        savedMouseCursorId = MOUSE_CURSOR_ARROW; // reset if it was in view scrolling mode
    }
    presenter()->cursorSet(MOUSE_CURSOR_WAIT_PLANET);
    fileSetReadProgressHandler(gameMouseRefreshImmediately, 32768);
    tileDisable();

    int rc = 0;

    presenter()->worldClear();
    animationStop();
    scriptsDisable();

    gMapSid = -1;

    const char* error = nullptr;

    error = "Invalid file handle";
    if (stream == nullptr) {
        goto err;
    }

    error = "Error reading header";
    if (mapHeaderRead(&gMapHeader, stream) != 0) {
        goto err;
    }
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: header read version=%d flags=0x%X globalVars=%d localVars=%d\n",
            gMapHeader.version, (unsigned int)gMapHeader.flags,
            gMapHeader.globalVariablesCount, gMapHeader.localVariablesCount);
    }

    error = "Invalid map version";
    if (gMapHeader.version != 19 && gMapHeader.version != 20) {
        goto err;
    }

    if (gEnteringElevation == -1) {
        // NOTE: Uninline.
        mapSetEnteringLocation(gMapHeader.enteringElevation, gMapHeader.enteringTile, gMapHeader.enteringRotation);
    }

    _obj_remove_all();

    if (gMapHeader.globalVariablesCount < 0) {
        gMapHeader.globalVariablesCount = 0;
    }

    if (gMapHeader.localVariablesCount < 0) {
        gMapHeader.localVariablesCount = 0;
    }

    error = "Error allocating global vars";
    // NOTE: Uninline.
    if (mapGlobalVariablesInit(gMapHeader.globalVariablesCount) != 0) {
        goto err;
    }

    error = "Error loading global vars";
    // NOTE: Uninline.
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: loading global vars\n");
    }
    if (mapGlobalVariablesLoad(stream) != 0) {
        goto err;
    }
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: global vars loaded\n");
    }

    error = "Error allocating local vars";
    // NOTE: Uninline.
    if (mapLocalVariablesInit(gMapHeader.localVariablesCount) != 0) {
        goto err;
    }

    error = "Error loading local vars";
    // NOTE: Uninline.
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: loading local vars\n");
    }
    if (mapLocalVariablesLoad(stream) != 0) {
        goto err;
    }
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: local vars loaded\n");
    }

    if (gMapViewerLoad) {
        debugPrint("client mapLoad: loading squares\n");
    }
    if (_square_load(stream, gMapHeader.flags) != 0) {
        goto err;
    }
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: squares loaded\n");
    }

    error = "Error reading scripts";
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: loading scripts\n");
    }
    if (scriptLoadAll(stream) != 0) {
        goto err;
    }
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: scripts loaded\n");
    }

    error = "Error reading objects";
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: loading objects\n");
    }
    if (objectLoadAll(stream) != 0) {
        goto err;
    }
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: objects loaded\n");
    }

    if (!_isLoadingGame()) {
        // Fix whoHitMe union.  When loading a saved game, combatLoad is responsible for this fix
        _map_fix_critter_combat_data();
    }

    error = "Error setting map elevation";
    if (mapSetElevation(gEnteringElevation) != 0) {
        goto err;
    }

    if (settings.system.executableIsMapper() || settings.ui.edg_support) {
        mapEdgeLoad(gMapHeader.name);
    }

    error = "Error setting tile center";
    if (tileSetCenter(gEnteringTile, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS) != 0) {
        goto err;
    }

    lightSetAmbientIntensity(LIGHT_INTENSITY_MAX, false);
    objectSetLocation(gDude, gEnteringTile, gElevation, nullptr);
    objectSetRotation(gDude, gEnteringRotation, nullptr);
    gMapHeader.index = wmMapMatchNameToIdx(gMapHeader.name);

    if ((gMapHeader.flags & MAP_HEADER_SAVED) == MAP_HEADER_NONE) {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "maps\\%s", gMapHeader.name);

        char* extension = strstr(path, ".MAP");
        if (extension == nullptr) {
            extension = strstr(path, ".map");
        }

        if (extension != nullptr) {
            *extension = '\0';
        }

        strcat(path, ".GAM");
        globalVarsRead(path, "MAP_GLOBAL_VARS:", &gMapGlobalVarsLength, &gMapGlobalVars);
        if (gMapHeader.globalVariablesCount != gMapGlobalVarsLength) {
            assert(gMapHeader.globalVariablesCount == gMapGlobalPointers.size());
            gMapGlobalPointers.resize(gMapGlobalVarsLength);
        }
        gMapHeader.globalVariablesCount = gMapGlobalVarsLength;

        // CE: globalVarsRead() reallocates gMapGlobalVars and rewrites the
        // length from the .GAM file, but knows nothing about the parallel
        // gMapGlobalPointers vector — sized above from the .MAP header's count.
        // When the .GAM declares more vars than the header does (cowbomb.map:
        // header 0, .GAM 1) the vector stays short, and since mapGetGlobalVar/
        // mapSetGlobalVar bounds-check only against gMapGlobalVarsLength, the
        // first map-var access indexes it out of bounds and segfaults. Keep the
        // two in lockstep.
        gMapGlobalPointers.resize(gMapGlobalVarsLength);
    }

    // STEP-4 viewer join (CLIENT_JOIN_DESIGN.md §E): a joining viewer is a puppet
    // and must run NO scripts. Leaving scripts disabled here makes every map-enter/
    // update/start proc below a no-op (scriptExecProc gates on gScriptsEnabled,
    // scripts.cc:1264), so the client adopts the blob's post-server-enter state
    // verbatim instead of re-running a second, divergent map-enter (which mutates
    // lvars / object flags / script bindings on non-idempotent maps). The map
    // script is still REGISTERED (scriptAdd below is not gated), only its proc
    // execution is suppressed. Default path (client + server) unchanged.
    if (!gMapViewerLoad) {
        scriptsEnable();
    }

    if (gMapHeader.scriptIndex > 0) {
        error = "Error creating new map script";
        if (scriptAdd(&gMapSid, SCRIPT_TYPE_SYSTEM) == -1) {
            goto err;
        }

        Object* object;
        objectCreateWithFrmIdProtoId(&object, MiscFrameId::ScrollBlocker, ProtoId::Empty());
        object->flags |= (OBJECT_LIGHT_THRU | OBJECT_NO_SAVE | OBJECT_HIDDEN);
        objectSetLocation(object, 1, ELEVATION_FIRST, nullptr);
        object->sid = gMapSid;
        scriptSetFixedParam(gMapSid, (gMapHeader.flags & MAP_HEADER_SAVED) == MAP_HEADER_NONE);

        Script* script;
        scriptGetScript(gMapSid, &script);
        script->index = gMapHeader.scriptIndex - 1;
        script->flags |= SCRIPT_FLAG_NO_SAVE;
        object->id = scriptsNewObjectId();
        script->ownerId = object->id;
        script->owner = object;
        _scr_spatials_disable();
        scriptExecProc(gMapSid, SCRIPT_PROC_MAP_ENTER);
        _scr_spatials_enable();

        // ►► NOT ON A VIEWER. A viewer builds this map from the server's blob, and
        // every encounter critter in it is an ordinary streamed object with a netId —
        // so spawning our own here would double the ambush with a set of ghosts the
        // server has never heard of, unaddressable by any verb. The exposure is real
        // rather than theoretical: encounterMapId is part of the SAVED worldmap block
        // (worldmap.cc wmWorldMap_save), and the baseline is emitted from inside this
        // same mapLoad, one step below — so a blob taken on arrival carries a live
        // staged encounter to every client that applies it.
        //
        // Same reasoning as the map-enter script suppression above: the server already
        // ran this, and its result is in the blob we are adopting.
        if (!gMapViewerLoad) {
            error = "Error Setting up random encounter";
            if (wmSetupRandomEncounter() == -1) {
                goto err;
            }
        }
    }

    error = nullptr;

err:

    if (error != nullptr) {
        if (gMapViewerLoad) {
            debugPrint("client mapLoad: failed: %s\n", error);
        }
        char message[100]; // TODO: Size is probably wrong.
        snprintf(message, sizeof(message), "%s while loading map.", error);
        debugPrint(message);
        mapNewMap();
        rc = -1;
    } else {
        if (gMapViewerLoad) {
            debugPrint("client mapLoad: map data loaded; preloading art cache\n");
        }
        _obj_preload_art_cache(gMapHeader.flags);

        // MP_PROTOCOL.md §2/§5: the active tactical map is now fully loaded — the
        // universal choke every in-game map change funnels through (exit grids and
        // elevators via mapHandleTransition, worldmap arrivals/encounters via
        // worldmap_ui.cc mapLoadById). No-op under the null/client presenter, so
        // byte-identical. gMapHeader.index / gElevation are the freshly-loaded
        // values. NOTE: new-game (main.cc) and save-restore (mapLoadSaved) also
        // reach here — that is snapshot territory; the NetworkPresenter adds the
        // §5 load-window guard in P5-C. See Presenter::mapTransition (presenter.h).
        presenter()->mapTransition(gMapHeader.index, gElevation);
    }

    sfallOnBeforeMapLoad();

    if (gMapViewerLoad) {
        debugPrint("client mapLoad: post-load setup\n");
    }
    _partyMemberRecoverLoad();
    presenter()->hudBarShow();
    _proto_dude_update_gender();
    _map_place_dude_and_mouse();
    fileSetReadProgressHandler(nullptr, 0);
    isoEnable();
    presenter()->scrollDisable();
    presenter()->cursorSet(MOUSE_CURSOR_WAIT_PLANET);

    if (scriptsExecStartProc() == -1) {
        debugPrint("\n   Error: scr_load_all_scripts failed!");
    }

    if (gMapViewerLoad) {
        debugPrint("client mapLoad: map start proc complete\n");
    }
    scriptsExecMapEnterProc();
    scriptsExecMapUpdateProc();
    tileEnable();

    if (gMapTransition.map > 0) {
        if (gMapTransition.rotation >= 0) {
            objectSetRotation(gDude, gMapTransition.rotation, nullptr);
        }
    } else {
        presenter()->worldInvalidate();
    }

    gameTimeScheduleUpdateEvent();

    if (_gsound_sfx_q_start() == -1) {
        rc = -1;
    }

    wmMapMarkVisited(gMapHeader.index);
    wmMapMarkMapEntranceState(gMapHeader.index, gElevation, 1);

    if (wmCheckGameAreaEvents() != 0) {
        rc = -1;
    }

    fileSetReadProgressHandler(nullptr, 0);

    if (gameUiIsDisabled() == 0) {
        presenter()->scrollEnable();
    }

    presenter()->cursorSet(savedMouseCursorId);

    // NOTE: Uninline.
    mapSetEnteringLocation(ELEVATION_INVALID, -1, ROTATION_INVALID);

    tile_hires_stencil_on_map_load();

    presenter()->movieFadeOut();

    gMapHeader.version = 20;

    if (mapLoadSoundId != 0) {
        scriptSoundStop(mapLoadSoundId);
        mapLoadSoundId = 0;
    }

    presenterSetEmissionsSuppressed(false);
    if (gMapViewerLoad) {
        debugPrint("client mapLoad: complete rc=%d map=%d elevation=%d\n", rc, gMapHeader.index, gElevation);
    }
    return rc;
}

// 0x483188 map_load_in_game
int mapLoadSaved(char* fileName)
{
    debugPrint("\nMAP: Loading SAVED map.");

    char mapName[16]; // TODO: Size is probably wrong.
    _strmfe(mapName, fileName, "SAV");

    int rc = mapLoadByName(mapName);

    if (gameTimeGetTime() >= gMapHeader.lastVisitTime) {
        if (((gameTimeGetTime() - gMapHeader.lastVisitTime) / GAME_TIME_TICKS_PER_HOUR) >= 24) {
            objectUnjamAll();
        }

        // NOTE: The original fails the load here, and `mapLoadByName` loads
        // the .MAP instead: the map as new. The saved map loads, the bodies
        // the aging couldn't replace stay for the next visit.
        if (_map_age_dead_critters() == -1) {
            debugPrint("\nError: Critter aging failed on map load!");
        }
    }

    if (!wmMapIsSaveable()) {
        debugPrint("\nDestroying RANDOM encounter map.");

        char mapName[16];
        strcpy(mapName, gMapHeader.name);

        _strmfe(gMapHeader.name, mapName, "SAV");

        _MapDirEraseFile_("MAPS\\", gMapHeader.name);

        strcpy(gMapHeader.name, mapName);
    }

    return rc;
}

// 0x48328C
static int _map_age_dead_critters()
{
    // SFALL: Fix for critters not being healed over time when entering a map
    // with 'dead_bodies_age=No' set in maps.txt: the setting keeps the dead
    // bodies, the living critters still heal (see below).
    bool deadBodiesAge = wmMapDeadBodiesAge();

    int hoursSinceLastVisit = (gameTimeGetTime() - gMapHeader.lastVisitTime) / GAME_TIME_TICKS_PER_HOUR;
    if (hoursSinceLastVisit == 0) {
        return 0;
    }

    Object* obj = objectFindFirst();
    while (obj != nullptr) {
        const ProtoId protoId = obj;
        if (protoId.objectType() == OBJ_TYPE_CRITTER
            && obj != gDude
            && !objectIsPartyMember(obj)
            && !critterIsDead(obj)) {
            obj->data.critter.combat.maneuver &= ~CRITTER_MANUEVER_FLEEING;
            if (critterGetKillType(obj) != KILL_TYPE_ROBOT && !critterFlagCheck(protoId, CRITTER_NO_HEAL)) {
                critterHealByHours(obj, hoursSinceLastVisit);
            }
        }
        obj = objectFindNext();
    }

    if (!deadBodiesAge) {
        return 0;
    }

    // NOTE: The original (0x483334) replaces dead bodies by blood stains
    // after 6 days and also removes the blood stains after 14 days.
    int agingType;
    if (hoursSinceLastVisit > 14 * 24) {
        agingType = 2;
    } else if (hoursSinceLastVisit > 6 * 24) {
        agingType = 1;
    } else {
        return 0;
    }

    int capacity = 100;
    int count = 0;
    Object** objects = (Object**)internal_malloc(sizeof(*objects) * capacity);

    obj = objectFindFirst();
    while (obj != nullptr) {
        const ProtoId protoId = obj;
        if (protoId.objectType() == OBJ_TYPE_CRITTER) {
            if (obj != gDude && critterIsDead(obj)) {
                if (critterGetKillType(obj) != KILL_TYPE_ROBOT && !critterFlagCheck(protoId, CRITTER_NO_AGE)) {
                    objects[count++] = obj;

                    if (count >= capacity) {
                        capacity *= 2;
                        objects = (Object**)internal_realloc(objects, sizeof(*objects) * capacity);
                        if (objects == nullptr) {
                            debugPrint("\nError: Out of Memory!");
                            return -1;
                        }
                    }
                }
            }
        } else if (agingType == 2 && protoId.objectType() == OBJ_TYPE_MISC && FrmId(obj) == MiscFrameId::BloodPool) {
            objects[count++] = obj;
            if (count >= capacity) {
                capacity *= 2;
                objects = (Object**)internal_realloc(objects, sizeof(*objects) * capacity);
                if (objects == nullptr) {
                    debugPrint("\nError: Out of Memory!");
                    return -1;
                }
            }
        }
        obj = objectFindNext();
    }

    int rc = 0;
    for (int index = 0; index < count; index++) {
        Object* obj = objects[index];
        const ProtoId protoId = obj;
        if (protoId.objectType() == OBJ_TYPE_CRITTER) {
            // replace the dead critter bodies by the blood pool stain
            if (replaceDeadCritter(obj) == -1) {
                debugPrint("\n%s: Could not replace dead body by the blood stain for the critter %d with pid %d.", __func__, obj->id, obj->pid);
                rc = -1;
                break;
            }

            // drop the critter owned items on top of the blood stain only when successfully replaced
            if (!critterFlagCheck(protoId, CRITTER_NO_DROP)) {
                itemDropAll(obj, obj->tile);
            }
        }

        reg_anim_clear(obj);
        objectDestroy(obj, nullptr);
    }

    internal_free(objects);

    return rc;
}

static int replaceDeadCritter(Object* critter)
{
    if (ProtoId(critter).objectType() != OBJ_TYPE_CRITTER) {
        return -1;
    }

    Object* blood;
    if (objectCreateWithProtoId(&blood, MiscProtoTypeId::Blood) == -1) {
        return -1;
    }

    if (objectSetLocation(blood, critter->tile, critter->elevation, nullptr) == -1) {
        return -1;
    }

    int frame = randomBetween(0, 3);
    if ((critter->flags & OBJECT_MULTIHEX) != OBJECT_NONE) {
        frame += 6;
    } else {
        KillType killType = critterGetKillType(critter);
        if (killType != KILL_TYPE_RAT && killType != KILL_TYPE_MANTIS) {
            frame += 3;
        }
    }

    // NOTE: The original (0x483545) ignores obj_set_frame's result. A multihex
    // body's frame (6 to 9) can be past the stain art's last one (8), then the
    // stain keeps its first frame. Failing here failed the critter aging, and
    // with it the saved map's load: the map was loaded as new.
    objectSetFrame(blood, frame, nullptr);

    return 0;
}

// 0x48358C
int mapGetLoadedAreaId()
{
    City city = CITY_INVALID;
    if (wmMatchAreaContainingMapIdx(gMapHeader.index, &city) == -1) {
        city = CITY_INVALID;
    }
    return city;
}

// 0x4835B4
int mapSetTransition(MapTransition* transition)
{
    return mapSetTransitionImpl(transition, nullptr);
}

// 0x4835F8
int mapHandleTransition()
{
    if (gMapTransition.map == 0) {
        return 0;
    }

    presenter()->mouseObjectsHide();

    presenter()->cursorSet(MOUSE_CURSOR_NONE);

    if (gMapTransition.map == MAP_INVALID) {
        if (!isInCombat()) {
            animationStop();
            // SFALL: Remove text floaters when moving to the world map
            textObjectsReset();
            scriptRequestHandler()->townMap(); // nb this is a world map transition
            memset(&gMapTransition, 0, sizeof(gMapTransition));
        }
    } else if (gMapTransition.map == MAP_TRANSITION) {
        if (!isInCombat()) {
            animationStop();
            // SFALL: Remove text floaters when moving to the world map
            textObjectsReset();
            scriptRequestHandler()->worldMap();
            memset(&gMapTransition, 0, sizeof(gMapTransition));
        }
    } else {
        if (!isInCombat()) {
            if (gMapTransition.map != gMapHeader.index || gElevation == gMapTransition.elevation) {
                // SFALL: Remove text floaters after moving to another map.
                textObjectsReset();

                // Destination map-enter is part of the group transition, not a
                // scope-less passive heartbeat. Give its ordinary `dude_obj` reads
                // the actual initiator, and expose the captured participant audience
                // only for give_exp_points while these procs execute.
                Object* initiator = gMapTransitionInitiatorSlot >= 0
                    ? playerActorAt(gMapTransitionInitiatorSlot)
                    : nullptr;
                ServerActorScope transitionScope(initiator);
                gMapActiveTransitionXpAudienceMask = gMapTransitionParticipantMask;
                mapLoadById(static_cast<Map>(gMapTransition.map));
                gMapActiveTransitionXpAudienceMask = 0;
            }

            if (gMapTransition.tile != -1 && gMapTransition.tile != 0
                && (disableSpecialMapIds
                    || (gMapHeader.index != MAP_MODOC_BEDNBREAKFAST && gMapHeader.index != MAP_THE_SQUAT_A))
                && elevationIsValid(gMapTransition.elevation)) {
                objectSetLocation(gDude, gMapTransition.tile, gMapTransition.elevation, nullptr);
                mapSetElevation(gMapTransition.elevation);
                objectSetRotation(gDude, gMapTransition.rotation, nullptr);

                // Everyone travels with the host (MP_PROPOSAL Ch 14.2). The host
                // is placed on the entry tile above; extras ring it. With an empty
                // registry the loop body never runs, so this is inert everywhere
                // but a co-op server.
                for (int slot = 1; slot < playerActorCount(); slot++) {
                    Object* actor = playerActorAt(slot);
                    // An offline body travels with the group ONLY as data (it is
                    // off the object list; objectSetLocation would fail closed
                    // anyway) — placing it would resurrect a player who left.
                    if (actor == nullptr || !playerActorOnline(slot)) {
                        continue;
                    }
                    int tile = playerActorFindFreeTileNear(gDude->tile, gMapTransition.elevation);
                    if (tile == -1) {
                        // Nowhere free within the ring: co-locate rather than
                        // strand the actor off-map. Two bodies on one tile is
                        // ugly; an actor at a stale tile on a new map is a bug.
                        tile = gDude->tile;
                    }
                    objectSetLocation(actor, tile, gMapTransition.elevation, nullptr);
                    objectSetRotation(actor, gMapTransition.rotation, nullptr);
                }
            }

            if (tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW) == -1) {
                debugPrint("\nError: map: attempt to center out-of-bounds!");
            }

            memset(&gMapTransition, 0, sizeof(gMapTransition));

            City city = CITY_INVALID;
            wmMatchAreaContainingMapIdx(gMapHeader.index, &city);
            if (wmTeleportToArea(city) == -1) {
                debugPrint("\nError: couldn't make jump on worldmap for map jump!");
            }
        }
    }

    return 0;
}

// 0x483784
void _map_fix_critter_combat_data()
{
    for (Object* object = objectFindFirst(); object != nullptr; object = objectFindNext()) {
        if (ProtoId(object).objectType() != OBJ_TYPE_CRITTER) {
            continue;
        }

        // Null the runtime-only whoHitMe POINTER on load, regardless of the persistent
        // whoHitMeCid. Out of combat whoHitMe is never re-resolved, so a critter loaded
        // with cid != -1 otherwise keeps a bad pointer — on 64-bit a 4-byte -1 sitting in
        // the 8-byte field — which _damage_object derefs (combat.cc:5425, guarded only
        // against nullptr) when an OUT-OF-COMBAT explosion (C4 / la bombia) kills it → a
        // ~50% server SIGSEGV. Combat re-resolves whoHitMe from whoHitMeCid via _find_cid
        // at _combat_begin (combat.cc:2063-2091), so nulling here is safe; the persistent
        // whoHitMeCid is untouched. (Vanilla nulled only when cid == -1 — a latent bug the
        // 64-bit pointer width makes reliably fatal.)
        object->data.critter.combat.whoHitMe = nullptr;
    }
}

// map_save
// 0x483850
int _map_save(bool isInGame)
{
    int rc = -1;
    if (gMapHeader.name[0] != '\0') {
        const char* mapFilePath = mapBuildSavePath(gMapHeader.name);
        File* stream = fileOpen(mapFilePath, "wb");
        if (stream != nullptr) {
            rc = _map_save_file(stream);
            fileClose(stream);
        } else {
            debugPrint("Unable to open %s to write!", gMapHeader.name);
        }

        if (rc == 0) {
            debugPrint("%s saved.", gMapHeader.name);

            if (!isInGame) {
                // Write the edge (.EDG) alongside the map.
                mapEdgeSave(gMapHeader.name);
            }
        }
    } else {
        debugPrint("\nError: map_save: map header corrupt!");
    }

    return rc;
}

// 0x483980
static int _map_save_file(File* stream)
{
    if (stream == nullptr) {
        return -1;
    }

    scriptsDisable();

    for (MapElevation elevation = ELEVATION_FIRST; elevation < ELEVATION_COUNT; elevation++) {
        int tile;
        for (tile = 0; tile < SQUARE_GRID_SIZE; tile++) {
            TileFrameId frameId;

            frameId = FloorTileFrmId(_square[elevation]->tileFid[tile]).frameId<TileFrameId>();
            if (frameId != TileFrameId::Grid) {
                break;
            }

            frameId = RoofTileFrmId(_square[elevation]->tileFid[tile]).frameId<TileFrameId>();
            if (frameId != TileFrameId::Grid) {
                break;
            }
        }

        if (tile == SQUARE_GRID_SIZE) {
            Object* object = objectFindFirstAtElevation(elevation);
            if (object != nullptr) {
                // TODO: Implementation is slightly different, check in debugger.
                while (object != nullptr && (object->flags & OBJECT_NO_SAVE)) {
                    object = objectFindNextAtElevation();
                }

                if (object != nullptr) {
                    gMapHeader.flags &= ~_map_data_elev_flags[elevation];
                } else {
                    gMapHeader.flags |= _map_data_elev_flags[elevation];
                }
            } else {
                gMapHeader.flags |= _map_data_elev_flags[elevation];
            }
        } else {
            gMapHeader.flags &= ~_map_data_elev_flags[elevation];
        }
    }

    gMapHeader.localVariablesCount = gMapLocalVarsLength;
    gMapHeader.globalVariablesCount = gMapGlobalVarsLength;
    gMapHeader.darkness = 1;

    mapHeaderWrite(&gMapHeader, stream);

    if (gMapHeader.globalVariablesCount != 0) {
        fileWriteInt32List(stream, gMapGlobalVars, gMapHeader.globalVariablesCount);
    }

    if (gMapHeader.localVariablesCount != 0) {
        fileWriteInt32List(stream, gMapLocalVars, gMapHeader.localVariablesCount);
    }

    for (MapElevation elevation = ELEVATION_FIRST; elevation < ELEVATION_COUNT; elevation++) {
        if ((gMapHeader.flags & _map_data_elev_flags[elevation]) == MAP_HEADER_NONE) {
            _db_fwriteLongCount(stream, _square[elevation]->tileFid, SQUARE_GRID_SIZE);
        }
    }

    char err[80];

    if (scriptSaveAll(stream) == -1) {
        snprintf(err, sizeof(err), "Error saving scripts in %s", gMapHeader.name);
        presenter()->errorBox(err);
    }

    if (objectSaveAll(stream) == -1) {
        snprintf(err, sizeof(err), "Error saving objects in %s", gMapHeader.name);
        presenter()->errorBox(err);
    }

    scriptsEnable();

    return 0;
}

// 0x483C98
int _map_save_in_game(bool isLeavingMap)
{
    if (gMapHeader.name[0] == '\0') {
        return 0;
    }

    animationStop();
    _partyMemberSaveProtos();

    if (isLeavingMap) {
        _queue_leaving_map();
        _partyMemberPrepLoad();
        _partyMemberPrepItemSaveAll();
        scriptsExecMapExitProc();

        if (gMapSid != -1) {
            Script* script;
            scriptGetScript(gMapSid, &script);
        }

        gameTimeScheduleUpdateEvent();
        _obj_reset_roof();
    }

    gMapHeader.flags |= MAP_HEADER_SAVED;
    gMapHeader.lastVisitTime = gameTimeGetTime();

    char name[16];

    if (isLeavingMap && !wmMapIsSaveable()) {
        debugPrint("\nNot saving RANDOM encounter map.");

        strcpy(name, gMapHeader.name);
        _strmfe(gMapHeader.name, name, "SAV");
        _MapDirEraseFile_("MAPS\\", gMapHeader.name);
        strcpy(gMapHeader.name, name);
    } else {
        debugPrint("\n Saving \".SAV\" map.");

        strcpy(name, gMapHeader.name);
        _strmfe(gMapHeader.name, name, "SAV");
        if (_map_save(true) == -1) {
            return -1;
        }

        strcpy(gMapHeader.name, name);

        automapSaveCurrent();
    }

    if (isLeavingMap) {
        gMapHeader.name[0] = '\0';
        _obj_remove_all();
        _proto_remove_all();
        _square_reset();
        gameTimeScheduleUpdateEvent();
    }

    return 0;
}

// 0x483E28
void mapMakeMapsDirectory()
{
    char path[COMPAT_MAX_PATH];

    strcpy(path, settings.system.master_patches_path.c_str());
    compat_mkdir(path);

    strcat(path, "\\MAPS");
    compat_mkdir(path);
}

// 0x483ED0


// 0x483EE4 map_scroll_refresh_game


// 0x483F44 map_scroll_refresh_mapper


// NOTE: Inlined.
//
// 0x483FE4
static int mapGlobalVariablesInit(int count)
{
    mapGlobalVariablesFree();

    if (count != 0) {
        gMapGlobalVars = (int*)internal_malloc(sizeof(*gMapGlobalVars) * count);
        if (gMapGlobalVars == nullptr) {
            return -1;
        }

        gMapGlobalPointers.resize(count);
    }

    gMapGlobalVarsLength = count;

    return 0;
}

// 0x484038
static void mapGlobalVariablesFree()
{
    if (gMapGlobalVars != nullptr) {
        internal_free(gMapGlobalVars);
        gMapGlobalVars = nullptr;
        gMapGlobalVarsLength = 0;
    }

    gMapGlobalPointers.clear();
}

// NOTE: Inlined.
//
// 0x48405C
static int mapGlobalVariablesLoad(File* stream)
{
    if (fileReadInt32List(stream, gMapGlobalVars, gMapGlobalVarsLength) != 0) {
        return -1;
    }

    return 0;
}

// NOTE: Inlined.
//
// 0x484080
static int mapLocalVariablesInit(int count)
{
    mapLocalVariablesFree();

    if (count != 0) {
        gMapLocalVars = (int*)internal_malloc(sizeof(*gMapLocalVars) * count);
        if (gMapLocalVars == nullptr) {
            return -1;
        }

        gMapLocalPointers.resize(count);
    }

    gMapLocalVarsLength = count;

    return 0;
}

// 0x4840D4
static void mapLocalVariablesFree()
{
    if (gMapLocalVars != nullptr) {
        internal_free(gMapLocalVars);
        gMapLocalVars = nullptr;
        gMapLocalVarsLength = 0;
    }

    gMapLocalPointers.clear();
}

// NOTE: Inlined.
//
// 0x4840F8
static int mapLocalVariablesLoad(File* stream)
{
    if (fileReadInt32List(stream, gMapLocalVars, gMapLocalVarsLength) != 0) {
        return -1;
    }

    return 0;
}

// 0x48411C
static void _map_place_dude_and_mouse()
{
    _obj_clear_seen();

    if (gDude != nullptr) {
        if (FrmId(gDude).animationType() != ANIM_STAND) {
            objectSetFrame(gDude, 0, nullptr);
            gDude->fid = FrmId(gDude, ANIM_STAND, gDude->rotation + 1).fid();
        }

        if (gDude->tile == -1) {
            objectSetLocation(gDude, gCenterTile, gElevation, nullptr);
            objectSetRotation(gDude, gMapHeader.enteringRotation, nullptr);
        }

        objectSetLight(gDude, 4, 0x10000, nullptr);
        gDude->flags |= OBJECT_NO_SAVE;

        _dude_stand(gDude, gDude->rotation, FrmId(gDude));
        _partyMemberSyncPosition();
    }

    // Same treatment for every other player actor (MP_PROPOSAL Ch 14.2): the
    // STAND fid fix and the NO_SAVE re-assert are per-ACTOR properties, not
    // per-gDude ones. The flags survive the map load (extras are NO_REMOVE
    // server-side), so re-asserting is belt-and-braces — but stating the
    // invariant here is what keeps it from silently applying to slot 0 only.
    //
    // ⚠ Placement here is UNCONDITIONAL, unlike the dude's `tile == -1` case.
    // Player actors are NO_REMOVE, so they SURVIVE the teardown still holding a
    // tile index from the map we just left — a number that means something else
    // entirely (or nothing) on the new one. There is no "did it get placed"
    // signal to test, because the stale tile looks perfectly valid. So every load
    // re-plants them beside the host. The transition path then moves the whole
    // group again if it has a specific entry tile; landing twice is harmless,
    // landing never is a body standing inside a wall on the wrong map.
    // Issue #10: gDude is not always slot 0 here — a non-host transition
    // initiator is bound via ServerActorScope across mapLoadById. Walk EVERY
    // slot and skip whoever gDude is, or the host is never re-planted (MODINN
    // skips the transition-path placement, stranding it on the wrong elevation).
    for (int slot = 0; slot < playerActorCount(); slot++) {
        Object* actor = playerActorAt(slot);
        if (actor == nullptr || gDude == nullptr || actor == gDude) {
            continue;
        }
        // Same offline rule as the transition placement above: a despawned body
        // stays off-map through a load until its player logs back in.
        if (!playerActorOnline(slot)) {
            continue;
        }

        if (FrmId(actor).animationType() != ANIM_STAND) {
            objectSetFrame(actor, 0, nullptr);
            actor->fid = FrmId(actor, ANIM_STAND).fid();
        }

        int tile = playerActorFindFreeTileNear(gDude->tile, gDude->elevation);
        objectSetLocation(actor, tile != -1 ? tile : gDude->tile, gDude->elevation, nullptr);
        objectSetRotation(actor, gMapHeader.enteringRotation, nullptr);

        objectSetLight(actor, 4, 0x10000, nullptr);
        actor->flags |= OBJECT_NO_SAVE;
        _dude_stand(actor, actor->rotation, FrmId(actor));
    }

    presenter()->mouseResetBouncingCursor();
    presenter()->mouseObjectsShow();
}

// NOTE: Inlined.
//
// 0x4841F0
void square_init()
{
    for (MapElevation elevation = ELEVATION_FIRST; elevation < ELEVATION_COUNT; elevation++) {
        _square[elevation] = &(_square_data[elevation]);
    }
}

// 0x484210
static void _square_reset()
{
    for (MapElevation elevation = ELEVATION_FIRST; elevation < ELEVATION_COUNT; elevation++) {
        int* p = _square[elevation]->tileFid;
        for (int y = 0; y < SQUARE_GRID_HEIGHT; y++) {
            for (int x = 0; x < SQUARE_GRID_WIDTH; x++) {
                int fid = *p;
                const FloorTileFrmId originalFloorTileFrmId = FloorTileFrmId(fid);
                const RoofTileFrmId originalRoofTileFrmId = RoofTileFrmId(fid);
                const TileFrmId updatedTileFrmId = TileFrmId(
                    FloorTileFrmId(
                        TileFrameId::Grid,
                        originalFloorTileFrmId.flags()),
                    RoofTileFrmId(
                        TileFrameId::Grid,
                        originalRoofTileFrmId.flags()));

                *p = updatedTileFrmId.fid();

                p++;
            }
        }
    }
}

// 0x48431C
static int _square_load(File* stream, MapHeaderFlags flags)
{
    _square_reset();

    for (MapElevation elevation = ELEVATION_FIRST; elevation < ELEVATION_COUNT; elevation++) {
        if ((flags & _map_data_elev_flags[elevation]) == MAP_HEADER_NONE) {
            int* tileFids = _square[elevation]->tileFid;
            if (_db_freadIntCount(stream, tileFids, SQUARE_GRID_SIZE) != 0) {
                return -1;
            }

            for (int tile = 0; tile < SQUARE_GRID_SIZE; tile++) {
                const RoofTileFrmId roofTileFrmId = RoofTileFrmId(tileFids[tile]);

                TileFlags roofTileFlags = roofTileFrmId.flags() & ~TileFlags::TemporarilyHidden;

                TileFrameId roofTileArtId = roofTileFrmId.frameId<TileFrameId>();
                if (roofTileArtId == TileFrameId::Invalid) {
                    roofTileArtId = TileFrameId::Last;
                }

                const FloorTileFrmId floorTileFrmId = FloorTileFrmId(tileFids[tile]);
                const TileFrmId updatedTileFrmId = TileFrmId(floorTileFrmId, RoofTileFrmId(roofTileArtId, roofTileFlags));
                tileFids[tile] = updatedTileFrmId.fid();
            }
        }
    }

    return 0;
}

// 0x4843B8
static int mapHeaderWrite(MapHeader* ptr, File* stream)
{
    if (fileWriteInt32(stream, ptr->version) == -1) return -1;
    if (fileWriteFixedLengthString(stream, ptr->name, 16) == -1) return -1;
    if (fileWriteInt32(stream, ptr->enteringTile) == -1) return -1;
    if (fileWriteInt32Enum<MapElevation>(stream, ptr->enteringElevation) == -1) return -1;
    if (fileWriteInt32Enum<Rotation>(stream, ptr->enteringRotation) == -1) return -1;
    if (fileWriteInt32(stream, ptr->localVariablesCount) == -1) return -1;
    if (fileWriteInt32(stream, ptr->scriptIndex) == -1) return -1;
    if (fileWriteInt32Enum<MapHeaderFlags>(stream, ptr->flags) == -1) return -1;
    if (fileWriteInt32(stream, ptr->darkness) == -1) return -1;
    if (fileWriteInt32(stream, ptr->globalVariablesCount) == -1) return -1;
    if (fileWriteInt32Enum<Map>(stream, ptr->index) == -1) return -1;
    if (fileWriteUInt32(stream, ptr->lastVisitTime) == -1) return -1;
    if (fileWriteInt32List(stream, ptr->field_3C, 44) == -1) return -1;

    return 0;
}

// 0x4844B4
static int mapHeaderRead(MapHeader* ptr, File* stream)
{
    if (fileReadInt32(stream, &(ptr->version)) == -1) return -1;
    if (fileReadFixedLengthString(stream, ptr->name, 16) == -1) return -1;
    if (fileReadInt32(stream, &(ptr->enteringTile)) == -1) return -1;
    if (fileReadInt32Enum<MapElevation>(stream, &(ptr->enteringElevation)) == -1) return -1;
    if (fileReadInt32Enum<Rotation>(stream, &(ptr->enteringRotation)) == -1) return -1;
    if (fileReadInt32(stream, &(ptr->localVariablesCount)) == -1) return -1;
    if (fileReadInt32(stream, &(ptr->scriptIndex)) == -1) return -1;
    if (fileReadInt32Enum<MapHeaderFlags>(stream, &(ptr->flags)) == -1) return -1;
    if (fileReadInt32(stream, &(ptr->darkness)) == -1) return -1;
    if (fileReadInt32(stream, &(ptr->globalVariablesCount)) == -1) return -1;
    if (fileReadInt32Enum<Map>(stream, &(ptr->index)) == -1) return -1;
    if (fileReadUInt32(stream, &(ptr->lastVisitTime)) == -1) return -1;
    if (fileReadInt32List(stream, ptr->field_3C, 44) == -1) return -1;

    return 0;
}



void mapSetViewerLoad(bool viewerLoad)
{
    gMapViewerLoad = viewerLoad;
}

void mapVariablesFree()
{
    // NOTE: Uninline.
    mapGlobalVariablesFree();

    // NOTE: Uninline.
    mapLocalVariablesFree();
}

unsigned int mapGetLoadGeneration()
{
    return gMapLoadGeneration;
}

void mapSetTransitionSuppressed(bool suppressed)
{
    gMapTransitionSuppressed = suppressed;
}

static int mapSetTransitionImpl(MapTransition* transition, Object* explicitActor)
{
    if (transition == nullptr) {
        return -1;
    }

    if (gMapTransitionSuppressed) {
        debugPrint("map: transition request dropped (actor may not transit)\n");
        return 0;
    }

    memcpy(&gMapTransition, transition, sizeof(gMapTransition));

    // Preserve the action context across the beat-tail transition. map_enter runs
    // inside mapLoad, after the spatial/elevator scope that requested travel has
    // already unwound. Slot ids survive the wholesale map object replacement.
    gMapTransitionInitiatorSlot = -1;
    gMapTransitionParticipantMask = 0;
    if (serverDedicatedActive()) {
        Object* initiator = explicitActor != nullptr ? explicitActor : scriptContextDude(nullptr);
        gMapTransitionInitiatorSlot = playerActorSlotOf(initiator);
        for (int slot = 0; slot < playerActorCount(); slot++) {
            if (playerActorAt(slot) != nullptr && playerActorOnline(slot)) {
                gMapTransitionParticipantMask |= 1u << slot;
            }
        }
    }

    if (gMapTransition.map == 0) {
        gMapTransition.map = MAP_TRANSITION;
    }

    if (isInCombat()) {
        _game_user_wants_to_quit = GAME_QUIT_REQUEST_END_COMBAT;
    }

    return 0;
}

int mapSetTransitionForActor(MapTransition* transition, Object* actor)
{
    return mapSetTransitionImpl(transition, actor);
}

unsigned int mapTransitionXpAudienceMask()
{
    return gMapActiveTransitionXpAudienceMask;
}

bool mapTransitionPending()
{
    return gMapTransition.map != 0;
}

int _MapDirEraseFile_(const char* a1, const char* a2)
{
    char path[COMPAT_MAX_PATH];

    snprintf(path, sizeof(path), "%s\\%s%s",
        settings.system.master_patches_path.c_str(), a1, a2);
    if (compat_remove(path) != 0) {
        return -1;
    }

    return 0;
}

int mapSaveToStream(File* stream, int* mapBodyLenOut)
{
    gMapHeader.flags |= MAP_HEADER_SAVED;
    gMapHeader.lastVisitTime = gameTimeGetTime();

    // Co-op: recruited party members carry OBJECT_NO_SAVE (party_member.cc:401),
    // so objectSaveAll would skip them and a viewer rebuilding its world from this
    // blob would lose every companion on a rebaseline (they otherwise ride only the
    // live recruit-time spawn delta). Ride them in the map body exactly as the DISK
    // save does: _partyMemberPrepSave clears NO_SAVE|NO_REMOVE on members (index > 0)
    // so objectSaveAll writes them, UnPrepSave restores. Bracket the SINGLE
    // _map_save_file call so no early return can split the latch — a split latch
    // permanently strips companion flags AND denies all future recruit/dismiss
    // (party_member.cc:390/455). This is the only save path that must add the
    // bracket: the disk save reaches _map_save_file via _map_save with its own
    // bracket (savegame.cc:731/833), never through mapSaveToStream.
    //
    // ►► Guard on an ACTUAL recruited member (length includes gDude at index 0, so
    // > 1 == someone recruited). _partyMemberPrepSave's script-flag clear at
    // party_member.cc:501-504 is NOT index>0-guarded — it strips SCRIPT_FLAG_0x08|
    // 0x10 from gDude's own script too, which perturbs the dude's script/lvar
    // serialization in the map body. Unbracketed when the party is empty, the blob
    // stays byte-identical (the no-party reconstruction goldens depend on this).
    bool bracketParty = partyHasRecruitedMembers();
    if (bracketParty) {
        _partyMemberPrepSave();
    }
    int mapSaveRc = _map_save_file(stream);
    if (bracketParty) {
        _partyMemberUnPrepSave();
    }
    if (mapSaveRc == -1) {
        return -1;
    }

    // The map body ends here; the dude blob follows. The two sub-streams
    // self-delimit on load, but the split length is a fail-loud wire guard (§2).
    if (mapBodyLenOut != nullptr) {
        *mapBodyLenOut = (int)fileTell(stream);
    }

    if (_obj_save_dude(stream) == -1) {
        return -1;
    }

    // B1-N: the extra player actors follow the host, in registry slot order
    // (MP_PROPOSAL.md Ch 5.3). They are NO_SAVE for the same reason the dude is,
    // so objectSaveAll skipped them too. Slot order IS wire order: the viewer
    // registers them in this order and then reproduces objectAssignAllNetIds,
    // which numbers the registry before the tile walk — get the order wrong and
    // every netId after the actors shifts.
    //
    // The sections self-delimit on load (each is one _obj_save_obj tree), so the
    // header carries the actor COUNT rather than per-section lengths; the
    // existing dudeBlobLen guard still bounds the whole appendix and the crc32
    // still covers every byte.
    for (int slot = 1; slot < playerActorCount(); slot++) {
        if (_obj_save_player_actor(stream, playerActorAt(slot)) == -1) {
            return -1;
        }
    }

    // B1-S: the CHARACTER SHEETS follow the actor objects, from slot 0 — the
    // sheet lives off the Object (in the proto row and the PC-globals), so
    // nothing above carries it. Before this the viewer seeded every sheet from
    // its own gDudeProto, which is right only until someone levels: a joiner
    // then sees the host's character as it was at boot, and every extra as a
    // copy of that (PLAYER_SHEET_DESIGN.md §5).
    //
    // Last in the appendix on purpose: the sections above self-delimit as
    // _obj_save_obj trees, so a reader that predates this block simply stops
    // early and ignores the tail (the headless F2_CLIENT_BLOB_IN probe does
    // exactly that, and is unaffected).
    if (playerSheetBlockWrite(stream, 0) == -1) {
        return -1;
    }

    return 0;
}

} // namespace fallout
