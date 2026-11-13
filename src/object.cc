#include "object_render.h"
#include "presenter.h"
#include "server_players.h"
#include "server_loop.h"
#include "client_net.h" // clientViewerActive — combat-mirror guards (COMBAT_CLIENT_DESIGN.md §5.1)
#include "geometry.h"
#include "object.h"

#include <assert.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <vector>

#include "animation.h"
#include "animation_defs.h"
#include "art.h"
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
#include "critter.h"
#include "debug.h"
#include "draw.h"
#include "game.h"
#include "game_mouse.h"
#include "item.h"
#include "light.h"
#include "map.h"
#include "map_defs.h"
#include "memory.h"
#include "party_member.h"
#include "proto.h"
#include "proto_instance.h"
#include "scripts.h"
#include "settings.h"
#include "svga.h"
#include "text_object.h"
#include "tile.h"
#include "worldmap.h"

namespace fallout {

static int gNextNetId = 1;

int objectReattach(Object* obj, int tile, MapElevation elevation);

Object* objectFindByNetId(int netId);

int _obj_save_player_actor(File* stream, Object* actor);

void objectApplyWireFlags(Object* obj, unsigned int wireFlags);

int _obj_load_player_actor(File* stream, Object** actorPtr);

int objectNextNetId();

int objectGetNextNetId();

void objectSetNextNetId(int netId);

static void objectAssignInventoryNetIds(Object* owner);

static void objectZeroInventoryNetIds(Object* owner);

void objectAssignAllNetIds();


static int objectLoadAllInternal(File* stream);
static void _object_fix_weapon_ammo(Object* obj);
static int objectWrite(Object* obj, File* stream);
static int _obj_offset_table_init();
static void _obj_offset_table_exit();
static int _obj_order_table_init();
static int _obj_order_comp_func_even(const void* a1, const void* a2);
static int _obj_order_comp_func_odd(const void* a1, const void* a2);
static void _obj_order_table_exit();


static void _obj_light_table_init();


static int _obj_save_obj(File* stream, Object* object);
static int _obj_load_obj(File* stream, Object** objectPtr, MapElevation elevation, Object* owner);
static int objectAllocate(Object** objectPtr);
static void objectDeallocate(Object** objectPtr);
static int objectListNodeCreate(ObjectListNode** nodePtr);
static void objectListNodeDestroy(ObjectListNode** nodePtr);
static int objectGetListNode(Object* obj, ObjectListNode** out_node, ObjectListNode** out_prev_node);
static void _obj_insert(ObjectListNode* ptr);
static int _obj_remove(ObjectListNode* a1, ObjectListNode* a2);
static int _obj_connect_to_tile(ObjectListNode* node, int tile_index, MapElevation elev, Rect* rect);
static int _obj_adjust_light(Object* obj, int a2, Rect* rect);


static int _obj_preload_sort(const void* fid1, const void* fid2);
static Object* objectPrepareWhoHitMeForSave(CritterCombatData* combatData);

// 0x5195F8 objInitialized
bool gObjectsInitialized = false;

// 0x5195FC updateHexWidth
static int gObjectsUpdateAreaHexWidth = 0;

// 0x519600 updateHexHeight
static int gObjectsUpdateAreaHexHeight = 0;

// 0x519604 updateHexArea
int gObjectsUpdateAreaHexSize = 0;

// 0x519608 orderTable
int* _orderTable[2] = {
    nullptr,
    nullptr,
};

// 0x519610 offsetTable
int* _offsetTable[2] = {
    nullptr,
    nullptr,
};

// 0x519618 offsetDivTable
int* _offsetDivTable = nullptr;

// 0x51961C offsetModTable
int* _offsetModTable = nullptr;

// 0x519620 renderTable


// Contains objects that are not bounded to tiles.
//
// 0x519628 floatingObjects
ObjectListNode* gObjectListHead = nullptr;

// 0x51962C centerToUpperLeft
static int _centerToUpperLeft = 0;

// 0x519630 find_elev
static MapElevation gObjectFindElevation = ELEVATION_FIRST;

// 0x519634 find_tile
static int gObjectFindTile = 0;

// 0x519638 find_ptr
static ObjectListNode* gObjectFindLastObjectListNode = nullptr;

// 0x51963C preload_list
static int* gObjectFids = nullptr;

// 0x519640 preload_list_index
static int gObjectFidsLength = 0;

// 0x51964C light_rect
static Rect _light_rect[9] = {
    { 0, 0, 96, 42 },
    { 0, 0, 160, 74 },
    { 0, 0, 224, 106 },
    { 0, 0, 288, 138 },
    { 0, 0, 352, 170 },
    { 0, 0, 416, 202 },
    { 0, 0, 480, 234 },
    { 0, 0, 544, 266 },
    { 0, 0, 608, 298 },
};

// 0x5196DC light_distance
static int _light_distance[36] = {
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    3,
    4,
    5,
    6,
    7,
    8,
    4,
    5,
    6,
    7,
    8,
    5,
    6,
    7,
    8,
    6,
    7,
    8,
    7,
    8,
    8,
};

// 0x51976C fix_violence_level
static ViolenceLevel gViolenceLevel = VIOLENCE_LEVEL_INVALID;

// 0x519770 obj_last_roof_x
static int _obj_last_roof_x = -1;

// 0x519774 obj_last_roof_y
static int _obj_last_roof_y = -1;

// 0x519778 obj_last_elev
static MapElevation _obj_last_elev = ELEVATION_INVALID;

// 0x51977C obj_last_is_empty
static bool _obj_last_is_empty = true;

// 0x519780 wallBlendTable


// 0x519784 glassBlendTable


// 0x519788 steamBlendTable


// 0x51978C energyBlendTable


// 0x519790 redBlendTable


// 0x519794 moveBlockObj
Object* _moveBlockObj = nullptr;

// 0x519798 objItemOutlineState
static int _objItemOutlineState = 0;

// 0x51979C cd_order
static int _cd_order[9] = {
    1,
    0,
    3,
    5,
    4,
    2,
    0,
    0,
    0,
};

static Object* objectPrepareWhoHitMeForSave(CritterCombatData* combatData)
{
    Object* whoHitMe = combatData->whoHitMe;
    if (whoHitMe == (Object*)-1) {
        whoHitMe = nullptr;
    }

    // Match sfall: only preserve whoHitMe in active combat saves.
    if (!isInCombat() || combatData->maneuver == CRITTER_MANEUVER_NONE) {
        combatData->whoHitMeCid = -1;
        return whoHitMe;
    }

    // NOTE: We only clear the cid for non-savable objects to prevent stale
    // references in the save file. We must NOT nullify `whoHitMe` itself,
    // otherwise the current combat AI logic will break.
    combatData->whoHitMeCid = objectIsSavable(whoHitMe) ? whoHitMe->cid : -1;
    return whoHitMe;
}

// 0x6391D0 light_blocked
static int _light_blocked[ROTATION_COUNT][36];

// 0x639530 light_offsets
static int _light_offsets[2][ROTATION_COUNT][36];

// 0x639BF0 buf_rect
Rect gObjectsWindowRect;

// Likely outlined objects on the screen.


// 0x639D90 updateAreaPixelBounds
static Rect gObjectsUpdateAreaPixelBounds;

// Contains objects that are bounded to tiles.
//
// 0x639DA0 objectTable
ObjectListNode* gObjectListHeadByTile[HEX_GRID_SIZE];

// 0x660EA0 glassGrayTable


// 0x660FA0 commonGrayTable


// 0x6610A0 buf_size
int gObjectsWindowBufferSize;

// 0x6610A4 back_buf
unsigned char* gObjectsWindowBuffer;

// 0x6610A8 buf_length
static int gObjectsWindowHeight;

// Translucent "egg" effect around player.
//
// 0x6610AC obj_egg
Object* gEgg;

// 0x6610B0 back_buf_width
int gObjectsWindowPitch;

// 0x6610B4 buf_width
static int gObjectsWindowWidth;

// obj_dude
// 0x6610B8 obj_dude
Object* gDude;

// 0x6610BC obj_seen_check
static char _obj_seen_check[5001];

// 0x662445 obj_seen
static char _obj_seen[5001];

// obj_init
// 0x488780 obj_init
int objectsInit(unsigned char* buf, int width, int height, int pitch)
{
    const FrmId dudeFrmId = FrmId(_art_vault_guy_num, ANIM_STAND, WeaponAnimation::None, ROTATION_NE);
    const InterfaceFrmId eggFrmId = InterfaceFrameId::Egg;

    memset(_obj_seen, 0, 5001);
    gObjectsUpdateAreaPixelBounds.right = width + 320;
    gObjectsUpdateAreaPixelBounds.left = -320;
    gObjectsUpdateAreaPixelBounds.bottom = height + 240;
    gObjectsUpdateAreaPixelBounds.top = -240;

    gObjectsUpdateAreaHexWidth = (gObjectsUpdateAreaPixelBounds.right + 320 + 1) / 32 + 1;
    gObjectsUpdateAreaHexHeight = (gObjectsUpdateAreaPixelBounds.bottom + 240 + 1) / 12 + 1;
    gObjectsUpdateAreaHexSize = gObjectsUpdateAreaHexWidth * gObjectsUpdateAreaHexHeight;

    memset(gObjectListHeadByTile, 0, sizeof(gObjectListHeadByTile));

    if (_obj_offset_table_init() == -1) {
        return -1;
    }

    if (_obj_order_table_init() == -1) {
        goto err;
    }

    if (_obj_render_table_init() == -1) {
        goto err_2;
    }

    if (lightInit() == -1) {
        goto err_2;
    }

    if (textObjectsInit(buf, width, height) == -1) {
        goto err_2;
    }

    _obj_light_table_init();
    _obj_blend_table_init();

    _centerToUpperLeft = tileFromScreenXY(gObjectsUpdateAreaPixelBounds.left, gObjectsUpdateAreaPixelBounds.top) - gCenterTile;
    gObjectsWindowWidth = width;
    gObjectsWindowHeight = height;
    gObjectsWindowBuffer = buf;

    gObjectsWindowRect.left = 0;
    gObjectsWindowRect.top = 0;
    gObjectsWindowRect.right = width - 1;
    gObjectsWindowRect.bottom = height - 1;

    gObjectsWindowBufferSize = height * width;
    gObjectsWindowPitch = pitch;

    objectCreateWithFrmIdProtoId(&gDude, dudeFrmId, CritterProtoTypeId::Dude);

    gDude->flags |= OBJECT_NO_REMOVE;
    gDude->flags |= OBJECT_NO_SAVE;
    gDude->flags |= OBJECT_HIDDEN;
    gDude->flags |= OBJECT_LIGHT_THRU;
    objectSetLight(gDude, 4, 0x10000, nullptr);

    if (partyMemberAdd(gDude) == -1) {
        debugPrint("\n  Error: Can't add Player into party!");
        exit(1);
    }

    objectCreateWithFrmIdProtoId(&gEgg, eggFrmId, ProtoId::Empty());
    gEgg->flags |= OBJECT_NO_REMOVE;
    gEgg->flags |= OBJECT_NO_SAVE;
    gEgg->flags |= OBJECT_HIDDEN;
    gEgg->flags |= OBJECT_LIGHT_THRU;

    gObjectsInitialized = true;

    return 0;

err_2:

    // NOTE: Uninline.
    _obj_order_table_exit();

err:

    _obj_offset_table_exit();

    return -1;
}

// 0x488A00 obj_reset
void objectsReset()
{
    if (gObjectsInitialized) {
        textObjectsReset();
        _obj_remove_all();
        memset(_obj_seen, 0, 5001);
        lightReset();
    }
}

// 0x488A30 obj_exit
void objectsExit()
{
    if (gObjectsInitialized) {
        gDude->flags &= ~OBJECT_NO_REMOVE;
        gEgg->flags &= ~OBJECT_NO_REMOVE;

        _obj_remove_all();
        textObjectsFree();

        // NOTE: Uninline.
        _obj_blend_table_exit();

        lightExit();

        // NOTE: Uninline.
        _obj_render_table_exit();

        // NOTE: Uninline.
        _obj_order_table_exit();

        _obj_offset_table_exit();
    }
}

// 0x488AF4 obj_read_obj
int objectRead(Object* obj, File* stream)
{
    int field_74;

    if (fileReadInt32(stream, &(obj->id)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->tile)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->x)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->y)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->sx)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->sy)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->frame)) == -1) return -1;
    if (fileReadInt32Enum<Rotation>(stream, &(obj->rotation)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->fid)) == -1) return -1;
    if (fileReadUInt32Enum<ObjectFlags>(stream, &(obj->flags)) == -1) return -1;
    if (fileReadInt32Enum<MapElevation>(stream, &(obj->elevation)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->pid)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->cid)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->lightDistance)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->lightIntensity)) == -1) return -1;
    if (fileReadInt32(stream, &field_74) == -1) return -1;
    if (fileReadInt32(stream, &(obj->sid)) == -1) return -1;
    if (fileReadInt32(stream, &(obj->scriptIndex)) == -1) return -1;

    obj->outline = OUTLINE_TYPE_NONE;
    obj->owner = nullptr;

    if (objectDataRead(obj, stream) != 0) {
        return -1;
    }

    // whoHitMe is a runtime-only CACHE of the persistent whoHitMeCid; a serialized
    // pointer is always garbage across a load (a foreign process address, or — the
    // fatal case — a legacy 4-byte value sitting in the 8-byte field on 64-bit, e.g.
    // -1 -> 0xffffffff, a non-null bogus pointer). The many nullptr-only-guarded deref
    // sites (combat.cc:5453 _damage_object, state_dump.cc:98, actions.cc:2449) fault on
    // it. Null it on EVERY deserialization — objectRead is the single chokepoint for map
    // load, savegame, join blob AND _obj_copy (which round-trips through here) — so no
    // path can surface a bogus whoHitMe. This SUBSUMES _map_fix_critter_combat_data
    // (which only covered critters present at map-load time — artemple's script-spawned
    // Temple guards loaded LATER escaped it → the cdamage SIGSEGV). whoHitMeCid is
    // untouched; combat re-resolves the pointer via _find_cid at _combat_begin. See the
    // id-plus-cached-pointer antipattern (do NOT 32-bit-mask the deref — fix the source).
    if (ProtoId(obj).objectType() == OBJ_TYPE_CRITTER) {
        obj->data.critter.combat.whoHitMe = nullptr;
    }

    if (isExitGridProtoId(obj)) {
        if (obj->data.misc.map <= 0) {
            constexpr int kExit2Grid1FrameId = MiscFrmId(MiscFrameId::Exit2Grid1).frameId();
            constexpr int kExit3Grid8FrameId = MiscFrmId(MiscFrameId::Exit3Grid8).frameId();
            constexpr int kExitGridCount = kExit3Grid8FrameId - kExit2Grid1FrameId + 1;
            const FrmId frmId = FrmId(obj);
            if (frmId.valid() && frmId.frameId() < kExit2Grid1FrameId) {
                obj->fid = MiscFrmId(static_cast<MiscFrameId>(frmId.frameId() + kExitGridCount), frmId.animationType()).fid();
            }
        }
    } else {
        if (ProtoId(obj).objectType() == OBJ_TYPE_ITEM) {
            if (!(gMapHeader.flags & MAP_HEADER_SAVED)) {
                _object_fix_weapon_ammo(obj);
            }

            if (itemGetType(obj) == ITEM_TYPE_WEAPON
                && obj->data.item.weapon.ammoQuantity < 0) {
                obj->data.item.weapon.ammoQuantity = 0;
            }
        }
    }

    return 0;
}

bool objectIsSavable(Object* obj)
{
    if (obj == nullptr) return false;
    if (obj == gDude) return true;
    if (objectIsPartyMember(obj)) return true;

    return (obj->flags & OBJECT_NO_SAVE) == OBJECT_NONE;
}

// 0x488CE4 obj_load
int objectLoadAll(File* stream)
{
    int rc = objectLoadAllInternal(stream);

    gViolenceLevel = VIOLENCE_LEVEL_INVALID;

    return rc;
}

// 0x488CF8 obj_load_func
static int objectLoadAllInternal(File* stream)
{
    if (stream == nullptr) {
        return -1;
    }

    bool fixMapInventory = settings.mapper.fix_map_inventory;

    gViolenceLevel = settings.preferences.violence_level;

    int objectCount;
    if (fileReadInt32(stream, &objectCount) == -1) {
        return -1;
    }

    if (objectCount < 0
        || static_cast<size_t>(objectCount) > std::numeric_limits<size_t>::max() / sizeof(*gObjectFids)) {
        debugPrint("\nError loading objects: invalid object count %d.\n", objectCount);
        return -1;
    }

    if (gObjectFids != nullptr) {
        internal_free(gObjectFids);
        gObjectFids = nullptr;
    }
    gObjectFidsLength = 0;

    if (objectCount != 0) {
        gObjectFids = (int*)internal_malloc(sizeof(*gObjectFids) * objectCount);
        if (gObjectFids == nullptr) {
            return -1;
        }
        memset(gObjectFids, 0, sizeof(*gObjectFids) * objectCount);
    }

    for (MapElevation elevation = ELEVATION_FIRST; elevation < ELEVATION_COUNT; elevation++) {
        int objectCountAtElevation;
        if (fileReadInt32(stream, &objectCountAtElevation) == -1) {
            return -1;
        }

        if (objectCountAtElevation < 0 || objectCountAtElevation > objectCount - gObjectFidsLength) {
            debugPrint("\nError loading objects: invalid elevation object count %d.\n", objectCountAtElevation);
            return -1;
        }

        for (int objectIndex = 0; objectIndex < objectCountAtElevation; objectIndex++) {
            ObjectListNode* objectListNode;

            // NOTE: Uninline.
            if (objectListNodeCreate(&objectListNode) == -1) {
                return -1;
            }

            if (objectAllocate(&(objectListNode->obj)) == -1) {
                // NOTE: Uninline.
                objectListNodeDestroy(&objectListNode);
                return -1;
            }

            if (objectRead(objectListNode->obj, stream) != 0) {
                // NOTE: Uninline.
                objectDeallocate(&(objectListNode->obj));

                // NOTE: Uninline.
                objectListNodeDestroy(&objectListNode);

                return -1;
            }

            objectListNode->obj->outline = OUTLINE_TYPE_NONE;
            gObjectFids[gObjectFidsLength++] = objectListNode->obj->fid;

            if (objectListNode->obj->sid != -1) {
                Script* script;
                if (scriptGetScript(objectListNode->obj->sid, &script) == -1) {
                    objectListNode->obj->sid = -1;
                    debugPrint("\nError connecting object to script!");
                } else {
                    script->owner = objectListNode->obj;
                    objectListNode->obj->scriptIndex = script->index;
                }
            }

            _obj_fix_violence_settings(&(objectListNode->obj->fid));
            objectListNode->obj->elevation = elevation;

            _obj_insert(objectListNode);

            if ((objectListNode->obj->flags & OBJECT_NO_REMOVE) && ProtoId(objectListNode->obj).objectType() == OBJ_TYPE_CRITTER && objectListNode->obj->pid != 18000) {
                objectListNode->obj->flags &= ~OBJECT_NO_REMOVE;
            }

            Inventory* inventory = &(objectListNode->obj->data.inventory);
            if (inventory->length != 0) {
                // length/capacity come straight from the object stream, which on a
                // rebaseline is a wire/blob-loaded trust boundary. A corrupt or
                // garbage count (observed: capacity ~0xfffffffe on a co-op map-switch
                // rebaseline) otherwise drives an impossible malloc — ASAN aborts with
                // allocation-size-too-big, a stock build crashes or wildly over-
                // allocates — and a length past capacity overruns the item array
                // below. Reject the object rather than trust it. (The corrupt count's
                // upstream cause is the applyBlob teardown/parse corruption; this is
                // the boundary backstop so a bad blob fails the load instead of
                // crashing.)
                if (inventory->length < 0
                    || inventory->capacity < inventory->length
                    || (size_t)(unsigned int)inventory->capacity > SIZE_MAX / sizeof(InventoryItem)) {
                    // Loud, not silent: name the object so a repro points AT the
                    // root (which object's serialization desynced the stream) instead
                    // of the reject hiding it. If pid/fid also look like garbage, the
                    // whole object struct is corrupt → a stream mis-alignment earlier
                    // in objectLoadAll; if they look valid, this specific object's
                    // inventory write/read disagrees.
                    debugPrint("Error loading inventory: bad length=%d capacity=%d "
                               "obj pid=0x%X fid=0x%X — corrupt blob or stream desync\n",
                        inventory->length, inventory->capacity,
                        objectListNode->obj->pid, objectListNode->obj->fid);
                    return -1;
                }
                inventory->items = (InventoryItem*)internal_malloc(sizeof(InventoryItem) * inventory->capacity);
                if (inventory->items == nullptr) {
                    return -1;
                }

                for (int inventoryItemIndex = 0; inventoryItemIndex < inventory->length; inventoryItemIndex++) {
                    InventoryItem* inventoryItem = &(inventory->items[inventoryItemIndex]);
                    if (fileReadInt32(stream, &(inventoryItem->quantity)) != 0) {
                        debugPrint("Error loading inventory\n");
                        return -1;
                    }

                    if (fixMapInventory) {
                        inventoryItem->item = (Object*)internal_malloc(sizeof(Object));
                        if (inventoryItem->item == nullptr) {
                            debugPrint("Error loading inventory\n");
                            return -1;
                        }

                        if (objectRead(inventoryItem->item, stream) != 0) {
                            debugPrint("Error loading inventory\n");
                            return -1;
                        }
                    } else {
                        if (_obj_load_obj(stream, &(inventoryItem->item), elevation, objectListNode->obj) == -1) {
                            return -1;
                        }
                    }
                }
            } else {
                inventory->capacity = 0;
                inventory->items = nullptr;
            }
        }
    }

    if (gObjectFidsLength != objectCount) {
        debugPrint("\nError loading objects: expected %d objects, read %d.\n", objectCount, gObjectFidsLength);
        return -1;
    }

    _obj_rebuild_all_light();

    return 0;
}

