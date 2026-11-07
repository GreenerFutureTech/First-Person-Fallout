#include "server_players.h" // playerActorSlotOf — per-actor trait rows
#include "trait.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "art_defs.h"
#include "config.h"
#include "debug.h"
#include "game.h"
#include "message.h"
#include "object.h"
#include "platform_compat.h"
#include "sfall_config.h"
#include "skill.h"
#include "stat.h"

#include <string>

namespace fallout {

static Trait gPlayerActorTraits[kMaxPlayerActors - 1][TRAITS_MAX_SELECTED_COUNT];

static Trait* traitRow(Object* subject);

void traitsPlayerActorSeed();

void traitsPlayerActorSeedSlot(int slot);

static Trait* traitsPlayerActorRow(int slot);

int traitsPlayerActorRowWrite(File* stream, int slot);

int traitsPlayerActorRowRead(File* stream, int slot);

void traitsMutateDrop(Trait* traits, int traitCount, int pickedLine, Trait firstListedTrait);

void traitsMutateGain(Trait* traits, int traitCount, Trait newTrait);

static bool traitHas(Object* subject, Trait trait);


// Provides metadata about traits.
typedef struct TraitDescription {
    // The name of trait.
    char* name;

    // The description of trait.
    //
    // The description is only used in character editor to inform player about
    // effects of this trait.
    char* description;

    // Identifier of art in [intrface.lst].
    SkillDexFrameId frmId;
} TraitDescription;

// 0x66BE38 trait_message_file
static MessageList gTraitsMessageList;

// List of selected traits.
//
// 0x66BE40 pc_trait
static Trait gSelectedTraits[TRAITS_MAX_SELECTED_COUNT];

// 0x51DB84 trait_data
static TraitDescription traitDescriptions[TRAIT_COUNT] = {
    { nullptr, nullptr, SkillDexFrameId::FastMetabolism },
    { nullptr, nullptr, SkillDexFrameId::Bruiser },
    { nullptr, nullptr, SkillDexFrameId::SmallFrame },
    { nullptr, nullptr, SkillDexFrameId::OneHander },
    { nullptr, nullptr, SkillDexFrameId::Finesse },
    { nullptr, nullptr, SkillDexFrameId::Kamikaze },
    { nullptr, nullptr, SkillDexFrameId::HeavyHanded },
    { nullptr, nullptr, SkillDexFrameId::FastShot },
    { nullptr, nullptr, SkillDexFrameId::BloodyMess },
    { nullptr, nullptr, SkillDexFrameId::Jinxed },
    { nullptr, nullptr, SkillDexFrameId::GoodNatured },
    { nullptr, nullptr, SkillDexFrameId::DrugAddict },
    { nullptr, nullptr, SkillDexFrameId::DrugResistant },
    { nullptr, nullptr, SkillDexFrameId::Empathy },
    { nullptr, nullptr, SkillDexFrameId::Skilled },
    { nullptr, nullptr, SkillDexFrameId::Gifted },
};

static const SkillDexFrameId defaultTraitFrmIds[TRAIT_COUNT] = {
    SkillDexFrameId::FastMetabolism,
    SkillDexFrameId::Bruiser,
    SkillDexFrameId::SmallFrame,
    SkillDexFrameId::OneHander,
    SkillDexFrameId::Finesse,
    SkillDexFrameId::Kamikaze,
    SkillDexFrameId::HeavyHanded,
    SkillDexFrameId::FastShot,
    SkillDexFrameId::BloodyMess,
    SkillDexFrameId::Jinxed,
    SkillDexFrameId::GoodNatured,
    SkillDexFrameId::DrugAddict,
    SkillDexFrameId::DrugResistant,
    SkillDexFrameId::Empathy,
    SkillDexFrameId::Skilled,
    SkillDexFrameId::Gifted,
};

static bool traitOverridesEnabled = false;
static int traitStatBonuses[TRAIT_COUNT][STAT_COUNT];
static int traitSkillBonuses[TRAIT_COUNT][SKILL_COUNT];
static bool traitHardcodeDisabled[TRAIT_COUNT];
static std::string traitOverrideNames[TRAIT_COUNT];
static std::string traitOverrideDescriptions[TRAIT_COUNT];

static void traitsResetSfallConfig();
static void traitsLoadSfallConfig();
static void traitsLoadSfallPairBonuses(Config* config, const char* sectionKey, const char* key, int* bonuses, int count);

// 0x4B39F0 trait_init
int traitsInit()
{
    traitsResetSfallConfig();

    if (!messageListInit(&gTraitsMessageList)) {
        return -1;
    }

    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "%s%s", gGameMsgPath, "trait.msg");

