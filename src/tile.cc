#include "tile.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <stack>
#include <vector>

#include "art.h"
#include "combat.h"
#include "color.h"
#include "debug.h"
#include "draw.h"
#include "game_mouse.h"
#include "light.h"
#include "map.h"
#include "map_edge.h"
#include "object.h"
#include "party_member.h"
#include "platform_compat.h"
#include "proto.h"
#include "settings.h"
#include "svga.h"
#include "tile_hires_stencil.h"

namespace fallout {

typedef struct RightsideUpTableEntry {
    int field_0;
    int field_4;
} RightsideUpTableEntry;

typedef struct UpsideDownTableEntry {
    int field_0;
    int field_4;
} UpsideDownTableEntry;

typedef struct STRUCT_51DA6C {
    int field_0;
    int offsets[2];
    int intensity;
} STRUCT_51DA6C;

typedef struct RightsideUpTriangle {
    int field_0;
    int field_4;
    int field_8;
} RightsideUpTriangle;

typedef struct UpsideDownTriangle {
    int field_0;
    int field_4;
    int field_8;
} UpsideDownTriangle;

struct roof_fill_task {
    int x;
    int y;
};

struct TileFirstPersonTopologyRenderContext {
    MapElevation elevation;
    int mapLeft;
    int mapTop;
    int cellSize;
};

struct TileFirstPersonGrayboxRenderContext {
    MapElevation elevation;
    unsigned char* wallCells;
    unsigned char* wallOrientations;
};

enum TileFirstPersonWallOrientation : unsigned char {
    TILE_FIRST_PERSON_WALL_ORIENTATION_CELL,
    TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH,
    TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST,
};

struct TileFirstPersonActorRenderContext {
    MapElevation elevation;
    double cameraX;
    double cameraY;
    double directionX;
    double directionY;
    double cameraPlaneX;
    double cameraPlaneY;
    int horizonY;
    std::vector<double>* wallDepths;
};

static void tileSetBorder(int windowWidth, int windowHeight, int hexGridWidth, int hexGridHeight);
static void tileRefreshMapper(Rect* rect, MapElevation elevation);
static void tileRefreshGame(Rect* rect, MapElevation elevation);
static void tileRenderFirstPersonPreview();
static void tileFirstPersonTopologyMarkObject(Object* object, void* context);
static void tileFirstPersonGrayboxMarkObject(Object* object, void* context);
static void tileFirstPersonRenderActor(Object* object, void* context);
static void tileFirstPersonDrawCrosshair();
static void tileFirstPersonDrawDiagnosticLine(int x1, int y1, int x2, int y2, Color color);
static bool tileFirstPersonGetWorldPosition(int tile, double* x, double* y);
static void roof_fill_push_task_if_in_bounds(std::stack<roof_fill_task>& tasks_stack, int x, int y);
static void roof_fill_off_process_task(std::stack<roof_fill_task>& tasks_stack, MapElevation elevation, bool on);
static void tileRenderRoof(const TileFrmId& frmId, int x, int y, Rect* rect, int light);
static void _draw_grid(int tile, MapElevation elevation, Rect* rect);
static void tileRenderFloor(const TileFrmId& frmId, int x, int y, Rect* rect);
static int _tile_make_line(int currentCenterTile, int newCenterTile, int* tiles, int tilesCapacity);

// 0x50E7C7 minus_4_0f
static double const dbl_50E7C7 = -4.0;

// 0x51D950 borderInitialized
bool gTileBorderInitialized = false;

// 0x51D954 scroll_blocking_on
static bool gTileScrollBlockingEnabled = true;

// 0x51D958 scroll_limiting_on
static bool gTileScrollLimitingEnabled = true;

// 0x51D95C show_roof
static bool gTileRoofIsVisible = true;

// 0x51D960 show_grid
static bool gTileGridIsVisible = false;

// 0x51D964 tile_refresh
static TileWindowRefreshElevationProc* gTileWindowRefreshElevationProc = tileRefreshGame;

// 0x51D968 refresh_enabled
static bool gTileEnabled = true;
static bool gFirstPersonPreviewEnabled = false;
static bool gFirstPersonPreviewTopologyEnabled = false;
static int gFirstPersonPreviewYaw = 0;

// 0x51D96C off_tile
const int _off_tile[ROTATION_COUNT] = {
    16,
    32,
    16,
    -16,
    -32,
    -16,
};

// 0x51D984
const int dword_51D984[ROTATION_COUNT] = {
    -12,
    0,
    12,
    12,
    0,
    -12,
};

// 0x51D99C rightside_up_table
static RightsideUpTableEntry _rightside_up_table[13] = {
    { -1, 2 },
    { 78, 2 },
    { 76, 6 },
    { 73, 8 },
    { 71, 10 },
    { 68, 14 },
    { 65, 16 },
    { 63, 18 },
    { 61, 20 },
    { 58, 24 },
    { 55, 26 },
    { 53, 28 },
    { 50, 32 },
};

// 0x51DA04 upside_down_table
static UpsideDownTableEntry _upside_down_table[13] = {
    { 0, 32 },
    { 48, 32 },
    { 49, 30 },
    { 52, 26 },
    { 55, 24 },
    { 57, 22 },
    { 60, 18 },
    { 63, 16 },
    { 65, 14 },
    { 67, 12 },
    { 70, 8 },
    { 73, 6 },
    { 75, 4 },
};

// 0x51DA6C verticies
static STRUCT_51DA6C _verticies[10] = {
    { 16, -1, -201, 0 },
    { 48, -2, -2, 0 },
    { 960, 0, 0, 0 },
    { 992, 199, -1, 0 },
    { 1024, 198, 198, 0 },
    { 1936, 200, 200, 0 },
    { 1968, 399, 199, 0 },
    { 2000, 398, 398, 0 },
    { 2912, 400, 400, 0 },
    { 2944, 599, 399, 0 },
};

// 0x51DB0C rightside_up_triangles
static RightsideUpTriangle _rightside_up_triangles[5] = {
    { 2, 3, 0 },
    { 3, 4, 1 },
    { 5, 6, 3 },
    { 6, 7, 4 },
    { 8, 9, 6 },
};

// 0x51DB48 upside_down_triangles
static UpsideDownTriangle _upside_down_triangles[5] = {
    { 0, 3, 1 },
    { 2, 5, 3 },
    { 3, 6, 4 },
    { 5, 8, 6 },
    { 6, 9, 7 },
};

// 0x668224 intensity_map
static int _intensity_map[3280];

// 0x66B564 dir_tile2
static int _dir_tile2[2][ROTATION_COUNT];

// Deltas to perform tile calculations in given direction.
//
// 0x66B594 dir_tile
static int _dir_tile[2][ROTATION_COUNT];

// 0x66B5C4 tile_grid_blocked
static unsigned char _tile_grid_blocked[512];

// 0x66B7C4 tile_grid_occupied
static unsigned char _tile_grid_occupied[512];

// 0x66B9C4 tile_mask
static unsigned char _tile_mask[512];

// 0x66BBC4 tile_border
int gTileBorderMinX = 0;

// 0x66BBC8
int gTileBorderMinY = 0;

// 0x66BBCC
int gTileBorderMaxX = 0;

// 0x66BBD0
int gTileBorderMaxY = 0;

// 0x66BBD4 buf_rect_2
static Rect gTileWindowRect;

// 0x66BBE4 tile_grid
static unsigned char _tile_grid[32 * 16];

// 0x66BDE4 square_y
static int _square_y;

// 0x66BDE8 square_x
static int _square_x;

// 0x66BDEC square_offx
static int _square_offx;

// 0x66BDF0 square_offy
static int _square_offy;

// 0x66BDF4 blit
static TileWindowRefreshProc* gTileWindowRefreshProc;

// 0x66BDF8 tile_offy
static int _tile_offy;

// 0x66BDFC tile_offx
static int _tile_offx;

// 0x66BE00 square_size
static int gSquareGridSize;

// Number of tiles horizontally.
//
// Currently this value is always 200.
//
// 0x66BE04 grid_width
static int gHexGridWidth;

// 0x66BE08 squares
static TileData** gTileSquares;

// 0x66BE0C tile_back_buf
static unsigned char* gTileWindowBuffer;

// Number of tiles vertically.
//
// Currently this value is always 200.
//
// 0x66BE10 grid_length
static int gHexGridHeight;

// 0x66BE14 buf_length_2
static int gTileWindowHeight;

// 0x66BE18 tile_x
static int _tile_x;

// 0x66BE1C tile_y
static int _tile_y;

// The number of tiles in the hex grid.
//
// 0x66BE20 grid_size
int gHexGridSize;

// 0x66BE24 square_length
static int gSquareGridHeight;

// 0x66BE28 buf_to_width
static int gTileWindowPitch;

// 0x66BE2C square_width
static int gSquareGridWidth;

// 0x66BE30 buf_width_2
static int gTileWindowWidth;

// 0x66BE34 tile_center_tile
int gCenterTile;

// Optional mapper overlay drawn over the iso view each refresh (edge editor). Null when unused.
static TileMapperOverlayProc* gTileMapperOverlayProc = nullptr;

void tileSetMapperOverlayProc(TileMapperOverlayProc* proc)
{
    gTileMapperOverlayProc = proc;
}

void tileMapperOverlayRender(unsigned char* buffer, int pitch, MapElevation elevation, const Rect* clip)
{
    if (gTileMapperOverlayProc != nullptr) {
        gTileMapperOverlayProc(buffer, pitch, elevation, clip);
    }
}

// 0x4B0C40 tile_init
int tileInit(TileData** squareGrid, int squareGridWidth, int squareGridHeight, int hexGridWidth, int hexGridHeight, unsigned char* buf, int windowWidth, int windowHeight, int windowPitch, TileWindowRefreshProc* windowRefreshProc)
{
    int v11;
    int v12;
    int v13;

    int v20;
    int v21;
    int v22;
    int v23;
    int v24;
    int v25;

    gSquareGridWidth = squareGridWidth;
    gTileSquares = squareGrid;
    gHexGridHeight = hexGridHeight;
    gSquareGridHeight = squareGridHeight;
    gHexGridWidth = hexGridWidth;
    _dir_tile[0][0] = -1;
    _dir_tile[0][4] = 1;
    _dir_tile[1][1] = -1;
    gHexGridSize = hexGridWidth * hexGridHeight;
    _dir_tile[1][3] = 1;
    gTileWindowBuffer = buf;
    _dir_tile2[0][0] = -1;
    gTileWindowWidth = windowWidth;
    _dir_tile2[0][3] = -1;
    gTileWindowHeight = windowHeight;
    _dir_tile2[1][1] = 1;
    gTileWindowPitch = windowPitch;
    _dir_tile2[1][2] = 1;
    gTileWindowRect.right = windowWidth - 1;
    gSquareGridSize = squareGridHeight * squareGridWidth;
    gTileWindowRect.bottom = windowHeight - 1;
    gTileWindowRect.left = 0;
    gTileWindowRefreshProc = windowRefreshProc;
    gTileWindowRect.top = 0;
    _dir_tile[0][1] = hexGridWidth - 1;
    _dir_tile[0][2] = hexGridWidth;
    gTileGridIsVisible = 0;
    _dir_tile[0][3] = hexGridWidth + 1;
    _dir_tile[1][2] = hexGridWidth;
    _dir_tile2[0][4] = hexGridWidth;
    _dir_tile2[0][5] = hexGridWidth;
    _dir_tile[0][5] = -hexGridWidth;
    _dir_tile[1][0] = -hexGridWidth - 1;
    _dir_tile[1][4] = 1 - hexGridWidth;
    _dir_tile[1][5] = -hexGridWidth;
    _dir_tile2[0][1] = -hexGridWidth - 1;
    _dir_tile2[1][4] = -hexGridWidth;
    _dir_tile2[0][2] = hexGridWidth - 1;
    _dir_tile2[1][5] = -hexGridWidth;
    _dir_tile2[1][0] = hexGridWidth + 1;
    _dir_tile2[1][3] = 1 - hexGridWidth;

    v11 = 0;
    v12 = 0;
    do {
        v13 = 64;
        do {
            _tile_mask[v12++] = v13 > v11;
            v13 -= 4;
        } while (v13);

        do {
            _tile_mask[v12++] = v13 > v11 ? 2 : 0;
            v13 += 4;
        } while (v13 != 64);

        v11 += 16;
    } while (v11 != 64);

    v11 = 0;
    do {
        v13 = 0;
        do {
            _tile_mask[v12++] = 0;
            v13++;
        } while (v13 < 32);
        v11++;
    } while (v11 < 8);

    v11 = 0;
    do {
        v13 = 0;
        do {
            _tile_mask[v12++] = v13 > v11 ? 0 : 3;
            v13 += 4;
        } while (v13 != 64);

        v13 = 64;
        do {
            _tile_mask[v12++] = v13 > v11 ? 0 : 4;
            v13 -= 4;
        } while (v13);

        v11 += 16;
    } while (v11 != 64);

    bufferFill(_tile_grid, 32, 16, 32, COLOR_FIRST);
    bufferDrawLine(_tile_grid, 32, 16, 0, 31, 4, COLOR_DARK_GREY_3);
    bufferDrawLine(_tile_grid, 32, 31, 4, 31, 12, COLOR_DARK_GREY_3);
    bufferDrawLine(_tile_grid, 32, 31, 12, 16, 15, COLOR_DARK_GREY_3);
    bufferDrawLine(_tile_grid, 32, 0, 12, 16, 15, COLOR_DARK_GREY_3);
    bufferDrawLine(_tile_grid, 32, 0, 4, 0, 12, COLOR_DARK_GREY_3);
    bufferDrawLine(_tile_grid, 32, 16, 0, 0, 4, COLOR_DARK_GREY_3);

    bufferFill(_tile_grid_occupied, 32, 16, 32, COLOR_FIRST);
    bufferDrawLine(_tile_grid_occupied, 32, 16, 0, 31, 4, COLOR_BLUE);
    bufferDrawLine(_tile_grid_occupied, 32, 31, 4, 31, 12, COLOR_BLUE);
    bufferDrawLine(_tile_grid_occupied, 32, 31, 12, 16, 15, COLOR_BLUE);
    bufferDrawLine(_tile_grid_occupied, 32, 0, 12, 16, 15, COLOR_BLUE);
    bufferDrawLine(_tile_grid_occupied, 32, 0, 4, 0, 12, COLOR_BLUE);
    bufferDrawLine(_tile_grid_occupied, 32, 16, 0, 0, 4, COLOR_BLUE);

    bufferFill(_tile_grid_blocked, 32, 16, 32, COLOR_FIRST);
    bufferDrawLine(_tile_grid_blocked, 32, 16, 0, 31, 4, COLOR_RED);
    bufferDrawLine(_tile_grid_blocked, 32, 31, 4, 31, 12, COLOR_RED);
    bufferDrawLine(_tile_grid_blocked, 32, 31, 12, 16, 15, COLOR_RED);
    bufferDrawLine(_tile_grid_blocked, 32, 0, 12, 16, 15, COLOR_RED);
    bufferDrawLine(_tile_grid_blocked, 32, 0, 4, 0, 12, COLOR_RED);
    bufferDrawLine(_tile_grid_blocked, 32, 16, 0, 0, 4, COLOR_RED);

    for (v20 = 0; v20 < 16; v20++) {
        v21 = v20 * 32;
        v22 = 31;
        v23 = v21 + 31;

        if (_tile_grid_blocked[v23] == 0) {
            do {
                --v22;
                --v23;
            } while (v22 > 0 && _tile_grid_blocked[v23] == 0);
        }

        v24 = v21;
        v25 = 0;
        if (_tile_grid_blocked[v21] == 0) {
            do {
                ++v25;
                ++v24;
            } while (v25 < 32 && _tile_grid_blocked[v24] == 0);
        }

        bufferDrawLine(_tile_grid_blocked, 32, v25, v20, v22, v20, COLOR_RED);
    }

    // In order to calculate scroll borders correctly we need to pretend we're
    // at original resolution. Since border is calculated only once at start,
    // there is not need to change it all the time.
    gTileWindowWidth = ORIGINAL_ISO_WINDOW_WIDTH;
    gTileWindowHeight = ORIGINAL_ISO_WINDOW_HEIGHT;

    tile_hires_stencil_init();

    tileSetCenter(hexGridWidth * (hexGridHeight / 2) + hexGridWidth / 2, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);
    tileSetBorder(windowWidth, windowHeight, hexGridWidth, hexGridHeight);

    // Restore actual window size and set center one more time to calculate
    // correct screen offsets, which are required for subsequent object update
    // area calculations.
    gTileWindowWidth = windowWidth;
    gTileWindowHeight = windowHeight;

    tileSetCenter(hexGridWidth * (hexGridHeight / 2) + hexGridWidth / 2, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);

    if (settings.system.executableIsMapper()) {
        gTileWindowRefreshElevationProc = tileRefreshMapper;
    }

    return 0;
}

// 0x4B11E4 tile_set_border
static void tileSetBorder(int windowWidth, int windowHeight, int hexGridWidth, int hexGridHeight)
{
    // TODO: Borders, scroll blockers and tile system overall were designed
    // with 640x480 in mind, so using windowWidth and windowHeight is
    // meaningless for calculating borders. For now keep borders for original
    // resolution.
    int v1 = tileFromScreenXY(-320, -240);
    int v2 = tileFromScreenXY(-320, ORIGINAL_ISO_WINDOW_HEIGHT + 240);

    gTileBorderMinX = abs(hexGridWidth - 1 - v2 % hexGridWidth - _tile_x) + 6;
    gTileBorderMinY = abs(_tile_y - v1 / hexGridWidth) + 7;
    gTileBorderMaxX = hexGridWidth - gTileBorderMinX - 1;
    gTileBorderMaxY = hexGridHeight - gTileBorderMinY - 1;

    if ((gTileBorderMinX & 1) == 0) {
        gTileBorderMinX++;
    }

    if ((gTileBorderMaxX & 1) == 0) {
        gTileBorderMinX--;
    }

    gTileBorderInitialized = true;
}

// NOTE: Collapsed.
//
// 0x4B129C
void _tile_reset_()
{
}

// NOTE: Uncollapsed 0x4B129C.
void tileReset()
{
    _tile_reset_();
}

// NOTE: Uncollapsed 0x4B129C.
void tileExit()
{
    _tile_reset_();
}

// 0x4B12A8 tile_disable_refresh
void tileDisable()
{
    gTileEnabled = false;
}

// 0x4B12B4 tile_enable_refresh
void tileEnable()
{
    gTileEnabled = true;
}

// 0x4B12C0 tile_refresh_rect
void tileWindowRefreshRect(Rect* rect, MapElevation elevation)
{
    if (gTileEnabled) {
        if (elevation == gElevation) {
            gTileWindowRefreshElevationProc(rect, elevation);
        }
    }
}

// 0x4B12D8 tile_refresh_display
void tileWindowRefresh()
{
    if (gTileEnabled) {
        gTileWindowRefreshElevationProc(&gTileWindowRect, gElevation);
    }
}

// 0x4B12F8 tile_set_center
int tileSetCenter(int tile, int flags)
{
    if (!tileIsValid(tile)) {
        return -1;
    }

    const bool edgeActive = mapEdgeIsEnabled();
    const bool isScroll = flags == 0;

    if (edgeActive && !isScroll) {
        // Forced positioning (teleport, load): clamp to edge boundary.
        tile = mapEdgeSelectZoneAndClamp(tile, gElevation);
        if (!tileIsValid(tile)) return -1;
    }

    bool boundaryModsSet = false;
    if ((flags & TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS) == 0) {
        if (gTileScrollLimitingEnabled) {
            int tileScreenX;
            int tileScreenY;
            tileToScreenXY(tile, &tileScreenX, &tileScreenY);

            int dudeScreenX;
            int dudeScreenY;
            tileToScreenXY(gDude->tile, &dudeScreenX, &dudeScreenY);

            int dx = abs(dudeScreenX - tileScreenX);
            int dy = abs(dudeScreenY - tileScreenY);

            if (dx > abs(dudeScreenX - _tile_offx)
                || dy > abs(dudeScreenY - _tile_offy)) {
                if (dx >= 480 || dy >= 400) {
                    return -1;
                }
            }
        }

        // Must run after scroll limiting: mapEdgeSetBoundaryMods mutates the persistent
        // alignment mods, so a scroll the limiter rejects must not touch them.
        if (edgeActive && isScroll && mapEdgeZoneIsSelected()) {
            // Block instead of clamp: clamping would shift the center and make
            // mapScroll's buffer copy produce artifacts.
            if (!mapEdgeTileInBounds(tile)) {
                return -1;
            }
            // On a boundary edge: set sub-tile mods and force a full redraw.
            if (mapEdgeSetBoundaryMods(tile)) {
                boundaryModsSet = true;
                flags |= TILE_SET_CENTER_REFRESH_WINDOW;
            }
        } else if ((!edgeActive || !mapEdgeZoneIsSelected()) && gTileScrollBlockingEnabled) {
            // Object scroll-blocker only applies when EDG isn't enforcing the boundary.
            if (_obj_scroll_blocking_at(tile, gElevation) == 0) {
                return -1;
            }
        }
    }

    int tile_x = gHexGridWidth - 1 - tile % gHexGridWidth;
    int tile_y = tile / gHexGridWidth;

    // Legacy global borders are for maps without EDG data. EDG maps use their
    // own boundary/clamp logic above, which can validly land on this border.
    if ((!edgeActive || !mapEdgeZoneIsSelected()) && gTileBorderInitialized && gTileScrollBlockingEnabled) {
        if (tile_x <= gTileBorderMinX || tile_x >= gTileBorderMaxX || tile_y <= gTileBorderMinY || tile_y >= gTileBorderMaxY) {
            return -1;
        }
    }

    _tile_y = tile_y;
    _tile_offx = mapEdgeGetTileXAlignment() + (gTileWindowWidth - 32) / 2;
    _tile_x = tile_x;
    _tile_offy = mapEdgeGetTileYAlignment() + (gTileWindowHeight - 16) / 2;

    if (tile_x & 1) {
        _tile_x -= 1;
        _tile_offx -= 32;
    }

    _square_x = _tile_x / 2;
    _square_y = _tile_y / 2;
    _square_offx = _tile_offx - 16;
    _square_offy = _tile_offy - 2;

    if (_tile_y & 1) {
        _square_offy -= 12;
        _square_offx -= 16;
    }

    gCenterTile = tile;

    tile_hires_stencil_on_center_tile_or_elevation_change();

    if ((flags & TILE_SET_CENTER_REFRESH_WINDOW) != 0) {
        // NOTE: Uninline.
        tileWindowRefresh();
        if (boundaryModsSet)
            return -1; // necessary for the correct rendering of the map
    }

    return 0;
}

bool checkRectNeedsClear(const Rect* rect, bool hasVisArea, const Rect* visArea)
{
    if (!hasVisArea) {
        return true;
    }

    return (rect->left < visArea->left || rect->right > visArea->right || rect->top < visArea->top || rect->bottom > visArea->bottom);
}

// TODO: these two functions are exact copies of isoWindowRefreshRect*. gTileWindowBuffer == gIsoWindowBuffer, these are the same window!
// 0x4B1554 refresh_mapper
static void tileRefreshMapper(Rect* rect, MapElevation elevation)
{
    Rect rectToUpdate;

    if (rectIntersection(rect, &gTileWindowRect, &rectToUpdate) == -1) {
        return;
    }

    Rect visArea;
    bool hasVisArea = mapEdgeComputeVisibleArea(elevation, &visArea);

    Rect renderRect = rectToUpdate;
    bool didClear = checkRectNeedsClear(&rectToUpdate, hasVisArea, &visArea);

    // HRP EdgeClipping: when clipped, clear only if CheckRect; otherwise always clear.
    if (didClear) {
        bufferFill(gTileWindowBuffer + gTileWindowPitch * rectToUpdate.top + rectToUpdate.left,
            rectGetWidth(&rectToUpdate),
            rectGetHeight(&rectToUpdate),
            gTileWindowPitch,
            COLOR_FIRST);
    }

    if (hasVisArea && rectIntersection(&renderRect, &visArea, &renderRect) == -1) {
        if (didClear) {
            gTileWindowRefreshProc(&rectToUpdate);
        }
        return;
    }

    tileRenderFloorsInRect(&renderRect, elevation);
    _grid_render(&renderRect, elevation);
    _obj_render_pre_roof(&renderRect, elevation);
    tileRenderRoofsInRect(&renderRect, elevation);
    _obj_render_post_roof(&renderRect, elevation);

    if (!hasVisArea) {
        tile_hires_stencil_draw(&renderRect, gTileWindowBuffer, gTileWindowWidth, gTileWindowHeight);
    }

    tileMapperOverlayRender(gTileWindowBuffer, gTileWindowPitch, elevation, &renderRect);

    gTileWindowRefreshProc(didClear ? &rectToUpdate : &renderRect);
}

// 0x4B15E8 refresh_game
static void tileRefreshGame(Rect* rect, MapElevation elevation)
{
    if (gFirstPersonPreviewEnabled) {
        tileFirstPersonPreviewRefresh();
        return;
    }

    Rect rectToUpdate;

    if (rectIntersection(rect, &gTileWindowRect, &rectToUpdate) == -1) {
        return;
    }

    Rect visArea;
    bool hasVisArea = mapEdgeComputeVisibleArea(elevation, &visArea);

    Rect renderRect = rectToUpdate;
    bool didClear = checkRectNeedsClear(&rectToUpdate, hasVisArea, &visArea);

    // HRP EdgeClipping: when clipped, clear only if CheckRect; otherwise always clear.
    if (didClear) {
        bufferFill(gTileWindowBuffer + rectToUpdate.top * gTileWindowPitch + rectToUpdate.left,
            rectGetWidth(&rectToUpdate),
            rectGetHeight(&rectToUpdate),
            gTileWindowPitch,
            COLOR_FIRST);
    }

    if (hasVisArea && rectIntersection(&renderRect, &visArea, &renderRect) == -1) {
        if (didClear) {
            gTileWindowRefreshProc(&rectToUpdate);
        }
        return;
    }

    tileRenderFloorsInRect(&renderRect, elevation);
    _obj_render_pre_roof(&renderRect, elevation);
    tileRenderRoofsInRect(&renderRect, elevation);
    _obj_render_post_roof(&renderRect, elevation);

    if (!hasVisArea) {
        tile_hires_stencil_draw(&renderRect, gTileWindowBuffer, gTileWindowWidth, gTileWindowHeight);
    }

    gTileWindowRefreshProc(didClear ? &rectToUpdate : &renderRect);
}

static void tileRenderFirstPersonPreview()
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kFieldOfViewScale = 0.66;

