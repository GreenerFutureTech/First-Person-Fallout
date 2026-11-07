#include "loot_highlight.h"
#include "savegame.h"
#include "sfall_config.h"
#include "player_sheet.h"
#include "loadsave.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>

#include "art.h"
#include "automap.h"
#include "ce_save_game.h"
#include "character_editor.h"
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
#include "config.h"
#include "critter.h"
#include "cycle.h"
#include "db.h"
#include "dbox.h"
#include "debug.h"
#include "delay.h"
#include "display_monitor.h"
#include "draw.h"
#include "file_utils.h"
#include "game.h"
#include "game_mouse.h"
#include "game_movie.h"
#include "game_sound.h"
#include "geometry.h"
#include "input.h"
#include "interface.h"
#include "item.h"
#include "kb.h"
#include "mainmenu.h"
#include "map.h"
#include "memory.h"
#include "message.h"
#include "mouse.h"
#include "object.h"
#include "palette.h"
#include "party_member.h"
#include "perk.h"
#include "pipboy.h"
#include "platform_compat.h"
#include "preferences.h"
#include "proto.h"
#include "queue.h"
#include "random.h"
#include "scripts.h"
#include "settings.h"
#include "sfall_callbacks.h"
#include "sfall_ext.h"
#include "sfall_global_scripts.h"
#include "sfall_global_vars.h"
#include "skill.h"
#include "stat.h"
#include "string_utils.h"
#include "svga.h"
#include "text_font.h"
#include "tile.h"
#include "trait.h"
#include "version.h"
#include "window_manager.h"
#include "word_wrap.h"
#include "worldmap.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace fallout {

static void lsgDisplayMessage(int messageId);


#define LOAD_SAVE_SIGNATURE "FALLOUT SAVE FILE"

#define LSGAME_MSG_NAME "LSGAME.MSG"

#define LS_WINDOW_WIDTH 640
#define LS_WINDOW_HEIGHT 480

#define LS_PREVIEW_WIDTH 224
#define LS_PREVIEW_HEIGHT 133
#define LS_PREVIEW_SIZE ((LS_PREVIEW_WIDTH) * (LS_PREVIEW_HEIGHT))

#define LS_COMMENT_WINDOW_X 169
#define LS_COMMENT_WINDOW_Y 116

// NOTE: The following are "normalized" path components for "proto/critters" and
// "proto/items". The original code does not use uniform case for them (as
// opposed to other path components like MAPS, SAVE.DAT, etc). It does not have
// effect on Windows, but it's important on Linux and Mac, where filesystem is
// case-sensitive. Lowercase is preferred as it is used in other parts of the
// codebase (see `protoInit`, `gArtListDescriptions`).

#define PROTO_DIR_NAME "proto"
#define CRITTERS_DIR_NAME "critters"
#define ITEMS_DIR_NAME "items"
#define PROTO_FILE_EXT "pro"




typedef enum LoadSaveWindowType {
    LOAD_SAVE_WINDOW_TYPE_SAVE_GAME,
    LOAD_SAVE_WINDOW_TYPE_PICK_QUICK_SAVE_SLOT,
    LOAD_SAVE_WINDOW_TYPE_LOAD_GAME,
    LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU,
    LOAD_SAVE_WINDOW_TYPE_PICK_QUICK_LOAD_SLOT,
} LoadSaveWindowType;

typedef enum LoadSaveSlotState {
    SLOT_STATE_EMPTY,
    SLOT_STATE_OCCUPIED,
    SLOT_STATE_ERROR,
    SLOT_STATE_UNSUPPORTED_VERSION,
} LoadSaveSlotState;

typedef enum LoadSaveScrollDirection {
    LOAD_SAVE_SCROLL_DIRECTION_NONE,
    LOAD_SAVE_SCROLL_DIRECTION_UP,
    LOAD_SAVE_SCROLL_DIRECTION_DOWN,
} LoadSaveScrollDirection;

static void loadSaveMessageListReset();



typedef enum LoadSaveFrm {
    LOAD_SAVE_FRM_BACKGROUND,
    LOAD_SAVE_FRM_BOX,
    LOAD_SAVE_FRM_PREVIEW_COVER,
    LOAD_SAVE_FRM_RED_BUTTON_PRESSED,
    LOAD_SAVE_FRM_RED_BUTTON_NORMAL,
    LOAD_SAVE_FRM_ARROW_DOWN_NORMAL,
    LOAD_SAVE_FRM_ARROW_DOWN_PRESSED,
    LOAD_SAVE_FRM_ARROW_UP_NORMAL,
    LOAD_SAVE_FRM_ARROW_UP_PRESSED,
    LOAD_SAVE_FRM_COUNT,
} LoadSaveFrm;

static int _QuickSnapShot();
static int lsgWindowInit(int windowType);
static int lsgWindowFree(int windowType);




static int _GetSlotList();
static void loadSaveLoadSlotPage(int page);
static void _ShowSlotList(int windowType);
static void _DrawInfoBox(int slot);
static int _LoadTumbSlot(int slot);
static int _GetComment(int slot);
static int _get_input_str2(int win, int doneKeyCode, int cancelKeyCode, char* description, int maxLength, int x, int y, ColorWithFlags textColor, Color backgroundColor, int flags);













// 0x47B7C0 lsgrphs
static constexpr InterfaceFrmId kLoadSaveFrmIds[LOAD_SAVE_FRM_COUNT] = {
    InterfaceFrameId::LoadSaveGame,
    InterfaceFrameId::LoadSaveBox,
    InterfaceFrameId::LoadSaveCover,
    InterfaceFrameId::LittleRedButtonDown,
    InterfaceFrameId::LittleRedButtonUp,
    InterfaceFrameId::CharacterEditorDownArrowOff,
    InterfaceFrameId::CharacterEditorDownArrowOn,
    InterfaceFrameId::CharacterEditorUpArrowOff,
    InterfaceFrameId::CharacterEditorUpArrowOn,
};

// Control max number of save/load pages
constexpr int saveLoadPages = 100;
constexpr int slotsPerPage = 10;
constexpr int saveLoadTotalSlots = kManualSaveSlotCount;
static_assert(saveLoadTotalSlots == saveLoadPages * slotsPerPage);
static const char* _patches = nullptr;
constexpr int kLoadSaveActionDone = 500;
constexpr int kLoadSaveFastPageStep = 10;

// Global variable to track the current slot page
static int _currentSlotPage = 0;

// 0x5193B8 slot_cursor
static int _slot_cursor = 0;

static int gDevLoadGameSlot = -1;

// 0x5193BC quick_done
static bool _quick_done = false;

// 0x5193C0 bk_enable_3
static bool gLoadSaveWindowIsoWasEnabled = false;

// 0x5193C4 map_backup_count


// 0x5193C8 automap_db_flag


// 0x5193CC patches


// 0x5193EC master_save_list


// 0x519458 master_load_list


// 0x5194C4 loadingGame




// lsgame.msg
//
// 0x613D28 lsgame_msgfl
static MessageList gLoadSaveMessageList;

// 0x613D30 LSData


// 0x614280 LSstatus
static int _LSstatus[saveLoadTotalSlots];
static bool gLoadSaveSlotPageLoaded[saveLoadPages];

// 0x6142A8 thumbnail_image
static unsigned char* _thumbnail_image;

// 0x6142AC snapshotBuf
static unsigned char* _snapshotBuf;

// 0x6142B0 lsgmesg
static MessageListItem gLoadSaveMessageListItem;

// 0x6142C0 dbleclkcntr
static int _dbleclkcntr;

// 0x6142C4 lsgwin
static int gLoadSaveWindow;

// 0x6142EC snapshot
static unsigned char* _snapshot;

// 0x6142F0 str2
static char _str2[COMPAT_MAX_PATH];

// 0x6143F4 str0
static char _str0[COMPAT_MAX_PATH];

// 0x6144F8 str1
static char _str1[COMPAT_MAX_PATH];

// 0x6145FC str
static char _str[COMPAT_MAX_PATH];

static void loadSaveMessageListReset()
{
    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_LSGAME, nullptr);
    messageListFree(&gLoadSaveMessageList);
}

// 0x614700 lsgbuf
static unsigned char* gLoadSaveWindowBuffer;

// 0x614704 gmpath
static char _gmpath[COMPAT_MAX_PATH];

// 0x614808 flptr
static File* _flptr;

// 0x61480C ls_error_code


// 0x614810 fontsave_2
static int gLoadSaveWindowOldFont;

static FrmImage _loadsaveFrmImages[LOAD_SAVE_FRM_COUNT];

static int quickSaveSlots = 0;
static bool autoQuickSaveSlots = false;

static constexpr char kLoadSaveSlotDataSection[] = "POSITION";
static constexpr char kLoadSaveSlotDataKey[] = "CurrentSlot";
static constexpr char kLoadSaveSlotDataFile[] = "SAVEGAME\\slotdat.ini";

static void loadSaveRememberSelectedSlot()
{
    assert(_patches != nullptr);

    _slot_cursor = 0;
    _currentSlotPage = 0;

    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s", _patches, kLoadSaveSlotDataFile);

    int slot = 0;
    if (ScopedConfig config { path, false }; config) {
        configGetInt(config.get(), kLoadSaveSlotDataSection, kLoadSaveSlotDataKey, &slot, 0);
    }

    _slot_cursor = std::clamp(slot, 0, saveLoadTotalSlots - 1);
    _currentSlotPage = _slot_cursor / slotsPerPage;
}

