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
    // Skilldex Sneak toggles.
    int sneakToggles;
    // Melee/unarmed swings without a target in reach: started, those that
    // reached someone at the impact (an ordinary attack attempt), those that
    // met nobody, the last swing animation, hit chance and defender.
    int freeSwings;
    int freeSwingAttempts;
    int freeSwingWhiffs;
    int lastSwingAnim;
    int lastSwingToHit;
    Object* lastSwingDefender;
    // R3 wider view toggles.
    int zoomToggles;
    // Bloody Mess: gory deaths by a critical hit of the player's that were
    // noted, worn armor destroyed by them, and the last destroyed armor.
    int goryDeathsNoted;
    int armorDestroyed;
    int lastArmorDestroyedPid;
    Object* lastArmorDestroyedOwner;
    // Fast Shot: animation frames played faster.
    int fastShotFrames;
    // When the player's last attack or swing started, and the time since the
    // one before (game time, ms).
    unsigned int lastPlayerAttackAt;
    unsigned int lastPlayerAttackInterval;
    // Melee/unarmed AP: AP spent on close-combat attacks, attacks refused for
    // lack of AP (throws included), and the last cost charged.
    float meleeApSpent;
    int meleeApCharges;
    int meleeApRefused;
    float lastMeleeApCost;
    // Throws: AP spent on them, throws made with RB (the right hand's
    // thrown weapon), RB presses refused (no target, no AP...), and the
    // held item and hit mode of the last RB throw.
    float throwApSpent;
    int throwApCharges;
    int rbThrows;
    int rbThrowRefused;
    int lastRbThrowPid;
    Object* lastRbThrowActiveItem;
    // Thrown weapons taken out of the left hand or the active hand.
    int thrownHandFixes;
    // Counterattacks after a missed attack of the player's: requested,
    // refused (ineligible enemy, already requested, the attack already has
    // one), started and expired; the last counterattacker and how long after
    // its request it attacked. Ranged ones: after a missed shot or throw.
    int counterRequests;
    int counterRangedRequests;
    int counterRangedAttacks;
    int lastCounterHitMode;
    bool lastCounterRanged;
    int counterIneligible;
    int counterDuplicates;
    int counterSwingRepeats;
    int counterAttacks;
    int counterExpired;
    Object* lastCounterCritter;
    unsigned int lastCounterDelayMs;
    // Enemy attacks on the player granted when the last counterattack was
    // asked for and when it started (its own grant included).
    int lastCounterRequestGrants;
    int lastCounterStartGrants;
    // Two-handed melee sweeps: swings, targets attempted in all of them, the
    // most targets one swing reached, and the targets of the latest one.
    int sweeps;
    int sweepTargets;
    int sweepMaxTargets;
    int lastSweepCount;
    Object* lastSweepTargets[8];
    // A target attacked twice by the same swing (must stay 0).
    int sweepRepeats;
    // NPC hit reactions played faster (`hit_reaction_speed`); targets let
    // move again when their reaction was over before the attacker finished
    // (the last one, when, and how long after its attack started); the
    // last NPC that started a walk toward its target, and when.
    int fastReactionAnims;
    int reactionReleases;
    Object* lastReleaseCritter;
    unsigned int lastReleaseAt;
    unsigned int lastReleaseDelayMs;
    Object* lastAiMoveCritter;
    unsigned int lastAiMoveAt;
    // NPC attack eligibility: attacks not started because the hit chance
    // was 0% or less, or the target was out of sight; NPC attacks started
    // on the player with no positive hit chance (must stay 0); the lowest
    // hit chance an NPC attack on the player was started with.
    int aiZeroChanceSkips;
    int aiSightBlockedSkips;
    int aiHopelessAttacks;
    int aiLowestToHit;
    // Enemy attacks on the player: ordinary outcomes the player isn't held
    // by (no part in the attacker's animation) and heavy ones (knockdown,
    // death...) that hold the player as before; the player's own hit
    // reactions played (at the impact) and skipped (busy with an attack,
    // reload...); enemy attacks started while the player's own attack was
    // in progress.
    int playerFreeAttacks;
    int playerFreeHits;
    int playerHeldAttacks;
    int playerReactions;
    int playerReactionsSkipped;
    int enemyAttacksDuringPlayerAttack;
    // Timing of repeated manual attacks: presses made during an attack or
    // its recovery, of those the extra ones (spam), attacks committed with
    // a penalty, and the last committed attack's penalty, extra presses,
    // how early its press was (ms before the recovery ended) and hit chance;
    // timing states cleared (cancellation, weapon change, menu...).
    int timingBusyPresses;
    int timingSpamPresses;
    int timingPenalized;
    int lastTimingPenalty;
    int lastTimingSpam;
    int lastTimingLeadMs;
    int lastTimingToHit;
    int timingClears;
    // Reloads in the armed idle pose (Hunting Rifle) and how long the last
    // reload of any kind took.
    int idleReloads;
    unsigned int lastReloadMs;
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
// Bloody Mess (see `show_damage`, `show_death`): `critter` is about to die
// a gory death from a critical hit of the player's. When its death
// animation is over its worn armor is destroyed (once; carried spare armor
// and natural armor are untouched).
void rt_note_gory_death(Object* critter);
void rt_gory_death_shown(Object* critter);
// Fast Shot: the frame time of `anim` for `obj`, shortened for the player's
// attack animations and the target's reactions to them (see `compute_tpf`).
unsigned int rt_anim_frame_time(Object* obj, int anim, unsigned int frameTime);
// Timing of repeated manual attacks: what the player's attack being
// committed right now loses from its hit chance (see `determine_to_hit`).
int rt_player_timing_penalty();
// An enemy attack on the player with an ordinary outcome: the player has no
// part in the attacker's animation (see `action_attack`, `show_damage`);
// `rt_player_impact_cue` takes the place of the player's reaction there and
// plays it as the player's own animation.
bool rt_action_detached_defender(Object* obj);
int rt_player_impact_cue(void* attacker, void* defender);
void rt_log(const char* format, ...);

