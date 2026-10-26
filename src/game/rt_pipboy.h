#ifndef FALLOUT_GAME_RT_PIPBOY_H_
#define FALLOUT_GAME_RT_PIPBOY_H_

#include "game/object_types.h"

namespace fallout {

// RT: The unified Pip-Boy.
//
// One Pip-Boy 2000 screen (the original artwork, clock and note) replaces the
// player's inventory, character screen, automap and the old Pip-Boy menus:
// - STATS: status (HP, AP, level, XP, derived stats, resistances,
//   conditions), S.P.E.C.I.A.L., skills (spending skill points) and perks
//   and traits (choosing perks when one is due).
// - INVENTORY: Weapons, Apparel, Aid, Ammo, Misc and All, with equipping,
//   using, unloading, inspecting and dropping.
// - MAP: the current map (live automap), automaps of maps visited before,
//   and a read-only world map (travel still happens on the world map).
// - DATA: quests, holodisks, video archives and the alarm clock (rest).
//
// It runs as a modal screen, so the game is paused while it is open.
// Controller: LT/RT main tab, LB/RB page, D-pad/left stick move, A select,
// B back (closes from a main page), X/Y context actions, Start closes.
// Keyboard: Q/E tabs, Page Up/Down pages, arrows, Enter, Escape, X/Y.

typedef enum RtPipboyTab {
    RT_PIPBOY_TAB_LAST = -1,
    RT_PIPBOY_TAB_STATS = 0,
    RT_PIPBOY_TAB_INVENTORY,
    RT_PIPBOY_TAB_MAP,
    RT_PIPBOY_TAB_DATA,
    RT_PIPBOY_TAB_COUNT,
} RtPipboyTab;

// Pages that a request can open directly.
typedef enum RtPipboyPage {
    RT_PIPBOY_PAGE_DEFAULT = -1,
    RT_PIPBOY_PAGE_MAP_LOCAL = 0,
    RT_PIPBOY_PAGE_DATA_REST = 3,
} RtPipboyPage;

// Sub-screens of a page.
typedef enum RtPipboyMode {
    RT_PIPBOY_MODE_NONE,
    RT_PIPBOY_MODE_ACTIONS,
    RT_PIPBOY_MODE_QUANTITY,
    RT_PIPBOY_MODE_TEXT,
    RT_PIPBOY_MODE_CHOOSE_PERK,
    RT_PIPBOY_MODE_CHOOSE_TAG,
    RT_PIPBOY_MODE_MUTATE_LOSE,
    RT_PIPBOY_MODE_MUTATE_GAIN,
    RT_PIPBOY_MODE_CONFIRM_PERK,
    RT_PIPBOY_MODE_SAVED_MAP,
} RtPipboyMode;

// Inventory item actions.
typedef enum RtPipboyAction {
    RT_PIPBOY_ACTION_USE,
    RT_PIPBOY_ACTION_EQUIP_LEFT,
    RT_PIPBOY_ACTION_EQUIP_RIGHT,
    RT_PIPBOY_ACTION_WEAR,
    RT_PIPBOY_ACTION_UNEQUIP,
    RT_PIPBOY_ACTION_UNLOAD,
    RT_PIPBOY_ACTION_INSPECT,
    RT_PIPBOY_ACTION_DROP,
} RtPipboyAction;

// Opens the Pip-Boy on `tab` (RT_PIPBOY_TAB_LAST: the tab used last time).
void rt_pipboy_open(int tab, int page = RT_PIPBOY_PAGE_DEFAULT);

// The motion sensor: shows critters on the current map's automap (in the
// Pip-Boy, opening it on the Map tab if needed).
void rt_pipboy_show_scanner();

bool rt_pipboy_is_open();

// The key the gameplay controller binding (B) sends to open the Pip-Boy on
// its last tab.
#define RT_PIPBOY_KEY_OPEN 0x7210

// Automated tests and diagnostics.
typedef struct RtPipboyState {
    bool open;
    int tab;
    int page;
    // A sub-screen (action menu, quantity, inspect, chooser...) is open.
    int mode;
    int cursor;
    int listCount;
    int opens;
    int closes;
    char lastMessage[128];
    // Inventory: the item under the cursor.
    Object* cursorItem;
    // Action menu or chooser entries (PB_MODE_* in rt_pipboy.cc).
    int choiceCount;
    int choices[64];
    int choiceCursor;
    int quantity;
    // Map: pixels drawn and whether the player marker is known.
    int mapPixels;
    bool mapPlayer;
    // Map pan positions (local/saved automap, world map).
    int mapPanX;
    int mapPanY;
    int worldX;
    int worldY;
    // Left stick subcategory pushes a nested control ignored.
    int nestedIgnored;
    // Perk card: the perk shown, its artwork (fid, -1 when missing or no
    // card) and where it was drawn; text drawn over the artwork or the
    // footer is counted in `perkTextOverlaps`.
    int perkCard;
    int perkArtFid;
    int perkArtX;
    int perkArtY;
    int perkArtWidth;
    int perkArtHeight;
    int perkTextOverlaps;
    int perkTextLines;
} RtPipboyState;

const RtPipboyState* rt_pipboy_get_state();

} // namespace fallout

#endif /* FALLOUT_GAME_RT_PIPBOY_H_ */
