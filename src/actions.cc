#include <stdint.h>
#include "client_net.h" // clientViewerActive — suppress AI chatter during wire replay (§3.c)
#include "combat_ap.h"
#include "server_players.h"
#include "pres_record.h"
#include "presenter.h"
#include "server_loop.h"
#include "sfall_config.h"
#include "actions.h"

#include <limits.h>
#include <string.h>

#include <assert.h>

#include "animation.h"
#include "animation_defs.h"
#include "art.h"
#include "art_defs.h"
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
#include "config.h"
#include "content_config.h"
#include "critter.h"
#include "debug.h"
#include "display_monitor.h"
#include "game.h"
#include "game_sound.h"
#include "geometry.h"
#include "interface.h"
#include "inventory.h"
#include "item.h"
#include "map.h"
#include "memory.h"
#include "object.h"
#include "party_member.h"
#include "perk.h"
#include "proto.h"
#include "proto_instance.h"
#include "proto_types.h"
#include "random.h"
#include "scripts.h"
#include "settings.h"
#include "sfall_script_hooks.h"
#include "skill.h"
#include "stat.h"
#include "text_object.h"
#include "tile.h"
#include "trait.h"

namespace fallout {

int _knockback_dest_tile(Object* obj, int maxDistance, int rotation);

static int _knockback_headless_dest(Object* critter, int flags, int knockbackDistance, int attackerTile);

int _combat_compute_knockback(Attack* attack, Object** victims, int* dests);

void _combat_commit_knockback(Object** victims, const int* dests, int count);

void _combat_apply_knockback(Attack* attack);

void* actionTalkToCallbackPtr();

void actionDeathDropItems(Object* obj, int anim);

void* actionShowDeathCallbackPtr();

void actionPresReplayShowDeath(Object* obj, int anim);

void actionThrowConsumeHeadless(Attack* attack);

void actionExplodeReplay(int tile, int elevation, Attack* attack);

void actionResolveDroppedExplosive(Object* dropper);

int actionUseItemOnObjectWithApCost(Object* user, Object* targetObj, Object* item);

void* actionHideProjectileCallbackPtr();


#define MAX_KNOCKDOWN_DISTANCE 20

enum ScienceRepairTargetType : int {
    SCIENCE_REPAIR_TARGET_TYPE_DEFAULT,
    SCIENCE_REPAIR_TARGET_TYPE_DUDE,
    SCIENCE_REPAIR_TARGET_TYPE_ANYONE,
};

// 0x5106D0 action_in_explode
static bool _action_in_explode = false;

// 0x5106D4 rotation
Rotation rotation;

// 0x5106E0 death_2
static const AnimationType gNormalDeathAnimations[DAMAGE_TYPE_COUNT] = {
    ANIM_DANCING_AUTOFIRE,
    ANIM_SLICED_IN_HALF,
    ANIM_CHARRED_BODY,
    ANIM_CHARRED_BODY,
    ANIM_ELECTRIFY,
    ANIM_FALL_BACK,
    ANIM_BIG_HOLE,
};

// 0x5106FC death_3
static const AnimationType gMaximumBloodDeathAnimations[DAMAGE_TYPE_COUNT] = {
    ANIM_CHUNKS_OF_FLESH,
    ANIM_SLICED_IN_HALF,
    ANIM_FIRE_DANCE,
    ANIM_MELTED_TO_NOTHING,
    ANIM_ELECTRIFIED_TO_NOTHING,
    ANIM_FALL_BACK,
    ANIM_EXPLODED_TO_NOTHING,
};

// Note: some of these are callbacks that always take two Object*, but may not use them.
// Ignored parameters are marked with underscores.
static int actionKnockdown(Object* obj, AnimationType* anim, int maxDistance, Rotation rotation, int delay);
static AnimationType actionBlood(Object* obj, AnimationType anim, int delay);
static AnimationType pickDeathAnim(Object* attacker, Object* defender, Object* weapon, int damage, AnimationType attackerAnimation, bool hitFromFront);
static AnimationType checkDeathAnim(Object* obj, AnimationType anim, int minViolenceLevel, bool hitFromFront);
static int _internal_destroy(Object* _, Object* toDestroy);
static void showDamageToObject(Object* defender, int damage, int flags, Object* weapon, bool hitFromFront, int knockbackDistance, Rotation knockbackRotation, AnimationType attackerAnimation, Object* attacker, int delay);
static int _show_death(Object* obj, AnimationType anim);
static int showDamageToExtras(Attack* attack);
static void showDamage(Attack* attack, AnimationType attackerAnimation, int delay);
static int _action_melee(Attack* attack, AnimationType anim);
static int _action_ranged(Attack* attack, AnimationType anim);
static int _is_next_to(Object* obj1, Object* obj2);
static int _action_climb_ladder(Object* critter, Object* ladder);
static bool actionRegisterUseAnimObj(Object* user, Object* targetObj, AnimationType* animPtr, int delay);
static int _action_use_skill_in_combat_error(Object* critter);
static AnimationType pickFallAnim(Object* obj, AnimationType anim);
static int _report_explosion(Attack* attack, Object* sourceObj);
static int _finished_explosion(Object*, Object*);
static int _compute_explosion_damage(int min, int max, Object* defender, int* knockbackDistancePtr);
static int _can_talk_to(Object* obj, Object* critter);
static int _talk_to(Object* _, Object* critter);
static int _report_dmg(Attack* attack, Object* _);
static int _compute_dmg_damage(int min, int max, Object* obj, int* knockbackDistancePtr, DamageType damageType);

static int hideProjectile(void* _, void* projectile);

static bool actionRegisterUseAnimObj(Object* user, Object* targetObj, AnimationType* animPtr, int delay)
{
    assert(animPtr != nullptr);

    AnimationType originalAnim = *animPtr;
    AnimationType hookAnim = scriptHooks_UseAnimObj(user, targetObj, originalAnim);
    if (hookAnim == ANIM_INVALID) {
        return false;
    }

    if (hookAnim != originalAnim) {
        const FrmId frmId = FrmId(user, hookAnim, user->rotation + 1);
        if (!frmId.exist()) {
            hookAnim = originalAnim;
        }
    }

    *animPtr = hookAnim;
    animationRegisterAnimate(user, hookAnim, delay);

    return true;
}

// 0x410468
int actionKnockdown(Object* obj, AnimationType* anim, int maxDistance, Rotation rotation, int delay)
{
    if (critterFlagCheck(obj, CRITTER_NO_KNOCKBACK)) {
        return -1;
    }

    if (*anim == ANIM_FALL_FRONT) {
        const FrmId frmId = FrmId(obj, *anim, obj->rotation + 1);
        if (!frmId.exist()) {
            *anim = ANIM_FALL_BACK;
        }
    }

    const char* soundEffectName = sfxBuildCharName(obj, *anim, CharacterSoundEffect::KnockDown);
    animationRegisterPlaySoundEffect(obj, soundEffectName, delay);

    int tile = _knockback_dest_tile(obj, maxDistance, rotation);

    if (tile == obj->tile) {
        animationRegisterAnimate(obj, *anim, 0);
    } else {
        animationRegisterMoveToTileStraightAndWaitForComplete(obj, tile, obj->elevation, *anim, 0);
    }

    return tile;
}

// Plays bleeding animation (after critter has fallen with ANIM_FALL_BACK or ANIM_FALL_FRONT).
//
// 0x410568 action_blood
AnimationType actionBlood(Object* obj, AnimationType anim, int delay)
{
    if (settings.preferences.violence_level == VIOLENCE_LEVEL_NONE) {
        return anim;
    }

    AnimationType bloodyAnim;
    if (anim == ANIM_FALL_BACK) {
        bloodyAnim = ANIM_FALL_BACK_BLOOD;
    } else if (anim == ANIM_FALL_FRONT) {
        bloodyAnim = ANIM_FALL_FRONT_BLOOD;
    } else {
        return anim;
    }

    const FrmId frmId = FrmId(obj, bloodyAnim, obj->rotation + 1);
    if (frmId.exist()) {
        animationRegisterAnimate(obj, bloodyAnim, delay);
    } else {
        bloodyAnim = anim;
    }

    return bloodyAnim;
}

// 0x41060C pick_death
AnimationType pickDeathAnim(Object* attacker, Object* defender, Object* weapon, int damage, AnimationType attackerAnimation, bool hitFromFront)
{
    if (FrmId(attacker) == MiscFrameId::RocketExplosion) {
        return checkDeathAnim(defender, ANIM_EXPLODED_TO_NOTHING, VIOLENCE_LEVEL_MAXIMUM_BLOOD, hitFromFront);
    }
    if (ProtoId(attacker) == SceneryProtoTypeId::ForceFieldNorthSouth) {
        return checkDeathAnim(defender, ANIM_ELECTRIFIED_TO_NOTHING, VIOLENCE_LEVEL_MAXIMUM_BLOOD, hitFromFront);
    }
    if (FrmId(attacker) == SceneryFrameId::ForceField3) {
        return checkDeathAnim(defender, attackerAnimation, VIOLENCE_LEVEL_MAXIMUM_BLOOD, hitFromFront);
    }

    int normalViolenceLevelDamageThreshold = 15;
    int maximumBloodViolenceLevelDamageThreshold = 45;

    DamageType damageType = weaponGetDamageType(attacker, weapon);

    if (ProtoId(weapon) == ItemProtoTypeId::MolotovCocktail) {
        normalViolenceLevelDamageThreshold = 5;
        maximumBloodViolenceLevelDamageThreshold = 15;
        damageType = DAMAGE_TYPE_FIRE;
        attackerAnimation = ANIM_FIRE_SINGLE;
    }

    if (playerActorIs(attacker) && perkHasRank(attacker, PERK_PYROMANIAC) && damageType == DAMAGE_TYPE_FIRE) {
        normalViolenceLevelDamageThreshold = 1;
        maximumBloodViolenceLevelDamageThreshold = 1;
    }

    if (weapon != nullptr && weaponGetPerk(weapon) == PERK_WEAPON_FLAMEBOY) {
        normalViolenceLevelDamageThreshold /= 3;
        maximumBloodViolenceLevelDamageThreshold /= 3;
    }

    int violenceLevel = settings.preferences.violence_level;

    if (critterFlagCheck(defender, CRITTER_SPECIAL_DEATH)) {
        return checkDeathAnim(defender, ANIM_EXPLODED_TO_NOTHING, VIOLENCE_LEVEL_NORMAL, hitFromFront);
    }

    bool hasBloodyMess = false;
    if (playerActorIs(attacker) && traitIsSelectedAndActive(TRAIT_BLOODY_MESS, attacker)) {
        hasBloodyMess = true;
    }

    // NOTE: Original code is slightly different. There are lots of jumps and
    // conditions. It's easier to set the default in advance, rather than catch
    // it with bunch of "else" statements.
    AnimationType deathAnim = ANIM_FALL_BACK;

    if ((attackerAnimation == ANIM_THROW_PUNCH && damageType == DAMAGE_TYPE_NORMAL)
        || attackerAnimation == ANIM_KICK_LEG
        || attackerAnimation == ANIM_THRUST_ANIM
        || attackerAnimation == ANIM_SWING_ANIM
        || (attackerAnimation == ANIM_THROW_ANIM && damageType != DAMAGE_TYPE_EXPLOSION)) {
        if (violenceLevel == VIOLENCE_LEVEL_MAXIMUM_BLOOD && hasBloodyMess) {
            deathAnim = ANIM_BIG_HOLE;
        }
    } else {
        if (attackerAnimation == ANIM_FIRE_SINGLE && damageType == DAMAGE_TYPE_NORMAL) {
            if (violenceLevel == VIOLENCE_LEVEL_MAXIMUM_BLOOD) {
                if (hasBloodyMess || maximumBloodViolenceLevelDamageThreshold <= damage) {
                    deathAnim = ANIM_BIG_HOLE;
                }
            }
        } else {
            if (violenceLevel > VIOLENCE_LEVEL_MINIMAL && (hasBloodyMess || normalViolenceLevelDamageThreshold <= damage)) {
                if (violenceLevel > VIOLENCE_LEVEL_NORMAL && (hasBloodyMess || maximumBloodViolenceLevelDamageThreshold <= damage)) {
                    deathAnim = gMaximumBloodDeathAnimations[damageType];
                    if (checkDeathAnim(defender, deathAnim, VIOLENCE_LEVEL_MAXIMUM_BLOOD, hitFromFront) != deathAnim) {
                        deathAnim = gNormalDeathAnimations[damageType];
                    }
                } else {
                    deathAnim = gNormalDeathAnimations[damageType];
                }
            }
        }
    }

    if (!hitFromFront && deathAnim == ANIM_FALL_BACK) {
        deathAnim = ANIM_FALL_FRONT;
    }

    return checkDeathAnim(defender, deathAnim, VIOLENCE_LEVEL_NONE, hitFromFront);
}

// Returns anim if art for it exists and selected violence_level >= minViolenceLevel.
// Otherwise, returns neutral fall animation (front or back, based on hitFromFront).
//
// 0x410814 check_death
AnimationType checkDeathAnim(Object* obj, AnimationType anim, int minViolenceLevel, bool hitFromFront)
{
    FrmId frmId;

    if (settings.preferences.violence_level >= minViolenceLevel) {
        frmId = FrmId(obj, anim, obj->rotation + 1);
        if (frmId.exist()) {
            return anim;
        }
    }

    if (hitFromFront) {
        return ANIM_FALL_BACK;
    }

    frmId = FrmId(obj, ANIM_FALL_FRONT, obj->rotation + 1);
    // CE: fixed vanilla logic that returned ANIM_FALL_BACK if artExists for ANIM_FALL_FRONT returned true.
    if (!frmId.exist()) {
        return ANIM_FALL_BACK;
    }

    return ANIM_FALL_FRONT;
}

// 0x4108C8
int _internal_destroy(Object* _, Object* toDestroy)
{
    return objectDestroy(toDestroy);
}

// TODO: Check very carefully, lots of conditions and jumps.
//
// 0x4108D0 show_damage_to_object
void showDamageToObject(Object* defender, int damage, int flags, Object* weapon, bool hitFromFront, int knockbackDistance, Rotation knockbackRotation, AnimationType attackerAnimation, Object* attacker, int delay)
{
    FrmId frmId;
    const char* sfx_name;

    if (critterFlagCheck(defender, CRITTER_NO_KNOCKBACK)) {
        knockbackDistance = 0;
    }

    AnimationType anim = FrmId(defender).animationType();
    if (!critterIsProne(defender)) {
        if ((flags & DAM_DEAD) != DAM_NONE) {
            anim = pickDeathAnim(attacker, defender, weapon, damage, attackerAnimation, hitFromFront);
            scriptHooks_DeathAnim(attacker, defender, weapon, damage, &anim);

            if (anim != ANIM_FIRE_DANCE) {
                if (knockbackDistance != 0 && (anim == ANIM_FALL_FRONT || anim == ANIM_FALL_BACK)) {
                    actionKnockdown(defender, &anim, knockbackDistance, knockbackRotation, delay);
                    anim = actionBlood(defender, anim, -1);
                } else {
                    sfx_name = sfxBuildCharName(defender, anim, CharacterSoundEffect::Die);
                    animationRegisterPlaySoundEffect(defender, sfx_name, delay);

                    anim = pickFallAnim(defender, anim);
                    animationRegisterAnimate(defender, anim, 0);

                    if (anim == ANIM_FALL_FRONT || anim == ANIM_FALL_BACK) {
                        anim = actionBlood(defender, anim, -1);
                    }
                }
            } else {
                frmId = FrmId(defender, ANIM_FIRE_DANCE, defender->rotation + 1);
                if (frmId.exist()) {
                    sfx_name = sfxBuildCharName(defender, anim, CharacterSoundEffect::Unused);
                    animationRegisterPlaySoundEffect(defender, sfx_name, delay);

                    // SFALL
                    if (explosionEmitsLight()) {
                        // 0xFFFF0002:
                        // - distance: 2
                        // - intensity: 65535
                        //
                        // NOTE: Change intensity to 65536 (which is on par with
                        // `anim_set_check_light_fix` Sfall's hack).
                        animationRegisterSetLightIntensity(defender, 2, 65536, 0);
                    }

                    animationRegisterAnimate(defender, anim, 0);

                    // SFALL
                    if (explosionEmitsLight()) {
                        // 0x00010000:
                        // - distance: 0
                        // - intensity: 1
                        //
                        // NOTE: Change intensity to 0. I guess using 1 was a
                        // workaround for `anim_set_check_light_fix` hack which
                        // requires two upper bytes to be non-zero to override
                        // default behaviour.
                        animationRegisterSetLightIntensity(defender, 0, 0, -1);
                    }

                    int randomDistance = randomBetween(2, 5);
                    Rotation randomRotation = static_cast<Rotation>(randomBetween(ROTATION_FIRST, ROTATION_LAST));

                    // CE: Fix to prevent critters (including player) to cross
                    // an exit grid as a result of fire dance animation. See
                    // `actionKnockdown` for notes.
                    Rotation rotation = randomRotation;
                    int distance = randomDistance;
                    while (true) {
                        int tile = tileGetTileInDirection(defender->tile, (rotation + randomRotation) % ROTATION_COUNT, distance);
                        if (!isExitGridAt(tile, defender->elevation)) {
                            Object* obstacle = nullptr;
                            _make_straight_path(defender, defender->tile, tile, nullptr, &obstacle, 4);
                            if (obstacle == nullptr) {
                                animationRegisterRotateToTile(defender, tile);
                                animationRegisterMoveToTileStraight(defender, tile, defender->elevation, anim, 0);
                                break;
                            }
                        }

                        if (distance > 0) {
                            distance--;
                        } else if (rotation < ROTATION_COUNT) {
                            rotation++;
                            distance = randomDistance;
                        } else {
                            break;
                        }
                    }
                }

                anim = ANIM_BURNED_TO_NOTHING;
                sfx_name = sfxBuildCharName(defender, anim, CharacterSoundEffect::Unused);
                animationRegisterPlaySoundEffect(defender, sfx_name, -1);
                animationRegisterAnimate(defender, anim, 0);
            }
        } else {
            if ((flags & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) != 0) {
                anim = hitFromFront ? ANIM_FALL_BACK : ANIM_FALL_FRONT;
                sfx_name = sfxBuildCharName(defender, anim, CharacterSoundEffect::Unused);
                animationRegisterPlaySoundEffect(defender, sfx_name, delay);
                if (knockbackDistance != 0) {
                    actionKnockdown(defender, &anim, knockbackDistance, knockbackRotation, 0);
                } else {
                    anim = pickFallAnim(defender, anim);
                    animationRegisterAnimate(defender, anim, 0);
                }
            } else if ((flags & DAM_ON_FIRE) != DAM_NONE && FrmId(defender, ANIM_FIRE_DANCE, defender->rotation + 1).exist()) {
                animationRegisterAnimate(defender, ANIM_FIRE_DANCE, delay);

                frmId = FrmId(defender, ANIM_STAND, defender->rotation + 1);
                animationRegisterSetFrmId(defender, frmId, -1);
            } else {
                if (knockbackDistance != 0) {
                    anim = hitFromFront ? ANIM_FALL_BACK : ANIM_FALL_FRONT;
                    actionKnockdown(defender, &anim, knockbackDistance, knockbackRotation, delay);
                    if (anim == ANIM_FALL_BACK) {
                        animationRegisterAnimate(defender, ANIM_BACK_TO_STANDING, -1);
                    } else {
                        animationRegisterAnimate(defender, ANIM_PRONE_TO_STANDING, -1);
                    }
                } else {
                    if (hitFromFront || !FrmId(defender, ANIM_HIT_FROM_BACK, defender->rotation + 1).exist()) {
                        anim = ANIM_HIT_FROM_FRONT;
                    } else {
                        anim = ANIM_HIT_FROM_BACK;
                    }

                    sfx_name = sfxBuildCharName(defender, anim, CharacterSoundEffect::Unused);
                    animationRegisterPlaySoundEffect(defender, sfx_name, delay);

                    animationRegisterAnimate(defender, anim, 0);
                }
            }
        }
    } else {
        if ((flags & DAM_DEAD) != DAM_NONE && (defender->data.critter.combat.results & DAM_DEAD) == DAM_NONE) {
            anim = actionBlood(defender, anim, delay);
        } else {
            return;
        }
    }

    if (weapon != nullptr) {
        if ((flags & DAM_EXPLODE) != DAM_NONE) {
            animationRegisterCallbackForced(defender, weapon, (AnimationCallback*)objectDrop, -1);
            animationRegisterSetFrmId(weapon, MiscFrameId::RocketExplosion, 0);
            animationRegisterAnimateAndHide(weapon, ANIM_STAND, 0);

            sfx_name = sfxBuildWeaponName(WEAPON_SOUND_EFFECT_HIT, weapon, HIT_MODE_RIGHT_WEAPON_PRIMARY, defender);
            animationRegisterPlaySoundEffect(weapon, sfx_name, 0);

            animationRegisterHideObjectForced(weapon);
        } else if ((flags & DAM_DESTROY) != DAM_NONE) {
            animationRegisterCallbackForced(defender, weapon, (AnimationCallback*)_internal_destroy, -1);
        } else if ((flags & DAM_DROP) != DAM_NONE) {
            animationRegisterCallbackForced(defender, weapon, (AnimationCallback*)objectDrop, -1);
        }
    }

    if ((flags & DAM_DEAD) != DAM_NONE) {
        // TODO: Get rid of casts.
        animationRegisterCallbackForced(defender, (void*)(uintptr_t)anim, (AnimationCallback*)_show_death, -1);
    }
}

// 0x410E24
int _show_death(Object* obj, AnimationType anim)
{
    Rect tempRect;
    Rect dirtyRect;

    objectGetRect(obj, &dirtyRect);
    if (anim < ANIM_FALL_BACK_SF && anim > ANIM_FALL_FRONT_BLOOD_SF) {
        const FrmId frmId = FrmId(obj, static_cast<AnimationType>(anim + 28), obj->rotation + 1);
        if (objectSetFrmId(obj, frmId, &tempRect) == 0) {
            rectUnion(&dirtyRect, &tempRect, &dirtyRect);
        }

        if (objectSetFrame(obj, 0, &tempRect) == 0) {
            rectUnion(&dirtyRect, &tempRect, &dirtyRect);
        }
    }

    if (!critterFlagCheck(obj, CRITTER_FLAT)) {
        obj->flags |= OBJECT_NO_BLOCK;
        if (_obj_toggle_flat(obj, &tempRect) == 0) {
            rectUnion(&dirtyRect, &tempRect, &dirtyRect);
        }
    }

    if (objectDisableOutline(obj, &tempRect) == 0) {
        rectUnion(&dirtyRect, &tempRect, &dirtyRect);
    }

    // ►► STATE, not presentation — and on the dedicated server _show_death never
    // runs at all (server_anim.cc animationRegisterCallbackForced RECORDS the
    // callback and returns without invoking it). The viewer, which DOES replay it,
    // must therefore not perform the drop: doing so dropped the corpse's inventory
    // into the CLIENT'S world only, producing ground items the server had never
    // heard of. Clicking one sent `get <netId>`, and the server — where those items
    // were still inside the corpse — answered "bad target ... ignored". Nobody could
    // ever loot an annihilated critter.
    //
    // The authoritative drop is applied server-side at the record site instead; see
    // actionDeathDropItems below. pres_record.h:193 always specified this half as
    // "guarded"; this is that guard, finally written.
    if (!clientViewerActive()) {
        actionDeathDropItems(obj, anim);
    }

    presenter()->worldInvalidateRect(&dirtyRect, obj->elevation);

    return 0;
}

// Animates damage to extras in a given attack (secondary targets).
//
// 0x410FEC show_damage_extras
int showDamageToExtras(Attack* attack)
{
    for (int index = 0; index < attack->extrasLength; index++) {
        Object* obj = attack->extras[index];
        if (FrmId(obj).objectType() == OBJ_TYPE_CRITTER) {
            // NOTE: Uninline.
            bool hitFromFront = _is_hit_from_front(attack->attacker, obj);
            reg_anim_begin(ANIMATION_REQUEST_RESERVED);
            _register_priority(1);
            AnimationType attackerAnimation = critterGetAnimationForHitMode(attack->attacker, attack->hitMode);
            Rotation knockbackRotation = tileGetRotationTo(attack->attacker->tile, obj->tile);
            showDamageToObject(obj, attack->extrasDamage[index], attack->extrasFlags[index], attack->weapon, hitFromFront, attack->extrasKnockback[index], knockbackRotation, attackerAnimation, attack->attacker, 0);
            reg_anim_end();
        }
    }

    return 0;
}

// Shows animation to attacker, in case of critical failure, backwash, etc.
static void showDamageToAttacker(Attack* attack, AnimationType attackerAnimation)
{
    showDamageToObject(attack->attacker, attack->attackerDamage, attack->attackerFlags, attack->weapon, true, 0, ROTATION_NE, attackerAnimation, attack->attacker, -1);
}

// Animates damage to defender (or attacker, in some cases) as a result of a given attack.
//
// 0x4110AC show_damage
void showDamage(Attack* attack, AnimationType attackerAnimation, int delay)
{
    for (int index = 0; index < attack->extrasLength; index++) {
        Object* object = attack->extras[index];
        if (FrmId(object).objectType() == OBJ_TYPE_CRITTER) {
            animationRegisterPing(ANIMATION_REQUEST_RESERVED, delay);
            delay = 0;
        }
    }

    if ((attack->attackerFlags & DAM_HIT) == DAM_NONE) {
        if ((attack->attackerFlags & DAM_CRITICAL) != DAM_NONE || (attack->attackerFlags & DAM_BACKWASH) != DAM_NONE) {
            showDamageToAttacker(attack, attackerAnimation);
        }
    } else {
        if (attack->defender != nullptr) {
            // NOTE: Uninline.
            bool hitFromFront = _is_hit_from_front(attack->attacker, attack->defender);

            if (FrmId(attack->defender).objectType() == OBJ_TYPE_CRITTER) {
                Rotation knockbackRotation = tileGetRotationTo(attack->attacker->tile, attack->defender->tile);
                AnimationType attackerAnimForShow = FrmId(attack->attacker) == SceneryFrameId::ForceField3
                    ? attackerAnimation
                    : critterGetAnimationForHitMode(attack->attacker, attack->hitMode);

                showDamageToObject(attack->defender, attack->defenderDamage, attack->defenderFlags, attack->weapon, hitFromFront, attack->defenderKnockback, knockbackRotation, attackerAnimForShow, attack->attacker, delay);
            }
        }

        if ((attack->attackerFlags & DAM_DUD) != DAM_NONE) {
            showDamageToAttacker(attack, attackerAnimation);
        }
    }
}

// 0x411224
int _action_attack(Attack* attack)
{
    if (reg_anim_clear(attack->attacker) == -2) {
        return -1;
    }

    if (reg_anim_clear(attack->defender) == -2) {
        return -1;
    }

    for (int index = 0; index < attack->extrasLength; index++) {
        if (reg_anim_clear(attack->extras[index]) == -2) {
            return -1;
        }
    }

    AnimationType anim = critterGetAnimationForHitMode(attack->attacker, attack->hitMode);
    if (anim < ANIM_FIRE_SINGLE && anim != ANIM_THROW_ANIM) {
        return _action_melee(attack, anim);
    } else {
        return _action_ranged(attack, anim);
    }
}

// 0x4112B4
int _action_melee(Attack* attack, AnimationType anim)
{
    FrmId frmId;
    Art* art;
    CacheEntry* cache_entry;
    int delay;
    const char* sfx_name;
    char sfx_name_temp[16];

    reg_anim_begin(ANIMATION_REQUEST_RESERVED);
    _register_priority(1);

    frmId = FrmId(attack->attacker, anim, attack->attacker->rotation + 1);
    art = artLock(frmId, &cache_entry);
    if (art != nullptr) {
        delay = artGetActionFrame(art);
    } else {
        delay = 0;
    }
    artUnlock(cache_entry);

    tileGetTileInDirection(attack->attacker->tile, attack->attacker->rotation, 1);
    animationRegisterRotateToTile(attack->attacker, attack->defender->tile);

    if (anim != ANIM_THROW_PUNCH && anim != ANIM_KICK_LEG) {
        sfx_name = sfxBuildWeaponName(WEAPON_SOUND_EFFECT_ATTACK, attack->weapon, attack->hitMode, attack->defender);
    } else {
        sfx_name = sfxBuildCharName(attack->attacker, anim, CharacterSoundEffect::Unused);
    }

    strcpy(sfx_name_temp, sfx_name);

    // The viewer replays this sequence from a wire ATTACK_RESULT; the server already
    // streams the real AI taunts as floatText, and the mirror does not faithfully
    // hold the AI packet _combatai_msg reads — so suppress the local chatter (§3.c).
    if (!clientViewerActive()) {
        _combatai_msg(attack->attacker, attack, AI_MESSAGE_TYPE_ATTACK, 0);
    }

    if (attack->attackerFlags & 0x0300) {
        animationRegisterPlaySoundEffect(attack->attacker, sfx_name_temp, 0);
        if (anim != ANIM_THROW_PUNCH && anim != ANIM_KICK_LEG) {
            sfx_name = sfxBuildWeaponName(WEAPON_SOUND_EFFECT_HIT, attack->weapon, attack->hitMode, attack->defender);
        } else {
            sfx_name = sfxBuildCharName(attack->attacker, anim, CharacterSoundEffect::Contact);
        }

        strcpy(sfx_name_temp, sfx_name);

        animationRegisterAnimate(attack->attacker, anim, 0);
        animationRegisterPlaySoundEffect(attack->attacker, sfx_name_temp, delay);
        showDamage(attack, anim, 0);
    } else {
        if (attack->defender->data.critter.combat.results & 0x03) {
            animationRegisterPlaySoundEffect(attack->attacker, sfx_name_temp, -1);
            animationRegisterAnimate(attack->attacker, anim, 0);
        } else {
            frmId = FrmId(attack->defender, ANIM_DODGE_ANIM, attack->defender->rotation + 1);
            art = artLock(frmId, &cache_entry);
            if (art != nullptr) {
                int dodgeDelay = artGetActionFrame(art);
                artUnlock(cache_entry);

                if (dodgeDelay <= delay) {
                    animationRegisterPlaySoundEffect(attack->attacker, sfx_name_temp, -1);
                    animationRegisterAnimate(attack->attacker, anim, 0);

                    sfx_name = sfxBuildCharName(attack->defender, ANIM_DODGE_ANIM, CharacterSoundEffect::Unused);
                    animationRegisterPlaySoundEffect(attack->defender, sfx_name, delay - dodgeDelay);
                    animationRegisterAnimate(attack->defender, ANIM_DODGE_ANIM, 0);
                } else {
                    sfx_name = sfxBuildCharName(attack->defender, ANIM_DODGE_ANIM, CharacterSoundEffect::Unused);
                    animationRegisterPlaySoundEffect(attack->defender, sfx_name, -1);
                    animationRegisterAnimate(attack->defender, ANIM_DODGE_ANIM, 0);
                    animationRegisterPlaySoundEffect(attack->attacker, sfx_name_temp, dodgeDelay - delay);
                    animationRegisterAnimate(attack->attacker, anim, 0);
                }
            }
        }
    }

    if (!clientViewerActive()) { // wire replay: server streams the real taunts (§3.c)
        if ((attack->attackerFlags & DAM_HIT) != 0) {
            if ((attack->defenderFlags & DAM_DEAD) == 0) {
                _combatai_msg(attack->attacker, attack, AI_MESSAGE_TYPE_HIT, -1);
            }
        } else {
            _combatai_msg(attack->attacker, attack, AI_MESSAGE_TYPE_MISS, -1);
        }
    }

    if (reg_anim_end() == -1) {
        return -1;
    }

    showDamageToExtras(attack);

    return 0;
}

// 0x411600
int _action_ranged(Attack* attack, AnimationType anim)
{
    Object* adjacentObjects[ROTATION_COUNT];
    for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
        adjacentObjects[rotation] = nullptr;
    }

