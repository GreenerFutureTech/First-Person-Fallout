#ifndef CRITTER_H
#define CRITTER_H

#include "animation_defs.h"
#include "art.h"
#include "art_defs.h"
#include "combat_defs.h"
#include "db.h"
#include "obj_types.h"
#include "proto_types.h"

namespace fallout {

// Radiation levels.
//
// The names of levels are taken from Fallout 3, comments from Fallout 2.
enum RadiationLevel : int {
    // Very nauseous.
    RADIATION_LEVEL_NONE,

    // Slightly fatigued.
    RADIATION_LEVEL_MINOR,

    // Vomiting does not stop.
    RADIATION_LEVEL_ADVANCED,

    // Hair is falling out.
    RADIATION_LEVEL_CRITICAL,

    // Skin is falling off.
    RADIATION_LEVEL_DEADLY,

    // Intense agony.
    RADIATION_LEVEL_FATAL,

    // The number of radiation levels.
    RADIATION_LEVEL_COUNT,
};

inline RadiationLevel operator++(RadiationLevel& e, int)
{
    RadiationLevel result = e;
    e = static_cast<RadiationLevel>(static_cast<int>(e) + 1);
    return result;
}

const CritterFrameId kGorisCombatBaseFrameId = CritterFrameId(100);
const CritterFrameId kGorisRobeBaseFrameId = CritterFrameId(99);

int critterInit();
void critterReset();
void critterExit();
int critterLoad(File* stream);
int critterSave(File* stream);
char* critterGetName(Object* obj);
void critterProtoDataCopy(CritterProtoData* dest, CritterProtoData* src);
int dudeSetName(const char* name);
void dudeResetName();

// Per-actor names (PLAYER_SHEET_DESIGN.md §8). Slot 0 is gDudeName, i.e.
// dudeSetName / dudeResetName above.
//
// The GETTER is the storage accessor, not a convenience wrapper: protoGetName
// must answer a sheet pid without calling critterGetName, or the two recurse.
char* critterGetNameForSlot(int slot);
int critterSetNameForSlot(int slot, const char* name);
void critterPlayerActorSeedNames();
// ONE slot, for spawn-at-login (see ACCOUNT_IDENTITY_DESIGN.md trap 1).
void critterPlayerActorSeedNameSlot(int slot);
int critterPlayerActorNameRowWrite(File* stream, int slot);
int critterPlayerActorNameRowRead(File* stream, int slot);
int critterGetHitPoints(Object* critter);
int critterAdjustHitPoints(Object* critter, int hp);
int critterGetPoison(Object* critter);
int critterAdjustPoison(Object* obj, int amount);
int poisonEventProcess(Object* obj, void* data);
int critterGetRadiation(Object* critter);
int critterAdjustRadiation(Object* obj, int amount);
int critterCheckRadiationEvent(Object* critter);
int radiationClearDamage(Object* obj, void* data);
void radiationProcess(Object* obj, RadiationLevel radiationLevel, bool direction);
int radiationEventProcess(Object* obj, void* data);
int radiationEventRead(File* stream, void** dataPtr);
int radiationEventWrite(File* stream, void* data);
DamageType critterGetDamageType(Object* critter);
int killsIncByType(KillType killType);
int killsGetByType(KillType killType);
int killsLoad(File* stream);
int killsSave(File* stream);
KillType critterGetKillType(Object* critter);
char* killTypeGetName(KillType killType);
char* killTypeGetDescription(KillType killType);
int critterHealByHours(Object* obj, int hours);
void critterKill(Object* critter, AnimationType anim, bool refreshRect);
// Minimal inverse of critterKill: revive a dead critter at 1 HP, standing and
// blocking again. No-op (returns false) if the critter is not actually dead.
bool critterRevive(Object* critter);
int critterGetExp(Object* critter);
bool critterIsActive(Object* critter);
bool critterIsDead(Object* critter);
bool critterIsCrippled(Object* critter);
bool critterIsProne(Object* critter);
BodyType critterGetBodyType(Object* critter);
// Checks physical/art capability only. Callers that expose weapon usability
// decisions must still call scriptHooks_CanUseWeapon with the final result.
bool critterCanUseWeapon(Object* critter, Object* weapon, HitMode hitMode);
CritterFrmId critterBuildGorisFrmId(Object* critter, CritterFrameId frameId);
int gcdLoad(const char* path);
int protoCritterDataRead(File* stream, CritterProtoData* critterData);
int gcdSave(const char* path);
int protoCritterDataWrite(File* stream, CritterProtoData* critterData);
void dudeDisableState(DudeState state, Object* subject = nullptr);
void dudeEnableState(DudeState state, Object* subject = nullptr);
void dudeToggleState(DudeState state, Object* subject = nullptr);
bool dudeHasState(DudeState state, Object* subject = nullptr);
int sneakEventProcess(Object* obj, void* data);
int critterDisableSneak(Object* obj, void* data);
bool dudeIsSneaking(Object* subject = nullptr);
// ►► THE THREE DUDE_STATE_* FLAGS (sneaking, level-up available, addicted) LIVE IN
// THE PLAYER'S PROTO ROW, and every player actor has its own row — so these take a
// SUBJECT. They used to read gDude->pid unconditionally, which meant an extra
// player's sneak toggle, level-up badge and addiction marker were all written into
// (and read out of) SLOT 0's sheet: the host's indicator bar lit up for someone
// else's chem habit, an extra could never see its own level-up badge, and two
// players sneaking shared one flag.
//
// `subject == nullptr` means gDude, so every vanilla call site keeps its exact
// behaviour and single-player is byte-identical. The row is marked sheet-dirty on
// change, which is what streams the new flags to that actor's own client.
// True iff `subject` (default gDude) is sneaking AND its last sneak roll held.
int knockoutEventProcess(Object* obj, void* data);
int knockoutClear(Object* obj, void* data);
int critterSetWhoHitMe(Object* critter, Object* hitMe);
bool critterCanDudeRest();
int critterGetMovementPointCostAdjustedForCrippledLegs(Object* critter, int distance);
bool critterIsEncumbered(Object* critter);
bool critterIsFleeing(Object* critter);
bool critterFlagCheck(const ProtoId& protoId, CritterFlags flag);
void critterFlagSet(const ProtoId& protoId, CritterFlags flag);
void critterFlagUnset(const ProtoId& protoId, CritterFlags flag);

} // namespace fallout

#endif /* CRITTER_H */
