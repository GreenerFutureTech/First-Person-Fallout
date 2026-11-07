#include "character_editor.h" // gCharacterEditorLastLevel — stamped so a screen never re-awards
#include "msg_channel.h" // kMsgChannelReward — a level-up is good news, addressed to its actor
#include "presenter.h"
#include "player_sheet.h" // playerSheetMarkDirty — stream runtime sheet changes
#include "server_players.h" // playerActorSlotOf — per-actor PC-stat rows
#include "stat.h"

#include <charconv>
#include <stdio.h>

#include <algorithm>
#include <string_view>

#include "art.h"
#include "art_defs.h"
#include "character_editor.h"
#include "combat.h"
#include "content_config.h"
#include "critter.h"
#include "display_monitor.h"
#include "game.h"
#include "game_sound.h"
#include "interface.h"
#include "item.h"
#include "memory.h"
#include "message.h"
#include "object.h"
#include "party_member.h"
#include "perk.h"
#include "platform_compat.h"
#include "proto.h"
#include "random.h"
#include "scripts.h"
#include "skill.h"
#include "svga.h"
#include "tile.h"
#include "trait.h"

namespace fallout {

static int gPlayerActorPcStats[kMaxPlayerActors - 1][PC_STAT_COUNT];

static int* pcStatRow(Object* subject);

bool pcLevelUpApply(int fromLevel, int toLevel, Object* subject);

void pcLevelUpBadgeRefresh(Object* subject);

void pcPlayerActorSeedStats();

void pcPlayerActorSeedStatsSlot(int slot);

static int* pcPlayerActorRow(int slot);

int pcPlayerActorRowWrite(File* stream, int slot);

int pcPlayerActorRowRead(File* stream, int slot);


// Provides metadata about stats.
typedef struct StatDescription {
    char* name;
    char* description;
    SkillDexFrameId frmId;
    int minimumValue;
    int maximumValue;
    int defaultValue;
} StatDescription;

// 0x51D53C stat_data
static StatDescription gStatDescriptions[STAT_COUNT] = {
    { nullptr, nullptr, SkillDexFrameId::Strength, PRIMARY_STAT_MIN, PRIMARY_STAT_MAX, 5 }, // STAT_STRENGTH
    { nullptr, nullptr, SkillDexFrameId::Perception, PRIMARY_STAT_MIN, PRIMARY_STAT_MAX, 5 }, // STAT_PERCEPTION
    { nullptr, nullptr, SkillDexFrameId::Endurance, PRIMARY_STAT_MIN, PRIMARY_STAT_MAX, 5 }, // STAT_ENDURANCE
    { nullptr, nullptr, SkillDexFrameId::Charisma, PRIMARY_STAT_MIN, PRIMARY_STAT_MAX, 5 }, // STAT_CHARISMA
    { nullptr, nullptr, SkillDexFrameId::Intelligence, PRIMARY_STAT_MIN, PRIMARY_STAT_MAX, 5 }, // STAT_INTELLIGENCE
    { nullptr, nullptr, SkillDexFrameId::Agility, PRIMARY_STAT_MIN, PRIMARY_STAT_MAX, 5 }, // STAT_AGILITY
    { nullptr, nullptr, SkillDexFrameId::Luck, PRIMARY_STAT_MIN, PRIMARY_STAT_MAX, 5 }, // STAT_LUCK
    { nullptr, nullptr, SkillDexFrameId::HitPoints, 0, 999, 0 }, // STAT_MAXIMUM_HIT_POINTS
    { nullptr, nullptr, SkillDexFrameId::BonusMove, 1, 99, 0 }, // STAT_MAXIMUM_ACTION_POINTS
    { nullptr, nullptr, SkillDexFrameId::ArmorClass, 0, 999, 0 }, // STAT_ARMOR_CLASS
    { nullptr, nullptr, SkillDexFrameId::Unarmed, 0, INT_MAX, 0 }, // STAT_UNARMED_DAMAGE
    { nullptr, nullptr, SkillDexFrameId::MeleeWeapons, 0, 500, 0 }, // STAT_MELEE_DAMAGE
    { nullptr, nullptr, SkillDexFrameId::CarryWeight, 0, 999, 0 }, // STAT_CARRY_WEIGHT
    { nullptr, nullptr, SkillDexFrameId::Sequence, 0, 60, 0 }, // STAT_SEQUENCE
    { nullptr, nullptr, SkillDexFrameId::HealingRate, 0, 30, 0 }, // STAT_HEALING_RATE
    { nullptr, nullptr, SkillDexFrameId::CriticalChance, 0, 100, 0 }, // STAT_CRITICAL_CHANCE
    { nullptr, nullptr, SkillDexFrameId::Empathy, -60, 100, 0 }, // STAT_BETTER_CRITICALS
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 100, 0 }, // STAT_DAMAGE_THRESHOLD
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 100, 0 }, // STAT_DAMAGE_THRESHOLD_LASER
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 100, 0 }, // STAT_DAMAGE_THRESHOLD_FIRE
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 100, 0 }, // STAT_DAMAGE_THRESHOLD_PLASMA
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 100, 0 }, // STAT_DAMAGE_THRESHOLD_ELECTRICAL
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 100, 0 }, // STAT_DAMAGE_THRESHOLD_EMP
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 100, 0 }, // STAT_DAMAGE_THRESHOLD_EXPLOSION
    { nullptr, nullptr, SkillDexFrameId::DamageResistance, 0, 90, 0 }, // STAT_DAMAGE_RESISTANCE
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 90, 0 }, // STAT_DAMAGE_RESISTANCE_LASER
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 90, 0 }, // STAT_DAMAGE_RESISTANCE_FIRE
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 90, 0 }, // STAT_DAMAGE_RESISTANCE_PLASMA
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 90, 0 }, // STAT_DAMAGE_RESISTANCE_ELECTRICAL
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 100, 0 }, // STAT_DAMAGE_RESISTANCE_EMP
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 90, 0 }, // STAT_DAMAGE_RESISTANCE_EXPLOSION
    { nullptr, nullptr, SkillDexFrameId::RadResistance, 0, 95, 0 }, // STAT_RADIATION_RESISTANCE
    { nullptr, nullptr, SkillDexFrameId::PoisonResistance, 0, 95, 0 }, // STAT_POISON_RESISTANCE
    { nullptr, nullptr, SkillDexFrameId::Strength, 16, 101, 25 }, // STAT_AGE
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, 1, 0 }, // STAT_GENDER
    { nullptr, nullptr, SkillDexFrameId::HitPoints, 0, 2000, 0 }, // STAT_CURRENT_HIT_POINTS
    { nullptr, nullptr, SkillDexFrameId::Poisoned, 0, 2000, 0 }, // STAT_CURRENT_POISON_LEVEL
    { nullptr, nullptr, SkillDexFrameId::Radiated, 0, 2000, 0 }, // STAT_CURRENT_RADIATION_LEVEL
};

