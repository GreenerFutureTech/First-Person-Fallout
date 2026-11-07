#ifndef FALLOUT_WORLDMAP_UI_H_
#define FALLOUT_WORLDMAP_UI_H_

#include <string>
#include <unordered_map>

// Seam between the worldmap core simulation (worldmap.cc) and its interface /
// HUD presentation (worldmap_ui.cc).
//
// NOTE: The core<->UI boundary here has INWARD by-name edges in BOTH
// directions (core calls UI functions directly, and UI calls core functions
// directly) rather than routing everything through a presenter. This is fine
// for a same-binary translation-unit split. A future link-level split would
// need to replace these direct cross-edges with presenter methods.
//
//   * SHARED mutable state: worldmap.cc owns the definitions; both TUs read and
//     write them. wmGenData in particular mixes simulation and chrome fields in
//     one struct (it is NOT read-only) -- flag for decomposition in a headless
//     split.
//   * INWARD (core -> UI): UI functions the core invokes by name; defined in
//     worldmap_ui.cc.
//   * OUTWARD (UI -> core): core functions the UI invokes by name; defined in
//     worldmap.cc.

#include "worldmap_defs.h"

namespace fallout {

#define MAX_TRAIL_LENGTH (1000)

typedef struct TrailDot {
    int x;
    int y;
} TrailDot;

typedef struct TrailMarkerState {
    bool hasPattern;
    int dotCount;
    TrailDot dots[MAX_TRAIL_LENGTH];
    int remainingDots;
    int remainingSpacing;
} TrailMarkerState;

// ---------------------------------------------------------------------------
// SHARED mutable state (defined in worldmap.cc, read/written by both TUs).
// ---------------------------------------------------------------------------
extern WmGenData wmGenData;
extern int wmBkWin;
extern unsigned char* wmBkWinBuf;
extern std::unordered_map<int, std::string> wmTownTitleOverrides;
extern bool wmTownNamesHidden;
extern CityInfo* wmAreaInfoList;
extern int wmMaxAreaNum;
extern CitySizeDescription wmSphereData[CITY_SIZE_COUNT];
extern TileInfo* wmTileInfoList;
extern int wmMaxTileNum;
extern int wmNumHorizontalTiles;
extern MessageList wmMsgFile;
extern int wmWorldOffsetX;
extern int wmWorldOffsetY;
extern unsigned int wmLastRndTime;
extern bool gTownMapHotkeysFix;
extern unsigned char* wmOverlayOffscreenBuf;
extern const InterfaceFrameId kDefaultCarInterfaceArtFrmId;
extern InterfaceFrameId carInterfaceArtFrmId;
extern const InterfaceFrmId wmRndCursorFrmIds[WORLD_MAP_ENCOUNTER_FRM_COUNT];
extern unsigned int wmLastTravelTick;
extern int worldmapTrailMarkers;
extern bool worldmapTerrainInfo;
extern bool wmTerrainInfoIsVisible;
extern TrailMarkerState trailMarkerState;
extern const Color worldmapTrailMarkerColor;

// Worldmap streaming flags (viewer-side). Set by wire decoder, read by wmWorldMapFunc
// and the main loop.
extern bool gPendingWorldmapEnter;
extern bool gWorldmapStreaming;
extern bool gWorldmapStateDirty;

// ---------------------------------------------------------------------------
// INWARD (core -> UI): UI functions the core calls by name.
// Defined in worldmap_ui.cc.
// ---------------------------------------------------------------------------
bool wmCursorIsVisible();
int wmInterfaceCenterOnParty();
int wmInterfaceScrollPixel(int stepX, int stepY, int dx, int dy, bool* success, bool shouldRefresh);
int wmInterfaceRefresh();
int wmRefreshInterfaceOverlay(bool shouldRefreshWindow);
void wmInterfaceDialSyncTime(bool shouldRefreshWindow);
void wmInterfaceRefreshDate(bool shouldRefreshWindow);
void wmBlinkRndEncounterIcon(bool special);
void wmFadeOut();
void wmFadeIn();
void wmFadeReset();
bool wmHasTownTitleOverrides();
bool wmTryMatchAreaContainingMapIdx(Map mapIdx, City* areaIdxPtr);
bool wmTravelTickDue(unsigned int now);
void wmAreaGetMarkWorldPos(CityInfo* city, int* xPtr, int* yPtr);
bool wmLockCarInterfaceArt(InterfaceFrameId artIndex, Art** artPtr, CacheEntry** handlePtr);
int wmInterfaceDrawCircleOverlaySafe(CityInfo* city, CitySizeDescription* citySizeDescription, unsigned char* dest, int x, int y);
void wmInterfaceDrawTerrainInfo();
const char* wmGetHotspotText();
void wmResetTerrainInfo();
void wmResetTrailMarkers();
void wmRunLocalMapEnterHook(Map* mapIdxPtr);

// ---------------------------------------------------------------------------
// OUTWARD (UI -> core): core functions the UI calls by name.
// Defined in worldmap.cc.
// ---------------------------------------------------------------------------
int wmMatchWorldPosToArea(int x, int y, City* areaIdxPtr);
int wmAreaFindFirstValidMap(Map* mapIdxPtr, MapElevation* elevationPtr, int* tilePtr, Rotation* rotationPtr);
int wmAreaFindFirstValidMap(int* mapIdxPtr);
// Resolve a town-map ENTRANCE INDEX (chosen on a viewer) to a map, server-side, and set
// the arrival spot. Refuses an out-of-range or undiscovered entrance. See the body.
int wmAreaResolveEntrance(int entranceIndex, int* mapIdxPtr);
int wmGetAreaName(CityInfo* city, char* name);

} // namespace fallout

#endif /* FALLOUT_WORLDMAP_UI_H_ */