    bufferFill(gTileWindowBuffer,
        gTileWindowWidth,
        gTileWindowHeight,
        gTileWindowPitch,
        COLOR_DARK_GREY_3);

    if (gTileWindowWidth < 2 || gTileWindowHeight < 2
        || !elevationIsValid(gElevation) || gTileSquares[gElevation] == nullptr) {
        return;
    }

    const int horizonY = gTileWindowHeight / 2;
    bufferFill(gTileWindowBuffer,
        gTileWindowWidth,
        horizonY,
        gTileWindowPitch,
        COLOR_DARK_GREY_3);
    bufferFill(gTileWindowBuffer + horizonY * gTileWindowPitch,
        gTileWindowWidth,
        gTileWindowHeight - horizonY,
        gTileWindowPitch,
        COLOR_DULL_BROWN);

    unsigned char wallCells[SQUARE_GRID_SIZE] = {};
    unsigned char wallOrientations[SQUARE_GRID_SIZE] = {};
    TileFirstPersonGrayboxRenderContext context = {
        gElevation,
        wallCells,
        wallOrientations,
    };
    objectVisitAll(tileFirstPersonGrayboxMarkObject, &context);

    double cameraX = SQUARE_GRID_WIDTH / 2.0;
    double cameraY = SQUARE_GRID_HEIGHT / 2.0;
    int cameraSquareTile = SQUARE_GRID_WIDTH * (SQUARE_GRID_HEIGHT / 2)
        + SQUARE_GRID_WIDTH / 2;
    if (gDude != nullptr && gDude->elevation == gElevation && tileIsValid(gDude->tile)) {
        tileFirstPersonGetWorldPosition(gDude->tile, &cameraX, &cameraY);
        const int playerSquareTile = squareTileFromTile(gDude->tile);
        if (squareGridTileIsValid(playerSquareTile)) {
            cameraSquareTile = playerSquareTile;
        }
    }