// 0x51D8CC pc_stat_data
static StatDescription gPcStatDescriptions[PC_STAT_COUNT] = {
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, INT_MAX, 0 },
    { nullptr, nullptr, SkillDexFrameId::Strength, 1, PC_LEVEL_MAX, 1 },
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, INT_MAX, 0 },
    { nullptr, nullptr, SkillDexFrameId::Strength, -20, 20, 0 },
    { nullptr, nullptr, SkillDexFrameId::Strength, 0, INT_MAX, 0 },
};

// 0x66817C stat_message_file
static MessageList gStatsMessageList;

// 0x668184
static char* gStatValueDescriptions[PRIMARY_STAT_RANGE];

// 0x6681AC curr_pc_stat
static int gPcStatValues[PC_STAT_COUNT];

static int pcStatMaximums[SAVEABLE_STAT_COUNT];
static int pcStatMinimums[SAVEABLE_STAT_COUNT];
static int npcStatMaximums[SAVEABLE_STAT_COUNT];
static int npcStatMinimums[SAVEABLE_STAT_COUNT];

static int unspentApBonus = 4;
static int unspentApPerkBonus = 4;
static int xpTable[PC_LEVEL_MAX];
static int xpTableThresholds = 0;

static void pcExperienceTableInit();
static int pcGetMaxLevel();
static int pcGetLevelForExperience(int xp);

static std::string_view pcExperienceTableTrimToken(std::string_view token)
{
    size_t first = token.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }

    size_t last = token.find_last_not_of(" \t\r\n");
    return token.substr(first, last - first + 1);
}

static bool pcExperienceTableParseToken(std::string_view token, int* value)
{
    token = pcExperienceTableTrimToken(token);
    if (token.empty()) {
        return false;
    }

    auto result = std::from_chars(token.data(), token.data() + token.size(), *value);
    return result.ec == std::errc() && result.ptr == token.data() + token.size();
}

static void statResetBounds();
static int statGetMaximum(Object* critter, Stat stat);
static int statGetMinimum(Object* critter, Stat stat);

// 0x4AED70
int statsInit()
{
    MessageListItem messageListItem;

    statResetBounds();

    // NOTE: Uninline.
    pcStatsReset();
    pcExperienceTableInit();

    if (!messageListInit(&gStatsMessageList)) {
        return -1;
    }

    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "%s%s", gGameMsgPath, "stat.msg");

    if (!messageListLoad(&gStatsMessageList, path)) {
        return -1;
    }

    for (Stat stat = STAT_FIRST; stat < STAT_COUNT; stat++) {
        gStatDescriptions[stat].name = getmsg(&gStatsMessageList, &messageListItem, 100 + stat);
        gStatDescriptions[stat].description = getmsg(&gStatsMessageList, &messageListItem, 200 + stat);
    }

    for (PcStat pcStat = PC_STAT_FIRST; pcStat < PC_STAT_COUNT; pcStat++) {
        gPcStatDescriptions[pcStat].name = getmsg(&gStatsMessageList, &messageListItem, 400 + pcStat);
        gPcStatDescriptions[pcStat].description = getmsg(&gStatsMessageList, &messageListItem, 500 + pcStat);
    }

    for (int index = PRIMARY_STAT_MIN - 1; index < PRIMARY_STAT_RANGE; index++) {
        gStatValueDescriptions[index] = getmsg(&gStatsMessageList, &messageListItem, 301 + index);
    }

    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_STAT, &gStatsMessageList);

    return 0;
}

// 0x4AEEC0
int statsReset()
{
    // NOTE: Uninline.
    pcStatsReset();
    statResetBounds();
    statResetUnspentApBonuses();

    return 0;
}

// 0x4AEEE4
int statsExit()
{
    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_STAT, nullptr);
    messageListFree(&gStatsMessageList);

    return 0;
}

// 0x4AEEF4
int statsLoad(File* stream)
{
    for (int index = 0; index < PC_STAT_COUNT; index++) {
        if (fileReadInt32(stream, &(gPcStatValues[index])) == -1) {
            return -1;
        }
    }

    return 0;
}

// 0x4AEF20
int statsSave(File* stream)
{
    for (int index = 0; index < PC_STAT_COUNT; index++) {
        if (fileWriteInt32(stream, gPcStatValues[index]) == -1) {
            return -1;
        }
    }

    return 0;
}

void statResetUnspentApBonuses()
{
    unspentApBonus = 4;
    unspentApPerkBonus = 4;
}

void statSetUnspentApBonus(int multiplier)
{
    unspentApBonus = multiplier;
}

int statGetUnspentApBonus()
{
    return unspentApBonus;
}

void statSetUnspentApPerkBonus(int multiplier)
{
    unspentApPerkBonus = multiplier;
}

int statGetUnspentApPerkBonus()
{
    return unspentApPerkBonus;
}

static void statResetBounds()
{
    for (int stat = 0; stat < SAVEABLE_STAT_COUNT; stat++) {
        pcStatMaximums[stat] = gStatDescriptions[stat].maximumValue;
        pcStatMinimums[stat] = gStatDescriptions[stat].minimumValue;
        npcStatMaximums[stat] = gStatDescriptions[stat].maximumValue;
        npcStatMinimums[stat] = gStatDescriptions[stat].minimumValue;
    }
}

static int statGetMaximum(Object* critter, Stat stat)
{
    return critter == gDude ? pcStatMaximums[stat] : npcStatMaximums[stat];
}