static void loadSaveSetCurrentPage(int page)
{
    int slotIndex = _slot_cursor % slotsPerPage;

    _currentSlotPage = std::clamp(page, 0, saveLoadPages - 1);
    _slot_cursor = std::min(_currentSlotPage * slotsPerPage + slotIndex, saveLoadTotalSlots - 1);
}

static int loadSavePageStep()
{
    return (SDL_GetModState() & KMOD_SHIFT) != 0 ? kLoadSaveFastPageStep : 1;
}

static int loadSaveNavigationY()
{
    int startIndex = _currentSlotPage * slotsPerPage;
    int visibleSlotCount = std::min(slotsPerPage, saveLoadTotalSlots - startIndex);
    return 87 + visibleSlotCount * (3 * fontGetLineHeight() + 4);
}

static int loadSavePageDeltaForInput(int keyCode, int mouseX, int mouseY)
{
    if (keyCode == KEY_ARROW_LEFT) {
        return -loadSavePageStep();
    }

    if (keyCode == KEY_ARROW_RIGHT) {
        return loadSavePageStep();
    }

    int navigationY = loadSaveNavigationY();
    if (mouseY < navigationY || mouseY >= navigationY + fontGetLineHeight()) {
        return 0;
    }

    // "<<" button: move back 10 pages.
    if (mouseX >= 55 && mouseX <= 85) {
        return -kLoadSaveFastPageStep;
    }

    // "BACK" button: move back one page.
    if (mouseX >= 86 && mouseX <= 180) {
        return -1;
    }

    // "MORE" button: move forward one page.
    if (mouseX >= 195 && mouseX <= 250) {
        return 1;
    }

    // ">>" button: move forward 10 pages.
    if (mouseX >= 251 && mouseX <= 285) {
        return kLoadSaveFastPageStep;
    }

    return 0;
}

static bool loadSaveChangePage(int delta, LoadSaveWindowType windowType)
{
    int page = std::clamp(_currentSlotPage + delta, 0, saveLoadPages - 1);
    if (page == _currentSlotPage) {
        return false;
    }

    soundPlayFile("ib1p1xx1");
    loadSaveSetCurrentPage(page);
    _ShowSlotList(windowType);
    windowRefresh(gLoadSaveWindow);
    return true;
}

static void loadSavePersistSelectedSlot()
{
    assert(_patches != nullptr);

    char savegamePath[COMPAT_MAX_PATH];
    snprintf(savegamePath, sizeof(savegamePath), "%s\\SAVEGAME", _patches);
    compat_mkdir(savegamePath);

    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s", _patches, kLoadSaveSlotDataFile);

    ScopedConfig config { path, false };
    if (!config.get()->isInitialized()) {
        return;
    }

    configSetInt(config.get(), kLoadSaveSlotDataSection, kLoadSaveSlotDataKey, _slot_cursor);
    configWrite(config.get(), path, false);
}

// 0x47B7E4
void _InitLoadSave()
{
    _quick_done = false;
    _patches = settings.system.master_patches_path.c_str();
    loadSaveRememberSelectedSlot();
    savegameRefreshPatchesPath();

    MapDirErase("MAPS\\", "SAV");
    MapDirErase(PROTO_DIR_NAME "\\" CRITTERS_DIR_NAME "\\", PROTO_FILE_EXT);
    MapDirErase(PROTO_DIR_NAME "\\" ITEMS_DIR_NAME "\\", PROTO_FILE_EXT);

    quickSaveSlots = settings.ui.auto_quick_save;
    if (quickSaveSlots > 0 && quickSaveSlots <= saveLoadTotalSlots) {
        autoQuickSaveSlots = true;
    }
}

// 0x47B85C


