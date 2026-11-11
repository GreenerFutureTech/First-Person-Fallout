#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "content_config.h"
#include "dinput.h"
#include "game_config.h"
#include "sfall_script_hooks.h"
#include "map.h" // mapSetEnteringLocation — an entrance carries its arrival spot
#include "server_players.h" // playerActorAt/Online — the party is every player, not slot 0
#include "msg_channel.h" // kMsgChannelReward — the encounter-catch XP is addressed
#include "client_net.h" // clientViewerActive — viewer skips the local map teardown
#include "server_loop.h"
#include "sim_clock.h"
#include "message.h"
#include "worldmap_ui.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "animation.h"
#include "art.h"
#include "automap.h"
#include "client_net.h"
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
#include "critter.h"
#include "cycle.h"
#include "db.h"
#include "dbox.h"
#include "debug.h"
#include "display_monitor.h"
#include "draw.h"
#include "game.h"
#include "game_mouse.h"
#include "game_movie.h"
#include "game_sound.h"
#include "input.h"
#include "interface.h"
#include "item.h"
#include "kb.h"
#include "map.h" // mapGetCurrentMap/mapGetName/gElevation — naming the map the ESC prompt returns to
#include "memory.h"
#include "mouse.h"
#include "object.h"
#include "palette.h"
#include "party_member.h"
#include "perk.h"
#include "presenter.h"
#include "proto_instance.h"
#include "queue.h"
#include "random.h"
#include "scripts.h"
#include "settings.h"
#include "sfall_config.h"
#include "sfall_global_scripts.h"
#include "skill.h"
#include "stat.h"
#include "string_parsers.h"
#include "svga.h"
#include "text_font.h"
#include "tile.h"
#include "window_manager.h"
#include "worldmap.h"
#include "worldmap_defs.h"

namespace fallout {
#define WM_TOWN_LIST_X (501)
#define WM_TOWN_LIST_Y (135)
#define WM_TOWN_LIST_WIDTH (119)
#define WM_TOWN_LIST_HEIGHT (178)
#define WM_OVERLAY_BUFFER_SIZE (200)

static constexpr int WM_TOWN_LIST_VISIBLE_SLOT_COUNT = 7;
static constexpr int WM_TOWN_LIST_SLOT_HEIGHT = 27;
static bool gMousePressed = false;

class WorldmapFirstPersonMouseScope {
public:
    WorldmapFirstPersonMouseScope()
        : _firstPersonPreviewWasEnabled(tileFirstPersonPreviewIsEnabled())
        , _relativeModeWasEnabled(mouseDeviceUsesRelativeMode())
        , _cursorWasHidden(cursorIsHidden())
    {
        if (_firstPersonPreviewWasEnabled) {
            if (_relativeModeWasEnabled && !mouseDeviceSetRelativeModeForced(false)) {
                debugPrint("Failed to release mouse for world map: %s\n", SDL_GetError());
            }
            mouseShowCursor();
        }
    }

    ~WorldmapFirstPersonMouseScope()
    {
        if (!_firstPersonPreviewWasEnabled || !tileFirstPersonPreviewIsEnabled()) {
            return;
        }

        if (_relativeModeWasEnabled && !mouseDeviceSetRelativeModeForced(true)) {
            debugPrint("Failed to restore mouse mode after world map: %s\n", SDL_GetError());
        }
        if (_cursorWasHidden) {
            mouseHideCursor();
        } else {
            mouseShowCursor();
        }
    }

private:
    bool _firstPersonPreviewWasEnabled;
    bool _relativeModeWasEnabled;
    bool _cursorWasHidden;
};


// 0x51DE38
static int wmInterfaceWasInitialized = 0;

bool gPendingWorldmapEnter = false;
bool gWorldmapStreaming = false;
bool gWorldmapStateDirty = false;

// 0x51DE84
static const int wmRndCursorFids[WORLD_MAP_ENCOUNTER_FRM_COUNT] = {
    154,
    155,
    438,
    439,
};

// 0x51DE94
static int* wmLabelList = nullptr;

// 0x51DE98
static int wmLabelCount = 0;

// 0x51DE9C
static int wmTownMapCurArea = -1;

// Buttons for city entrances.
//
// 0x672DD8
static int wmTownMapButtonId[ENTRANCE_LIST_CAPACITY];

// 0x672FD8
static int wmTownMapSubButtonIds[WM_TOWN_LIST_VISIBLE_SLOT_COUNT];

static FrmImage _backgroundFrmImage;

static FrmImage _townFrmImage;

static bool wmFaded = false;

static int wmWorldMapFunc(int a1);
static void wmCheckGameEvents();
static void wmInterfaceScrollTabsStart(int delta);
static void wmInterfaceScrollTabsStop();
static void wmInterfaceScrollTabsUpdate();
static int wmInterfaceInit();
static int wmInterfaceExit();
static int wmInterfaceScroll(int dx, int dy, bool* successPtr);
static void wmMouseBkProc();
static int wmTileGrabArt(int tileIdx);
int wmInterfaceRefresh();
static int wmInterfaceDrawCircleOverlay(CityInfo* cityInfo, CitySizeDescription* citySizeInfo, unsigned char* buffer, int x, int y);
static void wmInterfaceDrawSubTileRectFogged(unsigned char* dest, int width, int height, int pitch);
static int wmInterfaceDrawSubTileList(TileInfo* tileInfo, int column, int row, int x, int y, int a6);
static int wmDrawCursorStopped();
static int wmTownMapFunc(Map* mapIdxPtr, int* entranceIdxPtr = nullptr);
static bool wmViewerEnterCurrentArea();
static int wmTownMapInit();
static int wmTownMapRefresh();
static int wmTownMapExit();
int wmRefreshInterfaceOverlay(bool shouldRefreshWindow);
static void wmInterfaceRefreshCarFuel();
static int wmRefreshTabs();
static int wmMakeTabsLabelList(int** quickDestinationsPtr, int* quickDestinationsLengthPtr);
static int wmTabsCompareNames(const void* a1, const void* a2);
static int wmFreeTabsLabelList(int** quickDestinationsListPtr, int* quickDestinationsLengthPtr);
static void wmRefreshInterfaceDial(bool shouldRefreshWindow);

// ►►►► ESC ON THE STREAMED WORLDMAP ASKS BEFORE IT ACTS.
// Vanilla's wmWorldMapFunc has no KEY_ESCAPE branch at all — its worldmap has exactly
// one exit, enter a location — so nothing here is being changed for local play, and
// there is no vanilla behaviour a prompt could get in the way of.
//
// What our ESC does is send an INTENT that makes the authority cancel the whole travel
// session and put the party back into the map they walked out of, rewinding the world
// position with it (server_worldmap.cc, WM_INTENT_ESCAPE). That is a state change made
// on everyone's behalf — in co-op this is not "my screen", it is the screen the entire
// server is parked on — and it is not what a player pressing the universal get-me-out-of
// -this-screen key expects. Naming the destination map is the whole point of the prompt:
// it is the only thing that says ESC means "go back where we came from", not "leave the
// worldmap from here".
//
// Returns true if the player confirmed. Callers gate on gWorldmapStreaming, so local
// play never reaches this.
static bool wmConfirmReturnToMap()
{
    // The same map name the automap titles itself with (automap.cc). map.msg carries a
    // real description for EVERY map index, random-encounter terrain included
    // ("Desert"), so this reads sensibly whether the party walked out of Vault City or
    // out of a spore plant ambush. nullptr only for an out-of-range index.
    char prompt[128];
    const char* mapName = mapGetName(mapGetCurrentMap(), gElevation);
    if (mapName != nullptr && mapName[0] != '\0') {
        snprintf(prompt, sizeof(prompt), "Return to %s?", mapName);
    } else {
        snprintf(prompt, sizeof(prompt), "Leave the world map?");
    }

    int rc = showDialogBox(prompt, nullptr, 0, 169, 117,
        _colorTable[32328], nullptr, _colorTable[32328], DIALOG_BOX_YES_NO);

    // Repaint what the box covered. It destroys its own window (never a neighbour's),
    // but the worldmap is cheap to redraw and this also picks up any state the wire
    // delivered while the box owned input — showDialogBox's nested loop keeps running
    // tickersExecute, so the connection is still being serviced underneath it.
    wmInterfaceRefresh();

    return rc != 0;
}

// 0x4BFE0C
void wmWorldMap()
{
    wmWorldMapFunc(0);
}

// 0x4BFE10
static int wmWorldMapFunc(int a1)
{
    ScopedGameMode gm(GameMode::kWorldmap);
    WorldmapFirstPersonMouseScope firstPersonMouseScope;

    wmResetTrailMarkers();
    // ►► SNAPSHOT BEFORE ANYTHING THAT PUMPS THE WIRE, not after. This used to be read
    // further down, past two fades and wmInterfaceInit — all of which reach
    // tickersExecute, and ScopedGameMode has already put us in kWorldmap, which is
    // exactly the mask the viewer's service ticker gates itself to. So a worldmapEnd
    // decoded during init cleared gWorldmapStreaming BEFORE it was captured, leaving
    // streamingEntered false and the exit test below dead for the whole session — on a
    // screen whose only other way out is the `wmesc` INTENT, which a server with no
    // worldmap session discards. Reading it here cannot race the wire.
    bool streamingEntered = gWorldmapStreaming;

    debugPrint("worldmap_ui: modal start streaming=%d pos=%d,%d area=%d\n",
        streamingEntered ? 1 : 0, wmGenData.worldPosX, wmGenData.worldPosY, wmGenData.currentAreaId);
    wmFadeOut();
    debugPrint("worldmap_ui: fade-out complete\n");

    if (wmInterfaceInit() == -1) {
        wmInterfaceExit();
        wmFadeReset();
        debugPrint("worldmap_ui: interface init failed\n");
        return -1;
    }
    debugPrint("worldmap_ui: interface init complete\n");

    debugPrint("worldmap_ui: restoring palette\n");
    wmFadeIn();
    debugPrint("worldmap_ui: palette restored\n");

    touch_set_touchscreen_mode(false);
    debugPrint("worldmap_ui: setup complete; entering modal loop\n");

    wmMatchWorldPosToArea(wmGenData.worldPosX, wmGenData.worldPosY, &(wmGenData.currentAreaId));

    // SFALL CarPlacedTileFix — the solo/host half of the pair (the dedicated server
    // does it in worldmapServerDriver). No-ops for a viewer, which does not own gvars.
    wmCarClearPlacedTile();

    unsigned int partyHealTime = 0;
    Map map = MAP_INVALID;
    int rc = 0;
    wmResetTerrainInfo();
    wmLastTravelTick = getTicks();

    while (true) {
        sharedFpsLimiter.mark();

        // Streaming mode: the server ended the worldmap session; exit.
        if (streamingEntered && !gWorldmapStreaming) {
            break;
        }

        int keyCode = inputGetInput();

        // SFALL: WorldmapLoopHook.
        sfall_gl_scr_process_worldmap();

        unsigned int now = getTicks();

        int mouseX;
        int mouseY;
        mouseGetPositionInWindow(wmBkWin, &mouseX, &mouseY);

        int worldX = wmWorldOffsetX + mouseX - WM_VIEW_X;
        int worldY = wmWorldOffsetY + mouseY - WM_VIEW_Y;

        bool terrainInfoIsVisible = (worldmapTerrainInfo || !wmTownTitleOverrides.empty())
            && !wmGenData.isWalking
            && wmCursorIsVisible()
            && mouseHitTestInWindow(wmBkWin, WM_VIEW_X, WM_VIEW_Y, WM_VIEW_WIDTH + WM_VIEW_X, WM_VIEW_HEIGHT + WM_VIEW_Y)
            && abs(wmGenData.worldPosX - worldX) < 8
            && abs(wmGenData.worldPosY - worldY) < 6
            && wmGetHotspotText() != nullptr;
        if (terrainInfoIsVisible != wmTerrainInfoIsVisible) {
            wmTerrainInfoIsVisible = terrainInfoIsVisible;
            wmInterfaceRefresh();
        }

        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
            showQuitConfirmationDialog();
        }

        // NOTE: Uninline.
        wmCheckGameEvents();

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        int mouseEvent = mouseGetEvent();

        if (gWorldmapStreaming) {
            // The server owns the travel sim; we just render whatever state the
            // wire last delivered. Repaint on EVERY state change, NOT only while
            // walking: the arrival frame is precisely the one where isWalking
            // flips to false, and refreshing only inside an isWalking branch
            // means that final frame is never drawn — the map keeps showing the
            // travelling cross instead of the arrived-at marker until some other
            // event happens to force a repaint.
            if (gWorldmapStateDirty) {
                gWorldmapStateDirty = false;
                if (wmGenData.isWalking) {
                    // Follow the party while travelling. Vanilla gets this from
                    // wmInterfaceScrollPixel inside wmPartyWalkingStep, which is
                    // suppressed server-side (it is pure chrome).
                    wmInterfaceCenterOnParty();
                }
                wmInterfaceRefresh();
            }
        } else if (wmGenData.isWalking && wmTravelTickDue(now)) {
            {
                worldmapTravelStep(worldX, worldY);

                wmInterfaceRefresh();

                if (worldmapTravelRestHeal(now, partyHealTime)) {
                    interfaceRenderHitPoints(false);
                    partyHealTime = now;
                }

                worldmapTravelMarkVisited();

                wmInterfaceRefresh();

                if (worldmapTravelClockTick()) {
                    if (_game_user_wants_to_quit != 0) {
                        break;
                    }
                }

                if (wmGenData.isWalking) {
                    if (worldmapTravelEncounterCheck()) {
                        if (wmGenData.encounterMapId != -1) {
                            wmFadeOut();
                            mapLoadById(wmGenData.encounterMapId);
                            wmFadeIn();
                        }
                        break;
                    }
                }
            }
        }

        if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0 && (mouseEvent & MOUSE_EVENT_LEFT_BUTTON_REPEAT) == 0) {
            if (mouseHitTestInWindow(wmBkWin, WM_VIEW_X, WM_VIEW_Y, WM_VIEW_WIDTH + WM_VIEW_X, WM_VIEW_HEIGHT + WM_VIEW_Y)) {
                if (!wmGenData.isWalking && !gMousePressed && abs(wmGenData.worldPosX - worldX) < 5 && abs(wmGenData.worldPosY - worldY) < 5) {
                    gMousePressed = true;
                    wmInterfaceRefresh();
                    renderFpsCounter();
                    renderPresent();
                }
            } else {
                continue;
            }
        }

