#include "platform/git_version.h"
#include "character_selector.h"
#include "combat_ai.h"
#include "content_config.h"
#include "critter.h"
#include "dinput.h"
#include "endgame.h"
#include "font_manager.h"
#include "game_memory.h"
#include "game_movie.h"
#include "item.h"
#include "movie.h"
#include "movie_effect.h"
#include "object.h"
#include "party_member.h"
#include "perk.h"
#include "proto.h"
#include "queue.h"
#include "random.h"
#include "sfall_arrays.h"
#include "sfall_callbacks.h"
#include "sfall_config.h"
#include "sfall_ext.h"
#include "sfall_global_scripts.h"
#include "sfall_global_vars.h"
#include "sfall_ini.h"
#include "sfall_lists.h"
#include "sfall_script_hooks.h"
#include "stat.h"
#include "string_utils.h"
#include "trait.h"
#include "win32.h"
#include "worldmap.h"
#if __APPLE__
#include <TargetConditionals.h>
#endif
#include "presenter_client.h"
#include "presenter.h"
#include "savegame.h"
#include "window_manager_private.h"
#include "script_request_handler_client.h"
#include "game_ui.h"

#include <stdio.h>
#include <string.h>

#include "game.h"

#include "actions.h"
#include "animation.h"
#include "art.h"
#include "automap.h"
#include "character_editor.h"
#include "color.h"
#include "combat.h"
#include "config.h"
#include "cycle.h"
#include "db.h"
#include "dbox.h"
#include "debug.h"
#include "display_monitor.h"
#include "draw.h"
#include "game_config.h"
#include "game_dialog.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "input.h"
#include "interface.h"
#include "inventory.h"
#include "kb.h"
#include "loadsave.h"
#include "map.h"
#include "memory.h"
#include "message.h"
#include "mouse.h"
#include "options.h"
#include "palette.h"
#include "pipboy.h"
#include "platform_compat.h"
#include "preferences.h"
#include "scripts.h"
#include "settings.h"
#include "skill.h"
#include "skilldex.h"
#include "svga.h"
#include "text_font.h"
#include "tile.h"
#include "version.h"
#include "window_manager.h"

// Client-side UI/input seam extracted from game.cc (REWRITE_PLAN, TU split).
// Pure presentation/input: the top-level key dispatcher plus the modal
// help/splash/quit/death screens and the UI enable/disable gate. Core game.cc
// retains lifecycle orchestration and the global-var/state ownership; it calls
// showSplash() by name through the game_ui.h seam and shares the de-static'd
// gIsMapper flag it owns. The public UI entry points (gameHandleKey, gameUi*,
// showQuitConfirmationDialog, gameShowDeathDialog) stay declared in game.h
// since existing callers include that. This move is mechanical and
// replay-identical.