static int statGetMinimum(Object* critter, Stat stat)
{
    return critter == gDude ? pcStatMinimums[stat] : npcStatMinimums[stat];
}

int statGetConfiguredMaximum(Stat stat, bool npc)
{
    if (stat >= 0 && stat < SAVEABLE_STAT_COUNT) {
        return npc ? npcStatMaximums[stat] : pcStatMaximums[stat];
    }

    return 0;
}

int statGetConfiguredMinimum(Stat stat, bool npc)
{
    if (stat >= 0 && stat < SAVEABLE_STAT_COUNT) {
        return npc ? npcStatMinimums[stat] : pcStatMinimums[stat];
    }

    return 0;
}

void statSetPcMaximum(Stat stat, int maximum)
{
    if (stat >= 0 && stat < SAVEABLE_STAT_COUNT) {
        pcStatMaximums[stat] = maximum;
    }
}

void statSetPcMinimum(Stat stat, int minimum)
{
    if (stat >= 0 && stat < SAVEABLE_STAT_COUNT) {
        pcStatMinimums[stat] = minimum;
    }
}

void statSetNpcMaximum(Stat stat, int maximum)
{
    if (stat >= 0 && stat < SAVEABLE_STAT_COUNT) {
        npcStatMaximums[stat] = maximum;
    }
}

void statSetNpcMinimum(Stat stat, int minimum)
{
    if (stat >= 0 && stat < SAVEABLE_STAT_COUNT) {
        npcStatMinimums[stat] = minimum;
    }
}

// 0x4AEF48
int critterGetStat(Object* critter, Stat stat)
{
    if (ProtoId(critter).objectType() != OBJ_TYPE_CRITTER) {
        return 0;
    }
    int value;
    if (stat >= STAT_FIRST && stat < SAVEABLE_STAT_COUNT) {
        value = critterGetBaseStatWithTraitModifier(critter, stat);
        value += critterGetBonusStat(critter, stat);

        switch (stat) {
        case STAT_PERCEPTION:
            if ((critter->data.critter.combat.results & DAM_BLIND) != DAM_NONE) {
                value -= 5;
            }
            break;
        case STAT_MAXIMUM_ACTION_POINTS:
            if (1) {
                int remainingCarryWeight = critterGetStat(critter, STAT_CARRY_WEIGHT) - objectGetInventoryWeight(critter);
                if (remainingCarryWeight < 0) {
                    value -= -remainingCarryWeight / 40 + 1;
                }
            }
            break;
        case STAT_ARMOR_CLASS:
            if (isInCombat()) {
                if (_combat_whose_turn() != critter) {
                    int armorClassBonus = critter->data.critter.combat.ap * unspentApBonus;

                    // Every player actor evades with its OWN unarmed skill and
                    // its OWN equipped weapons — the whole block reads `critter`,
                    // not gDude, or an extra's armor class is computed from what
                    // the HOST happens to be holding.
                    if (playerActorIs(critter)) {
                        if (perkHasRank(critter, PERK_HTH_EVADE)) {
                            bool hasWeapon = false;

                            Object* item2 = critterGetItem2(critter);
                            if (item2 != nullptr) {
                                if (itemGetType(item2) == ITEM_TYPE_WEAPON) {
                                    if (weaponGetAnimationCode(item2) != WeaponAnimation::None) {
                                        hasWeapon = true;
                                    }
                                }
                            }

                            if (!hasWeapon) {
                                Object* item1 = critterGetItem1(critter);
                                if (item1 != nullptr) {
                                    if (itemGetType(item1) == ITEM_TYPE_WEAPON) {
                                        if (weaponGetAnimationCode(item1) != WeaponAnimation::None) {
                                            hasWeapon = true;
                                        }
                                    }
                                }
                            }

                            if (!hasWeapon) {
                                armorClassBonus += critter->data.critter.combat.ap * perkGetRank(critter, PERK_HTH_EVADE) * unspentApPerkBonus;
                                value += skillGetValue(critter, SKILL_UNARMED) / 12;
                            }
                        }
                    }
                    value += armorClassBonus / 4;
                }
            }
            break;
        case STAT_AGE:
            value += gameTimeGetTime() / GAME_TIME_TICKS_PER_YEAR;
            break;
        default:
            break;
        }

        // Perk-derived SPECIAL / damage-resistance / max-HP bonuses belong to the
        // subject actor, not whoever is bound to gDude. Keying on `== gDude` drops
        // an extra's bonuses whenever it is not the scoped actor (e.g. it is the
        // DEFENDER during an enemy turn, where gDude is restored to the host), so
        // its Dermal Armor DR / Adrenaline Rush / GAIN_* etc. silently vanish
        // off-turn. Use playerActorIs, matching the HtH-evade block above and
        // critterGetBaseStatWithTraitModifier.
        if (playerActorIs(critter)) {
            switch (stat) {
            case STAT_STRENGTH:
                if (perkGetRank(critter, PERK_GAIN_STRENGTH)) {
                    value++;
                }

                if (perkGetRank(critter, PERK_ADRENALINE_RUSH)) {
                    if (critterGetStat(critter, STAT_CURRENT_HIT_POINTS) < (critterGetStat(critter, STAT_MAXIMUM_HIT_POINTS) / 2)) {
                        value++;
                    }
                }
                break;
            case STAT_PERCEPTION:
                if (perkGetRank(critter, PERK_GAIN_PERCEPTION)) {
                    value++;
                }
                break;
            case STAT_ENDURANCE:
                if (perkGetRank(critter, PERK_GAIN_ENDURANCE)) {
                    value++;
                }
                break;
            case STAT_CHARISMA:
                if (1) {
                    if (perkGetRank(critter, PERK_GAIN_CHARISMA)) {
                        value++;
                    }

                    bool hasMirrorShades = false;

                    Object* item2 = critterGetItem2(critter);
                    if (ProtoId(item2) == ItemProtoTypeId::MirroredShades) {
                        hasMirrorShades = true;
                    }

                    Object* item1 = critterGetItem1(critter);
                    if (ProtoId(item1) == ItemProtoTypeId::MirroredShades) {
                        hasMirrorShades = true;
                    }

                    if (hasMirrorShades) {
                        value++;
                    }
                }
                break;
            case STAT_INTELLIGENCE:
                if (perkGetRank(critter, PERK_GAIN_INTELLIGENCE)) {
                    value++;
                }
                break;
            case STAT_AGILITY:
                if (perkGetRank(critter, PERK_GAIN_AGILITY)) {
                    value++;
                }
                break;
            case STAT_LUCK:
                if (perkGetRank(critter, PERK_GAIN_LUCK)) {
                    value++;
                }
                break;
            case STAT_MAXIMUM_HIT_POINTS:
                if (perkGetRank(critter, PERK_ALCOHOL_RAISED_HIT_POINTS)) {
                    value += 2;
                }

                if (perkGetRank(critter, PERK_ALCOHOL_RAISED_HIT_POINTS_II)) {
                    value += 4;
                }

                if (perkGetRank(critter, PERK_ALCOHOL_LOWERED_HIT_POINTS)) {
                    value -= 2;
                }

                if (perkGetRank(critter, PERK_ALCOHOL_LOWERED_HIT_POINTS_II)) {
                    value -= 4;
                }

                if (perkGetRank(critter, PERK_AUTODOC_RAISED_HIT_POINTS)) {
                    value += 2;
                }

                if (perkGetRank(critter, PERK_AUTODOC_RAISED_HIT_POINTS_II)) {
                    value += 4;
                }

                if (perkGetRank(critter, PERK_AUTODOC_LOWERED_HIT_POINTS)) {
                    value -= 2;
                }

                if (perkGetRank(critter, PERK_AUTODOC_LOWERED_HIT_POINTS_II)) {
                    value -= 4;
                }
                break;
            case STAT_DAMAGE_RESISTANCE:
            case STAT_DAMAGE_RESISTANCE_EXPLOSION:
                if (perkGetRank(critter, PERK_DERMAL_IMPACT_ARMOR)) {
                    value += 5;
                } else if (perkGetRank(critter, PERK_DERMAL_IMPACT_ASSAULT_ENHANCEMENT)) {
                    value += 10;
                }
                break;
            case STAT_DAMAGE_RESISTANCE_LASER:
            case STAT_DAMAGE_RESISTANCE_FIRE:
            case STAT_DAMAGE_RESISTANCE_PLASMA:
                if (perkGetRank(critter, PERK_PHOENIX_ARMOR_IMPLANTS)) {
                    value += 5;
                } else if (perkGetRank(critter, PERK_PHOENIX_ASSAULT_ENHANCEMENT)) {
                    value += 10;
                }
                break;
            case STAT_RADIATION_RESISTANCE:
                if (perkGetRank(critter, PERK_VAULT_CITY_INOCULATIONS)) {
                    value += perkGetVaultCityInoculationsRadBonus();
                }
                break;
            case STAT_POISON_RESISTANCE:
                if (perkGetRank(critter, PERK_VAULT_CITY_INOCULATIONS)) {
                    value += perkGetVaultCityInoculationsPoisonBonus();
                }
                break;
            default:
                break;
            }
        }

        int minimum = statGetMinimum(critter, stat);
        if (value < minimum) {
            value = minimum;
        } else {
            int maximum = statGetMaximum(critter, stat);
            if (value > maximum) {
                value = maximum;
            }
        }
    } else {
        switch (stat) {
        case STAT_CURRENT_HIT_POINTS:
            value = critterGetHitPoints(critter);
            break;
        case STAT_CURRENT_POISON_LEVEL:
            value = critterGetPoison(critter);
            break;
        case STAT_CURRENT_RADIATION_LEVEL:
            value = critterGetRadiation(critter);
            break;
        default:
            value = 0;
            break;
        }
    }

    return value;
}

