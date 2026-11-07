#include "game_ui.h"
#include "presenter_client.h"
#include "presenter.h"
#include "savegame.h"
#include "window_manager_private.h"
#include "script_request_handler_client.h"
#include "config.h"
#include "game_config.h"
#include "message.h"
#include "game.h"
#include "sfall_config.h"
#include "platform/git_version.h"

#include <stdio.h>
#include <string.h>

#include "actions.h"
#include "animation.h"
#include "art.h"
#include "automap.h"
#include "character_editor.h"
#include "character_selector.h"
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
#include "content_config.h"
#include "critter.h"
#include "cycle.h"
#include "db.h"
#include "dbox.h"
#include "debug.h"
#include "display_monitor.h"
#include "dinput.h"
#include "draw.h"
#include "endgame.h"
#include "font_manager.h"
#include "game.h"
#include "game_dialog.h"
#include "game_memory.h"
#include "game_mouse.h"
#include "game_movie.h"
#include "game_sound.h"
#include "interface.h"
#include "inventory.h"
#include "input.h"
#include "item.h"
#include "kb.h"
#include "loadsave.h"
#include "map.h"
#include "memory.h"
#include "mouse.h"
#include "mouse.h"
#include "movie.h"
#include "movie_effect.h"
#include "object.h"
#include "options.h"
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
#include "sfall_arrays.h"
#include "sfall_callbacks.h"
#include "sfall_config.h"
#include "sfall_ext.h"
#include "sfall_global_scripts.h"
#include "sfall_global_vars.h"
#include "sfall_ini.h"
#include "sfall_lists.h"
#include "sfall_script_hooks.h"
#include "skill.h"
#include "skilldex.h"
#include "stat.h"
#include "string_utils.h"
#include "svga.h"
#include "text_font.h"
#include "tile.h"
#include "trait.h"
#include "version.h"
#include "win32.h"
#include "window_manager.h"
#include "worldmap.h"

#if __APPLE__
#include <TargetConditionals.h>
#endif

