#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "content_config.h"
#include "interpreter_extra.h"
#include "settings.h"
#include "sfall_script_hooks.h"
#include <stdlib.h>
#include "pres_record.h"
#include "inventory_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "inventory.h"
#include "item.h"

#include "actions.h"
#include "animation.h"
#include "art.h"
#include "barter_intent.h"
#include "client_barter.h"
#include "client_dialog.h"
#include "client_steal.h" // the viewer half of a server-owned steal session
#include "object_delta.h" // objectDeltaScan — a parked tick streams nothing on its own
#include "steal_intent.h"
#include "msg_channel.h"
#include "server_players.h"
#include "client_net.h" // clientViewerActive — inventory is view-only on the wire viewer (Slice 3)
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
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
#include "geometry.h"
#include "input.h"
#include "interface.h"
#include "kb.h"
#include "light.h"
#include "map.h"
#include "memory.h"
#include "message.h"
#include "mouse.h"
#include "object.h"
#include "party_member.h"
#include "perk.h"
#include "platform_compat.h"
#include "presenter.h"
#include "proto.h"
#include "proto_instance.h"
#include "random.h"
#include "reaction.h"
#include "scripts.h"
#include "server_loop.h"
#include "sfall_config.h"
#include "skill.h"
#include "stat.h"
#include "svga.h"
#include "text_font.h"
#include "tile.h"
#include "trait.h"
#include "window_manager.h"

// Client-side inventory UI seam extracted from inventory.cc (REWRITE_PLAN, TU
// split). Pure presentation/input: every modal inventory/loot/steal/barter/
// quantity window plus its rendering, cursor, and context-menu chrome. Core
// inventory.cc retains the SDL-free inventory data-structure and equip logic
// (critterGet*, inventoryEquip/inventoryUnequip, _inven_find_*, adjustCritterStatsOnArmorChange,
// _inven_reset_dude). The public UI entry points (inventoryOpen, inventoryOpen*,
// inventorySetTimer, inventoryGetTargetObject) stay declared in inventory.h
// since existing callers include that. The only seam is five shared file-
// statics that core reads/writes and this layer reads/writes; they are defined
// in inventory.cc, de-static'd, and viewed here through inventory_ui.h. This
// move is mechanical and replay-identical.

namespace fallout {

#define INVENTORY_WINDOW_X 80
#define INVENTORY_WINDOW_Y 0

#define INVENTORY_TRADE_WINDOW_X 80
#define INVENTORY_TRADE_WINDOW_Y 0
#define INVENTORY_TRADE_WINDOW_WIDTH 480
#define INVENTORY_TRADE_WINDOW_HEIGHT 180

#define INVENTORY_LARGE_SLOT_WIDTH 90
#define INVENTORY_LARGE_SLOT_HEIGHT 61

#define INVENTORY_SLOT_WIDTH 64
#define INVENTORY_SLOT_HEIGHT 48

#define INVENTORY_LEFT_HAND_SLOT_X 154
#define INVENTORY_LEFT_HAND_SLOT_Y 286
#define INVENTORY_LEFT_HAND_SLOT_MAX_X (INVENTORY_LEFT_HAND_SLOT_X + INVENTORY_LARGE_SLOT_WIDTH)
#define INVENTORY_LEFT_HAND_SLOT_MAX_Y (INVENTORY_LEFT_HAND_SLOT_Y + INVENTORY_LARGE_SLOT_HEIGHT)

#define INVENTORY_RIGHT_HAND_SLOT_X 245
#define INVENTORY_RIGHT_HAND_SLOT_Y 286
#define INVENTORY_RIGHT_HAND_SLOT_MAX_X (INVENTORY_RIGHT_HAND_SLOT_X + INVENTORY_LARGE_SLOT_WIDTH)
#define INVENTORY_RIGHT_HAND_SLOT_MAX_Y (INVENTORY_RIGHT_HAND_SLOT_Y + INVENTORY_LARGE_SLOT_HEIGHT)

#define INVENTORY_ARMOR_SLOT_X 154
#define INVENTORY_ARMOR_SLOT_Y 183
#define INVENTORY_ARMOR_SLOT_MAX_X (INVENTORY_ARMOR_SLOT_X + INVENTORY_LARGE_SLOT_WIDTH)
#define INVENTORY_ARMOR_SLOT_MAX_Y (INVENTORY_ARMOR_SLOT_Y + INVENTORY_LARGE_SLOT_HEIGHT)

#define INVENTORY_TRADE_SCROLLER_Y 30
#define INVENTORY_TRADE_INNER_SCROLLER_Y 20

#define INVENTORY_TRADE_LEFT_SCROLLER_X 29
#define INVENTORY_TRADE_LEFT_SCROLLER_Y INVENTORY_TRADE_SCROLLER_Y

#define INVENTORY_TRADE_RIGHT_SCROLLER_X 388
#define INVENTORY_TRADE_RIGHT_SCROLLER_Y INVENTORY_TRADE_SCROLLER_Y

#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_X 165
#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y INVENTORY_TRADE_INNER_SCROLLER_Y

#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X 250
#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y INVENTORY_TRADE_INNER_SCROLLER_Y

#define INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_X 0
#define INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y 10
#define INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_MAX_X (INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_X 165
#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y 10
#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_MAX_X (INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_X 250
#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y 10
#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_MAX_X (INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_X 395
#define INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y 10
#define INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_MAX_X (INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_LOOT_LEFT_SCROLLER_X 180
#define INVENTORY_LOOT_LEFT_SCROLLER_Y 37
#define INVENTORY_LOOT_LEFT_SCROLLER_MAX_X (INVENTORY_LOOT_LEFT_SCROLLER_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_LOOT_RIGHT_SCROLLER_X 297
#define INVENTORY_LOOT_RIGHT_SCROLLER_Y 37
#define INVENTORY_LOOT_RIGHT_SCROLLER_MAX_X (INVENTORY_LOOT_RIGHT_SCROLLER_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_SCROLLER_X 46
#define INVENTORY_SCROLLER_Y 35
#define INVENTORY_SCROLLER_MAX_X (INVENTORY_SCROLLER_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_BODY_VIEW_WIDTH 60
#define INVENTORY_BODY_VIEW_HEIGHT 100

#define INVENTORY_PC_BODY_VIEW_X 176
#define INVENTORY_PC_BODY_VIEW_Y 37
#define INVENTORY_PC_BODY_VIEW_MAX_X (INVENTORY_PC_BODY_VIEW_X + INVENTORY_BODY_VIEW_WIDTH)
#define INVENTORY_PC_BODY_VIEW_MAX_Y (INVENTORY_PC_BODY_VIEW_Y + INVENTORY_BODY_VIEW_HEIGHT)

#define INVENTORY_LOOT_RIGHT_BODY_VIEW_X 422
#define INVENTORY_LOOT_RIGHT_BODY_VIEW_Y 35

#define INVENTORY_LOOT_LEFT_BODY_VIEW_X 44
#define INVENTORY_LOOT_LEFT_BODY_VIEW_Y 35

#define INVENTORY_SUMMARY_X 298
#define INVENTORY_SUMMARY_Y 45
#define INVENTORY_SUMMARY_MAX_X 440

#define INVENTORY_WINDOW_WIDTH 499
#define INVENTORY_USE_ON_WINDOW_WIDTH 292
#define INVENTORY_LOOT_WINDOW_WIDTH 537
#define INVENTORY_TRADE_WINDOW_WIDTH 480
#define INVENTORY_TIMER_WINDOW_WIDTH 259

#define INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH 640
#define INVENTORY_TRADE_BACKGROUND_WINDOW_HEIGHT 480
#define INVENTORY_TRADE_WINDOW_OFFSET ((INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH - INVENTORY_TRADE_WINDOW_WIDTH) / 2)

#define INVENTORY_SLOT_PADDING 4

#define INVENTORY_SCROLLER_X_PAD (INVENTORY_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_SCROLLER_Y_PAD (INVENTORY_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_LOOT_LEFT_SCROLLER_X_PAD (INVENTORY_LOOT_LEFT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_LOOT_LEFT_SCROLLER_Y_PAD (INVENTORY_LOOT_LEFT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_LOOT_RIGHT_SCROLLER_X_PAD (INVENTORY_LOOT_RIGHT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_LOOT_RIGHT_SCROLLER_Y_PAD (INVENTORY_LOOT_RIGHT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_TRADE_LEFT_SCROLLER_X_PAD (INVENTORY_TRADE_LEFT_SCROLLER_Y + INVENTORY_SLOT_PADDING)
#define INVENTORY_TRADE_LEFT_SCROLLER_Y_PAD (INVENTORY_TRADE_LEFT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_TRADE_RIGHT_SCROLLER_X_PAD (INVENTORY_TRADE_RIGHT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_TRADE_RIGHT_SCROLLER_Y_PAD (INVENTORY_TRADE_RIGHT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD (INVENTORY_TRADE_INNER_LEFT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD (INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD (INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD (INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_SLOT_WIDTH_PAD (INVENTORY_SLOT_WIDTH - INVENTORY_SLOT_PADDING * 2)
#define INVENTORY_SLOT_HEIGHT_PAD (INVENTORY_SLOT_HEIGHT - INVENTORY_SLOT_PADDING * 2)

#define INVENTORY_NORMAL_WINDOW_PC_ROTATION_DELAY (1000U / ROTATION_COUNT)
#define INVENTORY_FRM_COUNT 12

typedef enum InventoryArrowFrm {
    INVENTORY_ARROW_FRM_LEFT_ARROW_UP,
    INVENTORY_ARROW_FRM_LEFT_ARROW_DOWN,
    INVENTORY_ARROW_FRM_RIGHT_ARROW_UP,
    INVENTORY_ARROW_FRM_RIGHT_ARROW_DOWN,
    INVENTORY_ARROW_FRM_COUNT,
} InventoryArrowFrm;

typedef enum InventoryWindowCursor {
    INVENTORY_WINDOW_CURSOR_HAND,
    INVENTORY_WINDOW_CURSOR_ARROW,
    INVENTORY_WINDOW_CURSOR_PICK,
    INVENTORY_WINDOW_CURSOR_MENU,
    INVENTORY_WINDOW_CURSOR_BLANK,
    INVENTORY_WINDOW_CURSOR_COUNT,
} InventoryWindowCursor;

typedef enum InventoryWindowType {
    // Normal inventory window with quick character sheet.
    INVENTORY_WINDOW_TYPE_NORMAL,

    // Narrow inventory window with just an item scroller that's shown when
    // a "Use item on" is selected from context menu.
    INVENTORY_WINDOW_TYPE_USE_ITEM_ON,

    // Looting/strealing interface.
    INVENTORY_WINDOW_TYPE_LOOT,

    // Barter interface.
    INVENTORY_WINDOW_TYPE_TRADE,

    // Supplementary "Move items" window. Used to set quantity of items when
    // moving items between inventories.
    INVENTORY_WINDOW_TYPE_MOVE_ITEMS,

    // Supplementary "Set timer" window. Internally it's implemented as "Move
    // items" window but with timer overlay and slightly different adjustment
    // mechanics.
    INVENTORY_WINDOW_TYPE_SET_TIMER,

    INVENTORY_WINDOW_TYPE_COUNT,
} InventoryWindowType;

typedef struct InventoryWindowConfiguration {
    InterfaceFrameId frmId; // artId
    int width;
    int height;
    int x;
    int y;
} InventoryWindowDescription;

typedef struct InventoryCursorData {
    Art* frm;
    unsigned char* frmData;
    int width;
    int height;
    int offsetX;
    int offsetY;
    CacheEntry* frmHandle;
} InventoryCursorData;

typedef enum InventoryMoveResult {
    INVENTORY_MOVE_RESULT_FAILED,
    INVENTORY_MOVE_RESULT_CAUGHT_STEALING,
    INVENTORY_MOVE_RESULT_SUCCESS,
} InventoryMoveResult;

#define INVENTORY_BARTER_LEFT_BODY_VIEW_X 18


#define INVENTORY_BARTER_LEFT_BODY_VIEW_Y 25


#define INVENTORY_SUMMARY_WIDTH 154


#define INVENTORY_SUMMARY_HEIGHT 190


#define INVENTORY_LOOT_WINDOW_WIDTH_EXPANDED 673


#define INVENTORY_LOOT_CRITTER_TOGGLE_Y (INVENTORY_LOOT_LEFT_BODY_VIEW_Y + INVENTORY_BODY_VIEW_HEIGHT + 24)


#define INVENTORY_ROWS 6


#define INVENTORY_HAND_RIGHT_KEY 2600


#define INVENTORY_HAND_LEFT_KEY 2601


#define INVENTORY_ARMOR_KEY 2602


#define PARTY_WEAPON_SLOT_KEY 2603


#define PARTY_ARMOR_SLOT_KEY 2604


typedef enum InventoryAmmoMoveResult {
    INVENTORY_AMMO_MOVE_RESULT_FAILED = -1,
    INVENTORY_AMMO_MOVE_RESULT_SUCCESS = 0,
    INVENTORY_AMMO_MOVE_RESULT_BLOCKED = 1,
} InventoryAmmoMoveResult;

typedef struct InventoryNormalLayout {
    int columns;
    int visibleSlots;
    int windowWidth;
    int windowHeight;
    int scrollerX;
    int scrollerY;
    int scrollerWidth;
    int scrollerHeight;
    int leftHandSlotX;
    int rightHandSlotX;
    int armorSlotX;
    int bodyViewX;
    int summaryX;
    int scrollButtonX;
    int doneButtonX;
} InventoryNormalLayout;

typedef struct InventoryLootLayout {
    int columns;
    int visibleSlots;
    int windowWidth;
    int windowHeight;
    int leftScrollerX;
    int leftScrollerY;
    int rightScrollerX;
    int rightScrollerY;
    int scrollerWidth;
    int scrollerHeight;
    int leftBodyViewX;
    int rightBodyViewX;
    int leftScrollButtonX;
    int rightScrollButtonX;
    int takeAllButtonX;
    int doneButtonX;
    int prevCritterButtonX;
    int nextCritterButtonX;
    int partySlotX;
    int partyArmorSlotY;
} InventoryLootLayout;

typedef struct InventoryScroller {
    Rect hitRect;
    int* offsetPtr;
    int step;
    int visibleSlots;
    int totalItems;
    int keyUp;
    int keyDown;
    int keyPageUp;
    int keyPageDown;
    int keyHome;
    int keyEnd;
    void (*redraw)(const InventoryScroller& scroller, void* context);
    void* redrawContext;
    bool refreshWindow;
} InventoryScroller;

typedef struct InventoryScrollerDisplayContext {
    int inventoryWindowType;
    Inventory* inventory;
} InventoryScrollerDisplayContext;

typedef struct InventoryScrollerBarterContext {
    int win;
    Object* playerTable;
    Object* bartererTable;
} InventoryScrollerBarterContext;

static int inventoryMessageListInit();
static int inventoryMessageListFree();
static bool _setup_inventory(int inventoryWindowType);
static void _exit_inventory(bool shouldEnableIso);
static void _display_inventory(int stackOffset, int draggedSlotIndex, int inventoryWindowType);
static void _display_target_inventory(int stackOffset, int dragSlotIndex, Inventory* inventory, int inventoryWindowType);
static void _display_inventory_info(Object* item, int quantity, unsigned char* dest, int pitch, bool isDragged);
static void _display_body(const FrmId& frmId, int inventoryWindowType);
static int inventoryCommonInit();
static void inventoryCommonFree();
static void inventorySetCursor(int cursor);
static void inventoryItemSlotOnMouseEnter(int btn, int keyCode);
static void inventoryItemSlotOnMouseExit(int btn, int keyCode);
static void _inven_update_lighting(Object* activeItem);
static void _inven_pickup(int keyCode, int indexOffset);
static void _switch_hand(Object* sourceItem, Object** targetSlot, Object** sourceSlot, int itemIndex);
static void _adjust_fid();
static void inventoryRenderSummary();
static int _inven_from_button(int keyCode, Object** outItem, Object*** outItemSlot, Object** outOwner);
static void inventoryRenderItemDescription(const char* string);
static void inventoryExamineItem(Object* critter, Object* item);
static void inventoryWindowOpenContextMenu(int eventCode, int inventoryWindowType);
static InventoryMoveResult _move_inventory(Object* item, int slotIndex, Object* targetObj, bool isPlanting, int* stealXpOverridePtr);
static void barterMoveToTable(Object* item, int quantity, int slotIndex, int indexOffset, Object* npc, Object* sourceTable, bool fromDude);
static void barterMoveFromTable(Object* item, int quantity, int slotIndex, Object* npc, Object* sourceTable, bool fromDude);
static void barterDisplayTables(int win, Object* leftTable, Object* rightTable, int draggedSlotIndex);
static void _container_enter(int keyCode, int inventoryWindowType);
static void _container_exit(int keyCode, int inventoryWindowType);
static int _drop_into_container(Object* container, Object* item, int sourceIndex, Object** itemSlot, int quantity);
static InventoryAmmoMoveResult _drop_ammo_into_weapon(Object* weapon, Object* ammo, Object** ammoItemSlot, int quantity, int keyCode);
static void _draw_amount(int value, int inventoryWindowType);
static int inventoryQuantitySelect(int inventoryWindowType, Object* item, int maximum, int defaultValue = 1);
static int inventoryViewerDropQuantity(Object* item, Object** itemSlot, int quantity);
static int inventoryQuantityWindowInit(int inventoryWindowType, Object* item);
static int inventoryQuantityWindowFree(int inventoryWindowType);

// 0x46E6D0
static const Stat gSummaryStats[7] = {
    STAT_CURRENT_HIT_POINTS,
    STAT_ARMOR_CLASS,
    STAT_DAMAGE_THRESHOLD,
    STAT_DAMAGE_THRESHOLD_LASER,
    STAT_DAMAGE_THRESHOLD_FIRE,
    STAT_DAMAGE_THRESHOLD_PLASMA,
    STAT_DAMAGE_THRESHOLD_EXPLOSION,
};

// 0x46E6EC
static const Stat gSummaryStats2[7] = {
    STAT_MAXIMUM_HIT_POINTS,
    STAT_INVALID,
    STAT_DAMAGE_RESISTANCE,
    STAT_DAMAGE_RESISTANCE_LASER,
    STAT_DAMAGE_RESISTANCE_FIRE,
    STAT_DAMAGE_RESISTANCE_PLASMA,
    STAT_DAMAGE_RESISTANCE_EXPLOSION,
};

// 0x46E708
static const int gInventoryArrowFrmIds[INVENTORY_ARROW_FRM_COUNT] = {
    122, // left arrow up
    123, // left arrow down
    124, // right arrow up
    125, // right arrow down
};

// The number of items to show in scroller.
//
// 0x519054
static int gInventorySlotsCount = 6;

// 0x519060
static bool _inven_is_initialized = false;

// 0x519064
static int _inven_display_msg_line = 1;

// 0x519068
static const InventoryWindowDescription gInventoryWindowDescriptions[INVENTORY_WINDOW_TYPE_COUNT] = {
    { InterfaceFrameId::InventoryBox, INVENTORY_WINDOW_WIDTH, 377, 80, 0 },
    { InterfaceFrameId::UseItemOnWindow, INVENTORY_USE_ON_WINDOW_WIDTH, 376, 80, 0 },
    { InterfaceFrameId::LootContainerWindow, INVENTORY_LOOT_WINDOW_WIDTH, 376, 80, 0 },
    { InterfaceFrameId::BarterWindow, INVENTORY_TRADE_WINDOW_WIDTH, 180, 80, 290 },
    { InterfaceFrameId::MoveMultipleItemsInterface, INVENTORY_TIMER_WINDOW_WIDTH, 162, 140, 80 },
    { InterfaceFrameId::MoveMultipleItemsInterface, INVENTORY_TIMER_WINDOW_WIDTH, 162, 140, 80 },
};

// 0x5190E0
static bool _dropped_explosive = false;

// 0x5190E4
static int gInventoryScrollUpButton = -1;

// 0x5190E8
static int gInventoryScrollDownButton = -1;

// 0x5190EC
static int gSecondaryInventoryScrollUpButton = -1;

// 0x5190F0
static int gSecondaryInventoryScrollDownButton = -1;

// 0x5190F4
static unsigned int gInventoryWindowDudeRotationTimestamp = 0;

// 0x5190F8
static Rotation gInventoryWindowDudeRotation = ROTATION_NE;

// 0x5190FC
static const int gInventoryWindowCursorFrmIds[INVENTORY_WINDOW_CURSOR_COUNT] = {
    286, // pointing hand
    250, // action arrow
    282, // action pick
    283, // action menu
    266, // blank
};

// 0x519110
static Object* _last_target = nullptr;

// 0x519114
static const GameMouseActionMenuItem _act_use[4] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_USE,
    GAME_MOUSE_ACTION_MENU_ITEM_DROP,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x519124
static const GameMouseActionMenuItem _act_no_use[3] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_DROP,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x519130
static const GameMouseActionMenuItem _act_just_use[3] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_USE,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x51913C
static const GameMouseActionMenuItem _act_nothing[2] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x519144
static const GameMouseActionMenuItem _act_weap[4] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD,
    GAME_MOUSE_ACTION_MENU_ITEM_DROP,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x519154
static const GameMouseActionMenuItem _act_weap2[3] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// Scroll offsets to target inventory for every container nesting level (stack).
// 0x59E7EC
static int _target_stack_offset[10];

// inventory.msg
//
// 0x59E814
static MessageList gInventoryMessageList;

// Current target critter or container for every nesting level (stack).
// 0x59E81C
static Object* _target_stack[10];

// Scroll offsets to main inventory for every container nesting level (stack).
// 0x59E844
static int _stack_offset[10];

// Current critter or container for every nesting level (stack).
// 0x59E86C
static Object* _stack[10];

// 0x59E894
static int _mt_wid;

// Current barter price modifier, set from scripts.
// 0x59E898


// 0x59E89C


// 0x59E8A0


// 0x59E8A4


// 0x59E8A8
static InventoryCursorData gInventoryCursorData[INVENTORY_WINDOW_CURSOR_COUNT];

// 0x59E934


// 0x59E938
static InventoryPrintItemDescriptionHandler* gInventoryPrintItemDescriptionHandler;

// 0x59E93C
static int _im_value;

// 0x59E940
static int gInventoryCursor;

// 0x59E944


// Current nesting level for viewing target's bag/backpack contents.
// 0x59E948
static int _target_curr_stack;

// 0x59E94C


// 0x59E950
static bool _inven_ui_was_disabled;

// Rotating character's fid.
//
// 0x59E95C


// 0x59E960
static Inventory* _pud;

// 0x59E964
static int gInventoryWindow = -1;

// Current nesting level for viewing bag/backpack contents.
// 0x59E96C
static int _curr_stack;

// 0x59E970
static int gInventoryWindowMaxY;

// 0x59E974
static int gInventoryWindowMaxX;

// 0x59E978
static Inventory* _target_pud;

// 0x59E97C


static FrmImage _inventoryFrmImages[INVENTORY_FRM_COUNT];
static FrmImage _moveFrmImages[8];

// True while gInventoryMessageList holds a loaded inventry.msg. Vanilla had no
// need for this: the list was loaded when a screen opened and freed when it
// closed, and nothing else ever spoke from it. The dedicated server does — it
// runs the steal session with no screen anywhere on the machine — so the load
// has to be reachable on its own, and reachable TWICE without leaking the first
// copy. See inventoryMessageListInit's early return.
static bool gInventoryMessageListLoaded = false;

// inventory_msg_init
// 0x46E73C
static constexpr int kTradeSlotCount = 3;

constexpr int kPartySlotWidth = 106;

constexpr int kPartySlotHeight = 69;

constexpr int kPartySlotImageHeight = 139;

static void inventoryLootRenderPaneWeight(unsigned char* windowBuffer, int pitch, bool targetPane, Object* object, int extraWeight);

static void inventoryScrollerHandleInput(const InventoryScroller& scroller, int keyCode, int mouseEvent);

static void inventoryDrawCenteredText(unsigned char* buffer, int pitch, int width, int x, int y, const char* text, ColorWithFlags color);

static void inventorySetLeftPaneCritter(Object* critter, Object* target, int inventoryWindowType);

static std::pair<int, int> barterComputeTablesWeight(Object* dude, Object* npc);

static int barterGetMovedQuantity(Object* item, int maxQuantity, bool fromPlayer, bool fromInventory, bool immediate);

static void inventoryUnloadWeaponToOwner(Object* owner, Object* weapon);

static void inventoryDisplayMessage(int num);

static bool _ctrl_pressed();

static void _drag_item_loop(Object* item, bool immediate);

static void inventoryNormalLayoutUpdate();

static bool inventoryBackgroundLoad(FrmImage& image, InterfaceFrameId col1FrmId, const char* col2Name, int columns);

static int inventoryChooseColumns(FrmImage& image, int expandedWidth, InterfaceFrameId col1FrmId, const char* col2Name);

static void inventoryCreateSlotButtons(int baseKeyCode, int scrollerX, int scrollerY, int columns);

static bool inventoryGetCurrentWindowType(InventoryWindowType* inventoryWindowTypePtr);

static int inventoryGetWindowWidth(int inventoryWindowType);

static int inventoryGetWindowHeight(int inventoryWindowType);

static void inventoryNormalClampStackOffset();

static void inventoryLootLayoutUpdate();

static int inventoryLootGetSlotX(bool targetInventory, int slotIndex);

static int inventoryLootGetSlotY(int slotIndex);

static bool inventoryLootMouseHitTestScroller(bool targetInventory);

static void renderPartySlots();

static void createPartySlotButtons();

static void handlePartySlotPickup(InvenSlot slot);

static bool tryEquipPartyItem(Object* item, bool fromLeftPane);

static bool tryUnequipPartyItem(InvenSlot slot);

static bool tryMovePartyItemToLeftPane(InvenSlot slot, Object* item);

static FrmId buildPartyDisplayFrmId();

static FrmId getTargetDisplayFrmId();

static void setLootTarget(Object* target, Object* hiddenBox);

static void refreshAfterTargetChange();

static int inventoryWrapIndex(int index, int count, int direction);

static int inventoryComputeAlignedMaxOffset(int length, int visibleSlots, int scrollStep);

static int inventoryGetCenteredWindowY(int windowHeight);

static void displayLootPanePartyName(unsigned char* windowBuffer, int windowPitch, const Rect& rect, int index);

static constexpr InterfaceFrmId kInventoryArrowFrmIds[INVENTORY_ARROW_FRM_COUNT] = {
    InterfaceFrameId::LeftArrowUp,
    InterfaceFrameId::LeftArrowDown,
    InterfaceFrameId::RightArrowUp,
    InterfaceFrameId::RightArrowDown,
};

static InventoryNormalLayout inventoryLayout;

static InventoryLootLayout inventoryLootLayout;

static FrmImage inventoryFrmImage;

static FrmImage inventoryLootFrmImage;

static FrmImage partySlotFrmImage;

static constexpr InterfaceFrmId kInventoryWindowCursorFrmIds[INVENTORY_WINDOW_CURSOR_COUNT] = {
    InterfaceFrameId::PointingHand,
    InterfaceFrameId::ActionArrow,
    InterfaceFrameId::ActionPick,
    InterfaceFrameId::ActionMenu,
    InterfaceFrameId::MouseCursorBlank,
};

static int gBarterFinalModifier;

static int gBartererTableOffset;

static int gPlayerTableOffset;

static Inventory* gPlayerTableInventory;

static Object* gPlayerTableObj;

static Object* gBartererTableObj;

static Inventory* gBartererTableInventory;

static FrmId gInventoryWindowDudeFrmId;

static int gInventoryBarterBackgroundWindow;

static int partyRightEquippedWeight = 0;

static CritterEquipped* partyTargetEquipped = nullptr;

static Object* partyBaseTarget = nullptr;

static int buttonCreateSlot(int win, int x, int y, int width, int height, int keyCode, ButtonCallback* onEnter = nullptr, ButtonCallback* onExit = nullptr);

static int buttonCreateAction(int win, int x, int y, int width, int height, int keyCode);

static int buttonCreateActionWithFrm(int win, int x, int y, int mouseDownEventCode, int mouseUpEventCode, const InterfaceFrmId& normalId, const InterfaceFrmId& pressedId);

static ConstBuffer2D inventoryGetBackgroundBuffer(int inventoryWindowType, InterfaceFrameId fallbackFrmId, FrmImage& fallbackImage, int& sourceXOffset);

static void inventoryNormalApplyLayout(int columns);

static void inventoryLootApplyLayout(int columns);

static void inventoryClampOffset(int* offsetPtr, int inventoryLength, int visibleSlots);

static void barterClampTableOffsets();

static InventoryItem* inventoryGetReversedSlot(Inventory* inventory, int index);

static void inventoryScrollerRedrawInventory(const InventoryScroller& scroller, void* context);

static void inventoryScrollerRedrawTargetInventory(const InventoryScroller& scroller, void* context);

static void inventoryScrollerRedrawBarterTables(const InventoryScroller& scroller, void* context);

static void inventoryScrollerJumpTo(const InventoryScroller& scroller, int offset);

static void inventoryScrollerScroll(const InventoryScroller& scroller, int direction);

static void inventoryScrollerPage(const InventoryScroller& scroller, int direction);

static bool hasPartySlots();

static Object** getPartySlotItemPtr(InvenSlot slot);

static Object* getPartySlotItem(InvenSlot slot);

static Rect getPartySlotRect(InvenSlot slot);

static void refreshPartyEquippedAfterAction();

static int unequipLootArmorFunc(Object* critter);

static void inventoryStripEquippedToGlobals(Object* critter);

static void inventoryRestoreEquippedFromGlobals(Object* critter);

static std::pair<int, int> barterComputeTablesValue(Object* dude, Object* npc, bool offerButton);

int inventoryGetWindow();

class InventoryFirstPersonCursorScope {
public:
    InventoryFirstPersonCursorScope()
        : _firstPersonPreviewWasEnabled(tileFirstPersonPreviewIsEnabled())
        , _relativeModeWasEnabled(_firstPersonPreviewWasEnabled && mouseDeviceUsesRelativeMode())
    {
        if (!_firstPersonPreviewWasEnabled) {
            return;
        }

        if (_relativeModeWasEnabled) {
            if (mouseDeviceSetRelativeModeForced(false)) {
                tileFirstPersonPreviewSetCursorCaptured(false);
            } else {
                debugPrint("Failed to release mouse for inventory\n");
            }
        }
        mouseShowCursor();
    }

    ~InventoryFirstPersonCursorScope()
    {
        if (!_firstPersonPreviewWasEnabled || !tileFirstPersonPreviewIsEnabled()) {
            return;
        }

        if (_relativeModeWasEnabled) {
            if (mouseDeviceSetRelativeModeForced(true)) {
                tileFirstPersonPreviewSetCursorCaptured(true);
                mouseHideCursor();
            } else {
                debugPrint("Failed to recapture mouse after inventory\n");
            }
        } else {
            tileFirstPersonPreviewSetCursorCaptured(false);
            mouseShowCursor();
        }
    }

private:
    bool _firstPersonPreviewWasEnabled;
    bool _relativeModeWasEnabled;
};

void inventoryDisplayStats();

void inventoryRedraw(int redrawSide);

int inventoryMessageListInit()
{
    char path[COMPAT_MAX_PATH];

    // Already loaded (the server preloaded it for a steal session; see
    // stealSessionRun). messageListInit on a live list would strand its entries.
    if (gInventoryMessageListLoaded) {
        return 0;
    }

    if (!messageListInit(&gInventoryMessageList))
        return -1;

    snprintf(path, sizeof(path), "%s%s", gGameMsgPath, "inventry.msg");
    if (!messageListLoad(&gInventoryMessageList, path))
        return -1;

    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_INVENTORY, &gInventoryMessageList);
    gInventoryMessageListLoaded = true;

    return 0;
}

// inventory_msg_free
// 0x46E7A0
static int inventoryMessageListFree()
{
    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_INVENTORY, nullptr);
    messageListFree(&gInventoryMessageList);
    gInventoryMessageListLoaded = false;
    return 0;
}

// 0x46E7B0
void inventoryOpen()
{
    // On a WIRE VIEWER both in-combat gates below have ALREADY been decided by
    // the server, which owns the fight: it checked that it is our turn and
    // charged the AP before granting the screen (server_control.cc invopen). The
    // viewer must not re-decide either one.
    //
    // Re-deciding would not merely duplicate work, it would break both ways:
    // _combat_whose_turn() is never set on a viewer (combat runs server-side), so
    // the turn check would read nullptr and silently refuse to open a screen the
    // player has already paid 4 AP for; and inventoryApCostApply would charge
    // those 4 AP a SECOND time against the local mirror, showing a wrong AP bar
    // until the next reconcile overwrote it.
    bool serverRuledOnCombatEntry = clientViewerActive();

    if (isInCombat() && !serverRuledOnCombatEntry) {
        if (_combat_whose_turn() != _inven_dude) {
            return;
        }
    }

    if (inventoryCommonInit() == -1) {
        return;
    }

    if (isInCombat() && !serverRuledOnCombatEntry) {
        if (_inven_dude == gDude) {
            int actionPointsRequired = inventoryGetInvenApCost();
            if (actionPointsRequired > 0 && actionPointsRequired > gDude->data.critter.combat.ap) {
                inventoryDisplayMessage(19); // You don't have enough action points to use inventory.

                // NOTE: Uninline.
                inventoryCommonFree();

                return;
            }
        }
    }

    Object* oldArmor = critterGetArmor(_inven_dude);
    bool isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_NORMAL);
    reg_anim_clear(_inven_dude);
    inventoryRenderSummary();
    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_NORMAL);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
    InventoryFirstPersonCursorScope firstPersonCursorScope;
    ScopedGameMode gm(GameMode::kInventory);

    for (;;) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        int mouseEvent = mouseGetEvent();
        InventoryScrollerDisplayContext inventoryScrollerContext { INVENTORY_WINDOW_TYPE_NORMAL, nullptr };
        Rect normalRect = { inventoryLayout.scrollerX, inventoryLayout.scrollerY, inventoryLayout.scrollerX + inventoryLayout.scrollerWidth, inventoryLayout.scrollerY + inventoryLayout.scrollerHeight };
        InventoryScroller normalScroller {
            normalRect,
            &(_stack_offset[_curr_stack]),
            inventoryLayout.columns,
            inventoryLayout.visibleSlots,
            _pud->length,
            KEY_ARROW_UP,
            KEY_ARROW_DOWN,
            KEY_PAGE_UP,
            KEY_PAGE_DOWN,
            KEY_HOME,
            KEY_END,
            inventoryScrollerRedrawInventory,
            &inventoryScrollerContext,
            false,
        };

        // SFALL: Close with 'I'.
        if (keyCode == KEY_ESCAPE || keyCode == KEY_UPPERCASE_I || keyCode == KEY_LOWERCASE_I) {
            break;
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        _display_body(FrmId::Empty(), INVENTORY_WINDOW_TYPE_NORMAL);

        if (gameGetState() == GameState::DialogRequested) {
            break;
        }

        // Wire viewer: a live dude-inventory reconcile (a drop/give/consume streamed
        // in by the service ticker's pump, Slice 3b) mutated the mirror — repaint the
        // list + summary so the change shows immediately (this loop otherwise repaints
        // only on user events, so an async drop would linger visibly). Clamp the scroll
        // offset in case items were removed out from under it.
        if (clientViewerActive() && clientViewerConsumeDudeInvDirty()) {
            if (_stack_offset[_curr_stack] > _pud->length - gInventorySlotsCount) {
                _stack_offset[_curr_stack] = _pud->length - gInventorySlotsCount;
            }
            if (_stack_offset[_curr_stack] < 0) {
                _stack_offset[_curr_stack] = 0;
            }
            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_NORMAL);
            inventoryRenderSummary();
            // BLIT IT. Neither _display_inventory nor inventoryRenderSummary reaches
            // the screen on their own — they draw into the window buffer, and the
            // vanilla call sites all follow up with a refresh because they run from
            // a user event that was going to refresh anyway. This block does not: it
            // fires on an ASYNC server reconcile, so without this the new contents
            // sit in the buffer, invisible, until some unrelated event blits the
            // window (the tell is a sprite that partially appears as the mouse
            // cursor's own dirty-rect drags across it).
            windowRefresh(gInventoryWindow);
        }

        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X) {
            showQuitConfirmationDialog();
        } else if (keyCode == 2500) {
            _container_exit(keyCode, INVENTORY_WINDOW_TYPE_NORMAL);
        } else {
            inventoryScrollerHandleInput(normalScroller, keyCode, mouseEvent);

            if ((mouseEvent & MOUSE_EVENT_RIGHT_BUTTON_DOWN) != 0) {
                if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
                } else if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
                    inventoryRenderSummary();
                    windowRefresh(gInventoryWindow);
                }
            } else if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0) {
                if ((keyCode >= 1000 && keyCode < 1000 + inventoryLayout.visibleSlots) || keyCode == INVENTORY_HAND_RIGHT_KEY || keyCode == INVENTORY_HAND_LEFT_KEY || keyCode == INVENTORY_ARMOR_KEY) {
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_NORMAL);
                    } else {
                        _inven_pickup(keyCode, _stack_offset[_curr_stack]);
                    }
                }
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    _inven_dude = _stack[0];
    _adjust_fid();

    if (_inven_dude == gDude) {
        Rect rect;
        objectSetFrmId(_inven_dude, gInventoryWindowDudeFrmId, &rect);
        tileWindowRefreshRect(&rect, _inven_dude->elevation);
    }

    Object* newArmor = critterGetArmor(_inven_dude);
    if (_inven_dude == gDude) {
        if (oldArmor != newArmor) {
            interfaceRenderArmorClass(true);
        }
    }

    _exit_inventory(isoWasEnabled);

    // NOTE: Uninline.
    inventoryCommonFree();

    if (_inven_dude == gDude) {
        interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
    }
}