    reg_anim_begin(ANIMATION_REQUEST_RESERVED);
    _register_priority(1);

    Object* projectile = nullptr;
    Object* replacedWeapon = nullptr;
    FrmId weaponFrmId = FrmId::Empty();

    Proto* weaponProto;
    Object* weapon = attack->weapon;
    protoGetProto(weapon, &weaponProto);

    const FrmId frmId = FrmId(attack->attacker, anim, attack->attacker->rotation + 1);
    CacheEntry* artHandle;
    Art* art = artLock(frmId, &artHandle);
    int delay = (art != nullptr) ? artGetActionFrame(art) : 0;
    artUnlock(artHandle);

    weaponGetRange(attack->attacker, attack->hitMode);

    DamageType damageType = weaponGetDamageType(attack->attacker, attack->weapon);

    tileGetTileInDirection(attack->attacker->tile, attack->attacker->rotation, 1);

    animationRegisterRotateToTile(attack->attacker, attack->defender->tile);

    bool isGrenade = false;
    if (anim == ANIM_THROW_ANIM) {
        // SFALL
        if (damageType == explosionGetDamageType() || damageType == DAMAGE_TYPE_PLASMA || damageType == DAMAGE_TYPE_EMP) {
            isGrenade = true;
        }
    } else {
        animationRegisterAnimate(attack->attacker, ANIM_POINT, -1);
    }