        if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_UP) != 0) {
            if (gMousePressed) {
                gMousePressed = false;
                wmInterfaceRefresh();

                if (abs(wmGenData.worldPosX - worldX) < 5 && abs(wmGenData.worldPosY - worldY) < 5) {
                    if (gWorldmapStreaming) {
                        // The server resolves the destination (a chosen entrance, a
                        // city's first valid map, or map 0 for open wasteland). NOT
                        // gated on currentAreaId != -1: clicking your own position on
                        // open wasteland must still drop you into the world, and the
                        // local `map = 0` below is inert here since only the server
                        // may load.
                        wmViewerEnterCurrentArea();
                    } else if (wmGenData.currentAreaId != -1) {
                        CityInfo* city = &(wmAreaInfoList[wmGenData.currentAreaId]);
                        if (city->visitedState == VisitedState::Visited && city->mapFid != -1) {
                            if (wmTownMapFunc(&map) == -1) {
                                rc = -1;
                                break;
                            }
                        } else {
                            MapElevation elevation;
                            int tile;
                            Rotation rotation;
                            if (wmAreaFindFirstValidMap(&map, &elevation, &tile, &rotation) == -1) {
                                rc = -1;
                                break;
                            }

                            mapSetEnteringLocation(elevation, tile, rotation);

                            // SFALL/CE: LocalMapEnter runs after this first-entry
                            // state transition, so a hook that redirects to a
                            // different map still leaves the clicked area marked
                            // visited.
                            city->visitedState = VisitedState::Visited;
                        }
                    } else {
                        map = MAP_FIRST;
                    }

                    if (map != MAP_INVALID && !gWorldmapStreaming) {
                        wmRunLocalMapEnterHook(&map);
                        if (wmGenData.isInCar) {
                            wmGenData.isInCar = false;
                            if (wmGenData.currentAreaId == CITY_INVALID) {
                                City areaIdx;
                                if (wmTryMatchAreaContainingMapIdx(map, &areaIdx)) {
                                    wmGenData.currentCarAreaId = areaIdx;
                                }
                            } else {
                                wmGenData.currentCarAreaId = wmGenData.currentAreaId;
                            }
                        }

                        mapLoadById(map);
                        break;
                    }
                }
            } else {
                if (mouseHitTestInWindow(wmBkWin, WM_VIEW_X, WM_VIEW_Y, WM_VIEW_WIDTH + WM_VIEW_X, WM_VIEW_HEIGHT + WM_VIEW_Y)) {
                    if (gWorldmapStreaming) {
                        clientViewerWmMove(worldX, worldY);
                    } else {
                        wmPartyInitWalking(worldX, worldY);
                    }
                }

                gMousePressed = false;
            }
        }

        // NOTE: Uninline.
        wmInterfaceScrollTabsUpdate();

        if (keyCode == KEY_UPPERCASE_T || keyCode == KEY_LOWERCASE_T) {
            if (!wmGenData.isWalking && wmGenData.currentAreaId != -1) {
                if (gWorldmapStreaming) {
                    wmViewerEnterCurrentArea();
                } else {
                    CityInfo* city = &(wmAreaInfoList[wmGenData.currentAreaId]);
                    if (city->visitedState == VisitedState::Visited && city->mapFid != -1) {
                        if (wmTownMapFunc(&map) == -1) {
                            rc = -1;
                        }

                        if (map != MAP_INVALID) {
                            if (wmGenData.isInCar) {
                                wmGenData.isInCar = false;
                                wmCarParkAtMapArea(map);
                            }

                            wmFadeOut();
                            mapLoadById(map);
                        }
                    }
                }
            }
        } else if (keyCode == KEY_ESCAPE) {
            if (gWorldmapStreaming && wmConfirmReturnToMap()) {
                // ►► RE-CHECK STREAMING AFTER THE PROMPT. showDialogBox runs a nested
                // input loop that keeps pumping the wire (tickersExecute → the viewer
                // service ticker), so the server can have ended the session while the
                // box was open — a random encounter fired, a pump bail, or the deferred
                // -blob branch in client_net.cc cleared the flag for us. Sending into
                // that gap is mostly harmless, because server_control.cc refuses the
                // verb outright when no session is active; what it must not do is land
                // on the NEXT session, which the answer to a stale prompt would cancel
                // before the player had even seen it.
                if (gWorldmapStreaming) {
                    clientViewerWmEscape();
                }
            }
        } else if (keyCode == KEY_HOME) {
            wmInterfaceCenterOnParty();
        } else if (keyCode == KEY_ARROW_UP) {
            // NOTE: Uninline.
            wmInterfaceScroll(0, -1, nullptr);
        } else if (keyCode == KEY_ARROW_LEFT) {
            // NOTE: Uninline.
            wmInterfaceScroll(-1, 0, nullptr);
        } else if (keyCode == KEY_ARROW_DOWN) {
            // NOTE: Uninline.
            wmInterfaceScroll(0, 1, nullptr);
        } else if (keyCode == KEY_ARROW_RIGHT) {
            // NOTE: Uninline.
            wmInterfaceScroll(1, 0, nullptr);
        } else if (keyCode == KEY_CTRL_ARROW_UP) {
            wmInterfaceScrollTabsStart(-WM_TOWN_LIST_SLOT_HEIGHT);
        } else if (keyCode == KEY_CTRL_ARROW_DOWN) {
            wmInterfaceScrollTabsStart(WM_TOWN_LIST_SLOT_HEIGHT);
        } else if (keyCode >= KEY_CTRL_F1 && keyCode <= KEY_CTRL_F7) {
            int quickDestinationIndex = wmGenData.tabsOffsetY / WM_TOWN_LIST_SLOT_HEIGHT + (keyCode - KEY_CTRL_F1);
            if (quickDestinationIndex < wmLabelCount) {
                City areaIdx = static_cast<City>(wmLabelList[quickDestinationIndex]);
                CityInfo* city = &(wmAreaInfoList[areaIdx]);
                if (wmAreaIsKnown(city->areaId)) {
                    if (wmGenData.currentAreaId != areaIdx) {
                        int destX;
                        int destY;
                        wmAreaGetMarkWorldPos(city, &destX, &destY);
                        if (gWorldmapStreaming) clientViewerWmMove(destX, destY);
                        else wmPartyInitWalking(destX, destY);
                        gMousePressed = false;
                    }
                }
            }
        }

        if ((mouseEvent & MOUSE_EVENT_WHEEL) != 0) {
            int wheelX;
            int wheelY;
            mouseGetWheel(&wheelX, &wheelY);

            if (mouseHitTestInWindow(wmBkWin, WM_VIEW_X, WM_VIEW_Y, WM_VIEW_WIDTH + WM_VIEW_X, WM_VIEW_HEIGHT + WM_VIEW_Y)) {
                wmInterfaceScrollPixel(20, 20, wheelX, -wheelY, nullptr, true);
            } else if (mouseHitTestInWindow(wmBkWin,
                           WM_TOWN_LIST_X,
                           WM_TOWN_LIST_Y,
                           WM_TOWN_LIST_X + WM_TOWN_LIST_WIDTH,
                           WM_TOWN_LIST_Y + WM_TOWN_LIST_HEIGHT)) {
                if (wheelY != 0) {
                    wmInterfaceScrollTabsStart(wheelY > 0 ? -WM_TOWN_LIST_SLOT_HEIGHT : WM_TOWN_LIST_SLOT_HEIGHT);
                }
            }
        }

        if (map != MAP_INVALID || rc == -1) {
            break;
        }

        renderFpsCounter();
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (wmInterfaceExit() == -1) {
        paletteSetEntries(_cmap);
        return -1;
    }

    return rc;
}