// 0x46EC90
static bool _setup_inventory(int inventoryWindowType)
{
    _dropped_explosive = 0;
    _curr_stack = 0;
    _stack_offset[0] = 0;
    gInventorySlotsCount = 6;
    _pud = &(_inven_dude->data.inventory);
    _stack[0] = _inven_dude;
    bool isNormalWindow = inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL;

    if (inventoryWindowType <= INVENTORY_WINDOW_TYPE_LOOT) {
        if (isNormalWindow) {
            inventoryNormalLayoutUpdate();
        } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            inventoryLootLayoutUpdate();
            gInventorySlotsCount = inventoryLootLayout.visibleSlots;
        }

        const InventoryWindowDescription* windowDescription = &(gInventoryWindowDescriptions[inventoryWindowType]);
        int windowWidth = inventoryGetWindowWidth(inventoryWindowType);
        int windowHeight = inventoryGetWindowHeight(inventoryWindowType);

        // Maintain original position in original resolution, otherwise center it.
        bool preserveVanillaX = screenGetWidth() == 640
            && windowWidth == windowDescription->width
            && inventoryWindowType != INVENTORY_WINDOW_TYPE_LOOT;
        bool preserveVanillaY = screenGetHeight() == 480;
        int inventoryWindowX = preserveVanillaX
            ? INVENTORY_WINDOW_X
            : (screenGetWidth() - windowWidth) / 2;
        int inventoryWindowY = preserveVanillaY
            ? INVENTORY_WINDOW_Y
            : inventoryGetCenteredWindowY(windowHeight);
        gInventoryWindow = windowCreate(
            inventoryWindowX, inventoryWindowY, windowWidth, windowHeight,
            static_cast<ColorWithFlags>(257), WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
        gInventoryWindowMaxX = windowWidth + inventoryWindowX;
        gInventoryWindowMaxY = windowHeight + inventoryWindowY;

        Buffer2D destBuf { windowGetBuffer(gInventoryWindow), windowWidth, windowHeight };

        if (isNormalWindow) {
            assert(inventoryFrmImage.isLocked());
            blitBuffer2D(inventoryFrmImage.getBuffer(), destBuf);
        } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            assert(inventoryLootFrmImage.isLocked());
            blitBuffer2D(inventoryLootFrmImage.getBuffer(), destBuf);
        } else {
            FrmImage backgroundFrmImage;
            if (backgroundFrmImage.lock(windowDescription->frmId)) {
                blitBuffer2D(backgroundFrmImage.getBuffer(), destBuf);
            }
        }

        gInventoryPrintItemDescriptionHandler = displayMonitorAddMessage;
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        if (gInventoryBarterBackgroundWindow == -1) {
            exit(1);
        }

        int extraSlots = gameDialogIsBarterWindowExpanded() ? kExpandedBarterExtraSlots : 0;
        gInventorySlotsCount = kTradeSlotCount + extraSlots;

        int tradeWindowHeight = INVENTORY_TRADE_WINDOW_HEIGHT + extraSlots * INVENTORY_SLOT_HEIGHT;

        // Trade inventory window is a part of game dialog, which is 640x480.
        Rect bgWindowRect;
        windowGetRect(gInventoryBarterBackgroundWindow, &bgWindowRect);
        int tradeWindowX = bgWindowRect.left + INVENTORY_TRADE_WINDOW_X;
        int tradeWindowY = bgWindowRect.top + INVENTORY_TRADE_WINDOW_Y;
        gInventoryWindow = windowCreate(tradeWindowX, tradeWindowY, INVENTORY_TRADE_WINDOW_WIDTH, tradeWindowHeight, static_cast<ColorWithFlags>(257), 0);
        gInventoryWindowMaxX = tradeWindowX + INVENTORY_TRADE_WINDOW_WIDTH;
        gInventoryWindowMaxY = tradeWindowY + tradeWindowHeight;

        Buffer2D dest { windowGetBuffer(gInventoryWindow), INVENTORY_TRADE_WINDOW_WIDTH, tradeWindowHeight };
        ConstBuffer2D src { windowGetBuffer(gInventoryBarterBackgroundWindow), INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowGetHeight(gInventoryBarterBackgroundWindow) };
        blitBuffer2D(src, INVENTORY_TRADE_WINDOW_X, 0, INVENTORY_TRADE_WINDOW_WIDTH, tradeWindowHeight, dest);

        gInventoryPrintItemDescriptionHandler = gameDialogRenderSupplementaryMessage;
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        inventoryCreateSlotButtons(1000, inventoryLootLayout.leftScrollerX, inventoryLootLayout.leftScrollerY, inventoryLootLayout.columns);
        inventoryCreateSlotButtons(2000, inventoryLootLayout.rightScrollerX, inventoryLootLayout.rightScrollerY, inventoryLootLayout.columns);
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        int y1 = INVENTORY_TRADE_SCROLLER_Y;
        int y2 = INVENTORY_TRADE_INNER_SCROLLER_Y;

        for (int index = 0; index < gInventorySlotsCount; index++) {
            buttonCreateSlot(gInventoryWindow, INVENTORY_TRADE_LEFT_SCROLLER_X, y1,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, 1000 + index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
            buttonCreateSlot(gInventoryWindow, INVENTORY_TRADE_RIGHT_SCROLLER_X, y1,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, 2000 + index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
            buttonCreateSlot(gInventoryWindow, INVENTORY_TRADE_INNER_LEFT_SCROLLER_X, y2,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, 2300 + index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
            buttonCreateSlot(gInventoryWindow, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X, y2,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, 2400 + index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
            y1 += INVENTORY_SLOT_HEIGHT;
            y2 += INVENTORY_SLOT_HEIGHT;
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        inventoryCreateSlotButtons(1000, inventoryLayout.scrollerX, inventoryLayout.scrollerY, inventoryLayout.columns);
    } else {
        // Create invisible buttons representing item slots.
        for (int index = 0; index < gInventorySlotsCount; index++) {
            buttonCreateSlot(gInventoryWindow,
                INVENTORY_SCROLLER_X,
                INVENTORY_SLOT_HEIGHT * (gInventorySlotsCount - index - 1) + INVENTORY_SCROLLER_Y,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT,
                999 + gInventorySlotsCount - index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
        }
    }

    if (isNormalWindow) {
        // Item2 slot
        buttonCreateSlot(gInventoryWindow, inventoryLayout.rightHandSlotX, INVENTORY_RIGHT_HAND_SLOT_Y,
            INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, INVENTORY_HAND_RIGHT_KEY,
            inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);

        // Item1 slot
        buttonCreateSlot(gInventoryWindow, inventoryLayout.leftHandSlotX, INVENTORY_LEFT_HAND_SLOT_Y,
            INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, INVENTORY_HAND_LEFT_KEY,
            inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);

        // Armor slot
        buttonCreateSlot(gInventoryWindow, inventoryLayout.armorSlotX, INVENTORY_ARMOR_SLOT_Y,
            INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, INVENTORY_ARMOR_KEY,
            inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
    }

    std::pair<int, int> doneButtonXY = { -1, -1 };
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        doneButtonXY = { inventoryLayout.doneButtonX, 329 };
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_USE_ITEM_ON) {
        doneButtonXY = { 233, 328 };
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        doneButtonXY = { inventoryLootLayout.doneButtonX, 331 };
    }

    if (doneButtonXY.first != -1) {
        buttonCreateActionWithFrm(gInventoryWindow,
            doneButtonXY.first, doneButtonXY.second,
            -1, KEY_ESCAPE,
            InterfaceFrameId::LittleRedButtonUp,
            InterfaceFrameId::LittleRedButtonDown);
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        // Left inventory up button.
        buttonCreateActionWithFrm(gInventoryWindow,
            109, 56,
            KEY_ARROW_UP, -1,
            InterfaceFrameId::DialogUpButtonUp,
            InterfaceFrameId::DialogUpButtonDown);

        // Right inventory up button.
        buttonCreateActionWithFrm(gInventoryWindow,
            342, 56,
            KEY_CTRL_ARROW_UP, -1,
            InterfaceFrameId::DialogUpButtonUp,
            InterfaceFrameId::DialogUpButtonDown);
    } else {
        int scrollUpX = isNormalWindow                          ? inventoryLayout.scrollButtonX
            : inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT ? inventoryLootLayout.leftScrollButtonX
                                                                : 128;
        // Left inventory up button.
        gInventoryScrollUpButton = buttonCreateActionWithFrm(gInventoryWindow,
            scrollUpX, 39,
            KEY_ARROW_UP, -1,
            InterfaceFrameId::InventoryButtonOutUp,
            InterfaceFrameId::InventoryButtonInUp);
        if (gInventoryScrollUpButton != -1) {
            buttonSetDisabledFrm(gInventoryScrollUpButton,
                InterfaceFrameId::InventoryButtonUpDisabled,
                InterfaceFrameId::InventoryButtonUpDisabled,
                InterfaceFrameId::InventoryButtonUpDisabled);
            buttonDisable(gInventoryScrollUpButton);
        }

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            // Right inventory up button.
            gSecondaryInventoryScrollUpButton = buttonCreateActionWithFrm(gInventoryWindow,
                inventoryLootLayout.rightScrollButtonX, 39,
                KEY_CTRL_ARROW_UP, -1,
                InterfaceFrameId::InventoryButtonOutUp,
                InterfaceFrameId::InventoryButtonInUp);
            if (gSecondaryInventoryScrollUpButton != -1) {
                buttonSetDisabledFrm(gSecondaryInventoryScrollUpButton,
                    InterfaceFrameId::InventoryButtonUpDisabled,
                    InterfaceFrameId::InventoryButtonUpDisabled,
                    InterfaceFrameId::InventoryButtonUpDisabled);
                buttonDisable(gSecondaryInventoryScrollUpButton);
            }
        }
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        // Left inventory down button.
        buttonCreateActionWithFrm(gInventoryWindow,
            109, 82,
            KEY_ARROW_DOWN, -1,
            InterfaceFrameId::DialogDownButtonUp,
            InterfaceFrameId::DialogDownButtonDown);

        // Right inventory down button
        buttonCreateActionWithFrm(gInventoryWindow,
            342, 82,
            KEY_CTRL_ARROW_DOWN, -1,
            InterfaceFrameId::DialogDownButtonUp,
            InterfaceFrameId::DialogDownButtonDown);

        // Invisible button representing left character.
        buttonCreateAction(gInventoryBarterBackgroundWindow,
            INVENTORY_BARTER_LEFT_BODY_VIEW_X, INVENTORY_BARTER_LEFT_BODY_VIEW_Y, INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2500);

        // Invisible button representing right character.
        buttonCreateAction(gInventoryBarterBackgroundWindow,
            560, 25, INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2501);
    } else {
        int scrollDownX = isNormalWindow                        ? inventoryLayout.scrollButtonX
            : inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT ? inventoryLootLayout.leftScrollButtonX
                                                                : 128;
        // Left inventory down button.
        gInventoryScrollDownButton = buttonCreateActionWithFrm(gInventoryWindow,
            scrollDownX, 62,
            KEY_ARROW_DOWN, -1,
            InterfaceFrameId::InventoryButtonOutDown,
            InterfaceFrameId::InventoryButtonInDown);
        if (gInventoryScrollDownButton != -1) {
            buttonSetDisabledFrm(gInventoryScrollDownButton,
                InterfaceFrameId::InventoryButtonDownDisabled,
                InterfaceFrameId::InventoryButtonDownDisabled,
                InterfaceFrameId::InventoryButtonDownDisabled);
            buttonDisable(gInventoryScrollDownButton);
        }

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            // Invisible button representing left character.
            buttonCreateAction(gInventoryWindow,
                inventoryLootLayout.leftBodyViewX, INVENTORY_LOOT_LEFT_BODY_VIEW_Y,
                INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2500);

            // Right inventory down button.
            gSecondaryInventoryScrollDownButton = buttonCreateActionWithFrm(gInventoryWindow,
                inventoryLootLayout.rightScrollButtonX, 62,
                KEY_CTRL_ARROW_DOWN, -1,
                InterfaceFrameId::InventoryButtonOutDown, InterfaceFrameId::InventoryButtonInDown);
            if (gSecondaryInventoryScrollDownButton != -1) {
                buttonSetDisabledFrm(gSecondaryInventoryScrollDownButton,
                    InterfaceFrameId::InventoryButtonDownDisabled, InterfaceFrameId::InventoryButtonDownDisabled, InterfaceFrameId::InventoryButtonDownDisabled);
                buttonDisable(gSecondaryInventoryScrollDownButton);
            }

            // Invisible button representing right character.
            buttonCreateAction(gInventoryWindow,
                inventoryLootLayout.rightBodyViewX, INVENTORY_LOOT_RIGHT_BODY_VIEW_Y,
                INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2501);
        } else {
            // Invisible button representing character (in inventory and use on dialogs).
            buttonCreateAction(gInventoryWindow,
                isNormalWindow ? inventoryLayout.bodyViewX : INVENTORY_PC_BODY_VIEW_X,
                INVENTORY_PC_BODY_VIEW_Y,
                INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2500);
        }
    }

    if (inventoryWindowType != INVENTORY_WINDOW_TYPE_TRADE) {
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            if (!_gIsSteal) {
                // Take all button.
                buttonCreateActionWithFrm(gInventoryWindow,
                    inventoryLootLayout.takeAllButtonX, 204, 2502, -1,
                    InterfaceFrameId::InventoryLootAllUp, InterfaceFrameId::InventoryLootAllDown);
            }
        }
    } else {
        // Left offered inventory up button.
        buttonCreateActionWithFrm(gInventoryWindow,
            128, 113, KEY_PAGE_UP, -1,
            InterfaceFrameId::InventoryButtonOutUp, InterfaceFrameId::InventoryButtonInUp);

        // Left offered inventory down button.
        buttonCreateActionWithFrm(gInventoryWindow,
            128, 136, KEY_PAGE_DOWN, -1,
            InterfaceFrameId::InventoryButtonOutDown, InterfaceFrameId::InventoryButtonInDown);

        // Right offered inventory up button.
        buttonCreateActionWithFrm(gInventoryWindow,
            333, 113, KEY_CTRL_PAGE_UP, -1,
            InterfaceFrameId::InventoryButtonOutUp, InterfaceFrameId::InventoryButtonInUp);

        // Right offered inventory down button.
        buttonCreateActionWithFrm(gInventoryWindow,
            333, 136, KEY_CTRL_PAGE_DOWN, -1,
            InterfaceFrameId::InventoryButtonOutDown, InterfaceFrameId::InventoryButtonInDown);
    }

    gInventoryRightHandItem = nullptr;
    gInventoryArmor = nullptr;
    gInventoryLeftHandItem = nullptr;
    partyTargetEquipped = nullptr;
    partyBaseTarget = nullptr;

    for (int index = 0; index < _pud->length; index++) {
        InventoryItem* inventoryItem = &(_pud->items[index]);
        Object* item = inventoryItem->item;
        if ((item->flags & OBJECT_IN_LEFT_HAND) != OBJECT_NONE) {
            if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
                gInventoryRightHandItem = item;
            }
            gInventoryLeftHandItem = item;
        } else if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
            gInventoryRightHandItem = item;
        } else if ((item->flags & OBJECT_WORN) != OBJECT_NONE) {
            gInventoryArmor = item;
        }
    }

    if (gInventoryLeftHandItem != nullptr) {
        itemRemoveWithReason(_inven_dude, gInventoryLeftHandItem, 1, RemoveInventoryObjectHookReason::LeftHandEquipped);
    }

    if (gInventoryRightHandItem != nullptr && gInventoryRightHandItem != gInventoryLeftHandItem) {
        itemRemoveWithReason(_inven_dude, gInventoryRightHandItem, 1, RemoveInventoryObjectHookReason::RightHandEquipped);
    }

    if (gInventoryArmor != nullptr) {
        itemRemoveWithReason(_inven_dude, gInventoryArmor, 1, RemoveInventoryObjectHookReason::ArmorEquipped);
    }

    _adjust_fid();

    bool isoWasEnabled = isoDisable();

    _gmouse_disable(0);
    touch_set_touchscreen_mode(true);
    touch_set_pan_mode(true);

    return isoWasEnabled;
}

// 0x46FBD8
static void _exit_inventory(bool shouldEnableIso)
{
    _inven_dude = _stack[0];

    // Ledger H-4: equip commit extracted to core (item.cc).
    equipmentApply(_inven_dude, gInventoryLeftHandItem, gInventoryRightHandItem, gInventoryArmor);

    gInventoryRightHandItem = nullptr;
    gInventoryArmor = nullptr;
    gInventoryLeftHandItem = nullptr;

    for (int index = 0; index < INVENTORY_FRM_COUNT; index++) {
        _inventoryFrmImages[index].unlock();
    }

    if (shouldEnableIso) {
        isoEnable();
    }

    windowDestroy(gInventoryWindow);
    inventoryFrmImage.unlock();
    inventoryLootFrmImage.unlock();
    partySlotFrmImage.unlock();

    _gmouse_enable();
    touch_set_touchscreen_mode(false);
    touch_set_pan_mode(false);

    if (_dropped_explosive) {
        // Ledger H-5: RNG-consuming resolution extracted to core (actions.cc).
        actionResolveDroppedExplosive(gDude);
        _dropped_explosive = false;
    }
}

// 0x46FDF4
static void _display_inventory(int stackOffset, int dragSlotIndex, int inventoryWindowType)
{
    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch;

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        pitch = inventoryLayout.windowWidth;

        unsigned char* backgroundData = inventoryFrmImage.getData();
        int backgroundWidth = inventoryFrmImage.getWidth();
        if (backgroundData != nullptr) {
            // Clear scroll view background.
            blitBufferToBuffer(backgroundData + backgroundWidth * inventoryLayout.scrollerY + inventoryLayout.scrollerX,
                inventoryLayout.scrollerWidth,
                inventoryLayout.scrollerHeight,
                backgroundWidth,
                windowBuffer + pitch * inventoryLayout.scrollerY + inventoryLayout.scrollerX,
                pitch);

            // Clear armor button background.
            blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_ARMOR_SLOT_Y + inventoryLayout.armorSlotX,
                INVENTORY_LARGE_SLOT_WIDTH,
                INVENTORY_LARGE_SLOT_HEIGHT,
                backgroundWidth,
                windowBuffer + pitch * INVENTORY_ARMOR_SLOT_Y + inventoryLayout.armorSlotX,
                pitch);

            if (gInventoryLeftHandItem != nullptr && gInventoryLeftHandItem == gInventoryRightHandItem) {
                blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX,
                    INVENTORY_LARGE_SLOT_WIDTH * 2,
                    INVENTORY_LARGE_SLOT_HEIGHT,
                    backgroundWidth,
                    windowBuffer + pitch * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX,
                    pitch);
            } else {
                // Clear both items in one go.
                blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX,
                    INVENTORY_LARGE_SLOT_WIDTH * 2,
                    INVENTORY_LARGE_SLOT_HEIGHT,
                    backgroundWidth,
                    windowBuffer + pitch * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX,
                    pitch);
            }
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_USE_ITEM_ON) {
        pitch = INVENTORY_USE_ON_WINDOW_WIDTH;

        FrmImage backgroundFrmImage;
        if (backgroundFrmImage.lock(InterfaceFrameId::UseItemOnWindow)) {
            // Clear scroll view background.
            blitBufferToBuffer(backgroundFrmImage.getData() + pitch * INVENTORY_SCROLLER_Y + INVENTORY_SCROLLER_X,
                INVENTORY_SLOT_WIDTH,
                gInventorySlotsCount * INVENTORY_SLOT_HEIGHT,
                pitch,
                windowBuffer + pitch * INVENTORY_SCROLLER_Y + INVENTORY_SCROLLER_X,
                pitch);
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        pitch = inventoryLootLayout.windowWidth;

        unsigned char* backgroundData = inventoryLootFrmImage.getData();
        int backgroundWidth = inventoryLootFrmImage.getWidth();
        if (backgroundData != nullptr) {
            // Clear scroll view background.
            blitBufferToBuffer(backgroundData + backgroundWidth * inventoryLootLayout.leftScrollerY + inventoryLootLayout.leftScrollerX,
                inventoryLootLayout.scrollerWidth,
                inventoryLootLayout.scrollerHeight,
                backgroundWidth,
                windowBuffer + pitch * inventoryLootLayout.leftScrollerY + inventoryLootLayout.leftScrollerX,
                pitch);
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        pitch = INVENTORY_TRADE_WINDOW_WIDTH;

        windowBuffer = windowGetBuffer(gInventoryWindow);

        blitBufferToBuffer(windowGetBuffer(gInventoryBarterBackgroundWindow) + INVENTORY_TRADE_LEFT_SCROLLER_Y * INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH + INVENTORY_TRADE_LEFT_SCROLLER_X + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount, INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowBuffer + pitch * INVENTORY_TRADE_LEFT_SCROLLER_Y + INVENTORY_TRADE_LEFT_SCROLLER_X, pitch);
    } else {
        assert(false && "Should be unreachable");
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL
        || inventoryWindowType == INVENTORY_WINDOW_TYPE_USE_ITEM_ON
        || inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        if (gInventoryScrollUpButton != -1) {
            if (stackOffset <= 0) {
                buttonDisable(gInventoryScrollUpButton);
            } else {
                buttonEnable(gInventoryScrollUpButton);
            }
        }

        if (gInventoryScrollDownButton != -1) {
            int visibleSlots = inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL ? inventoryLayout.visibleSlots : gInventorySlotsCount;
            if (_pud->length - stackOffset <= visibleSlots) {
                buttonDisable(gInventoryScrollDownButton);
            } else {
                buttonEnable(gInventoryScrollDownButton);
            }
        }
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        for (int slotIndex = 0; slotIndex + stackOffset < _pud->length && slotIndex < inventoryLayout.visibleSlots; slotIndex += 1) {
            int itemIndex = slotIndex + stackOffset + 1;
            int row = slotIndex / inventoryLayout.columns;
            int column = slotIndex % inventoryLayout.columns;
            int itemPaddingY = INVENTORY_SLOT_PADDING + (inventoryLayout.columns > 1 ? INVENTORY_SLOT_PADDING : 0);
            int offset = pitch * (inventoryLayout.scrollerY + row * INVENTORY_SLOT_HEIGHT + itemPaddingY)
                + inventoryLayout.scrollerX + column * INVENTORY_SLOT_WIDTH + INVENTORY_SLOT_PADDING;

            InventoryItem* inventoryItem = &(_pud->items[_pud->length - itemIndex]);

            const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
            artRender(inventoryFrmId, windowBuffer + offset, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, pitch);
            _display_inventory_info(inventoryItem->item, inventoryItem->quantity, windowBuffer + offset, pitch, slotIndex == dragSlotIndex);
        }
    } else {
        int y = 0;
        for (int slotIndex = 0; slotIndex + stackOffset < _pud->length && slotIndex < gInventorySlotsCount; slotIndex += 1) {
            int itemIndex = slotIndex + stackOffset + 1;

            int offset;
            if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
                offset = pitch * (y + INVENTORY_TRADE_LEFT_SCROLLER_Y_PAD) + INVENTORY_TRADE_LEFT_SCROLLER_X_PAD;
            } else {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
                    offset = pitch * (inventoryLootGetSlotY(slotIndex) + INVENTORY_SLOT_PADDING) + inventoryLootGetSlotX(false, slotIndex) + INVENTORY_SLOT_PADDING;
                } else {
                    offset = pitch * (y + INVENTORY_SCROLLER_Y_PAD) + INVENTORY_SCROLLER_X_PAD;
                }
            }

            InventoryItem* inventoryItem = &(_pud->items[_pud->length - itemIndex]);

            const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
            artRender(inventoryFrmId, windowBuffer + offset, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, pitch);
            _display_inventory_info(inventoryItem->item, inventoryItem->quantity, windowBuffer + offset, pitch, slotIndex == dragSlotIndex);

            if (inventoryWindowType != INVENTORY_WINDOW_TYPE_LOOT) {
                y += INVENTORY_SLOT_HEIGHT;
            }
        }
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        if (gInventoryRightHandItem != nullptr) {
            int width = gInventoryRightHandItem == gInventoryLeftHandItem ? INVENTORY_LARGE_SLOT_WIDTH * 2 : INVENTORY_LARGE_SLOT_WIDTH;
            const FrmId inventoryFrmId = itemGetInventoryFrmId(gInventoryRightHandItem);
            artRender(inventoryFrmId, windowBuffer + pitch * INVENTORY_RIGHT_HAND_SLOT_Y + inventoryLayout.rightHandSlotX, width, INVENTORY_LARGE_SLOT_HEIGHT, pitch);
        }

        if (gInventoryLeftHandItem != nullptr && gInventoryLeftHandItem != gInventoryRightHandItem) {
            const FrmId inventoryFrmId = itemGetInventoryFrmId(gInventoryLeftHandItem);
            artRender(inventoryFrmId, windowBuffer + pitch * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX, INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, pitch);
        }

        if (gInventoryArmor != nullptr) {
            const FrmId inventoryFrmId = itemGetInventoryFrmId(gInventoryArmor);
            artRender(inventoryFrmId, windowBuffer + pitch * INVENTORY_ARMOR_SLOT_Y + inventoryLayout.armorSlotX, INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, pitch);
        }
    }

    // CE: Show items weight.
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        inventoryLootRenderPaneWeight(windowBuffer, pitch, false, _stack[0], 0);
    }

    windowRefresh(gInventoryWindow);
}

// Render inventory item.
//
// [stackOffset] is an index of the first visible item in the scrolling view.
// [dragSlotIndex] is an index of item being dragged (it decreases displayed number of items in inner functions).
//
// 0x47036C
static void _display_target_inventory(int stackOffset, int dragSlotIndex, Inventory* inventory, int inventoryWindowType)
{
    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);

    int pitch;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        pitch = inventoryLootLayout.windowWidth;

        unsigned char* backgroundData = inventoryLootFrmImage.getData();
        int backgroundWidth = inventoryLootFrmImage.getWidth();
        if (backgroundData != nullptr) {
            blitBufferToBuffer(backgroundData + backgroundWidth * inventoryLootLayout.rightScrollerY + inventoryLootLayout.rightScrollerX,
                inventoryLootLayout.scrollerWidth,
                inventoryLootLayout.scrollerHeight,
                backgroundWidth,
                windowBuffer + pitch * inventoryLootLayout.rightScrollerY + inventoryLootLayout.rightScrollerX,
                pitch);
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        pitch = INVENTORY_TRADE_WINDOW_WIDTH;

        unsigned char* src = windowGetBuffer(gInventoryBarterBackgroundWindow);
        blitBufferToBuffer(src + INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH * INVENTORY_TRADE_RIGHT_SCROLLER_Y + INVENTORY_TRADE_RIGHT_SCROLLER_X + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount, INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_RIGHT_SCROLLER_Y + INVENTORY_TRADE_RIGHT_SCROLLER_X, INVENTORY_TRADE_WINDOW_WIDTH);
    } else {
        assert(false && "Should be unreachable");
    }

    int y = 0;
    for (int slotIndex = 0; slotIndex < gInventorySlotsCount; slotIndex++) {
        int itemIndex = stackOffset + slotIndex;
        if (itemIndex >= inventory->length) {
            break;
        }

        int offset;
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            offset = pitch * (inventoryLootGetSlotY(slotIndex) + INVENTORY_SLOT_PADDING) + inventoryLootGetSlotX(true, slotIndex) + INVENTORY_SLOT_PADDING;
        } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
            offset = pitch * (y + INVENTORY_TRADE_RIGHT_SCROLLER_Y_PAD) + INVENTORY_TRADE_RIGHT_SCROLLER_X_PAD;
        } else {
            assert(false && "Should be unreachable");
        }

        InventoryItem* inventoryItem = &(inventory->items[inventory->length - (itemIndex + 1)]);
        const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
        artRender(inventoryFrmId, windowBuffer + offset, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, pitch);
        _display_inventory_info(inventoryItem->item, inventoryItem->quantity, windowBuffer + offset, pitch, slotIndex == dragSlotIndex);

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
            y += INVENTORY_SLOT_HEIGHT;
        }
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        if (gSecondaryInventoryScrollUpButton != -1) {
            if (stackOffset <= 0) {
                buttonDisable(gSecondaryInventoryScrollUpButton);
            } else {
                buttonEnable(gSecondaryInventoryScrollUpButton);
            }
        }

        if (gSecondaryInventoryScrollDownButton != -1) {
            if (inventory->length - stackOffset <= gInventorySlotsCount) {
                buttonDisable(gSecondaryInventoryScrollDownButton);
            } else {
                buttonEnable(gSecondaryInventoryScrollDownButton);
            }
        }
    }

    // CE: Show items weight.
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        inventoryLootRenderPaneWeight(windowBuffer, pitch, true, _target_stack[_target_curr_stack], partyRightEquippedWeight);
        if (hasPartySlots()) {
            renderPartySlots();
        }
    }
}

// Renders inventory item quantity.
//
// 0x4705A0
static void _display_inventory_info(Object* item, int quantity, unsigned char* dest, int pitch, bool isDragged)
{
    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    char formattedText[12];

    // NOTE: Original code is slightly different and probably used goto.
    int displayQuantity = 0;
    if (itemGetType(item) == ITEM_TYPE_AMMO) {
        displayQuantity = ammoGetCapacity(item) * (quantity - 1);
        if (!isDragged) {
            displayQuantity += ammoGetQuantity(item);
        }
    } else {
        if (quantity > 1) {
            displayQuantity = quantity;
            if (isDragged) {
                // Note: can display "x1" during drag, which is otherwise not possible.
                displayQuantity -= 1;
            }
        }
    }

    if (displayQuantity > 0) {
        snprintf(formattedText, sizeof(formattedText), "x%d", std::clamp(displayQuantity, 0, 99999));
        fontDrawText(dest, formattedText, 80, pitch, COLOR_WHITE);
    }

    fontSetCurrent(oldFont);
}

// 0x470650
static void _display_body(const FrmId& frmId, int inventoryWindowType)
{
    if (getTicksSince(gInventoryWindowDudeRotationTimestamp) < INVENTORY_NORMAL_WINDOW_PC_ROTATION_DELAY) {
        return;
    }

    gInventoryWindowDudeRotation = gInventoryWindowDudeRotation + 1;

    if (gInventoryWindowDudeRotation == ROTATION_COUNT) {
        gInventoryWindowDudeRotation = ROTATION_FIRST;
    }

    Rotation rotations[2];
    if (!frmId.valid()) {
        rotations[0] = gInventoryWindowDudeRotation;
        rotations[1] = ROTATION_SE;
    } else {
        rotations[0] = ROTATION_SW;
        rotations[1] = inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT && hasPartySlots()
            ? ROTATION_SW
            : _target_stack[_target_curr_stack]->rotation;
    }

    const FrmId frmIds[2] = {
        gInventoryWindowDudeFrmId,
        frmId,
    };

    for (int index = 0; index < 2; index += 1) {
        FrmId fid = frmIds[index];
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT && index == 1) {
            fid = getTargetDisplayFrmId();
        }
        if (!fid.valid()) {
            continue;
        }

        CacheEntry* handle;
        Art* art = artLock(fid, &handle);
        if (art == nullptr) {
            continue;
        }

        int frame = 0;
        if (index == 1) {
            frame = artGetFrameCount(art) - 1;
        }

        Rotation rotation = rotations[index];

        unsigned char* frameData = artGetFrameData(art, frame, rotation);

        int framePitch = artGetWidth(art, frame, rotation);
        int frameWidth = std::min(framePitch, INVENTORY_BODY_VIEW_WIDTH);
        int frameHeight = std::min(artGetHeight(art, frame, rotation), INVENTORY_BODY_VIEW_HEIGHT);

        int win;
        Rect rect;
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
            unsigned char* windowBuffer = windowGetBuffer(gInventoryBarterBackgroundWindow);
            int windowPitch = windowGetWidth(gInventoryBarterBackgroundWindow);

            if (index == 1) {
                rect.left = 560;
                rect.top = 25;
            } else {
                rect.left = INVENTORY_BARTER_LEFT_BODY_VIEW_X;
                rect.top = INVENTORY_BARTER_LEFT_BODY_VIEW_Y;
            }

            rect.right = rect.left + INVENTORY_BODY_VIEW_WIDTH - 1;
            rect.bottom = rect.top + INVENTORY_BODY_VIEW_HEIGHT - 1;

            FrmImage backgroundFrmImage;
            const InterfaceFrmId backgroundFid = gGameDialogSpeakerIsPartyMember ? InterfaceFrameId::TradeWindow : InterfaceFrameId::BarterWindow;
            if (backgroundFrmImage.lock(backgroundFid)) {
                blitBufferToBuffer(backgroundFrmImage.getData() + rect.top * 640 + rect.left,
                    INVENTORY_BODY_VIEW_WIDTH,
                    INVENTORY_BODY_VIEW_HEIGHT,
                    640,
                    windowBuffer + windowPitch * rect.top + rect.left,
                    windowPitch);
            }

            blitBufferToBufferTrans(frameData, frameWidth, frameHeight, framePitch,
                windowBuffer + windowPitch * (rect.top + (INVENTORY_BODY_VIEW_HEIGHT - frameHeight) / 2) + (INVENTORY_BODY_VIEW_WIDTH - frameWidth) / 2 + rect.left,
                windowPitch);

            displayLootPanePartyName(windowBuffer, windowPitch, rect, index);

            win = gInventoryBarterBackgroundWindow;
        } else {
            unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
            int windowPitch = windowGetWidth(gInventoryWindow);

            FrmImage backgroundFrmImage;
            InterfaceFrameId Fid = InterfaceFrameId::LootContainerWindow;
            int sourceXOffset = 0;

            if (index == 1) {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
                    rect.left = inventoryLootLayout.rightBodyViewX;
                    rect.top = INVENTORY_LOOT_RIGHT_BODY_VIEW_Y;
                } else {
                    rect.left = 297; // inventory data window? ?not used?
                    rect.top = 37;
                    Fid = InterfaceFrameId::InventoryBox;
                    sourceXOffset = 229;
                }
            } else {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
                    rect.left = inventoryLootLayout.leftBodyViewX;
                    rect.top = INVENTORY_LOOT_LEFT_BODY_VIEW_Y;
                } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_USE_ITEM_ON) {
                    rect.left = 176; // Use item cha window
                    rect.top = 37;
                    Fid = InterfaceFrameId::UseItemOnWindow;
                    sourceXOffset = 292;
                } else {
                    rect.left = inventoryLayout.bodyViewX; // inventory cha window
                    rect.top = 37;
                }
            }

            rect.right = rect.left + INVENTORY_BODY_VIEW_WIDTH - 1;
            rect.bottom = rect.top + INVENTORY_BODY_VIEW_HEIGHT - 1;

            Buffer2D dst { windowBuffer, windowPitch, windowGetHeight(gInventoryWindow) };
            ConstBuffer2D background = inventoryGetBackgroundBuffer(inventoryWindowType, Fid, backgroundFrmImage, sourceXOffset);
            if (background) {
                blitBuffer2D(background,
                    rect.left + sourceXOffset,
                    rect.top,
                    INVENTORY_BODY_VIEW_WIDTH,
                    INVENTORY_BODY_VIEW_HEIGHT,
                    dst,
                    rect.left,
                    rect.top);
            }

            blitBufferToBufferTrans(frameData, frameWidth, frameHeight, framePitch,
                windowBuffer + windowPitch * (rect.top + (INVENTORY_BODY_VIEW_HEIGHT - frameHeight) / 2) + (INVENTORY_BODY_VIEW_WIDTH - frameWidth) / 2 + rect.left,
                windowPitch);

            if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
                displayLootPanePartyName(windowBuffer, windowPitch, rect, index);
            }

            win = gInventoryWindow;
        }
        windowRefreshRect(win, &rect);

        artUnlock(handle);
    }

    gInventoryWindowDudeRotationTimestamp = getTicks();
}

