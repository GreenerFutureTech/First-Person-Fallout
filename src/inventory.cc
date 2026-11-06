#include <stdlib.h>
#include "inventory_ui.h"
#include "pres_record.h"
#include "presenter.h"
#include "server_loop.h"
#include "server_players.h"
#include "barter_intent.h"
#include "client_barter.h"
#include "client_dialog.h"
#include "client_steal.h" // the viewer half of a server-owned steal session
#include "object_delta.h" // objectDeltaScan — a parked tick streams nothing on its own
#include "steal_intent.h"
#include "msg_channel.h"
#include "client_net.h" // clientViewerActive — inventory is view-only on the wire viewer (Slice 3)
#include "geometry.h"
#include "memory.h"
#include "sfall_config.h"
#include "trait.h"
#include "inventory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "actions.h"
#include "animation.h"
#include "art.h"
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
#include "content_config.h"
#include "critter.h"
#include "dbox.h"
#include "debug.h"
#include "dialog.h"
#include "display_monitor.h"
#include "draw.h"
#include "game.h"
#include "game_dialog.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "input.h"
#include "interface.h"
#include "interpreter_extra.h"
#include "item.h"
#include "kb.h"
#include "light.h"
#include "map.h"
#include "message.h"
#include "mouse.h"
#include "object.h"
#include "party_member.h"
#include "perk.h"
#include "platform_compat.h"
#include "proto.h"
#include "proto_instance.h"
#include "random.h"
#include "reaction.h"
#include "scripts.h"
#include "settings.h"
#include "sfall_script_hooks.h"
#include "skill.h"
#include "stat.h"
#include "svga.h"
#include "text_font.h"
#include "tile.h"
#include "window_manager.h"

namespace fallout {

void invenRederiveWeaponFid(Object* critter);



























































// offsets for normal inventory window (not steal, loot, &c.)


















































































// 0x46E6D0 stats_array0


// 0x46E6EC stats_array1


// 0x46E708


// The number of items to show in scroller.
//
// 0x519054 inven_cur_disp









// 0x519058 inven_dude
Object* _inven_dude = nullptr;

// Probably fid of armor to display in inventory dialog.
//
// 0x51905C inven_pid
ProtoId inventoryProtoId = ProtoId::Empty();

// 0x519060 inven_is_initialized


// 0x519064 inven_display_msg_line


// 0x519068 iscr_data


// 0x5190E0 dropped_explosive


// 0x5190E4 inven_scroll_up_bid


// 0x5190E8 inven_scroll_dn_bid


// 0x5190EC loot_scroll_up_bid


// 0x5190F0 loot_scroll_dn_bid


// 0x5190F4 ticker


// 0x5190F8 curr_rot


// 0x5190FC num


// 0x519110 last_target


// 0x519114 act_use


// 0x519124 act_no_use


// 0x519130 act_just_use


// 0x51913C act_nothing


// 0x519144 act_weap


// 0x519154 act_weap2


// Scroll offsets to target inventory for every container nesting level (stack).
// 0x59E7EC target_stack_offset


// inventory.msg
//
// 0x59E814 inventry_message_file


// Current target critter or container for every nesting level (stack).
// 0x59E81C target_stack


// Scroll offsets to main inventory for every container nesting level (stack).
// 0x59E844 stack_offset


// Current critter or container for every nesting level (stack).
// 0x59E86C stack


// 0x59E894 mt_wid


static constexpr int kDefaultInventoryApCost = 4;
static constexpr int kDefaultQuickPocketsApCostReduction = 2;

// Current inventory AP cost
static int inventoryApCost = kDefaultInventoryApCost;
static int inventoryDefaultApCost = kDefaultInventoryApCost;
static int quickPocketsApCostReduction = kDefaultQuickPocketsApCostReduction;

// Current barter price modifier, based on gGameDialogBarterModifier, with the reaction-based modifier added (in percent).
// 0x59E898 barter_mod


// 0x59E89C btable_offset


// 0x59E8A0 ptable_offset


// An inventory containing a subset of Player's items offered to the NPC during barter.
// 0x59E8A4 ptable_pud


// 0x59E8A8 imdata


// An object (PID -1) with an inventory containing a subset of Player's items offered to the NPC during barter.
// 0x59E934 ptable


// 0x59E938 display_msg


// 0x59E93C im_value
 // "keyCode" corresponding to an inventory item "button", or -1 if nothing

// 0x59E940 immode


// An object (PID -1) with an inventory containing a subset of NPC's items asked for by the player during barter.
// 0x59E944 btable


// Current nesting level for viewing target's bag/backpack contents.
// 0x59E948 target_curr_stack


// 0x59E94C btable_pud


// 0x59E950 inven_ui_was_disabled


// 0x59E954 i_worn
Object* gInventoryArmor;

// 0x59E958 i_lhand
Object* gInventoryLeftHandItem;

// Rotating character's fid.
//
// 0x59E95C i_fid


// 0x59E960 pud


// 0x59E964 i_wid


// item2
// 0x59E968 i_rhand
Object* gInventoryRightHandItem;

// Current nesting level for viewing bag/backpack contents.
// 0x59E96C curr_stack


// 0x59E970 i_wid_max_y


// 0x59E974 i_wid_max_x


// 0x59E978 target_pud


// 0x59E97C barter_back_win


// Weight of equipped items stripped from the left/right loot-pane critter.
// Non-zero only while inventoryOpenLooting is active.