// Returns base stat value (accounting for traits if critter is dude).
//
// 0x4AF3E0
int critterGetBaseStatWithTraitModifier(Object* critter, Stat stat)
{
    int value = critterGetBaseStat(critter, stat);

    // EVERY player actor gets its own trait modifiers, not just the host —
    // playerActorIs IS `critter == gDude` with an empty registry.
    if (playerActorIs(critter)) {
        value += traitGetStatModifier(stat, critter);
    }

    return value;
}

// 0x4AF408
int critterGetBaseStat(Object* critter, Stat stat)
{
    Proto* proto;

    if (stat >= 0 && stat < SAVEABLE_STAT_COUNT) {
        // MP hardening: a critter's pid must resolve to a proto. It ALWAYS does in
        // single-player, so vanilla dereferences unconditionally — but a co-op
        // viewer can transiently hold an actor whose pid does not resolve during a
        // rebaseline, and the unchecked deref then reads out of bounds (ASAN:
        // heap-buffer-overflow). Degrade to 0 instead of crashing; the next
        // interface update reads the correct value once the world settles.
        //
        // The pid must also be a CRITTER pid. baseStats lives deep inside
        // CritterProto (proto_size 0x1A0); every other proto type is allocated at
        // its own, smaller size (proto.cc _proto_sizes — ItemProto 0x84, SceneryProto
        // 0x38, ...). skillUse (skill.cc:663-664) reads the TARGET's HP stats before
        // it branches on skill, so using Lockpick/Repair/Traps on a door, container
        // or item reaches here with a non-critter target; indexing baseStats[stat]
        // then runs off the end of that smaller allocation. Vanilla has the same
        // unconditional read but got away with it — the OOB bytes landed in adjacent
        // 32-bit heap and were never used for non-healing skills. Return 0: a
        // non-critter has no SPECIAL/derived stats, and the healing math that would
        // consume HP only runs for critter targets.
        if (ProtoId(critter).objectType() != OBJ_TYPE_CRITTER
            || protoGetProto(critter, &proto) == -1 || proto == nullptr) {
            return 0;
        }
        return proto->critter.data.baseStats[stat];
    }

    switch (stat) {
    case STAT_CURRENT_HIT_POINTS:
        return critterGetHitPoints(critter);
    case STAT_CURRENT_POISON_LEVEL:
        return critterGetPoison(critter);
    case STAT_CURRENT_RADIATION_LEVEL:
        return critterGetRadiation(critter);
    default:
        return 0;
    }
}