namespace fallout {

static bool gameFirstPersonPreviewSetMouseReleased(bool enabled);

static void gameFirstPersonPreviewShowActionMenu(Object* targetObj, int mouseX, int mouseY);

static void gameFirstPersonPreviewStartLoot(Object* critter);

static char _aBuildDate[] = _BUILD_DATE;

static char _aBuildHash[] = _BUILD_HASH;

static bool gFirstPersonPreviewMouseReleased = false;

static bool gFirstPersonPreviewLootPending = false;

static bool gFirstPersonPreviewLootWindowShown = false;

static bool gFirstPersonPreviewLootMouseReleased = false;

static bool gFirstPersonPreviewActionMenuShown = false;

static unsigned int gFirstPersonPreviewLeftClickStartTime = 0;

void gameHandleFirstPersonPreviewInput(int eventCode)
{
    if (!tileFirstPersonPreviewIsEnabled()) {
        return;
    }

    if (gFirstPersonPreviewLootPending) {
        if (GameMode::isInGameMode(GameMode::kLoot)) {
            gFirstPersonPreviewLootWindowShown = true;
            return;
        } else if (gFirstPersonPreviewLootWindowShown) {
            gFirstPersonPreviewLootPending = false;
            gFirstPersonPreviewLootWindowShown = false;
            gameFirstPersonPreviewSetMouseReleased(gFirstPersonPreviewLootMouseReleased);
            gFirstPersonPreviewActionMenuShown = false;
            gFirstPersonPreviewLeftClickStartTime = 0;
            mouseResetButtonState();
        }
    }
    if (gFirstPersonPreviewMouseReleased != !mouseDeviceUsesRelativeMode()) {
        gameFirstPersonPreviewSetMouseReleased(gFirstPersonPreviewMouseReleased);
    } else if (gFirstPersonPreviewMouseReleased) {
        mouseShowCursor();
    } else {
        mouseHideCursor();
    }

    if (mouseDeviceUsesRelativeMode()) {
        int deltaX;
        int deltaY;
        mouseDeviceGetRelativeMotion(&deltaX, &deltaY);
        (void)deltaY;
        tileFirstPersonPreviewLook(static_cast<int>(deltaX * settings.preferences.mouse_sensitivity));
    }

    if (eventCode == INPUT_EVENT_FIRST_PERSON_RIGHT_CLICK) {
        gameFirstPersonPreviewSetMouseReleased(!gFirstPersonPreviewMouseReleased);
    }
}

void gameToggleFirstPersonPreview()
{
    if (tileFirstPersonPreviewIsEnabled()) {
        tileFirstPersonPreviewToggle();
        mouseShowCursor();
        gFirstPersonPreviewMouseReleased = false;
        gFirstPersonPreviewLootPending = false;
        gFirstPersonPreviewLootWindowShown = false;
        gFirstPersonPreviewActionMenuShown = false;
        gFirstPersonPreviewLeftClickStartTime = 0;
        if (!mouseDeviceSetRelativeModeForced(false)) {
            debugPrint("Failed to restore mouse mode: %s\n", SDL_GetError());
        }
    } else if (mouseDeviceSetRelativeModeForced(true)) {
        inputEventQueueReset();
        mouseResetButtonState();
        mouseHideCursor();
        tileFirstPersonPreviewToggle();
    } else {
        debugPrint("Failed to enable relative mouse mode for preview: %s\n", SDL_GetError());
    }
}

static void gameFirstPersonPreviewStartLoot(Object* critter)
{
    if (critter == nullptr || FrmId(critter).objectType() != OBJ_TYPE_CRITTER) {
        return;
    }

    gFirstPersonPreviewLootPending = true;
    gFirstPersonPreviewLootWindowShown = false;
    gFirstPersonPreviewLootMouseReleased = gFirstPersonPreviewMouseReleased;
    actionLootCritter(gDude, critter);
}

static bool gameFirstPersonPreviewUseObject(Object* object)
{
    if (object == nullptr) {
        return false;
    }

    const ObjectType objectType = FrmId(object).objectType();
    if (objectType == OBJ_TYPE_SCENERY) {
        Proto* proto = nullptr;
        if (protoGetProto(object, &proto) == 0
            && proto->scenery.type == SCENERY_TYPE_DOOR) {
            _action_use_an_object(gDude, object);
            return true;
        }
        if (!_obj_action_can_use(object)) {
            return false;
        }
        gFirstPersonPreviewLootPending = true;
        gFirstPersonPreviewLootWindowShown = true;
        gFirstPersonPreviewLootMouseReleased = gFirstPersonPreviewMouseReleased;
        inventoryOpenLooting(gDude, object);
        gFirstPersonPreviewLootPending = false;
        gFirstPersonPreviewLootWindowShown = false;
        gameFirstPersonPreviewSetMouseReleased(gFirstPersonPreviewLootMouseReleased);
        gFirstPersonPreviewActionMenuShown = false;
        gFirstPersonPreviewLeftClickStartTime = 0;
        mouseResetButtonState();
        return true;
    }

    if (objectType == OBJ_TYPE_ITEM) {
        gFirstPersonPreviewLootPending = true;
        gFirstPersonPreviewLootWindowShown = false;
        gFirstPersonPreviewLootMouseReleased = gFirstPersonPreviewMouseReleased;
        actionPickUp(gDude, object);
        return true;
    }

    return false;
}

static void gameFirstPersonPreviewLogObject(Object* object)
{
    if (object == nullptr) {
        return;
    }

    const SceneryProtoTypeId sceneryProtoId = ProtoId(object).protoId<SceneryProtoTypeId>();

    debugPrint(
        "F11 object click: proto=%d type=%d pid=%d fid=%d frame=%d rotation=%d tile=%d elevation=%d position=(%d,%d)\n",
        static_cast<int>(sceneryProtoId),
        static_cast<int>(FrmId(object).objectType()),
        object->pid,
        object->fid,
        object->frame,
        static_cast<int>(object->rotation),
        object->tile,
        static_cast<int>(object->elevation),
        object->x,
        object->y);
}

static bool gameFirstPersonPreviewCanShowObjectMenu(Object* object)
{
    if (object == nullptr) {
        return false;
    }

    const ObjectType objectType = FrmId(object).objectType();
    return objectType == OBJ_TYPE_ITEM
        || (objectType == OBJ_TYPE_SCENERY && _obj_action_can_use(object));
}

static Object* gameFirstPersonPreviewPickActionObjectAt(int screenX, int screenY)
{
    Object* object = tileFirstPersonPreviewPickObjectAt(screenX, screenY);
    if (gameFirstPersonPreviewCanShowObjectMenu(object)) {
        return object;
    }

    return tileFirstPersonPreviewPickDoorAt(screenX, screenY);
}

static void gameFirstPersonPreviewShowActionMenu(Object* targetObj, int mouseX, int mouseY)
{
    if (targetObj == nullptr || targetObj == gDude) {
        return;
    }

    GameMouseActionMenuItem menuItems[GAME_MOUSE_ACTION_MENU_ITEM_COUNT - 1];
    int menuItemCount = 0;
    const ObjectType objectType = FrmId(targetObj).objectType();
    const bool isCritter = objectType == OBJ_TYPE_CRITTER;
    const bool canUseSkill = isCritter
        || objectType == OBJ_TYPE_SCENERY
        || (objectType == OBJ_TYPE_ITEM && itemGetType(targetObj) == ITEM_TYPE_CONTAINER);
    if (isCritter) {
        if (_obj_action_can_talk_to(targetObj)) {
            if (!isInCombat()) {
                menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_TALK;
            }
            if (settings.qol.party_trade_from_menu && objectIsPartyMember(targetObj) && !isInCombat()) {
                menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_USE;
            }
        } else if (!critterFlagCheck(targetObj, CRITTER_NO_STEAL)) {
            menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_USE;
        }
    } else {
        menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_USE;
    }
    if (isCritter && actionCheckPush(gDude, targetObj)) {
        menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_PUSH;
    }
    menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_LOOK;
    if (isCritter) {
        menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_INVENTORY;
    }
    if (canUseSkill) {
        menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_USE_SKILL;
    }
    menuItems[menuItemCount++] = GAME_MOUSE_ACTION_MENU_ITEM_CANCEL;

    Rect isoWindowRect;
    if (windowGetRect(gIsoWindow, &isoWindowRect) != 0
        || gameMouseRenderActionMenuItems(mouseX, mouseY,
               menuItems, menuItemCount,
               _scr_size.right - _scr_size.left + 1,
               _scr_size.bottom - _scr_size.top - 99) != 0) {
        return;
    }

    const bool mouseWasReleased = gFirstPersonPreviewMouseReleased;
    const bool relativeModeWasEnabled = mouseDeviceUsesRelativeMode();
    if (relativeModeWasEnabled) {
        mouseDeviceSetRelativeModeForced(false);
    }
    mouseShowCursor();
    const bool isoWasEnabled = !isoIsDisabled();
    if (isoWasEnabled) {
        isoDisable();
    }
    tileFirstPersonPreviewRefresh();
    const int menuWindow = windowCreate(0, 0,
        screenGetWidth(), screenGetHeight(),
        COLOR_FIRST, WINDOW_HIDDEN | WINDOW_MOVE_ON_TOP | WINDOW_TRANSPARENT);
    if (menuWindow == -1) {
        if (isoWasEnabled) isoEnable();
        if (relativeModeWasEnabled) mouseDeviceSetRelativeModeForced(true);
        gFirstPersonPreviewMouseReleased = mouseWasReleased;
        if (mouseWasReleased) mouseShowCursor(); else mouseHideCursor();
        return;
    }
    if (menuWindow != -1) {
        unsigned char* menuBuffer = windowGetBuffer(menuWindow);
        if (menuBuffer != nullptr) {
            memset(menuBuffer, 0, windowGetWidth(menuWindow) * windowGetHeight(menuWindow));
            gameMouseCopyActionMenuBuffer(menuBuffer, windowGetWidth(menuWindow),
                windowGetWidth(menuWindow), windowGetHeight(menuWindow),
                mouseX, mouseY);
            windowShow(menuWindow);
        }
    }

    int selectedIndex = 0;
    mouseDeviceSetLogicalPosition(mouseX, mouseY);
    _mouse_set_position(mouseX, mouseY);
    while ((mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_UP) == 0) {
        sharedFpsLimiter.mark();
        inputGetInput();

        int updatedMouseX;
        int updatedMouseY;
        mouseGetPosition(&updatedMouseX, &updatedMouseY);
        if (abs(updatedMouseY - mouseY) > 10) {
            int nextIndex = selectedIndex + (mouseY >= updatedMouseY ? -1 : 1);
            if (nextIndex < 0) nextIndex = 0;
            if (nextIndex >= menuItemCount) nextIndex = menuItemCount - 1;
            if (gameMouseHighlightActionMenuItemAtIndex(nextIndex) == 0) {
                selectedIndex = nextIndex;
                if (menuWindow != -1 && windowGetBuffer(menuWindow) != nullptr) {
                    unsigned char* menuBuffer = windowGetBuffer(menuWindow);
                    memset(menuBuffer, 0, windowGetWidth(menuWindow) * windowGetHeight(menuWindow));
                    gameMouseCopyActionMenuBuffer(menuBuffer, windowGetWidth(menuWindow),
                        windowGetWidth(menuWindow), windowGetHeight(menuWindow),
                        updatedMouseX, updatedMouseY);
                    windowRefresh(menuWindow);
                }
            }
            mouseY = updatedMouseY;
        }
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    windowDestroy(menuWindow);
    if (isoWasEnabled) {
        isoEnable();
    }
    tileFirstPersonPreviewRefresh();
    mouseDeviceSetLogicalPosition(mouseX, mouseY);
    _mouse_set_position(mouseX, mouseY);

    const GameMouseActionMenuItem selectedAction = gameMouseGetHighlightedActionMenuItem();
    bool lootStarted = false;

    switch (selectedAction) {
    case GAME_MOUSE_ACTION_MENU_ITEM_TALK:
        actionTalk(gDude, targetObj);
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_USE:
        if (isCritter && objectIsPartyMember(targetObj)) {
            actionUseSkill(gDude, targetObj, SKILL_STEAL);
        } else if (isCritter) {
            gameFirstPersonPreviewStartLoot(targetObj);
            lootStarted = true;
        } else {
            gameFirstPersonPreviewUseObject(targetObj);
            lootStarted = true;
        }
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_PUSH:
        actionPush(gDude, targetObj);
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_LOOK:
        if (objectExamine(gDude, targetObj) == -1) {
            objectLookAt(gDude, targetObj);
        }
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_INVENTORY:
        inventoryOpenUseItemOn(targetObj);
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_USE_SKILL: {
        Skill skill = SKILL_INVALID;
        switch (skilldexOpen()) {
        case SKILLDEX_RC_SNEAK: _action_skill_use(SKILL_SNEAK); break;
        case SKILLDEX_RC_LOCKPICK: skill = SKILL_LOCKPICK; break;
        case SKILLDEX_RC_STEAL: skill = SKILL_STEAL; break;
        case SKILLDEX_RC_TRAPS: skill = SKILL_TRAPS; break;
        case SKILLDEX_RC_FIRST_AID: skill = SKILL_FIRST_AID; break;
        case SKILLDEX_RC_DOCTOR: skill = SKILL_DOCTOR; break;
        case SKILLDEX_RC_SCIENCE: skill = SKILL_SCIENCE; break;
        case SKILLDEX_RC_REPAIR: skill = SKILL_REPAIR; break;
        default: break;
        }
        if (skill != SKILL_INVALID) {
            actionUseSkill(gDude, targetObj, skill);
        }
        break;
    }
    default:
        break;
    }
    if (!lootStarted) {
        gFirstPersonPreviewMouseReleased = mouseWasReleased;
    }
    if (relativeModeWasEnabled) {
        mouseDeviceSetRelativeModeForced(!lootStarted);
    }
    if (lootStarted || mouseWasReleased) {
        mouseShowCursor();
    } else {
        mouseHideCursor();
    }
}

static bool gameFirstPersonPreviewSetMouseReleased(bool enabled);

class FirstPersonPreviewMouseReleaseScope {
public:
    FirstPersonPreviewMouseReleaseScope()
        : _firstPersonPreviewWasEnabled(tileFirstPersonPreviewIsEnabled())
        , _mouseWasReleased(gFirstPersonPreviewMouseReleased)
    {
        if (_firstPersonPreviewWasEnabled) {
            gameFirstPersonPreviewSetMouseReleased(true);
            mouseResetButtonState();
        }
    }

    ~FirstPersonPreviewMouseReleaseScope()
    {
        if (_firstPersonPreviewWasEnabled && tileFirstPersonPreviewIsEnabled()) {
            gameFirstPersonPreviewSetMouseReleased(_mouseWasReleased);
        }
    }

private:
    bool _firstPersonPreviewWasEnabled;
    bool _mouseWasReleased;
};

static bool gameFirstPersonPreviewSetMouseReleased(bool enabled)
{
    if (mouseDeviceSetRelativeModeForced(!enabled)) {
        gFirstPersonPreviewMouseReleased = enabled;
        if (enabled) {
            mouseShowCursor();
        } else {
            mouseHideCursor();
        }
        return true;
    }

    debugPrint("Failed to change first-person mouse mode: %s\n", SDL_GetError());
    return false;
}

void gameHandleSkilldexResult(SkilldexRC rc)
{
    GameMouseMode mode = GAME_MOUSE_MODE_INVALID;

    switch (rc) {
    case SKILLDEX_RC_ERROR:
        debugPrint("\n ** Error calling skilldex_select()! ** \n");
        break;
    case SKILLDEX_RC_SNEAK:
        _action_skill_use(SKILL_SNEAK);
        break;
    case SKILLDEX_RC_LOCKPICK:
        mode = GAME_MOUSE_MODE_USE_LOCKPICK;
        break;
    case SKILLDEX_RC_STEAL:
        mode = GAME_MOUSE_MODE_USE_STEAL;
        break;
    case SKILLDEX_RC_TRAPS:
        mode = GAME_MOUSE_MODE_USE_TRAPS;
        break;
    case SKILLDEX_RC_FIRST_AID:
        mode = GAME_MOUSE_MODE_USE_FIRST_AID;
        break;
    case SKILLDEX_RC_DOCTOR:
        mode = GAME_MOUSE_MODE_USE_DOCTOR;
        break;
    case SKILLDEX_RC_SCIENCE:
        mode = GAME_MOUSE_MODE_USE_SCIENCE;
        break;
    case SKILLDEX_RC_REPAIR:
        mode = GAME_MOUSE_MODE_USE_REPAIR;
        break;
    default:
        break;
    }

    if (mode != GAME_MOUSE_MODE_INVALID) {
        gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
        gameMouseSetMode(mode);
    }
}


#define HELP_SCREEN_WIDTH (640)
#define HELP_SCREEN_HEIGHT (480)

#define SPLASH_WIDTH (640)
#define SPLASH_HEIGHT (480)
#define SPLASH_COUNT (10)

void showHelp();

// 0x5186B4
static bool gGameUiDisabled = false;

// 0x442D44
int gameHandleKey(int eventCode, bool isInCombatMode)
{
    gameHandleFirstPersonPreviewInput(eventCode);

    if (tileFirstPersonPreviewIsEnabled()
        && eventCode == -1
        && (mouseGetEvent() & MOUSE_EVENT_WHEEL) != 0) {
        int wheelX;
        int wheelY;
        mouseGetWheel(&wheelX, &wheelY);
        (void)wheelX;
        if (tileFirstPersonPreviewZoomTopology(wheelY)) {
            return 0;
        }
    }

    int topologyZoomDirection = 0;
    if (eventCode == KEY_PLUS || eventCode == KEY_EQUAL) {
        topologyZoomDirection = 1;
    } else if (eventCode == KEY_MINUS || eventCode == KEY_UNDERSCORE) {
        topologyZoomDirection = -1;
    }
    if (topologyZoomDirection != 0
        && tileFirstPersonPreviewZoomTopology(topologyZoomDirection)) {
        return 0;
    }

    // NOTE: Uninline.
    if (gameGetState() == GameState::DialogRequested) {
        const bool firstPersonPreviewWasEnabled = tileFirstPersonPreviewIsEnabled();
        const bool firstPersonPreviewMouseWasReleased = gFirstPersonPreviewMouseReleased;
        if (firstPersonPreviewWasEnabled) {
            mouseShowCursor();
            if (!mouseDeviceSetRelativeModeForced(false)) {
                debugPrint("Failed to enable mouse cursor for dialogue: %s\n", SDL_GetError());
            }
        }

        _gdialogSystemEnter();

        if (firstPersonPreviewWasEnabled && tileFirstPersonPreviewIsEnabled()) {
            gameFirstPersonPreviewSetMouseReleased(firstPersonPreviewMouseWasReleased);
        }
    }

    if (tileFirstPersonPreviewIsEnabled()
        && eventCode == INPUT_EVENT_FIRST_PERSON_RIGHT_CLICK) {
        tileFirstPersonPreviewSetMoveMarkerTile(-1);
        return 0;
    }

    if (tileFirstPersonPreviewIsEnabled()
        && (eventCode == -1 || eventCode == -2 || eventCode == INPUT_EVENT_FIRST_PERSON_LEFT_CLICK)) {
        const int mouseState = mouseGetEvent();
        if (eventCode == -2
            && (mouseState & MOUSE_EVENT_RIGHT_BUTTON_DOWN) != 0
            && (mouseState & MOUSE_EVENT_RIGHT_BUTTON_REPEAT) == 0) {
            return 0;
        }

        if (isInCombat()) {
            if (gFirstPersonPreviewMouseReleased
                && eventCode == -2
                && (mouseState & MOUSE_EVENT_LEFT_BUTTON_UP) != 0) {
                int mouseX;
                int mouseY;
                mouseGetPosition(&mouseX, &mouseY);

                Rect isoWindowRect;
                if (windowGetRect(gIsoWindow, &isoWindowRect) == 0
                    && mouseX >= isoWindowRect.left
                    && mouseX <= isoWindowRect.right
                    && mouseY >= isoWindowRect.top
                    && mouseY <= isoWindowRect.bottom) {
                    Object* critter = gameMouseGetMode() == GAME_MOUSE_MODE_CROSSHAIR
                        ? tileFirstPersonPreviewPickCritterAt(mouseX, mouseY)
                        : nullptr;
                    if (critter != nullptr) {
                        gameFirstPersonPreviewLogObject(critter);
                        if (critterIsDead(critter)) {
                            gameFirstPersonPreviewStartLoot(critter);
                        } else {
                            _combat_attack_this(critter);
                        }
                    } else {
                        Object* object = tileFirstPersonPreviewPickObjectAt(mouseX, mouseY);
                        gameFirstPersonPreviewLogObject(object);
                        bool handled = gameFirstPersonPreviewUseObject(object);
                        if (!handled) {
                            Object* door = tileFirstPersonPreviewPickDoorAt(mouseX, mouseY);
                            if (door != nullptr) {
                                gameFirstPersonPreviewLogObject(door);
                                _action_use_an_object(gDude, door);
                                handled = true;
                            }
                        }
                        if (!handled) {
                            const int destinationTile = tileFirstPersonPreviewPickGroundTileAt(
                                mouseX - isoWindowRect.left,
                                mouseY - isoWindowRect.top);
                            if (tileIsValid(destinationTile)) {
                            int actionPoints = gDude->data.critter.combat.ap + _combat_free_move;
                            if (gPressedPhysicalKeys[SDL_SCANCODE_RCTRL]
                                || gPressedPhysicalKeys[SDL_SCANCODE_LCTRL]) {
                                HitMode hitMode;
                                bool aiming;
                                interfaceGetCurrentHitMode(&hitMode, &aiming);
                                actionPoints -= itemGetActionPointCost(gDude, hitMode, aiming);
                            }
                            if (actionPoints > 0) {
                                tileFirstPersonPreviewSetMoveMarkerTile(destinationTile);
                                tileFirstPersonPreviewTurnToTile(destinationTile);
                                if (reg_anim_begin(ANIMATION_REQUEST_RESERVED) == 0) {
                                    if (settings.preferences.running) {
                                        animationRegisterRunToTile(gDude, destinationTile,
                                            gDude->elevation, actionPoints, 0);
                                    } else {
                                        animationRegisterMoveToTile(gDude, destinationTile,
                                            gDude->elevation, actionPoints, 0);
                                    }
                                    reg_anim_end();
                                }
                            }
                            }
                        }
                    }
                }
                return 0;
            }
            if (eventCode == INPUT_EVENT_FIRST_PERSON_LEFT_CLICK) {
                return 0;
            }
        } else {
            if (eventCode == INPUT_EVENT_FIRST_PERSON_LEFT_CLICK) {
                gFirstPersonPreviewLeftClickStartTime = getTicks();
                const int centerX = screenGetWidth() / 2;
                const int centerY = screenGetHeight() / 2;
                Object* critter = tileFirstPersonPreviewPickCritterAt(centerX, centerY);
                bool handled = false;
                if (critter != nullptr) {
                    gameFirstPersonPreviewLogObject(critter);
                    if (critterIsDead(critter)) {
                        gameFirstPersonPreviewStartLoot(critter);
                        handled = true;
                    } else if (_obj_action_can_talk_to(critter)) {
                        actionTalk(gDude, critter);
                        handled = true;
                    }
                }
                Object* object = handled ? nullptr : tileFirstPersonPreviewPickObjectAt(centerX, centerY);
                gameFirstPersonPreviewLogObject(object);
                handled = handled || gameFirstPersonPreviewUseObject(object);
                if (!handled) {
                    Object* door = tileFirstPersonPreviewPickDoorAt(centerX, centerY);
                    if (door != nullptr) {
                        gameFirstPersonPreviewLogObject(door);
                        _action_use_an_object(gDude, door);
                        handled = true;
                    }
                }
                if (handled) {
                    gFirstPersonPreviewLeftClickStartTime = 0;
                }
                return 0;
            }

            if (eventCode == -1 || eventCode == -2) {
                int mouseX;
                int mouseY;
                mouseGetPosition(&mouseX, &mouseY);
                const Uint32 sdlMouseButtons = SDL_GetMouseState(nullptr, nullptr);
                const bool leftButtonDown = !gFirstPersonPreviewMouseReleased
                    ? (sdlMouseButtons & SDL_BUTTON_LMASK) != 0
                    : (mouseState & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0;
                const bool leftButtonHeld = leftButtonDown
                    && getTicksSince(gFirstPersonPreviewLeftClickStartTime) >= BUTTON_REPEAT_TIME;

                if (gFirstPersonPreviewMouseReleased) {
                    Rect isoWindowRect;
                    if (windowGetRect(gIsoWindow, &isoWindowRect) == 0
                        && mouseX >= isoWindowRect.left
                        && mouseX <= isoWindowRect.right
                        && mouseY >= isoWindowRect.top
                        && mouseY <= isoWindowRect.bottom) {
                        if ((mouseState & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0
                            && (mouseState & MOUSE_EVENT_LEFT_BUTTON_REPEAT) == 0) {
                            gFirstPersonPreviewLeftClickStartTime = getTicks();
                        }
                        if (leftButtonHeld) {
                            if (!gFirstPersonPreviewActionMenuShown) {
                                Object* critter = tileFirstPersonPreviewPickCritterAt(mouseX, mouseY);
                                if (critter != nullptr) {
                                    gFirstPersonPreviewActionMenuShown = true;
                                    gameFirstPersonPreviewShowActionMenu(critter, mouseX, mouseY);
                                } else {
                                    Object* object = gameFirstPersonPreviewPickActionObjectAt(mouseX, mouseY);
                                    if (object != nullptr) {
                                        gFirstPersonPreviewActionMenuShown = true;
                                        gameFirstPersonPreviewShowActionMenu(object, mouseX, mouseY);
                                    }
                                }
                            }
                        } else if ((eventCode == -2
                                       && (mouseState & MOUSE_EVENT_LEFT_BUTTON_UP) != 0)
                            || (eventCode == -1 && !leftButtonDown)) {
                            if (!gFirstPersonPreviewActionMenuShown
                                && gFirstPersonPreviewLeftClickStartTime != 0) {
                                Object* critter = tileFirstPersonPreviewPickCritterAt(mouseX, mouseY);
                                if (critter != nullptr) {
                                    gameFirstPersonPreviewLogObject(critter);
                                    if (_obj_action_can_talk_to(critter)) actionTalk(gDude, critter);
                                    else gameFirstPersonPreviewStartLoot(critter);
                                } else {
                                    Object* object = tileFirstPersonPreviewPickObjectAt(mouseX, mouseY);
                                    gameFirstPersonPreviewLogObject(object);
                                    bool handled = gameFirstPersonPreviewUseObject(object);
                                    if (!handled) {
                                        Object* door = tileFirstPersonPreviewPickDoorAt(mouseX, mouseY);
                                        if (door != nullptr) {
                                            gameFirstPersonPreviewLogObject(door);
                                            _action_use_an_object(gDude, door);
                                            handled = true;
                                        }
                                    }
                                    if (!handled) {
                                        const int destinationTile = tileFirstPersonPreviewPickGroundTileAt(
                                            mouseX - isoWindowRect.left,
                                            mouseY - isoWindowRect.top);
                                        if (tileIsValid(destinationTile)) {
                                        tileFirstPersonPreviewSetMoveMarkerTile(destinationTile);
                                        tileFirstPersonPreviewTurnToTile(destinationTile);
                                        if (settings.system.interrupt_walk) {
                                            reg_anim_clear(gDude);
                                        }
                                        if (reg_anim_begin(ANIMATION_REQUEST_RESERVED) == 0) {
                                            if (settings.preferences.running) {
                                                animationRegisterRunToTile(gDude, destinationTile,
                                                    gDude->elevation, -1, 0);
                                            } else {
                                                animationRegisterMoveToTile(gDude, destinationTile,
                                                    gDude->elevation, -1, 0);
                                            }
                                            reg_anim_end();
                                        }
                                        }
                                    }
                                }
                            }
                            gFirstPersonPreviewActionMenuShown = false;
                            gFirstPersonPreviewLeftClickStartTime = 0;
                        }
                        return 0;
                    }
                } else {
                    if (leftButtonHeld
                        && getTicksSince(gFirstPersonPreviewLeftClickStartTime) >= BUTTON_REPEAT_TIME
                        && !gFirstPersonPreviewActionMenuShown) {
                        Object* critter = tileFirstPersonPreviewPickCritter();
                        if (critter != nullptr) {
                            gFirstPersonPreviewActionMenuShown = true;
                            gameFirstPersonPreviewShowActionMenu(critter,
                                screenGetWidth() / 2, screenGetHeight() / 2);
                        } else {
                            Rect isoWindowRect;
                            if (windowGetRect(gIsoWindow, &isoWindowRect) == 0) {
                                const int centerX = isoWindowRect.left + windowGetWidth(gIsoWindow) / 2;
                                const int centerY = isoWindowRect.top + windowGetHeight(gIsoWindow) / 2;
                                Object* object = gameFirstPersonPreviewPickActionObjectAt(centerX, centerY);
                                if (object != nullptr) {
                                    gFirstPersonPreviewActionMenuShown = true;
                                    gameFirstPersonPreviewShowActionMenu(object, centerX, centerY);
                                }
                            }
                        }
                    } else if (eventCode == -2
                        && !leftButtonHeld
                        && !leftButtonDown
                        && gFirstPersonPreviewLeftClickStartTime != 0) {
                        if (!gFirstPersonPreviewActionMenuShown) {
                            Object* critter = tileFirstPersonPreviewPickCritter();
                            if (critter != nullptr) {
                                if (_obj_action_can_talk_to(critter)) actionTalk(gDude, critter);
                                else gameFirstPersonPreviewStartLoot(critter);
                            } else {
                                Rect isoWindowRect;
                                if (windowGetRect(gIsoWindow, &isoWindowRect) == 0) {
                                    const int centerX = isoWindowRect.left + windowGetWidth(gIsoWindow) / 2;
                                    const int centerY = isoWindowRect.top + windowGetHeight(gIsoWindow) / 2;
                                    Object* object = tileFirstPersonPreviewPickObjectAt(centerX, centerY);
                                    gameFirstPersonPreviewLogObject(object);
                                    bool handled = gameFirstPersonPreviewUseObject(object);
                                    if (!handled) {
                                        Object* door = tileFirstPersonPreviewPickDoorAt(centerX, centerY);
                                        if (door != nullptr) {
                                            gameFirstPersonPreviewLogObject(door);
                                            _action_use_an_object(gDude, door);
                                        }
                                    }
                                }
                            }
                        }
                        gFirstPersonPreviewActionMenuShown = false;
                        gFirstPersonPreviewLeftClickStartTime = 0;
                    }
                }
                return 0;
            }
        }
    }

    if (tileFirstPersonPreviewIsEnabled()) {
        int movementDirection = -1;
        switch (eventCode) {
        case KEY_LOWERCASE_W:
        case KEY_UPPERCASE_W:
            movementDirection = 0;
            break;
        case KEY_LOWERCASE_D:
        case KEY_UPPERCASE_D:
            movementDirection = 1;
            break;
        case KEY_LOWERCASE_S:
        case KEY_UPPERCASE_S:
            movementDirection = 2;
            break;
        case KEY_LOWERCASE_A:
        case KEY_UPPERCASE_A:
            movementDirection = 3;
            break;
        default:
            break;
        }

        if (movementDirection != -1) {
            tileFirstPersonPreviewMove(movementDirection);
            return 0;
        }
    }

    if (eventCode == -1) {
        if ((mouseGetEvent() & MOUSE_EVENT_WHEEL) != 0) {
            int wheelX;
            int wheelY;
            mouseGetWheel(&wheelX, &wheelY);

            int dx = 0;
            if (wheelX > 0) {
                dx = 1;
            } else if (wheelX < 0) {
                dx = -1;
            }

            int dy = 0;
            if (wheelY > 0) {
                dy = -1;
            } else if (wheelY < 0) {
                dy = 1;
            }

            mapScroll(dx, dy);
        }
        return 0;
    }

    if (eventCode == -2) {
        int mouseState = mouseGetEvent();
        int mouseX;
        int mouseY;
        mouseGetPosition(&mouseX, &mouseY);

        if ((mouseState & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0) {
            if ((mouseState & MOUSE_EVENT_LEFT_BUTTON_REPEAT) == 0) {
                if (mouseX == _scr_size.left || mouseX == _scr_size.right
                    || mouseY == _scr_size.top || mouseY == _scr_size.bottom) {
                    _gmouse_clicked_on_edge = true;
                } else {
                    _gmouse_clicked_on_edge = false;
                }
            }
        } else {
            if ((mouseState & MOUSE_EVENT_LEFT_BUTTON_UP) != 0) {
                _gmouse_clicked_on_edge = false;
            }
        }

        _gmouse_handle_event(mouseX, mouseY, mouseState);
        return 0;
    }

    if (_gmouse_is_scrolling()) {
        return 0;
    }

    switch (eventCode) {
    case -20:
        if (interfaceBarEnabled()) {
            _intface_use_item();
        }
        break;
    case -2:
        if (1) {
            int mouseEvent = mouseGetEvent();
            int mouseX;
            int mouseY;
            mouseGetPosition(&mouseX, &mouseY);

            if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0) {
                if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_REPEAT) == 0) {
                    if (mouseX == _scr_size.left || mouseX == _scr_size.right
                        || mouseY == _scr_size.top || mouseY == _scr_size.bottom) {
                        _gmouse_clicked_on_edge = true;
                    } else {
                        _gmouse_clicked_on_edge = false;
                    }
                }
            } else {
                if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_UP) != 0) {
                    _gmouse_clicked_on_edge = false;
                }
            }

            _gmouse_handle_event(mouseX, mouseY, mouseEvent);
        }
        break;
    case KEY_CTRL_Q:
    case KEY_CTRL_X:
    case KEY_F10:
    {
        soundPlayFile("ib1p1xx1");
        FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
        showQuitConfirmationDialog();
        break;
    }
    case KEY_TAB:
        if (interfaceBarEnabled()
            && gPressedPhysicalKeys[SDL_SCANCODE_LALT] == 0
            && gPressedPhysicalKeys[SDL_SCANCODE_RALT] == 0) {
            soundPlayFile("ib1p1xx1");
            const bool firstPersonPreviewWasEnabled = tileFirstPersonPreviewIsEnabled();
            const bool firstPersonPreviewMouseWasReleased = gFirstPersonPreviewMouseReleased;
            if (firstPersonPreviewWasEnabled) {
                tileFirstPersonPreviewToggle();
                mouseShowCursor();
                gFirstPersonPreviewMouseReleased = false;
                gFirstPersonPreviewActionMenuShown = false;
                gFirstPersonPreviewLeftClickStartTime = 0;
                if (!mouseDeviceSetRelativeModeForced(false)) {
                    debugPrint("Failed to restore mouse mode before automap: %s\n", SDL_GetError());
                }
            }
            automapShow(true, false);
            if (firstPersonPreviewWasEnabled) {
                if (tileFirstPersonPreviewIsEnabled()) {
                    debugPrint("First-person mode was unexpectedly enabled after automap\n");
                } else {
                    tileFirstPersonPreviewToggle();
                    if (!gameFirstPersonPreviewSetMouseReleased(firstPersonPreviewMouseWasReleased)) {
                        debugPrint("Failed to restore first-person mouse mode after automap: %s\n", SDL_GetError());
                    }
                }
            }
        }
        break;
    case KEY_CTRL_P:
    {
        soundPlayFile("ib1p1xx1");
        FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
        showPause(false);
        break;
    }
    case KEY_UPPERCASE_A:
    case KEY_LOWERCASE_A:
        if (interfaceBarEnabled()) {
            if (!isInCombatMode) {
                _combat(nullptr);
            }
        }
        break;
    case KEY_UPPERCASE_N:
    case KEY_LOWERCASE_N:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            interfaceCycleItemAction();
        }
        break;
    case KEY_UPPERCASE_M:
    case KEY_LOWERCASE_M:
        gameMouseCycleMode();
        break;
    case KEY_UPPERCASE_B:
    case KEY_LOWERCASE_B:
        // change active hand
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            interfaceBarSwapHands(true);
        }
        break;
    case KEY_UPPERCASE_C:
    case KEY_LOWERCASE_C:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            bool isoWasEnabled = isoDisable();
            FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
            characterEditorShow(false);
            if (isoWasEnabled) {
                isoEnable();
            }
        }
        break;
    case KEY_UPPERCASE_I:
    case KEY_LOWERCASE_I:
        // open inventory
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            const bool firstPersonPreviewWasEnabled = tileFirstPersonPreviewIsEnabled();
            const bool firstPersonPreviewMouseWasReleased = gFirstPersonPreviewMouseReleased;
            if (firstPersonPreviewWasEnabled) {
                if (!gameFirstPersonPreviewSetMouseReleased(true)) {
                    debugPrint("Failed to enable mouse cursor for inventory: %s\n", SDL_GetError());
                }
                mouseResetButtonState();
            }
            inventoryOpen();
            if (firstPersonPreviewWasEnabled && tileFirstPersonPreviewIsEnabled()) {
                if (!gameFirstPersonPreviewSetMouseReleased(firstPersonPreviewMouseWasReleased)) {
                    debugPrint("Failed to restore first-person mouse mode after inventory: %s\n", SDL_GetError());
                }
            }
        }
        break;
    case KEY_ESCAPE:
    case KEY_UPPERCASE_O:
    case KEY_LOWERCASE_O:
        // options
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
            showOptions();
        }
        break;
    case KEY_UPPERCASE_P:
    case KEY_LOWERCASE_P:
        // pipboy
        if (interfaceBarEnabled()) {
            if (isInCombatMode) {
                soundPlayFile("iisxxxx1");

                // Pipboy not available in combat!
                MessageListItem messageListItem;
                char title[128];
                stringCopy(title, getmsg(&gMiscMessageList, &messageListItem, 7));
                showDialogBox(title, nullptr, 0, 192, 116, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
            } else {
                soundPlayFile("ib1p1xx1");
                FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
                pipboyOpen(PIPBOY_OPEN_INTENT_UNSPECIFIED);
            }
        }
        break;
    case KEY_UPPERCASE_S:
    case KEY_LOWERCASE_S:
        // skilldex
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");

            // NOTE: There is an `inc` for this value to build jump table which
            // is not needed.
            FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
            SkilldexRC rc = skilldexOpen();

            gameHandleSkilldexResult(rc);
        }
        break;
    case KEY_UPPERCASE_Z:
    case KEY_LOWERCASE_Z:
        if (interfaceBarEnabled()) {
            if (isInCombatMode) {
                soundPlayFile("iisxxxx1");

                // Pipboy not available in combat!
                MessageListItem messageListItem;
                char title[128];
                stringCopy(title, getmsg(&gMiscMessageList, &messageListItem, 7));
                showDialogBox(title, nullptr, 0, 192, 116, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
            } else {
                soundPlayFile("ib1p1xx1");
                FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
                pipboyOpen(PIPBOY_OPEN_INTENT_REST);
            }
        }
        break;
    case KEY_HOME:
        if (gDude->elevation != gElevation) {
            mapSetElevation(gDude->elevation);
        }

        if (gIsMapper) {
            tileSetCenter(gDude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
        } else {
            _tile_scroll_to(gDude->tile, 2);
        }

        break;
    case KEY_1:
    case KEY_EXCLAMATION:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            _action_skill_use(SKILL_SNEAK);
        }
        break;
    case KEY_2:
    case KEY_AT:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            gameMouseSetMode(GAME_MOUSE_MODE_USE_LOCKPICK);
        }
        break;
    case KEY_3:
    case KEY_NUMBER_SIGN:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            gameMouseSetMode(GAME_MOUSE_MODE_USE_STEAL);
        }
        break;
    case KEY_4:
    case KEY_DOLLAR:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            gameMouseSetMode(GAME_MOUSE_MODE_USE_TRAPS);
        }
        break;
    case KEY_5:
    case KEY_PERCENT:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            gameMouseSetMode(GAME_MOUSE_MODE_USE_FIRST_AID);
        }
        break;
    case KEY_6:
    case KEY_CARET:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            gameMouseSetMode(GAME_MOUSE_MODE_USE_DOCTOR);
        }
        break;
    case KEY_7:
    case KEY_AMPERSAND:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            gameMouseSetMode(GAME_MOUSE_MODE_USE_SCIENCE);
        }
        break;
    case KEY_8:
    case KEY_ASTERISK:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            gameMouseSetMode(GAME_MOUSE_MODE_USE_REPAIR);
        }
        break;
    case KEY_MINUS:
    case KEY_UNDERSCORE:
        brightnessDecrease();
        break;
    case KEY_EQUAL:
    case KEY_PLUS:
        brightnessIncrease();
        break;
    case KEY_COMMA:
    case KEY_LESS:
        if (reg_anim_begin(ANIMATION_REQUEST_RESERVED) == 0) {
            animationRegisterRotateCounterClockwise(gDude);
            reg_anim_end();
        }
        break;
    case KEY_DOT:
    case KEY_GREATER:
        if (reg_anim_begin(ANIMATION_REQUEST_RESERVED) == 0) {
            animationRegisterRotateClockwise(gDude);
            reg_anim_end();
        }
        break;
    case KEY_SLASH:
    case KEY_QUESTION:
        if (1) {
            soundPlayFile("ib1p1xx1");

            int month;
            int day;
            int year;
            gameTimeGetDate(&month, &day, &year);

            MessageList messageList;
            if (messageListInit(&messageList)) {
                char path[COMPAT_MAX_PATH];
                snprintf(path, sizeof(path), "%s%s", gGameMsgPath, "editor.msg");

                if (messageListLoad(&messageList, path)) {
                    MessageListItem messageListItem;
                    messageListItem.num = 500 + month - 1;
                    if (messageListGetItem(&messageList, &messageListItem)) {
                        char* time = gameTimeGetTimeString();

                        char date[128];
                        snprintf(date, sizeof(date), "%s: %d/%d %s", messageListItem.text, day, year, time);

                        displayMonitorAddMessage(date);
                    }
                }

                messageListFree(&messageList);
            }
        }
        break;
    case KEY_F1:
    {
        soundPlayFile("ib1p1xx1");
        FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
        showHelp();
        break;
    }
    case KEY_F2:
        gameSoundSetMasterVolume(gameSoundGetMasterVolume() - 2047);
        break;
    case KEY_F3:
        gameSoundSetMasterVolume(gameSoundGetMasterVolume() + 2047);
        break;
    case KEY_CTRL_S:
    case KEY_F4:
    {
        soundPlayFile("ib1p1xx1");
        FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
        if (lsgSaveGame(1) == -1) {
            debugPrint("\n ** Error calling SaveGame()! **\n");
        }
        break;
    }
    case KEY_CTRL_L:
    case KEY_F5:
    {
        soundPlayFile("ib1p1xx1");
        FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
        if (lsgLoadGame(LOAD_SAVE_MODE_NORMAL) == -1) {
            debugPrint("\n ** Error calling LoadGame()! **\n");
        }
        break;
    }
    case KEY_F6:
        if (1) {
            soundPlayFile("ib1p1xx1");

            FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
            int rc = lsgSaveGame(LOAD_SAVE_MODE_QUICK);
            if (rc == -1) {
                debugPrint("\n ** Error calling SaveGame()! **\n");
            } else if (rc == 1) {
                MessageListItem messageListItem;
                // Quick save game successfully saved.
                char* msg = getmsg(&gMiscMessageList, &messageListItem, 5);
                displayMonitorAddMessage(msg);
            }
        }
        break;
    case KEY_F7:
        if (1) {
            soundPlayFile("ib1p1xx1");

            FirstPersonPreviewMouseReleaseScope mouseReleaseScope;
            int rc = lsgLoadGame(LOAD_SAVE_MODE_QUICK);
            if (rc == -1) {
                debugPrint("\n ** Error calling LoadGame()! **\n");
            } else if (rc == 1) {
                MessageListItem messageListItem;
                // Quick load game successfully loaded.
                char* msg = getmsg(&gMiscMessageList, &messageListItem, 4);
                displayMonitorAddMessage(msg);
            }
        }
        break;
    case KEY_F9:
        tileFirstPersonPreviewToggleTopology();
        break;
    case KEY_F11:
        gameToggleFirstPersonPreview();
        break;
    case KEY_CTRL_V:
        if (1) {
            soundPlayFile("ib1p1xx1");

            char version[VERSION_MAX];
            versionGetVersion(version, sizeof(version));
            displayMonitorAddMessage(version);
            displayMonitorAddMessage(_aBuildHash);
            displayMonitorAddMessage(_aBuildDate);
        }
        break;
    case KEY_ARROW_LEFT:
        mapScroll(-1, 0);
        break;
    case KEY_ARROW_RIGHT:
        mapScroll(1, 0);
        break;
    case KEY_ARROW_UP:
        mapScroll(0, -1);
        break;
    case KEY_ARROW_DOWN:
        mapScroll(0, 1);
        break;
    }

    return 0;
}