// 0x4C05C4
int wmInterfaceCenterOnParty()
{
    wmWorldOffsetX = std::clamp(wmGenData.worldPosX - 203, 0, wmGenData.viewportMaxX);
    wmWorldOffsetY = std::clamp(wmGenData.worldPosY - 200, 0, wmGenData.viewportMaxY);

    wmInterfaceRefresh();

    return 0;
}

// NOTE: Inlined.
//
// 0x4C0624
static void wmCheckGameEvents()
{
    _scriptsCheckGameEvents(nullptr, wmBkWin);
}

// 0x4C219C
static void wmInterfaceScrollTabsStart(int delta)
{
    int tabsScrollMaxOffsetY = std::max(0,
        wmGenData.tabsBackgroundFrmImage.getHeight() - (WM_TOWN_LIST_HEIGHT + 52));

    // SFALL: Fix world map cities list scrolling bug that might leave buttons
    // in the disabled state.
    if (delta >= 0) {
        if (wmGenData.tabsOffsetY < tabsScrollMaxOffsetY) {
            wmGenData.oldTabsOffsetY = std::min(wmGenData.tabsOffsetY + delta, tabsScrollMaxOffsetY);
            wmGenData.tabsScrollingDelta = delta;
        }
    } else {
        if (wmGenData.tabsOffsetY > 0) {
            wmGenData.oldTabsOffsetY = std::max(wmGenData.tabsOffsetY + delta, 0);
            wmGenData.tabsScrollingDelta = delta;
        }
    }

    if (wmGenData.tabsScrollingDelta == 0) {
        return;
    }

    for (int index = 0; index < WM_TOWN_LIST_VISIBLE_SLOT_COUNT; index++) {
        buttonDisable(wmTownMapSubButtonIds[index]);
    }

    wmInterfaceScrollTabsUpdate();
}

// 0x4C2270
static void wmInterfaceScrollTabsStop()
{
    wmGenData.tabsScrollingDelta = 0;

    for (int index = 0; index < WM_TOWN_LIST_VISIBLE_SLOT_COUNT; index++) {
        buttonEnable(wmTownMapSubButtonIds[index]);
    }
}

// NOTE: Inlined.
//
// 0x4C2290
static void wmInterfaceScrollTabsUpdate()
{
    if (wmGenData.tabsScrollingDelta != 0) {
        wmGenData.tabsOffsetY += wmGenData.tabsScrollingDelta;
        wmRefreshInterfaceOverlay(true);

        if (wmGenData.tabsScrollingDelta >= 0) {
            if (wmGenData.oldTabsOffsetY <= wmGenData.tabsOffsetY) {
                // NOTE: Uninline.
                wmInterfaceScrollTabsStop();
            }
        } else {
            if (wmGenData.oldTabsOffsetY >= wmGenData.tabsOffsetY) {
                // NOTE: Uninline.
                wmInterfaceScrollTabsStop();
            }
        }
    }
}