// 0x4AF474
int critterGetBonusStat(Object* critter, Stat stat)
{
    if (stat >= STAT_FIRST && stat < SAVEABLE_STAT_COUNT) {
        Proto* proto;
        // MP hardening + non-critter guard, same as critterGetBaseStat — see the
        // note there. bonusStats is the same CritterProto-only array, so a
        // non-critter target (Lockpick/Repair on a door) would read off the end of
        // its smaller proto allocation.
        if (ProtoId(critter).objectType() != OBJ_TYPE_CRITTER
            || protoGetProto(critter, &proto) == -1 || proto == nullptr) {
            return 0;
        }
        return proto->critter.data.bonusStats[stat];
    }

    return 0;
}

// 0x4AF4BC
int critterSetBaseStat(Object* critter, Stat stat, int value)
{
    Proto* proto;

    if (!statIsValid(stat)) {
        return -5;
    }

    if (stat >= STAT_FIRST && stat < SAVEABLE_STAT_COUNT) {
        if (stat > STAT_LUCK && stat <= STAT_POISON_RESISTANCE) {
            // Cannot change base value of derived stats.
            return -1;
        }

        if (playerActorIs(critter)) {
            value -= traitGetStatModifier(stat, critter);
        }

        if (value < statGetMinimum(critter, stat)) {
            return -2;
        }

        if (value > statGetMaximum(critter, stat)) {
            return -3;
        }

        protoGetProto(critter, &proto);
        proto->critter.data.baseStats[stat] = value;

        if (stat >= STAT_STRENGTH && stat <= STAT_LUCK) {
            critterUpdateDerivedStats(critter);
        }

        // Sheet lives in the per-slot proto row, not on the Object — stream the
        // row so viewers see the change live (no-op off a network presenter).
        playerSheetMarkDirty(critter);

        return 0;
    }

    switch (stat) {
    case STAT_CURRENT_HIT_POINTS:
        return critterAdjustHitPoints(critter, value - critterGetHitPoints(critter));
    case STAT_CURRENT_POISON_LEVEL:
        return critterAdjustPoison(critter, value - critterGetPoison(critter));
    case STAT_CURRENT_RADIATION_LEVEL:
        return critterAdjustRadiation(critter, value - critterGetRadiation(critter));
    default:
        // Should be unreachable
        return 0;
    }
}

// 0x4AF5D4
int critterIncBaseStat(Object* critter, Stat stat)
{
    int value = critterGetBaseStat(critter, stat);

    // EVERY player actor gets its own trait modifiers, not just the host —
    // playerActorIs IS `critter == gDude` with an empty registry.
    if (playerActorIs(critter)) {
        value += traitGetStatModifier(stat, critter);
    }

    return critterSetBaseStat(critter, stat, value + 1);
}

// 0x4AF608
int critterDecBaseStat(Object* critter, Stat stat)
{
    int value = critterGetBaseStat(critter, stat);

    // EVERY player actor gets its own trait modifiers, not just the host —
    // playerActorIs IS `critter == gDude` with an empty registry.
    if (playerActorIs(critter)) {
        value += traitGetStatModifier(stat, critter);
    }

    return critterSetBaseStat(critter, stat, value - 1);
}

// 0x4AF63C
int critterSetBonusStat(Object* critter, Stat stat, int value)
{
    if (!statIsValid(stat)) {
        return -5;
    }

    if (stat >= STAT_FIRST && stat < SAVEABLE_STAT_COUNT) {
        Proto* proto;
        protoGetProto(critter, &proto);
        proto->critter.data.bonusStats[stat] = value;

        if (stat >= STAT_STRENGTH && stat <= STAT_LUCK) {
            critterUpdateDerivedStats(critter);
        }

        // Sheet lives in the per-slot proto row, not on the Object — stream the
        // row so viewers see the change live (no-op off a network presenter).
        playerSheetMarkDirty(critter);

        return 0;
    }

    switch (stat) {
    case STAT_CURRENT_HIT_POINTS:
        return critterAdjustHitPoints(critter, value);
    case STAT_CURRENT_POISON_LEVEL:
        return critterAdjustPoison(critter, value);
    case STAT_CURRENT_RADIATION_LEVEL:
        return critterAdjustRadiation(critter, value);
    default:
        // Should be unreachable
        return -1;
    }
}

// 0x4AF6CC
void protoCritterDataResetStats(CritterProtoData* data)
{
    for (Stat stat = STAT_FIRST; stat < SAVEABLE_STAT_COUNT; stat++) {
        data->baseStats[stat] = gStatDescriptions[stat].defaultValue;
        data->bonusStats[stat] = 0;
    }
}

// 0x4AF6FC
void critterUpdateDerivedStats(Object* critter)
{
    int strength = critterGetStat(critter, STAT_STRENGTH);
    int perception = critterGetStat(critter, STAT_PERCEPTION);
    int endurance = critterGetStat(critter, STAT_ENDURANCE);
    int intelligence = critterGetStat(critter, STAT_INTELLIGENCE);
    int agility = critterGetStat(critter, STAT_AGILITY);
    int luck = critterGetStat(critter, STAT_LUCK);

    Proto* proto;
    protoGetProto(critter, &proto);
    CritterProtoData* data = &(proto->critter.data);

    data->baseStats[STAT_MAXIMUM_HIT_POINTS] = critterGetBaseStatWithTraitModifier(critter, STAT_STRENGTH) + critterGetBaseStatWithTraitModifier(critter, STAT_ENDURANCE) * 2 + 15;
    data->baseStats[STAT_MAXIMUM_ACTION_POINTS] = agility / 2 + 5;
    data->baseStats[STAT_ARMOR_CLASS] = agility;
    data->baseStats[STAT_MELEE_DAMAGE] = std::max(strength - 5, 1);
    data->baseStats[STAT_CARRY_WEIGHT] = 25 * strength + 25;
    data->baseStats[STAT_SEQUENCE] = 2 * perception;
    data->baseStats[STAT_HEALING_RATE] = std::max(endurance / 3, 1);
    data->baseStats[STAT_CRITICAL_CHANCE] = luck;
    data->baseStats[STAT_BETTER_CRITICALS] = 0;
    data->baseStats[STAT_RADIATION_RESISTANCE] = 2 * endurance;
    data->baseStats[STAT_POISON_RESISTANCE] = 5 * endurance;
}