// Used by the automated test and diagnostics.
const RtStats* rt_get_stats();
Object* rt_get_lock();
const RtIndicator* rt_get_indicator();
// Draws the label and the lock-on indicator over the play area, in display
// coordinates (called by rt_view).
void rt_render_overlay(unsigned char* buffer, int pitch, Rect* rect);
// Moves the label and the indicator to where their objects appear (after a
// camera move).
void rt_overlays_relayout(bool invalidate);
bool rt_is_reloading();
Object* rt_get_player_slot_holder();
int rt_get_player_slot_queue_length();
int rt_get_player_slot_queue(Object** critters, int capacity);
// What A would interact with right now.
Object* rt_get_interact_target();
int rt_get_camera_margin_pct();
bool rt_is_hostile(Object* critter);

// Whether `critter` is the player or a critter on the current elevation (a
// pointer check only: nothing is read from it). For pointers that may be
// stale, such as a critter's "who hit me".
bool rt_is_live_critter(Object* critter);
void rt_debug_set_lock(Object* target);
void rt_debug_camera_follow();
// Test fixture: sets the player's current AP.
void rt_debug_set_ap(float ap);
// Test fixture: NPCs stop acting while paused.
void rt_debug_set_ai_paused(bool paused);
// Tests: the player's next `count` attacks miss (no damage, no effects);
// asks for a counterattack as a miss of `swing` would (see
// `rt_request_counter`); counterattacks still owed.
void rt_debug_force_player_misses(int count);
// Tests: the player's next `count` attacks hit for `damage` with the
// defender flags `flags` (0: an ordinary hit reaction; no critical or
// knockback); attacks pending with `obj` as a target.
void rt_debug_force_player_hits(int count, int damage, int flags = 0);
int rt_debug_attacks_on(Object* obj);
// Tests: whether the player is held by an attack (theirs or an enemy's).
bool rt_debug_player_held();
// Tests: the timing penalty the next RT attack would get now.
int rt_debug_timing_preview();
// Tests: -1 while the player's own attack is in flight, otherwise how long
// its recovery (the attack cooldown) still lasts.
int rt_debug_recovery_left_ms();
void rt_debug_counter_request(Object* critter, int swing, bool ranged = false);
int rt_debug_counter_count();

// Whether an attack with `weapon` in `hitMode` sweeps (two-handed melee,
// see `rt_is_sweep`).
bool rt_weapon_sweeps(Object* weapon, int hitMode);
float rt_get_dude_ap();
bool rt_is_run_toggled();
bool rt_is_engaged(Object* critter);

// Whether a hostile critter is engaged with the player team nearby (the
// real-time equivalent of being in combat).
bool rt_player_in_combat();

// Toggles Sneak (R3, or Sneak in the Skilldex).
void rt_toggle_sneak();
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
int rt_aim_attack(Object* target, int hitMode, int hitLocation, float apCost);

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