// Fixes ammo pid and number of charges.
//
// 0x48911C object_fix_weapon_ammo
static void _object_fix_weapon_ammo(Object* obj)
{
    const ProtoId protoId = obj;
    if (protoId.objectType() != OBJ_TYPE_ITEM) {
        return;
    }

    Proto* proto;
    if (protoGetProto(protoId, &proto) == -1) {
        debugPrint("\nError: obj_load: proto_ptr failed on pid");
        exit(1);
    }

    int charges;
    if (itemGetType(obj) == ITEM_TYPE_WEAPON) {
        int ammoTypePid = obj->data.item.weapon.ammoTypePid;
        if (ammoTypePid == 0xCCCCCCCC || ammoTypePid == ProtoId::kEmptyPid) {
            obj->data.item.weapon.ammoTypePid = proto->item.data.weapon.ammoTypePid;
        }

        charges = obj->data.item.weapon.ammoQuantity;
        if (charges == 0xCCCCCCCC || charges == -1 || charges != proto->item.data.weapon.ammoCapacity) {
            obj->data.item.weapon.ammoQuantity = proto->item.data.weapon.ammoCapacity;
        }
    } else {
        if (ProtoId(obj).objectType() == OBJ_TYPE_MISC) {
            // FIXME: looks like this code in unreachable
            charges = obj->data.item.misc.charges;
            if (charges == 0xCCCCCCCC) {
                charges = proto->item.data.misc.charges;
                obj->data.item.misc.charges = charges;
                if (charges == 0xCCCCCCCC) {
                    debugPrint("\nError: Misc Item Prototype %s: charges incorrect!", protoGetName(obj));
                    obj->data.item.misc.charges = 0;
                }
            } else {
                if (charges != proto->item.data.misc.charges) {
                    obj->data.item.misc.charges = proto->item.data.misc.charges;
                }
            }
        }
    }
}

// 0x489200 obj_write_obj
static int objectWrite(Object* obj, File* stream)
{
    if (fileWriteInt32(stream, obj->id) == -1) return -1;
    if (fileWriteInt32(stream, obj->tile) == -1) return -1;
    if (fileWriteInt32(stream, obj->x) == -1) return -1;
    if (fileWriteInt32(stream, obj->y) == -1) return -1;
    if (fileWriteInt32(stream, obj->sx) == -1) return -1;
    if (fileWriteInt32(stream, obj->sy) == -1) return -1;
    if (fileWriteInt32(stream, obj->frame) == -1) return -1;
    if (fileWriteInt32Enum<Rotation>(stream, obj->rotation) == -1) return -1;
    if (fileWriteInt32(stream, obj->fid) == -1) return -1;
    if (fileWriteUInt32Enum<ObjectFlags>(stream, obj->flags) == -1) return -1;
    if (fileWriteInt32Enum<MapElevation>(stream, obj->elevation) == -1) return -1;
    if (fileWriteInt32(stream, obj->pid) == -1) return -1;
    if (fileWriteInt32(stream, obj->cid) == -1) return -1;
    if (fileWriteInt32(stream, obj->lightDistance) == -1) return -1;
    if (fileWriteInt32(stream, obj->lightIntensity) == -1) return -1;
    if (fileWriteInt32Enum<OutlineType>(stream, obj->outline) == -1) return -1;
    if (fileWriteInt32(stream, obj->sid) == -1) return -1;
    if (fileWriteInt32(stream, obj->scriptIndex) == -1) return -1;
    if (objectDataWrite(obj, stream) == -1) return -1;

    return 0;
}

// 0x48935C obj_save
int objectSaveAll(File* stream)
{
    if (stream == nullptr) {
        return -1;
    }

    _obj_process_seen();

    int objectCount = 0;

    long objectCountPos = fileTell(stream);
    if (fileWriteInt32(stream, objectCount) == -1) {
        return -1;
    }

    for (MapElevation elevation = ELEVATION_FIRST; elevation < ELEVATION_COUNT; elevation++) {
        int objectCountAtElevation = 0;

        long objectCountAtElevationPos = fileTell(stream);
        if (fileWriteInt32(stream, objectCountAtElevation) == -1) {
            return -1;
        }

        for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
            for (ObjectListNode* objectListNode = gObjectListHeadByTile[tile]; objectListNode != nullptr; objectListNode = objectListNode->next) {
                Object* object = objectListNode->obj;
                if (object->elevation != elevation) {
                    continue;
                }

                if ((object->flags & OBJECT_NO_SAVE) != OBJECT_NONE) {
                    continue;
                }

                CritterCombatData* combatData = nullptr;
                Object* whoHitMe = nullptr;
                if (ProtoId(object).objectType() == OBJ_TYPE_CRITTER) {
                    combatData = &(object->data.critter.combat);
                    whoHitMe = objectPrepareWhoHitMeForSave(combatData);
                }

                if (objectWrite(object, stream) == -1) {
                    return -1;
                }

                if (ProtoId(object).objectType() == OBJ_TYPE_CRITTER) {
                    combatData->whoHitMe = whoHitMe;
                }

                Inventory* inventory = &(object->data.inventory);
                for (int index = 0; index < inventory->length; index++) {
                    InventoryItem* inventoryItem = &(inventory->items[index]);

                    if (fileWriteInt32(stream, inventoryItem->quantity) == -1) {
                        return -1;
                    }

                    if (_obj_save_obj(stream, inventoryItem->item) == -1) {
                        return -1;
                    }
                }

                objectCountAtElevation++;
            }
        }

        long pos = fileTell(stream);
        fileSeek(stream, objectCountAtElevationPos, SEEK_SET);
        fileWriteInt32(stream, objectCountAtElevation);
        fileSeek(stream, pos, SEEK_SET);

        objectCount += objectCountAtElevation;
    }

    long pos = fileTell(stream);
    fileSeek(stream, objectCountPos, SEEK_SET);
    fileWriteInt32(stream, objectCount);
    fileSeek(stream, pos, SEEK_SET);

    return 0;
}

// 0x489550 obj_render_pre_roof


// 0x4897EC obj_render_post_roof


// 0x489A84 obj_new
int objectCreateWithFrmIdProtoId(Object** objectPtr, const FrmId& frmId, const ProtoId& protoId)
{
    ObjectListNode* objectListNode;

    // NOTE: Uninline;
    if (objectListNodeCreate(&objectListNode) == -1) {
        return -1;
    }

    if (objectAllocate(&(objectListNode->obj)) == -1) {
        // Uninline.
        objectListNodeDestroy(&objectListNode);
        return -1;
    }

    if (frmId.valid()) {
        assert(frmId.hasFid() && "objectCreateWithFrmIdProtoId(Object** objectPtr, const FrmId& frmId, const ProtoId& protoId) called with path based FrmId which is not supported!");
    }

    objectListNode->obj->fid = frmId.fid();
    _obj_insert(objectListNode);

    if (objectPtr) {
        *objectPtr = objectListNode->obj;
    }

    objectListNode->obj->pid = protoId.pid();
    objectListNode->obj->id = scriptsNewObjectId();

    if (!protoId.valid() || protoId.objectType() == OBJ_TYPE_TILE) {
        Inventory* inventory = &(objectListNode->obj->data.inventory);
        inventory->length = 0;
        inventory->items = nullptr;
        return 0;
    }

    _proto_update_init(objectListNode->obj);

    Proto* proto = nullptr;
    if (protoGetProto(protoId, &proto) == -1) {
        return 0;
    }

    objectSetLight(objectListNode->obj, proto->lightDistance, proto->lightIntensity, nullptr);

    if ((proto->flags & PROTO_FLAG_FLAT) != PROTO_FLAG_NONE) {
        _obj_toggle_flat(objectListNode->obj, nullptr);
    }

    if ((proto->flags & PROTO_FLAG_NO_BLOCK) != PROTO_FLAG_NONE) {
        objectListNode->obj->flags |= OBJECT_NO_BLOCK;
    }

    if ((proto->flags & PROTO_FLAG_MULTIHEX) != PROTO_FLAG_NONE) {
        objectListNode->obj->flags |= OBJECT_MULTIHEX;
    }

    if ((proto->flags & PROTO_FLAG_TRANS_NONE) != PROTO_FLAG_NONE) {
        objectListNode->obj->flags |= OBJECT_TRANS_NONE;
    } else {
        if ((proto->flags & PROTO_FLAG_TRANS_WALL) != PROTO_FLAG_NONE) {
            objectListNode->obj->flags |= OBJECT_TRANS_WALL;
        } else if ((proto->flags & PROTO_FLAG_TRANS_GLASS) != PROTO_FLAG_NONE) {
            objectListNode->obj->flags |= OBJECT_TRANS_GLASS;
        } else if ((proto->flags & PROTO_FLAG_TRANS_STEAM) != PROTO_FLAG_NONE) {
            objectListNode->obj->flags |= OBJECT_TRANS_STEAM;
        } else if ((proto->flags & PROTO_FLAG_TRANS_ENERGY) != PROTO_FLAG_NONE) {
            objectListNode->obj->flags |= OBJECT_TRANS_ENERGY;
        } else if ((proto->flags & PROTO_FLAG_TRANS_RED) != PROTO_FLAG_NONE) {
            objectListNode->obj->flags |= OBJECT_TRANS_RED;
        }
    }

    if ((proto->flags & PROTO_FLAG_LIGHT_THRU) != PROTO_FLAG_NONE) {
        objectListNode->obj->flags |= OBJECT_LIGHT_THRU;
    }

    if ((proto->flags & PROTO_FLAG_SHOOT_THRU) != PROTO_FLAG_NONE) {
        objectListNode->obj->flags |= OBJECT_SHOOT_THRU;
    }

    if ((proto->flags & PROTO_FLAG_WALL_TRANS_END) != PROTO_FLAG_NONE) {
        objectListNode->obj->flags |= OBJECT_WALL_TRANS_END;
    }

    if ((proto->flags & PROTO_FLAG_NO_HIGHLIGHT) != PROTO_FLAG_NONE) {
        objectListNode->obj->flags |= OBJECT_NO_HIGHLIGHT;
    }

    objectSetScriptFromProto(objectListNode->obj, &(objectListNode->obj->sid));

    return 0;
}

// 0x489C9C obj_pid_new
int objectCreateWithProtoId(Object** objectPtr, const ProtoId& protoId)
{
    Proto* proto;

    *objectPtr = nullptr;

    if (protoGetProto(protoId, &proto) == -1) {
        return -1;
    }

    return objectCreateWithFrmIdProtoId(objectPtr, proto, protoId);
}

// 0x489CCC obj_copy
int _obj_copy(Object** a1, Object* a2)
{
    if (a2 == nullptr) {
        return -1;
    }

    ObjectListNode* objectListNode;

    // NOTE: Uninline.
    if (objectListNodeCreate(&objectListNode) == -1) {
        return -1;
    }

    if (objectAllocate(&(objectListNode->obj)) == -1) {
        // NOTE: Uninline.
        objectListNodeDestroy(&objectListNode);
        return -1;
    }

    objectDataReset(objectListNode->obj);

    memcpy(objectListNode->obj, a2, sizeof(Object));

    if (a1 != nullptr) {
        *a1 = objectListNode->obj;
    }

    _obj_insert(objectListNode);

    objectListNode->obj->id = scriptsNewObjectId();

    if (serverLoopActive()) {
        objectListNode->obj->netId = objectNextNetId();
    }

    if (objectListNode->obj->sid != -1) {
        objectListNode->obj->sid = -1;
        objectSetScriptFromProto(objectListNode->obj, &(objectListNode->obj->sid));
    }

    if (objectSetRotation(objectListNode->obj, a2->rotation, nullptr) == -1) {
        // TODO: Probably leaking object allocated with objectAllocate.
        // NOTE: Uninline.
        objectListNodeDestroy(&objectListNode);
        return -1;
    }

    objectListNode->obj->flags &= ~OBJECT_QUEUED;

    Inventory* newInventory = &(objectListNode->obj->data.inventory);
    newInventory->length = 0;
    newInventory->capacity = 0;
    // The memcpy above copied the SOURCE's items POINTER as well. Clearing only
    // length/capacity leaves the copy aliasing the source's array, and itemAdd's
    // growth test (`length == capacity || items == nullptr`) then takes the
    // realloc branch on that borrowed pointer — reallocating the SOURCE's array
    // and adopting it. Both objects end up sharing one array while the source's
    // pointer may already be dangling: edits to one inventory show up in the
    // other, entries read back as garbage items, and teardown frees freed memory.
    // Latent in vanilla only because every other caller copies an object whose
    // inventory is empty AND nullptr; copying the DUDE (co-op extra actors, which
    // carry the starting inventory) is what first supplies a non-null pointer.
    newInventory->items = nullptr;

    Inventory* oldInventory = &(a2->data.inventory);
    for (int index = 0; index < oldInventory->length; index++) {
        InventoryItem* oldInventoryItem = &(oldInventory->items[index]);

        Object* newItem;
        if (_obj_copy(&newItem, oldInventoryItem->item) == -1) {
            // TODO: Probably leaking object allocated with objectAllocate.
            // NOTE: Uninline.
            objectListNodeDestroy(&objectListNode);
            return -1;
        }

        if (itemAdd(objectListNode->obj, newItem, oldInventoryItem->quantity) == 1) {
            // TODO: Probably leaking object allocated with objectAllocate.
            // NOTE: Uninline.
            objectListNodeDestroy(&objectListNode);
            return -1;
        }

        // The recursive _obj_copy above _obj_insert'ed this item into the WORLD
        // object list, and itemAdd has now also put it in an inventory — the item
        // is in two places at once. Carried items belong in neither the world list
        // nor a tile bucket (see command.cc's give, which _obj_disconnect's right
        // after objectCreateWithProtoId for exactly this reason).
        //
        // The damage is deferred and looks unrelated: map teardown walks the world
        // list and frees the item, but an owner that survives teardown keeps
        // pointing at it. Observed with co-op extra actors (OBJECT_NO_REMOVE, so
        // they DO survive): after a transition their carried spear read back as
        // pid 0x0500000C — a freed object reinterpreted — rendering as a garbage
        // item. Left in the world list it is also a double free waiting for any
        // owner that is torn down normally.
        _obj_disconnect(newItem, nullptr);
    }

    // MP_PROTOCOL.md §4 spawn: the second create path (duplication / stack
    // split). Recursion above already emitted objectCreated for each copied
    // inventory item, so children precede their parent on the wire.
    presenter()->objectCreated(objectListNode->obj);

    return 0;
}

// 0x489EC4 obj_connect
int _obj_connect(Object* object, int tile, MapElevation elevation, Rect* rect)
{
    if (object == nullptr) {
        return -1;
    }

    if (!hexGridTileIsValid(tile)) {
        return -1;
    }

    if (!elevationIsValid(elevation)) {
        return -1;
    }

    ObjectListNode* objectListNode;

    // NOTE: Uninline.
    if (objectListNodeCreate(&objectListNode) == -1) {
        return -1;
    }

    objectListNode->obj = object;

    int rc = _obj_connect_to_tile(objectListNode, tile, elevation, rect);
    if (rc == 0) {
        // MP_PROTOCOL.md §4/§6.2b item<->world lifecycle: the object attached to a
        // world tile (drop/scatter/unload/script obj_connect). This bypasses
        // objectCreateWithFidPid and objectSetLocation, so it is the sole signal an
        // item re-parented from an inventory now appears at a tile. Distinct from
        // objectCreated — the object persists, it is not born here.
        presenter()->objectConnected(object, tile, elevation);
    }
    return rc;
}

// 0x489F34 obj_disconnect
int _obj_disconnect(Object* obj, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    ObjectListNode* node;
    ObjectListNode* prev_node;
    if (objectGetListNode(obj, &node, &prev_node) != 0) {
        return -1;
    }

    if (_obj_adjust_light(obj, 1, rect) == -1) {
        if (rect != nullptr) {
            objectGetRect(obj, rect);
        }
    }

    if (prev_node != nullptr) {
        prev_node->next = node->next;
    } else {
        int tile = node->obj->tile;
        if (tile == -1) {
            gObjectListHead = gObjectListHead->next;
        } else {
            gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
        }
    }

    if (node != nullptr) {
        internal_free(node);
    }

    obj->tile = -1;

    // MP_PROTOCOL.md §4/§6.2b item<->world lifecycle: the object detached from its
    // world tile (pickup/consume/unload/script obj_disconnect) into an inventory or
    // limbo. Bypasses objectDestroy (the object persists), so this is the sole
    // signal the item left the world. Reached only on the success path; obj->id is
    // intact, obj->tile is now -1.
    presenter()->objectDisconnected(obj);

    return 0;
}