    const double yaw = gFirstPersonPreviewYaw * kPi / 180.0;
    const double directionX = sin(yaw);
    const double directionY = -cos(yaw);
    const double cameraPlaneX = -directionY * kFieldOfViewScale;
    const double cameraPlaneY = directionX * kFieldOfViewScale;

    int floorTextureSquareTile = -1;
    int nearestFloorDistance = std::numeric_limits<int>::max();
    for (int squareTile = 0; squareTile < SQUARE_GRID_SIZE; squareTile++) {
        const TileFrameId frameId =
            FloorTileFrmId(gTileSquares[gElevation]->tileFid[squareTile]).frameId<TileFrameId>();
        if (frameId == TileFrameId::Invalid
            || frameId == TileFrameId::Reserved
            || frameId == TileFrameId::Grid) {
            continue;
        }

        const int distance = abs(squareTile % SQUARE_GRID_WIDTH - cameraSquareTile % SQUARE_GRID_WIDTH)
            + abs(squareTile / SQUARE_GRID_WIDTH - cameraSquareTile / SQUARE_GRID_WIDTH);
        if (distance < nearestFloorDistance) {
            nearestFloorDistance = distance;
            floorTextureSquareTile = squareTile;
        }
    }

    FrmImage floorTexture;
    if (floorTextureSquareTile >= 0) {
        const FloorTileFrmId floorFrmId(
            gTileSquares[gElevation]->tileFid[floorTextureSquareTile]);
        floorTexture.lock(FrmId(floorFrmId.frameId<TileFrameId>()));
    }
    const ConstBuffer2D floorTextureFrame = floorTexture.getBuffer();
    if (floorTextureFrame && floorTextureFrame.width > 0 && floorTextureFrame.height > 0) {
        for (int y = horizonY + 1; y < gTileWindowHeight; y++) {
            const double rowDistance = (gTileWindowHeight * 0.5) / (y - horizonY);
            for (int x = 0; x < gTileWindowWidth; x++) {
                const double cameraOffset = 2.0 * x / gTileWindowWidth - 1.0;
                const double rayDirectionX = directionX + cameraPlaneX * cameraOffset;
                const double rayDirectionY = directionY + cameraPlaneY * cameraOffset;
                const double worldX = cameraX + rayDirectionX * rowDistance;
                const double worldY = cameraY + rayDirectionY * rowDistance;
                const double fractionX = worldX - floor(worldX);
                const double fractionY = worldY - floor(worldY);
                const int textureX = std::min(floorTextureFrame.width - 1,
                    static_cast<int>(fractionX * floorTextureFrame.width));
                const int textureY = std::min(floorTextureFrame.height - 1,
                    static_cast<int>(fractionY * floorTextureFrame.height));
                const unsigned char color = floorTextureFrame.data[textureY * floorTextureFrame.width + textureX];
                if (color != 0) {
                    gTileWindowBuffer[y * gTileWindowPitch + x] = color;
                }
            }
        }
    }

    std::vector<double> wallDepths(gTileWindowWidth,
        std::numeric_limits<double>::infinity());

    for (int x = 0; x < gTileWindowWidth; x++) {
        const double cameraOffset = 2.0 * x / gTileWindowWidth - 1.0;
        const double rayDirectionX = directionX + cameraPlaneX * cameraOffset;
        const double rayDirectionY = directionY + cameraPlaneY * cameraOffset;

        int mapX = static_cast<int>(cameraX);
        int mapY = static_cast<int>(cameraY);
        const double deltaDistanceX = rayDirectionX == 0.0
            ? 1.0e30
            : fabs(1.0 / rayDirectionX);
        const double deltaDistanceY = rayDirectionY == 0.0
            ? 1.0e30
            : fabs(1.0 / rayDirectionY);
        const int stepX = rayDirectionX < 0.0 ? -1 : 1;
        const int stepY = rayDirectionY < 0.0 ? -1 : 1;
        double sideDistanceX = (rayDirectionX < 0.0
                ? cameraX - mapX
                : mapX + 1.0 - cameraX)
            * deltaDistanceX;
        double sideDistanceY = (rayDirectionY < 0.0
                ? cameraY - mapY
                : mapY + 1.0 - cameraY)
            * deltaDistanceY;
        int side = 0;
        unsigned char wallType = 0;
        double wallDistance = std::numeric_limits<double>::infinity();
        double cellEntryDistance = 0.0;
        while (mapX >= 0 && mapX < SQUARE_GRID_WIDTH
            && mapY >= 0 && mapY < SQUARE_GRID_HEIGHT) {
            const int cellIndex = mapY * SQUARE_GRID_WIDTH + mapX;
            wallType = wallCells[cellIndex];
            if (wallType != 0) {
                const TileFirstPersonWallOrientation wallOrientation =
                    static_cast<TileFirstPersonWallOrientation>(wallOrientations[cellIndex]);
                if (wallOrientation == TILE_FIRST_PERSON_WALL_ORIENTATION_CELL) {
                    wallDistance = std::max(0.1, cellEntryDistance);
                    break;
                }

                const double cellExitDistance = std::min(sideDistanceX, sideDistanceY);
                constexpr double kIntersectionEpsilon = 1.0e-8;
                constexpr double kWallThickness = 0.2;
                const bool wallRunsNorthSouth =
                    wallOrientation == TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH;
                const double wallMinX = wallRunsNorthSouth
                    ? mapX + 0.5 - kWallThickness / 2.0
                    : mapX;
                const double wallMaxX = wallRunsNorthSouth
                    ? mapX + 0.5 + kWallThickness / 2.0
                    : mapX + 1.0;
                const double wallMinY = wallRunsNorthSouth
                    ? mapY
                    : mapY + 0.5 - kWallThickness / 2.0;
                const double wallMaxY = wallRunsNorthSouth
                    ? mapY + 1.0
                    : mapY + 0.5 + kWallThickness / 2.0;
                double nearX = -std::numeric_limits<double>::infinity();
                double farX = std::numeric_limits<double>::infinity();
                double nearY = -std::numeric_limits<double>::infinity();
                double farY = std::numeric_limits<double>::infinity();
                bool rayIntersectsWallBounds = true;
                if (fabs(rayDirectionX) <= kIntersectionEpsilon) {
                    rayIntersectsWallBounds = cameraX >= wallMinX - kIntersectionEpsilon
                        && cameraX <= wallMaxX + kIntersectionEpsilon;
                } else {
                    nearX = (wallMinX - cameraX) / rayDirectionX;
                    farX = (wallMaxX - cameraX) / rayDirectionX;
                    if (nearX > farX) {
                        std::swap(nearX, farX);
                    }
                }

                if (rayIntersectsWallBounds) {
                    if (fabs(rayDirectionY) <= kIntersectionEpsilon) {
                        rayIntersectsWallBounds = cameraY >= wallMinY - kIntersectionEpsilon
                            && cameraY <= wallMaxY + kIntersectionEpsilon;
                    } else {
                        nearY = (wallMinY - cameraY) / rayDirectionY;
                        farY = (wallMaxY - cameraY) / rayDirectionY;
                        if (nearY > farY) {
                            std::swap(nearY, farY);
                        }
                    }
                }

                const double wallEntryDistance = std::max(nearX, nearY);
                const double wallExitDistance = std::min(farX, farY);
                const double clippedWallEntryDistance =
                    std::max(cellEntryDistance, wallEntryDistance);
                if (rayIntersectsWallBounds
                    && wallExitDistance >= clippedWallEntryDistance - kIntersectionEpsilon
                    && clippedWallEntryDistance <= cellExitDistance + kIntersectionEpsilon) {
                    wallDistance = std::max(0.1, clippedWallEntryDistance);
                    if (wallEntryDistance >= cellEntryDistance - kIntersectionEpsilon) {
                        side = nearX > nearY ? 0 : 1;
                    }
                    break;
                }
            }

            cellEntryDistance = std::min(sideDistanceX, sideDistanceY);
            if (sideDistanceX < sideDistanceY) {
                sideDistanceX += deltaDistanceX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistanceY += deltaDistanceY;
                mapY += stepY;
                side = 1;
            }

            if (mapX < 0 || mapX >= SQUARE_GRID_WIDTH
                || mapY < 0 || mapY >= SQUARE_GRID_HEIGHT) {
                break;
            }

        }

        if (wallType == 0
            || wallDistance == std::numeric_limits<double>::infinity()) {
            continue;
        }

        wallDepths[x] = wallDistance;
        const int wallHeight = std::min(gTileWindowHeight,
            static_cast<int>(gTileWindowHeight / wallDistance));
        const int wallTop = std::max(0, horizonY - wallHeight / 2);
        const int wallBottom = std::min(gTileWindowHeight - 1, horizonY + wallHeight / 2);
        Color wallColor = COLOR_LIGHT_RED;
        switch (wallType) {
        case 3:
            wallColor = COLOR_LIGHT_GOLD;
            break;
        case 4:
            wallColor = COLOR_YELLOW_2;
            break;
        default:
            break;
        }

        if (side == 1) {
            switch (wallType) {
            case 1:
                wallColor = COLOR_DARK_RED;
                break;
            case 3:
                wallColor = COLOR_MAGENTA;
                break;
            case 4:
                wallColor = COLOR_LIGHT_GREEN_2;
                break;
            default:
                break;
            }
        }
        bufferDrawLine(gTileWindowBuffer,
            gTileWindowPitch,
            x,
            wallTop,
            x,
            wallBottom,
            wallColor);
    }