// 0x4AF854
char* statGetName(Stat stat)
{
    return statIsValid(stat) ? gStatDescriptions[stat].name : nullptr;
}

// 0x4AF898
char* statGetDescription(Stat stat)
{
    return statIsValid(stat) ? gStatDescriptions[stat].description : nullptr;
}

// 0x4AF8DC
char* statGetValueDescription(int value)
{
    if (value < PRIMARY_STAT_MIN) {
        value = PRIMARY_STAT_MIN;
    } else if (value > PRIMARY_STAT_MAX) {
        value = PRIMARY_STAT_MAX;
    }

    return gStatValueDescriptions[value - PRIMARY_STAT_MIN];
}

// 0x4AF8FC
int pcGetStat(PcStat pcStat, Object* subject)
{
    return pcStatIsValid(pcStat) ? pcStatRow(subject)[pcStat] : 0;
}

// 0x4AF910
int pcSetStat(PcStat pcStat, int value, Object* subject)
{
    int result;

    if (!pcStatIsValid(pcStat)) {
        return -5;
    }

    if (value < gPcStatDescriptions[pcStat].minimumValue) {
        return -2;
    }

    int maximumValue = pcStat == PC_STAT_LEVEL ? pcGetMaxLevel() : gPcStatDescriptions[pcStat].maximumValue;
    if (value > maximumValue) {
        return -3;
    }

    int* row = pcStatRow(subject);

    if (pcStat != PC_STAT_EXPERIENCE || value >= row[PC_STAT_EXPERIENCE]) {
        row[pcStat] = value;
        if (pcStat == PC_STAT_EXPERIENCE) {
            result = pcAddExperienceWithOptions(0, true, nullptr, subject);
        } else {
            result = 0;
        }
    } else {
        result = pcSetExperience(value, subject);
    }

    // Stream the row: level / karma / unspent skill points are per-actor sheet
    // fields, so a viewer must see them change (same channel as SPECIAL/XP).
    playerSheetMarkDirty(subject);

    return result;
}

// Reset stats.
//
// 0x4AF980
void pcStatsReset()
{
    for (PcStat pcStat = PC_STAT_FIRST; pcStat < PC_STAT_COUNT; pcStat++) {
        gPcStatValues[pcStat] = gPcStatDescriptions[pcStat].defaultValue;
    }

    // Extra actors reset with the host — these are file statics, not malloc'd per
    // run, so a new game would otherwise inherit the previous one's XP and level
    // (the same trap perkResetRanks has).
    for (int slot = 0; slot < kMaxPlayerActors - 1; slot++) {
        for (int pcStat = 0; pcStat < PC_STAT_COUNT; pcStat++) {
            gPlayerActorPcStats[slot][pcStat] = gPcStatDescriptions[pcStat].defaultValue;
        }
    }
}

static void pcExperienceTableInit()
{
    xpTable[0] = 0;
    xpTableThresholds = 0;

    char* value;
    if (!configGetString(&gContentConfig, CONTENT_CONFIG_STATS_SECTION, "xp_table", &value) || value[0] == '\0') {
        return;
    }

    std::string_view remaining(value);

    while (!remaining.empty() && xpTableThresholds < PC_LEVEL_MAX - 1) {
        size_t comma = remaining.find(',');
        std::string_view token = comma == std::string_view::npos
            ? remaining
            : remaining.substr(0, comma);

        int xp;
        if (pcExperienceTableParseToken(token, &xp)) {
            xpTableThresholds++;
            xpTable[xpTableThresholds] = xp;
        }

        if (comma == std::string_view::npos) {
            break;
        }

        remaining.remove_prefix(comma + 1);
    }
}

static int pcGetMaxLevel()
{
    if (xpTableThresholds == 0) {
        return PC_LEVEL_MAX;
    }

    return xpTableThresholds + 1;
}

static int pcGetLevelForExperience(int xp)
{
    int level = 1;
    int maxLevel = pcGetMaxLevel();
    while (level < maxLevel) {
        int nextLevelXp = pcGetExperienceForLevel(level + 1);
        if (nextLevelXp == -1 || xp < nextLevelXp) {
            break;
        }

        level++;
    }

    return level;
}

// Returns experience to reach next level.
//
// 0x4AF9A0
int pcGetExperienceForNextLevel(Object* subject)
{
    return pcGetExperienceForLevel(pcStatRow(subject)[PC_STAT_LEVEL] + 1);
}

// Returns exp to reach given level.
//
// 0x4AF9A8
int pcGetExperienceForLevel(int level)
{
    if (level < 1 || level > pcGetMaxLevel()) {
        return -1;
    }

    if (xpTableThresholds != 0) {
        return xpTable[level - 1];
    }

    int halfLevel = level / 2;
    if ((level & 1) != 0) {
        return 1000 * halfLevel * level;
    } else {
        return 1000 * halfLevel * (level - 1);
    }
}

// 0x4AF9F4
char* pcStatGetName(PcStat pcStat)
{
    return pcStatIsValid(pcStat) ? gPcStatDescriptions[pcStat].name : nullptr;
}

// 0x4AFA14
char* pcStatGetDescription(PcStat pcStat)
{
    return pcStatIsValid(pcStat) ? gPcStatDescriptions[pcStat].description : nullptr;
}

// 0x4AFA34
SkillDexFrmId statGetFrmId(Stat stat)
{
    return statIsValid(stat) ? gStatDescriptions[stat].frmId : SkillDexFrameId::Invalid;
}

// Roll D10 against specified stat.
//
// This function is intended to be used with one of SPECIAL stats (which are
// capped at 10, hence d10), not with artitrary stat, but does not enforce it.
//
// An optional [modifier] can be supplied as a bonus (or penalty) to the stat's
// value.
//
// Upon return [howMuch] will be set to difference between stat's value
// (accounting for given [modifier]) and d10 roll, which can be positive (or
// zero) when roll succeeds, or negative when roll fails. Set [howMuch] to
// `NULL` if you're not interested in this value.
//
// 0x4AFA78
int statRoll(Object* critter, Stat stat, int modifier, int* howMuch)
{
    int value = critterGetStat(critter, stat) + modifier;
    int chance = randomBetween(PRIMARY_STAT_MIN, PRIMARY_STAT_MAX);

    if (howMuch != nullptr) {
        *howMuch = value - chance;
    }

    if (chance <= value) {
        return ROLL_SUCCESS;
    }

    return ROLL_FAILURE;
}