// SaveGame
// 0x47B88C
int lsgSaveGame(int mode)
{
    ScopedGameMode gm(GameMode::kSaveGame);

    MessageListItem messageListItem;

    // Drop any highlight-lootables outlines so they are not written into the
    // object stream (obj->outline is serialized).
    lootHighlightClear();

    savegameResetErrorCode();
    savegameRefreshPatchesPath();

    // SFALL: skip slot selection if auto quicksave is enabled
    if (autoQuickSaveSlots) {
        _quick_done = true;
    }

    if (mode == LOAD_SAVE_MODE_QUICK && _quick_done) {
        // SFALL: cycle through first N slots for quicksaving
        if (autoQuickSaveSlots) {
            if (++_slot_cursor >= quickSaveSlots) {
                _slot_cursor = 0;
            }
        }
        snprintf(_gmpath, sizeof(_gmpath), "%s\\%s%.2d\\", "SAVEGAME", "SLOT", _slot_cursor + 1);
        strcat(_gmpath, "SAVE.DAT");

        _flptr = fileOpen(_gmpath, "rb");
        if (_flptr != nullptr) {
            savegameSetSlot(_slot_cursor);
            lsgLoadHeaderInSlot(_flptr, _slot_cursor);
            fileClose(_flptr);
        }

        if (!messageListInit(&gLoadSaveMessageList)) {
            return -1;
        }

        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%s%s", gGameMsgPath, "LSGAME.MSG");
        if (!messageListLoad(&gLoadSaveMessageList, path)) {
            return -1;
        }
        messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_LSGAME, &gLoadSaveMessageList);

        _snapshotBuf = nullptr;
        savegameSetPreviewBuffer(_snapshotBuf);
        int v6 = _QuickSnapShot();
        if (v6 == 1) {
            savegameSetSlot(_slot_cursor);
            int v7 = lsgPerformSaveGame();
            if (v7 != -1) {
                v6 = v7;
                lsgDisplayMessage(140);
            }
        }

        if (_snapshotBuf != nullptr) {
            internal_free(_snapshot);
        }

        gameMouseSetCursor(MOUSE_CURSOR_ARROW);

        if (v6 != -1) {
            loadSaveMessageListReset();
            return 1;
        }

        soundPlayFile("iisxxxx1");

        // Error saving game!
        stringCopy(_str0, getmsg(&gLoadSaveMessageList, &messageListItem, 132));
        // Unable to save game.
        stringCopy(_str1, getmsg(&gLoadSaveMessageList, &messageListItem, 133));

        const char* body[] = {
            _str1,
        };
        showDialogBox(_str0, body, 1, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);

        loadSaveMessageListReset();

        return -1;
    }

    touch_set_touchscreen_mode(mode == LOAD_SAVE_MODE_NORMAL);

    _quick_done = false;

    int windowType = mode == LOAD_SAVE_MODE_QUICK
        ? LOAD_SAVE_WINDOW_TYPE_PICK_QUICK_SAVE_SLOT
        : LOAD_SAVE_WINDOW_TYPE_SAVE_GAME;
    if (lsgWindowInit(windowType) == -1) {
        debugPrint("\nLOADSAVE: ** Error loading save game screen data! **\n");
        return -1;
    }

    pipboyMessageListInit();

    if (_GetSlotList() == -1) {
        windowRefresh(gLoadSaveWindow);

        soundPlayFile("iisxxxx1");

        // Error loading save game list!
        stringCopy(_str0, getmsg(&gLoadSaveMessageList, &messageListItem, 106));
        // Save game directory:
        stringCopy(_str1, getmsg(&gLoadSaveMessageList, &messageListItem, 107));

        snprintf(_str2, sizeof(_str2), "\"%s\\\"", "SAVEGAME");

        // TODO: Check.
        stringCopy(_str2, getmsg(&gLoadSaveMessageList, &messageListItem, 108));

        const char* body[] = {
            _str1,
            _str2,
        };
        showDialogBox(_str0, body, 2, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);

        lsgWindowFree(0);

        return -1;
    }

    switch (_LSstatus[_slot_cursor]) {
    case SLOT_STATE_EMPTY:
    case SLOT_STATE_ERROR:
    case SLOT_STATE_UNSUPPORTED_VERSION:
        blitBufferToBuffer(_snapshotBuf,
            LS_PREVIEW_WIDTH - 1,
            LS_PREVIEW_HEIGHT - 1,
            LS_PREVIEW_WIDTH,
            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 58 + 366,
            LS_WINDOW_WIDTH);
        break;
    default:
        _LoadTumbSlot(_slot_cursor);
        blitBufferToBuffer(_thumbnail_image,
            LS_PREVIEW_WIDTH - 1,
            LS_PREVIEW_HEIGHT - 1,
            LS_PREVIEW_WIDTH,
            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 58 + 366,
            LS_WINDOW_WIDTH);
        break;
    }

    _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_SAVE_GAME);
    _DrawInfoBox(_slot_cursor);
    windowRefresh(gLoadSaveWindow);

    _dbleclkcntr = 24;

    int rc = -1;
    int doubleClickSlot = -1;
    while (rc == -1) {
        sharedFpsLimiter.mark();

        unsigned int tick = getTicks();
        int keyCode = inputGetInput();
        bool selectionChanged = false;
        int scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_NONE;

        convertMouseWheelToArrowKey(&keyCode);

        if (keyCode == KEY_ESCAPE || keyCode == 501 || _game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            rc = 0;
        } else {
            switch (keyCode) {
            case KEY_ARROW_UP:
                if (_slot_cursor > 0) { // Prevent going below 0
                    if (_slot_cursor % 10 == 0 && _currentSlotPage > 0) {
                        // Move to the previous page and set cursor to the last slot on that page
                        _currentSlotPage--;
                        _slot_cursor--;
                        _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_SAVE_GAME);
                        windowRefresh(gLoadSaveWindow);
                    } else {
                        // Normal movement within the page
                        _slot_cursor--;
                    }
                }

                selectionChanged = true;
                doubleClickSlot = -1;
                break;

            case KEY_ARROW_DOWN:
                if (_slot_cursor < (saveLoadTotalSlots - 1)) {
                    if (_slot_cursor % 10 == 9 && _currentSlotPage < (saveLoadTotalSlots / 10) - 1) {
                        // Move to the next page and set cursor to the first slot on that page
                        _currentSlotPage++;
                        _slot_cursor++;
                        _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_SAVE_GAME);
                        windowRefresh(gLoadSaveWindow);
                    } else {
                        // Normal movement within the page
                        _slot_cursor++;
                    }
                }

                selectionChanged = true;
                doubleClickSlot = -1;
                break;
            case KEY_HOME:
                // Move to the first slot of the current page
                _slot_cursor = _currentSlotPage * 10;
                selectionChanged = true;
                doubleClickSlot = -1;
                break;

            case KEY_END:
                // Move to the last slot of the current page
                _slot_cursor = (_currentSlotPage * 10) + 9;

                // Prevent overflow on the last page
                if (_slot_cursor > (saveLoadTotalSlots - 1)) {
                    _slot_cursor = (saveLoadTotalSlots - 1);
                }

                selectionChanged = true;
                doubleClickSlot = -1;
                break;
            case 506:
                scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_UP;
                break;

            case 504:
                scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_DOWN;
                break;

            case KEY_ARROW_RIGHT:
            case KEY_ARROW_LEFT:
            case 502: { // Mouse click detected
                int mouseX, mouseY;
                mouseGetPositionInWindow(gLoadSaveWindow, &mouseX, &mouseY);

                int pageDelta = loadSavePageDeltaForInput(keyCode, mouseX, mouseY);
                if (pageDelta != 0) {
                    if (loadSaveChangePage(pageDelta, LOAD_SAVE_WINDOW_TYPE_SAVE_GAME)) {
                        selectionChanged = true;
                        doubleClickSlot = -1;
                    }
                    break;
                }

                // Calculate the clicked slot, adjusting for pagination
                int relativeSlot = (mouseY - 79) / (3 * fontGetLineHeight() + 4);
                if (relativeSlot < 0) {
                    relativeSlot = 0;
                } else if (relativeSlot > 9) {
                    relativeSlot = 9;
                }

                // Adjust for the current page
                int clickedSlot = (_currentSlotPage * 10) + relativeSlot;

                if (clickedSlot > (saveLoadTotalSlots - 1)) { // Ensure we don't go beyond max slots
                    clickedSlot = (saveLoadTotalSlots - 1);
                }

                _slot_cursor = clickedSlot;
                if (clickedSlot == doubleClickSlot) {
                    keyCode = kLoadSaveActionDone;
                    soundPlayFile("ib1p1xx1");
                }

                selectionChanged = true;
                doubleClickSlot = _slot_cursor;
                scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_NONE;
            } break;

            case KEY_CTRL_Q:
            case KEY_CTRL_X:
            case KEY_F10:
                showQuitConfirmationDialog();

                if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
                    rc = 0;
                }
                break;
            case KEY_PLUS:
            case KEY_EQUAL:
                brightnessIncrease();
                break;
            case KEY_MINUS:
            case KEY_UNDERSCORE:
                brightnessDecrease();
                break;
            case KEY_RETURN:
                keyCode = kLoadSaveActionDone;
                break;
            }
        }

        if (keyCode == kLoadSaveActionDone) {
            if (_LSstatus[_slot_cursor] == SLOT_STATE_OCCUPIED) {
                rc = 1;
                // Save game already exists, overwrite?
                const char* title = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 131);
                if (showDialogBox(title, nullptr, 0, 169, 131, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_YES_NO) == 0) {
                    rc = -1;
                }
            } else {
                rc = 1;
            }

            selectionChanged = true;
            scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_NONE;
        }

        if (scrollDirection) {
            unsigned int scrollVelocity = 4;
            bool isScrolling = false;
            int scrollCounter = 0;
            do {
                sharedFpsLimiter.mark();

                unsigned int start = getTicks();
                scrollCounter += 1;

                if ((!isScrolling && scrollCounter == 1) || (isScrolling && scrollCounter > 14.4)) {
                    isScrolling = true;

                    if (scrollCounter > 14.4) {
                        scrollVelocity += 1;
                        if (scrollVelocity > 24) {
                            scrollVelocity = 24;
                        }
                    }
                    // handle scrolling between pages via buttons
                    if (scrollDirection == LOAD_SAVE_SCROLL_DIRECTION_UP) {
                        _slot_cursor--;

                        // If moving up past the first slot of the page, go to the previous page
                        if (_slot_cursor < _currentSlotPage * 10) {
                            if (_currentSlotPage > 0) {
                                _currentSlotPage--;
                                _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
                                windowRefresh(gLoadSaveWindow);
                                _slot_cursor = (_currentSlotPage * 10) + 9; // Move to the last slot of the previous page
                            } else {
                                _slot_cursor = 0; // Prevent underflow
                            }
                        }
                    } else { // LOAD_SAVE_SCROLL_DIRECTION_DOWN
                        _slot_cursor++;

                        // If moving down past the last slot of the page, go to the next page
                        if (_slot_cursor > (_currentSlotPage * 10) + 9) {
                            if (_currentSlotPage < (saveLoadTotalSlots / 10) - 1) { // Max pages: 0-9
                                _currentSlotPage++;
                                _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
                                windowRefresh(gLoadSaveWindow);
                                _slot_cursor = _currentSlotPage * 10; // Move to the first slot of the next page
                            } else {
                                _slot_cursor = (saveLoadTotalSlots - 1); // Prevent overflow (last slot overall)
                            }
                        }
                    }

                    // TODO: Does not check for unsupported version error like
                    // other switches do.
                    // fixed to match load screen 'thumbnail'/'blank' updating
                    switch (_LSstatus[_slot_cursor]) {
                    case SLOT_STATE_EMPTY:
                    case SLOT_STATE_ERROR:
                        blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getData(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                            LS_WINDOW_WIDTH);
                        break;
                    default:
                        _LoadTumbSlot(_slot_cursor);
                        blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_BACKGROUND].getData() + LS_WINDOW_WIDTH * 39 + 340,
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                            LS_WINDOW_WIDTH,
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                            LS_WINDOW_WIDTH);
                        blitBufferToBuffer(_thumbnail_image,
                            LS_PREVIEW_WIDTH - 1,
                            LS_PREVIEW_HEIGHT - 1,
                            LS_PREVIEW_WIDTH,
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 58 + 366,
                            LS_WINDOW_WIDTH);
                        break;
                    }

                    _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_SAVE_GAME);
                    _DrawInfoBox(_slot_cursor);
                    windowRefresh(gLoadSaveWindow);
                }

                if (scrollCounter > 14.4) {
                    delay_ms(1000 / scrollVelocity - (getTicks() - start));
                } else {
                    delay_ms(1000 / 24 - (getTicks() - start));
                }

                keyCode = inputGetInput();

                renderFpsCounter();
                renderPresent();
                sharedFpsLimiter.throttle();
            } while (keyCode != 505 && keyCode != 503);
        } else {
            if (selectionChanged) {
                // fixed to match load screen 'thumbnail'/'blank' updating
                switch (_LSstatus[_slot_cursor]) {
                case SLOT_STATE_EMPTY:
                case SLOT_STATE_ERROR:
                case SLOT_STATE_UNSUPPORTED_VERSION:
                    blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getData(),
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                        gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                        LS_WINDOW_WIDTH);
                    break;
                default:
                    _LoadTumbSlot(_slot_cursor);
                    blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_BACKGROUND].getData() + LS_WINDOW_WIDTH * 39 + 340,
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                        LS_WINDOW_WIDTH,
                        gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                        LS_WINDOW_WIDTH);
                    blitBufferToBuffer(_thumbnail_image,
                        LS_PREVIEW_WIDTH - 1,
                        LS_PREVIEW_HEIGHT - 1,
                        LS_PREVIEW_WIDTH,
                        gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 58 + 366,
                        LS_WINDOW_WIDTH);
                    break;
                }

                _DrawInfoBox(_slot_cursor);
                _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_SAVE_GAME);
            }

            windowRefresh(gLoadSaveWindow);

            _dbleclkcntr -= 1;
            if (_dbleclkcntr == 0) {
                _dbleclkcntr = 24;
                doubleClickSlot = -1;
            }

            delay_ms(1000 / 24 - (getTicks() - tick));
        }

        if (rc == 1) {
            int v50 = _GetComment(_slot_cursor);
            if (v50 == -1) {
                gameMouseSetCursor(MOUSE_CURSOR_ARROW);
                soundPlayFile("iisxxxx1");
                debugPrint("\nLOADSAVE: ** Error getting save file comment **\n");

                // Error saving game!
                stringCopy(_str0, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 132));
                // Unable to save game.
                stringCopy(_str1, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 133));

                const char* body[1] = {
                    _str1,
                };
                showDialogBox(_str0, body, 1, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);
                rc = -1;
            } else if (v50 == 0) {
                gameMouseSetCursor(MOUSE_CURSOR_ARROW);
                rc = -1;
            } else if (v50 == 1) {
                savegameSetSlot(_slot_cursor);
                int saveRc = lsgPerformSaveGame();
                if (saveRc != -1) {
                    lsgDisplayMessage(140);
                }
                if (saveRc == -1) {
                    gameMouseSetCursor(MOUSE_CURSOR_ARROW);
                    soundPlayFile("iisxxxx1");

                    // Error saving game!
                    stringCopy(_str0, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 132));
                    // Unable to save game.
                    stringCopy(_str1, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 133));

                    rc = -1;

                    const char* body[1] = {
                        _str1,
                    };
                    showDialogBox(_str0, body, 1, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);

                    if (_GetSlotList() == -1) {
                        windowRefresh(gLoadSaveWindow);
                        soundPlayFile("iisxxxx1");

                        // Error loading save agme list!
                        stringCopy(_str0, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 106));
                        // Save game directory:
                        stringCopy(_str1, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 107));

                        snprintf(_str2, sizeof(_str2), "\"%s\\\"", "SAVEGAME");

                        char text[260];
                        // Doesn't exist or is corrupted.
                        stringCopy(text, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 107));

                        const char* body[2] = {
                            _str1,
                            _str2,
                        };
                        showDialogBox(_str0, body, 2, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);

                        lsgWindowFree(0);

                        return -1;
                    }
                    // fixed to match load screen 'thumbnail'/'blank' updating
                    switch (_LSstatus[_slot_cursor]) {
                    case SLOT_STATE_EMPTY:
                    case SLOT_STATE_ERROR:
                    case SLOT_STATE_UNSUPPORTED_VERSION:
                        blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getData(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                            LS_WINDOW_WIDTH);
                        break;
                    default:
                        _LoadTumbSlot(_slot_cursor);
                        blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_BACKGROUND].getData() + LS_WINDOW_WIDTH * 39 + 340,
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                            LS_WINDOW_WIDTH,
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                            LS_WINDOW_WIDTH);
                        blitBufferToBuffer(_thumbnail_image,
                            LS_PREVIEW_WIDTH - 1,
                            LS_PREVIEW_HEIGHT - 1,
                            LS_PREVIEW_WIDTH,
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 58 + 366,
                            LS_WINDOW_WIDTH);
                        break;
                    }

                    _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_SAVE_GAME);
                    _DrawInfoBox(_slot_cursor);
                    windowRefresh(gLoadSaveWindow);
                    _dbleclkcntr = 24;
                }
            }
        }

        renderFpsCounter();
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    lsgWindowFree(LOAD_SAVE_WINDOW_TYPE_SAVE_GAME);

    pipboyMessageListFree();

    tileWindowRefresh();

    if (mode == LOAD_SAVE_MODE_QUICK) {
        if (rc == 1) {
            _quick_done = true;
        }
    }

    return rc;
}