    TileFirstPersonActorRenderContext actorContext = {
        gElevation,
        cameraX,
        cameraY,
        directionX,
        directionY,
        cameraPlaneX,
        cameraPlaneY,
        horizonY,
        &wallDepths,
    };
    objectVisitAll(tileFirstPersonRenderActor, &actorContext);
    tileFirstPersonDrawCrosshair();
}

static void tileFirstPersonGrayboxMarkObject(Object* object, void* context)
{
    const TileFirstPersonGrayboxRenderContext* renderContext =
        static_cast<const TileFirstPersonGrayboxRenderContext*>(context);
    if (object->elevation != renderContext->elevation
        || (object->flags & OBJECT_HIDDEN) != OBJECT_NONE
        || !tileIsValid(object->tile)) {
        return;
    }

    const ObjectType objectType = FrmId(object).objectType();
    if (objectType != OBJ_TYPE_WALL && objectType != OBJ_TYPE_SCENERY) {
        return;
    }

    if ((object->flags & (OBJECT_NO_BLOCK | OBJECT_LIGHT_THRU)) != OBJECT_NONE) {
        return;
    }

    const int squareTile = squareTileFromTile(object->tile);
    if (!squareGridTileIsValid(squareTile)) {
        return;
    }

    unsigned char wallType = 0;
    TileFirstPersonWallOrientation wallOrientation =
        TILE_FIRST_PERSON_WALL_ORIENTATION_CELL;
    if (objectType == OBJ_TYPE_WALL) {
        wallType = 1;
        Proto* proto = nullptr;
        if (protoGetProto(object, &proto) == 0) {
            constexpr unsigned int kCornerFlags = PROTO_EXT_FLAG_NORTH_CORNER
                | PROTO_EXT_FLAG_SOUTH_CORNER
                | PROTO_EXT_FLAG_EAST_CORNER
                | PROTO_EXT_FLAG_WEST_CORNER;
            const unsigned int extendedFlags = proto->wall.extendedFlags;
            if ((extendedFlags & kCornerFlags) == 0) {
                wallOrientation = (extendedFlags & PROTO_EXT_FLAG_HIDDEN) != 0
                    ? TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST
                    : TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH;
            }
        }
    } else {
        Proto* proto;
        if (protoGetProto(object, &proto) == 0 && proto->scenery.type == SCENERY_TYPE_DOOR) {
            wallType = 3;
        } else {
            wallType = 4;
        }
    }

    unsigned char& cellWallType = renderContext->wallCells[squareTile];
    if (cellWallType == 0 || wallType == 3) {
        cellWallType = wallType;
        renderContext->wallOrientations[squareTile] = wallOrientation;
    }
}

static void tileFirstPersonRenderActor(Object* object, void* context)
{
    const TileFirstPersonActorRenderContext* renderContext =
        static_cast<const TileFirstPersonActorRenderContext*>(context);
    if (object == gDude
        || object->elevation != renderContext->elevation
        || (object->flags & OBJECT_HIDDEN) != OBJECT_NONE
        || !tileIsValid(object->tile)
        || FrmId(object).objectType() != OBJ_TYPE_CRITTER) {
        return;
    }

    double objectX;
    double objectY;
    if (!tileFirstPersonGetWorldPosition(object->tile, &objectX, &objectY)) {
        return;
    }

    const double relativeX = objectX - renderContext->cameraX;
    const double relativeY = objectY - renderContext->cameraY;
    const double determinant = renderContext->cameraPlaneX * renderContext->directionY
        - renderContext->directionX * renderContext->cameraPlaneY;
    if (determinant == 0.0) {
        return;
    }

    const double inverseDeterminant = 1.0 / determinant;
    const double transformX = inverseDeterminant
        * (renderContext->directionY * relativeX - renderContext->directionX * relativeY);
    const double transformY = inverseDeterminant
        * (-renderContext->cameraPlaneY * relativeX + renderContext->cameraPlaneX * relativeY);
    if (transformY <= 0.1) {
        return;
    }

    const int screenCenterX = static_cast<int>(
        (gTileWindowWidth / 2.0) * (1.0 + transformX / transformY));
    const double projectionScale = gTileWindowHeight / transformY;
    const int spriteHeight = std::min(gTileWindowHeight * 2,
        std::max(1, static_cast<int>(projectionScale * 0.9)));
    const int spriteWidth = std::max(1, static_cast<int>(projectionScale * 0.42));
    const int spriteTop = renderContext->horizonY - spriteHeight / 2;
    const int spriteBottom = spriteTop + spriteHeight;
    const int spriteLeft = screenCenterX - spriteWidth / 2;
    const int spriteRight = spriteLeft + spriteWidth;
    Color actorColor = COLOR_AMBER;
    if (objectIsPartyMember(object)
        || (gDude != nullptr
            && object->data.critter.combat.team == gDude->data.critter.combat.team)) {
        actorColor = COLOR_LIGHT_GREEN_3;
    } else if (isInCombat()) {
        actorColor = COLOR_LIGHT_RED;
    }

    Rotation facingRotation = rotationIsValid(object->rotation)
        ? object->rotation
        : ROTATION_NE;
    {
        double viewX = renderContext->cameraX - objectX;
        double viewY = renderContext->cameraY - objectY;
        const double viewLength = sqrt(viewX * viewX + viewY * viewY);
        if (viewLength > 0.0) {
            viewX /= viewLength;
            viewY /= viewLength;

            double bestDot = -std::numeric_limits<double>::infinity();
            for (int rotationIndex = ROTATION_FIRST; rotationIndex < ROTATION_COUNT; rotationIndex++) {
                const Rotation rotation = static_cast<Rotation>(rotationIndex);
                const int adjacentTile = tileGetTileInDirection(object->tile, rotation, 1);
                double adjacentX;
                double adjacentY;
                if (!tileFirstPersonGetWorldPosition(adjacentTile, &adjacentX, &adjacentY)) {
                    continue;
                }

                const double directionX = adjacentX - objectX;
                const double directionY = adjacentY - objectY;
                const double directionLength = sqrt(directionX * directionX + directionY * directionY);
                if (directionLength <= 0.0) {
                    continue;
                }

                const double dot = (directionX * viewX + directionY * viewY) / directionLength;
                if (dot > bestDot) {
                    bestDot = dot;
                    facingRotation = rotation;
                }
            }
        }
    }

    FrmImage image;
    if (image.lock(FrmId(object), object->frame, facingRotation)) {
        const ConstBuffer2D frame = image.getBuffer();
        if (frame.width <= 0 || frame.height <= 0) {
            return;
        }
        const int artHeight = std::min(gTileWindowHeight * 2,
            std::max(1, static_cast<int>(projectionScale * 0.9)));
        const int artWidth = std::max(1,
            static_cast<int>(artHeight * frame.width / static_cast<double>(frame.height)));
        const int artBottom = renderContext->horizonY
            + static_cast<int>(projectionScale * 0.42);
        const int artTop = artBottom - artHeight;
        const int artLeft = screenCenterX - artWidth / 2;

        for (int x = std::max(0, artLeft); x < std::min(gTileWindowWidth, artLeft + artWidth); x++) {
            if (transformY >= (*renderContext->wallDepths)[x]) {
                continue;
            }

            const int sourceX = (x - artLeft) * frame.width / artWidth;
            for (int y = std::max(0, artTop); y < std::min(gTileWindowHeight, artBottom); y++) {
                const int sourceY = (y - artTop) * frame.height / artHeight;
                const unsigned char color = frame.data[sourceY * frame.width + sourceX];
                if (color != 0) {
                    gTileWindowBuffer[y * gTileWindowPitch + x] = color;
                }
            }
        }

        const int markerY = std::max(0, artTop - 3);
        const int markerLeft = std::max(0, screenCenterX - 2);
        const int markerRight = std::min(gTileWindowWidth - 1, screenCenterX + 2);
        if (markerY < gTileWindowHeight && markerLeft <= markerRight) {
            bufferDrawLine(gTileWindowBuffer,
                gTileWindowPitch,
                markerLeft,
                markerY,
                markerRight,
                markerY,
                actorColor);
        }
        return;
    }

    for (int x = std::max(0, spriteLeft); x < std::min(gTileWindowWidth, spriteRight); x++) {
        if (transformY >= (*renderContext->wallDepths)[x]) {
            continue;
        }

        const double normalizedX = (x - spriteLeft) / static_cast<double>(spriteWidth);
        for (int y = std::max(0, spriteTop); y < std::min(gTileWindowHeight, spriteBottom); y++) {
            const double normalizedY = (y - spriteTop) / static_cast<double>(spriteHeight);
            double silhouetteWidth;
            if (normalizedY < 0.2) {
                silhouetteWidth = 0.2;
            } else if (normalizedY < 0.3) {
                silhouetteWidth = 0.1;
            } else if (normalizedY < 0.72) {
                silhouetteWidth = 0.46;
            } else {
                silhouetteWidth = 0.34;
            }

            const double centerDistance = fabs(normalizedX - 0.5);
            if (centerDistance > silhouetteWidth / 2.0) {
                continue;
            }

            gTileWindowBuffer[y * gTileWindowPitch + x] = actorColor;
        }
    }
}

static bool tileFirstPersonGetWorldPosition(int tile, double* x, double* y)
{
    if (!tileIsValid(tile)) {
        return false;
    }

    *x = (tile % gHexGridWidth) / 2.0 + 0.25;
    *y = (tile / gHexGridWidth) / 2.0 + 0.25;
    return true;
}

static void tileFirstPersonDrawCrosshair()
{
    const int centerX = gTileWindowWidth / 2;
    const int centerY = gTileWindowHeight / 2;
    bufferDrawLine(gTileWindowBuffer,
        gTileWindowPitch,
        std::max(0, centerX - 5),
        centerY,
        std::max(0, centerX - 2),
        centerY,
        COLOR_WHITE);
    bufferDrawLine(gTileWindowBuffer,
        gTileWindowPitch,
        std::min(gTileWindowWidth - 1, centerX + 2),
        centerY,
        std::min(gTileWindowWidth - 1, centerX + 5),
        centerY,
        COLOR_WHITE);
    bufferDrawLine(gTileWindowBuffer,
        gTileWindowPitch,
        centerX,
        std::max(0, centerY - 5),
        centerX,
        std::max(0, centerY - 2),
        COLOR_WHITE);
    bufferDrawLine(gTileWindowBuffer,
        gTileWindowPitch,
        centerX,
        std::min(gTileWindowHeight - 1, centerY + 2),
        centerX,
        std::min(gTileWindowHeight - 1, centerY + 5),
        COLOR_WHITE);
}

static void tileFirstPersonDrawDiagnosticLine(int x1, int y1, int x2, int y2, Color color)
{
    const int deltaX = abs(x2 - x1);
    const int stepX = x1 < x2 ? 1 : -1;
    const int deltaY = -abs(y2 - y1);
    const int stepY = y1 < y2 ? 1 : -1;
    int error = deltaX + deltaY;

    while (true) {
        if (x1 >= 0 && x1 < gTileWindowWidth && y1 >= 0 && y1 < gTileWindowHeight) {
            gTileWindowBuffer[y1 * gTileWindowPitch + x1] = color;
        }
        if (x1 == x2 && y1 == y2) {
            break;
        }

        const int doubledError = 2 * error;
        if (doubledError >= deltaY) {
            error += deltaY;
            x1 += stepX;
        }
        if (doubledError <= deltaX) {
            error += deltaX;
            y1 += stepY;
        }
    }
}