// 0x470A2C
static int inventoryCommonInit()
{
    if (inventoryMessageListInit() == -1) {
        return -1;
    }

    _inven_ui_was_disabled = gameUiIsDisabled();

    if (_inven_ui_was_disabled) {
        gameUiEnable();
    }

    gameMouseObjectsHide();

    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    int index;
    for (index = 0; index < INVENTORY_WINDOW_CURSOR_COUNT; index++) {
        InventoryCursorData* cursorData = &(gInventoryCursorData[index]);

        Art* frm = artLock(kInventoryWindowCursorFrmIds[index], &(cursorData->frmHandle));
        if (frm == nullptr) {
            break;
        }

        cursorData->frm = frm;
        cursorData->frmData = artGetFrameData(frm);
        cursorData->width = artGetWidth(frm);
        cursorData->height = artGetHeight(frm);
        artGetFrameOffsets(frm, 0, ROTATION_NE, &(cursorData->offsetX), &(cursorData->offsetY));
    }

    if (index != INVENTORY_WINDOW_CURSOR_COUNT) {
        for (; index >= 0; index--) {
            artUnlock(gInventoryCursorData[index].frmHandle);
        }

        if (_inven_ui_was_disabled) {
            gameUiDisable(0);
        }

        inventoryMessageListFree();
        gInventoryMessageListLoaded = false;

        return -1;
    }

    _inven_is_initialized = true;
    _im_value = -1;

    return 0;
}

// NOTE: Inlined.
//
// 0x470B8C
static void inventoryCommonFree()
{
    for (int index = 0; index < INVENTORY_WINDOW_CURSOR_COUNT; index++) {
        artUnlock(gInventoryCursorData[index].frmHandle);
    }

    if (_inven_ui_was_disabled) {
        gameUiDisable(0);
    }

    // NOTE: Uninline.
    inventoryMessageListFree();

    _inven_is_initialized = 0;
}

// 0x470BCC
static void inventorySetCursor(int cursor)
{
    gInventoryCursor = cursor;

    if (cursor != INVENTORY_WINDOW_CURSOR_ARROW || _im_value == -1) {
        InventoryCursorData* cursorData = &(gInventoryCursorData[cursor]);
        mouseSetFrame(cursorData->frmData, cursorData->width, cursorData->height, cursorData->width, cursorData->offsetX, cursorData->offsetY, 0);
    } else {
        inventoryItemSlotOnMouseEnter(-1, _im_value);
    }
}

// 0x470C2C
static void inventoryItemSlotOnMouseEnter(int btn, int keyCode)
{
    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
        int x;
        int y;
        mouseGetPositionInWindow(gInventoryWindow, &x, &y);

        Object* item = nullptr;
        if (_inven_from_button(keyCode, &item, nullptr, nullptr) != 0) {
            gameMouseRenderPrimaryAction(x, y, GAME_MOUSE_ACTION_MENU_ITEM_LOOK, gInventoryWindowMaxX, gInventoryWindowMaxY);

            int cursorHotspotX = 0;
            int cursorHotspotY = 0;
            _gmouse_3d_pick_frame_hot(&cursorHotspotX, &cursorHotspotY);

            InventoryCursorData* cursorData = &(gInventoryCursorData[INVENTORY_WINDOW_CURSOR_PICK]);
            mouseSetFrame(cursorData->frmData, cursorData->width, cursorData->height, cursorData->width, cursorHotspotX, cursorHotspotY, 0);

            if (item != _last_target) {
                objectLookAtFunc(_stack[0], item, gInventoryPrintItemDescriptionHandler);
            }
        } else {
            InventoryCursorData* cursorData = &(gInventoryCursorData[INVENTORY_WINDOW_CURSOR_ARROW]);
            mouseSetFrame(cursorData->frmData, cursorData->width, cursorData->height, cursorData->width, cursorData->offsetX, cursorData->offsetY, 0);
        }

        _last_target = item;
    }

    _im_value = keyCode;
}

// 0x470D1C
static void inventoryItemSlotOnMouseExit(int btn, int keyCode)
{
    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
        InventoryCursorData* cursorData = &(gInventoryCursorData[INVENTORY_WINDOW_CURSOR_ARROW]);
        mouseSetFrame(cursorData->frmData, cursorData->width, cursorData->height, cursorData->width, cursorData->offsetX, cursorData->offsetY, 0);
    }

    _im_value = -1;
}

// 0x470D5C
static void _inven_update_lighting(Object* activeItem)
{
    if (gDude == _inven_dude) {
        int lightDistance;
        if (activeItem != nullptr && activeItem->lightDistance > 4) {
            lightDistance = activeItem->lightDistance;
        } else {
            lightDistance = 4;
        }

        Rect rect;
        objectSetLight(_inven_dude, lightDistance, 0x10000, &rect);
        presenter()->worldInvalidateRect(&rect, gElevation);
    }
}

// 0x470DB8
static void _inven_pickup(int buttonCode, int indexOffset)
{
    Object* item;
    Object** itemSlot = nullptr;
    int count = _inven_from_button(buttonCode, &item, &itemSlot, nullptr);
    if (count == 0) {
        return;
    }

    int itemIndex = -1;
    Object* itemInHand = nullptr;
    Rect rect;

    switch (buttonCode) {
    case INVENTORY_HAND_RIGHT_KEY:
        rect.left = inventoryLayout.rightHandSlotX;
        rect.top = INVENTORY_RIGHT_HAND_SLOT_Y;
        if (_inven_dude == gDude && interfaceGetCurrentHand() != HAND_LEFT) {
            itemInHand = item;
        }
        break;
    case INVENTORY_HAND_LEFT_KEY:
        rect.left = inventoryLayout.leftHandSlotX;
        rect.top = INVENTORY_LEFT_HAND_SLOT_Y;
        if (_inven_dude == gDude && interfaceGetCurrentHand() == HAND_LEFT) {
            itemInHand = item;
        }
        break;
    case INVENTORY_ARMOR_KEY:
        rect.left = inventoryLayout.armorSlotX;
        rect.top = INVENTORY_ARMOR_SLOT_Y;
        break;
    default:
        // Normal inventory slot buttons use 1000-based key codes; convert
        // the button code to a zero-based slot index for grid positioning.
        itemIndex = buttonCode - 1000;
        rect.left = inventoryLayout.scrollerX + itemIndex % inventoryLayout.columns * INVENTORY_SLOT_WIDTH;
        rect.top = INVENTORY_SLOT_HEIGHT * (itemIndex / inventoryLayout.columns) + inventoryLayout.scrollerY;
        break;
    }

    bool pickUpFromSlot = itemIndex == -1; // true if item was picked up from armor or weapon slots
    if (pickUpFromSlot || _pud->items[_pud->length - (itemIndex + indexOffset + 1)].quantity <= 1) {
        // erase background unless item is part of a 2+ quantity stack
        unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
        int width, height;
        if (gInventoryRightHandItem != gInventoryLeftHandItem || item != gInventoryLeftHandItem) {
            if (pickUpFromSlot) {
                height = INVENTORY_LARGE_SLOT_HEIGHT;
                width = INVENTORY_LARGE_SLOT_WIDTH;
            } else {
                height = INVENTORY_SLOT_HEIGHT;
                width = INVENTORY_SLOT_WIDTH;
            }
        } else {
            // seems to wipe both hand slots at once, but I don't know how to trigger this in game
            height = INVENTORY_LARGE_SLOT_HEIGHT;
            width = 180;
            rect.left = inventoryLayout.leftHandSlotX;
            rect.top = INVENTORY_LEFT_HAND_SLOT_Y;
        }
        rect.right = rect.left + width - 1;
        rect.bottom = rect.top + height - 1;
        unsigned char* backgroundData = inventoryFrmImage.getData();
        int backgroundWidth = inventoryFrmImage.getWidth();
        if (backgroundData != nullptr) {
            blitBufferToBuffer(backgroundData + backgroundWidth * rect.top + rect.left,
                width,
                height,
                backgroundWidth,
                windowBuffer + inventoryLayout.windowWidth * rect.top + rect.left,
                inventoryLayout.windowWidth);
        }

        windowRefreshRect(gInventoryWindow, &rect);
    } else {
        _display_inventory(indexOffset, itemIndex, INVENTORY_WINDOW_TYPE_NORMAL);
    }

    if (itemInHand != nullptr) {
        _inven_update_lighting(nullptr);
    }

    // allow ctrl-click to quick unequip or equip item
    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    // A viewer owns the drag gesture; the server owns every inventory change.
    if (clientViewerActive()) {
        bool inList = mouseHitTestInWindow(gInventoryWindow, inventoryLayout.scrollerX, inventoryLayout.scrollerY,
            inventoryLayout.scrollerX + inventoryLayout.scrollerWidth, inventoryLayout.scrollerY + inventoryLayout.scrollerHeight);
        bool inLeft = mouseHitTestInWindow(gInventoryWindow, inventoryLayout.leftHandSlotX, INVENTORY_LEFT_HAND_SLOT_Y,
            inventoryLayout.leftHandSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LEFT_HAND_SLOT_MAX_Y);
        bool inRight = mouseHitTestInWindow(gInventoryWindow, inventoryLayout.rightHandSlotX, INVENTORY_RIGHT_HAND_SLOT_Y,
            inventoryLayout.rightHandSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_RIGHT_HAND_SLOT_MAX_Y);
        bool inArmor = mouseHitTestInWindow(gInventoryWindow, inventoryLayout.armorSlotX, INVENTORY_ARMOR_SLOT_Y,
            inventoryLayout.armorSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_ARMOR_SLOT_MAX_Y);
        auto canLoadAmmo = [&](Object* weapon) {
            return weapon != nullptr && itemGetType(weapon) == ITEM_TYPE_WEAPON
                && itemGetType(item) == ITEM_TYPE_AMMO
                && (weaponCanBeReloadedWith(weapon, item)
                    || (settings.qol.fast_ammo_load
                        && weaponCanBeUnloaded(weapon)
                        && weaponCanBeReloadedWithReplacingAmmo(weapon, item)));
        };
        bool handledListDrop = false;
        if (!immediate && inList) {
            int x;
            int y;
            mouseGetPositionInWindow(gInventoryWindow, &x, &y);
            int row = (y - inventoryLayout.scrollerY) / INVENTORY_SLOT_HEIGHT;
            int column = (x - inventoryLayout.scrollerX) / INVENTORY_SLOT_WIDTH;
            row = std::clamp(row, 0, INVENTORY_ROWS - 1);
            column = std::clamp(column, 0, inventoryLayout.columns - 1);
            int targetIndex = row * inventoryLayout.columns + column + indexOffset;
            if (targetIndex < _pud->length) {
                Object* targetItem = _pud->items[_pud->length - (targetIndex + 1)].item;
                if (targetItem != item && itemGetType(targetItem) == ITEM_TYPE_CONTAINER) {
                    _drop_into_container(targetItem, item, itemIndex, itemSlot, count);
                    handledListDrop = true;
                } else if (targetItem != item && canLoadAmmo(targetItem)) {
                    _drop_ammo_into_weapon(targetItem, item, itemSlot, count, buttonCode);
                    handledListDrop = true;
                }
            }
        }
        if (!handledListDrop && pickUpFromSlot && (immediate || inList)) {
            int hand = itemSlot == &gInventoryArmor ? HAND_COUNT : (itemSlot == &gInventoryLeftHandItem ? HAND_LEFT : HAND_RIGHT);
            clientViewerUnwield(hand);
        } else if (!handledListDrop && (immediate || inArmor) && itemGetType(item) == ITEM_TYPE_ARMOR) {
            clientViewerWield(item, HAND_LEFT);
        } else if (!handledListDrop && !pickUpFromSlot && immediate && itemGetType(item) == ITEM_TYPE_AMMO) {
            InventoryAmmoMoveResult ammoMoveResult = _drop_ammo_into_weapon(gInventoryLeftHandItem, item, itemSlot, count, buttonCode);
            if (ammoMoveResult == INVENTORY_AMMO_MOVE_RESULT_FAILED) {
                _drop_ammo_into_weapon(gInventoryRightHandItem, item, itemSlot, count, buttonCode);
            }
        } else if (!handledListDrop && (inLeft || inRight || (immediate && itemGetType(item) != ITEM_TYPE_AMMO))) {
            Hand hand = inRight ? HAND_RIGHT : HAND_LEFT;
            if (immediate) hand = gInventoryLeftHandItem == nullptr || gInventoryRightHandItem != nullptr ? HAND_LEFT : HAND_RIGHT;
            Object* equipped = hand == HAND_LEFT ? gInventoryLeftHandItem : gInventoryRightHandItem;
            bool containerStore = equipped != nullptr && equipped != item && itemGetType(equipped) == ITEM_TYPE_CONTAINER;
            bool ammoLoad = canLoadAmmo(equipped);
            if (containerStore) {
                _drop_into_container(equipped, item, itemIndex, itemSlot, count);
            } else if (ammoLoad) {
                _drop_ammo_into_weapon(equipped, item, itemSlot, count, buttonCode);
            } else {
                clientViewerWield(item, hand);
            }
        } else if (!handledListDrop && _curr_stack == 0 && !mouseHitTestInWindow(gInventoryWindow, 0, 0, inventoryLayout.windowWidth, inventoryLayout.windowHeight)) {
            int quantity = inventoryViewerDropQuantity(item, itemSlot, count);
            if (quantity != -1) clientViewerDrop(item, quantity);
        }
        if (itemInHand != nullptr) _inven_update_lighting(itemInHand);
        _display_inventory(indexOffset, -1, INVENTORY_WINDOW_TYPE_NORMAL);
        inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
        return;
    }


    // drag into inventory list, or ctrl-click from slot
    if (pickUpFromSlot && (immediate || mouseHitTestInWindow(gInventoryWindow, inventoryLayout.scrollerX, inventoryLayout.scrollerY, inventoryLayout.scrollerX + inventoryLayout.scrollerWidth, inventoryLayout.scrollerY + inventoryLayout.scrollerHeight))) {
        int x;
        int y;
        mouseGetPositionInWindow(gInventoryWindow, &x, &y);

        int row = (y - inventoryLayout.scrollerY) / INVENTORY_SLOT_HEIGHT;
        int column = (x - inventoryLayout.scrollerX) / INVENTORY_SLOT_WIDTH;
        if (row < 0) {
            row = 0;
        } else if (row >= INVENTORY_ROWS) {
            row = INVENTORY_ROWS - 1;
        }
        if (column < 0) {
            column = 0;
        } else if (column >= inventoryLayout.columns) {
            column = inventoryLayout.columns - 1;
        }

        int targetIndex = row * inventoryLayout.columns + column + indexOffset;
        if (!immediate && targetIndex < _pud->length) {
            Object* targetItem = _pud->items[_pud->length - (targetIndex + 1)].item;
            if (targetItem != item) {
                // Dropping item on top of another item.
                if (itemGetType(targetItem) == ITEM_TYPE_CONTAINER) {
                    if (_drop_into_container(targetItem, item, itemIndex, itemSlot, count) == 0) {
                        itemIndex = 0;
                    }
                } else {
                    if (_drop_ammo_into_weapon(targetItem, item, itemSlot, count, buttonCode) == INVENTORY_AMMO_MOVE_RESULT_SUCCESS) {
                        itemIndex = 0;
                    }
                }
            }
        }

        if (immediate || itemIndex == -1) {
            if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_MAIN_BACKPACK, item, nullptr)) {
                goto inventory_move_done;
            }

            // TODO: Holy shit, needs refactoring.
            *itemSlot = nullptr;
            if (itemAdd(_inven_dude, item, 1)) {
                *itemSlot = item;
            } else if (itemSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], item, nullptr);
            } else if (gInventoryRightHandItem == gInventoryLeftHandItem) {
                gInventoryLeftHandItem = nullptr;
                gInventoryRightHandItem = nullptr;
            }
        }

    } else if (!pickUpFromSlot && immediate && itemGetType(item) == ITEM_TYPE_AMMO) {
        InventoryAmmoMoveResult ammoMoveResult = _drop_ammo_into_weapon(gInventoryLeftHandItem, item, itemSlot, count, buttonCode);
        if (ammoMoveResult == INVENTORY_AMMO_MOVE_RESULT_FAILED) {
            ammoMoveResult = _drop_ammo_into_weapon(gInventoryRightHandItem, item, itemSlot, count, buttonCode);
        }
    } else if (!pickUpFromSlot && immediate && itemGetType(item) != ITEM_TYPE_ARMOR) {
        // ctrl-click non-armor to quick-equip:
        // default to first empty hand, or left hand if both are full
        bool left = gInventoryLeftHandItem == nullptr || gInventoryRightHandItem != nullptr;
        if (left) {
            _switch_hand(item, &gInventoryLeftHandItem, itemSlot, itemIndex);
        } else {
            _switch_hand(item, &gInventoryRightHandItem, itemSlot, itemIndex);
        }

        // drop in left hand slot
    } else if (mouseHitTestInWindow(gInventoryWindow, inventoryLayout.leftHandSlotX, INVENTORY_LEFT_HAND_SLOT_Y, inventoryLayout.leftHandSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LEFT_HAND_SLOT_MAX_Y)) {
        if (gInventoryLeftHandItem != nullptr && itemGetType(gInventoryLeftHandItem) == ITEM_TYPE_CONTAINER && gInventoryLeftHandItem != item) {
            _drop_into_container(gInventoryLeftHandItem, item, itemIndex, itemSlot, count);
        } else if (gInventoryLeftHandItem == nullptr) {
            _switch_hand(item, &gInventoryLeftHandItem, itemSlot, itemIndex);
        } else if (_drop_ammo_into_weapon(gInventoryLeftHandItem, item, itemSlot, count, buttonCode) == INVENTORY_AMMO_MOVE_RESULT_FAILED) {
            _switch_hand(item, &gInventoryLeftHandItem, itemSlot, itemIndex);
        }

        // drop in right hand slot
    } else if (mouseHitTestInWindow(gInventoryWindow, inventoryLayout.rightHandSlotX, INVENTORY_RIGHT_HAND_SLOT_Y, inventoryLayout.rightHandSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_RIGHT_HAND_SLOT_MAX_Y)) {
        if (gInventoryRightHandItem != nullptr && itemGetType(gInventoryRightHandItem) == ITEM_TYPE_CONTAINER && gInventoryRightHandItem != item) {
            _drop_into_container(gInventoryRightHandItem, item, itemIndex, itemSlot, count);
        } else if (gInventoryRightHandItem == nullptr) {
            _switch_hand(item, &gInventoryRightHandItem, itemSlot, itemIndex);
        } else if (_drop_ammo_into_weapon(gInventoryRightHandItem, item, itemSlot, count, buttonCode) == INVENTORY_AMMO_MOVE_RESULT_FAILED) {
            _switch_hand(item, &gInventoryRightHandItem, itemSlot, itemIndex);
        }

    } else if ((immediate && itemGetType(item) == ITEM_TYPE_ARMOR) || mouseHitTestInWindow(gInventoryWindow, inventoryLayout.armorSlotX, INVENTORY_ARMOR_SLOT_Y, inventoryLayout.armorSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_ARMOR_SLOT_MAX_Y)) {
        if (itemGetType(item) == ITEM_TYPE_ARMOR) {
            Object* currentArmor = gInventoryArmor;
            if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_ARMOR_SLOT, item, currentArmor)) {
                goto inventory_move_done;
            }

            int itemAddResult = 0;
            if (itemIndex != -1) {
                itemRemoveWithReason(_inven_dude, item, 1, RemoveInventoryObjectHookReason::EquipArmor);
            }

            if (gInventoryArmor != nullptr) {
                if (itemSlot != nullptr) {
                    *itemSlot = gInventoryArmor;
                } else {
                    gInventoryArmor = nullptr;
                    itemAddResult = itemAdd(_inven_dude, currentArmor, 1);
                }
            } else {
                if (itemSlot != nullptr) {
                    *itemSlot = gInventoryArmor;
                }
            }

            if (itemAddResult != 0) {
                gInventoryArmor = currentArmor;
                if (itemIndex != -1) {
                    itemAdd(_inven_dude, item, 1);
                }
            } else {
                adjustCritterStatsOnArmorChange(_stack[0], currentArmor, item);
                gInventoryArmor = item;
            }
        }
    } else if (mouseHitTestInWindow(gInventoryWindow, inventoryLayout.bodyViewX, INVENTORY_PC_BODY_VIEW_Y, inventoryLayout.bodyViewX + INVENTORY_BODY_VIEW_WIDTH, INVENTORY_PC_BODY_VIEW_MAX_Y)) {
        if (_curr_stack == 0) {
            // Call the hook when dropping item on the PC portrait when not in a container.  Return value is irrelevant.
            if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_CHARACTER_PORTRAIT, item, nullptr)) {
                goto inventory_move_done;
            }
        } else {
            // If we are looking inside nested inventory (such as backpack item), we see this item in the PC Body View instead of the player.
            // So we drop item into it.
            _drop_into_container(_stack[_curr_stack - 1], item, itemIndex, itemSlot, count);
        }
    }

inventory_move_done:
    _adjust_fid();
    inventoryRenderSummary();
    _display_inventory(indexOffset, -1, INVENTORY_WINDOW_TYPE_NORMAL);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
    if (_inven_dude == gDude) {
        Object* item;
        if (interfaceGetCurrentHand() == HAND_LEFT) {
            item = critterGetItem1(_inven_dude);
        } else {
            item = critterGetItem2(_inven_dude);
        }

        if (item != nullptr) {
            _inven_update_lighting(item);
        }
    }
}

static void _switch_hand(Object* sourceItem, Object** targetSlot, Object** sourceSlot, int itemIndex)
{
    if (*targetSlot != nullptr) {
        if (itemGetType(*targetSlot) == ITEM_TYPE_WEAPON && itemGetType(sourceItem) == ITEM_TYPE_AMMO) {
            return;
        }
    }

    HookInventoryMoveType targetSlotType = targetSlot == &gInventoryLeftHandItem
        ? HOOK_INVENTORYMOVE_LEFT_HAND
        : HOOK_INVENTORYMOVE_RIGHT_HAND;
    if (!scriptHooks_InventoryMove(targetSlotType, sourceItem, *targetSlot)) {
        return;
    }

    if (*targetSlot != nullptr) {
        if (sourceSlot != nullptr && (sourceSlot != &gInventoryArmor || itemGetType(*targetSlot) == ITEM_TYPE_ARMOR)) {
            if (sourceSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], gInventoryArmor, *targetSlot);
            }
            *sourceSlot = *targetSlot;
        } else {
            if (itemIndex != -1) {
                itemRemoveWithReason(_inven_dude, sourceItem, 1, RemoveInventoryObjectHookReason::EquipWeapon);
            }

            Object* existingItem = *targetSlot;
            *targetSlot = nullptr;
            if (itemAdd(_inven_dude, existingItem, 1) != 0) {
                itemAdd(_inven_dude, sourceItem, 1);
                return;
            }

            itemIndex = -1;

            if (sourceSlot != nullptr) {
                if (sourceSlot == &gInventoryArmor) {
                    adjustCritterStatsOnArmorChange(_stack[0], gInventoryArmor, nullptr);
                }
                *sourceSlot = nullptr;
            }
        }
    } else {
        if (sourceSlot != nullptr) {
            if (sourceSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], gInventoryArmor, nullptr);
            }
            *sourceSlot = nullptr;
        }
    }

    *targetSlot = sourceItem;

    if (itemIndex != -1) {
        itemRemoveWithReason(_inven_dude, sourceItem, 1, RemoveInventoryObjectHookReason::EquipWeapon);
    }
}

// 0x4716E8
static void _adjust_fid()
{
    const FrmId frmId = inventoryComputeCritterFrmId(_inven_dude,
        inventoryProtoId,
        gInventoryRightHandItem,
        gInventoryLeftHandItem,
        gInventoryArmor,
        interfaceGetCurrentHand(),
        ANIM_STAND,
        ROTATION_NE);
    gInventoryWindowDudeFrmId = scriptHooks_AdjustFid(frmId, frmId);
}

// 0x4717E4
void inventoryOpenUseItemOn(Object* targetObj)
{
    if (inventoryCommonInit() == -1) {
        return;
    }

    bool isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_USE_ITEM_ON);
    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_USE_ITEM_ON);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
    ScopedGameMode gm(GameMode::kUseOn);
    for (;;) {
        sharedFpsLimiter.mark();

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        _display_body(FrmId::Empty(), INVENTORY_WINDOW_TYPE_USE_ITEM_ON);

        int keyCode = inputGetInput();
        int mouseEvent = mouseGetEvent();
        InventoryScrollerDisplayContext inventoryScrollerContext { INVENTORY_WINDOW_TYPE_USE_ITEM_ON, nullptr };
        InventoryScroller useItemOnScroller {
            { INVENTORY_SCROLLER_X, INVENTORY_SCROLLER_Y, INVENTORY_SCROLLER_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_SCROLLER_Y },
            &(_stack_offset[_curr_stack]),
            1,
            gInventorySlotsCount,
            _pud->length,
            KEY_ARROW_UP,
            KEY_ARROW_DOWN,
            KEY_PAGE_UP,
            KEY_PAGE_DOWN,
            KEY_HOME,
            KEY_END,
            inventoryScrollerRedrawInventory,
            &inventoryScrollerContext,
            false,
        };
        switch (keyCode) {
        case 2500:
            _container_exit(keyCode, INVENTORY_WINDOW_TYPE_USE_ITEM_ON);
            break;
        default:
            inventoryScrollerHandleInput(useItemOnScroller, keyCode, mouseEvent);

            if ((mouseEvent & MOUSE_EVENT_RIGHT_BUTTON_DOWN) != 0) {
                if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
                } else {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
                }
            } else if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0) {
                if (keyCode >= 1000 && keyCode < 1000 + gInventorySlotsCount) {
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_USE_ITEM_ON);
                    } else {
                        int inventoryItemIndex = _pud->length - (_stack_offset[_curr_stack] + keyCode - 1000 + 1);
                        // SFALL: Fix crash when clicking on empty space in the inventory list
                        // opened by "Use Inventory Item On" (backpack) action icon
                        if (inventoryItemIndex < _pud->length && inventoryItemIndex >= 0) {
                            InventoryItem* inventoryItem = &(_pud->items[inventoryItemIndex]);
                            if (clientViewerActive()) {
                                // Viewer: don't run the local action-on-object (it would
                                // mutate the authoritative dude/target locally). Send the
                                // claim-gated wire verb — the server walks the dude to the
                                // target and runs the real _action_use_an_item_on_object;
                                // the outcome streams back (Slice 2 reconcile). netId 0 =
                                // an unsynced target → drop the pick, just close.
                                if (targetObj->netId > 0) {
                                    clientViewerUseItemOn(targetObj->netId, inventoryItem->item->pid);
                                }
                            } else {
                                // Ledger H-2: AP gate + debit extracted to core
                                // (actions.cc).
                                actionUseItemOnObjectWithApCost(gDude, targetObj, inventoryItem->item);
                            }
                            // fix for click through bug
                            gBlockMouseUpEvent = true;
                            keyCode = KEY_ESCAPE;
                        } else {
                            keyCode = -1;
                        }
                    }
                }
            }
        }

        if (keyCode == KEY_ESCAPE) {
            break;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    _exit_inventory(isoWasEnabled);

    // NOTE: Uninline.
    inventoryCommonFree();
}

// Renders character's summary of SPECIAL stats, equipped armor bonuses,
// and weapon's damage/range.
//
// 0x471D5C
static void inventoryRenderSummary()
{
    Stat summaryStats[7];
    memcpy(summaryStats, gSummaryStats, sizeof(summaryStats));

    Stat summaryStats2[7];
    memcpy(summaryStats2, gSummaryStats2, sizeof(summaryStats2));

    char formattedText[80];

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch = inventoryLayout.windowWidth;
    int summaryX = inventoryLayout.summaryX;
    int summaryMaxX = summaryX + (INVENTORY_SUMMARY_MAX_X - INVENTORY_SUMMARY_X);

    unsigned char* backgroundData = inventoryFrmImage.getData();
    int backgroundWidth = inventoryFrmImage.getWidth();
    if (backgroundData != nullptr) {
        blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_SUMMARY_Y + summaryX,
            INVENTORY_SUMMARY_WIDTH,
            INVENTORY_SUMMARY_HEIGHT,
            backgroundWidth,
            windowBuffer + pitch * INVENTORY_SUMMARY_Y + summaryX,
            pitch);
    }

    // Render character name.
    const char* critterName = critterGetName(_stack[0]);
    fontDrawText(windowBuffer + pitch * INVENTORY_SUMMARY_Y + summaryX, critterName, 80, pitch, COLOR_GREEN);

    bufferDrawLine(windowBuffer,
        pitch,
        summaryX,
        3 * fontGetLineHeight() / 2 + INVENTORY_SUMMARY_Y,
        summaryMaxX,
        3 * fontGetLineHeight() / 2 + INVENTORY_SUMMARY_Y,
        COLOR_GREEN);

    MessageListItem messageListItem;

    int offset = pitch * 2 * fontGetLineHeight() + pitch * INVENTORY_SUMMARY_Y + summaryX;
    for (Stat stat = STAT_FIRST; stat < PRIMARY_STAT_COUNT; stat++) {
        messageListItem.num = stat;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            fontDrawText(windowBuffer + offset, messageListItem.text, 80, pitch, COLOR_GREEN);
        }

        int value = critterGetStat(_stack[0], stat);
        snprintf(formattedText, sizeof(formattedText), "%d", value);
        fontDrawText(windowBuffer + offset + 24, formattedText, 80, pitch, COLOR_GREEN);

        offset += pitch * fontGetLineHeight();
    }

    offset -= pitch * 7 * fontGetLineHeight();

    for (int index = 0; index < 7; index += 1) {
        messageListItem.num = 7 + index;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            fontDrawText(windowBuffer + offset + 40, messageListItem.text, 80, pitch, COLOR_GREEN);
        }

        if (summaryStats2[index] == STAT_INVALID) {
            int value = critterGetStat(_stack[0], summaryStats[index]);
            snprintf(formattedText, sizeof(formattedText), "   %d", value);
        } else {
            int value1 = critterGetStat(_stack[0], summaryStats[index]);
            int value2 = critterGetStat(_stack[0], summaryStats2[index]);
            const char* format = index != 0 ? "%d/%d%%" : "%d/%d";
            snprintf(formattedText, sizeof(formattedText), format, value1, value2);
        }

        fontDrawText(windowBuffer + offset + 104, formattedText, 80, pitch, COLOR_GREEN);

        offset += pitch * fontGetLineHeight();
    }

    bufferDrawLine(windowBuffer, pitch, summaryX, 18 * fontGetLineHeight() / 2 + 48, summaryMaxX, 18 * fontGetLineHeight() / 2 + 48, COLOR_GREEN);
    bufferDrawLine(windowBuffer, pitch, summaryX, 26 * fontGetLineHeight() / 2 + 48, summaryMaxX, 26 * fontGetLineHeight() / 2 + 48, COLOR_GREEN);

    Object* itemsInHands[2] = {
        gInventoryLeftHandItem,
        gInventoryRightHandItem,
    };

    const HitMode hitModes[2] = {
        HIT_MODE_LEFT_WEAPON_PRIMARY,
        HIT_MODE_RIGHT_WEAPON_PRIMARY,
    };

    const HitMode secondaryHitModes[2] = {
        HIT_MODE_LEFT_WEAPON_SECONDARY,
        HIT_MODE_RIGHT_WEAPON_SECONDARY,
    };

    const HitMode unarmedHitModes[2] = {
        HIT_MODE_PUNCH,
        HIT_MODE_KICK,
    };

    offset += pitch * fontGetLineHeight();

    for (int index = 0; index < 2; index += 1) {
        Object* item = itemsInHands[index];
        if (item == nullptr) {
            formattedText[0] = '\0';

            // No item
            messageListItem.num = 14;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                fontDrawText(windowBuffer + offset, messageListItem.text, 120, pitch, COLOR_GREEN);
            }

            offset += pitch * fontGetLineHeight();

            // Unarmed dmg:
            messageListItem.num = 24;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                // SFALL: Display the actual damage values of unarmed attacks.
                // CE: Implementation is different.
                HitMode hitMode = unarmedHitModes[index];
                if (_stack[0] == gDude) {
                    InterfaceItemAction actions[2];
                    interfaceGetItemActions(&(actions[0]), &(actions[1]));

                    bool isSecondary = actions[index] == INTERFACE_ITEM_ACTION_SECONDARY
                        || actions[index] == INTERFACE_ITEM_ACTION_SECONDARY_AIMING;

                    if (index == HAND_LEFT) {
                        hitMode = unarmedGetPunchHitMode(isSecondary);
                    } else {
                        hitMode = unarmedGetKickHitMode(isSecondary);
                    }
                }

                // Formula is the same as in `weaponGetDamage`.
                int minDamage;
                int maxDamage;
                int bonusDamage = unarmedGetDamage(hitMode, &minDamage, &maxDamage);
                int meleeDamage = critterGetStat(_stack[0], STAT_MELEE_DAMAGE);
                // TODO: Localize unarmed attack names.
                snprintf(formattedText, sizeof(formattedText), "%s %d-%d",
                    messageListItem.text,
                    bonusDamage + minDamage,
                    bonusDamage + meleeDamage + maxDamage);
            }

            fontDrawText(windowBuffer + offset, formattedText, 120, pitch, COLOR_GREEN);

            offset += 3 * pitch * fontGetLineHeight();
            continue;
        }

        const char* itemName = itemGetName(item);
        fontDrawText(windowBuffer + offset, itemName, 140, pitch, COLOR_GREEN);

        offset += pitch * fontGetLineHeight();

        ItemType itemType = itemGetType(item);
        if (itemType != ITEM_TYPE_WEAPON) {
            if (itemType == ITEM_TYPE_ARMOR) {
                // (Not worn)
                messageListItem.num = 18;
                if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                    fontDrawText(windowBuffer + offset, messageListItem.text, 120, pitch, COLOR_GREEN);
                }
            }

            offset += 3 * pitch * fontGetLineHeight();
            continue;
        }

        // SFALL: Fix displaying secondary mode weapon range.
        HitMode hitMode = hitModes[index];
        if (_stack[0] == gDude) {
            InterfaceItemAction actions[2];
            interfaceGetItemActions(&(actions[0]), &(actions[1]));

            bool isSecondary = actions[index] == INTERFACE_ITEM_ACTION_SECONDARY
                || actions[index] == INTERFACE_ITEM_ACTION_SECONDARY_AIMING;

            if (isSecondary) {
                hitMode = secondaryHitModes[index];
            }
        }

        int range = weaponGetRange(_stack[0], hitMode);

        int damageMin;
        int damageMax;
        weaponGetDamageMinMax(item, &damageMin, &damageMax);

        // CE: Fix displaying secondary mode weapon damage (affects throwable
        // melee weapons - knifes, spears, etc.).
        AttackType attackType = weaponGetAttackTypeForHitMode(item, hitMode);

        formattedText[0] = '\0';

        int meleeDamage;
        if (attackType == ATTACK_TYPE_MELEE || attackType == ATTACK_TYPE_UNARMED) {
            meleeDamage = critterGetStat(_stack[0], STAT_MELEE_DAMAGE);

            // SFALL: Display melee damage without "Bonus HtH Damage" bonus.
            if (damageModGetBonusHthDamageFix() && !damageModGetDisplayBonusDamage()) {
                meleeDamage -= 2 * perkGetRank(gDude, PERK_BONUS_HTH_DAMAGE);
            }
        } else {
            meleeDamage = 0;
        }

        messageListItem.num = 15; // Dmg:
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            if (attackType != 4 && range <= 1) {
                // SFALL: Display bonus damage.
                if (damageModGetBonusHthDamageFix() && damageModGetDisplayBonusDamage()) {
                    // CE: Just in case check for attack type, however it looks
                    // like we cannot be here with anything besides melee or
                    // unarmed.
                    if (_stack[0] == gDude && (attackType == ATTACK_TYPE_MELEE || attackType == ATTACK_TYPE_UNARMED)) {
                        // See explanation in `weaponGetDamage`.
                        damageMin += 2 * perkGetRank(gDude, PERK_BONUS_HTH_DAMAGE);
                    }
                }
                snprintf(formattedText, sizeof(formattedText), "%s %d-%d", messageListItem.text, damageMin, damageMax + meleeDamage);
            } else {
                MessageListItem rangeMessageListItem;
                rangeMessageListItem.num = 16; // Rng:
                if (messageListGetItem(&gInventoryMessageList, &rangeMessageListItem)) {
                    // SFALL: Display bonus damage.
                    if (damageModGetDisplayBonusDamage()) {
                        // CE: There is a bug in Sfall diplaying wrong damage
                        // bonus for melee weapons with range > 1 (spears,
                        // sledgehammers) and throwables (secondary mode).
                        if (_stack[0] == gDude && attackType == ATTACK_TYPE_RANGED) {
                            int damageBonus = 2 * perkGetRank(gDude, PERK_BONUS_RANGED_DAMAGE);
                            damageMin += damageBonus;
                            damageMax += damageBonus;
                        }
                    }

                    snprintf(formattedText, sizeof(formattedText), "%s %d-%d   %s %d", messageListItem.text, damageMin, damageMax + meleeDamage, rangeMessageListItem.text, range);
                }
            }

            fontDrawText(windowBuffer + offset, formattedText, 140, pitch, COLOR_GREEN);
        }

        offset += pitch * fontGetLineHeight();

        if (ammoGetCapacity(item) > 0) {
            const ProtoId ammoTypeProtoId = weaponGetAmmoTypeProtoId(item);

            formattedText[0] = '\0';

            messageListItem.num = 17; // Ammo:
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                if (ammoTypeProtoId.valid()) {
                    if (ammoGetQuantity(item) != 0) {
                        const char* ammoName = protoGetName(ammoTypeProtoId);
                        int capacity = ammoGetCapacity(item);
                        int quantity = ammoGetQuantity(item);
                        snprintf(formattedText, sizeof(formattedText), "%s %d/%d %s", messageListItem.text, quantity, capacity, ammoName);
                    } else {
                        int capacity = ammoGetCapacity(item);
                        int quantity = ammoGetQuantity(item);
                        snprintf(formattedText, sizeof(formattedText), "%s %d/%d", messageListItem.text, quantity, capacity);
                    }
                }
            } else {
                int capacity = ammoGetCapacity(item);
                int quantity = ammoGetQuantity(item);
                snprintf(formattedText, sizeof(formattedText), "%s %d/%d", messageListItem.text, quantity, capacity);
            }

            fontDrawText(windowBuffer + offset, formattedText, 140, pitch, COLOR_GREEN);
        }

        offset += 2 * pitch * fontGetLineHeight();
    }

    // Total wt:
    messageListItem.num = 20;
    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
        if (ProtoId(_stack[0]).objectType() == OBJ_TYPE_CRITTER) {
            int carryWeight = critterGetStat(_stack[0], STAT_CARRY_WEIGHT);
            int inventoryWeight = objectGetInventoryWeight(_stack[0]);
            snprintf(formattedText, sizeof(formattedText), "%s %d/%d", messageListItem.text, inventoryWeight, carryWeight);

            Color color = COLOR_GREEN;
            if (critterIsEncumbered(_stack[0])) {
                color = COLOR_RED;
            }

            fontDrawText(windowBuffer + offset + 15, formattedText, 120, pitch, color);
        } else {
            int inventoryWeight = objectGetInventoryWeight(_stack[0]);
            snprintf(formattedText, sizeof(formattedText), "%s %d", messageListItem.text, inventoryWeight);

            fontDrawText(windowBuffer + offset + 30, formattedText, 80, pitch, COLOR_GREEN);
        }
    }

    fontSetCurrent(oldFont);
}