    if (!clientViewerActive()) { // wire replay: server streams the real taunts (§3.c)
        _combatai_msg(attack->attacker, attack, AI_MESSAGE_TYPE_ATTACK, 0);
    }

    const char* sfx;
    if ((FrmId(attack->attacker).weaponAnimation()) != WeaponAnimation::None) {
        sfx = sfxBuildWeaponName(WEAPON_SOUND_EFFECT_ATTACK, weapon, attack->hitMode, attack->defender);
    } else {
        sfx = sfxBuildCharName(attack->attacker, anim, CharacterSoundEffect::Unused);
    }
    animationRegisterPlaySoundEffect(attack->attacker, sfx, -1);

    animationRegisterAnimate(attack->attacker, anim, 0);

    if (anim != ANIM_FIRE_CONTINUOUS) {
        if ((attack->attackerFlags & DAM_HIT) != DAM_NONE || (attack->attackerFlags & DAM_CRITICAL) == DAM_NONE) {
            bool l56 = false;

            const ProtoId projectileProtoId = weaponGetProjectileProtoId(weapon);
            Proto* projectileProto;
            if (protoGetProto(projectileProtoId, &projectileProto) != -1 && FrmId(projectileProto).valid()) {
                if (anim == ANIM_THROW_ANIM && !presRecordActive()) {
                    projectile = weapon;
                    weaponFrmId = FrmId(weapon);
                    ObjectFlags weaponFlags = weapon->flags;
                    // CE: Read before _cAIPrepWeaponItem() lights an unlit flare.
                    const ProtoId weaponPid(weapon);

                    InterfaceItemAction leftItemAction;
                    InterfaceItemAction rightItemAction;
                    interfaceGetItemActions(&leftItemAction, &rightItemAction);

                    itemRemoveWithReason(attack->attacker, weapon, 1, RemoveInventoryObjectHookReason::Throw);
                    replacedWeapon = itemReplace(attack->attacker, weapon, weaponFlags & OBJECT_IN_ANY_HAND);
                    objectSetFrmId(projectile, projectileProto, nullptr);
                    _cAIPrepWeaponItem(attack->attacker, weapon);

                    // CE: Restore the thrower's light after throwing a held lit flare.
                    if (weaponPid == ItemProtoTypeId::LitFlare && (weaponFlags & OBJECT_IN_ANY_HAND) != OBJECT_NONE) {
                        critterRestoreLightWithoutFlare(attack->attacker);
                    }

                    if (attack->attacker == gDude) {
                        if (replacedWeapon == nullptr) {
                            if ((weaponFlags & OBJECT_IN_LEFT_HAND) != OBJECT_NONE) {
                                leftItemAction = INTERFACE_ITEM_ACTION_DEFAULT;
                            } else if ((weaponFlags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
                                rightItemAction = INTERFACE_ITEM_ACTION_DEFAULT;
                            }
                        }
                        presenter()->hudItems(false, leftItemAction, rightItemAction);
                    }

                    _obj_connect(weapon, attack->attacker->tile, attack->attacker->elevation, nullptr);
                } else {
                    // Non-throw ranged, OR a RECORDED throw. For a recorded throw the
                    // authoritative consumption + landed ground weapon are applied by
                    // actionThrowConsumeHeadless on the STATE arm (combat.cc) — so we must
                    // NOT mutate the attacker's inventory here; we only build the flight
                    // PRESENTATION with a transient projectile, exactly like a non-throw
                    // ranged shot (the common flight/explosion code below then streams it,
                    // and the leak-fix destroys it after the section). weaponFid feeds the
                    // tail's land-as-weapon setFid for a thrown weapon; it stays -1 for
                    // bullets, where the tail never reads it.
                    if (anim == ANIM_THROW_ANIM) {
                        weaponFrmId = FrmId(weapon);
                    }
                    objectCreateWithFrmIdProtoId(&projectile, FrmId(projectileProto), ProtoId::Empty());

                    // PRESENTATION-RECORD: resolveRef runs DURING the record section and
                    // needs NO_SAVE (+ created-this-beat) to classify the projectile as a
                    // transient and ship it as OBJ_CREATE; vanilla sets NO_SAVE only later
                    // in hideProjectile, too late for the recorder. Mark it now so its pose
                    // streams (record-server only; SP is untouched — presRecordActive() is
                    // false there). Destroyed after the section closes (below, leak fix).
                    if (presRecordActive()) {
                        projectile->flags |= OBJECT_NO_SAVE;
                        // A NON-explosive thrown weapon becomes a ground item: the STATE arm
                        // (actionThrowConsumeHeadless) _obj_connects the real weapon by netId,
                        // and the AI later picks it up (disconnect). Map that netId onto this
                        // flight transient so the disconnect removes it on the viewer — else
                        // the spear phantoms and accumulates. Explosive throws (grenades) are
                        // destroyed by the blast, not connected, so they must NOT adopt.
                        if (anim == ANIM_THROW_ANIM) {
                            bool explosive = damageType == explosionGetDamageType()
                                || damageType == DAMAGE_TYPE_PLASMA || damageType == DAMAGE_TYPE_EMP;
                            if (!explosive) {
                                presRecordSetAdoptNetId(projectile, weapon->netId);
                            }
                        }
                    }
                }

                objectHide(projectile, nullptr);

                // SFALL
                if (explosionEmitsLight() && projectile->lightIntensity == 0) {
                    objectSetLight(projectile, projectileProto->item.lightDistance, projectileProto->item.lightIntensity, nullptr);
                } else {
                    objectSetLight(projectile, 9, projectile->lightIntensity, nullptr);
                }

                int projectileOrigin = _combat_bullet_start(attack->attacker, attack->defender);
                objectSetLocation(projectile, projectileOrigin, attack->attacker->elevation, nullptr);

                Rotation projectileRotation = tileGetRotationTo(attack->attacker->tile, attack->defender->tile);
                objectSetRotation(projectile, projectileRotation, nullptr);

                animationRegisterUnsetFlag(projectile, OBJECT_HIDDEN, delay);

                const char* sfx = sfxBuildWeaponName(WEAPON_SOUND_EFFECT_AMMO_FLYING, weapon, attack->hitMode, attack->defender);
                animationRegisterPlaySoundEffect(projectile, sfx, 0);

                int explosionCenterTile;
                if ((attack->attackerFlags & DAM_HIT) != DAM_NONE) {
                    animationRegisterMoveToTileStraight(projectile, attack->defender->tile, attack->defender->elevation, ANIM_WALK, 0);
                    delay = _make_straight_path(projectile, projectileOrigin, attack->defender->tile, nullptr, nullptr, 32) - 1;
                    explosionCenterTile = attack->defender->tile;
                } else {
                    animationRegisterMoveToTileStraight(projectile, attack->tile, attack->defender->elevation, ANIM_WALK, 0);
                    delay = 0;
                    explosionCenterTile = attack->tile;
                }

                // SFALL
                if (isGrenade || damageType == explosionGetDamageType()) {
                    if ((attack->attackerFlags & DAM_DROP) == DAM_NONE) {
                        MiscFrmId explosionFrmId;
                        if (isGrenade) {
                            switch (damageType) {
                            case DAMAGE_TYPE_EMP:
                                explosionFrmId = MiscFrameId::EmpExplosion;
                                break;
                            case DAMAGE_TYPE_PLASMA:
                                explosionFrmId = MiscFrameId::PlasmaExplosion;
                                break;
                            default:
                                explosionFrmId = MiscFrameId::FireExplosion;
                                break;
                            }
                        } else {
                            explosionFrmId = MiscFrameId::RocketExplosion;
                        }

                        // SFALL
                        const MiscFrmId explosionFrmIdOverride = explosionGetFrmId();
                        if (explosionFrmIdOverride.valid()) {
                            explosionFrmId = explosionFrmIdOverride;
                        }

                        if (isGrenade) {
                            animationRegisterSetFrmId(projectile, weaponFrmId, -1);
                        }

                        animationRegisterSetFrmId(projectile, explosionFrmId, -1);

                        const char* sfx = sfxBuildWeaponName(WEAPON_SOUND_EFFECT_HIT, weapon, attack->hitMode, attack->defender);
                        animationRegisterPlaySoundEffect(projectile, sfx, 0);

                        // SFALL
                        if (explosionEmitsLight()) {
                            animationRegisterAnimate(projectile, ANIM_STAND, 0);
                            // 0xFFFF0008
                            // - distance: 8
                            // - intensity: 65535
                            animationRegisterSetLightIntensity(projectile, 8, 65536, 0);
                        } else {
                            animationRegisterAnimateAndHide(projectile, ANIM_STAND, 0);
                        }

                        // SFALL
                        Rotation startRotation;
                        Rotation endRotation;
                        explosionGetPattern(&startRotation, &endRotation);

                        for (Rotation rotation = startRotation; rotation < endRotation; rotation++) {
                            if (objectCreateWithFrmIdProtoId(&(adjacentObjects[rotation]), explosionFrmId, ProtoId::Empty()) != -1) {
                                objectHide(adjacentObjects[rotation], nullptr);

                                int adjacentTile = tileGetTileInDirection(explosionCenterTile, rotation, 1);
                                objectSetLocation(adjacentObjects[rotation], adjacentTile, projectile->elevation, nullptr);

                                int delay;
                                if (rotation != ROTATION_NE) {
                                    delay = 0;
                                } else {
                                    if (damageType == DAMAGE_TYPE_PLASMA) {
                                        delay = 4;
                                    } else {
                                        delay = 2;
                                    }
                                }

                                animationRegisterUnsetFlag(adjacentObjects[rotation], OBJECT_HIDDEN, delay);
                                animationRegisterAnimateAndHide(adjacentObjects[rotation], ANIM_STAND, 0);
                            }
                        }

                        l56 = true;
                    }
                } else {
                    if (anim != ANIM_THROW_ANIM) {
                        animationRegisterHideObjectForced(projectile);
                    }
                }

                if (!l56) {
                    const char* sfx = sfxBuildWeaponName(WEAPON_SOUND_EFFECT_HIT, weapon, attack->hitMode, attack->defender);
                    animationRegisterPlaySoundEffect(weapon, sfx, delay);
                }

                delay = 0;
            } else {
                if ((attack->attackerFlags & DAM_HIT) == DAM_NONE) {
                    Object* defender = attack->defender;
                    if ((defender->data.critter.combat.results & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) == 0) {
                        animationRegisterAnimate(defender, ANIM_DODGE_ANIM, delay);
                        l56 = true;
                    }
                }
            }
        }
    }

    showDamage(attack, anim, delay);

    if (!clientViewerActive()) { // wire replay: server streams the real taunts (§3.c)
        if ((attack->attackerFlags & DAM_HIT) == 0) {
            _combatai_msg(attack->defender, attack, AI_MESSAGE_TYPE_MISS, -1);
        } else {
            if ((attack->defenderFlags & DAM_DEAD) == 0) {
                _combatai_msg(attack->defender, attack, AI_MESSAGE_TYPE_HIT, -1);
            }
        }
    }

    // SFALL
    if (projectile != nullptr && (isGrenade || damageType == explosionGetDamageType())) {
        // CE: Use custom callback to hide projectile instead of relying on
        // `animationRegisterHideObjectForced`. The problem is that completing
        // `ANIM_KIND_HIDE` removes (frees) object entirely. When this happens
        // `attack->weapon` becomes a dangling pointer, but this object is
        // needed to process `damage_p_proc` by scripting engine which can
        // interrogate weapon's properties (leading to crash on some platforms).
        // So instead of removing projectile follow a pattern established in
        // `opDestroyObject` for self-deleting objects (mark it hidden +
        // no-save).
        animationRegisterCallbackForced(attack, projectile, hideProjectile, -1);
    } else if (anim == ANIM_THROW_ANIM && projectile != nullptr) {
        animationRegisterSetFrmId(projectile, weaponFrmId, -1);
    }

    for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
        if (adjacentObjects[rotation] != nullptr) {
            animationRegisterHideObjectForced(adjacentObjects[rotation]);
        }
    }