// 0x4AFAA8
int pcAddExperience(int xp, int* xpGained, Object* subject)
{
    return pcAddExperienceWithOptions(xp, true, xpGained, subject);
}

// 0x4AFAB8
int pcAddExperienceWithOptions(int xp, bool doParty, int* xpGained, Object* subject)
{
    // Resolve ONCE, up front. Everything below that used to read gDude reads this
    // instead, and the row it indexes must be the same actor's — an earner whose
    // XP lands in one row while the level-up spends another's is risk #2, and it
    // is silent (§7).
    Object* earner = subject != nullptr ? subject : gDude;
    int* row = pcStatRow(earner);

    int oldXp = row[PC_STAT_EXPERIENCE];

    int newXp = row[PC_STAT_EXPERIENCE];
    newXp += xp;
    newXp += perkGetRank(earner, PERK_SWIFT_LEARNER) * 5 * xp / 100;

    if (newXp < gPcStatDescriptions[PC_STAT_EXPERIENCE].minimumValue) {
        newXp = gPcStatDescriptions[PC_STAT_EXPERIENCE].minimumValue;
    }

    if (newXp > gPcStatDescriptions[PC_STAT_EXPERIENCE].maximumValue) {
        newXp = gPcStatDescriptions[PC_STAT_EXPERIENCE].maximumValue;
    }

    row[PC_STAT_EXPERIENCE] = newXp;

    // The level-up award is the EARNER's: hit points land on the actor that did
    // the killing, not on whoever happens to be gDude.
    // gDude is rebound by ServerActorScope, so pointer equality with it means
    // "current acting player", not host. Slot 0 is the stable host identity.
    bool isHost = playerActorSlotOf(earner) == 0;

    while (row[PC_STAT_LEVEL] < pcGetMaxLevel()) {
        if (newXp < pcGetExperienceForNextLevel(earner)) {
            break;
        }

        int levelBefore = row[PC_STAT_LEVEL];
        if (pcSetStat(PC_STAT_LEVEL, row[PC_STAT_LEVEL] + 1, earner) == 0) {
            int maxHpBefore = critterGetStat(earner, STAT_MAXIMUM_HIT_POINTS);

            // ►► THE SKILL POINTS AND THE FREE PERK ARE AWARDED HERE, on the level
            // itself, for the actor that earned it.
            //
            // They used to be awarded by characterEditorUpdateLevel — that is, LAZILY,
            // the next time somebody opened the character screen (vanilla awards HP in
            // this funnel but points on the screen). In co-op that is not a quirk, it is
            // a hole: the dedicated server has no character screen at all, and every
            // player's screen is server-owned, so NOBODY was ever awarded a point. The
            // owed perk was worse than missing — it was one shared PC-global, so the
            // first player to open their sheet consumed the pick and everyone else's
            // vanished (PLAYER_SHEET_DESIGN.md §8).
            //
            // Awarding at the funnel makes the earner the subject and needs no screen.
            // For single-player the totals are identical, only earlier: the points are
            // already in the row when the screen opens instead of appearing as it does.
            pcLevelUpApply(levelBefore, levelBefore + 1, earner);
            characterEditorHandleLevelUp(levelBefore + 1, earner);
            if (isHost) {
                // This process's screen has nothing left to reconcile — without the
                // stamp, characterEditorUpdateLevel would award the same level again.
                gCharacterEditorLastLevel = row[PC_STAT_LEVEL];
            }

            // ►► "YOU HAVE GONE UP A LEVEL" NOW REACHES THE ACTOR WHO LEVELLED, not just
            // the host. This block used to be host-only with the note "until per-client HUD
            // routing exists" — it exists now (consoleMessageStyled addresses one actor), and
            // the gap was worse than cosmetic: an extra levelled "silently but really", so a
            // player gained a level, HP and skill points with NOTHING on their screen to say
            // so. Addressed, so it lands on the right screen and nobody else's.
            //
            // The interface repaints below stay HOST-ONLY on purpose: they act on THIS
            // process's single interface bar, so firing them for an extra would repaint
            // the host's bar with another character's numbers. The level-up FLAG is no
            // longer in that group — dudeEnableState takes a subject now, so it marks the
            // earner's own sheet row and streams to the earner's own indicator bar.
            {
                MessageListItem messageListItem;
                messageListItem.num = 600; // You have gone up a level.
                if (messageListGetItem(&gStatsMessageList, &messageListItem)) {
                    presenter()->consoleMessageStyled(earner != nullptr ? earner->netId : 0,
                        kMsgChannelReward, messageListItem.text);
                }
            }
            pcLevelUpBadgeRefresh(earner);
            if (isHost) {
                presenter()->sfxPlay("levelup");
            }

            // NOTE: Uninline.
            int endurance = critterGetBaseStatWithTraitModifier(earner, STAT_ENDURANCE);

            int hpPerLevel = endurance / 2 + 2;
            hpPerLevel += perkGetRank(earner, PERK_LIFEGIVER) * perkGetLifegiverBonus();

            int bonusHp = critterGetBonusStat(earner, STAT_MAXIMUM_HIT_POINTS);
            critterSetBonusStat(earner, STAT_MAXIMUM_HIT_POINTS, bonusHp + hpPerLevel);

            int maxHpAfter = critterGetStat(earner, STAT_MAXIMUM_HIT_POINTS);
            critterAdjustHitPoints(earner, maxHpAfter - maxHpBefore);

            if (isHost) {
                presenter()->hudHitPoints(false);

                // SFALL: Update unarmed attack after leveling up.
                InterfaceItemAction leftItemAction;
                InterfaceItemAction rightItemAction;
                interfaceGetItemActions(&leftItemAction, &rightItemAction);
                presenter()->hudItems(false, leftItemAction, rightItemAction);
            }

            // Companions follow the HOST's level only. They are one shared party
            // (MP_PROPOSAL Ch 14), so letting each player's level-up advance them
            // would compound their growth by the player count.
            if (doParty && isHost && partyMemberEngineLevelUpEnabled()) {
                _partyMemberIncLevels();
            }
        }
    }

    if (xpGained != nullptr) {
        *xpGained = newXp - oldXp;
    }

    // Stream the earner's new XP/level row. The award is already per-actor (the
    // combat bucket credits the killer), but without this the extra's client keeps
    // showing seeded XP — "only slot 0 gains XP". The XP write above is direct (not
    // via pcSetStat), so mark here; whole-row is idempotent so the nested level-up
    // pcSetStat marking again is harmless.
    playerSheetMarkDirty(earner);

    return 0;
}