// 0x472B54
static int _inven_from_button(int keyCode, Object** outItem, Object*** outItemSlot, Object** outOwner)
{
    Object** itemSlot;
    Object* owner;
    Object* item;
    int quantity = 0;

    switch (keyCode) {
    case PARTY_WEAPON_SLOT_KEY:
        itemSlot = getPartySlotItemPtr(InvenSlot::RightHand);
        owner = partyBaseTarget;
        item = itemSlot != nullptr ? *itemSlot : nullptr;
        break;
    case PARTY_ARMOR_SLOT_KEY:
        itemSlot = getPartySlotItemPtr(InvenSlot::Armor);
        owner = partyBaseTarget;
        item = itemSlot != nullptr ? *itemSlot : nullptr;
        break;
    case INVENTORY_HAND_RIGHT_KEY:
        itemSlot = &gInventoryRightHandItem;
        owner = _stack[0];
        item = gInventoryRightHandItem;
        break;
    case INVENTORY_HAND_LEFT_KEY:
        itemSlot = &gInventoryLeftHandItem;
        owner = _stack[0];
        item = gInventoryLeftHandItem;
        break;
    case INVENTORY_ARMOR_KEY:
        itemSlot = &gInventoryArmor;
        owner = _stack[0];
        item = gInventoryArmor;
        break;
    default:
        itemSlot = nullptr;
        owner = nullptr;
        item = nullptr;

        InventoryItem* inventoryItem = nullptr;
        if (keyCode < 2000) {
            int index = _stack_offset[_curr_stack] + keyCode - 1000;
            if (index >= _pud->length) {
                break;
            }

            inventoryItem = &(_pud->items[_pud->length - (index + 1)]);
            item = inventoryItem->item;
            owner = _stack[_curr_stack];
        } else if (keyCode < 2300) {
            int index = _target_stack_offset[_target_curr_stack] + keyCode - 2000;
            if (index >= _target_pud->length) {
                break;
            }

            inventoryItem = &(_target_pud->items[_target_pud->length - (index + 1)]);
            item = inventoryItem->item;
            owner = _target_stack[_target_curr_stack];
        } else if (keyCode < 2400) {
            int index = gPlayerTableOffset + keyCode - 2300;
            if (index >= gPlayerTableInventory->length) {
                break;
            }

            inventoryItem = &(gPlayerTableInventory->items[gPlayerTableInventory->length - (index + 1)]);
            item = inventoryItem->item;
            owner = gPlayerTableObj;
        } else {
            int index = gBartererTableOffset + keyCode - 2400;
            if (index >= gBartererTableInventory->length) {
                break;
            }

            inventoryItem = &(gBartererTableInventory->items[gBartererTableInventory->length - (index + 1)]);
            item = inventoryItem->item;
            owner = gBartererTableObj;
        }

        if (inventoryItem != nullptr) {
            quantity = inventoryItem->quantity;
        }
    }

    if (outItemSlot != nullptr) {
        *outItemSlot = itemSlot;
    }

    if (outItem != nullptr) {
        *outItem = item;
    }

    if (outOwner != nullptr) {
        *outOwner = owner;
    }

    if (quantity == 0 && item != nullptr) {
        quantity = 1;
    }

    return quantity;
}

// Displays item description.
//
// The [string] is mutated in the process replacing spaces back and forth
// for word wrapping purposes.
//
// inven_display_msg
// 0x472D24
static void inventoryRenderItemDescription(const char* string)
{
    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    if (string == nullptr) {
        fontSetCurrent(oldFont);
        return;
    }

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch = inventoryLayout.windowWidth;
    windowBuffer += pitch * INVENTORY_SUMMARY_Y + inventoryLayout.summaryX;

    std::string mutableString(string);

    char* c = mutableString.data();
    while (c != nullptr && *c != '\0') {
        _inven_display_msg_line += 1;
        if (_inven_display_msg_line > 17) {
            debugPrint("\nError: inven_display_msg: out of bounds!");
            goto end;
        }

        char* space = nullptr;
        if (fontGetStringWidth(c) > 152) {
            // Look for next space.
            space = c + 1;
            while (*space != '\0' && *space != ' ') {
                space += 1;
            }

            if (*space == '\0') {
                // This was the last line containing very long word. Text
                // drawing routine will silently truncate it after reaching
                // desired length.
                fontDrawText(windowBuffer + pitch * _inven_display_msg_line * fontGetLineHeight(), c, 152, pitch, COLOR_GREEN);
                goto end;
            }

            char* nextSpace = space + 1;
            while (true) {
                while (*nextSpace != '\0' && *nextSpace != ' ') {
                    nextSpace += 1;
                }

                if (*nextSpace == '\0') {
                    break;
                }

                // Break string and measure it.
                *nextSpace = '\0';
                if (fontGetStringWidth(c) >= 152) {
                    // Next space is too far to fit in one line. Restore next
                    // space's character and stop.
                    *nextSpace = ' ';
                    break;
                }

                space = nextSpace;

                // Restore next space's character and continue looping from the
                // next character.
                *nextSpace = ' ';
                nextSpace += 1;
            }

            if (*space == ' ') {
                *space = '\0';
            }
        }

        if (fontGetStringWidth(c) > 152) {
            debugPrint("\nError: inven_display_msg: word too long!");
            goto end;
        }

        fontDrawText(windowBuffer + pitch * _inven_display_msg_line * fontGetLineHeight(), c, 152, pitch, COLOR_GREEN);

        if (space != nullptr) {
            c = space + 1;
            if (*space == '\0') {
                *space = ' ';
            }
        } else {
            c = nullptr;
        }
    }

end:
    fontSetCurrent(oldFont);
}

// Examines inventory item.
//
// 0x472EB8
static void inventoryExamineItem(Object* critter, Object* item)
{
    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch = inventoryLayout.windowWidth;
    int summaryX = inventoryLayout.summaryX;
    int summaryMaxX = summaryX + (INVENTORY_SUMMARY_MAX_X - INVENTORY_SUMMARY_X);

    // Clear item description area.
    unsigned char* backgroundData = inventoryFrmImage.getData();
    int backgroundWidth = inventoryFrmImage.getWidth();
    if (backgroundData != nullptr) {
        blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_SUMMARY_Y + summaryX,
            INVENTORY_SUMMARY_WIDTH,
            INVENTORY_SUMMARY_HEIGHT,
            backgroundWidth,
            windowBuffer + pitch * INVENTORY_SUMMARY_Y + summaryX,
            pitch);
    }

    // Reset item description lines counter.
    _inven_display_msg_line = 0;

    // Render item's name.
    char* itemName = objectGetName(item);
    inventoryRenderItemDescription(itemName);

    // Increment line counter to accomodate separator below.
    _inven_display_msg_line += 1;

    int lineHeight = fontGetLineHeight();

    // Draw separator.
    // SFALL: Fix separator position when item name is longer than one line.
    bufferDrawLine(windowBuffer,
        pitch,
        summaryX,
        (_inven_display_msg_line - 1) * lineHeight + lineHeight / 2 + 49,
        summaryMaxX,
        (_inven_display_msg_line - 1) * lineHeight + lineHeight / 2 + 49,
        COLOR_GREEN);

    // Examine item.
    objectExamineFunc(critter, item, inventoryRenderItemDescription);

    // Add weight if neccessary.
    int weight = itemGetWeight(item);
    if (weight != 0) {
        MessageListItem messageListItem;
        messageListItem.num = 540;

        if (weight == 1) {
            messageListItem.num = 541;
        }

        if (!messageListGetItem(&gProtoMessageList, &messageListItem)) {
            debugPrint("\nError: Couldn't find message!");
        }

        char formattedText[40];
        snprintf(formattedText, sizeof(formattedText), messageListItem.text, weight);
        inventoryRenderItemDescription(formattedText);
    }

    fontSetCurrent(oldFont);
}

// 0x47304C
static void inventoryWindowOpenContextMenu(int keyCode, int inventoryWindowType)
{
    Object* item;
    Object** itemSlot;
    Object* owner;

    int quantity = _inven_from_button(keyCode, &item, &itemSlot, &owner);
    if (quantity == 0) {
        return;
    }

    ItemType itemType = itemGetType(item);

    int mouseState;
    do {
        sharedFpsLimiter.mark();

        inputGetInput();

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
            _display_body(FrmId::Empty(), INVENTORY_WINDOW_TYPE_NORMAL);
        }

        mouseState = mouseGetEvent();
        if ((mouseState & MOUSE_EVENT_LEFT_BUTTON_UP) != 0) {
            if (inventoryWindowType != INVENTORY_WINDOW_TYPE_NORMAL) {
                objectLookAtFunc(_stack[0], item, gInventoryPrintItemDescriptionHandler);
            } else {
                inventoryExamineItem(_stack[0], item);
            }
            windowRefresh(gInventoryWindow);
            return;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    } while ((mouseState & MOUSE_EVENT_LEFT_BUTTON_DOWN_REPEAT) != MOUSE_EVENT_LEFT_BUTTON_DOWN_REPEAT);

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_BLANK);

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);

    int x;
    int y;
    mouseGetPosition(&x, &y);

    int actionMenuItemsLength;
    const GameMouseActionMenuItem* actionMenuItems;
    if (itemType == ITEM_TYPE_WEAPON && weaponCanBeUnloaded(item)) {
        if (inventoryWindowType != INVENTORY_WINDOW_TYPE_NORMAL && objectGetOwner(item) != gDude) {
            actionMenuItemsLength = 3;
            actionMenuItems = _act_weap2;
        } else {
            actionMenuItemsLength = 4;
            actionMenuItems = _act_weap;
        }
    } else {
        if (inventoryWindowType != INVENTORY_WINDOW_TYPE_NORMAL) {
            // SFALL: Fix crash when trying to open bag/backpack on the table
            // in the bartering interface.
            Object* owner = objectGetOwner(item);
            if (owner != gDude) {
                if (itemType == ITEM_TYPE_CONTAINER && (owner == _stack[_curr_stack] || owner == _target_stack[_target_curr_stack])) {
                    actionMenuItemsLength = 3;
                    actionMenuItems = _act_just_use;
                } else {
                    actionMenuItemsLength = 2;
                    actionMenuItems = _act_nothing;
                }
            } else {
                if (itemType == ITEM_TYPE_CONTAINER) {
                    actionMenuItemsLength = 4;
                    actionMenuItems = _act_use;
                } else {
                    actionMenuItemsLength = 3;
                    actionMenuItems = _act_no_use;
                }
            }
        } else {
            if (itemType == ITEM_TYPE_CONTAINER && itemSlot != nullptr) {
                actionMenuItemsLength = 3;
                actionMenuItems = _act_no_use;
            } else {
                if (_obj_action_can_use(item) || _proto_action_can_use_on(item)) {
                    actionMenuItemsLength = 4;
                    actionMenuItems = _act_use;
                } else {
                    actionMenuItemsLength = 3;
                    actionMenuItems = _act_no_use;
                }
            }
        }
    }

    const InventoryWindowDescription* windowDescription = &(gInventoryWindowDescriptions[inventoryWindowType]);
    int windowWidth = inventoryGetWindowWidth(inventoryWindowType);
    int windowHeight = inventoryGetWindowHeight(inventoryWindowType);

    Rect windowRect;
    windowGetRect(gInventoryWindow, &windowRect);
    int inventoryWindowX = windowRect.left;
    int inventoryWindowY = windowRect.top;

    if (gameMouseRenderActionMenuItems(x, y, actionMenuItems, actionMenuItemsLength,
            windowWidth + inventoryWindowX,
            windowHeight + inventoryWindowY)
        == -1) {
        inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
        return;
    }

    InventoryCursorData* cursorData = &(gInventoryCursorData[INVENTORY_WINDOW_CURSOR_MENU]);

    int offsetX;
    int offsetY;
    artGetRotationOffsets(cursorData->frm, ROTATION_NE, &offsetX, &offsetY);

    Rect rect;
    rect.left = x - inventoryWindowX - cursorData->width / 2 + offsetX;
    rect.top = y - inventoryWindowY - cursorData->height + 1 + offsetY;
    rect.right = rect.left + cursorData->width - 1;
    rect.bottom = rect.top + cursorData->height - 1;

    int menuButtonHeight = cursorData->height;
    if (rect.top + menuButtonHeight > windowHeight) {
        menuButtonHeight = windowHeight - rect.top;
    }

    int btn = buttonCreate(gInventoryWindow,
        rect.left, rect.top, cursorData->width, menuButtonHeight,
        -1, -1, -1, -1,
        cursorData->frmData, cursorData->frmData,
        nullptr, BUTTON_FLAG_TRANSPARENT);
    windowRefreshRect(gInventoryWindow, &rect);

    int menuItemIndex = 0;
    int previousMouseY = y;
    while ((mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_UP) == 0) {
        sharedFpsLimiter.mark();

        inputGetInput();

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
            _display_body(FrmId::Empty(), INVENTORY_WINDOW_TYPE_NORMAL);
        }

        int x;
        int y;
        mouseGetPosition(&x, &y);
        if (y - previousMouseY > 10 || previousMouseY - y > 10) {
            if (y >= previousMouseY || menuItemIndex <= 0) {
                if (previousMouseY < y && menuItemIndex < actionMenuItemsLength - 1) {
                    menuItemIndex++;
                }
            } else {
                menuItemIndex--;
            }
            gameMouseHighlightActionMenuItemAtIndex(menuItemIndex);
            windowRefreshRect(gInventoryWindow, &rect);
            previousMouseY = y;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    buttonDestroy(btn);

    Buffer2D dst { windowBuffer, windowWidth, windowHeight };
    FrmImage backgroundFrmImage;
    int sourceXOffset;
    ConstBuffer2D background = inventoryGetBackgroundBuffer(inventoryWindowType, windowDescription->frmId, backgroundFrmImage, sourceXOffset);
    if (background) {
        blitBuffer2D(background,
            rect.left + sourceXOffset,
            rect.top,
            cursorData->width,
            menuButtonHeight,
            dst,
            rect.left,
            rect.top);
    }

    _mouse_set_position(x, y);

    _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);

    int actionMenuItem = actionMenuItems[menuItemIndex];

    if (clientViewerActive() && actionMenuItem != GAME_MOUSE_ACTION_MENU_ITEM_LOOK) {
        switch (actionMenuItem) {
        case GAME_MOUSE_ACTION_MENU_ITEM_DROP:
            if (owner == gDude) {
                int amount = inventoryViewerDropQuantity(item, itemSlot, quantity);
                if (amount != -1) clientViewerDrop(item, amount);
            }
            break;
        case GAME_MOUSE_ACTION_MENU_ITEM_USE:
            if (owner != gDude) {
                break;
            }
            if (explosiveIsExplosive(item)) {
                int seconds = inventorySetTimer(item);
                if (seconds != -1) clientViewerArmExplosive(item->pid, seconds);
            } else if (itemType != ITEM_TYPE_CONTAINER) {
                clientViewerUseItem(item->pid);
            }
            break;
        case GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD:
            if (owner == gDude) {
                clientViewerUnload(item);
            }
            break;
        default: break;
        }
        inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
        return;
    }

    switch (actionMenuItem) {
    case GAME_MOUSE_ACTION_MENU_ITEM_DROP: {
        bool inventoryMoveAlreadyChecked = false;
        if (itemSlot != nullptr) {
            if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
                break;
            }

            inventoryMoveAlreadyChecked = true;
            if (itemSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], item, nullptr);
            }
            itemAdd(owner, item, 1);
            quantity = 1;
            *itemSlot = nullptr;
        }

        if (ProtoId(item) == ItemProtoTypeId::Money) {
            if (quantity > 1) {
                quantity = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
            } else {
                quantity = 1;
            }

            if (quantity > 0) {
                if (quantity == 1) {
                    if (inventoryMoveAlreadyChecked || scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
                        itemSetMoney(item, 1);
                        objectDrop(owner, item);
                    }
                } else {
                    if (itemRemoveWithReason(owner, item, quantity - 1, RemoveInventoryObjectHookReason::InventoryDropCaps) == 0) {
                        Object* item2;
                        if (_inven_from_button(keyCode, &item2, &itemSlot, &owner) != 0) {
                            if (scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item2, nullptr)) {
                                itemSetMoney(item2, quantity);
                                objectDrop(owner, item2);
                            } else {
                                itemAdd(owner, item, quantity - 1);
                            }
                        } else {
                            itemAdd(owner, item, quantity - 1);
                        }
                    }
                }
            }
        } else if (explosiveIsActiveExplosive(item)) {
            if (inventoryMoveAlreadyChecked || scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
                _dropped_explosive = 1;
                objectDrop(owner, item);
            }
        } else {
            if (quantity > 1) {
                quantity = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);

                for (int index = 0; index < quantity; index++) {
                    if (_inven_from_button(keyCode, &item, &itemSlot, &owner) != 0) {
                        if (scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
                            objectDrop(owner, item);
                        }
                    }
                }
            } else {
                if (inventoryMoveAlreadyChecked || scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
                    objectDrop(owner, item);
                }
            }
        }
        break;
    }
    case GAME_MOUSE_ACTION_MENU_ITEM_LOOK:
        if (inventoryWindowType != INVENTORY_WINDOW_TYPE_NORMAL) {
            objectExamineFunc(_stack[0], item, gInventoryPrintItemDescriptionHandler);
        } else {
            inventoryExamineItem(_stack[0], item);
        }
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_USE:
        switch (itemType) {
        case ITEM_TYPE_CONTAINER:
            _container_enter(keyCode, inventoryWindowType);
            break;
        case ITEM_TYPE_DRUG:
            if (drugItemTakeDrug(_stack[0], item)) {
                if (itemSlot != nullptr) {
                    *itemSlot = nullptr;
                } else {
                    itemRemoveWithReason(owner, item, 1, RemoveInventoryObjectHookReason::ConsumeDrug);
                }

                _obj_connect(item, gDude->tile, gDude->elevation, nullptr);
                objectDestroy(item);
            }
            interfaceRenderHitPoints(true);
            break;
        case ITEM_TYPE_WEAPON:
        case ITEM_TYPE_MISC:
            if (itemSlot == nullptr) {
                itemRemoveWithReason(owner, item, 1, RemoveInventoryObjectHookReason::UseObj);
            }

            UseItemResultCode useResult;
            if (_obj_action_can_use(item)) {
                useResult = objectUseItemInternal(_stack[0], item);
            } else {
                useResult = objectUseItemOnInternal(_stack[0], _stack[0], item);
            }

            if (useResult == USE_ITEM_RESULT_REMOVE) {
                if (itemSlot != nullptr) {
                    *itemSlot = nullptr;
                }

                _obj_connect(item, gDude->tile, gDude->elevation, nullptr);
                objectDestroy(item);
            } else {
                if (itemSlot == nullptr) {
                    itemAdd(owner, item, 1);
                }
            }
        default:
            break;
        }
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD:
        if (itemSlot == nullptr) {
            itemRemoveWithReason(owner, item, 1, RemoveInventoryObjectHookReason::UnloadWeapon);
        }

        inventoryUnloadWeaponToOwner(owner, item);

        if (itemSlot == nullptr) {
            itemAdd(owner, item, 1);
        }
        break;
    default:
        break;
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL && actionMenuItem != GAME_MOUSE_ACTION_MENU_ITEM_LOOK) {
        inventoryRenderSummary();
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT
        || inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, inventoryWindowType);
    }

    _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        barterDisplayTables(gInventoryBarterBackgroundWindow, gPlayerTableObj, gBartererTableObj, -1);
    }

    _adjust_fid();
}

static bool inventoryStealWatcherOnly()
{
    return clientViewerActive() && clientStealActive() && !clientStealIsDriver();
}

// 0x473904
int inventoryOpenLooting(Object* looter, Object* target)
{
    MessageListItem messageListItem;

    if (looter != _inven_dude) {
        return 0;
    }

    switch (lootOpenCheck(looter, target, _gIsSteal)) {
    case LOOT_OPEN_NO_STEAL:
        inventoryDisplayMessage(50); // You can't find anything to take from that.
        _gIsSteal = 0;
        return 0;
    case LOOT_OPEN_BLOCKED:
        _gIsSteal = 0;
        return 0;
    default:
        break;
    }

    if (inventoryCommonInit() == -1) {
        return 0;
    }

    Object* hiddenBox = nullptr;
    if (objectCreateWithFrmIdProtoId(&hiddenBox, FrmId::Empty(), ItemProtoTypeId::JesseContainer) == -1) {
        return 0;
    }
    CritterEquipped stealTargetEquipped {};

    bool isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_LOOT);
    partyTargetEquipped = _gIsSteal ? &stealTargetEquipped : nullptr;
    partyBaseTarget = nullptr;
    setLootTarget(target, hiddenBox);
    createPartySlotButtons();

    auto makeButton = [&](int x, int y, int keyCode) {
        int upFrmId = INVENTORY_ARROW_FRM_LEFT_ARROW_UP;
        int downFrmId = INVENTORY_ARROW_FRM_LEFT_ARROW_DOWN;
        if (keyCode == KEY_ARROW_RIGHT || keyCode == KEY_PAGE_DOWN) {
            upFrmId = INVENTORY_ARROW_FRM_RIGHT_ARROW_UP;
            downFrmId = INVENTORY_ARROW_FRM_RIGHT_ARROW_DOWN;
        }
        int btn = buttonCreateWithFrm(gInventoryWindow,
            x, y, -1, -1, -1, keyCode,
            kInventoryArrowFrmIds[upFrmId],
            kInventoryArrowFrmIds[downFrmId]);
        if (btn != -1) {
            buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
        }
    };

    Object** critters = nullptr;
    int critterCount = 0;
    int critterIndex = 0;
    if (!_gIsSteal) {
        if (FrmId(target).objectType() == OBJ_TYPE_CRITTER) {
            critterCount = objectListCreate(target->tile, target->elevation, OBJ_TYPE_CRITTER, &critters);
            int endIndex = critterCount - 1;
            for (int index = 0; index < critterCount; index++) {
                Object* critter = critters[index];
                if ((critter->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT)) == 0) {
                    critters[index] = critters[endIndex];
                    critters[endIndex] = critter;
                    critterCount--;
                    index--;
                    endIndex--;
                } else {
                    critterIndex++;
                }
            }

            if (critterCount == 1) {
                objectListFree(critters);
                critterCount = 0;
            }

            if (critterCount > 1) {
                makeButton(inventoryLootLayout.prevCritterButtonX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_PAGE_UP);
                makeButton(inventoryLootLayout.nextCritterButtonX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_PAGE_DOWN);

                for (int index = 0; index < critterCount; index++) {
                    if (target == critters[index]) {
                        critterIndex = index;
                    }
                }
            }
        }
    }

    // Party member navigation: in normal looting left-side arrows and Left/Right
    // switch whose inventory is shown on the left pane. In party quick-loot,
    // Left/Right instead cycles the active right-side party target.
    Object* const playerObj = _inven_dude;
    FrmId savedDudeFrmId = gInventoryWindowDudeFrmId;
    bool switchActivePartyTarget = settings.qol.party_loot_and_barter
        && _gIsSteal
        && objectIsPartyMember(target);
    std::vector<Object*> partyTargets;
    if (switchActivePartyTarget) {
        for (Object* pm : get_all_party_members_objects(false)) {
            if (pm != _inven_dude) {
                partyTargets.push_back(pm);
            }
        }
    } else if (settings.qol.party_loot_and_barter && (!_gIsSteal || objectIsPartyMember(target))) {
        partyTargets.push_back(_inven_dude);
        for (Object* pm : get_all_party_members_objects(false)) {
            if (pm != gDude && pm != target) {
                partyTargets.push_back(pm);
            }
        }
    }

    int partyTargetIndex = 0;
    for (int index = 0; index < partyTargets.size(); index++) {
        if ((switchActivePartyTarget ? target : _inven_dude) == partyTargets[index]) {
            partyTargetIndex = index;
            break;
        }
    }

    if (switchActivePartyTarget && partyTargets.size() > 1) {
        makeButton(inventoryLootLayout.prevCritterButtonX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_ARROW_LEFT);
        makeButton(inventoryLootLayout.nextCritterButtonX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_ARROW_RIGHT);
    } else if (partyTargets.size() > 1) {
        const int btnCenterX = inventoryLootLayout.leftBodyViewX + INVENTORY_BODY_VIEW_WIDTH / 2;

        makeButton(btnCenterX - 20, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_ARROW_LEFT);
        makeButton(btnCenterX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_ARROW_RIGHT);
    }

    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
    gInventoryWindowDudeRotationTimestamp = 0;
    _display_body(FrmId(target), INVENTORY_WINDOW_TYPE_LOOT);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);

    // Trigger game mode change _after_ window and loot_obj are set up
    ScopedGameMode gm(GameMode::kLoot);
    if (tileFirstPersonPreviewIsEnabled()) {
        mouseDeviceSetRelativeModeForced(false);
        mouseShowCursor();
    }

    bool isCaughtStealing = false;
    int stealingXp = 0;
    int stealingXpBonus = 10;
    for (;;) {
        sharedFpsLimiter.mark();

        if (clientViewerActive()) clientViewerSetLootTarget(_target_stack[_target_curr_stack]->netId);
        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        if (isCaughtStealing) {
            break;
        }

        // Wire viewer, STEAL: the SESSION is the server's, so its end is the
        // screen's end — caught, closed, or bailed, everyone's window comes down
        // together. (A viewer never sets isCaughtStealing itself: it runs no roll.)
        if (clientViewerActive() && clientStealEndPending()) {
            break;
        }

        int keyCode = inputGetInput();
        int mouseEvent = mouseGetEvent();
        InventoryScrollerDisplayContext inventoryScrollerContext { INVENTORY_WINDOW_TYPE_LOOT, nullptr };
        InventoryScrollerDisplayContext targetScrollerContext { INVENTORY_WINDOW_TYPE_LOOT, _target_pud };
        InventoryScroller lootInventoryScroller {
            { inventoryLootLayout.leftScrollerX, inventoryLootLayout.leftScrollerY, inventoryLootLayout.leftScrollerX + inventoryLootLayout.scrollerWidth, inventoryLootLayout.leftScrollerY + inventoryLootLayout.scrollerHeight },
            &(_stack_offset[_curr_stack]),
            inventoryLootLayout.columns,
            gInventorySlotsCount,
            _pud->length,
            KEY_ARROW_UP,
            KEY_ARROW_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawInventory,
            &inventoryScrollerContext,
            false,
        };
        InventoryScroller lootTargetScroller {
            { inventoryLootLayout.rightScrollerX, inventoryLootLayout.rightScrollerY, inventoryLootLayout.rightScrollerX + inventoryLootLayout.scrollerWidth, inventoryLootLayout.rightScrollerY + inventoryLootLayout.scrollerHeight },
            &(_target_stack_offset[_target_curr_stack]),
            inventoryLootLayout.columns,
            gInventorySlotsCount,
            _target_pud->length,
            KEY_CTRL_ARROW_UP,
            KEY_CTRL_ARROW_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawTargetInventory,
            &targetScrollerContext,
            true,
        };

        // Wire viewer: loot transfers (take/put/takeall) are server-authoritative —
        // the local move is skipped and the truth streams back as an inventory
        // reconcile pumped by the service ticker inside inputGetInput. Repaint BOTH
        // panels when it lands: the dude-inv dirty flag fires when the dude side changes
        // (take/takeall/put), the loot-target dirty flag when the container side is
        // reconciled — consume BOTH (bitwise | so neither is short-circuited away), else
        // an async transfer would not show until the next user event. Clamp both scroll
        // offsets in case items were removed out from under them.
        // (Steal adds a third source: on a SPECTATOR neither of those two fires —
        // the thief is not their dude and the victim is the loot target only for
        // whoever set it — so the session's own STATE heartbeat is what tells
        // their screen to repaint. Consumed with the others, never short-circuited.)
        if (clientViewerActive()
            && (static_cast<int>(clientViewerConsumeDudeInvDirty())
                    | static_cast<int>(clientViewerConsumeLootTargetInvDirty())
                    | static_cast<int>(clientStealConsumeDirty()))
                != 0) {
            if (_stack_offset[_curr_stack] > _pud->length - gInventorySlotsCount) {
                _stack_offset[_curr_stack] = _pud->length - gInventorySlotsCount;
            }
            if (_stack_offset[_curr_stack] < 0) {
                _stack_offset[_curr_stack] = 0;
            }
            if (_target_stack_offset[_target_curr_stack] > _target_pud->length - gInventorySlotsCount) {
                _target_stack_offset[_target_curr_stack] = _target_pud->length - gInventorySlotsCount;
            }
            if (_target_stack_offset[_target_curr_stack] < 0) {
                _target_stack_offset[_target_curr_stack] = 0;
            }
            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
            // BLIT IT — see the identical note in inventoryOpen. Both display calls
            // above draw into the window buffer only, and this block runs off an
            // async reconcile rather than a user event, so nothing else blits. This
            // is why a deposited item showed no sprite on the container side until
            // the mouse moved over it, and why the weight/capacity line (drawn by
            // these same two functions) never updated live either.
            windowRefresh(gInventoryWindow);
        }

        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
            showQuitConfirmationDialog();
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        if (keyCode == 2502 || keyCode == KEY_LOWERCASE_A) {
            if (!_gIsSteal) {
                if (keyCode == KEY_LOWERCASE_A) {
                    soundPlayFile("ib1p1xx1");
                }
                int maxCarryWeight = critterGetStat(_inven_dude, STAT_CARRY_WEIGHT);
                int currentWeight = objectGetInventoryWeight(_inven_dude);
                int newInventoryWeight = objectGetInventoryWeight(target);
                if (clientViewerActive() && !_gIsSteal) {
                    if (target->netId > 0) {
                        clientViewerLootTakeAll(target->netId);
                    } else {
                        debugPrint("inventory: viewer cannot take all from unbound loot target\n");
                    }
                } else if (newInventoryWeight <= maxCarryWeight - currentWeight) {
                    itemMoveAll(target, _inven_dude);
                    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
                    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
                } else {
                    // Sorry, you cannot carry that much.
                    messageListItem.num = 31;
                    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                        showDialogBox(messageListItem.text, nullptr, 0, 169, 117, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
                    }
                }
            }

            // change selected party member
        } else if (keyCode == KEY_ARROW_LEFT) {
            if (partyTargets.size() > 1) {
                partyTargetIndex = inventoryWrapIndex(partyTargetIndex, partyTargets.size(), -1);
                if (switchActivePartyTarget) {
                    target = partyTargets[partyTargetIndex];
                    setLootTarget(target, hiddenBox);
                    refreshAfterTargetChange();
                } else {
                    inventorySetLeftPaneCritter(partyTargets[partyTargetIndex], target, INVENTORY_WINDOW_TYPE_LOOT);
                }
            }
        } else if (keyCode == KEY_ARROW_RIGHT) {
            if (partyTargets.size() > 1) {
                partyTargetIndex = inventoryWrapIndex(partyTargetIndex, partyTargets.size(), 1);
                if (switchActivePartyTarget) {
                    target = partyTargets[partyTargetIndex];
                    setLootTarget(target, hiddenBox);
                    refreshAfterTargetChange();
                } else {
                    inventorySetLeftPaneCritter(partyTargets[partyTargetIndex], target, INVENTORY_WINDOW_TYPE_LOOT);
                }
            }
        } else if (keyCode == KEY_PAGE_UP) {
            if (critterCount != 0) {
                critterIndex = inventoryWrapIndex(critterIndex, critterCount, -1);
                target = critters[critterIndex];
                setLootTarget(target, hiddenBox);
                refreshAfterTargetChange();
            }
        } else if (keyCode == KEY_PAGE_DOWN) {
            if (critterCount != 0) {
                critterIndex = inventoryWrapIndex(critterIndex, critterCount, 1);
                target = critters[critterIndex];
                setLootTarget(target, hiddenBox);
                refreshAfterTargetChange();
            }
        } else if (keyCode >= 2500 && keyCode <= 2501) {
            _container_exit(keyCode, INVENTORY_WINDOW_TYPE_LOOT);
        } else {
            inventoryScrollerHandleInput(lootInventoryScroller, keyCode, mouseEvent);
            inventoryScrollerHandleInput(lootTargetScroller, keyCode, mouseEvent);

            if ((mouseEvent & MOUSE_EVENT_RIGHT_BUTTON_DOWN) != 0) {
                if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
                } else {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
                }
            } else if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0) {
                if (keyCode >= 1000 && keyCode < 1000 + gInventorySlotsCount) {
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        // A witness to someone else's steal gets no item actions
                        // either — Drop/Use here would act through THEIR own dude.
                        if (!inventoryStealWatcherOnly()) {
                            inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_LOOT);
                        }
                    } else {
                        int slotIndex = keyCode - 1000;
                        if (slotIndex + _stack_offset[_curr_stack] < _pud->length
                            && !inventoryStealWatcherOnly()) {
                            _gStealCount += 1;
                            _gStealSize += itemGetSize(_stack[_curr_stack]);

                            InventoryItem* inventoryItem = &(_pud->items[_pud->length - (slotIndex + _stack_offset[_curr_stack] + 1)]);
                            int stealXpOverride = -1;
                            InventoryMoveResult rc = _move_inventory(inventoryItem->item, slotIndex, _target_stack[_target_curr_stack], true, &stealXpOverride);
                            if (rc == INVENTORY_MOVE_RESULT_CAUGHT_STEALING) {
                                isCaughtStealing = true;
                            } else if (rc == INVENTORY_MOVE_RESULT_SUCCESS) {
                                stealingXp += stealXpOverride >= 0 ? stealXpOverride : stealingXpBonus;
                                stealingXpBonus += 10;
                            }

                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
                            _display_body(getTargetDisplayFrmId(), INVENTORY_WINDOW_TYPE_LOOT);
                        }

                        keyCode = -1;
                    }
                } else if (keyCode >= 2000 && keyCode < 2000 + gInventorySlotsCount) {
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        // A witness to someone else's steal gets no item actions
                        // either — Drop/Use here would act through THEIR own dude.
                        if (!inventoryStealWatcherOnly()) {
                            inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_LOOT);
                        }
                    } else {
                        int slotIndex = keyCode - 2000;
                        if (slotIndex + _target_stack_offset[_target_curr_stack] < _target_pud->length
                            && !inventoryStealWatcherOnly()) {
                            _gStealCount += 1;
                            _gStealSize += itemGetSize(_stack[_curr_stack]);

                            InventoryItem* inventoryItem = &(_target_pud->items[_target_pud->length - (slotIndex + _target_stack_offset[_target_curr_stack] + 1)]);
                            int stealXpOverride = -1;
                            InventoryMoveResult rc = _move_inventory(inventoryItem->item, slotIndex, _target_stack[_target_curr_stack], false, &stealXpOverride);
                            if (rc == INVENTORY_MOVE_RESULT_CAUGHT_STEALING) {
                                isCaughtStealing = true;
                            } else if (rc == INVENTORY_MOVE_RESULT_SUCCESS) {
                                stealingXp += stealXpOverride >= 0 ? stealXpOverride : stealingXpBonus;
                                stealingXpBonus += 10;
                            }

                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
                            _display_body(getTargetDisplayFrmId(), INVENTORY_WINDOW_TYPE_LOOT);
                        }
                    }
                } else if ((keyCode == PARTY_WEAPON_SLOT_KEY || keyCode == PARTY_ARMOR_SLOT_KEY)
                    && hasPartySlots()) {
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_LOOT);
                    } else {
                        handlePartySlotPickup(keyCode == PARTY_WEAPON_SLOT_KEY ? InvenSlot::RightHand : InvenSlot::Armor);
                    }
                }
            }
        }

        if (keyCode == KEY_ESCAPE) {
            // ►► IN A STEAL SESSION, LEAVING IS A REQUEST. The session is the
            // server's: it holds the victim's detached gear and owes everyone a
            // close, so a viewer that tore its own window down would be watching a
            // theft it could no longer see (and a WITNESS could dismiss someone
            // else's crime). The thief asks with `sdone` and the window closes on
            // EVENT_STEAL_END; a witness's ESC is consumed and does nothing. Same
            // ruling as the spectator's ESC in a streamed dialog.
            if (clientViewerActive() && clientStealActive()) {
                // ONCE. The window closes on EVENT_STEAL_END, not on the keypress,
                // so a second ESC during the round trip only sends an `sdone` that
                // arrives after the session is gone.
                if (clientStealIsDriver() && clientStealConsumeCloseRequest()) {
                    clientViewerStealVerb("sdone", -1, 0);
                }
            } else {
                break;
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (critterCount != 0) {
        objectListFree(critters);
    }

    if (_gIsSteal) {
        critterRestoreEquipped(target, stealTargetEquipped);
    }

    itemMoveAll(hiddenBox, target);
    objectDestroy(hiddenBox, nullptr);

    if (_gIsSteal && !isCaughtStealing && stealingXp > 0 && !objectIsPartyMember(target)) {
        stealingXp = std::min(300 - skillGetValue(looter, SKILL_STEAL), stealingXp);
        debugPrint("\n[[[%d]]]", 300 - skillGetValue(looter, SKILL_STEAL));
    }

    // A VIEWER AWARDS NOTHING. Its stealingXp counter tracked local clicks, but
    // the rolls happened on the server and so did the XP (and the caught check) —
    // paying it again here would be a client-side invention that the next sheet
    // delta contradicts.
    if (_gIsSteal && !isCaughtStealing && stealingXp > 0 && !clientViewerActive()) {
        // SFALL: Display actual xp received.
        int xpGained;
        if (lootStealExperience(looter, target, stealingXp, &xpGained)) {
            // You gain %d experience points for successfully using your Steal skill.
            messageListItem.num = 29;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                char formattedText[200];
                snprintf(formattedText, sizeof(formattedText), messageListItem.text, xpGained);
                presenter()->consoleMessage(formattedText);
            }
        }
    }

    partyRightEquippedWeight = 0;
    partyTargetEquipped = nullptr;
    partyBaseTarget = nullptr;
    gInventoryWindowDudeFrmId = savedDudeFrmId;

    _exit_inventory(isoWasEnabled);
    _inven_dude = playerObj;

    // NOTE: Uninline.
    inventoryCommonFree();

    // Wire viewer: the loot screen is closed — stop full-reconciling this container
    // (its delta reverts to the equip-flags-only path) and drop any pending dirty.
    if (clientViewerActive()) {
        clientViewerSetLootTarget(0);
    }

    if (_gIsSteal && isCaughtStealing && _gStealCount > 0) {
        lootCaughtStealingReact(looter, target);
    }

    return 0;
}