 // stripped equipment of party member being "looted"




































































































// 0x46E724
void inventoryResetDude()
{
    _inven_dude = gDude;
    inventoryProtoId = CritterProtoTypeId::Dude;
}

int inventoryGetInvenApCost()
{
    int quickPockets = 0;
    if (gDude != nullptr) {
        quickPockets = perkGetRank(gDude, PERK_QUICK_POCKETS);
    }

    return std::max(inventoryApCost - quickPocketsApCostReduction * quickPockets, 0);
}

void inventorySetInvenApCost(int cost)
{
    inventoryApCost = std::max(cost, 0);
}

void inventoryResetInvenApCost()
{
    configGetInt(&gContentConfig, CONTENT_CONFIG_COMBAT_SECTION, "inventory_ap_cost", &inventoryDefaultApCost, kDefaultInventoryApCost);
    configGetInt(&gContentConfig, CONTENT_CONFIG_COMBAT_SECTION, "quick_pockets_ap_cost_reduction", &quickPocketsApCostReduction, kDefaultQuickPocketsApCostReduction);

    inventoryApCost = inventoryDefaultApCost;
}

// inven_set_dude
void inventorySetDude(Object* obj, const ProtoId& protoId)
{
    _inven_dude = obj;
    inventoryProtoId = protoId;
}

// TODO(CE): move to more generic location
FrmId inventoryComputeCritterFrmId(Object* critter, const ProtoId& baseProtoId, Object* rightHandItem, Object* leftHandItem, Object* armor, Hand activeHand, AnimationType anim, Rotation rotation)
{
    if (FrmId(critter).objectType() != OBJ_TYPE_CRITTER) {
        return FrmId(critter);
    }

    Proto* proto = nullptr;

    CritterFrameId inventoryFrameId = _art_vault_guy_num;
    if (protoGetProto(baseProtoId, &proto) != -1) {
        inventoryFrameId = FrmId(proto).frameId<CritterFrameId>();
    }

    if (armor != nullptr) {
        if (protoGetProto(armor, &proto) != -1 && proto != nullptr) {
            if (critterGetStat(critter, STAT_GENDER) == GENDER_FEMALE) {
                inventoryFrameId = FrmId(proto->item.data.armor.femaleFid).frameId<CritterFrameId>();
            } else {
                inventoryFrameId = FrmId(proto->item.data.armor.maleFid).frameId<CritterFrameId>();
            }

            if (inventoryFrameId == CritterFrameId::Invalid) {
                inventoryFrameId = _art_vault_guy_num;
            }
        }
    }

    WeaponAnimation animationCode = WeaponAnimation::None;
    Object* itemInHand = activeHand == HAND_RIGHT ? rightHandItem : leftHandItem;
    if (itemInHand != nullptr) {
        if (protoGetProto(itemInHand, &proto) != -1
            && proto != nullptr
            && proto->item.type == ITEM_TYPE_WEAPON) {
            animationCode = proto->item.data.weapon.animationCode;
        }
    }

    return CritterFrmId(inventoryFrameId, anim, animationCode, rotation);
}

// inventory_msg_init
// 0x46E73C




// inventory_msg_free
// 0x46E7A0


// 0x46E7B0


// 0x46EC90


// 0x46FBD8


// 0x46FDF4 display_inventory


// Render inventory item.
//
// [stackOffset] is an index of the first visible item in the scrolling view.
// [dragSlotIndex] is an index of item being dragged (it decreases displayed number of items in inner functions).
//
// 0x47036C


// Renders inventory item quantity.
//
// 0x4705A0


// 0x470650 display_body displays both left and right character portraits


// 0x470A2C


// NOTE: Inlined.
//
// 0x470B8C


// 0x470BCC


// 0x470C2C


// 0x470D1C


// 0x470D5C


// 0x470DB8


// 0x4714E0


// This function removes armor bonuses and effects granted by [oldArmor] and
// adds appropriate bonuses and effects granted by [newArmor]. Both [oldArmor]
// and [newArmor] can be NULL.
//
// 0x4715F8
void adjustCritterStatsOnArmorChange(Object* critter, Object* oldArmor, Object* newArmor)
{
    int armorClassBonus = critterGetBonusStat(critter, STAT_ARMOR_CLASS);
    int oldArmorClass = armorGetArmorClass(oldArmor);
    int newArmorClass = armorGetArmorClass(newArmor);
    critterSetBonusStat(critter, STAT_ARMOR_CLASS, armorClassBonus - oldArmorClass + newArmorClass);

    Stat damageResistanceStat = STAT_DAMAGE_RESISTANCE;
    Stat damageThresholdStat = STAT_DAMAGE_THRESHOLD;
    for (DamageType damageType = DAMAGE_TYPE_FIRST; damageType < DAMAGE_TYPE_COUNT; damageType++) {
        int damageResistanceBonus = critterGetBonusStat(critter, damageResistanceStat);
        int oldArmorDamageResistance = armorGetDamageResistance(oldArmor, damageType);
        int newArmorDamageResistance = armorGetDamageResistance(newArmor, damageType);
        critterSetBonusStat(critter, damageResistanceStat, damageResistanceBonus - oldArmorDamageResistance + newArmorDamageResistance);

        int damageThresholdBonus = critterGetBonusStat(critter, damageThresholdStat);
        int oldArmorDamageThreshold = armorGetDamageThreshold(oldArmor, damageType);
        int newArmorDamageThreshold = armorGetDamageThreshold(newArmor, damageType);
        critterSetBonusStat(critter, damageThresholdStat, damageThresholdBonus - oldArmorDamageThreshold + newArmorDamageThreshold);

        damageResistanceStat++;
        damageThresholdStat++;
    }

    if (objectIsPartyMember(critter)) {
        if (oldArmor != nullptr) {
            Perk perk = armorGetPerk(oldArmor);
            perkRemoveEffect(critter, perk);
        }

        if (newArmor != nullptr) {
            Perk perk = armorGetPerk(newArmor);
            perkAddEffect(critter, perk);
        }
    }
}

// 0x4716E8 adjust_fid


// 0x4717E4 use_inventory_on


// 0x471B70
Object* critterGetItem2(Object* critter)
{
    int i;
    Inventory* inventory;
    Object* item;

    if (gInventoryRightHandItem != nullptr && critter == _inven_dude) {
        return gInventoryRightHandItem;
    }

    inventory = &(critter->data.inventory);
    for (i = 0; i < inventory->length; i++) {
        item = inventory->items[i].item;
        if (item->flags & OBJECT_IN_RIGHT_HAND) {
            return item;
        }
    }

    return nullptr;
}

// 0x471BBC
Object* critterGetItem1(Object* critter)
{
    int i;
    Inventory* inventory;
    Object* item;

    if (gInventoryLeftHandItem != nullptr && critter == _inven_dude) {
        return gInventoryLeftHandItem;
    }

    inventory = &(critter->data.inventory);
    for (i = 0; i < inventory->length; i++) {
        item = inventory->items[i].item;
        if (item->flags & OBJECT_IN_LEFT_HAND) {
            return item;
        }
    }

    return nullptr;
}

// 0x471C08
Object* critterGetArmor(Object* critter)
{
    int i;
    Inventory* inventory;
    Object* item;

    if (gInventoryArmor != nullptr && critter == _inven_dude) {
        return gInventoryArmor;
    }

    inventory = &(critter->data.inventory);
    for (i = 0; i < inventory->length; i++) {
        item = inventory->items[i].item;
        if (item->flags & OBJECT_WORN) {
            return item;
        }
    }

    return nullptr;
}

// Critter inventories are displayed without equipped items in almost all cases.  This is accomplished by
// temporarily removing them when inventory is viewed.
CritterEquipped critterStripEquipped(Object* critter)
{
    CritterEquipped equipped;
    Inventory* inv = &critter->data.inventory;
    for (int i = 0; i < inv->length; i++) {
        Object* item = inv->items[i].item;
        if ((item->flags & OBJECT_IN_LEFT_HAND) != OBJECT_NONE) {
            if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
                equipped.rightHand = item;
            }
            equipped.leftHand = item;
        } else if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
            equipped.rightHand = item;
        } else if ((item->flags & OBJECT_WORN) != OBJECT_NONE) {
            equipped.armor = item;
        }
    }
    if (equipped.leftHand != nullptr) {
        equipped.weight += itemGetWeight(equipped.leftHand);
        itemRemoveWithReason(critter, equipped.leftHand, 1, RemoveInventoryObjectHookReason::LeftHandEquipped);
    }
    if (equipped.rightHand != nullptr && equipped.rightHand != equipped.leftHand) {
        equipped.weight += itemGetWeight(equipped.rightHand);
        itemRemoveWithReason(critter, equipped.rightHand, 1, RemoveInventoryObjectHookReason::RightHandEquipped);
    }
    if (equipped.armor != nullptr) {
        equipped.weight += itemGetWeight(equipped.armor);
        itemRemoveWithReason(critter, equipped.armor, 1, RemoveInventoryObjectHookReason::ArmorEquipped);
    }
    return equipped;
}