namespace fallout {

bool gameTerminalQuitRequested();

void gameResetSim();






static bool tryLoadBaseCEModAtPath(const char* path, bool* found, bool* openFailed);






inline constexpr char kBaseModPath[] = "ce.dat";

// 0x501C9C aGame_0
static char _aGame_0[] = "game\\";

// 0x5020B8 aDec11199816543



// 0x5186B4 game_ui_disabled








// 0x5186B8 game_state_cur
static GameState gGameState = GameState::Normal;

// 0x5186BC game_in_mapper
bool gIsMapper = false;

// 0x5186C0 game_global_vars
int* gGameGlobalVars = nullptr;

// 0x5186C4 num_game_global_vars
int gGameGlobalVarsLength = 0;

// 0x5186C8 msg_path
const char* gGameMsgPath = _aGame_0;

// 0x5186CC game_user_wants_to_quit
GameQuitRequest _game_user_wants_to_quit = GAME_QUIT_REQUEST_NONE;

// misc.msg
//
// 0x58E940 misc_message_file
MessageList gMiscMessageList;

// CE: Texts CE adds (ce.msg). The ones missing in a translation are shown in
// English, as the code has them.
MessageList gCeMessageList;

bool gGameLoaded = false;

// CE: Sonora folks like to store objects in global variables.
static void** gGameGlobalPointers = nullptr;

// 0x442580


// 0x442B84


// 0x442C34









// 0x442D44


;



// game_ui_disable
// 0x443BFC
// pass allowScrolling = 1 to allow scrolling




// game_ui_enable
// 0x443C30


// game_ui_is_disabled
// 0x443C60


// 0x443C68
int gameGetGlobalVar(GameGlobalVar var)
{
    if (!globalVariableIsValid(var)) {
        debugPrint("ERROR: attempt to reference global var out of range: %d", var);
        return 0;
    }

    return gGameGlobalVars[var];
}

// 0x443C98
int gameSetGlobalVar(GameGlobalVar var, int value)
{
    if (!globalVariableIsValid(var)) {
        debugPrint("ERROR: attempt to reference global var out of range: %d", var);
        return -1;
    }

    // SFALL: Display karma changes.
    if (var == GVAR_PLAYER_REPUTATION) {
        bool shouldDisplayKarmaChanges = settings.ui.display_karma_changes;
        if (shouldDisplayKarmaChanges) {
            int diff = value - gGameGlobalVars[var];
            if (diff != 0) {
                char formattedMessage[80];
                if (diff > 0) {
                    snprintf(formattedMessage, sizeof(formattedMessage), "You gained %d karma.", diff);
                } else {
                    snprintf(formattedMessage, sizeof(formattedMessage), "You lost %d karma.", -diff);
                }
                presenter()->consoleMessage(formattedMessage);
            }
        }
    }

    gGameGlobalVars[var] = value;

    return 0;
}

// game_load_info
// 0x443CC8
int gameLoadGlobalVars()
{
    if (globalVarsRead("data\\vault13.gam", "GAME_GLOBAL_VARS:", &gGameGlobalVarsLength, &gGameGlobalVars) != 0) {
        return -1;
    }

    gGameGlobalPointers = reinterpret_cast<void**>(internal_malloc(sizeof(*gGameGlobalPointers) * gGameGlobalVarsLength));
    if (gGameGlobalPointers == nullptr) {
        return -1;
    }

    memset(gGameGlobalPointers, 0, sizeof(*gGameGlobalPointers) * gGameGlobalVarsLength);

    return 0;
}

// 0x443CE8
int globalVarsRead(const char* path, const char* section, int* variablesListLengthPtr, int** variablesListPtr)
{
    inventoryResetDude();

    File* stream = fileOpen(path, "rt");
    if (stream == nullptr) {
        return -1;
    }

    if (*variablesListLengthPtr != 0) {
        internal_free(*variablesListPtr);
        *variablesListPtr = nullptr;
        *variablesListLengthPtr = 0;
    }

    char string[260];
    if (section != nullptr) {
        while (fileReadString(string, 258, stream)) {
            if (strncmp(string, section, 16) == 0) {
                break;
            }
        }
    }

    while (fileReadString(string, 258, stream)) {
        if (string[0] == '\n') {
            continue;
        }

        if (string[0] == '/' && string[1] == '/') {
            continue;
        }

        char* semicolon = strchr(string, ';');
        if (semicolon != nullptr) {
            *semicolon = '\0';
        }

        *variablesListLengthPtr = *variablesListLengthPtr + 1;
        *variablesListPtr = (int*)internal_realloc(*variablesListPtr, sizeof(int) * *variablesListLengthPtr);

        if (*variablesListPtr == nullptr) {
            exit(1);
        }

        char* equals = strchr(string, '=');
        if (equals != nullptr) {
            sscanf(equals + 1, "%d", *variablesListPtr + *variablesListLengthPtr - 1);
        } else {
            (*variablesListPtr)[*variablesListLengthPtr - 1] = 0;
        }
    }

    fileClose(stream);

    return 0;
}

// 0x443E2C
GameState gameGetState()
{
    return gGameState;
}

// 0x443E34
int gameRequestState(GameState newGameState)
{
    switch (newGameState) {
    case GameState::Normal:
        newGameState = GameState::NormalPending;
        break;
    case GameState::DialogFinished:
        newGameState = GameState::DialogFinishedPending;
        break;
    case GameState::DialogActive:
        newGameState = GameState::DialogRequested;
        break;
    default:
        break;
    }

    if (gGameState == GameState::DialogActive && newGameState == GameState::DialogRequested) {
        return -1;
    }

    gGameState = newGameState;
    return 0;
}

// 0x443E90
void gameUpdateState()
{
    switch (gGameState) {
    case GameState::NormalPending:
        gGameState = GameState::Normal;
        break;
    case GameState::DialogFinishedPending:
        gGameState = GameState::DialogFinished;
        break;
    case GameState::DialogRequested:
        gGameState = GameState::DialogActive;
        break;
    default:
        break;
    }
}

// 0x443EF0


// NOTE: Inlined.
//
// 0x443F50
void gameFreeGlobalVars()
{
    gGameGlobalVarsLength = 0;
    if (gGameGlobalVars != nullptr) {
        internal_free(gGameGlobalVars);
        gGameGlobalVars = nullptr;
    }

    if (gGameGlobalPointers != nullptr) {
        internal_free(gGameGlobalPointers);
        gGameGlobalPointers = nullptr;
    }
}

// 0x443F74


// 0x4440B8


static void TryLoadBaseCEMod()
{
    bool found = false;
    bool openFailed = false;

    if (tryLoadBaseCEModAtPath(kBaseModPath, &found, &openFailed)) {
        return;
    }

#if __APPLE__ && TARGET_OS_OSX
    const char* bundleResourcesPath = getMacOsBundleResourcesPath();
    if (bundleResourcesPath != nullptr) {
        char absolutePath[COMPAT_MAX_PATH];
        snprintf(absolutePath, sizeof(absolutePath), "%s/ce.dat", bundleResourcesPath);
        if (tryLoadBaseCEModAtPath(absolutePath, &found, &openFailed)) {
            return;
        }
    }
#endif

    if (openFailed) {
        debugPrint("Error opening base mod file/folder!\n");
    } else {
        debugPrint("Error opening base mod: no file or folder name %s found.\n", kBaseModPath);
    }
}

static bool tryLoadBaseCEModAtPath(const char* path, bool* found, bool* openFailed)
{
    if (compat_access(path, 0) != 0) {
        return false;
    }

    if (found != nullptr) {
        *found = true;
    }

    debugPrint("Loading base FO:CE mod: %s\n", path);
    if (dbOpen(path) == -1) {
        if (openFailed != nullptr) {
            *openFailed = true;
        }
        return false;
    }

    return true;
}

// 0x44418C
int gameDbInit()
{
    const char* master_dat_path = settings.system.master_dat_path.c_str();
    if (*master_dat_path == '\0') {
        master_dat_path = nullptr;
    }

    const char* master_patches_path = settings.system.master_patches_path.c_str();
    if (*master_patches_path == '\0') {
        master_patches_path = nullptr;
    }
    // Try to ensure that patches dir exists early. This is needed for auto-generated game.cfg later.
    if (master_patches_path != nullptr) {
        compat_mkdir_recursive(master_patches_path);
    }

    const char* critter_dat_path = settings.system.critter_dat_path.c_str();
    if (*critter_dat_path == '\0') {
        critter_dat_path = nullptr;
    }

    const char* critter_patches_path = settings.system.critter_patches_path.c_str();
    if (*critter_patches_path == '\0') {
        critter_patches_path = nullptr;
    }

    // Load archives in reverse priority order (dbOpen prepends to chain).
    // Resulting chain (head = highest priority):
    //   master_patches > critter_patches > mods > patchXXX.dat > ce.dat > f2_res_dat > critter.dat > master.dat

    if (dbOpen(master_dat_path) == -1) {
        showMessageBox("Could not find the master datafile. Please make sure the FALLOUT CD is in the drive and that you are running FALLOUT from the directory you installed it to.");
        return -1;
    }

    if (dbOpen(critter_dat_path) == -1) {
        showMessageBox("Could not find the critter datafile. Please make sure the FALLOUT CD is in the drive and that you are running FALLOUT from the directory you installed it to.");
        return -1;
    }

    const char* highResPatchDatPath = settings.system.f2_res_dat_path.c_str();
    if (*highResPatchDatPath != '\0' && compat_access(highResPatchDatPath, 0) == 0) {
        debugPrint("Loading HRP data mod: %s\n", highResPatchDatPath);
        dbOpen(highResPatchDatPath);
    }

    TryLoadBaseCEMod();

    // SFALL: custom patch file name.
    const char* path_file_name_template = nullptr;
    {
        char* configVal = nullptr;
        configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_PATCH_FILE, &configVal);
        if (configVal != nullptr && *configVal != '\0') {
            path_file_name_template = configVal;
        }
    }
    if (path_file_name_template == nullptr) {
        path_file_name_template = "patch%03d.dat";
    }