static void tileRenderFirstPersonTopology()
{
    bufferFill(gTileWindowBuffer,
        gTileWindowWidth,
        gTileWindowHeight,
        gTileWindowPitch,
        COLOR_BLACK);

    if (gTileWindowWidth < SQUARE_GRID_WIDTH || gTileWindowHeight < SQUARE_GRID_HEIGHT
        || !elevationIsValid(gElevation) || gTileSquares[gElevation] == nullptr) {
        return;
    }

    const int cellSize = std::min(
        (gTileWindowWidth - 16) / SQUARE_GRID_WIDTH,
        (gTileWindowHeight - 16) / SQUARE_GRID_HEIGHT);
    if (cellSize <= 0) {
        return;
    }

    const int mapWidth = cellSize * SQUARE_GRID_WIDTH;
    const int mapHeight = cellSize * SQUARE_GRID_HEIGHT;
    const int mapLeft = (gTileWindowWidth - mapWidth) / 2;
    const int mapTop = (gTileWindowHeight - mapHeight) / 2;

    for (int squareTile = 0; squareTile < SQUARE_GRID_SIZE; squareTile++) {
        const FloorTileFrmId floorFid(gTileSquares[gElevation]->tileFid[squareTile]);
        if (floorFid.frameId<TileFrameId>() == TileFrameId::Invalid) {
            continue;
        }

        const int x = mapLeft + (squareTile % SQUARE_GRID_WIDTH) * cellSize;
        const int y = mapTop + (squareTile / SQUARE_GRID_WIDTH) * cellSize;
        bufferFill(gTileWindowBuffer + y * gTileWindowPitch + x,
            cellSize - 1,
            cellSize - 1,
            gTileWindowPitch,
            COLOR_DARK_GREY_2);
    }

    TileFirstPersonTopologyRenderContext context = {
        gElevation,
        mapLeft,
        mapTop,
        cellSize,
    };
    objectVisitAll(tileFirstPersonTopologyMarkObject, &context);

    if (gDude != nullptr && gDude->elevation == gElevation && tileIsValid(gDude->tile)) {
        const int squareTile = squareTileFromTile(gDude->tile);
        if (squareGridTileIsValid(squareTile)) {
            const int cellX = mapLeft + (squareTile % SQUARE_GRID_WIDTH) * cellSize;
            const int cellY = mapTop + (squareTile / SQUARE_GRID_WIDTH) * cellSize;
            bufferFill(gTileWindowBuffer + (cellY + (cellSize - 1) / 2) * gTileWindowPitch
                    + cellX + (cellSize - 1) / 2,
                1,
                1,
                gTileWindowPitch,
                COLOR_WHITE);
        }
    }
}