void critterRestoreEquipped(Object* critter, CritterEquipped& equipped)
{
    if (equipped.leftHand != nullptr) {
        equipped.leftHand->flags |= OBJECT_IN_LEFT_HAND;
        if (equipped.leftHand == equipped.rightHand) equipped.leftHand->flags |= OBJECT_IN_RIGHT_HAND;
        itemAdd(critter, equipped.leftHand, 1);
    }
    if (equipped.rightHand != nullptr && equipped.rightHand != equipped.leftHand) {
        equipped.rightHand->flags |= OBJECT_IN_RIGHT_HAND;
        itemAdd(critter, equipped.rightHand, 1);
    }
    if (equipped.armor != nullptr) {
        equipped.armor->flags |= OBJECT_WORN;
        itemAdd(critter, equipped.armor, 1);
    }
    equipped = {};
}







// 0x471CA0
Object* objectGetCarriedObjectByProtoId(Object* obj, const ProtoId& protoId)
{
    Inventory* inventory = &(obj->data.inventory);

    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        if (ProtoId(inventoryItem->item) == protoId) {
            return inventoryItem->item;
        }

        Object* found = objectGetCarriedObjectByProtoId(inventoryItem->item, protoId);
        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

// 0x471CDC
int objectGetCarriedQuantityByProtoId(Object* object, const ProtoId& protoId)
{
    int quantity = 0;

    Inventory* inventory = &(object->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        if (ProtoId(inventoryItem->item) == protoId) {
            quantity += inventoryItem->quantity;
        }

        quantity += objectGetCarriedQuantityByProtoId(inventoryItem->item, protoId);
    }

    return quantity;
}

// Renders character's summary of SPECIAL stats, equipped armor bonuses,
// and weapon's damage/range.
//
// 0x471D5C display_stats


// Finds next item of given [itemType] (can be -1 which means any type of
// item).
//
// The [index] is used to control where to continue the search from, -1 - from
// the beginning.
//
// 0x472698
Object* inventoryFindByType(Object* obj, ItemType itemType, int* indexPtr)
{
    int dummy = -1;
    if (indexPtr == nullptr) {
        indexPtr = &dummy;
    }

    *indexPtr += 1;

    Inventory* inventory = &(obj->data.inventory);

    // TODO: Refactor with for loop.
    if (*indexPtr >= inventory->length) {
        return nullptr;
    }

    while (itemType != -1 && itemGetType(inventory->items[*indexPtr].item) != itemType) {
        *indexPtr += 1;

        if (*indexPtr >= inventory->length) {
            return nullptr;
        }
    }

    return inventory->items[*indexPtr].item;
}

// Searches for an item with a given id inside given obj's inventory.
//
// 0x4726EC
Object* inventoryFindById(Object* obj, int id)
{
    if (obj->id == id) {
        return obj;
    }

    Inventory* inventory = &(obj->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        Object* item = inventoryItem->item;
        if (item->id == id) {
            return item;
        }

        if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
            item = inventoryFindById(item, id);
            if (item != nullptr) {
                return item;
            }
        }
    }

    return nullptr;
}

