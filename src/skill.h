#ifndef SKILL_H
#define SKILL_H

#include "art.h"
#include "db.h"
#include "obj_types.h"
#include "proto_types.h"
#include "skill_defs.h"

namespace fallout {

extern int _gIsSteal;
extern int _gStealCount;
extern int _gStealSize;

enum class SkillStealResult {
    Caught = 0,
    Success = 1,
    Fail = 2,
};

int skillsInit();
void skillsReset();
void skillsExit();
int skillsLoad(File* stream);
int skillsSave(File* stream);
void protoCritterDataResetSkills(CritterProtoData* data);
void skillsSetTagged(Skill* skills, int count, Object* subject = nullptr);
void skillsGetTagged(Skill* skills, int count, Object* subject = nullptr);
bool skillIsTagged(Skill skill, Object* subject = nullptr);
int skillGetValue(Object* critter, Skill skill);
void skillSetMaximum(int maximum);
int skillGetDefaultValue(Skill skill);
int skillAdd(Object* critter, Skill skill);
int skillAddForce(Object* critter, Skill skill);
int skillsGetCost(int skillValue);
int skillSub(Object* critter, Skill skill);
int skillSubForce(Object* critter, Skill skill);
int skillRoll(Object* critter, Skill skill, int modifier, int* howMuch);
char* skillGetName(Skill skill);
char* skillGetDescription(Skill skill);
char* skillGetAttributes(Skill skill);
SkillDexFrmId skillGetFrmId(Skill skill);
int skillUse(Object* obj, Object* target, Skill skill, int skillBonus);
SkillStealResult skillsPerformStealing(Object* thief, Object* target, Object* item, int quantity, bool isPlanting, int* xpOverride);
int skillGetGameDifficultyModifier(Skill skill);
int skillUpdateLastUse(Skill skill);
// The tagged-skill row belongs to ONE actor (PLAYER_SHEET_DESIGN.md §8). A null
// subject means "no subject in hand" and resolves to gDude — today's behavior
// verbatim, which is why the character-editor and debug callers pass nothing.

// Copy the host's tagged skills into every extra actor's row. Part of the
// stage-2 seeding set — an actor missing it is ~20 points short on every skill
// the host tagged.
void skillsPlayerActorSeed();
// ONE slot, for spawn-at-login (see ACCOUNT_IDENTITY_DESIGN.md trap 1).
void skillsPlayerActorSeedSlot(int slot);
int skillsPlayerActorTaggedRowWrite(File* stream, int slot);
int skillsPlayerActorTaggedRowRead(File* stream, int slot);
void skillsTagPerkApply(Skill* taggedSkills, Skill skill);
int skillGetBaseValue(Object* critter, Skill skill);
// Number of non-expired usage slots recorded for `skill` today (the anti-spam
// cooldown state). Exposed for state-dump visibility.
int skillGetUsesToday(Skill skill);
int skillsUsageSave(File* stream);
int skillsUsageLoad(File* stream);
char* skillsGetGenericResponse(Object* critter, bool isDude);

} // namespace fallout

#endif /* SKILL_H */
