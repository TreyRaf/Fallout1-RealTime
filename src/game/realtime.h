#ifndef FALLOUT_GAME_REALTIME_H_
#define FALLOUT_GAME_REALTIME_H_

#include "game/combat_defs.h"
#include "game/object_types.h"
#include "plib/gnw/rect.h"

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
    // Attacks dropped because a participant left the map before they landed.
    int attacksDropped;
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
    int playerThrow;
    int playerOther;
    int companionAttacks;
    int maxConcurrentAttacks;
    int maxConcurrentAttackers;
    int damageMismatches;
    int lastPlayerAmmoUsed;
    int lastPlayerRounds;
    int targetingRequests;
    int chemUses;
    int lockSwitches;
    int lockSwitchMisses;
    int playerSlotGrants;
    int playerSlotMaxQueue;
    int playerSlotMaxWaitMs;
    int playerSlotOverlaps;
    int reloadsStarted;
    int reloadsCompleted;
    int reloadsInterrupted;
    int reloadFallbacks;
    int reloadAmmoTransfers;
    int cameraScrolls;
    int cameraRecenters;
    int cameraLimitStops;
    int cameraPans;
} RtStats;

// The lock-on indicator (crosshair and hit chance) as currently drawn.
typedef struct RtIndicator {
    bool visible;
    // Screen position of the crosshair center.
    int x;
    int y;
    // Vanilla hit chance, or -1 when the attack is not possible right now.
    int toHit;
    // `COMBAT_BAD_SHOT_*` for the current weapon and attack mode.
    int badShot;
    char text[32];
} RtIndicator;

bool rt_enabled();
void rt_reset();
void rt_tick();
void rt_combat_request(STRUCT_664980* attack);
// Called when an object is removed from the game (see `obj_erase_object`),
// so no reference to it is kept.
void rt_object_erased(Object* obj);
void rt_log(const char* format, ...);

// Used by the automated test and diagnostics.
const RtStats* rt_get_stats();
Object* rt_get_lock();
const RtIndicator* rt_get_indicator();
// Draws the lock-on indicator on top of the map (called by the renderer).
void rt_render_overlay(unsigned char* buffer, int pitch, Rect* rect);
bool rt_is_reloading();
Object* rt_get_player_slot_holder();
int rt_get_player_slot_queue_length();
int rt_get_player_slot_queue(Object** critters, int capacity);
// What A would interact with right now.
Object* rt_get_interact_target();
int rt_get_camera_margin_pct();
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
