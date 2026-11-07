#include "character_editor.h"

#include <algorithm>

#include "object.h"
#include "perk.h"
#include "server_players.h"
#include "settings.h"
#include "stat.h"
#include "trait.h"

namespace fallout {

int gCharacterEditorLastLevel = 1;

static constexpr int kMaxPerkOwed = 250;
static constexpr int kMaxSelectablePerks = 37;
static int perkFrequencyOverride;
static int skillPointsPerLevelModifier;
static std::vector<int> owedLevels[kMaxPlayerActors];
static bool owedLevelsInitialized[kMaxPlayerActors];

static int progressionSlot(Object* subject)
{
    Object* actor = subject != nullptr ? subject : gDude;
    int slot = actor != nullptr ? playerActorSlotOf(actor) : 0;
    // On a viewer the controlled actor is mirrored as gDude before its roster
    // binding is complete, matching the perk and PC-stat row accessors.
    return slot >= 0 && slot < kMaxPlayerActors ? slot : 0;
}

static int levelsPerPerk(Object* subject)
{
    if (perkFrequencyOverride > 0) return perkFrequencyOverride;
    if (perkFrequencyOverride < 0) return PC_LEVEL_MAX + 1;
    return traitIsSelectedAndActive(TRAIT_SKILLED, subject) ? 4 : 3;
}

int characterEditorGetPerkOwed(Object* subject)
{
    return perkOwedPickCount(subject);
}

void characterEditorSetPerkOwed(int value, Object* subject)
{
    unsigned int count = static_cast<unsigned int>(value) & 0xFF;
    if (count > kMaxPerkOwed) return;
    int slot = progressionSlot(subject);
    if (owedLevelsInitialized[slot]) {
        owedLevels[slot].resize(count, pcGetStat(PC_STAT_LEVEL, subject));
    }
    perkOwedPickAdd(subject, static_cast<int>(count) - perkOwedPickCount(subject));
}

void characterEditorSetPerkFrequency(int value) { perkFrequencyOverride = value; }
void characterEditorSetSkillPointsPerLevelModifier(int value)
{
    skillPointsPerLevelModifier = std::clamp(value, -100, 100);
}
int characterEditorGetSkillPointsPerLevelModifier() { return skillPointsPerLevelModifier; }

void characterEditorMigrateLegacyPerkSelectionState(Object* subject)
{
    int slot = progressionSlot(subject);
    if (owedLevelsInitialized[slot]) return;
    owedLevels[slot].clear();
    owedLevelsInitialized[slot] = true;
    int owed = perkOwedPickCount(subject);
    if (owed == 0) return;
    int currentLevel = pcGetStat(PC_STAT_LEVEL, subject);
    int cadence = levelsPerPerk(subject);
    int selectionLevel = currentLevel;
    if (owed > 1) {
        int spent = 0;
        for (Perk perk = PERK_FIRST; perk < PERK_COUNT; perk++) spent += perkGetRank(subject != nullptr ? subject : gDude, perk);
        if (spent < (currentLevel - 1) / cadence) selectionLevel = std::min(currentLevel, std::max(3, (spent + 1) * cadence));
    }
    for (int i = 0; i < owed; ++i) {
        owedLevels[slot].push_back(selectionLevel);
        selectionLevel = std::min(currentLevel, (selectionLevel / cadence + 1) * cadence);
    }
}

const std::vector<int>& characterEditorGetOwedPerkLevels(Object* subject)
{
    characterEditorMigrateLegacyPerkSelectionState(subject);
    int slot = progressionSlot(subject);
    // Older row formats carry only the entitlement count. Reconstruct their
    // queue once rather than returning a stale queue from a former occupant.
    if (owedLevels[slot].size() != static_cast<size_t>(perkOwedPickCount(subject))) {
        owedLevelsInitialized[slot] = false;
        characterEditorMigrateLegacyPerkSelectionState(subject);
    }
    return owedLevels[slot];
}

bool characterEditorSetOwedPerkLevels(const std::vector<int>& levels, Object* subject)
{
    if (levels.size() > kMaxPerkOwed || levels.size() != static_cast<size_t>(perkOwedPickCount(subject))) return false;
    int previousLevel = 0;
    for (int level : levels) {
        if (level <= 0 || level > PC_LEVEL_MAX || level < previousLevel) return false;
        previousLevel = level;
    }
    int slot = progressionSlot(subject);
    owedLevels[slot] = levels;
    owedLevelsInitialized[slot] = true;
    return true;
}

int characterEditorGetPerkSelectionLevel(Object* subject)
{
    int currentLevel = pcGetStat(PC_STAT_LEVEL, subject);
    if (settings.gameplay.perk_carryover != PERK_CARRY_OVER_MODE_ON) return currentLevel;
    const auto& levels = characterEditorGetOwedPerkLevels(subject);
    return levels.empty() ? currentLevel : std::min(levels.front(), currentLevel);
}

void characterEditorHandleLevelUp(int level, Object* subject)
{
    if (level <= 0 || level % levelsPerPerk(subject) != 0) return;
    Object* actor = subject != nullptr ? subject : gDude;
    int selected = 0;
    for (Perk perk = PERK_FIRST; perk < PERK_COUNT; perk++) {
        if (perkGetRank(actor, perk) != 0 && ++selected >= kMaxSelectablePerks) return;
    }
    characterEditorGetOwedPerkLevels(subject);
    int slot = progressionSlot(subject);
    if (settings.gameplay.perk_carryover == PERK_CARRY_OVER_MODE_OFF) {
        characterEditorSetPerkOwed(1, subject);
        owedLevels[slot].assign(1, level);
    } else if (perkOwedPickCount(subject) < kMaxPerkOwed) {
        owedLevels[slot].push_back(level);
        perkOwedPickAdd(subject, 1);
    }
}

void characterEditorConsumeOwedPerk(Object* subject)
{
    if (perkOwedPickCount(subject) == 0) return;
    characterEditorGetOwedPerkLevels(subject);
    int slot = progressionSlot(subject);
    if (!owedLevels[slot].empty()) owedLevels[slot].erase(owedLevels[slot].begin());
    perkOwedPickAdd(subject, -1);
}

void characterEditorResetPerkStateSlot(int slot)
{
    if (slot < 0 || slot >= kMaxPlayerActors) return;
    owedLevels[slot].clear();
    owedLevelsInitialized[slot] = false;
}

void characterEditorResetProgression()
{
    gCharacterEditorLastLevel = 1;
    perkFrequencyOverride = 0;
    skillPointsPerLevelModifier = 0;
    for (int slot = 0; slot < kMaxPlayerActors; ++slot) characterEditorResetPerkStateSlot(slot);
    characterEditorSetPerkOwed(0);
}

int characterEditorPerkLevelsRowWrite(File* stream, int slot)
{
    if (slot < 0 || slot >= kMaxPlayerActors) return -1;
    characterEditorGetOwedPerkLevels(playerActorAt(slot));
    if (fileWriteInt32(stream, static_cast<int>(owedLevels[slot].size())) == -1) return -1;
    for (int level : owedLevels[slot]) if (fileWriteInt32(stream, level) == -1) return -1;
    return 0;
}

int characterEditorPerkLevelsRowRead(File* stream, int slot)
{
    if (slot < 0 || slot >= kMaxPlayerActors) return -1;
    int count;
    if (fileReadInt32(stream, &count) == -1 || count < 0 || count > kMaxPerkOwed) return -1;
    std::vector<int> levels;
    for (int i = 0; i < count; ++i) {
        int level;
        if (fileReadInt32(stream, &level) == -1 || level <= 0 || level > PC_LEVEL_MAX || (!levels.empty() && level < levels.back())) return -1;
        levels.push_back(level);
    }
    owedLevels[slot] = std::move(levels);
    owedLevelsInitialized[slot] = true;
    return 0;
}

int characterEditorSave(File* stream)
{
    if (fileWriteInt32(stream, gCharacterEditorLastLevel) == -1) return -1;
    return fileWriteUInt8(stream, static_cast<unsigned char>(characterEditorGetPerkOwed()));
}

int characterEditorLoad(File* stream)
{
    unsigned char owed;
    if (fileReadInt32(stream, &gCharacterEditorLastLevel) == -1 || fileReadUInt8(stream, &owed) == -1) return -1;
    characterEditorResetPerkStateSlot(progressionSlot(nullptr));
    characterEditorSetPerkOwed(owed);
    return 0;
}

} // namespace fallout