    if ((attack->attackerFlags & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN | DAM_DEAD)) == 0) {
        if (anim == ANIM_THROW_ANIM) {
            bool takeOutAnimationRegistered = false;
            if (replacedWeapon != nullptr) {
                WeaponAnimation weaponAnimationCode = weaponGetAnimationCode(replacedWeapon);
                if (weaponAnimationCode != WeaponAnimation::None) {
                    animationRegisterTakeOutWeapon(attack->attacker, weaponAnimationCode, -1);
                    takeOutAnimationRegistered = true;
                }
            }

            if (!takeOutAnimationRegistered) {
                const FrmId frmId = FrmId(attack->attacker, ANIM_STAND, WeaponAnimation::None, attack->attacker->rotation + 1);
                animationRegisterSetFrmId(attack->attacker, frmId, -1);
            }
        } else {
            animationRegisterAnimate(attack->attacker, ANIM_UNPOINT, -1);
        }
    }

    if (reg_anim_end() == -1) {
        debugPrint("Something went wrong with a ranged attack sequence!\n");
        if (projectile != nullptr && (isGrenade || damageType == DAMAGE_TYPE_EXPLOSION || anim != ANIM_THROW_ANIM)) {
            objectDestroy(projectile, nullptr);
        }

        for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
            if (adjacentObjects[rotation] != nullptr) {
                objectDestroy(adjacentObjects[rotation], nullptr);
            }
        }

        return -1;
    }

    showDamageToExtras(attack);

    // PRESENTATION-RECORD leak fix: under record the animate branch's completion
    // callbacks are DROPped server-side (the viewer destroys the projectile via its own
    // anim completion; the record server has no completion), so the transient projectile
    // + any explosion clouds created above would leak on the record server (no_save
    // count grows → run_record_purity FAIL). Their OBJ_CREATE poses are already captured
    // in the stream, so free them now that the section has recorded. NON-THROW ONLY by
    // construction — a throw is never recorded (combat.cc opens no section for it), so
    // presRecordActive() is false and the real inventory weapon used as a throw
    // projectile is never touched here. Mirrors the reg_anim_end()==-1 cleanup and
    // actionExplode's STATE-block cloud destroy.
    if (presRecordActive()) {
        if (projectile != nullptr) {
            objectDestroy(projectile, nullptr);
        }
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            if (adjacentObjects[rotation] != nullptr) {
                objectDestroy(adjacentObjects[rotation], nullptr);
            }
        }
    }

    return 0;
}

// 0x411D68
int _is_next_to(Object* obj1, Object* obj2)
{
    if (objectGetDistanceBetween(obj1, obj2) > 1) {
        if (obj2 == gDude) {
            MessageListItem messageListItem;
            // You cannot get there.
            messageListItem.num = 2000;
            if (messageListGetItem(&gMiscMessageList, &messageListItem)) {
                presenter()->consoleMessage(messageListItem.text);
            }
        }
        return -1;
    }

    return 0;
}

// 0x411DB4
int _action_climb_ladder(Object* critter, Object* ladder)
{
    if (critter == gDude) {
        AnimationType anim = FrmId(gDude).animationType();
        if (anim == ANIM_WALK || anim == ANIM_RUNNING) {
            reg_anim_clear(gDude);
        }
    }

    AnimationRequestOptions animationRequestOptions;
    int actionPoints;
    if (isInCombat()) {
        animationRequestOptions = ANIMATION_REQUEST_RESERVED;
        // SFALL: Fix for the player's movement in combat being interrupted
        // when trying to use objects with Bonus Move APs available: the walk
        // may take the free move points too (the player's, 0 out of his turn).
        actionPoints = critter->data.critter.combat.ap + _combat_free_move;
    } else {
        animationRequestOptions = ANIMATION_REQUEST_UNRESERVED;
        actionPoints = -1;
    }

    if (critter == gDude) {
        animationRequestOptions = ANIMATION_REQUEST_RESERVED;
    }

    int destinationMap = ladder->data.scenery.ladder.destinationMap;
    int destinationBuiltTile = ladder->data.scenery.ladder.destinationBuiltTile;
    if (destinationBuiltTile == -1 || (destinationMap != 0 && destinationMap != mapGetCurrentMap())) {
        animationRequestOptions |= ANIMATION_REQUEST_NO_STAND;
    }

    reg_anim_begin(animationRequestOptions);

    // note: distance is checked to ladder tile, but movement is to SE tile, so the walk/run choice
    // could be slightly off
    int tile = tileGetTileInDirection(ladder->tile, ROTATION_SE, 1);
    if (actionPoints != -1 || objectWithinWalkDistance(critter, ladder)) {
        animationRegisterMoveToTile(critter, tile, ladder->elevation, actionPoints, 0);
    } else {
        animationRegisterRunToTile(critter, tile, ladder->elevation, actionPoints, 0);
    }

    animationRegisterCallbackForced(critter, ladder, (AnimationCallback*)_is_next_to, -1);
    animationRegisterRotateToTile(critter, ladder->tile);
    animationRegisterCallbackForced(critter, ladder, (AnimationCallback*)checkSceneryUseActionPointCost, -1);

    WeaponAnimation weaponAnimationCode = FrmId(critter).weaponAnimation();
    if (weaponAnimationCode != WeaponAnimation::None) {
        const char* puttingAwaySfx = sfxBuildCharName(critter, ANIM_PUT_AWAY, CharacterSoundEffect::Unused);
        animationRegisterPlaySoundEffect(critter, puttingAwaySfx, -1);
        animationRegisterAnimate(critter, ANIM_PUT_AWAY, 0);
    }

    const char* climbingSfx = sfxBuildCharName(critter, ANIM_CLIMB_LADDER, CharacterSoundEffect::Unused);
    animationRegisterPlaySoundEffect(critter, climbingSfx, -1);
    animationRegisterAnimate(critter, ANIM_CLIMB_LADDER, 0);
    animationRegisterCallback(critter, ladder, (AnimationCallback*)objectUse, -1);

    if (weaponAnimationCode != WeaponAnimation::None) {
        animationRegisterTakeOutWeapon(critter, weaponAnimationCode, -1);
    }

    return reg_anim_end();
}

// 0x411F2C
int _action_use_an_item_on_object(Object* user, Object* targetObj, Object* item)
{
    Proto* proto = nullptr;
    ObjectType type = FrmId(targetObj).objectType();
    int sceneryType = -1;
    if (type == OBJ_TYPE_SCENERY) {
        if (protoGetProto(targetObj, &proto) == -1) {
            return -1;
        }

        sceneryType = proto->scenery.type;
    }

    if (serverLoopActive()) {
        // Headless: the outcome (objectUse for use-object/ladder/stairs/door, or
        // objectUseItemOn for use-item-on-object) is only reached via a
        // reg_anim completion callback that never fires — no animation drives
        // it — so this action was a silent no-op on the server path. Both
        // handlers run synchronously in-call, so invoke them directly; mirrors
        // objectUseDoor / actionPickUp. objectUse itself dispatches ladders and
        // stairs to useLadder*/useStairs (the SCENERY_TYPE_LADDER_UP special
        // case that the legacy path routes through _action_climb_ladder), and
        // doors to objectUseDoor. objectUse ignores adjacency, which is the
        // intended headless behavior (no walk-to).
        if (item != nullptr) {
            return objectUseItemOn(user, targetObj, item);
        }
        // Charge scenery AP as the legacy item==nullptr path does. HONOR the
        // result: a refusal (not enough AP — it streams proto msg 700 itself)
        // means the use does not happen. This return was discarded while the
        // comment here could still say "a no-op out of combat, which the headless
        // dude always is" — true until in-combat interaction became reachable
        // over the wire, at which point ignoring it meant a player with 0 AP
        // still opened the door, for free, on someone else's turn.
        if (checkSceneryUseActionPointCost(user, targetObj) == -1) {
            return -1;
        }
        return objectUse(user, targetObj);
    }

    if (sceneryType != SCENERY_TYPE_LADDER_UP || item != nullptr) {
        if (user == gDude) {
            AnimationType anim = FrmId(gDude).animationType();
            if (anim == ANIM_WALK || anim == ANIM_RUNNING) {
                reg_anim_clear(gDude);
            }
        }

        AnimationRequestOptions animationRequestOptions;
        int actionPoints;
        if (isInCombat()) {
            animationRequestOptions = ANIMATION_REQUEST_RESERVED;
            // SFALL: Fix for the player's movement in combat being
            // interrupted when trying to use objects with Bonus Move APs
            // available (see `_action_climb_ladder`).
            actionPoints = user->data.critter.combat.ap + _combat_free_move;
        } else {
            animationRequestOptions = ANIMATION_REQUEST_UNRESERVED;
            actionPoints = -1;
        }

        if (user == gDude) {
            animationRequestOptions = ANIMATION_REQUEST_RESERVED;
        }

        reg_anim_begin(animationRequestOptions);

        if (actionPoints != -1 || objectWithinWalkDistance(user, targetObj)) {
            animationRegisterMoveToObject(user, targetObj, actionPoints, 0);
        } else {
            animationRegisterRunToObject(user, targetObj, -1, 0);
        }

        animationRegisterCallbackForced(user, targetObj, (AnimationCallback*)_is_next_to, -1);

        if (item == nullptr) {
            animationRegisterCallback(user, targetObj, (AnimationCallback*)checkSceneryUseActionPointCost, -1);
        }

        WeaponAnimation weaponAnimCode = FrmId(user).weaponAnimation();
        if (weaponAnimCode != WeaponAnimation::None) {
            const char* sfx = sfxBuildCharName(user, ANIM_PUT_AWAY, CharacterSoundEffect::Unused);
            animationRegisterPlaySoundEffect(user, sfx, -1);
            animationRegisterAnimate(user, ANIM_PUT_AWAY, 0);
        }

        AnimationType anim;
        ObjectType objectType = FrmId(targetObj).objectType();
        if (objectType == OBJ_TYPE_CRITTER && critterIsProne(targetObj)) {
            anim = ANIM_MAGIC_HANDS_GROUND;
        } else if (objectType == OBJ_TYPE_SCENERY && (proto->scenery.extendedFlags & PROTO_EXT_FLAG_MAGIC_HANDS_GROUND) != PROTO_EXT_FLAG_NONE) {
            anim = ANIM_MAGIC_HANDS_GROUND;
        } else {
            anim = ANIM_MAGIC_HANDS_MIDDLE;
        }

        if (sceneryType != SCENERY_TYPE_STAIRS && item == nullptr) {
            actionRegisterUseAnimObj(user, targetObj, &anim, -1);
        }

        if (item != nullptr) {
            // TODO: Get rid of cast.
            animationRegisterCallback3(user, targetObj, item, (AnimationCallback3*)objectUseItemOn, -1);
        } else {
            animationRegisterCallback(user, targetObj, (AnimationCallback*)objectUse, -1);
        }

        if (weaponAnimCode != WeaponAnimation::None) {
            animationRegisterTakeOutWeapon(user, weaponAnimCode, -1);
        }

        return reg_anim_end();
    }

    return _action_climb_ladder(user, targetObj);
}

