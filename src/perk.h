#ifndef PERK_H
#define PERK_H

#include "art.h"
#include "db.h"
#include "obj_types.h"
#include "perk_defs.h"
#include "skill_defs.h"

namespace fallout {

enum class PerkProperty {
    FrmId,
    MaxRank,
    MinLevel,
    Stat,
    StatModifier,
    Param1,
    Value1,
    ParamMode,
    Param2,
    Value2,
    Strength,
    Perception,
    Endurance,
    Charisma,
    Intelligence,
    Agility,
    Luck,
};
// Ledger H-44: follow-up choice a just-committed perk demands from the
// caller (see perkChoiceApply).
typedef enum PerkChoicePending {
    PERK_CHOICE_PENDING_NONE = 0,
    PERK_CHOICE_PENDING_TAG,
    PERK_CHOICE_PENDING_MUTATE,
} PerkChoicePending;

int perksInit();
void perksReset();
void perksExit();
int perksLoad(File* stream);
int perksSave(File* stream);
int perkAdd(Object* critter, Perk perk);
int perkAddAtLevel(Object* critter, Perk perk, int level);
int perkAddForce(Object* critter, Perk perk);
int perkRemove(Object* critter, Perk perk);
int perkGetAvailablePerks(Object* critter, int level, Perk* perks);
int perkGetRank(Object* critter, Perk perk);
char* perkGetName(Perk perk);
int perkGetMaxRank(Perk perk);
char* perkGetDescription(Perk perk);
SkillDexFrmId perkGetFrmId(Perk perk);
bool perkSetProperty(Perk perk, PerkProperty property, int value);
bool perkSetName(Perk perk, const char* value);
bool perkSetDescription(Perk perk, const char* value);
void perkAddEffect(Object* critter, Perk perk);
void perkRemoveEffect(Object* critter, Perk perk);
int perkGetSkillModifier(Object* critter, Skill skill);
int perkGetNightVisionBonus();
int perkGetMasterTraderBonus();
int perkGetEducatedBonus();
int perkGetHealerMinBonus();
int perkGetHealerMaxBonus();
int perkGetLifegiverBonus();
int perkGetWeaponLongRangeBonus();
int perkGetWeaponScopeRangeBonus();
int perkGetWeaponScopeRangePenalty();
int perkGetWeaponAccurateBonus();
int perkGetVaultCityInoculationsPoisonBonus();
int perkGetVaultCityInoculationsRadBonus();
int perkGetCautiousNatureBonus();
int perkGetComprehensionBonus();
int perkGetDemolitionExpertBonus();
int perkGetLivingAnatomyBonus();
int perkGetPyromaniacBonus();
int perkGetStonewallPercent();
int perkGetWeaponHandlingBonus();
int perkChoiceApply(Object* critter, Perk perk, const int* perksBackup, int* pendingChoicePtr);

// Copy the host's perk ranks into every extra player actor's row. Pairs with
// protoPlayerActorSheetsSeed — call both, or an extra gets the host's skills and
// nobody's perks (PLAYER_SHEET_DESIGN.md stage 2).
void perkPlayerActorSeedRanks();
// ONE slot, for spawn-at-login (see ACCOUNT_IDENTITY_DESIGN.md trap 1).
void perkPlayerActorSeedRanksSlot(int slot);
// Zero ONE slot's perks — a CREATED character inherits none of the host's.
void perkPlayerActorClearRanksSlot(int slot);

// One actor's perk ranks (PLAYER_SHEET_DESIGN.md §5). Slot 0 is the host's row.
// Companion rows are perksSave's business and never travel here.
int perkPlayerActorRowWrite(File* stream, int slot);
int perkPlayerActorRowRead(File* stream, int slot);

// The OWED FREE PERK PICK for one actor — the level-up entitlement, per player.
// The XP funnel (pcAddExperienceWithOptions) sets it when a level crosses the
// perk cadence; the pick clears it. `nullptr` means gDude, the same "no subject
// in hand" default the stat/trait resolvers use.
//
// ⚠ This is what makes a perk pick BOUNDED. perkAdd/perkCanAdd only check
// prerequisites, never entitlement, so a caller that skips this flag lets a
// player take every perk they qualify for.
// Does this actor owe at least one free perk pick?
bool perkOwedPickGet(Object* critter);
// How many — a multi-level XP award can owe several (levels 3, 6, 9, …).
int perkOwedPickCount(Object* critter);
// Award (+1) or spend (-1) ONE pick, clamped at zero. EVERY award/spend must come
// through here: perkOwedPickSet is absolute, so using it to spend would wipe a
// two- or three-perk debt down to nothing.
void perkOwedPickAdd(Object* critter, int delta);
// ABSOLUTE set (0 or 1) — restoring a snapshot, or a load. Not for award/spend.
void perkOwedPickSet(Object* critter, bool owed);

// The owed-pick flag as a sheet-row field (one byte). Slot 0's copy is what the
// savegame's characterEditorSave/Load byte holds, so this pair carries extras.
int perkPlayerActorOwedPickRowWrite(File* stream, int slot);
int perkPlayerActorOwedPickRowRead(File* stream, int slot);

// Returns true if perk is valid.

// Returns true if critter has at least one rank in specified perk.
//
// NOTE: Most perks have only 1 rank, which means dude either have perk, or
// not.
//
// On the other hand, there are several places in editor, where they made two
// consequtive calls to [perkGetRank], first to check for presence, then get
// the actual value for displaying. So a macro could exist, or this very
// function, but due to similarity to [perkGetRank] it could have been
// collapsed by compiler.
static inline bool perkHasRank(Object* critter, Perk perk)
{
    return perkGetRank(critter, perk) != 0;
}

} // namespace fallout

#endif /* PERK_H */
