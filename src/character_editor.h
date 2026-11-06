#ifndef CHARACTER_EDITOR_H
#define CHARACTER_EDITOR_H

#include <vector>

#include "db.h"

namespace fallout {

extern int gCharacterEditorRemainingCharacterPoints;

enum PerkCarryOverMode {
    // Unspent perks are lost on level up (vanilla)
    PERK_CARRY_OVER_MODE_OFF = 0,
    // Unspent perks are preserved on level up, but you still spend unspent perks at the appropriate levels.
    // E.g. if you reach level 9 and have never spent any perks, you first choose from the level 3 perks, then 6, then 9. (CE)
    PERK_CARRY_OVER_MODE_ON = 1,
    // Unspent perks are preserved on level up, and you can choose to spend them on perks for the current level.
    PERK_CARRY_OVER_MODE_SFALL = 2,
};

int characterEditorShow(bool isCreationMode);
// The co-op player's character sheet: characterEditorShow(0) whose spends EMIT
// WIRE INTENTS instead of mutating locally. See the definition.
int characterEditorShowViewOnly();
void characterEditorInit();
bool _isdoschar(int ch);
// Lives in character_editor_state.cc so the savegame driver in f2_core can reach
// it; the editor screen owns only the undo copy. The owed PERK moved to
// perkOwedPickGet/Set (per player — this one is per process).
extern int gCharacterEditorLastLevel;

int characterEditorSave(File* stream);
int characterEditorLoad(File* stream);
void characterEditorReset();
int characterEditorGetWindow();
void characterEditorDisplayStats();
int characterEditorGetPerkOwed();
void characterEditorSetPerkOwed(int value);
void characterEditorSetPerkFrequency(int value);
void characterEditorSetSkillPointsPerLevelModifier(int value);
void characterEditorHandleLevelUp(int level);
int characterEditorGetPerkSelectionLevel();
const std::vector<int>& characterEditorGetOwedPerkLevels();
bool characterEditorSetOwedPerkLevels(const std::vector<int>& levels);
void characterEditorMigrateLegacyPerkSelectionState();

} // namespace fallout

#endif /* CHARACTER_EDITOR_H */