    if (!messageListLoad(&gTraitsMessageList, path)) {
        return -1;
    }

    for (Trait trait = TRAIT_FIRST; trait < TRAIT_COUNT; trait++) {
        MessageListItem messageListItem;

        messageListItem.num = 100 + trait;
        if (messageListGetItem(&gTraitsMessageList, &messageListItem)) {
            traitDescriptions[trait].name = messageListItem.text;
        }

        messageListItem.num = 200 + trait;
        if (messageListGetItem(&gTraitsMessageList, &messageListItem)) {
            traitDescriptions[trait].description = messageListItem.text;
        }
    }

    traitsLoadSfallConfig();

    // NOTE: Uninline.
    traitsReset();

    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_TRAIT, &gTraitsMessageList);

    return 0;
}

// 0x4B3ADC trait_reset
void traitsReset()
{
    for (int index = 0; index < TRAITS_MAX_SELECTED_COUNT; index++) {
        gSelectedTraits[index] = TRAIT_INVALID;
    }

    // Extras reset with the host — file statics, not malloc'd per run, so a new
    // game would otherwise inherit the previous one's traits (same trap as
    // perkResetRanks and pcStatsReset).
    for (int slot = 0; slot < kMaxPlayerActors - 1; slot++) {
        for (int index = 0; index < TRAITS_MAX_SELECTED_COUNT; index++) {
            gPlayerActorTraits[slot][index] = TRAIT_INVALID;
        }
    }
}

// 0x4B3AF8 trait_exit
void traitsExit()
{
    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_TRAIT, nullptr);
    messageListFree(&gTraitsMessageList);
    traitsResetSfallConfig();
}

// Loads trait system state from save game.
//
// 0x4B3B08 trait_load
int traitsLoad(File* stream)
{
    return fileReadInt32EnumList<Trait>(stream, gSelectedTraits, TRAITS_MAX_SELECTED_COUNT);
}

// Saves trait system state to save game.
//
// 0x4B3B28 trait_save
int traitsSave(File* stream)
{
    return fileWriteInt32EnumList<Trait>(stream, gSelectedTraits, TRAITS_MAX_SELECTED_COUNT);
}

// Sets selected traits.
//
// 0x4B3B48 trait_set
void traitsSetSelected(Trait trait1, Trait trait2, Object* subject)
{
    Trait* row = traitRow(subject);
    row[0] = trait1;
    row[1] = trait2;
}

// Returns selected traits.
//
// 0x4B3B54 trait_get
void traitsGetSelected(Trait* trait1, Trait* trait2, Object* subject)
{
    Trait* row = traitRow(subject);
    *trait1 = row[0];
    *trait2 = row[1];
}

// Returns a name of the specified trait, or `NULL` if the specified trait is
// out of range.
//
// 0x4B3B68 trait_name
char* traitGetName(Trait trait)
{
    return traitIsValid(trait) ? traitDescriptions[trait].name : nullptr;
}

// Returns a description of the specified trait, or `NULL` if the specified
// trait is out of range.
//
// 0x4B3B88 trait_description
char* traitGetDescription(Trait trait)
{
    return traitIsValid(trait) ? traitDescriptions[trait].description : nullptr;
}

// Return an FrmId of the specified trait, or FrmId::Empty() if the specified trait is
// out of range.
//
// 0x4B3BA8 trait_pic
SkillDexFrmId traitGetFrmId(Trait trait)
{
    return traitIsValid(trait) ? traitDescriptions[trait].frmId : SkillDexFrameId::Invalid;
}