// 0x47C5B4
static int _QuickSnapShot()
{
    _snapshot = (unsigned char*)internal_malloc(LS_PREVIEW_SIZE);
    if (_snapshot == nullptr) {
        return -1;
    }

    bool gameMouseWasVisible = gameMouseObjectsIsVisible();
    if (gameMouseWasVisible) {
        gameMouseObjectsHide();
    }

    mouseHideCursor();
    tileWindowRefresh();
    mouseShowCursor();

    if (gameMouseWasVisible) {
        gameMouseObjectsShow();
    }

    // For preview take 640x380 area in the center of isometric window.
    Window* window = windowGetWindow(gIsoWindow);
    unsigned char* isoWindowBuffer = window->buffer
        + window->width * (window->height - ORIGINAL_ISO_WINDOW_HEIGHT) / 2
        + (window->width - ORIGINAL_ISO_WINDOW_WIDTH) / 2;
    blitBufferToBufferStretch(isoWindowBuffer,
        ORIGINAL_ISO_WINDOW_WIDTH,
        ORIGINAL_ISO_WINDOW_HEIGHT,
        windowGetWidth(gIsoWindow),
        _snapshot,
        LS_PREVIEW_WIDTH,
        LS_PREVIEW_HEIGHT,
        LS_PREVIEW_WIDTH);

    _snapshotBuf = _snapshot;
    savegameSetPreviewBuffer(_snapshotBuf);

    return 1;
}