// 0x4746A0
int inventoryOpenStealing(Object* thief, Object* target)
{
    if (thief == target) {
        return -1;
    }

    _gIsSteal = ProtoId(thief).objectType() == OBJ_TYPE_CRITTER && critterIsActive(target);
    _gStealCount = 0;
    _gStealSize = 0;

    int rc = inventoryOpenLooting(thief, target);

    _gIsSteal = 0;
    _gStealCount = 0;
    _gStealSize = 0;

    return rc;
}

// -- SERVER-AUTHORITATIVE STEAL SESSION (co-op) -----------------------------
// Installed by f2_server (inventory.h). Null everywhere else.
static std::function<bool()> gStealServerPump;

// The live session, or nullptrs. Read by the stake/splant/sdone trust boundary
// in server_control.cc: a steal verb only means anything inside a session, and
// only from the actor whose hands are in the pockets.
static Object* gStealSessionThief = nullptr;
static Object* gStealSessionTarget = nullptr;

void stealSetServerPump(std::function<bool()> pump)
{
    gStealServerPump = std::move(pump);
}

bool stealSessionActive()
{
    return gStealSessionThief != nullptr;
}

Object* stealSessionThief()
{
    return gStealSessionThief;
}

Object* stealSessionTarget()
{
    return gStealSessionTarget;
}

// Find a top-level stack by proto id, as the barter drain does and for the same
// reason: the screen's slot math is reverse-indexed into a live window and means
// nothing here, so the wire names items by pid.
static Object* _steal_find_item_by_pid(Object* owner, int pid)
{
    if (owner == nullptr) {
        return nullptr;
    }
    Inventory* inventory = &(owner->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item != nullptr
            && inventory->items[index].item->pid == pid) {
            return inventory->items[index].item;
        }
    }
    return nullptr;
}

// Ship what the two pockets look like right now.
//
// ►► WHY A SCAN AND NOT A SNAPSHOT (this is the one place steal is CHEAPER than
// barter): barter had to invent an event carrying both tables because a barter
// table is created with pid -1 and never reaches the wire at all. A steal
// session moves items between two REAL world objects that every viewer already
// mirrors by netId, so the existing OBJECT_DELTA_INVENTORY reconcile is exactly
// the right channel — it already does add/remove/qty against a mirror, and the
// loot screen already repaints off it. What is missing is only the TRIGGER: the
// tick is parked inside this session, so the scan that normally runs once per
// beat never fires. Run it here, then emit the (empty) state event, whose
// serializer flushes the frame the deltas were just written into.
static void stealEmitState()
{
    objectDeltaScan();
    presenter()->stealState();
}

// Run a steal / pickpocket / plant session on the dedicated server.
//
// ►► THE SHAPE, AND WHY IT IS BARTER'S AND NOT LOOT'S. Looting in co-op is
// viewer-local: the client opens the vanilla screen and every transfer is a
// take/put verb, with the sim still running behind it. Stealing cannot be, for
// two reasons the owner named directly. (1) The transfer is a SKILL ROLL —
// whose skill, against whose Perception, with vanilla's per-item difficulty
// ramp — and a client may not roll its own dice. (2) It is theatre the whole
// party should see: the world stops and everyone watches the thief's screen, so
// a steal that goes wrong goes wrong in front of witnesses. So this parks the
// tick in a block-and-pump barrier exactly like a trade, and every viewer opens
// the same screen (thief drives, the rest watch).
//
// Everything below the announce is vanilla's own steal loop
// (inventoryOpenLooting with _gIsSteal set) with the UI amputated: the same
// entry gates, the same detach of the target's equipped gear so it cannot be
// lifted, the same _gStealCount difficulty ramp, the same 10/20/30 XP ladder,
// the same caught-ends-it rule, the same PICKUP react on the way out. The parts
// that are ours are the barrier, the addressing, and the fact that the roll
// happens here instead of on the thief's machine.
void stealSessionRun(Object* thief, Object* target)
{
    if (thief == nullptr || target == nullptr || thief == target) {
        return;
    }
    if (gStealSessionThief != nullptr) {
        // No nesting. A script cannot open a steal inside a steal, but a racing
        // verb could try; refuse rather than corrupt the detach bookkeeping.
        fprintf(stderr, "f2_server: steal session already open, ignoring net=%d -> net=%d\n",
            thief->netId, target->netId);
        return;
    }

    // ►► gDude := the THIEF for the whole session. skillsPerformStealing gates
    // the Pickpocket perk and the steal-from-a-party-member auto-success on
    // `thief == gDude`, skillGetValue reads the actor's own sheet, and
    // lootStealExperience awards to the actor — every one of those is correct
    // only inside this scope. This is the same conversation-long scope the
    // dialog barrier holds, for the same reason.
    ServerActorScope scope(thief);

    // ►► THE SERVER HAS TO LOAD THE STRINGS ITSELF. inventry.msg is loaded by
    // inventoryCommonInit — i.e. by OPENING A WINDOW — and a dedicated server never
    // opens one, so every messageListGetItem below quietly returned false and the
    // line was dropped. Two of them matter and both looked like missing FEATURES: the
    // XP award ("You gain %d experience points for successfully using your Steal
    // skill.", msg 29) went unannounced, so a successful theft read as paying nothing
    // — the owner reasonably asked whether stealing granted XP at all; it always did,
    // silently. And msg 50 ("You can't find anything to take from that.") meant a
    // refused session opened and shut with no word at all.
    inventoryMessageListInit();

    _gIsSteal = ProtoId(thief).objectType() == OBJ_TYPE_CRITTER && critterIsActive(target);
    _gStealCount = 0;
    _gStealSize = 0;

    MessageListItem messageListItem;

    switch (lootOpenCheck(thief, target, _gIsSteal)) {
    case LOOT_OPEN_NO_STEAL:
        // You can't find anything to take from that. (Addressed: it answers ONE
        // player's click and means nothing to the others.)
        messageListItem.num = 50;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            presenter()->consoleMessageFor(thief->netId, messageListItem.text);
        }
        _gIsSteal = 0;
        return;
    case LOOT_OPEN_BLOCKED:
        _gIsSteal = 0;
        return;
    default:
        break;
    }

    Object* item1 = nullptr;
    Object* item2 = nullptr;
    Object* armor = nullptr;
    Object* hiddenBox = lootTargetDetach(target, _gIsSteal, &item1, &item2, &armor);
    if (hiddenBox == nullptr) {
        _gIsSteal = 0;
        return;
    }

    gStealSessionThief = thief;
    gStealSessionTarget = target;
    // A verb that raced the previous session's teardown must not be spent on
    // this target's pockets.
    stealIntentClear();

    // ►► TELL EVERYONE, BY NAME. Two things need saying and they are the same
    // line: the world just stopped (the tick is parked in this barrier, and an
    // unexplained freeze reads as a crash — bugs-list U), and somebody is
    // committing a crime in front of you. The owner asked for this one
    // explicitly. Emitted BEFORE stealBegin because consoleMessageStyled only
    // BUFFERS — stealBegin force-flushes and carries it out.
    for (int slot = 0; slot < playerActorCount(); slot++) {
        Object* other = playerActorAt(slot);
        if (other == nullptr || other == thief) {
            continue;
        }
        char line[256];
        snprintf(line, sizeof(line), "%s is stealing from %s.",
            critterGetName(thief), objectGetName(target));
        presenter()->consoleMessageStyled(other->netId, kMsgChannelSystem, line);
    }

    fprintf(stderr, "f2_server: steal session OPEN thief=net%d target=net%d isSteal=%d\n",
        thief->netId, target->netId, _gIsSteal);

    presenter()->stealBegin(thief, target);
    stealEmitState();

    bool caught = false;
    int stealingXp = 0;
    int stealingXpBonus = 10;

    for (;;) {
        StealIntent intent;
        bool haveIntent = stealIntentPeek(&intent);
        if (gStealServerPump != nullptr) {
            // Block-and-pump, the steal twin of the barter barrier: park the tick
            // inside the open screen and service the control channel until the
            // thief's next move lands, or the pump bails (thief gone / quit).
            while (!haveIntent) {
                if (!gStealServerPump()) {
                    break;
                }
                haveIntent = stealIntentPeek(&intent);
            }
        }
        if (!haveIntent) {
            // No pump installed (nothing but f2_server installs one) or the pump
            // bailed. Either way the session is over; the teardown below returns
            // the target's own gear, so a dropped thief costs the victim nothing.
            break;
        }
        stealIntentPop();

        if (intent.kind == STEAL_INTENT_DONE) {
            break;
        }

        bool planting = intent.kind == STEAL_INTENT_PLANT;
        Object* from = planting ? thief : target;
        Object* item = _steal_find_item_by_pid(from, intent.pid);
        if (item == nullptr) {
            // Another player emptied that pocket, or the viewer's mirror is
            // stale. Same wording as the loot path's identical case.
            presenter()->consoleMessageFor(thief->netId, "That item is gone.");
            stealEmitState();
            continue;
        }

        int available = itemGetQuantity(from, item);
        int quantity = intent.quantity;
        if (quantity <= 0 || quantity > available) {
            quantity = available;
        }

        // Vanilla's per-move difficulty ramp: every attempt in one session makes
        // the next harder (stealModifier = -_gStealCount + 1). Counted for the
        // ATTEMPT, before the roll, exactly as the screen's loop counts it.
        _gStealCount += 1;
        _gStealSize += itemGetSize(item);

        LootTransferResult rc = lootTransferItem(thief, target, item, quantity, planting, _gIsSteal != 0);
        fprintf(stderr, "f2_server: steal %s pid=%d qty=%d rc=%d count=%d\n",
            planting ? "plant" : "take", intent.pid, quantity, (int)rc, _gStealCount);

        if (rc == LOOT_TRANSFER_CAUGHT_STEALING) {
            caught = true;
            stealEmitState();
            break;
        }
        if (rc == LOOT_TRANSFER_NO_ROOM) {
            presenter()->consoleMessageFor(thief->netId,
                planting ? "There is no space left for that item."
                         : "You cannot pick that up. You are at your maximum weight capacity.");
        } else {
            stealingXp += stealingXpBonus;
            stealingXpBonus += 10;
        }

        stealEmitState();
    }

    lootTargetReattach(target, hiddenBox, item1, item2, armor);
    // ►► FLUSH THE RESTORE WHILE THE VIEWERS STILL KNOW WHO THE VICTIM IS. A viewer
    // reconciles the victim's pockets in full only for the duration of the session
    // (client_net.cc gViewerStealTargetNetId, set by BEGIN and cleared by END), which
    // is what makes the detached weapon/armor vanish from the right panel. The
    // reattach that puts them back is the mirror image and needs the same window:
    // emitted after END it would arrive with the gate already shut, and the victim's
    // mirror would stay stripped of its own gun until the next rebaseline.
    stealEmitState();

    if (_gIsSteal && !caught && stealingXp > 0) {
        int xpGained;
        if (lootStealExperience(thief, target, stealingXp, &xpGained)) {
            // You gain %d experience points for successfully using your Steal skill.
            messageListItem.num = 29;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                char formattedText[200];
                snprintf(formattedText, sizeof(formattedText), messageListItem.text, xpGained);
                presenter()->consoleMessageFor(thief->netId, formattedText);
            }
        }
    }

    bool reactNeeded = _gIsSteal && caught && _gStealCount > 0;

    gStealSessionThief = nullptr;
    gStealSessionTarget = nullptr;
    _gIsSteal = 0;
    _gStealCount = 0;
    _gStealSize = 0;
    stealIntentClear();

    // Close every viewer's screen, THEN run the react. The react executes the
    // target's PICKUP proc, which is script that can start a fight, talk, or
    // move the world — none of which should happen with a modal held open on
    // five machines.
    presenter()->stealEnd();
    stealEmitState();

    fprintf(stderr, "f2_server: steal session CLOSE thief=net%d caught=%d xp=%d\n",
        thief->netId, caught ? 1 : 0, stealingXp);

    if (reactNeeded) {
        lootCaughtStealingReact(thief, target);
    }
}

// VIEWER-side steal screen. The server is running the session; this is what it
// looks like from a seat.
//
// ►► ANCHORED ON THE THIEF, ON EVERY MACHINE. The left panel is the thief's pack
// and the right is the victim's, for spectators too — that is the owner's spec
// ("show the stealing player's view") and it is also the only rendering that
// makes sense: an item leaving the victim has to arrive somewhere visible.
// vanilla's `looter != _inven_dude` gate at the top of inventoryOpenLooting is
// exactly the anchor test, so pointing _inven_dude at the thief is both what
// opens the screen and what fills it.
//
// ►► AND THE ANCHOR MUST BE PUT BACK. _inven_dude / _stack[0] are the PERSISTENT
// anchor the ordinary inventory screen reads; barter shipped the bug where a
// spectator who watched someone else's trade found their own pack rendering
// empty forever after. Same save/restore, same reason.
void inventoryOpenStealingViewer(Object* thief, Object* target)
{
    if (thief == nullptr || target == nullptr || thief == target) {
        return;
    }

    // ►► SUBJECT SLOTS MUST BE PROTO-RESOLVABLE (viewer-modal invariant I3): both
    // of these land in subject slots (_inven_dude / _target_stack[0]) and go
    // through proto/stat/art resolution at dozens of sites. A session naming an
    // object this viewer has not been told about yet is not renderable — refuse
    // it, because a missing screen is recoverable and a fault is not.
    Proto* probe = nullptr;
    if (protoGetProto(ProtoId(thief), &probe) != 0 || protoGetProto(ProtoId(target), &probe) != 0) {
        debugPrint("client_steal: subject slot not proto-resolvable — refusing the steal screen\n");
        return;
    }

    Object* savedInvenDude = _inven_dude;
    Object* savedStack0 = _stack[0];

    _inven_dude = thief;
    _stack[0] = thief;

    inventoryOpenStealing(thief, target);

    _inven_dude = savedInvenDude;
    _stack[0] = savedStack0;
}

// 0x474708
static InventoryMoveResult _move_inventory(Object* item, int slotIndex, Object* targetObj, bool isPlanting, int* stealXpOverridePtr)
{
    assert(stealXpOverridePtr != nullptr);
    *stealXpOverridePtr = -1;

    bool needRefresh = true;

    Rect rect;

    int quantity;
    if (isPlanting) {
        rect.left = inventoryLootGetSlotX(false, slotIndex);
        rect.top = inventoryLootGetSlotY(slotIndex);

        InventoryItem* inventoryItem = &(_pud->items[_pud->length - (slotIndex + _stack_offset[_curr_stack] + 1)]);
        quantity = inventoryItem->quantity;
        if (quantity > 1) {
            _display_inventory(_stack_offset[_curr_stack], slotIndex, INVENTORY_WINDOW_TYPE_LOOT);
            needRefresh = false;
        }
    } else {
        rect.left = inventoryLootGetSlotX(true, slotIndex);
        rect.top = inventoryLootGetSlotY(slotIndex);

        InventoryItem* inventoryItem = &(_target_pud->items[_target_pud->length - (slotIndex + _target_stack_offset[_target_curr_stack] + 1)]);
        quantity = inventoryItem->quantity;
        if (quantity > 1) {
            _display_target_inventory(_target_stack_offset[_target_curr_stack], slotIndex, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
            windowRefresh(gInventoryWindow);
            needRefresh = false;
        }
    }

    if (needRefresh) {
        unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);

        unsigned char* backgroundData = inventoryLootFrmImage.getData();
        int backgroundWidth = inventoryLootFrmImage.getWidth();
        if (backgroundData != nullptr) {
            blitBufferToBuffer(backgroundData + backgroundWidth * rect.top + rect.left,
                INVENTORY_SLOT_WIDTH,
                INVENTORY_SLOT_HEIGHT,
                backgroundWidth,
                windowBuffer + inventoryLootLayout.windowWidth * rect.top + rect.left,
                inventoryLootLayout.windowWidth);
        }

        rect.right = rect.left + INVENTORY_SLOT_WIDTH - 1;
        rect.bottom = rect.top + INVENTORY_SLOT_HEIGHT - 1;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    InventoryMoveResult result = INVENTORY_MOVE_RESULT_FAILED;

    if (isPlanting) {
        if (!immediate && tryEquipPartyItem(item, true)) {
            result = INVENTORY_MOVE_RESULT_SUCCESS;
        } else if (immediate || inventoryLootMouseHitTestScroller(true)) {
            int quantityToMove = quantity;
            if (quantity > 1 && !immediate) {
                quantityToMove = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
            }

            if (quantityToMove != -1) {
                bool skipMove = false;
                if (_gIsSteal && _inven_dude == gDude) {
                    SkillStealResult stealResult = skillsPerformStealing(_inven_dude, targetObj, item, quantityToMove, true, stealXpOverridePtr);
                    if (stealResult == SkillStealResult::Caught) {
                        result = INVENTORY_MOVE_RESULT_CAUGHT_STEALING;
                    } else if (stealResult == SkillStealResult::Fail) {
                        skipMove = true;
                    }
                }

                if (!skipMove && result != INVENTORY_MOVE_RESULT_CAUGHT_STEALING) {
                    if (clientViewerActive() && !_gIsSteal) {
                        if (targetObj->netId > 0) {
                            clientViewerLootPut(targetObj->netId, item->pid, quantityToMove);
                            result = INVENTORY_MOVE_RESULT_SUCCESS;
                        } else {
                            debugPrint("inventory: viewer cannot put item into unbound loot target (pid %d)\n",
                                item->pid);
                        }
                    } else if (itemMove(_inven_dude, targetObj, item, quantityToMove) != -1) {
                        result = INVENTORY_MOVE_RESULT_SUCCESS;
                    } else {
                        inventoryDisplayMessage(26); // There is no space left for that item.
                    }
                }
            }
        }
    } else {
        if (!immediate && tryEquipPartyItem(item, false)) {
            result = INVENTORY_MOVE_RESULT_SUCCESS;
        } else if (immediate || inventoryLootMouseHitTestScroller(false)) {
            int quantityToMove = quantity;
            if (quantity > 1 && !immediate) {
                quantityToMove = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
            }

            if (quantityToMove != -1) {
                bool skipMove = false;
                if (_gIsSteal && _inven_dude == gDude) {
                    SkillStealResult stealResult = skillsPerformStealing(_inven_dude, targetObj, item, quantityToMove, false, stealXpOverridePtr);
                    if (stealResult == SkillStealResult::Caught) {
                        result = INVENTORY_MOVE_RESULT_CAUGHT_STEALING;
                    } else if (stealResult == SkillStealResult::Fail) {
                        skipMove = true;
                    }
                }

                if (!skipMove && result != INVENTORY_MOVE_RESULT_CAUGHT_STEALING) {
                    if (clientViewerActive() && !_gIsSteal) {
                        if (targetObj->netId > 0) {
                            clientViewerLootTake(targetObj->netId, item->pid, quantityToMove);
                            result = INVENTORY_MOVE_RESULT_SUCCESS;
                        } else {
                            debugPrint("inventory: viewer cannot take item from unbound loot target (pid %d)\n",
                                item->pid);
                        }
                    } else if (itemMove(targetObj, _inven_dude, item, quantityToMove) == 0) {
                        if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
                            targetObj->fid = FrmId(targetObj, WeaponAnimation::None, targetObj->rotation + 1).fid();
                        }

                        targetObj->flags &= ~OBJECT_EQUIPPED;

                        result = INVENTORY_MOVE_RESULT_SUCCESS;
                    } else {
                        inventoryDisplayMessage(25); // You cannot pick that up. You are at your maximum weight capacity.
                    }
                }
            }
        }
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);

    return result;
}

// 0x474DAC
static void barterMoveToTable(Object* item, int quantity, int slotIndex, int indexOffset, Object* npc, Object* sourceTable, bool fromDude)
{
    MessageListItem messageListItem;
    Rect rect;
    if (fromDude) {
        rect.left = 31;
        rect.top = INVENTORY_SLOT_HEIGHT * slotIndex + 31;
    } else {
        rect.left = 389;
        rect.top = INVENTORY_SLOT_HEIGHT * slotIndex + 31;
    }

    if (quantity > 1) {
        if (fromDude) {
            _display_inventory(indexOffset, slotIndex, INVENTORY_WINDOW_TYPE_TRADE);
        } else {
            _display_target_inventory(indexOffset, slotIndex, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
        }
    } else {
        unsigned char* dest = windowGetBuffer(gInventoryWindow);
        unsigned char* src = windowGetBuffer(gInventoryBarterBackgroundWindow);

        int pitch = INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH;
        blitBufferToBuffer(src + pitch * rect.top + rect.left + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, pitch, dest + INVENTORY_TRADE_WINDOW_WIDTH * rect.top + rect.left, INVENTORY_TRADE_WINDOW_WIDTH);

        rect.right = rect.left + INVENTORY_SLOT_WIDTH - 1;
        rect.bottom = rect.top + INVENTORY_SLOT_HEIGHT - 1;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    if (fromDude) {
        if (immediate || mouseHitTestInWindow(gInventoryWindow, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_X, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y)) {
            int quantityToMove = barterGetMovedQuantity(item, quantity, true, true, immediate);
            if (quantityToMove != -1) {
                if (clientBarterIsDriver()) {
                    // Viewer driver: OFFER is authoritative on the SERVER. Send the
                    // verb; do not mutate the mirror locally -- the server echoes the
                    // result as EVENT_BARTER_STATE, applied at the trade-loop top.
                    // The pickup/drag gesture and quantity dial above are pure vanilla
                    // and ran locally for feel.
                    clientViewerBarterVerb("boffer", item->pid, quantityToMove);
                } else if (itemMoveForce(_inven_dude, sourceTable, item, quantityToMove) == -1) {
                    // There is no space left for that item.
                    messageListItem.num = 26;
                    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                        presenter()->consoleMessage(messageListItem.text);
                    }
                }
            }
        }
    } else {
        if (immediate || mouseHitTestInWindow(gInventoryWindow, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_X, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y)) {
            int quantityToMove = barterGetMovedQuantity(item, quantity, false, true, immediate);
            if (quantityToMove != -1) {
                if (clientBarterIsDriver()) {
                    // Viewer driver: TAKE moves a merchant item onto the buy table on
                    // the SERVER (see the boffer note above).
                    clientViewerBarterVerb("btake", item->pid, quantityToMove);
                } else if (itemMoveForce(npc, sourceTable, item, quantityToMove) == -1) {
                    // You cannot pick that up. You are at your maximum weight capacity.
                    messageListItem.num = 25;
                    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                        presenter()->consoleMessage(messageListItem.text);
                    }
                }
            }
        }
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
}

// 0x475070
static void barterMoveFromTable(Object* item, int quantity, int slotIndex, Object* npc, Object* sourceTable, bool fromDude)
{
    MessageListItem messageListItem;
    Rect rect;
    if (fromDude) {
        rect.left = INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD;
        rect.top = INVENTORY_SLOT_HEIGHT * slotIndex + INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD;
    } else {
        rect.left = INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD;
        rect.top = INVENTORY_SLOT_HEIGHT * slotIndex + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD;
    }

    if (quantity > 1) {
        if (fromDude) {
            barterDisplayTables(gInventoryBarterBackgroundWindow, sourceTable, nullptr, slotIndex);
        } else {
            barterDisplayTables(gInventoryBarterBackgroundWindow, nullptr, sourceTable, slotIndex);
        }
    } else {
        unsigned char* dest = windowGetBuffer(gInventoryWindow);
        unsigned char* src = windowGetBuffer(gInventoryBarterBackgroundWindow);

        int pitch = INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH;
        blitBufferToBuffer(src + pitch * rect.top + rect.left + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, pitch, dest + INVENTORY_TRADE_WINDOW_WIDTH * rect.top + rect.left, INVENTORY_TRADE_WINDOW_WIDTH);

        rect.right = rect.left + INVENTORY_SLOT_WIDTH - 1;
        rect.bottom = rect.top + INVENTORY_SLOT_HEIGHT - 1;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    if (fromDude) {
        if (immediate || mouseHitTestInWindow(gInventoryWindow, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_X, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y)) {
            int quantityToMove = barterGetMovedQuantity(item, quantity, true, false, immediate);
            if (quantityToMove != -1) {
                if (clientBarterIsDriver()) {
                    // Viewer driver: pulling a good back off the player's offer table.
                    // The server's UNOFFER searches both tables by pid, so one verb
                    // covers either direction (see the boffer note above).
                    clientViewerBarterVerb("bunoffer", item->pid, quantityToMove);
                } else if (itemMoveForce(sourceTable, _inven_dude, item, quantityToMove) == -1) {
                    // There is no space left for that item.
                    messageListItem.num = 26;
                    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                        presenter()->consoleMessage(messageListItem.text);
                    }
                }
            }
        }
    } else {
        if (immediate || mouseHitTestInWindow(gInventoryWindow, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_X, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y)) {
            int quantityToMove = barterGetMovedQuantity(item, quantity, false, false, immediate);
            if (quantityToMove != -1) {
                if (clientBarterIsDriver()) {
                    // Viewer driver: pulling a good back off the merchant's buy table
                    // (see the bunoffer note above -- one verb, both tables).
                    clientViewerBarterVerb("bunoffer", item->pid, quantityToMove);
                } else if (itemMoveForce(sourceTable, npc, item, quantityToMove) == -1) {
                    // You cannot pick that up. You are at your maximum weight capacity.
                    messageListItem.num = 25;
                    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                        presenter()->consoleMessage(messageListItem.text);
                    }
                }
            }
        }
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
}

// 0x475334
static void barterDisplayTables(int win, Object* leftTable, Object* rightTable, int draggedSlotIndex)
{
    barterClampTableOffsets();

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    char formattedText[80];
    int rectHeight = fontGetLineHeight() + INVENTORY_SLOT_HEIGHT * gInventorySlotsCount;

    auto [requestValue, offerValue] = gGameDialogSpeakerIsPartyMember
        ? barterComputeTablesWeight(gDude, _target_stack[0])
        : barterComputeTablesValue(gDude, _target_stack[0], false);

    if (leftTable != nullptr) {
        unsigned char* src = windowGetBuffer(win);
        blitBufferToBuffer(src + INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH * INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y + INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, rectHeight + 1, INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y + INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD, INVENTORY_TRADE_WINDOW_WIDTH);

        unsigned char* dest = windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD + INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD;
        Inventory* inventory = &(leftTable->data.inventory);
        for (int index = 0; index < gInventorySlotsCount && index + gPlayerTableOffset < inventory->length; index++) {
            InventoryItem* inventoryItem = &(inventory->items[inventory->length - (index + gPlayerTableOffset + 1)]);
            const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
            artRender(inventoryFrmId, dest, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, INVENTORY_TRADE_WINDOW_WIDTH);
            _display_inventory_info(inventoryItem->item, inventoryItem->quantity, dest, INVENTORY_TRADE_WINDOW_WIDTH, index == draggedSlotIndex);

            dest += INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_SLOT_HEIGHT;
        }

        if (gGameDialogSpeakerIsPartyMember) {
            MessageListItem messageListItem;
            messageListItem.num = 30;

            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                snprintf(formattedText, sizeof(formattedText), "%s %d", messageListItem.text, offerValue);
            }
        } else {
            snprintf(formattedText, sizeof(formattedText), "$%d", clientBarterActive() ? clientBarterOfferValue() : offerValue);
        }

        fontDrawText(windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * (INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD) + INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD, formattedText, 80, INVENTORY_TRADE_WINDOW_WIDTH, COLOR_WHITE);

        Rect rect;
        rect.left = INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD;
        rect.top = INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD;
        // NOTE: Odd math, the only way to get 223 is to subtract 2.
        rect.right = INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD + INVENTORY_SLOT_WIDTH_PAD - 2;
        rect.bottom = rect.top + rectHeight;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    if (rightTable != nullptr) {
        unsigned char* src = windowGetBuffer(win);
        blitBufferToBuffer(src + INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH * INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, rectHeight + 1, INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD, INVENTORY_TRADE_WINDOW_WIDTH);

        unsigned char* dest = windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD;
        Inventory* inventory = &(rightTable->data.inventory);
        for (int index = 0; index < gInventorySlotsCount && index + gBartererTableOffset < inventory->length; index++) {
            InventoryItem* inventoryItem = &(inventory->items[inventory->length - (index + gBartererTableOffset + 1)]);
            const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
            artRender(inventoryFrmId, dest, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, INVENTORY_TRADE_WINDOW_WIDTH);
            _display_inventory_info(inventoryItem->item, inventoryItem->quantity, dest, INVENTORY_TRADE_WINDOW_WIDTH, index == draggedSlotIndex);

            dest += INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_SLOT_HEIGHT;
        }

        if (gGameDialogSpeakerIsPartyMember) {
            MessageListItem messageListItem;
            messageListItem.num = 30;

            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                snprintf(formattedText, sizeof(formattedText), "%s %d", messageListItem.text, clientBarterActive() ? clientBarterAskingValue() : requestValue);
            }
        } else {
            snprintf(formattedText, sizeof(formattedText), "$%d", clientBarterActive() ? clientBarterAskingValue() : requestValue);
        }

        fontDrawText(windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * (INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD) + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD, formattedText, 80, INVENTORY_TRADE_WINDOW_WIDTH, COLOR_WHITE);

        Rect rect;
        rect.left = INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD;
        rect.top = INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD;
        // NOTE: Odd math, likely should be `INVENTORY_SLOT_WIDTH_PAD`.
        rect.right = INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD + INVENTORY_SLOT_WIDTH;
        rect.bottom = rect.top + rectHeight;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    fontSetCurrent(oldFont);
}

// 0x4757F0
// Server barter: resolve a proto id to a live item Object* in `owner`'s
// top-level inventory. The dude, the merchant, and the two barter tables are
// all flat inventories here, so no nested-container search is needed. Returns
// the first matching item, or nullptr if absent. Used by the serverLoopActive()
// drain in barterProcessUI to reference items by pid rather than by the
// window's fragile reverse-indexed slot math.
static Object* _barter_find_item_by_pid(Object* owner, int pid)
{
    Inventory* inventory = &(owner->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item->pid == pid) {
            return inventory->items[index].item;
        }
    }
    return nullptr;
}