static void tileFirstPersonTopologyMarkObject(Object* object, void* context)
{
    const TileFirstPersonTopologyRenderContext* renderContext =
        static_cast<const TileFirstPersonTopologyRenderContext*>(context);
    if (object->elevation != renderContext->elevation
        || (object->flags & OBJECT_HIDDEN) != OBJECT_NONE
        || !tileIsValid(object->tile)) {
        return;
    }

    const ObjectType objectType = FrmId(object).objectType();
    if (artIsObjectTypeHidden(objectType)) {
        return;
    }

    const int squareTile = squareTileFromTile(object->tile);
    if (!squareGridTileIsValid(squareTile)) {
        return;
    }

    const int cellX = renderContext->mapLeft + (squareTile % SQUARE_GRID_WIDTH) * renderContext->cellSize;
    const int cellY = renderContext->mapTop + (squareTile / SQUARE_GRID_WIDTH) * renderContext->cellSize;

    Color markerColor = COLOR_LIGHT_GREY;
    Proto* proto = nullptr;
    const bool hasProto = protoGetProto(object, &proto) == 0;
    ProtoExtendedFlags cornerFlags = PROTO_EXT_FLAG_NONE;
    switch (objectType) {
    case OBJ_TYPE_WALL:
        markerColor = (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
            ? COLOR_LIGHT_RED
            : COLOR_AMBER;
        if (hasProto) {
            cornerFlags = proto->wall.extendedFlags;
        }
        break;
    case OBJ_TYPE_SCENERY:
        if (hasProto && proto->scenery.type == SCENERY_TYPE_DOOR) {
            markerColor = (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
                ? COLOR_LIGHT_RED
                : COLOR_LIGHT_GREEN_3;
        } else {
            markerColor = (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
                ? COLOR_LIGHT_GOLD
                : COLOR_YELLOW_2;
        }
        if (hasProto) {
            cornerFlags = proto->scenery.extendedFlags;
        }
        break;
    case OBJ_TYPE_CRITTER:
        markerColor = COLOR_LIGHT_GREEN_3;
        break;
    case OBJ_TYPE_ITEM:
        markerColor = COLOR_CYAN;
        break;
    default:
        break;
    }

    const int markerSize = std::min(renderContext->cellSize - 1, 3);
    if (markerSize > 0) {
        bufferFill(gTileWindowBuffer
                + (cellY + (renderContext->cellSize - markerSize) / 2) * gTileWindowPitch
                + cellX + (renderContext->cellSize - markerSize) / 2,
            markerSize,
            markerSize,
            gTileWindowPitch,
            markerColor);
    }

    if (objectType != OBJ_TYPE_WALL && objectType != OBJ_TYPE_SCENERY) {
        return;
    }

    if (rotationIsValid(object->rotation)) {
        static constexpr int kRotationX[ROTATION_COUNT] = { 1, 1, 1, -1, -1, -1 };
        static constexpr int kRotationY[ROTATION_COUNT] = { -1, 0, 1, 1, 0, -1 };
        const int centerX = cellX + renderContext->cellSize / 2;
        const int centerY = cellY + renderContext->cellSize / 2;
        const int arrowLength = std::max(1, (renderContext->cellSize - 1) / 2);
        const int arrowX = centerX + kRotationX[object->rotation] * arrowLength;
        const int arrowY = centerY + kRotationY[object->rotation] * arrowLength;
        Color rotationColor;
        switch (object->rotation) {
        case ROTATION_NE:
            rotationColor = COLOR_MAGENTA;
            break;
        case ROTATION_E:
            rotationColor = COLOR_CYAN;
            break;
        case ROTATION_SE:
            rotationColor = COLOR_LIGHT_GREEN_3;
            break;
        case ROTATION_SW:
            rotationColor = COLOR_BLUE;
            break;
        case ROTATION_W:
            rotationColor = COLOR_LIGHT_PINK;
            break;
        case ROTATION_NW:
            rotationColor = COLOR_WHITE;
            break;
        default:
            rotationColor = COLOR_WHITE;
            break;
        }
        tileFirstPersonDrawDiagnosticLine(centerX, centerY, arrowX, arrowY, rotationColor);
    }

    constexpr unsigned int kCornerFlags = PROTO_EXT_FLAG_EAST_CORNER
        | PROTO_EXT_FLAG_NORTH_CORNER
        | PROTO_EXT_FLAG_SOUTH_CORNER
        | PROTO_EXT_FLAG_WEST_CORNER;
    const unsigned int cornerFlagBits = static_cast<unsigned int>(cornerFlags) & kCornerFlags;
    if (cornerFlagBits != 0) {
        Color cornerColor = COLOR_MAGENTA;
        if (cornerFlagBits == PROTO_EXT_FLAG_EAST_CORNER) {
            cornerColor = COLOR_CYAN;
        } else if (cornerFlagBits == PROTO_EXT_FLAG_NORTH_CORNER) {
            cornerColor = COLOR_YELLOW_2;
        } else if (cornerFlagBits == PROTO_EXT_FLAG_SOUTH_CORNER) {
            cornerColor = COLOR_MAGENTA;
        } else if (cornerFlagBits == PROTO_EXT_FLAG_WEST_CORNER) {
            cornerColor = COLOR_BLUE;
        } else {
            cornerColor = COLOR_WHITE;
        }

        const int cornerSize = std::min(2, renderContext->cellSize - 1);
        const int cornerX = cellX + renderContext->cellSize - cornerSize;
        const int cornerY = cellY;
        if (cornerSize > 0 && cornerX >= 0 && cornerX + cornerSize <= gTileWindowWidth
            && cornerY >= 0 && cornerY + cornerSize <= gTileWindowHeight) {
            bufferFill(gTileWindowBuffer + cornerY * gTileWindowPitch + cornerX,
                cornerSize,
                cornerSize,
                gTileWindowPitch,
                cornerColor);
        }
    }
}

bool tileFirstPersonPreviewIsEnabled()
{
    return gFirstPersonPreviewEnabled;
}

void tileFirstPersonPreviewRefresh()
{
    if (!gFirstPersonPreviewEnabled || gTileWindowBuffer == nullptr || gTileWindowRefreshProc == nullptr) {
        return;
    }

    if (gFirstPersonPreviewTopologyEnabled) {
        tileRenderFirstPersonTopology();
    } else {
        tileRenderFirstPersonPreview();
    }
    gTileWindowRefreshProc(&gTileWindowRect);
}

void tileFirstPersonPreviewToggleTopology()
{
    if (!gFirstPersonPreviewEnabled) {
        return;
    }

    gFirstPersonPreviewTopologyEnabled = !gFirstPersonPreviewTopologyEnabled;
    tileFirstPersonPreviewRefresh();
}

void tileFirstPersonPreviewLook(int deltaX)
{
    if (!gFirstPersonPreviewEnabled || gFirstPersonPreviewTopologyEnabled || deltaX == 0) {
        return;
    }

    gFirstPersonPreviewYaw = (gFirstPersonPreviewYaw + deltaX) % 360;
    if (gFirstPersonPreviewYaw < 0) {
        gFirstPersonPreviewYaw += 360;
    }

    tileFirstPersonPreviewRefresh();
}

void tileFirstPersonPreviewToggle()
{
    if (gTileWindowBuffer == nullptr || gTileWindowRefreshProc == nullptr) {
        return;
    }

    gFirstPersonPreviewEnabled = !gFirstPersonPreviewEnabled;
    if (gFirstPersonPreviewEnabled) {
        gFirstPersonPreviewTopologyEnabled = false;
        gFirstPersonPreviewYaw = 0;
        if (gDude != nullptr && tileIsValid(gDude->tile) && rotationIsValid(gDude->rotation)) {
            const int playerSquareTile = squareTileFromTile(gDude->tile);
            for (int distance = 1; distance <= 4; distance++) {
                const int facingTile = tileGetTileInDirection(gDude->tile, gDude->rotation, distance);
                const int facingSquareTile = squareTileFromTile(facingTile);
                if (!squareGridTileIsValid(playerSquareTile)
                    || !squareGridTileIsValid(facingSquareTile)) {
                    continue;
                }

                const int deltaX = facingSquareTile % SQUARE_GRID_WIDTH - playerSquareTile % SQUARE_GRID_WIDTH;
                const int deltaY = facingSquareTile / SQUARE_GRID_WIDTH - playerSquareTile / SQUARE_GRID_WIDTH;
                if (deltaX != 0 || deltaY != 0) {
                    constexpr double kPi = 3.14159265358979323846;
                    gFirstPersonPreviewYaw = static_cast<int>(atan2(deltaX, -deltaY) * 180.0 / kPi);
                    if (gFirstPersonPreviewYaw < 0) {
                        gFirstPersonPreviewYaw += 360;
                    }
                    break;
                }
            }
        }
        tileFirstPersonPreviewRefresh();
    } else {
        tileRefreshGame(&gTileWindowRect, gElevation);
    }
}

// 0x4B1634 tile_toggle_roof
void tile_toggle_roof(bool refresh)
{
    gTileRoofIsVisible = !gTileRoofIsVisible;

    if (refresh) {
        // NOTE: Uninline.
        tileWindowRefresh();
    }
}

// 0x4B166C tile_roof_visible
int tileRoofIsVisible()
{
    return gTileRoofIsVisible;
}

// 0x4B1674 tile_coord
int tileToScreenXY(int tile, int* screenX, int* screenY)
{
    int v3;
    int v4;
    int v5;
    int v6;

    *screenX = 0;
    *screenY = 0;

    if (!tileIsValid(tile)) {
        return -1;
    }

    v3 = gHexGridWidth - 1 - tile % gHexGridWidth;
    v4 = tile / gHexGridWidth;

    *screenX = _tile_offx;
    *screenY = _tile_offy;

    v5 = (v3 - _tile_x) / -2;
    *screenX += 48 * ((v3 - _tile_x) / 2);
    *screenY += 12 * v5;

    if (v3 & 1) {
        if (v3 <= _tile_x) {
            *screenX -= 16;
            *screenY += 12;
        } else {
            *screenX += 32;
        }
    }

    v6 = v4 - _tile_y;
    *screenX += 16 * v6;
    *screenY += 12 * v6;

    return 0;
}

// CE: Added optional `ignoreBounds` param to return tile number without
// validating hex grid bounds. The resulting invalid tile number serves as an
// origin for calculations using prepared offsets table during objects
// rendering.
// 0x4B1754 tile_num
int tileFromScreenXY(int screenX, int screenY, bool ignoreBounds)
{
    int x, y;

    int yTileOff = screenY - _tile_offy;
    if (yTileOff >= 0) {
        y = yTileOff / 12;
    } else {
        y = (yTileOff + 1) / 12 - 1;
    }

    int xTileOff = screenX - _tile_offx - 16 * y;
    int yOffset = yTileOff - (y * 12);

    if (xTileOff >= 0) {
        x = xTileOff / 64;
    } else {
        x = (xTileOff + 1) / 64 - 1;
    }

    int xOffset = xTileOff - (x * 64);

    int tY = x + y;
    int tX = 2 * x;

    if (xOffset >= 32) {
        xOffset -= 32;
        tX++;
    }

    int xTile = _tile_x + tX;
    int yTile = _tile_y + tY;

    switch (_tile_mask[(32 * yOffset) + xOffset]) {
    case 1:
        yTile--;
        break;
    case 2:
        xTile++;
        if (xTile & 1) {
            yTile--;
        }
        break;
    case 3:
        xTile--;
        if (!(xTile & 1)) {
            yTile++;
        }
        break;
    case 4:
        yTile++;
        break;
    default:
        break;
    }

    int xPos = gHexGridWidth - 1 - xTile;
    if (ignoreBounds
        || (xPos >= 0 && xPos < gHexGridWidth && yTile >= 0 && yTile < gHexGridHeight)) {
        return (gHexGridWidth * yTile) + xPos;
    } else {
        return -1;
    }
}

int squareTileFromTile(int tile)
{
    if (!tileIsValid(tile)) {
        return -1;
    }

    int tileCol = gHexGridWidth - 1 - tile % gHexGridWidth;
    int tileRow = tile / gHexGridWidth;
    int squareCol = tileCol / 2;
    int squareRow = tileRow / 2;

    return (gSquareGridWidth - 1 - squareCol) + gSquareGridWidth * squareRow;
}

// tile_distance
// 0x4B185C tile_dist
int tileDistanceBetween(int tile1, int tile2)
{
    if (!tileIsValid(tile1) || !tileIsValid(tile2)) {
        return TILE_MAX_DISTANCE;
    }

    int distance = 0;
    int curTile = tile1;
    for (; curTile != tile2 && distance < TILE_MAX_DISTANCE; distance++) {
        int dir = tileGetRotationTo(curTile, tile2);

        curTile += _dir_tile[curTile % gHexGridWidth & 1][dir];
    }

    return distance;
}

// 0x4B1994 tile_in_front_of
bool tileIsInFrontOf(int tile1, int tile2)
{
    int x1, y1;
    tileToScreenXY(tile1, &x1, &y1);

    int x2, y2;
    tileToScreenXY(tile2, &x2, &y2);

    int dx = x2 - x1;
    int dy = y2 - y1;

    return (double)dx <= (double)dy * dbl_50E7C7;
}

// 0x4B1A00 tile_to_right_of
bool tileIsToRightOf(int tile1, int tile2)
{
    int x1, y1;
    tileToScreenXY(tile1, &x1, &y1);

    int x2, y2;
    tileToScreenXY(tile2, &x2, &y2);

    int dx = x2 - x1;
    int dy = y2 - y1;

    // NOTE: the value below looks like 4/3, which is 0x3FF55555555555, but it's
    // binary value is slightly different: 0x3FF55555555556. This difference plays
    // important role as seen right in the beginning of the game, comparing tiles
    // 17488 (0x4450) and 15288 (0x3BB8).
    return (double)dx <= (double)dy * 1.3333333333333335;
}

// tile_num_in_direction
// 0x4B1A6C tile_num_in_direction
int tileGetTileInDirection(int tile, Rotation rotation, int distance)
{
    int newTile = tile;
    for (int index = 0; index < distance; index++) {
        if (tileIsEdge(newTile)) {
            break;
        }

        int parity = (newTile % gHexGridWidth) & 1;
        newTile += _dir_tile[parity][rotation];
    }

    return newTile;
}

// rotation_to_tile
// 0x4B1ABC tile_dir
Rotation tileGetRotationTo(int tile1, int tile2)
{
    int x1, y1;
    tileToScreenXY(tile1, &x1, &y1);

    int x2, y2;
    tileToScreenXY(tile2, &x2, &y2);

    int dy = y2 - y1;
    int dx = x2 - x1;

    if (dx != 0) {
        int raw = (int)trunc(atan2((double)-dy, (double)dx) * 180.0 / M_PI); // radians -> degrees
        int angle = 360 - (raw + 180) - 90;
        if (angle < 0) {
            angle += 360;
        }

        angle /= 60; // convert from degrees to hex direction

        if (angle >= ROTATION_COUNT) {
            angle = ROTATION_NW;
        }
        return static_cast<Rotation>(angle);
    }

    return dy < 0 ? ROTATION_NE : ROTATION_SE;
}

// 0x4B1B84 tile_num_beyond
int _tile_num_beyond(int from, int to, int distance)
{
    if (distance <= 0 || from == to) {
        return from;
    }

    int fromX, fromY;
    tileToScreenXY(from, &fromX, &fromY);
    fromX += 16;
    fromY += 8;

    int toX, toY;
    tileToScreenXY(to, &toX, &toY);
    toX += 16;
    toY += 8;

    int deltaX = toX - fromX;
    int deltaY = toY - fromY;

    int v27 = 2 * abs(deltaX);

    int stepX = 0;
    if (deltaX > 0)
        stepX = 1;
    else if (deltaX < 0)
        stepX = -1;

    int v26 = 2 * abs(deltaY);

    int stepY = 0;
    if (deltaY > 0)
        stepY = 1;
    else if (deltaY < 0)
        stepY = -1;

    int v28 = from;
    int tileX = fromX;
    int tileY = fromY;

    int v6 = 0;

    if (v27 > v26) {
        int middle = v26 - v27 / 2;
        while (true) {
            int tile = tileFromScreenXY(tileX, tileY);
            if (tile != v28) {
                v6 += 1;
                if (v6 == distance || tileIsEdge(tile)) {
                    return tile;
                }

                v28 = tile;
            }

            if (middle >= 0) {
                middle -= v27;
                tileY += stepY;
            }

            middle += v26;
            tileX += stepX;
        }
    } else {
        int middle = v27 - v26 / 2;
        while (true) {
            int tile = tileFromScreenXY(tileX, tileY);
            if (tile != v28) {
                v6 += 1;
                if (v6 == distance || tileIsEdge(tile)) {
                    return tile;
                }

                v28 = tile;
            }

            if (middle >= 0) {
                middle -= v26;
                tileX += stepX;
            }

            middle += v27;
            tileY += stepY;
        }
    }

    assert(false && "Should be unreachable");
}

// 0x4B1D20 tile_on_edge
bool tileIsEdge(int tile)
{
    if (!tileIsValid(tile)) {
        return false;
    }

    if (tile < gHexGridWidth) {
        return true;
    }

    if (tile >= gHexGridSize - gHexGridWidth) {
        return true;
    }

    if (tile % gHexGridWidth == 0) {
        return true;
    }

    if (tile % gHexGridWidth == gHexGridWidth - 1) {
        return true;
    }

    return false;
}

// 0x4B1D80 tile_enable_scroll_blocking
void tileScrollBlockingEnable()
{
    gTileScrollBlockingEnabled = true;
}

// 0x4B1D8C tile_disable_scroll_blocking
void tileScrollBlockingDisable()
{
    gTileScrollBlockingEnabled = false;
}

// 0x4B1D98 tile_get_scroll_blocking
bool tileScrollBlockingIsEnabled()
{
    return gTileScrollBlockingEnabled;
}

// 0x4B1DA0 tile_enable_scroll_limiting
void tileScrollLimitingEnable()
{
    gTileScrollLimitingEnabled = true;
}

// 0x4B1DAC tile_disable_scroll_limiting
void tileScrollLimitingDisable()
{
    gTileScrollLimitingEnabled = false;
}

// 0x4B1DB8 tile_get_scroll_limiting
bool tileScrollLimitingIsEnabled()
{
    return gTileScrollLimitingEnabled;
}

// 0x4B1DC0 square_coord
int squareTileToScreenXY(int squareTile, int* coordX, int* coordY, MapElevation elevation)
{
    *coordX = 0;
    *coordY = 0;

    if (squareTile < 0 || squareTile >= gSquareGridSize) {
        return -1;
    }

    int tileCol = gSquareGridWidth - 1 - squareTile % gSquareGridWidth;
    int tileRow = squareTile / gSquareGridWidth;

    *coordX = _square_offx;
    *coordY = _square_offy;

    int colDelta = tileCol - _square_x;
    *coordX += 48 * colDelta;
    *coordY -= 12 * colDelta;

    int rowDelta = tileRow - _square_y;
    *coordX += 32 * rowDelta;
    *coordY += 24 * rowDelta;

    return 0;
}

// 0x4B1E60 square_coord_roof
int squareTileToRoofScreenXY(int squareTile, int* screenX, int* screenY, MapElevation elevation)
{
    int v5;
    int v6;
    int v7;
    int v8;
    int v9;
    int v10;

    *screenX = 0;
    *screenY = 0;

    if (squareTile < 0 || squareTile >= gSquareGridSize) {
        return -1;
    }

    v5 = gSquareGridWidth - 1 - squareTile % gSquareGridWidth;
    v6 = squareTile / gSquareGridWidth;
    v7 = _square_x;
    *screenX = _square_offx;
    *screenY = _square_offy;

    v8 = v5 - v7;
    *screenX += 48 * v8;
    *screenY -= 12 * v8;

    v9 = v6 - _square_y;
    *screenX += 32 * v9;
    v10 = 24 * v9 + *screenY;
    *screenY = v10;
    *screenY = v10 - 96;

    return 0;
}

// 0x4B1F04 square_num
int squareTileFromScreenXY(int screenX, int screenY, MapElevation elevation)
{
    int coordY;
    int coordX;

    squareTileScreenToCoord(screenX, screenY, elevation, &coordX, &coordY);

    if (coordX >= 0 && coordX < gSquareGridWidth && coordY >= 0 && coordY < gSquareGridHeight) {
        return coordX + gSquareGridWidth * coordY;
    }

    return -1;
}

// 0x4B1F94 square_xy
void squareTileScreenToCoord(int screenX, int screenY, MapElevation elevation, int* coordX, int* coordY)
{
    int v4;
    int v5;
    int v6;
    int v8;

    v4 = screenX - _square_offx;
    v5 = screenY - _square_offy - 12;
    v6 = 3 * v4 - 4 * v5;
    *coordX = v6 >= 0 ? (v6 / 192) : ((v6 + 1) / 192 - 1);

    v8 = 4 * v5 + v4;
    *coordY = v8 >= 0 ? (v8 / 128) : ((v8 + 1) / 128 - 1);

    *coordX += _square_x;
    *coordY += _square_y;

    *coordX = gSquareGridWidth - 1 - *coordX;
}

// 0x4B203C square_xy_roof
void squareTileScreenToCoordRoof(int screenX, int screenY, MapElevation elevation, int* coordX, int* coordY)
{
    int v4;
    int v5;
    int v6;
    int v8;

    v4 = screenX - _square_offx;
    v5 = screenY + 96 - _square_offy - 12;
    v6 = 3 * v4 - 4 * v5;

    *coordX = (v6 >= 0) ? (v6 / 192) : ((v6 + 1) / 192 - 1);

    v8 = 4 * v5 + v4;
    *coordY = v8 >= 0 ? (v8 / 128) : ((v8 + 1) / 128 - 1);

    *coordX += _square_x;
    *coordY += _square_y;

    *coordX = gSquareGridWidth - 1 - *coordX;
}

// 0x4B20E8 square_render_roof
void tileRenderRoofsInRect(Rect* rect, MapElevation elevation)
{
    if (!gTileRoofIsVisible) {
        return;
    }

    int temp;
    int minY;
    int minX;
    int maxX;
    int maxY;

    squareTileScreenToCoordRoof(rect->left, rect->top, elevation, &temp, &minY);
    squareTileScreenToCoordRoof(rect->right, rect->top, elevation, &minX, &temp);
    squareTileScreenToCoordRoof(rect->left, rect->bottom, elevation, &maxX, &temp);
    squareTileScreenToCoordRoof(rect->right, rect->bottom, elevation, &temp, &maxY);

    if (minX < 0) {
        minX = 0;
    }

    if (minX >= gSquareGridWidth) {
        minX = gSquareGridWidth - 1;
    }

    if (minY < 0) {
        minY = 0;
    }

    // FIXME: Probably a bug - testing X, then changing Y.
    if (minX >= gSquareGridHeight) {
        minY = gSquareGridHeight - 1;
    }

    int light = lightGetAmbientIntensity();

    int baseSquareTile = gSquareGridWidth * minY;

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            int squareTile = baseSquareTile + x;
            const RoofTileFrmId roofTileFrmId = RoofTileFrmId(gTileSquares[elevation]->tileFid[squareTile]);

            if ((roofTileFrmId.flags() & TileFlags::TemporarilyHidden) == TileFlags::None) {
                TileFrameId frameId = roofTileFrmId.frameId<TileFrameId>();
                if (frameId == TileFrameId::Invalid) {
                    frameId = TileFrameId::Last;
                }
                if (frameId != TileFrameId::Grid) {
                    int screenX;
                    int screenY;
                    squareTileToRoofScreenXY(squareTile, &screenX, &screenY, elevation);
                    tileRenderRoof(frameId, screenX, screenY, rect, light);
                }
            }
        }
        baseSquareTile += gSquareGridWidth;
    }
}

static void roof_fill_push_task_if_in_bounds(std::stack<roof_fill_task>& tasks_stack, int x, int y)
{
    if (x >= 0 && x < gSquareGridWidth && y >= 0 && y < gSquareGridHeight) {
        tasks_stack.push(roof_fill_task { x, y });
    };
};

static void roof_fill_off_process_task(std::stack<roof_fill_task>& tasks_stack, MapElevation elevation, bool on)
{
    auto [x, y] = tasks_stack.top();
    tasks_stack.pop();

    int squareTileIndex = gSquareGridWidth * y + x;
    int squareTile = gTileSquares[elevation]->tileFid[squareTileIndex];
    const FloorTileFrmId floorTileFrmId = FloorTileFrmId(squareTile);
    const RoofTileFrmId roofTileFrmId = RoofTileFrmId(squareTile);
    TileFrameId roofFrameId = roofTileFrmId.frameId<TileFrameId>();
    if (roofFrameId == TileFrameId::Invalid) {
        roofFrameId = TileFrameId::Last;
    }

    if (roofFrameId != TileFrameId::Grid) {
        TileFlags flag = roofTileFrmId.flags();

        bool updateFid = false;
        if (on) {
            if ((flag & TileFlags::TemporarilyHidden) != TileFlags::None) {
                flag = flag & ~TileFlags::TemporarilyHidden;
                updateFid = true;
            }
        } else {
            if ((flag & (TileFlags::TemporarilyHidden | TileFlags::AlwaysHidden)) == TileFlags::None) {
                flag = flag | TileFlags::TemporarilyHidden;
                updateFid = true;
            }
        }

        if (updateFid) {
            const TileFrmId updatedFrmId = TileFrmId(floorTileFrmId, RoofTileFrmId(roofFrameId, flag));
            gTileSquares[elevation]->tileFid[squareTileIndex] = updatedFrmId.fid();

            roof_fill_push_task_if_in_bounds(tasks_stack, x - 1, y);
            roof_fill_push_task_if_in_bounds(tasks_stack, x + 1, y);
            roof_fill_push_task_if_in_bounds(tasks_stack, x, y - 1);
            roof_fill_push_task_if_in_bounds(tasks_stack, x, y + 1);
        }
    }
}

// 0x4B23D4 tile_fill_roof
void tile_fill_roof(int x, int y, MapElevation elevation, bool on)
{
    std::stack<roof_fill_task> tasks_stack;

    roof_fill_push_task_if_in_bounds(tasks_stack, x, y);

    while (!tasks_stack.empty()) {
        roof_fill_off_process_task(tasks_stack, elevation, on);
    }
}

// 0x4B24E0 roof_draw
static void tileRenderRoof(const TileFrmId& frmId, int x, int y, Rect* rect, int light)
{
    CacheEntry* tileFrmHandle;
    Art* tileFrm = artLock(frmId, &tileFrmHandle);
    if (tileFrm == nullptr) {
        return;
    }

    int tileWidth = artGetWidth(tileFrm);
    int tileHeight = artGetHeight(tileFrm);

    Rect tileRect;
    tileRect.left = x;
    tileRect.top = y;
    tileRect.right = x + tileWidth - 1;
    tileRect.bottom = y + tileHeight - 1;

    if (rectIntersection(&tileRect, rect, &tileRect) == 0) {
        unsigned char* tileFrmBuffer = artGetFrameData(tileFrm);
        tileFrmBuffer += tileWidth * (tileRect.top - y) + (tileRect.left - x);

        CacheEntry* eggFrmHandle;
        Art* eggFrm = artLock(FrmId(gEgg), &eggFrmHandle);
        if (eggFrm != nullptr) {
            int eggWidth = artGetWidth(eggFrm);
            int eggHeight = artGetHeight(eggFrm);

            int eggScreenX;
            int eggScreenY;
            tileToScreenXY(gEgg->tile, &eggScreenX, &eggScreenY);

            eggScreenX += 16;
            eggScreenY += 8;

            eggScreenX += eggFrm->xOffsets[0];
            eggScreenY += eggFrm->yOffsets[0];

            eggScreenX += gEgg->x;
            eggScreenY += gEgg->y;

            Rect eggRect;
            eggRect.left = eggScreenX - eggWidth / 2;
            eggRect.top = eggScreenY - eggHeight + 1;
            eggRect.right = eggRect.left + eggWidth - 1;
            eggRect.bottom = eggScreenY;

            gEgg->sx = eggRect.left;
            gEgg->sy = eggRect.top;

            Rect intersectedRect;
            if (rectIntersection(&eggRect, &tileRect, &intersectedRect) == 0) {
                Rect rects[4];

                rects[0].left = tileRect.left;
                rects[0].top = tileRect.top;
                rects[0].right = tileRect.right;
                rects[0].bottom = intersectedRect.top - 1;

                rects[1].left = tileRect.left;
                rects[1].top = intersectedRect.top;
                rects[1].right = intersectedRect.left - 1;
                rects[1].bottom = intersectedRect.bottom;

                rects[2].left = intersectedRect.right + 1;
                rects[2].top = intersectedRect.top;
                rects[2].right = tileRect.right;
                rects[2].bottom = intersectedRect.bottom;

                rects[3].left = tileRect.left;
                rects[3].top = intersectedRect.bottom + 1;
                rects[3].right = tileRect.right;
                rects[3].bottom = tileRect.bottom;

                for (int i = 0; i < 4; i++) {
                    Rect* cr = &(rects[i]);
                    if (cr->left <= cr->right && cr->top <= cr->bottom) {
                        _dark_trans_buf_to_buf(tileFrmBuffer + tileWidth * (cr->top - tileRect.top) + (cr->left - tileRect.left),
                            cr->right - cr->left + 1,
                            cr->bottom - cr->top + 1,
                            tileWidth,
                            gTileWindowBuffer,
                            cr->left,
                            cr->top,
                            gTileWindowPitch,
                            light);
                    }
                }

                unsigned char* eggBuf = artGetFrameData(eggFrm);
                _intensity_mask_buf_to_buf(tileFrmBuffer + tileWidth * (intersectedRect.top - tileRect.top) + (intersectedRect.left - tileRect.left),
                    intersectedRect.right - intersectedRect.left + 1,
                    intersectedRect.bottom - intersectedRect.top + 1,
                    tileWidth,
                    gTileWindowBuffer + gTileWindowPitch * intersectedRect.top + intersectedRect.left,
                    gTileWindowPitch,
                    eggBuf + eggWidth * (intersectedRect.top - eggRect.top) + (intersectedRect.left - eggRect.left),
                    eggWidth,
                    light);
            } else {
                _dark_trans_buf_to_buf(tileFrmBuffer, tileRect.right - tileRect.left + 1, tileRect.bottom - tileRect.top + 1, tileWidth, gTileWindowBuffer, tileRect.left, tileRect.top, gTileWindowPitch, light);
            }

            artUnlock(eggFrmHandle);
        }
    }

    artUnlock(tileFrmHandle);
}

// 0x4B2944 square_render_floor
void tileRenderFloorsInRect(Rect* rect, MapElevation elevation)
{
    int minY;
    int maxX;
    int maxY;
    int minX;
    int temp;

    squareTileScreenToCoord(rect->left, rect->top, elevation, &temp, &minY);
    squareTileScreenToCoord(rect->right, rect->top, elevation, &minX, &temp);
    squareTileScreenToCoord(rect->left, rect->bottom, elevation, &maxX, &temp);
    squareTileScreenToCoord(rect->right, rect->bottom, elevation, &temp, &maxY);

    if (minX < 0) {
        minX = 0;
    }

    if (minX >= gSquareGridWidth) {
        minX = gSquareGridWidth - 1;
    }

    if (minY < 0) {
        minY = 0;
    }

    if (minX >= gSquareGridHeight) {
        minY = gSquareGridHeight - 1;
    }

    lightGetAmbientIntensity();

    int baseSquareTile = gSquareGridWidth * minY;

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            int squareTile = baseSquareTile + x;
            const FloorTileFrmId floorTileFrmId = FloorTileFrmId(gTileSquares[elevation]->tileFid[squareTile]);
            if ((floorTileFrmId.flags() & TileFlags::TemporarilyHidden) == TileFlags::None) {
                int tileScreenX;
                int tileScreenY;
                squareTileToScreenXY(squareTile, &tileScreenX, &tileScreenY, elevation);
                TileFrameId frameId = floorTileFrmId.frameId<TileFrameId>();
                if (frameId == TileFrameId::Invalid) {
                    frameId = TileFrameId::Last;
                }
                tileRenderFloor(frameId, tileScreenX, tileScreenY, rect);
            }
        }
        baseSquareTile += gSquareGridWidth;
    }
}

