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
// - Action points are used for running and for AIMED SHOT (see
//   `rt_aimed.h`) and regenerate over time; ordinary attacks cost none.
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
    // AIMED SHOT: player attacks on a chosen body part.
    int aimedAttacks;
    int aimedHits;
    int aimedCriticals;
    // The body part aimed at (a ranged miss is recorded as a torso miss by
    // the original calculation).
    int lastAimedLocation;
    int lastAimedToHit;
    int lastAimedFlags;
    Object* lastAimedDefender;
    // Interaction candidates cycled with the D-pad.
    int interactCycles;
    int favoritesOpened;
    // X: taps that asked for a reload; holsters started, completed and
    // animated; draw animations played when a weapon is readied again.
    int reloadTaps;
    int holstersStarted;
    int holstersCompleted;
    int holsterAnimations;
    int drawsPlayed;
    // Sprinting time (ms) in and out of combat, and the AP it drained.
    float sprintCombatMs;
    float sprintCombatAp;
    float sprintCalmMs;
    float sprintCalmAp;
    // AP regenerated while sprinting outside combat.
    float sprintCalmRegen;
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
// Test fixture: sets the player's current AP.
void rt_debug_set_ap(float ap);
float rt_get_dude_ap();
bool rt_is_run_toggled();
bool rt_is_engaged(Object* critter);

// Whether a hostile critter is engaged with the player team nearby (the
// real-time equivalent of being in combat).
bool rt_player_in_combat();
const char* rt_get_log_dir();
unsigned int rt_get_time();
int rt_get_pending_count();

// AIMED SHOT support (see `rt_aimed.h`).
// The locked target when it is valid, otherwise the nearest enemy (never a
// neutral NPC unless it is the deliberately locked target), or NULL.
Object* rt_aim_default_target();
// The target in the flicked screen direction (lock-on switching rules).
Object* rt_aim_switch_target(Object* from, float fx, float fy);
bool rt_aim_target_valid(Object* target);
// `COMBAT_BAD_SHOT_*` for the player attacking `target` (no AP check).
int rt_aim_check_shot(Object* target, int hitMode);
bool rt_aim_player_in_attack();

#define RT_AIM_ATTACK_STARTED 0
#define RT_AIM_ATTACK_WAIT 1
#define RT_AIM_ATTACK_FAILED 2

// Commits an aimed attack: the original attack on `hitLocation`, spending
// `apCost` AP once it has started.
int rt_aim_attack(Object* target, int hitMode, int hitLocation, int apCost);

// Favorites support (see `rt_favorites.h`).
// No attack, reload, chem use or aimed queue is in progress.
bool rt_player_can_change_equipment();
// Uses a chem on the player, as RT with a chem does (waits for a moment
// when nothing else involves the player).
void rt_use_item_on_self(Object* item);
// Plays the weapon's draw (take out) animation after it was readied.
void rt_play_draw(Object* weapon);
// A holster (X held) is in progress.
bool rt_is_holstering();

// Interaction candidates (A): what is highlighted and how many there are.
int rt_get_interact_candidate_count();
int rt_get_interact_candidates(Object** out, int capacity);
// The interaction label shown above the highlighted object ("" when none).
const char* rt_get_interact_label();

} // namespace fallout

#endif /* FALLOUT_GAME_REALTIME_H_ */