// Returns `true` if the specified trait is selected.
//
// 0x4B3BC8 trait_level
bool traitIsSelected(Trait trait, Object* subject)
{
    Trait* row = traitRow(subject);
    return row[0] == trait || row[1] == trait;
}

bool traitIsSelectedAndActive(Trait trait, Object* subject)
{
    return traitIsValid(trait) && traitIsSelected(trait, subject) && !traitHardcodeDisabled[trait];
}

static void traitsLoadSfallConfig()
{
    char* perksFile = nullptr;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_PERKS_FILE_KEY, &perksFile);
    if (perksFile == nullptr || perksFile[0] == '\0') {
        return;
    }

    ScopedConfig config { perksFile, false };
    if (!config) {
        debugPrint("Traits config %s not found.\n", perksFile);
        return;
    }

    int enabled = 0;
    configGetInt(config.get(), "Traits", "Enable", &enabled, 1);
    traitOverridesEnabled = enabled != 0;
    if (!traitOverridesEnabled) {
        return;
    }

    char sectionKey[8];
    for (Trait trait = TRAIT_FIRST; trait < TRAIT_COUNT; trait++) {
        snprintf(sectionKey, sizeof(sectionKey), "t%d", trait);

        char* string = nullptr;
        if (configGetString(config.get(), sectionKey, "Name", &string) && string != nullptr) {
            traitOverrideNames[trait] = string;
            traitDescriptions[trait].name = traitOverrideNames[trait].data();
        }

        if (configGetString(config.get(), sectionKey, "Desc", &string) && string != nullptr) {
            traitOverrideDescriptions[trait] = string;
            traitDescriptions[trait].description = traitOverrideDescriptions[trait].data();
        }

        SkillDexFrameId image = SkillDexFrameId::Strength;
        if (configGetEnum<SkillDexFrameId>(config.get(), sectionKey, "Image", &image)) {
            traitDescriptions[trait].frmId = image;
        }

        traitsLoadSfallPairBonuses(config.get(), sectionKey, "StatMod", traitStatBonuses[trait], STAT_COUNT);
        traitsLoadSfallPairBonuses(config.get(), sectionKey, "SkillMod", traitSkillBonuses[trait], SKILL_COUNT);

        int noHardcode = 0;
        if (configGetInt(config.get(), sectionKey, "NoHardcode", &noHardcode, 0) && noHardcode != 0) {
            traitHardcodeDisabled[trait] = true;
        }
    }
}

static void traitsResetSfallConfig()
{
    traitOverridesEnabled = false;

    for (Trait trait = TRAIT_FIRST; trait < TRAIT_COUNT; trait++) {
        traitHardcodeDisabled[trait] = false;
        traitDescriptions[trait].name = nullptr;
        traitDescriptions[trait].description = nullptr;
        traitDescriptions[trait].frmId = defaultTraitFrmIds[trait];
        traitOverrideNames[trait].clear();
        traitOverrideDescriptions[trait].clear();

        for (Stat stat = STAT_FIRST; stat < STAT_COUNT; stat++) {
            traitStatBonuses[trait][stat] = 0;
        }

        for (Skill skill = SKILL_FIRST; skill < SKILL_COUNT; skill++) {
            traitSkillBonuses[trait][skill] = 0;
        }
    }
}