// Returns inventory item at a given index.
//
// 0x472740
Object* inventoryItemByIndex(Object* obj, int index)
{
    Inventory* inventory;

    inventory = &(obj->data.inventory);

    if (index < 0 || index >= inventory->length) {
        return nullptr;
    }

    return inventory->items[index].item;
}

// inven_wield
// 0x472758
int inventoryEquip(Object* critter, Object* item, Hand hand)
{
    return inventoryEquipFunc(critter, item, hand, true);
}

// 0x472768
int inventoryEquipFunc(Object* critter, Object* item, Hand handIndex, bool animate)
{
    ItemType itemType = itemGetType(item);

    InvenSlot invenSlot = itemType == ITEM_TYPE_ARMOR
        ? InvenSlot::Armor
        : (handIndex == HAND_RIGHT ? InvenSlot::RightHand : InvenSlot::LeftHand);
    if (!scriptHooks_InvenWield(critter, item, invenSlot, 1, 0)) {
        return -1;
    }

    // PRESENTATION-RECORD (weapon-draw family, PRESENTATION_RECORD_REPLAY_SPEC.md §8):
    // capture the wield's reg_anim leaves (put-away + take-out draw) in a record section
    // and ship them as EVENT_PRES_SEQ instead of the combat-only weaponTakeOut cue below.
    // Unlike explosion/damage there is NO !serverLoopActive() gate to relax here — iso is
    // enabled server-side so this animate branch already runs; the section only CAPTURES
    // its leaves. The inline authoritative state (in-hand flags / armed fid / light) still
    // executes (the section restores only RNG, which these leaves don't draw). Ship only an
    // actual weapon TAKE-OUT (recordedDraw); armor / unarmed swaps abort the section — their
    // fid rides OBJECT_DELTA, so shipping would double-apply on the viewer. The section must
    // wrap reg_anim_begin (it records SEQ_BEGIN), so the mid-function -1 returns abort it.
    // Actor = the wielder, so the viewer glues the draw behind any approach glide. Inert
    // (recording=false) in the client/golden binary → goldens byte-identical.
    bool recording = animate && !isoIsDisabled() && presRecordEnabled();
    bool recordedDraw = false;
    if (recording) {
        presRecordSectionBegin();
    }

    if (animate) {
        if (!isoIsDisabled()) {
            reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        }
    }

    if (itemType == ITEM_TYPE_ARMOR) {
        Object* armor = critterGetArmor(critter);
        if (armor != nullptr) {
            armor->flags &= ~OBJECT_WORN;
        }

        item->flags |= OBJECT_WORN;

        CritterFrameId baseFrameId;
        if (critterGetStat(critter, STAT_GENDER) == GENDER_FEMALE) {
            baseFrameId = armorGetFemaleFrameId(item);
        } else {
            baseFrameId = armorGetMaleFrameId(item);
        }

        if (baseFrameId == CritterFrameId::Invalid) {
            baseFrameId = CritterFrameId::First;
        }

        if (critter == gDude) {
            if (!isoIsDisabled()) {
                const CritterFrmId frmId = CritterFrmId(baseFrameId, ANIM_STAND, FrmId(critter).weaponAnimation(), critter->rotation + 1);
                animationRegisterSetFrmId(critter, frmId, 0);
            }
        } else {
            adjustCritterStatsOnArmorChange(critter, armor, item);
        }
    } else {
        Hand hand;
        if (critter == gDude) {
            hand = interfaceGetCurrentHand();
        } else {
            hand = HAND_RIGHT;
        }

        bool itemIsWeapon = itemType == ITEM_TYPE_WEAPON;
        WeaponAnimation weaponAnimationCode = itemIsWeapon ? weaponGetAnimationCode(item) : WeaponAnimation::None;
        AnimationType hitModeAnimationCode = itemIsWeapon ? weaponGetAnimationForHitMode(item, HIT_MODE_RIGHT_WEAPON_PRIMARY) : ANIM_STAND;
        const FrmId frmId = FrmId(critter, hitModeAnimationCode, weaponAnimationCode, critter->rotation + 1);
        if (!frmId.exist()) {
            debugPrint("\ninven_wield failed!  ERROR ERROR ERROR!");
            fprintf(stderr, "f2_server: inven_wield FAILED (art missing) critter_net=%d pid=%d "
                            "item_pid=%d fid=0x%x baseFrm=%d hitAnim=%d weapAnim=%d — it will "
                            "fight UNARMED\n",
                critter->netId, critter->pid, item->pid, fid, critter->fid & 0xFFF,
                hitModeAnimationCode, weaponAnimationCode);
            if (recording) {
                presRecordSectionAbort();
            }
            return -1;
        }

        Object* equippedItem;
        if (handIndex == HAND_RIGHT) {
            equippedItem = critterGetItem2(critter);
            item->flags |= OBJECT_IN_RIGHT_HAND;
        } else {
            equippedItem = critterGetItem1(critter);
            item->flags |= OBJECT_IN_LEFT_HAND;
        }
        if (getenv("F2_TRACE_EVENTS") != nullptr) {
            fprintf(stderr, "[wield] critter_net=%d item_pid=%d hand=%d animate=%d\n",
                critter->netId, item->pid, handIndex, animate);
        }

        Rect rect;
        if (equippedItem != nullptr) {
            equippedItem->flags &= ~OBJECT_IN_ANY_HAND;

            if (ProtoId(equippedItem) == ItemProtoTypeId::LitFlare) {
                critterRestoreLightWithoutFlare(critter);
            }
        }

        if (ProtoId(item) == ItemProtoTypeId::LitFlare) {
            int lightDistance = item->lightDistance;
            if (lightDistance < critter->lightDistance) {
                lightDistance = critter->lightDistance;
            }

            int lightIntensity = item->lightIntensity;
            if (lightIntensity < critter->lightIntensity) {
                lightIntensity = critter->lightIntensity;
            }

            objectSetLight(critter, lightDistance, lightIntensity, &rect);
            presenter()->worldInvalidateRect(&rect, gElevation);
        }

        // SFALL: Fix for "Unlimited Ammo" exploit: one item of a stack is
        // wielded, the rest stays in the inventory. A whole stack in hand
        // (from `wield_obj_critter`, a party member's equipping) shared one
        // weapon's ammo.
        ObjectFlags handFlags = item->flags & OBJECT_IN_ANY_HAND;
        bool removed = itemRemoveWithReason(critter, item, 1, RemoveInventoryObjectHookReason::WieldStack, nullptr) == 0;

        Object* stackInHand = handIndex == HAND_RIGHT ? critterGetItem2(critter) : critterGetItem1(critter);
        if (stackInHand != nullptr) {
            stackInHand->flags &= ~OBJECT_IN_ANY_HAND;
        }

        item->flags |= handFlags;

        if (removed) {
            itemAdd(critter, item, 1);
        }

        if (itemGetType(item) == ITEM_TYPE_WEAPON) {
            weaponAnimationCode = weaponGetAnimationCode(item);
        } else {
            weaponAnimationCode = WeaponAnimation::None;
        }

        if (hand == handIndex) {
            if (serverLoopActive() && isInCombat()) {
                objectSetFrmId(critter, FrmId(critter, ANIM_STAND, weaponAnimationCode, critter->rotation + 1), nullptr);
                objectSetFrame(critter, 0, nullptr);
            }
            if (FrmId(critter).weaponAnimation() != WeaponAnimation::None) {
                if (animate) {
                    if (!isoIsDisabled()) {
                        const char* soundEffectName = sfxBuildCharName(critter, ANIM_PUT_AWAY, CharacterSoundEffect::Unused);
                        animationRegisterPlaySoundEffect(critter, soundEffectName, 0);
                        animationRegisterAnimate(critter, ANIM_PUT_AWAY, 0);
                    }
                }
            }

            if (animate && !isoIsDisabled()) {
                if (weaponAnimationCode != WeaponAnimation::None) {
                    animationRegisterTakeOutWeapon(critter, weaponAnimationCode, -1);
                    recordedDraw = true; // a real draw — ship the recorded section below
                } else {
                    const FrmId frmId = FrmId(critter, ANIM_STAND, WeaponAnimation::None, critter->rotation + 1);
                    animationRegisterSetFrmId(critter, frmId, -1);
                }
            } else {
                const FrmId frmId = FrmId(critter, ANIM_STAND, weaponAnimationCode, critter->rotation + 1);
                _dude_stand(critter, critter->rotation, frmId);
            }
        }
    }

    if (animate) {
        if (!isoIsDisabled()) {
            int rc = reg_anim_end();
            if (recording) {
                if (recordedDraw) {
                    presRecordSectionEnd();
                    presenter()->presSeq(presRecordData(), presRecordSize(), presRecordOpCount(), critter->netId);
                } else {
                    // Armor / unarmed swap: nothing to replay (fid rides OBJECT_DELTA).
                    presRecordSectionAbort();
                }
            }
            return rc;
        }
    }

    if (recording) {
        // recording implies animate && !isoIsDisabled(), so the branch above returned;
        // this only balances the section if that invariant ever changes.
        presRecordSectionAbort();
    }
    return 0;
}

