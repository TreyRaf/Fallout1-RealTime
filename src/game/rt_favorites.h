#ifndef FALLOUT_GAME_RT_FAVORITES_H_
#define FALLOUT_GAME_RT_FAVORITES_H_

#include <stddef.h>

#include "game/object_types.h"
#include "plib/db/db.h"

namespace fallout {

// RT: Favorites.
//
// Four slots, one per D-pad direction (Up, Right, Down, Left). A slot stores
// an item type (prototype), never an object, so it can't point at something
// that is gone and nothing is duplicated: activating it uses an item of that
// type the player carries. A slot whose item isn't carried does nothing but
// say so, and works again once one is picked up.
// - Pip-Boy inventory: Y on an item opens the assignment radial: the stick
//   or D-pad picks a direction, A assigns (replacing what was there), X
//   clears the slot, B goes back.
// - Gameplay: a D-pad press activates that direction's slot right away (see
//   `realtime.cc` for when left/right select interactions instead). Weapons
//   are held in the active hand, armor is worn, chems are used through the
//   same action as RT with a chem, other items through their use action.
//   Nothing is dropped, thrown or destroyed by being selected.
// Slots are saved at the end of the save file (older saves load with no
// favorites; saves with the earlier eight-slot layout are migrated, see
// `rt_favorites_migrate`).

#define RT_FAVORITES_COUNT 4

#define RT_FAVORITES_UP 0
#define RT_FAVORITES_RIGHT 1
#define RT_FAVORITES_DOWN 2
#define RT_FAVORITES_LEFT 3

// The earlier layout: eight slots clockwise from up.
#define RT_FAVORITES_OLD_COUNT 8

typedef struct RtFavoritesState {
    bool open;
    int selected;
    int slots[RT_FAVORITES_COUNT];
    char lastMessage[160];
    int opens;
    int activations;
    int assigns;
    int clears;
    int refusals;
    // Last migration of an eight-slot save.
    int migratedMoved;
    int migratedDropped;
} RtFavoritesState;

void rt_favorites_reset();
int rt_favorites_save(DB_FILE* stream);
int rt_favorites_load(DB_FILE* stream);

// Eight slots (clockwise from up) to four directions: the four cardinal
// slots keep their direction; a diagonal one moves to a free neighboring
// direction (the earlier one clockwise first) unless its item already has a
// direction; one that finds both neighbors taken is dropped. Nothing is
// overwritten. `report` describes every move and drop. Returns the number
// of dropped slots.
int rt_favorites_migrate(const int oldSlots[RT_FAVORITES_OLD_COUNT], int slots[RT_FAVORITES_COUNT], int* moved, char* report, size_t size);

// Pip-Boy (Y on an inventory item).
void rt_favorites_open_assign(Object* item);

#define RT_FAVORITES_DONE 0
#define RT_FAVORITES_WAIT 1
#define RT_FAVORITES_FAILED 2
#define RT_FAVORITES_EMPTY 3

// Activates a slot (a D-pad press in gameplay). RT_FAVORITES_WAIT: an
// equipment change has to wait for the player's current attack, reload or
// chem animation (try again). RT_FAVORITES_EMPTY: nothing assigned.
int rt_favorites_activate(int slot);

// Gives up a waiting activation with this message.
void rt_favorites_refuse(const char* message);

// Whether `item` can be a favorite; `reason` explains why not.
bool rt_favorites_is_eligible(Object* item, const char** reason);

// Slot holding this item type, or -1.
int rt_favorites_slot_of(int pid);

// An item of the slot's type the player carries, or NULL.
Object* rt_favorites_find_item(int slot);

// How many of the slot's item the player carries.
int rt_favorites_quantity(int slot);

// "Up", "Right", "Down", "Left".
const char* rt_favorites_slot_name(int slot);

const RtFavoritesState* rt_favorites_get_state();

// Automated tests.
void rt_favorites_debug_set(int slot, int pid);

} // namespace fallout

#endif /* FALLOUT_GAME_RT_FAVORITES_H_ */
