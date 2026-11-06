#include "tile.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stack>
#include <unordered_map>
#include <utility>
#include <vector>

#include "art.h"
#include "animation.h"
#include "billboard_clip_bands.h"
#include "combat.h"
#include "color.h"
#include "critter.h"
#include "debug.h"
#include "draw.h"
#include "first_person_sky.h"
#include "game_mouse.h"
#include "input.h"
#include "light.h"
#include "map.h"
#include "map_edge.h"
#include "mouse.h"
#include "object.h"
#include "party_member.h"
#include "platform_compat.h"
#include "proto.h"
#include "settings.h"
#include "svga.h"
#include "tile_hires_stencil.h"
#include "text_object.h"
#include "window_manager.h"

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
    bool structuralOnly;
};

struct TileFirstPersonGrayboxRenderContext {
    MapElevation elevation;
    int gridWidth;
    unsigned char* wallCells;
    unsigned char* wallOrientations;
    std::vector<int>* exitGridTiles;
};

enum TileFirstPersonWallOrientation : unsigned char {
    TILE_FIRST_PERSON_WALL_ORIENTATION_CELL,
    TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH,
    TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST,
};

struct TileFirstPersonTopologyMarker {
    int tile;
    ObjectType objectType;
    Color color;
    unsigned int cornerFlags;
    Rotation rotation;
    TileFirstPersonWallOrientation wallOrientation;
    bool structural;
    bool door;
    bool openDoor;
};

struct TileFirstPersonActorHitTarget {
    Object* object;
    double depth;
    int left;
    int top;
    int width;
    int height;
    int frame;
    Rotation rotation;
};

struct TileFirstPersonDoorHitTarget {
    Object* object;
    double depth;
    int screenX;
};

struct TileFirstPersonObjectHitTarget {
    Object* object;
    double depth;
};

// Ray traversal retains at most one door cap per column. Bottom is exclusive,
// matching billboard clip rectangles; an empty span contributes no occlusion.
struct TileFirstPersonDoorCapSpan {
    int top = 0;
    int bottom = 0;
    double depth = std::numeric_limits<double>::infinity();
};

struct TileFirstPersonActorRenderContext {
    MapElevation elevation;
    double cameraX;
    double cameraY;
    double directionX;
    double directionY;
    double cameraPlaneX;
    double cameraPlaneY;
    double inverseDeterminant;
    int horizonY;
    std::vector<double>* wallDepths;
    std::vector<double>* wallSillDepths;
    std::vector<TileFirstPersonDoorCapSpan>* doorCapDepths;
    std::vector<double>* actorDepths;
    std::vector<unsigned int>* actorDepthStamps;
    unsigned int actorDepthStamp;
    std::vector<TileFirstPersonActorHitTarget>* hitTargets;
};

struct TileFirstPersonExitGridRenderContext {
    MapElevation elevation;
    double cameraX;
    double cameraY;
    double directionX;
    double directionY;
    double cameraPlaneX;
    double cameraPlaneY;
    double inverseDeterminant;
    int horizonY;
    std::vector<double>* wallDepths;
    std::vector<double>* wallSillDepths;
    std::vector<TileFirstPersonDoorCapSpan>* doorCapDepths;
};

static void tileSetBorder(int windowWidth, int windowHeight, int hexGridWidth, int hexGridHeight);
static void tileRefreshMapper(Rect* rect, MapElevation elevation);
static void tileRefreshGame(Rect* rect, MapElevation elevation);
static void tileRenderFirstPersonPreview();
static void tileRenderFirstPersonTopologyOverlay();
static void tileFirstPersonInitializeWallTextures();
static unsigned int tileFirstPersonTextureNoise(int x, int y, unsigned int seed);
static void tileFirstPersonTopologyMarkObject(Object* object, void* context);
static void tileFirstPersonSnapshotTopologyObject(Object* object, void* context);
static void tileFirstPersonGrayboxMarkObject(Object* object, void* context);
static void tileFirstPersonRenderActor(Object* object, void* context);
static void tileFirstPersonRenderExitGridMarkers(
    const std::vector<int>& tiles,
    const TileFirstPersonExitGridRenderContext* context);
static void tileFirstPersonRenderExitGridMarker(
    int tile,
    const TileFirstPersonExitGridRenderContext* context);
static void tileFirstPersonDrawCrosshair(double directionX, double directionY);
static void tileFirstPersonDrawMoveMarker(double directionX, double directionY);
static void tileFirstPersonDrawDiagnosticLine(int x1, int y1, int x2, int y2, Color color);
static bool tileFirstPersonGetWorldPosition(int tile, double* x, double* y);
static Rotation tileFirstPersonGetRotationToward(int tile, double targetX, double targetY);
static double tileFirstPersonPreviewGetYaw();
static void tileFirstPersonGetCameraPosition(int tile, MapElevation elevation, double* x, double* y);
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
static bool gFirstPersonPreviewRefreshPending = false;
static bool gFirstPersonPreviewRefreshing = false;
static bool gFirstPersonPreviewTopologyEnabled = false;
static bool gFirstPersonPreviewTopologyRendering = false;
static int gFirstPersonPreviewTopologyPosition = -1;
static int gFirstPersonPreviewTopologyZoom = 0;
static constexpr int kFirstPersonWallTextureSize = 64;
static std::array<Color, kFirstPersonWallTextureSize * kFirstPersonWallTextureSize> gFirstPersonStoneWallTexture;
static std::array<Color, kFirstPersonWallTextureSize * kFirstPersonWallTextureSize> gFirstPersonDoorWallTexture;
static bool gFirstPersonWallTexturesInitialized = false;
static int gFirstPersonPreviewYaw = 0;
static double gFirstPersonPreviewYawStart = 0.0;
static double gFirstPersonPreviewYawTarget = 0.0;
static unsigned int gFirstPersonPreviewYawStartTime = 0;
static bool gFirstPersonPreviewYawTurning = false;
static int gFirstPersonPreviewMoveMarkerTile = -1;
static int gFirstPersonPreviewPendingMoveDirection = -1;
static unsigned int gFirstPersonPreviewPendingMoveTime = 0;
static bool gFirstPersonPreviewCameraInitialized = false;
static int gFirstPersonPreviewCameraTile = -1;
static MapElevation gFirstPersonPreviewCameraElevation;
static bool gFirstPersonPreviewCameraInCombat = false;
static unsigned int gFirstPersonPreviewCameraStartTime = 0;
static double gFirstPersonPreviewCameraStartX = 0.0;
static double gFirstPersonPreviewCameraStartY = 0.0;
static double gFirstPersonPreviewCameraTargetX = 0.0;
static double gFirstPersonPreviewCameraTargetY = 0.0;
static std::vector<double> gFirstPersonPreviewWallDepths;
static std::vector<TileFirstPersonActorHitTarget> gFirstPersonPreviewActorHitTargets;
static std::vector<TileFirstPersonDoorHitTarget> gFirstPersonPreviewDoorHitTargets;
static std::vector<unsigned char> gFirstPersonPreviewWallCells;
static std::vector<unsigned char> gFirstPersonPreviewWallOrientations;
static std::vector<int> gFirstPersonPreviewExitGridTiles;
static std::vector<TileFirstPersonTopologyMarker> gFirstPersonPreviewTopologyMarkers;
static unsigned int gFirstPersonPreviewProfileFrames = 0;
static unsigned int gFirstPersonPreviewProfileTotalMs = 0;
static unsigned int gFirstPersonPreviewProfileFloorMs = 0;
static unsigned int gFirstPersonPreviewProfileWallMs = 0;
static unsigned int gFirstPersonPreviewProfileActorMs = 0;

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

struct TileFirstPersonWindowState {
    unsigned char* buffer;
    int width;
    int height;
    int pitch;
    std::vector<TileFirstPersonActorHitTarget> actorHitTargets;

    TileFirstPersonWindowState()
        : buffer(gTileWindowBuffer)
        , width(gTileWindowWidth)
        , height(gTileWindowHeight)
        , pitch(gTileWindowPitch)
        , actorHitTargets(std::move(gFirstPersonPreviewActorHitTargets))
    {
    }

    ~TileFirstPersonWindowState()
    {
        gTileWindowBuffer = buffer;
        gTileWindowWidth = width;
        gTileWindowHeight = height;
        gTileWindowPitch = pitch;
        gFirstPersonPreviewActorHitTargets = std::move(actorHitTargets);
    }
};

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
        tileFirstPersonPreviewRequestRefresh();
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