// LoadGame
// 0x47C640
int lsgLoadGame(int mode)
{
    ScopedGameMode gm(GameMode::kLoadGame);

    MessageListItem messageListItem;

    const char* body[] = {
        _str1,
        _str2,
    };

    savegameResetErrorCode();
    savegameRefreshPatchesPath();

    if (mode == LOAD_SAVE_MODE_QUICK && _quick_done) {
        int quickSaveWindowX = (screenGetWidth() - LS_WINDOW_WIDTH) / 2;
        int quickSaveWindowY = (screenGetHeight() - LS_WINDOW_HEIGHT) / 2;
        int window = windowCreate(quickSaveWindowX,
            quickSaveWindowY,
            LS_WINDOW_WIDTH,
            LS_WINDOW_HEIGHT,
            static_cast<ColorWithFlags>(256),
            WINDOW_MODAL | WINDOW_DONT_MOVE_TOP);
        if (window != -1) {
            unsigned char* windowBuffer = windowGetBuffer(window);
            bufferFill(windowBuffer, LS_WINDOW_WIDTH, LS_WINDOW_HEIGHT, LS_WINDOW_WIDTH, COLOR_BLACK);
            windowRefresh(window);
            renderPresent();
        }

        savegameSetSlot(_slot_cursor);
        if (lsgLoadGameInSlot(_slot_cursor) != -1) {
            lsgDisplayMessage(141);
            if (window != -1) {
                windowDestroy(window);
            }
            gameMouseSetCursor(MOUSE_CURSOR_ARROW);
            return 1;
        }

        if (!messageListInit(&gLoadSaveMessageList)) {
            return -1;
        }

        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%s\\%s", gGameMsgPath, "LSGAME.MSG");
        if (!messageListLoad(&gLoadSaveMessageList, path)) {
            return -1;
        }
        messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_LSGAME, &gLoadSaveMessageList);

        if (window != -1) {
            windowDestroy(window);
        }

        gameMouseSetCursor(MOUSE_CURSOR_ARROW);
        soundPlayFile("iisxxxx1");
        stringCopy(_str0, getmsg(&gLoadSaveMessageList, &messageListItem, 134));
        stringCopy(_str1, getmsg(&gLoadSaveMessageList, &messageListItem, 135));
        showDialogBox(_str0, body, 1, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);

        loadSaveMessageListReset();
        mapNewMap();
        _game_user_wants_to_quit = GAME_QUIT_REQUEST_MAIN_MENU;

        return -1;
    }

    _quick_done = false;

    int windowType;
    switch (mode) {
    case LOAD_SAVE_MODE_FROM_MAIN_MENU:
        windowType = LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU;
        break;
    case LOAD_SAVE_MODE_NORMAL:
        windowType = LOAD_SAVE_WINDOW_TYPE_LOAD_GAME;
        break;
    case LOAD_SAVE_MODE_QUICK:
        windowType = LOAD_SAVE_WINDOW_TYPE_PICK_QUICK_LOAD_SLOT;
        break;
    default:
        assert(false && "Should be unreachable");
    }

    int devAutoloadSlot = -1;
    if (mode == LOAD_SAVE_MODE_FROM_MAIN_MENU && gDevLoadGameSlot != -1) {
        devAutoloadSlot = gDevLoadGameSlot;
        gDevLoadGameSlot = -1;
    }

    touch_set_touchscreen_mode(windowType == LOAD_SAVE_WINDOW_TYPE_LOAD_GAME || windowType == LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU);

    if (lsgWindowInit(windowType) == -1) {
        debugPrint("\nLOADSAVE: ** Error loading save game screen data! **\n");
        return -1;
    }

    pipboyMessageListInit();

    if (_GetSlotList() == -1) {
        gameMouseSetCursor(MOUSE_CURSOR_ARROW);
        windowRefresh(gLoadSaveWindow);
        renderPresent();
        if (mode == LOAD_SAVE_MODE_FROM_MAIN_MENU) {
            mainMenuShowSubscreen(true);
        }
        soundPlayFile("iisxxxx1");
        stringCopy(_str0, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 106));
        stringCopy(_str1, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 107));
        snprintf(_str2, sizeof(_str2), "\"%s\\\"", "SAVEGAME");
        showDialogBox(_str0, body, 2, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);
        lsgWindowFree(windowType);
        return -1;
    }

    bool devAutoloadPending = false;

    if (devAutoloadSlot != -1) {
        if (devAutoloadSlot >= 0 && devAutoloadSlot < saveLoadTotalSlots) {
            _slot_cursor = devAutoloadSlot;
            _currentSlotPage = devAutoloadSlot / slotsPerPage;
            loadSaveLoadSlotPage(_currentSlotPage);
            if (_LSstatus[_slot_cursor] == SLOT_STATE_OCCUPIED) {
                devAutoloadPending = true;
            } else {
                debugPrint("LOADSAVE: dev load slot %d is not occupied\n", _slot_cursor + 1);
            }
        } else {
            debugPrint("LOADSAVE: invalid dev load slot %d\n", devAutoloadSlot + 1);
        }
    }

    switch (_LSstatus[_slot_cursor]) {
    case SLOT_STATE_EMPTY:
    case SLOT_STATE_ERROR:
    case SLOT_STATE_UNSUPPORTED_VERSION:
        blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getData(),
            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
            LS_WINDOW_WIDTH);
        break;
    default:
        _LoadTumbSlot(_slot_cursor);
        blitBufferToBuffer(_thumbnail_image,
            LS_PREVIEW_WIDTH - 1,
            LS_PREVIEW_HEIGHT - 1,
            LS_PREVIEW_WIDTH,
            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 58 + 366,
            LS_WINDOW_WIDTH);
        break;
    }

    _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
    _DrawInfoBox(_slot_cursor);
    windowRefresh(gLoadSaveWindow);
    renderPresent();
    if (mode == LOAD_SAVE_MODE_FROM_MAIN_MENU) {
        mainMenuShowSubscreen(true);
    }
    _dbleclkcntr = 24;

    int rc = -1;

    int doubleClickSlot = -1;
    while (rc == -1) {
        sharedFpsLimiter.mark();

        unsigned int time = getTicks();
        int keyCode = devAutoloadPending ? kLoadSaveActionDone : inputGetInput();
        devAutoloadPending = false;
        bool selectionChanged = false;
        int scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_NONE;

        convertMouseWheelToArrowKey(&keyCode);

        if (keyCode == KEY_ESCAPE || keyCode == 501 || _game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            rc = 0;
        } else {
            switch (keyCode) {
            case KEY_ARROW_UP:
                if (_slot_cursor > 0) { // Prevent going below 0
                    if (_slot_cursor % 10 == 0 && _currentSlotPage > 0) {
                        // Move to the previous page and set cursor to the last slot on that page
                        _currentSlotPage--;
                        _slot_cursor--;
                        _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
                        windowRefresh(gLoadSaveWindow);
                    } else {
                        // Normal movement within the page
                        _slot_cursor--;
                    }
                }

                selectionChanged = true;
                doubleClickSlot = -1;
                break;

            case KEY_ARROW_DOWN:
                if (_slot_cursor < (saveLoadTotalSlots - 1)) {
                    if (_slot_cursor % 10 == 9 && _currentSlotPage < (saveLoadTotalSlots / 10) - 1) {
                        // Move to the next page and set cursor to the first slot on that page
                        _currentSlotPage++;
                        _slot_cursor++;
                        _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
                        windowRefresh(gLoadSaveWindow);
                    } else {
                        // Normal movement within the page
                        _slot_cursor++;
                    }
                }

                selectionChanged = true;
                doubleClickSlot = -1;
                break;
            case KEY_HOME:
                // Move to the first slot of the current page
                _slot_cursor = _currentSlotPage * 10;
                selectionChanged = true;
                doubleClickSlot = -1;
                break;

            case KEY_END:
                // Move to the last slot of the current page
                _slot_cursor = (_currentSlotPage * 10) + 9;

                // Prevent overflow in the last page (e.g., last page may have less than 10 slots)
                if (_slot_cursor > (saveLoadTotalSlots - 1)) {
                    _slot_cursor = (saveLoadTotalSlots - 1);
                }

                selectionChanged = true;
                doubleClickSlot = -1;
                break;
            case 506:
                scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_UP;
                break;
            case 504:
                scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_DOWN;
                break;
            case KEY_ARROW_RIGHT:
            case KEY_ARROW_LEFT:
            case 502: { // Mouse click
                int mouseX, mouseY;
                mouseGetPositionInWindow(gLoadSaveWindow, &mouseX, &mouseY);

                int pageDelta = loadSavePageDeltaForInput(keyCode, mouseX, mouseY);
                if (pageDelta != 0) {
                    if (loadSaveChangePage(pageDelta, LOAD_SAVE_WINDOW_TYPE_LOAD_GAME)) {
                        selectionChanged = true;
                        doubleClickSlot = -1;
                    }
                    break;
                }

                // Calculate the clicked slot, adjusting for pagination
                int relativeSlot = (mouseY - 79) / (3 * fontGetLineHeight() + 4);
                if (relativeSlot < 0) {
                    relativeSlot = 0;
                } else if (relativeSlot > 9) {
                    relativeSlot = 9;
                }

                // Adjust for the current page
                int clickedSlot = (_currentSlotPage * 10) + relativeSlot;

                if (clickedSlot > (saveLoadTotalSlots - 1)) { // Ensure we don't go beyond max slots
                    clickedSlot = (saveLoadTotalSlots - 1);
                }

                _slot_cursor = clickedSlot;
                if (clickedSlot == doubleClickSlot) {
                    keyCode = kLoadSaveActionDone;
                    soundPlayFile("ib1p1xx1");
                }

                selectionChanged = true;
                doubleClickSlot = _slot_cursor;
                scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_NONE;
            } break;

            case KEY_MINUS:
            case KEY_UNDERSCORE:
                brightnessDecrease();
                break;
            case KEY_EQUAL:
            case KEY_PLUS:
                brightnessIncrease();
                break;
            case KEY_RETURN:
                keyCode = kLoadSaveActionDone;
                break;
            case KEY_CTRL_Q:
            case KEY_CTRL_X:
            case KEY_F10:
                showQuitConfirmationDialog();
                if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
                    rc = 0;
                }
                break;
            }
        }

        if (keyCode == kLoadSaveActionDone) {
            if (_LSstatus[_slot_cursor] != SLOT_STATE_EMPTY) {
                rc = 1;
            } else {
                rc = -1;
            }

            selectionChanged = true;
            scrollDirection = LOAD_SAVE_SCROLL_DIRECTION_NONE;
        }

        if (scrollDirection != LOAD_SAVE_SCROLL_DIRECTION_NONE) {
            unsigned int scrollVelocity = 4;
            bool isScrolling = false;
            int scrollCounter = 0;
            do {
                sharedFpsLimiter.mark();

                unsigned int start = getTicks();
                scrollCounter += 1;

                if ((!isScrolling && scrollCounter == 1) || (isScrolling && scrollCounter > 14.4)) {
                    isScrolling = true;

                    if (scrollCounter > 14.4) {
                        scrollVelocity += 1;
                        if (scrollVelocity > 24) {
                            scrollVelocity = 24;
                        }
                    }
                    // handle scrolling between pages via buttons
                    if (scrollDirection == LOAD_SAVE_SCROLL_DIRECTION_UP) {
                        _slot_cursor--;

                        // If moving up past the first slot of the page, go to the previous page
                        if (_slot_cursor < _currentSlotPage * 10) {
                            if (_currentSlotPage > 0) {
                                _currentSlotPage--;
                                _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
                                windowRefresh(gLoadSaveWindow);
                                _slot_cursor = (_currentSlotPage * 10) + 9; // Move to the last slot of the previous page
                            } else {
                                _slot_cursor = 0; // Don't go under
                            }
                        }
                    } else { // LOAD_SAVE_SCROLL_DIRECTION_DOWN
                        soundPlayFile("ib1p1xx1");
                        _slot_cursor++;

                        // If moving down past the last slot of the page, go to the next page
                        if (_slot_cursor > (_currentSlotPage * 10) + 9) {
                            if (_currentSlotPage < (saveLoadTotalSlots / 10) - 1) { // Max pages: 0-9
                                _currentSlotPage++;
                                _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
                                windowRefresh(gLoadSaveWindow);
                                _slot_cursor = _currentSlotPage * 10; // Move to the first slot of the next page
                            } else {
                                _slot_cursor = (saveLoadTotalSlots - 1); // Prevent overflow (last slot overall)
                            }
                        }
                    }

                    switch (_LSstatus[_slot_cursor]) {
                    case SLOT_STATE_EMPTY:
                    case SLOT_STATE_ERROR:
                    case SLOT_STATE_UNSUPPORTED_VERSION:
                        blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getData(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                            LS_WINDOW_WIDTH);
                        break;
                    default:
                        _LoadTumbSlot(_slot_cursor);
                        blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_BACKGROUND].getData() + LS_WINDOW_WIDTH * 39 + 340,
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                            _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                            LS_WINDOW_WIDTH,
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                            LS_WINDOW_WIDTH);
                        blitBufferToBuffer(_thumbnail_image,
                            LS_PREVIEW_WIDTH - 1,
                            LS_PREVIEW_HEIGHT - 1,
                            LS_PREVIEW_WIDTH,
                            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 58 + 366,
                            LS_WINDOW_WIDTH);
                        break;
                    }

                    _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
                    _DrawInfoBox(_slot_cursor);
                    windowRefresh(gLoadSaveWindow);
                }

                if (scrollCounter > 14.4) {
                    delay_ms(1000 / scrollVelocity - (getTicks() - start));
                } else {
                    delay_ms(1000 / 24 - (getTicks() - start));
                }

                keyCode = inputGetInput();

                renderFpsCounter();
                renderPresent();
                sharedFpsLimiter.throttle();
            } while (keyCode != 505 && keyCode != 503);
        } else {
            if (selectionChanged) {
                switch (_LSstatus[_slot_cursor]) {
                case SLOT_STATE_EMPTY:
                case SLOT_STATE_ERROR:
                case SLOT_STATE_UNSUPPORTED_VERSION:
                    blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getData(),
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                        gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                        LS_WINDOW_WIDTH);
                    break;
                default:
                    _LoadTumbSlot(_slot_cursor);
                    blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_BACKGROUND].getData() + LS_WINDOW_WIDTH * 39 + 340,
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getWidth(),
                        _loadsaveFrmImages[LOAD_SAVE_FRM_PREVIEW_COVER].getHeight(),
                        LS_WINDOW_WIDTH,
                        gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 39 + 340,
                        LS_WINDOW_WIDTH);
                    blitBufferToBuffer(_thumbnail_image,
                        LS_PREVIEW_WIDTH - 1,
                        LS_PREVIEW_HEIGHT - 1,
                        LS_PREVIEW_WIDTH,
                        gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 58 + 366,
                        LS_WINDOW_WIDTH);
                    break;
                }

                _DrawInfoBox(_slot_cursor);
                _ShowSlotList(LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);
            }

            windowRefresh(gLoadSaveWindow);

            _dbleclkcntr -= 1;
            if (_dbleclkcntr == 0) {
                _dbleclkcntr = 24;
                doubleClickSlot = -1;
            }
        }

        if (rc == 1) {
            switch (_LSstatus[_slot_cursor]) {
            case SLOT_STATE_UNSUPPORTED_VERSION:
                soundPlayFile("iisxxxx1");
                stringCopy(_str0, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 134));
                stringCopy(_str1, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 136));
                stringCopy(_str2, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 135));
                showDialogBox(_str0, body, 2, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);
                rc = -1;
                break;
            case SLOT_STATE_ERROR:
                soundPlayFile("iisxxxx1");
                stringCopy(_str0, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 134));
                stringCopy(_str1, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 136));
                showDialogBox(_str0, body, 1, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);
                rc = -1;
                break;
            default:
                savegameSetSlot(_slot_cursor);
                int loadRc = lsgLoadGameInSlot(_slot_cursor);
                if (loadRc != -1) {
                    lsgDisplayMessage(141);
                }
                if (loadRc == -1) {
                    gameMouseSetCursor(MOUSE_CURSOR_ARROW);
                    soundPlayFile("iisxxxx1");
                    stringCopy(_str0, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 134));
                    stringCopy(_str1, getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 135));
                    showDialogBox(_str0, body, 1, 169, 116, COLOR_AMBER, nullptr, COLOR_AMBER, DIALOG_BOX_LARGE);
                    mapNewMap();
                    _game_user_wants_to_quit = GAME_QUIT_REQUEST_MAIN_MENU;
                    rc = -1;
                }
                break;
            }
        }

        renderFpsCounter();
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (mode == LOAD_SAVE_MODE_FROM_MAIN_MENU && rc == 0) {
        mainMenuFadeOutForMenuReturn(true);
    }

    lsgWindowFree(mode == LOAD_SAVE_MODE_FROM_MAIN_MENU
            ? LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU
            : LOAD_SAVE_WINDOW_TYPE_LOAD_GAME);

    pipboyMessageListFree();

    if (mode == LOAD_SAVE_MODE_QUICK) {
        if (rc == 1) {
            _quick_done = true;
        }
    }

    return rc;
}