// game_ui_disable
// 0x443BFC
void gameUiDisable(int allowScrolling)
{
    if (!gGameUiDisabled) {
        gameMouseObjectsHide();
        _gmouse_disable(allowScrolling);
        keyboardDisable();
        interfaceBarDisable();
        gGameUiDisabled = true;
    }
}

// game_ui_enable
// 0x443C30
void gameUiEnable()
{
    if (gGameUiDisabled) {
        interfaceBarEnable();
        keyboardEnable();
        keyboardReset();
        _gmouse_enable();
        gameMouseObjectsShow();
        gGameUiDisabled = false;
    }
}

// game_ui_is_disabled
// 0x443C60
bool gameUiIsDisabled()
{
    return gGameUiDisabled;
}

// 0x443F74
void showHelp()
{
    ScopedGameMode gm(GameMode::kHelp);

    bool isoWasEnabled = isoDisable();
    bool gameMouseWasVisible;
    if (isoWasEnabled) {
        gameMouseWasVisible = gameMouseObjectsIsVisible();
    } else {
        gameMouseWasVisible = false;
    }

    if (gameMouseWasVisible) {
        gameMouseObjectsHide();
    }

    gameMouseSetCursor(MOUSE_CURSOR_NONE);

    bool colorCycleWasEnabled = colorCycleEnabled();
    colorCycleDisable();

    // CE: Help screen uses separate color palette which is incompatible with
    // colors in other windows. Setup overlay to hide everything.
    int overlay = windowCreate(0, 0, screenGetWidth(), screenGetHeight(), COLOR_FIRST, WINDOW_HIDDEN | WINDOW_MOVE_ON_TOP);

    int helpWindowX = (screenGetWidth() - HELP_SCREEN_WIDTH) / 2;
    int helpWindowY = (screenGetHeight() - HELP_SCREEN_HEIGHT) / 2;
    int win = windowCreate(helpWindowX, helpWindowY, HELP_SCREEN_WIDTH, HELP_SCREEN_HEIGHT, COLOR_FIRST, WINDOW_HIDDEN | WINDOW_MOVE_ON_TOP);
    if (win != -1) {
        unsigned char* windowBuffer = windowGetBuffer(win);
        if (windowBuffer != nullptr) {
            FrmImage backgroundFrmImage;
            if (backgroundFrmImage.lock(InterfaceFrameId::HelpBackground)) {
                paletteSetEntries(gPaletteBlack);
                blitBufferToBuffer(backgroundFrmImage.getData(), HELP_SCREEN_WIDTH, HELP_SCREEN_HEIGHT, HELP_SCREEN_WIDTH, windowBuffer, HELP_SCREEN_WIDTH);

                colorPaletteLoad("art\\intrface\\helpscrn.pal");
                paletteSetEntries(_cmap);

                // CE: Fill overlay with darkest color in the palette. It might
                // not be completely black, but at least it's uniform.
                bufferFill(windowGetBuffer(overlay),
                    screenGetWidth(),
                    screenGetHeight(),
                    screenGetWidth(),
                    intensityColorTable[COLOR_BLACK][0]);

                windowShow(overlay);
                windowShow(win);

                while (inputGetInput() == -1 && _game_user_wants_to_quit == GAME_QUIT_REQUEST_NONE) {
                    sharedFpsLimiter.mark();
                    renderPresent();
                    sharedFpsLimiter.throttle();
                }

                while (mouseGetEvent() != 0) {
                    sharedFpsLimiter.mark();

                    inputGetInput();

                    renderPresent();
                    sharedFpsLimiter.throttle();
                }

                paletteSetEntries(gPaletteBlack);
            }
        }

        windowDestroy(overlay);
        windowDestroy(win);
        colorPaletteLoad("color.pal");
        paletteSetEntries(_cmap);
    }

    if (colorCycleWasEnabled) {
        colorCycleEnable();
    }

    if (gameMouseWasVisible) {
        gameMouseObjectsShow();
    }

    if (isoWasEnabled) {
        isoEnable();
    }
}