// Port of sfall HRP ViewMap::square_obj_render
void tileRenderEdgeBlackSquares(Rect* rect, MapElevation elevation, bool drawOnTop)
{
    if (!mapEdgeIsEnabled() || !mapEdgeHasSquareRect(elevation)) {
        return;
    }

    Rect squareRect;
    mapEdgeGetSquareRect(elevation, &squareRect);
    EdgeZone::ClipSides clipSides = mapEdgeGetClipSides(elevation);

    int maxX, minY, minX, maxY, temp;

    squareTileScreenToCoord(rect->left, rect->bottom, elevation, &maxX, &temp);
    squareTileScreenToCoord(rect->left, rect->top, elevation, &temp, &minY);
    squareTileScreenToCoord(rect->right, rect->top, elevation, &minX, &temp);
    squareTileScreenToCoord(rect->right, rect->bottom, elevation, &temp, &maxY);

    if (++maxX > gSquareGridWidth) maxX = gSquareGridWidth - 1;
    if (--minX < 0) minX = 0;
    if (--minY < 0) minY = 0;
    if (++maxY > gSquareGridHeight) maxY = gSquareGridHeight - 1;

    if (minY >= maxY || minX >= maxX) return;

    bool drawLeft = clipSides.left == drawOnTop;
    bool drawTop = clipSides.top == drawOnTop;
    bool drawRight = clipSides.right == drawOnTop;
    bool drawBottom = clipSides.bottom == drawOnTop;

    constexpr TileFrmId kEdgeFrmId = TileFrameId::Grid;
    int baseSquareTile = gSquareGridWidth * minY;

    for (int y = minY; y < maxY; y++) {
        for (int x = minX; x < maxX; x++) {
            if ((drawLeft && x > squareRect.left)
                || (drawTop && y < squareRect.top)
                || (drawRight && x < squareRect.right)
                || (drawBottom && y > squareRect.bottom)) {
                int sx, sy;
                squareTileToScreenXY(baseSquareTile + x, &sx, &sy, elevation);
                tileRenderFloor(kEdgeFrmId, sx, sy, rect);
            }
        }
        baseSquareTile += gSquareGridWidth;
    }
}

// 0x4B2B10 square_roof_intersect
bool _square_roof_intersect(int x, int y, MapElevation elevation)
{
    if (!gTileRoofIsVisible) {
        return false;
    }

    bool result = false;

    int tileX;
    int tileY;
    squareTileScreenToCoordRoof(x, y, elevation, &tileX, &tileY);

    TileData* ptr = gTileSquares[elevation];
    int idx = gSquareGridWidth * tileY + tileX;
    const RoofTileFrmId roofTileFrmId = RoofTileFrmId(ptr->tileFid[gSquareGridWidth * tileY + tileX]);
    TileFrameId frameId = roofTileFrmId.frameId<TileFrameId>();
    if (frameId == TileFrameId::Invalid) {
        frameId = TileFrameId::Last;
    }

    if (frameId != TileFrameId::Grid) {
        if ((roofTileFrmId.flags() & TileFlags::TemporarilyHidden) == TileFlags::None) {
            CacheEntry* handle;
            Art* art = artLock(frameId, &handle);
            if (art != nullptr) {
                unsigned char* data = artGetFrameData(art);
                if (data != nullptr) {
                    int v18;
                    int v17;
                    squareTileToRoofScreenXY(idx, &v18, &v17, elevation);

                    int width = artGetWidth(art);
                    if (data[width * (y - v17) + x - v18] != 0) {
                        result = true;
                    }
                }
                artUnlock(handle);
            }
        }
    }

    return result;
}