void lsgDevSetLoadGameSlot(int slot)
{
    gDevLoadGameSlot = slot;
}

int lsgGetTotalSlotCount()
{
    return saveLoadTotalSlots;
}

// 0x47D2E4
static int lsgWindowInit(int windowType)
{
    gLoadSaveWindowOldFont = fontGetCurrent();
    fontSetCurrent(103);

    gLoadSaveWindowIsoWasEnabled = false;
    if (!messageListInit(&gLoadSaveMessageList)) {
        return -1;
    }

    snprintf(_str, sizeof(_str), "%s%s", gGameMsgPath, LSGAME_MSG_NAME);
    if (!messageListLoad(&gLoadSaveMessageList, _str)) {
        return -1;
    }
    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_LSGAME, &gLoadSaveMessageList);

    _snapshot = (unsigned char*)internal_malloc(61632);
    if (_snapshot == nullptr) {
        loadSaveMessageListReset();
        fontSetCurrent(gLoadSaveWindowOldFont);
        return -1;
    }

    _thumbnail_image = _snapshot;
    _snapshotBuf = _snapshot + LS_PREVIEW_SIZE;
    savegameSetPreviewBuffer(_snapshotBuf);

    if (windowType != LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU) {
        gLoadSaveWindowIsoWasEnabled = isoDisable();
    }

    colorCycleDisable();

    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    if (windowType == LOAD_SAVE_WINDOW_TYPE_SAVE_GAME || windowType == LOAD_SAVE_WINDOW_TYPE_PICK_QUICK_SAVE_SLOT) {
        bool gameMouseWasVisible = gameMouseObjectsIsVisible();
        if (gameMouseWasVisible) {
            gameMouseObjectsHide();
        }

        mouseHideCursor();
        tileWindowRefresh();
        mouseShowCursor();

        if (gameMouseWasVisible) {
            gameMouseObjectsShow();
        }

        // For preview take 640x380 area in the center of isometric window.
        Window* window = windowGetWindow(gIsoWindow);
        unsigned char* isoWindowBuffer = window->buffer
            + window->width * (window->height - ORIGINAL_ISO_WINDOW_HEIGHT) / 2
            + (window->width - ORIGINAL_ISO_WINDOW_WIDTH) / 2;
        blitBufferToBufferStretch(isoWindowBuffer,
            ORIGINAL_ISO_WINDOW_WIDTH,
            ORIGINAL_ISO_WINDOW_HEIGHT,
            windowGetWidth(gIsoWindow),
            _snapshotBuf,
            LS_PREVIEW_WIDTH,
            LS_PREVIEW_HEIGHT,
            LS_PREVIEW_WIDTH);
    }

    for (int index = 0; index < LOAD_SAVE_FRM_COUNT; index++) {
        if (!_loadsaveFrmImages[index].lock(kLoadSaveFrmIds[index])) {
            while (--index >= 0) {
                _loadsaveFrmImages[index].unlock();
            }
            internal_free(_snapshot);
            loadSaveMessageListReset();
            fontSetCurrent(gLoadSaveWindowOldFont);

            if (windowType != LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU) {
                if (gLoadSaveWindowIsoWasEnabled) {
                    isoEnable();
                }
            }

            colorCycleEnable();
            gameMouseSetCursor(MOUSE_CURSOR_ARROW);
            return -1;
        }
    }

    int lsWindowX = (screenGetWidth() - LS_WINDOW_WIDTH) / 2;
    int lsWindowY = (screenGetHeight() - LS_WINDOW_HEIGHT) / 2;
    gLoadSaveWindow = windowCreate(lsWindowX,
        lsWindowY,
        LS_WINDOW_WIDTH,
        LS_WINDOW_HEIGHT,
        static_cast<ColorWithFlags>(256),
        WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (gLoadSaveWindow == -1) {
        // FIXME: Leaking frms.
        internal_free(_snapshot);
        loadSaveMessageListReset();
        fontSetCurrent(gLoadSaveWindowOldFont);

        if (windowType != LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU) {
            if (gLoadSaveWindowIsoWasEnabled) {
                isoEnable();
            }
        }

        colorCycleEnable();
        gameMouseSetCursor(MOUSE_CURSOR_ARROW);
        return -1;
    }

    gLoadSaveWindowBuffer = windowGetBuffer(gLoadSaveWindow);
    memcpy(gLoadSaveWindowBuffer, _loadsaveFrmImages[LOAD_SAVE_FRM_BACKGROUND].getData(), LS_WINDOW_WIDTH * LS_WINDOW_HEIGHT);

    int messageId;
    switch (windowType) {
    case LOAD_SAVE_WINDOW_TYPE_SAVE_GAME:
        // SAVE GAME
        messageId = 102;
        break;
    case LOAD_SAVE_WINDOW_TYPE_PICK_QUICK_SAVE_SLOT:
        // PICK A QUICK SAVE SLOT
        messageId = 103;
        break;
    case LOAD_SAVE_WINDOW_TYPE_LOAD_GAME:
    case LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU:
        // LOAD GAME
        messageId = 100;
        break;
    case LOAD_SAVE_WINDOW_TYPE_PICK_QUICK_LOAD_SLOT:
        // PICK A QUICK LOAD SLOT
        messageId = 101;
        break;
    default:
        assert(false && "Should be unreachable");
    }

    char* msg;

    msg = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, messageId);
    fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 27 + 48, msg, LS_WINDOW_WIDTH, LS_WINDOW_WIDTH, COLOR_DARK_YELLOW);

    // DONE
    msg = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 104);
    fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 348 + 410, msg, LS_WINDOW_WIDTH, LS_WINDOW_WIDTH, COLOR_DARK_YELLOW);

    // CANCEL
    msg = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 105);
    fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 348 + 515, msg, LS_WINDOW_WIDTH, LS_WINDOW_WIDTH, COLOR_DARK_YELLOW);

    int btn;

    btn = buttonCreate(gLoadSaveWindow,
        391,
        349,
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getHeight(),
        -1,
        -1,
        -1,
        kLoadSaveActionDone,
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_NORMAL].getData(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    btn = buttonCreate(gLoadSaveWindow,
        495,
        349,
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getHeight(),
        -1,
        -1,
        -1,
        501,
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_NORMAL].getData(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    btn = buttonCreate(gLoadSaveWindow,
        35,
        58,
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_UP_PRESSED].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_UP_PRESSED].getHeight(),
        -1,
        505,
        506,
        505,
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_UP_NORMAL].getData(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_UP_PRESSED].getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    btn = buttonCreate(gLoadSaveWindow,
        35,
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_UP_PRESSED].getHeight() + 58,
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_DOWN_PRESSED].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_DOWN_PRESSED].getHeight(),
        -1,
        503,
        504,
        503,
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_DOWN_NORMAL].getData(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_ARROW_DOWN_PRESSED].getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    // tweaked bounds to accomodate Next/Previous buttons
    buttonCreate(gLoadSaveWindow, 55, 87, 230, 348, -1, -1, -1, 502, nullptr, nullptr, nullptr, BUTTON_FLAG_TRANSPARENT);

    fontSetCurrent(101);

    return 0;
}