// 0x412114
int _action_use_an_object(Object* user, Object* targetObj)
{
    return _action_use_an_item_on_object(user, targetObj, nullptr);
}

// 0x412134
int actionPickUp(Object* critter, Object* item)
{
    if (FrmId(item).objectType() != OBJ_TYPE_ITEM) {
        return -1;
    }

    Proto* itemProto;
    protoGetProto(ProtoId(item), &itemProto);
    bool canPickup = itemProto->item.type != ITEM_TYPE_CONTAINER || _proto_action_can_pickup(ProtoId(item));

    // PRESENTATION-RECORD (pickup gesture): on the dedicated server IN COMBAT, RUN the
    // normally-skipped animate branch — the walk to the item + the crouch-and-grab
    // ANIM_MAGIC_HANDS_GROUND — inside a record section so it streams as EVENT_PRES_SEQ,
    // then apply the pickup outcome authoritatively below (the animate branch's objectPickup
    // callback DROPs under record). Ground items only (a container pickup opens a modal not
    // driven here). Flag off / SP / out of combat → the state-only fast path below (goldens
    // unchanged). See COMBAT_MOVE_RECORD_DESIGN.md.
    // The adopt-mapping (throw transient -> real netId) now registers at the seq's DECODE
    // pass (client_net.cc OBJ_CREATE), so the thrown weapon is in _net before this pickup's
    // state-lane DISCONNECT arrives regardless of pump lag — the phantom-spear regression
    // that forced this off is fixed. Ground items only (a container pickup opens a modal not
    // driven here). Flag off / SP / out of combat -> the state-only fast path below.
    bool recording = serverLoopActive() && presRecordEnabled() && isInCombat()
        && !presRecordActive() && canPickup;

    if (serverLoopActive() && !recording) {
        // Headless: the reg_anim completion callbacks that carry the pickup outcome never
        // fire (no animation drives them), so apply the outcome directly. Container pickup
        // opens the loot UI (a modal not driven headless) — decline rather than hang.
        if (canPickup) {
            // ►► The AP result is DELIBERATELY IGNORED here, and must stay that
            // way until the asymmetry below is fixed. See the record-purity note
            // in the `recording` branch: this path does not walk and that one
            // does, so the two do not agree on the critter's AP by the time the
            // pickup is decided. Honoring the refusal on either side alone makes
            // record mode observable in the simulation, which the record-purity
            // melee gate catches immediately (verified 2026-07-20).
            checkSceneryUseActionPointCost(critter, item);
            return objectPickup(critter, item);
        }
        return -1;
    }

    if (recording) {
        presRecordAmbientBegin();
    }

    if (critter == gDude) {
        AnimationType animationCode = FrmId(gDude).animationType();
        if (animationCode == ANIM_WALK || animationCode == ANIM_RUNNING) {
            reg_anim_clear(gDude);
        }
    }

    if (isInCombat()) {
        reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        animationRegisterMoveToObject(critter, item, critter->data.critter.combat.ap, 0);
    } else {
        reg_anim_begin(critter == gDude ? ANIMATION_REQUEST_RESERVED : ANIMATION_REQUEST_UNRESERVED);
        if (!objectWithinWalkDistance(critter, item)) {
            animationRegisterRunToObject(critter, item, -1, 0);
        } else {
            animationRegisterMoveToObject(critter, item, -1, 0);
        }
    }

    animationRegisterCallbackForced(critter, item, (AnimationCallback*)_is_next_to, -1);
    animationRegisterCallback(critter, item, (AnimationCallback*)checkSceneryUseActionPointCost, -1);

    if (canPickup) {
        animationRegisterAnimate(critter, ANIM_MAGIC_HANDS_GROUND, 0);

        const FrmId frmId = FrmId(critter, ANIM_MAGIC_HANDS_GROUND, critter->rotation + 1);

        int actionFrame;
        CacheEntry* cacheEntry;
        Art* art = artLock(frmId, &cacheEntry);
        if (art != nullptr) {
            actionFrame = artGetActionFrame(art);
        } else {
            actionFrame = -1;
        }

        char sfx[16];
        if (artCopyFileName(FrmId(item), sfx) == 0) {
            // NOTE: looks like they copy sfx one more time, what for?
            animationRegisterPlaySoundEffect(item, sfx, actionFrame);
        }

        animationRegisterCallback(critter, item, (AnimationCallback*)objectPickup, actionFrame);
    } else {
        WeaponAnimation weaponAnimationCode = FrmId(critter).weaponAnimation();
        if (weaponAnimationCode != WeaponAnimation::None) {
            const char* sfx = sfxBuildCharName(critter, ANIM_PUT_AWAY, CharacterSoundEffect::Unused);
            animationRegisterPlaySoundEffect(critter, sfx, -1);
            animationRegisterAnimate(critter, ANIM_PUT_AWAY, -1);
        }

        // ground vs middle animation
        AnimationType anim = (itemProto->item.data.container.openFlags & CONTAINER_FLAG_OPEN) == 0
            ? ANIM_MAGIC_HANDS_MIDDLE
            : ANIM_MAGIC_HANDS_GROUND;
        bool animateUse = actionRegisterUseAnimObj(critter, item, &anim, 0);

        const FrmId frmId = FrmId(critter, anim, WeaponAnimation::None, critter->rotation + 1);

        int actionFrame = -1;
        CacheEntry* cacheEntry;
        if (animateUse) {
            Art* art = artLock(frmId, &cacheEntry);
            if (art != nullptr) {
                actionFrame = artGetActionFrame(art);
                artUnlock(cacheEntry);
            }
        }

        if (item->frame != 1) {
            animationRegisterCallback(critter, item, (AnimationCallback*)objectUseContainer, actionFrame);
        }

        if (weaponAnimationCode != WeaponAnimation::None) {
            animationRegisterTakeOutWeapon(critter, weaponAnimationCode, -1);
        }

        if (item->frame == 0 || item->frame == 1) {
            animationRegisterCallback(critter, item, (AnimationCallback*)scriptsRequestLooting, -1);
        }
    }

    int rc = reg_anim_end();

    if (recording) {
        presRecordAmbientEnd();
        if (presRecordOpCount() > 2) {
            presenter()->presSeq(presRecordData(), presRecordSize(), presRecordOpCount(), critter->netId);
        }
        presRecordCommitDeferred(); // the MoveToObject walk to the item
        // Apply the pickup outcome authoritatively — the recorded objectPickup /
        // checkSceneryUseActionPointCost callbacks DROP under record (the server never runs a
        // recorded reg_anim's callbacks), so the state must be applied here.
        //
        // ►► KNOWN PRE-EXISTING ASYMMETRY, do not "fix" by honoring this return.
        // This path registers an approach walk before charging; the non-record
        // path above charges without walking. The two therefore disagree about
        // the critter's AP at this point. While both sides IGNORE the refusal the
        // divergence stays invisible (the pickup happens either way), which is
        // why the record-purity gate has always passed. Honoring it on either
        // side alone makes record mode observable in the sim and fails that gate
        // (verified 2026-07-20). Making pickup respect AP properly means removing
        // the walk asymmetry first — banked, not done here.
        checkSceneryUseActionPointCost(critter, item);
        bool traceP = getenv("F2_TRACE_EVENTS") != nullptr;
        if (traceP) fprintf(stderr, "[cpickup] critter=%d item_net=%d item_tile=%d ops=%d\n",
            critter->netId, item->netId, item->tile, presRecordOpCount());
        int pr = objectPickup(critter, item);
        if (traceP) fprintf(stderr, "[cpickup] done rc=%d item_tile_now=%d\n", pr, item->tile);
    }

    return rc;
}

// This is primarily used to loot critters, namely corpses.  I don't believe it can
// be called with a living creature, but am not 100% certain.
//
// 0x4123E8 was _action_loot_container
int actionLootCritter(Object* critter, Object* target)
{
    if (FrmId(target).objectType() != OBJ_TYPE_CRITTER) {
        return -1;
    }

    // SFALL: Fix for trying to loot corpses with the "NoSteal" flag.
    if (critterFlagCheck(target, CRITTER_NO_STEAL)) {
        return -1;
    }

    if (critter == gDude) {
        AnimationType anim = FrmId(gDude).animationType();
        if (anim == ANIM_WALK || anim == ANIM_RUNNING) {
            reg_anim_clear(gDude);
        }
    }

    if (isInCombat()) {
        reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        animationRegisterMoveToObject(critter, target, critter->data.critter.combat.ap, 0);
    } else {
        reg_anim_begin(critter == gDude ? ANIMATION_REQUEST_RESERVED : ANIMATION_REQUEST_UNRESERVED);

        if (objectWithinWalkDistance(critter, target)) {
            animationRegisterMoveToObject(critter, target, -1, 0);
        } else {
            animationRegisterRunToObject(critter, target, -1, 0);
        }
    }

    animationRegisterCallbackForced(critter, target, (AnimationCallback*)_is_next_to, -1);
    animationRegisterCallback(critter, target, (AnimationCallback*)checkSceneryUseActionPointCost, -1);
    animationRegisterCallback(critter, target, (AnimationCallback*)scriptsRequestLooting, -1);
    return reg_anim_end();
}

// 0x4124E0
int _action_skill_use(Skill skill)
{
    if (skill == SKILL_SNEAK) {
        reg_anim_clear(gDude);
        dudeToggleState(DUDE_STATE_SNEAKING);
        return 0;
    }

    return -1;
}

// NOTE: Inlined.
//
// 0x412500
static int _action_use_skill_in_combat_error(Object* critter)
{
    MessageListItem messageListItem;

    // Any player actor, addressed to them — see checkSceneryUseActionPointCost for why
    // the gDude test had to be generalized and why a refusal is not broadcast.
    if (playerActorIs(critter)) {
        messageListItem.num = 902;
        if (messageListGetItem(&gProtoMessageList, &messageListItem) == 1) {
            presenter()->consoleMessageFor(critter->netId, messageListItem.text);
        }
    }

    return -1;
}

