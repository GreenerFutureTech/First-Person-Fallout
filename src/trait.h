#ifndef TRAIT_H
#define TRAIT_H

#include "art.h"
#include "db.h"
#include "skill_defs.h"
#include "stat_defs.h"
#include "obj_types.h"
#include "trait_defs.h"

namespace fallout {

int traitsInit();
void traitsReset();
void traitsExit();
int traitsLoad(File* stream);
int traitsSave(File* stream);
void traitsSetSelected(Trait trait1, Trait trait2, Object* subject = nullptr);
void traitsGetSelected(Trait* trait1, Trait* trait2, Object* subject = nullptr);
char* traitGetName(Trait trait);
char* traitGetDescription(Trait trait);
SkillDexFrmId traitGetFrmId(Trait trait);
bool traitIsSelected(Trait trait, Object* subject = nullptr);
bool traitIsSelectedAndActive(Trait trait, Object* subject = nullptr);
int traitGetStatModifier(Stat stat, Object* subject = nullptr);
int traitGetSkillModifier(Skill skill, Object* subject = nullptr);
// The TRAIT API. `subject` is the player actor whose traits these are; nullptr
// means gDude, which is today's behavior and why unconverted callers keep
// working (PLAYER_SHEET_DESIGN.md §3).

// Copy the host's traits into every extra actor's row. Call with the other three
// seeders — see stat.h's pcPlayerActorSeedStats.
void traitsPlayerActorSeed();
// ONE slot, for spawn-at-login (see ACCOUNT_IDENTITY_DESIGN.md trap 1).
void traitsPlayerActorSeedSlot(int slot);

// One actor's selected traits (PLAYER_SHEET_DESIGN.md §5). Slot 0 is the host's
// row.
int traitsPlayerActorRowWrite(File* stream, int slot);
int traitsPlayerActorRowRead(File* stream, int slot);
void traitsMutateDrop(Trait* traits, int traitCount, int pickedLine, Trait firstListedTrait);
void traitsMutateGain(Trait* traits, int traitCount, Trait newTrait);

} // namespace fallout

#endif /* TRAIT_H */