// 0x489FF8 obj_offset
int _obj_offset(Object* obj, int x, int y, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    ObjectListNode* node = nullptr;
    ObjectListNode* previousNode = nullptr;
    if (objectGetListNode(obj, &node, &previousNode) == -1) {
        return -1;
    }

    if (obj == gDude) {
        if (rect != nullptr) {
            Rect eggRect;
            objectGetRect(gEgg, &eggRect);
            rectCopy(rect, &eggRect);

            if (previousNode != nullptr) {
                previousNode->next = node->next;
            } else {
                int tile = node->obj->tile;
                if (tile == -1) {
                    gObjectListHead = gObjectListHead->next;
                } else {
                    gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
                }
            }

            obj->x += x;
            obj->sx += x;

            obj->y += y;
            obj->sy += y;

            _obj_insert(node);

            rectOffset(&eggRect, x, y);

            _obj_offset(gEgg, x, y, nullptr);
            rectUnion(rect, &eggRect, rect);
        } else {
            if (previousNode != nullptr) {
                previousNode->next = node->next;
            } else {
                int tile = node->obj->tile;
                if (tile == -1) {
                    gObjectListHead = gObjectListHead->next;
                } else {
                    gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
                }
            }

            obj->x += x;
            obj->sx += x;

            obj->y += y;
            obj->sy += y;

            _obj_insert(node);

            _obj_offset(gEgg, x, y, nullptr);
        }
    } else {
        if (rect != nullptr) {
            objectGetRect(obj, rect);

            if (previousNode != nullptr) {
                previousNode->next = node->next;
            } else {
                int tile = node->obj->tile;
                if (tile == -1) {
                    gObjectListHead = gObjectListHead->next;
                } else {
                    gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
                }
            }

            obj->x += x;
            obj->sx += x;

            obj->y += y;
            obj->sy += y;

            _obj_insert(node);

            Rect objectRect;
            rectCopy(&objectRect, rect);

            rectOffset(&objectRect, x, y);

            rectUnion(rect, &objectRect, rect);
        } else {
            if (previousNode != nullptr) {
                previousNode->next = node->next;
            } else {
                int tile = node->obj->tile;
                if (tile == -1) {
                    gObjectListHead = gObjectListHead->next;
                } else {
                    gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
                }
            }

            obj->x += x;
            obj->sx += x;

            obj->y += y;
            obj->sy += y;

            _obj_insert(node);
        }
    }

    return 0;
}

// 0x48A324 obj_move
int _obj_move(Object* a1, int a2, int a3, MapElevation elevation, Rect* a5)
{
    if (a1 == nullptr) {
        return -1;
    }

    // TODO: Get rid of initialization.
    ObjectListNode* node = nullptr;
    ObjectListNode* previousNode;
    int v22 = 0;

    int tile = a1->tile;
    if (hexGridTileIsValid(tile)) {
        if (objectGetListNode(a1, &node, &previousNode) == -1) {
            return -1;
        }

        if (_obj_adjust_light(a1, 1, a5) == -1) {
            if (a5 != nullptr) {
                objectGetRect(a1, a5);
            }
        }

        if (previousNode != nullptr) {
            previousNode->next = node->next;
        } else {
            int tile = node->obj->tile;
            if (tile == -1) {
                gObjectListHead = gObjectListHead->next;
            } else {
                gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
            }
        }

        a1->tile = -1;
        a1->elevation = elevation;
        v22 = 1;
    } else {
        if (elevation == a1->elevation) {
            if (a5 != nullptr) {
                objectGetRect(a1, a5);
            }
        } else {
            if (objectGetListNode(a1, &node, &previousNode) == -1) {
                return -1;
            }

            if (a5 != nullptr) {
                objectGetRect(a1, a5);
            }

            if (previousNode != nullptr) {
                previousNode->next = node->next;
            } else {
                int tile = node->obj->tile;
                if (tile != -1) {
                    gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
                } else {
                    gObjectListHead = gObjectListHead->next;
                }
            }

            a1->elevation = elevation;
            v22 = 1;
        }
    }

    CacheEntry* cacheHandle;
    int width;
    int height;
    Art* art = artLock(FrmId(a1), &cacheHandle);
    if (art != nullptr) {
        artGetSize(art, a1->frame, a1->rotation, &width, &height);
        a1->sx = a2 - width / 2;
        a1->sy = a3 - (height - 1);
        artUnlock(cacheHandle);
    }

    if (v22) {
        _obj_insert(node);
    }

    if (a5 != nullptr) {
        Rect rect;
        objectGetRect(a1, &rect);
        rectUnion(a5, &rect, a5);
    }

    if (a1 == gDude) {
        if (a1 != nullptr) {
            Rect rect;
            _obj_move(gEgg, a2, a3, elevation, &rect);
            rectUnion(a5, &rect, a5);
        } else {
            _obj_move(gEgg, a2, a3, elevation, nullptr);
        }
    }

    return 0;
}

// 0x48A568 obj_move_to_tile
int objectSetLocation(Object* obj, int tile, MapElevation elevation, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    if (!hexGridTileIsValid(tile)) {
        return -1;
    }

    if (!elevationIsValid(elevation)) {
        return -1;
    }

    ObjectListNode* node;
    ObjectListNode* prevNode;
    if (objectGetListNode(obj, &node, &prevNode) == -1) {
        return -1;
    }

    Rect v23;
    int v5 = _obj_adjust_light(obj, 1, rect);
    if (rect != nullptr) {
        if (v5 == -1) {
            objectGetRect(obj, rect);
        }

        rectCopy(&v23, rect);
    }

    int oldElevation = obj->elevation;
    int oldTile = obj->tile;
    if (prevNode != nullptr) {
        prevNode->next = node->next;
    } else {
        int tileIndex = node->obj->tile;
        if (tileIndex == -1) {
            gObjectListHead = gObjectListHead->next;
        } else {
            gObjectListHeadByTile[tileIndex] = gObjectListHeadByTile[tileIndex]->next;
        }
    }

    if (_obj_connect_to_tile(node, tile, elevation, rect) == -1) {
        return -1;
    }

    // MP_PROTOCOL.md §4 move: tile/elevation is now committed. The per-tile
    // stream is coalesced into a path event downstream; suppressed during map
    // load by the consumer (dude placement moves through here). Pixel-offset
    // interpolation (_obj_offset) is client-derived, not synced.
    //
    // During a script game-time skip the per-tile stream is COALESCED instead:
    // the window records this object's origin once and ships a single snap to
    // its final tile when the skip ends (presenter.h TIME-SKIP MOVE COALESCING).
    if (presenterTimeSkipActive()) {
        presenterTimeSkipRecordMove(obj, oldTile, oldElevation);
    } else {
        presenter()->objectMoved(obj, oldTile, oldElevation, tile, elevation);
    }

    if (isInCombat() && !clientViewerActive()) {
        if (FrmId(obj).objectType() == OBJ_TYPE_CRITTER) {
            bool enableOutline = obj->outline != OUTLINE_TYPE_NONE && (obj->outline & OUTLINE_DISABLED) == OUTLINE_TYPE_NONE;
            _combat_update_critter_outline_for_los(obj, enableOutline);
        }
    }

    if (rect != nullptr) {
        rectUnion(rect, &v23, rect);
    }

    if (obj == gDude) {
        // ►►►► A WIRE VIEWER MUST NOT DISCOVER ITS OWN EXIT GRIDS. Transitions belong
        // to the authority: the server walks its dude onto the grid, latches the
        // MapTransition, performs the load and rebaselines us. Our gDude is a MIRROR
        // being replayed, so when the replayed walk lands on the same tile this block
        // used to latch a SECOND, local transition — and mapSetTransition opens with
        //
        //     if (isInCombat()) { _game_user_wants_to_quit = 1; }
        //
        // which on a viewer is read off a MIRRORED combat state, not a local fight.
        // Vanilla is safe because that 1 is an in-band "break out of the combat loop"
        // signal that combat.cc itself clears again (combat.cc:3294/3643). The viewer
        // has no such loop to consume it, so the flag survived, and BOTH the worldmap
        // modal (worldmap_ui.cc `if (_game_user_wants_to_quit != 0) break;`) and the
        // frame loop (`while (_game_user_wants_to_quit == 0)`) then exited. The client
        // vanished the instant you walked off the edge of a map during a fight, which
        // reads exactly like a crash and leaves no core file, because it is a clean
        // return out of main.
        //
        // ►► THE SAME BUG, ONE BRANCH DOWN, WAS ALREADY FOUND AND FIXED: the elevation
        // change at the bottom of this function carries `&& !clientViewerActive()` with
        // a comment saying a wire-driven change "would silently kill mainClientViewer"
        // (COMBAT_CLIENT_DESIGN.md §5.1). That guard was put on the symptom that had
        // been noticed; this scan is the other door into the identical failure. Whenever
        // gDude is a mirror, ask of every `obj == gDude` branch whether it is OBSERVING
        // the dude or DECIDING for it — deciding is the authority's job.
        //
        // Nothing else in this scan is wanted on a viewer either: it looks for exit
        // grids and does nothing but latch the transition and mark the worldmap entrance
        // known, and that entrance state is server-owned too.
        ObjectListNode* objectListNode = clientViewerActive() ? nullptr : gObjectListHeadByTile[tile];
        while (objectListNode != nullptr) {
            Object* obj = objectListNode->obj;
            int elev = obj->elevation;
            if (elevation < elev) {
                break;
            }

            if (elevation == elev) {
                if (FrmId(obj).objectType() == OBJ_TYPE_MISC) {
                    if (isExitGridProtoId(obj)) {
                        if ((obj->flags & OBJECT_HIDDEN) != OBJECT_NONE) {
                            objectListNode = objectListNode->next;
                            continue;
                        }

                        ObjectData* data = &(obj->data);

                        MapTransition transition;
                        memset(&transition, 0, sizeof(transition));

                        transition.map = data->misc.map;
                        transition.tile = data->misc.tile;
                        transition.elevation = data->misc.elevation;
                        transition.rotation = data->misc.rotation;
                        mapSetTransition(&transition);

                        wmMapMarkMapEntranceState(transition.map, transition.elevation, 1);
                    }
                }
            }

            objectListNode = objectListNode->next;
        }

        // NOTE: Uninline.
        obj_set_seen(tile);

        int roofX = tile % 200 / 2;
        int roofY = tile / 200 / 2;
        if (roofX != _obj_last_roof_x || roofY != _obj_last_roof_y || elevation != _obj_last_elev) {
            int currentSquare = _square[elevation]->tileFid[roofX + 100 * roofY];
            const RoofTileFrmId currentSquareRoofFrmId = RoofTileFrmId(currentSquare);
            const TileFrameId currentSquareRoofFrameId = currentSquareRoofFrmId.frameId<TileFrameId>();
            // CE: Add additional checks for -1 to prevent array lookup at index -101.
            int previousSquare = _obj_last_roof_x != -1 && _obj_last_roof_y != -1
                ? _square[elevation]->tileFid[_obj_last_roof_x + 100 * _obj_last_roof_y]
                : 0;
            const RoofTileFrmId previousSquareRoofFrmId = RoofTileFrmId(previousSquare);
            bool isEmpty = currentSquareRoofFrameId == TileFrameId::Grid;
            if (isEmpty != _obj_last_is_empty || currentSquareRoofFrmId.flags() != previousSquareRoofFrmId.flags()) {
                if (!_obj_last_is_empty) {
                    tile_fill_roof(_obj_last_roof_x, _obj_last_roof_y, elevation, true);
                }

                if (!isEmpty) {
                    tile_fill_roof(roofX, roofY, elevation, false);
                }

                if (rect != nullptr) {
                    rectUnion(rect, &_scr_size, rect);
                }
            }

            _obj_last_roof_x = roofX;
            _obj_last_roof_y = roofY;
            _obj_last_elev = elevation;
            _obj_last_is_empty = isEmpty;
        }

        if (rect != nullptr) {
            Rect r;
            objectSetLocation(gEgg, tile, elevation, &r);
            rectUnion(rect, &r, rect);
        } else {
            objectSetLocation(gEgg, tile, elevation, nullptr);
        }

        if (elevation != oldElevation) {
            // SFALL: Remove text floaters after moving to another elevation.
            textObjectsReset();

            mapSetElevation(elevation);
            tileSetCenter(tile, TILE_SET_CENTER_REFRESH_WINDOW | TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);
            if (isInCombat() && !clientViewerActive()) {
                _game_user_wants_to_quit = GAME_QUIT_REQUEST_END_COMBAT;
            }
        }
    } else {
        if (elevation != _obj_last_elev && ProtoId(obj).objectType() == OBJ_TYPE_CRITTER) {
            _combat_delete_critter(obj);
        }
    }

    return 0;
}

// 0x48A9A0 obj_reset_roof
int _obj_reset_roof()
{
    TileFrameId frameId = RoofTileFrmId(_square[gDude->elevation]->tileFid[_obj_last_roof_x + 100 * _obj_last_roof_y]).frameId<TileFrameId>();
    if (frameId != TileFrameId::Grid) {
        tile_fill_roof(_obj_last_roof_x, _obj_last_roof_y, gDude->elevation, 1);
    }
    return 0;
}

// Sets object fid.
//
// 0x48AA3C obj_change_fid
int objectSetFrmId(Object* obj, const FrmId& frmId, Rect* dirtyRect)
{
    Rect new_rect;

    if (obj == nullptr) {
        return -1;
    }

    if (frmId.valid()) {
        assert(frmId.hasFid() && "objectSetFrmId(Object* obj, const FrmId& frmId, Rect* dirtyRect) called with path based FrmId which is not supported!");
    }

    if (dirtyRect != nullptr) {
        objectGetRect(obj, dirtyRect);

        obj->fid = frmId.fid();

        objectGetRect(obj, &new_rect);
        rectUnion(dirtyRect, &new_rect, dirtyRect);
    } else {
        obj->fid = frmId.fid();
    }

    return 0;
}

// Sets object frame.
//
// 0x48AA84 obj_set_frame
int objectSetFrame(Object* obj, int frame, Rect* rect)
{
    Rect new_rect;
    Art* art;
    CacheEntry* cache_entry;
    int framesPerDirection;

    if (obj == nullptr) {
        return -1;
    }

    art = artLock(FrmId(obj), &cache_entry);
    if (art == nullptr) {
        return -1;
    }

    framesPerDirection = art->frameCount;

    artUnlock(cache_entry);

    if (frame >= framesPerDirection) {
        return -1;
    }

    if (rect != nullptr) {
        objectGetRect(obj, rect);
        obj->frame = frame;
        objectGetRect(obj, &new_rect);
        rectUnion(rect, &new_rect, rect);
    } else {
        obj->frame = frame;
    }

    return 0;
}

// 0x48AAF0 obj_inc_frame
int objectSetNextFrame(Object* obj, Rect* dirtyRect)
{
    Art* art;
    CacheEntry* cache_entry;
    int framesPerDirection;
    int nextFrame;

    if (obj == nullptr) {
        return -1;
    }

    art = artLock(FrmId(obj), &cache_entry);
    if (art == nullptr) {
        return -1;
    }

    framesPerDirection = art->frameCount;

    artUnlock(cache_entry);

    nextFrame = obj->frame + 1;
    if (nextFrame >= framesPerDirection) {
        nextFrame = 0;
    }

    if (dirtyRect != nullptr) {

        objectGetRect(obj, dirtyRect);

        obj->frame = nextFrame;

        Rect updatedRect;
        objectGetRect(obj, &updatedRect);
        rectUnion(dirtyRect, &updatedRect, dirtyRect);
    } else {
        obj->frame = nextFrame;
    }

    return 0;
}

// 0x48AB60 obj_dec_frame
//
int objectSetPrevFrame(Object* obj, Rect* dirtyRect)
{
    Art* art;
    CacheEntry* cache_entry;
    int framesPerDirection;
    int prevFrame;
    Rect newRect;

    if (obj == nullptr) {
        return -1;
    }

    art = artLock(FrmId(obj), &cache_entry);
    if (art == nullptr) {
        return -1;
    }

    framesPerDirection = art->frameCount;

    artUnlock(cache_entry);

    prevFrame = obj->frame - 1;
    if (prevFrame < 0) {
        prevFrame = framesPerDirection - 1;
    }

    if (dirtyRect != nullptr) {
        objectGetRect(obj, dirtyRect);
        obj->frame = prevFrame;
        objectGetRect(obj, &newRect);
        rectUnion(dirtyRect, &newRect, dirtyRect);
    } else {
        obj->frame = prevFrame;
    }

    return 0;
}

// 0x48ABD4 obj_set_rotation
int objectSetRotation(Object* obj, Rotation rotation, Rect* dirtyRect)
{
    if (obj == nullptr) {
        return -1;
    }

    if (!rotationIsValid(rotation)) {
        return -1;
    }

    if (dirtyRect != nullptr) {
        objectGetRect(obj, dirtyRect);
        obj->rotation = rotation;

        Rect newRect;
        objectGetRect(obj, &newRect);
        rectUnion(dirtyRect, &newRect, dirtyRect);
    } else {
        obj->rotation = rotation;
    }

    return 0;
}

// 0x48AC20 obj_inc_rotation
int objectRotateClockwise(Object* obj, Rect* dirtyRect)
{
    Rotation rotation = obj->rotation + 1;
    if (rotation >= ROTATION_COUNT) {
        rotation = ROTATION_NE;
    }

    return objectSetRotation(obj, rotation, dirtyRect);
}

// 0x48AC38 obj_dec_rotation
int objectRotateCounterClockwise(Object* obj, Rect* dirtyRect)
{
    Rotation rotation = obj->rotation - 1;
    if (rotation < 0) {
        rotation = ROTATION_NW;
    }

    return objectSetRotation(obj, rotation, dirtyRect);
}

// 0x48AC54 obj_rebuild_all_light
void _obj_rebuild_all_light()
{
    lightResetTileIntensity();

    for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
        ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
        while (objectListNode != nullptr) {
            _obj_adjust_light(objectListNode->obj, 0, nullptr);
            objectListNode = objectListNode->next;
        }
    }
}

// 0x48AC90 obj_set_light
int objectSetLight(Object* obj, int lightDistance, int lightIntensity, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    int rc = _obj_turn_off_light(obj, rect);
    if (lightIntensity > 0) {
        obj->lightDistance = std::min(lightDistance, 8);
        obj->lightIntensity = lightIntensity;

        if (rect != nullptr) {
            Rect tempRect;
            rc = _obj_turn_on_light(obj, &tempRect);
            rectUnion(rect, &tempRect, rect);
        } else {
            rc = _obj_turn_on_light(obj, nullptr);
        }
    } else {
        obj->lightIntensity = 0;
        obj->lightDistance = 0;
    }

    return rc;
}

