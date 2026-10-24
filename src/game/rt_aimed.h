#ifndef FALLOUT_GAME_RT_AIMED_H_
#define FALLOUT_GAME_RT_AIMED_H_

#include "game/object_types.h"

namespace fallout {

// RT: AIMED SHOT (LB).
//
// LB opens the original called shot window (its artwork, the target's body
// part picture, the body part names from combat.msg for that creature, and
// the to-hit digits) on the locked target, or on the nearest enemy when
// nothing is locked. The game is paused while it is open:
// - D-pad picks a body part; right stick flicks switch targets (same rules as
//   the lock-on).
// - A queues an aimed attack on that body part, X removes the last one, RT
//   carries out the queue, B or LB cancels it.
// Queued attacks reserve action points (the weapon's attack cost plus the
// original aimed shot surcharge, `item_w_mp_cost(..., true)`) and ammunition;
// nothing is spent until an attack is committed. During execution the game
// runs again; each attack is checked again just before it starts (target
// alive and valid, range, line of fire, ammunition, same weapon) and is
// skipped otherwise, never redirected. B cancels the attacks not started yet.
// AP doesn't regenerate during execution.

#define RT_AIMED_QUEUE_MAX 16
#define RT_AIMED_PARTS 8

typedef struct RtAimedEntry {
    Object* target;
    int hitLocation;
    int hitMode;
    // Prototype of the weapon used (-1 unarmed).
    int weaponPid;
    int apCost;
    // Rounds (or thrown items) reserved.
    int rounds;
    // Hit chance shown when it was queued.
    int toHit;
} RtAimedEntry;

typedef struct RtAimedState {
    bool selecting;
    bool executing;
    Object* target;
    // Selected body part: index into the window's layout (0-3 left column,
    // 4-7 right column).
    int selected;
    int hitMode;
    int apCost;
    // Usable AP left after the reservations.
    int availableAp;
    int reservedAp;
    int reservedRounds;
    // `COMBAT_BAD_SHOT_*` for the current target.
    int badShot;
    // Per layout index: the hit location, whether this creature has that
    // body part, and the hit chance shown (-1 when the attack isn't possible).
    int locations[RT_AIMED_PARTS];
    bool available[RT_AIMED_PARTS];
    int toHit[RT_AIMED_PARTS];
    int queueLength;
    RtAimedEntry queue[RT_AIMED_QUEUE_MAX];
    // Execution progress.
    int next;
    char message[128];
    // Totals (diagnostics, tests).
    int opens;
    int refusals;
    int queued;
    int executions;
    int committed;
    int skipped;
    int cancelled;
    int targetSwitches;
} RtAimedState;

// LB in gameplay.
void rt_aimed_open();

// Runs the queue (called every gameplay tick). `canAct` is false while the
// player is knocked down or the interface is disabled; `pressB` cancels.
void rt_aimed_update(bool canAct, bool pressB);

bool rt_aimed_is_executing();

// The next queued attack is waiting for its moment: enemies leave the
// player the next window, as they do for a buffered RT press.
bool rt_aimed_wants_window();

// Drops the queue and every reservation (map change, death, load).
void rt_aimed_reset(const char* reason);
void rt_aimed_object_erased(Object* obj);

const RtAimedState* rt_aimed_get_state();

// Body part name for this creature, or NULL if it has none at `hitLocation`.
const char* rt_aimed_part_name(Object* critter, int hitLocation);

} // namespace fallout

#endif /* FALLOUT_GAME_RT_AIMED_H_ */