    char filename[COMPAT_MAX_PATH];
    for (int patch_index = 0; patch_index < 1000; patch_index++) {
        snprintf(filename, sizeof(filename), path_file_name_template, patch_index);

        if (compat_access(filename, 0) != 0) {
            break;
        }
        dbOpen(filename);
    }

    sfallLoadMods();

    if (critter_patches_path != nullptr) {
        dbOpen(critter_patches_path);
    }

    if (master_patches_path != nullptr) {
        dbOpen(master_patches_path);
    }

    return 0;
}

// 0x444384




void* gameGetGlobalPointer(GameGlobalVar var)
{
    if (!globalVariableIsValid(var)) {
        debugPrint("ERROR: attempt to reference global pointer out of range: %d", var);
        return nullptr;
    }

    return gGameGlobalPointers[var];
}

int gameSetGlobalPointer(GameGlobalVar var, void* value)
{
    if (!globalVariableIsValid(var)) {
        debugPrint("ERROR: attempt to reference global var out of range: %d", var);
        return -1;
    }

    gGameGlobalPointers[var] = value;

    return 0;
}

GameMode::Flags GameMode::currentGameMode = kNone;

void GameMode::enterGameMode(GameMode::Flags gameMode)
{
    GameMode::Flags previousGameMode = currentGameMode;
    currentGameMode = currentGameMode | gameMode;
    if (currentGameMode != previousGameMode) {
        sfallOnGameModeChange(0, previousGameMode);
    }
}

