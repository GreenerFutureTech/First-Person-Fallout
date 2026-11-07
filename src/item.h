#ifndef ITEM_H
#define ITEM_H

#include <utility>

#include "animation_defs.h"
#include "art.h"
#include "art_defs.h"
#include "combat_defs.h"
#include "db.h"
#include "obj_types.h"
#include "perk_defs.h"
#include "proto_instance.h"
#include "proto_types.h"
#include "skill_defs.h"

namespace fallout {

enum class RemoveInventoryObjectHookReason;

enum AttackType : int {
    ATTACK_TYPE_INVALID = -1,
    ATTACK_TYPE_NONE,
    ATTACK_TYPE_UNARMED,
    ATTACK_TYPE_MELEE,
    ATTACK_TYPE_THROW,
    ATTACK_TYPE_RANGED,
    ATTACK_TYPE_COUNT,
    ATTACK_TYPE_FIRST = ATTACK_TYPE_NONE
};

enum HealingItem : int {
    HEALING_ITEM_STIMPAK,
    HEALING_ITEM_SUPER_STIMPAK,
    HEALING_ITEM_HEALING_POWDER,
    HEALING_ITEM_COUNT,
    HEALING_ITEM_FIRST = HEALING_ITEM_STIMPAK
};

inline HealingItem operator++(HealingItem& e, int)
{
    HealingItem result = e;
    e = static_cast<HealingItem>(static_cast<int>(e) + 1);
    return result;
}

int itemsInit();
void itemsReset();
void itemsExit();
int itemsLoad(File* stream);
int itemsSave(File* stream);
int itemAttemptAdd(Object* owner, Object* itemToAdd, int quantity);
int itemAdd(Object* owner, Object* itemToAdd, int quantity);
int itemRemove(Object* owner, Object* itemToRemove, int quantity);
int itemRemoveWithReason(Object* owner, Object* itemToRemove, int quantity, RemoveInventoryObjectHookReason reason, Object* target = nullptr);
int itemRemoveQuietly(Object* owner, Object* itemToRemove, int quantity);
int itemMove(Object* from, Object* to, Object* item, int quantity);
int itemMoveForce(Object* from, Object* to, Object* item, int quantity);
void itemMoveAll(Object* from, Object* to);
int itemMoveAllHidden(Object* from, Object* to);
int itemDestroyAllHidden(Object* owner);
int itemDropAll(Object* critter, int tile);
char* itemGetName(Object* obj);
char* itemGetDescription(Object* obj);
ItemType itemGetType(Object* item);
MaterialType itemGetMaterial(Object* item);
int itemGetSize(Object* obj);
int itemGetWeight(Object* item);
int itemGetCost(Object* obj);
int objectGetCost(Object* obj);
int objectGetInventoryWeight(Object* obj);
bool dudeIsWeaponDisabled(Object* weapon);
FrmId itemGetInventoryFrmId(Object* obj);
Object* critterGetWeaponForHitMode(Object* critter, HitMode hitMode);
int itemGetActionPointCost(Object* obj, HitMode hitMode, bool aiming);
int itemGetQuantity(Object* obj, Object* item);
int itemIsQueued(Object* obj);
Object* itemReplace(Object* owner, Object* itemToReplace, ObjectFlags flags);
bool itemIsHidden(Object* obj);
AttackType weaponGetAttackTypeForHitMode(Object* weapon, HitMode hitMode);
Skill weaponGetSkillForHitMode(Object* weapon, HitMode hitMode);
int weaponGetSkillValue(Object* critter, HitMode hitMode);
int weaponGetDamageMinMax(Object* weapon, int* minDamagePtr, int* maxDamagePtr);
int weaponGetDamage(Object* critter, HitMode hitMode);
DamageType weaponGetDamageType(Object* critter, Object* weapon);
int weaponIsTwoHanded(Object* weapon);
AnimationType critterGetAnimationForHitMode(Object* critter, HitMode hitMode);
AnimationType weaponGetAnimationForHitMode(Object* weapon, HitMode hitMode);
int ammoGetCapacity(Object* ammoOrWeapon);
int ammoGetQuantity(Object* ammoOrWeapon);
CaliberType ammoGetCaliber(Object* ammoOrWeapon);
void ammoSetQuantity(Object* ammoOrWeapon, int quantity);
int weaponAttemptReload(Object* critter, Object* weapon);
bool weaponCanBeReloadedWith(Object* weapon, Object* ammo);
bool weaponCanBeReloadedWithReplacingAmmo(Object* weapon, Object* ammo);
int weaponReload(Object* weapon, Object* ammo);
int weaponGetRange(Object* critter, HitMode hitMode);
int weaponGetActionPointCost(Object* critter, HitMode hitMode, bool aiming);
int weaponGetMinStrengthRequired(Object* weapon);
int weaponGetCriticalFailureType(Object* weapon);
Perk weaponGetPerk(Object* weapon);
int weaponGetBurstRounds(Object* weapon);
WeaponAnimation weaponGetAnimationCode(Object* weapon);
ProtoId weaponGetProjectileProtoId(Object* weapon);
ProtoId weaponGetAmmoTypeProtoId(Object* weapon);

// ►► DOES THIS CRITTER'S ART HAVE FRAMES FOR WIELDING THIS WEAPON? Asks the SAME question
// inventoryEquipFunc asks before equipping (inventory.cc: buildFid + artExists), but from a
// weapon PID, so a caller can pick a weapon a critter can actually hold WITHOUT creating a
// candidate object per guess.
//
// Worth a named function because the failure it prevents is nasty and silent: a critter
// handed a weapon its frame set has no animation for does not equip it and FIGHTS UNARMED —
// looking armed to the operator while punching. A tribal body with a rifle, or the dude with
// a throwing knife (both observed in this codebase).
bool weaponArtSupportedForCritter(Object* critter, int weaponPid);
char weaponGetSoundId(Object* weapon);
bool critterCanAim(Object* critter, HitMode hitMode);
void aimedShotOverridesReset();
void forceAimedShots(const ProtoId& protoId);
void disableAimedShots(const ProtoId& protoId);
int weaponCanBeUnloaded(Object* weapon);
Object* weaponUnload(Object* weapon);
int weaponGetPrimaryActionPointCost(Object* weapon);
int weaponGetSecondaryActionPointCost(Object* weapon);
int weaponComputeAmmoCost(const Object* obj, int* ammoQty);
bool weaponHasAmmoForAttack(const Object* weapon, HitMode hitMode);
bool weaponIsGrenade(Object* weapon);
int weaponGetDamageRadius(Object* weapon, HitMode hitMode);
int weaponGetGrenadeExplosionRadius(Object* weapon);
int weaponGetRocketExplosionRadius(Object* weapon);
int weaponGetAmmoArmorClassModifier(Object* weapon);
int weaponGetAmmoDamageResistanceModifier(Object* weapon);
int weaponGetAmmoDamageMultiplier(Object* weapon);
int weaponGetAmmoDamageDivisor(Object* weapon);
int armorGetArmorClass(Object* armor);
int armorGetDamageResistance(Object* armor, DamageType damageType);
int armorGetDamageThreshold(Object* armor, DamageType damageType);
Perk armorGetPerk(Object* armor);
CritterFrameId armorGetMaleFrameId(Object* armor);
CritterFrameId armorGetFemaleFrameId(Object* armor);
int miscItemGetMaxCharges(Object* miscItem);
int miscItemGetCharges(Object* miscItem);
int miscItemSetCharges(Object* miscItem, int charges);
int miscItemGetPowerType(Object* miscItem);
ProtoId miscItemGetPowerTypeProtoId(Object* miscItem);
bool miscItemUsesCharges(Object* obj);
UseItemResultCode miscItemUseCharged(Object* critter, Object* item);
int miscItemConsumeCharge(Object* miscItem);
int miscItemTrickleEventProcess(Object* item_obj, void* data);
bool miscItemIsOn(Object* obj);
int miscItemTurnOn(Object* item_obj);
int miscItemTurnOff(Object* item_obj);
int miscItemTurnOffFromQueue(Object* obj, void* data);
int containerGetMaxSize(Object* container);
int containerGetTotalSize(Object* container);
int ammoGetArmorClassModifier(Object* armor);
int ammoGetDamageResistanceModifier(Object* armor);
int ammoGetDamageMultiplier(Object* armor);
int ammoGetDamageDivisor(Object* armor);
UseItemResultCode drugItemTakeDrug(Object* critter_obj, Object* item_obj);
int drugItemClear(Object* obj, void* data);
int drugEffectEventProcess(Object* obj, void* data);
int drugEffectEventRead(File* stream, void** dataPtr);
int drugEffectEventWrite(File* stream, void* data);
int withdrawalClear(Object* obj, void* data);
int withdrawalEventProcess(Object* obj, void* data);
int withdrawalEventRead(File* stream, void** dataPtr);
int withdrawalEventWrite(File* stream, void* data);
typedef enum LootTransferResult {
    LOOT_TRANSFER_OK,
    LOOT_TRANSFER_CAUGHT_STEALING,
    LOOT_TRANSFER_NO_ROOM,
} LootTransferResult;

LootTransferResult lootTransferItem(Object* dude, Object* target, Object* item, int quantity, bool isPlanting, bool isSteal);
void equipmentDetach(Object* critter, Object** leftHandPtr, Object** rightHandPtr, Object** armorPtr);
void equipmentApply(Object* critter, Object* leftHand, Object* rightHand, Object* armor);
bool inventoryApCostApply(Object* critter);
bool itemDropStack(Object* owner, Object* item, int quantity);
bool itemUseDrug(Object* user, Object* owner, Object* item, bool inventoryResident);
int itemUseFromInventory(Object* user, Object* owner, Object* item, bool inventoryResident);
void weaponUnloadIntoInventory(Object* owner, Object* item, bool inventoryResident);
int containerStoreItem(Object* critter, Object* container, Object* item, int quantityToMove, bool inventoryResident);
int weaponLoadAmmo(Object* critter, Object* weapon, Object* ammo, int quantityToMove, bool ammoInventoryResident, bool* firstPackConsumedPtr);
typedef enum LootOpenResult {
    LOOT_OPEN_OK,
    LOOT_OPEN_NO_STEAL, // "can't find anything to take" feedback
    LOOT_OPEN_BLOCKED, // silent refusal (closed container, script override)
} LootOpenResult;

LootOpenResult lootOpenCheck(Object* looter, Object* target, bool isSteal);
Object* lootTargetDetach(Object* target, bool isSteal, Object** item1Ptr, Object** item2Ptr, Object** armorPtr);
void lootTargetReattach(Object* target, Object* hiddenBox, Object* item1, Object* item2, Object* armor);
void lootCaughtStealingReact(Object* looter, Object* target);
bool lootTakeAll(Object* looter, Object* target);
bool lootStealExperience(Object* looter, Object* target, int stealingXp, int* xpGainedPtr);

typedef enum BarterResult {
    BARTER_RESULT_OK,
    BARTER_RESULT_TOO_HEAVY, // dude can't carry the goods
    BARTER_RESULT_NPC_TOO_HEAVY, // party member can't carry the offer
    BARTER_RESULT_BAD_OFFER, // offer not good enough
} BarterResult;

int barterReactionModifier(Object* critter);
std::pair<int, int> barterComputeValues(Object* dude, Object* npc, Object* barterTable,
    Object* offerTable, int barterMod, bool speakerIsPartyMember, bool offerButton = false);
int barterComputeValue(Object* dude, Object* npc, Object* barterTable, int barterMod, bool speakerIsPartyMember);
BarterResult barterAttemptTransaction(Object* dude, Object* offerTable, Object* npc, Object* barterTable, int barterMod, bool speakerIsPartyMember);
int itemGetTotalCaps(Object* obj);
int itemCapsAdjust(Object* obj, int amount);
int itemGetMoney(Object* obj);
int itemSetMoney(Object* obj, int amount);

bool booksGetInfo(const ProtoId& protoId, int* messageIdPtr, Skill* skillPtr);
bool explosionEmitsLight();
void weaponSetGrenadeExplosionRadius(int value);
void weaponSetRocketExplosionRadius(int value);
void explosiveAdd(const ProtoId& protoId, const ProtoId& activeProtoId, int minDamage, int maxDamage);
bool explosiveIsExplosive(const ProtoId& protoId);
bool explosiveIsActiveExplosive(const ProtoId& protoId);
bool explosiveActivate(ProtoId& protoId);
bool explosiveSetDamage(const ProtoId& protoId, int minDamage, int maxDamage);
bool explosiveGetDamage(const ProtoId& protoId, int* minDamagePtr, int* maxDamagePtr);
void explosionSettingsReset();
void explosionGetPattern(Rotation* startRotationPtr, Rotation* endRotationPtr);
void explosionSetPattern(Rotation startRotation, Rotation endRotation);
MiscFrmId explosionGetFrmId();
void explosionSetFrmId(MiscFrmId frm);
void explosionSetRadius(int radius);
DamageType explosionGetDamageType();
void explosionSetDamageType(DamageType damageType);
int explosionGetMaxTargets();
void explosionSetMaxTargets(int maxTargets);
bool itemIsHealing(const ProtoId& protoId);

// Loot transfer source pick (issue #9): first top-level stack of `pid` that is
// NOT equipped. nullptr when only an equipped copy (or nothing) matches.
Object* serverLootFindStack(Object* from, int pid, int* quantityOut);

// Loot open (issue #8): USE the target + vanilla's lootOpenCheck gate (runs
// pickup_p_proc). False = refused, do not grant the loot screen.
bool serverLootOpen(Object* actor, Object* target);

} // namespace fallout

#endif /* ITEM_H */