// inven_unwield
// 0x472A54
int inventoryUnequip(Object* critter_obj, Hand hand)
{
    return inventoryUnequipFunc(critter_obj, hand, true);
}

// CE: Restore a critter's own light unless a lit flare is still in either hand.
void critterRestoreLightWithoutFlare(Object* critter)
{
    Object* leftHandItem = critterGetItem1(critter);
    if (leftHandItem != nullptr && ProtoId(leftHandItem) == ItemProtoTypeId::LitFlare) {
        return;
    }

    Object* rightHandItem = critterGetItem2(critter);
    if (rightHandItem != nullptr && ProtoId(rightHandItem) == ItemProtoTypeId::LitFlare) {
        return;
    }

    int lightDistance;
    int lightIntensity;
    if (critter == gDude) {
        lightDistance = 4;
        lightIntensity = LIGHT_INTENSITY_MAX;
    } else {
        Proto* proto;
        if (protoGetProto(critter, &proto) == -1) {
            return;
        }

        lightDistance = proto->lightDistance;
        lightIntensity = proto->lightIntensity;
    }

    Rect rect;
    if (objectSetLight(critter, lightDistance, lightIntensity, &rect) == 0) {
        tileWindowRefreshRect(&rect, critter->elevation);
    }
}

// 0x472A64
int inventoryUnequipFunc(Object* critter, Hand hand, bool animate)
{
    int activeHand;
    Object* item;

    // The ACTIVE hand decides whether this unwield touches the fid at all — only the active
    // hand's weapon is on the SPRITE (fid bits 0xF000). Three answers, in authority order:
    //
    // ►► A PLAYER ACTOR ON A DEDICATED SERVER: the per-seat registry value (`hand` verb).
    // interfaceGetCurrentHand() is a STUB there — `return HAND_LEFT`, server_stubs.cc, and
    // already noted as a banked gap in that file — so asking it made every RIGHT-hand
    // unwield fail `activeHand == hand` and skip the fid re-derive below. Owner-reported
    // symptom: "no weapon equipped, but my character is still holding some kind of rifle."
    // It is a shared-path defect, not a co-op one; a caller working around it (destroying a
    // weapon and then re-deriving the fid itself) would just fork this function.
    //
    // gDude ON A CLIENT keeps its own interface, so single-player and the goldens are
    // untouched; anyone else keeps vanilla's NPCs-only-use-the-right-slot assumption.
    if (serverDedicatedActive() && playerActorIs(critter)) {
        activeHand = serverActorActiveHand(playerActorSlotOf(critter));
    } else if (critter == gDude) {
        activeHand = interfaceGetCurrentHand();
    } else {
        activeHand = HAND_RIGHT; // NPC's only ever use right slot
    }

    if (hand) {
        item = critterGetItem2(critter);
    } else {
        item = critterGetItem1(critter);
    }

    // Notify scripts before mutating the OBJECT_IN_ANY_HAND flag..
    if (item != nullptr) {
        InvenSlot invenSlot = hand == HAND_RIGHT ? InvenSlot::RightHand : InvenSlot::LeftHand;
        if (!scriptHooks_InvenWield(critter, item, invenSlot, 0, 0)) {
            return -1;
        }
    }

    if (item) {
        item->flags &= ~OBJECT_IN_ANY_HAND;

        // CE: Restore light when unwielding a lit flare.
        if (ProtoId(item) == ItemProtoTypeId::LitFlare) {
            critterRestoreLightWithoutFlare(critter);
        }
    }

    if (activeHand == hand && (FrmId(critter).weaponAnimation() != WeaponAnimation::None)) {
        if (animate && !isoIsDisabled()) {
            reg_anim_begin(ANIMATION_REQUEST_RESERVED);

            const char* sfx = sfxBuildCharName(critter, ANIM_PUT_AWAY, CharacterSoundEffect::Unused);
            animationRegisterPlaySoundEffect(critter, sfx, 0);

            animationRegisterAnimate(critter, ANIM_PUT_AWAY, 0);

            const FrmId frmId = FrmId(critter, ANIM_STAND, WeaponAnimation::None, critter->rotation + 1);
            animationRegisterSetFrmId(critter, frmId, -1);

            return reg_anim_end();
        }

        const FrmId frmId = FrmId(critter, ANIM_STAND, WeaponAnimation::None, critter->rotation + 1);
        _dude_stand(critter, critter->rotation, frmId);
    }

    return 0;
}