// 0x47D824
static int lsgWindowFree(int windowType)
{
    loadSavePersistSelectedSlot();

    windowDestroy(gLoadSaveWindow);
    fontSetCurrent(gLoadSaveWindowOldFont);
    loadSaveMessageListReset();

    for (int index = 0; index < LOAD_SAVE_FRM_COUNT; index++) {
        _loadsaveFrmImages[index].unlock();
    }

    internal_free(_snapshot);

    if (windowType != LOAD_SAVE_WINDOW_TYPE_LOAD_GAME_FROM_MAIN_MENU) {
        if (gLoadSaveWindowIsoWasEnabled) {
            isoEnable();
        }
    }

    colorCycleEnable();
    gameMouseSetCursor(MOUSE_CURSOR_ARROW);
    touch_set_touchscreen_mode(false);

    return 0;
}

#if defined(__EMSCRIPTEN__)
// clang-format off
EM_ASYNC_JS(void, do_save_idbfs_loadsave, (), {
    await new Promise((resolve, reject) => FS.syncfs(err => err ? reject(err) : resolve()))
});
// clang-format on
#endif

// 0x47D88C


// 0x47DC60




// 0x47DC68


// 0x47DF10


// 0x47E2E4


// 0x47E5D0
static int _GetSlotList()
{
    std::fill_n(gLoadSaveSlotPageLoaded, saveLoadPages, false);
    loadSaveLoadSlotPage(_currentSlotPage);
    return slotsPerPage;
}

static void loadSaveLoadSlotPage(int page)
{
    assert(page >= 0 && page < saveLoadPages);

    if (gLoadSaveSlotPageLoaded[page]) {
        return;
    }

    gLoadSaveSlotPageLoaded[page] = true;

    int startIndex = page * slotsPerPage;
    int endIndex = std::min(startIndex + slotsPerPage, saveLoadTotalSlots);
    for (int index = startIndex; index < endIndex; index++) {
        snprintf(_str, sizeof(_str), "%s\\%s%.2d\\%s", "SAVEGAME", "SLOT", index + 1, "SAVE.DAT");

        int fileSize;
        if (dbGetFileSize(_str, &fileSize) != 0) {
            _LSstatus[index] = SLOT_STATE_EMPTY;
        } else {
            _flptr = fileOpen(_str, "rb");

            if (_flptr == nullptr) {
                debugPrint("\nLOADSAVE: ** Error opening save  game for reading! **\n");
                _LSstatus[index] = SLOT_STATE_ERROR;
                continue;
            }

            if (lsgLoadHeaderInSlot(_flptr, index) == -1) {
                if (savegameGetErrorCode() == 1) {
                    debugPrint("LOADSAVE: ** save file #%d is an older version! **\n", _slot_cursor);
                    _LSstatus[index] = SLOT_STATE_UNSUPPORTED_VERSION;
                } else {
                    debugPrint("LOADSAVE: ** Save file #%d corrupt! **", index);
                    _LSstatus[index] = SLOT_STATE_ERROR;
                }
            } else {
                _LSstatus[index] = SLOT_STATE_OCCUPIED;
            }

            fileClose(_flptr);
        }
    }
}

// 0x47E6D8

static void _ShowSlotList(int windowType)
{
    loadSaveLoadSlotPage(_currentSlotPage);

    // Clear display area
    bufferFill(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 87 + 55, 230, 353, LS_WINDOW_WIDTH, static_cast<Color>(gLoadSaveWindowBuffer[LS_WINDOW_WIDTH * 86 + 55] & COLOR_LAST));

    int y = 87;
    int startIndex = _currentSlotPage * slotsPerPage;
    int endIndex = startIndex + slotsPerPage;
    if (endIndex > saveLoadTotalSlots) endIndex = saveLoadTotalSlots;

    for (int index = startIndex; index < endIndex; index++) {
        Color color = index == _slot_cursor ? COLOR_LIGHT_YELLOW : COLOR_GREEN;
        const char* text = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, windowType != 0 ? 110 : 109);
        snprintf(_str, sizeof(_str), "[   %s %.2d:   ]", text, index + 1);
        fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * y + 55, _str, LS_WINDOW_WIDTH, LS_WINDOW_WIDTH, color);

        y += fontGetLineHeight();
        switch (_LSstatus[index]) {
        case SLOT_STATE_OCCUPIED:
            strcpy(_str, (*savegameSlotData(index)).description);
            break;
        case SLOT_STATE_EMPTY:
            // - EMPTY -
            text = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 111);
            snprintf(_str, sizeof(_str), "       %s", text);
            break;
        case SLOT_STATE_ERROR:
            // - CORRUPT SAVE FILE -
            text = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 112);
            snprintf(_str, sizeof(_str), "%s", text);
            color = COLOR_AMBER;
            break;
        case SLOT_STATE_UNSUPPORTED_VERSION:
            // - OLD VERSION -
            text = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 113);
            snprintf(_str, sizeof(_str), " %s", text);
            color = COLOR_AMBER;
            break;
        }

        fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * y + 55, _str, LS_WINDOW_WIDTH, LS_WINDOW_WIDTH, color);
        y += 2 * fontGetLineHeight() + 4;
    }

    // Pagination navigation
    if (saveLoadTotalSlots > 10) {
        Color activeColor = COLOR_GREEN;
        Color inactiveColor = COLOR_LIGHT_GREEN_2;
        int navigationY = loadSaveNavigationY();

        fontDrawText(
            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * navigationY + 55,
            "<<",
            LS_WINDOW_WIDTH,
            LS_WINDOW_WIDTH,
            _currentSlotPage > 0 ? activeColor : inactiveColor);

        {
            MessageListItem messageListItemBack = { 201, 0, nullptr, nullptr };
            // TODO: localize "BACK" and "MORE"
            char backText[] = "BACK";
            if (!messageListGetItem(&gPipboyMessageList, &messageListItemBack)) {
                debugPrint("Error: Couldn't find LoadSave Message!");
                messageListItemBack.text = backText;
            }
            fontDrawText(
                gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * navigationY + 95,
                messageListItemBack.text,
                LS_WINDOW_WIDTH,
                LS_WINDOW_WIDTH,
                _currentSlotPage > 0 ? activeColor : inactiveColor);
        }
        {
            MessageListItem messageListItemMore = { 200, 0, nullptr, nullptr };
            char moreText[] = "MORE";
            if (!messageListGetItem(&gPipboyMessageList, &messageListItemMore)) {
                debugPrint("Error: Couldn't find LoadSave Message!");
                messageListItemMore.text = moreText;
            }
            fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * navigationY + 210,
                messageListItemMore.text,
                LS_WINDOW_WIDTH,
                LS_WINDOW_WIDTH,
                _currentSlotPage < saveLoadPages - 1 ? activeColor : inactiveColor);
        }

        fontDrawText(
            gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * navigationY + 270,
            ">>",
            LS_WINDOW_WIDTH,
            LS_WINDOW_WIDTH,
            _currentSlotPage < saveLoadPages - 1 ? activeColor : inactiveColor);
    }
}

// 0x47E8E0
static void _DrawInfoBox(int slot)
{
    blitBufferToBuffer(_loadsaveFrmImages[LOAD_SAVE_FRM_BACKGROUND].getData() + LS_WINDOW_WIDTH * 253 + 396, 164, 60, LS_WINDOW_WIDTH, gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 253 + 396, 640);

    unsigned char* dest;
    const char* text;
    Color color = COLOR_GREEN;

    switch (_LSstatus[slot]) {
    case SLOT_STATE_OCCUPIED:
        if (1) {
            LoadSaveSlotData* ptr = &((*savegameSlotData(slot)));
            // raise this one pixel as well to match above
            fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 253 + 396, ptr->characterName, LS_WINDOW_WIDTH, LS_WINDOW_WIDTH, color);

            snprintf(_str,
                sizeof(_str),
                "%.2d %s %.4d   %.4d",
                ptr->gameDay,
                getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 116 + ptr->gameMonth),
                ptr->gameYear,
                100 * ((ptr->gameTime / 600) / 60 % 24) + (ptr->gameTime / 600) % 60);

            int v2 = fontGetLineHeight();
            fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * (255 + v2) + 397, _str, LS_WINDOW_WIDTH, LS_WINDOW_WIDTH, color);

            snprintf(_str,
                sizeof(_str),
                "%s %s",
                mapGetCityName(ptr->map),
                mapGetName(ptr->map, ptr->elevation));

            int y = v2 + 2 + v2 + 255;
            short beginnings[WORD_WRAP_MAX_COUNT];
            short count;
            if (wordWrap(_str, 164, beginnings, &count) == 0) {
                for (int index = 0; index < count - 1; index += 1) {
                    char* beginning = _str + beginnings[index];
                    char* ending = _str + beginnings[index + 1];

                    // Calculate length of the substring
                    size_t lineLength = ending - beginning;

                    // Create a temporary buffer to hold the substring
                    char temp[256]; // Ensure the buffer size is sufficient
                    strncpy(temp, beginning, lineLength);
                    temp[lineLength] = '\0'; // Null-terminate the copied substring

                    // Add a one-pixel shift to the x-coordinate for the second and subsequent lines for tapering readout display
                    int xShift = (index > 0) ? 2 : 0;

                    // Draw the substring
                    fontDrawText(gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * y + 399 + xShift, temp, 164, LS_WINDOW_WIDTH, color);
                    y += v2 + 2;
                }
            }
        }
        return;
    case SLOT_STATE_EMPTY:
        // Empty.
        text = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 114);
        dest = gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 262 + 404;
        break;
    case SLOT_STATE_ERROR:
        // Error!
        text = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 115);
        dest = gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 262 + 404;
        color = COLOR_AMBER;
        break;
    case SLOT_STATE_UNSUPPORTED_VERSION:
        // Old version.
        text = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 116);
        dest = gLoadSaveWindowBuffer + LS_WINDOW_WIDTH * 262 + 400;
        color = COLOR_AMBER;
        break;
    default:
        assert(false && "Should be unreachable");
    }

    fontDrawText(dest, text, LS_WINDOW_WIDTH, LS_WINDOW_WIDTH, color);
}

