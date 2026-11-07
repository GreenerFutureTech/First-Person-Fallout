#include <stdio.h>
#include "animation.h" // reg_anim_clear — an elevator ride cancels a walk in progress
#include "config.h"
#include "game.h" // gDude
#include "light.h" // _obj_rebuild_all_light — the closing doors change the lighting
#include "map.h" // gMapHeader / mapSetTransition
#include "object.h"
#include "proto_types.h" // PROTO_ID_* — the three elevator-door scenery pids
#include "proto_instance.h" // objectAttemptPlacement
#include "server_players.h" // playerActorAt/Count/Online — the party rides together
#include "tile.h" // tileDistanceBetween
#include "elevator.h"

#include <ctype.h>
#include <string.h>

#include <algorithm>

#include "art.h"
#include "cycle.h"
#include "dbox.h"
#include "debug.h"
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
#include "sfall_config.h"
#include "svga.h"
#include "touch.h"
#include "window_manager.h"
#include "worldmap.h"

namespace fallout {

int elevatorPickLevel(int elevator, int startLevel);


// The maximum number of elevator levels.
#define ELEVATOR_LEVEL_MAX (4)

// Max number of elevators that can be loaded from elevators.ini. This limit is
// emposed by Sfall.
#define ELEVATORS_MAX 50

typedef enum ElevatorFrm {
    ELEVATOR_FRM_BUTTON_DOWN,
    ELEVATOR_FRM_BUTTON_UP,
    ELEVATOR_FRM_GAUGE,
    ELEVATOR_FRM_COUNT,
} ElevatorFrm;

typedef struct ElevatorBackground {
    InterfaceFrameId backgroundFrmId;
    InterfaceFrameId panelFrmId;
} ElevatorBackground;



static int elevatorWindowInit(int elevator);
static void elevatorWindowFree();
static int elevatorGetLevelFromKeyCode(int elevator, int keyCode);
static int elevatorGetLevelFromEscKey(int elevator, int map);

// 0x43E950 grph_id_2
static constexpr InterfaceFrmId kElevatorFrmIds[ELEVATOR_FRM_COUNT] = {
    InterfaceFrameId::MapElevatorButtonIn,
    InterfaceFrameId::MapElevatorButtonOut,
    InterfaceFrameId::MapElevatorGaj000,
};

// 0x43E95C intotal
static ElevatorBackground gElevatorBackgrounds[ELEVATORS_MAX] = {
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
    { InterfaceFrameId::MapElevatorMast1, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorMast1, InterfaceFrameId::MapElevatorMast2 },
    { InterfaceFrameId::MapElevatorMil1, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorMil1, InterfaceFrameId::MapElevatorMil2 },
    { InterfaceFrameId::MapElevatorMil1, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorMil1, InterfaceFrameId::MapElevatorMil3 },
    { InterfaceFrameId::MapElevatorVault, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorMil1, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorMil1, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorMil1, InterfaceFrameId::MapElevatorMil2 },
    { InterfaceFrameId::MapElevatorSierraBase, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
    { InterfaceFrameId::MapElevatorVault, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorVault, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorVault, InterfaceFrameId::Invalid },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
    { InterfaceFrameId::MapElevatorBos, InterfaceFrameId::MapElevatorBos2 },
};

// Number of levels for eleveators.
//
// 0x43EA1C btncnt


// 0x43EA7C retvals


// NOTE: These values are also used as key bindings.
//
// 0x43EEFC keytable
static char gElevatorLevelLabels[ELEVATORS_MAX][ELEVATOR_LEVEL_MAX] = {
    { '1', '2', '3', '4' },
    { 'G', '1', '\0', '\0' },
    { '1', '2', '3', '\0' },
    { '3', '4', '\0', '\0' },
    { '1', '2', '3', '\0' },
    { '3', '4', '\0', '\0' },
    { '1', '2', '3', '\0' },
    { '3', '4', '6', '\0' },
    { '1', '2', '3', '\0' },
    { '1', '2', '3', '\0' },
    { '1', '2', '3', '\0' },
    { '3', '4', '\0', '\0' },
    { '1', '2', '3', '4' },
    { '1', '2', '\0', '\0' },
    { '1', '2', '3', '\0' },
    { '1', '2', '3', '\0' },
    { '1', '2', '3', '\0' },
    { '1', '2', '\0', '\0' },
    { '1', '2', '\0', '\0' },
    { '1', '2', '\0', '\0' },
    { '1', '2', '\0', '\0' },
    { '1', '2', '\0', '\0' },
    { '1', '2', '\0', '\0' },
    { '1', '2', '\0', '\0' },
};

// 0x51862C sfxtable
static const char* gElevatorSoundEffects[ELEVATOR_LEVEL_MAX - 1][ELEVATOR_LEVEL_MAX] = {
    {
        "ELV1_1",
        "ELV1_1",
        "ERROR",
        "ERROR",
    },
    {
        "ELV1_2",
        "ELV1_2",
        "ELV1_1",
        "ERROR",
    },
    {
        "ELV1_3",
        "ELV1_3",
        "ELV2_3",
        "ELV1_1",
    },
};

// 0x570A54 elev_win
static int gElevatorWindow;

// 0x570A6C win_buf_2
static unsigned char* gElevatorWindowBuffer;

// 0x570A70 bk_enable_2
static bool gElevatorWindowIsoWasEnabled;

static FrmImage _elevatorFrmImages[ELEVATOR_FRM_COUNT];
static FrmImage _elevatorBackgroundFrmImage;
static FrmImage _elevatorPanelFrmImage;

// Presents elevator dialog for player to pick a desired level.
//
// 0x43EF5C elevator_select
int elevatorSelectLevel(int elevator, Map* mapPtr, int* elevationPtr, int* tilePtr)
{
    int startLevel = elevatorResolveStartLevel(elevator, *mapPtr, *elevationPtr);
    int level = elevatorPickLevel(elevator, startLevel);
    if (level == -1) level = elevatorGetLevelFromEscKey(elevator, *mapPtr);
    if (level < 0) {
        return level == -2 ? -1 : 0; // -2 = could not open the screen; -1 = escaped
    }

    elevatorResolveDestination(elevator, level, mapPtr, elevationPtr, tilePtr);

    return 0;
}

// 0x43F324 elevator_start
static int elevatorWindowInit(int elevator)
{
    gElevatorWindowIsoWasEnabled = isoDisable();
    colorCycleDisable();

    gameMouseSetCursor(MOUSE_CURSOR_ARROW);
    gameMouseObjectsHide();

    gameMouseSetCursor(MOUSE_CURSOR_ARROW);
    scriptsDisable();

    int index;
    for (index = 0; index < ELEVATOR_FRM_COUNT; index++) {
        if (!_elevatorFrmImages[index].lock(kElevatorFrmIds[index])) {
            break;
        }
    }

    if (index != ELEVATOR_FRM_COUNT) {
        for (int reversedIndex = index - 1; reversedIndex >= 0; reversedIndex--) {
            _elevatorFrmImages[reversedIndex].unlock();
        }

        if (gElevatorWindowIsoWasEnabled) {
            isoEnable();
        }

        colorCycleEnable();
        gameMouseSetCursor(MOUSE_CURSOR_ARROW);
        return -1;
    }

    const ElevatorBackground* elevatorBackground = &(gElevatorBackgrounds[elevator]);
    bool backgroundsLoaded = true;

    const InterfaceFrmId backgroundFrmId = elevatorBackground->backgroundFrmId;
    if (_elevatorBackgroundFrmImage.lock(backgroundFrmId)) {
        if (elevatorBackground->panelFrmId != InterfaceFrameId::Invalid) {
            const InterfaceFrmId panelFrmId = elevatorBackground->panelFrmId;
            if (!_elevatorPanelFrmImage.lock(panelFrmId)) {
                backgroundsLoaded = false;
            }
        }
    } else {
        backgroundsLoaded = false;
    }

    if (!backgroundsLoaded) {
        _elevatorBackgroundFrmImage.unlock();
        _elevatorPanelFrmImage.unlock();

        for (int index = 0; index < ELEVATOR_FRM_COUNT; index++) {
            _elevatorFrmImages[index].unlock();
        }

        if (gElevatorWindowIsoWasEnabled) {
            isoEnable();
        }

        colorCycleEnable();
        gameMouseSetCursor(MOUSE_CURSOR_ARROW);
        return -1;
    }

    int elevatorWindowX = (screenGetWidth() - _elevatorBackgroundFrmImage.getWidth()) / 2;
    int elevatorWindowY = (screenGetHeight() - INTERFACE_BAR_HEIGHT - 1 - _elevatorBackgroundFrmImage.getHeight()) / 2;
    gElevatorWindow = windowCreate(
        elevatorWindowX,
        elevatorWindowY,
        _elevatorBackgroundFrmImage.getWidth(),
        _elevatorBackgroundFrmImage.getHeight(),
        static_cast<ColorWithFlags>(256),
        WINDOW_MODAL | WINDOW_DONT_MOVE_TOP);
    if (gElevatorWindow == -1) {
        _elevatorBackgroundFrmImage.unlock();
        _elevatorPanelFrmImage.unlock();

        for (int index = 0; index < ELEVATOR_FRM_COUNT; index++) {
            _elevatorFrmImages[index].unlock();
        }

        if (gElevatorWindowIsoWasEnabled) {
            isoEnable();
        }

        colorCycleEnable();
        gameMouseSetCursor(MOUSE_CURSOR_ARROW);
        return -1;
    }

    gElevatorWindowBuffer = windowGetBuffer(gElevatorWindow);
    memcpy(gElevatorWindowBuffer, _elevatorBackgroundFrmImage.getData(), static_cast<size_t>(_elevatorBackgroundFrmImage.getWidth()) * _elevatorBackgroundFrmImage.getHeight());

    if (_elevatorPanelFrmImage.isLocked()) {
        blitBufferToBuffer(_elevatorPanelFrmImage.getData(),
            _elevatorPanelFrmImage.getWidth(),
            _elevatorPanelFrmImage.getHeight(),
            _elevatorPanelFrmImage.getWidth(),
            gElevatorWindowBuffer + static_cast<size_t>(_elevatorBackgroundFrmImage.getWidth()) * (_elevatorBackgroundFrmImage.getHeight() - _elevatorPanelFrmImage.getHeight()),
            _elevatorBackgroundFrmImage.getWidth());
    }

    int y = 40;
    for (int level = 0; level < elevatorLevelCount(elevator); level++) {
        int btn = buttonCreate(gElevatorWindow,
            13,
            y,
            _elevatorFrmImages[ELEVATOR_FRM_BUTTON_DOWN].getWidth(),
            _elevatorFrmImages[ELEVATOR_FRM_BUTTON_DOWN].getHeight(),
            -1,
            -1,
            -1,
            500 + level,
            _elevatorFrmImages[ELEVATOR_FRM_BUTTON_UP].getData(),
            _elevatorFrmImages[ELEVATOR_FRM_BUTTON_DOWN].getData(),
            nullptr,
            BUTTON_FLAG_TRANSPARENT);
        if (btn != -1) {
            buttonSetCallbacks(btn, _gsound_red_butt_press, nullptr);
        }
        y += 60;
    }

    return 0;
}

// 0x43F6D0 elevator_end
static void elevatorWindowFree()
{
    windowDestroy(gElevatorWindow);

    _elevatorBackgroundFrmImage.unlock();
    _elevatorPanelFrmImage.unlock();

    for (int index = 0; index < ELEVATOR_FRM_COUNT; index++) {
        _elevatorFrmImages[index].unlock();
    }

    scriptsEnable();

    if (gElevatorWindowIsoWasEnabled) {
        isoEnable();
    }

    colorCycleEnable();

    gameMouseSetCursor(MOUSE_CURSOR_ARROW);
}

// 0x43F73C Check4Keys
static int elevatorGetLevelFromKeyCode(int elevator, int keyCode)
{
    for (int index = 0; index < ELEVATOR_LEVEL_MAX; index++) {
        char c = gElevatorLevelLabels[elevator][index];
        if (c == '\0') {
            break;
        }

        // consider use std toupper instead of & 0xFF
        if (c == (char)(keyCode & 0xFF)) {
            return index + 1;
        }
    }
    return 0;
}

static int elevatorGetLevelFromEscKey(int elevator, int currentMap)
{
    return elevatorEscapeLevel(elevator, currentMap);
}

void elevatorsInit()
{
    elevatorsDataInit();
    char* elevatorsFileName;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_ELEVATORS_FILE_KEY, &elevatorsFileName);
    if (elevatorsFileName == nullptr || *elevatorsFileName == '\0') {
        debugPrint("\nElevators init: no ElevatorsFile configured, custom elevators disabled");
        return;
    }

    ScopedConfig elevatorsConfig(elevatorsFileName, false);
    if (!elevatorsConfig) {
        debugPrint("\nElevators init: failed to load %s", elevatorsFileName);
        return;
    }

    char sectionKey[4];
    char key[32];
    for (int index = 0; index < ELEVATORS_MAX; index++) {
        snprintf(sectionKey, sizeof(sectionKey), "%d", index);


        configGetEnum<InterfaceFrameId>(elevatorsConfig.get(), sectionKey, "MainFrm", &(gElevatorBackgrounds[index].backgroundFrmId));
        configGetEnum<InterfaceFrameId>(elevatorsConfig.get(), sectionKey, "ButtonsFrm", &(gElevatorBackgrounds[index].panelFrmId));

    }

    // NOTE: Sfall implementation is slightly different. It uses one
    // loop and stores `type` value in a separate lookup table. This
    // value is then used in the certain places to remap from
    // requested elevator to the new one.
    for (int index = 0; index < ELEVATORS_MAX; index++) {
        snprintf(sectionKey, sizeof(sectionKey), "%d", index);

        int type;
        if (configGetInt(elevatorsConfig.get(), sectionKey, "Image", &type)) {
            type = std::clamp(type, 0, ELEVATORS_MAX - 1);
            if (index != type) {
                memcpy(&(gElevatorBackgrounds[index]), &(gElevatorBackgrounds[type]), sizeof(*gElevatorBackgrounds));
                memcpy(&(gElevatorLevelLabels[index]), &(gElevatorLevelLabels[type]), sizeof(*gElevatorLevelLabels));
            }
        }
    }
}



int elevatorPickLevel(int elevator, int startLevel)
{
    if (elevator < 0 || elevator >= ELEVATORS_MAX) return -2;
    if (elevatorLevelCount(elevator) < 2 || elevatorLevelCount(elevator) > ELEVATOR_LEVEL_MAX) {
        char message[80];
        snprintf(message, sizeof(message), "Elevator type %d not found.", elevator);
        const char* body = "Check configuration";
        showDialogBox(message, &body, 1, 192, 135, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);
        debugPrint("\nElevator select: invalid button count type=%d buttonCount=%d",
            elevator,
            elevatorLevelCount(elevator));
        return -2;
    }

    // SFALL
    if (elevatorWindowInit(elevator) == -1) {
        return -2;
    }

    touch_set_touchscreen_mode(true);
    int elevationValue = std::clamp(startLevel, 0, elevatorLevelCount(elevator) - 1);
    int* elevationPtr = &elevationValue;
    debugPrint("\n the start elev level %d\n", *elevationPtr);

    int gaugeSliceSize = (_elevatorFrmImages[ELEVATOR_FRM_GAUGE].getWidth() * _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getHeight()) / 13;
    float gaugeUnitsPerLevel = 12.0f / (float)(elevatorLevelCount(elevator) - 1);
    blitBufferToBuffer(
        _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getData() + gaugeSliceSize * (int)((float)(*elevationPtr) * gaugeUnitsPerLevel),
        _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getWidth(),
        _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getHeight() / 13,
        _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getWidth(),
        gElevatorWindowBuffer + _elevatorBackgroundFrmImage.getWidth() * 41 + 121,
        _elevatorBackgroundFrmImage.getWidth());
    windowRefresh(gElevatorWindow);

    bool done = false;
    bool skipGauge = false;
    int keyCode;
    while (!done) {
        sharedFpsLimiter.mark();

        keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            keyCode = -1;
            skipGauge = true;
            done = true;
        }

        if (keyCode >= 500 && keyCode < 500 + elevatorLevelCount(elevator)) {
            done = true;
        }

        if (keyCode > 0 && keyCode < 500) {
            int level = elevatorGetLevelFromKeyCode(elevator, keyCode);
            if (level != 0) {
                keyCode = 500 + level - 1;
                done = true;
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (!skipGauge) {
        keyCode -= 500;

        if (*elevationPtr != keyCode) {
            float levelStep = (float)(elevatorLevelCount(elevator) - 1) / 12.0f;

            const int delay = std::max(static_cast<int>(levelStep * 276.92307f / settings.ui.anim_speed), 1);

            if (keyCode < *elevationPtr) {
                levelStep = -levelStep;
            }

            int numberOfLevelsTravelled = keyCode - *elevationPtr;
            if (numberOfLevelsTravelled < 0) {
                numberOfLevelsTravelled = -numberOfLevelsTravelled;
            }

            soundPlayFile(gElevatorSoundEffects[elevatorLevelCount(elevator) - 2][numberOfLevelsTravelled]);

            float targetGaugePosition = (float)keyCode * gaugeUnitsPerLevel;
            float currentGaugePosition = (float)(*elevationPtr) * gaugeUnitsPerLevel;
            do {
                sharedFpsLimiter.mark();

                unsigned int tick = getTicks();
                currentGaugePosition += levelStep;
                blitBufferToBuffer(
                    _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getData() + gaugeSliceSize * (int)currentGaugePosition,
                    _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getWidth(),
                    _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getHeight() / 13,
                    _elevatorFrmImages[ELEVATOR_FRM_GAUGE].getWidth(),
                    gElevatorWindowBuffer + _elevatorBackgroundFrmImage.getWidth() * 41 + 121,
                    _elevatorBackgroundFrmImage.getWidth());

                windowRefresh(gElevatorWindow);

                _GNW95_process_message();
                delay_ms(delay - (getTicks() - tick));
                _GNW95_process_message();

                renderPresent();
                sharedFpsLimiter.throttle();
            } while ((levelStep <= 0.0 || currentGaugePosition < targetGaugePosition) && (levelStep > 0.0 || currentGaugePosition > targetGaugePosition));

            inputPauseForTocks(200);
        }
    }

    elevatorWindowFree();
    touch_set_touchscreen_mode(false);

    return keyCode >= 0 && keyCode < elevatorLevelCount(elevator) ? keyCode : -1;
}

} // namespace fallout