static void traitsLoadSfallPairBonuses(Config* config, const char* sectionKey, const char* key, int* bonuses, int count)
{
    char* string = nullptr;
    if (!configGetString(config, sectionKey, key, &string) || string == nullptr || string[0] == '\0') {
        return;
    }

    char buffer[512];
    strncpy(buffer, string, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* id = strtok(buffer, "|");
    char* mod = strtok(nullptr, "|");
    while (id != nullptr && mod != nullptr) {
        int index = atoi(id);
        if (index >= 0 && index < count) {
            bonuses[index] = atoi(mod);
        }

        id = strtok(nullptr, "|");
        mod = strtok(nullptr, "|");
    }
}

// Returns stat modifier depending on selected traits.
//
// 0x4B3C7C trait_adjust_stat
int traitGetStatModifier(Stat stat, Object* subject)
{
    int modifier = 0;

    if (traitOverridesEnabled && statIsValid(stat)) {
        for (int index = 0; index < TRAITS_MAX_SELECTED_COUNT; index++) {
            Trait trait = traitRow(subject)[index];
            if (traitIsValid(trait)) {
                modifier += traitStatBonuses[trait][stat];
            }
        }
    }

    switch (stat) {
    case STAT_STRENGTH:
        if (traitIsSelectedAndActive(TRAIT_GIFTED, subject)) {
            modifier += 1;
        }
        if (traitIsSelectedAndActive(TRAIT_BRUISER, subject)) {
            modifier += 2;
        }
        break;
    case STAT_PERCEPTION:
        if (traitIsSelectedAndActive(TRAIT_GIFTED, subject)) {
            modifier += 1;
        }
        break;
    case STAT_ENDURANCE:
        if (traitIsSelectedAndActive(TRAIT_GIFTED, subject)) {
            modifier += 1;
        }
        break;
    case STAT_CHARISMA:
        if (traitIsSelectedAndActive(TRAIT_GIFTED, subject)) {
            modifier += 1;
        }
        break;
    case STAT_INTELLIGENCE:
        if (traitIsSelectedAndActive(TRAIT_GIFTED, subject)) {
            modifier += 1;
        }
        break;
    case STAT_AGILITY:
        if (traitIsSelectedAndActive(TRAIT_GIFTED, subject)) {
            modifier += 1;
        }
        if (traitIsSelectedAndActive(TRAIT_SMALL_FRAME, subject)) {
            modifier += 1;
        }
        break;
    case STAT_LUCK:
        if (traitIsSelectedAndActive(TRAIT_GIFTED, subject)) {
            modifier += 1;
        }
        break;
    case STAT_MAXIMUM_ACTION_POINTS:
        if (traitIsSelectedAndActive(TRAIT_BRUISER, subject)) {
            modifier -= 2;
        }
        break;
    case STAT_ARMOR_CLASS:
        if (traitIsSelectedAndActive(TRAIT_KAMIKAZE, subject)) {
            modifier -= critterGetBaseStat(subject, STAT_ARMOR_CLASS);
        }
        break;
    case STAT_MELEE_DAMAGE:
        if (traitIsSelectedAndActive(TRAIT_HEAVY_HANDED, subject)) {
            modifier += 4;
        }
        break;
    case STAT_CARRY_WEIGHT:
        if (traitIsSelectedAndActive(TRAIT_SMALL_FRAME, subject)) {
            modifier -= 10 * critterGetBaseStat(subject, STAT_STRENGTH);
        }
        break;
    case STAT_SEQUENCE:
        if (traitIsSelectedAndActive(TRAIT_KAMIKAZE, subject)) {
            modifier += 5;
        }
        break;
    case STAT_HEALING_RATE:
        if (traitIsSelectedAndActive(TRAIT_FAST_METABOLISM, subject)) {
            modifier += 2;
        }
        break;
    case STAT_CRITICAL_CHANCE:
        if (traitIsSelectedAndActive(TRAIT_FINESSE, subject)) {
            modifier += 10;
        }
        break;
    case STAT_BETTER_CRITICALS:
        if (traitIsSelectedAndActive(TRAIT_HEAVY_HANDED, subject)) {
            modifier -= 30;
        }
        break;
    case STAT_RADIATION_RESISTANCE:
        if (traitIsSelectedAndActive(TRAIT_FAST_METABOLISM, subject)) {
            modifier -= critterGetBaseStat(subject, STAT_RADIATION_RESISTANCE);
        }
        break;
    case STAT_POISON_RESISTANCE:
        if (traitIsSelectedAndActive(TRAIT_FAST_METABOLISM, subject)) {
            modifier -= critterGetBaseStat(subject, STAT_POISON_RESISTANCE);
        }
        break;
    default:
        break;
    }

    return modifier;
}

// Returns skill modifier depending on selected traits.
//
// 0x4B40FC trait_adjust_skill
int traitGetSkillModifier(Skill skill, Object* subject)
{
    int modifier = 0;

    if (traitOverridesEnabled && skillIsValid(skill)) {
        for (int index = 0; index < TRAITS_MAX_SELECTED_COUNT; index++) {
            Trait trait = traitRow(subject)[index];
            if (traitIsValid(trait)) {
                modifier += traitSkillBonuses[trait][skill];
            }
        }
    }

    if (traitIsSelectedAndActive(TRAIT_GIFTED, subject)) {
        modifier -= 10;
    }

    if (traitIsSelectedAndActive(TRAIT_GOOD_NATURED, subject)) {
        switch (skill) {
        case SKILL_SMALL_GUNS:
        case SKILL_BIG_GUNS:
        case SKILL_ENERGY_WEAPONS:
        case SKILL_UNARMED:
        case SKILL_MELEE_WEAPONS:
        case SKILL_THROWING:
            modifier -= 10;
            break;
        case SKILL_FIRST_AID:
        case SKILL_DOCTOR:
        case SKILL_SPEECH:
        case SKILL_BARTER:
            modifier += 15;
            break;
        default:
            break;
        }
    }

    return modifier;
}



static Trait* traitRow(Object* subject)
{
    int slot = playerActorSlotOf(subject != nullptr ? subject : gDude);
    return slot > 0 ? gPlayerActorTraits[slot - 1] : gSelectedTraits;
}

void traitsPlayerActorSeed()
{
    for (int slot = 1; slot < kMaxPlayerActors; slot++) {
        traitsPlayerActorSeedSlot(slot);
    }
}

void traitsPlayerActorSeedSlot(int slot)
{
    if (slot < 1 || slot >= kMaxPlayerActors) {
        return;
    }

    for (int index = 0; index < TRAITS_MAX_SELECTED_COUNT; index++) {
        gPlayerActorTraits[slot - 1][index] = gSelectedTraits[index];
    }
}

static Trait* traitsPlayerActorRow(int slot)
{
    if (slot == 0) {
        return gSelectedTraits;
    }

    if (slot < 1 || slot >= kMaxPlayerActors) {
        return nullptr;
    }

    return gPlayerActorTraits[slot - 1];
}

int traitsPlayerActorRowWrite(File* stream, int slot)
{
    Trait* row = traitsPlayerActorRow(slot);
    if (row == nullptr) {
        return -1;
    }

    return fileWriteInt32EnumList<Trait>(stream, row, TRAITS_MAX_SELECTED_COUNT);
}

int traitsPlayerActorRowRead(File* stream, int slot)
{
    Trait* row = traitsPlayerActorRow(slot);
    if (row == nullptr) {
        return -1;
    }

    return fileReadInt32EnumList<Trait>(stream, row, TRAITS_MAX_SELECTED_COUNT);
}

void traitsMutateDrop(Trait* traits, int traitCount, int pickedLine, Trait firstListedTrait)
{
    if (pickedLine == 0) {
        if (traitCount == 1) {
            traits[0] = TRAIT_INVALID;
            traits[1] = TRAIT_INVALID;
        } else {
            if (firstListedTrait == traits[0]) {
                traits[0] = traits[1];
                traits[1] = TRAIT_INVALID;
            } else {
                traits[1] = TRAIT_INVALID;
            }
        }
    } else {
        if (firstListedTrait == traits[0]) {
            traits[1] = TRAIT_INVALID;
        } else {
            traits[0] = traits[1];
            traits[1] = TRAIT_INVALID;
        }
    }
}

void traitsMutateGain(Trait* traits, int traitCount, Trait newTrait)
{
    if (traitCount != 0) {
        traits[1] = newTrait;
    } else {
        traits[0] = newTrait;
        traits[1] = TRAIT_INVALID;
    }

    traitsSetSelected(traits[0], traits[1]);
}

static bool traitHas(Object* subject, Trait trait)
{
    return traitIsSelected(trait, subject);
}

} // namespace fallout