// 0x4C2324
static int wmInterfaceInit()
{
    debugPrint("worldmap_ui: interface init start\n");
    wmLastRndTime = getTicks();

    // SFALL: Fix default worldmap font.
    // CE: This setting affects only city names. In Sfall it's configurable via
    // WorldMapFontPatch and is turned off by default.
    wmGenData.oldFont = fontGetCurrent();
    fontSetCurrent(101);

    wmTransitionSaveMap();
    debugPrint("worldmap_ui: map transition saved\n");

    const char* backgroundSoundFileName = gameSoundGetMusicOverride(
        wmGenData.isInCar ? "worldmap_car_music" : "worldmap_music",
        wmGenData.isInCar ? "20car" : "23world");
    _gsound_background_play_level_music(backgroundSoundFileName, GSOUND_LIMIT_AFTER);
    debugPrint("worldmap_ui: background music started\n");

    // CE: Hide entire interface, not just indicator bar, and disable tile
    // engine.
    interfaceBarHide();
    tileDisable();
    isoDisable();
    colorCycleDisable();
    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    // CE: Clear map window.
    windowFill(gIsoWindow,
        0,
        0,
        windowGetWidth(gIsoWindow),
        windowGetHeight(gIsoWindow),
        COLOR_BLACK);
    windowRefresh(gIsoWindow);

    // CE: Stop all animations.
    animationStop();
    debugPrint("worldmap_ui: map view cleared\n");

    int worldmapWindowX = (screenGetWidth() - WM_WINDOW_WIDTH) / 2;
    int worldmapWindowY = (screenGetHeight() - WM_WINDOW_HEIGHT) / 2;
    wmBkWin = windowCreate(worldmapWindowX, worldmapWindowY, WM_WINDOW_WIDTH, WM_WINDOW_HEIGHT, COLOR_BLACK, WINDOW_MOVE_ON_TOP);
    if (wmBkWin == -1) {
        return -1;
    }
    debugPrint("worldmap_ui: modal window created\n");

    if (!_backgroundFrmImage.lock(InterfaceFrameId::WorldMapDialogBox)) {
        return -1;
    }

    wmBkWinBuf = windowGetBuffer(wmBkWin);
    if (wmBkWinBuf == nullptr) {
        return -1;
    }

    // CE: Allocate offscreen buffer for safe city overlay rendering
    wmOverlayOffscreenBuf = (unsigned char*)internal_malloc(WM_OVERLAY_BUFFER_SIZE * WM_OVERLAY_BUFFER_SIZE);
    if (wmOverlayOffscreenBuf == nullptr) {
        return -1;
    }
    debugPrint("worldmap_ui: overlay buffer allocated\n");

    blitBufferToBuffer(_backgroundFrmImage.getData(),
        _backgroundFrmImage.getWidth(),
        _backgroundFrmImage.getHeight(),
        _backgroundFrmImage.getWidth(),
        wmBkWinBuf,
        WM_WINDOW_WIDTH);

    for (CitySize citySize = CITY_SIZE_FIRST; citySize < CITY_SIZE_COUNT; citySize++) {
        CitySizeDescription* citySizeDescription = &(wmSphereData[citySize]);
        if (!citySizeDescription->frmImage.lock(FrmId(citySizeDescription->fid))) {
            return -1;
        }
    }

    if (!wmGenData.hotspotNormalFrmImage.lock(InterfaceFrameId::TownMapHotspot1)) {
        return -1;
    }

    if (!wmGenData.hotspotPressedFrmImage.lock(InterfaceFrameId::TownMapHotspot2)) {
        return -1;
    }

    if (!wmGenData.destinationMarkerFrmImage.lock(InterfaceFrameId::WorldMapMoveTargetMarker1)) {
        return -1;
    }

    if (!wmGenData.locationMarkerFrmImage.lock(InterfaceFrameId::WorldMapLocationMarker)) {
        return -1;
    }

    for (int index = 0; index < WORLD_MAP_ENCOUNTER_FRM_COUNT; index++) {
        if (!wmGenData.encounterCursorFrmImages[index].lock(wmRndCursorFrmIds[index])) {
            return -1;
        }
    }

    for (int index = 0; index < wmMaxTileNum; index++) {
        wmTileInfoList[index].handle = INVALID_CACHE_ENTRY;
    }

    if (!wmGenData.tabsBackgroundFrmImage.lock(InterfaceFrameId::WorldMapTownTabsUnderlay)) {
        return -1;
    }

    if (!wmGenData.tabsBorderFrmImage.lock(InterfaceFrameId::WorldMapTownTabsEdgingOverlay)) {
        return -1;
    }

    wmGenData.dialFrm = artLock(InterfaceFrameId::WorldMapNightDayDial, &(wmGenData.dialFrmHandle));
    if (wmGenData.dialFrm == nullptr) {
        return -1;
    }

    wmGenData.dialFrmWidth = artGetWidth(wmGenData.dialFrm);
    wmGenData.dialFrmHeight = artGetHeight(wmGenData.dialFrm);

    if (!wmGenData.carOverlayFrmImage.lock(InterfaceFrameId::WorldMapOverlayScreen)) {
        return -1;
    }

    if (!wmGenData.globeOverlayFrmImage.lock(InterfaceFrameId::WorldMapGlobeStampOverlay)) {
        return -1;
    }

    wmGenData.redButtonNormalFrmImage.lock(InterfaceFrameId::LittleRedButtonUp);

    wmGenData.redButtonPressedFrmImage.lock(InterfaceFrameId::LittleRedButtonDown);

    if (!wmGenData.monthsFrmImage.lock(InterfaceFrameId::PipBoyMonthStrings)) {
        return -1;
    }

    if (!wmGenData.numbersFrmImage.lock(InterfaceFrameId::HitPointsNumbers)) {
        return -1;
    }
    debugPrint("worldmap_ui: static interface art loaded\n");

    // create town/world switch button
    int switchBtn = buttonCreate(wmBkWin,
        WM_TOWN_WORLD_SWITCH_X,
        WM_TOWN_WORLD_SWITCH_Y,
        wmGenData.redButtonNormalFrmImage.getWidth(),
        wmGenData.redButtonNormalFrmImage.getHeight(),
        -1,
        -1,
        -1,
        KEY_UPPERCASE_T,
        wmGenData.redButtonNormalFrmImage.getData(),
        wmGenData.redButtonPressedFrmImage.getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);

    // SFALL: Add missing button sounds.
    if (switchBtn != -1) {
        buttonSetCallbacks(switchBtn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    for (int index = 0; index < WM_TOWN_LIST_VISIBLE_SLOT_COUNT; index++) {
        wmTownMapSubButtonIds[index] = buttonCreate(wmBkWin,
            508,
            138 + WM_TOWN_LIST_SLOT_HEIGHT * index,
            wmGenData.redButtonNormalFrmImage.getWidth(),
            wmGenData.redButtonNormalFrmImage.getHeight(),
            -1,
            -1,
            -1,
            KEY_CTRL_F1 + index,
            wmGenData.redButtonNormalFrmImage.getData(),
            wmGenData.redButtonPressedFrmImage.getData(),
            nullptr,
            BUTTON_FLAG_TRANSPARENT);

        // SFALL: Add missing button sounds.
        if (wmTownMapSubButtonIds[index] != -1) {
            buttonSetCallbacks(wmTownMapSubButtonIds[index], _gsound_red_butt_press, _gsound_red_butt_release);
        }
    }

    constexpr InterfaceFrmId kScrollUpButtonFrmIds[WORLDMAP_ARROW_FRM_COUNT] = {
        InterfaceFrameId::CharacterEditorUpArrowOff,
        InterfaceFrameId::CharacterEditorUpArrowOn,
    };

    for (int index = 0; index < WORLDMAP_ARROW_FRM_COUNT; index++) {
        if (!wmGenData.scrollUpButtonFrmImages[index].lock(kScrollUpButtonFrmIds[index])) {
            return -1;
        }
    }

    constexpr InterfaceFrmId kScrollDownButtonFrmIds[WORLDMAP_ARROW_FRM_COUNT] = {
        InterfaceFrameId::CharacterEditorDownArrowOff,
        InterfaceFrameId::CharacterEditorDownArrowOn,
    };

    for (int index = 0; index < WORLDMAP_ARROW_FRM_COUNT; index++) {
        if (!wmGenData.scrollDownButtonFrmImages[index].lock(kScrollDownButtonFrmIds[index])) {
            return -1;
        }
    }

    // Scroll up button.
    int scrollUpBtn = buttonCreate(wmBkWin,
        WM_TOWN_LIST_SCROLL_UP_X,
        WM_TOWN_LIST_SCROLL_UP_Y,
        wmGenData.scrollUpButtonFrmImages[WORLDMAP_ARROW_FRM_NORMAL].getWidth(),
        wmGenData.scrollUpButtonFrmImages[WORLDMAP_ARROW_FRM_NORMAL].getHeight(),
        -1,
        -1,
        -1,
        KEY_CTRL_ARROW_UP,
        wmGenData.scrollUpButtonFrmImages[WORLDMAP_ARROW_FRM_NORMAL].getData(),
        wmGenData.scrollUpButtonFrmImages[WORLDMAP_ARROW_FRM_PRESSED].getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);

    // SFALL: Add missing button sounds.
    if (scrollUpBtn != -1) {
        buttonSetCallbacks(scrollUpBtn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    // Scroll down button.
    int scrollDownBtn = buttonCreate(wmBkWin,
        WM_TOWN_LIST_SCROLL_DOWN_X,
        WM_TOWN_LIST_SCROLL_DOWN_Y,
        wmGenData.scrollDownButtonFrmImages[WORLDMAP_ARROW_FRM_NORMAL].getWidth(),
        wmGenData.scrollDownButtonFrmImages[WORLDMAP_ARROW_FRM_NORMAL].getHeight(),
        -1,
        -1,
        -1,
        KEY_CTRL_ARROW_DOWN,
        wmGenData.scrollDownButtonFrmImages[WORLDMAP_ARROW_FRM_NORMAL].getData(),
        wmGenData.scrollDownButtonFrmImages[WORLDMAP_ARROW_FRM_PRESSED].getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);

    // SFALL: Add missing button sounds.
    if (scrollDownBtn != -1) {
        buttonSetCallbacks(scrollDownBtn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    if (wmGenData.isInCar) {
        if (!wmLockCarInterfaceArt(carInterfaceArtFrmId, &(wmGenData.carImageFrm), &(wmGenData.carImageFrmHandle))) {
            carInterfaceArtFrmId = kDefaultCarInterfaceArtFrmId;

            if (!wmLockCarInterfaceArt(carInterfaceArtFrmId, &(wmGenData.carImageFrm), &(wmGenData.carImageFrmHandle))) {
                return -1;
            }
        }

        wmGenData.carImageFrmWidth = artGetWidth(wmGenData.carImageFrm);
        wmGenData.carImageFrmHeight = artGetHeight(wmGenData.carImageFrm);
    }
    debugPrint("worldmap_ui: interface controls created\n");

    tickersAdd(wmMouseBkProc);

    debugPrint("worldmap_ui: building destination tabs areas=%d\n", wmMaxAreaNum);
    if (wmMakeTabsLabelList(&wmLabelList, &wmLabelCount) == -1) {
        return -1;
    }
    debugPrint("worldmap_ui: destination tabs ready count=%d\n", wmLabelCount);

    wmInterfaceWasInitialized = 1;

    debugPrint("worldmap_ui: initial map render start offset=%d,%d\n", wmWorldOffsetX, wmWorldOffsetY);
    if (wmInterfaceRefresh() == -1) {
        return -1;
    }
    debugPrint("worldmap_ui: initial map render complete\n");

    windowRefresh(wmBkWin);
    wmTransitionSuspendScripts();
    debugPrint("worldmap_ui: interface init done\n");

    return 0;
}

// 0x4C2E44
static int wmInterfaceExit()
{
    int i;
    TileInfo* tile;

    tickersRemove(wmMouseBkProc);

    _backgroundFrmImage.unlock();

    if (wmBkWin != -1) {
        windowDestroy(wmBkWin);
        wmBkWin = -1;
    }

    wmGenData.hotspotNormalFrmImage.unlock();
    wmGenData.hotspotPressedFrmImage.unlock();

    wmGenData.destinationMarkerFrmImage.unlock();
    wmGenData.locationMarkerFrmImage.unlock();

    for (i = 0; i < 4; i++) {
        wmGenData.encounterCursorFrmImages[i].unlock();
    }

    for (CitySize citySize = CITY_SIZE_FIRST; citySize < CITY_SIZE_COUNT; citySize++) {
        CitySizeDescription* citySizeDescription = &(wmSphereData[citySize]);
        citySizeDescription->frmImage.unlock();
    }

    for (i = 0; i < wmMaxTileNum; i++) {
        tile = &(wmTileInfoList[i]);
        if (tile->handle != INVALID_CACHE_ENTRY) {
            artUnlock(tile->handle);
            tile->handle = INVALID_CACHE_ENTRY;
            tile->data = nullptr;

            if (tile->walkMaskData != nullptr) {
                internal_free(tile->walkMaskData);
                tile->walkMaskData = nullptr;
            }
        }
    }

    wmGenData.tabsBackgroundFrmImage.unlock();
    wmGenData.tabsBorderFrmImage.unlock();

    if (wmGenData.dialFrm != nullptr) {
        artUnlock(wmGenData.dialFrmHandle);
        wmGenData.dialFrmHandle = INVALID_CACHE_ENTRY;
        wmGenData.dialFrm = nullptr;
    }

    wmGenData.carOverlayFrmImage.unlock();
    wmGenData.globeOverlayFrmImage.unlock();

    wmGenData.redButtonNormalFrmImage.unlock();
    wmGenData.redButtonPressedFrmImage.unlock();

    for (i = 0; i < 2; i++) {
        wmGenData.scrollUpButtonFrmImages[i].unlock();
        wmGenData.scrollDownButtonFrmImages[i].unlock();
    }

    wmGenData.monthsFrmImage.unlock();
    wmGenData.numbersFrmImage.unlock();

    if (wmGenData.carImageFrm != nullptr) {
        artUnlock(wmGenData.carImageFrmHandle);
        wmGenData.carImageFrmHandle = INVALID_CACHE_ENTRY;
        wmGenData.carImageFrm = nullptr;

        wmGenData.carImageFrmWidth = 0;
        wmGenData.carImageFrmHeight = 0;
    }

    wmEncounterStagingClear();

    // CE: Enable tile engine and interface.
    interfaceBarShow();
    tileEnable();
    isoEnable();
    colorCycleEnable();

    fontSetCurrent(wmGenData.oldFont);

    // NOTE: Uninline.
    wmFreeTabsLabelList(&wmLabelList, &wmLabelCount);

    // CE: Free offscreen buffer for safe city overlay rendering
    if (wmOverlayOffscreenBuf != nullptr) {
        internal_free(wmOverlayOffscreenBuf);
        wmOverlayOffscreenBuf = nullptr;
    }

    wmInterfaceWasInitialized = 0;

    wmTransitionResumeScripts();

    return 0;
}

// NOTE: Inlined.
//
// 0x4C31E8
static int wmInterfaceScroll(int dx, int dy, bool* successPtr)
{
    return wmInterfaceScrollPixel(20, 20, dx, dy, successPtr, 1);
}

// FIXME: There is small bug in this function. There is [success] flag returned
// by reference so that calling code can update scrolling mouse cursor to invalid
// range. It works OK on straight directions. But in diagonals when scrolling in
// one direction is possible (and in fact occured), it will still be reported as
// error.
//
// 0x4C3200
int wmInterfaceScrollPixel(int stepX, int stepY, int dx, int dy, bool* success, bool shouldRefresh)
{
    if (success != nullptr) {
        *success = true;
    }

    if (dy < 0) {
        if (wmWorldOffsetY > 0) {
            wmWorldOffsetY -= stepY;
            if (wmWorldOffsetY < 0) {
                wmWorldOffsetY = 0;
            }
        } else {
            if (success != nullptr) {
                *success = false;
            }
        }
    } else if (dy > 0) {
        if (wmWorldOffsetY < wmGenData.viewportMaxY) {
            wmWorldOffsetY += stepY;
            if (wmWorldOffsetY > wmGenData.viewportMaxY) {
                wmWorldOffsetY = wmGenData.viewportMaxY;
            }
        } else {
            if (success != nullptr) {
                *success = false;
            }
        }
    }

    if (dx < 0) {
        if (wmWorldOffsetX > 0) {
            wmWorldOffsetX -= stepX;
            if (wmWorldOffsetX < 0) {
                wmWorldOffsetX = 0;
            }
        } else {
            if (success != nullptr) {
                *success = false;
            }
        }
    } else if (dx > 0) {
        if (wmWorldOffsetX < wmGenData.viewportMaxX) {
            wmWorldOffsetX += stepX;
            if (wmWorldOffsetX > wmGenData.viewportMaxX) {
                wmWorldOffsetX = wmGenData.viewportMaxX;
            }
        } else {
            if (success != nullptr) {
                *success = false;
            }
        }
    }

    if (shouldRefresh) {
        if (wmInterfaceRefresh() == -1) {
            return -1;
        }
    }

    return 0;
}

// 0x4C32EC
static void wmMouseBkProc()
{
    // 0x51DEB0
    static unsigned int lastTime = 0;

    // 0x51DEB4
    static bool couldScroll = true;

    int x;
    int y;
    mouseGetPosition(&x, &y);

    int dx = 0;
    if (x == screenGetWidth() - 1) {
        dx = 1;
    } else if (x == 0) {
        dx = -1;
    }

    int dy = 0;
    if (y == screenGetHeight() - 1) {
        dy = 1;
    } else if (y == 0) {
        dy = -1;
    }

    MouseCursorType oldMouseCursor = gameMouseGetCursor();
    MouseCursorType newMouseCursor = oldMouseCursor;

    if (dx != 0 || dy != 0) {
        if (dx > 0) {
            if (dy > 0) {
                newMouseCursor = MOUSE_CURSOR_SCROLL_SE;
            } else if (dy < 0) {
                newMouseCursor = MOUSE_CURSOR_SCROLL_NE;
            } else {
                newMouseCursor = MOUSE_CURSOR_SCROLL_E;
            }
        } else if (dx < 0) {
            if (dy > 0) {
                newMouseCursor = MOUSE_CURSOR_SCROLL_SW;
            } else if (dy < 0) {
                newMouseCursor = MOUSE_CURSOR_SCROLL_NW;
            } else {
                newMouseCursor = MOUSE_CURSOR_SCROLL_W;
            }
        } else {
            if (dy < 0) {
                newMouseCursor = MOUSE_CURSOR_SCROLL_N;
            } else if (dy > 0) {
                newMouseCursor = MOUSE_CURSOR_SCROLL_S;
            }
        }

        unsigned int tick = _get_bk_time();
        if (getTicksBetween(tick, lastTime) > 50) {
            lastTime = _get_bk_time();
            // NOTE: Uninline.
            wmInterfaceScroll(dx, dy, &couldScroll);
        }

        if (!couldScroll) {
            newMouseCursor = newMouseCursor + MOUSE_CURSOR_SCROLL_OFFSET_INVALID;
        }
    } else {
        if (oldMouseCursor != MOUSE_CURSOR_ARROW) {
            newMouseCursor = MOUSE_CURSOR_ARROW;
        }
    }

    if (oldMouseCursor != newMouseCursor) {
        gameMouseSetCursor(newMouseCursor);
    }
}

// Load tile art if needed.
//
// 0x4C37EC
static int wmTileGrabArt(int tileIdx)
{
    TileInfo* tile = &(wmTileInfoList[tileIdx]);
    if (tile->data != nullptr) {
        return 0;
    }

    Art* art = artLock(FrmId(tile->fid), &(tile->handle));
    if (art != nullptr) {
        tile->data = artGetFrameData(art, 0, ROTATION_NE);
        if (tile->data != nullptr) {
            return 0;
        }
    }

    wmInterfaceExit();

    return -1;
}

// 0x4C3830
int wmInterfaceRefresh()
{
    if (wmInterfaceWasInitialized != 1) {
        return 0;
    }

    int v17 = wmWorldOffsetX % WM_TILE_WIDTH;
    int v18 = wmWorldOffsetY % WM_TILE_HEIGHT;
    int v20 = WM_TILE_HEIGHT - v18;
    int v21 = WM_TILE_WIDTH * v18;
    int v19 = WM_TILE_WIDTH - v17;

    // Render tiles.
    int y = 0;
    int x = 0;
    int v0 = wmWorldOffsetY / WM_TILE_HEIGHT * wmNumHorizontalTiles + wmWorldOffsetX / WM_TILE_WIDTH % wmNumHorizontalTiles;
    while (y < WM_VIEW_HEIGHT) {
        x = 0;
        int v23 = 0;
        int height = WM_TILE_HEIGHT;
        while (x < WM_VIEW_WIDTH) {
            if (wmTileGrabArt(v0) == -1) {
                return -1;
            }

            int width = WM_TILE_WIDTH;

            int srcX = 0;
            if (x == 0) {
                srcX = v17;
                width = v19;
            }

            if (width + x > WM_VIEW_WIDTH) {
                width = WM_VIEW_WIDTH - x;
            }

            height = WM_TILE_HEIGHT;
            if (y == 0) {
                height = v20;
                srcX += v21;
            }

            if (height + y > WM_VIEW_HEIGHT) {
                height = WM_VIEW_HEIGHT - y;
            }

            TileInfo* tileInfo = &(wmTileInfoList[v0]);
            blitBufferToBuffer(tileInfo->data + srcX,
                width,
                height,
                WM_TILE_WIDTH,
                wmBkWinBuf + WM_WINDOW_WIDTH * (y + WM_VIEW_Y) + WM_VIEW_X + x,
                WM_WINDOW_WIDTH);
            v0++;

            x += width;
            v23++;
        }

        v0 += wmNumHorizontalTiles - v23;
        y += height;
    }

    // Render cities.
    for (int index = 0; index < wmMaxAreaNum; index++) {
        CityInfo* cityInfo = &(wmAreaInfoList[index]);
        if (cityInfo->state != CITY_STATE_UNKNOWN) {
            CitySizeDescription* citySizeDescription = &(wmSphereData[cityInfo->size]);
            int cityX = cityInfo->x - wmWorldOffsetX;
            int cityY = cityInfo->y - wmWorldOffsetY;
            // CE: Use safe overlay drawing with proper bounds checking instead of hardcoded limits
            wmInterfaceDrawCircleOverlaySafe(cityInfo, citySizeDescription, wmBkWinBuf, cityX, cityY);
        }
    }

    // Hide unknown subtiles, dim unvisited.
    int v25 = wmWorldOffsetX / WM_TILE_WIDTH % wmNumHorizontalTiles + wmWorldOffsetY / WM_TILE_HEIGHT * wmNumHorizontalTiles;
    int v30 = 0;
    while (v30 < WM_VIEW_HEIGHT) {
        int v24 = 0;
        int v33 = 0;
        int v29 = WM_TILE_HEIGHT;
        while (v33 < WM_VIEW_WIDTH) {
            int v31 = WM_TILE_WIDTH;
            if (v33 == 0) {
                v31 = WM_TILE_WIDTH - v17;
            }

            if (v33 + v31 > WM_VIEW_WIDTH) {
                v31 = WM_VIEW_WIDTH - v33;
            }

            v29 = WM_TILE_HEIGHT;
            if (v30 == 0) {
                v29 -= v18;
            }

            if (v30 + v29 > WM_VIEW_HEIGHT) {
                v29 = WM_VIEW_HEIGHT - v30;
            }

            int v32;
            if (v30 != 0) {
                v32 = WM_VIEW_Y;
            } else {
                v32 = WM_VIEW_Y - v18;
            }

            int v13 = 0;
            int v34 = v30 + v32;

            for (int row = 0; row < SUBTILE_GRID_HEIGHT; row++) {
                int v35;
                if (v33 != 0) {
                    v35 = WM_VIEW_X;
                } else {
                    v35 = WM_VIEW_X - v17;
                }

                int v15 = v33 + v35;
                for (int column = 0; column < SUBTILE_GRID_WIDTH; column++) {
                    TileInfo* tileInfo = &(wmTileInfoList[v25]);
                    wmInterfaceDrawSubTileList(tileInfo, column, row, v15, v34, 1);

                    v15 += WM_SUBTILE_SIZE;
                    v35 += WM_SUBTILE_SIZE;
                }

                v32 += WM_SUBTILE_SIZE;
                v34 += WM_SUBTILE_SIZE;
            }

            v25++;
            v24++;
            v33 += v31;
        }

        v25 += wmNumHorizontalTiles - v24;
        v30 += v29;
    }

    wmDrawCursorStopped();

    wmRefreshInterfaceOverlay(true);

    return 0;
}

// 0x4C3C9C
void wmInterfaceRefreshDate(bool shouldRefreshWindow)
{
    // CE: Headless guard — the core travel tick (worldmapTravelClockTick,
    // ledger H-13) advances the game clock through wmGameTimeIncrement, which
    // refreshes the date display. Without the worldmap window there is
    // nothing to draw into. Inert in UI runs: the window buffer always exists
    // while the worldmap loop is running.
    if (wmBkWinBuf == nullptr) {
        return;
    }

    int month;
    int day;
    int year;
    gameTimeGetDate(&month, &day, &year);

    month--;

    unsigned char* dest = wmBkWinBuf;

    int numbersFrmWidth = wmGenData.numbersFrmImage.getWidth();
    int numbersFrmHeight = wmGenData.numbersFrmImage.getHeight();
    unsigned char* numbersFrmData = wmGenData.numbersFrmImage.getData();

    dest += WM_WINDOW_WIDTH * 12 + 487;
    blitBufferToBuffer(numbersFrmData + 9 * (day / 10), 9, numbersFrmHeight, numbersFrmWidth, dest, WM_WINDOW_WIDTH);
    blitBufferToBuffer(numbersFrmData + 9 * (day % 10), 9, numbersFrmHeight, numbersFrmWidth, dest + 9, WM_WINDOW_WIDTH);

    int monthsFrmWidth = wmGenData.monthsFrmImage.getWidth();
    unsigned char* monthsFrmData = wmGenData.monthsFrmImage.getData();
    blitBufferToBuffer(monthsFrmData + monthsFrmWidth * 15 * month, 29, 14, 29, dest + WM_WINDOW_WIDTH + 26, WM_WINDOW_WIDTH);

    dest += 98;
    for (int index = 0; index < 4; index++) {
        dest -= 9;
        blitBufferToBuffer(numbersFrmData + 9 * (year % 10), 9, numbersFrmHeight, numbersFrmWidth, dest, WM_WINDOW_WIDTH);
        year /= 10;
    }

    int gameTimeHour = gameTimeGetHour();
    dest += 72;
    for (int index = 0; index < 4; index++) {
        blitBufferToBuffer(numbersFrmData + 9 * (gameTimeHour % 10), 9, numbersFrmHeight, numbersFrmWidth, dest, WM_WINDOW_WIDTH);
        dest -= 9;
        gameTimeHour /= 10;
    }

    if (shouldRefreshWindow) {
        Rect rect;
        rect.left = 487;
        rect.top = 12;
        rect.bottom = numbersFrmHeight + 12;
        rect.right = 630;
        windowRefreshRect(wmBkWin, &rect);
    }
}

// 0x4C3FA8
static int wmInterfaceDrawCircleOverlay(CityInfo* city, CitySizeDescription* citySizeDescription, unsigned char* dest, int x, int y)
{
    _dark_translucent_trans_buf_to_buf(citySizeDescription->frmImage.getData(),
        citySizeDescription->frmImage.getWidth(),
        citySizeDescription->frmImage.getHeight(),
        citySizeDescription->frmImage.getWidth(),
        dest,
        x,
        y,
        WM_WINDOW_WIDTH,
        0x10000,
        circleBlendTable,
        _commonGrayTable);

    // CE: Slightly increase whitespace between cirle and city name.
    int nameY = y + citySizeDescription->frmImage.getHeight() + 3;
    int maxY = 464 - fontGetLineHeight();
    if (!wmTownNamesHidden && nameY < maxY) {
        MessageListItem messageListItem;
        char name[40];
        if (wmAreaIsKnown(city->areaId)) {
            // NOTE: Uninline.
            wmGetAreaName(city, name);
        } else {
            strncpy(name, getmsg(&wmMsgFile, &messageListItem, 1004), 40);
        }

        int width = fontGetStringWidth(name);
        fontDrawText(dest + WM_WINDOW_WIDTH * nameY + x + citySizeDescription->frmImage.getWidth() / 2 - width / 2,
            name,
            width,
            WM_WINDOW_WIDTH,
            COLOR_GREEN | DRAW_TEXT_FLAG_SHADOWED);
    }

    return 0;
}

// Helper function that dims specified rectangle in given buffer. It's used to
// slightly darken subtile which is known, but not visited.
//
// 0x4C40A8
static void wmInterfaceDrawSubTileRectFogged(unsigned char* dest, int width, int height, int pitch)
{
    int skipY = pitch - width;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned char color = *dest;
            *dest++ = intensityColorTable[color][75];
        }
        dest += skipY;
    }
}

// 0x4C40E4
static int wmInterfaceDrawSubTileList(TileInfo* tileInfo, int column, int row, int x, int y, int a6)
{
    SubtileInfo* subtileInfo = &(tileInfo->subtiles[row][column]);

    int destY = y;
    int destX = x;

    int height = WM_SUBTILE_SIZE;
    if (y < WM_VIEW_Y) {
        if (y < 0) {
            height = y + 29;
        } else {
            height = WM_SUBTILE_SIZE - (WM_VIEW_Y - y);
        }
        destY = WM_VIEW_Y;
    }

    if (height + y > WM_VIEW_Y + WM_VIEW_HEIGHT) {
        height -= height + y - (WM_VIEW_Y + WM_VIEW_HEIGHT);
    }

    int width = WM_SUBTILE_SIZE * a6;
    if (x < WM_VIEW_X) {
        destX = WM_VIEW_X;
        width -= WM_VIEW_X - x;
    }

    if (width + x > WM_VIEW_X + WM_VIEW_WIDTH) {
        width -= width + x - (WM_VIEW_X + WM_VIEW_WIDTH);
    }

    if (width > 0 && height > 0) {
        unsigned char* dest = wmBkWinBuf + WM_WINDOW_WIDTH * destY + destX;
        switch (subtileInfo->state) {
        case SUBTILE_STATE_UNKNOWN:
            bufferFill(dest, width, height, WM_WINDOW_WIDTH, COLOR_BLACK);
            break;
        case SUBTILE_STATE_KNOWN:
            wmInterfaceDrawSubTileRectFogged(dest, width, height, WM_WINDOW_WIDTH);
            break;
        default:
            break;
        }
    }

    return 0;
}

// 0x4C41EC
static int wmDrawCursorStopped()
{
    unsigned char* src;
    int width;
    int height;

    bool isWalkingNow = wmGenData.walkDestinationX > 0 || wmGenData.walkDestinationY > 0;

    if (isWalkingNow) {
        // moving cursor
        if (wmGenData.encounterIconIsVisible) {
            src = wmGenData.encounterCursorFrmImages[wmGenData.encounterCursorId].getData();
            width = wmGenData.encounterCursorFrmImages[wmGenData.encounterCursorId].getWidth();
            height = wmGenData.encounterCursorFrmImages[wmGenData.encounterCursorId].getHeight();
        } else {
            // current location (+)
            src = wmGenData.locationMarkerFrmImage.getData();
            width = wmGenData.locationMarkerFrmImage.getWidth();
            height = wmGenData.locationMarkerFrmImage.getHeight();
        }

        if (wmGenData.worldPosX >= wmWorldOffsetX && wmGenData.worldPosX < wmWorldOffsetX + WM_VIEW_WIDTH
            && wmGenData.worldPosY >= wmWorldOffsetY && wmGenData.worldPosY < wmWorldOffsetY + WM_VIEW_HEIGHT) {
            blitBufferToBufferTrans(src, width, height, width, wmBkWinBuf + WM_WINDOW_WIDTH * (WM_VIEW_Y - wmWorldOffsetY + wmGenData.worldPosY - height / 2) + WM_VIEW_X - wmWorldOffsetX + wmGenData.worldPosX - width / 2, WM_WINDOW_WIDTH);
        }

        if (wmGenData.walkDestinationX >= wmWorldOffsetX && wmGenData.walkDestinationX < wmWorldOffsetX + WM_VIEW_WIDTH
            && wmGenData.walkDestinationY >= wmWorldOffsetY && wmGenData.walkDestinationY < wmWorldOffsetY + WM_VIEW_HEIGHT) {
            blitBufferToBufferTrans(wmGenData.destinationMarkerFrmImage.getData(),
                wmGenData.destinationMarkerFrmImage.getWidth(),
                wmGenData.destinationMarkerFrmImage.getHeight(),
                wmGenData.destinationMarkerFrmImage.getWidth(),
                wmBkWinBuf + WM_WINDOW_WIDTH * (WM_VIEW_Y - wmWorldOffsetY + wmGenData.walkDestinationY - wmGenData.destinationMarkerFrmImage.getHeight() / 2) + WM_VIEW_X - wmWorldOffsetX + wmGenData.walkDestinationX - wmGenData.destinationMarkerFrmImage.getWidth() / 2,
                WM_WINDOW_WIDTH);
        }
    } else {
        if (wmGenData.encounterIconIsVisible) {
            src = wmGenData.encounterCursorFrmImages[wmGenData.encounterCursorId].getData();
            width = wmGenData.encounterCursorFrmImages[wmGenData.encounterCursorId].getWidth();
            height = wmGenData.encounterCursorFrmImages[wmGenData.encounterCursorId].getHeight();
        } else {
            src = gMousePressed ? wmGenData.hotspotPressedFrmImage.getData() : wmGenData.hotspotNormalFrmImage.getData();
            width = wmGenData.hotspotNormalFrmImage.getWidth();
            height = wmGenData.hotspotNormalFrmImage.getHeight();
        }

        if (wmGenData.worldPosX >= wmWorldOffsetX && wmGenData.worldPosX < wmWorldOffsetX + WM_VIEW_WIDTH
            && wmGenData.worldPosY >= wmWorldOffsetY && wmGenData.worldPosY < wmWorldOffsetY + WM_VIEW_HEIGHT) {
            blitBufferToBufferTrans(src, width, height, width, wmBkWinBuf + WM_WINDOW_WIDTH * (WM_VIEW_Y - wmWorldOffsetY + wmGenData.worldPosY - height / 2) + WM_VIEW_X - wmWorldOffsetX + wmGenData.worldPosX - width / 2, WM_WINDOW_WIDTH);
        }
    }

    // Dotted Trail logic

    if (worldmapTrailMarkers) {
        // Clear the trail when player stops - needs to be done when reloading map too
        if (!isWalkingNow) {
            wmResetTrailMarkers();
        }

        // Render the trail dots
        for (int i = 0; i < trailMarkerState.dotCount; i++) {
            int x = trailMarkerState.dots[i].x;
            int y = trailMarkerState.dots[i].y;
            if (x >= wmWorldOffsetX && x < wmWorldOffsetX + WM_VIEW_WIDTH
                && y >= wmWorldOffsetY && y < wmWorldOffsetY + WM_VIEW_HEIGHT) {
                int screenY = WM_VIEW_Y - wmWorldOffsetY + y;
                int screenX = WM_VIEW_X - wmWorldOffsetX + x;
                unsigned char* dst = wmBkWinBuf
                    + WM_WINDOW_WIDTH * screenY
                    + screenX;
                *dst = worldmapTrailMarkerColor;
            }
        }
    }

    wmInterfaceDrawTerrainInfo();

    return 0;
}

// 0x4C4490
bool wmCursorIsVisible()
{
    return wmGenData.worldPosX >= wmWorldOffsetX
        && wmGenData.worldPosY >= wmWorldOffsetY
        && wmGenData.worldPosX < wmWorldOffsetX + WM_VIEW_WIDTH
        && wmGenData.worldPosY < wmWorldOffsetY + WM_VIEW_HEIGHT;
}

// 0x4C4850
void wmTownMap()
{
    wmWorldMapFunc(1);
}

// 0x4C485C
// `entranceIdxPtr` (optional) reports WHICH entrance was picked, not just where it
// leads. A viewer needs the index rather than the map: it sends the index upstream and
// the server resolves the destination from its own table.
// ►► THE TOWN MAP, FOR A VIEWER. Streaming used to send an unconditional "enter", so a
// co-op player always landed on a city's FIRST map — no districts, no named gates,
// anywhere in the game. The picker is city ART plus buttons, which is ours to draw and
// the server has none of, so we run vanilla's own screen and send back the ENTRANCE
// INDEX; the server resolves what that means (never a map index off the wire).
//
// Returns true if something was sent upstream. Escaping the picker sends NOTHING — the
// player changed their mind, exactly as cancelling an elevator never rides it. A city
// whose layout we have not seen has nothing to pick from, so it falls back to the plain
// enter, which is what the server answers with its front door.
static bool wmViewerEnterCurrentArea()
{
    if (wmGenData.currentAreaId == -1) {
        clientViewerWmEnter();
        return true;
    }

    CityInfo* city = &(wmAreaInfoList[wmGenData.currentAreaId]);
    if (city->visitedState != VisitedState::Visited || city->mapFid == -1) {
        clientViewerWmEnter();
        return true;
    }

    Map townMap = MAP_INVALID;
    int entranceIndex = -1;
    if (wmTownMapFunc(&townMap, &entranceIndex) == -1) {
        return false; // the screen itself failed; do not guess a destination
    }

    if (entranceIndex < 0) {
        return false; // escaped — no travel, no message
    }

    clientViewerWmEnterEntrance(entranceIndex);
    return true;
}

static int wmTownMapFunc(Map* mapIdxPtr, int* entranceIdxPtr)
{
    *mapIdxPtr = MAP_INVALID;
    if (entranceIdxPtr != nullptr) *entranceIdxPtr = -1;

    if (wmTownMapInit() == -1) {
        wmTownMapExit();
        return -1;
    }

    if (wmGenData.currentAreaId == CITY_INVALID) {
        return -1;
    }

    CityInfo* city = &(wmAreaInfoList[wmGenData.currentAreaId]);

    for (;;) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
            showQuitConfirmationDialog();
        }

        if (_game_user_wants_to_quit) {
            break;
        }

        if (keyCode != -1) {
            if (keyCode == KEY_ESCAPE) {
                break;
            }

            if (keyCode >= KEY_1 && keyCode < KEY_1 + city->entrancesLength) {
                EntranceInfo* entrance = &(city->entrances[keyCode - KEY_1]);

                // SFALL: Prevent using number keys to enter unvisited areas on
                // a town map.
                if (gTownMapHotkeysFix) {
                    if (entrance->state == 0 || entrance->x == -1 || entrance->y == -1) {
                        continue;
                    }
                }

                *mapIdxPtr = entrance->map;
                if (entranceIdxPtr != nullptr) {
                    *entranceIdxPtr = keyCode - KEY_1;
                }

                mapSetEnteringLocation(entrance->elevation, entrance->tile, entrance->rotation);

                break;
            }

            if (keyCode >= KEY_CTRL_F1 && keyCode <= KEY_CTRL_F7) {
                int quickDestinationIndex = wmGenData.tabsOffsetY / WM_TOWN_LIST_SLOT_HEIGHT + keyCode - KEY_CTRL_F1;
                if (quickDestinationIndex < wmLabelCount) {
                    City areaIdx = static_cast<City>(wmLabelList[quickDestinationIndex]);
                    CityInfo* city = &(wmAreaInfoList[areaIdx]);
                    if (!wmAreaIsKnown(city->areaId)) {
                        break;
                    }

                    if (areaIdx != wmGenData.currentAreaId) {
                        int destX;
                        int destY;
                        wmAreaGetMarkWorldPos(city, &destX, &destY);
                        wmPartyInitWalking(destX, destY);

                        gMousePressed = false;

                        break;
                    }
                }
            } else {
                if (keyCode == KEY_CTRL_ARROW_UP) {
                    wmInterfaceScrollTabsStart(-WM_TOWN_LIST_SLOT_HEIGHT);
                } else if (keyCode == KEY_CTRL_ARROW_DOWN) {
                    wmInterfaceScrollTabsStart(WM_TOWN_LIST_SLOT_HEIGHT);
                } else if (keyCode == 2069) {
                    if (wmTownMapRefresh() == -1) {
                        return -1;
                    }
                }

                if (keyCode == KEY_UPPERCASE_T || keyCode == KEY_LOWERCASE_T || keyCode == KEY_UPPERCASE_W || keyCode == KEY_LOWERCASE_W) {
                    keyCode = KEY_ESCAPE;
                }

                if (keyCode == KEY_ESCAPE) {
                    break;
                }
            }
        }

        renderFpsCounter();
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (wmTownMapExit() == -1) {
        return -1;
    }

    return 0;
}

// 0x4C4A6C
static int wmTownMapInit()
{
    wmTownMapCurArea = wmGenData.currentAreaId;

    CityInfo* city = &(wmAreaInfoList[wmGenData.currentAreaId]);

    if (!_townFrmImage.lock(FrmId(city->mapFid))) {
        return -1;
    }

    for (int index = 0; index < city->entrancesLength; index++) {
        wmTownMapButtonId[index] = -1;
    }

    for (int index = 0; index < city->entrancesLength; index++) {
        EntranceInfo* entrance = &(city->entrances[index]);
        if (entrance->state == 0) {
            continue;
        }

        if (entrance->x == -1 || entrance->y == -1) {
            continue;
        }

        wmTownMapButtonId[index] = buttonCreate(wmBkWin,
            entrance->x,
            entrance->y,
            wmGenData.hotspotNormalFrmImage.getWidth(),
            wmGenData.hotspotNormalFrmImage.getHeight(),
            -1,
            2069,
            -1,
            KEY_1 + index,
            wmGenData.hotspotNormalFrmImage.getData(),
            wmGenData.hotspotPressedFrmImage.getData(),
            nullptr,
            BUTTON_FLAG_TRANSPARENT);

        if (wmTownMapButtonId[index] == -1) {
            return -1;
        }
    }

    tickersRemove(wmMouseBkProc);

    if (wmTownMapRefresh() == -1) {
        return -1;
    }

    return 0;
}

// 0x4C4BD0
static int wmTownMapRefresh()
{
    blitBufferToBuffer(_townFrmImage.getData(),
        std::min(_townFrmImage.getWidth(), WM_VIEW_WIDTH),
        std::min(_townFrmImage.getHeight(), WM_VIEW_HEIGHT),
        _townFrmImage.getWidth(),
        wmBkWinBuf + WM_WINDOW_WIDTH * WM_VIEW_Y + WM_VIEW_X,
        WM_WINDOW_WIDTH);

    wmRefreshInterfaceOverlay(false);

    CityInfo* city = &(wmAreaInfoList[wmGenData.currentAreaId]);

    for (int index = 0; index < city->entrancesLength; index++) {
        EntranceInfo* entrance = &(city->entrances[index]);
        if (entrance->state == 0) {
            continue;
        }

        if (entrance->x == -1 || entrance->y == -1) {
            continue;
        }

        MessageListItem messageListItem;
        messageListItem.num = 200 + 10 * wmTownMapCurArea + index;
        if (messageListGetItem(&wmMsgFile, &messageListItem)) {
            if (messageListItem.text != nullptr) {
                int width = fontGetStringWidth(messageListItem.text);
                // CE: Slightly increase whitespace between marker and entrance name.
                windowDrawText(wmBkWin,
                    messageListItem.text,
                    width,
                    wmGenData.hotspotNormalFrmImage.getWidth() / 2 + entrance->x - width / 2,
                    wmGenData.hotspotNormalFrmImage.getHeight() + entrance->y + 4,
                    COLOR_GREEN | DRAW_TEXT_FLAG_NO_BG | DRAW_TEXT_FLAG_SHADOWED);
            }
        }
    }

    windowRefresh(wmBkWin);

    return 0;
}

// 0x4C4D00
static int wmTownMapExit()
{
    _townFrmImage.unlock();

    if (wmTownMapCurArea != -1) {
        CityInfo* city = &(wmAreaInfoList[wmTownMapCurArea]);
        for (int index = 0; index < city->entrancesLength; index++) {
            if (wmTownMapButtonId[index] != -1) {
                buttonDestroy(wmTownMapButtonId[index]);
                wmTownMapButtonId[index] = -1;
            }
        }
    }

    if (wmInterfaceRefresh() == -1) {
        return -1;
    }

    tickersAdd(wmMouseBkProc);

    return 0;
}

// 0x4C50F4
int wmRefreshInterfaceOverlay(bool shouldRefreshWindow)
{
    blitBufferToBufferTrans(_backgroundFrmImage.getData(),
        _backgroundFrmImage.getWidth(),
        _backgroundFrmImage.getHeight(),
        _backgroundFrmImage.getWidth(),
        wmBkWinBuf,
        WM_WINDOW_WIDTH);

    wmRefreshTabs();

    // NOTE: Uninline.
    wmInterfaceDialSyncTime(false);

    wmRefreshInterfaceDial(false);

    if (wmGenData.isInCar) {
        unsigned char* data = artGetFrameData(wmGenData.carImageFrm, wmGenData.carImageCurrentFrameIndex);
        if (data == nullptr) {
            return -1;
        }

        blitBufferToBuffer(data,
            wmGenData.carImageFrmWidth,
            wmGenData.carImageFrmHeight,
            wmGenData.carImageFrmWidth,
            wmBkWinBuf + WM_WINDOW_WIDTH * WM_WINDOW_CAR_Y + WM_WINDOW_CAR_X,
            WM_WINDOW_WIDTH);

        blitBufferToBufferTrans(wmGenData.carOverlayFrmImage.getData(),
            wmGenData.carOverlayFrmImage.getWidth(),
            wmGenData.carOverlayFrmImage.getHeight(),
            wmGenData.carOverlayFrmImage.getWidth(),
            wmBkWinBuf + WM_WINDOW_WIDTH * WM_WINDOW_CAR_OVERLAY_Y + WM_WINDOW_CAR_OVERLAY_X,
            WM_WINDOW_WIDTH);

        wmInterfaceRefreshCarFuel();
    } else {
        blitBufferToBufferTrans(wmGenData.globeOverlayFrmImage.getData(),
            wmGenData.globeOverlayFrmImage.getWidth(),
            wmGenData.globeOverlayFrmImage.getHeight(),
            wmGenData.globeOverlayFrmImage.getWidth(),
            wmBkWinBuf + WM_WINDOW_WIDTH * WM_WINDOW_GLOBE_OVERLAY_Y + WM_WINDOW_GLOBE_OVERLAY_X,
            WM_WINDOW_WIDTH);
    }

    wmInterfaceRefreshDate(false);

    if (shouldRefreshWindow) {
        windowRefresh(wmBkWin);
    }

    return 0;
}

// 0x4C5244
static void wmInterfaceRefreshCarFuel()
{
    int ratio = (WM_WINDOW_CAR_FUEL_BAR_HEIGHT * wmGenData.carFuel) / CAR_FUEL_MAX;
    if ((ratio & 1) != 0) {
        ratio -= 1;
    }

    unsigned char* dest = wmBkWinBuf + WM_WINDOW_WIDTH * WM_WINDOW_CAR_FUEL_BAR_Y + WM_WINDOW_CAR_FUEL_BAR_X;

    for (int index = WM_WINDOW_CAR_FUEL_BAR_HEIGHT; index > ratio; index--) {
        *dest = 14;
        dest += 640;
    }

    while (ratio > 0) {
        *dest = 196;
        dest += WM_WINDOW_WIDTH;

        *dest = 14;
        dest += WM_WINDOW_WIDTH;

        ratio -= 2;
    }
}

// 0x4C52B0
static int wmRefreshTabs()
{
    unsigned char* firstTabBottomDest;
    unsigned char* firstTabDest;
    int firstVisibleLabelIndex;
    CityInfo* city;
    int firstTabVisibleHeight;
    unsigned char* firstTabSrc;
    unsigned char* firstTabClampedDest;
    int lastLabelIndexToDraw;
    unsigned char* nextTabDest;
    FrmImage labelFrm;

    // CE: Skip first empty tab (original code does this in the
    // `wmInterfaceInit`).
    unsigned char* src = wmGenData.tabsBackgroundFrmImage.getData() + wmGenData.tabsBackgroundFrmImage.getWidth() * WM_TOWN_LIST_SLOT_HEIGHT;
    blitBufferToBufferTrans(src + wmGenData.tabsBackgroundFrmImage.getWidth() * wmGenData.tabsOffsetY + 9,
        WM_TOWN_LIST_WIDTH,
        WM_TOWN_LIST_HEIGHT,
        wmGenData.tabsBackgroundFrmImage.getWidth(),
        wmBkWinBuf + WM_WINDOW_WIDTH * WM_TOWN_LIST_Y + WM_TOWN_LIST_X,
        WM_WINDOW_WIDTH);

    int tabsOffsetWithinSlot = wmGenData.tabsOffsetY % WM_TOWN_LIST_SLOT_HEIGHT;
    firstTabBottomDest = wmBkWinBuf + WM_WINDOW_WIDTH * 138 + 530;
    firstTabDest = firstTabBottomDest - WM_WINDOW_WIDTH * tabsOffsetWithinSlot;
    firstVisibleLabelIndex = wmGenData.tabsOffsetY / WM_TOWN_LIST_SLOT_HEIGHT;

    if (firstVisibleLabelIndex < wmLabelCount) {
        city = &(wmAreaInfoList[wmLabelList[firstVisibleLabelIndex]]);
        const FrmId cityLabelFrmId = FrmId(city->labelFid);
        if (cityLabelFrmId.valid()) {
            if (!labelFrm.lock(cityLabelFrmId)) {
                return -1;
            }

            firstTabVisibleHeight = labelFrm.getHeight() - tabsOffsetWithinSlot;
            firstTabSrc = labelFrm.getData() + labelFrm.getWidth() * tabsOffsetWithinSlot;

            firstTabClampedDest = firstTabDest;
            if (firstTabDest < firstTabBottomDest - WM_WINDOW_WIDTH) {
                firstTabClampedDest = firstTabBottomDest - WM_WINDOW_WIDTH;
            }

            blitBufferToBuffer(firstTabSrc,
                labelFrm.getWidth(),
                firstTabVisibleHeight,
                labelFrm.getWidth(),
                firstTabClampedDest,
                WM_WINDOW_WIDTH);

            labelFrm.unlock();
        }
    }

    nextTabDest = firstTabDest + WM_WINDOW_WIDTH * WM_TOWN_LIST_SLOT_HEIGHT;
    lastLabelIndexToDraw = firstVisibleLabelIndex + (WM_TOWN_LIST_VISIBLE_SLOT_COUNT - 1);

    for (int labelIndex = firstVisibleLabelIndex + 1; labelIndex < lastLabelIndexToDraw; labelIndex++) {
        if (labelIndex < wmLabelCount) {
            city = &(wmAreaInfoList[wmLabelList[labelIndex]]);
            const FrmId cityLabelFrmId = FrmId(city->labelFid);
            if (cityLabelFrmId.valid()) {
                if (!labelFrm.lock(cityLabelFrmId)) {
                    return -1;
                }

                blitBufferToBuffer(labelFrm.getData(),
                    labelFrm.getWidth(),
                    labelFrm.getHeight(),
                    labelFrm.getWidth(),
                    nextTabDest,
                    WM_WINDOW_WIDTH);

                labelFrm.unlock();
            }
        }
        nextTabDest += WM_WINDOW_WIDTH * WM_TOWN_LIST_SLOT_HEIGHT;
    }

    if (lastLabelIndexToDraw < wmLabelCount) {
        city = &(wmAreaInfoList[wmLabelList[lastLabelIndexToDraw]]);
        const FrmId cityLabelFrmId = FrmId(city->labelFid);
        if (cityLabelFrmId.valid()) {
            if (!labelFrm.lock(cityLabelFrmId)) {
                return -1;
            }

            blitBufferToBuffer(labelFrm.getData(),
                labelFrm.getWidth(),
                labelFrm.getHeight() - 5,
                labelFrm.getWidth(),
                nextTabDest,
                WM_WINDOW_WIDTH);

            labelFrm.unlock();
        }
    }

    blitBufferToBufferTrans(wmGenData.tabsBorderFrmImage.getData(),
        WM_TOWN_LIST_WIDTH,
        WM_TOWN_LIST_HEIGHT,
        WM_TOWN_LIST_WIDTH,
        wmBkWinBuf + WM_WINDOW_WIDTH * WM_TOWN_LIST_Y + WM_TOWN_LIST_X,
        WM_WINDOW_WIDTH);

    return 0;
}

// Creates array of cities available as quick destinations.
//
// 0x4C55D4
static int wmMakeTabsLabelList(int** quickDestinationsPtr, int* quickDestinationsLengthPtr)
{
    int* quickDestinations = *quickDestinationsPtr;

    // NOTE: Uninline.
    wmFreeTabsLabelList(quickDestinationsPtr, quickDestinationsLengthPtr);

    int capacity = 10;

    quickDestinations = (int*)internal_malloc(sizeof(*quickDestinations) * capacity);
    *quickDestinationsPtr = quickDestinations;

    if (quickDestinations == nullptr) {
        return -1;
    }

    int quickDestinationsLength = *quickDestinationsLengthPtr;
    for (City index = CITY_FIRST; index < wmMaxAreaNum; index++) {
        if (wmAreaIsKnown(index) && wmAreaInfoList[index].labelFid != -1) {
            quickDestinationsLength++;
            *quickDestinationsLengthPtr = quickDestinationsLength;

            if (capacity <= quickDestinationsLength) {
                capacity += 10;

                quickDestinations = (int*)internal_realloc(quickDestinations, sizeof(*quickDestinations) * capacity);
                if (quickDestinations == nullptr) {
                    return -1;
                }

                *quickDestinationsPtr = quickDestinations;
            }

            quickDestinations[quickDestinationsLength - 1] = index;
        }
    }

    qsort(quickDestinations, quickDestinationsLength, sizeof(*quickDestinations), wmTabsCompareNames);

    return 0;
}

// 0x4C56C8
static int wmTabsCompareNames(const void* a1, const void* a2)
{
    int index1 = *(int*)a1;
    int index2 = *(int*)a2;

    CityInfo* city1 = &(wmAreaInfoList[index1]);
    CityInfo* city2 = &(wmAreaInfoList[index2]);

    return compat_stricmp(city1->name, city2->name);
}

// NOTE: Inlined.
//
// 0x4C5710
static int wmFreeTabsLabelList(int** quickDestinationsListPtr, int* quickDestinationsLengthPtr)
{
    if (*quickDestinationsListPtr != nullptr) {
        internal_free(*quickDestinationsListPtr);
        *quickDestinationsListPtr = nullptr;
    }

    *quickDestinationsLengthPtr = 0;

    return 0;
}

// 0x4C5734
static void wmRefreshInterfaceDial(bool shouldRefreshWindow)
{
    unsigned char* data = artGetFrameData(wmGenData.dialFrm, wmGenData.dialFrmCurrentFrameIndex);
    blitBufferToBufferTrans(data,
        wmGenData.dialFrmWidth,
        wmGenData.dialFrmHeight,
        wmGenData.dialFrmWidth,
        wmBkWinBuf + WM_WINDOW_WIDTH * WM_WINDOW_DIAL_Y + WM_WINDOW_DIAL_X,
        WM_WINDOW_WIDTH);

    if (shouldRefreshWindow) {
        Rect rect;
        rect.left = WM_WINDOW_DIAL_X;
        rect.top = WM_WINDOW_DIAL_Y - 1;
        rect.right = rect.left + wmGenData.dialFrmWidth;
        rect.bottom = rect.top + wmGenData.dialFrmHeight;
        windowRefreshRect(wmBkWin, &rect);
    }
}

// NOTE: Inlined.
//
// 0x4C57BC
void wmInterfaceDialSyncTime(bool shouldRefreshWindow)
{
    // CE: Headless guard — see wmInterfaceRefreshDate. Without it this relies
    // on artGetFrameCount(nullptr) returning -1 to no-op.
    if (wmBkWinBuf == nullptr) {
        return;
    }

    int gameHour;
    int frame;

    gameHour = gameTimeGetHour();
    frame = (gameHour / 100 + 12) % artGetFrameCount(wmGenData.dialFrm);
    if (frame != wmGenData.dialFrmCurrentFrameIndex) {
        wmGenData.dialFrmCurrentFrameIndex = frame;
        wmRefreshInterfaceDial(shouldRefreshWindow);
    }
}

void wmFadeOut()
{
    if (!wmFaded) {
        paletteFadeTo(gPaletteBlack);
        wmFaded = true;
    }
}

void wmFadeIn()
{
    if (wmFaded) {
        paletteFadeTo(_cmap);
        wmFaded = false;
    }
}

void wmFadeReset()
{
    wmFaded = false;
    paletteSetEntries(_cmap);
}

void wmBlinkRndEncounterIcon(bool special)
{
    wmGenData.encounterIconIsVisible = true;

    // CE: Original code cycles circled bright and non-circled dark icons.
    int dark;
    int bright;
    if (special) {
        dark = WORLD_MAP_ENCOUNTER_FRM_SPECIAL_DARK;
        bright = WORLD_MAP_ENCOUNTER_FRM_SPECIAL_BRIGHT;
    } else {
        dark = WORLD_MAP_ENCOUNTER_FRM_RANDOM_DARK;
        bright = WORLD_MAP_ENCOUNTER_FRM_RANDOM_BRIGHT;
    }

    for (int index = 0; index < 7; index++) {
        wmGenData.encounterCursorId = index % 2 == 0 ? dark : bright;

        if (wmInterfaceRefresh() == -1) {
            return;
        }

        renderPresent();
        inputBlockForTocks(200);
    }

    wmGenData.encounterIconIsVisible = false;
}

} // namespace fallout
