#ifndef FALLOUT_GAME_REALTIME_H_
#define FALLOUT_GAME_REALTIME_H_

#include "game/combat_defs.h"
#include "game/object_types.h"

namespace fallout {

// RT: Real-time combat layer.
//
// Replaces Fallout's turn-based combat with continuous simulation driven by
// the main game loop:
// - `combat()` requests are converted into hostility ("engagement") instead
//   of starting turn-based combat.
// - Attacks are resolved with the vanilla `compute_attack`/`action_attack`/
//   `apply_damage` pipeline (same hit chance, damage and animations), gated
//   by per-attacker cooldowns instead of action points.
// - Action points are only used for running and regenerate over time.
// - The player is controlled with a game controller (see `gamepad.h`).

typedef struct RtStats {
    int attacksStarted;
    int attacksResolved;
    int hits;
    int misses;
    int kills;
    int playerAttacks;
    int playerHits;
    int damageToPlayer;
    int combatRequests;
    int engagements;
    int locks;
    int moveRequests;
    int playerRejected;
    int lastRejectReason;
    int playerUnarmed;
    int playerMelee;
    int playerSingle;
    int playerBurst;
    int playerOther;
    int companionAttacks;
    int maxConcurrentAttacks;
    int maxConcurrentAttackers;
    int damageMismatches;
    int lastPlayerAmmoUsed;
    int lastPlayerRounds;
    int targetingRequests;
} RtStats;

bool rt_enabled();
void rt_reset();
void rt_tick();
void rt_combat_request(STRUCT_664980* attack);
void rt_log(const char* format, ...);

// Used by the automated test and diagnostics.
const RtStats* rt_get_stats();
Object* rt_get_lock();
Object* rt_get_lock_marker();
bool rt_is_hostile(Object* critter);
void rt_debug_set_lock(Object* target);
float rt_get_dude_ap();
bool rt_is_run_toggled();
bool rt_is_engaged(Object* critter);
const char* rt_get_log_dir();
unsigned int rt_get_time();
int rt_get_pending_count();

} // namespace fallout

#endif /* FALLOUT_GAME_REALTIME_H_ */