// 0x4440B8
int showQuitConfirmationDialog()
{
    bool isoWasEnabled = isoDisable();

    bool gameMouseWasVisible;
    if (isoWasEnabled) {
        gameMouseWasVisible = gameMouseObjectsIsVisible();
    } else {
        gameMouseWasVisible = false;
    }

    if (gameMouseWasVisible) {
        gameMouseObjectsHide();
    }

    bool cursorWasHidden = cursorIsHidden();
    if (cursorWasHidden) {
        mouseShowCursor();
    }

    MouseCursorType oldCursor = gameMouseGetCursor();
    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    int rc;

    // Are you sure you want to quit?
    MessageListItem messageListItem;
    messageListItem.num = 0;
    if (messageListGetItem(&gMiscMessageList, &messageListItem)) {
        rc = showDialogBox(messageListItem.text, nullptr, 0, 169, 117, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO);
        if (rc != 0) {
            _game_user_wants_to_quit = GAME_QUIT_REQUEST_MAIN_MENU;
        }
    } else {
        rc = -1;
    }

    gameMouseSetCursor(oldCursor);

    if (cursorWasHidden) {
        mouseHideCursor();
    }

    if (gameMouseWasVisible) {
        gameMouseObjectsShow();
    }

    if (isoWasEnabled) {
        isoEnable();
    }

    return rc;
}