// Installed by f2_server (inventory.h). Null everywhere else, which is what
// leaves the golden fixtures on break-on-empty.
static std::function<bool()> gBarterServerPump;

// Snapshot one table's top-level stacks into the presenter's row form. Top-level
// only, matching every other inventory the wire carries (nested container
// contents are the standing "A2" gap); a barter table never nests in practice.
static int barterSnapshotTable(Object* table, Presenter::BarterStack* out, int cap)
{
    if (table == nullptr) {
        return 0;
    }
    Inventory* inv = &(table->data.inventory);
    int count = 0;
    for (int i = 0; i < inv->length && count < cap; i++) {
        if (inv->items[i].item == nullptr) {
            continue;
        }
        out[count].pid = inv->items[i].item->pid;
        out[count].quantity = inv->items[i].quantity;
        count++;
    }
    return count;
}

// Ship what the trade looks like right now. Called after every accepted move, and
// once at open so a viewer starts from the truth rather than an empty guess.
//
// The two valuations are computed HERE, server-side, because they cannot be
// derived viewer-side (see the presenter.h note): barterComputeValue reads the
// party's best barter skill, a gDude-gated Master Trader check, and a barterMod
// carrying the script-set reaction.
static void barterEmitState(Object* barterer, Object* playerTable, Object* bartererTable,
    int barterMod, bool speakerIsPartyMember, int resultCode = -1)
{
    constexpr int kMaxRows = 64;
    Presenter::BarterStack playerRows[kMaxRows];
    Presenter::BarterStack merchantRows[kMaxRows];

    Presenter::BarterStack driverRows[kMaxRows];
    Presenter::BarterStack merchantInvRows[kMaxRows];

    Presenter::BarterView view;
    // All four, because the tick is parked for the trade's whole life: no mirror
    // on any viewer will update on its own, so anything not in here is frozen at
    // whatever it held when the trade opened.
    view.driverInvCount = barterSnapshotTable(_inven_dude, driverRows, kMaxRows);
    view.driverInv = driverRows;
    view.merchantInvCount = barterSnapshotTable(barterer, merchantInvRows, kMaxRows);
    view.merchantInv = merchantInvRows;
    view.playerTableCount = barterSnapshotTable(playerTable, playerRows, kMaxRows);
    view.playerTable = playerRows;
    view.merchantTableCount = barterSnapshotTable(bartererTable, merchantRows, kMaxRows);
    view.merchantTable = merchantRows;

    // offerValue = what the player is putting up; askingValue = what the merchant
    // wants for the goods on their table. The player's side is valued WITHOUT the
    // barter modifier: the modifier prices the MERCHANT's goods, and vanilla's
    // offer side is the raw worth of what you hand over.
    view.offerValue = objectGetCost(playerTable);
    view.askingValue = barterComputeValue(_inven_dude, barterer, bartererTable,
        barterMod, speakerIsPartyMember);
    view.resultCode = resultCode;

    presenter()->barterState(view);
}

void barterSetServerPump(std::function<bool()> pump)
{
    gBarterServerPump = std::move(pump);
}

void barterProcessUI(int win, Object* barterer, Object* playerTable, Object* bartererTable, int barterMod)
{
    ScopedGameMode gm(GameMode::kBarter);

    gBarterFinalModifier = barterMod;

    if (inventoryCommonInit() == -1) {
        return;
    }

    // SIM setup (runs on both paths): strip the merchant's worn armor and
    // held weapon so they aren't offered as barter goods; the teardown re-adds
    // them. This mutates the merchant's inventory, so it must run headless too.
    Object* armor = critterGetArmor(barterer);
    if (armor != nullptr) {
        itemRemoveWithReason(barterer, armor, 1, RemoveInventoryObjectHookReason::BarterArmor);
    }

    Object* item1 = nullptr;
    Object* item2 = critterGetItem2(barterer);
    if (item2 != nullptr) {
        itemRemoveWithReason(barterer, item2, 1, RemoveInventoryObjectHookReason::BarterWeapon);
    } else {
        if (!gGameDialogSpeakerIsPartyMember) {
            item1 = inventoryFindByType(barterer, ITEM_TYPE_WEAPON, nullptr);
            if (item1 != nullptr) {
                itemRemoveWithReason(barterer, item1, 1, RemoveInventoryObjectHookReason::BarterWeapon);
            }
        }
    }

    Object* hiddenBox = nullptr;
    if (objectCreateWithFrmIdProtoId(&hiddenBox, FrmId::Empty(), ItemProtoTypeId::JesseContainer) == -1) {
        return;
    }

    // Sfall: remove hidden items of barterer (relevant to Goris)
    itemMoveAllHidden(barterer, hiddenBox);

    _pud = &(_inven_dude->data.inventory);
    gBartererTableObj = bartererTable;
    gPlayerTableObj = playerTable;

    gPlayerTableOffset = 0;
    gBartererTableOffset = 0;

    gPlayerTableInventory = &(playerTable->data.inventory);
    gBartererTableInventory = &(bartererTable->data.inventory);

    gInventoryBarterBackgroundWindow = win;
    _target_curr_stack = 0;
    _target_pud = &(barterer->data.inventory);

    _target_stack[0] = barterer;
    _target_stack_offset[0] = 0;

    Object* const playerObj = _inven_dude;
    const FrmId savedDudeFrmId = gInventoryWindowDudeFrmId;

    std::vector<Object*> partyTargets = { _inven_dude };
    if (settings.qol.party_loot_and_barter) {
        for (Object* pm : get_all_party_members_objects(false)) {
            if (pm != gDude && pm != barterer) {
                partyTargets.push_back(pm);
            }
        }
    }
    int partyTargetIndex = 0;

    bool isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_TRADE);

    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
    _display_inventory(_stack_offset[0], -1, INVENTORY_WINDOW_TYPE_TRADE);
    _display_body(FrmId(barterer), INVENTORY_WINDOW_TYPE_TRADE);
    windowRefresh(gInventoryBarterBackgroundWindow);
    barterDisplayTables(win, playerTable, bartererTable, -1);

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);

    int barterReactionModifier = 0;
    switch (reactionTranslateValue(reactionGetValue(barterer))) {
    case NPC_REACTION_BAD:
        barterReactionModifier = 25;
        break;
    case NPC_REACTION_NEUTRAL:
        break;
    case NPC_REACTION_GOOD:
        barterReactionModifier = -15;
        break;
    default:
        assert(false && "Should be unreachable");
    }

    int keyCode = -1;
    for (;;) {
        if (serverLoopActive()) {
            // Headless: no mouse/keyboard/render/fps loop. Drain the barter
            // intent queue, moving items with itemMoveForce (bypassing the
            // display move-helpers, which reverse-index live window slots) and
            // committing via the same barterAttemptTransaction the 'M' button
            // drives below. An empty queue ends the loop — we cannot block for
            // input (same policy as the server dialog/combat drains).
            gBarterFinalModifier = barterMod + barterReactionModifier;

            BarterIntent intent;
            bool haveIntent = barterIntentPeek(&intent);
            if (gBarterServerPump != nullptr) {
                // Block-and-pump, the barter twin of the dialog barrier
                // (game_dialog.cc gameDialogProcessUI): park the server tick inside the
                // open trade and service the control channel until the driver's
                // next move lands, or the pump bails (driver gone / combat /
                // quit). Vanilla freezes the world during barter anyway, so a
                // trade is an accepted input barrier exactly like a dialog node.
                while (!haveIntent) {
                    if (!gBarterServerPump()) {
                        break;
                    }
                    haveIntent = barterIntentPeek(&intent);
                }
            }
            if (!haveIntent) {
                if (gBarterServerPump != nullptr) {
                    // ►► PUMP BAIL — SWEEP THE TABLES OR THE PLAYER LOSES THEM.
                    // Teardown ends at _gdialog_barter_destroy_win, which calls
                    // objectDestroy on both tables WITH THEIR CONTENTS. 'T'/DONE
                    // sweeps first; ESC/CANCEL deliberately does not. A bail is
                    // neither — it is the driver vanishing mid-trade (disconnect,
                    // quit) with goods still on the table, and destroying those is
                    // never the right answer to a dropped connection. So a bail
                    // exits like DONE, not like CANCEL.
                    itemMoveAll(bartererTable, barterer);
                    itemMoveAll(playerTable, gDude);
                    presenter()->barterEnd(); // close every viewer's window too
                }
                // No pump: the ORIGINAL headless behavior, and the one every
                // golden depends on — a fixture pre-queues its moves plus a
                // terminating bdone, so a dry queue means the fixture is done.
                break;
            }
            barterIntentPop();

            if (intent.kind == BARTER_INTENT_DONE) {
                // == the 'T'/Talk button (lines ~4332-4336): return both tables
                // to their owners, then leave barter to resume/close the dialog.
                itemMoveAll(bartererTable, barterer);
                itemMoveAll(playerTable, gDude);
                gameDialogEndBarter();
                presenter()->barterEnd();
                break;
            }

            if (intent.kind == BARTER_INTENT_CANCEL) {
                presenter()->barterEnd();
                // == ESC: leave without a table sweep; the teardown below
                // restores the merchant's worn/held items and the hidden box.
                break;
            }

            if (intent.kind == BARTER_INTENT_COMMIT) {
                // == the 'M'/Offer button (lines ~4337-4373), sim half only. The
                // bad-offer/too-heavy message TEXT rendering is UI and dropped, but
                // the RESULT CODE now rides the state emit so a viewer can confirm or
                // deny the Offer button (see barterEmitState's resultCode).
                BarterResult rc = BARTER_RESULT_OK;
                if (playerTable->data.inventory.length != 0 || bartererTable->data.inventory.length != 0) {
                    rc = barterAttemptTransaction(_inven_dude, playerTable, barterer, bartererTable, gBarterFinalModifier, gGameDialogSpeakerIsPartyMember);
                    if (getenv("F2_BARTER_TRACE") != nullptr) {
                        debugPrint("[btrace] commit rc=%d ptable_len=%d btable_len=%d mod=%d\n",
                            rc, playerTable->data.inventory.length, bartererTable->data.inventory.length, gBarterFinalModifier);
                    }
                }
                // A commit that SUCCEEDS empties both tables; one that is refused
                // (bad offer / too heavy) leaves them exactly as they were. Re-send
                // either way — "nothing moved" is the answer to a refused offer, and
                // a viewer that only heard about successes could never show it.
                barterEmitState(barterer, playerTable, bartererTable,
                    gBarterFinalModifier, gGameDialogSpeakerIsPartyMember, (int)rc);
                continue;
            }

            // OFFER/TAKE/UNOFFER: move `quantity` of `pid` between an owner and
            // a table. Mirrors the LMB dude/merchant/table slot handlers.
            Object* item = nullptr;
            Object* from = nullptr;
            Object* to = nullptr;
            switch (intent.kind) {
            case BARTER_INTENT_OFFER_ITEM:
                from = _inven_dude;
                to = playerTable;
                item = _barter_find_item_by_pid(_inven_dude, intent.pid);
                break;
            case BARTER_INTENT_TAKE_ITEM:
                from = barterer;
                to = bartererTable;
                item = _barter_find_item_by_pid(barterer, intent.pid);
                break;
            case BARTER_INTENT_UNOFFER_ITEM:
                item = _barter_find_item_by_pid(playerTable, intent.pid);
                if (item != nullptr) {
                    from = playerTable;
                    to = _inven_dude;
                } else {
                    item = _barter_find_item_by_pid(bartererTable, intent.pid);
                    from = bartererTable;
                    to = barterer;
                }
                break;
            }

            if (item != nullptr) {
                int available = itemGetQuantity(from, item);
                int quantity = intent.quantity;
                if (quantity <= 0 || quantity > available) {
                    quantity = available;
                }
                itemMoveForce(from, to, item, quantity);
                if (getenv("F2_BARTER_TRACE") != nullptr) {
                    debugPrint("[btrace] move kind=%d pid=%d qty=%d/%d\n",
                        intent.kind, intent.pid, quantity, available);
                }
            } else if (getenv("F2_BARTER_TRACE") != nullptr) {
                debugPrint("[btrace] move kind=%d pid=%d NOT FOUND\n", intent.kind, intent.pid);
            }

            // The owner's spec, literally: no animation, no travel — the item is
            // simply on the table now, on every screen at once.
            barterEmitState(barterer, playerTable, bartererTable,
                gBarterFinalModifier, gGameDialogSpeakerIsPartyMember);
            continue;
        }

        sharedFpsLimiter.mark();

        if (keyCode == KEY_ESCAPE || _game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        keyCode = inputGetInput();
        int mouseEvent = mouseGetEvent();
        barterClampTableOffsets();
        InventoryScrollerDisplayContext inventoryScrollerContext { INVENTORY_WINDOW_TYPE_TRADE, nullptr };
        InventoryScrollerDisplayContext targetScrollerContext { INVENTORY_WINDOW_TYPE_TRADE, _target_pud };
        InventoryScrollerBarterContext barterScrollerContext { win, playerTable, bartererTable };
        InventoryScroller tradeInventoryScroller {
            { INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_X, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y },
            &(_stack_offset[_curr_stack]),
            1,
            gInventorySlotsCount,
            _pud->length,
            KEY_ARROW_UP,
            KEY_ARROW_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawInventory,
            &inventoryScrollerContext,
            false,
        };
        InventoryScroller tradePlayerTableScroller {
            { INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_X, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y },
            &gPlayerTableOffset,
            1,
            gInventorySlotsCount,
            gPlayerTableInventory->length,
            KEY_PAGE_UP,
            KEY_PAGE_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawBarterTables,
            &barterScrollerContext,
            false,
        };
        InventoryScroller tradeTargetScroller {
            { INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_X, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y },
            &(_target_stack_offset[_target_curr_stack]),
            1,
            gInventorySlotsCount,
            _target_pud->length,
            KEY_CTRL_ARROW_UP,
            KEY_CTRL_ARROW_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawTargetInventory,
            &targetScrollerContext,
            true,
        };
        InventoryScroller tradeBartererTableScroller {
            { INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_X, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y },
            &gBartererTableOffset,
            1,
            gInventorySlotsCount,
            gBartererTableInventory->length,
            KEY_CTRL_PAGE_UP,
            KEY_CTRL_PAGE_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawBarterTables,
            &barterScrollerContext,
            false,
        };
        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
            showQuitConfirmationDialog();
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        gBarterFinalModifier = barterMod + barterReactionModifier;

        if (keyCode == KEY_LOWERCASE_T || barterReactionModifier <= -30) {
            // T == return to talk
            itemMoveAll(bartererTable, barterer);
            itemMoveAll(playerTable, gDude);
            gameDialogEndBarter();
            break;
        } else if (keyCode == KEY_LOWERCASE_M) {
            if (playerTable->data.inventory.length != 0 || gBartererTableObj->data.inventory.length != 0) {
                BarterResult rc = barterAttemptTransaction(_inven_dude, playerTable, barterer, bartererTable, gBarterFinalModifier, gGameDialogSpeakerIsPartyMember);
                if (rc == BARTER_RESULT_OK) {
                    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                    barterDisplayTables(win, playerTable, bartererTable, -1);

                    // Ok, that's a good trade.
                    MessageListItem messageListItem;
                    messageListItem.num = 27;
                    if (!gGameDialogSpeakerIsPartyMember) {
                        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                            gameDialogRenderSupplementaryMessage(messageListItem.text);
                        }
                    }
                } else {
                    MessageListItem messageListItem;
                    // 31: Sorry, you cannot carry that much.
                    // 32: Sorry, that's too much to carry.
                    // 28: No, your offer is not good enough.
                    switch (rc) {
                    case BARTER_RESULT_TOO_HEAVY:
                        messageListItem.num = 31;
                        break;
                    case BARTER_RESULT_NPC_TOO_HEAVY:
                        messageListItem.num = 32;
                        break;
                    default:
                        messageListItem.num = 28;
                        break;
                    }
                    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                        gameDialogRenderSupplementaryMessage(messageListItem.text);
                    }
                }
            }
        } else if (keyCode == KEY_ARROW_LEFT) {
            if (partyTargets.size() > 1) {
                partyTargetIndex = (partyTargetIndex > 0) ? partyTargetIndex - 1 : (int)partyTargets.size() - 1;
                inventorySetLeftPaneCritter(partyTargets[partyTargetIndex], barterer, INVENTORY_WINDOW_TYPE_TRADE);
            }
        } else if (keyCode == KEY_ARROW_RIGHT) {
            if (partyTargets.size() > 1) {
                partyTargetIndex = (partyTargetIndex < (int)partyTargets.size() - 1) ? partyTargetIndex + 1 : 0;
                inventorySetLeftPaneCritter(partyTargets[partyTargetIndex], barterer, INVENTORY_WINDOW_TYPE_TRADE);
            }
        } else if (keyCode >= 2500 && keyCode <= 2501) {
            _container_exit(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
        } else {
            inventoryScrollerHandleInput(tradeInventoryScroller, keyCode, mouseEvent);
            inventoryScrollerHandleInput(tradePlayerTableScroller, keyCode, mouseEvent);
            inventoryScrollerHandleInput(tradeTargetScroller, keyCode, mouseEvent);
            inventoryScrollerHandleInput(tradeBartererTableScroller, keyCode, mouseEvent);

            if ((mouseEvent & MOUSE_EVENT_RIGHT_BUTTON_DOWN) != 0) {
                if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
                } else {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
                }
            } else if ((mouseEvent & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0) {
                if (keyCode >= 1000 && keyCode <= 1000 + gInventorySlotsCount) {
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
                        barterDisplayTables(win, playerTable, nullptr, -1);
                    } else {
                        // player inventory
                        int slotIndex = keyCode - 1000;
                        if (slotIndex + _stack_offset[_curr_stack] < _pud->length) {
                            int offset = _stack_offset[_curr_stack];
                            InventoryItem* inventoryItem = &(_pud->items[_pud->length - (slotIndex + offset + 1)]);
                            barterMoveToTable(inventoryItem->item, inventoryItem->quantity, slotIndex, offset, barterer, playerTable, true);
                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                            barterDisplayTables(win, playerTable, nullptr, -1);
                        }
                    }

                    keyCode = -1;
                } else if (keyCode >= 2000 && keyCode <= 2000 + gInventorySlotsCount) {
                    // merchant inventory
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
                        barterDisplayTables(win, nullptr, bartererTable, -1);
                    } else {
                        int slotIndex = keyCode - 2000;
                        if (slotIndex + _target_stack_offset[_target_curr_stack] < _target_pud->length) {
                            int stackOffset = _target_stack_offset[_target_curr_stack];
                            InventoryItem* inventoryItem = &(_target_pud->items[_target_pud->length - (slotIndex + stackOffset + 1)]);
                            barterMoveToTable(inventoryItem->item, inventoryItem->quantity, slotIndex, stackOffset, barterer, bartererTable, false);
                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                            barterDisplayTables(win, nullptr, bartererTable, -1);
                        }
                    }

                    keyCode = -1;
                } else if (keyCode >= 2300 && keyCode <= 2300 + gInventorySlotsCount) {
                    // player table (offer)
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
                        barterDisplayTables(win, playerTable, nullptr, -1);
                    } else {
                        int slotIndex = keyCode - 2300;
                        int tableIndex = slotIndex + gPlayerTableOffset;
                        InventoryItem* inventoryItem = inventoryGetReversedSlot(gPlayerTableInventory, tableIndex);
                        if (inventoryItem != nullptr) {
                            barterMoveFromTable(inventoryItem->item, inventoryItem->quantity, slotIndex, barterer, playerTable, true);
                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                            barterDisplayTables(win, playerTable, nullptr, -1);
                        }
                    }

                    keyCode = -1;
                } else if (keyCode >= 2400 && keyCode <= 2400 + gInventorySlotsCount) {
                    // merchant table (offer)
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
                        barterDisplayTables(win, nullptr, bartererTable, -1);
                    } else {
                        int slotIndex = keyCode - 2400;
                        int tableIndex = slotIndex + gBartererTableOffset;
                        InventoryItem* inventoryItem = inventoryGetReversedSlot(gBartererTableInventory, tableIndex);
                        if (inventoryItem != nullptr) {
                            barterMoveFromTable(inventoryItem->item, inventoryItem->quantity, slotIndex, barterer, bartererTable, false);
                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                            barterDisplayTables(win, nullptr, bartererTable, -1);
                        }
                    }

                    keyCode = -1;
                }
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    itemMoveAll(hiddenBox, barterer);
    objectDestroy(hiddenBox, nullptr);

    if (armor != nullptr) {
        armor->flags |= OBJECT_WORN;
        itemAdd(barterer, armor, 1);
    }

    if (item2 != nullptr) {
        item2->flags |= OBJECT_IN_RIGHT_HAND;
        itemAdd(barterer, item2, 1);
    }

    if (item1 != nullptr) {
        itemAdd(barterer, item1, 1);
    }

    gInventoryWindowDudeFrmId = savedDudeFrmId;
    _exit_inventory(isoWasEnabled);
    _inven_dude = playerObj;

    // NOTE: Uninline.
    inventoryCommonFree();
}


// Same shape as client_dialog: bypass the loop that would execute authority
// locally, call the pure-render pieces directly.
//
// ►► DRIVER TRADES VIA THE b* VERBS; SPECTATORS ARE READ-ONLY. Vanilla's slot
// handlers mutate inventories in place, which on a mirror would invent items the
// server does not have, so the driver's clicks are rerouted to the b* verbs (see
// the modal loop below), including the partial-quantity dial. Spectators send
// nothing.
void inventoryOpenTradeViewer(Object* merchant, Object* playerTable, Object* merchantTable)
{
    if (merchant == nullptr || playerTable == nullptr || merchantTable == nullptr) {
        return;
    }

    // ►► REQUIRE A LIVE DIALOG SESSION WINDOW. The trade screen is built on top of
    // the dialog background window (it is the backing window below, and
    // _gdialog_barter_create_win blits from that window's buffer). A viewer that
    // dropped its dialog out of sync with the server — e.g. a spectator that closed
    // it locally — has no such window, and building the trade screen on it faults.
    // Refuse instead: barter reopens on the next EVENT_BARTER_STATE once the session
    // is back, and a missing screen is recoverable where a fault is not.
    if (!gameDialogBackgroundActive()) {
        fprintf(stderr, "client_barter: no dialog session window — refusing the trade screen\n");
        return;
    }

    // ►► SUBJECT SLOTS MUST BE PROTO-RESOLVABLE. Everything the screen puts in a
    // subject slot (_inven_dude, _stack[], _target_stack[]) goes through proto /
    // stat / art resolution at dozens of sites, and protoGetProto on a bad pid
    // hands back a null the caller dereferences — that is the segfault this
    // screen already shipped once. Refuse to open instead: a missing screen is
    // recoverable and reportable, a fault takes the whole viewer down.
    Object* driver = clientBarterDriver();
    Proto* probe = nullptr;
    if (driver == nullptr || protoGetProto(ProtoId(driver), &probe) != 0
        || protoGetProto(ProtoId(merchant), &probe) != 0) {
        fprintf(stderr, "client_barter: subject slot not proto-resolvable — refusing the trade screen\n");
        return;
    }

    ScopedGameMode gm(GameMode::kBarter);

    if (inventoryCommonInit() == -1) {
        return;
    }

    // ►► BUILD THE BARTER BACKGROUND FIRST. game_dialog.cc calls
    // _gdialog_barter_create_win() immediately before barterProcessUI, and
    // skipping it here was a real bug: without it there is no barter.frm behind
    // the screen, so the DIALOG skeleton shows through and the trade chrome
    // (money, the Offer/Talk buttons) draws on top of it, with an unloaded FRM in
    // the corner. The window below is the dialog window on purpose -- that is what
    // vanilla passes as `win` -- but it is the BACKING window, not the background.
    // ►► WHAT MUST COME DOWN BEFORE THE TRADE WINDOW GOES UP.
    // (1) The OPTIONS list: barter replaces it, it is WINDOW_MOVE_ON_TOP, and leaving
    // it lit was the reported "dialog leftovers blended with the new background".
    // clientBarterActive() is already true here, so this sync hides it — the single
    // owner does it. The REPLY window deliberately STAYS (vanilla keeps it through a
    // trade, it does not overlap the trade strip, and it is where a moused-over
    // item's description renders — see the handler note below).
    // (2) The NORMAL control subwindow (Barter/Review buttons): gGameDialogWindow is
    // one handle, and _gdialog_barter_create_win below OVERWRITES it. Without this
    // teardown the normal control window leaks and its Barter button stays live over
    // the trade screen (the reported leftover-button), and on exit the flavor bookkeeping
    // is wrong. Destroy it, then build the barter flavor into the same handle. The
    // dialog SESSION (background/head) window stays up — it is what gameDialogGetWindow()
    // returns below as the trade window's backing.
    clientModalWindowsSync();
    gameDialogExitControlWindow();
    gameDialogInitBarterWindows();

    int win = gameDialogGetWindow();

    // ►► RENDER THE DRIVER'S PACK, NOT THE LOCAL PLAYER'S. On a spectator's screen
    // the left panel is whoever is TRADING; showing your own inventory there was
    // the reported "P2 can't see P1's inventory". And even for the driver the real
    // object is the wrong source: the tick is parked for the whole trade, so no
    // inventory delta arrives and the mirror is frozen at open — items moved onto
    // a table would never leave the pack on screen. The snapshot is the only
    // accurate source while a trade runs, for everyone.
    // ►► TWO DIFFERENT THINGS, AND CONFLATING THEM SEGFAULTED. _inven_dude must be
    // a REAL CRITTER: the inventory UI reads it for stats, armour, encumbrance and
    // body art at 60+ sites, so a bare snapshot container (pid -1, no proto) faults
    // the first time one of those resolves a proto. Only the rendered item LIST
    // comes from the snapshot — which is still essential, because the tick is
    // parked and the real mirror is frozen for the whole trade.
    //
    // ►► SAVE THE LOCAL INVENTORY ANCHOR AND RESTORE IT ON EVERY EXIT. _inven_dude /
    // _stack[0] are the PERSISTENT anchor the plain inventory screen reads
    // (inventoryOpen does NOT re-derive them — _setup_inventory seeds _inven_dude
    // FROM _stack[0]). Pointing them at the trade DRIVER here and walking away left
    // a spectator's own inventory screen anchored to the driver afterwards: the
    // driver had no items, so the spectator's pack rendered EMPTY and stayed empty
    // (owner-reported "inventory vanished, doesn't recover"). Restore the local
    // actor on the way out. [[coop-mp-track]] cached-gDude-anchor bug class.
    Object* savedInvenDude = _inven_dude;
    Object* savedStack0 = _stack[0];
    _inven_dude = clientBarterDriver();
    _pud = &(clientBarterDriverInv()->data.inventory);
    gBartererTableObj = merchantTable;
    gPlayerTableObj = playerTable;
    gPlayerTableOffset = 0;
    gBartererTableOffset = 0;
    gPlayerTableInventory = &(playerTable->data.inventory);
    gBartererTableInventory = &(merchantTable->data.inventory);
    gInventoryBarterBackgroundWindow = win;
    _target_curr_stack = 0;
    _target_pud = &(clientBarterMerchantInv()->data.inventory); // list: snapshot
    _target_stack[0] = merchant;                                 // subject: real critter
    _target_stack_offset[0] = 0;

    // ►► RECOMPUTE THE BODY FID FOR THE DRIVER. gInventoryWindowDudeFrmId is set by
    // _adjust_fid inside inventoryCommonInit ABOVE, while _inven_dude still held
    // whatever the last screen left it -- so _display_body would blit a stale body
    // to the top-left panel (the reported unexplained sprite there). Now that
    // _inven_dude is the driver, recompute it before any display call reads it.
    _adjust_fid();

    bool isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_TRADE);

    // ►► RE-SEED AFTER _setup_inventory, AND CHECK IT TOOK. _setup_inventory
    // unconditionally re-derives the list slots from the subject slots
    // (`_pud = &(_inven_dude->data.inventory); _stack[0] = _inven_dude;` above),
    // so seeding them before this call silently accomplishes nothing — the panel
    // then renders the FROZEN real mirror instead of the snapshot, which is
    // invisible until something actually moves. That is a general hazard of
    // driving vanilla screens from mirrors, not a one-off, hence the loud check:
    // NDEBUG compiles assert() away, so this has to be a real branch.
    _pud = &(clientBarterDriverInv()->data.inventory);
    _target_pud = &(clientBarterMerchantInv()->data.inventory);
    if (_pud != &(clientBarterDriverInv()->data.inventory)
        || _target_pud != &(clientBarterMerchantInv()->data.inventory)) {
        fprintf(stderr, "client_barter: list slots did not take — refusing the trade screen\n");
        _exit_inventory(isoWasEnabled);
        gameDialogExitBarterWindows();
        // Restore the normal control window we tore down on entry (same reason as the
        // normal-exit path below) so a refused open leaves a valid dialog window.
        if (clientDialogActive()) {
            gameDialogInitControlWindow();
        }
        _inven_dude = savedInvenDude; // restore the local anchor (see save site)
        _stack[0] = savedStack0;
        inventoryCommonFree();
        return;
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);

    // ►► INSPECT IS LIVE AGAIN, AND THE FIX WAS UPSTREAM. This used to route the
    // description handler to a no-op sink, because _setup_inventory(TRADE) points it
    // at gameDialogRenderSupplementaryMessage — which draws into the dialog REPLY
    // window, and the viewer used to tear that window down when the trade screen went
    // up. Anything that rendered a description then faulted in the font blit (an
    // ARROW-cursor hover, or the quantity dial's closing cursor reset — the reported
    // "cancel the dial, then segfault"). Suppressing the render was treating the
    // symptom: the real defect was that the viewer destroyed a window VANILLA KEEPS
    // ALIVE FOR THE WHOLE CONVERSATION, trade included. client_dialog now keeps it and
    // only hides the options list, so the handler has its true target back and an
    // item description appears where vanilla puts it — the text area above the trade
    // strip (reply y225..283, trade y290..470: no overlap). The renderer also guards
    // the handle it writes to now, so a stale one is a no-op instead of a crash.
    // Nothing to save and restore any more: the handler _setup_inventory chose is
    // the one that runs.

    // Repaint everything the stream can change. Called on open and on every
    // STATE, which is the whole visual cue: no animation, no travel, the item is
    // simply on the table now.
    auto repaint = [&]() {
        _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
        _display_inventory(_stack_offset[0], -1, INVENTORY_WINDOW_TYPE_TRADE);
        _display_body(FrmId(merchant), INVENTORY_WINDOW_TYPE_TRADE);
        windowRefresh(gInventoryBarterBackgroundWindow);
        barterDisplayTables(win, gPlayerTableObj, gBartererTableObj, -1);
    };
    clientBarterApplyPending(); // fold in any snapshot latched before the window opened
    repaint();

    // The modal loop. GameMode::kBarter must be in kViewerModalMask or the wire
    // STALLS here — this loop starves conn.pump() exactly like every other
    // vanilla modal, and the service ticker is what keeps it fed.
    for (;;) {
        sharedFpsLimiter.mark();

        // The SERVER owns the trade's lifetime. When it says the trade is over,
        // this window goes, whoever you are — a spectator cannot dismiss someone
        // else's trade early, and the driver's own exit comes back around the
        // same way (verb -> server -> EVENT_BARTER_END).
        if (!clientBarterActive()) {
            break;
        }

        int keyCode = inputGetInput();

        // A trade may have ENDED during that pump (onBarterEnd only latches; it
        // does not free the mirrors — clientBarterFinalize does, from the main
        // loop). Break BEFORE any code below dereferences a mirror.
        if (!clientBarterActive()) {
            break;
        }

        // ►► ONLY THE DRIVER MAY ACT. Spectators render the same screen but send
        // nothing: the server refuses them anyway ("This isn't your trade"), and
        // a spectator's ESC must not close a window the server still owns. Every
        // branch below is gated on it, so a spectator falls straight through to
        // the repaint.
        bool draggedThisFrame = false;
        if (clientBarterIsDriver()) {
            if (keyCode == KEY_ESCAPE || keyCode == KEY_LOWERCASE_T) {
                // Leave the trade. A REQUEST, never local: the window closes when
                // the server's EVENT_BARTER_END comes back, so every viewer leaves
                // together and the mirrors are never torn down under a live trade.
                clientViewerBarterVerb("bdone", -1, 0);
            } else if (keyCode == KEY_LOWERCASE_M) {
                clientViewerBarterVerb("bcommit", -1, 0); // the Offer button
            } else if (keyCode == KEY_ARROW_UP) {
                if (_stack_offset[_curr_stack] > 0) {
                    _stack_offset[_curr_stack] -= 1;
                    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                    windowRefresh(gInventoryWindow);
                }
            } else if (keyCode == KEY_ARROW_DOWN) {
                if (_stack_offset[_curr_stack] + gInventorySlotsCount < _pud->length) {
                    _stack_offset[_curr_stack] += 1;
                    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                    windowRefresh(gInventoryWindow);
                }
            } else if (keyCode == KEY_CTRL_ARROW_UP) {
                if (_target_stack_offset[_target_curr_stack] > 0) {
                    _target_stack_offset[_target_curr_stack] -= 1;
                    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                    windowRefresh(gInventoryWindow);
                }
            } else if (keyCode == KEY_CTRL_ARROW_DOWN) {
                if (_target_stack_offset[_target_curr_stack] + gInventorySlotsCount < _target_pud->length) {
                    _target_stack_offset[_target_curr_stack] += 1;
                    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                    windowRefresh(gInventoryWindow);
                }
            } else if (keyCode == KEY_PAGE_UP) {
                if (gPlayerTableOffset > 0) {
                    gPlayerTableOffset -= 1;
                    barterDisplayTables(win, gPlayerTableObj, nullptr, -1);
                }
            } else if (keyCode == KEY_PAGE_DOWN) {
                if (gPlayerTableOffset + gInventorySlotsCount < gPlayerTableInventory->length) {
                    gPlayerTableOffset += 1;
                    barterDisplayTables(win, gPlayerTableObj, nullptr, -1);
                }
            } else if (keyCode == KEY_CTRL_PAGE_UP) {
                if (gBartererTableOffset > 0) {
                    gBartererTableOffset -= 1;
                    barterDisplayTables(win, nullptr, gBartererTableObj, -1);
                }
            } else if (keyCode == KEY_CTRL_PAGE_DOWN) {
                if (gBartererTableOffset + gInventorySlotsCount < gBartererTableInventory->length) {
                    gBartererTableOffset += 1;
                    barterDisplayTables(win, nullptr, gBartererTableObj, -1);
                }
            } else if ((mouseGetEvent() & MOUSE_EVENT_RIGHT_BUTTON_DOWN) != 0) {
                // ►► RIGHT-CLICK SWAPS HAND <-> ARROW, exactly as it does on every
                // other inventory screen — and on the trade screen that IS the
                // inspect switch. Under HAND a slot hover is inert (that is what
                // makes drag-and-drop feel like dragging); under ARROW the hover
                // fires objectLookAtFunc and the item's description renders into
                // the reply area above. It was missing here only because the render
                // it triggers used to fault (see the handler note above), so the
                // toggle was left out along with it.
                inventorySetCursor(gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND
                        ? INVENTORY_WINDOW_CURSOR_ARROW
                        : INVENTORY_WINDOW_CURSOR_HAND);
            } else if ((mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_DOWN) != 0
                && gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                // ►► REAL DRAG/DROP, VANILLA MACHINERY. A left-click on a slot runs
                // the actual vanilla move helper — pickup sprite, hold-to-drag,
                // drop hit-test, quantity dial — exactly as single-player barter.
                // The ONE difference is the authority boundary: on a driver the
                // helper SENDS THE VERB instead of mutating locally (see the 4
                // itemMoveForce sites, now gated on clientBarterIsDriver()), and the
                // server echoes the move back as EVENT_BARTER_STATE, applied at the
                // top of this loop. Resolving the item is the same reverse-indexed
                // row math vanilla uses; the helper takes it from there.
                //
                // The helpers run inner input loops (drag hold, quantity dial) that
                // pump the wire. That is SAFE now only because the reconcile is
                // deferred: a STATE arriving mid-drag merely latches, so no mirror
                // item is freed while the helper holds a pointer to it. Do not
                // reintroduce an apply at decode time.
                //
                // ►► ALWAYS REPAINT AFTER A DRAG (draggedThisFrame). The helper
                // visually ERASES the picked-up slot at pickup, but the driver does
                // NO local move (the move is a verb, applied only when the server's
                // STATE echoes back). So without an immediate repaint the slot stays
                // blank: if the drop sent a verb, until the echo lands a frame or two
                // later; if it landed on a non-target region (dropped back where it
                // was), FOREVER, until some other action repaints — the reported
                // "item no longer visible, a later click brings it back". Repainting
                // now restores the current mirror view; the echo then repaints the
                // moved result. Vanilla does the same unconditional redraw here.
                draggedThisFrame = true;
                if (keyCode >= 1000 && keyCode <= 1000 + gInventorySlotsCount) {
                    int slotIndex = keyCode - 1000;
                    int offset = _stack_offset[_curr_stack];
                    if (slotIndex + offset < _pud->length) {
                        InventoryItem* it = &(_pud->items[_pud->length - (slotIndex + offset + 1)]);
                        barterMoveToTable(it->item, it->quantity, slotIndex, offset, merchant, gPlayerTableObj, true);
                    }
                } else if (keyCode >= 2000 && keyCode <= 2000 + gInventorySlotsCount) {
                    int slotIndex = keyCode - 2000;
                    int offset = _target_stack_offset[_target_curr_stack];
                    if (slotIndex + offset < _target_pud->length) {
                        InventoryItem* it = &(_target_pud->items[_target_pud->length - (slotIndex + offset + 1)]);
                        barterMoveToTable(it->item, it->quantity, slotIndex, offset, merchant, gBartererTableObj, false);
                    }
                } else if (keyCode >= 2300 && keyCode <= 2300 + gInventorySlotsCount) {
                    int slotIndex = keyCode - 2300;
                    if (slotIndex + gPlayerTableOffset < gPlayerTableInventory->length) {
                        InventoryItem* it = &(gPlayerTableInventory->items[gPlayerTableInventory->length - (slotIndex + gPlayerTableOffset + 1)]);
                        barterMoveFromTable(it->item, it->quantity, slotIndex, merchant, gPlayerTableObj, true);
                    }
                } else if (keyCode >= 2400 && keyCode <= 2400 + gInventorySlotsCount) {
                    int slotIndex = keyCode - 2400;
                    if (slotIndex + gBartererTableOffset < gBartererTableInventory->length) {
                        InventoryItem* it = &(gBartererTableInventory->items[gBartererTableInventory->length - (slotIndex + gBartererTableOffset + 1)]);
                        barterMoveFromTable(it->item, it->quantity, slotIndex, merchant, gBartererTableObj, false);
                    }
                }
            }
        }
        if (_game_user_wants_to_quit != 0) {
            break;
        }

        // ►► SAFE POINT: apply any latched snapshot here, with no drag helper or
        // quantity dial on the stack, then repaint. Doing the apply here (not at
        // decode time) is the whole reason the vanilla drag helpers above can pump
        // the wire without a use-after-free. Repaint on a STATE change OR after a
        // drag (see draggedThisFrame — the drag leaves the panels mid-edit).
        clientBarterApplyPending();
        if (draggedThisFrame || clientBarterConsumeDirty()) {
            repaint();
        }

        // ►► COMMIT FEEDBACK. The server answers the Offer button only by moving
        // items (or not); a refused offer looks like "nothing happened". Give the
        // driver an audible confirm/deny for the last commit (the visible why is the
        // offer-vs-asking $ totals, now server-correct). Text feedback wants a
        // visible surface the trade screen does not have free — that lands with the
        // <inspect> pass. Driver-only: a spectator never committed.
        if (clientBarterIsDriver()) {
            int rc = clientBarterConsumeResult();
            if (rc == 0) {
                soundPlayFile("ib1p1xx1"); // BARTER_RESULT_OK — the trade went through
            } else if (rc > 0) {
                soundPlayFile("iisxxxx1"); // refused (bad offer / too heavy)
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    _exit_inventory(isoWasEnabled);
    gameDialogExitBarterWindows();
    // ►► RECREATE THE NORMAL CONTROL SUBWINDOW if the conversation is still live —
    // this is the segfault fix. gameDialogExitBarterWindows just set gGameDialogWindow
    // to -1; the main loop's next node render would dereference that dead handle. The
    // barter flavor was destroyed above; swap the normal flavor back into the same
    // handle (and restore _dialogue_state=1) so the dialog resumes with a valid
    // control window. If the trade's exit ALSO ended the conversation (bdone ->
    // _barter_end_to_talk_to can), leave it down: clientModalWindowsSync() sees the
    // session as over and _gdialogExitFromScript cleans the background + the already
    // -gone control, no orphan windows.
    //
    // ►► NODE SUBWINDOWS ARE NOT TOUCHED HERE — the main loop's clientModalWindowsSync()
    // owns those (I2). They stayed UP for the whole trade (vanilla does the same, and
    // it is what let an item description render above the trade strip); the options
    // list was merely hidden, and the next reconcile un-hides it iff the conversation
    // is still live. The old order-of-arrival guess (BARTER_END vs DIALOG_END) is
    // gone: the reconcile reads the final latched state.
    if (clientDialogActive()) {
        gameDialogInitControlWindow();
    }
    // Restore the local inventory anchor (see the save site above) so the plain
    // inventory screen reads the local actor again, not the trade driver.
    _inven_dude = savedInvenDude;
    _stack[0] = savedStack0;
    // Restore the item-description handler we routed to a sink for the trade (the
    // loop only breaks, never returns, so this is the one exit past that swap).
    inventoryCommonFree();
}

// 0x47620C
static void _container_enter(int keyCode, int inventoryWindowType)
{
    if (keyCode >= 2000) {
        int index = _target_pud->length - (_target_stack_offset[_target_curr_stack] + keyCode - 2000 + 1);
        if (index < _target_pud->length && _target_curr_stack < 9) {
            InventoryItem* inventoryItem = &(_target_pud->items[index]);
            Object* item = inventoryItem->item;
            if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
                _target_curr_stack += 1;
                _target_stack[_target_curr_stack] = item;
                _target_stack_offset[_target_curr_stack] = 0;

                _target_pud = &(item->data.inventory);

                _display_body(FrmId(item), inventoryWindowType);
                _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, inventoryWindowType);
                windowRefresh(gInventoryWindow);
            }
        }
    } else {
        int index = _pud->length - (_stack_offset[_curr_stack] + keyCode - 1000 + 1);
        if (index < _pud->length && _curr_stack < 9) {
            InventoryItem* inventoryItem = &(_pud->items[index]);
            Object* item = inventoryItem->item;
            if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
                _curr_stack += 1;

                _stack[_curr_stack] = item;
                _stack_offset[_curr_stack] = 0;

                _inven_dude = _stack[_curr_stack];
                _pud = &(item->data.inventory);

                _adjust_fid();
                _display_body(FrmId::Empty(), inventoryWindowType);
                _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);
            }
        }
    }
}