// 0x48AD04 obj_get_visible_light
int objectGetLightIntensity(Object* obj)
{
    int ambientIntensity = lightGetAmbientIntensity();
    int tileIntensity = lightGetTrueTileIntensity(obj->elevation, obj->tile);

    if (obj == gDude) {
        tileIntensity -= gDude->lightIntensity;
    }

    if (tileIntensity >= ambientIntensity) {
        if (tileIntensity > LIGHT_INTENSITY_MAX) {
            tileIntensity = LIGHT_INTENSITY_MAX;
        }
    } else {
        tileIntensity = ambientIntensity;
    }

    return tileIntensity;
}

// 0x48AD48 obj_turn_on_light
int _obj_turn_on_light(Object* obj, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    if (obj->lightIntensity <= 0) {
        obj->flags &= ~OBJECT_LIGHTING;
        return -1;
    }

    if ((obj->flags & OBJECT_LIGHTING) == OBJECT_NONE) {
        obj->flags |= OBJECT_LIGHTING;

        if (_obj_adjust_light(obj, 0, rect) == -1) {
            if (rect != nullptr) {
                objectGetRect(obj, rect);
            }
        }
    }

    return 0;
}

// 0x48AD9C obj_turn_off_light
int _obj_turn_off_light(Object* obj, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    if (obj->lightIntensity <= 0) {
        obj->flags &= ~OBJECT_LIGHTING;
        return -1;
    }

    if ((obj->flags & OBJECT_LIGHTING) != OBJECT_NONE) {
        if (_obj_adjust_light(obj, 1, rect) == -1) {
            if (rect != nullptr) {
                objectGetRect(obj, rect);
            }
        }

        obj->flags &= ~OBJECT_LIGHTING;
    }

    return 0;
}

// 0x48ADF0 obj_turn_on
int objectShow(Object* obj, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    if ((obj->flags & OBJECT_HIDDEN) == OBJECT_NONE) {
        return -1;
    }

    obj->flags &= ~OBJECT_HIDDEN;
    obj->outline &= ~OUTLINE_DISABLED;

    if (_obj_adjust_light(obj, 0, rect) == -1) {
        if (rect != nullptr) {
            objectGetRect(obj, rect);
        }
    }

    if (obj == gDude) {
        if (rect != nullptr) {
            Rect eggRect;
            objectGetRect(gEgg, &eggRect);
            rectUnion(rect, &eggRect, rect);
        }
    }

    return 0;
}

// 0x48AE68 obj_turn_off
int objectHide(Object* object, Rect* rect)
{
    if (object == nullptr) {
        return -1;
    }

    if ((object->flags & OBJECT_HIDDEN) != OBJECT_NONE) {
        return -1;
    }

    if (_obj_adjust_light(object, 1, rect) == -1) {
        if (rect != nullptr) {
            objectGetRect(object, rect);
        }
    }

    object->flags |= OBJECT_HIDDEN;

    if (objectHasOutline(object)) {
        object->outline |= OUTLINE_DISABLED;
    }

    if (object == gDude) {
        if (rect != nullptr) {
            Rect eggRect;
            objectGetRect(gEgg, &eggRect);
            rectUnion(rect, &eggRect, rect);
        }
    }

    return 0;
}

// 0x48AEE4 obj_turn_on_outline
int objectEnableOutline(Object* object, Rect* rect)
{
    if (object == nullptr) {
        return -1;
    }

    object->outline &= ~OUTLINE_DISABLED;

    if (rect != nullptr) {
        objectGetRect(object, rect);
    }

    return 0;
}

// 0x48AF00 obj_turn_off_outline
int objectDisableOutline(Object* object, Rect* rect)
{
    if (object == nullptr) {
        return -1;
    }

    if (objectHasOutline(object)) {
        object->outline |= OUTLINE_DISABLED;
    }

    if (rect != nullptr) {
        objectGetRect(object, rect);
    }

    return 0;
}

// 0x48AF2C obj_toggle_flat
int _obj_toggle_flat(Object* object, Rect* rect)
{
    Rect v1;

    if (object == nullptr) {
        return -1;
    }

    ObjectListNode* node;
    ObjectListNode* previousNode;
    if (objectGetListNode(object, &node, &previousNode) == -1) {
        return -1;
    }

    if (rect != nullptr) {
        objectGetRect(object, rect);

        if (previousNode != nullptr) {
            previousNode->next = node->next;
        } else {
            int tile_index = node->obj->tile;
            if (tile_index == -1) {
                gObjectListHead = gObjectListHead->next;
            } else {
                gObjectListHeadByTile[tile_index] = gObjectListHeadByTile[tile_index]->next;
            }
        }

        object->flags ^= OBJECT_FLAT;

        _obj_insert(node);
        objectGetRect(object, &v1);
        rectUnion(rect, &v1, rect);
    } else {
        if (previousNode != nullptr) {
            previousNode->next = node->next;
        } else {
            int tile = node->obj->tile;
            if (tile == -1) {
                gObjectListHead = gObjectListHead->next;
            } else {
                gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
            }
        }

        object->flags ^= OBJECT_FLAT;

        _obj_insert(node);
    }

    return 0;
}

// 0x48B0FC obj_erase_object
int objectDestroy(Object* object, Rect* rect)
{
    if (object == nullptr) {
        return -1;
    }

    // MP_PROTOCOL.md §4 destroy: emit before the free so obj->id is readable.
    // Map teardown (_obj_remove_all) bypasses objectDestroy, so this fires only
    // for gameplay destroys, not the bulk world-clear on map change.
    presenter()->objectDestroyed(object);

    // A time-skip window may be holding this object's pre-skip origin; the
    // pointer is about to become unreadable.
    presenterTimeSkipForget(object);

    _gmouse_remove_item_outline(object);

    ObjectListNode* node;
    ObjectListNode* previousNode;
    if (objectGetListNode(object, &node, &previousNode) == 0) {
        if (_obj_adjust_light(object, 1, rect) == -1) {
            if (rect != nullptr) {
                objectGetRect(object, rect);
            }
        }

        if (_obj_remove(node, previousNode) != 0) {
            return -1;
        }

        return 0;
    }

    // NOTE: Uninline.
    if (objectListNodeCreate(&node) == -1) {
        return -1;
    }

    node->obj = object;

    if (_obj_remove(node, node) == -1) {
        return -1;
    }

    return 0;
}

// 0x48B1B0 obj_inven_free
int _obj_inven_free(Inventory* inventory)
{
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);

        ObjectListNode* node;
        // NOTE: Uninline.
        objectListNodeCreate(&node);

        node->obj = inventoryItem->item;
        node->obj->flags &= ~OBJECT_NO_REMOVE;
        _obj_remove(node, node);

        inventoryItem->item = nullptr;
    }

    if (inventory->items != nullptr) {
        internal_free(inventory->items);
        inventory->items = nullptr;
        inventory->capacity = 0;
        inventory->length = 0;
    }

    return 0;
}

// 0x48B24C obj_action_can_use
bool _obj_action_can_use(Object* obj)
{
    const ProtoId protoId = ProtoId(obj);
    // SFALL
    if (protoId != ItemProtoTypeId::LitFlare && !explosiveIsActiveExplosive(protoId)) {
        return _proto_action_can_use(protoId);
    } else {
        return false;
    }
}

// 0x48B278 obj_action_can_talk_to
bool _obj_action_can_talk_to(Object* obj)
{
    const ProtoId protoId = obj;
    return _proto_action_can_talk_to(protoId) && (protoId.objectType() == OBJ_TYPE_CRITTER) && critterIsActive(obj);
}

// 0x48B2A8 obj_portal_is_walk_thru
bool _obj_portal_is_walk_thru(Object* obj)
{
    const ProtoId protoId = obj;
    if (protoId.objectType() != OBJ_TYPE_SCENERY) {
        return false;
    }

    Proto* proto;
    if (protoGetProto(protoId, &proto) == -1) {
        return false;
    }

    if (settings.qol.auto_open_doors) {
        if (!isInCombat()) {
            if (proto->scenery.type == SCENERY_TYPE_DOOR) // Door
            {
                // Unlocked, and has no script ID
                if ((proto->scenery.data.door.openFlags == 0) && (obj->sid == -1)) {
                    return true;
                }
            }
        }
    }

    return (proto->scenery.data.generic.genericFlags & 0x04) != 0;
}

// 0x48B2E8 objFindObjPtrFromID
Object* objectFindById(int a1)
{
    Object* obj = objectFindFirst();
    while (obj != nullptr) {
        if (obj->id == a1) {
            return obj;
        }
        obj = objectFindNext();
    }

    return nullptr;
}

// Returns root owner of given object.
//
// 0x48B304 obj_top_environment
Object* objectGetOwner(Object* object)
{
    Object* owner = object->owner;
    if (owner == nullptr) {
        return nullptr;
    }

    while (owner->owner != nullptr) {
        owner = owner->owner;
    }

    return owner;
}

// 0x48B318 obj_remove_all
void _obj_remove_all()
{
    ObjectListNode* node;
    ObjectListNode* prev;
    ObjectListNode* next;

    // Bulk world-clear bypasses objectDestroy, so nothing else drops the
    // time-skip window's held pointers. Every one of them dies here.
    presenterTimeSkipForgetAll();

    _scr_remove_all();

    for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
        node = gObjectListHeadByTile[tile];
        prev = nullptr;

        while (node != nullptr) {
            next = node->next;
            if (_obj_remove(node, prev) == -1) {
                prev = node;
            }
            node = next;
        }
    }

    node = gObjectListHead;
    prev = nullptr;

    while (node != nullptr) {
        next = node->next;
        if (_obj_remove(node, prev) == -1) {
            prev = node;
        }
        node = next;
    }

    _obj_last_roof_y = -1;
    _obj_last_elev = ELEVATION_INVALID;
    _obj_last_is_empty = true;
    _obj_last_roof_x = -1;
}

// 0x48B3A8 obj_find_first
Object* objectFindFirst()
{
    gObjectFindElevation = ELEVATION_FIRST;

    for (gObjectFindTile = 0; gObjectFindTile < HEX_GRID_SIZE; gObjectFindTile++) {
        ObjectListNode* objectListNode = gObjectListHeadByTile[gObjectFindTile];
        while (objectListNode != nullptr) {
            Object* object = objectListNode->obj;
            if (!artIsObjectTypeHidden(FrmId(object).objectType())) {
                gObjectFindLastObjectListNode = objectListNode;
                return object;
            }
            objectListNode = objectListNode->next;
        }
    }

    gObjectFindLastObjectListNode = nullptr;
    return nullptr;
}

// 0x48B41C obj_find_next
Object* objectFindNext()
{
    if (gObjectFindLastObjectListNode == nullptr) {
        return nullptr;
    }

    ObjectListNode* objectListNode = gObjectFindLastObjectListNode->next;

    while (true) {
        if (objectListNode == nullptr) {
            gObjectFindTile++;
            if (gObjectFindTile >= HEX_GRID_SIZE) {
                break;
            }

            objectListNode = gObjectListHeadByTile[gObjectFindTile];
        }

        while (objectListNode != nullptr) {
            Object* object = objectListNode->obj;
            if (!artIsObjectTypeHidden(FrmId(object).objectType())) {
                gObjectFindLastObjectListNode = objectListNode;
                return object;
            }
            objectListNode = objectListNode->next;
        }
    }

    gObjectFindLastObjectListNode = nullptr;
    return nullptr;
}

void objectVisitAll(ObjectVisitorProc* visitor, void* context)
{
    if (visitor == nullptr) {
        return;
    }

    for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
        for (ObjectListNode* node = gObjectListHeadByTile[tile]; node != nullptr; node = node->next) {
            visitor(node->obj, context);
        }
    }

    for (ObjectListNode* node = gObjectListHead; node != nullptr; node = node->next) {
        visitor(node->obj, context);
    }
}

// 0x48B48C obj_find_first_at
Object* objectFindFirstAtElevation(MapElevation elevation)
{
    gObjectFindElevation = elevation;
    gObjectFindTile = 0;

    for (gObjectFindTile = 0; gObjectFindTile < HEX_GRID_SIZE; gObjectFindTile++) {
        ObjectListNode* objectListNode = gObjectListHeadByTile[gObjectFindTile];
        while (objectListNode != nullptr) {
            Object* object = objectListNode->obj;
            if (object->elevation == elevation) {
                if (!artIsObjectTypeHidden(FrmId(object).objectType())) {
                    gObjectFindLastObjectListNode = objectListNode;
                    return object;
                }
            }
            objectListNode = objectListNode->next;
        }
    }

    gObjectFindLastObjectListNode = nullptr;
    return nullptr;
}

// 0x48B510 obj_find_next_at
Object* objectFindNextAtElevation()
{
    if (gObjectFindLastObjectListNode == nullptr) {
        return nullptr;
    }

    ObjectListNode* objectListNode = gObjectFindLastObjectListNode->next;

    while (true) {
        if (objectListNode == nullptr) {
            gObjectFindTile++;
            if (gObjectFindTile >= HEX_GRID_SIZE) {
                break;
            }

            objectListNode = gObjectListHeadByTile[gObjectFindTile];
        }

        while (objectListNode != nullptr) {
            Object* object = objectListNode->obj;
            if (object->elevation == gObjectFindElevation) {
                if (!artIsObjectTypeHidden(FrmId(object).objectType())) {
                    gObjectFindLastObjectListNode = objectListNode;
                    return object;
                }
            }
            objectListNode = objectListNode->next;
        }
    }

    gObjectFindLastObjectListNode = nullptr;
    return nullptr;
}

// 0x48B5A8 obj_find_first_at_tile
Object* objectFindFirstAtLocation(MapElevation elevation, int tile)
{
    gObjectFindElevation = elevation;
    gObjectFindTile = tile;

    ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
    while (objectListNode != nullptr) {
        Object* object = objectListNode->obj;
        if (object->elevation == elevation) {
            if (!artIsObjectTypeHidden(FrmId(object).objectType())) {
                gObjectFindLastObjectListNode = objectListNode;
                return object;
            }
        }
        objectListNode = objectListNode->next;
    }

    gObjectFindLastObjectListNode = nullptr;
    return nullptr;
}

// 0x48B608 obj_find_next_at_tile
Object* objectFindNextAtLocation()
{
    if (gObjectFindLastObjectListNode == nullptr) {
        return nullptr;
    }

    ObjectListNode* objectListNode = gObjectFindLastObjectListNode->next;

    while (objectListNode != nullptr) {
        Object* object = objectListNode->obj;
        if (object->elevation == gObjectFindElevation) {
            if (!artIsObjectTypeHidden(FrmId(object).objectType())) {
                gObjectFindLastObjectListNode = objectListNode;
                return object;
            }
        }
        objectListNode = objectListNode->next;
    }

    gObjectFindLastObjectListNode = nullptr;
    return nullptr;
}

// 0x48B66C obj_bound
void objectGetRect(Object* obj, Rect* rect)
{
    if (obj == nullptr) {
        return;
    }

    if (rect == nullptr) {
        return;
    }

    bool isOutlined = objectHasOutline(obj);

    CacheEntry* artHandle;
    Art* art = artLock(FrmId(obj), &artHandle);
    if (art == nullptr) {
        rect->left = 0;
        rect->top = 0;
        rect->right = 0;
        rect->bottom = 0;
        return;
    }

    int width;
    int height;
    artGetSize(art, obj->frame, obj->rotation, &width, &height);

    if (obj->tile == -1) {
        rect->left = obj->sx;
        rect->top = obj->sy;
        rect->right = obj->sx + width - 1;
        rect->bottom = obj->sy + height - 1;
    } else {
        int tileScreenY;
        int tileScreenX;
        if (tileToScreenXY(obj->tile, &tileScreenX, &tileScreenY) == 0) {
            tileScreenX += 16;
            tileScreenY += 8;

            tileScreenX += art->xOffsets[obj->rotation];
            tileScreenY += art->yOffsets[obj->rotation];

            tileScreenX += obj->x;
            tileScreenY += obj->y;

            rect->left = tileScreenX - width / 2;
            rect->top = tileScreenY - height + 1;
            rect->right = width + rect->left - 1;
            rect->bottom = tileScreenY;
        } else {
            rect->left = 0;
            rect->top = 0;
            rect->right = 0;
            rect->bottom = 0;
            isOutlined = false;
        }
    }

    artUnlock(artHandle);

    if (isOutlined) {
        rect->left--;
        rect->top--;
        rect->right++;
        rect->bottom++;
    }
}

// 0x48B7F8 obj_occupied
bool _obj_occupied(int tile, MapElevation elevation)
{
    ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
    while (objectListNode != nullptr) {
        if (objectListNode->obj->elevation == elevation
            && objectListNode->obj != gGameMouseBouncingCursor
            && objectListNode->obj != gGameMouseHexCursor) {
            return true;
        }
        objectListNode = objectListNode->next;
    }

    return false;
}

// 0x48B848 obj_blocking_at
Object* _obj_blocking_at(Object* excludeObj, int tile, MapElevation elev)
{
    ObjectListNode* objectListNode;
    Object* obj;

    if (!hexGridTileIsValid(tile)) {
        return nullptr;
    }

    objectListNode = gObjectListHeadByTile[tile];
    while (objectListNode != nullptr) {
        obj = objectListNode->obj;
        if (obj->elevation == elev) {
            if ((obj->flags & OBJECT_HIDDEN) == OBJECT_NONE && (obj->flags & OBJECT_NO_BLOCK) == OBJECT_NONE && obj != excludeObj) {
                ObjectType type = FrmId(obj).objectType();
                if (type == OBJ_TYPE_CRITTER
                    || type == OBJ_TYPE_SCENERY
                    || type == OBJ_TYPE_WALL) {
                    return obj;
                }
            }
        }
        objectListNode = objectListNode->next;
    }

    for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
        int neighboor = tileGetTileInDirection(tile, rotation, 1);
        if (hexGridTileIsValid(neighboor)) {
            objectListNode = gObjectListHeadByTile[neighboor];
            while (objectListNode != nullptr) {
                obj = objectListNode->obj;
                if ((obj->flags & OBJECT_MULTIHEX) != OBJECT_NONE) {
                    if (obj->elevation == elev) {
                        if ((obj->flags & OBJECT_HIDDEN) == OBJECT_NONE && (obj->flags & OBJECT_NO_BLOCK) == OBJECT_NONE && obj != excludeObj) {
                            ObjectType type = FrmId(obj).objectType();
                            if (type == OBJ_TYPE_CRITTER
                                || type == OBJ_TYPE_SCENERY
                                || type == OBJ_TYPE_WALL) {
                                return obj;
                            }
                        }
                    }
                }
                objectListNode = objectListNode->next;
            }
        }
    }

    return nullptr;
}