// 0x4B2E98 grid_render
void _grid_render(Rect* rect, MapElevation elevation)
{
    if (!gTileGridIsVisible) {
        return;
    }

    for (int y = rect->top - 12; y < rect->bottom + 12; y += 6) {
        for (int x = rect->left - 32; x < rect->right + 32; x += 16) {
            int tile = tileFromScreenXY(x, y);
            _draw_grid(tile, elevation, rect);
        }
    }
}

// 0x4B2F4C draw_grid
static void _draw_grid(int tile, MapElevation elevation, Rect* rect)
{
    if (tile == -1) {
        return;
    }

    int x;
    int y;
    tileToScreenXY(tile, &x, &y);

    Rect r;
    r.left = x;
    r.top = y;
    r.right = x + 32 - 1;
    r.bottom = y + 16 - 1;

    if (rectIntersection(&r, rect, &r) == -1) {
        return;
    }

    if (_obj_blocking_at(nullptr, tile, elevation) != nullptr) {
        blitBufferToBufferTrans(_tile_grid_blocked + 32 * (r.top - y) + (r.left - x),
            r.right - r.left + 1,
            r.bottom - r.top + 1,
            32,
            gTileWindowBuffer + gTileWindowPitch * r.top + r.left,
            gTileWindowPitch);
        return;
    }

    if (_obj_occupied(tile, elevation)) {
        blitBufferToBufferTrans(_tile_grid_occupied + 32 * (r.top - y) + (r.left - x),
            r.right - r.left + 1,
            r.bottom - r.top + 1,
            32,
            gTileWindowBuffer + gTileWindowPitch * r.top + r.left,
            gTileWindowPitch);
        return;
    }

    _translucent_trans_buf_to_buf(_tile_grid_occupied + 32 * (r.top - y) + (r.left - x),
        r.right - r.left + 1,
        r.bottom - r.top + 1,
        32,
        gTileWindowBuffer + gTileWindowPitch * r.top + r.left,
        0,
        0,
        gTileWindowPitch,
        _wallBlendTable,
        _commonGrayTable);
}

// 0x4B30C4 floor_draw
static void tileRenderFloor(const TileFrmId& frmId, int x, int y, Rect* rect)
{
    if (artIsObjectTypeHidden(frmId.objectType()) != 0) {
        return;
    }

    CacheEntry* cacheEntry;
    Art* art = artLock(frmId, &cacheEntry);
    if (art == nullptr) {
        return;
    }

    MapElevation elev = gElevation;
    int left = rect->left;
    int top = rect->top;
    int width = rect->right - rect->left + 1;
    int height = rect->bottom - rect->top + 1;
    int frameWidth;
    int frameHeight;
    int tile;
    int v76;
    int v77;
    int v78;
    int v79;

    int savedX = x;
    int savedY = y;

    if (left < 0) {
        left = 0;
    }

    if (top < 0) {
        top = 0;
    }

    if (left + width > gTileWindowWidth) {
        width = gTileWindowWidth - left;
    }

    if (top + height > gTileWindowHeight) {
        height = gTileWindowHeight - top;
    }

    if (x >= gTileWindowWidth || x > rect->right || y >= gTileWindowHeight || y > rect->bottom) goto out;

    frameWidth = artGetWidth(art);
    frameHeight = artGetHeight(art);

    if (left < x) {
        v79 = 0;
        int v12 = left + width;
        v77 = frameWidth + x <= v12 ? frameWidth : v12 - x;
    } else {
        v79 = left - x;
        x = left;
        v77 = frameWidth - v79;
        if (v77 > width) {
            v77 = width;
        }
    }

    if (top < y) {
        int v14 = height + top;
        v78 = 0;
        v76 = frameHeight + y <= v14 ? frameHeight : v14 - y;
    } else {
        v78 = top - y;
        y = top;
        v76 = frameHeight - v78;
        if (v76 > height) {
            v76 = height;
        }
    }

    if (v77 <= 0 || v76 <= 0) goto out;

    tile = tileFromScreenXY(savedX, savedY + 13);
    if (tile != -1) {
        int parity = tile & 1;
        int ambientIntensity = lightGetAmbientIntensity();
        for (int i = 0; i < 10; i++) {
            // NOTE: Calls `lightGetTileIntensity` twice.
            _verticies[i].intensity = std::max(lightGetTileIntensity(elev, tile + _verticies[i].offsets[parity]), ambientIntensity);
        }

        int v23 = 0;
        for (int i = 0; i < 9; i++) {
            if (_verticies[i + 1].intensity != _verticies[i].intensity) {
                break;
            }

            v23++;
        }

        if (v23 == 9) {
            unsigned char* buf = artGetFrameData(art);
            _dark_trans_buf_to_buf(buf + frameWidth * v78 + v79, v77, v76, frameWidth, gTileWindowBuffer, x, y, gTileWindowPitch, _verticies[0].intensity);
            goto out;
        }

        for (int i = 0; i < 5; i++) {
            RightsideUpTriangle* triangle = &(_rightside_up_triangles[i]);
            int v32 = _verticies[triangle->field_8].intensity;
            int v33 = _verticies[triangle->field_8].field_0;
            int v34 = _verticies[triangle->field_4].intensity - _verticies[triangle->field_0].intensity;
            // TODO: Probably wrong.
            int v35 = v34 / 32;
            int v36 = (_verticies[triangle->field_0].intensity - v32) / 13;
            int* v37 = &(_intensity_map[v33]);
            if (v35 != 0) {
                if (v36 != 0) {
                    for (int i = 0; i < 13; i++) {
                        int v41 = v32;
                        int v42 = _rightside_up_table[i].field_4;
                        v37 += _rightside_up_table[i].field_0;
                        for (int j = 0; j < v42; j++) {
                            *v37++ = v41;
                            v41 += v35;
                        }
                        v32 += v36;
                    }
                } else {
                    for (int i = 0; i < 13; i++) {
                        int v38 = v32;
                        int v39 = _rightside_up_table[i].field_4;
                        v37 += _rightside_up_table[i].field_0;
                        for (int j = 0; j < v39; j++) {
                            *v37++ = v38;
                            v38 += v35;
                        }
                    }
                }
            } else {
                if (v36 != 0) {
                    for (int i = 0; i < 13; i++) {
                        int v46 = _rightside_up_table[i].field_4;
                        v37 += _rightside_up_table[i].field_0;
                        for (int j = 0; j < v46; j++) {
                            *v37++ = v32;
                        }
                        v32 += v36;
                    }
                } else {
                    for (int i = 0; i < 13; i++) {
                        int v44 = _rightside_up_table[i].field_4;
                        v37 += _rightside_up_table[i].field_0;
                        for (int j = 0; j < v44; j++) {
                            *v37++ = v32;
                        }
                    }
                }
            }
        }

        for (int i = 0; i < 5; i++) {
            UpsideDownTriangle* triangle = &(_upside_down_triangles[i]);
            int v50 = _verticies[triangle->field_0].intensity;
            int v51 = _verticies[triangle->field_0].field_0;
            int v52 = _verticies[triangle->field_8].intensity - v50;
            // TODO: Probably wrong.
            int v53 = v52 / 32;
            int v54 = (_verticies[triangle->field_4].intensity - v50) / 13;
            int* v55 = &(_intensity_map[v51]);
            if (v53 != 0) {
                if (v54 != 0) {
                    for (int i = 0; i < 13; i++) {
                        int v59 = v50;
                        int v60 = _upside_down_table[i].field_4;
                        v55 += _upside_down_table[i].field_0;
                        for (int j = 0; j < v60; j++) {
                            *v55++ = v59;
                            v59 += v53;
                        }
                        v50 += v54;
                    }
                } else {
                    for (int i = 0; i < 13; i++) {
                        int v56 = v50;
                        int v57 = _upside_down_table[i].field_4;
                        v55 += _upside_down_table[i].field_0;
                        for (int j = 0; j < v57; j++) {
                            *v55++ = v56;
                            v56 += v53;
                        }
                    }
                }
            } else {
                if (v54 != 0) {
                    for (int i = 0; i < 13; i++) {
                        int v64 = _upside_down_table[i].field_4;
                        v55 += _upside_down_table[i].field_0;
                        for (int j = 0; j < v64; j++) {
                            *v55++ = v50;
                        }
                        v50 += v54;
                    }
                } else {
                    for (int i = 0; i < 13; i++) {
                        int v62 = _upside_down_table[i].field_4;
                        v55 += _upside_down_table[i].field_0;
                        for (int j = 0; j < v62; j++) {
                            *v55++ = v50;
                        }
                    }
                }
            }
        }

        unsigned char* v66 = gTileWindowBuffer + gTileWindowPitch * y + x;
        unsigned char* v67 = artGetFrameData(art) + frameWidth * v78 + v79;
        int* v68 = &(_intensity_map[160 + 80 * v78]) + v79;
        int v86 = frameWidth - v77;
        int v85 = gTileWindowPitch - v77;
        int v87 = 80 - v77;

        while (--v76 != -1) {
            for (int kk = 0; kk < v77; kk++) {
                if (*v67 != 0) {
                    *v66 = intensityColorTable[*v67][*v68 >> 9];
                }
                v67++;
                v68++;
                v66++;
            }
            v66 += v85;
            v68 += v87;
            v67 += v86;
        }
    }

out:

    artUnlock(cacheEntry);
}

// 0x4B372C tile_make_line
static int _tile_make_line(int from, int to, int* tiles, int tilesCapacity)
{
    if (tilesCapacity <= 1) {
        return 0;
    }

    int count = 0;

    int fromX;
    int fromY;
    tileToScreenXY(from, &fromX, &fromY);
    fromX += 16;
    fromY += 8;

    int toX;
    int toY;
    tileToScreenXY(to, &toX, &toY);
    toX += 16;
    toY += 8;

    tiles[count++] = from;

    int stepX;
    int deltaX = toX - fromX;
    if (deltaX > 0)
        stepX = 1;
    else if (deltaX < 0)
        stepX = -1;
    else
        stepX = 0;

    int stepY;
    int deltaY = toY - fromY;
    if (deltaY > 0)
        stepY = 1;
    else if (deltaY < 0)
        stepY = -1;
    else
        stepY = 0;

    int v28 = 2 * abs(toX - fromX);
    int v27 = 2 * abs(toY - fromY);

    int tileX = fromX;
    int tileY = fromY;

    if (v28 <= v27) {
        int middleX = v28 - v27 / 2;
        while (true) {
            int tile = tileFromScreenXY(tileX, tileY);
            tiles[count] = tile;

            if (tile == to) {
                count++;
                break;
            }

            if (tile != tiles[count - 1] && (count == 1 || tile != tiles[count - 2])) {
                count++;
                if (count == tilesCapacity) {
                    break;
                }
            }

            if (tileY == toY) {
                break;
            }

            if (middleX >= 0) {
                tileX += stepX;
                middleX -= v27;
            }

            middleX += v28;
            tileY += stepY;
        }
    } else {
        int middleY = v27 - v28 / 2;
        while (true) {
            int tile = tileFromScreenXY(tileX, tileY);
            tiles[count] = tile;

            if (tile == to) {
                count++;
                break;
            }

            if (tile != tiles[count - 1] && (count == 1 || tile != tiles[count - 2])) {
                count++;
                if (count == tilesCapacity) {
                    break;
                }
            }

            if (tileX == toX) {
                return count;
            }

            if (middleY >= 0) {
                tileY += stepY;
                middleY -= v28;
            }

            middleY += v27;
            tileX += stepX;
        }
    }

    return count;
}

// 0x4B3924 tile_scroll_to
int _tile_scroll_to(int tile, int flags)
{
    if (tile == gCenterTile) {
        return -1;
    }

    int oldCenterTile = gCenterTile;

    int v9[200];
    int count = _tile_make_line(gCenterTile, tile, v9, 200);
    if (count == 0) {
        return -1;
    }

    int index = 1;
    for (; index < count; index++) {
        if (tileSetCenter(v9[index], 0) == -1) {
            break;
        }
    }

    int rc = 0;
    if ((flags & 0x01) != 0) {
        if (index != count) {
            tileSetCenter(oldCenterTile, 0);
            rc = -1;
        }
    }

    if ((flags & 0x02) != 0) {
        // NOTE: Uninline.
        tileWindowRefresh();
    }

    return rc;
}

} // namespace fallout