// skill_use
// 0x41255C
int actionUseSkill(Object* user, Object* target, Skill skill)
{
    UseSkillOnHookResult hookResult = scriptHooks_UseSkillOn(&user, target, skill);
    if (!hookResult.shouldContinue) {
        return -1;
    }

    ObjectType targetObjectType = ProtoId(target).objectType();
    switch (skill) {
    case SKILL_FIRST_AID:
    case SKILL_DOCTOR:
        if (isInCombat() && !hookResult.allowInCombat) {
            // NOTE: Uninline.
            return _action_use_skill_in_combat_error(user);
        }

        if (targetObjectType != OBJ_TYPE_CRITTER) {
            return -1;
        }
        break;
    case SKILL_LOCKPICK:
        if (isInCombat() && !hookResult.allowInCombat) {
            // NOTE: Uninline.
            return _action_use_skill_in_combat_error(user);
        }

        if (targetObjectType != OBJ_TYPE_ITEM && targetObjectType != OBJ_TYPE_SCENERY) {
            return -1;
        }

        break;
    case SKILL_STEAL:
        if (isInCombat() && !hookResult.allowInCombat) {
            // NOTE: Uninline.
            return _action_use_skill_in_combat_error(user);
        }

        if (targetObjectType != OBJ_TYPE_ITEM && targetObjectType != OBJ_TYPE_CRITTER) {
            return -1;
        }

        if (target == user) {
            return -1;
        }

        break;
    case SKILL_TRAPS:
        if (isInCombat() && !hookResult.allowInCombat) {
            // NOTE: Uninline.
            return _action_use_skill_in_combat_error(user);
        }

        if (targetObjectType == OBJ_TYPE_CRITTER) {
            return -1;
        }

        break;
    case SKILL_SCIENCE:
    case SKILL_REPAIR:
        if (isInCombat() && !hookResult.allowInCombat) {
            // NOTE: Uninline.
            return _action_use_skill_in_combat_error(user);
        }

        if (targetObjectType != OBJ_TYPE_CRITTER) {
            break;
        }

        if (critterGetKillType(target) == KILL_TYPE_ROBOT) {
            break;
        }

        if (critterGetKillType(target) == KILL_TYPE_BRAHMIN
            && skill == SKILL_SCIENCE) {
            break;
        }

        // SFALL: Science on critters patch.
        if (1) {
            ScienceRepairTargetType targetType = SCIENCE_REPAIR_TARGET_TYPE_DEFAULT;
            configGetEnum<ScienceRepairTargetType>(&gContentConfig, CONTENT_CONFIG_COMBAT_SECTION, "science_on_critters", &targetType, SCIENCE_REPAIR_TARGET_TYPE_DEFAULT);
            if (targetType == SCIENCE_REPAIR_TARGET_TYPE_DUDE) {
                if (target == gDude) {
                    break;
                }
            } else if (targetType == SCIENCE_REPAIR_TARGET_TYPE_ANYONE) {
                break;
            }
        }

        return -1;
    case SKILL_SNEAK:
        // The user's own state — this is reached from the server's `skill` verb too,
        // where `user` is the acting player and not necessarily the host.
        dudeToggleState(DUDE_STATE_SNEAKING, user);
        return 0;
    default:
        debugPrint("\nskill_use: invalid skill used.");
    }

    // Performer is either the acting player, or the party member who's best at the
    // specified skill in the entire party, and this skill is his/her own best.
    Object* performer = user != nullptr ? user : gDude;

    if (playerActorIs(user) && !hookResult.userOverridden) {
        Object* partyMember = partyMemberGetBestInSkill(skill, user);

        if (partyMember == user) {
            partyMember = nullptr;
        }

        // Only dude can perform stealing.
        if (skill == SKILL_STEAL) {
            partyMember = nullptr;
        }

        if (partyMember != nullptr) {
            if (partyMemberGetBestSkill(partyMember) != skill) {
                partyMember = nullptr;
            }
        }

        if (partyMember != nullptr) {
            performer = partyMember;
            AnimationType anim = FrmId(partyMember).animationType();
            if (anim != ANIM_WALK && anim != ANIM_RUNNING) {
                if (anim != ANIM_STAND) {
                    performer = user;
                    partyMember = nullptr;
                }
            } else {
                reg_anim_clear(partyMember);
            }
        }

        if (partyMember != nullptr) {
            // "Is the acting player already standing on it?" — if so they do it
            // themselves rather than sending a companion over.
            bool isDude = false;
            if (objectGetDistanceBetween(user, target) <= 1) {
                isDude = true;
            }

            char* msg = skillsGetGenericResponse(partyMember, isDude);

            presenter()->floatText(partyMember, msg, 101, COLOR_LIGHT_YELLOW, COLOR_BLACK);

            if (isDude) {
                performer = user;
                partyMember = nullptr;
            }
        }

        if (partyMember == nullptr) {
            AnimationType anim = FrmId(performer).animationType();
            if (anim == ANIM_WALK || anim == ANIM_RUNNING) {
                reg_anim_clear(performer);
            }
        }
    }

    if (serverLoopActive()) {
        // Headless: the skill outcome (objectUseSkillOn) is welded to a reg_anim
        // completion callback (animationRegisterCallback3 at the tail of this
        // function) that never fires with no animation to drive it, so on the
        // server path actionUseSkill was a silent no-op. Apply the outcome
        // directly with the selected performer, mirroring the doors/pickup/
        // use-object fast-paths and _combat_apply_attack_results. objectUseSkillOn
        // runs the target's SCRIPT_PROC_USE_SKILL_ON (lockpick/science/traps lock +
        // gvar effects) and then skillUse (first-aid/doctor/repair HP + cripple
        // clears) in the correct order, so it is the single call to make. Adjacency
        // (_is_next_to) is intentionally not gated, matching actionPickUp / objectUse.
        return objectUseSkillOn(performer, target, skill);
    }

    if (isInCombat()) {
        reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        animationRegisterMoveToObject(performer, target, performer->data.critter.combat.ap, 0);
    } else {
        reg_anim_begin(user == gDude ? ANIMATION_REQUEST_RESERVED : ANIMATION_REQUEST_UNRESERVED);
        if (target != gDude) {
            if (!objectWithinWalkDistance(performer, target)) {
                animationRegisterRunToObject(performer, target, -1, 0);
            } else {
                animationRegisterMoveToObject(performer, target, -1, 0);
            }
        }
    }

    animationRegisterCallbackForced(performer, target, (AnimationCallback*)_is_next_to, -1);

    AnimationType anim = (FrmId(target).objectType() == OBJ_TYPE_CRITTER && critterIsProne(target)) ? ANIM_MAGIC_HANDS_GROUND : ANIM_MAGIC_HANDS_MIDDLE;
    const FrmId frmId = FrmId(performer, anim, WeaponAnimation::None, performer->rotation + 1);

    CacheEntry* artHandle;
    Art* art = artLock(frmId, &artHandle);
    if (art != nullptr) {
        artGetActionFrame(art);
        artUnlock(artHandle);
    }

    animationRegisterAnimate(performer, anim, -1);
    // TODO: Get rid of casts.
    animationRegisterCallback3(performer, target, (void*)(uintptr_t)skill, (AnimationCallback3*)objectUseSkillOn, -1);
    return reg_anim_end();
}

// 0x412BC4
bool _is_hit_from_front(const Object* attacker, const Object* defender)
{
    int diff = attacker->rotation - defender->rotation;
    if (diff < 0) {
        diff = -diff;
    }

    return diff != 0 && diff != 1 && diff != 5;
}

// 0x412BEC
bool _can_see(Object* source, Object* target)
{
    int diff;

    diff = source->rotation - tileGetRotationTo(source->tile, target->tile);
    if (diff < 0) {
        diff = -diff;
    }

    return diff == 0 || diff == 1 || diff == 5;
}

// Tries to change between ANIM_FALL_FRONT and ANIM_FALL_BACK based on available free space in front or behind and presence of art frames.
//
// 0x412C1C pick_fall
AnimationType pickFallAnim(Object* obj, AnimationType anim)
{
    int i;
    Rotation rotation;
    int tile_num;
    FrmId frmId;

    if (anim == ANIM_FALL_FRONT) {
        rotation = obj->rotation;
        for (i = 1; i < 3; i++) {
            tile_num = tileGetTileInDirection(obj->tile, rotation, i);
            if (_obj_blocking_at(obj, tile_num, obj->elevation) != nullptr) {
                anim = ANIM_FALL_BACK;
                break;
            }
        }
    } else if (anim == ANIM_FALL_BACK) {
        rotation = (obj->rotation + 3) % ROTATION_COUNT;
        for (i = 1; i < 3; i++) {
            tile_num = tileGetTileInDirection(obj->tile, rotation, i);
            if (_obj_blocking_at(obj, tile_num, obj->elevation) != nullptr) {
                anim = ANIM_FALL_FRONT;
                break;
            }
        }
    }

    if (anim == ANIM_FALL_FRONT) {
        frmId = FrmId(obj, ANIM_FALL_FRONT, obj->rotation + 1);
        if (!frmId.exist()) {
            anim = ANIM_FALL_BACK;
        }
    }

    return anim;
}

// 0x412CE4
bool _action_explode_running()
{
    return _action_in_explode;
}

// action_explode
// 0x412CF4
int actionExplode(int tile, MapElevation elevation, int minDamage, int maxDamage, Object* sourceObj, bool animate)
{
    if (animate && _action_in_explode) {
        return -2;
    }

    Attack* attack = (Attack*)internal_malloc(sizeof(*attack));
    if (attack == nullptr) {
        return -1;
    }

    Object* explosion;
    if (objectCreateWithFrmIdProtoId(&explosion, MiscFrameId::RocketExplosion, ProtoId::Empty()) == -1) {
        internal_free(attack);
        return -1;
    }

    objectHide(explosion, nullptr);
    explosion->flags |= OBJECT_NO_SAVE;

    objectSetLocation(explosion, tile, elevation, nullptr);

    Object* adjacentExplosions[ROTATION_COUNT];
    for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
        if (objectCreateWithFrmIdProtoId(&(adjacentExplosions[rotation]), MiscFrameId::RocketExplosion, ProtoId::Empty()) == -1) {
            while (--rotation >= ROTATION_FIRST) {
                objectDestroy(adjacentExplosions[rotation], nullptr);
            }

            objectDestroy(explosion, nullptr);
            internal_free(attack);
            return -1;
        }

        objectHide(adjacentExplosions[rotation], nullptr);
        adjacentExplosions[rotation]->flags |= OBJECT_NO_SAVE;

        int adjacentTile = tileGetTileInDirection(tile, rotation, 1);
        objectSetLocation(adjacentExplosions[rotation], adjacentTile, elevation, nullptr);
    }

    Object* critter = _obj_blocking_at(nullptr, tile, elevation);
    if (critter != nullptr) {
        if (FrmId(critter).objectType() != OBJ_TYPE_CRITTER || (critter->data.critter.combat.results & DAM_DEAD) != DAM_NONE) {
            critter = nullptr;
        }
    }

    attackInit(attack, explosion, critter, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);

    attack->tile = tile;
    attack->attackerFlags = DAM_HIT;

    gameUiDisable(1);

    if (critter != nullptr) {
        if (reg_anim_clear(critter) == -2) {
            debugPrint("Cannot clear target's animation for action_explode!\n");
        }
        attack->defenderDamage = _compute_explosion_damage(minDamage, maxDamage, critter, &(attack->defenderKnockback));
    }

    _compute_explosion_on_extras(attack, 0, 0, 1);

    for (int index = 0; index < attack->extrasLength; index++) {
        Object* critter = attack->extras[index];
        if (reg_anim_clear(critter) == -2) {
            debugPrint("Cannot clear extra's animation for action_explode!\n");
        }

        attack->extrasDamage[index] = _compute_explosion_damage(minDamage, maxDamage, critter, &(attack->extrasKnockback[index]));
    }

    attackComputeDeathFlags(attack);

    // Headless: the animated branch welds damage/death/XP/scenery-destruction to
    // reg_anim callbacks (_report_explosion / _finished_explosion) whose sequence
    // is rejected by reg_anim_end under the server loop, dropping the whole
    // outcome. Force the non-animated branch, which applies the identical outcome
    // directly. Covers both the op_explosion opcode and the EXPLOSION queue event
    // (both funnel through here). Mirror of the actionDamage gate below.
    //
    // PRESENTATION-RECORD POC (PRESENTATION_RECORD_REPLAY_SPEC.md §11): on the
    // dedicated server with F2_SERVER_PRES_RECORD set, RUN this animate branch
    // (normally skipped by !serverLoopActive) inside a record section so its LEAVES
    // record a command stream (server_anim.cc + pres_record.cc), then ship it as
    // EVENT_PRES_SEQ instead of the bespoke explosionFx cue. The state branch below
    // still applies the outcome (the callbacks here are DROPped by the recorder).
    // Gate OFF (or any non-server build) → this is false → today's path exactly.
    bool recording = serverLoopActive() && presRecordEnabled();
    if (animate && (!serverLoopActive() || recording)) {
        if (recording) {
            presRecordSectionBegin();
        }
        _action_in_explode = true;

        reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        _register_priority(1);
        animationRegisterPlaySoundEffect(explosion, "whn1xxx1", 0);
        animationRegisterUnsetFlag(explosion, OBJECT_HIDDEN, 0);
        animationRegisterAnimateAndHide(explosion, ANIM_STAND, 0);
        showDamage(attack, ANIM_STAND, 1);

        for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
            animationRegisterUnsetFlag(adjacentExplosions[rotation], OBJECT_HIDDEN, 0);
            animationRegisterAnimateAndHide(adjacentExplosions[rotation], ANIM_STAND, 0);
        }

        animationRegisterCallbackForced(explosion, nullptr, (AnimationCallback*)_combat_explode_scenery, -1);
        animationRegisterHideObjectForced(explosion);

        for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
            animationRegisterHideObjectForced(adjacentExplosions[rotation]);
        }

        animationRegisterCallbackForced(attack, sourceObj, (AnimationCallback*)_report_explosion, -1);
        animationRegisterCallbackForced(nullptr, nullptr, (AnimationCallback*)_finished_explosion, -1);
        if (reg_anim_end() == -1) {
            _action_in_explode = false;
            if (recording) {
                presRecordSectionAbort();
            }

            objectDestroy(explosion, nullptr);

            for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
                objectDestroy(adjacentExplosions[rotation], nullptr);
            }

            internal_free(attack);

            gameUiEnable();
            return -1;
        }

        showDamageToExtras(attack);

        if (recording) {
            // Close the section (restore RNG so the sim stream is untouched) and
            // ship the recorded stream. Clear the _action_in_explode latch the
            // DROPped _finished_explosion would have cleared. Then fall through to
            // the STATE block, which applies the outcome server-authoritatively.
            presRecordSectionEnd();
            _action_in_explode = false;
            presenter()->presSeq(presRecordData(), presRecordSize(), presRecordOpCount());
        } else {
            // SP client: the reg_anim callbacks (run for real) applied the outcome
            // and destroyed the transients — done, do not run the STATE block.
            return 0;
        }
    }

    if (serverLoopActive()) {
        // Server equivalent of the animated path: report/apply FIRST so XP is
        // awarded (mainTargetWasDead/extrasWasDead are false pre-kill) and each
        // victim's destroy_p_proc runs, THEN finalize corpses with critterKill
        // (the _show_death animation's job). Running critterKill first (as the
        // legacy branch does) sets DAM_DEAD + strips the script before
        // _apply_damage, dropping both the XP and the destroy procs. Mirrors
        // the _combat_apply_attack_results ordering. (_report_explosion frees
        // attack, so capture the dead targets first; max 1 defender + 6 extras.)
        Object* deadTargets[7];
        int deadTargetsLength = 0;
        if (critter != nullptr && (attack->defenderFlags & DAM_DEAD) != 0) {
            deadTargets[deadTargetsLength++] = critter;
        }
        for (int index = 0; index < attack->extrasLength; index++) {
            if ((attack->extrasFlags[index] & DAM_DEAD) != 0) {
                deadTargets[deadTargetsLength++] = attack->extras[index];
            }
        }

        // Displace surviving critters (away from the blast center = the
        // explosion object's tile) BEFORE _report_explosion, which runs
        // _apply_damage. The animated path completes the actionKnockdown tile
        // moves before _apply_damage, so the knockback/prone decision must see
        // each critter's PRE-damage state; running it after _report_explosion
        // would let the freshly-ORed DAM_KNOCKED_DOWN make critterIsProne
        // suppress the knockback. Death flags were set by attackComputeDeathFlags
        // above; _combat_apply_knockback skips DAM_DEAD victims itself.
        // Mirror of the _combat_apply_attack_results knockback block.
        _combat_apply_knockback(attack);

        _report_explosion(attack, sourceObj);

        for (int index = 0; index < deadTargetsLength; index++) {
            critterKill(deadTargets[index], ANIM_INVALID, false);
        }
    } else {
        // SFALL: Fix for destroy_p_proc not being called if the critter is
        // killed by explosives when you leave the map: run it before
        // critterKill removes the critter's script.
        if (critter != nullptr) {
            if ((attack->defenderFlags & DAM_DEAD) != DAM_NONE) {
                scriptExecProc(critter->sid, SCRIPT_PROC_DESTROY);
                critterKill(critter, ANIM_INVALID, false);
            }
        }

        for (int index = 0; index < attack->extrasLength; index++) {
            if ((attack->extrasFlags[index] & DAM_DEAD) != DAM_NONE) {
                scriptExecProc(attack->extras[index]->sid, SCRIPT_PROC_DESTROY);
                critterKill(attack->extras[index], ANIM_INVALID, false);
            }
        }

        _report_explosion(attack, sourceObj);
    }

    // Common cloud tail (was the outer else-branch tail): destroy the transient
    // blast objects + run scenery damage. Runs for every state path (record and
    // non-record server, and the non-animate path); the SP animate branch returned
    // above (its reg_anim callbacks own teardown).
    _combat_explode_scenery(explosion, nullptr);

    objectDestroy(explosion, nullptr);

    for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
        objectDestroy(adjacentExplosions[rotation], nullptr);
    }

    return 0;
}