// 0x48B930 obj_shoot_blocking_at
Object* _obj_shoot_blocking_at(Object* excludeObj, int tile, MapElevation elev)
{
    if (!hexGridTileIsValid(tile)) {
        return nullptr;
    }

    ObjectListNode* objectListItem = gObjectListHeadByTile[tile];
    while (objectListItem != nullptr) {
        Object* candidate = objectListItem->obj;
        if (candidate->elevation == elev) {
            unsigned int flags = candidate->flags;
            if ((flags & OBJECT_HIDDEN) == OBJECT_NONE && ((flags & OBJECT_NO_BLOCK) == OBJECT_NONE || (flags & OBJECT_SHOOT_THRU) == OBJECT_NONE) && candidate != excludeObj) {
                ObjectType type = FrmId(candidate).objectType();
                // SFALL: Fix to prevent corpses from blocking line of fire.
                if ((type == OBJ_TYPE_CRITTER && !critterIsDead(candidate))
                    || type == OBJ_TYPE_SCENERY
                    || type == OBJ_TYPE_WALL) {
                    return candidate;
                }
            }
        }
        objectListItem = objectListItem->next;
    }

    for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
        int adjacentTile = tileGetTileInDirection(tile, rotation, 1);
        if (!hexGridTileIsValid(adjacentTile)) {
            continue;
        }

        ObjectListNode* objectListItem = gObjectListHeadByTile[adjacentTile];
        while (objectListItem != nullptr) {
            Object* candidate = objectListItem->obj;
            unsigned int flags = candidate->flags;
            if ((flags & OBJECT_MULTIHEX) != OBJECT_NONE) {
                if (candidate->elevation == elev) {
                    if ((flags & OBJECT_HIDDEN) == OBJECT_NONE && (flags & OBJECT_NO_BLOCK) == OBJECT_NONE && candidate != excludeObj) {
                        ObjectType type = FrmId(candidate).objectType();
                        // SFALL: Fix to prevent corpses from blocking line of
                        // fire.
                        if ((type == OBJ_TYPE_CRITTER && !critterIsDead(candidate))
                            || type == OBJ_TYPE_SCENERY
                            || type == OBJ_TYPE_WALL) {
                            return candidate;
                        }
                    }
                }
            }
            objectListItem = objectListItem->next;
        }
    }

    return nullptr;
}

// 0x48BA20 obj_ai_blocking_at
Object* _obj_ai_blocking_at(Object* excludeObj, int tile, MapElevation elevation)
{
    if (!hexGridTileIsValid(tile)) {
        return nullptr;
    }

    ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
    while (objectListNode != nullptr) {
        Object* object = objectListNode->obj;
        if (object->elevation == elevation) {
            if ((object->flags & OBJECT_HIDDEN) == OBJECT_NONE
                && (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
                && object != excludeObj) {
                ObjectType objectType = FrmId(object).objectType();
                if (objectType == OBJ_TYPE_CRITTER
                    || objectType == OBJ_TYPE_SCENERY
                    || objectType == OBJ_TYPE_WALL) {
                    if (_moveBlockObj != nullptr || objectType != OBJ_TYPE_CRITTER) {
                        return object;
                    }

                    _moveBlockObj = object;
                }
            }
        }
        objectListNode = objectListNode->next;
    }

    for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
        int candidate = tileGetTileInDirection(tile, rotation, 1);
        if (!hexGridTileIsValid(candidate)) {
            continue;
        }

        objectListNode = gObjectListHeadByTile[candidate];
        while (objectListNode != nullptr) {
            Object* object = objectListNode->obj;
            if ((object->flags & OBJECT_MULTIHEX) != OBJECT_NONE) {
                if (object->elevation == elevation) {
                    if ((object->flags & OBJECT_HIDDEN) == OBJECT_NONE
                        && (object->flags & OBJECT_NO_BLOCK) == OBJECT_NONE
                        && object != excludeObj) {
                        ObjectType objectType = FrmId(object).objectType();
                        if (objectType == OBJ_TYPE_CRITTER
                            || objectType == OBJ_TYPE_SCENERY
                            || objectType == OBJ_TYPE_WALL) {
                            if (_moveBlockObj != nullptr || objectType != OBJ_TYPE_CRITTER) {
                                return object;
                            }

                            _moveBlockObj = object;
                        }
                    }
                }
            }
            objectListNode = objectListNode->next;
        }
    }

    return nullptr;
}

// 0x48BB44 obj_scroll_blocking_at
int _obj_scroll_blocking_at(int tile, MapElevation elev)
{
    // TODO: Might be an error - why tile 0 is excluded?
    if (tile <= 0 || tile >= 40000) {
        return -1;
    }

    ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
    while (objectListNode != nullptr) {
        if (elev < objectListNode->obj->elevation) {
            break;
        }

        if (objectListNode->obj->elevation == elev && ProtoId(objectListNode->obj) == MiscProtoTypeId::Id0x0C) {
            return 0;
        }

        objectListNode = objectListNode->next;
    }

    return -1;
}

// 0x48BB88 obj_sight_blocking_at
Object* _obj_sight_blocking_at(Object* excludeObj, int tile, MapElevation elevation)
{
    ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
    while (objectListNode != nullptr) {
        Object* object = objectListNode->obj;
        if (object->elevation == elevation
            && (object->flags & OBJECT_HIDDEN) == OBJECT_NONE
            && (object->flags & OBJECT_LIGHT_THRU) == OBJECT_NONE
            && object != excludeObj) {
            ObjectType objectType = FrmId(object).objectType();
            if (objectType == OBJ_TYPE_SCENERY || objectType == OBJ_TYPE_WALL) {
                return object;
            }
        }
        objectListNode = objectListNode->next;
    }

    return nullptr;
}

// 0x48BBD4 obj_dist
int objectGetDistanceBetween(Object* object1, Object* object2)
{
    if (object1 == nullptr || object2 == nullptr) {
        return 0;
    }

    int distance = tileDistanceBetween(object1->tile, object2->tile);

    if ((object1->flags & OBJECT_MULTIHEX) != OBJECT_NONE) {
        distance -= 1;
    }

    if ((object2->flags & OBJECT_MULTIHEX) != OBJECT_NONE) {
        distance -= 1;
    }

    if (distance < 0) {
        distance = 0;
    }

    return distance;
}

// 0x48BC08 obj_dist_with_tile
int objectGetDistanceBetweenTiles(Object* object1, int tile1, Object* object2, int tile2)
{
    if (object1 == nullptr || object2 == nullptr) {
        return 0;
    }

    int distance = tileDistanceBetween(tile1, tile2);

    if ((object1->flags & OBJECT_MULTIHEX) != OBJECT_NONE) {
        distance -= 1;
    }

    if ((object2->flags & OBJECT_MULTIHEX) != OBJECT_NONE) {
        distance -= 1;
    }

    if (distance < 0) {
        distance = 0;
    }

    return distance;
}

bool objectWithinWalkDistance(Object* critter, Object* target)
{
    if (critter == nullptr || target == nullptr) {
        return false;
    }
    int walkDistanceLimit = settings.qol.use_walk_distance + 2;
    int distance = objectGetDistanceBetween(critter, target);
    if (distance <= 1) {
        return true;
    }
    if (distance >= walkDistanceLimit) {
        return false;
    }

    return _make_path(critter, critter->tile, target->tile, nullptr, 0) < walkDistanceLimit;
}

// 0x48BC38 obj_create_list
int objectListCreate(int tile, MapElevation elevation, ObjectType objectType, Object*** objectListPtr)
{
    if (objectListPtr == nullptr) {
        return -1;
    }

    int count = 0;
    if (tile == -1) {
        for (int index = 0; index < HEX_GRID_SIZE; index++) {
            ObjectListNode* objectListNode = gObjectListHeadByTile[index];
            while (objectListNode != nullptr) {
                Object* obj = objectListNode->obj;
                if ((obj->flags & OBJECT_HIDDEN) == OBJECT_NONE
                    && obj->elevation == elevation
                    && FrmId(obj).objectType() == objectType) {
                    count++;
                }
                objectListNode = objectListNode->next;
            }
        }
    } else {
        ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
        while (objectListNode != nullptr) {
            Object* obj = objectListNode->obj;
            if ((obj->flags & OBJECT_HIDDEN) == OBJECT_NONE
                && obj->elevation == elevation
                && FrmId(objectListNode->obj).objectType() == objectType) {
                count++;
            }
            objectListNode = objectListNode->next;
        }
    }

    if (count == 0) {
        return 0;
    }

    Object** objects = *objectListPtr = (Object**)internal_malloc(sizeof(*objects) * count);
    if (objects == nullptr) {
        return -1;
    }

    if (tile == -1) {
        for (int index = 0; index < HEX_GRID_SIZE; index++) {
            ObjectListNode* objectListNode = gObjectListHeadByTile[index];
            while (objectListNode) {
                Object* obj = objectListNode->obj;
                if ((obj->flags & OBJECT_HIDDEN) == OBJECT_NONE
                    && obj->elevation == elevation
                    && FrmId(obj).objectType() == objectType) {
                    *objects++ = obj;
                }
                objectListNode = objectListNode->next;
            }
        }
    } else {
        ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
        while (objectListNode != nullptr) {
            Object* obj = objectListNode->obj;
            if ((obj->flags & OBJECT_HIDDEN) == OBJECT_NONE
                && obj->elevation == elevation
                && FrmId(obj).objectType() == objectType) {
                *objects++ = obj;
            }
            objectListNode = objectListNode->next;
        }
    }

    return count;
}

// 0x48BDCC obj_delete_list
void objectListFree(Object** objectList)
{
    if (objectList != nullptr) {
        internal_free(objectList);
    }
}

// 0x48BDD8 translucent_trans_buf_to_buf


// 0x48BEFC dark_trans_buf_to_buf


// 0x48BF88 dark_translucent_trans_buf_to_buf


// 0x48C03C intensity_mask_buf_to_buf


// 0x48C2B4 obj_outline_object
int objectSetOutline(Object* obj, OutlineType outlineType, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    if (objectHasOutline(obj)) {
        return -1;
    }

    if ((obj->flags & OBJECT_NO_HIGHLIGHT) != OBJECT_NONE) {
        return -1;
    }

    obj->outline = outlineType;

    if ((obj->flags & OBJECT_HIDDEN) != OBJECT_NONE) {
        obj->outline |= OUTLINE_DISABLED;
    }

    if (rect != nullptr) {
        objectGetRect(obj, rect);
    }

    return 0;
}

// 0x48C2F0 obj_remove_outline
int objectClearOutline(Object* object, Rect* rect)
{
    if (object == nullptr) {
        return -1;
    }

    if (rect != nullptr) {
        objectGetRect(object, rect);
    }

    object->outline = OUTLINE_TYPE_NONE;

    return 0;
}