// 0x476394
static void _container_exit(int keyCode, int inventoryWindowType)
{
    if (keyCode == 2500) {
        if (_curr_stack > 0) {
            _curr_stack -= 1;
            _inven_dude = _stack[_curr_stack];
            _pud = &_inven_dude->data.inventory;
            _adjust_fid();
            _display_body(FrmId::Empty(), inventoryWindowType);
            _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);
        }
    } else if (keyCode == 2501) {
        if (_target_curr_stack > 0) {
            _target_curr_stack -= 1;
            Object* target = _target_stack[_target_curr_stack];
            _target_pud = &(target->data.inventory);
            _display_body(FrmId(target), inventoryWindowType);
            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, inventoryWindowType);
            windowRefresh(gInventoryWindow);
        }
    }
}

// Drop item inside a container item (bag, backpack, etc.).
// 0x476464
// How many of a stack the VIEWER's drop leaves should send. Same convention as the
// local stack moves (_drop_into_container / _drop_ammo_into_weapon just below): more
// than one asks HOW MANY, one is implicit, -1 means the player cancelled. Equipped
// slot = 1, and an ARMED explosive skips the prompt exactly like the local DROP leaf
// (whatever count it sends is moot — itemDropStack already drops the single armed
// object and ignores the count, item.cc). The count modal is wire-safe inside the
// viewer's inventory screen: the loot take/put leaves already prompt through it.
static int inventoryViewerDropQuantity(Object* item, Object** itemSlot, int quantity)
{
    if (itemSlot != nullptr) {
        return 1;
    }
    if (quantity > 1 && !explosiveIsActiveExplosive(ProtoId(item))) {
        return inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
    }
    return quantity > 1 ? quantity : 1;
}

static int _drop_into_container(Object* container, Object* item, int sourceIndex, Object** itemSlot, int quantity)
{
    if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_CONTAINER, item, container)) {
        return -1;
    }

    int quantityToMove;
    if (quantity > 1) {
        quantityToMove = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
    } else {
        quantityToMove = 1;
    }

    if (quantityToMove == -1) {
        return -1;
    }

    if (clientViewerActive()) {
        if (container->netId == 0 || item->netId == 0) {
            debugPrint("inventory: viewer cannot store an unbound item/container\n");
            return -1;
        }
        clientViewerPutInContainer(container, item, quantityToMove);
        return 0;
    }

    if (sourceIndex != -1) {
        if (itemRemoveWithReason(_inven_dude, item, quantityToMove, RemoveInventoryObjectHookReason::DropIntoContainer) == -1) {
            return -1;
        }
    }

    int rc = itemAttemptAdd(container, item, quantityToMove);
    if (rc != 0) {
        if (sourceIndex != -1) {
            // SFALL: Fix for items disappearing from inventory when you try to
            // drag them to bag/backpack in the inventory list and are
            // overloaded.
            itemAdd(_inven_dude, item, quantityToMove);
        }
    } else {
        if (itemSlot != nullptr) {
            if (itemSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], gInventoryArmor, nullptr);
            }
            *itemSlot = nullptr;
        }
    }

    return rc;
}

// 0x47650C
static InventoryAmmoMoveResult _drop_ammo_into_weapon(Object* weapon, Object* ammo, Object** ammoItemSlot, int quantity, int keyCode)
{
    if (itemGetType(weapon) != ITEM_TYPE_WEAPON) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    if (itemGetType(ammo) != ITEM_TYPE_AMMO) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    if (ProtoId(weapon) == ItemProtoTypeId::SolarScorcher) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    bool replaceAmmo = false;
    if (!weaponCanBeReloadedWith(weapon, ammo)) {
        if (!settings.qol.fast_ammo_load
            || !weaponCanBeUnloaded(weapon)
            || !weaponCanBeReloadedWithReplacingAmmo(weapon, ammo)) {
            return INVENTORY_AMMO_MOVE_RESULT_FAILED;
        }
        replaceAmmo = true;
    }

    if (!replaceAmmo && ammoGetQuantity(weapon) >= ammoGetCapacity(weapon)) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_WEAPON_RELOAD, ammo, weapon)) {
        return INVENTORY_AMMO_MOVE_RESULT_BLOCKED;
    }

    int quantityToMove;
    if (settings.qol.fast_ammo_load) {
        quantityToMove = quantity;
    } else if (quantity > 1) {
        quantityToMove = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, ammo, quantity);
    } else {
        quantityToMove = 1;
    }

    if (quantityToMove == -1) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    if (clientViewerActive()) {
        if (weapon->netId == 0 || ammo->netId == 0) {
            debugPrint("inventory: viewer cannot load unbound weapon/ammo\n");
            return INVENTORY_AMMO_MOVE_RESULT_FAILED;
        }
        clientViewerLoadAmmo(weapon, ammo, quantityToMove, replaceAmmo);
        return INVENTORY_AMMO_MOVE_RESULT_SUCCESS;
    }

    Object* sourceItem = ammo;
    bool isReloaded = false;
    int rc = itemRemoveQuietly(_inven_dude, weapon, 1);
    std::vector<Object*> unloadedAmmo;
    if (replaceAmmo) {
        Object* oldAmmo;
        while ((oldAmmo = weaponUnload(weapon)) != nullptr) {
            unloadedAmmo.push_back(oldAmmo);
        }
    }

    for (int index = 0; index < quantityToMove; index++) {
        int rcReload = weaponReload(weapon, sourceItem);
        if (rcReload == 0) {
            if (ammoItemSlot != nullptr) {
                *ammoItemSlot = nullptr;
            } else {
                itemRemoveQuietly(_inven_dude, sourceItem, 1);
            }

            objectDestroy(sourceItem);

            isReloaded = true;
            if (_inven_from_button(keyCode, &sourceItem, nullptr, nullptr) == 0) {
                break;
            }
        }
        if (rcReload != -1) {
            isReloaded = true;
        }
        if (rcReload != 0) {
            break;
        }
    }

    if (rc != -1) {
        itemAdd(_inven_dude, weapon, 1);
    }

    for (Object* oldAmmo : unloadedAmmo) {
        itemAdd(_inven_dude, oldAmmo, 1);
    }

    if (!isReloaded) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    const char* sfx = sfxBuildWeaponName(WEAPON_SOUND_EFFECT_READY, weapon, HIT_MODE_RIGHT_WEAPON_PRIMARY, nullptr);
    soundPlayFile(sfx);

    return INVENTORY_AMMO_MOVE_RESULT_SUCCESS;
}

// 0x47664C
static void _draw_amount(int value, int inventoryWindowType)
{
    FrmImage numbersFrmImage;
    if (!numbersFrmImage.lock(InterfaceFrameId::BigNum)) {
        return;
    }

    Rect rect;

    int windowWidth = windowGetWidth(_mt_wid);
    unsigned char* windowBuffer = windowGetBuffer(_mt_wid);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        rect.left = 125;
        rect.top = 45;
        rect.right = 195;
        rect.bottom = 69;

        int ranks[5];
        ranks[4] = value % 10;
        ranks[3] = value / 10 % 10;
        ranks[2] = value / 100 % 10;
        ranks[1] = value / 1000 % 10;
        ranks[0] = value / 10000 % 10;

        windowBuffer += rect.top * windowWidth + rect.left;

        for (int index = 0; index < 5; index++) {
            unsigned char* src = numbersFrmImage.getData() + 14 * ranks[index];
            blitBufferToBuffer(src, 14, 24, 336, windowBuffer, windowWidth);
            windowBuffer += 14;
        }
    } else {
        rect.left = 133;
        rect.top = 64;
        rect.right = 189;
        rect.bottom = 88;

        windowBuffer += windowWidth * rect.top + rect.left;
        blitBufferToBuffer(numbersFrmImage.getData() + 14 * (value / 60), 14, 24, 336, windowBuffer, windowWidth);
        blitBufferToBuffer(numbersFrmImage.getData() + 14 * (value % 60 / 10), 14, 24, 336, windowBuffer + 14 * 2, windowWidth);
        blitBufferToBuffer(numbersFrmImage.getData() + 14 * (value % 10), 14, 24, 336, windowBuffer + 14 * 3, windowWidth);
    }

    windowRefreshRect(_mt_wid, &rect);
}

// 0x47688C
static int inventoryQuantitySelect(int inventoryWindowType, Object* item, int max, int defaultValue)
{
    ScopedGameMode gm(GameMode::kCounter);

    inventoryQuantityWindowInit(inventoryWindowType, item);

    int value;
    int min;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        if (max > 99999) {
            max = 99999;
        }
        min = 1;
        value = std::clamp(defaultValue, min, max);
    } else {
        value = 60;
        min = 10;
    }

    _draw_amount(value, inventoryWindowType);

    bool isTyping = false;
    for (;;) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            inventoryQuantityWindowFree(inventoryWindowType);
            return -1;
        }

        if (keyCode == KEY_RETURN || keyCode == 500) {
            if (value >= min && value <= max) {
                if (inventoryWindowType != INVENTORY_WINDOW_TYPE_SET_TIMER || value % 10 == 0) {
                    if (keyCode != 500) {
                        soundPlayFile("ib1p1xx1");
                    }
                    break;
                }
            }

            soundPlayFile("iisxxxx1");

        } else if (keyCode == 5000 || keyCode == KEY_LOWERCASE_A) {
            if (keyCode == KEY_LOWERCASE_A) {
                soundPlayFile("ib1p1xx1");
            }
            isTyping = false;
            value = max;
            _draw_amount(value, inventoryWindowType);
        } else if (keyCode == 6000) {
            isTyping = false;
            if (value < max) {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
                    if ((mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_REPEAT) != 0) {
                        getTicks();

                        unsigned int delay = 100;
                        while ((mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_REPEAT) != 0) {
                            sharedFpsLimiter.mark();

                            if (value < max) {
                                value++;
                            }

                            _draw_amount(value, inventoryWindowType);
                            inputGetInput();

                            if (delay > 1) {
                                delay--;
                                inputPauseForTocks(delay);
                            }

                            renderPresent();
                            sharedFpsLimiter.throttle();
                        }
                    } else {
                        if (value < max) {
                            value++;
                        }
                    }
                } else {
                    value += 10;
                }

                _draw_amount(value, inventoryWindowType);
                continue;
            }
        } else if (keyCode == 7000) {
            isTyping = false;
            if (value > min) {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
                    if ((mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_REPEAT) != 0) {
                        getTicks();

                        unsigned int delay = 100;
                        while ((mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_REPEAT) != 0) {
                            sharedFpsLimiter.mark();

                            if (value > min) {
                                value--;
                            }

                            _draw_amount(value, inventoryWindowType);
                            inputGetInput();

                            if (delay > 1) {
                                delay--;
                                inputPauseForTocks(delay);
                            }

                            renderPresent();
                            sharedFpsLimiter.throttle();
                        }
                    } else {
                        if (value > min) {
                            value--;
                        }
                    }
                } else {
                    value -= 10;
                }

                _draw_amount(value, inventoryWindowType);
                continue;
            }
        }

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
            if (keyCode >= KEY_0 && keyCode <= KEY_9) {
                int number = keyCode - KEY_0;
                if (!isTyping) {
                    value = 0;
                }

                value = 10 * value % 100000 + number;
                isTyping = true;

                _draw_amount(value, inventoryWindowType);
                continue;
            } else if (keyCode == KEY_BACKSPACE) {
                if (!isTyping) {
                    value = 0;
                }

                value /= 10;
                isTyping = true;

                _draw_amount(value, inventoryWindowType);
                continue;
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    inventoryQuantityWindowFree(inventoryWindowType);

    return value;
}

// Creates move items/set timer interface.
//
// 0x476AB8
static int inventoryQuantityWindowInit(int inventoryWindowType, Object* item)
{
    const int oldFont = fontGetCurrent();
    fontSetCurrent(103);

    const InventoryWindowDescription* windowDescription = &(gInventoryWindowDescriptions[inventoryWindowType]);

    // Maintain original position in original resolution, otherwise center it.
    int quantityWindowX = screenGetWidth() != 640
        ? (screenGetWidth() - windowDescription->width) / 2
        : windowDescription->x;
    int quantityWindowY = screenGetHeight() != 480
        ? inventoryGetCenteredWindowY(windowDescription->height)
        : windowDescription->y;
    _mt_wid = windowCreate(quantityWindowX, quantityWindowY, windowDescription->width, windowDescription->height, static_cast<ColorWithFlags>(257), WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    unsigned char* windowBuffer = windowGetBuffer(_mt_wid);

    FrmImage backgroundFrmImage;
    const FrmId backgroundFrmId = FrmId(windowDescription->frmId);
    if (backgroundFrmImage.lock(backgroundFrmId)) {
        blitBufferToBuffer(backgroundFrmImage.getData(),
            windowDescription->width,
            windowDescription->height,
            windowDescription->width,
            windowBuffer,
            windowDescription->width);
    }

    MessageListItem messageListItem;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        // MOVE ITEMS
        messageListItem.num = 21;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            int length = fontGetStringWidth(messageListItem.text);
            fontDrawText(windowBuffer + windowDescription->width * 9 + (windowDescription->width - length) / 2, messageListItem.text, 200, windowDescription->width, COLOR_YELLOW_2);
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_SET_TIMER) {
        // SET TIMER
        messageListItem.num = 23;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            int length = fontGetStringWidth(messageListItem.text);
            fontDrawText(windowBuffer + windowDescription->width * 9 + (windowDescription->width - length) / 2, messageListItem.text, 200, windowDescription->width, COLOR_YELLOW_2);
        }

        // Timer overlay
        FrmImage overlayFrmImage;
        if (overlayFrmImage.lock(InterfaceFrameId::TimerOverlay)) {
            blitBufferToBuffer(overlayFrmImage.getData(),
                105, 81, 105,
                windowBuffer + 34 * windowDescription->width + 113, windowDescription->width);
        }
    }

    const FrmId inventoryFrmId = itemGetInventoryFrmId(item);
    artRender(inventoryFrmId, windowBuffer + windowDescription->width * 46 + 16, INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, windowDescription->width);

    int x = 194;
    int y = 64;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        x = 200;
        y = 46;
    }

    // Plus button
    int btn = buttonCreateActionWithFrm(_mt_wid,
        x, y, 6000, -1, InterfaceFrameId::CharacterEditorSpecialPositiveOff, InterfaceFrameId::CharacterEditorSpecialPositiveOn);

    // Minus button
    btn = buttonCreateActionWithFrm(_mt_wid,
        x, y + 12, 7000, -1, InterfaceFrameId::CharacterEditorSpecialNegativeOff, InterfaceFrameId::CharacterEditorSpecialNegativeOn);

    // Done
    btn = buttonCreateActionWithFrm(_mt_wid,
        98, 128, -1, 500, InterfaceFrameId::LittleRedButtonUp, InterfaceFrameId::LittleRedButtonDown);

    // Cancel
    btn = buttonCreateActionWithFrm(_mt_wid,
        148, 128, -1, KEY_ESCAPE, InterfaceFrameId::LittleRedButtonUp, InterfaceFrameId::LittleRedButtonDown);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        _moveFrmImages[6].lock(InterfaceFrameId::AllButtonPressed);
        _moveFrmImages[7].lock(InterfaceFrameId::AllButtonUnpressed);

        if (_moveFrmImages[6].isLocked() && _moveFrmImages[7].isLocked()) {
            // ALL
            messageListItem.num = 22;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                int length = fontGetStringWidth(messageListItem.text);

                // TODO: Where is y? Is it hardcoded in to 376?
                fontDrawText(_moveFrmImages[6].getData() + (94 - length) / 2 + 376, messageListItem.text, 200, 94, COLOR_YELLOW_2);
                fontDrawText(_moveFrmImages[7].getData() + (94 - length) / 2 + 470, messageListItem.text, 200, 94, COLOR_DARK_YELLOW_4);

                btn = buttonCreate(_mt_wid,
                    120, 80, 94, 33, -1, -1, -1, 5000,
                    _moveFrmImages[6].getData(),
                    _moveFrmImages[7].getData(),
                    nullptr,
                    BUTTON_FLAG_TRANSPARENT);
                if (btn != -1) {
                    buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
                }
            }
        }
    }

    windowRefresh(_mt_wid);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
    fontSetCurrent(oldFont);

    return 0;
}

// 0x477030
static int inventoryQuantityWindowFree(int inventoryWindowType)
{
    int count = inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS ? 8 : 6;

    for (int index = 0; index < count; index++) {
        _moveFrmImages[index].unlock();
    }

    windowDestroy(_mt_wid);

    return 0;
}

// 0x477074
int inventorySetTimer(Object* item)
{
    bool isInitialized = _inven_is_initialized;

    if (!isInitialized) {
        if (inventoryCommonInit() == -1) {
            return -1;
        }
    }

    int seconds = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_SET_TIMER, item, 180);

    if (!isInitialized) {
        // NOTE: Uninline.
        inventoryCommonFree();
    }

    return seconds;
}

Object* inventoryGetTargetObject()
{
    return _target_stack[_target_curr_stack];
}

static bool inventoryBackgroundLoad(FrmImage& image, InterfaceFrameId col1FrmId, const char* col2Name, int columns)
{
    image.unlock();

    if (columns == 1) {
        return image.lock(col1FrmId);
    }

    if (columns == 2) {
        return image.lock(OBJ_TYPE_INTERFACE, col2Name);
    }

    return false;
}

static int buttonCreateSlot(int win, int x, int y, int width, int height, int keyCode, ButtonCallback* onEnter, ButtonCallback* onExit)
{
    int btn = buttonCreate(win, x, y, width, height, keyCode, -1, keyCode);
    if (btn != -1) {
        buttonSetMouseCallbacks(btn, onEnter, onExit, nullptr, nullptr);
    }
    return btn;
}

static int buttonCreateAction(int win, int x, int y, int width, int height, int keyCode)
{
    return buttonCreate(win, x, y, width, height, -1, -1, keyCode);
}

static int buttonCreateActionWithFrm(int win, int x, int y, int mouseDownEventCode, int mouseUpEventCode, const InterfaceFrmId& normalId, const InterfaceFrmId& pressedId)
{
    int btn = buttonCreateWithFrm(win,
        x, y, -1, -1,
        mouseDownEventCode, mouseUpEventCode,
        normalId, pressedId,
        {}, BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }
    return btn;
}

static ConstBuffer2D inventoryGetBackgroundBuffer(int inventoryWindowType, InterfaceFrameId fallbackFrmId, FrmImage& fallbackImage, int& sourceXOffset)
{
    sourceXOffset = 0;

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        return inventoryFrmImage.getBuffer();
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        return inventoryLootFrmImage.getBuffer();
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        sourceXOffset = INVENTORY_TRADE_WINDOW_OFFSET;
        return { windowGetBuffer(gInventoryBarterBackgroundWindow),
            INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH,
            windowGetHeight(gInventoryBarterBackgroundWindow) };
    }

    if (fallbackImage.lock(fallbackFrmId)) {
        return fallbackImage.getBuffer();
    }

    return {};
}

static void inventoryCreateSlotButtons(int baseKeyCode, int scrollerX, int scrollerY, int columns)
{
    for (int row = 0; row < INVENTORY_ROWS; row++) {
        for (int column = 0; column < columns; column++) {
            int keyCode = baseKeyCode + row * columns + column;
            buttonCreateSlot(gInventoryWindow,
                scrollerX + column * INVENTORY_SLOT_WIDTH,
                scrollerY + row * INVENTORY_SLOT_HEIGHT,
                INVENTORY_SLOT_WIDTH,
                INVENTORY_SLOT_HEIGHT,
                keyCode,
                inventoryItemSlotOnMouseEnter,
                inventoryItemSlotOnMouseExit);
        }
    }
}

static bool inventoryGetCurrentWindowType(InventoryWindowType* inventoryWindowTypePtr)
{
    if (GameMode::isInGameMode(GameMode::kBarter)) {
        *inventoryWindowTypePtr = INVENTORY_WINDOW_TYPE_TRADE;
        return true;
    }

    if (GameMode::isInGameMode(GameMode::kLoot)) {
        *inventoryWindowTypePtr = INVENTORY_WINDOW_TYPE_LOOT;
        return true;
    }

    if (GameMode::isInGameMode(GameMode::kUseOn)) {
        *inventoryWindowTypePtr = INVENTORY_WINDOW_TYPE_USE_ITEM_ON;
        return true;
    }

    if (GameMode::isInGameMode(GameMode::kInventory)) {
        *inventoryWindowTypePtr = INVENTORY_WINDOW_TYPE_NORMAL;
        return true;
    }

    return false;
}

static int inventoryGetWindowWidth(int inventoryWindowType)
{
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) return inventoryLayout.windowWidth;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) return inventoryLootLayout.windowWidth;
    return gInventoryWindowDescriptions[inventoryWindowType].width;
}

static int inventoryGetWindowHeight(int inventoryWindowType)
{
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) return inventoryLayout.windowHeight;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) return inventoryLootLayout.windowHeight;
    return gInventoryWindowDescriptions[inventoryWindowType].height;
}

static int inventoryGetCenteredWindowY(int windowHeight)
{
    return (screenGetVisibleHeight() - windowHeight) / 2;
}

static int inventoryChooseColumns(FrmImage& image, int expandedWidth, InterfaceFrameId col1FrmId, const char* col2Name)
{
    if (settings.ui.inventory_columns <= 1) {
        return 1;
    }

    if (expandedWidth <= screenGetWidth() && inventoryBackgroundLoad(image, col1FrmId, col2Name, 2)) {
        return 2;
    }

    return 1;
}

static void inventoryNormalApplyLayout(int columns)
{
    int shift = (columns - 1) * INVENTORY_SLOT_WIDTH;
    inventoryLayout.columns = columns;
    inventoryLayout.visibleSlots = INVENTORY_ROWS * columns;
    inventoryLayout.windowWidth = INVENTORY_WINDOW_WIDTH + shift;
    inventoryLayout.windowHeight = gInventoryWindowDescriptions[INVENTORY_WINDOW_TYPE_NORMAL].height;
    inventoryLayout.scrollerX = INVENTORY_SCROLLER_X;
    inventoryLayout.scrollerY = INVENTORY_SCROLLER_Y;
    inventoryLayout.scrollerWidth = columns * INVENTORY_SLOT_WIDTH;
    inventoryLayout.scrollerHeight = INVENTORY_ROWS * INVENTORY_SLOT_HEIGHT;
    inventoryLayout.leftHandSlotX = INVENTORY_LEFT_HAND_SLOT_X + shift;
    inventoryLayout.rightHandSlotX = INVENTORY_RIGHT_HAND_SLOT_X + shift;
    inventoryLayout.armorSlotX = INVENTORY_ARMOR_SLOT_X + shift;
    inventoryLayout.bodyViewX = INVENTORY_PC_BODY_VIEW_X + shift;
    inventoryLayout.summaryX = INVENTORY_SUMMARY_X + shift;
    inventoryLayout.scrollButtonX = 128 + shift;
    inventoryLayout.doneButtonX = 437 + shift;
}

static void inventoryLootApplyLayout(int columns)
{
    int extraColumns = columns - 1;
    int scrollerShift = extraColumns * (367 - INVENTORY_LOOT_RIGHT_SCROLLER_X);
    int leftBodyViewShift = extraColumns * (47 - INVENTORY_LOOT_LEFT_BODY_VIEW_X);
    int rightBodyViewShift = extraColumns * (563 - INVENTORY_LOOT_RIGHT_BODY_VIEW_X);
    int rightPaneShift = extraColumns * (612 - 476);

    inventoryLootLayout.columns = columns;
    inventoryLootLayout.visibleSlots = INVENTORY_ROWS * columns;
    inventoryLootLayout.windowWidth = INVENTORY_LOOT_WINDOW_WIDTH + extraColumns * (INVENTORY_LOOT_WINDOW_WIDTH_EXPANDED - INVENTORY_LOOT_WINDOW_WIDTH);
    inventoryLootLayout.windowHeight = gInventoryWindowDescriptions[INVENTORY_WINDOW_TYPE_LOOT].height;
    inventoryLootLayout.leftScrollerX = INVENTORY_LOOT_LEFT_SCROLLER_X;
    inventoryLootLayout.leftScrollerY = INVENTORY_LOOT_LEFT_SCROLLER_Y;
    inventoryLootLayout.rightScrollerX = INVENTORY_LOOT_RIGHT_SCROLLER_X + scrollerShift;
    inventoryLootLayout.rightScrollerY = INVENTORY_LOOT_RIGHT_SCROLLER_Y;
    inventoryLootLayout.scrollerWidth = columns * INVENTORY_SLOT_WIDTH;
    inventoryLootLayout.scrollerHeight = INVENTORY_ROWS * INVENTORY_SLOT_HEIGHT;
    inventoryLootLayout.leftBodyViewX = INVENTORY_LOOT_LEFT_BODY_VIEW_X + leftBodyViewShift;
    inventoryLootLayout.rightBodyViewX = INVENTORY_LOOT_RIGHT_BODY_VIEW_X + rightBodyViewShift;
    inventoryLootLayout.leftScrollButtonX = 128;
    inventoryLootLayout.rightScrollButtonX = 379 + rightPaneShift;
    inventoryLootLayout.takeAllButtonX = 432 + rightPaneShift;
    inventoryLootLayout.doneButtonX = 476 + rightPaneShift;
    // Center the two 20px-wide critter-nav arrows below the right avatar
    int critterBtnCenterX = inventoryLootLayout.rightBodyViewX + INVENTORY_BODY_VIEW_WIDTH / 2;
    inventoryLootLayout.prevCritterButtonX = critterBtnCenterX - 20;
    inventoryLootLayout.nextCritterButtonX = critterBtnCenterX;
    inventoryLootLayout.partySlotX = inventoryLootLayout.rightBodyViewX - 22;
    inventoryLootLayout.partyArmorSlotY = 181;
}

static void inventoryNormalLayoutUpdate()
{
    int columns = inventoryChooseColumns(inventoryFrmImage, INVENTORY_WINDOW_WIDTH + INVENTORY_SLOT_WIDTH, InterfaceFrameId::InventoryBox, "invbox2.png");
    if (columns == 1) {
        inventoryBackgroundLoad(inventoryFrmImage, InterfaceFrameId::InventoryBox, "invbox2.png", 1);
    }

    inventoryNormalApplyLayout(columns);
}