// 0x413144
int _report_explosion(Attack* attack, Object* sourceObj)
{
    bool mainTargetWasDead;
    if (attack->defender != nullptr) {
        mainTargetWasDead = (attack->defender->data.critter.combat.results & DAM_DEAD) != DAM_NONE;
    } else {
        mainTargetWasDead = false;
    }

    bool extrasWasDead[EXPLOSION_TARGET_COUNT] = {};
    for (int index = 0; index < attack->extrasLength; index++) {
        extrasWasDead[index] = (attack->extras[index]->data.critter.combat.results & DAM_DEAD) != DAM_NONE;
    }

    attackComputeDeathFlags(attack);
    combatNarrateAttack(attack);
    _apply_damage(attack, false);

    Object* anyDefender = nullptr;
    int xp = 0;
    if (sourceObj != nullptr) {
        if (attack->defender != nullptr && attack->defender != sourceObj) {
            if ((attack->defender->data.critter.combat.results & DAM_DEAD) != DAM_NONE) {
                if (sourceObj == gDude && !mainTargetWasDead) {
                    xp += critterGetExp(attack->defender);
                }
            } else {
                critterSetWhoHitMe(attack->defender, sourceObj);
                anyDefender = attack->defender;
            }
        }

        for (int index = 0; index < attack->extrasLength; index++) {
            Object* critter = attack->extras[index];
            if (critter != sourceObj) {
                if ((critter->data.critter.combat.results & DAM_DEAD) != DAM_NONE) {
                    if (sourceObj == gDude && !extrasWasDead[index]) {
                        xp += critterGetExp(critter);
                    }
                } else {
                    critterSetWhoHitMe(critter, sourceObj);

                    if (anyDefender == nullptr) {
                        anyDefender = critter;
                    }
                }
            }
        }

        if (anyDefender != nullptr) {
            if (!isInCombat()) {
                CombatStartData combat;
                combat.attacker = anyDefender;
                combat.defender = sourceObj;
                combat.actionPointsBonus = 0;
                combat.accuracyBonus = 0;
                combat.damageBonus = 0;
                combat.minDamage = 0;
                combat.maxDamage = INT_MAX;
                combat.overrideAttackResults = 0;
                scriptsRequestCombat(&combat);
            }
        }
    }

    internal_free(attack);
    gameUiEnable();

    // Any player actor earns here, not just the host — and it earns as ITSELF.
    // playerActorIs IS `sourceObj == gDude` with an empty registry.
    if (playerActorIs(sourceObj)) {
        _combat_give_exps(xp, sourceObj);
    }

    return 0;
}

// 0x4132C0
int _finished_explosion(Object* _, Object* __)
{
    _action_in_explode = false;
    return 0;
}

// calculate explosion damage applying threshold and resistances
// 0x4132CC
int _compute_explosion_damage(int min, int max, Object* defender, int* knockbackDistancePtr)
{
    int damage = randomBetween(min, max) - critterGetStat(defender, STAT_DAMAGE_THRESHOLD_EXPLOSION);
    if (damage > 0) {
        damage -= critterGetStat(defender, STAT_DAMAGE_RESISTANCE_EXPLOSION) * damage / 100;
    }

    if (damage < 0) {
        damage = 0;
    }

    if (knockbackDistancePtr != nullptr) {
        if ((defender->flags & OBJECT_MULTIHEX) == OBJECT_NONE) {
            *knockbackDistancePtr = damage / 10;
        }
    }

    return damage;
}

// 0x413330
int actionTalk(Object* obj, Object* critter)
{
    if (obj != gDude) {
        return -1;
    }

    if (FrmId(critter).objectType() != OBJ_TYPE_CRITTER) {
        return -1;
    }

    AnimationType anim = FrmId(gDude).animationType();
    if (anim == ANIM_WALK || anim == ANIM_RUNNING) {
        reg_anim_clear(gDude);
    }

    if (isInCombat()) {
        reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        animationRegisterMoveToObject(obj, critter, obj->data.critter.combat.ap, 0);
    } else {
        reg_anim_begin(obj == gDude ? ANIMATION_REQUEST_RESERVED : ANIMATION_REQUEST_UNRESERVED);

        if (objectGetDistanceBetween(obj, critter) >= 9 || _combat_is_shot_blocked(obj, obj->tile, critter->tile, critter, nullptr)) {
            animationRegisterRunToObject(obj, critter, -1, 0);
        }
    }

    animationRegisterCallbackForced(obj, critter, (AnimationCallback*)_can_talk_to, -1);
    animationRegisterCallback(obj, critter, (AnimationCallback*)_talk_to, -1);
    return reg_anim_end();
}

// 0x413420
int _can_talk_to(Object* obj, Object* critter)
{
    if (_combat_is_shot_blocked(obj, obj->tile, critter->tile, critter, nullptr) || objectGetDistanceBetween(obj, critter) >= 9) {
        if (obj == gDude) {
            // You cannot get there. (used in actions.c)
            MessageListItem messageListItem;
            messageListItem.num = 2000;
            if (messageListGetItem(&gMiscMessageList, &messageListItem)) {
                presenter()->consoleMessage(messageListItem.text);
            }
        }

        return -1;
    }

    return 0;
}

// 0x413488
int _talk_to(Object* _, Object* critter)
{
    scriptsRequestDialog(critter);
    return 0;
}