// 0x4AFC38
int pcSetExperience(int xp, Object* subject)
{
    Object* earner = subject != nullptr ? subject : gDude;
    int* row = pcStatRow(earner);

    int oldLevel = row[PC_STAT_LEVEL];
    row[PC_STAT_EXPERIENCE] = xp;

    int newLevel = pcGetLevelForExperience(xp);

    pcSetStat(PC_STAT_LEVEL, newLevel, earner);

    // The DEMOTION path (XP set backwards). Mirror of the award in
    // pcAddExperienceWithOptions, and gated the same way: the HP is taken off the
    // earner whoever it is, and the badge clears on the earner's own row; only the
    // interface repaint is the host's.
    bool isHost = playerActorSlotOf(earner) == 0;
    dudeDisableState(DUDE_STATE_LEVEL_UP_AVAILABLE, earner);

    if (newLevel > oldLevel) {
        for (int level = oldLevel + 1; level <= newLevel; ++level) {
            pcLevelUpApply(level - 1, level, earner);
            characterEditorHandleLevelUp(level, earner);
        }
        pcLevelUpBadgeRefresh(earner);
    }

    // NOTE: Uninline.
    int endurance = critterGetBaseStatWithTraitModifier(earner, STAT_ENDURANCE);

    int hpPerLevel = endurance / 2 + 2;
    hpPerLevel += perkGetRank(earner, PERK_LIFEGIVER) * perkGetLifegiverBonus();

    int deltaHp = (oldLevel - newLevel) * hpPerLevel;
    critterAdjustHitPoints(earner, -deltaHp);

    int bonusHp = critterGetBonusStat(earner, STAT_MAXIMUM_HIT_POINTS);

    critterSetBonusStat(earner, STAT_MAXIMUM_HIT_POINTS, bonusHp - deltaHp);

    if (isHost) {
        presenter()->hudHitPoints(false);
    }

    return 0;
}



static int* pcStatRow(Object* subject)
{
    int slot = playerActorSlotOf(subject != nullptr ? subject : gDude);
    return slot > 0 ? gPlayerActorPcStats[slot - 1] : gPcStatValues;
}

bool pcLevelUpApply(int fromLevel, int toLevel, Object* subject)
{
    Object* earner = subject != nullptr ? subject : gDude;
    bool freePerk = false;

    for (int nextLevel = fromLevel + 1; nextLevel <= toLevel; nextLevel++) {
        int sp = pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS, earner);
        sp += 5 + characterEditorGetSkillPointsPerLevelModifier();
        sp += critterGetBaseStatWithTraitModifier(earner, STAT_INTELLIGENCE) * 2;
        sp += perkGetRank(earner, PERK_EDUCATED) * perkGetEducatedBonus();
        sp += traitIsSelectedAndActive(TRAIT_SKILLED, earner) * 5;
        if (traitIsSelectedAndActive(TRAIT_GIFTED, earner)) {
            sp -= 5;
            if (sp < 0) {
                sp = 0;
            }
        }
        if (sp > 99) {
            sp = 99;
        }

        pcSetStat(PC_STAT_UNSPENT_SKILL_POINTS, sp, earner);

        int selectedPerksCount = 0;
        for (Perk perk = PERK_FIRST; perk < PERK_COUNT; perk++) {
            if (perkGetRank(earner, perk) != 0) {
                selectedPerksCount += 1;
                if (selectedPerksCount >= 37) {
                    break;
                }
            }
        }

        if (selectedPerksCount < 37) {
            int progression = 3;
            if (traitIsSelectedAndActive(TRAIT_SKILLED, earner)) {
                progression += 1;
            }

            if (nextLevel % progression == 0) {
                freePerk = true;
            }
        }
    }

    return freePerk;
}

void pcLevelUpBadgeRefresh(Object* subject)
{
    Object* actor = subject != nullptr ? subject : gDude;

    bool owed = pcGetStat(PC_STAT_UNSPENT_SKILL_POINTS, actor) > 0
        || perkOwedPickGet(actor);

    if (owed) {
        dudeEnableState(DUDE_STATE_LEVEL_UP_AVAILABLE, actor);
    } else {
        dudeDisableState(DUDE_STATE_LEVEL_UP_AVAILABLE, actor);
    }
}

void pcPlayerActorSeedStats()
{
    for (int slot = 1; slot < kMaxPlayerActors; slot++) {
        pcPlayerActorSeedStatsSlot(slot);
    }
}

void pcPlayerActorSeedStatsSlot(int slot)
{
    if (slot < 1 || slot >= kMaxPlayerActors) {
        return;
    }

    for (int pcStat = 0; pcStat < PC_STAT_COUNT; pcStat++) {
        gPlayerActorPcStats[slot - 1][pcStat] = gPcStatValues[pcStat];
    }
}

static int* pcPlayerActorRow(int slot)
{
    if (slot == 0) {
        return gPcStatValues;
    }

    if (slot < 1 || slot >= kMaxPlayerActors) {
        return nullptr;
    }

    return gPlayerActorPcStats[slot - 1];
}

int pcPlayerActorRowWrite(File* stream, int slot)
{
    int* row = pcPlayerActorRow(slot);
    if (row == nullptr) {
        return -1;
    }

    return fileWriteInt32List(stream, row, PC_STAT_COUNT);
}

int pcPlayerActorRowRead(File* stream, int slot)
{
    int* row = pcPlayerActorRow(slot);
    if (row == nullptr) {
        return -1;
    }

    return fileReadInt32List(stream, row, PC_STAT_COUNT);
}

} // namespace fallout