static unsigned int tileFirstPersonTextureNoise(int x, int y, unsigned int seed)
{
    unsigned int value = static_cast<unsigned int>(x) * 0x9E3779B9u
        ^ static_cast<unsigned int>(y) * 0x85EBCA6Bu
        ^ seed * 0xC2B2AE35u;
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

static void tileFirstPersonInitializeWallTextures()
{
    if (gFirstPersonWallTexturesInitialized) {
        return;
    }

    const Color mortar = _colorTable[rgb555(0x302922)];
    const Color stoneColors[] = {
        _colorTable[rgb555(0x4E4237)],
        _colorTable[rgb555(0x594A3D)],
        _colorTable[rgb555(0x655442)],
        _colorTable[rgb555(0x71604D)],
        _colorTable[rgb555(0x7C6A55)],
    };
    const Color stoneEdge = _colorTable[rgb555(0x44382F)];
    const Color stoneHighlight = _colorTable[rgb555(0x89765F)];

    const Color woodColors[] = {
        _colorTable[rgb555(0x38271D)],
        _colorTable[rgb555(0x4C3424)],
        _colorTable[rgb555(0x60432D)],
        _colorTable[rgb555(0x735237)],
    };
    const Color woodEdge = _colorTable[rgb555(0x2A1E18)];
    const Color woodHighlight = _colorTable[rgb555(0x806040)];

    for (int y = 0; y < kFirstPersonWallTextureSize; y++) {
        const int course = y / 16;
        const int localY = y % 16;
        const int horizontalOffset = (course & 1) != 0 ? 16 : 0;
        for (int x = 0; x < kFirstPersonWallTextureSize; x++) {
            const int phaseX = (x + horizontalOffset) & 31;
            const int blockX = ((x + horizontalOffset) / 32) & 1;
            const int blockSeed = course * 2 + blockX;
            const unsigned int noise = tileFirstPersonTextureNoise(x, y, 17);
            const int stoneBase = (blockSeed
                + static_cast<int>(tileFirstPersonTextureNoise(blockX, course, 29) % 3)) % 5;
            const size_t index = static_cast<size_t>(y) * kFirstPersonWallTextureSize + x;

            if (localY == 0 || phaseX == 0) {
                gFirstPersonStoneWallTexture[index] = mortar;
            } else if (localY == 1 || phaseX == 1) {
                gFirstPersonStoneWallTexture[index] = stoneEdge;
            } else if (localY == 15 || phaseX == 31) {
                gFirstPersonStoneWallTexture[index] = stoneColors[(stoneBase + 1) % 5];
            } else {
                if (localY == 2 && (noise & 31) == 0) {
                    gFirstPersonStoneWallTexture[index] = stoneHighlight;
                } else if ((noise & 63) == 0 && stoneBase > 0) {
                    gFirstPersonStoneWallTexture[index] = stoneColors[stoneBase - 1];
                } else {
                    gFirstPersonStoneWallTexture[index] = stoneColors[stoneBase];
                }
            }

            const int plankX = x & 15;
            const int plank = x / 16;
            const int woodBase = (plank + static_cast<int>(tileFirstPersonTextureNoise(plank, 0, 43) % 2)) % 4;
            if (plankX == 0) {
                gFirstPersonDoorWallTexture[index] = woodEdge;
            } else if (plankX == 1) {
                gFirstPersonDoorWallTexture[index] = woodColors[0];
            } else if (plankX == 15) {
                gFirstPersonDoorWallTexture[index] = woodColors[1];
            } else if ((x % 16 == 4 || x % 16 == 11)
                && ((noise >> 3) & 7) == 0) {
                gFirstPersonDoorWallTexture[index] = woodHighlight;
            } else if ((noise & 31) == 0 && woodBase > 0) {
                gFirstPersonDoorWallTexture[index] = woodColors[woodBase - 1];
            } else {
                gFirstPersonDoorWallTexture[index] = woodColors[woodBase];
            }
        }
    }

    gFirstPersonWallTexturesInitialized = true;
}

static void tileRenderFirstPersonPreview()
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kFieldOfViewScale = 0.66;
    const bool profileFrame = settings.debug.show_fps;
    const unsigned int frameStart = profileFrame ? getTicks() : 0;
    const int ambientIntensity = lightGetAmbientIntensity();
    const int ambientIntensityIndex = std::clamp(ambientIntensity / 512, 0, 255);
    const unsigned char lightModulation = static_cast<unsigned char>(
        std::clamp(ambientIntensity * 255 / LIGHT_INTENSITY_MAX, 0, 255));
    gpuBillboardBeginFrame(lightModulation);
    gFirstPersonPreviewActorHitTargets.clear();

    bufferFill(gTileWindowBuffer,
        gTileWindowWidth,
        gTileWindowHeight,
        gTileWindowPitch,
        COLOR_DARK_GREY_3);

    if (gTileWindowWidth < 2 || gTileWindowHeight < 2
        || !elevationIsValid(gElevation) || gTileSquares[gElevation] == nullptr) {
        return;
    }

    tileFirstPersonInitializeWallTextures();
    gFirstPersonPreviewDoorHitTargets.clear();

    const int horizonY = gTileWindowHeight / 2;
    firstPersonSkyRender(gTileWindowBuffer, gTileWindowWidth, gTileWindowHeight,
        gTileWindowPitch, tileFirstPersonPreviewGetYaw(), kFieldOfViewScale,
        ambientIntensityIndex);
    bufferFill(gTileWindowBuffer + horizonY * gTileWindowPitch,
        gTileWindowWidth,
        gTileWindowHeight - horizonY,
        gTileWindowPitch,
        COLOR_DULL_BROWN);

    gFirstPersonPreviewWallCells.assign(gHexGridSize, 0);
    gFirstPersonPreviewWallOrientations.assign(gHexGridSize, 0);
    gFirstPersonPreviewExitGridTiles.clear();
    TileFirstPersonGrayboxRenderContext context = {
        gElevation,
        gHexGridWidth,
        gFirstPersonPreviewWallCells.data(),
        gFirstPersonPreviewWallOrientations.data(),
        &gFirstPersonPreviewExitGridTiles,
    };
    objectVisitAll(tileFirstPersonGrayboxMarkObject, &context);
    std::sort(gFirstPersonPreviewExitGridTiles.begin(), gFirstPersonPreviewExitGridTiles.end());
    gFirstPersonPreviewExitGridTiles.erase(
        std::unique(gFirstPersonPreviewExitGridTiles.begin(), gFirstPersonPreviewExitGridTiles.end()),
        gFirstPersonPreviewExitGridTiles.end());
    if (gFirstPersonPreviewTopologyEnabled) {
        gFirstPersonPreviewTopologyMarkers.clear();
        objectVisitAll(tileFirstPersonSnapshotTopologyObject, &gFirstPersonPreviewTopologyMarkers);
    }

    double cameraX = SQUARE_GRID_WIDTH / 2.0;
    double cameraY = SQUARE_GRID_HEIGHT / 2.0;
    if (gDude != nullptr && gDude->elevation == gElevation && tileIsValid(gDude->tile)) {
        tileFirstPersonGetCameraPosition(gDude->tile, gDude->elevation, &cameraX, &cameraY);
    }

    const double yaw = tileFirstPersonPreviewGetYaw() * kPi / 180.0;
    const double directionX = sin(yaw);
    const double directionY = -cos(yaw);
    const double cameraPlaneX = -directionY * kFieldOfViewScale;
    const double cameraPlaneY = directionX * kFieldOfViewScale;
    const double inverseDeterminant =
        1.0 / (cameraPlaneX * directionY - directionX * cameraPlaneY);
    static std::vector<double> rayDirectionXs;
    static std::vector<double> rayDirectionYs;
    rayDirectionXs.resize(gTileWindowWidth);
    rayDirectionYs.resize(gTileWindowWidth);
    for (int x = 0; x < gTileWindowWidth; x++) {
        const double cameraOffset = 2.0 * x / gTileWindowWidth - 1.0;
        rayDirectionXs[x] = directionX + cameraPlaneX * cameraOffset;
        rayDirectionYs[x] = directionY + cameraPlaneY * cameraOffset;
    }

    // Compose the isometric tile overlap once per floor layout. The old
    // per-pixel neighbor search repeated map/FID/texture lookups hundreds of
    // thousands of times every frame.
    constexpr int kFloorCacheCellResolution = 16;
    constexpr int kFloorCacheCellArea = kFloorCacheCellResolution * kFloorCacheCellResolution;
    static std::vector<int> cachedFloorFids;
    static std::vector<unsigned char> cachedFloorColors;
    static std::vector<FrmImage> floorTextures;
    static std::vector<int> floorTextureIndices;
    static int cachedFloorElevation = -1;
    const int* currentFloorFids = gTileSquares[gElevation]->tileFid;
    const unsigned int floorCacheStart = profileFrame ? getTicks() : 0;
    const bool floorLayoutChanged = cachedFloorElevation != static_cast<int>(gElevation)
        || cachedFloorFids.size() != SQUARE_GRID_SIZE
        || !std::equal(currentFloorFids, currentFloorFids + SQUARE_GRID_SIZE,
            cachedFloorFids.begin(), [](int current, int cached) {
                // Packed tile IDs also contain roof visibility. Walking into
                // a room hides its roof but does not change the floor cache.
                return FloorTileFrmId(current).fid() == FloorTileFrmId(cached).fid();
            });
    if (floorLayoutChanged) {
        cachedFloorFids.assign(currentFloorFids, currentFloorFids + SQUARE_GRID_SIZE);
        floorTextures.clear();
        floorTextureIndices.assign(FrmId::kMaxFrameId + 1, -1);
        for (int squareTile = 0; squareTile < SQUARE_GRID_SIZE; squareTile++) {
            const FloorTileFrmId floorTileFrmId(currentFloorFids[squareTile]);
            if ((floorTileFrmId.flags() & TileFlags::TemporarilyHidden) != TileFlags::None) {
                continue;
            }

            const TileFrameId frameId = floorTileFrmId.frameId<TileFrameId>();
            if (frameId == TileFrameId::Invalid
                || frameId == TileFrameId::Reserved
                || frameId == TileFrameId::Grid) {
                continue;
            }

            const int frameIndex = static_cast<int>(frameId);
            if (frameIndex < 0 || frameIndex >= static_cast<int>(floorTextureIndices.size())
                || floorTextureIndices[frameIndex] != -1) {
                continue;
            }

            floorTextureIndices[frameIndex] = -2;
            floorTextures.emplace_back();
            if (floorTextures.back().lock(FrmId(frameId))) {
                floorTextureIndices[frameIndex] = static_cast<int>(floorTextures.size()) - 1;
            }
        }

        cachedFloorColors.assign(SQUARE_GRID_SIZE * kFloorCacheCellArea, COLOR_DULL_BROWN);
        for (int mapY = 0; mapY < SQUARE_GRID_HEIGHT; mapY++) {
            for (int mapX = 0; mapX < SQUARE_GRID_WIDTH; mapX++) {
                for (int sampleY = 0; sampleY < kFloorCacheCellResolution; sampleY++) {
                    for (int sampleX = 0; sampleX < kFloorCacheCellResolution; sampleX++) {
                        const double worldX = mapX + (sampleX + 0.5) / kFloorCacheCellResolution;
                        const double worldY = mapY + (sampleY + 0.5) / kFloorCacheCellResolution;
                        unsigned char floorColor = COLOR_DULL_BROWN;
                        double bestDistance = std::numeric_limits<double>::infinity();
                        for (int neighborY = std::max(0, mapY - 1);
                             neighborY <= std::min(SQUARE_GRID_HEIGHT - 1, mapY + 1);
                             neighborY++) {
                            for (int neighborX = std::max(0, mapX - 1);
                                 neighborX <= std::min(SQUARE_GRID_WIDTH - 1, mapX + 1);
                                 neighborX++) {
                                const double deltaX = worldX - neighborX;
                                const double deltaY = worldY - neighborY;
                                const double distance = deltaX * deltaX + deltaY * deltaY;
                                if (distance >= bestDistance) {
                                    continue;
                                }

                                const int neighborTile = neighborY * SQUARE_GRID_WIDTH
                                    + (SQUARE_GRID_WIDTH - 1 - neighborX);
                                const FloorTileFrmId neighborFid(cachedFloorFids[neighborTile]);
                                if ((neighborFid.flags() & TileFlags::TemporarilyHidden) != TileFlags::None) {
                                    continue;
                                }

                                const int neighborFrame = static_cast<int>(neighborFid.frameId<TileFrameId>());
                                if (neighborFrame < 0 || neighborFrame >= static_cast<int>(floorTextureIndices.size())) {
                                    continue;
                                }
                                const int textureIndex = floorTextureIndices[neighborFrame];
                                if (textureIndex < 0) {
                                    continue;
                                }

                                const ConstBuffer2D texture = floorTextures[textureIndex].getBuffer();
                                if (!texture || texture.width <= 0 || texture.height <= 0) {
                                    continue;
                                }
                                const double textureX = texture.width * 0.5
                                    + deltaX * (48.0 * texture.width / 80.0)
                                    + deltaY * (32.0 * texture.width / 80.0);
                                const double textureY = texture.height * 0.5
                                    - deltaX * (12.0 * texture.height / 36.0)
                                    + deltaY * (24.0 * texture.height / 36.0);
                                if (textureX < 0.0 || textureX >= texture.width
                                    || textureY < 0.0 || textureY >= texture.height) {
                                    continue;
                                }
                                const unsigned char color = texture.data[
                                    static_cast<int>(textureY) * texture.width + static_cast<int>(textureX)];
                                if (color != 0) {
                                    floorColor = color;
                                    bestDistance = distance;
                                }
                            }
                        }
                        const int cacheIndex = (mapY * SQUARE_GRID_WIDTH + mapX) * kFloorCacheCellArea
                            + sampleY * kFloorCacheCellResolution + sampleX;
                        cachedFloorColors[cacheIndex] = floorColor;
                    }
                }
            }
        }
        cachedFloorElevation = static_cast<int>(gElevation);
    }
    const unsigned int floorCacheEnd = profileFrame ? getTicks() : 0;
    if (profileFrame && floorLayoutChanged) {
        debugPrint("F11 floor cache rebuild: tick=%u tile=%d elevation=%d ms=%u\n",
            floorCacheEnd, gDude != nullptr ? gDude->tile : -1,
            static_cast<int>(gElevation), getTicksBetween(floorCacheEnd, floorCacheStart));
    }

    std::vector<double>& wallDepths = gFirstPersonPreviewWallDepths;
    wallDepths.assign(gTileWindowWidth, std::numeric_limits<double>::infinity());
    static std::vector<double> wallSillDepths;
    wallSillDepths.assign(gTileWindowWidth, std::numeric_limits<double>::infinity());
    static std::vector<TileFirstPersonDoorCapSpan> doorCapDepths;
    doorCapDepths.assign(gTileWindowWidth, TileFirstPersonDoorCapSpan {});
    bool hasDoorCaps = false;
    static std::vector<double> actorDepths;
    static std::vector<unsigned int> actorDepthStamps;
    static unsigned int actorDepthStamp = 0;
    const size_t actorDepthSize =
        static_cast<size_t>(gTileWindowWidth) * gTileWindowHeight;
    if (actorDepths.size() != actorDepthSize) {
        actorDepths.resize(actorDepthSize);
        actorDepthStamps.assign(actorDepthSize, 0);
        actorDepthStamp = 0;
    }
    if (++actorDepthStamp == 0) {
        std::fill(actorDepthStamps.begin(), actorDepthStamps.end(), 0);
        actorDepthStamp = 1;
    }

    // Solid walls and window sills cover a continuous span from the horizon
    // down to their floor edge. Door caps end above the floor and do not hide
    // it. Record actual raster bounds so floor clipping agrees at every edge.
    static std::vector<int> floorOcclusionBottoms;
    floorOcclusionBottoms.assign(gTileWindowWidth, horizonY);

    const unsigned int wallStart = profileFrame ? getTicks() : 0;
    for (int x = 0; x < gTileWindowWidth; x++) {
        const double rayDirectionX = rayDirectionXs[x];
        const double rayDirectionY = rayDirectionYs[x];

        // Wall objects are stored on the hex grid, which is twice as dense as
        // the square floor grid used by the ground projection.
        const double rayCameraX = cameraX * 2.0;
        const double rayCameraY = cameraY * 2.0;
        int mapX = static_cast<int>(rayCameraX);
        int mapY = static_cast<int>(rayCameraY);
        const double deltaDistanceX = rayDirectionX == 0.0
            ? 1.0e30
            : fabs(1.0 / rayDirectionX);
        const double deltaDistanceY = rayDirectionY == 0.0
            ? 1.0e30
            : fabs(1.0 / rayDirectionY);
        const int stepX = rayDirectionX < 0.0 ? -1 : 1;
        const int stepY = rayDirectionY < 0.0 ? -1 : 1;
        double sideDistanceX = (rayDirectionX < 0.0
                ? rayCameraX - mapX
                : mapX + 1.0 - rayCameraX)
            * deltaDistanceX;
        double sideDistanceY = (rayDirectionY < 0.0
                ? rayCameraY - mapY
                : mapY + 1.0 - rayCameraY)
            * deltaDistanceY;
        int side = 0;
        unsigned char wallType = 0;
        TileFirstPersonWallOrientation wallOrientation = TILE_FIRST_PERSON_WALL_ORIENTATION_CELL;
        double wallDistance = std::numeric_limits<double>::infinity();
        unsigned char windowType = 0;
        TileFirstPersonWallOrientation windowOrientation = TILE_FIRST_PERSON_WALL_ORIENTATION_CELL;
        double windowDistance = std::numeric_limits<double>::infinity();
        int windowSide = 0;
        double doorCapDistance = std::numeric_limits<double>::infinity();
        TileFirstPersonWallOrientation doorCapOrientation =
            TILE_FIRST_PERSON_WALL_ORIENTATION_CELL;
        int doorCapSide = 0;
        double cellEntryDistance = 0.0;
        while (mapX >= 0 && mapX < gHexGridWidth
            && mapY >= 0 && mapY < gHexGridHeight) {
            const int cellIndex = mapY * gHexGridWidth + mapX;
            wallType = gFirstPersonPreviewWallCells[cellIndex];
            if (wallType != 0) {
                wallOrientation =
                    static_cast<TileFirstPersonWallOrientation>(
                        gFirstPersonPreviewWallOrientations[cellIndex]);
                if (wallOrientation == TILE_FIRST_PERSON_WALL_ORIENTATION_CELL) {
                    const double hitDistance = std::max(0.1, cellEntryDistance / 2.0);
                    if (wallType == 2) {
                        if (windowDistance == std::numeric_limits<double>::infinity()) {
                            windowType = wallType;
                            windowOrientation = wallOrientation;
                            windowDistance = hitDistance;
                            windowSide = side;
                        }
                    } else if (wallType == 5) {
                        if (doorCapDistance == std::numeric_limits<double>::infinity()) {
                            doorCapDistance = hitDistance;
                            doorCapOrientation = wallOrientation;
                            doorCapSide = side;
                        }
                    } else {
                        wallDistance = hitDistance;
                        break;
                    }
                }

                const double cellExitDistance = std::min(sideDistanceX, sideDistanceY);
                constexpr double kIntersectionEpsilon = 1.0e-8;
                constexpr double kWallThickness = 0.4;
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
                    rayIntersectsWallBounds = rayCameraX >= wallMinX - kIntersectionEpsilon
                        && rayCameraX <= wallMaxX + kIntersectionEpsilon;
                } else {
                    nearX = (wallMinX - rayCameraX) / rayDirectionX;
                    farX = (wallMaxX - rayCameraX) / rayDirectionX;
                    if (nearX > farX) {
                        std::swap(nearX, farX);
                    }
                }

                if (rayIntersectsWallBounds) {
                    if (fabs(rayDirectionY) <= kIntersectionEpsilon) {
                        rayIntersectsWallBounds = rayCameraY >= wallMinY - kIntersectionEpsilon
                            && rayCameraY <= wallMaxY + kIntersectionEpsilon;
                    } else {
                        nearY = (wallMinY - rayCameraY) / rayDirectionY;
                        farY = (wallMaxY - rayCameraY) / rayDirectionY;
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
                    const double hitDistance = std::max(0.1, clippedWallEntryDistance / 2.0);
                    if (wallType == 2) {
                        if (windowDistance == std::numeric_limits<double>::infinity()) {
                            windowType = wallType;
                            windowOrientation = wallOrientation;
                            windowDistance = hitDistance;
                            windowSide = wallEntryDistance >= cellEntryDistance - kIntersectionEpsilon
                                ? nearX > nearY ? 0 : 1
                                : side;
                        }
                    } else if (wallType == 5) {
                        if (doorCapDistance == std::numeric_limits<double>::infinity()) {
                            doorCapDistance = hitDistance;
                            doorCapOrientation = wallOrientation;
                            doorCapSide = wallEntryDistance >= cellEntryDistance - kIntersectionEpsilon
                                ? nearX > nearY ? 0 : 1
                                : side;
                        }
                    } else {
                        wallDistance = hitDistance;
                        if (wallEntryDistance >= cellEntryDistance - kIntersectionEpsilon) {
                            side = nearX > nearY ? 0 : 1;
                        }
                        break;
                    }
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

            if (mapX < 0 || mapX >= gHexGridWidth
                || mapY < 0 || mapY >= gHexGridHeight) {
                break;
            }

        }

        if (wallDistance == std::numeric_limits<double>::infinity()
            && windowDistance == std::numeric_limits<double>::infinity()
            && doorCapDistance == std::numeric_limits<double>::infinity()) {
            continue;
        }

        wallDepths[x] = wallDistance;
        wallSillDepths[x] = windowDistance;
        if (wallDistance == std::numeric_limits<double>::infinity()) {
            wallType = windowType;
            wallOrientation = windowOrientation;
            wallDistance = windowDistance;
            side = windowSide;
        }
        if (wallType == 3 || wallType == 4) {
            const int hexColumn = gHexGridWidth - 1 - mapX;
            const int doorTile = mapY * gHexGridWidth + hexColumn;
            Object* object = objectFindFirstAtLocation(gElevation, doorTile);
            while (object != nullptr) {
                Proto* proto = nullptr;
                bool canInteract = false;
                if (protoGetProto(object, &proto) == 0) {
                    if (wallType == 3
                        && FrmId(object).objectType() == OBJ_TYPE_SCENERY
                        && proto->scenery.type == SCENERY_TYPE_DOOR
                        && (object->data.scenery.door.openFlags & DOOR_FLAG_OPEN) == 0
                        && (object->flags & OBJECT_OPEN_DOOR) == OBJECT_NONE) {
                        canInteract = true;
                    } else if (wallType == 4) {
                        const ObjectType objectType = FrmId(object).objectType();
                        canInteract = (objectType == OBJ_TYPE_ITEM && proto->item.type == ITEM_TYPE_CONTAINER)
                            || (objectType == OBJ_TYPE_SCENERY && _obj_action_can_use(object));
                    }
                }
                if (canInteract) {
                    if (wallType == 3) {
                        gFirstPersonPreviewDoorHitTargets.push_back({ object, wallDistance, x });
                    } else {
                        bool alreadyTracked = false;
                    }
                    break;
                }
                object = objectFindNextAtLocation();
            }
        }
        const auto renderWall = [&](unsigned char type, double distance,
                                    TileFirstPersonWallOrientation orientation, int wallSide) {
            const double projectedWallHeight = gTileWindowHeight / distance;
            const bool halfHeightWall = type == 2;
            const double wallHeight = halfHeightWall
                ? std::max(1.0, projectedWallHeight / 2.0)
                : projectedWallHeight * 2.0;
            const double floorY = horizonY + projectedWallHeight / 2.0;
            const double unclippedWallTop = halfHeightWall
                ? horizonY
                : floorY - wallHeight;
            const double doorSplitY = unclippedWallTop + wallHeight / 2.0;
            const int wallTop = std::max(0,
                static_cast<int>(ceil(unclippedWallTop)));
            const int wallBottom = std::min(gTileWindowHeight - 1,
                static_cast<int>(floor(type == 5 ? doorSplitY : floorY)));
            if (wallTop <= horizonY + 1 && wallBottom >= horizonY + 1) {
                floorOcclusionBottoms[x] = std::max(floorOcclusionBottoms[x], wallBottom);
            }
            const double hitWorldX = cameraX + rayDirectionX * distance;
            const double hitWorldY = cameraY + rayDirectionY * distance;
            const double wallCoordinate = orientation == TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH
                    ? hitWorldY
                    : orientation == TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST
                    ? hitWorldX
                    : wallSide == 0 ? hitWorldY : hitWorldX;
            int textureX = static_cast<int>(floor(wallCoordinate * kFirstPersonWallTextureSize))
                % kFirstPersonWallTextureSize;
            if (textureX < 0) {
                textureX += kFirstPersonWallTextureSize;
            }
            const double textureHeight = std::max(1.0, projectedWallHeight);
            const int faceShade = wallSide == 0 ? 116 : 96;
            const int distanceShade = std::min(48, static_cast<int>(distance * 7.0));
            const int light = std::max(48, faceShade - distanceShade)
                * ambientIntensity / LIGHT_INTENSITY_MAX;
            const int wallRenderStep = type == 3 || type == 5 ? 1 : 2;
            if (type == 5 && wallTop <= wallBottom) {
                hasDoorCaps = true;
                doorCapDepths[x] = { wallTop, wallBottom + 1, distance };
            }
            for (int y = wallTop; y <= wallBottom; y += wallRenderStep) {
                const bool brickTexture = type != 3 || y < doorSplitY;
                const auto& texture = brickTexture
                    ? gFirstPersonStoneWallTexture
                    : gFirstPersonDoorWallTexture;
                const double textureOffset = type == 3 && !brickTexture
                    ? y - doorSplitY
                    : y - unclippedWallTop;
                const double textureScaleHeight = type == 3 || type == 5
                    ? std::max(1.0, wallHeight / 2.0)
                    : textureHeight;
                int textureY = static_cast<int>(
                    textureOffset * kFirstPersonWallTextureSize / textureScaleHeight);
                textureY %= kFirstPersonWallTextureSize;
                const Color texel = texture[textureY * kFirstPersonWallTextureSize + textureX];
                const unsigned char color = intensityColorTable[texel][light];
                gTileWindowBuffer[y * gTileWindowPitch + x] = color;
                if (wallRenderStep == 2 && y + 1 <= wallBottom) {
                    gTileWindowBuffer[(y + 1) * gTileWindowPitch + x] = color;
                }
            }
        };
        if (windowDistance != std::numeric_limits<double>::infinity()
            && wallDepths[x] != std::numeric_limits<double>::infinity()) {
            renderWall(wallType, wallDepths[x], wallOrientation, side);
            renderWall(windowType, windowDistance, windowOrientation, windowSide);
        } else if (wallDistance != std::numeric_limits<double>::infinity()) {
            renderWall(wallType, wallDistance, wallOrientation, side);
        } else if (windowDistance != std::numeric_limits<double>::infinity()) {
            renderWall(windowType, windowDistance, windowOrientation, windowSide);
        }
        if (doorCapDistance != std::numeric_limits<double>::infinity()) {
            renderWall(5, doorCapDistance, doorCapOrientation, doorCapSide);
        }
    }

    const unsigned int wallEnd = profileFrame ? getTicks() : 0;

    const unsigned int floorStart = profileFrame ? getTicks() : 0;
    constexpr int kFloorRenderStep = 2;
    for (int y = horizonY + 1; y < gTileWindowHeight; y += kFloorRenderStep) {
        const double rowDistance = (gTileWindowHeight * 0.5) / (y - horizonY);
        double worldX = cameraX + rayDirectionXs[0] * rowDistance;
        double worldY = cameraY + rayDirectionYs[0] * rowDistance;
        const double worldXStep = gTileWindowWidth > 1
            ? (rayDirectionXs[std::min(kFloorRenderStep, gTileWindowWidth - 1)]
                    - rayDirectionXs[0]) * rowDistance
            : 0.0;
        const double worldYStep = gTileWindowWidth > 1
            ? (rayDirectionYs[std::min(kFloorRenderStep, gTileWindowWidth - 1)]
                    - rayDirectionYs[0]) * rowDistance
            : 0.0;
        for (int x = 0; x < gTileWindowWidth; x += kFloorRenderStep) {
            const int blockBottom = std::min(gTileWindowHeight, y + kFloorRenderStep);
            const int blockRight = std::min(gTileWindowWidth, x + kFloorRenderStep);
            int coveredThrough = floorOcclusionBottoms[x];
            for (int blockX = x + 1; blockX < blockRight; blockX++) {
                coveredThrough = std::min(coveredThrough, floorOcclusionBottoms[blockX]);
            }
            if (blockBottom - 1 <= coveredThrough) {
                // Preserve the original incremental coordinates even when a
                // sample is skipped, avoiding texture shifts at wall edges.
                worldX += worldXStep;
                worldY += worldYStep;
                continue;
            }
            const int mapX = static_cast<int>(floor(worldX));
            const int mapY = static_cast<int>(floor(worldY));
            if (mapX < 0 || mapX >= SQUARE_GRID_WIDTH
                || mapY < 0 || mapY >= SQUARE_GRID_HEIGHT) {
                worldX += worldXStep;
                worldY += worldYStep;
                continue;
            }

            const int sampleX = std::min(kFloorCacheCellResolution - 1,
                static_cast<int>((worldX - mapX) * kFloorCacheCellResolution));
            const int sampleY = std::min(kFloorCacheCellResolution - 1,
                static_cast<int>((worldY - mapY) * kFloorCacheCellResolution));
            const int cacheIndex = (mapY * SQUARE_GRID_WIDTH + mapX) * kFloorCacheCellArea
                + sampleY * kFloorCacheCellResolution + sampleX;
            const unsigned char color = intensityColorTable[
                cachedFloorColors[cacheIndex]][ambientIntensityIndex];
            for (int blockY = y;
                 blockY < blockBottom;
                 blockY++) {
                for (int blockX = x;
                     blockX < blockRight;
                     blockX++) {
                    // Partially covered blocks retain the same sample, but
                    // must not overwrite wall pixels drawn earlier.
                    if (blockY > floorOcclusionBottoms[blockX]) {
                        gTileWindowBuffer[blockY * gTileWindowPitch + blockX] = color;
                    }
                }
            }
            worldX += worldXStep;
            worldY += worldYStep;
        }
    }

    const unsigned int floorEnd = profileFrame ? getTicks() : 0;
    TileFirstPersonExitGridRenderContext exitGridContext = {
        gElevation,
        cameraX,
        cameraY,
        directionX,
        directionY,
        cameraPlaneX,
        cameraPlaneY,
        inverseDeterminant,
        horizonY,
        &wallDepths,
        &wallSillDepths,
        hasDoorCaps ? &doorCapDepths : nullptr,
    };
    tileFirstPersonRenderExitGridMarkers(gFirstPersonPreviewExitGridTiles, &exitGridContext);

    TileFirstPersonActorRenderContext actorContext = {
        gElevation,
        cameraX,
        cameraY,
        directionX,
        directionY,
        cameraPlaneX,
        cameraPlaneY,
        inverseDeterminant,
        horizonY,
        &wallDepths,
        &wallSillDepths,
        hasDoorCaps ? &doorCapDepths : nullptr,
        &actorDepths,
        &actorDepthStamps,
        actorDepthStamp,
        &gFirstPersonPreviewActorHitTargets,
    };
    const unsigned int actorStart = profileFrame ? getTicks() : 0;
    objectVisitAll(tileFirstPersonRenderActor, &actorContext);
    const unsigned int actorEnd = profileFrame ? getTicks() : 0;
    tileFirstPersonDrawMoveMarker(directionX, directionY);
    tileFirstPersonDrawCrosshair(directionX, directionY);
    tileRenderFirstPersonTopologyOverlay();

    if (profileFrame) {
        const unsigned int frameEnd = getTicks();
        const unsigned int totalMs = getTicksBetween(frameEnd, frameStart);
        const unsigned int floorMs = getTicksBetween(floorEnd, floorStart);
        const unsigned int wallMs = getTicksBetween(wallEnd, wallStart);
        const unsigned int actorMs = getTicksBetween(actorEnd, actorStart);
        // Averages hide isolated doorway hitches. Include setup/cache and
        // overlay time, which are outside the three raster/submission stages.
        if (totalMs >= 50) {
            debugPrint("F11 slow frame: tick=%u tile=%d elevation=%d total=%u setup=%u cache=%u rebuilt=%d floor=%u wall=%u actors=%u overlay=%u ms\n",
                frameEnd, gDude != nullptr ? gDude->tile : -1,
                static_cast<int>(gElevation), totalMs,
                getTicksBetween(wallStart, frameStart),
                getTicksBetween(floorCacheEnd, floorCacheStart),
                floorLayoutChanged ? 1 : 0, floorMs, wallMs, actorMs,
                getTicksBetween(frameEnd, actorEnd));
        }
        gFirstPersonPreviewProfileFrames++;
        gFirstPersonPreviewProfileTotalMs += totalMs;
        gFirstPersonPreviewProfileFloorMs += getTicksBetween(floorEnd, floorStart);
        gFirstPersonPreviewProfileWallMs += getTicksBetween(wallEnd, wallStart);
        gFirstPersonPreviewProfileActorMs += getTicksBetween(actorEnd, actorStart);
        if (gFirstPersonPreviewProfileFrames >= 60) {
            const double frameCount = gFirstPersonPreviewProfileFrames;
            debugPrint("F11 timing avg ms: total=%.2f floor=%.2f wall=%.2f actors=%.2f\n",
                gFirstPersonPreviewProfileTotalMs / frameCount,
                gFirstPersonPreviewProfileFloorMs / frameCount,
                gFirstPersonPreviewProfileWallMs / frameCount,
                gFirstPersonPreviewProfileActorMs / frameCount);
            gFirstPersonPreviewProfileFrames = 0;
            gFirstPersonPreviewProfileTotalMs = 0;
            gFirstPersonPreviewProfileFloorMs = 0;
            gFirstPersonPreviewProfileWallMs = 0;
            gFirstPersonPreviewProfileActorMs = 0;
        }
    }
}

static void tileFirstPersonSnapshotTopologyObject(Object* object, void* context)
{
    if (object->elevation != gElevation
        || (object->flags & OBJECT_HIDDEN) != OBJECT_NONE
        || !tileIsValid(object->tile)) {
        return;
    }

    const ObjectType objectType = FrmId(object).objectType();
    if (objectType != OBJ_TYPE_WALL
        && objectType != OBJ_TYPE_SCENERY
        && objectType != OBJ_TYPE_CRITTER
        && objectType != OBJ_TYPE_ITEM) {
        return;
    }
    if (artIsObjectTypeHidden(objectType)) {
        return;
    }

    Proto* proto = nullptr;
    const bool hasProto = protoGetProto(object, &proto) == 0;
    const bool isDoor = objectType == OBJ_TYPE_SCENERY
        && hasProto
        && proto->scenery.type == SCENERY_TYPE_DOOR;
    const bool openDoor = isDoor
        && (((object->data.scenery.door.openFlags & DOOR_FLAG_OPEN) != 0)
            || (object->flags & OBJECT_OPEN_DOOR) != OBJECT_NONE);

    Color color = COLOR_LIGHT_GREY;
    switch (objectType) {
    case OBJ_TYPE_WALL:
        color = (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
                && (object->flags & (OBJECT_TRANS_GLASS | OBJECT_TRANS_WALL | OBJECT_LIGHT_THRU)) != OBJECT_NONE
            ? COLOR_LIGHT_BLUE
            : (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE ? COLOR_LIGHT_RED : COLOR_AMBER;
        break;
    case OBJ_TYPE_SCENERY:
        color = isDoor
                ? (openDoor || (object->flags & OBJECT_NO_BLOCK) != OBJECT_NONE
                    ? COLOR_LIGHT_GREEN_3
                    : COLOR_LIGHT_RED)
                : ((object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
                    ? COLOR_LIGHT_GOLD
                    : COLOR_YELLOW_2);
        break;
    case OBJ_TYPE_CRITTER:
        color = COLOR_LIGHT_GREEN_3;
        break;
    case OBJ_TYPE_ITEM:
        color = COLOR_CYAN;
        break;
    default:
        break;
    }

    unsigned int cornerFlags = 0;
    if (hasProto) {
        cornerFlags = objectType == OBJ_TYPE_WALL
            ? static_cast<unsigned int>(proto->wall.extendedFlags)
            : objectType == OBJ_TYPE_SCENERY
                ? static_cast<unsigned int>(proto->scenery.extendedFlags)
                : 0;
    }

    const bool halfHeightWall = objectType == OBJ_TYPE_WALL
        && (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
        && (object->flags & (OBJECT_TRANS_GLASS | OBJECT_TRANS_WALL | OBJECT_LIGHT_THRU)) != OBJECT_NONE;
    const bool structural = (objectType == OBJ_TYPE_WALL
            || (objectType == OBJ_TYPE_SCENERY && isDoor))
        && !openDoor
        && (halfHeightWall
            || (object->flags & (OBJECT_NO_BLOCK | OBJECT_LIGHT_THRU)) == OBJECT_NONE);

    TileFirstPersonWallOrientation wallOrientation =
        TILE_FIRST_PERSON_WALL_ORIENTATION_CELL;
    if (objectType == OBJ_TYPE_WALL && hasProto) {
        constexpr unsigned int kCornerFlags = PROTO_EXT_FLAG_NORTH_CORNER
            | PROTO_EXT_FLAG_SOUTH_CORNER | PROTO_EXT_FLAG_EAST_CORNER
            | PROTO_EXT_FLAG_WEST_CORNER;
        if ((cornerFlags & kCornerFlags) == 0) {
            wallOrientation = (cornerFlags & PROTO_EXT_FLAG_HIDDEN) != 0
                ? TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST
                : TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH;
        }
    }

    auto* markers = static_cast<std::vector<TileFirstPersonTopologyMarker>*>(context);
    markers->push_back({
        object->tile,
        objectType,
        color,
        cornerFlags,
        object->rotation,
        wallOrientation,
        structural,
        isDoor,
        openDoor,
    });
}

static void tileFirstPersonRenderExitGridMarkers(
    const std::vector<int>& tiles,
    const TileFirstPersonExitGridRenderContext* context)
{
    if (tiles.empty()) {
        return;
    }

    static std::vector<unsigned int> visited;
    static std::vector<int> pending;
    static std::vector<int> component;
    static unsigned int visitStamp = 0;
    if (visited.size() != static_cast<size_t>(gHexGridSize)) {
        visited.assign(gHexGridSize, 0);
        visitStamp = 0;
    }
    if (++visitStamp == 0) {
        std::fill(visited.begin(), visited.end(), 0);
        visitStamp = 1;
    }

    for (int tile : tiles) {
        if (visited[tile] == visitStamp) {
            continue;
        }

        pending.clear();
        component.clear();
        pending.push_back(tile);
        visited[tile] = visitStamp;
        double centerX = 0.0;
        double centerY = 0.0;
        while (!pending.empty()) {
            const int currentTile = pending.back();
            pending.pop_back();
            component.push_back(currentTile);
            double worldX;
            double worldY;
            if (tileFirstPersonGetWorldPosition(currentTile, &worldX, &worldY)) {
                centerX += worldX;
                centerY += worldY;
            }

            for (int rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
                const int adjacentTile = tileGetTileInDirection(
                    currentTile, static_cast<Rotation>(rotation), 1);
                if (!tileIsValid(adjacentTile)
                    || visited[adjacentTile] == visitStamp
                    || !std::binary_search(tiles.begin(), tiles.end(), adjacentTile)) {
                    continue;
                }
                visited[adjacentTile] = visitStamp;
                pending.push_back(adjacentTile);
            }
        }

        centerX /= component.size();
        centerY /= component.size();
        int representativeTile = component.front();
        double closestDistance = std::numeric_limits<double>::infinity();
        for (int componentTile : component) {
            double worldX;
            double worldY;
            if (!tileFirstPersonGetWorldPosition(componentTile, &worldX, &worldY)) {
                continue;
            }
            const double deltaX = worldX - centerX;
            const double deltaY = worldY - centerY;
            const double distance = deltaX * deltaX + deltaY * deltaY;
            if (distance < closestDistance) {
                closestDistance = distance;
                representativeTile = componentTile;
            }
        }
        tileFirstPersonRenderExitGridMarker(representativeTile, context);
    }
}

static double tileFirstPersonGetWallDepthAt(
    int x,
    int y,
    int horizonY,
    const std::vector<double>* wallDepths,
    const std::vector<double>* wallSillDepths,
    const std::vector<TileFirstPersonDoorCapSpan>* doorCapDepths)
{
    double depth = y < horizonY
        ? (*wallDepths)[x]
        : std::min((*wallDepths)[x], (*wallSillDepths)[x]);
    if (doorCapDepths != nullptr) {
        const TileFirstPersonDoorCapSpan& cap = (*doorCapDepths)[x];
        if (y >= cap.top && y < cap.bottom) {
            depth = std::min(depth, cap.depth);
        }
    }
    return depth;
}

static void tileFirstPersonRenderExitGridMarker(
    int tile,
    const TileFirstPersonExitGridRenderContext* renderContext)
{
    double objectX;
    double objectY;
    if (!tileFirstPersonGetWorldPosition(tile, &objectX, &objectY)) {
        return;
    }

    const double relativeX = objectX - renderContext->cameraX;
    const double relativeY = objectY - renderContext->cameraY;
    const double transformX = renderContext->inverseDeterminant
        * (renderContext->directionY * relativeX - renderContext->directionX * relativeY);
    const double transformY = renderContext->inverseDeterminant
        * (-renderContext->cameraPlaneY * relativeX + renderContext->cameraPlaneX * relativeY);
    if (transformY <= 0.1) {
        return;
    }

    const double projectionScale = gTileWindowHeight / transformY;
    const int centerX = static_cast<int>(
        (gTileWindowWidth / 2.0) * (1.0 + transformX / transformY));
    const int radiusX = std::max(3, static_cast<int>(projectionScale * 0.42));
    const int radiusY = std::max(1, radiusX / 5);
    const int baseY = renderContext->horizonY + static_cast<int>(projectionScale * 0.5);
    const int topY = baseY - static_cast<int>(projectionScale * 0.38);
    const int openingY = topY + radiusY;
    if (centerX + radiusX < 0 || centerX - radiusX >= gTileWindowWidth
        || baseY < 0 || openingY - radiusY >= gTileWindowHeight) {
        return;
    }

    const int firstX = std::max(0, centerX - radiusX);
    const int lastX = std::min(gTileWindowWidth - 1, centerX + radiusX);
    const int firstY = std::max(0, openingY - radiusY);
    const int lastY = std::min(gTileWindowHeight - 1, baseY);
    for (int y = firstY; y <= lastY; y++) {
        for (int x = firstX; x <= lastX; x++) {
            const double wallDepth = tileFirstPersonGetWallDepthAt(
                x,
                y,
                renderContext->horizonY,
                renderContext->wallDepths,
                renderContext->wallSillDepths,
                renderContext->doorCapDepths);
            if (transformY >= wallDepth) {
                continue;
            }

            const double normalizedX = static_cast<double>(x - centerX) / radiusX;
            const double normalizedY = static_cast<double>(y - openingY) / radiusY;
            unsigned char color = 0;
            if (normalizedX * normalizedX + normalizedY * normalizedY <= 1.0) {
                const double openingDistance = normalizedX * normalizedX
                    + normalizedY * normalizedY;
                color = openingDistance > 0.48 ? COLOR_LIGHT_RED
                    : openingDistance > 0.30 ? COLOR_RED
                    : COLOR_DARK_RED;
            } else if (y >= openingY && y <= baseY) {
                const double bodyProgress = static_cast<double>(y - openingY)
                    / std::max(1, baseY - openingY);
                const double sideRadius = 0.82 + bodyProgress * 0.18;
                if (fabs(normalizedX) <= sideRadius) {
                    if (fabs(normalizedX) > sideRadius - 0.12) {
                        color = COLOR_DARK_RED;
                    } else if (static_cast<int>(bodyProgress * 8.0) % 4 == 0
                        || fabs(normalizedX) < 0.06) {
                        color = COLOR_RED_2;
                    } else {
                        color = COLOR_RED;
                    }
                }
            }

            if (color != 0) {
                gTileWindowBuffer[y * gTileWindowPitch + x] = color;
            }
        }
    }
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

    if (isExitGridProtoId(object)) {
        renderContext->exitGridTiles->push_back(object->tile);
    }

    const ObjectType objectType = FrmId(object).objectType();
    if (objectType != OBJ_TYPE_WALL && objectType != OBJ_TYPE_SCENERY) {
        return;
    }

    const bool halfHeightWall = objectType == OBJ_TYPE_WALL
        && (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
        && (object->flags & (OBJECT_TRANS_GLASS | OBJECT_TRANS_WALL | OBJECT_LIGHT_THRU)) != OBJECT_NONE;
    Proto* proto = nullptr;
    const bool hasProto = protoGetProto(object, &proto) == 0;
    const bool isDoor = objectType == OBJ_TYPE_SCENERY
        && hasProto
        && proto->scenery.type == SCENERY_TYPE_DOOR;
    const bool doorOpen = isDoor
        && ((object->data.scenery.door.openFlags & DOOR_FLAG_OPEN) != 0
            || (object->flags & OBJECT_OPEN_DOOR) != OBJECT_NONE);
    if (!halfHeightWall
        && !doorOpen
        && (object->flags & (OBJECT_NO_BLOCK | OBJECT_LIGHT_THRU)) != OBJECT_NONE) {
        return;
    }

    if (renderContext->gridWidth <= 0) {
        return;
    }
    const int hexRow = object->tile / renderContext->gridWidth;
    const int hexColumn = renderContext->gridWidth - 1 - object->tile % renderContext->gridWidth;
    const int renderHexTile = hexRow * renderContext->gridWidth + hexColumn;
    if (renderHexTile < 0 || renderHexTile >= gHexGridSize) {
        return;
    }

    unsigned char wallType = 0;
    TileFirstPersonWallOrientation wallOrientation =
        TILE_FIRST_PERSON_WALL_ORIENTATION_CELL;
    if (objectType == OBJ_TYPE_WALL) {
        wallType = halfHeightWall ? 2 : 1;
        if (hasProto) {
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
    } else if (isDoor) {
        // Some door objects do not keep the generic pass-through object
        // flags in sync with their authoritative door state.
        if (doorOpen) {
            wallType = 5;
        } else {
            wallType = 3;
        }
    } else {
        // Scenery is rendered as a billboard from its FRM below. Do not turn
        // it into a solid graybox wall, otherwise trees and other scenery
        // obscure their own textures.
        return;
    }

    // Fallout's object grid stores X in reverse order. Convert it to the
    // renderer's east-to-west world coordinates without collapsing to a square tile.
    unsigned char& cellWallType = renderContext->wallCells[renderHexTile];
    if (cellWallType == 0 || wallType == 3 || wallType == 5) {
        cellWallType = wallType;
        renderContext->wallOrientations[renderHexTile] = wallOrientation;
    }
}

static void tileFirstPersonRenderActor(Object* object, void* context)
{
    const TileFirstPersonActorRenderContext* renderContext =
        static_cast<const TileFirstPersonActorRenderContext*>(context);
    const ObjectType objectType = FrmId(object).objectType();
    if (object == gDude
        || object->elevation != renderContext->elevation
        || (object->flags & OBJECT_HIDDEN) != OBJECT_NONE
        || !tileIsValid(object->tile)
        || (objectType != OBJ_TYPE_CRITTER
            && objectType != OBJ_TYPE_ITEM
            && objectType != OBJ_TYPE_SCENERY)) {
        return;
    }

    if (objectType == OBJ_TYPE_SCENERY) {
        Proto* proto = nullptr;
        if (protoGetProto(object, &proto) == 0
            && proto->scenery.type == SCENERY_TYPE_DOOR) {
            return;
        }
    }

    double objectX;
    double objectY;
    if (!tileFirstPersonGetWorldPosition(object->tile, &objectX, &objectY)) {
        return;
    }

    const double relativeX = objectX - renderContext->cameraX;
    const double relativeY = objectY - renderContext->cameraY;
    const double transformX = renderContext->inverseDeterminant
        * (renderContext->directionY * relativeX - renderContext->directionX * relativeY);
    const double transformY = renderContext->inverseDeterminant
        * (-renderContext->cameraPlaneY * relativeX + renderContext->cameraPlaneX * relativeY);
    if (transformY <= 0.1) {
        return;
    }


    const int screenCenterX = static_cast<int>(
        (gTileWindowWidth / 2.0) * (1.0 + transformX / transformY));
    const int projectedRadius = static_cast<int>(
        std::ceil(gTileWindowHeight / transformY));
    if (screenCenterX + projectedRadius < 0
        || screenCenterX - projectedRadius >= gTileWindowWidth) {
        return;
    }
    const int occlusionRadius = std::max(2, projectedRadius * 2 + 2);
    const int occlusionLeft = std::max(0, screenCenterX - occlusionRadius);
    const int occlusionRight = std::min(gTileWindowWidth, screenCenterX + occlusionRadius + 1);
    bool potentiallyVisible = false;
    for (int x = occlusionLeft; x < occlusionRight; x++) {
        const double lowerDepth = std::min((*renderContext->wallDepths)[x],
            (*renderContext->wallSillDepths)[x]);
        if (transformY < (*renderContext->wallDepths)[x]
            || transformY < lowerDepth) {
            potentiallyVisible = true;
            break;
        }
    }
    if (!potentiallyVisible) {
        return;
    }
    Rotation facingRotation = ROTATION_NE;
    if (objectType == OBJ_TYPE_CRITTER) {
        facingRotation = rotationIsValid(object->rotation)
            ? object->rotation
            : ROTATION_NE;
    }
    if (objectType != OBJ_TYPE_CRITTER) {
        facingRotation = tileFirstPersonGetRotationToward(object->tile,
            renderContext->cameraX,
            renderContext->cameraY);
    }

    const double projectionScale = gTileWindowHeight / transformY;
    constexpr double kCritterFullSizeDistance = 0.75;
    constexpr double kCritterReducedSizeDistance = 1.5;
    constexpr double kMinimumCritterScale = 0.75;
    const double critterScale = std::clamp(
        (kCritterReducedSizeDistance - transformY)
            / (kCritterReducedSizeDistance - kCritterFullSizeDistance),
        kMinimumCritterScale,
        1.0);
    const double actorProjectionScale = objectType == OBJ_TYPE_CRITTER
        ? projectionScale * critterScale
        : projectionScale;
    bool isTree = false;
    bool isWideScenery = false;
    bool isMediumScenery = false;
    bool isTallScenery = false;
    bool isRock = false;
    bool isTinyScenery = false;

            
    double fallbackHeightScale = 0.55;
    double fallbackWidthScale = 1.0;
    bool fallbackTallScenery = false;
    bool fallbackTinyScenery = false;
    if (objectType == OBJ_TYPE_SCENERY) {
        const SceneryProtoTypeId sceneryProtoId = ProtoId(object).protoId<SceneryProtoTypeId>();
        struct SceneryProfile {
            bool wide;
            bool medium;
            bool tall;
            bool rock;
            bool tiny;
        };
        static std::unordered_map<int, SceneryProfile> sceneryProfiles;
        const int sceneryProfileKey = static_cast<int>(sceneryProtoId);
        const auto cachedProfile = sceneryProfiles.find(sceneryProfileKey);
        if (cachedProfile != sceneryProfiles.end()) {
            isWideScenery = cachedProfile->second.wide;
            isMediumScenery = cachedProfile->second.medium;
            isTallScenery = cachedProfile->second.tall;
            isRock = cachedProfile->second.rock;
            isTinyScenery = cachedProfile->second.tiny;
        } else {
        isTinyScenery = sceneryProtoId == SceneryProtoTypeId::SmallBowl
        || sceneryProtoId == SceneryProtoTypeId::TempleRubble
        || sceneryProtoId == SceneryProtoTypeId::Rubble1
        || sceneryProtoId == SceneryProtoTypeId::Rocks
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt9
        || sceneryProtoId == static_cast<SceneryProtoTypeId>(91)
        || sceneryProtoId == static_cast<SceneryProtoTypeId>(429)
        || sceneryProtoId == static_cast<SceneryProtoTypeId>(430)
        || sceneryProtoId == static_cast<SceneryProtoTypeId>(432)
        
        || sceneryProtoId == SceneryProtoTypeId::Unknown1;

        isMediumScenery = sceneryProtoId == SceneryProtoTypeId::Mailbox
        || sceneryProtoId == SceneryProtoTypeId::FireHydrant
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt2
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt3
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt4
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt5
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt6
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt7
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt8
        || sceneryProtoId == SceneryProtoTypeId::RocksAlt11
        || sceneryProtoId == SceneryProtoTypeId::BedAlt10
        || sceneryProtoId == SceneryProtoTypeId::BedAlt9
        || sceneryProtoId == SceneryProtoTypeId::OldMattressAlt
        || sceneryProtoId == SceneryProtoTypeId::Bed


        || sceneryProtoId == static_cast<SceneryProtoTypeId>(435)
        || sceneryProtoId == static_cast<SceneryProtoTypeId>(433)
        || sceneryProtoId == static_cast<SceneryProtoTypeId>(947)
        || sceneryProtoId == static_cast<SceneryProtoTypeId>(278)


        || sceneryProtoId == SceneryProtoTypeId::RocksAlt12;



        isWideScenery = sceneryProtoId == SceneryProtoTypeId::BucknerHouseSign


        || sceneryProtoId == SceneryProtoTypeId::Stove
        || sceneryProtoId == SceneryProtoTypeId::StoveAlt

        || sceneryProtoId == SceneryProtoTypeId::TableAlt11
        || sceneryProtoId == SceneryProtoTypeId::TableAlt12
        || sceneryProtoId == SceneryProtoTypeId::TableAlt13


        || sceneryProtoId == SceneryProtoTypeId::Desk
        || sceneryProtoId == SceneryProtoTypeId::DeskAlt

        || sceneryProtoId == SceneryProtoTypeId::Brace
        || sceneryProtoId == SceneryProtoTypeId::BraceAlt

        || sceneryProtoId == SceneryProtoTypeId::BedAlt
        || sceneryProtoId == SceneryProtoTypeId::BedAlt2
        || sceneryProtoId == SceneryProtoTypeId::BedAlt3
        || sceneryProtoId == SceneryProtoTypeId::BedAlt4
        || sceneryProtoId == SceneryProtoTypeId::BedAlt5
        || sceneryProtoId == SceneryProtoTypeId::BedAlt6
        || sceneryProtoId == SceneryProtoTypeId::BedAlt7
        || sceneryProtoId == SceneryProtoTypeId::BedAlt8
        || sceneryProtoId == SceneryProtoTypeId::Boxes
        || sceneryProtoId == SceneryProtoTypeId::BoxesAlt
        || sceneryProtoId == SceneryProtoTypeId::Crate
        || sceneryProtoId == SceneryProtoTypeId::CrateAlt
        || sceneryProtoId == SceneryProtoTypeId::StackOfTires
        || sceneryProtoId == SceneryProtoTypeId::Table
        || sceneryProtoId == SceneryProtoTypeId::TableAlt
        || sceneryProtoId == SceneryProtoTypeId::TableAlt2
        || sceneryProtoId == SceneryProtoTypeId::BathTub
        || sceneryProtoId == SceneryProtoTypeId::BoxesAlt2
        || sceneryProtoId == SceneryProtoTypeId::BoxesAlt3

        || sceneryProtoId == SceneryProtoTypeId::TableAlt9
        || sceneryProtoId == SceneryProtoTypeId::TableAlt10
        || sceneryProtoId == SceneryProtoTypeId::Bar
        || sceneryProtoId == SceneryProtoTypeId::Rocks;

        isTallScenery = sceneryProtoId == SceneryProtoTypeId::LampPost
        || sceneryProtoId == SceneryProtoTypeId::TreeAlt
        || sceneryProtoId == SceneryProtoTypeId::TreeAlt2
        || sceneryProtoId == SceneryProtoTypeId::TreeAlt3
        || sceneryProtoId == SceneryProtoTypeId::TreeAlt4

        || sceneryProtoId == SceneryProtoTypeId::Plant
        || sceneryProtoId == SceneryProtoTypeId::PlantAlt
        || sceneryProtoId == SceneryProtoTypeId::PlantAlt2
        || sceneryProtoId == SceneryProtoTypeId::PlantAlt3
        || sceneryProtoId == SceneryProtoTypeId::PlantAlt4
        || sceneryProtoId == SceneryProtoTypeId::PlantAlt5

        || sceneryProtoId == SceneryProtoTypeId::Rope
        || sceneryProtoId == SceneryProtoTypeId::RopeAlt
        || sceneryProtoId == SceneryProtoTypeId::RopeAlt2
        || sceneryProtoId == SceneryProtoTypeId::RopeAlt3
        || sceneryProtoId == SceneryProtoTypeId::RopeAlt4

        || sceneryProtoId == SceneryProtoTypeId::LampPost
        || sceneryProtoId == SceneryProtoTypeId::LampPostAlt

        || sceneryProtoId == SceneryProtoTypeId::StreetLight
        || sceneryProtoId == SceneryProtoTypeId::StreetLightAlt
        || sceneryProtoId == SceneryProtoTypeId::StreetLightAlt2
        || sceneryProtoId == SceneryProtoTypeId::StreetLightAlt3

        || sceneryProtoId == SceneryProtoTypeId::Poster
        || sceneryProtoId == SceneryProtoTypeId::MirrorTall
        || sceneryProtoId == SceneryProtoTypeId::VicsSign

        || sceneryProtoId == SceneryProtoTypeId::BathHouseSign
        || sceneryProtoId == SceneryProtoTypeId::GoldenGeckoSign
        || sceneryProtoId == SceneryProtoTypeId::Signpost
        || sceneryProtoId == SceneryProtoTypeId::StopSign
        || sceneryProtoId == SceneryProtoTypeId::Graffiti
        || sceneryProtoId == SceneryProtoTypeId::GraffitiAlt

        || sceneryProtoId == SceneryProtoTypeId::SignpostAlt
        || sceneryProtoId == SceneryProtoTypeId::RopeAlt5
        || sceneryProtoId == SceneryProtoTypeId::GraffitiAlt2
        || sceneryProtoId == SceneryProtoTypeId::BrokenPost
        || sceneryProtoId == SceneryProtoTypeId::unknown1
        || sceneryProtoId == SceneryProtoTypeId::unknown2
        || sceneryProtoId == SceneryProtoTypeId::unknown3


        || sceneryProtoId == SceneryProtoTypeId::Tree;

        

            sceneryProfiles.emplace(sceneryProfileKey,
                SceneryProfile {
                    isWideScenery,
                    isMediumScenery,
                    isTallScenery,
                    isRock,
                    isTinyScenery,
                });
        }

        CacheEntry* cacheHandle = nullptr;
        Art* art = artLock(FrmId(object), &cacheHandle);
        if (art != nullptr) {
            int artWidth;
            int artHeight;
            if (artGetSize(art, object->frame, facingRotation, &artWidth, &artHeight) == 0) {
                isTree = artHeight >= 120 && artHeight > artWidth * 1.25;

                isTree = artHeight >= 120 && artHeight > artWidth * 1.25;

                const double aspect =
                    artWidth > 0
                        ? static_cast<double>(artHeight) / artWidth
                        : 1.0;

                if (artHeight >= 120 || aspect > 2.5) {
                    fallbackTallScenery = true;
                    fallbackHeightScale = 1.8;
                } else if (artHeight >= 70) {
                    fallbackHeightScale = 0.8;
                } else if (artWidth >= 80) {
                    fallbackHeightScale = 0.7;
                    fallbackWidthScale = 1.4;
                }

                // debugPrint(
                //     "Scenery proto=%d size=%dx%d\n",
                //     static_cast<int>(sceneryProtoId),
                //     artWidth,
                //     artHeight);

            }
            artUnlock(cacheHandle);

            isTallScenery = isTallScenery || fallbackTallScenery;

            isTinyScenery = isTinyScenery || fallbackTinyScenery;

            if (!isWideScenery && fallbackWidthScale > 1.0) {
                isWideScenery = true;
            }


            
            // if (!isTallScenery
            //     && artHeight >= 70
            //     && artHeight > artWidth * 1.5) {
            //     debugPrint(
            //         "Tall candidate: proto=%d size=%dx%d\n",
            //         static_cast<int>(sceneryProtoId),
            //         artWidth,
            //         artHeight);
            // }
        }
    }


    const bool isDeadCritter = objectType == OBJ_TYPE_CRITTER && critterIsDead(object);
    const double objectHeightScale =
        objectType == OBJ_TYPE_CRITTER
            ? (isDeadCritter ? 0.5 : 0.9)
            : isTree ? 2.0
            : isTallScenery ? 1.8
            : isTinyScenery ? 0.15
            : isMediumScenery ? 0.4
            : fallbackHeightScale;

        const double objectWidthScale =
            isWideScenery ? 1.8
            : isMediumScenery ? 1.3
            : fallbackWidthScale;
    const int projectedActorHeight = std::max(1, static_cast<int>(actorProjectionScale * objectHeightScale));
    const int actorHeight = objectType == OBJ_TYPE_CRITTER
        ? std::min(
            std::max(1, static_cast<int>(gTileWindowHeight * 0.8)),
            projectedActorHeight)
        : projectedActorHeight;
    const double critterBottomLeeway = isDeadCritter ? 0.5 : 0.2;
    const int projectedActorBottom = renderContext->horizonY
        + static_cast<int>(projectionScale * 0.5);
    const int actorBottom = isRock
        ? projectedActorBottom + std::max(20, actorHeight / 3)
        : objectType == OBJ_TYPE_CRITTER
            ? std::min(
                gTileWindowHeight + static_cast<int>(actorHeight * critterBottomLeeway),
                projectedActorBottom)
        : projectedActorBottom;

    const int spriteWidth = std::max(1, static_cast<int>(
        actorHeight * 0.42 / objectHeightScale * objectWidthScale));
    const int spriteTop = actorBottom - actorHeight;
    const int spriteBottom = actorBottom;
    const int spriteLeft = screenCenterX - spriteWidth / 2;
    const int spriteRight = spriteLeft + spriteWidth;
    if (spriteRight <= 0 || spriteLeft >= gTileWindowWidth
        || spriteBottom <= 0 || spriteTop >= gTileWindowHeight) {
        return;
    }
    const int firstColumn = std::max(0, spriteLeft);
    const int lastColumn = std::min(gTileWindowWidth, spriteRight);
    const int upperTop = std::max(0, spriteTop);
    const int upperBottom = std::min(renderContext->horizonY, spriteBottom);
    const int lowerTop = std::max(renderContext->horizonY, spriteTop);
    const int lowerBottom = std::min(gTileWindowHeight, spriteBottom);
    bool upperVisible = upperTop >= upperBottom;
    bool lowerVisible = lowerTop >= lowerBottom;
    for (int x = firstColumn; x < lastColumn && (!upperVisible || !lowerVisible); x++) {
        if (!upperVisible && transformY < (*renderContext->wallDepths)[x]) {
            upperVisible = true;
        }
        if (!lowerVisible
            && transformY < std::min((*renderContext->wallDepths)[x],
                (*renderContext->wallSillDepths)[x])) {
            lowerVisible = true;
        }
    }
    if (!upperVisible && !lowerVisible) {
        return;
    }
    Color actorColor = COLOR_AMBER;
    if (objectType == OBJ_TYPE_CRITTER) {
        if (objectIsPartyMember(object)
            || (gDude != nullptr
                && object->data.critter.combat.team == gDude->data.critter.combat.team)) {
            actorColor = COLOR_LIGHT_GREEN_3;
        } else if (isInCombat()) {
            actorColor = COLOR_LIGHT_RED;
        }
    }

    FrmImage image;
    if (image.lock(FrmId(object), object->frame, facingRotation)) {
        const ConstBuffer2D frame = image.getBuffer();
        if (frame.width <= 0 || frame.height <= 0) {
            return;
        }
        const int artHeight = actorHeight;
        const int artWidth = std::max(1,
            static_cast<int>(artHeight * frame.width / static_cast<double>(frame.height)
                * objectWidthScale));
        struct SpriteBounds {
            int width;
            int height;
            int left;
            int top;
            int right;
            int bottom;
        };
        static std::unordered_map<const unsigned char*, SpriteBounds> spriteBoundsCache;
        SpriteBounds spriteBounds {};
        const auto spriteBoundsIt = spriteBoundsCache.find(frame.data);
        if (spriteBoundsIt != spriteBoundsCache.end()
            && spriteBoundsIt->second.width == frame.width
            && spriteBoundsIt->second.height == frame.height) {
            spriteBounds = spriteBoundsIt->second;
        } else {
            spriteBounds = {
                frame.width,
                frame.height,
                frame.width,
                frame.height,
                -1,
                -1,
            };
            for (int sourceY = 0; sourceY < frame.height; sourceY++) {
                for (int sourceX = 0; sourceX < frame.width; sourceX++) {
                    if (frame.data[sourceY * frame.width + sourceX] != 0) {
                        spriteBounds.left = std::min(spriteBounds.left, sourceX);
                        spriteBounds.top = std::min(spriteBounds.top, sourceY);
                        spriteBounds.right = std::max(spriteBounds.right, sourceX);
                        spriteBounds.bottom = std::max(spriteBounds.bottom, sourceY);
                    }
                }
            }
            spriteBoundsCache[frame.data] = spriteBounds;
        }
        if (spriteBounds.right < 0) {
            return;
        }
        const int visibleHeight = std::max(1,
            (spriteBounds.bottom + 1) * artHeight / frame.height);
        const int artTop = actorBottom - visibleHeight;
        const int artLeft = screenCenterX - artWidth / 2;
        const int opaqueLeft = artLeft + spriteBounds.left * artWidth / frame.width;
        const int opaqueRight = artLeft
            + (spriteBounds.right + 1) * artWidth / frame.width;
        const int opaqueTop = artTop + spriteBounds.top * artHeight / frame.height;
        const int opaqueBottom = artTop
            + (spriteBounds.bottom + 1) * artHeight / frame.height;
        renderContext->hitTargets->push_back({
            object,
            transformY,
            artLeft,
            artTop,
            artWidth,
            artHeight,
            object->frame,
            facingRotation,
        });

        const int firstOpaqueColumn = std::max(0, opaqueLeft);
        const int lastColumn = std::min(gTileWindowWidth, opaqueRight);
        if ((objectType == OBJ_TYPE_CRITTER
                || objectType == OBJ_TYPE_SCENERY
                || objectType == OBJ_TYPE_ITEM)
            && screenIsHardwareAccelerated()) {
            bool hasVisibleColumn = false;
            const int clippedOpaqueTop = std::max(0, opaqueTop);
            const int clippedOpaqueBottom = std::min(gTileWindowHeight, opaqueBottom);
            // Within each horizon band, wall depth is constant vertically and
            // a door cap is one contiguous interval. If neither endpoint is
            // visible, the entire band is hidden; no per-pixel scan is needed.
            const auto bandVisible = [&](int x, int top, int bottom) {
                return top < bottom
                    && (transformY < tileFirstPersonGetWallDepthAt(x, top,
                            renderContext->horizonY, renderContext->wallDepths,
                            renderContext->wallSillDepths, renderContext->doorCapDepths)
                        || transformY < tileFirstPersonGetWallDepthAt(x, bottom - 1,
                            renderContext->horizonY, renderContext->wallDepths,
                            renderContext->wallSillDepths, renderContext->doorCapDepths));
            };
            for (int x = firstOpaqueColumn; x < lastColumn; x++) {
                hasVisibleColumn = bandVisible(x, clippedOpaqueTop,
                                       std::min(clippedOpaqueBottom, renderContext->horizonY))
                    || bandVisible(x, std::max(clippedOpaqueTop, renderContext->horizonY),
                        clippedOpaqueBottom);
                if (hasVisibleColumn) {
                    break;
                }
            }
            if (!hasVisibleColumn) {
                if (objectType != OBJ_TYPE_SCENERY) {
                    textObjectsRenderForOwner(object,
                        gTileWindowBuffer,
                        gTileWindowPitch,
                        gTileWindowWidth,
                        gTileWindowHeight,
                        screenCenterX,
                        artTop - 3);
                }
                return;
            }
            const SDL_Rect destination = { artLeft, artTop, artWidth, artHeight };
            const auto submitVisibleRange = [&](int top, int bottom) {
                if (top >= bottom) {
                    return;
                }
                bool overlapsDoorCap = false;
                static BillboardClipBands clipBands;
                clipBands.reset(top, bottom);
                if (renderContext->doorCapDepths != nullptr) {
                    for (int x = firstOpaqueColumn; x < lastColumn; x++) {
                        const TileFirstPersonDoorCapSpan& cap = (*renderContext->doorCapDepths)[x];
                        if (cap.top < cap.bottom && cap.top < bottom && cap.bottom > top
                            && transformY >= cap.depth) {
                            overlapsDoorCap = true;
                            clipBands.addOccluder(cap.top, cap.bottom);
                        }
                    }
                }
                if (!overlapsDoorCap) {
                    // Each call stays on one side of the horizon. Visibility
                    // therefore depends only on x; merge adjacent columns into
                    // full-height clips without allocating vectors per row.
                    int runStart = firstOpaqueColumn;
                    bool inRun = false;
                    for (int x = firstOpaqueColumn; x <= lastColumn; x++) {
                        const bool visible = x < lastColumn
                            && transformY < (top < renderContext->horizonY
                                    ? (*renderContext->wallDepths)[x]
                                    : std::min((*renderContext->wallDepths)[x],
                                          (*renderContext->wallSillDepths)[x]));
                        if (visible && !inRun) {
                            runStart = x;
                            inRun = true;
                        } else if (!visible && inRun) {
                            const SDL_Rect clip = { runStart, top, x - runStart, bottom - top };
                            gpuBillboardSubmit(frame.data, frame.width, frame.height,
                                destination, clip, transformY);
                            inRun = false;
                        }
                    }
                    return;
                }
                // Depth is unchanged between cap edges. Scan each distinct
                // band once instead of repeating the same work on every row.
                const std::vector<int>& boundaries = clipBands.boundaries();
                static std::vector<SDL_Rect> activeClips;
                static std::vector<SDL_Rect> nextClips;
                static std::vector<bool> retained;
                activeClips.clear();
                for (size_t band = 0; band + 1 < boundaries.size(); band++) {
                    const int y = boundaries[band];
                    const int bandHeight = boundaries[band + 1] - y;
                    nextClips.clear();
                    retained.assign(activeClips.size(), false);
                    int runStart = firstOpaqueColumn;
                    bool inRun = false;
                    const auto finishRun = [&](int runEnd) {
                        SDL_Rect clip = { runStart, y, runEnd - runStart, bandHeight };
                        for (size_t index = 0; index < activeClips.size(); index++) {
                            SDL_Rect& active = activeClips[index];
                            if (!retained[index]
                                && active.x == clip.x && active.w == clip.w
                                && active.y + active.h == y) {
                                active.h += bandHeight;
                                nextClips.push_back(active);
                                retained[index] = true;
                                return;
                            }
                        }
                        nextClips.push_back(clip);
                    };
                    for (int x = firstOpaqueColumn; x <= lastColumn; x++) {
                        const bool nextVisible = x < lastColumn
                            && transformY < tileFirstPersonGetWallDepthAt(
                                x,
                                y,
                                renderContext->horizonY,
                                renderContext->wallDepths,
                                renderContext->wallSillDepths,
                                renderContext->doorCapDepths);
                        if (nextVisible && !inRun) {
                            runStart = x;
                            inRun = true;
                        } else if (!nextVisible && inRun) {
                            finishRun(x);
                            inRun = false;
                        }
                    }
                    for (size_t index = 0; index < activeClips.size(); index++) {
                        if (!retained[index]) {
                            gpuBillboardSubmit(frame.data, frame.width, frame.height,
                                destination, activeClips[index], transformY);
                        }
                    }
                    activeClips.swap(nextClips);
                }
                for (const SDL_Rect& clip : activeClips) {
                    gpuBillboardSubmit(frame.data, frame.width, frame.height,
                        destination, clip, transformY);
                }
            };
            submitVisibleRange(std::max(0, opaqueTop),
                std::min(gTileWindowHeight, std::min(opaqueBottom, renderContext->horizonY)));
            submitVisibleRange(std::max(0, std::max(opaqueTop, renderContext->horizonY)),
                std::min(gTileWindowHeight, opaqueBottom));
            if (objectType != OBJ_TYPE_SCENERY) {
                textObjectsRenderForOwner(object,
                    gTileWindowBuffer,
                    gTileWindowPitch,
                    gTileWindowWidth,
                    gTileWindowHeight,
                    screenCenterX,
                    artTop - 3);
            }
            return;
        }

        // Large scenery billboards are intentionally rendered as coarse
        // blocks. Their source art is already heavily magnified at close
        // range, and full-resolution depth testing dominates frame time.
        const int spriteSampleStep = objectType != OBJ_TYPE_SCENERY
            ? 1
            : artHeight >= 192 ? 4
            : artHeight >= 96 ? 3
            : 2;
        int sourceXNumerator = (firstOpaqueColumn - artLeft) * frame.width;
        int sourceX = sourceXNumerator / artWidth;
        int sourceXRemainder = sourceXNumerator % artWidth;
        for (int x = firstOpaqueColumn; x < lastColumn; x += spriteSampleStep) {
            if (sourceX < spriteBounds.left || sourceX > spriteBounds.right) {
                for (int step = 0; step < spriteSampleStep && x + step < lastColumn; step++) {
                    sourceXRemainder += frame.width;
                    while (sourceXRemainder >= artWidth) {
                        sourceXRemainder -= artWidth;
                        sourceX++;
                    }
                }
                continue;
            }
            const auto renderColumnRange = [&](int startY, int endY) {
                if (startY >= endY) {
                    return;
                }
                int sourceNumerator = (startY - artTop) * frame.height;
                int sourceY = sourceNumerator / artHeight;
                int sourceRemainder = sourceNumerator % artHeight;
                for (int y = startY; y < endY; y += spriteSampleStep) {
                    const unsigned char color = frame.data[sourceY * frame.width + sourceX];
                    if (color != 0) {
                        for (int blockY = y;
                             blockY < std::min(endY, y + spriteSampleStep);
                             blockY++) {
                            for (int blockX = x;
                                 blockX < std::min(lastColumn, x + spriteSampleStep);
                                 blockX++) {
                                if (transformY >= tileFirstPersonGetWallDepthAt(
                                        blockX,
                                        blockY,
                                        renderContext->horizonY,
                                        renderContext->wallDepths,
                                        renderContext->wallSillDepths,
                                        renderContext->doorCapDepths)) {
                                    continue;
                                }
                                const size_t depthIndex =
                                    static_cast<size_t>(blockY) * gTileWindowWidth + blockX;
                                if ((*renderContext->actorDepthStamps)[depthIndex]
                                    != renderContext->actorDepthStamp) {
                                    (*renderContext->actorDepthStamps)[depthIndex] =
                                        renderContext->actorDepthStamp;
                                    (*renderContext->actorDepths)[depthIndex] =
                                        std::numeric_limits<double>::infinity();
                                }
                                if (transformY < (*renderContext->actorDepths)[depthIndex]) {
                                    gTileWindowBuffer[blockY * gTileWindowPitch + blockX] = color;
                                    (*renderContext->actorDepths)[depthIndex] = transformY;
                                }
                            }
                        }
                    }
                    for (int step = 0; step < spriteSampleStep && y + step < endY; step++) {
                        sourceRemainder += frame.height;
                        while (sourceRemainder >= artHeight) {
                            sourceRemainder -= artHeight;
                            sourceY++;
                        }
                    }
                }
            };
            const int clippedTop = std::max(0, opaqueTop);
            const int clippedBottom = std::min(gTileWindowHeight, opaqueBottom);
            const int horizon = renderContext->horizonY;
            if (transformY < (*renderContext->wallDepths)[x]) {
                renderColumnRange(clippedTop, std::min(clippedBottom, horizon));
            }
            if (transformY < std::min((*renderContext->wallDepths)[x],
                    (*renderContext->wallSillDepths)[x])) {
                renderColumnRange(std::max(clippedTop, horizon), clippedBottom);
            }
            for (int step = 0; step < spriteSampleStep && x + step < lastColumn; step++) {
                sourceXRemainder += frame.width;
                while (sourceXRemainder >= artWidth) {
                    sourceXRemainder -= artWidth;
                    sourceX++;
                }
            }
        }

        if (objectType != OBJ_TYPE_SCENERY) {
            textObjectsRenderForOwner(object,
                gTileWindowBuffer,
                gTileWindowPitch,
                gTileWindowWidth,
                gTileWindowHeight,
                screenCenterX,
                artTop - 3);
        }

        return;
    }

    renderContext->hitTargets->push_back({
        object,
        transformY,
        spriteLeft,
        spriteTop,
        spriteWidth,
        actorHeight,
        object->frame,
        facingRotation,
    });
    for (int x = std::max(0, spriteLeft); x < std::min(gTileWindowWidth, spriteRight); x++) {
        const double normalizedX = (x - spriteLeft) / static_cast<double>(spriteWidth);
        const auto renderSilhouetteRange = [&](int startY, int endY) {
            for (int y = startY; y < endY; y++) {
                if (transformY >= tileFirstPersonGetWallDepthAt(
                        x,
                        y,
                        renderContext->horizonY,
                        renderContext->wallDepths,
                        renderContext->wallSillDepths,
                        renderContext->doorCapDepths)) {
                    continue;
                }
                const double normalizedY = (y - spriteTop) / static_cast<double>(actorHeight);
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

                const size_t depthIndex = static_cast<size_t>(y) * gTileWindowWidth + x;
                if ((*renderContext->actorDepthStamps)[depthIndex]
                    != renderContext->actorDepthStamp) {
                    (*renderContext->actorDepthStamps)[depthIndex] =
                        renderContext->actorDepthStamp;
                    (*renderContext->actorDepths)[depthIndex] =
                        std::numeric_limits<double>::infinity();
                }
                if (transformY < (*renderContext->actorDepths)[depthIndex]) {
                    gTileWindowBuffer[y * gTileWindowPitch + x] = actorColor;
                    (*renderContext->actorDepths)[depthIndex] = transformY;
                }
            }
        };
        const int clippedTop = std::max(0, spriteTop);
        const int clippedBottom = std::min(gTileWindowHeight, spriteBottom);
        const int horizon = renderContext->horizonY;
        if (transformY < (*renderContext->wallDepths)[x]) {
            renderSilhouetteRange(clippedTop, std::min(clippedBottom, horizon));
        }
        if (transformY < std::min((*renderContext->wallDepths)[x],
                (*renderContext->wallSillDepths)[x])) {
            renderSilhouetteRange(std::max(clippedTop, horizon), clippedBottom);
        }
    }
}

static bool tileFirstPersonGetWorldPosition(int tile, double* x, double* y)
{
    if (!tileIsValid(tile)) {
        return false;
    }

    *x = (gHexGridWidth - 1 - tile % gHexGridWidth) / 2.0 + 0.25;
    *y = (tile / gHexGridWidth) / 2.0 + 0.25;
    return true;
}

static Rotation tileFirstPersonGetRotationToward(int tile, double targetX, double targetY)
{
    static std::array<std::array<std::array<double, 2>, ROTATION_COUNT>, 2> facingDirections {};
    static int facingDirectionsGridWidth = 0;
    if (facingDirectionsGridWidth != gHexGridWidth) {
        for (int parity = 0; parity < 2; parity++) {
            const int referenceTile = gHexGridWidth * 2 + 2 + parity;
            double referenceX;
            double referenceY;
            if (!tileFirstPersonGetWorldPosition(referenceTile, &referenceX, &referenceY)) {
                continue;
            }
            for (int rotationIndex = ROTATION_FIRST; rotationIndex < ROTATION_COUNT; rotationIndex++) {
                const Rotation rotation = static_cast<Rotation>(rotationIndex);
                const int adjacentTile = tileGetTileInDirection(referenceTile, rotation, 1);
                double adjacentX;
                double adjacentY;
                if (!tileFirstPersonGetWorldPosition(adjacentTile, &adjacentX, &adjacentY)) {
                    continue;
                }

                const double directionX = adjacentX - referenceX;
                const double directionY = adjacentY - referenceY;
                const double directionLength = sqrt(directionX * directionX + directionY * directionY);
                if (directionLength > 0.0) {
                    facingDirections[parity][rotationIndex] = {
                        directionX / directionLength,
                        directionY / directionLength,
                    };
                }
            }
        }
        facingDirectionsGridWidth = gHexGridWidth;
    }

    double objectX;
    double objectY;
    if (!tileFirstPersonGetWorldPosition(tile, &objectX, &objectY)) {
        return ROTATION_NE;
    }

    const double viewX = targetX - objectX;
    const double viewY = targetY - objectY;
    double bestDot = -std::numeric_limits<double>::infinity();
    Rotation facingRotation = ROTATION_NE;
    const int parity = (tile % gHexGridWidth) & 1;
    for (int rotationIndex = ROTATION_FIRST; rotationIndex < ROTATION_COUNT; rotationIndex++) {
        const double dot = facingDirections[parity][rotationIndex][0] * viewX
            + facingDirections[parity][rotationIndex][1] * viewY;
        if (dot > bestDot) {
            bestDot = dot;
            facingRotation = static_cast<Rotation>(rotationIndex);
        }
    }

    return facingRotation;
}

static double tileFirstPersonPreviewGetYaw()
{
    if (!gFirstPersonPreviewYawTurning) {
        return gFirstPersonPreviewYaw;
    }

    constexpr unsigned int kTurnDuration = 140;
    const unsigned int now = getTicks();
    const double progress = std::min(1.0,
        static_cast<double>(getTicksBetween(now, gFirstPersonPreviewYawStartTime)) / kTurnDuration);
    const double easedProgress = progress * progress * (3.0 - 2.0 * progress);
    const double yaw = gFirstPersonPreviewYawStart
        + (gFirstPersonPreviewYawTarget - gFirstPersonPreviewYawStart) * easedProgress;
    if (progress >= 1.0) {
        gFirstPersonPreviewYawTurning = false;
        gFirstPersonPreviewYaw = static_cast<int>(gFirstPersonPreviewYawTarget + 360.0) % 360;
        return gFirstPersonPreviewYaw;
    }
    return yaw;
}

static void tileFirstPersonPreviewResetForModeRefresh()
{
    gFirstPersonPreviewCameraInitialized = false;
    gFirstPersonPreviewMoveMarkerTile = -1;
    gFirstPersonPreviewPendingMoveDirection = -1;
    gFirstPersonPreviewYawTurning = false;
    gFirstPersonPreviewYaw = 0;

    if (gDude == nullptr || !tileIsValid(gDude->tile) || !rotationIsValid(gDude->rotation)) {
        return;
    }

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

static void tileFirstPersonGetCameraPosition(int tile, MapElevation elevation, double* x, double* y)
{
    double targetX;
    double targetY;
    if (!tileFirstPersonGetWorldPosition(tile, &targetX, &targetY)) {
        *x = SQUARE_GRID_WIDTH / 2.0;
        *y = SQUARE_GRID_HEIGHT / 2.0;
        gFirstPersonPreviewCameraInitialized = false;
        return;
    }

    const unsigned int now = getTicks();
    const bool inCombat = isInCombat();
    const bool combatStateChanged = gFirstPersonPreviewCameraInCombat != inCombat;
    const unsigned int cameraLerpDuration = inCombat ? 80 : 180;
    if (!gFirstPersonPreviewCameraInitialized
        || gFirstPersonPreviewCameraElevation != elevation
        || combatStateChanged) {
        if (combatStateChanged) {
            gFirstPersonPreviewPendingMoveDirection = -1;
        }

        gFirstPersonPreviewCameraInitialized = true;
        gFirstPersonPreviewCameraTile = tile;
        gFirstPersonPreviewCameraElevation = elevation;
        gFirstPersonPreviewCameraInCombat = inCombat;
        gFirstPersonPreviewCameraStartTime = now;
        gFirstPersonPreviewCameraStartX = targetX;
        gFirstPersonPreviewCameraStartY = targetY;
        gFirstPersonPreviewCameraTargetX = targetX;
        gFirstPersonPreviewCameraTargetY = targetY;
    } else if (gFirstPersonPreviewCameraTile != tile) {
        const double progress = std::min(1.0,
            static_cast<double>(getTicksBetween(now, gFirstPersonPreviewCameraStartTime))
                / cameraLerpDuration);
        gFirstPersonPreviewCameraStartX = gFirstPersonPreviewCameraStartX
            + (gFirstPersonPreviewCameraTargetX - gFirstPersonPreviewCameraStartX) * progress;
        gFirstPersonPreviewCameraStartY = gFirstPersonPreviewCameraStartY
            + (gFirstPersonPreviewCameraTargetY - gFirstPersonPreviewCameraStartY) * progress;
        gFirstPersonPreviewCameraTargetX = targetX;
        gFirstPersonPreviewCameraTargetY = targetY;
        gFirstPersonPreviewCameraStartTime = now;
        gFirstPersonPreviewCameraTile = tile;
    }

    const double progress = std::min(1.0,
        static_cast<double>(getTicksBetween(now, gFirstPersonPreviewCameraStartTime))
            / cameraLerpDuration);
    *x = gFirstPersonPreviewCameraStartX
        + (gFirstPersonPreviewCameraTargetX - gFirstPersonPreviewCameraStartX) * progress;
    *y = gFirstPersonPreviewCameraStartY
        + (gFirstPersonPreviewCameraTargetY - gFirstPersonPreviewCameraStartY) * progress;
}

static void tileFirstPersonDrawCrosshair(double directionX, double directionY)
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

    const int arrowLength = 14;
    const int tipX = centerX + static_cast<int>(directionX * arrowLength);
    const int tipY = centerY + static_cast<int>(directionY * arrowLength);
    const int wingLength = 5;
    const int wingX = centerX + static_cast<int>(directionX * (arrowLength - wingLength));
    const int wingY = centerY + static_cast<int>(directionY * (arrowLength - wingLength));
    const int leftX = wingX + static_cast<int>(-directionY * wingLength);
    const int leftY = wingY + static_cast<int>(directionX * wingLength);
    const int rightX = wingX - static_cast<int>(-directionY * wingLength);
    const int rightY = wingY - static_cast<int>(directionX * wingLength);
    bufferDrawLine(gTileWindowBuffer, gTileWindowPitch, centerX, centerY, tipX, tipY, COLOR_BLACK);
    bufferDrawLine(gTileWindowBuffer, gTileWindowPitch, tipX, tipY, leftX, leftY, COLOR_BLACK);
    bufferDrawLine(gTileWindowBuffer, gTileWindowPitch, tipX, tipY, rightX, rightY, COLOR_BLACK);
    bufferDrawLine(gTileWindowBuffer, gTileWindowPitch, centerX, centerY, tipX, tipY, COLOR_LIGHT_GREEN_3);
    bufferDrawLine(gTileWindowBuffer, gTileWindowPitch, tipX, tipY, leftX, leftY, COLOR_LIGHT_GREEN_3);
    bufferDrawLine(gTileWindowBuffer, gTileWindowPitch, tipX, tipY, rightX, rightY, COLOR_LIGHT_GREEN_3);
}

static void tileFirstPersonDrawMoveMarker(double directionX, double directionY)
{
    if (!tileIsValid(gFirstPersonPreviewMoveMarkerTile)
        || gDude == nullptr || gDude->elevation != gElevation) {
        return;
    }

    constexpr double kFieldOfViewScale = 0.66;
    double cameraX;
    double cameraY;
    double targetX;
    double targetY;
    if (!tileFirstPersonGetWorldPosition(gDude->tile, &cameraX, &cameraY)
        || !tileFirstPersonGetWorldPosition(gFirstPersonPreviewMoveMarkerTile, &targetX, &targetY)) {
        return;
    }

    const double cameraPlaneX = -directionY * kFieldOfViewScale;
    const double cameraPlaneY = directionX * kFieldOfViewScale;
    const double relativeX = targetX - cameraX;
    const double relativeY = targetY - cameraY;
    const double determinant = cameraPlaneX * directionY - directionX * cameraPlaneY;
    if (determinant == 0.0) {
        return;
    }

    const double inverseDeterminant = 1.0 / determinant;
    const double transformX = inverseDeterminant
        * (directionY * relativeX - directionX * relativeY);
    const double transformY = inverseDeterminant
        * (-cameraPlaneY * relativeX + cameraPlaneX * relativeY);
    if (transformY <= 0.1) {
        return;
    }

    const int centerX = static_cast<int>((gTileWindowWidth / 2.0) * (1.0 + transformX / transformY));
    const int centerY = gTileWindowHeight / 2
        + static_cast<int>((gTileWindowHeight * 0.5) / transformY);
    if (centerX < 0 || centerX >= gTileWindowWidth || centerY < 0 || centerY >= gTileWindowHeight
        || (gFirstPersonPreviewWallDepths.size() == static_cast<size_t>(gTileWindowWidth)
            && transformY >= gFirstPersonPreviewWallDepths[centerX])) {
        return;
    }

    const int radius = std::max(3, std::min(18, static_cast<int>(gTileWindowHeight * 0.08 / transformY)));
    const int thickness = 3;

    for (int t = 0; t < thickness; t++) {
        int r = radius - t;

        tileFirstPersonDrawDiagnosticLine(centerX, centerY - r, centerX + r, centerY, COLOR_RED);
        tileFirstPersonDrawDiagnosticLine(centerX + r, centerY, centerX, centerY + r, COLOR_RED);
        tileFirstPersonDrawDiagnosticLine(centerX, centerY + r, centerX - r, centerY, COLOR_RED);
        tileFirstPersonDrawDiagnosticLine(centerX - r, centerY, centerX, centerY - r, COLOR_RED);
    }
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
    gFirstPersonPreviewActorHitTargets.clear();
    bufferFill(gTileWindowBuffer,
        gTileWindowWidth,
        gTileWindowHeight,
        gTileWindowPitch,
        COLOR_BLACK);

    if (gTileWindowWidth < SQUARE_GRID_WIDTH || gTileWindowHeight < SQUARE_GRID_HEIGHT
        || gTileWindowPitch < gTileWindowWidth
        || !elevationIsValid(gElevation) || gTileSquares[gElevation] == nullptr) {
        return;
    }

    const int cellSize = std::min(
        (gTileWindowWidth - 16) / SQUARE_GRID_WIDTH,
        (gTileWindowHeight - 16) / SQUARE_GRID_HEIGHT);
    if (cellSize <= 0) {
        return;
    }

    constexpr int kMaximumTopologyZoom = 3;
    const int zoomScale = 1 << std::max(1,
        std::min(kMaximumTopologyZoom, gFirstPersonPreviewTopologyZoom));
    const int zoomedCellSize = cellSize * zoomScale;
    const int mapWidth = zoomedCellSize * SQUARE_GRID_WIDTH;
    const int mapHeight = zoomedCellSize * SQUARE_GRID_HEIGHT;
    int mapLeft = (gTileWindowWidth - mapWidth) / 2;
    int mapTop = (gTileWindowHeight - mapHeight) / 2;
    if (gFirstPersonPreviewTopologyZoom > 0) {
        int focusSquareTile = -1;
        if (gDude != nullptr && gDude->elevation == gElevation && tileIsValid(gDude->tile)) {
            focusSquareTile = squareTileFromTile(gDude->tile);
        }
        if (!squareGridTileIsValid(focusSquareTile)) {
            focusSquareTile = SQUARE_GRID_SIZE / 2;
        }

        const int renderFocusSquareTile = focusSquareTile / SQUARE_GRID_WIDTH * SQUARE_GRID_WIDTH
            + (SQUARE_GRID_WIDTH - 1 - focusSquareTile % SQUARE_GRID_WIDTH);
        if (!squareGridTileIsValid(renderFocusSquareTile)) {
            return;
        }
        const int focusX = renderFocusSquareTile % SQUARE_GRID_WIDTH;
        const int focusY = renderFocusSquareTile / SQUARE_GRID_WIDTH;
        mapLeft = gTileWindowWidth / 2 - (focusX * zoomedCellSize + zoomedCellSize / 2);
        mapTop = gTileWindowHeight / 2 - (focusY * zoomedCellSize + zoomedCellSize / 2);
    }

    for (int renderSquareTile = 0; renderSquareTile < SQUARE_GRID_SIZE; renderSquareTile++) {
        const int mapSquareTile = renderSquareTile / SQUARE_GRID_WIDTH * SQUARE_GRID_WIDTH
            + (SQUARE_GRID_WIDTH - 1 - renderSquareTile % SQUARE_GRID_WIDTH);
        const FloorTileFrmId floorFid(gTileSquares[gElevation]->tileFid[mapSquareTile]);
        if (floorFid.frameId<TileFrameId>() == TileFrameId::Invalid) {
            continue;
        }

        const int x = mapLeft + (renderSquareTile % SQUARE_GRID_WIDTH) * zoomedCellSize;
        const int y = mapTop + (renderSquareTile / SQUARE_GRID_WIDTH) * zoomedCellSize;
        if (x > gTileWindowWidth - 1 || y > gTileWindowHeight - 1
            || x + zoomedCellSize - 2 < 0
            || y + zoomedCellSize - 2 < 0) {
            continue;
        }
        const int left = std::max(0, x);
        const int top = std::max(0, y);
        const int right = std::min(gTileWindowWidth - 1, x + zoomedCellSize - 2);
        const int bottom = std::min(gTileWindowHeight - 1, y + zoomedCellSize - 2);
        if (left <= right && top <= bottom) {
            bufferFill(gTileWindowBuffer + top * gTileWindowPitch + left,
                right - left + 1,
                bottom - top + 1,
                gTileWindowPitch,
                ((renderSquareTile / SQUARE_GRID_WIDTH + renderSquareTile % SQUARE_GRID_WIDTH) & 1) != 0
                    ? COLOR_DARK_GREY_2
                    : COLOR_DARK_GREY);
            if (zoomedCellSize >= 3) {
                bufferDrawRect(gTileWindowBuffer, gTileWindowPitch,
                    left, top, right, bottom, COLOR_DARK_GREY_3);
            }
        }
    }

    if (gFirstPersonPreviewTopologyZoom >= 2
        && gFirstPersonPreviewWallCells.size() == static_cast<size_t>(gHexGridSize)
        && gFirstPersonPreviewWallOrientations.size() == static_cast<size_t>(gHexGridSize)) {
        for (int renderHexTile = 0; renderHexTile < gHexGridSize; renderHexTile++) {
            if (gFirstPersonPreviewWallCells[renderHexTile] == 0) {
                continue;
            }

            const int hexRow = renderHexTile / gHexGridWidth;
            const int renderHexColumn = renderHexTile % gHexGridWidth;
            const int tile = hexRow * gHexGridWidth + (gHexGridWidth - 1 - renderHexColumn);
            const int squareTile = squareTileFromTile(tile);
            if (!squareGridTileIsValid(squareTile)) {
                continue;
            }

            const int renderSquareTile = squareTile / SQUARE_GRID_WIDTH * SQUARE_GRID_WIDTH
                + (SQUARE_GRID_WIDTH - 1 - squareTile % SQUARE_GRID_WIDTH);
            const int cellX = mapLeft + (renderSquareTile % SQUARE_GRID_WIDTH) * zoomedCellSize;
            const int cellY = mapTop + (renderSquareTile / SQUARE_GRID_WIDTH) * zoomedCellSize;
            const int centerX = cellX + zoomedCellSize / 2;
            const int centerY = cellY + zoomedCellSize / 2;
            if (cellX > gTileWindowWidth - 1 || cellY > gTileWindowHeight - 1
                || cellX + zoomedCellSize - 1 < 0
                || cellY + zoomedCellSize - 1 < 0) {
                continue;
            }
            if (centerX < 0 || centerX >= gTileWindowWidth
                || centerY < 0 || centerY >= gTileWindowHeight) {
                continue;
            }

            const auto orientation = static_cast<TileFirstPersonWallOrientation>(
                gFirstPersonPreviewWallOrientations[renderHexTile]);
            if (orientation == TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH) {
                tileFirstPersonDrawDiagnosticLine(centerX, cellY, centerX,
                    cellY + zoomedCellSize - 1, COLOR_LIGHT_GREY);
            } else if (orientation == TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST) {
                tileFirstPersonDrawDiagnosticLine(cellX, centerY,
                    cellX + zoomedCellSize - 1, centerY, COLOR_LIGHT_GREY);
            } else {
                bufferDrawRect(gTileWindowBuffer, gTileWindowPitch,
                    cellX + 1, cellY + 1,
                    cellX + zoomedCellSize - 2, cellY + zoomedCellSize - 2,
                        COLOR_LIGHT_GREY);
            }
        }
    }

    for (const TileFirstPersonTopologyMarker& marker : gFirstPersonPreviewTopologyMarkers) {
        const int squareTile = squareTileFromTile(marker.tile);
        if (!squareGridTileIsValid(squareTile)) {
            continue;
        }

        const int renderSquareTile = squareTile / SQUARE_GRID_WIDTH * SQUARE_GRID_WIDTH
            + (SQUARE_GRID_WIDTH - 1 - squareTile % SQUARE_GRID_WIDTH);
        const int cellX = mapLeft + (renderSquareTile % SQUARE_GRID_WIDTH) * zoomedCellSize;
        const int cellY = mapTop + (renderSquareTile / SQUARE_GRID_WIDTH) * zoomedCellSize;
        const int hexColumn = gHexGridWidth - 1 - marker.tile % gHexGridWidth;
        const int hexRow = marker.tile / gHexGridWidth;
        const int markerMaxLocal = std::max(0, zoomedCellSize - 2);
        const int markerX = cellX + std::min(markerMaxLocal,
            (hexColumn & 1) == 0 ? 3 * zoomedCellSize / 4 : zoomedCellSize / 4);
        const int markerY = cellY + std::min(markerMaxLocal,
            (hexRow & 1) == 0 ? zoomedCellSize / 4 : 3 * zoomedCellSize / 4);
        if (markerX < 0 || markerX >= gTileWindowWidth
            || markerY < 0 || markerY >= gTileWindowHeight) {
            continue;
        }

        bufferFill(gTileWindowBuffer + markerY * gTileWindowPitch + markerX,
            1,
            1,
            gTileWindowPitch,
            marker.color);

        if (gFirstPersonPreviewTopologyZoom < 2 && marker.structural) {
            const int centerX = cellX + zoomedCellSize / 2;
            const int centerY = cellY + zoomedCellSize / 2;
            if (marker.wallOrientation == TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH) {
                tileFirstPersonDrawDiagnosticLine(centerX, cellY, centerX,
                    cellY + zoomedCellSize - 1, COLOR_LIGHT_GREY);
            } else if (marker.wallOrientation == TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST) {
                tileFirstPersonDrawDiagnosticLine(cellX, centerY,
                    cellX + zoomedCellSize - 1, centerY, COLOR_LIGHT_GREY);
            } else if (cellX + 1 <= cellX + zoomedCellSize - 2
                && cellY + 1 <= cellY + zoomedCellSize - 2) {
                bufferDrawRect(gTileWindowBuffer, gTileWindowPitch,
                    cellX + 1, cellY + 1,
                    cellX + zoomedCellSize - 2, cellY + zoomedCellSize - 2,
                COLOR_LIGHT_GREY);
            }
        }

        if ((marker.objectType == OBJ_TYPE_WALL || marker.objectType == OBJ_TYPE_SCENERY)
            && rotationIsValid(marker.rotation)) {
            static constexpr int kRotationX[ROTATION_COUNT] = { 1, 1, 1, -1, -1, -1 };
            static constexpr int kRotationY[ROTATION_COUNT] = { -1, 0, 1, 1, 0, -1 };
            const int arrowLength = std::max(1, (zoomedCellSize - 1) / 2);
            const int arrowX = markerX + kRotationX[marker.rotation] * arrowLength;
            const int arrowY = markerY + kRotationY[marker.rotation] * arrowLength;
            tileFirstPersonDrawDiagnosticLine(markerX, markerY, arrowX, arrowY, COLOR_LIGHT_GREY);
        }

        if (marker.door && zoomedCellSize >= 3) {
            const int iconWidth = std::min(5, std::max(3, zoomedCellSize / 2));
            const int iconHeight = std::min(7, std::max(3, zoomedCellSize));
            const int iconLeft = markerX - iconWidth / 2;
            const int iconTop = markerY - iconHeight / 2;
            const int iconRight = iconLeft + iconWidth - 1;
            const int iconBottom = iconTop + iconHeight - 1;
            if (iconLeft >= 0 && iconTop >= 0
                && iconRight < gTileWindowWidth && iconBottom < gTileWindowHeight) {
                bufferDrawRect(gTileWindowBuffer, gTileWindowPitch,
                    iconLeft, iconTop, iconRight, iconBottom, COLOR_BLACK);
                bufferFill(gTileWindowBuffer + (iconTop + 1) * gTileWindowPitch + iconLeft + 1,
                    iconWidth - 2,
                    iconHeight - 2,
                    gTileWindowPitch,
                    marker.openDoor ? COLOR_LIGHT_GREEN_3 : COLOR_LIGHT_RED);
            }
        }
    }

    if (gDude != nullptr && gDude->elevation == gElevation && tileIsValid(gDude->tile)) {
        const int squareTile = squareTileFromTile(gDude->tile);
        if (squareGridTileIsValid(squareTile)) {
            const int renderSquareTile = squareTile / SQUARE_GRID_WIDTH * SQUARE_GRID_WIDTH
                + (SQUARE_GRID_WIDTH - 1 - squareTile % SQUARE_GRID_WIDTH);
            const int cellX = mapLeft + (renderSquareTile % SQUARE_GRID_WIDTH) * zoomedCellSize;
            const int cellY = mapTop + (renderSquareTile / SQUARE_GRID_WIDTH) * zoomedCellSize;
            const int hexColumn = gHexGridWidth - 1 - gDude->tile % gHexGridWidth;
            const int hexRow = gDude->tile / gHexGridWidth;
            const int localX = (hexColumn & 1) == 0 ? 3 * zoomedCellSize / 4 : zoomedCellSize / 4;
            const int localY = (hexRow & 1) == 0 ? zoomedCellSize / 4 : 3 * zoomedCellSize / 4;
            const int playerX = cellX + std::min(zoomedCellSize - 1, localX);
            const int playerY = cellY + std::min(zoomedCellSize - 1, localY);
            if (playerX >= 0 && playerX < gTileWindowWidth && playerY >= 0 && playerY < gTileWindowHeight) {
                const int radius = std::max(3, std::min(8, zoomedCellSize / 2));
                const int outlineRadius = radius + 2;
                tileFirstPersonDrawDiagnosticLine(playerX, playerY - outlineRadius,
                    playerX + outlineRadius, playerY, COLOR_BLACK);
                tileFirstPersonDrawDiagnosticLine(playerX + outlineRadius, playerY,
                    playerX, playerY + outlineRadius, COLOR_BLACK);
                tileFirstPersonDrawDiagnosticLine(playerX, playerY + outlineRadius,
                    playerX - outlineRadius, playerY, COLOR_BLACK);
                tileFirstPersonDrawDiagnosticLine(playerX - outlineRadius, playerY,
                    playerX, playerY - outlineRadius, COLOR_BLACK);
                tileFirstPersonDrawDiagnosticLine(playerX, playerY - radius,
                    playerX + radius, playerY, COLOR_RED);
                tileFirstPersonDrawDiagnosticLine(playerX + radius, playerY,
                    playerX, playerY + radius, COLOR_RED);
                tileFirstPersonDrawDiagnosticLine(playerX, playerY + radius,
                    playerX - radius, playerY, COLOR_RED);
                tileFirstPersonDrawDiagnosticLine(playerX - radius, playerY,
                    playerX, playerY - radius, COLOR_RED);
                if (radius > 2) {
                    tileFirstPersonDrawDiagnosticLine(playerX, playerY - radius + 1,
                        playerX + radius - 1, playerY, COLOR_RED);
                    tileFirstPersonDrawDiagnosticLine(playerX + radius - 1, playerY,
                        playerX, playerY + radius - 1, COLOR_RED);
                    tileFirstPersonDrawDiagnosticLine(playerX, playerY + radius - 1,
                        playerX - radius + 1, playerY, COLOR_RED);
                    tileFirstPersonDrawDiagnosticLine(playerX - radius + 1, playerY,
                        playerX, playerY - radius + 1, COLOR_RED);
                }
            }
        }
    }
}

static void tileRenderFirstPersonTopologyOverlay()
{
    if (!gFirstPersonPreviewTopologyEnabled || gFirstPersonPreviewTopologyPosition < 0
        || gTileWindowWidth < 2 || gTileWindowHeight < 2
        || gFirstPersonPreviewTopologyRendering) {
        return;
    }
    gFirstPersonPreviewTopologyRendering = true;

    const int overlayWidth = std::min(320, std::max(200, gTileWindowWidth / 3));
    const int overlayHeight = std::min(240, std::max(160, gTileWindowHeight / 3));
    const int margin = 8;
    const int left = gFirstPersonPreviewTopologyPosition % 2 == 0
        ? margin
        : gTileWindowWidth - overlayWidth - margin;
    const int top = gFirstPersonPreviewTopologyPosition < 2
        ? margin
        : gTileWindowHeight - overlayHeight - margin;
    if (left < 0 || top < 0
        || left + overlayWidth > gTileWindowWidth
        || top + overlayHeight > gTileWindowHeight) {
        gFirstPersonPreviewTopologyRendering = false;
        return;
    }

    if (overlayWidth <= 0 || overlayHeight <= 0 || overlayWidth > gTileWindowPitch) {
        gFirstPersonPreviewTopologyRendering = false;
        return;
    }

    static std::vector<unsigned char> overlay;
    overlay.assign(static_cast<size_t>(overlayWidth) * overlayHeight, COLOR_BLACK);
    {
        TileFirstPersonWindowState savedWindowState;
        gTileWindowBuffer = overlay.data();
        gTileWindowWidth = overlayWidth;
        gTileWindowHeight = overlayHeight;
        gTileWindowPitch = overlayWidth;
        tileRenderFirstPersonTopology();
    }

    bufferFill(gTileWindowBuffer + top * gTileWindowPitch + left,
        overlayWidth,
        overlayHeight,
        gTileWindowPitch,
        COLOR_BLACK);
    for (int y = 0; y < overlayHeight; y++) {
        memcpy(gTileWindowBuffer + (top + y) * gTileWindowPitch + left,
            overlay.data() + y * overlayWidth,
            overlayWidth);
    }
    bufferDrawRect(gTileWindowBuffer,
        gTileWindowPitch,
        left - 1,
        top - 1,
        left + overlayWidth,
        top + overlayHeight,
        COLOR_DARK_GREY_3);
    gFirstPersonPreviewTopologyRendering = false;
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
    if (renderContext->structuralOnly
        && objectType != OBJ_TYPE_WALL
        && objectType != OBJ_TYPE_SCENERY) {
        return;
    }
    if (artIsObjectTypeHidden(objectType)) {
        return;
    }

    const int mapSquareTile = squareTileFromTile(object->tile);
    if (!squareGridTileIsValid(mapSquareTile)) {
        return;
    }

    const int renderSquareTile = mapSquareTile / SQUARE_GRID_WIDTH * SQUARE_GRID_WIDTH
        + (SQUARE_GRID_WIDTH - 1 - mapSquareTile % SQUARE_GRID_WIDTH);
    const int cellX = renderContext->mapLeft + (renderSquareTile % SQUARE_GRID_WIDTH) * renderContext->cellSize;
    const int cellY = renderContext->mapTop + (renderSquareTile / SQUARE_GRID_WIDTH) * renderContext->cellSize;
    const int hexColumn = gHexGridWidth - 1 - object->tile % gHexGridWidth;
    const int hexRow = object->tile / gHexGridWidth;
    const int markerMaxLocal = std::max(0, renderContext->cellSize - 2);
    const int markerX = cellX + std::min(markerMaxLocal,
        (hexColumn & 1) == 0 ? 3 * renderContext->cellSize / 4 : renderContext->cellSize / 4);
    const int markerY = cellY + std::min(markerMaxLocal,
        (hexRow & 1) == 0 ? renderContext->cellSize / 4 : 3 * renderContext->cellSize / 4);

    if (markerX < 0 || markerX >= gTileWindowWidth || markerY < 0 || markerY >= gTileWindowHeight) {
        return;
    }

    Color markerColor = COLOR_LIGHT_GREY;
    Proto* proto = nullptr;
    const bool hasProto = protoGetProto(object, &proto) == 0;
    ProtoExtendedFlags cornerFlags = PROTO_EXT_FLAG_NONE;
    switch (objectType) {
    case OBJ_TYPE_WALL:
        markerColor = (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
                && (object->flags & (OBJECT_TRANS_GLASS | OBJECT_TRANS_WALL | OBJECT_LIGHT_THRU)) != OBJECT_NONE
            ? COLOR_LIGHT_BLUE
            : (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE ? COLOR_LIGHT_RED : COLOR_AMBER;
        if (hasProto) {
            cornerFlags = proto->wall.extendedFlags;
        }
        break;
    case OBJ_TYPE_SCENERY:
        if (hasProto && proto->scenery.type == SCENERY_TYPE_DOOR) {
            const bool doorOpen = (object->data.scenery.door.openFlags & DOOR_FLAG_OPEN) != 0
                || (object->flags & OBJECT_OPEN_DOOR) != OBJECT_NONE;
            markerColor = doorOpen || (object->flags & OBJECT_NO_BLOCK) != OBJECT_NONE
                ? COLOR_LIGHT_GREEN_3
                : COLOR_LIGHT_RED;
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

    const int markerSize = 1;
    if (markerSize > 0) {
        bufferFill(gTileWindowBuffer + markerY * gTileWindowPitch + markerX,
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
        const int centerX = markerX;
        const int centerY = markerY;
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

    // Show the shape consumed by the graybox wall grid. A vertical line is a
    // north/south wall (thin on X); a horizontal line is east/west (thin on Y).
    // Cell blockers and corner pieces use a box so their conservative geometry
    // is apparent in the topology view.
    const bool openDoor = objectType == OBJ_TYPE_SCENERY && hasProto
        && proto->scenery.type == SCENERY_TYPE_DOOR
        && ((object->data.scenery.door.openFlags & DOOR_FLAG_OPEN) != 0
            || (object->flags & OBJECT_OPEN_DOOR) != OBJECT_NONE);
    const bool halfHeightWall = objectType == OBJ_TYPE_WALL
        && (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
        && (object->flags & (OBJECT_TRANS_GLASS | OBJECT_TRANS_WALL | OBJECT_LIGHT_THRU)) != OBJECT_NONE;
    if ((objectType == OBJ_TYPE_WALL || objectType == OBJ_TYPE_SCENERY)
        && !openDoor
        && (halfHeightWall
            || (object->flags & (OBJECT_NO_BLOCK | OBJECT_LIGHT_THRU)) == OBJECT_NONE)) {
        TileFirstPersonWallOrientation orientation = TILE_FIRST_PERSON_WALL_ORIENTATION_CELL;
        if (objectType == OBJ_TYPE_WALL && hasProto) {
            constexpr unsigned int kCornerFlags = PROTO_EXT_FLAG_NORTH_CORNER
                | PROTO_EXT_FLAG_SOUTH_CORNER | PROTO_EXT_FLAG_EAST_CORNER
                | PROTO_EXT_FLAG_WEST_CORNER;
            const unsigned int flags = static_cast<unsigned int>(proto->wall.extendedFlags);
            if ((flags & kCornerFlags) == 0) {
                orientation = (flags & PROTO_EXT_FLAG_HIDDEN) != 0
                    ? TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST
                    : TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH;
            }
        }

        const int centerX = cellX + renderContext->cellSize / 2;
        const int centerY = cellY + renderContext->cellSize / 2;
        if (orientation == TILE_FIRST_PERSON_WALL_ORIENTATION_NORTH_SOUTH) {
            tileFirstPersonDrawDiagnosticLine(centerX, cellY, centerX, cellY + renderContext->cellSize - 1,
                COLOR_WHITE);
            tileFirstPersonDrawDiagnosticLine(centerX - 1, cellY, centerX - 1,
                cellY + renderContext->cellSize - 1, COLOR_CYAN);
        } else if (orientation == TILE_FIRST_PERSON_WALL_ORIENTATION_EAST_WEST) {
            tileFirstPersonDrawDiagnosticLine(cellX, centerY, cellX + renderContext->cellSize - 1, centerY,
                COLOR_WHITE);
            tileFirstPersonDrawDiagnosticLine(cellX, centerY - 1, cellX + renderContext->cellSize - 1,
                centerY - 1, COLOR_CYAN);
        } else {
            bufferDrawRect(gTileWindowBuffer, gTileWindowPitch, cellX + 1, cellY + 1,
                cellX + renderContext->cellSize - 2, cellY + renderContext->cellSize - 2, COLOR_WHITE);
        }
    }
}

bool tileFirstPersonPreviewIsEnabled()
{
    return gFirstPersonPreviewEnabled;
}

Rotation tileFirstPersonPreviewGetFacingRotation(int tile)
{
    if (!gFirstPersonPreviewEnabled
        || gDude == nullptr
        || !tileIsValid(gDude->tile)
        || !tileIsValid(tile)) {
        return ROTATION_NE;
    }

    double cameraX;
    double cameraY;
    tileFirstPersonGetCameraPosition(gDude->tile, gDude->elevation, &cameraX, &cameraY);
    return tileFirstPersonGetRotationToward(tile, cameraX, cameraY);
}

static Object* tileFirstPersonPreviewPickCritterAtPosition(int x, int y)
{
    tileFirstPersonPreviewFlushRefresh();
    if (!gFirstPersonPreviewEnabled
        || x < 0
        || x >= gTileWindowWidth
        || y < 0
        || y >= gTileWindowHeight
        || gFirstPersonPreviewWallDepths.size() != static_cast<size_t>(gTileWindowWidth)) {
        return nullptr;
    }

    Object* pickedObject = nullptr;
    double pickedDepth = std::numeric_limits<double>::infinity();
    for (const TileFirstPersonActorHitTarget& target : gFirstPersonPreviewActorHitTargets) {
        if (FrmId(target.object).objectType() != OBJ_TYPE_CRITTER) {
            continue;
        }
        if (target.depth >= pickedDepth
            || x < target.left
            || x >= target.left + target.width
            || y < target.top
            || y >= target.top + target.height
            || target.depth >= gFirstPersonPreviewWallDepths[x]) {
            continue;
        }

        FrmImage image;
        if (image.lock(FrmId(target.object), target.frame, target.rotation)) {
            const ConstBuffer2D frame = image.getBuffer();
            if (!frame || frame.width <= 0 || frame.height <= 0) {
                continue;
            }

            const int sourceX = std::clamp(
                (x - target.left) * frame.width / target.width, 0, frame.width - 1);
            const int sourceY = std::clamp(
                (y - target.top) * frame.height / target.height, 0, frame.height - 1);
            if (frame.data[sourceY * frame.width + sourceX] == 0) {
                continue;
            }
        }

        pickedObject = target.object;
        pickedDepth = target.depth;
    }

    return pickedObject;
}

Object* tileFirstPersonPreviewPickCritter()
{
    return tileFirstPersonPreviewPickCritterAtPosition(gTileWindowWidth / 2, gTileWindowHeight / 2);
}

Object* tileFirstPersonPreviewPickCritterAt(int screenX, int screenY)
{
    Rect isoWindowRect;
    if (windowGetRect(gIsoWindow, &isoWindowRect) != 0
        || screenX < isoWindowRect.left
        || screenX > isoWindowRect.right
        || screenY < isoWindowRect.top
        || screenY > isoWindowRect.bottom) {
        return nullptr;
    }

    return tileFirstPersonPreviewPickCritterAtPosition(
        screenX - isoWindowRect.left,
        screenY - isoWindowRect.top);
}

Object* tileFirstPersonPreviewPickObjectAt(int screenX, int screenY)
{
    tileFirstPersonPreviewFlushRefresh();
    if (!gFirstPersonPreviewEnabled
        || gFirstPersonPreviewWallDepths.size() != static_cast<size_t>(gTileWindowWidth)) {
        return nullptr;
    }

    Rect isoWindowRect;
    if (windowGetRect(gIsoWindow, &isoWindowRect) != 0
        || screenX < isoWindowRect.left || screenX > isoWindowRect.right
        || screenY < isoWindowRect.top || screenY > isoWindowRect.bottom) {
        return nullptr;
    }

    const int x = screenX - isoWindowRect.left;
    const int y = screenY - isoWindowRect.top;
    Object* pickedObject = nullptr;
    double pickedDepth = std::numeric_limits<double>::infinity();
    Object* fallbackObject = nullptr;
    double fallbackDepth = std::numeric_limits<double>::infinity();
    for (const TileFirstPersonActorHitTarget& target : gFirstPersonPreviewActorHitTargets) {
        const ObjectType objectType = FrmId(target.object).objectType();
        if (objectType != OBJ_TYPE_SCENERY && objectType != OBJ_TYPE_ITEM) {
            continue;
        }
        const int hitPadding = objectType == OBJ_TYPE_SCENERY
            ? std::max(6, target.width / 2)
            : std::max(4, target.width / 2);
        const int verticalHitPadding = objectType == OBJ_TYPE_SCENERY
            ? std::max(6, target.height / 4)
            : std::max(4, target.height / 2);
        if (target.depth >= pickedDepth
            || x < target.left - hitPadding
            || x >= target.left + target.width + hitPadding
            || y < target.top - verticalHitPadding
            || y >= target.top + target.height + verticalHitPadding
            || target.depth >= gFirstPersonPreviewWallDepths[x]) {
            continue;
        }
        if (target.depth < fallbackDepth) {
            fallbackObject = target.object;
            fallbackDepth = target.depth;
        }

        FrmImage image;
        if (image.lock(FrmId(target.object), target.frame, target.rotation)) {
            const ConstBuffer2D frame = image.getBuffer();
            if (!frame || frame.width <= 0 || frame.height <= 0) {
                continue;
            }

            const int sourceX = std::clamp(
                (x - target.left) * frame.width / target.width, 0, frame.width - 1);
            const int sourceY = std::clamp(
                (y - target.top) * frame.height / target.height, 0, frame.height - 1);
            if (frame.data[sourceY * frame.width + sourceX] == 0) {
                continue;
            }
        }

        pickedObject = target.object;
        pickedDepth = target.depth;
    }

    Object* result = pickedObject != nullptr ? pickedObject : fallbackObject;
    return result;
}

Object* tileFirstPersonPreviewPickDoorAt(int screenX, int screenY)
{
    tileFirstPersonPreviewFlushRefresh();
    if (!gFirstPersonPreviewEnabled
        || gFirstPersonPreviewWallDepths.size() != static_cast<size_t>(gTileWindowWidth)) {
        return nullptr;
    }

    Rect isoWindowRect;
    if (windowGetRect(gIsoWindow, &isoWindowRect) != 0
        || screenX < isoWindowRect.left || screenX > isoWindowRect.right
        || screenY < isoWindowRect.top || screenY > isoWindowRect.bottom) {
        return nullptr;
    }

    const int x = screenX - isoWindowRect.left;
    const int y = screenY - isoWindowRect.top;
    const int horizonY = gTileWindowHeight / 2;
    if (x < 0 || x >= gTileWindowWidth || y < horizonY || y >= gTileWindowHeight) {
        return nullptr;
    }

    Object* pickedDoor = nullptr;
    double pickedDepth = std::numeric_limits<double>::infinity();
    for (const TileFirstPersonDoorHitTarget& target : gFirstPersonPreviewDoorHitTargets) {
        if (target.screenX != x || target.depth >= pickedDepth) {
            continue;
        }
        const int fullWallHeight = std::min(gTileWindowHeight,
            static_cast<int>(gTileWindowHeight / target.depth));
        const int top = std::max(0, horizonY - fullWallHeight / 2);
        const int bottom = std::min(gTileWindowHeight - 1, horizonY + fullWallHeight / 2);
        if (y < top || y > bottom) {
            continue;
        }
        pickedDoor = target.object;
        pickedDepth = target.depth;
    }
    return pickedDoor;
}

int tileFirstPersonPreviewPickGroundTileAt(int screenX, int screenY)
{
    tileFirstPersonPreviewFlushRefresh();
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kFieldOfViewScale = 0.66;
    if (!gFirstPersonPreviewEnabled
        || gDude == nullptr || !elevationIsValid(gElevation)
        || screenY < gTileWindowHeight / 2 || screenY >= gTileWindowHeight
        || screenX < 0 || screenX >= gTileWindowWidth
        || gFirstPersonPreviewWallDepths.size() != static_cast<size_t>(gTileWindowWidth)) {
        return -1;
    }

    double cameraX;
    double cameraY;
    tileFirstPersonGetCameraPosition(gDude->tile, gDude->elevation, &cameraX, &cameraY);
    const double yaw = tileFirstPersonPreviewGetYaw() * kPi / 180.0;
    const double directionX = sin(yaw);
    const double directionY = -cos(yaw);
    const double cameraPlaneX = -directionY * kFieldOfViewScale;
    const double cameraPlaneY = directionX * kFieldOfViewScale;
    const double cameraOffset = 2.0 * screenX / gTileWindowWidth - 1.0;
    const double rayDirectionX = directionX + cameraPlaneX * cameraOffset;
    const double rayDirectionY = directionY + cameraPlaneY * cameraOffset;
    const double distance = (gTileWindowHeight * 0.5) / (screenY - gTileWindowHeight / 2);
    if (distance >= gFirstPersonPreviewWallDepths[screenX]) {
        return -1;
    }

    const int mapX = static_cast<int>(floor(cameraX + rayDirectionX * distance));
    const int mapY = static_cast<int>(floor(cameraY + rayDirectionY * distance));
    if (mapX < 0 || mapX >= SQUARE_GRID_WIDTH || mapY < 0 || mapY >= SQUARE_GRID_HEIGHT) {
        return -1;
    }

    const int squareTile = mapY * SQUARE_GRID_WIDTH
        + (SQUARE_GRID_WIDTH - 1 - mapX);
    const FloorTileFrmId floorTileFrmId(gTileSquares[gElevation]->tileFid[squareTile]);
    if (floorTileFrmId.frameId<TileFrameId>() == TileFrameId::Invalid
        || (floorTileFrmId.flags() & TileFlags::TemporarilyHidden) != TileFlags::None) {
        return -1;
    }

    const int tileColumn = gHexGridWidth - 1
        - static_cast<int>(floor((cameraX + rayDirectionX * distance - 0.25) * 2.0 + 0.5));
    const int tileRow = static_cast<int>(floor((cameraY + rayDirectionY * distance - 0.25) * 2.0 + 0.5));
    const int tile = tileRow * gHexGridWidth + tileColumn;
    return tileIsValid(tile) ? tile : -1;
}

void tileFirstPersonPreviewSetMoveMarkerTile(int tile)
{
    if (!gFirstPersonPreviewEnabled) {
        tile = -1;
    } else if (!tileIsValid(tile)) {
        tile = -1;
    }

    if (gFirstPersonPreviewMoveMarkerTile != tile) {
        gFirstPersonPreviewMoveMarkerTile = tile;
        tileFirstPersonPreviewRefresh();
    }
}

bool tileFirstPersonPreviewMove(int relativeDirection)
{
    if (!gFirstPersonPreviewEnabled
        || gDude == nullptr
        || !tileIsValid(gDude->tile)
        || relativeDirection < 0
        || relativeDirection > 3) {
        return false;
    }

    if (animationIsBusy(gDude) == -1) {
        gFirstPersonPreviewPendingMoveDirection = relativeDirection;
        gFirstPersonPreviewPendingMoveTime = getTicks();
        return true;
    }
    gFirstPersonPreviewPendingMoveDirection = -1;

    int actionPoints = -1;
    if (isInCombat()) {
        actionPoints = gDude->data.critter.combat.ap + _combat_free_move;
        if (actionPoints <= 0) {
            return false;
        }
    }

    constexpr double kPi = 3.14159265358979323846;
    const double targetYaw = (gFirstPersonPreviewYaw + relativeDirection * 90) * kPi / 180.0;
    const double targetX = sin(targetYaw);
    const double targetY = -cos(targetYaw);
    Rotation movementRotation = ROTATION_NE;
    double bestDot = -std::numeric_limits<double>::infinity();
    double playerX;
    double playerY;
    if (!tileFirstPersonGetWorldPosition(gDude->tile, &playerX, &playerY)) {
        return false;
    }
    for (int rotationIndex = ROTATION_FIRST; rotationIndex < ROTATION_COUNT; rotationIndex++) {
        const Rotation candidate = static_cast<Rotation>(rotationIndex);
        const int adjacentTile = tileGetTileInDirection(gDude->tile, candidate, 1);
        double adjacentX;
        double adjacentY;
        if (!tileFirstPersonGetWorldPosition(adjacentTile, &adjacentX, &adjacentY)) {
            continue;
        }

        double directionX = adjacentX - playerX;
        double directionY = adjacentY - playerY;
        const double directionLength = sqrt(directionX * directionX + directionY * directionY);
        if (directionLength <= 0.0) {
            continue;
        }

        directionX /= directionLength;
        directionY /= directionLength;
        const double dot = directionX * targetX + directionY * targetY;
        if (dot > bestDot) {
            bestDot = dot;
            movementRotation = candidate;
        }
    }

    if (bestDot == -std::numeric_limits<double>::infinity()) {
        return false;
    }

    const int destinationTile = tileGetTileInDirection(gDude->tile, movementRotation, 1);
    if (!tileIsValid(destinationTile)) {
        return false;
    }

    if (reg_anim_begin(ANIMATION_REQUEST_RESERVED) != 0) {
        return false;
    }
    const int result = animationRegisterRunToTile(gDude, destinationTile, gDude->elevation, actionPoints, 0);
    const int endResult = reg_anim_end();
    return result == 0 && endResult == 0;
}

void tileFirstPersonPreviewProcessPendingMove()
{
    if (gFirstPersonPreviewPendingMoveDirection == -1) {
        return;
    }

    if (!gFirstPersonPreviewEnabled
        || gDude == nullptr
        || !tileIsValid(gDude->tile)
        || getTicksBetween(getTicks(), gFirstPersonPreviewPendingMoveTime) > 500
        || (isInCombat() && gDude->data.critter.combat.ap + _combat_free_move <= 0)) {
        gFirstPersonPreviewPendingMoveDirection = -1;
        return;
    }

    if (animationIsBusy(gDude) == -1) {
        return;
    }

    const int direction = gFirstPersonPreviewPendingMoveDirection;
    gFirstPersonPreviewPendingMoveDirection = -1;
    tileFirstPersonPreviewMove(direction);
}

void tileFirstPersonPreviewRequestRefresh()
{
    if (gFirstPersonPreviewEnabled && !gFirstPersonPreviewTopologyRendering) {
        gFirstPersonPreviewRefreshPending = true;
    }
}

void tileFirstPersonPreviewFlushRefresh()
{
    if (gFirstPersonPreviewRefreshPending) {
        tileFirstPersonPreviewRefresh();
    }
}

void tileFirstPersonPreviewRefresh()
{
    if (!gFirstPersonPreviewEnabled || gFirstPersonPreviewTopologyRendering
        || gFirstPersonPreviewRefreshing
        || gTileWindowBuffer == nullptr || gTileWindowRefreshProc == nullptr) {
        return;
    }

    // Consume before rendering so any new invalidation is retained. Guard
    // against recursive flushes from window/cursor composition callbacks.
    gFirstPersonPreviewRefreshPending = false;
    gFirstPersonPreviewRefreshing = true;
    tileRenderFirstPersonPreview();
    gTileWindowRefreshProc(&gTileWindowRect);
    gpuBillboardMarkSceneComplete();
    if (!cursorIsHidden()) {
        mouseShowCursor();
    }
    gFirstPersonPreviewRefreshing = false;
}

void tileFirstPersonPreviewRefreshCombat()
{
    if (!gFirstPersonPreviewEnabled) {
        return;
    }

    const int preservedYaw = static_cast<int>(tileFirstPersonPreviewGetYaw());
    tileFirstPersonPreviewToggle();
    tileFirstPersonPreviewToggle();
    gFirstPersonPreviewYaw = preservedYaw;
    gFirstPersonPreviewYawTurning = false;
    tileFirstPersonPreviewRefresh();
}

void tileFirstPersonPreviewToggleTopology()
{
    if (!gFirstPersonPreviewEnabled) {
        return;
    }

    gFirstPersonPreviewTopologyPosition = (gFirstPersonPreviewTopologyPosition + 1) % 5;
    gFirstPersonPreviewTopologyEnabled = gFirstPersonPreviewTopologyPosition != 4;
    if (!gFirstPersonPreviewTopologyEnabled) {
        gFirstPersonPreviewTopologyZoom = 1;
    }
    tileFirstPersonPreviewRefresh();
}

bool tileFirstPersonPreviewZoomTopology(int direction)
{
    if (!gFirstPersonPreviewEnabled || !gFirstPersonPreviewTopologyEnabled || direction == 0) {
        return false;
    }

    constexpr int kMaximumTopologyZoom = 3;
    gFirstPersonPreviewTopologyZoom = std::max(1,
        std::min(kMaximumTopologyZoom, gFirstPersonPreviewTopologyZoom + (direction > 0 ? 1 : -1)));
    tileFirstPersonPreviewRefresh();
    return true;
}

void tileFirstPersonPreviewLook(int deltaX)
{
    if (!gFirstPersonPreviewEnabled || deltaX == 0) {
        return;
    }

    gFirstPersonPreviewYaw = static_cast<int>(tileFirstPersonPreviewGetYaw());
    gFirstPersonPreviewYaw = (gFirstPersonPreviewYaw + deltaX) % 360;
    if (gFirstPersonPreviewYaw < 0) {
        gFirstPersonPreviewYaw += 360;
    }
    gFirstPersonPreviewYawTurning = false;

    tileFirstPersonPreviewRequestRefresh();
}

void tileFirstPersonPreviewTurnToTile(int tile)
{
    if (!gFirstPersonPreviewEnabled || gDude == nullptr || !tileIsValid(tile)) {
        return;
    }

    double currentX;
    double currentY;
    double targetX;
    double targetY;
    if (!tileFirstPersonGetWorldPosition(gDude->tile, &currentX, &currentY)
        || !tileFirstPersonGetWorldPosition(tile, &targetX, &targetY)) {
        return;
    }

    constexpr double kPi = 3.14159265358979323846;
    double targetYaw = atan2(targetX - currentX, -(targetY - currentY)) * 180.0 / kPi;
    if (targetYaw < 0.0) {
        targetYaw += 360.0;
    }

    const double currentYaw = tileFirstPersonPreviewGetYaw();
    double delta = targetYaw - currentYaw;
    while (delta > 180.0) {
        delta -= 360.0;
    }
    while (delta < -180.0) {
        delta += 360.0;
    }

    gFirstPersonPreviewYawStart = currentYaw;
    gFirstPersonPreviewYawTarget = currentYaw + delta;
    gFirstPersonPreviewYawStartTime = getTicks();
    gFirstPersonPreviewYawTurning = true;
}

void tileFirstPersonPreviewToggle()
{
    if (gTileWindowBuffer == nullptr || gTileWindowRefreshProc == nullptr) {
        return;
    }

    gFirstPersonPreviewEnabled = !gFirstPersonPreviewEnabled;
    if (gFirstPersonPreviewEnabled) {
        gFirstPersonPreviewTopologyEnabled = false;
        gFirstPersonPreviewTopologyPosition = -1;
        gFirstPersonPreviewTopologyZoom = 0;
        tileFirstPersonPreviewResetForModeRefresh();
        tileFirstPersonPreviewRefresh();
    } else {
        gFirstPersonPreviewRefreshPending = false;
        gpuBillboardClear();
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
    const bool profileRoof = gFirstPersonPreviewEnabled && settings.debug.show_fps;
    const unsigned int start = profileRoof ? getTicks() : 0;
    unsigned int visitedTasks = 0;
    std::stack<roof_fill_task> tasks_stack;

    roof_fill_push_task_if_in_bounds(tasks_stack, x, y);

    while (!tasks_stack.empty()) {
        roof_fill_off_process_task(tasks_stack, elevation, on);
        if (profileRoof) {
            visitedTasks++;
        }
    }
    if (profileRoof) {
        const unsigned int end = getTicks();
        debugPrint("F11 roof fill: tick=%u tile=%d elevation=%d square=(%d,%d) visible=%d tasks=%u ms=%u\n",
            end, gDude != nullptr ? gDude->tile : -1,
            static_cast<int>(elevation), x, y, on ? 1 : 0,
            visitedTasks, getTicksBetween(end, start));
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