// 0x47EC48
static int _LoadTumbSlot(int slot)
{
    if (_LSstatus[_slot_cursor] != SLOT_STATE_EMPTY
        && _LSstatus[_slot_cursor] != SLOT_STATE_ERROR
        && _LSstatus[_slot_cursor] != SLOT_STATE_UNSUPPORTED_VERSION) {
        snprintf(_str, sizeof(_str), "%s\\%s%.2d\\%s", "SAVEGAME", "SLOT", _slot_cursor + 1, "SAVE.DAT");
        debugPrint(" Filename %s\n", _str);

        File* stream = fileOpen(_str, "rb");
        if (stream == nullptr) {
            debugPrint("\nLOADSAVE: ** (A) Error reading thumbnail #%d! **\n", slot);
            return -1;
        }

        if (fileSeek(stream, 131, SEEK_SET) != 0) {
            debugPrint("\nLOADSAVE: ** (B) Error reading thumbnail #%d! **\n", slot);
            fileClose(stream);
            return -1;
        }

        if (fileRead(_thumbnail_image, LS_PREVIEW_SIZE, 1, stream) != 1) {
            debugPrint("\nLOADSAVE: ** (C) Error reading thumbnail #%d! **\n", slot);
            fileClose(stream);
            return -1;
        }

        fileClose(stream);
    }

    return 0;
}

// 0x47ED5C
static int _GetComment(int slot)
{
    // Maintain original position in original resolution, otherwise center it.
    int commentWindowX = screenGetWidth() != 640
        ? (screenGetWidth() - _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth()) / 2
        : LS_COMMENT_WINDOW_X;
    int commentWindowY = screenGetHeight() != 480
        ? (screenGetHeight() - _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getHeight()) / 2
        : LS_COMMENT_WINDOW_Y;
    int window = windowCreate(commentWindowX,
        commentWindowY,
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getHeight(),
        static_cast<ColorWithFlags>(256),
        WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (window == -1) {
        return -1;
    }

    unsigned char* windowBuffer = windowGetBuffer(window);
    memcpy(windowBuffer,
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getData(),
        static_cast<size_t>(_loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getHeight()) * _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth());

    fontSetCurrent(103);

    const char* msg;

    // DONE
    msg = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 104);
    fontDrawText(windowBuffer + _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth() * 57 + 56,
        msg,
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth(),
        COLOR_DARK_YELLOW);

    // CANCEL
    msg = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 105);
    fontDrawText(windowBuffer + _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth() * 57 + 181,
        msg,
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth(),
        COLOR_DARK_YELLOW);

    // DESCRIPTION
    msg = getmsg(&gLoadSaveMessageList, &gLoadSaveMessageListItem, 130);

    char title[260];
    stringCopy(title, msg);

    int width = fontGetStringWidth(title);
    fontDrawText(windowBuffer + _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth() * 7 + (_loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth() - width) / 2,
        title,
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth(),
        COLOR_DARK_YELLOW);

    fontSetCurrent(101);

    int btn;

    // DONE
    btn = buttonCreate(window,
        34,
        58,
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getHeight(),
        -1,
        -1,
        -1,
        507,
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_NORMAL].getData(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    // CANCEL
    btn = buttonCreate(window,
        160,
        58,
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getWidth(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getHeight(),
        -1,
        -1,
        -1,
        508,
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_NORMAL].getData(),
        _loadsaveFrmImages[LOAD_SAVE_FRM_RED_BUTTON_PRESSED].getData(),
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    windowRefresh(window);

    char description[LOAD_SAVE_DESCRIPTION_LENGTH];
    if (_LSstatus[_slot_cursor] == SLOT_STATE_OCCUPIED) {
        strncpy(description, (*savegameSlotData(slot)).description, LOAD_SAVE_DESCRIPTION_LENGTH);
    } else {
        memset(description, '\0', LOAD_SAVE_DESCRIPTION_LENGTH);
    }

    int rc;

    Color backgroundColor = static_cast<Color>(*(_loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getData() + _loadsaveFrmImages[LOAD_SAVE_FRM_BOX].getWidth() * 35 + 24) & COLOR_LAST);
    if (_get_input_str2(window, 507, 508, description, LOAD_SAVE_DESCRIPTION_LENGTH - 1, 24, 35, COLOR_GREEN | DRAW_TEXT_FLAG_NONE, backgroundColor, 0) == 0) {
        strncpy((*savegameSlotData(slot)).description, description, LOAD_SAVE_DESCRIPTION_LENGTH);
        (*savegameSlotData(slot)).description[LOAD_SAVE_DESCRIPTION_LENGTH - 1] = '\0';
        rc = 1;
    } else {
        rc = 0;
    }

    windowDestroy(window);

    return rc;
}

// 0x47F084
static int _get_input_str2(int win, int doneKeyCode, int cancelKeyCode, char* description, int maxLength, int x, int y, ColorWithFlags textColor, Color backgroundColor, int flags)
{
    int cursorWidth = fontGetStringWidth("_") - 4;
    int windowWidth = windowGetWidth(win);
    int lineHeight = fontGetLineHeight();
    unsigned char* windowBuffer = windowGetBuffer(win);
    if (maxLength > 255) {
        maxLength = 255;
    }

    char text[256];
    strcpy(text, description);

    size_t textLength = strlen(text);
    text[textLength] = ' ';
    text[textLength + 1] = '\0';

    int nameWidth = fontGetStringWidth(text);

    bufferFill(windowBuffer + windowWidth * y + x, nameWidth, lineHeight, windowWidth, backgroundColor);
    fontDrawText(windowBuffer + windowWidth * y + x, text, windowWidth, windowWidth, textColor);

    windowRefresh(win);
    renderPresent();

    beginTextInput();

    int blinkCounter = 3;
    bool blink = false;

    int v1 = 0;

    int rc = 1;
    while (rc == 1) {
        sharedFpsLimiter.mark();

        int tick = getTicks();

        int keyCode = inputGetInput();
        if ((keyCode & 0x80000000) == 0) {
            v1++;
        }

        if (keyCode == doneKeyCode || keyCode == KEY_RETURN) {
            rc = 0;
        } else if (keyCode == cancelKeyCode || keyCode == KEY_ESCAPE) {
            rc = -1;
        } else {
            if ((keyCode == KEY_DELETE || keyCode == KEY_BACKSPACE) && textLength > 0) {
                bufferFill(windowBuffer + windowWidth * y + x, fontGetStringWidth(text), lineHeight, windowWidth, backgroundColor);

                // TODO: Probably incorrect, needs testing.
                if (v1 == 1) {
                    textLength = 1;
                }

                text[textLength - 1] = ' ';
                text[textLength] = '\0';
                fontDrawText(windowBuffer + windowWidth * y + x, text, windowWidth, windowWidth, textColor);
                textLength--;
            } else if ((keyCode >= KEY_FIRST_INPUT_CHARACTER && keyCode <= KEY_LAST_INPUT_CHARACTER) && textLength < maxLength) {
                if ((flags & 0x01) != 0) {
                    if (!_isdoschar(keyCode)) {
                        break;
                    }
                }

                bufferFill(windowBuffer + windowWidth * y + x, fontGetStringWidth(text), lineHeight, windowWidth, backgroundColor);

                text[textLength] = keyCode & 0xFF;
                text[textLength + 1] = ' ';
                text[textLength + 2] = '\0';
                fontDrawText(windowBuffer + windowWidth * y + x, text, windowWidth, windowWidth, textColor);
                textLength++;

                windowRefresh(win);
            }
        }

        blinkCounter -= 1;
        if (blinkCounter == 0) {
            blinkCounter = 3;
            blink = !blink;

            Color color = blink ? backgroundColor : static_cast<Color>(textColor & COLOR_LAST);
            bufferFill(windowBuffer + windowWidth * y + x + fontGetStringWidth(text) - cursorWidth, cursorWidth, lineHeight - 2, windowWidth, color);
            windowRefresh(win);
        }

        delay_ms(1000 / 24 - (getTicks() - tick));

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    endTextInput();

    if (rc == 0) {
        text[textLength] = '\0';
        strcpy(description, text);
    }

    return rc;
}

// 0x47F48C


// 0x47F490


// 0x47F4C8


// 0x47F510


// SlotMap2Game
// 0x47F990


// 0x47FE14


// 0x47FE58


// InitLoadSave
// 0x48000C


// 0x480040


// 0x4800C8


// 0x480104


// 0x4803D8


// 0x480710


// 0x480734


// 0x480754




static void lsgDisplayMessage(int messageId)
{
    gLoadSaveMessageListItem.num = messageId;
    if (messageListGetItem(&gLoadSaveMessageList, &gLoadSaveMessageListItem)) {
        displayMonitorAddMessage(gLoadSaveMessageListItem.text);
    } else {
        debugPrint("\nError: Couldn't find LoadSave Message!");
    }
}

} // namespace fallout