// 0x472B54






// Displays item description.
//
// inven_display_msg
// 0x472D24


// Examines inventory item.
//
// 0x472EB8


// 0x47304C


// 0x473904


// 0x4746A0


// 0x474708
// note: this is looting and stealing, not the inventory screen


// TODO: move barter related code to separate file

// Calculates value of NPC/barterer (request) and player (offer) tables.
//
// 0x474B2C barter_compute_value


// Calculates weight of NPC (request) and player (offer) tables. Used when bartering with party members.


// 0x474C50 barter_attempt_transaction




// clicked on an inventory in preparation of dragging or immediate transfer


// 0x474DAC barter_move_inventory


// 0x475070 barter_move_from_table_inventory


// 0x475334 display_table_inventories




// 0x4757F0 barter_inventory


// 0x47620C


// 0x476394


// Drop item inside a container item (bag, backpack, etc.).
// 0x476464




// 0x47650C drop ammo into weapon


// 0x47664C draw_amount


// 0x47688C do_move_timer


// Creates move items/set timer interface.
//
// 0x476AB8 setup_move_timer_win


// 0x477030 exit_move_timer_win


// 0x477074 inven_set_timer














void invenRederiveWeaponFid(Object* critter)
{
    if (critter == nullptr || FID_TYPE(critter->fid) != OBJ_TYPE_CRITTER) {
        return;
    }

    Object* leftItem = critterGetItem1(critter);
    Object* rightItem = critterGetItem2(critter);
    bool leftIsWeapon = leftItem != nullptr && itemGetType(leftItem) == ITEM_TYPE_WEAPON;
    bool rightIsWeapon = rightItem != nullptr && itemGetType(rightItem) == ITEM_TYPE_WEAPON;

    int weaponAnimationCode;
    if (!leftIsWeapon && !rightIsWeapon) {
        weaponAnimationCode = 0;
    } else if (leftIsWeapon != rightIsWeapon) {
        weaponAnimationCode = weaponGetAnimationCode(leftIsWeapon ? leftItem : rightItem);
    } else {
        int currentCode = (critter->fid & 0xF000) >> 12;
        int leftCode = weaponGetAnimationCode(leftItem);
        int rightCode = weaponGetAnimationCode(rightItem);
        if (currentCode == leftCode || currentCode == rightCode) {
            return;
        }
        if (serverDedicatedActive() && playerActorIs(critter)) {
            weaponAnimationCode = serverActorActiveHand(playerActorSlotOf(critter)) == HAND_LEFT
                ? leftCode
                : rightCode;
        } else {
            return;
        }
    }

    if (((critter->fid & 0xF000) >> 12) == weaponAnimationCode) {
        return;
    }

    // objectSetFrame because objectSetFid does NOT reset obj->frame, and a frame index past
    // the new art's count renders NOTHING ([[frame-index-render-gotcha]]).
    int fid = buildFid(OBJ_TYPE_CRITTER, critter->fid & 0xFFF, ANIM_STAND, weaponAnimationCode,
        (critter->fid & 0x70000000) >> 28);
    _dude_stand(critter, critter->rotation, fid);
    objectSetFrame(critter, 0, nullptr);
}

} // namespace fallout