// 0x48C340 obj_intersects_with
ObjectFlags _obj_intersects_with(Object* object, int x, int y)
{
    ObjectFlags flags = OBJECT_NONE;

    if (object == gEgg || (object->flags & OBJECT_HIDDEN) == OBJECT_NONE) {
        CacheEntry* handle;
        Art* art = artLock(FrmId(object), &handle);
        if (art != nullptr) {
            int width;
            int height;
            artGetSize(art, object->frame, object->rotation, &width, &height);

            int minX;
            int minY;
            int maxX;
            int maxY;
            if (object->tile == -1) {
                minX = object->sx;
                minY = object->sy;
                maxX = minX + width - 1;
                maxY = minY + height - 1;
            } else {
                int tileScreenX;
                int tileScreenY;
                tileToScreenXY(object->tile, &tileScreenX, &tileScreenY);
                tileScreenX += 16;
                tileScreenY += 8;

                tileScreenX += art->xOffsets[object->rotation];
                tileScreenY += art->yOffsets[object->rotation];

                tileScreenX += object->x;
                tileScreenY += object->y;

                minX = tileScreenX - width / 2;
                maxX = minX + width - 1;

                minY = tileScreenY - height + 1;
                maxY = tileScreenY;
            }

            if (x >= minX && x <= maxX && y >= minY && y <= maxY) {
                unsigned char* data = artGetFrameData(art, object->frame, object->rotation);
                if (data != nullptr) {
                    if (data[width * (y - minY) + x - minX] != 0) {
                        flags |= OBJECT_HIDDEN;

                        if ((object->flags & OBJECT_FLAG_0xFC000) != OBJECT_NONE) {
                            if ((object->flags & OBJECT_TRANS_NONE) == OBJECT_NONE) {
                                flags &= ~(OBJECT_0X02 | OBJECT_HIDDEN);
                                flags |= OBJECT_0X02;
                            }
                        } else {
                            ObjectType type = FrmId(object).objectType();
                            if (type == OBJ_TYPE_SCENERY || type == OBJ_TYPE_WALL) {
                                Proto* proto;
                                protoGetProto(object, &proto);

                                bool v20;
                                ProtoExtendedFlags extendedFlags = proto->scenery.extendedFlags;
                                if ((extendedFlags & PROTO_EXT_FLAG_HIDDEN) != PROTO_EXT_FLAG_NONE || (extendedFlags & PROTO_EXT_FLAG_WEST_CORNER) != PROTO_EXT_FLAG_NONE) {
                                    v20 = tileIsInFrontOf(object->tile, gDude->tile);
                                } else if ((extendedFlags & PROTO_EXT_FLAG_NORTH_CORNER) != PROTO_EXT_FLAG_NONE) {
                                    // NOTE: Original code uses bitwise or, but given the fact that these functions return
                                    // bools, logical or is more suitable.
                                    v20 = tileIsInFrontOf(object->tile, gDude->tile) || tileIsToRightOf(gDude->tile, object->tile);
                                } else if ((extendedFlags & PROTO_EXT_FLAG_SOUTH_CORNER) != PROTO_EXT_FLAG_NONE) {
                                    v20 = tileIsInFrontOf(object->tile, gDude->tile) && tileIsToRightOf(gDude->tile, object->tile);
                                } else {
                                    v20 = tileIsToRightOf(gDude->tile, object->tile);
                                }

                                if (v20) {
                                    if (_obj_intersects_with(gEgg, x, y) != OBJECT_NONE) {
                                        flags |= OBJECT_NO_SAVE;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            artUnlock(handle);
        }
    }

    return flags;
}

// 0x48C5C4 obj_create_intersect_list
int _obj_create_intersect_list(int x, int y, MapElevation elevation, ObjectType objectType, ObjectWithFlags** entriesPtr)
{
    int upperLeftTile = tileFromScreenXY(x - 320, y - 240, true);
    *entriesPtr = nullptr;

    if (gObjectsUpdateAreaHexSize <= 0) {
        return 0;
    }

    int count = 0;

    int parity = gCenterTile & 1;
    for (int index = 0; index < gObjectsUpdateAreaHexSize; index++) {
        int offsetIndex = _orderTable[parity][index];
        if (_offsetDivTable[offsetIndex] < 30 && _offsetModTable[offsetIndex] < 20) {
            int tile = _offsetTable[parity][offsetIndex] + upperLeftTile;
            ObjectListNode* objectListNode = hexGridTileIsValid(tile)
                ? gObjectListHeadByTile[tile]
                : nullptr;
            while (objectListNode != nullptr) {
                Object* object = objectListNode->obj;
                if (object->elevation > elevation) {
                    break;
                }

                if (object->elevation == elevation
                    && (objectType == OBJ_TYPE_INVALID || FrmId(object).objectType() == objectType)
                    && object != gEgg) {
                    ObjectFlags flags = _obj_intersects_with(object, x, y);
                    if (flags != OBJECT_NONE) {
                        ObjectWithFlags* entries = (ObjectWithFlags*)internal_realloc(*entriesPtr, sizeof(*entries) * (count + 1));
                        if (entries != nullptr) {
                            *entriesPtr = entries;
                            entries[count].object = object;
                            entries[count].flags = flags;
                            count++;
                        }
                    }
                }

                objectListNode = objectListNode->next;
            }
        }
    }

    return count;
}

// 0x48C74C obj_delete_intersect_list
void _obj_delete_intersect_list(ObjectWithFlags** entriesPtr)
{
    if (entriesPtr != nullptr && *entriesPtr != nullptr) {
        internal_free(*entriesPtr);
        *entriesPtr = nullptr;
    }
}

// NOTE: Inlined.
//
// 0x48C76C obj_set_seen
void obj_set_seen(int tile)
{
    _obj_seen[tile >> 3] |= 1 << (tile & 7);
}

// 0x48C788 obj_clear_seen
void _obj_clear_seen()
{
    memset(_obj_seen, 0, sizeof(_obj_seen));
}

// 0x48C7A0 obj_process_seen
void _obj_process_seen()
{
    int i;
    int v7;
    int v8;
    int v5;
    int v0;
    int v3;
    ObjectListNode* obj_entry;

    memset(_obj_seen_check, 0, 5001);

    v0 = 400;
    for (i = 0; i < 5001; i++) {
        if (_obj_seen[i] != 0) {
            for (v3 = i - 400; v3 != v0; v3 += 25) {
                if (v3 >= 0 && v3 < 5001) {
                    _obj_seen_check[v3] = -1;
                    if (v3 > 0) {
                        _obj_seen_check[v3 - 1] = -1;
                    }
                    if (v3 < 5000) {
                        _obj_seen_check[v3 + 1] = -1;
                    }
                    if (v3 > 1) {
                        _obj_seen_check[v3 - 2] = -1;
                    }
                    if (v3 < 4999) {
                        _obj_seen_check[v3 + 2] = -1;
                    }
                }
            }
        }
        v0++;
    }

    v7 = 0;
    for (i = 0; i < 5001; i++) {
        if (_obj_seen_check[i] != 0) {
            v8 = 1;
            for (v5 = v7; v5 < v7 + 8; v5++) {
                if (v8 & _obj_seen_check[i]) {
                    if (v5 < 40000) {
                        for (obj_entry = gObjectListHeadByTile[v5]; obj_entry != nullptr; obj_entry = obj_entry->next) {
                            if (obj_entry->obj->elevation == gDude->elevation) {
                                obj_entry->obj->flags |= OBJECT_SEEN;
                            }
                        }
                    }
                }
                v8 *= 2;
            }
        }
        v7 += 8;
    }

    memset(_obj_seen, 0, 5001);
}

// 0x48C8E4 object_name
char* objectGetName(Object* obj)
{
    ObjectType objectType = FrmId(obj).objectType();
    switch (objectType) {
    case OBJ_TYPE_ITEM:
        return itemGetName(obj);
    case OBJ_TYPE_CRITTER:
        return critterGetName(obj);
    default:
        return protoGetName(obj);
    }
}

// 0x48C914 object_description
char* objectGetDescription(Object* obj)
{
    if (FrmId(obj).objectType() == OBJ_TYPE_ITEM) {
        return itemGetDescription(obj);
    }

    return protoGetDescription(obj);
}

// Warm objects cache?
//
// 0x48C938 obj_preload_art_cache
void _obj_preload_art_cache(MapHeaderFlags flags)
{
    if (gObjectFids == nullptr) {
        return;
    }

    unsigned char arr[FrmId::kMaxFrameId + 1];
    memset(arr, 0, sizeof(arr));

    constexpr MapHeaderFlags kElevationFlags[ELEVATION_COUNT] = {
        MAP_HEADER_ELEVATION_0,
        MAP_HEADER_ELEVATION_1,
        MAP_HEADER_ELEVATION_2,
    };

    for (MapElevation elevation = ELEVATION_FIRST; elevation < ELEVATION_COUNT; elevation++) {
        if ((flags & kElevationFlags[elevation]) != MAP_HEADER_NONE) {
            continue;
        }

        for (int tile = 0; tile < SQUARE_GRID_SIZE; tile++) {
            int tileFids = _square[elevation]->tileFid[tile];
            const FloorTileFrmId floorTileFrmId = FloorTileFrmId(tileFids);
            const RoofTileFrmId roofTileFrmId = RoofTileFrmId(tileFids);
            TileFrameId floorTileFrameId = floorTileFrmId.frameId<TileFrameId>();
            if (floorTileFrameId == TileFrameId::Invalid) {
                floorTileFrameId = TileFrameId::Last;
            }

            TileFrameId roofTileFrameId = roofTileFrmId.frameId<TileFrameId>();
            if (roofTileFrameId == TileFrameId::Invalid) {
                roofTileFrameId = TileFrameId::Last;
            }

            arr[static_cast<int>(floorTileFrameId)] = 1;
            arr[static_cast<int>(roofTileFrameId)] = 1;
        }
    }

    qsort(gObjectFids, gObjectFidsLength, sizeof(*gObjectFids), _obj_preload_sort);

    int v11 = gObjectFidsLength;
    int v12 = gObjectFidsLength;

    if (FrmId(gObjectFids[v12 - 1]).objectType() == OBJ_TYPE_WALL) {
        ObjectType objectType = OBJ_TYPE_ITEM;
        do {
            v11--;
            objectType = FrmId(gObjectFids[v12 - 1]).objectType();
            v12--;
        } while (objectType == OBJ_TYPE_WALL);
        v11++;
    }

    CacheEntry* cache_handle;
    if (artLock(FrmId(*gObjectFids), &cache_handle) != nullptr) {
        artUnlock(cache_handle);
    }

    for (int i = 1; i < v11; i++) {
        if (gObjectFids[i - 1] != gObjectFids[i]) {
            if (artLock(FrmId(gObjectFids[i]), &cache_handle) != nullptr) {
                artUnlock(cache_handle);
            }
        }
    }

    for (int i = FrmId::kMinFrameId; i <= FrmId::kMaxFrameId; i++) {
        if (arr[i] != 0) {
            if (artLock(static_cast<TileFrameId>(i), &cache_handle) != nullptr) {
                artUnlock(cache_handle);
            }
        }
    }

    for (int i = v11; i < gObjectFidsLength; i++) {
        if (gObjectFids[i - 1] != gObjectFids[i]) {
            if (artLock(FrmId(gObjectFids[i]), &cache_handle) != nullptr) {
                artUnlock(cache_handle);
            }
        }
    }

    internal_free(gObjectFids);
    gObjectFids = nullptr;

    gObjectFidsLength = 0;
}

// 0x48CB88 obj_offset_table_init
static int _obj_offset_table_init()
{
    int i;

    if (_offsetTable[0] != nullptr) {
        return -1;
    }

    if (_offsetTable[1] != nullptr) {
        return -1;
    }

    _offsetTable[0] = (int*)internal_malloc(sizeof(int) * gObjectsUpdateAreaHexSize);
    if (_offsetTable[0] == nullptr) {
        goto err;
    }

    _offsetTable[1] = (int*)internal_malloc(sizeof(int) * gObjectsUpdateAreaHexSize);
    if (_offsetTable[1] == nullptr) {
        goto err;
    }

    for (int parity = 0; parity < 2; parity++) {
        int originTile = tileFromScreenXY(gObjectsUpdateAreaPixelBounds.left, gObjectsUpdateAreaPixelBounds.top);
        if (originTile != -1) {
            int* offsets = _offsetTable[gCenterTile & 1];
            int originTileX;
            int originTileY;
            tileToScreenXY(originTile, &originTileX, &originTileY);

            int parityShift = 16;
            originTileX += 16;
            originTileY += 8;
            if (originTileX > gObjectsUpdateAreaPixelBounds.left) {
                parityShift = -parityShift;
            }

            int tileX = originTileX;
            for (int y = 0; y < gObjectsUpdateAreaHexHeight; y++) {
                for (int x = 0; x < gObjectsUpdateAreaHexWidth; x++) {
                    int tile = tileFromScreenXY(tileX, originTileY);
                    if (tile == -1) {
                        goto err;
                    }

                    tileX += 32;
                    *offsets++ = tile - originTile;
                }

                tileX = parityShift + originTileX;
                originTileY += 12;
                parityShift = -parityShift;
            }
        }

        if (tileSetCenter(gCenterTile + 1, TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS) == -1) {
            goto err;
        }
    }

    _offsetDivTable = (int*)internal_malloc(sizeof(int) * gObjectsUpdateAreaHexSize);
    if (_offsetDivTable == nullptr) {
        goto err;
    }

    for (i = 0; i < gObjectsUpdateAreaHexSize; i++) {
        _offsetDivTable[i] = i / gObjectsUpdateAreaHexWidth;
    }

    _offsetModTable = (int*)internal_malloc(sizeof(int) * gObjectsUpdateAreaHexSize);
    if (_offsetModTable == nullptr) {
        goto err;
    }

    for (i = 0; i < gObjectsUpdateAreaHexSize; i++) {
        _offsetModTable[i] = i % gObjectsUpdateAreaHexWidth;
    }

    return 0;

err:
    _obj_offset_table_exit();

    return -1;
}

// 0x48CDA0 obj_offset_table_exit
static void _obj_offset_table_exit()
{
    if (_offsetModTable != nullptr) {
        internal_free(_offsetModTable);
        _offsetModTable = nullptr;
    }

    if (_offsetDivTable != nullptr) {
        internal_free(_offsetDivTable);
        _offsetDivTable = nullptr;
    }

    if (_offsetTable[1] != nullptr) {
        internal_free(_offsetTable[1]);
        _offsetTable[1] = nullptr;
    }

    if (_offsetTable[0] != nullptr) {
        internal_free(_offsetTable[0]);
        _offsetTable[0] = nullptr;
    }
}

// 0x48CE10 obj_order_table_init
static int _obj_order_table_init()
{
    if (_orderTable[0] != nullptr || _orderTable[1] != nullptr) {
        return -1;
    }

    _orderTable[0] = (int*)internal_malloc(sizeof(int) * gObjectsUpdateAreaHexSize);
    if (_orderTable[0] == nullptr) {
        goto err;
    }

    _orderTable[1] = (int*)internal_malloc(sizeof(int) * gObjectsUpdateAreaHexSize);
    if (_orderTable[1] == nullptr) {
        goto err;
    }

    for (int index = 0; index < gObjectsUpdateAreaHexSize; index++) {
        _orderTable[0][index] = index;
        _orderTable[1][index] = index;
    }

    qsort(_orderTable[0], gObjectsUpdateAreaHexSize, sizeof(int), _obj_order_comp_func_even);
    qsort(_orderTable[1], gObjectsUpdateAreaHexSize, sizeof(int), _obj_order_comp_func_odd);

    return 0;

err:

    // NOTE: Uninline.
    _obj_order_table_exit();

    return -1;
}

// 0x48CF20 obj_order_comp_func_even
static int _obj_order_comp_func_even(const void* a1, const void* a2)
{
    int v1 = *(int*)a1;
    int v2 = *(int*)a2;
    return _offsetTable[0][v1] - _offsetTable[0][v2];
}

// 0x48CF38 obj_order_comp_func_odd
static int _obj_order_comp_func_odd(const void* a1, const void* a2)
{
    int v1 = *(int*)a1;
    int v2 = *(int*)a2;
    return _offsetTable[1][v1] - _offsetTable[1][v2];
}

// NOTE: Inlined.
//
// 0x48CF50 obj_order_table_exit
static void _obj_order_table_exit()
{
    if (_orderTable[1] != nullptr) {
        internal_free(_orderTable[1]);
        _orderTable[1] = nullptr;
    }

    if (_orderTable[0] != nullptr) {
        internal_free(_orderTable[0]);
        _orderTable[0] = nullptr;
    }
}

// 0x48CF8C obj_render_table_init


// NOTE: Inlined.
//
// 0x48D000 obj_render_table_exit


// 0x48D020 obj_light_table_init
static void _obj_light_table_init()
{
    for (int s = 0; s < 2; s++) {
        int v4 = gCenterTile + s;
        for (Rotation i = ROTATION_FIRST; i < ROTATION_COUNT; i++) {
            int v15 = 8;
            int* p = _light_offsets[v4 & 1][i];
            for (int j = 0; j < 8; j++) {
                int tile = tileGetTileInDirection(v4, (i + 1) % ROTATION_COUNT, j);

                for (int m = 0; m < v15; m++) {
                    *p++ = tileGetTileInDirection(tile, i, m + 1) - v4;
                }

                v15--;
            }
        }
    }
}

// 0x48D1E4 obj_blend_table_init


// NOTE: Inlined.
//
// 0x48D2E8 obj_blend_table_exit


// 0x48D348 obj_save_obj
static int _obj_save_obj(File* stream, Object* object)
{
    if ((object->flags & OBJECT_NO_SAVE) != OBJECT_NONE) {
        return 0;
    }

    CritterCombatData* combatData = nullptr;
    Object* whoHitMe = nullptr;
    if (ProtoId(object).objectType() == OBJ_TYPE_CRITTER) {
        combatData = &(object->data.critter.combat);
        whoHitMe = objectPrepareWhoHitMeForSave(combatData);
    }

    if (objectWrite(object, stream) == -1) {
        return -1;
    }

    if (ProtoId(object).objectType() == OBJ_TYPE_CRITTER) {
        combatData->whoHitMe = whoHitMe;
    }

    Inventory* inventory = &(object->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);

        if (fileWriteInt32(stream, inventoryItem->quantity) == -1) {
            return -1;
        }

        if (_obj_save_obj(stream, inventoryItem->item) == -1) {
            return -1;
        }

        if ((inventoryItem->item->flags & OBJECT_NO_SAVE) != OBJECT_NONE) {
            return -1;
        }
    }

    return 0;
}

// 0x48D414 obj_load_obj
static int _obj_load_obj(File* stream, Object** objectPtr, MapElevation elevation, Object* owner)
{
    Object* obj;

    if (objectAllocate(&obj) == -1) {
        *objectPtr = nullptr;
        return -1;
    }

    if (objectRead(obj, stream) != 0) {
        *objectPtr = nullptr;
        return -1;
    }

    if (obj->sid != -1) {
        Script* script;
        if (scriptGetScript(obj->sid, &script) == -1) {
            obj->sid = -1;
        } else {
            script->owner = obj;
        }
    }

    _obj_fix_violence_settings(&(obj->fid));

    if (!FrmId(obj).exist()) {
        debugPrint("\nError: invalid object art fid: %u\n", obj->fid);
        // NOTE: Uninline.
        objectDeallocate(&obj);
        return -2;
    }

    if (elevation == ELEVATION_INVALID) {
        elevation = obj->elevation;
    } else {
        obj->elevation = elevation;
    }

    obj->owner = owner;

    Inventory* inventory = &(obj->data.inventory);
    if (inventory->length <= 0) {
        inventory->capacity = 0;
        inventory->items = nullptr;
        *objectPtr = obj;
        return 0;
    }

    InventoryItem* inventoryItems = inventory->items = (InventoryItem*)internal_malloc(sizeof(*inventoryItems) * inventory->capacity);
    if (inventoryItems == nullptr) {
        return -1;
    }

    for (int inventoryItemIndex = 0; inventoryItemIndex < inventory->length; inventoryItemIndex++) {
        InventoryItem* inventoryItem = &(inventoryItems[inventoryItemIndex]);
        if (fileReadInt32(stream, &(inventoryItem->quantity)) != 0) {
            return -1;
        }

        if (_obj_load_obj(stream, &(inventoryItem->item), elevation, obj) != 0) {
            return -1;
        }
    }

    *objectPtr = obj;

    return 0;
}

// obj_save_dude
// 0x48D59C obj_save_dude
int _obj_save_dude(File* stream)
{
    int field_78 = gDude->sid;

    gDude->flags &= ~OBJECT_NO_SAVE;
    gDude->sid = -1;

    int rc = _obj_save_obj(stream, gDude);

    gDude->sid = field_78;
    gDude->flags |= OBJECT_NO_SAVE;

    if (fileWriteInt32(stream, gCenterTile) == -1) {
        fileClose(stream);
        return -1;
    }

    return rc;
}

// obj_load_dude
// 0x48D600 obj_load_dude
int _obj_load_dude(File* stream)
{
    int savedTile = gDude->tile;
    MapElevation savedElevation = gDude->elevation;
    Rotation savedRotation = gDude->rotation;
    int savedOid = gDude->id;

    scriptsClearDudeScript();

    Object* temp = nullptr;
    int rc = _obj_load_obj(stream, &temp, ELEVATION_INVALID, nullptr);
    if (rc == -1 || temp == nullptr) {
        gDude->tile = savedTile;
        gDude->elevation = savedElevation;
        gDude->rotation = savedRotation;
        gDude->id = savedOid;
        scriptsSetDudeScript();
        return -1;
    }

    // Free the current dude's carried inventory BEFORE the memcpy overwrites the
    // data.inventory pointer (APPLYBLOB_TEARDOWN_PLAN step 1). gDude's items live
    // only inside its Inventory (never in tile buckets), so the mapLoad → _obj_remove_all
    // that precedes this on a co-op rebaseline SKIPS them — the memcpy below would then
    // clobber the items[] pointer and leak the whole array (+ every carried item) every
    // rebaseline. No-op on the first load (empty inventory) and on the single-player load
    // path (the in-progress inventory is being replaced by the save's anyway); on the
    // client _net is already cleared this teardown, so freeing these items strands no
    // stale netId map entry.
    _obj_inven_free(&(gDude->data.inventory));

    memcpy(gDude, temp, sizeof(*gDude));

    if (serverLoopActive()) {
        gDude->netId = 1;
    }

    gDude->flags |= OBJECT_NO_SAVE;

    scriptsClearDudeScript();

    gDude->id = savedOid;

    scriptsSetDudeScript();

    int newTile = gDude->tile;
    gDude->tile = savedTile;

    MapElevation newElevation = gDude->elevation;
    gDude->elevation = savedElevation;

    Rotation newRotation = gDude->rotation;
    gDude->rotation = savedRotation;

    scriptsSetDudeScript();

    if (rc != -1) {
        objectSetLocation(gDude, newTile, newElevation, nullptr);
        objectSetRotation(gDude, newRotation, nullptr);
    }

    // Set ownership of inventory items from temporary instance to dude.
    Inventory* inventory = &(gDude->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        inventoryItem->item->owner = gDude;
    }

    // Dude has claimed ownership of items in temporary instance's inventory.
    // We don't need object's dealloc routine to remove these items from the
    // game, so simply nullify temporary inventory as if nothing was there.
    Inventory* tempInventory = &(temp->data.inventory);
    tempInventory->length = 0;
    tempInventory->capacity = 0;
    tempInventory->items = nullptr;

    temp->flags &= ~OBJECT_NO_REMOVE;

    if (objectDestroy(temp, nullptr) == -1) {
        debugPrint("\nError: obj_load_dude: Can't destroy temp object!\n");
    }

    inventoryResetDude();

    int tile;
    if (fileReadInt32(stream, &tile) == -1) {
        fileClose(stream);
        return -1;
    }

    tileSetCenter(tile, TILE_SET_CENTER_REFRESH_WINDOW | TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);

    return rc;
}

// 0x48D778 obj_create_object
static int objectAllocate(Object** objectPtr)
{
    if (objectPtr == nullptr) {
        return -1;
    }

    Object* object = *objectPtr = (Object*)internal_malloc(sizeof(Object));
    if (object == nullptr) {
        return -1;
    }

    memset(object, 0, sizeof(Object));

    object->id = -1;
    object->tile = -1;
    object->cid = -1;
    object->outline = OUTLINE_TYPE_NONE;
    object->pid = ProtoId::kEmptyPid;
    object->sid = -1;
    object->owner = nullptr;
    object->scriptIndex = -1;
    object->netId = 0;

    return 0;
}

// NOTE: Inlined.
//
// 0x48D7F8 obj_destroy_object
static void objectDeallocate(Object** objectPtr)
{
    if (objectPtr == nullptr) {
        return;
    }

    if (*objectPtr == nullptr) {
        return;
    }

    internal_free(*objectPtr);

    *objectPtr = nullptr;
}

// NOTE: Inlined.
//
// 0x48D818 obj_create_object_node
static int objectListNodeCreate(ObjectListNode** nodePtr)
{
    if (nodePtr == nullptr) {
        return -1;
    }

    ObjectListNode* node = *nodePtr = (ObjectListNode*)internal_malloc(sizeof(*node));
    if (node == nullptr) {
        return -1;
    }

    node->obj = nullptr;
    node->next = nullptr;

    return 0;
}

// NOTE: Inlined.
//
// 0x48D84C obj_destroy_object_node
static void objectListNodeDestroy(ObjectListNode** nodePtr)
{
    if (nodePtr == nullptr) {
        return;
    }

    if (*nodePtr == nullptr) {
        return;
    }

    internal_free(*nodePtr);

    *nodePtr = nullptr;
}

// 0x48D86C obj_node_ptr
static int objectGetListNode(Object* object, ObjectListNode** nodePtr, ObjectListNode** previousNodePtr)
{
    if (object == nullptr) {
        return -1;
    }

    if (nodePtr == nullptr) {
        return -1;
    }

    int tile = object->tile;
    if (tile != -1) {
        *nodePtr = gObjectListHeadByTile[tile];
    } else {
        *nodePtr = gObjectListHead;
    }

    if (previousNodePtr != nullptr) {
        *previousNodePtr = nullptr;
        while (*nodePtr != nullptr) {
            if (object == (*nodePtr)->obj) {
                break;
            }

            *previousNodePtr = *nodePtr;

            *nodePtr = (*nodePtr)->next;
        }
    } else {
        while (*nodePtr != nullptr) {
            if (object == (*nodePtr)->obj) {
                break;
            }

            *nodePtr = (*nodePtr)->next;
        }
    }

    if (*nodePtr != nullptr) {
        return 0;
    }

    return -1;
}

// 0x48D8E8 obj_insert
static void _obj_insert(ObjectListNode* objectListNode)
{
    ObjectListNode** objectListNodePtr;

    if (objectListNode == nullptr) {
        return;
    }

    if (objectListNode->obj->tile == -1) {
        objectListNodePtr = &gObjectListHead;
    } else {
        Art* art = nullptr;
        CacheEntry* cacheHandle = nullptr;

        objectListNodePtr = &(gObjectListHeadByTile[objectListNode->obj->tile]);

        while (*objectListNodePtr != nullptr) {
            Object* obj = (*objectListNodePtr)->obj;
            if (obj->elevation > objectListNode->obj->elevation) {
                break;
            }

            if (obj->elevation == objectListNode->obj->elevation) {
                if ((obj->flags & OBJECT_FLAT) == OBJECT_NONE && (objectListNode->obj->flags & OBJECT_FLAT) != OBJECT_NONE) {
                    break;
                }

                if ((obj->flags & OBJECT_FLAT) == (objectListNode->obj->flags & OBJECT_FLAT)) {
                    bool v11 = false;
                    CacheEntry* a2;
                    Art* v12 = artLock(FrmId(obj), &a2);
                    if (v12 != nullptr) {

                        if (art == nullptr) {
                            art = artLock(FrmId(objectListNode->obj), &cacheHandle);
                        }

                        // TODO: Incomplete.

                        artUnlock(a2);

                        if (v11) {
                            break;
                        }
                    }
                }
            }

            objectListNodePtr = &((*objectListNodePtr)->next);
        }

        if (art != nullptr) {
            artUnlock(cacheHandle);
        }
    }

    objectListNode->next = *objectListNodePtr;
    *objectListNodePtr = objectListNode;
}

// 0x48DA58 obj_remove
static int _obj_remove(ObjectListNode* a1, ObjectListNode* a2)
{
    if (a1->obj == nullptr) {
        return -1;
    }

    if ((a1->obj->flags & OBJECT_NO_REMOVE) != OBJECT_NONE) {
        return -1;
    }

    _obj_inven_free(&(a1->obj->data.inventory));

    if (a1->obj->sid != -1) {
        scriptExecProc(a1->obj->sid, SCRIPT_PROC_DESTROY);
        scriptRemove(a1->obj->sid);
    }

    aiRemoveBurstDisabled(a1->obj);

    if (a1 != a2) {
        if (a2 != nullptr) {
            a2->next = a1->next;
        } else {
            int tile = a1->obj->tile;
            if (tile == -1) {
                gObjectListHead = gObjectListHead->next;
            } else {
                gObjectListHeadByTile[tile] = gObjectListHeadByTile[tile]->next;
            }
        }
    }

    // NOTE: Uninline.
    objectDeallocate(&(a1->obj));

    // NOTE: Uninline.
    objectListNodeDestroy(&a1);

    return 0;
}

// 0x48DB28 obj_connect_to_tile
static int _obj_connect_to_tile(ObjectListNode* node, int tile, MapElevation elevation, Rect* rect)
{
    if (node == nullptr) {
        return -1;
    }

    if (!hexGridTileIsValid(tile)) {
        return -1;
    }

    if (!elevationIsValid(elevation)) {
        return -1;
    }

    node->obj->tile = tile;
    node->obj->elevation = elevation;
    node->obj->x = 0;
    node->obj->y = 0;
    node->obj->owner = nullptr;

    _obj_insert(node);

    if (_obj_adjust_light(node->obj, 0, rect) == -1) {
        if (rect != nullptr) {
            objectGetRect(node->obj, rect);
        }
    }

    return 0;
}

// 0x48DC28 obj_adjust_light
static int _obj_adjust_light(Object* obj, int a2, Rect* rect)
{
    if (obj == nullptr) {
        return -1;
    }

    if (obj->lightIntensity <= 0) {
        return -1;
    }

    if ((obj->flags & OBJECT_HIDDEN) != OBJECT_NONE) {
        return -1;
    }

    if ((obj->flags & OBJECT_LIGHTING) == OBJECT_NONE) {
        return -1;
    }

    if (!hexGridTileIsValid(obj->tile)) {
        return -1;
    }

    AdjustLightIntensityProc* adjustLightIntensity = a2 ? lightDecreaseTileIntensity : lightIncreaseTileIntensity;
    adjustLightIntensity(obj->elevation, obj->tile, obj->lightIntensity);

    Rect objectRect;
    objectGetRect(obj, &objectRect);

    if (obj->lightDistance > 8) {
        obj->lightDistance = 8;
    }

    if (obj->lightIntensity > 65536) {
        obj->lightIntensity = 65536;
    }

    int(*v70)[36] = _light_offsets[obj->tile & 1];
    int v7 = (obj->lightIntensity - 655) / (obj->lightDistance + 1);
    int v28[36];
    v28[0] = obj->lightIntensity - v7;
    v28[1] = v28[0] - v7;
    v28[8] = v28[0] - v7;
    v28[2] = v28[0] - v7 - v7;
    v28[9] = v28[2];
    v28[15] = v28[0] - v7 - v7;
    v28[3] = v28[2] - v7;
    v28[10] = v28[2] - v7;
    v28[16] = v28[2] - v7;
    v28[21] = v28[2] - v7;
    v28[4] = v28[2] - v7 - v7;
    v28[11] = v28[4];
    v28[17] = v28[2] - v7 - v7;
    v28[22] = v28[2] - v7 - v7;
    v28[26] = v28[2] - v7 - v7;
    v28[5] = v28[4] - v7;
    v28[12] = v28[4] - v7;
    v28[18] = v28[4] - v7;
    v28[23] = v28[4] - v7;
    v28[27] = v28[4] - v7;
    v28[30] = v28[4] - v7;
    v28[6] = v28[4] - v7 - v7;
    v28[13] = v28[6];
    v28[19] = v28[4] - v7 - v7;
    v28[24] = v28[4] - v7 - v7;
    v28[28] = v28[4] - v7 - v7;
    v28[31] = v28[4] - v7 - v7;
    v28[33] = v28[4] - v7 - v7;
    v28[7] = v28[6] - v7;
    v28[14] = v28[6] - v7;
    v28[20] = v28[6] - v7;
    v28[25] = v28[6] - v7;
    v28[29] = v28[6] - v7;
    v28[32] = v28[6] - v7;
    v28[34] = v28[6] - v7;
    v28[35] = v28[6] - v7;

    for (int index = 0; index < 36; index++) {
        if (obj->lightDistance >= _light_distance[index]) {
            for (Rotation rotation = ROTATION_FIRST; rotation < ROTATION_COUNT; rotation++) {
                int v14;
                int nextRotation = (rotation + 1) % ROTATION_COUNT;
                int eax;
                int edx;
                int ebx;
                int esi;
                int edi;
                switch (index) {
                case 0:
                    v14 = 0;
                    break;
                case 1:
                    v14 = _light_blocked[rotation][0];
                    break;
                case 2:
                    v14 = _light_blocked[rotation][1];
                    break;
                case 3:
                    v14 = _light_blocked[rotation][2];
                    break;
                case 4:
                    v14 = _light_blocked[rotation][3];
                    break;
                case 5:
                    v14 = _light_blocked[rotation][4];
                    break;
                case 6:
                    v14 = _light_blocked[rotation][5];
                    break;
                case 7:
                    v14 = _light_blocked[rotation][6];
                    break;
                case 8:
                    v14 = _light_blocked[rotation][0] & _light_blocked[nextRotation][0];
                    break;
                case 9:
                    v14 = _light_blocked[rotation][1] & _light_blocked[rotation][8];
                    break;
                case 10:
                    v14 = _light_blocked[rotation][2] & _light_blocked[rotation][9];
                    break;
                case 11:
                    v14 = _light_blocked[rotation][3] & _light_blocked[rotation][10];
                    break;
                case 12:
                    v14 = _light_blocked[rotation][4] & _light_blocked[rotation][11];
                    break;
                case 13:
                    v14 = _light_blocked[rotation][5] & _light_blocked[rotation][12];
                    break;
                case 14:
                    v14 = _light_blocked[rotation][6] & _light_blocked[rotation][13];
                    break;
                case 15:
                    v14 = _light_blocked[rotation][8] & _light_blocked[nextRotation][1];
                    break;
                case 16:
                    v14 = _light_blocked[rotation][8] | (_light_blocked[rotation][9] & _light_blocked[rotation][15]);
                    break;
                case 17:
                    edx = _light_blocked[rotation][9];
                    edx |= _light_blocked[rotation][10];
                    ebx = _light_blocked[rotation][8];
                    esi = _light_blocked[rotation][16];
                    ebx &= edx;
                    edx &= esi;
                    edi = _light_blocked[rotation][15];
                    ebx |= edx;
                    edx = _light_blocked[rotation][10];
                    eax = _light_blocked[rotation][9];
                    edx |= edi;
                    eax &= edx;
                    v14 = ebx | eax;
                    break;
                case 18:
                    edx = _light_blocked[rotation][0];
                    ebx = _light_blocked[rotation][9];
                    esi = _light_blocked[rotation][10];
                    edx |= ebx;
                    edi = _light_blocked[rotation][11];
                    edx |= esi;
                    ebx = _light_blocked[rotation][17];
                    edx |= edi;
                    ebx &= edx;
                    edx = esi;
                    esi = _light_blocked[rotation][16];
                    edi = _light_blocked[rotation][9];
                    edx &= esi;
                    edx |= edi;
                    edx |= ebx;
                    v14 = edx;
                    break;
                case 19:
                    edx = _light_blocked[rotation][17];
                    edi = _light_blocked[rotation][18];
                    ebx = _light_blocked[rotation][11];
                    edx |= edi;
                    esi = _light_blocked[rotation][10];
                    ebx &= edx;
                    edx = _light_blocked[rotation][9];
                    edx |= esi;
                    ebx |= edx;
                    edx = _light_blocked[rotation][12];
                    edx &= edi;
                    ebx |= edx;
                    v14 = ebx;
                    break;
                case 20:
                    edx = _light_blocked[rotation][2];
                    esi = _light_blocked[rotation][11];
                    edi = _light_blocked[rotation][12];
                    ebx = _light_blocked[rotation][8];
                    edx |= esi;
                    esi = _light_blocked[rotation][9];
                    edx |= edi;
                    edi = _light_blocked[rotation][10];
                    ebx &= edx;
                    edx &= esi;
                    esi = _light_blocked[rotation][17];
                    ebx |= edx;
                    edx = _light_blocked[rotation][16];
                    ebx |= edi;
                    edi = _light_blocked[rotation][18];
                    edx |= esi;
                    esi = _light_blocked[rotation][19];
                    edx |= edi;
                    eax = _light_blocked[rotation][11];
                    edx |= esi;
                    eax &= edx;
                    ebx |= eax;
                    v14 = ebx;
                    break;
                case 21:
                    v14 = (_light_blocked[rotation][8] & _light_blocked[nextRotation][1])
                        | (_light_blocked[rotation][15] & _light_blocked[nextRotation][2]);
                    break;
                case 22:
                    edx = _light_blocked[nextRotation][1];
                    ebx = _light_blocked[rotation][15];
                    esi = _light_blocked[rotation][21];
                    edx |= ebx;
                    ebx = _light_blocked[rotation][8];
                    edx |= esi;
                    ebx &= edx;
                    edx = _light_blocked[rotation][9];
                    edi = esi;
                    edx |= esi;
                    esi = _light_blocked[rotation][15];
                    edx &= esi;
                    ebx |= edx;
                    edx = esi;
                    esi = _light_blocked[rotation][16];
                    edx |= edi;
                    edx &= esi;
                    ebx |= edx;
                    v14 = ebx;
                    break;
                case 23:
                    edx = _light_blocked[rotation][3];
                    ebx = _light_blocked[rotation][16];
                    esi = _light_blocked[rotation][15];
                    ebx |= edx;
                    edx = _light_blocked[rotation][9];
                    edx &= esi;
                    edi = _light_blocked[rotation][22];
                    ebx |= edx;
                    edx = _light_blocked[rotation][17];
                    edx &= edi;
                    ebx |= edx;
                    v14 = ebx;
                    break;
                case 24:
                    edx = _light_blocked[rotation][0];
                    edi = _light_blocked[rotation][9];
                    ebx = _light_blocked[rotation][10];
                    edx |= edi;
                    esi = _light_blocked[rotation][17];
                    edx |= ebx;
                    edi = _light_blocked[rotation][18];
                    edx |= esi;
                    ebx = _light_blocked[rotation][16];
                    edx |= edi;
                    esi = _light_blocked[rotation][16];
                    ebx &= edx;
                    edx = _light_blocked[rotation][15];
                    edi = _light_blocked[rotation][23];
                    edx |= esi;
                    esi = _light_blocked[rotation][9];
                    edx |= edi;
                    edi = _light_blocked[rotation][8];
                    edx &= esi;
                    edx |= edi;
                    esi = _light_blocked[rotation][22];
                    ebx |= edx;
                    edx = _light_blocked[rotation][15];
                    edi = _light_blocked[rotation][23];
                    edx |= esi;
                    esi = _light_blocked[rotation][17];
                    edx |= edi;
                    edx &= esi;
                    ebx |= edx;
                    edx = _light_blocked[rotation][18];
                    edx &= edi;
                    ebx |= edx;
                    v14 = ebx;
                    break;
                case 25:
                    edx = _light_blocked[rotation][8];
                    edi = _light_blocked[rotation][15];
                    ebx = _light_blocked[rotation][16];
                    edx |= edi;
                    esi = _light_blocked[rotation][23];
                    edx |= ebx;
                    edi = _light_blocked[rotation][24];
                    edx |= esi;
                    ebx = _light_blocked[rotation][9];
                    edx |= edi;
                    esi = _light_blocked[rotation][1];
                    ebx &= edx;
                    edx = _light_blocked[rotation][8];
                    edx &= esi;
                    edi = _light_blocked[rotation][16];
                    ebx |= edx;
                    edx = _light_blocked[rotation][8];
                    esi = _light_blocked[rotation][17];
                    edx |= edi;
                    edi = _light_blocked[rotation][24];
                    esi |= edx;
                    esi |= edi;
                    esi &= _light_blocked[rotation][10];
                    edi = _light_blocked[rotation][23];
                    ebx |= esi;
                    esi = _light_blocked[rotation][17];
                    edx |= edi;
                    ebx |= esi;
                    esi = _light_blocked[rotation][24];
                    edi = _light_blocked[rotation][18];
                    edx |= esi;
                    edx &= edi;
                    esi = _light_blocked[rotation][19];
                    ebx |= edx;
                    edx = _light_blocked[rotation][0];
                    eax = _light_blocked[rotation][24];
                    edx |= esi;
                    eax &= edx;
                    ebx |= eax;
                    v14 = ebx;
                    break;
                case 26:
                    ebx = _light_blocked[rotation][8];
                    esi = _light_blocked[nextRotation][1];
                    edi = _light_blocked[nextRotation][2];
                    esi &= ebx;
                    ebx = _light_blocked[rotation][15];
                    ebx &= edi;
                    eax = _light_blocked[rotation][21];
                    ebx |= esi;
                    eax &= _light_blocked[nextRotation][3];
                    ebx |= eax;
                    v14 = ebx;
                    break;
                case 27:
                    edx = _light_blocked[nextRotation][0];
                    edi = _light_blocked[rotation][15];
                    esi = _light_blocked[rotation][21];
                    edx |= edi;
                    edi = _light_blocked[rotation][26];
                    edx |= esi;
                    esi = _light_blocked[rotation][22];
                    edx |= edi;
                    edi = _light_blocked[nextRotation][1];
                    esi &= edx;
                    edx = _light_blocked[rotation][8];
                    ebx = _light_blocked[rotation][15];
                    edx &= edi;
                    edx |= ebx;
                    edi = _light_blocked[rotation][16];
                    esi |= edx;
                    edx = _light_blocked[rotation][8];
                    eax = _light_blocked[rotation][21];
                    edx |= edi;
                    eax &= edx;
                    esi |= eax;
                    v14 = esi;
                    break;
                case 28:
                    ebx = _light_blocked[rotation][9];
                    edi = _light_blocked[rotation][16];
                    esi = _light_blocked[rotation][23];
                    edx = _light_blocked[nextRotation][0];
                    ebx |= edi;
                    edi = _light_blocked[rotation][15];
                    ebx |= esi;
                    esi = _light_blocked[rotation][8];
                    ebx &= edi;
                    edi = _light_blocked[rotation][21];
                    ebx |= esi;
                    esi = _light_blocked[rotation][22];
                    edx |= edi;
                    edi = _light_blocked[rotation][27];
                    edx |= esi;
                    esi = _light_blocked[rotation][16];
                    edx |= edi;
                    edx &= esi;
                    edi = _light_blocked[rotation][17];
                    ebx |= edx;
                    edx = _light_blocked[rotation][9];
                    esi = _light_blocked[rotation][23];
                    edx |= edi;
                    edi = _light_blocked[rotation][22];
                    edx |= esi;
                    edx &= edi;
                    ebx |= edx;
                    edx = esi;
                    edx &= _light_blocked[rotation][27];
                    ebx |= edx;
                    v14 = ebx;
                    break;
                case 29:
                    edx = _light_blocked[rotation][8];
                    edi = _light_blocked[rotation][16];
                    ebx = _light_blocked[rotation][23];
                    edx |= edi;
                    esi = _light_blocked[rotation][15];
                    ebx |= edx;
                    edx = _light_blocked[rotation][9];
                    edx &= esi;
                    edi = _light_blocked[rotation][22];
                    ebx |= edx;
                    edx = _light_blocked[rotation][17];
                    edx &= edi;
                    esi = _light_blocked[rotation][28];
                    ebx |= edx;
                    edx = _light_blocked[rotation][24];
                    edx &= esi;
                    ebx |= edx;
                    v14 = ebx;
                    break;
                case 30:
                    ebx = _light_blocked[rotation][8];
                    esi = _light_blocked[nextRotation][1];
                    edi = _light_blocked[nextRotation][2];
                    esi &= ebx;
                    ebx = _light_blocked[rotation][15];
                    ebx &= edi;
                    edi = _light_blocked[nextRotation][3];
                    esi |= ebx;
                    ebx = _light_blocked[rotation][21];
                    ebx &= edi;
                    eax = _light_blocked[rotation][26];
                    ebx |= esi;
                    eax &= _light_blocked[nextRotation][4];
                    ebx |= eax;
                    v14 = ebx;
                    break;
                case 31:
                    edx = _light_blocked[rotation][8];
                    esi = _light_blocked[nextRotation][1];
                    edi = _light_blocked[rotation][15];
                    edx &= esi;
                    ebx = _light_blocked[rotation][21];
                    edx |= edi;
                    esi = _light_blocked[rotation][22];
                    ebx |= edx;
                    edx = _light_blocked[rotation][8];
                    edi = _light_blocked[rotation][27];
                    edx |= esi;
                    esi = _light_blocked[rotation][26];
                    edx |= edi;
                    edx &= esi;
                    ebx |= edx;
                    edx = edi;
                    edx &= _light_blocked[rotation][30];
                    ebx |= edx;
                    v14 = ebx;
                    break;
                case 32:
                    ebx = _light_blocked[rotation][8];
                    edi = _light_blocked[rotation][9];
                    esi = _light_blocked[rotation][16];
                    ebx |= edi;
                    edi = _light_blocked[rotation][23];
                    ebx |= esi;
                    esi = _light_blocked[rotation][28];
                    ebx |= edi;
                    ebx |= esi;
                    esi = _light_blocked[rotation][15];
                    esi &= ebx;
                    edx = _light_blocked[rotation][8];
                    edx &= _light_blocked[nextRotation][1];
                    ebx = _light_blocked[rotation][16];
                    esi |= edx;
                    edx = _light_blocked[rotation][8];
                    edx |= ebx;
                    ebx = _light_blocked[rotation][28];
                    edi = _light_blocked[rotation][21];
                    ebx |= edx;
                    ebx &= edi;
                    edi = _light_blocked[rotation][23];
                    ebx |= esi;
                    esi = _light_blocked[rotation][22];
                    edx |= edi;
                    ebx |= esi;
                    esi = _light_blocked[rotation][28];
                    edi = _light_blocked[rotation][27];
                    edx |= esi;
                    edx &= edi;
                    esi = _light_blocked[rotation][31];
                    ebx |= edx;
                    edx = _light_blocked[rotation][0];
                    edi = _light_blocked[rotation][28];
                    edx |= esi;
                    edx &= edi;
                    ebx |= edx;
                    v14 = ebx;
                    break;
                case 33:
                    esi = _light_blocked[rotation][8];
                    edi = _light_blocked[nextRotation][1];
                    ebx = _light_blocked[rotation][15];
                    esi &= edi;
                    ebx &= _light_blocked[nextRotation][2];
                    edi = _light_blocked[nextRotation][3];
                    esi |= ebx;
                    ebx = _light_blocked[rotation][21];
                    ebx &= edi;
                    edi = _light_blocked[nextRotation][4];
                    esi |= ebx;
                    ebx = _light_blocked[rotation][26];
                    ebx &= edi;
                    eax = _light_blocked[rotation][30];
                    ebx |= esi;
                    eax &= _light_blocked[nextRotation][5];
                    ebx |= eax;
                    v14 = ebx;
                    break;
                case 34:
                    edx = _light_blocked[nextRotation][2];
                    edi = _light_blocked[rotation][26];
                    ebx = _light_blocked[rotation][30];
                    edx |= edi;
                    esi = _light_blocked[rotation][15];
                    edx |= ebx;
                    ebx = _light_blocked[rotation][8];
                    edi = _light_blocked[rotation][21];
                    ebx &= edx;
                    edx &= esi;
                    esi = _light_blocked[rotation][22];
                    ebx |= edx;
                    edx = _light_blocked[rotation][16];
                    ebx |= edi;
                    edi = _light_blocked[rotation][27];
                    edx |= esi;
                    esi = _light_blocked[rotation][31];
                    edx |= edi;
                    eax = _light_blocked[rotation][26];
                    edx |= esi;
                    eax &= edx;
                    ebx |= eax;
                    v14 = ebx;
                    break;
                case 35:
                    ebx = _light_blocked[rotation][8];
                    esi = _light_blocked[nextRotation][1];
                    edi = _light_blocked[nextRotation][2];
                    esi &= ebx;
                    ebx = _light_blocked[rotation][15];
                    ebx &= edi;
                    edi = _light_blocked[nextRotation][3];
                    esi |= ebx;
                    ebx = _light_blocked[rotation][21];
                    ebx &= edi;
                    edi = _light_blocked[nextRotation][4];
                    esi |= ebx;
                    ebx = _light_blocked[rotation][26];
                    ebx &= edi;
                    edi = _light_blocked[nextRotation][5];
                    esi |= ebx;
                    ebx = _light_blocked[rotation][30];
                    ebx &= edi;
                    eax = _light_blocked[rotation][33];
                    ebx |= esi;
                    eax &= _light_blocked[nextRotation][6];
                    ebx |= eax;
                    v14 = ebx;
                    break;
                default:
                    assert(false && "Should be unreachable");
                }

                if (v14 == 0) {
                    // TODO: Check.
                    int tile = obj->tile + v70[rotation][index];
                    if (hexGridTileIsValid(tile)) {
                        bool v12 = true;

                        ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
                        while (objectListNode != nullptr) {
                            if ((objectListNode->obj->flags & OBJECT_HIDDEN) == OBJECT_NONE) {
                                if (objectListNode->obj->elevation > obj->elevation) {
                                    break;
                                }

                                if (objectListNode->obj->elevation == obj->elevation) {
                                    Rect v29;
                                    objectGetRect(objectListNode->obj, &v29);
                                    rectUnion(&objectRect, &v29, &objectRect);

                                    v14 = (objectListNode->obj->flags & OBJECT_LIGHT_THRU) == OBJECT_NONE;

                                    if (FrmId(objectListNode->obj).objectType() == OBJ_TYPE_WALL) {
                                        if ((objectListNode->obj->flags & OBJECT_FLAT) == OBJECT_NONE) {
                                            Proto* proto;
                                            protoGetProto(objectListNode->obj, &proto);
                                            if ((proto->wall.extendedFlags & PROTO_EXT_FLAG_HIDDEN) != PROTO_EXT_FLAG_NONE || (proto->wall.extendedFlags & PROTO_EXT_FLAG_EAST_CORNER) != PROTO_EXT_FLAG_NONE) {
                                                if (rotation != ROTATION_W
                                                    && rotation != ROTATION_NW
                                                    && (rotation != ROTATION_NE || index >= 8)
                                                    && (rotation != ROTATION_SW || index <= 15)) {
                                                    v12 = false;
                                                }
                                            } else if ((proto->wall.extendedFlags & PROTO_EXT_FLAG_NORTH_CORNER) != PROTO_EXT_FLAG_NONE) {
                                                if (rotation != ROTATION_NE && rotation != ROTATION_NW) {
                                                    v12 = false;
                                                }
                                            } else if ((proto->wall.extendedFlags & PROTO_EXT_FLAG_SOUTH_CORNER) != PROTO_EXT_FLAG_NONE) {
                                                if (rotation != ROTATION_NE
                                                    && rotation != ROTATION_E
                                                    && rotation != ROTATION_W
                                                    && rotation != ROTATION_NW
                                                    && (rotation != ROTATION_SW || index <= 15)) {
                                                    v12 = false;
                                                }
                                            } else {
                                                if (rotation != ROTATION_NE
                                                    && rotation != ROTATION_E
                                                    && (rotation != ROTATION_NW || index <= 7)) {
                                                    v12 = false;
                                                }
                                            }
                                        }
                                    } else {
                                        if (v14 && rotation >= ROTATION_E && rotation <= ROTATION_SW) {
                                            v12 = false;
                                        }
                                    }

                                    if (v14) {
                                        break;
                                    }
                                }
                            }
                            objectListNode = objectListNode->next;
                        }

                        if (v12) {
                            adjustLightIntensity(obj->elevation, tile, v28[index]);
                        }
                    }
                }

                _light_blocked[rotation][index] = v14;
            }
        }
    }

    if (rect != nullptr) {
        Rect* lightDistanceRect = &(_light_rect[obj->lightDistance]);
        memcpy(rect, lightDistanceRect, sizeof(*lightDistanceRect));

        int x;
        int y;
        tileToScreenXY(obj->tile, &x, &y);
        x += 16;
        y += 8;

        x -= rect->right / 2;
        y -= rect->bottom / 2;

        rectOffset(rect, x, y);
        rectUnion(rect, &objectRect, rect);
    }

    return 0;
}

// 0x48EABC obj_render_outline


// 0x48F1B0 obj_render_object


// Updates fid according to current violence level.
//
// 0x48FA14 obj_fix_violence_settings
void _obj_fix_violence_settings(int* fid)
{
    const FrmId frmId = FrmId(*fid);
    if (frmId.objectType() != OBJ_TYPE_CRITTER) {
        return;
    }

    bool shouldResetViolenceLevel = false;
    if (gViolenceLevel == VIOLENCE_LEVEL_INVALID) {
        gViolenceLevel = settings.preferences.violence_level;
        shouldResetViolenceLevel = true;
    }

    int start;
    int end;

    switch (gViolenceLevel) {
    case VIOLENCE_LEVEL_NONE:
        start = ANIM_BIG_HOLE_SF;
        end = ANIM_FALL_FRONT_BLOOD_SF;
        break;
    case VIOLENCE_LEVEL_MINIMAL:
        start = ANIM_BIG_HOLE_SF;
        end = ANIM_FIRE_DANCE_SF;
        break;
    case VIOLENCE_LEVEL_NORMAL:
        start = ANIM_BIG_HOLE_SF;
        end = ANIM_SLICED_IN_HALF_SF;
        break;
    default:
        // Do not replace anything.
        start = ANIM_COUNT + 1;
        end = ANIM_COUNT + 1;
        break;
    }

    AnimationType anim = frmId.animationType();
    if (anim >= start && anim <= end) {
        anim = (anim == ANIM_FALL_BACK_BLOOD_SF)
            ? ANIM_FALL_BACK_SF
            : ANIM_FALL_FRONT_SF;
        *fid = CritterFrmId(frmId.frameId<CritterFrameId>(), anim, frmId.weaponAnimation(), frmId.rotation()).fid();
    }

    if (shouldResetViolenceLevel) {
        gViolenceLevel = VIOLENCE_LEVEL_INVALID;
    }
}

// 0x48FB08 obj_preload_sort
static int _obj_preload_sort(const void* fid1, const void* fid2)
{
    const FrmId frmId1 = FrmId(*(int*)fid1);
    const FrmId frmId2 = FrmId(*(int*)fid2);

    const ObjectType objType1 = frmId1.objectType();
    const ObjectType objType2 = frmId2.objectType();

    if (!frmId1.valid() || !frmId2.valid()) {
        return objType1 - objType2;
    }

    constexpr size_t kCdOrderSize = (sizeof(_cd_order) / sizeof(_cd_order[0]));
    assert(objType1 < kCdOrderSize && objType2 < kCdOrderSize && "_obj_preload_sort(const void* fid1, const void* fid2) called object type higher than _cd_order size!");

    int cmp;
    if (objType1 < kCdOrderSize && objType2 < kCdOrderSize) {
        cmp = _cd_order[objType1] - _cd_order[objType2];
        if (cmp != 0) {
            return cmp;
        }
    } else {
        cmp = objType1 - objType2;
        if (cmp != 0) {
            return cmp;
        }
    }

    cmp = frmId1.frameId() - frmId2.frameId();
    if (cmp != 0) {
        return cmp;
    }

    cmp = static_cast<int>(frmId1.weaponAnimation()) - static_cast<int>(frmId2.weaponAnimation());
    if (cmp != 0) {
        return cmp;
    }

    cmp = frmId1.animationType() - frmId2.animationType();
    return cmp;
}

Object* objectTypedFindById(int id, ObjectType type)
{
    Object* obj = objectFindFirst();
    while (obj != nullptr) {
        if (obj->id == id && ProtoId(obj).objectType() == type) {
            return obj;
        }
        obj = objectFindNext();
    }

    return nullptr;
}

bool isExitGridAt(int tile, MapElevation elevation)
{
    ObjectListNode* objectListNode = gObjectListHeadByTile[tile];
    while (objectListNode != nullptr) {
        Object* obj = objectListNode->obj;
        if (obj->elevation == elevation) {
            if ((obj->flags & OBJECT_HIDDEN) == OBJECT_NONE) {
                if (isExitGridProtoId(obj)) {
                    return true;
                }
            }
        }
        objectListNode = objectListNode->next;
    }

    return false;
}
UniqueObject::UniqueObject(Object* ptr)
    : _ptr(ptr)
{
}
UniqueObject::~UniqueObject()
{
    if (_ptr) objectDestroy(_ptr, nullptr);
}
UniqueObject::UniqueObject(UniqueObject&& other) noexcept
    : _ptr(other._ptr)
{
    other._ptr = nullptr;
}
UniqueObject& UniqueObject::operator=(UniqueObject&& other) noexcept
{
    if (this != &other) {
        if (_ptr) objectDestroy(_ptr, nullptr);
        _ptr = other._ptr;
        other._ptr = nullptr;
    }
    return *this;
}
Object* UniqueObject::release()
{
    Object* p = _ptr;
    _ptr = nullptr;
    return p;
}
void UniqueObject::reset(Object* p)
{
    if (_ptr) objectDestroy(_ptr, nullptr);
    _ptr = p;
}

int objectCreateWithFrmIdProtoId(UniqueObject& obj, const FrmId& frmId, const ProtoId& protoId)
{
    Object* raw;
    int rc = objectCreateWithFrmIdProtoId(&raw, frmId, protoId);
    if (rc != -1) obj.reset(raw);
    return rc;
}



int objectReattach(Object* obj, int tile, MapElevation elevation)
{
    if (obj == nullptr) {
        return -1;
    }

    ObjectListNode* node;
    ObjectListNode* previousNode;
    if (objectGetListNode(obj, &node, &previousNode) != 0) {
        return _obj_connect(obj, tile, elevation, nullptr);
    }

    return objectSetLocation(obj, tile, elevation, nullptr);
}

Object* objectFindByNetId(int netId)
{
    if (netId <= 0) {
        return nullptr;
    }
    Object* obj = objectFindFirst();
    while (obj != nullptr) {
        if (obj->netId == netId) {
            return obj;
        }
        obj = objectFindNext();
    }

    return nullptr;
}

int _obj_save_player_actor(File* stream, Object* actor)
{
    if (actor == nullptr) {
        return -1;
    }

    int savedSid = actor->sid;
    ObjectFlags savedFlags = actor->flags;

    actor->flags &= ~OBJECT_NO_SAVE;
    actor->sid = -1;

    int rc = _obj_save_obj(stream, actor);

    actor->sid = savedSid;
    actor->flags = savedFlags;

    return rc;
}

void objectApplyWireFlags(Object* obj, unsigned int wireFlags)
{
    if (obj == nullptr) {
        return;
    }

    const unsigned int kLocalLifecycleFlags = OBJECT_NO_REMOVE | OBJECT_NO_SAVE;
    obj->flags = static_cast<ObjectFlags>((wireFlags & ~kLocalLifecycleFlags) | (obj->flags & kLocalLifecycleFlags));

    // Door openFlags come from the loaded map, while subsequent server changes
    // arrive as object flags. Keep both representations in sync, including on
    // close, so the viewer does not keep treating a previously open door as open.
    if (ProtoId(obj).objectType() == OBJ_TYPE_SCENERY) {
        Proto* proto = nullptr;
        if (protoGetProto(obj, &proto) == 0 && proto->scenery.type == SCENERY_TYPE_DOOR) {
            if ((obj->flags & OBJECT_OPEN_DOOR) == OBJECT_OPEN_DOOR) {
                obj->data.scenery.door.openFlags |= DOOR_FLAG_OPEN;
            } else {
                obj->data.scenery.door.openFlags &= ~DOOR_FLAG_OPEN;
            }
        }
    }
}

int _obj_load_player_actor(File* stream, Object** actorPtr)
{
    Object* obj = nullptr;
    if (_obj_load_obj(stream, &obj, MapElevation(-1), nullptr) != 0 || obj == nullptr) {
        if (actorPtr != nullptr) {
            *actorPtr = nullptr;
        }
        return -1;
    }

    obj->flags &= ~OBJECT_NO_REMOVE;
    obj->flags |= OBJECT_NO_SAVE;

    // ⚠ _obj_load_obj calls objectAllocate, NOT objectListNodeCreate: it hands
    // back a bare Object* that belongs to NO object list. Every list-aware
    // primitive fails CLOSED on such an object — objectSetLocation returns -1 at
    // its objectGetListNode call and does nothing at all, silently — so the actor
    // would render nowhere, never appear in the objectFindFirst walk (so the
    // decoder's netId map never learns it and every MOVE addressed to it is
    // dropped), and be invisible to the whole engine. Give it a node first, the
    // way objectCreateWithFidPid does: insert at tile -1 (the "not placed yet"
    // head list), then let objectSetLocation do the real placement with its
    // lighting/connect work.
    ObjectListNode* node;
    if (objectListNodeCreate(&node) == -1) {
        if (actorPtr != nullptr) {
            *actorPtr = nullptr;
        }
        return -1;
    }

    int tile = obj->tile;
    MapElevation elevation = obj->elevation;

    node->obj = obj;
    obj->tile = -1;
    _obj_insert(node);

    objectSetLocation(obj, tile, elevation, nullptr);
    objectSetRotation(obj, obj->rotation, nullptr);

    if (actorPtr != nullptr) {
        *actorPtr = obj;
    }

    return 0;
}

int objectNextNetId()
{
    return gNextNetId++;
}

int objectGetNextNetId()
{
    return gNextNetId;
}

void objectSetNextNetId(int netId)
{
    gNextNetId = netId;
}

static void objectAssignInventoryNetIds(Object* owner)
{
    Inventory* inv = &(owner->data.inventory);
    for (int i = 0; i < inv->length; ++i) {
        Object* item = inv->items[i].item;
        item->netId = objectNextNetId();
        if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
            objectAssignInventoryNetIds(item);
        }
    }
}

static void objectZeroInventoryNetIds(Object* owner)
{
    Inventory* inv = &(owner->data.inventory);
    for (int i = 0; i < inv->length; ++i) {
        Object* item = inv->items[i].item;
        item->netId = 0;
        if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
            objectZeroInventoryNetIds(item);
        }
    }
}

void objectAssignAllNetIds()
{
    objectSetNextNetId(1);

    // Player actors are numbered FIRST, in registry slot order, ahead of the
    // tile walk (MP_PROPOSAL.md Ch 5.1). With nothing registered this is exactly
    // the old "gDude then the walk" shape — playerActorCount() is 1 and slot 0
    // resolves to gDude.
    //
    // ⚠ Slot k does NOT get netId k+1: each actor's inventory is numbered
    // immediately after it. Never infer an actor's netId from its slot — read
    // the roster (EVENT_PLAYER_ROSTER), which is re-announced after every
    // baseline for exactly this reason.
    for (int slot = 0; slot < playerActorCount(); slot++) {
        Object* actor = playerActorAt(slot);
        if (actor != nullptr) {
            actor->netId = objectNextNetId();
            objectAssignInventoryNetIds(actor);
        }
    }

    Object* obj = objectFindFirst();
    while (obj != nullptr) {
        // Domain alignment (CLIENT_JOIN_DESIGN.md §C): the netId walk, the join
        // blob (objectSaveAll, object.cc:674), and the delta layer (objectIsSyncable,
        // object_delta.cc) must number the SAME object set, or a slot mismatch
        // shifts every netId after it (the client cannot then reproduce the walk).
        // OBJECT_NO_SAVE objects (cursors, the egg) are absent from the blob AND
        // excluded from deltas, so they take no netId slot. Player actors are
        // NO_SAVE but ARE primary actors: numbered first, above, and skipped here.
        // Recruited party members are ALSO NO_SAVE but ARE in the sync domain — the
        // join blob rides them in the map body under a prep bracket (mapSaveToStream)
        // and objectIsSyncable tracks their deltas, so they must take one inline slot
        // each here too, in the SAME tile-walk order the blob wrote them.
        if (!playerActorIs(obj) && ((obj->flags & OBJECT_NO_SAVE) == 0 || objectIsPartyMember(obj))) {
            obj->netId = objectNextNetId();
            objectAssignInventoryNetIds(obj);
        } else if (!playerActorIs(obj)) {
            // Skipped NO_SAVE object: zero its netId so exactly the syncable set
            // carries a nonzero netId after the walk. A NO_SAVE object that
            // SURVIVES a rebaseline (e.g. gEgg, which follows the dude across
            // maps) would otherwise retain a stale netId that could collide with
            // a syncable object's freshly-renumbered value (1..N) — the wire
            // would then LIE about which object a delta/move addresses. netId 0 =
            // "no object" (presenter_network.cc:97), so such an object's lifecycle
            // events are harmlessly skipped by the decoder. NO_SAVE objects that
            // spawn LATER still get a fresh wire netId at creation (object.cc:761).
            obj->netId = 0;
            objectZeroInventoryNetIds(obj);
        }
        obj = objectFindNext();
    }
}

} // namespace fallout