// 0x444384
void showSplash()
{
    int splash = settings.system.splash;

    char path[64];
    const char* language = settings.system.language.c_str();
    if (compat_stricmp(language, ENGLISH) != 0) {
        snprintf(path, sizeof(path), "art\\%s\\splash\\", language);
    } else {
        snprintf(path, sizeof(path), "art\\splash\\");
    }

    File* stream;
    for (int index = 0; index < SPLASH_COUNT; index++) {
        char filePath[64];
        snprintf(filePath, sizeof(filePath), "%ssplash%d.rix", path, splash);
        stream = fileOpen(filePath, "rb");
        if (stream != nullptr) {
            break;
        }

        splash++;

        if (splash >= SPLASH_COUNT) {
            splash = 0;
        }
    }

    if (stream == nullptr) {
        return;
    }

    unsigned char* palette = reinterpret_cast<unsigned char*>(internal_malloc(768));
    if (palette == nullptr) {
        fileClose(stream);
        return;
    }

    int version;
    fileReadInt32(stream, &version);
    if (memcmp(&version, "RIX3", 4) != 0) {
        fileClose(stream);
        return;
    }

    short width;
    fileRead(&width, sizeof(width), 1, stream);

    short height;
    fileRead(&height, sizeof(height), 1, stream);

    unsigned char* data = reinterpret_cast<unsigned char*>(internal_malloc(width * height));
    if (data == nullptr) {
        internal_free(palette);
        fileClose(stream);
        return;
    }

    paletteSetEntries(gPaletteBlack);
    fileSeek(stream, 10, SEEK_SET);
    fileRead(palette, 1, 768, stream);
    fileRead(data, 1, width * height, stream);
    fileClose(stream);

    // Fix of wrong Palette, without it this makes background bright
    // Basically just swapping first and last colors, this problem presented ONLY in F2, F1 has right palette in every splash
    memcpy(palette + (255 * 3), palette, 3);
    memset(palette, 0, 3);

    for (int i = 0; i < width * height; i++) {
        if (data[i] == 0) {
            data[i] = 255;
        } else if (data[i] == 255) {
            data[i] = 0;
        }
    }

    int size = settings.ui.splash_screen_size;

    int screenWidth = screenGetWidth();
    int screenHeight = screenGetHeight();

    if (size != 0 || screenWidth < width || screenHeight < height) {
        int scaledWidth;
        int scaledHeight;

        if (size == 2) {
            scaledWidth = screenWidth;
            scaledHeight = screenHeight;
        } else {
            if (screenHeight * width >= screenWidth * height) {
                scaledWidth = screenWidth;
                scaledHeight = screenWidth * height / width;
            } else {
                scaledWidth = screenHeight * width / height;
                scaledHeight = screenHeight;
            }
        }

        unsigned char* scaled = reinterpret_cast<unsigned char*>(internal_malloc(static_cast<size_t>(scaledWidth) * scaledHeight));
        if (scaled != nullptr) {
            blitBufferToBufferStretch(data, width, height, width, scaled, scaledWidth, scaledHeight, scaledWidth);

            int x = screenWidth > scaledWidth ? (screenWidth - scaledWidth) / 2 : 0;
            int y = screenHeight > scaledHeight ? (screenHeight - scaledHeight) / 2 : 0;
            _scr_blit(scaled, scaledWidth, scaledHeight, 0, 0, scaledWidth, scaledHeight, x, y);
            paletteFadeTo(palette);

            internal_free(scaled);
        }
    } else {
        int x = (screenWidth - width) / 2;
        int y = (screenHeight - height) / 2;
        _scr_blit(data, width, height, 0, 0, width, height, x, y);
        paletteFadeTo(palette);
    }

    internal_free(data);
    internal_free(palette);

    settings.system.splash = splash + 1;
}

int gameShowDeathDialog(const char* message)
{
    bool isoWasEnabled = isoDisable();

    bool gameMouseWasVisible;
    if (isoWasEnabled) {
        gameMouseWasVisible = gameMouseObjectsIsVisible();
    } else {
        gameMouseWasVisible = false;
    }

    if (gameMouseWasVisible) {
        gameMouseObjectsHide();
    }

    bool cursorWasHidden = cursorIsHidden();
    if (cursorWasHidden) {
        mouseShowCursor();
    }

    MouseCursorType oldCursor = gameMouseGetCursor();
    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    GameQuitRequest oldUserWantsToQuit = _game_user_wants_to_quit;
    _game_user_wants_to_quit = GAME_QUIT_REQUEST_NONE;

    int rc = showDialogBox(message, nullptr, 0, 169, 117, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);

    _game_user_wants_to_quit = oldUserWantsToQuit;

    gameMouseSetCursor(oldCursor);

    if (cursorWasHidden) {
        mouseHideCursor();
    }

    if (gameMouseWasVisible) {
        gameMouseObjectsShow();
    }

    if (isoWasEnabled) {
        isoEnable();
    }

    return rc;
}

} // namespace fallout