void GameMode::exitGameMode(GameMode::Flags gameMode)
{
    GameMode::Flags previousGameMode = currentGameMode;
    currentGameMode = currentGameMode & ~gameMode;
    if (currentGameMode != previousGameMode) {
        sfallOnGameModeChange(0, previousGameMode);
    }
}

// remove game mode without triggering hooks
void GameMode::exitGameModeQuietly(GameMode::Flags gameMode)
{
    currentGameMode = currentGameMode & ~gameMode;
}

bool GameMode::isInGameMode(GameMode::Flags gameMode)
{
    return (currentGameMode & gameMode) != kNone;
}

ScopedGameMode::ScopedGameMode(GameMode::Flags gameMode)
{
    this->gameMode = gameMode;
    GameMode::enterGameMode(gameMode);
}

ScopedGameMode::~ScopedGameMode()
{
    GameMode::exitGameMode(gameMode);
}





bool gameTerminalQuitRequested()
{
    return _game_user_wants_to_quit > 1;
}

void gameResetSim()
{
    tileDisable();
    randomReset();
    skillsReset();
    statsReset();
    perksReset();
    traitsReset();
    itemsReset();
    queueExit();
    animationReset();
    lsgInit();
    critterReset();
    aiReset();
    inventoryResetDude();
    isoReset();
    protoReset();
    _scr_reset();
    gameFreeGlobalVars();
    gameLoadGlobalVars();
    scriptsReset();
    wmWorldMap_reset();
    partyMembersReset();
    _ResetLoadSave();
    combatReset();
    _game_user_wants_to_quit = GAME_QUIT_REQUEST_NONE;

    // SFALL
    sfall_gl_vars_reset();
    sfallListsReset();
    messageListRepositoryReset();
    scriptHooksReset();
    sfallArraysReset();
    sfall_gl_scr_reset();
    sfall_ini_cache_clear();
    sfallOnGameReset();
    gGameLoaded = false;
}

} // namespace fallout