static void inventoryLootLayoutUpdate()
{
    int columns = inventoryChooseColumns(inventoryLootFrmImage, INVENTORY_LOOT_WINDOW_WIDTH_EXPANDED, InterfaceFrameId::LootContainerWindow, "loot2.png");
    if (columns == 1) {
        inventoryBackgroundLoad(inventoryLootFrmImage, InterfaceFrameId::LootContainerWindow, "loot2.png", 1);
    }

    inventoryLootApplyLayout(columns);
}

static void inventoryNormalClampStackOffset()
{
    int maxOffset = inventoryComputeAlignedMaxOffset(_pud->length, inventoryLayout.visibleSlots, inventoryLayout.columns);
    if (_stack_offset[_curr_stack] > maxOffset) {
        _stack_offset[_curr_stack] = maxOffset;
    }
    if (_stack_offset[_curr_stack] < 0) {
        _stack_offset[_curr_stack] = 0;
    }
}

static void inventoryClampOffset(int* offsetPtr, int inventoryLength, int visibleSlots)
{
    assert(offsetPtr != nullptr);

    int maxOffset = inventoryComputeAlignedMaxOffset(inventoryLength, visibleSlots, 1);
    if (*offsetPtr > maxOffset) {
        *offsetPtr = maxOffset;
    }
    if (*offsetPtr < 0) {
        *offsetPtr = 0;
    }
}

static void barterClampTableOffsets()
{
    if (gPlayerTableInventory != nullptr) {
        inventoryClampOffset(&gPlayerTableOffset, gPlayerTableInventory->length, gInventorySlotsCount);
    }

    if (gBartererTableInventory != nullptr) {
        inventoryClampOffset(&gBartererTableOffset, gBartererTableInventory->length, gInventorySlotsCount);
    }
}

static InventoryItem* inventoryGetReversedSlot(Inventory* inventory, int index)
{
    if (inventory == nullptr || index < 0 || index >= inventory->length) {
        return nullptr;
    }

    return &(inventory->items[inventory->length - (index + 1)]);
}

static int inventoryLootGetSlotX(bool targetInventory, int slotIndex)
{
    int scrollerX = targetInventory ? inventoryLootLayout.rightScrollerX : inventoryLootLayout.leftScrollerX;
    return scrollerX + slotIndex % inventoryLootLayout.columns * INVENTORY_SLOT_WIDTH;
}

static int inventoryLootGetSlotY(int slotIndex)
{
    return inventoryLootLayout.leftScrollerY + slotIndex / inventoryLootLayout.columns * INVENTORY_SLOT_HEIGHT;
}

static bool inventoryLootMouseHitTestScroller(bool targetInventory)
{
    int scrollerX = targetInventory ? inventoryLootLayout.rightScrollerX : inventoryLootLayout.leftScrollerX;
    int scrollerY = targetInventory ? inventoryLootLayout.rightScrollerY : inventoryLootLayout.leftScrollerY;
    return mouseHitTestInWindow(gInventoryWindow,
        scrollerX,
        scrollerY,
        scrollerX + inventoryLootLayout.scrollerWidth,
        scrollerY + inventoryLootLayout.scrollerHeight);
}

static void inventoryLootRenderPaneWeight(unsigned char* windowBuffer, int pitch, bool targetPane, Object* object, int extraWeight)
{
    int loot_weight_indicator = settings.ui.loot_weight_indicator;

    bool showLabel = inventoryLootLayout.columns > 1;
    bool showDetailed = loot_weight_indicator == 2 && showLabel;
    bool showSize = loot_weight_indicator == 3;
    bool showSimple = loot_weight_indicator == 1 || (loot_weight_indicator == 2 && !showLabel) || showSize;

    if (!showDetailed && !showSimple) {
        return;
    }

    int containerSizeThreshold = settings.ui.loot_container_size_indicator_threshold;

    // Wt.
    MessageListItem messageListItem;
    messageListItem.num = 30;

    if (showLabel) {
        if (!messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            showSimple = true;
            showDetailed = false;
            showLabel = false;
        }
    }

    char formattedText[30];
    formattedText[0] = '\0';

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    if (inventoryLootFrmImage.isLocked()) {
        int x = targetPane ? inventoryLootLayout.rightScrollerX : inventoryLootLayout.leftScrollerX;
        int y = (targetPane ? inventoryLootLayout.rightScrollerY : inventoryLootLayout.leftScrollerY) + inventoryLootLayout.scrollerHeight + 2;
        blitBufferToBuffer(inventoryLootFrmImage.getData() + inventoryLootFrmImage.getWidth() * y + x,
            inventoryLootLayout.scrollerWidth,
            fontGetLineHeight(),
            inventoryLootFrmImage.getWidth(),
            windowBuffer + pitch * y + x,
            pitch);
    }

    ColorWithFlags color = COLOR_GREEN | DRAW_TEXT_FLAG_NONE;
    int inventoryWeight = objectGetInventoryWeight(object);
    if (ProtoId(object).objectType() == OBJ_TYPE_CRITTER) {
        int currentWeight = inventoryWeight + extraWeight;
        int maxWeight = critterGetStat(object, STAT_CARRY_WEIGHT);
        int weightPercentage = maxWeight < 1 ? 0 : (int)std::ceil(currentWeight * 100 / (float)maxWeight);

        if (showSimple) {
            if (showLabel && !showSize) {
                snprintf(formattedText, sizeof(formattedText), "%s %d/%d", messageListItem.text, currentWeight, maxWeight);
            } else {
                snprintf(formattedText, sizeof(formattedText), "%d/%d", currentWeight, maxWeight);
            }
        } else if (showDetailed) {
            snprintf(formattedText, sizeof(formattedText), "%s %d/%d (%d%%)", messageListItem.text, currentWeight, maxWeight, weightPercentage);
        }

        if (currentWeight > maxWeight) {
            color = COLOR_RED | DRAW_TEXT_FLAG_NONE;
        }
    } else if (targetPane && ProtoId(object).objectType() == OBJ_TYPE_ITEM && itemGetType(object) == ITEM_TYPE_CONTAINER) {
        int currentSize = containerGetTotalSize(object);
        int maxSize = containerGetMaxSize(object);
        int sizePercentage = maxSize < 1 ? 0 : (int)std::ceil(currentSize * 100 / (float)maxSize);

        if (sizePercentage < containerSizeThreshold) {
            fontSetCurrent(oldFont);
            return;
        }

        if (showSize) {
            snprintf(formattedText, sizeof(formattedText), "%d/%d", currentSize, maxSize);
        } else if (showSimple) {
            snprintf(formattedText, sizeof(formattedText), "%d%%", sizePercentage);
        } else if (showDetailed) {
            snprintf(formattedText, sizeof(formattedText), "%s %d (%d%%)", messageListItem.text, inventoryWeight, sizePercentage);
        }
    } else {
        if (showLabel && !showSize) {
            snprintf(formattedText, sizeof(formattedText), "%s %d", messageListItem.text, inventoryWeight);
        } else {
            snprintf(formattedText, sizeof(formattedText), "%d", inventoryWeight);
        }
    }

    if (strlen(formattedText) > 0) {
        int x = targetPane ? inventoryLootLayout.rightScrollerX : inventoryLootLayout.leftScrollerX;
        int y = (targetPane ? inventoryLootLayout.rightScrollerY : inventoryLootLayout.leftScrollerY) + inventoryLootLayout.scrollerHeight + 2;
        inventoryDrawCenteredText(windowBuffer, pitch, inventoryLootLayout.scrollerWidth, x, y, formattedText, color);
    }

    fontSetCurrent(oldFont);
}

static void inventoryScrollerRedrawInventory(const InventoryScroller& scroller, void* context)
{
    InventoryScrollerDisplayContext* displayContext = static_cast<InventoryScrollerDisplayContext*>(context);
    _display_inventory(*scroller.offsetPtr, -1, displayContext->inventoryWindowType);
}

static void inventoryScrollerRedrawTargetInventory(const InventoryScroller& scroller, void* context)
{
    InventoryScrollerDisplayContext* displayContext = static_cast<InventoryScrollerDisplayContext*>(context);
    _display_target_inventory(*scroller.offsetPtr, -1, displayContext->inventory, displayContext->inventoryWindowType);
}

static void inventoryScrollerRedrawBarterTables(const InventoryScroller& scroller, void* context)
{
    InventoryScrollerBarterContext* barterContext = static_cast<InventoryScrollerBarterContext*>(context);
    barterDisplayTables(barterContext->win, barterContext->playerTable, barterContext->bartererTable, -1);
}

static void inventoryScrollerJumpTo(const InventoryScroller& scroller, int offset)
{
    assert(scroller.offsetPtr != nullptr);
    assert(scroller.redraw != nullptr);

    int maxOffset = inventoryComputeAlignedMaxOffset(scroller.totalItems, scroller.visibleSlots, scroller.step);
    int nextOffset = std::clamp(offset, 0, maxOffset);
    if (nextOffset == *scroller.offsetPtr) {
        return;
    }

    *scroller.offsetPtr = nextOffset;
    scroller.redraw(scroller, scroller.redrawContext);
    if (scroller.refreshWindow) {
        windowRefresh(gInventoryWindow);
    }
}

static void inventoryScrollerScroll(const InventoryScroller& scroller, int direction)
{
    assert(scroller.offsetPtr != nullptr);
    assert(scroller.step > 0);
    assert(scroller.redraw != nullptr);

    inventoryScrollerJumpTo(scroller, *scroller.offsetPtr + direction * scroller.step);
}

static void inventoryScrollerPage(const InventoryScroller& scroller, int direction)
{
    assert(scroller.offsetPtr != nullptr);
    assert(scroller.visibleSlots > 0);
    assert(scroller.redraw != nullptr);

    inventoryScrollerJumpTo(scroller, *scroller.offsetPtr + direction * scroller.visibleSlots);
}

static void inventoryScrollerHandleInput(const InventoryScroller& scroller, int keyCode, int mouseEvent)
{
    if (scroller.keyUp != -1 && keyCode == scroller.keyUp) {
        inventoryScrollerScroll(scroller, -1);
    } else if (scroller.keyDown != -1 && keyCode == scroller.keyDown) {
        inventoryScrollerScroll(scroller, 1);
    } else if (scroller.keyPageUp != -1 && keyCode == scroller.keyPageUp) {
        inventoryScrollerPage(scroller, -1);
    } else if (scroller.keyPageDown != -1 && keyCode == scroller.keyPageDown) {
        inventoryScrollerPage(scroller, 1);
    } else if (scroller.keyHome != -1 && keyCode == scroller.keyHome) {
        inventoryScrollerJumpTo(scroller, 0);
    } else if (scroller.keyEnd != -1 && keyCode == scroller.keyEnd) {
        inventoryScrollerJumpTo(scroller, scroller.totalItems - scroller.visibleSlots);
    }
    if ((mouseEvent & MOUSE_EVENT_WHEEL) == 0) {
        return;
    }
    if (!mouseHitTestInWindow(gInventoryWindow, scroller.hitRect.left, scroller.hitRect.top, scroller.hitRect.right, scroller.hitRect.bottom)) {
        return;
    }

    int wheelX;
    int wheelY;
    mouseGetWheel(&wheelX, &wheelY);
    if (wheelY > 0) {
        inventoryScrollerScroll(scroller, -1);
    } else if (wheelY < 0) {
        inventoryScrollerScroll(scroller, 1);
    }
}

static bool hasPartySlots()
{
    return inventoryLootLayout.columns == 2
        && _gIsSteal
        && _target_curr_stack == 0
        && partyTargetEquipped != nullptr
        && partyBaseTarget != nullptr
        && _target_stack[0] == partyBaseTarget
        && ProtoId(partyBaseTarget).objectType() == OBJ_TYPE_CRITTER
        && objectIsPartyMember(partyBaseTarget);
}

static Object** getPartySlotItemPtr(InvenSlot slot)
{
    if (!hasPartySlots()) {
        return nullptr;
    }

    switch (slot) {
    case InvenSlot::RightHand:
        return &(partyTargetEquipped->rightHand);
    case InvenSlot::Armor:
        return &(partyTargetEquipped->armor);
    default:
        assert(false);
        return nullptr;
    }
}

static Object* getPartySlotItem(InvenSlot slot)
{
    Object** itemSlot = getPartySlotItemPtr(slot);
    return itemSlot != nullptr ? *itemSlot : nullptr;
}

static Rect getPartySlotRect(InvenSlot slot)
{
    Rect rect {};

    switch (slot) {
    case InvenSlot::RightHand:
        rect.left = inventoryLootLayout.partySlotX;
        rect.top = inventoryLootLayout.partyArmorSlotY + kPartySlotHeight;
        break;
    case InvenSlot::Armor:
        rect.left = inventoryLootLayout.partySlotX;
        rect.top = inventoryLootLayout.partyArmorSlotY;
        break;
    default:
        assert(false);
        return rect;
    }

    rect.bottom = rect.top + kPartySlotHeight - 1;
    rect.right = rect.left + kPartySlotWidth - 1;
    return rect;
}

static void renderPartySlots()
{
    if (!partySlotFrmImage.isLocked()) {
        return;
    }

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch = inventoryLootLayout.windowWidth;

    Rect armorRect = getPartySlotRect(InvenSlot::Armor);
    Rect weaponRect = getPartySlotRect(InvenSlot::RightHand);
    blitBufferToBuffer(partySlotFrmImage.getData(),
        kPartySlotWidth,
        kPartySlotImageHeight,
        kPartySlotWidth,
        windowBuffer + pitch * armorRect.top + armorRect.left,
        pitch);

    Object* armor = getPartySlotItem(InvenSlot::Armor);
    if (armor != nullptr) {
        int itemX = armorRect.left + (kPartySlotWidth - INVENTORY_LARGE_SLOT_WIDTH) / 2;
        int itemY = armorRect.top + (kPartySlotHeight - INVENTORY_LARGE_SLOT_HEIGHT) / 2 + 3;
        artRender(itemGetInventoryFrmId(armor),
            windowBuffer + pitch * itemY + itemX,
            INVENTORY_LARGE_SLOT_WIDTH,
            INVENTORY_LARGE_SLOT_HEIGHT,
            pitch);
    }

    Object* weapon = getPartySlotItem(InvenSlot::RightHand);
    if (weapon != nullptr) {
        int itemX = weaponRect.left + (kPartySlotWidth - INVENTORY_LARGE_SLOT_WIDTH) / 2;
        int itemY = weaponRect.top + (kPartySlotHeight - INVENTORY_LARGE_SLOT_HEIGHT) / 2 - 3;
        artRender(itemGetInventoryFrmId(weapon),
            windowBuffer + pitch * itemY + itemX,
            INVENTORY_LARGE_SLOT_WIDTH,
            INVENTORY_LARGE_SLOT_HEIGHT,
            pitch);
    }
}

static void createPartySlotButtons()
{
    if (!hasPartySlots()) {
        return;
    }

    bool slotImageLoaded = partySlotFrmImage.lock(OBJ_TYPE_INTERFACE, "partyslot.png");
    if (!slotImageLoaded) {
        return;
    }

    buttonCreateSlot(gInventoryWindow,
        inventoryLootLayout.partySlotX,
        inventoryLootLayout.partyArmorSlotY,
        kPartySlotWidth,
        kPartySlotHeight,
        PARTY_ARMOR_SLOT_KEY,
        inventoryItemSlotOnMouseEnter,
        inventoryItemSlotOnMouseExit);
    buttonCreateSlot(gInventoryWindow,
        inventoryLootLayout.partySlotX,
        inventoryLootLayout.partyArmorSlotY + kPartySlotHeight,
        kPartySlotWidth,
        kPartySlotHeight,
        PARTY_WEAPON_SLOT_KEY,
        inventoryItemSlotOnMouseEnter,
        inventoryItemSlotOnMouseExit);
}

static FrmId buildPartyDisplayFrmId()
{
    WeaponAnimation weaponAnimationCode = WeaponAnimation::None;
    Object* rightHandItem = partyTargetEquipped->rightHand;
    if (rightHandItem != nullptr && itemGetType(rightHandItem) == ITEM_TYPE_WEAPON) {
        weaponAnimationCode = weaponGetAnimationCode(rightHandItem);
    }

    return FrmId(partyBaseTarget, ANIM_STAND, weaponAnimationCode, ROTATION_NE);
}

static FrmId getTargetDisplayFrmId()
{
    if (!hasPartySlots()) {
        return FrmId(_target_stack[_target_curr_stack]->fid);
    }

    return buildPartyDisplayFrmId();
}

static void setLootTarget(Object* target, Object* hiddenBox)
{
    assert(target != nullptr);
    assert(hiddenBox != nullptr);

    if (partyBaseTarget != nullptr && partyBaseTarget != target) {
        if (partyTargetEquipped != nullptr) {
            critterRestoreEquipped(partyBaseTarget, *partyTargetEquipped);
        }
        itemMoveAll(hiddenBox, partyBaseTarget);
    }

    itemMoveAllHidden(target, hiddenBox);
    if (partyTargetEquipped != nullptr) {
        *partyTargetEquipped = critterStripEquipped(target);
        partyRightEquippedWeight = partyTargetEquipped->weight;
    }

    partyBaseTarget = target;
    _target_pud = &(target->data.inventory);
    _target_curr_stack = 0;
    _target_stack[0] = target;
    _target_stack_offset[0] = 0;
    gInventoryWindowDudeRotationTimestamp = 0;
}

static void refreshAfterTargetChange()
{
    _display_target_inventory(0, -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
    _display_body(getTargetDisplayFrmId(), INVENTORY_WINDOW_TYPE_LOOT);
}

static int inventoryWrapIndex(int index, int count, int direction)
{
    assert(count > 0);
    return (index + direction + count) % count;
}

static void refreshPartyEquippedAfterAction()
{
    assert(hasPartySlots());

    *partyTargetEquipped = critterStripEquipped(partyBaseTarget);
    partyRightEquippedWeight = partyTargetEquipped->weight;
    gInventoryWindowDudeRotationTimestamp = 0;
}

static int unequipLootArmorFunc(Object* critter)
{
    Object* armor = critterGetArmor(critter);
    if (armor == nullptr) {
        return -1;
    }

    if (!scriptHooks_InvenWield(critter, armor, InvenSlot::Armor, 0, 0)) {
        return -1;
    }

    armor->flags &= ~OBJECT_WORN;
    adjustCritterStatsOnArmorChange(critter, armor, nullptr);
    return 0;
}

static bool tryUnequipPartyItem(InvenSlot slot)
{
    assert(hasPartySlots());

    Object* item = getPartySlotItem(slot);
    if (item == nullptr) {
        return false;
    }

    critterRestoreEquipped(partyBaseTarget, *partyTargetEquipped);

    int rc = -1;
    if (slot == InvenSlot::RightHand) {
        rc = inventoryUnequipFunc(partyBaseTarget, HAND_RIGHT, false);
    } else if (slot == InvenSlot::Armor) {
        rc = unequipLootArmorFunc(partyBaseTarget);
    }

    if (rc == 0) {
        Object* unequippedItem = inventoryFindById(partyBaseTarget, item->id);
        if (unequippedItem != nullptr) {
            // fix stacking
            itemRemoveQuietly(partyBaseTarget, unequippedItem, 1);
            itemAdd(partyBaseTarget, unequippedItem, 1);
        }
    }

    refreshPartyEquippedAfterAction();
    return rc == 0;
}

static bool tryEquipPartyItem(Object* item, bool fromLeftPane)
{
    if (!hasPartySlots() || !partySlotFrmImage.isLocked() || item == nullptr) {
        return false;
    }

    InvenSlot partySlot;
    if (itemGetType(item) == ITEM_TYPE_WEAPON) {
        partySlot = InvenSlot::RightHand;
    } else if (itemGetType(item) == ITEM_TYPE_ARMOR) {
        partySlot = InvenSlot::Armor;
    } else {
        return false;
    }

    Rect rect = getPartySlotRect(partySlot);
    if (!mouseHitTestInWindow(gInventoryWindow, rect.left, rect.top, rect.right + 1, rect.bottom + 1)) {
        return false;
    }

    if (partySlot == InvenSlot::RightHand) {
        if (!scriptHooks_CanUseWeapon(
                critterCanUseWeapon(partyBaseTarget, item, HIT_MODE_RIGHT_WEAPON_PRIMARY),
                partyBaseTarget,
                item,
                HIT_MODE_RIGHT_WEAPON_PRIMARY)) {
            displayMonitorAddMessage("I can't use that."); // TODO: translate
            return false;
        }
    } else if (partySlot == InvenSlot::Armor && !partyMemberProtoIdCanEquipArmor(partyBaseTarget)) {
        displayMonitorAddMessage("I can't use that."); // TODO: translate
        return false;
    }

    critterRestoreEquipped(partyBaseTarget, *partyTargetEquipped);

    // move from player -> party member
    if (fromLeftPane && itemMove(_inven_dude, partyBaseTarget, item, 1) != 0) {
        refreshPartyEquippedAfterAction();
        return false;
    }

    // equip from target's inventory
    int rc = inventoryEquipFunc(partyBaseTarget, item, HAND_RIGHT, false);
    if (rc != 0 && fromLeftPane) {
        itemMove(partyBaseTarget, _inven_dude, item, 1);
    }

    refreshPartyEquippedAfterAction();
    return rc == 0;
}

static bool tryMovePartyItemToLeftPane(InvenSlot slot, Object* item)
{
    if (!hasPartySlots() || item == nullptr) {
        return false;
    }

    if (!tryUnequipPartyItem(slot)) {
        return false;
    }

    Object* movedItem = inventoryFindById(partyBaseTarget, item->id);
    if (movedItem == nullptr) {
        return false;
    }

    if (itemMove(partyBaseTarget, _inven_dude, movedItem, 1) == 0) {
        return true;
    }

    inventoryDisplayMessage(25); // You cannot pick that up. You are at your maximum weight capacity.
    return false;
}

static void handlePartySlotPickup(InvenSlot slot)
{
    Object* item = getPartySlotItem(slot);
    if (item == nullptr) {
        return;
    }

    Rect rect = getPartySlotRect(slot);
    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    assert(partySlotFrmImage.isLocked());
    int sourceY = slot == InvenSlot::RightHand ? kPartySlotHeight : 0;
    blitBufferToBuffer(partySlotFrmImage.getData() + kPartySlotWidth * sourceY,
        kPartySlotWidth,
        kPartySlotHeight,
        kPartySlotWidth,
        windowBuffer + inventoryLootLayout.windowWidth * rect.top + rect.left,
        inventoryLootLayout.windowWidth);
    windowRefreshRect(gInventoryWindow, &rect);

    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    if (!immediate && inventoryLootMouseHitTestScroller(false)) {
        tryMovePartyItemToLeftPane(slot, item);
    } else if (immediate || inventoryLootMouseHitTestScroller(true)) {
        tryUnequipPartyItem(slot);
    }

    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
    _display_body(getTargetDisplayFrmId(), INVENTORY_WINDOW_TYPE_LOOT);
    windowRefresh(gInventoryWindow);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
}

static int inventoryComputeAlignedMaxOffset(int length, int visibleSlots, int scrollStep)
{
    int maxOffset = length - visibleSlots;
    if (maxOffset <= 0) {
        return 0;
    }

    return (maxOffset + scrollStep - 1) / scrollStep * scrollStep;
}

static void inventoryDisplayMessage(int num)
{
    MessageListItem messageListItem;
    messageListItem.num = num;
    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
        displayMonitorAddMessage(messageListItem.text);
    }
}

static void inventoryStripEquippedToGlobals(Object* critter)
{
    CritterEquipped equipped = critterStripEquipped(critter);
    gInventoryLeftHandItem = equipped.leftHand;
    gInventoryRightHandItem = equipped.rightHand;
    gInventoryArmor = equipped.armor;
}

static void inventoryRestoreEquippedFromGlobals(Object* critter)
{
    CritterEquipped equipped { gInventoryLeftHandItem, gInventoryRightHandItem, gInventoryArmor };
    critterRestoreEquipped(critter, equipped);
    gInventoryLeftHandItem = nullptr;
    gInventoryRightHandItem = nullptr;
    gInventoryArmor = nullptr;
}

static void inventorySetLeftPaneCritter(Object* critter, Object* target, int inventoryWindowType)
{
    assert(critter != nullptr);
    assert(target != nullptr);

    // _inven_dude can point at a nested container while browsing bags, but
    // equipped items always need to be restored onto the owning critter.
    inventoryRestoreEquippedFromGlobals(_stack[0]);
    inventoryStripEquippedToGlobals(critter);
    _inven_dude = critter;
    _curr_stack = 0;
    _pud = &(critter->data.inventory);
    _stack[0] = critter;
    _stack_offset[0] = 0;

    WeaponAnimation animationCode = WeaponAnimation::None;
    Object* itemInHand = interfaceGetCurrentHand() == HAND_RIGHT ? gInventoryRightHandItem : gInventoryLeftHandItem;
    if (itemInHand != nullptr) {
        Proto* proto = nullptr;
        if (protoGetProto(ProtoId(itemInHand), &proto) != -1
            && proto != nullptr
            && proto->item.type == ITEM_TYPE_WEAPON) {
            animationCode = proto->item.data.weapon.animationCode;
        }
    }

    gInventoryWindowDudeFrmId = FrmId(critter, ANIM_STAND, animationCode, ROTATION_NE);
    gInventoryWindowDudeRotationTimestamp = 0;
    _display_inventory(0, -1, inventoryWindowType);
    _display_body(FrmId(target), inventoryWindowType);
}

static void inventoryDrawCenteredText(unsigned char* buffer, int pitch, int width, int x, int y, const char* text, ColorWithFlags color)
{
    if (text == nullptr) {
        return;
    }

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);
    int textWidth = std::min(fontGetStringWidth(text), width);
    int textX = x + width / 2 - textWidth / 2;
    fontDrawText(buffer + pitch * y + textX, text, width, pitch, color);
    fontSetCurrent(oldFont);
}

static void displayLootPanePartyName(unsigned char* windowBuffer, int windowPitch, const Rect& rect, int index)
{
    Object* critter = nullptr;
    if (index == 0) {
        if (_stack[0] != gDude) {
            critter = _stack[0];
        }
    } else if (index == 1) {
        if (hasPartySlots()) {
            critter = partyBaseTarget;
        }
    }

    if (critter == nullptr) {
        return;
    }

    const char* name = critterGetName(critter);
    if (name == nullptr) {
        return;
    }

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);
    int nameY = rect.bottom - fontGetLineHeight();
    fontSetCurrent(oldFont);

    inventoryDrawCenteredText(windowBuffer, windowPitch, INVENTORY_BODY_VIEW_WIDTH, rect.left, nameY, name, COLOR_GREEN | DRAW_TEXT_FLAG_NONE);
}

static std::pair<int, int> barterComputeTablesValue(Object* dude, Object* npc, bool offerButton)
{
    assert(!gGameDialogSpeakerIsPartyMember);

    const int rawValue = objectGetCost(gBartererTableObj);
    const int caps = itemGetTotalCaps(gBartererTableObj);
    const int offerValue = objectGetCost(gPlayerTableObj);

    BarterPriceContext ctx { dude, npc, gBartererTableObj, gPlayerTableObj, 0, offerValue, rawValue, caps, offerButton, false };

    const int valueMinusCaps = rawValue - caps;
    double perkBonus = 0.0;
    if (dude == gDude) {
        if (perkHasRank(gDude, PERK_MASTER_TRADER)) {
            perkBonus = perkGetMasterTraderBonus();
        }
    }

    const int partyBarter = partyGetBestSkillValue(SKILL_BARTER);
    const int npcBarter = skillGetValue(npc, SKILL_BARTER);

    // TODO: Check in debugger, complex math, probably uses floats, not doubles.
    double barterModMult = (gBarterFinalModifier + 100.0 - perkBonus) * 0.01;
    const double balancedCost = (160.0 + npcBarter) / (160.0 + partyBarter) * (valueMinusCaps * 2.0);
    if (barterModMult < 0) {
        // TODO: Probably 0.01 as float.
        barterModMult = 0.0099999998;
    }

    ctx.value = static_cast<int>(barterModMult * balancedCost + caps);
    scriptHooks_BarterPrice(&ctx);

    return { ctx.value, ctx.offerValue };
}

static std::pair<int, int> barterComputeTablesWeight(Object* dude, Object* npc)
{
    assert(gGameDialogSpeakerIsPartyMember);
    const int rawValue = objectGetCost(gBartererTableObj);
    const int caps = itemGetTotalCaps(gBartererTableObj);
    const int offerValue = objectGetCost(gPlayerTableObj);

    BarterPriceContext ctx { dude, npc, gBartererTableObj, gPlayerTableObj, 0, offerValue, rawValue, caps, false, true };

    // Hook is invoked but return values are not used. This matches sfall behavior.
    scriptHooks_BarterPrice(&ctx);

    return { objectGetInventoryWeight(gBartererTableObj), objectGetInventoryWeight(gPlayerTableObj) };
}

static int barterGetMovedQuantity(Object* item, int maxQuantity, bool fromPlayer, bool fromInventory, bool immediate)
{
    if (maxQuantity <= 1) {
        return maxQuantity;
    }

    int suggestedValue = 1;
    if (ProtoId(item) == ItemProtoTypeId::Money && !gGameDialogSpeakerIsPartyMember) {
        // Calculate change money automatically
        auto [totalCostNpc, totalCostPlayer] = barterComputeTablesValue(gDude, _target_stack[0], false);
        // Actor's balance: negative - the actor must add money to balance the tables and vice versa
        int balance = fromPlayer ? totalCostPlayer - totalCostNpc : totalCostNpc - totalCostPlayer;

        if ((balance < 0 && fromInventory) || (balance > 0 && !fromInventory)) {
            suggestedValue = std::min(std::abs(balance), maxQuantity);
            if (immediate) {
                return suggestedValue;
            }
        }
    }

    if (immediate) {
        return maxQuantity;
    }

    return inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, maxQuantity, suggestedValue);
}

static void _drag_item_loop(Object* item, bool immediate)
{
    if (immediate) {
        soundPlayFile("iputdown");
        // prevent item look from occuring after immediate move
        _im_value = -1;
        return;
    }

    FrmImage itemInventoryFrmImage;
    const FrmId itemInventoryFrmId = itemGetInventoryFrmId(item);
    if (itemInventoryFrmImage.lock(itemInventoryFrmId)) {
        int width = itemInventoryFrmImage.getWidth();
        int height = itemInventoryFrmImage.getHeight();
        unsigned char* data = itemInventoryFrmImage.getData();
        mouseSetFrame(data, width, height, width, width / 2, height / 2, 0);
        soundPlayFile("ipickup1");
    }

    do {
        sharedFpsLimiter.mark();

        inputGetInput();

        renderPresent();
        sharedFpsLimiter.throttle();
    } while ((mouseGetEvent() & MOUSE_EVENT_LEFT_BUTTON_REPEAT) != 0);

    if (itemInventoryFrmImage.isLocked()) {
        itemInventoryFrmImage.unlock();
        soundPlayFile("iputdown");
    }
}

static bool _ctrl_pressed()
{
#if defined(F2_SERVER)
    return false;
#else
    const Uint8* keyboardState = SDL_GetKeyboardState(nullptr);
    return keyboardState[SDL_SCANCODE_LCTRL] || keyboardState[SDL_SCANCODE_RCTRL];
#endif
}

static void inventoryUnloadWeaponToOwner(Object* owner, Object* weapon)
{
    Object* ammo;
    while ((ammo = weaponUnload(weapon)) != nullptr) {
        itemAdd(owner, ammo, 1);
    }
}

int inventoryGetWindow()
{
    return windowGetWindow(gInventoryWindow) != nullptr ? gInventoryWindow : -1;
}

void inventoryDisplayStats()
{
    if (windowGetWindow(gInventoryWindow) == nullptr) {
        return;
    }

    inventoryRenderSummary();
    windowRefresh(gInventoryWindow);
}

void inventoryRedraw(int redrawSide)
{
    if (windowGetWindow(gInventoryWindow) == nullptr) {
        return;
    }

    InventoryWindowType inventoryWindowType;
    if (!inventoryGetCurrentWindowType(&inventoryWindowType)) {
        return;
    }

    if (redrawSide <= 0) {
        _stack_offset[_curr_stack] = 0;
        _display_inventory(0, -1, inventoryWindowType);
    }

    if (redrawSide != 0
        && (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT
            || inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE)) {
        _target_stack_offset[_target_curr_stack] = 0;
        _display_target_inventory(0, -1, _target_pud, inventoryWindowType);
        windowRefresh(gInventoryWindow);
    }
}

int inventoryUnwieldSlot(Object* critter, InvenSlot slot)
{
    if (critter == nullptr) {
        return -1;
    }

    bool isDude = critter == gDude;
    InventoryWindowType inventoryWindowType;
    bool uiModeActive = inventoryGetCurrentWindowType(&inventoryWindowType);
    bool update = false;

    if (slot != InvenSlot::Armor && !uiModeActive) {
        int rc = inventoryUnequipFunc(critter, slot == InvenSlot::LeftHand ? HAND_LEFT : HAND_RIGHT, false);
        if (rc == 0) {
            update = isDude;
        }

        if (update) {
            interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
        }
        return rc;
    }

    bool forceAdd = false;
    Object* item = nullptr;
    Object** itemSlot = nullptr;
    if (slot != InvenSlot::Armor) {
        if (!isDude || critter != _inven_dude) {
            return 0;
        }

        if (slot == InvenSlot::RightHand) {
            item = gInventoryRightHandItem;
            itemSlot = &gInventoryRightHandItem;
        } else {
            item = gInventoryLeftHandItem;
            itemSlot = &gInventoryLeftHandItem;
        }

        if (item != nullptr) {
            if (!scriptHooks_InvenWield(critter, item, slot, 0, 0, false)) {
                return -1;
            }

            int oldFlags = item->flags;
            bool clearBothHandSlots = gInventoryLeftHandItem == item && gInventoryRightHandItem == item;
            if (clearBothHandSlots) {
                gInventoryLeftHandItem = nullptr;
                gInventoryRightHandItem = nullptr;
            } else {
                *itemSlot = nullptr;
            }
            item->flags &= ~OBJECT_IN_ANY_HAND;

            if (itemAdd(critter, item, 1) != 0) {
                // item will be in an invalid state, but this can only happen on malloc failure
                return -1;
            }

            // CE: This branch bypasses inventoryUnequipFunc(), so restore light here too.
            if (ProtoId(item) == ItemProtoTypeId::LitFlare) {
                critterRestoreLightWithoutFlare(critter);
            }

            update = true;
        }
    } else {
        if (isDude && critter == _inven_dude) {
            item = gInventoryArmor;
        }

        if (item == nullptr) {
            item = critterGetArmor(critter);
        } else {
            gInventoryArmor = nullptr;
            forceAdd = true;
        }

        if (item != nullptr) {
            item->flags &= ~OBJECT_WORN;
            if (correctFidForRemovedItem(critter, item, OBJECT_WORN) != 0) {
                if (forceAdd) {
                    gInventoryArmor = item;
                    item->flags |= OBJECT_WORN;
                }
                return -1;
            }

            if (forceAdd && itemAdd(critter, item, 1) != 0) {
                // improper cleanup, but this can only happen on malloc failure
                return -1;
            }

            if (isDude) {
                interfaceRenderArmorClass(false);
            }
        }
    }

    if (!uiModeActive) {
        return 0;
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        inventoryRenderSummary();
    }

    FrmId targetFrmId = FrmId::Empty();
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT
        || inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        targetFrmId = FrmId(_target_stack[_target_curr_stack]->fid);
        _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, inventoryWindowType);
    }

    _adjust_fid();
    _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);
    _display_body(targetFrmId, inventoryWindowType);
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        barterDisplayTables(gInventoryBarterBackgroundWindow, gPlayerTableObj, gBartererTableObj, -1);
    }

    if (update) {
        interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
    }

    return 0;
}

} // namespace fallout