// 0x413494
void actionDamage(int tile, MapElevation elevation, int minDamage, int maxDamage, DamageType damageType, bool animated, bool bypassArmor)
{
    Attack* attack = (Attack*)internal_malloc(sizeof(*attack));
    if (attack == nullptr) {
        return;
    }

    Object* attacker;
    if (objectCreateWithFrmIdProtoId(&attacker, SceneryFrameId::ForceField3, ProtoId::Empty()) == -1) {
        internal_free(attack);
        return;
    }

    objectHide(attacker, nullptr);

    attacker->flags |= OBJECT_NO_SAVE;

    objectSetLocation(attacker, tile, elevation, nullptr);

    Object* defender = _obj_blocking_at(nullptr, tile, elevation);
    attackInit(attack, attacker, defender, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
    attack->tile = tile;
    attack->attackerFlags = DAM_HIT;
    gameUiDisable(1);

    if (defender != nullptr) {
        reg_anim_clear(defender);

        int damage;
        if (bypassArmor) {
            damage = maxDamage;
        } else {
            damage = _compute_dmg_damage(minDamage, maxDamage, defender, &(attack->defenderKnockback), damageType);
        }

        attack->defenderDamage = damage;
    }

    attackComputeDeathFlags(attack);

    // Headless: the animated path welds _apply_damage/critterKill to the
    // _report_dmg reg_anim callback, which never fires under the server loop
    // (the sequence's reg_anim_end is rejected). Force the non-animated branch,
    // which applies the identical outcome directly (mirror of
    // _combat_apply_attack_results / actionExplode(animate=false)).
    //
    // PRESENTATION-RECORD (PRESENTATION_RECORD_REPLAY_SPEC.md §8, receive-damage /
    // hit-react): on the dedicated server with F2_SERVER_PRES_RECORD set, RUN this
    // animate branch (normally skipped by !serverLoopActive) inside a record section
    // so its LEAVES record a command stream, then ship it as EVENT_PRES_SEQ. The
    // STATE block below still applies the outcome (the _report_dmg callback here is
    // DROPped by the recorder). Gate OFF (or the client/golden binary) → recording
    // is false → today's path exactly. Mirror of the actionExplode gate above.
    //
    // The visible reaction (SFX + ANIM_HIT_FROM_* / death anim) rides the DEFENDER,
    // a persistent object the viewer resolves by netId. The synthetic `attacker`
    // (FID_0x20001F5, NO_SAVE, created THIS beat) is classified transient by
    // resolveRef (created-this-beat && NO_SAVE) → ships as OBJ_CREATE, so its
    // whc1xxx1 SFX + hide replay from a viewer-minted local transient. (This is the
    // first family to exercise OBJ_CREATE end-to-end for a synthetic attacker.)
    bool recording = serverLoopActive() && presRecordEnabled();
    if (animated && (!serverLoopActive() || recording)) {
        if (recording) {
            presRecordSectionBegin();
        }

        reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        animationRegisterPlaySoundEffect(attacker, "whc1xxx1", 0);
        showDamage(attack, gMaximumBloodDeathAnimations[damageType], 0);
        animationRegisterCallbackForced(attack, nullptr, (AnimationCallback*)_report_dmg, 0);
        animationRegisterHideObjectForced(attacker);

        if (reg_anim_end() == -1) {
            if (recording) {
                presRecordSectionAbort();
            }
            objectDestroy(attacker, nullptr);
            internal_free(attack);
            return;
        }

        if (!recording) {
            // SP client: the reg_anim callbacks (_report_dmg) apply the outcome and
            // free `attack`; the attacker is hidden by the sequence. Done — do NOT
            // run the STATE block below.
            gameUiEnable();
            return;
        }

        // Recording: close the section (restore RNG so the sim stream is untouched)
        // and ship the recorded stream, then fall through to the STATE block, which
        // applies the outcome server-authoritatively.
        presRecordSectionEnd();
        presenter()->presSeq(presRecordData(), presRecordSize(), presRecordOpCount());
    }

    if (serverLoopActive()) {
        // Server equivalent of the animated path: apply damage FIRST (via
        // _report_dmg -> _apply_damage) so the victim's destroy_p_proc runs and
        // all death consequences fire, THEN finalize the corpse physically with
        // critterKill (the _show_death animation's job). The legacy branch below
        // runs critterKill first, which sets DAM_DEAD + strips the script before
        // _apply_damage, silently dropping the destroy proc. Mirrors the
        // _combat_apply_attack_results ordering. (_report_dmg frees attack, so
        // capture the death decision first.)
        Object* deadDefender = (defender != nullptr && (attack->defenderFlags & DAM_DEAD) != 0) ? defender : nullptr;

        _report_dmg(attack, nullptr);

        if (deadDefender != nullptr) {
            critterKill(deadDefender, ANIM_INVALID, true);
        }

        objectDestroy(attacker, nullptr);
    } else {
        if (defender != nullptr) {
            if ((attack->defenderFlags & DAM_DEAD) != DAM_NONE) {
                critterKill(defender, ANIM_INVALID, 1);
            }
        }

        // NOTE: Uninline.
        _report_dmg(attack, nullptr);

        objectDestroy(attacker, nullptr);
    }

    gameUiEnable();
}

// 0x41363C
int _report_dmg(Attack* attack, Object* _)
{
    combatNarrateAttack(attack);
    _apply_damage(attack, false);
    internal_free(attack);
    gameUiEnable();
    return 0;
}

// Calculate damage by applying threshold and resistances.
//
// 0x413660
int _compute_dmg_damage(int min, int max, Object* obj, int* knockbackDistancePtr, DamageType damageType)
{
    if (critterFlagCheck(obj, CRITTER_NO_KNOCKBACK)) {
        knockbackDistancePtr = nullptr;
    }

    int damage = randomBetween(min, max) - critterGetStat(obj, STAT_DAMAGE_THRESHOLD + damageType);
    if (damage > 0) {
        damage -= critterGetStat(obj, STAT_DAMAGE_RESISTANCE + damageType) * damage / 100;
    }

    if (damage < 0) {
        damage = 0;
    }

    if (knockbackDistancePtr != nullptr) {
        if ((obj->flags & OBJECT_MULTIHEX) == OBJECT_NONE && damageType != DAMAGE_TYPE_ELECTRICAL) {
            *knockbackDistancePtr = damage / 10;
        }
    }

    return damage;
}

// 0x4136EC
bool actionCheckPush(Object* obj, Object* target)
{
    // Cannot push anything but critters.
    if (FrmId(target).objectType() != OBJ_TYPE_CRITTER) {
        return false;
    }

    // Cannot push itself.
    if (obj == target) {
        return false;
    }

    // Cannot push inactive critters.
    if (!critterIsActive(target)) {
        return false;
    }

    if (_action_can_talk_to(obj, target) != 0) {
        return false;
    }

    // Can only push critters that have push handler.
    if (!scriptHasProc(target->sid, SCRIPT_PROC_PUSH)) {
        return false;
    }

    if (isInCombat()) {
        if (target->data.critter.combat.team == obj->data.critter.combat.team
            && target == obj->data.critter.combat.whoHitMe) {
            return false;
        }

        // TODO: Check.
        Object* whoHitMe = target->data.critter.combat.whoHitMe;
        if (whoHitMe != nullptr
            && whoHitMe->data.critter.combat.team == obj->data.critter.combat.team) {
            return false;
        }
    }

    return true;
}

// 0x413790
int actionPush(Object* obj, Object* target)
{
    if (!actionCheckPush(obj, target)) {
        return -1;
    }

    int sid;
    if (objectGetSid(target, &sid) == 0) {
        scriptSetObjects(sid, obj, target);
        scriptExecProc(sid, SCRIPT_PROC_PUSH);

        bool scriptOverrides = false;

        Script* scr;
        if (scriptGetScript(sid, &scr) != -1) {
            scriptOverrides = scr->scriptOverrides;
        }

        if (scriptOverrides) {
            return -1;
        }
    }

    Rotation rotation = tileGetRotationTo(obj->tile, target->tile);
    int tile;
    do {
        tile = tileGetTileInDirection(target->tile, rotation, 1);
        if (_obj_blocking_at(target, tile, target->elevation) == nullptr) {
            break;
        }

        tile = tileGetTileInDirection(target->tile, (rotation + 1) % ROTATION_COUNT, 1);
        if (_obj_blocking_at(target, tile, target->elevation) == nullptr) {
            break;
        }

        tile = tileGetTileInDirection(target->tile, (rotation + 5) % ROTATION_COUNT, 1);
        if (_obj_blocking_at(target, tile, target->elevation) == nullptr) {
            break;
        }

        tile = tileGetTileInDirection(target->tile, (rotation + 2) % ROTATION_COUNT, 1);
        if (_obj_blocking_at(target, tile, target->elevation) == nullptr) {
            break;
        }

        tile = tileGetTileInDirection(target->tile, (rotation + 4) % ROTATION_COUNT, 1);
        if (_obj_blocking_at(target, tile, target->elevation) == nullptr) {
            break;
        }

        tile = tileGetTileInDirection(target->tile, (rotation + 3) % ROTATION_COUNT, 1);
        if (_obj_blocking_at(target, tile, target->elevation) == nullptr) {
            break;
        }

        return -1;
    } while (0);

    int actionPoints;
    if (isInCombat()) {
        actionPoints = target->data.critter.combat.ap;
    } else {
        actionPoints = -1;
    }

    reg_anim_begin(ANIMATION_REQUEST_RESERVED);
    animationRegisterRotateToTile(target, tile);
    animationRegisterMoveToTile(target, tile, target->elevation, actionPoints, 0);
    return reg_anim_end();
}

// Returns -1 if can't see there (can't find a path there)
// Returns -2 if it's too far (> 12 tiles).
//
// 0x413970
int _action_can_talk_to(Object* obj, Object* target)
{
    if (pathfinderFindPath(obj, obj->tile, target->tile, nullptr, 0, _obj_sight_blocking_at) == 0) {
        return -1;
    }

    if (tileDistanceBetween(obj->tile, target->tile) > 12) {
        return -2;
    }

    return 0;
}

static int hideProjectile(void* _, void* projectile)
{
    Object* proj = reinterpret_cast<Object*>(projectile);

    Rect rect;
    if (objectHide(proj, &rect) == 0) {
        presenter()->worldInvalidateRect(&rect, proj->elevation);
    }

    proj->flags |= OBJECT_NO_SAVE;

    return 0;
}



int _knockback_dest_tile(Object* obj, int maxDistance, int rotation)
{
    // SFALL: Fix to limit the maximum distance for the knockback animation.
    if (maxDistance > MAX_KNOCKDOWN_DISTANCE) {
        maxDistance = MAX_KNOCKDOWN_DISTANCE;
    }

    int distance;
    int tile;
    for (distance = 1; distance <= maxDistance; distance++) {
        tile = tileGetTileInDirection(obj->tile, static_cast<Rotation>(rotation), distance);
        if (_obj_blocking_at(obj, tile, obj->elevation) != nullptr) {
            distance--;
            break;
        }

        // CE: Fix to prevent critters (including player) cross an exit grid as
        // a result of knockback. Sfall has similar fix done differently and it
        // affects the player only. This approach is better since it also
        // prevents unreachable (=unlootable) corpses on exit grids.
        if (isExitGridAt(tile, obj->elevation)) {
            distance--;
            break;
        }
    }

    // TODO: Check, probably step back because we've started with 1?
    distance--;

    if (distance <= 0) {
        return obj->tile;
    }

    return tileGetTileInDirection(obj->tile, static_cast<Rotation>(rotation), distance);
}

static int _knockback_headless_dest(Object* critter, int flags, int knockbackDistance, int attackerTile)
{
    if (critter == nullptr || knockbackDistance == 0) {
        return critter != nullptr ? critter->tile : -1;
    }

    if ((flags & DAM_DEAD) != 0) {
        return critter->tile;
    }

    if (critterFlagCheck(ProtoId(critter), CRITTER_NO_KNOCKBACK)) {
        return critter->tile;
    }

    // Already-prone critters are not displaced: showDamageToObject's whole
    // knockback block is gated on !critterIsProne(defender).
    if (critterIsProne(critter)) {
        return critter->tile;
    }

    // The fire-dance branch of showDamageToObject burns the critter in place
    // and never calls actionKnockdown, so neither do we — otherwise the server
    // would displace a critter the client leaves put.
    if ((flags & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) == 0
        && (flags & DAM_ON_FIRE) != 0
        && FrmId(critter, ANIM_FIRE_DANCE, FrmId(critter).weaponAnimation(), critter->rotation + 1).exist()) {
        return critter->tile;
    }

    int rotation = tileGetRotationTo(attackerTile, critter->tile);
    return _knockback_dest_tile(critter, knockbackDistance, rotation);
}

int _combat_compute_knockback(Attack* attack, Object** victims, int* dests)
{
    if (attack->attacker == nullptr) {
        return 0;
    }

    int attackerTile = attack->attacker->tile;
    int count = 0;

    if (attack->defender != nullptr) {
        victims[count] = attack->defender;
        dests[count] = _knockback_headless_dest(attack->defender, attack->defenderFlags, attack->defenderKnockback, attackerTile);
        count++;
    }

    for (int index = 0; index < attack->extrasLength; index++) {
        victims[count] = attack->extras[index];
        dests[count] = _knockback_headless_dest(attack->extras[index], attack->extrasFlags[index], attack->extrasKnockback[index], attackerTile);
        count++;
    }

    return count;
}

void _combat_commit_knockback(Object** victims, const int* dests, int count)
{
    for (int index = 0; index < count; index++) {
        if (dests[index] != victims[index]->tile) {
            objectSetLocation(victims[index], dests[index], victims[index]->elevation, nullptr);
        }
    }
}

void _combat_apply_knockback(Attack* attack)
{
    // Compute + commit together — the explosion caller (actionExplode) already emits this
    // AFTER its recorded presSeq, so no reorder is needed there. Only the melee/ranged path
    // (_combat_apply_attack_results) splits the two halves around attackResult().
    Object* victims[1 + EXPLOSION_TARGET_COUNT];
    int dests[1 + EXPLOSION_TARGET_COUNT];
    int count = _combat_compute_knockback(attack, victims, dests);
    _combat_commit_knockback(victims, dests, count);
}

void* actionTalkToCallbackPtr()
{
    return (void*)(AnimationCallback*)_talk_to;
}

void actionDeathDropItems(Object* obj, int anim)
{
    if (anim >= ANIM_ELECTRIFIED_TO_NOTHING && anim <= ANIM_EXPLODED_TO_NOTHING
        && !critterFlagCheck(ProtoId(obj), CRITTER_SPECIAL_DEATH)
        && !critterFlagCheck(ProtoId(obj), CRITTER_NO_DROP)) {
        itemDropAll(obj, obj->tile);
    }
}

void* actionShowDeathCallbackPtr()
{
    return (void*)(AnimationCallback*)_show_death;
}

void actionPresReplayShowDeath(Object* obj, int anim)
{
    animationRegisterCallbackForced(obj, (void*)(intptr_t)anim, (AnimationCallback*)_show_death, -1);
}

void actionThrowConsumeHeadless(Attack* attack)
{
    int anim = critterGetAnimationForHitMode(attack->attacker, attack->hitMode);
    if (anim != ANIM_THROW_ANIM) {
        return;
    }

    // Same entry gate as _action_ranged: the throw branch (and thus consumption) is skipped
    // on a critical miss that did not hit (line 886: DAM_HIT != 0 || DAM_CRITICAL == 0).
    if ((attack->attackerFlags & DAM_HIT) == 0 && (attack->attackerFlags & DAM_CRITICAL) != 0) {
        return;
    }

    Object* weapon = attack->weapon;
    if (weapon == nullptr) {
        return;
    }

    // A throw only consumes when the weapon actually has a projectile proto (matches the
    // _action_ranged guard); otherwise the throw branch was never entered.
    ProtoId projectilePid = weaponGetProjectileProtoId(weapon);
    Proto* projectileProto;
    if (protoGetProto(projectilePid, &projectileProto) == -1 || projectileProto->fid == -1) {
        return;
    }

    ObjectFlags weaponFlags = weapon->flags;
    int damageType = weaponGetDamageType(attack->attacker, weapon);
    // SFALL isGrenade test (actions.cc:864): explosive throwables are consumed by the blast.
    bool explosive = damageType == explosionGetDamageType()
        || damageType == DAMAGE_TYPE_PLASMA
        || damageType == DAMAGE_TYPE_EMP;

    itemRemove(attack->attacker, weapon, 1);
    itemReplace(attack->attacker, weapon, weaponFlags & OBJECT_IN_ANY_HAND);
    _cAIPrepWeaponItem(attack->attacker, weapon);

    // itemRemove detaches `weapon` (owner cleared) but does NOT free it — reconnect it to
    // the world (spear) or destroy it (grenade), else it orphans.
    if (explosive) {
        objectDestroy(weapon, nullptr);
    } else {
        int landTile = (attack->attackerFlags & DAM_HIT) != 0 ? attack->defender->tile : attack->tile;
        weapon->flags &= ~OBJECT_HIDDEN; // the animated path leaves it visible on the ground
        _obj_connect(weapon, landTile, attack->defender->elevation, nullptr);
    }
}

void actionExplodeReplay(int tile, int elevation, Attack* attack)
{
    const FrmId fid(MiscFrameId::RocketExplosion);
    const MapElevation mapElevation = static_cast<MapElevation>(elevation);

    Object* explosion = nullptr;
    if (objectCreateWithFrmIdProtoId(&explosion, fid, ProtoId::Empty()) == -1) {
        return;
    }
    objectHide(explosion, nullptr);
    explosion->flags |= OBJECT_NO_SAVE;
    objectSetLocation(explosion, tile, mapElevation, nullptr);

    Object* adjacentExplosions[ROTATION_COUNT];
    int adjacentCount = 0;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        Object* obj = nullptr;
        if (objectCreateWithFrmIdProtoId(&obj, fid, ProtoId::Empty()) == -1) {
            break;
        }
        objectHide(obj, nullptr);
        obj->flags |= OBJECT_NO_SAVE;
        objectSetLocation(obj, tileGetTileInDirection(tile, static_cast<Rotation>(rotation), 1), mapElevation, nullptr);
        adjacentExplosions[adjacentCount++] = obj;
    }

    // The center cloud is the attacker (its explosion fid drives the gib-anim choice).
    attack->attacker = explosion;
    attack->tile = tile;

    reg_anim_begin(ANIMATION_REQUEST_RESERVED);
    _register_priority(1);
    animationRegisterPlaySoundEffect(explosion, "whn1xxx1", 0);
    animationRegisterUnsetFlag(explosion, OBJECT_HIDDEN, 0);
    animationRegisterAnimateAndHide(explosion, ANIM_STAND, 0);
    showDamage(attack, ANIM_STAND, 1); // defender reaction (no-op when attack->defender == null)

    for (int rotation = 0; rotation < adjacentCount; rotation++) {
        animationRegisterUnsetFlag(adjacentExplosions[rotation], OBJECT_HIDDEN, 0);
        animationRegisterAnimateAndHide(adjacentExplosions[rotation], ANIM_STAND, 0);
    }

    animationRegisterHideObjectForced(explosion);
    for (int rotation = 0; rotation < adjacentCount; rotation++) {
        animationRegisterHideObjectForced(adjacentExplosions[rotation]);
    }
    if (reg_anim_end() == -1) {
        // Rejected (should not happen for brand-new objects) — tear the transients down
        // directly so nothing leaks, and skip the extras (their own sequences).
        objectDestroy(explosion, nullptr);
        for (int rotation = 0; rotation < adjacentCount; rotation++) {
            objectDestroy(adjacentExplosions[rotation], nullptr);
        }
        return;
    }

    showDamageToExtras(attack); // ring victims (each its own reg_anim sequence)
}

void actionResolveDroppedExplosive(Object* dropper)
{
    Attack attack;
    attackInit(&attack, dropper, nullptr, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
    attack.attackerFlags = DAM_HIT;
    attack.tile = dropper->tile;
    _compute_explosion_on_extras(&attack, 0, 0, 1);

    Object* watcher = nullptr;
    for (int index = 0; index < attack.extrasLength; index++) {
        Object* critter = attack.extras[index];
        if (critter != dropper
            && critter->data.critter.combat.team != dropper->data.critter.combat.team
            && statRoll(critter, STAT_PERCEPTION, 0, nullptr) >= ROLL_SUCCESS) {
            critterSetWhoHitMe(critter, dropper);

            if (watcher == nullptr) {
                watcher = critter;
            }
        }
    }

    if (watcher != nullptr) {
        if (!isInCombat()) {
            CombatStartData combat;
            combat.attacker = watcher;
            combat.defender = dropper;
            combat.actionPointsBonus = 0;
            combat.accuracyBonus = 0;
            combat.damageBonus = 0;
            combat.minDamage = 0;
            combat.maxDamage = INT_MAX;
            combat.overrideAttackResults = 0;
            scriptsRequestCombat(&combat);
        }
    }
}

int actionUseItemOnObjectWithApCost(Object* user, Object* targetObj, Object* item)
{
    // Policy switch, combat_ap.h: charging off → fall through to the untaxed
    // pass-through below. One of the three interaction charge points.
    if (isInCombat() && kCombatApChargeEnabled) {
        if (user->data.critter.combat.ap < 2) {
            return -1;
        }

        int rc = _action_use_an_item_on_object(user, targetObj, item);
        if (rc != -1) {
            int actionPoints = user->data.critter.combat.ap;
            if (actionPoints < 2) {
                user->data.critter.combat.ap = 0;
            } else {
                user->data.critter.combat.ap = actionPoints - 2;
            }

            presenter()->hudActionPoints(user->data.critter.combat.ap, _combat_free_move);
        }

        return rc;
    }

    return _action_use_an_item_on_object(user, targetObj, item);
}

void* actionHideProjectileCallbackPtr()
{
    return (void*)(AnimationCallback*)hideProjectile;
}

} // namespace fallout
