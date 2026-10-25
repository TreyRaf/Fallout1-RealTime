#ifndef FALLOUT_GAME_INVENTRY_H_
#define FALLOUT_GAME_INVENTRY_H_

#include "game/art.h"
#include "game/object_types.h"

namespace fallout {

// TODO: Convert to enum.
#define OFF_59E7BC_COUNT 12

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

extern CacheEntry* ikey[OFF_59E7BC_COUNT];

void inven_set_dude(Object* obj, int pid);
void inven_reset_dude();
void handle_inventory();
bool setup_inventory(int inventoryWindowType);
void exit_inventory(bool a1);
void display_inventory(int a1, int a2, int inventoryWindowType);
void display_target_inventory(int a1, int a2, Inventory* a3, int a4);
void display_body(int fid, int inventoryWindowType);
int inven_init();
void inven_exit();
void inven_set_mouse(int cursor);
void inven_hover_on(int btn, int keyCode);
void inven_hover_off(int btn, int keyCode);
void inven_pickup(int keyCode, int a2);
void switch_hand(Object* a1, Object** a2, Object** a3, int a4);
void adjust_ac(Object* critter, Object* oldArmor, Object* newArmor);
void adjust_fid();
void use_inventory_on(Object* a1);
Object* inven_right_hand(Object* obj);
Object* inven_left_hand(Object* obj);
Object* inven_worn(Object* obj);
int inven_pid_is_carried(Object* obj, int pid);
Object* inven_pid_is_carried_ptr(Object* obj, int pid);
int inven_pid_quantity_carried(Object* obj, int pid);
void display_stats();
Object* inven_find_type(Object* obj, int a2, int* inout_a3);
Object* inven_find_id(Object* obj, int a2);
Object* inven_index_ptr(Object* obj, int a2);
int inven_wield(Object* critter, Object* item, int a3);
int inven_unwield(Object* critter, int a2);
int inven_from_button(int input, Object** a2, Object*** a3, Object** a4);
void inven_display_msg(char* string);
void inven_obj_examine_func(Object* critter, Object* item);
void inven_action_cursor(int eventCode, int inventoryWindowType);
int loot_container(Object* a1, Object* a2);
int inven_steal_container(Object* a1, Object* a2);
int move_inventory(Object* a1, int a2, Object* a3, bool a4);
void barter_inventory(int win, Object* a2, Object* a3, Object* a4, int a5);
void container_enter(int a1, int a2);
void container_exit(int keyCode, int inventoryWindowType);
int drop_into_container(Object* a1, Object* a2, int a3, Object** a4, int quantity);
int drop_ammo_into_weapon(Object* weapon, Object* ammo, Object** a3, int quantity, int keyCode);
void draw_amount(int value, int inventoryWindowType);
int inven_set_timer(Object* a1);

// RT: player inventory operations for the unified Pip-Boy.
#define INVEN_RT_SLOT_LEFT_HAND 0
#define INVEN_RT_SLOT_RIGHT_HAND 1
#define INVEN_RT_SLOT_ARMOR 2
int inven_rt_quantity(Object* item);
void inven_rt_refresh_appearance();
int inven_rt_equip(Object* item, int slot);
int inven_rt_unequip(Object* item);
int inven_rt_use(Object* item);
int inven_rt_drop(Object* item, int quantity);
int inven_rt_unload(Object* weapon);

// RT: controller layer of the loot screen (corpses and containers).
// The loot side is focused when it opens; LT focuses the player's items, RT
// the loot; D-pad/left stick up and down move through the focused list; A
// moves the selected item to the other side (a stack asks how many: D-pad or
// stick changes it by 1, LB/RB by 10, LT/RT by 100 (held, they repeat), Y
// picks the whole stack, A confirms, B cancels); X takes everything it
// can from the loot side; B closes. Transfers go through the original rules
// (`move_inventory`'s steal check, `item_move`'s weight and space limits).
#define INVEN_RT_LOOT_SIDE_PLAYER 0
#define INVEN_RT_LOOT_SIDE_LOOT 1

typedef struct InvenRtLootState {
    bool open;
    int side;
    // Absolute index of the selected item and first shown row, per side.
    int selected[2];
    int offset[2];
    int count[2];
    // The focused side's selected item (NULL when that side is empty).
    Object* selectedItem;
    int selectedQuantity;
    // The quantity selector (`do_move_timer`) while it is open.
    bool quantityOpen;
    int quantityValue;
    int quantityMax;
    int transfers;
    int takeAlls;
    // Stacks left behind by the last Take All.
    int takeAllLeft;
    char lastMessage[128];
    // Stealing: the shown chance the selected item goes unnoticed (-1 none).
    int stealOdds;
    bool stealing;
} InvenRtLootState;

const InvenRtLootState* inven_rt_loot_state();

// RT: controller layer of the barter screen. Four columns: the player's
// items, the player's offer, the trader's offer, the trader's items. It
// opens on the trader's items; LT jumps to the player's items, RT to the
// trader's, LB/RB to the player's/trader's offer, D-pad left/right step
// between columns, up/down move. A puts the selected item on the offer (or
// takes it back off an offer); stacks ask how many. X offers the trade (the
// original acceptance rules decide), Y reviews the exchange in the reply
// window, Back returns to talking, B leaves (offers go back to their owners,
// as the original does).
#define INVEN_RT_BARTER_PLAYER_ITEMS 0
#define INVEN_RT_BARTER_PLAYER_OFFER 1
#define INVEN_RT_BARTER_TRADER_OFFER 2
#define INVEN_RT_BARTER_TRADER_ITEMS 3

typedef struct InvenRtBarterState {
    bool open;
    int column;
    int selected[4];
    int offset[4];
    int count[4];
    Object* selectedItem;
    int selectedQuantity;
    // The player's offer, what the trader asks for its offer (Barter skill,
    // reaction, perks applied), and whether the trader would accept now.
    int offerValue;
    int askValue;
    bool accepts;
    int trades;
    int rejections;
    int reviews;
    char lastMessage[160];
} InvenRtBarterState;

const InvenRtBarterState* inven_rt_barter_state();

// Automated tests: the barter screen with `trader`, outside dialogue (offer
// tables made and returned as the dialogue does).
void inven_rt_debug_barter(Object* trader);

} // namespace fallout

#endif /* FALLOUT_GAME_INVENTRY_H_ */
