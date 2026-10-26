#include "game/realtime.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL.h>

#include "game/actions.h"
#include "game/anim.h"
#include "game/art.h"
#include "game/combat.h"
#include "game/combatai.h"
#include "game/critter.h"
#include "game/display.h"
#include "game/game.h"
#include "game/gconfig.h"
#include "game/gmouse.h"
#include "game/gsound.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/party.h"
#include "game/perk.h"
#include "game/proto.h"
#include "game/roll.h"
#include "game/rt_aimed.h"
#include "game/rt_context.h"
#include "game/rt_favorites.h"
#include "game/rt_pipboy.h"
#include "game/protinst.h"
#include "game/rt_rules.h"
#include "game/rt_view.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/tile.h"
#include "platform_compat.h"
#include "plib/color/color.h"
#include "plib/gnw/gamepad.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

#define RT_CONFIG_SECTION "realtime"

// Returned by `rt_attack` when the attacker or defender is already part of an
// attack sequence (Fallout's attack animations cannot overlap per object).
#define RT_ATTACK_BUSY 1

#define RT_PENDING_ATTACK_TIMEOUT 6000

// How long a swing's impact waits for a target busy with another attack.
#define RT_SWING_IMPACT_WAIT_MS 400
#define RT_TARGET_MAX_DISTANCE 25
#define RT_ATTACK_BUFFER_MS 1200
#define RT_PLAYER_RECOVERY_MS 700
// RT held this long after a press starts repeating (automatic weapons only).
#define RT_HOLD_REPEAT_MS 250
// Right stick target switching: candidates within 60 degrees of the flick.
#define RT_SWITCH_CONE_COS 0.5f
// Player attack slot: a waiting enemy that stops asking for this long (moved,
// lost its shot) leaves the queue; a queue head that can't start its attack
// for this long goes to the back so it never blocks the others.
#define RT_SLOT_STALE_MS 1000
// Queue entries count as ready to attack when they asked this recently.
#define RT_SLOT_READY_MS 300
#define RT_SLOT_HEAD_TIMEOUT_MS 1500
// Reload without put-away/draw art: the weapon is busy this long instead.
#define RT_RELOAD_FALLBACK_MS 1000
// Right stick panning: time between scroll steps at full deflection.

// An RT press with a chem waits this long for an attack involving the player.
#define RT_CHEM_BUFFER_MS 1500
// A walk that hasn't moved a critter for this long is stalled.
#define RT_MOVE_STALL_MS 1500
// A reload press waits this long for an attack involving the player to end.
#define RT_RELOAD_BUFFER_MS 1500
// A favorite picked while an animation of the player's plays waits this long.
#define RT_FAVORITE_BUFFER_MS 2500

// Results of `rt_reload`.
#define RT_RELOAD_STARTED 0
#define RT_RELOAD_WAIT 1
#define RT_RELOAD_FAILED 2

// Results of `rt_player_attack`.
#define RT_PLAYER_ATTACK_STARTED 0
#define RT_PLAYER_ATTACK_WAIT 1
#define RT_PLAYER_ATTACK_FAILED 2
// Interaction: objects up to this far are highlighted (and can be stepped
// through); A acts only on one within RT_INTERACT_REACH; left/right steps
// only to an object within RT_INTERACT_CYCLE_DISTANCE of the highlighted one.
#define RT_INTERACT_MAX_DISTANCE 3
#define RT_INTERACT_REACH 1
#define RT_INTERACT_CYCLE_DISTANCE 2
#define RT_MOVE_LOOKAHEAD 8
#define RT_ENGAGE_TEAM_RADIUS 12
#define RT_COMPANION_RADIUS 14
#define RT_DISENGAGE_DISTANCE 30

typedef struct RtCritterState {
    bool engaged;
    Object* target;
    unsigned int nextActionAt;
    bool moving;
    int moveTargetTile;
    unsigned int knockedDownAt;
    unsigned int lastDebugAt;
    // Last time this critter was ready to attack the player (slot queue).
    unsigned int slotRequestAt;
    // Walk progress (see the stall check in `rt_ai_act`).
    int progressTile;
    unsigned int progressAt;
} RtCritterState;

typedef struct RtPendingAttack {
    Attack attack;
    unsigned int startedAt;
    // Deaths decided by a critical hit effect rather than by the damage
    // (attacker, defender, extras), see `rt_refresh_death_flags`.
    bool criticalDeath[2 + EXPLOSION_TARGET_COUNT];
    bool slowLogged;
    // An ordinary attack of the player's (not an AIMED SHOT): the swing or
    // shot it belongs to, so a miss can give that enemy a counterattack
    // (one per swing or shot, see `rt_request_counter`). 0 otherwise.
    // `counterRanged`: a shot or throw rather than a close-combat attack.
    int counterSwing;
    bool counterRanged;
    // Who the attack was aimed at: a missed shot can be redirected to
    // whatever lies beyond (`check_ranged_miss` changes the defender), but
    // the counterattack belongs to the intended target.
    Object* counterTarget;
    // The defender ([0]) and extras ([1 + index]) whose ordinary reaction is
    // over: they may move again before the attack lands (while the attacker
    // finishes its own animation), see `rt_release_targets`.
    bool released[1 + EXPLOSION_TARGET_COUNT];
    // An enemy attack on the player with an ordinary outcome: the player
    // has no part in its animation and isn't held by it (see
    // `rt_player_detachable`).
    bool playerDetached;
    // Timing penalty this attack of the player's was committed with.
    int timingPenalty;
} RtPendingAttack;

// A melee or unarmed swing started without a target in reach (see
// `rt_player_swing`); its hit is decided at the impact frame.
typedef struct RtSwing {
    bool active;
    int hitMode;
    int anim;
    // The target in reach when the swing started, if any.
    Object* aim;
    bool impactDue;
    bool resolved;
    bool hit;
    unsigned int startedAt;
    unsigned int impactAt;
    // Numbers swings (counterattacks are once per swing).
    int serial;
    // Two-handed melee: the swing can hit several targets in its arc; those
    // attacked so far (each at most once).
    bool sweep;
    Object* sweepDone[8];
    int sweepDoneCount;
    // Timing penalty of the RT press that started it (see
    // `rt_timing_penalty_for`), for each of its hits.
    int timingPenalty;
} RtSwing;

// A counterattack an enemy is owed after the player's attack missed it:
// its next attack on the player goes before the others'. After a missed
// shot or throw (`ranged`) it has longer, as it may first have to get within
// its own weapon's range.
typedef struct RtCounter {
    Object* critter;
    unsigned int requestedAt;
    unsigned int windowMs;
    bool ranged;
} RtCounter;

static void rt_init();
static void rt_open_log();
static void rt_rebuild_critter_list();
static bool rt_is_known_critter(Object* obj);
static bool rt_is_alive(Object* critter);
static bool rt_is_player_team(Object* critter);
static bool rt_obj_in_attack(Object* obj);
static void rt_timing_clear(const char* reason);
static int rt_timing_preview();
static void rt_update_player_impacts();
static int rt_attack(Object* attacker, Object* defender, int hitMode, int hitLocation = HIT_LOCATION_UNCALLED);
static void rt_attack_started(RtPendingAttack* pending, int hitLocation, int toHit);
static int rt_player_swing(int hitMode);
static void rt_update_swing();
static void rt_note_critical_deaths(RtPendingAttack* pending);
static void rt_stop_for_attack(Object* obj);
static void rt_refresh_death_flags(RtPendingAttack* pending);
static void rt_resolve_attacks();
static void rt_engage(Object* critter, Object* target, const char* reason);
static void rt_update_engagements();
static void rt_update_knockdowns();
static void rt_update_ai();
static void rt_ai_act(Object* critter, RtCritterState* state);
static int rt_npc_hit_mode(Object* critter, bool* reloaded);
static int rt_check_shot(Object* attacker, Object* defender, int hitMode);
static unsigned int rt_attack_cooldown(Object* attacker, int hitMode);
static void rt_update_dude(float dtMs);
static void rt_update_ap(float dtMs);
static void rt_sync_weapon_sprite();
static void rt_update_movement(bool canAct);
static void rt_stop_walking();
static int rt_find_move_dest(int rotation);
static int rt_rotation_from_vector(float x, float y);
static bool rt_rotation_vector(int rotation, float* x, float* y);
static bool rt_facing_vector(float* x, float* y);
static float rt_facing_cos(Object* obj, float fx, float fy);
static bool rt_is_hostile_to_player(Object* critter);
static bool rt_lock_is_valid(Object* target);
static Object* rt_find_lock_candidate();
static void rt_set_lock(Object* target);
static bool rt_is_lock_candidate(Object* critter);
static bool rt_screen_pos(Object* obj, int* x, int* y);
static void rt_switch_lock(float dx, float dy);
static void rt_update_indicator();
static void rt_update_player_slot();
static bool rt_player_slot_may_attack(Object* critter, RtCritterState* state);
static int rt_reload_commit(void* a1, void* a2);
static int rt_reload_finish(void* a1, void* a2);
static void rt_update_x_button(bool pressX);
static void rt_update_holster();
static void rt_update_draw();
static int rt_holster_commit(void* a1, void* a2);
static void rt_update_reload();
static bool rt_get_play_area(int* width, int* height);
static bool rt_is_automatic(int hitMode);
static void rt_request_chem(Object* chem);
static void rt_update_chem_request();
static void rt_update_favorite_request();
static void rt_update_attack_input(bool pressAttack);
static int rt_player_attack(bool explicitPress);
static int rt_player_attack_with(int hitMode, bool rbThrow);
static void rt_set_throw_sprite(Object* item);
static void rt_update_throw_input();
static int rt_player_hit_mode(int* hitMode);
static Object* rt_find_interactable();
static void rt_collect_interactables();
static bool rt_interact_reachable(Object* obj);
static void rt_cycle_interaction(int delta);
static bool rt_can_cycle_interaction(Object* current);
static void rt_update_label(Object* target);
static void rt_interact(Object* obj);
static int rt_reload();
static void rt_set_interact_highlight(Object* obj);
static void rt_set_outline(Object** slot, Object* obj, int outlineType);
static void rt_update_camera();
static void rt_toggle_zoom();
static void rt_print(const char* format, ...);
static char* rt_name(Object* obj);
static int rt_hits(Object* obj);

static bool rt_initialized = false;
static bool rt_is_enabled = true;
static bool rt_camera_edge_scroll = true;
static int rt_camera_margin_pct = 25;
static int rt_run_drain_ms = 400;
static int rt_ap_regen_ms = 800;
static int rt_ms_per_ap = 110;
// X held this long holsters the weapon (shorter: a reload).
static int rt_holster_hold_ms = 400;

static FILE* rt_log_stream = NULL;
static char rt_log_dir[COMPAT_MAX_PATH] = "";

static unsigned int rt_now = 0;
static unsigned int rt_last_real_tick = 0;

static RtStats rt_stats;
static std::unordered_map<Object*, RtCritterState> rt_states;
static std::vector<RtPendingAttack*> rt_pending;
static std::vector<Object*> rt_critters;
static std::unordered_set<Object*> rt_critter_set;
static int rt_critter_elevation = -1;

// Player state.
static float rt_dude_ap = 0.0f;
static int rt_dude_ap_shown = -1;
static bool rt_run_toggled = false;
static bool rt_move_active = false;
static int rt_move_dir = -1;
static bool rt_move_run = false;
static int rt_move_dest = -1;
static unsigned int rt_next_move_issue_at = 0;
static int rt_move_progress_tile = -1;
static unsigned int rt_move_progress_at = 0;
static unsigned int rt_dude_next_attack_at = 0;
// Bloody Mess: critters whose gory death (see `rt_note_gory_death`) is
// still playing.
static std::vector<Object*> rt_gory_deaths;
// Fast Shot: the target of the player attack being registered.
static Object* rt_registering_target = NULL;
// The player while an enemy attack with an ordinary outcome on them is
// being registered (see `rt_action_detached_defender`).
static Object* rt_registering_detached = NULL;
// Enemy attacks whose impact on the player came (their attackers, see
// `rt_player_impact_cue`): the player's reaction plays on the next tick.
static std::vector<Object*> rt_player_impacts;
// Timing of repeated manual attacks (see `rt_timing_penalty_for`): RT
// presses made during the player's current attack or its recovery, the
// latest of them, when the recovery last ended, whether it was in progress
// on the last tick, the weapon and attack mode of the presses, the penalty
// of the attack being committed, and the input interruption count (a focus
// loss or disconnect in between clears it all; so does a menu, see
// `rt_tick`).
static int rt_timing_presses = 0;
static unsigned int rt_timing_press_at = 0;
static unsigned int rt_timing_ready_at = 0;
static bool rt_timing_recovering = false;
static Object* rt_timing_weapon = NULL;
static int rt_timing_hit_mode = -1;
static int rt_timing_committing = 0;
static int rt_timing_interruptions = 0;
// Hunting Rifle reloads in the armed idle pose: when the ammunition moves
// (0 when the reload isn't one).
static unsigned int rt_reload_commit_at = 0;
static unsigned int rt_reload_started_at = 0;
// Its weapon code and the length of its idle animation.
static int rt_reload_idle_code = 0;
static unsigned int rt_reload_idle_anim_ms = 0;
// When its idle animation playing now ends (a lone idle animation doesn't
// count as busy, see `anim_busy`).
static unsigned int rt_reload_idle_next_at = 0;
static Object* rt_lock = NULL;
static RtSwing rt_swing;
static int rt_swing_serial = 0;
// Counterattacks owed, oldest first, and the swing that asked last.
static std::vector<RtCounter> rt_counters;
static int rt_counter_last_swing = 0;

// Something drawn over the map by `rt_render_overlay`, at its normal size
// wherever the object it belongs to appears in the play area (whatever the
// camera pan and zoom).
typedef struct RtOverlay {
    bool visible;
    // The object's bounds in world buffer coordinates (`obj_bound`).
    Rect world;
    // Where it is drawn in the play area; empty when hidden.
    Rect shown;
    int kind;
} RtOverlay;

#define RT_OVERLAY_INDICATOR 0
#define RT_OVERLAY_LABEL 1

static void rt_overlay_set(RtOverlay* overlay, bool visible, const Rect* world, bool contentChanged);

// Lock-on indicator: crosshair art plus hit chance, drawn over the map.
static RtIndicator rt_indicator;
static RtOverlay rt_indicator_overlay = { false, { 0, 0, -1, -1 }, { 0, 0, -1, -1 }, RT_OVERLAY_INDICATOR };
static unsigned char* rt_indicator_buf = NULL;
static int rt_indicator_width = 0;
static int rt_indicator_height = 0;
static int rt_indicator_cross_width = 0;
static int rt_indicator_cross_height = 0;
static char rt_indicator_built_text[32] = "";
static int rt_indicator_built_color = -1;
static unsigned int rt_attack_buffered_until = 0;
// RB: a throw of the right hand's thrown weapon waiting to start.
static unsigned int rt_throw_buffered_until = 0;
static bool rt_fire_hold = false;
static unsigned int rt_fire_pressed_at = 0;
// The attack of the latest RT press has started; only then can a hold repeat.
static bool rt_fire_press_done = false;
static unsigned int rt_player_recovery_until = 0;
static Object* rt_interact_outlined = NULL;
static Object* rt_interact_target = NULL;
static int rt_last_camera_elevation = -1;
static bool rt_camera_scrolling = false;
// The right stick moved the view away from the player.
static bool rt_camera_free = false;
// Smooth camera: pan velocity (play area pixels per second), who owns the
// right stick for the current push (camera or target switching), the wider
// view toggle and the tick length.
static float rt_pan_vx = 0.0f;
static float rt_pan_vy = 0.0f;
static int rt_rstick_owner = 0;
// Fastest edge-follow glide, play area pixels per second.
#define RT_CAMERA_FOLLOW_MAX_SPEED 1100.0f
#define RT_RSTICK_NONE 0
#define RT_RSTICK_PAN 1
#define RT_RSTICK_SWITCH 2
static bool rt_camera_limit_logged = false;
static float rt_camera_dt = 0.0f;
static int rt_cam_deadzone_pct = 20;
static int rt_cam_pan_speed = 1100;
static int rt_cam_pan_smoothing_ms = 60;
static int rt_cam_follow_ms = 110;
static int rt_cam_zoom_ms = 200;

// Player attack slot: one committed enemy attack on the player at a time,
// granted first come, first served.
static Object* rt_slot_holder = NULL;
static std::vector<Object*> rt_slot_queue;
static unsigned int rt_slot_head_since = 0;
static std::unordered_map<Object*, unsigned int> rt_slot_wait_start;

// Reload in progress (holster, ammo transfer, draw).
static bool rt_reload_active = false;
static bool rt_reload_committed = false;
static bool rt_reload_finished = false;
static Object* rt_reload_weapon = NULL;
static unsigned int rt_reload_until = 0;
static unsigned int rt_reload_requested_until = 0;

// X (see `rt_update_x_button`) and holstering (see `rt_update_holster`).
static bool rt_x_pending = false;
static unsigned int rt_x_pressed_at = 0;
static unsigned int rt_x_seen_at = 0;
static int rt_x_interruptions = 0;
static bool rt_holster_active = false;
static bool rt_holster_committed = false;
static Object* rt_holster_weapon = NULL;
static unsigned int rt_holster_requested_until = 0;
// A draw animation waiting for the player's current animation to end.
static Object* rt_draw_pending = NULL;
static unsigned int rt_draw_until = 0;

// Chem use with RT or a favorite (see `rt_update_chem_request`).
static bool rt_chem_active = false;
static unsigned int rt_chem_requested_until = 0;
static Object* rt_chem_request_item = NULL;

// A favorite (weapon, armor, item) waiting to be activated.
static int rt_fav_pending_slot = -1;
static unsigned int rt_fav_pending_until = 0;

// Interaction (A): the objects in reach, in a stable clockwise order around
// the player, and the one picked with the D-pad (kept while it stays valid).
static std::vector<Object*> rt_interact_candidates;
static Object* rt_interact_manual = NULL;

// Whether an object near the player can be reached on foot (no wall in
// between), computed again only when either of them moved.
typedef struct RtReach {
    int dudeTile;
    int objTile;
    bool reachable;
} RtReach;
static std::unordered_map<Object*, RtReach> rt_reach_cache;

// Interaction label: the highlighted object's name, what A does and, with
// several objects in reach, the D-pad prompts.
static RtOverlay rt_label = { false, { 0, 0, -1, -1 }, { 0, 0, -1, -1 }, RT_OVERLAY_LABEL };
static char rt_label_text[200] = "";
static unsigned char* rt_label_buf = NULL;
static int rt_label_capacity = 0;
static int rt_label_width = 0;
static int rt_label_height = 0;

bool rt_enabled()
{
    if (!rt_initialized) {
        rt_init();
    }
    return rt_is_enabled;
}

static void rt_init()
{
    rt_initialized = true;

    int value;
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "enabled", &value)) {
        rt_is_enabled = value != 0;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "camera_edge_scroll", &value)) {
        rt_camera_edge_scroll = value != 0;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "camera_margin_pct", &value) && value >= 5 && value <= 45) {
        rt_camera_margin_pct = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "run_drain_ms_per_ap", &value) && value > 0) {
        rt_run_drain_ms = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "ap_regen_ms_per_ap", &value) && value > 0) {
        rt_ap_regen_ms = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "attack_cooldown_ms_per_ap", &value) && value >= 0) {
        rt_ms_per_ap = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "holster_hold_ms", &value) && value >= 100 && value <= 2000) {
        rt_holster_hold_ms = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "camera_pan_deadzone_pct", &value) && value >= 5 && value <= 60) {
        rt_cam_deadzone_pct = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "camera_pan_speed", &value) && value >= 100 && value <= 5000) {
        rt_cam_pan_speed = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "camera_pan_smoothing_ms", &value) && value >= 0 && value <= 500) {
        rt_cam_pan_smoothing_ms = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "camera_follow_ms", &value) && value >= 0 && value <= 1000) {
        rt_cam_follow_ms = value;
    }
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "camera_zoom_transition_ms", &value) && value >= 0 && value <= 1000) {
        rt_cam_zoom_ms = value;
    }
    gamepad_set_look_deadzone(rt_cam_deadzone_pct / 100.0f);

    rt_open_log();

    rt_log("Fallout1-RealTime: real-time layer %s", rt_is_enabled ? "ENABLED" : "disabled");
    rt_log("config: camera_edge_scroll=%d camera_margin_pct=%d run_drain_ms_per_ap=%d ap_regen_ms_per_ap=%d attack_cooldown_ms_per_ap=%d",
        rt_camera_edge_scroll, rt_camera_margin_pct, rt_run_drain_ms, rt_ap_regen_ms, rt_ms_per_ap);
    rt_log("config: camera_pan_deadzone_pct=%d camera_pan_speed=%d camera_pan_smoothing_ms=%d camera_follow_ms=%d camera_zoom_transition_ms=%d camera_zoom_out=%.2f",
        rt_cam_deadzone_pct, rt_cam_pan_speed, rt_cam_pan_smoothing_ms, rt_cam_follow_ms, rt_cam_zoom_ms, rt_view_get_max_zoom());
    rt_log("controllers detected at startup: %d%s%s",
        gamepad_get_detected_count(),
        gamepad_get_detected_count() > 0 ? ", using: " : "",
        gamepad_get_name());
}

static void rt_open_log()
{
    // Logs belong in the project's logs folder, next to runtime\.
    const char* candidates[] = { "..\\logs\\", "logs\\", "" };
    for (const char* dir : candidates) {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%srealtime.log", dir);
        rt_log_stream = compat_fopen(path, "wt");
        if (rt_log_stream != NULL) {
            SDL_strlcpy(rt_log_dir, dir, sizeof(rt_log_dir));
            break;
        }
    }
}

unsigned int rt_get_time()
{
    return rt_now;
}

int rt_get_pending_count()
{
    return (int)rt_pending.size();
}

const char* rt_get_log_dir()
{
    if (!rt_initialized) {
        rt_init();
    }
    return rt_log_dir;
}

void rt_log(const char* format, ...)
{
    if (rt_log_stream == NULL) {
        return;
    }

    va_list args;
    va_start(args, format);
    fprintf(rt_log_stream, "[%8u] ", rt_now);
    vfprintf(rt_log_stream, format, args);
    fputc('\n', rt_log_stream);
    fflush(rt_log_stream);
    va_end(args);
}

static void rt_print(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    display_print(buffer);
    rt_log("ui: %s", buffer);
}

// AI decision log, at most once a second per critter.
static void rt_ai_debug(Object* critter, RtCritterState* state, const char* format, ...)
{
    if (rt_now - state->lastDebugAt < 1000) {
        return;
    }
    state->lastDebugAt = rt_now;

    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    rt_log("ai: %s%s: %s", critter_name(critter), rt_is_player_team(critter) ? " (player team)" : "", buffer);
}

static char* rt_name(Object* obj)
{
    return FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER ? critter_name(obj) : object_name(obj);
}

static int rt_hits(Object* obj)
{
    return FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER ? critter_get_hits(obj) : 0;
}

void rt_reset()
{
    if (!rt_enabled()) {
        return;
    }

    // Objects referenced by pending attacks may be gone after a map change;
    // those attacks never land (counted as dropped).
    for (RtPendingAttack* pending : rt_pending) {
        rt_stats.attacksDropped++;
        delete pending;
    }
    rt_pending.clear();
    rt_states.clear();
    rt_critters.clear();
    rt_critter_set.clear();
    rt_critter_elevation = -1;

    rt_dude_ap = -1.0f;
    rt_dude_ap_shown = -1;
    rt_run_toggled = false;
    rt_move_active = false;
    rt_move_dir = -1;
    rt_move_dest = -1;
    rt_lock = NULL;
    rt_indicator.visible = false;
    rt_indicator_overlay.visible = false;
    rt_indicator_overlay.shown = { 0, 0, -1, -1 };
    rt_attack_buffered_until = 0;
    rt_throw_buffered_until = 0;
    rt_fire_hold = false;
    rt_fire_press_done = false;
    rt_interact_outlined = NULL;
    rt_interact_target = NULL;
    rt_last_camera_elevation = -1;
    rt_camera_free = false;
    rt_camera_scrolling = false;
    rt_camera_limit_logged = false;
    rt_pan_vx = 0.0f;
    rt_pan_vy = 0.0f;
    rt_rstick_owner = RT_RSTICK_NONE;
    rt_view_reset_pan();
    rt_slot_holder = NULL;
    rt_slot_queue.clear();
    rt_slot_wait_start.clear();
    // An unfinished reload never transferred ammo (that happens mid-sequence
    // in an animation callback), so dropping it loses nothing.
    rt_chem_active = false;
    rt_chem_requested_until = 0;
    rt_reload_requested_until = 0;
    rt_reload_active = false;
    rt_reload_weapon = NULL;
    rt_dude_next_attack_at = 0;
    rt_player_recovery_until = 0;
    rt_chem_request_item = NULL;
    rt_fav_pending_slot = -1;
    rt_x_pending = false;
    rt_holster_active = false;
    rt_holster_weapon = NULL;
    rt_draw_pending = NULL;
    rt_holster_requested_until = 0;
    rt_interact_manual = NULL;
    rt_interact_candidates.clear();
    rt_reach_cache.clear();
    rt_label_text[0] = '\0';
    rt_label.visible = false;
    rt_label.shown = { 0, 0, -1, -1 };
    memset(&rt_swing, 0, sizeof(rt_swing));
    rt_counters.clear();
    rt_counter_last_swing = 0;
    rt_aimed_reset("map load");
    rt_gory_deaths.clear();
    rt_registering_target = NULL;
    rt_registering_detached = NULL;
    rt_player_impacts.clear();
    rt_timing_clear(NULL);
    rt_timing_ready_at = 0;
    rt_timing_recovering = false;
    rt_reload_commit_at = 0;

    gamepad_reset_gameplay_input();

    rt_log("reset (map load)");
}

void rt_object_erased(Object* obj)
{
    if (!rt_initialized || obj == NULL) {
        return;
    }

    if (rt_interact_outlined == obj) {
        rt_interact_outlined = NULL;
    }
    if (rt_interact_target == obj) {
        rt_interact_target = NULL;
    }
    if (rt_interact_manual == obj) {
        rt_interact_manual = NULL;
    }
    rt_interact_candidates.erase(std::remove(rt_interact_candidates.begin(), rt_interact_candidates.end(), obj), rt_interact_candidates.end());
    rt_reach_cache.erase(obj);
    if (rt_chem_request_item == obj) {
        rt_chem_request_item = NULL;
        rt_chem_requested_until = 0;
    }
    if (rt_swing.aim == obj) {
        rt_swing.aim = NULL;
    }
    for (int index = 0; index < rt_swing.sweepDoneCount; index++) {
        if (rt_swing.sweepDone[index] == obj) {
            rt_swing.sweepDone[index] = NULL;
        }
    }
    rt_counters.erase(std::remove_if(rt_counters.begin(), rt_counters.end(), [obj](const RtCounter& counter) { return counter.critter == obj; }), rt_counters.end());
    for (RtPendingAttack* pending : rt_pending) {
        if (pending->counterTarget == obj) {
            pending->counterTarget = NULL;
        }
    }
    // A critter that remembers the removed object as its attacker forgets
    // it (turn-based combat does this in `combat_delete_critter`).
    for (Object* critter : rt_critters) {
        if (critter != obj && critter->data.critter.combat.whoHitMe == obj) {
            critter->data.critter.combat.whoHitMe = NULL;
        }
    }
    if (obj_dude != NULL && obj_dude != obj && obj_dude->data.critter.combat.whoHitMe == obj) {
        obj_dude->data.critter.combat.whoHitMe = NULL;
    }
    rt_gory_deaths.erase(std::remove(rt_gory_deaths.begin(), rt_gory_deaths.end(), obj), rt_gory_deaths.end());
    if (rt_registering_target == obj) {
        rt_registering_target = NULL;
    }
    rt_player_impacts.erase(std::remove(rt_player_impacts.begin(), rt_player_impacts.end(), obj), rt_player_impacts.end());
    if (rt_timing_weapon == obj) {
        rt_timing_weapon = NULL;
    }
    rt_aimed_object_erased(obj);
    if (rt_lock == obj) {
        rt_lock = NULL;
        rt_fire_hold = false;
        rt_attack_buffered_until = 0;
        rt_throw_buffered_until = 0;
        rt_log("lock: cleared (target removed)");
    }
    if (rt_holster_weapon == obj) {
        rt_holster_weapon = NULL;
    }
    if (rt_draw_pending == obj) {
        rt_draw_pending = NULL;
    }
    if (rt_reload_weapon == obj) {
        rt_reload_weapon = NULL;
    }
    if (rt_slot_holder == obj) {
        rt_slot_holder = NULL;
    }
    rt_slot_queue.erase(std::remove(rt_slot_queue.begin(), rt_slot_queue.end(), obj), rt_slot_queue.end());
    rt_slot_wait_start.erase(obj);
    rt_states.erase(obj);
    for (auto& entry : rt_states) {
        if (entry.second.target == obj) {
            entry.second.engaged = false;
            entry.second.target = NULL;
        }
    }
    rt_critter_set.erase(obj);
    rt_critters.erase(std::remove(rt_critters.begin(), rt_critters.end(), obj), rt_critters.end());

    // Attacks involving it can't land any more.
    for (size_t index = 0; index < rt_pending.size();) {
        Attack* attack = &(rt_pending[index]->attack);
        bool involved = attack->attacker == obj || attack->defender == obj;
        for (int extra = 0; extra < attack->extrasLength && !involved; extra++) {
            involved = attack->extras[extra] == obj;
        }

        if (involved) {
            rt_log("attack dropped: participant removed");
            rt_stats.attacksDropped++;
            delete rt_pending[index];
            rt_pending.erase(rt_pending.begin() + index);
        } else {
            index++;
        }
    }
}

const RtStats* rt_get_stats()
{
    return &rt_stats;
}

Object* rt_get_lock()
{
    return rt_lock;
}

const RtIndicator* rt_get_indicator()
{
    return &rt_indicator;
}

bool rt_is_reloading()
{
    return rt_reload_active;
}

Object* rt_get_player_slot_holder()
{
    return rt_slot_holder;
}

int rt_get_player_slot_queue_length()
{
    return (int)rt_slot_queue.size();
}

Object* rt_get_interact_target()
{
    return rt_interact_target;
}

int rt_get_camera_margin_pct()
{
    return rt_camera_margin_pct;
}

int rt_get_player_slot_queue(Object** critters, int capacity)
{
    int count = 0;
    for (Object* critter : rt_slot_queue) {
        if (count >= capacity) {
            break;
        }
        critters[count++] = critter;
    }
    return count;
}

bool rt_is_live_critter(Object* critter)
{
    if (critter == NULL) {
        return false;
    }
    if (critter == obj_dude) {
        return true;
    }
    if (!rt_initialized || rt_critter_elevation == -1) {
        // Nothing to check against yet: trust it, as the original does.
        return true;
    }
    return rt_critter_set.find(critter) != rt_critter_set.end();
}

bool rt_is_hostile(Object* critter)
{
    return rt_is_hostile_to_player(critter);
}

// Harness: the camera follows the player again (as when the player moves
// after a manual pan).
void rt_debug_camera_follow()
{
    rt_camera_free = false;
    rt_pan_vx = 0.0f;
    rt_pan_vy = 0.0f;
}

void rt_debug_set_lock(Object* target)
{
    rt_set_lock(target);
}

void rt_debug_set_ap(float ap)
{
    rt_dude_ap = ap;
    rt_dude_ap_shown = -1;
}

float rt_get_dude_ap()
{
    return rt_dude_ap;
}

bool rt_is_run_toggled()
{
    return rt_run_toggled;
}

bool rt_is_engaged(Object* critter)
{
    auto it = rt_states.find(critter);
    return it != rt_states.end() && it->second.engaged;
}

// Sneak in the Skilldex: the original Sneak toggle (the detection
// rules and the HUD's SNEAK box are the original game's). Fallout 1 has no
// sneaking animation: the player walks normally. A run in progress is
// turned off, since running ends sneaking (unless Silent Running).
void rt_toggle_sneak()
{
    bool on = !is_pc_flag(PC_FLAG_SNEAKING);
    if (on && rt_run_toggled && !perk_level(PERK_SILENT_RUNNING)) {
        rt_run_toggled = false;
        rt_log("run: off (sneaking)");
    }

    pc_flag_toggle(PC_FLAG_SNEAKING);
    rt_stats.sneakToggles++;
    rt_print(on ? "Sneaking." : "You stop sneaking.");
    rt_log("sneak: %s", on ? "on" : "off");
}

bool rt_player_in_combat()
{
    for (Object* critter : rt_critters) {
        if (critter->elevation == obj_dude->elevation
            && rt_is_hostile_to_player(critter)
            && obj_dist(obj_dude, critter) <= RT_DISENGAGE_DISTANCE) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Main tick
// -----------------------------------------------------------------------------

void rt_tick()
{
    if (!rt_enabled()) {
        return;
    }

    // Controller gameplay: no 3D map cursor, no mouse edge scrolling.
    gmouse_set_rt_controller_mode(true);

    unsigned int realTick = SDL_GetTicks();
    unsigned int realDelta = rt_last_real_tick != 0 ? realTick - rt_last_real_tick : 0;
    rt_last_real_tick = realTick;

    // A menu, dialogue or other modal screen was open since the last tick: RT
    // presses made before it don't carry over.
    static int modalPolls = -1;
    int polls = gamepad_get_modal_poll_count();
    if (modalPolls != -1 && polls != modalPolls) {
        rt_timing_clear("menu or other screen");
    }
    modalPolls = polls;

    // Modal menus and dialogue block the main loop, so a long gap means the
    // game was paused. Clamping keeps cooldowns and AP from jumping ahead.
    if (realDelta > 100) {
        realDelta = 100;
    }
    rt_now += realDelta;

    if (obj_dude == NULL || critter_is_dead(obj_dude)) {
        rt_aimed_reset("player dead");
        return;
    }

    if (rt_dude_ap < 0.0f) {
        rt_dude_ap = (float)stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS);
    }

    rt_rebuild_critter_list();
    rt_resolve_attacks();
    rt_update_swing();
    rt_update_player_impacts();
    rt_update_player_slot();
    rt_update_engagements();
    rt_update_knockdowns();
    rt_update_dude((float)realDelta);
    rt_update_ai();
    rt_camera_dt = (float)realDelta;
    rt_update_camera();
    rt_update_indicator();
}

static void rt_rebuild_critter_list()
{
    rt_critters.clear();
    rt_critter_set.clear();

    Object** list;
    int count = obj_create_list(-1, map_elevation, OBJ_TYPE_CRITTER, &list);
    for (int index = 0; index < count; index++) {
        rt_critters.push_back(list[index]);
        rt_critter_set.insert(list[index]);
    }
    if (count > 0) {
        obj_delete_list(list);
    }

    // Forget state of critters that left this elevation or were destroyed.
    for (auto it = rt_states.begin(); it != rt_states.end();) {
        if (rt_critter_set.find(it->first) == rt_critter_set.end()) {
            it = rt_states.erase(it);
        } else {
            ++it;
        }
    }

    // A locked target that left the map must not be touched again.
    if (rt_lock != NULL && !rt_is_known_critter(rt_lock)) {
        rt_log("lock: cleared (target left the map)");
        rt_lock = NULL;
    }

    rt_critter_elevation = map_elevation;
}

static bool rt_is_known_critter(Object* obj)
{
    return obj != NULL && rt_critter_set.find(obj) != rt_critter_set.end();
}

static bool rt_is_alive(Object* critter)
{
    return critter != NULL
        && FID_TYPE(critter->fid) == OBJ_TYPE_CRITTER
        && (critter->data.critter.combat.results & DAM_DEAD) == 0;
}

static bool rt_is_player_team(Object* critter)
{
    return critter == obj_dude
        || isPartyMember(critter)
        || critter->data.critter.combat.team == obj_dude->data.critter.combat.team;
}

// -----------------------------------------------------------------------------
// Attacks
// -----------------------------------------------------------------------------

// Whether `obj` must wait for an attack it is part of before acting: as
// the attacker until the attack lands; as a target until its reaction is
// over (see `rt_release_targets`), after which it may move but not attack
// (`rt_attack` still refuses while the attack is pending).
static bool rt_obj_held_by_attack(Object* obj)
{
    if (obj == obj_dude) {
        return rt_obj_in_attack(obj);
    }

    for (RtPendingAttack* pending : rt_pending) {
        Attack* attack = &(pending->attack);
        if (attack->attacker == obj || (attack->defender == obj && !pending->released[0])) {
            return true;
        }

        for (int index = 0; index < attack->extrasLength; index++) {
            if (attack->extras[index] == obj && !pending->released[1 + index]) {
                return true;
            }
        }
    }
    return false;
}

static bool rt_obj_in_attack(Object* obj)
{
    // A swing in progress keeps the player busy like an attack.
    if (obj == obj_dude && rt_swing.active) {
        return true;
    }

    for (RtPendingAttack* pending : rt_pending) {
        Attack* attack = &(pending->attack);
        // The player under an enemy attack with an ordinary outcome isn't
        // part of it until it lands.
        if (attack->attacker == obj || (attack->defender == obj && !pending->playerDetached)) {
            return true;
        }

        for (int index = 0; index < attack->extrasLength; index++) {
            if (attack->extras[index] == obj) {
                return true;
            }
        }
    }
    return false;
}

// An enemy attack on the player is pending (held or not): the attack slot
// lets one at a time through.
static bool rt_player_under_attack()
{
    for (RtPendingAttack* pending : rt_pending) {
        if (pending->attack.defender == obj_dude) {
            return true;
        }
    }
    return false;
}

// Whether the outcome of an enemy attack on the player is an ordinary one
// (a hit with no knockdown, knockback, fire, death or weapon loss, or a
// miss): the player then takes no part in the attacker's animation, plays
// their own hit reaction at the impact (none for a miss) and isn't held.
// Heavier outcomes play in the attack's sequence as before.
static bool rt_player_detachable(Attack* attack)
{
    if (attack->defender != obj_dude || attack->attacker == obj_dude || critter_is_prone(obj_dude)) {
        return false;
    }

    if ((attack->attackerFlags & DAM_HIT) == 0) {
        return true;
    }

    int heavy = DAM_DEAD | DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN | DAM_ON_FIRE | DAM_DROP | DAM_DESTROY | DAM_EXPLODE;
    return (attack->defenderFlags & heavy) == 0 && attack->defenderKnockback == 0;
}

bool rt_action_detached_defender(Object* obj)
{
    return obj != NULL && obj == rt_registering_detached;
}

int rt_player_impact_cue(void* attacker, void* defender)
{
    if (defender == obj_dude) {
        rt_player_impacts.push_back((Object*)attacker);
    }
    return 0;
}

// Plays the player's hit reaction for the enemy hits that just came, as the
// player's own short animation, when the player stands; a walk, attack,
// reload, chem use or holster of theirs goes on (no reaction then).
static void rt_update_player_impacts()
{
    if (rt_player_impacts.empty()) {
        return;
    }

    Object* attacker = rt_player_impacts.back();
    rt_player_impacts.clear();

    if (!rt_is_alive(obj_dude) || critter_is_prone(obj_dude)
        || (obj_dude->data.critter.combat.results & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) != 0) {
        return;
    }

    // Only when standing: a walk, an attack or anything else of the
    // player's goes on (an enemy hit never takes the controls away).
    if (anim_busy(obj_dude) || rt_move_active || rt_obj_in_attack(obj_dude) || rt_reload_active || rt_chem_active
        || rt_holster_active || rt_aimed_is_executing()) {
        rt_stats.playerReactionsSkipped++;
        rt_log("player reaction: skipped (busy, anim %d)", FID_ANIM_TYPE(obj_dude->fid));
        return;
    }

    // From the front unless the attacker stands behind (as `is_hit_from_front`).
    int anim = ANIM_HIT_FROM_FRONT;
    if (rt_is_known_critter(attacker)) {
        int delta = abs(attacker->rotation - obj_dude->rotation);
        bool front = delta != 0 && delta != 1 && delta != 5;
        int backFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_HIT_FROM_BACK, (obj_dude->fid & 0xF000) >> 12, obj_dude->rotation + 1);
        if (!front && art_exists(backFid)) {
            anim = ANIM_HIT_FROM_BACK;
        }
    }

    register_begin(ANIMATION_REQUEST_RESERVED);
    register_object_play_sfx(obj_dude, gsnd_build_character_sfx_name(obj_dude, anim, CHARACTER_SOUND_EFFECT_UNUSED), 0);
    register_object_animate(obj_dude, anim, 0);
    if (register_end() == 0) {
        rt_stats.playerReactions++;
        rt_log("player reaction: %s", anim == ANIM_HIT_FROM_BACK ? "hit from behind" : "hit from the front");
    }
}

bool rt_debug_player_held()
{
    return rt_obj_in_attack(obj_dude);
}

// Attack rate statistics (Fast Shot tests).
static void rt_note_player_attack_start()
{
    if (rt_stats.lastPlayerAttackAt != 0) {
        rt_stats.lastPlayerAttackInterval = rt_now - rt_stats.lastPlayerAttackAt;
    }
    rt_stats.lastPlayerAttackAt = rt_now;
}

// -----------------------------------------------------------------------------
// Attack AP and counterattacks
// -----------------------------------------------------------------------------

static bool rt_is_close_combat(int hitMode)
{
    int attackType = item_w_subtype(item_hit_with(obj_dude, hitMode), hitMode);
    return attackType == ATTACK_TYPE_MELEE || attackType == ATTACK_TYPE_UNARMED;
}

static bool rt_is_throw(int hitMode)
{
    Object* weapon = item_hit_with(obj_dude, hitMode);
    return weapon != NULL && item_w_subtype(weapon, hitMode) == ATTACK_TYPE_THROW;
}

// Ordinary attacks that cost AP: melee, unarmed and throws (gunfire costs AP
// only as an AIMED SHOT).
static bool rt_attack_costs_ap(int hitMode)
{
    return rt_is_close_combat(hitMode) || rt_is_throw(hitMode);
}

// Takes `cost` AP from the player (spent once, when an attack is committed).
static void rt_spend_ap(double cost)
{
    rt_dude_ap -= (float)cost;
    if (rt_dude_ap < 0.0f) {
        rt_dude_ap = 0.0f;
    }
    rt_dude_ap_shown = (int)rt_dude_ap;
    obj_dude->data.critter.combat.ap = rt_dude_ap_shown;
    intface_update_move_points(rt_dude_ap_shown, 0);
}

// Whether the player has the AP an ordinary attack with `hitMode` costs
// (`rt_attack_ap_cost`: 0.5 for fists, 1-2 for melee weapons, 3 for a
// throw). False, with a brief message, when they have less.
static bool rt_attack_ap_ok(int hitMode)
{
    double cost = rt_attack_ap_cost(obj_dude, hitMode, false);
    if (rt_dude_ap + 0.0001f >= (float)cost) {
        return true;
    }

    rt_stats.meleeApRefused++;
    char text[16];
    static unsigned int lastMessageAt = 0;
    if (lastMessageAt == 0 || rt_now - lastMessageAt >= 1000) {
        lastMessageAt = rt_now;
        rt_print("Not enough Action Points: this attack needs %s.", rt_ap_text(cost, text, sizeof(text)));
    }
    rt_log("ap: attack refused, %.1f AP needed, %.2f left", cost, rt_dude_ap);
    return false;
}

// Charges an ordinary attack's `cost` (worked out before the attack: a
// throw takes the item from the hand when it starts).
static void rt_spend_attack_ap(double cost, bool thrown)
{
    rt_spend_ap(cost);
    if (thrown) {
        rt_stats.throwApSpent += (float)cost;
        rt_stats.throwApCharges++;
    } else {
        rt_stats.meleeApSpent += (float)cost;
        rt_stats.meleeApCharges++;
        rt_stats.lastMeleeApCost = (float)cost;
    }
    rt_log("ap: %.1f AP spent on the attack, %.2f left", cost, rt_dude_ap);
}

static bool rt_is_counter(Object* critter)
{
    for (const RtCounter& counter : rt_counters) {
        if (counter.critter == critter) {
            return true;
        }
    }
    return false;
}

// Whether `critter` can still use a counterattack: alive, on the map, not
// the player's team, not knocked down or out, and fighting.
static bool rt_counter_eligible(Object* critter)
{
    if (!rt_is_known_critter(critter) || !rt_is_alive(critter) || rt_is_player_team(critter)) {
        return false;
    }
    if ((critter->data.critter.combat.results & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) != 0) {
        return false;
    }
    auto it = rt_states.find(critter);
    return it != rt_states.end() && it->second.engaged;
}

// The player's attack `swing` (a melee/unarmed swing, or a shot or throw:
// `ranged`) missed `critter`: it gets the next chance to attack the player
// before anyone else (an attempt, with its own to-hit roll; range, line of
// sight, cooldown and the attack slot still apply). An enemy out of its
// weapon's range or sight moves until it can attack, as it always does. One
// per swing or shot, one per enemy at a time.
static void rt_request_counter(Object* critter, int swing, bool ranged)
{
    if (swing == rt_counter_last_swing) {
        rt_stats.counterSwingRepeats++;
        return;
    }
    if (!rt_counter_eligible(critter)) {
        rt_stats.counterIneligible++;
        rt_log("counter: %s can't counterattack", rt_name(critter));
        return;
    }
    if (rt_is_counter(critter)) {
        rt_stats.counterDuplicates++;
        return;
    }

    rt_counter_last_swing = swing;
    rt_states[critter].target = obj_dude;
    unsigned int window = (unsigned int)(ranged ? rt_rules()->rangedCounterWindowMs : rt_rules()->counterWindowMs);
    rt_counters.push_back({ critter, rt_now, window, ranged });
    rt_stats.counterRequests++;
    if (ranged) {
        rt_stats.counterRangedRequests++;
    }
    rt_stats.lastCounterRequestGrants = rt_stats.playerSlotGrants;
    rt_log("counter: %s gets the next attack after the player's missed %s (%u ms, dist %d)",
        critter_name(critter), ranged ? "shot" : "swing", window, obj_dist(critter, obj_dude));
}

// Drops counterattacks that expired or can't happen any more.
static void rt_update_counters()
{
    for (size_t index = 0; index < rt_counters.size();) {
        RtCounter& counter = rt_counters[index];
        bool expired = rt_now - counter.requestedAt > counter.windowMs;
        if (expired || !rt_counter_eligible(counter.critter)) {
            rt_stats.counterExpired++;
            rt_log("counter: %s %s", rt_is_known_critter(counter.critter) ? critter_name(counter.critter) : "(gone)",
                expired ? "didn't attack in time" : "can no longer counterattack");
            rt_counters.erase(rt_counters.begin() + index);
        } else {
            index++;
        }
    }
}

// The counterattacker that goes first: the oldest one ready to attack the
// player right now (asked for the slot recently).
static Object* rt_counter_ready()
{
    for (const RtCounter& counter : rt_counters) {
        auto it = rt_states.find(counter.critter);
        if (it != rt_states.end() && rt_now - it->second.slotRequestAt <= RT_SLOT_READY_MS) {
            return counter.critter;
        }
    }
    return NULL;
}

// Tests: player attacks made to miss, or to hit for a fixed damage with
// no other effect (see `rt_debug_force_player_misses`).
static int rt_forced_misses = 0;
static int rt_forced_hits = 0;
static int rt_forced_hit_damage = 0;
static int rt_forced_hit_flags = 0;

static void rt_apply_forced_miss(Attack* attack, Object* defender)
{
    if (rt_forced_hits > 0 && attack->attacker == obj_dude) {
        rt_forced_hits--;
        // A rolled miss may have been sent past the target (`check_ranged_miss`).
        attack->defender = defender;
        attack->tile = defender->tile;
        attack->attackerFlags = DAM_HIT;
        attack->attackerDamage = 0;
        attack->defenderFlags = rt_forced_hit_flags;
        attack->defenderDamage = rt_forced_hit_damage;
        attack->defenderKnockback = 0;
        attack->extrasLength = 0;
        return;
    }

    if (rt_forced_misses <= 0 || attack->attacker != obj_dude) {
        return;
    }
    rt_forced_misses--;
    attack->attackerFlags &= ~(DAM_HIT | DAM_CRITICAL | DAM_DEAD);
    attack->attackerDamage = 0;
    attack->defenderFlags = 0;
    attack->defenderDamage = 0;
    attack->extrasLength = 0;
}

void rt_debug_force_player_misses(int count)
{
    rt_forced_misses = count;
}

void rt_debug_force_player_hits(int count, int damage, int flags)
{
    rt_forced_hits = count;
    rt_forced_hit_damage = damage;
    rt_forced_hit_flags = flags;
}

int rt_debug_attacks_on(Object* obj)
{
    int count = 0;
    for (RtPendingAttack* pending : rt_pending) {
        Attack* attack = &(pending->attack);
        bool target = attack->defender == obj;
        for (int index = 0; index < attack->extrasLength; index++) {
            target = target || attack->extras[index] == obj;
        }
        if (target) {
            count++;
        }
    }
    return count;
}

void rt_debug_counter_request(Object* critter, int swing, bool ranged)
{
    rt_request_counter(critter, swing, ranged);
}

int rt_debug_counter_count()
{
    return (int)rt_counters.size();
}

static void rt_counter_started(Object* critter, int hitMode)
{
    for (size_t index = 0; index < rt_counters.size(); index++) {
        if (rt_counters[index].critter == critter) {
            rt_stats.counterAttacks++;
            if (rt_counters[index].ranged) {
                rt_stats.counterRangedAttacks++;
            }
            rt_stats.lastCounterRanged = rt_counters[index].ranged;
            rt_stats.lastCounterHitMode = hitMode;
            rt_stats.lastCounterCritter = critter;
            rt_stats.lastCounterDelayMs = rt_now - rt_counters[index].requestedAt;
            rt_stats.lastCounterStartGrants = rt_stats.playerSlotGrants;
            rt_log("counter: %s counterattacks (%u ms after the miss, hit mode %d, dist %d)",
                critter_name(critter), rt_stats.lastCounterDelayMs, hitMode, obj_dist(critter, obj_dude));
            rt_counters.erase(rt_counters.begin() + index);
            return;
        }
    }
}

static int rt_attack(Object* attacker, Object* defender, int hitMode, int hitLocation)
{
    // An enemy attack on the player doesn't wait for the player's own
    // attack (it may not need the player at all, see below); it waits for
    // another enemy attack on the player.
    bool onPlayer = defender == obj_dude && attacker != obj_dude;
    if (rt_obj_in_attack(attacker) || (onPlayer ? rt_player_under_attack() : rt_obj_in_attack(defender))) {
        return RT_ATTACK_BUSY;
    }

    // Same as `combat_attack`: unarmed attacks are sometimes kicks.
    if (hitMode == HIT_MODE_PUNCH && roll_random(1, 4) == 1) {
        int fid = art_id(OBJ_TYPE_CRITTER, attacker->fid & 0xFFF, ANIM_KICK_LEG, (attacker->fid & 0xF000) >> 12, (attacker->fid & 0x70000000) >> 28);
        if (art_exists(fid)) {
            hitMode = HIT_MODE_KICK;
        }
    }

    RtPendingAttack* pending = new RtPendingAttack();
    Attack* attack = &(pending->attack);
    combat_ctd_init(attack, attacker, defender, hitMode, hitLocation);

    // The player's RT attack carries its timing penalty (0 for anything
    // else: held automatic fire, AIMED SHOT, throws...).
    int timing = attacker == obj_dude ? rt_timing_committing : 0;

    // The hit chance `compute_attack` rolls against (same state, same call).
    int toHit = determine_to_hit(attacker, defender, hitLocation, hitMode);

    // Vanilla to-hit roll, critical and damage calculations.
    int computed = combat_rt_compute_attack(attack);
    if (attacker == obj_dude) {
        rt_timing_committing = 0;
    }
    if (computed == -1) {
        rt_log("attack: compute_attack failed for %s -> %s", critter_name(attacker), critter_name(defender));
        delete pending;
        return -1;
    }
    rt_apply_forced_miss(attack, defender);
    pending->timingPenalty = timing;

    // An ordinary outcome leaves the player out of the enemy's animation; a
    // heavy one (knockdown, death...) needs the player, so it waits for an
    // attack of theirs to end (`action_attack` would fail).
    pending->playerDetached = onPlayer && rt_player_detachable(attack);
    if (onPlayer && attack->defender == obj_dude && !pending->playerDetached && rt_obj_in_attack(obj_dude)) {
        delete pending;
        return RT_ATTACK_BUSY;
    }
    if (pending->playerDetached) {
        pending->released[0] = true;
    }

    // Any object in the attack may still be in another prioritized sequence
    // (e.g. an extra hit by a burst), which makes `action_attack` fail.
    for (int index = 0; index < attack->extrasLength; index++) {
        if (rt_obj_in_attack(attack->extras[index])) {
            delete pending;
            return RT_ATTACK_BUSY;
        }
    }

    // Attacker and targets stop what they were doing (a walk, a reload) for
    // the attack, as they would be standing still in the original turn-based
    // game. Otherwise the attack would only land once they stopped by
    // themselves. A detached player keeps going, and a missed shot sent
    // elsewhere (see `check_ranged_miss`) leaves its intended target alone.
    rt_stop_for_attack(attacker);
    if (!pending->playerDetached) {
        rt_stop_for_attack(attack->defender);
    }
    for (int index = 0; index < attack->extrasLength; index++) {
        rt_stop_for_attack(attack->extras[index]);
    }

    // Before the attack is registered: a throw takes the item from the hand
    // right away.
    bool closeCombat = attacker == obj_dude && rt_is_close_combat(attack->hitMode);

    rt_registering_target = attacker == obj_dude ? defender : NULL;
    rt_registering_detached = pending->playerDetached ? obj_dude : NULL;
    int registered = action_attack(attack);
    rt_registering_target = NULL;
    rt_registering_detached = NULL;
    if (registered == -1) {
        // An object is still in a prioritized animation sequence.
        static unsigned int lastLockLog = 0;
        if (rt_now - lastLockLog >= 1000) {
            lastLockLog = rt_now;
            rt_log("attack sequence failed: %s sprite weapon code %d, attack anim %d",
                critter_name(attacker),
                (attacker->fid & 0xF000) >> 12,
                item_w_anim(attacker, attack->hitMode));
            char description[512];
            Object* objects[2] = { attacker, defender };
            for (Object* obj : objects) {
                if (anim_rt_is_locked(obj, description, sizeof(description))) {
                    rt_log("attack blocked: %s locked by %s", critter_name(obj), description);
                }
            }
        }
        delete pending;
        return RT_ATTACK_BUSY;
    }

    rt_attack_started(pending, hitLocation, toHit);
    if (attacker == obj_dude) {
        rt_note_player_attack_start();
        // An ordinary (not aimed) attack is its own swing or shot (a
        // swing started without a target in reach sets its own, see
        // `rt_swing_hit`).
        if (hitLocation == HIT_LOCATION_UNCALLED) {
            pending->counterSwing = ++rt_swing_serial;
            pending->counterRanged = !closeCombat;
            pending->counterTarget = defender;
        }
    }
    return 0;
}

// Bookkeeping for an attack whose animations were registered: it lands when
// they are over (see `rt_resolve_attacks`).
static void rt_attack_started(RtPendingAttack* pending, int hitLocation, int toHit)
{
    Attack* attack = &(pending->attack);
    Object* attacker = attack->attacker;
    Object* defender = attack->defender;

    if (defender == obj_dude) {
        for (RtPendingAttack* other : rt_pending) {
            if (other->attack.defender == obj_dude && other->attack.attacker != attacker) {
                rt_stats.playerSlotOverlaps++;
            }
        }
    }

    if (attacker != obj_dude && defender == obj_dude) {
        if (toHit <= 0) {
            rt_stats.aiHopelessAttacks++;
        }
        if (rt_stats.aiLowestToHit == 0 || toHit < rt_stats.aiLowestToHit) {
            rt_stats.aiLowestToHit = toHit;
        }
        if (pending->playerDetached) {
            rt_stats.playerFreeAttacks++;
            if ((attack->attackerFlags & DAM_HIT) != 0) {
                rt_stats.playerFreeHits++;
            }
        } else {
            rt_stats.playerHeldAttacks++;
        }
        bool playerAttacking = rt_swing.active;
        for (RtPendingAttack* other : rt_pending) {
            playerAttacking = playerAttacking || other->attack.attacker == obj_dude;
        }
        if (playerAttacking) {
            rt_stats.enemyAttacksDuringPlayerAttack++;
        }
    }

    if (attacker == obj_dude && pending->timingPenalty > 0) {
        rt_stats.timingPenalized++;
    }
    if (attacker == obj_dude && hitLocation == HIT_LOCATION_UNCALLED) {
        rt_stats.lastTimingToHit = toHit;
    }

    rt_note_critical_deaths(pending);
    pending->startedAt = rt_now;
    rt_pending.push_back(pending);

    rt_stats.attacksStarted++;
    if (attacker == obj_dude) {
        rt_stats.playerAttacks++;
        critter_set_who_hit_me(attacker, defender);

        if (hitLocation != HIT_LOCATION_UNCALLED) {
            rt_stats.aimedAttacks++;
            rt_stats.lastAimedLocation = hitLocation;
            rt_stats.lastAimedToHit = toHit;
            rt_stats.lastAimedFlags = attack->defenderFlags;
            rt_stats.lastAimedDefender = defender;
            if ((attack->attackerFlags & DAM_HIT) != 0) {
                rt_stats.aimedHits++;
            }
            if ((attack->attackerFlags & DAM_CRITICAL) != 0) {
                rt_stats.aimedCriticals++;
            }
        }

        // Attacking someone who wasn't fighting (a neutral NPC) makes them and
        // their nearby teammates hostile, hit or miss, as starting combat does
        // in the original game. Locking on alone never gets here.
        if (FID_TYPE(defender->fid) == OBJ_TYPE_CRITTER && !rt_is_hostile_to_player(defender)) {
            rt_engage(defender, obj_dude, "attacked by player");
        }

        switch (item_w_anim(attacker, attack->hitMode)) {
        case ANIM_THROW_PUNCH:
        case ANIM_KICK_LEG:
            rt_stats.playerUnarmed++;
            break;
        case ANIM_SWING_ANIM:
        case ANIM_THRUST_ANIM:
            rt_stats.playerMelee++;
            break;
        case ANIM_FIRE_SINGLE:
            rt_stats.playerSingle++;
            break;
        case ANIM_FIRE_BURST:
        case ANIM_FIRE_CONTINUOUS:
            rt_stats.playerBurst++;
            break;
        case ANIM_THROW_ANIM:
            rt_stats.playerThrow++;
            break;
        default:
            rt_stats.playerOther++;
            break;
        }
    } else if (rt_is_player_team(attacker)) {
        rt_stats.companionAttacks++;
    }

    // Concurrency diagnostics: attacks by different attackers in flight.
    std::unordered_set<Object*> attackers;
    for (RtPendingAttack* other : rt_pending) {
        attackers.insert(other->attack.attacker);
    }
    if ((int)rt_pending.size() > rt_stats.maxConcurrentAttacks) {
        rt_stats.maxConcurrentAttacks = (int)rt_pending.size();
    }
    if ((int)attackers.size() > rt_stats.maxConcurrentAttackers) {
        rt_stats.maxConcurrentAttackers = (int)attackers.size();
    }

    rt_log("attack: %s -> %s (hitMode=%d, anim=%d, rounds=%d, dist=%d, location=%d%s) %s%s dmg=%d flags=0x%X to-hit=%d%s%s",
        critter_name(attacker),
        rt_name(attack->defender),
        attack->hitMode,
        item_w_anim(attacker, attack->hitMode),
        attack->ammoQuantity,
        obj_dist(attacker, defender),
        attack->defenderHitLocation,
        hitLocation != HIT_LOCATION_UNCALLED ? " aimed" : "",
        (attack->attackerFlags & DAM_HIT) != 0 ? "HIT" : "MISS",
        (attack->attackerFlags & DAM_CRITICAL) != 0 ? " CRITICAL" : "",
        attack->defenderDamage,
        attack->defenderFlags,
        toHit,
        pending->playerDetached ? " (player not held)" : "",
        pending->timingPenalty > 0 ? " (timing penalty)" : "");
}

// -----------------------------------------------------------------------------
// Melee and unarmed swings without a target in reach
// -----------------------------------------------------------------------------

// Whether the hex rotation from the player to `obj` is within `maxSteps` of
// the way the player faces (0: straight ahead, 1: the front arc).
static bool rt_swing_in_front(Object* obj, int maxSteps)
{
    if (obj->tile == obj_dude->tile) {
        return false;
    }
    int diff = abs(tile_dir(obj_dude->tile, obj->tile) - obj_dude->rotation);
    diff = std::min(diff, 6 - diff);
    return diff <= maxSteps;
}

// Who a swing can hit: a living critter (not the player's team) within the
// attack's reach (Bonus HtH Attacks included, not through walls) and in
// front of the player: an enemy anywhere in the front arc, anyone else only
// straight ahead. The lock comes first, then the closest, then the best
// aligned.
static Object* rt_find_swing_target(int hitMode)
{
    if (rt_lock != NULL
        && rt_is_known_critter(rt_lock)
        && rt_lock->elevation == obj_dude->elevation
        && rt_check_shot(obj_dude, rt_lock, hitMode) == COMBAT_BAD_SHOT_OK
        && rt_swing_in_front(rt_lock, 1)) {
        return rt_lock;
    }

    Object* best = NULL;
    int bestScore = 0;
    for (Object* critter : rt_critters) {
        if (critter == obj_dude
            || critter->elevation != obj_dude->elevation
            || (critter->flags & OBJECT_HIDDEN) != 0
            || rt_is_player_team(critter)
            || !rt_is_alive(critter)) {
            continue;
        }

        bool hostile = rt_is_hostile_to_player(critter);
        if (!rt_swing_in_front(critter, hostile ? 1 : 0)) {
            continue;
        }

        if (rt_check_shot(obj_dude, critter, hitMode) != COMBAT_BAD_SHOT_OK) {
            continue;
        }

        int score = obj_dist(obj_dude, critter) * 10 + (rt_swing_in_front(critter, 0) ? 0 : 1);
        if (best == NULL || score < bestScore) {
            best = critter;
            bestScore = score;
        }
    }
    return best;
}

// Two-handed melee weapons sweep: a swing can hit several targets in its
// arc. Decided by the weapon data: the two-handed flag (WEAPON_TWO_HAND)
// and a melee attack in the hit mode used, so two-handed firearms, thrown
// attacks (the spear's throw), one-handed melee and unarmed attacks never
// sweep.
bool rt_weapon_sweeps(Object* weapon, int hitMode)
{
    return weapon != NULL
        && item_get_type(weapon) == ITEM_TYPE_WEAPON
        && item_w_is_2handed(weapon)
        && item_w_subtype(weapon, hitMode) == ATTACK_TYPE_MELEE;
}

static bool rt_is_sweep(int hitMode)
{
    return rt_weapon_sweeps(item_hit_with(obj_dude, hitMode), hitMode);
}

// Hex centers on a plane where neighbors are 1 apart and 60 degrees from
// each other (as `rt_tile_distance_exact`).
static void rt_hex_point(int tile, double* x, double* y)
{
    int column = tile % HEX_GRID_WIDTH;
    *x = column * 0.8660254037844386;
    *y = tile / HEX_GRID_WIDTH + ((column & 1) == 0 ? 0.5 : 0.0);
}

// Whether `obj` is within the sweep arc (`melee_sweep_arc_deg`, centered on
// the way the player faces).
static bool rt_in_sweep_arc(Object* obj)
{
    int ahead = tile_num_in_direction(obj_dude->tile, obj_dude->rotation, 1);
    if (ahead == -1 || obj->tile == obj_dude->tile) {
        return false;
    }

    double px;
    double py;
    double ax;
    double ay;
    double ox;
    double oy;
    rt_hex_point(obj_dude->tile, &px, &py);
    rt_hex_point(ahead, &ax, &ay);
    rt_hex_point(obj->tile, &ox, &oy);
    double fx = ax - px;
    double fy = ay - py;
    double tx = ox - px;
    double ty = oy - py;
    double length = sqrt(fx * fx + fy * fy) * sqrt(tx * tx + ty * ty);
    if (length <= 0.0) {
        return false;
    }
    double halfArc = std::clamp(rt_rules()->sweepArcDeg, 0, 360) / 2.0 * 3.14159265358979 / 180.0;
    return (fx * tx + fy * ty) / length >= cos(halfArc) - 1e-6;
}

// Who a sweep hits at its impact: living critters (not the player's team)
// on the player's elevation, in the arc, within the weapon's reach (Bonus
// HtH Attacks included, never through walls, see `rt_check_shot`). Enemies
// anywhere in the arc; a bystander only when locked or straight ahead, as
// for one-handed swings. The aim first, then the closest, then the best
// aligned; at most `capacity`.
static int rt_find_sweep_targets(Object* aim, int hitMode, Object** out, int capacity)
{
    std::vector<std::pair<int, Object*>> found;
    for (Object* critter : rt_critters) {
        if (critter == obj_dude
            || critter->elevation != obj_dude->elevation
            || (critter->flags & OBJECT_HIDDEN) != 0
            || rt_is_player_team(critter)
            || !rt_is_alive(critter)
            || !rt_in_sweep_arc(critter)) {
            continue;
        }

        bool chosen = critter == aim || critter == rt_lock;
        if (!chosen && !rt_is_hostile_to_player(critter) && !rt_swing_in_front(critter, 0)) {
            continue;
        }

        if (rt_check_shot(obj_dude, critter, hitMode) != COMBAT_BAD_SHOT_OK) {
            continue;
        }

        // A sweep never reaches through walls or scenery, even within the
        // weapon's own reach (the original melee check only looks at the
        // distance).
        if (obj_dist(obj_dude, critter) > 1
            && combat_is_shot_blocked(obj_dude, obj_dude->tile, critter->tile, critter, NULL)) {
            continue;
        }

        int score = (critter == aim ? 0 : 1000) + obj_dist(obj_dude, critter) * 10 + (rt_swing_in_front(critter, 0) ? 0 : 1);
        found.push_back({ score, critter });
    }

    std::stable_sort(found.begin(), found.end(), [](const std::pair<int, Object*>& a, const std::pair<int, Object*>& b) {
        return a.first < b.first;
    });

    int count = 0;
    for (const auto& entry : found) {
        if (count >= capacity) {
            break;
        }
        out[count++] = entry.second;
    }
    return count;
}

static int rt_swing_impact(void* a1, void* a2)
{
    rt_swing.impactDue = true;
    rt_swing.impactAt = rt_now;
    return 0;
}

// RT with a melee weapon or unarmed and no locked target in reach: one swing
// toward the way the player faces (turning to an enemy in the front arc).
// Whether it hits anyone is decided at its impact frame.
static int rt_player_swing(int hitMode)
{
    if (rt_swing.active || rt_obj_in_attack(obj_dude) || rt_now < rt_dude_next_attack_at) {
        return RT_PLAYER_ATTACK_WAIT;
    }

    Object* weapon = item_hit_with(obj_dude, hitMode);
    if (weapon != NULL) {
        int results = obj_dude->data.critter.combat.results;
        if ((results & DAM_CRIP_ARM_LEFT) != 0 && (results & DAM_CRIP_ARM_RIGHT) != 0) {
            rt_print("You can't use that weapon with crippled arms.");
            return RT_PLAYER_ATTACK_FAILED;
        }
        if ((results & DAM_CRIP_ARM_ANY) != 0 && item_w_is_2handed(weapon)) {
            rt_print("You can't use that weapon with crippled arms.");
            return RT_PLAYER_ATTACK_FAILED;
        }
    }

    // Melee and unarmed attacks cost AP, empty swings included.
    if (!rt_attack_ap_ok(hitMode)) {
        return RT_PLAYER_ATTACK_FAILED;
    }
    int costHitMode = hitMode;

    // The sprite must show the weapon in hand first (right after a swap it
    // may still hold the previous one).
    int code = weapon != NULL ? item_w_anim_code(weapon) : 0;
    if (((obj_dude->fid & 0xF000) >> 12) != code) {
        rt_sync_weapon_sprite();
        if (((obj_dude->fid & 0xF000) >> 12) != code) {
            return RT_PLAYER_ATTACK_WAIT;
        }
    }

    // Same as `rt_attack`: unarmed attacks are sometimes kicks.
    if (hitMode == HIT_MODE_PUNCH && roll_random(1, 4) == 1) {
        int kickFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_KICK_LEG, (obj_dude->fid & 0xF000) >> 12, obj_dude->rotation + 1);
        if (art_exists(kickFid)) {
            hitMode = HIT_MODE_KICK;
        }
    }

    int anim = item_w_anim(obj_dude, hitMode);
    int fid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, anim, (obj_dude->fid & 0xF000) >> 12, obj_dude->rotation + 1);
    CacheEntry* handle;
    Art* art = art_ptr_lock(fid, &handle);
    if (art == NULL) {
        rt_log("swing: no attack art (anim %d, weapon code %d)", anim, (obj_dude->fid & 0xF000) >> 12);
        return RT_PLAYER_ATTACK_FAILED;
    }
    int actionFrame = art_frame_action_frame(art);
    art_ptr_unlock(handle);

    // Turn to the enemy the swing aims at (only enemies in the front arc:
    // the player never turns toward a bystander).
    bool sweep = rt_is_sweep(hitMode);
    Object* aim = rt_find_swing_target(hitMode);
    int turnTile = -1;
    if (sweep && rt_lock != NULL && rt_is_known_critter(rt_lock)
        && rt_lock->elevation == obj_dude->elevation
        && rt_check_shot(obj_dude, rt_lock, hitMode) == COMBAT_BAD_SHOT_OK) {
        // A sweep with a locked target in reach turns to face it, wherever
        // it stands: the arc is centered on it.
        aim = rt_lock;
        if (!rt_swing_in_front(rt_lock, 0)) {
            turnTile = rt_lock->tile;
        }
    } else if (aim != NULL && rt_is_hostile_to_player(aim) && !rt_swing_in_front(aim, 0)) {
        turnTile = aim->tile;
    } else if (aim == NULL && rt_lock != NULL && rt_is_hostile_to_player(rt_lock)
        && rt_lock->elevation == obj_dude->elevation && rt_swing_in_front(rt_lock, 1)) {
        turnTile = rt_lock->tile;
    }

    rt_stop_for_attack(obj_dude);
    rt_move_active = false;

    const char* sfx;
    if (anim != ANIM_THROW_PUNCH && anim != ANIM_KICK_LEG) {
        sfx = gsnd_build_weapon_sfx_name(WEAPON_SOUND_EFFECT_ATTACK, weapon, hitMode, NULL);
    } else {
        sfx = gsnd_build_character_sfx_name(obj_dude, anim, CHARACTER_SOUND_EFFECT_UNUSED);
    }
    char sfxName[16];
    SDL_strlcpy(sfxName, sfx != NULL ? sfx : "", sizeof(sfxName));

    if (register_clear(obj_dude) == -2) {
        return RT_PLAYER_ATTACK_WAIT;
    }

    register_begin(ANIMATION_REQUEST_RESERVED);
    register_priority(1);
    if (turnTile != -1) {
        register_object_turn_towards(obj_dude, turnTile);
    }
    register_object_play_sfx(obj_dude, sfxName, -1);
    register_object_animate(obj_dude, anim, 0);
    register_object_call(obj_dude, NULL, rt_swing_impact, actionFrame);
    if (register_end() == -1) {
        return RT_PLAYER_ATTACK_WAIT;
    }

    rt_swing.active = true;
    rt_swing.hitMode = hitMode;
    rt_swing.anim = anim;
    rt_swing.aim = aim;
    rt_swing.impactDue = false;
    rt_swing.resolved = false;
    rt_swing.hit = false;
    rt_swing.startedAt = rt_now;
    rt_swing.impactAt = 0;
    rt_swing.serial = ++rt_swing_serial;
    rt_swing.sweep = sweep;
    rt_swing.sweepDoneCount = 0;
    rt_swing.timingPenalty = rt_timing_committing;
    if (sweep) {
        rt_stats.sweeps++;
    }

    // Spent once, now that the swing is committed (hit, miss or nothing).
    rt_spend_attack_ap(rt_attack_ap_cost(obj_dude, costHitMode, false), false);

    rt_stats.freeSwings++;
    rt_stats.lastSwingAnim = anim;
    rt_note_player_attack_start();
    rt_log("swing: %s (anim %d) toward rotation %d, aiming at %s",
        hitMode == HIT_MODE_KICK ? "kick" : "attack", anim, obj_dude->rotation, aim != NULL ? rt_name(aim) : "nobody");
    return RT_PLAYER_ATTACK_STARTED;
}

// The swing's hit at its impact frame: an ordinary attack attempt (vanilla
// to-hit roll, damage, armor and criticals), landing when the reactions are
// over. Returns 0, RT_ATTACK_BUSY or -1.
static int rt_swing_hit(Object* defender)
{
    if (rt_obj_in_attack(defender)) {
        return RT_ATTACK_BUSY;
    }

    // A swing reaches each target once.
    for (int index = 0; index < rt_swing.sweepDoneCount; index++) {
        if (rt_swing.sweepDone[index] == defender) {
            rt_stats.sweepRepeats++;
            return -1;
        }
    }

    RtPendingAttack* pending = new RtPendingAttack();
    Attack* attack = &(pending->attack);
    combat_ctd_init(attack, obj_dude, defender, rt_swing.hitMode, HIT_LOCATION_UNCALLED);
    // The swing's press timing penalty (taken when it started).
    rt_timing_committing = rt_swing.timingPenalty;
    int toHit = determine_to_hit(obj_dude, defender, HIT_LOCATION_UNCALLED, rt_swing.hitMode);
    int computed = combat_rt_compute_attack(attack);
    rt_timing_committing = 0;

    if (computed == -1) {
        rt_log("swing: compute_attack failed on %s", rt_name(defender));
        delete pending;
        return -1;
    }
    rt_apply_forced_miss(attack, defender);
    pending->timingPenalty = rt_swing.timingPenalty;

    rt_stop_for_attack(defender);
    rt_registering_target = defender;
    int registered = action_rt_melee_impact(attack, rt_swing.anim);
    rt_registering_target = NULL;
    if (registered == -1) {
        delete pending;
        return RT_ATTACK_BUSY;
    }

    rt_stats.lastSwingToHit = toHit;
    rt_stats.lastSwingDefender = defender;
    pending->counterSwing = rt_swing.serial;
    rt_attack_started(pending, HIT_LOCATION_UNCALLED, toHit);
    return 0;
}

// Resolves the swing at its impact frame (the target is checked again
// there: still alive, in reach and in front) and ends it with the
// animation.
static void rt_update_swing()
{
    if (!rt_swing.active) {
        return;
    }

    if (rt_swing.impactDue && !rt_swing.resolved && rt_swing.sweep) {
        // Two-handed sweep: every target in the arc and in reach, each with
        // its own attack (to-hit, armor, damage, criticals), at most once.
        // Targets busy with another attack are tried again until the wait
        // ends.
        Object* aim = rt_swing.aim != NULL && rt_is_known_critter(rt_swing.aim) ? rt_swing.aim : NULL;
        int maxTargets = std::clamp(rt_rules()->sweepMaxTargets, 1, 8);
        Object* targets[8];
        int count = rt_find_sweep_targets(aim, rt_swing.hitMode, targets, 8);
        bool waiting = false;
        for (int index = 0; index < count && rt_swing.sweepDoneCount < maxTargets; index++) {
            Object* target = targets[index];
            bool done = false;
            for (int other = 0; other < rt_swing.sweepDoneCount; other++) {
                done = done || rt_swing.sweepDone[other] == target;
            }
            if (done) {
                continue;
            }
            int rc = rt_swing_hit(target);
            if (rc == 0) {
                rt_swing.sweepDone[rt_swing.sweepDoneCount++] = target;
                rt_stats.sweepTargets++;
            } else if (rc == RT_ATTACK_BUSY) {
                waiting = true;
            }
        }

        if (!waiting || rt_now - rt_swing.impactAt > RT_SWING_IMPACT_WAIT_MS) {
            rt_swing.resolved = true;
            rt_swing.hit = rt_swing.sweepDoneCount > 0;
            if (rt_swing.hit) {
                rt_stats.freeSwingAttempts++;
            } else {
                rt_stats.freeSwingWhiffs++;
            }
            rt_stats.lastSweepCount = rt_swing.sweepDoneCount;
            for (int index = 0; index < 8; index++) {
                rt_stats.lastSweepTargets[index] = index < rt_swing.sweepDoneCount ? rt_swing.sweepDone[index] : NULL;
            }
            rt_stats.sweepMaxTargets = std::max(rt_stats.sweepMaxTargets, rt_swing.sweepDoneCount);
            char tiles[96] = "";
            for (int index = 0; index < rt_swing.sweepDoneCount; index++) {
                char part[16];
                snprintf(part, sizeof(part), " %d", rt_swing.sweepDone[index] != NULL ? rt_swing.sweepDone[index]->tile : -1);
                SDL_strlcat(tiles, part, sizeof(tiles));
            }
            rt_log("sweep: %d target(s) reached, at tiles%s (player at %d, facing %d)", rt_swing.sweepDoneCount, tiles, obj_dude->tile, obj_dude->rotation);
        }
    } else if (rt_swing.impactDue && !rt_swing.resolved) {
        Object* target = NULL;
        Object* aim = rt_swing.aim;
        if (aim != NULL
            && rt_is_known_critter(aim)
            && aim->elevation == obj_dude->elevation
            && rt_check_shot(obj_dude, aim, rt_swing.hitMode) == COMBAT_BAD_SHOT_OK
            && rt_swing_in_front(aim, 1)) {
            target = aim;
        } else {
            target = rt_find_swing_target(rt_swing.hitMode);
        }

        if (target == NULL) {
            rt_swing.resolved = true;
            rt_stats.freeSwingWhiffs++;
            rt_log("swing: nobody in reach at the impact%s", aim != NULL ? " (the target moved away)" : "");
        } else {
            int rc = rt_swing_hit(target);
            if (rc == 0) {
                rt_swing.resolved = true;
                rt_swing.hit = true;
                rt_stats.freeSwingAttempts++;
            } else if (rc == -1 || rt_now - rt_swing.impactAt > RT_SWING_IMPACT_WAIT_MS) {
                // The target stayed busy with another attack: the swing
                // passes without contact.
                rt_swing.resolved = true;
                rt_stats.freeSwingWhiffs++;
                rt_log("swing: %s busy at the impact, no contact", rt_name(target));
            }
        }
    }

    bool animDone = !anim_busy(obj_dude);
    bool timedOut = rt_now - rt_swing.startedAt > RT_PENDING_ATTACK_TIMEOUT;
    if ((animDone && (rt_swing.resolved || !rt_swing.impactDue)) || timedOut) {
        if (!rt_swing.impactDue) {
            rt_log("swing: interrupted before the impact");
        }
        rt_swing.active = false;
        rt_swing.aim = NULL;
        if (!rt_swing.hit) {
            // A hit sets the cooldown when it lands (see `rt_resolve_attacks`).
            rt_dude_next_attack_at = rt_now + rt_fast_shot_interval(item_w_mp_cost(obj_dude, rt_swing.hitMode, false) * rt_ms_per_ap);
        }
    }
}

static void rt_stop_for_attack(Object* obj)
{
    if (obj == NULL || FID_TYPE(obj->fid) != OBJ_TYPE_CRITTER || !anim_busy(obj)) {
        return;
    }

    // Only walks, runs and the player's reload; anything else (standing up,
    // dying) plays out.
    int anim = FID_ANIM_TYPE(obj->fid);
    bool reloading = obj == obj_dude && rt_reload_active;
    if (anim != ANIM_WALK && anim != ANIM_RUNNING && !reloading) {
        return;
    }

    // Only its own sequences: an attack it is already part of keeps playing.
    if (anim_rt_end_unprioritized(obj) == 0) {
        return;
    }

    if (obj == obj_dude) {
        rt_move_active = false;
    } else {
        auto it = rt_states.find(obj);
        if (it != rt_states.end()) {
            it->second.moving = false;
        }
    }
}

// The vanilla attack calculation decides death up front: a hit that takes
// the target to 0 hit points gets DAM_DEAD (see `check_for_death`), and
// applying the damage later never kills by itself. In real time the hit lands
// when its animation finishes, so hit points can change in between (healing,
// another attack landing first). Deaths caused by the damage are therefore
// re-decided when the hit lands; deaths from critical hit effects stand.
static bool rt_death_by_damage(Object* obj, int damage)
{
    return obj != NULL && damage > 0 && critter_get_hits(obj) - damage <= 0;
}

static void rt_note_critical_deaths(RtPendingAttack* pending)
{
    Attack* attack = &(pending->attack);
    pending->criticalDeath[0] = (attack->attackerFlags & DAM_DEAD) != 0 && !rt_death_by_damage(attack->attacker, attack->attackerDamage);
    pending->criticalDeath[1] = (attack->defenderFlags & DAM_DEAD) != 0 && !rt_death_by_damage(attack->defender, attack->defenderDamage);
    for (int index = 0; index < attack->extrasLength; index++) {
        pending->criticalDeath[2 + index] = (attack->extrasFlags[index] & DAM_DEAD) != 0 && !rt_death_by_damage(attack->extras[index], attack->extrasDamage[index]);
    }
}

static void rt_refresh_death_flag(Object* obj, int damage, int* flags, bool criticalDeath)
{
    // Same exclusions as `check_for_death`.
    if (obj == NULL || FID_TYPE(obj->fid) != OBJ_TYPE_CRITTER || obj->pid == 0x1000008) {
        return;
    }

    int before = *flags;
    *flags &= ~DAM_DEAD;
    if (criticalDeath || rt_death_by_damage(obj, damage)) {
        *flags |= DAM_DEAD;
    }

    if (*flags != before) {
        rt_log("death re-decided for %s: %s (hp %d, damage %d)",
            critter_name(obj), (*flags & DAM_DEAD) != 0 ? "dies" : "survives", critter_get_hits(obj), damage);
    }
}

static void rt_refresh_death_flags(RtPendingAttack* pending)
{
    Attack* attack = &(pending->attack);
    rt_refresh_death_flag(attack->attacker, attack->attackerDamage, &(attack->attackerFlags), pending->criticalDeath[0]);
    rt_refresh_death_flag(attack->defender, attack->defenderDamage, &(attack->defenderFlags), pending->criticalDeath[1]);
    for (int index = 0; index < attack->extrasLength; index++) {
        rt_refresh_death_flag(attack->extras[index], attack->extrasDamage[index], &(attack->extrasFlags[index]), pending->criticalDeath[2 + index]);
    }
}

// Whether a target may move again once its animations for the attack are
// over, before the attack lands: only after an ordinary reaction (a hit, a
// dodge or none). Deaths, knockdowns, knockouts, knockback and burning keep
// it until the attack lands (their effects apply then).
static bool rt_may_release(Object* attacker, Object* target, int flags, int knockback)
{
    return target != obj_dude
        && target != attacker
        && rt_is_known_critter(target)
        && (flags & (DAM_DEAD | DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN | DAM_ON_FIRE)) == 0
        && knockback == 0
        && !critter_is_prone(target);
}

// A target whose reaction is over isn't kept standing until the attacker
// finishes its own animation (e.g. the rest of the player's swing): it may
// walk again at once. It can't attack before the attack lands, and the
// damage is applied when it lands, as before.
static void rt_release_targets(RtPendingAttack* pending)
{
    Attack* attack = &(pending->attack);
    Object* targets[1 + EXPLOSION_TARGET_COUNT];
    int flags[1 + EXPLOSION_TARGET_COUNT];
    int knockbacks[1 + EXPLOSION_TARGET_COUNT];
    int count = 0;
    targets[count] = attack->defender;
    flags[count] = attack->defenderFlags;
    knockbacks[count] = attack->defenderKnockback;
    count++;
    for (int index = 0; index < attack->extrasLength && count < 1 + EXPLOSION_TARGET_COUNT; index++) {
        targets[count] = attack->extras[index];
        flags[count] = attack->extrasFlags[index];
        knockbacks[count] = attack->extrasKnockback[index];
        count++;
    }

    for (int index = 0; index < count; index++) {
        // Its reaction may be part of the attacker's sequence (an attack on a
        // target in reach): once played, it is detached from it.
        if (pending->released[index]
            || !rt_may_release(attack->attacker, targets[index], flags[index], knockbacks[index])
            || !anim_rt_detach_finished(targets[index])) {
            continue;
        }

        pending->released[index] = true;
        // Only an actual wait (the attacker still busy) counts.
        if (anim_busy(attack->attacker)) {
            rt_stats.reactionReleases++;
            rt_stats.lastReleaseCritter = targets[index];
            rt_stats.lastReleaseAt = rt_now;
            rt_stats.lastReleaseDelayMs = rt_now - pending->startedAt;
            rt_log("reaction over: %s may move again (%u ms after the attack started, %s still busy)",
                critter_name(targets[index]), rt_now - pending->startedAt, critter_name(attack->attacker));
        }
    }
}

static void rt_resolve_attacks()
{
    for (size_t index = 0; index < rt_pending.size();) {
        RtPendingAttack* pending = rt_pending[index];
        Attack* attack = &(pending->attack);

        // A missed shot can be redirected to a non-critter obstacle (see
        // `check_ranged_miss`), so only critter participants are validated.
        bool defenderValid = FID_TYPE(attack->defender->fid) != OBJ_TYPE_CRITTER || rt_is_known_critter(attack->defender);
        if (!rt_is_known_critter(attack->attacker) || !defenderValid) {
            rt_log("attack dropped: participant left the map");
            rt_stats.attacksDropped++;
            delete pending;
            rt_pending.erase(rt_pending.begin() + index);
            continue;
        }

        rt_release_targets(pending);

        // A target that started walking meanwhile (e.g. moved by its script)
        // stops to take the hit; one whose reaction is over may walk again.
        if (!pending->released[0]) {
            rt_stop_for_attack(attack->defender);
        }
        for (int extra = 0; extra < attack->extrasLength; extra++) {
            if (!pending->released[1 + extra] && rt_is_known_critter(attack->extras[extra])) {
                rt_stop_for_attack(attack->extras[extra]);
            }
        }

        bool busy = anim_busy(attack->attacker) || (!pending->released[0] && anim_busy(attack->defender));
        for (int extra = 0; extra < attack->extrasLength && !busy; extra++) {
            busy = !pending->released[1 + extra] && rt_is_known_critter(attack->extras[extra]) && anim_busy(attack->extras[extra]);
        }

        if (busy && !pending->slowLogged && rt_now - pending->startedAt > 2500) {
            // Diagnostics: which participant keeps the attack from resolving.
            pending->slowLogged = true;
            rt_log("attack slow: %s -> %s, attacker busy=%d (anim %d), defender busy=%d (anim %d)",
                critter_name(attack->attacker), rt_name(attack->defender),
                anim_busy(attack->attacker) != 0, FID_ANIM_TYPE(attack->attacker->fid),
                anim_busy(attack->defender) != 0, FID_ANIM_TYPE(attack->defender->fid));
            for (int extra = 0; extra < attack->extrasLength; extra++) {
                Object* obj = attack->extras[extra];
                rt_log("attack slow: extra %s busy=%d (anim %d, dead %d)", rt_name(obj),
                    rt_is_known_critter(obj) && anim_busy(obj) != 0, FID_ANIM_TYPE(obj->fid), !rt_is_alive(obj));
            }
        }

        if (busy && rt_now - pending->startedAt < RT_PENDING_ATTACK_TIMEOUT) {
            index++;
            continue;
        }

        // Mirrors `combat_anim_finished`, which only runs in turn-based mode.
        Object* weapon = item_hit_with(attack->attacker, attack->hitMode);
        if (weapon != NULL && item_w_max_ammo(weapon) > 0) {
            int before = item_w_curr_ammo(weapon);
            int ammo = before - attack->ammoQuantity;
            item_w_set_curr_ammo(weapon, ammo < 0 ? 0 : ammo);
            if (attack->attacker == obj_dude) {
                intface_update_ammo_lights();
                rt_stats.lastPlayerAmmoUsed = before - item_w_curr_ammo(weapon);
                rt_stats.lastPlayerRounds = attack->ammoQuantity;
            }
        }

        bool defenderWasAlive = rt_is_alive(attack->defender);
        int defenderHpBefore = rt_hits(attack->defender);

        // Death from damage depends on the hit points when the hit lands.
        rt_refresh_death_flags(pending);

        // A released target the hit now kills (it was hurt meanwhile) stops
        // walking first.
        if (pending->released[0] && (attack->defenderFlags & DAM_DEAD) != 0) {
            rt_stop_for_attack(attack->defender);
        }
        for (int extra = 0; extra < attack->extrasLength; extra++) {
            if (pending->released[1 + extra] && (attack->extrasFlags[extra] & DAM_DEAD) != 0) {
                rt_stop_for_attack(attack->extras[extra]);
            }
        }

        combat_display(attack);
        apply_damage(attack, true);

        // Each hit's damage must be applied exactly once.
        if (defenderWasAlive && FID_TYPE(attack->defender->fid) == OBJ_TYPE_CRITTER && attack->defender != attack->attacker) {
            int expected = attack->defenderDamage > 0 ? attack->defenderDamage : 0;
            int actual = defenderHpBefore - rt_hits(attack->defender);
            if (actual != expected) {
                rt_stats.damageMismatches++;
                rt_log("damage mismatch: expected %d, applied %d", expected, actual);
            }
        }

        rt_stats.attacksResolved++;
        if ((attack->attackerFlags & DAM_HIT) != 0) {
            rt_stats.hits++;
            if (attack->attacker == obj_dude) {
                rt_stats.playerHits++;
            }
        } else {
            rt_stats.misses++;
        }

        if (attack->defender == obj_dude) {
            rt_stats.damageToPlayer += defenderHpBefore - critter_get_hits(obj_dude);
            // Recovery window: the next enemy hit can't start right away, so the
            // player is never stun-locked by several attackers in a row.
            rt_player_recovery_until = rt_now + RT_PLAYER_RECOVERY_MS;
        }

        if (defenderWasAlive && !rt_is_alive(attack->defender)) {
            rt_stats.kills++;
            rt_log("kill: %s killed %s", critter_name(attack->attacker), rt_name(attack->defender));
        }

        rt_log("resolved: %s -> %s, defender hp %d -> %d",
            critter_name(attack->attacker),
            rt_name(attack->defender),
            defenderHpBefore,
            rt_hits(attack->defender));

        if (combat_exps > 0) {
            combat_give_exps(combat_exps);
            combat_exps = 0;
        }

        // A missed ordinary attack of the player's (a swing, a shot, a
        // burst none of whose rounds hit, a throw whose blast spared the
        // target): that enemy may counterattack first.
        Object* intended = pending->counterTarget != NULL ? pending->counterTarget : attack->defender;
        if (pending->counterSwing != 0 && (attack->attackerFlags & DAM_HIT) == 0
            && rt_is_known_critter(intended)) {
            bool struck = false;
            for (int extra = 0; extra < attack->extrasLength; extra++) {
                if (attack->extras[extra] == intended && attack->extrasDamage[extra] > 0) {
                    struck = true;
                }
            }
            if (!struck) {
                rt_request_counter(intended, pending->counterSwing, pending->counterRanged);
            }
        }

        if (attack->attacker == obj_dude) {
            int hitMode = attack->hitMode;
            rt_dude_next_attack_at = rt_now + rt_fast_shot_interval(item_w_mp_cost(obj_dude, hitMode, false) * rt_ms_per_ap);
        }

        delete pending;
        rt_pending.erase(rt_pending.begin() + index);
    }
}

static int rt_check_shot(Object* attacker, Object* defender, int hitMode)
{
    // Same as `combat_check_bad_shot` minus the action point requirement.
    if (!rt_is_alive(defender)) {
        return COMBAT_BAD_SHOT_ALREADY_DEAD;
    }

    Object* weapon = item_hit_with(attacker, hitMode);
    if (weapon != NULL) {
        if ((attacker->data.critter.combat.results & DAM_CRIP_ARM_LEFT) != 0
            && (attacker->data.critter.combat.results & DAM_CRIP_ARM_RIGHT) != 0) {
            return COMBAT_BAD_SHOT_BOTH_ARMS_CRIPPLED;
        }

        if ((attacker->data.critter.combat.results & DAM_CRIP_ARM_ANY) != 0 && item_w_is_2handed(weapon)) {
            return COMBAT_BAD_SHOT_ARM_CRIPPLED;
        }
    }

    // Reach includes Bonus HtH Attacks (fractional, blocked by walls).
    int reach = combat_rt_check_reach(attacker, defender, hitMode);
    if (reach != COMBAT_BAD_SHOT_OK) {
        return reach;
    }

    if (item_w_max_ammo(weapon) > 0 && item_w_curr_ammo(weapon) == 0) {
        return COMBAT_BAD_SHOT_NO_AMMO;
    }

    int attackType = item_w_subtype(weapon, hitMode);
    if (attackType == ATTACK_TYPE_RANGED || attackType == ATTACK_TYPE_THROW) {
        if (combat_is_shot_blocked(attacker, attacker->tile, defender->tile, defender, NULL)) {
            return COMBAT_BAD_SHOT_AIM_BLOCKED;
        }
    }

    return COMBAT_BAD_SHOT_OK;
}

static bool rt_is_ranged_mode(Object* attacker, int hitMode)
{
    int attackType = item_w_subtype(item_hit_with(attacker, hitMode), hitMode);
    return attackType == ATTACK_TYPE_RANGED || attackType == ATTACK_TYPE_THROW;
}

// Whether scenery or a wall that light doesn't pass (a tent, a building)
// stands between the two: an NPC doesn't shoot at what it can't see, even
// where the shot itself could pass (see `obj_sight_blocking_at`).
static bool rt_view_blocked(Object* from, Object* to)
{
    if (from->elevation != to->elevation) {
        return true;
    }

    Object* obstacle = from;
    make_straight_path_func(from, from->tile, to->tile, NULL, &obstacle, 0, obj_sight_blocking_at);
    return obstacle != NULL && obstacle != from && obstacle != to;
}

static unsigned int rt_attack_cooldown(Object* attacker, int hitMode)
{
    // Measured from the start of the attack, so it includes the animation.
    return 900 + item_w_mp_cost(attacker, hitMode, false) * rt_ms_per_ap + roll_random(0, 300);
}

// -----------------------------------------------------------------------------
// Traits (Bloody Mess, Fast Shot)
// -----------------------------------------------------------------------------

void rt_note_gory_death(Object* critter)
{
    if (!rt_enabled() || critter == NULL || critter == obj_dude) {
        return;
    }

    if (std::find(rt_gory_deaths.begin(), rt_gory_deaths.end(), critter) == rt_gory_deaths.end()) {
        rt_gory_deaths.push_back(critter);
        rt_stats.goryDeathsNoted++;
        rt_log("bloody mess: %s dies a gory death from a critical hit", critter_name(critter));
    }
}

void rt_gory_death_shown(Object* critter)
{
    auto it = std::find(rt_gory_deaths.begin(), rt_gory_deaths.end(), critter);
    if (it == rt_gory_deaths.end()) {
        return;
    }
    rt_gory_deaths.erase(it);

    // Only armor the victim wears; spare armor in its inventory, other loot
    // and a creature's natural armor (part of its stats) stay.
    Object* armor = inven_worn(critter);
    if (armor == NULL) {
        rt_log("bloody mess: %s wore no armor", critter_name(critter));
        return;
    }

    rt_stats.armorDestroyed++;
    rt_stats.lastArmorDestroyedPid = armor->pid;
    rt_stats.lastArmorDestroyedOwner = critter;
    rt_log("bloody mess: %s's %s is destroyed", critter_name(critter), object_name(armor));
    obj_destroy(armor);
}

// The player's attack animations, and the reactions they cause.
static bool rt_is_attack_anim(int anim)
{
    switch (anim) {
    case ANIM_THROW_PUNCH:
    case ANIM_KICK_LEG:
    case ANIM_THROW_ANIM:
    case ANIM_THRUST_ANIM:
    case ANIM_SWING_ANIM:
    case ANIM_POINT:
    case ANIM_UNPOINT:
    case ANIM_FIRE_SINGLE:
    case ANIM_FIRE_BURST:
    case ANIM_FIRE_CONTINUOUS:
        return true;
    }
    return false;
}

static bool rt_is_reaction_anim(int anim)
{
    return anim == ANIM_HIT_FROM_FRONT || anim == ANIM_HIT_FROM_BACK || anim == ANIM_DODGE_ANIM;
}

// Fast Shot: the player's next attack waits for the current one to land,
// i.e. for its animation and the target's reaction, then for the cooldown.
// All three are shortened by the same factor, so the whole interval is
// divided by 1.25 (frame-counted impact events stay on their frames).
// Movement, reloads, holstering and death animations keep their speed.
//
// Ordinary hit reactions of NPCs also play faster (`hit_reaction_speed`), so
// a hit enemy is back on its feet sooner; falls, knockdowns, deaths, dodges,
// attacks and movement keep their speed.
unsigned int rt_anim_frame_time(Object* obj, int anim, unsigned int frameTime)
{
    if (!rt_enabled() || obj == NULL) {
        return frameTime;
    }

    if (obj != obj_dude
        && (anim == ANIM_HIT_FROM_FRONT || anim == ANIM_HIT_FROM_BACK)
        && FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER) {
        double speed = rt_rules()->hitReactionSpeed;
        if (speed > 1.0) {
            frameTime = std::max(1u, (unsigned int)(frameTime / speed + 0.5));
            rt_stats.fastReactionAnims++;
        }
    }

    if (!rt_fast_shot_active()) {
        return frameTime;
    }

    bool fast = false;
    if (obj == obj_dude) {
        fast = rt_is_attack_anim(anim);
    } else if (rt_is_reaction_anim(anim)) {
        // A reaction can start while the attack is still being registered.
        fast = obj == rt_registering_target;
        for (RtPendingAttack* pending : rt_pending) {
            if (pending->attack.attacker == obj_dude && pending->attack.defender == obj) {
                fast = true;
                break;
            }
        }
    }

    if (!fast) {
        return frameTime;
    }

    rt_stats.fastShotFrames++;
    return rt_fast_shot_interval(frameTime);
}

// -----------------------------------------------------------------------------
// Hostility
// -----------------------------------------------------------------------------

void rt_combat_request(STRUCT_664980* request)
{
    if (!rt_enabled()) {
        return;
    }

    rt_stats.combatRequests++;

    if (request == NULL) {
        rt_log("combat request without participants (ignored, no turn-based mode)");
        return;
    }

    Object* attacker = request->attacker;
    Object* defender = request->defender;

    rt_log("combat request: %s -> %s",
        attacker != NULL ? critter_name(attacker) : "(none)",
        defender != NULL ? rt_name(defender) : "(none)");

    if (attacker == obj_dude) {
        // Player attacks are driven by the controller; a mouse attack click
        // just selects the target.
        if (defender != NULL && FID_TYPE(defender->fid) == OBJ_TYPE_CRITTER) {
            if (rt_is_known_critter(defender) && rt_lock_is_valid(defender)) {
                rt_set_lock(defender);
            }
        }
        return;
    }

    if (attacker != NULL && defender != NULL) {
        rt_engage(attacker, defender, "script");

        if (defender != obj_dude
            && FID_TYPE(defender->fid) == OBJ_TYPE_CRITTER
            && defender->data.critter.combat.team != attacker->data.critter.combat.team) {
            rt_engage(defender, attacker, "defend");
        }
    }
}

static void rt_engage(Object* critter, Object* target, const char* reason)
{
    if (critter == NULL || critter == obj_dude || target == NULL || critter == target) {
        return;
    }

    if (!rt_is_alive(critter) || !rt_is_alive(target)) {
        return;
    }

    if (FID_TYPE(target->fid) != OBJ_TYPE_CRITTER) {
        return;
    }

    if (critter->elevation != target->elevation) {
        return;
    }

    // Companions never turn on the player team (vanilla also prevents this
    // outside of scripted team switches).
    if (rt_is_player_team(critter) && rt_is_player_team(target)) {
        return;
    }

    RtCritterState* state = &(rt_states[critter]);
    if (state->engaged && state->target == target) {
        return;
    }

    bool wasEngaged = state->engaged;
    state->engaged = true;
    state->target = target;
    state->moving = false;
    if (!wasEngaged) {
        // Short reaction time before the first action.
        state->nextActionAt = rt_now + roll_random(300, 800);
        rt_stats.engagements++;
    }

    rt_log("engage: %s -> %s (%s)", critter_name(critter), critter_name(target), reason);

    if (wasEngaged) {
        return;
    }

    // Teammates nearby join, like critters joining vanilla combat.
    for (Object* other : rt_critters) {
        if (other == critter || other == obj_dude || !rt_is_alive(other)) {
            continue;
        }

        if (other->data.critter.combat.team != critter->data.critter.combat.team) {
            continue;
        }

        if (rt_is_engaged(other)) {
            continue;
        }

        int distanceToTarget = obj_dist(other, target);
        if (distanceToTarget > RT_DISENGAGE_DISTANCE) {
            continue;
        }

        if (obj_dist(other, critter) <= RT_ENGAGE_TEAM_RADIUS || is_within_perception(other, target)) {
            rt_engage(other, target, "team");
        }
    }
}

static void rt_update_engagements()
{
    for (Object* critter : rt_critters) {
        if (critter == obj_dude || !rt_is_alive(critter)) {
            continue;
        }

        RtCritterState* state = &(rt_states[critter]);

        if (state->engaged) {
            Object* target = state->target;
            bool lost = !rt_is_known_critter(target)
                || !rt_is_alive(target)
                || obj_dist(critter, target) > RT_DISENGAGE_DISTANCE
                // A team switch (e.g. joining the player) ends the fight.
                || (rt_is_player_team(critter) && rt_is_player_team(target))
                || critter->data.critter.combat.team == target->data.critter.combat.team;
            if (!lost) {
                continue;
            }

            rt_log("disengage: %s (target gone)", critter_name(critter));
            bool targetWasPlayerTeam = rt_is_known_critter(target) && rt_is_player_team(target);
            state->engaged = false;
            state->target = NULL;

            // Keep fighting the player team if any member is nearby.
            if (targetWasPlayerTeam && !rt_is_player_team(critter)) {
                Object* best = NULL;
                int bestDistance = RT_COMPANION_RADIUS + 1;
                for (Object* candidate : rt_critters) {
                    if (rt_is_alive(candidate) && rt_is_player_team(candidate)) {
                        int distance = obj_dist(critter, candidate);
                        if (distance < bestDistance) {
                            best = candidate;
                            bestDistance = distance;
                        }
                    }
                }

                if (best != NULL) {
                    rt_engage(critter, best, "retarget");
                }
            }
            continue;
        }

        // Retaliation: `apply_damage` records the attacker in `whoHitMe`.
        Object* whoHitMe = critter->data.critter.combat.whoHitMe;
        if (whoHitMe != NULL
            && whoHitMe != critter
            && rt_is_known_critter(whoHitMe)
            && rt_is_alive(whoHitMe)
            && whoHitMe->data.critter.combat.team != critter->data.critter.combat.team
            && obj_dist(critter, whoHitMe) <= RT_DISENGAGE_DISTANCE) {
            rt_engage(critter, whoHitMe, "retaliation");
            continue;
        }

        // Companions help against anyone fighting the player team.
        if (critter != obj_dude && rt_is_player_team(critter)) {
            Object* best = NULL;
            int bestDistance = RT_COMPANION_RADIUS + 1;
            for (auto& entry : rt_states) {
                Object* hostile = entry.first;
                if (!entry.second.engaged || !rt_is_alive(hostile) || !rt_is_known_critter(entry.second.target)) {
                    continue;
                }

                if (!rt_is_player_team(entry.second.target) || rt_is_player_team(hostile)) {
                    continue;
                }

                int distance = obj_dist(critter, hostile);
                if (distance < bestDistance) {
                    best = hostile;
                    bestDistance = distance;
                }
            }

            if (best != NULL) {
                rt_engage(critter, best, "companion");
            }
        }
    }
}

static void rt_update_knockdowns()
{
    for (Object* critter : rt_critters) {
        if (!rt_is_alive(critter)) {
            continue;
        }

        int results = critter->data.critter.combat.results;
        if ((results & DAM_KNOCKED_DOWN) == 0 || (results & DAM_KNOCKED_OUT) != 0) {
            continue;
        }

        if (anim_busy(critter) || rt_obj_in_attack(critter)) {
            continue;
        }

        RtCritterState* state = &(rt_states[critter]);
        if (state->knockedDownAt == 0) {
            state->knockedDownAt = rt_now;
            continue;
        }

        // In turn-based mode a knocked down critter stands up on its turn.
        if (rt_now - state->knockedDownAt >= 700) {
            state->knockedDownAt = 0;
            dude_standup(critter);
            rt_log("stand up: %s", critter_name(critter));
        }
    }
}

// -----------------------------------------------------------------------------
// AI
// -----------------------------------------------------------------------------

// Test fixture: NPCs don't act (attack rate measurements).
static bool rt_ai_paused = false;

void rt_debug_set_ai_paused(bool paused)
{
    rt_ai_paused = paused;
}

static void rt_update_ai()
{
    if (rt_ai_paused) {
        return;
    }

    for (Object* critter : rt_critters) {
        if (critter == obj_dude || !rt_is_alive(critter)) {
            continue;
        }

        if ((critter->data.critter.combat.results & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN)) != 0) {
            continue;
        }

        auto it = rt_states.find(critter);
        if (it == rt_states.end() || !it->second.engaged) {
            continue;
        }

        rt_ai_act(critter, &(it->second));
    }
}

static int rt_npc_hit_mode(Object* critter, bool* reloaded)
{
    *reloaded = false;

    Object* hands[2] = { inven_right_hand(critter), inven_left_hand(critter) };
    int hitModes[2] = { HIT_MODE_RIGHT_WEAPON_PRIMARY, HIT_MODE_LEFT_WEAPON_PRIMARY };

    for (int index = 0; index < 2; index++) {
        Object* weapon = hands[index];
        if (weapon == NULL || item_get_type(weapon) != ITEM_TYPE_WEAPON) {
            continue;
        }

        if (item_w_max_ammo(weapon) > 0 && item_w_curr_ammo(weapon) == 0) {
            if (item_w_try_reload(critter, weapon) != -1) {
                *reloaded = true;
                return hitModes[index];
            }
            continue;
        }

        return hitModes[index];
    }

    return HIT_MODE_PUNCH;
}

static void rt_ai_act(Object* critter, RtCritterState* state)
{
    if (rt_obj_held_by_attack(critter)) {
        return;
    }

    bool busy = anim_busy(critter);
    if (!busy) {
        state->moving = false;
    }

    if (busy && !state->moving) {
        rt_ai_debug(critter, state, "waiting: busy with another animation");
        return;
    }

    if (rt_now < state->nextActionAt) {
        return;
    }

    Object* target = state->target;
    if (!rt_is_known_critter(target) || !rt_is_alive(target)) {
        return;
    }

    bool reloaded;
    int hitMode = rt_npc_hit_mode(critter, &reloaded);
    if (reloaded) {
        rt_log("ai: %s reloads", critter_name(critter));
        state->nextActionAt = rt_now + 1000;
        return;
    }

    // An attack is only committed with a real chance: in range or reach,
    // nothing blocking the shot or the view, ammunition, and a hit chance
    // above 0% (a small one is enough). Otherwise the critter moves to a
    // better position, without taking a place in the attack slot queue.
    int shot = rt_check_shot(critter, target, hitMode);
    if (shot == COMBAT_BAD_SHOT_OK && rt_is_ranged_mode(critter, hitMode) && rt_view_blocked(critter, target)) {
        rt_stats.aiSightBlockedSkips++;
        rt_ai_debug(critter, state, "no line of sight to %s", critter_name(target));
        shot = COMBAT_BAD_SHOT_AIM_BLOCKED;
    }
    if (shot == COMBAT_BAD_SHOT_OK) {
        int toHit = determine_to_hit(critter, target, HIT_LOCATION_UNCALLED, hitMode);
        if (toHit <= 0) {
            rt_stats.aiZeroChanceSkips++;
            rt_ai_debug(critter, state, "no chance to hit %s (%d%%), moving closer", critter_name(target), toHit);
            shot = COMBAT_BAD_SHOT_AIM_BLOCKED;
        }
    }
    if (shot == COMBAT_BAD_SHOT_OK) {
        if (state->moving) {
            if (register_clear(critter) == -2) {
                return;
            }
            state->moving = false;
        }

        if (target == obj_dude) {
            // Enemies ready to hit the player line up for the single attack
            // slot. Waiting ones keep polling so their place stays valid.
            bool mayAttack = rt_player_slot_may_attack(critter, state);

            if (rt_now < rt_player_recovery_until) {
                state->nextActionAt = rt_now + 100;
                return;
            }

            // AIMED SHOT planning gets its window. The player's buffered
            // RT/RB presses, reloads and chem uses don't hold enemies back:
            // an ordinary enemy attack doesn't hold the player either (see
            // `rt_player_detachable`), and RT spam must not freeze enemies.
            if (rt_aimed_wants_window()) {
                state->nextActionAt = rt_now + 100;
                return;
            }

            if (!mayAttack) {
                rt_ai_debug(critter, state, "waiting for the player attack slot (queue %d)", (int)rt_slot_queue.size());
                state->nextActionAt = rt_now + 100;
                return;
            }
        }

        int rc = rt_attack(critter, target, hitMode);
        if (rc == 0) {
            if (target == obj_dude) {
                rt_slot_holder = critter;
                rt_slot_queue.erase(std::remove(rt_slot_queue.begin(), rt_slot_queue.end(), critter), rt_slot_queue.end());
                unsigned int waited = rt_now - rt_slot_wait_start[critter];
                rt_slot_wait_start.erase(critter);
                if ((int)waited > rt_stats.playerSlotMaxWaitMs) {
                    rt_stats.playerSlotMaxWaitMs = (int)waited;
                }
                rt_stats.playerSlotGrants++;
                rt_counter_started(critter, hitMode);
                rt_slot_head_since = rt_now;
                rt_log("slot: %s attacks the player (waited %u ms, %d queued)", critter_name(critter), waited, (int)rt_slot_queue.size());
            }
            state->nextActionAt = rt_now + rt_attack_cooldown(critter, hitMode);
        } else if (rc == RT_ATTACK_BUSY) {
            rt_ai_debug(critter, state, "attack on %s busy", critter_name(target));
            state->nextActionAt = rt_now + 150;
        } else {
            rt_ai_debug(critter, state, "attack on %s failed (rc %d)", critter_name(target), rc);
            state->nextActionAt = rt_now + 500;
        }
        return;
    }

    if (shot != COMBAT_BAD_SHOT_OUT_OF_RANGE && shot != COMBAT_BAD_SHOT_AIM_BLOCKED) {
        rt_ai_debug(critter, state, "can't attack %s (bad shot %d)", critter_name(target), shot);
        state->nextActionAt = rt_now + 1000;
        return;
    }

    // A walk that makes no progress is dropped and planned again: in real
    // time two critters stepping toward each other can each wait for the
    // other's next tile forever (turn-based play only ever moves one).
    if (state->moving) {
        if (critter->tile != state->progressTile) {
            state->progressTile = critter->tile;
            state->progressAt = rt_now;
        } else if (rt_now - state->progressAt > RT_MOVE_STALL_MS) {
            rt_log("ai: %s: walk stalled at tile %d, planning again", critter_name(critter), critter->tile);
            anim_rt_end_unprioritized(critter);
            state->moving = false;
            state->nextActionAt = rt_now + roll_random(100, 400);
            return;
        }
    }

    // Keep the current path unless the target moved away from its end.
    if (state->moving && tile_dist(state->moveTargetTile, target->tile) < 2) {
        return;
    }

    if (busy && register_clear(critter) == -2) {
        return;
    }

    int distance = obj_dist(critter, target);
    int range = item_w_range(critter, hitMode);
    int steps = shot == COMBAT_BAD_SHOT_OUT_OF_RANGE && range > 1 ? distance - range + 1 : 6;
    if (steps < 1) {
        steps = 1;
    } else if (steps > 6) {
        steps = 6;
    }

    int runFid = art_id(OBJ_TYPE_CRITTER, critter->fid & 0xFFF, ANIM_RUNNING, (critter->fid & 0xF000) >> 12, critter->rotation + 1);
    bool run = distance > 5 && art_exists(runFid);

    register_begin(ANIMATION_REQUEST_RESERVED);
    if (run) {
        register_object_run_to_object(critter, target, steps, 0);
    } else {
        register_object_move_to_object(critter, target, steps, 0);
    }

    if (register_end() == -1) {
        rt_ai_debug(critter, state, "move towards %s failed (dist %d)", critter_name(target), distance);
        state->nextActionAt = rt_now + 500;
        return;
    }

    rt_ai_debug(critter, state, "moving towards %s (dist %d, steps %d, run %d)", critter_name(target), distance, steps, run);
    state->moving = true;
    state->moveTargetTile = target->tile;
    state->progressTile = critter->tile;
    state->progressAt = rt_now;
    state->nextActionAt = rt_now + 200;
    rt_stats.lastAiMoveCritter = critter;
    rt_stats.lastAiMoveAt = rt_now;
}

// -----------------------------------------------------------------------------
// Player attack slot
// -----------------------------------------------------------------------------

// Marks `critter` as ready to attack the player and tells whether it may do
// so now: the slot must be free and every enemy that queued earlier and is
// still ready goes first.
static bool rt_player_slot_may_attack(Object* critter, RtCritterState* state)
{
    state->slotRequestAt = rt_now;

    if (std::find(rt_slot_queue.begin(), rt_slot_queue.end(), critter) == rt_slot_queue.end()) {
        if (rt_slot_queue.empty()) {
            rt_slot_head_since = rt_now;
        }
        rt_slot_queue.push_back(critter);
        rt_slot_wait_start[critter] = rt_now;
        if ((int)rt_slot_queue.size() > rt_stats.playerSlotMaxQueue) {
            rt_stats.playerSlotMaxQueue = (int)rt_slot_queue.size();
        }
    }

    if (rt_slot_holder != NULL && rt_slot_holder != critter) {
        return false;
    }

    // An enemy owed a counterattack (the player's close-combat attack
    // missed it) goes first when it is ready; an attack already committed
    // (the holder's) is never interrupted.
    Object* counter = rt_counter_ready();
    if (counter != NULL) {
        return counter == critter;
    }

    // Entries that are not ready right now (e.g. stepping back into range)
    // don't hold up the ones behind them.
    for (Object* queued : rt_slot_queue) {
        if (queued == critter) {
            return true;
        }

        auto it = rt_states.find(queued);
        if (it != rt_states.end() && rt_now - it->second.slotRequestAt <= RT_SLOT_READY_MS) {
            return false;
        }
    }

    return false;
}

static void rt_update_player_slot()
{
    rt_update_counters();

    // The slot is released when the holder's attack on the player resolved
    // (after its animation) or was dropped (attacker died or left the map).
    if (rt_slot_holder != NULL) {
        bool active = false;
        for (RtPendingAttack* pending : rt_pending) {
            if (pending->attack.attacker == rt_slot_holder && pending->attack.defender == obj_dude) {
                active = true;
                break;
            }
        }

        if (!active) {
            rt_log("slot: released by %s", rt_is_known_critter(rt_slot_holder) ? critter_name(rt_slot_holder) : "(gone)");
            rt_slot_holder = NULL;
            rt_slot_head_since = rt_now;
        }
    }

    Object* head = rt_slot_queue.empty() ? NULL : rt_slot_queue.front();

    for (size_t index = 0; index < rt_slot_queue.size();) {
        Object* queued = rt_slot_queue[index];
        bool keep = rt_is_known_critter(queued) && rt_is_alive(queued);
        if (keep) {
            auto it = rt_states.find(queued);
            keep = it != rt_states.end()
                && it->second.engaged
                && it->second.target == obj_dude
                && rt_now - it->second.slotRequestAt <= RT_SLOT_STALE_MS;
        }

        if (keep) {
            index++;
        } else {
            rt_slot_wait_start.erase(queued);
            rt_slot_queue.erase(rt_slot_queue.begin() + index);
        }
    }

    if (rt_slot_queue.empty()) {
        return;
    }

    if (rt_slot_queue.front() != head) {
        rt_slot_head_since = rt_now;
    }

    // A head that can't start its attack (its animation is blocked, etc.)
    // moves to the back instead of stalling everyone.
    if (rt_slot_holder == NULL
        && rt_slot_queue.size() > 1
        && rt_now - rt_slot_head_since > RT_SLOT_HEAD_TIMEOUT_MS) {
        Object* stuck = rt_slot_queue.front();
        rt_slot_queue.erase(rt_slot_queue.begin());
        rt_slot_queue.push_back(stuck);
        rt_slot_head_since = rt_now;
        rt_log("slot: %s could not attack in time, moved to the back", critter_name(stuck));
    }
}

// -----------------------------------------------------------------------------
// Player
// -----------------------------------------------------------------------------

// Drawing a weapon is an animation that ends by switching the sprite to the
// weapon-holding set. In real time that animation can be cut short (e.g. by
// an enemy hit), leaving the active weapon without its sprite, which makes
// the original attack animations unavailable. When idle, finish the switch.
static void rt_sync_weapon_sprite()
{
    if (anim_busy(obj_dude) || rt_obj_in_attack(obj_dude) || critter_is_prone(obj_dude)) {
        return;
    }

    Object* item = NULL;
    intface_get_current_item(&item);
    int code = item != NULL && item_get_type(item) == ITEM_TYPE_WEAPON ? item_w_anim_code(item) : 0;
    if (((obj_dude->fid & 0xF000) >> 12) == code) {
        return;
    }

    int fid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_STAND, code, obj_dude->rotation + 1);
    if (!art_exists(fid)) {
        return;
    }

    Rect rect;
    if (obj_change_fid(obj_dude, fid, &rect) == 0) {
        tile_refresh_rect(&rect, obj_dude->elevation);
    }
    rt_log("weapon sprite synced (weapon code %d)", code);
}

// Thrown weapons belong in the right hand (item 2) and are never the active
// item (RB throws them). Equipping enforces this (`inven_rt_equip`); this
// catches the rest (old saves, scripts): a thrown weapon in the left hand
// moves to an empty right hand, or back to the inventory, and an active
// right hand holding one gives the turn to the left.
static void rt_enforce_thrown_hand()
{
    static unsigned int lastCheckAt = 0;
    if (lastCheckAt != 0 && rt_now - lastCheckAt < 250 && rt_now >= lastCheckAt) {
        return;
    }
    lastCheckAt = rt_now;

    if (!rt_player_can_change_equipment()) {
        return;
    }

    Object* left = inven_left_hand(obj_dude);
    if (rt_is_thrown_weapon(left)) {
        char name[64];
        SDL_strlcpy(name, object_name(left), sizeof(name));
        if (inven_right_hand(obj_dude) == NULL && inven_rt_equip(left, INVEN_RT_SLOT_RIGHT_HAND) == 0) {
            rt_print("The %s can only be held in the right hand: moved there. RB throws it.", name);
        } else if (inven_rt_unequip(left) == 0) {
            rt_print("The %s can only be held in the right hand: put away.", name);
        }
        rt_stats.thrownHandFixes++;
        rt_log("hands: thrown weapon %s taken out of the left hand", name);
    }

    if (intface_is_item_right_hand() && rt_is_thrown_weapon(inven_right_hand(obj_dude))) {
        intface_toggle_items(false);
        inven_rt_refresh_appearance();
        rt_stats.thrownHandFixes++;
        rt_log("hands: active hand back to the left (thrown weapon in the right)");
    }
}

static void rt_update_dude(float dtMs)

{
    int results = obj_dude->data.critter.combat.results;
    bool canAct = !game_ui_is_disabled()
        && (results & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN | DAM_DEAD)) == 0
        && obj_dude->elevation == map_elevation;

    rt_update_ap(dtMs);
    rt_sync_weapon_sprite();

    // Presses are consumed every tick, even when they can't be acted upon, so
    // they don't fire later by surprise.
    bool pressRun = gamepad_consume_press(GAMEPAD_BUTTON_LEFT_STICK);
    bool pressAttack = gamepad_consume_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
    bool pressLock = gamepad_consume_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
    bool pressA = gamepad_consume_press(GAMEPAD_BUTTON_A);
    bool pressB = gamepad_consume_press(GAMEPAD_BUTTON_B);
    bool pressX = gamepad_consume_press(GAMEPAD_BUTTON_X);
    bool pressLB = gamepad_consume_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
    bool pressRB = gamepad_consume_press(GAMEPAD_BUTTON_RIGHT_SHOULDER);
    bool pressBack = gamepad_consume_press(GAMEPAD_BUTTON_BACK);
    bool pressStart = gamepad_consume_press(GAMEPAD_BUTTON_START);
    bool pressUp = gamepad_consume_press(GAMEPAD_BUTTON_DPAD_UP);
    bool pressDown = gamepad_consume_press(GAMEPAD_BUTTON_DPAD_DOWN);
    bool pressLeft = gamepad_consume_press(GAMEPAD_BUTTON_DPAD_LEFT);
    bool pressRight = gamepad_consume_press(GAMEPAD_BUTTON_DPAD_RIGHT);
    // R3 toggles the wider view, View (Back) opens the Skilldex (Sneak is
    // there), Y cycles the attack mode.
    bool pressR3 = gamepad_consume_press(GAMEPAD_BUTTON_RIGHT_STICK);
    bool pressY = gamepad_consume_press(GAMEPAD_BUTTON_Y);
    float flickX;
    float flickY;
    bool flick = gamepad_consume_flick(&flickX, &flickY);

    // A right stick push belongs to the camera or to target switching,
    // whichever applied when it left the center, until it returns there; one
    // push never pans and switches targets.
    float lookX;
    float lookY;
    gamepad_get_look_stick(&lookX, &lookY);
    if (lookX == 0.0f && lookY == 0.0f) {
        rt_rstick_owner = RT_RSTICK_NONE;
    } else if (rt_rstick_owner == RT_RSTICK_NONE) {
        rt_rstick_owner = rt_lock != NULL ? RT_RSTICK_SWITCH : RT_RSTICK_PAN;
    }
    if (flick && rt_rstick_owner != RT_RSTICK_SWITCH) {
        flick = false;
    }

    if (pressR3) {
        rt_toggle_zoom();
    }

    // The lock follows its target and drops it when it becomes invalid.
    if (rt_lock != NULL && !rt_lock_is_valid(rt_lock)) {
        rt_log("lock: cleared (%s)", rt_is_known_critter(rt_lock) && !rt_is_alive(rt_lock) ? "target died" : "target invalid");
        rt_set_lock(NULL);
    }

    rt_update_reload();

    // A chem use ends with its animation.
    if (rt_chem_active && !anim_busy(obj_dude)) {
        rt_chem_active = false;
        intface_update_items(false);
        intface_update_hit_points(false);
    }

    if (pressStart) {
        // Pause menu (vanilla options window); handled on the next frame.
        GNW_add_input_buffer(KEY_ESCAPE);
    }

    // Aimed attacks being carried out: B cancels the ones not started yet.
    bool aimedRunning = rt_aimed_is_executing();
    rt_aimed_update(canAct, aimedRunning && pressB);
    if (aimedRunning) {
        pressB = false;
    }

    if (!canAct) {
        rt_timing_clear("the player can't act");
        rt_fire_hold = false;
        rt_attack_buffered_until = 0;
        rt_throw_buffered_until = 0;
        rt_set_interact_highlight(NULL);
        rt_update_label(NULL);
        rt_move_active = false;
        return;
    }

    rt_enforce_thrown_hand();

    // Nothing may interfere with queued aimed attacks: no movement, attacks,
    // reloads, interactions or equipment changes until the queue is done.
    // The lock can still be moved (it doesn't change any queued target).
    if (rt_aimed_is_executing()) {
        if (pressAttack || pressX || pressA || pressRB || pressBack || pressLB || pressY
            || pressUp || pressDown || pressLeft || pressRight || pressRun) {
            rt_print("Aimed attacks in progress. B cancels the rest.");
        }
        if (pressLock) {
            rt_set_lock(rt_lock != NULL ? NULL : rt_find_lock_candidate());
        }
        if (flick && rt_lock != NULL) {
            rt_switch_lock(flickX, flickY);
        }
        rt_fire_hold = false;
        rt_attack_buffered_until = 0;
        rt_throw_buffered_until = 0;
        rt_reload_requested_until = 0;
        rt_timing_clear("AIMED SHOT");
        rt_fav_pending_slot = -1;
        rt_interact_manual = NULL;
        rt_interact_target = NULL;
        rt_set_interact_highlight(NULL);
        rt_update_label(NULL);
        rt_update_movement(false);
        return;
    }

    if (pressB) {
        GNW_add_input_buffer(RT_PIPBOY_KEY_OPEN);
    }
    if (pressRB) {
        // RB throws the right hand's (item 2's) thrown weapon without
        // changing the active item; with no thrown weapon there, it switches
        // hands as before.
        if (rt_is_thrown_weapon(inven_right_hand(obj_dude))) {
            rt_log("input: RB pressed: throw (lock=%s)", rt_lock != NULL ? critter_name(rt_lock) : "none");
            rt_throw_buffered_until = rt_now + RT_ATTACK_BUFFER_MS;
        } else if (rt_reload_active) {
            rt_log("input: RB ignored while reloading");
        } else {
            GNW_add_input_buffer(KEY_LOWERCASE_B);
        }
    }
    if (pressY) {
        // Attack mode (single/burst, punch/kick, swing/thrust).
        GNW_add_input_buffer(KEY_LOWERCASE_N);
    }
    if (pressBack) {
        GNW_add_input_buffer(KEY_LOWERCASE_S);
    }

    if (pressLB) {
        // AIMED SHOT: plans aimed attacks (modal, the game is paused).
        rt_stats.targetingRequests++;
        rt_stop_walking();
        rt_set_interact_highlight(NULL);
        rt_update_label(NULL);
        rt_aimed_open();
        if (rt_aimed_is_executing()) {
            rt_update_movement(false);
            return;
        }
    }

    if (pressRun) {
        if (rt_run_toggled) {
            rt_run_toggled = false;
            rt_log("run: off");
        } else if (rt_dude_ap >= 1.0f || !rt_player_in_combat()) {
            // Outside combat running is free, even at 0 AP.
            rt_run_toggled = true;
            rt_log("run: on (ap=%.2f)", rt_dude_ap);
        } else {
            rt_print("You are too tired to run.");
        }
    }

    if (pressLock) {
        if (rt_lock != NULL) {
            rt_print("Target lock released.");
            rt_set_lock(NULL);
        } else {
            Object* target = rt_find_lock_candidate();
            if (target != NULL) {
                rt_set_lock(target);
            } else {
                rt_print("No target nearby.");
            }
        }
    }

    if (flick) {
        if (rt_lock != NULL) {
            rt_switch_lock(flickX, flickY);
        }
    }

    rt_update_x_button(pressX);
    rt_update_holster();
    rt_update_draw();
    if (rt_reload_requested_until != 0) {
        if (rt_reload() != RT_RELOAD_WAIT) {
            rt_reload_requested_until = 0;
        } else if (rt_now > rt_reload_requested_until) {
            rt_reload_requested_until = 0;
            rt_print("You can't reload right now.");
        }
    }

    rt_update_attack_input(pressAttack);
    rt_update_throw_input();
    rt_update_chem_request();
    rt_update_favorite_request();

    // Interaction: what A would act on.
    rt_collect_interactables();
    Object* interactable = rt_find_interactable();

    // D-pad: left/right steps through nearby interactions when another one
    // is within 2 hexes of the highlighted one; otherwise every direction
    // activates its favorite right away. One action per press (a press, not
    // a hold, so a held direction never uses up a stack).
    int direction = -1;
    if (pressUp) {
        direction = RT_FAVORITES_UP;
    } else if (pressRight) {
        direction = RT_FAVORITES_RIGHT;
    } else if (pressDown) {
        direction = RT_FAVORITES_DOWN;
    } else if (pressLeft) {
        direction = RT_FAVORITES_LEFT;
    }

    bool horizontal = direction == RT_FAVORITES_LEFT || direction == RT_FAVORITES_RIGHT;
    if (horizontal && rt_can_cycle_interaction(interactable)) {
        rt_cycle_interaction(direction == RT_FAVORITES_RIGHT ? 1 : -1);
        interactable = rt_find_interactable();
    } else if (direction != -1) {
        rt_stats.favoritesOpened++;
        rt_fav_pending_slot = direction;
        rt_fav_pending_until = rt_now + RT_FAVORITE_BUFFER_MS;
        rt_update_favorite_request();
        rt_update_chem_request();
    }

    rt_interact_target = interactable;
    rt_set_interact_highlight(interactable);
    rt_update_label(interactable);

    if (pressA) {
        if (rt_reload_active) {
            rt_log("interact: ignored while reloading");
        } else if (interactable == NULL) {
            rt_log("interact: nothing nearby");
        } else if (tile_dist(obj_dude->tile, interactable->tile) > RT_INTERACT_REACH) {
            // Highlighted but out of reach: A never acts from farther away.
            rt_print("Too far away. Move next to the %s.", rt_name(interactable));
        } else {
            rt_interact(interactable);
        }
    }

    rt_update_movement(true);
}

static void rt_update_ap(float dtMs)
{
    float maxAp = (float)stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS);

    bool running = rt_move_active
        && rt_move_run
        && anim_busy(obj_dude)
        && FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING;

    // Sprinting costs AP only in combat (a hostile engaged with the player's
    // team, see `rt_player_in_combat`); Bonus Move lowers that drain.
    // Outside combat a sprint is free and AP regenerates as when walking.
    bool inCombat = running && rt_player_in_combat();
    if (running && !inCombat) {
        rt_stats.sprintCalmMs += dtMs;
    }

    if (inCombat) {
        float drain = dtMs / rt_run_drain_ms * rt_sprint_drain_pct(true) / 100.0f;
        rt_dude_ap -= drain;
        rt_stats.sprintCombatMs += dtMs;
        rt_stats.sprintCombatAp += drain;
        if (rt_dude_ap <= 0.0f) {
            rt_dude_ap = 0.0f;
            if (rt_run_toggled) {
                // A new click is needed to run again.
                rt_run_toggled = false;
                rt_print("You are out of breath.");
                rt_log("run: AP exhausted in combat, back to walking");
            }
        }
    } else {
        // Frozen while aimed attacks are carried out, so regeneration can't
        // pay for more of them than were reserved.
        if (!rt_aimed_is_executing()) {
            float regen = dtMs / rt_ap_regen_ms;
            rt_dude_ap += regen;
            if (running) {
                rt_stats.sprintCalmRegen += regen;
            }
        }
    }

    if (rt_dude_ap > maxAp) {
        rt_dude_ap = maxAp;
    }

    int shown = (int)rt_dude_ap;
    obj_dude->data.critter.combat.ap = shown;
    if (shown != rt_dude_ap_shown) {
        rt_dude_ap_shown = shown;
        intface_update_move_points(shown, 0);
    }
}

static bool rt_rotation_vector(int rotation, float* x, float* y)
{
    int tile = obj_dude->tile;
    int neighbor = tile_num_in_direction(tile, rotation, 1);
    if (neighbor == -1) {
        return false;
    }

    int x1;
    int y1;
    int x2;
    int y2;
    tile_coord(tile, &x1, &y1, obj_dude->elevation);
    tile_coord(neighbor, &x2, &y2, obj_dude->elevation);

    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float length = sqrtf(dx * dx + dy * dy);
    if (length == 0.0f) {
        return false;
    }

    *x = dx / length;
    *y = dy / length;
    return true;
}

static int rt_rotation_from_vector(float x, float y)
{
    int best = -1;
    float bestDot = -2.0f;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        float rx;
        float ry;
        if (!rt_rotation_vector(rotation, &rx, &ry)) {
            continue;
        }

        float dot = rx * x + ry * y;
        if (dot > bestDot) {
            bestDot = dot;
            best = rotation;
        }
    }
    return best;
}

static bool rt_facing_vector(float* x, float* y)
{
    float sx;
    float sy;
    gamepad_get_move_stick(&sx, &sy);
    float length = sqrtf(sx * sx + sy * sy);
    if (length > 0.0f) {
        *x = sx / length;
        *y = sy / length;
        return true;
    }

    return rt_rotation_vector(obj_dude->rotation, x, y);
}

static float rt_facing_cos(Object* obj, float fx, float fy)
{
    int x1;
    int y1;
    int x2;
    int y2;
    tile_coord(obj_dude->tile, &x1, &y1, obj_dude->elevation);
    tile_coord(obj->tile, &x2, &y2, obj->elevation);

    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float length = sqrtf(dx * dx + dy * dy);
    if (length == 0.0f) {
        return 1.0f;
    }

    return (dx * fx + dy * fy) / length;
}

static int rt_find_move_dest(int rotation)
{
    int tile = obj_dude->tile;
    int dest = -1;
    for (int step = 0; step < RT_MOVE_LOOKAHEAD; step++) {
        int next = tile_num_in_direction(tile, rotation, 1);
        if (next == -1 || next == tile) {
            break;
        }

        if (obj_blocking_at(obj_dude, next, obj_dude->elevation) != NULL) {
            break;
        }

        dest = next;
        tile = next;
    }
    return dest;
}

// Ends the player's walk now (before a screen that pauses the game), so it
// doesn't carry on to the end of its path afterwards.
static void rt_stop_walking()
{
    if (rt_move_active && anim_busy(obj_dude)) {
        int anim = FID_ANIM_TYPE(obj_dude->fid);
        if (anim == ANIM_WALK || anim == ANIM_RUNNING) {
            register_clear(obj_dude);
        }
    }
    rt_move_active = false;
}

static void rt_update_movement(bool canAct)
{
    float stickX;
    float stickY;
    gamepad_get_move_stick(&stickX, &stickY);
    bool stick = stickX != 0.0f || stickY != 0.0f;

    // No walking during a reload: the holster and draw animations play in place.
    if (!canAct || rt_obj_in_attack(obj_dude) || rt_reload_active || rt_chem_active || rt_holster_active) {
        if (stick) {
            static unsigned int lastBlockedLog = 0;
            if (rt_now - lastBlockedLog >= 500) {
                lastBlockedLog = rt_now;
                rt_log("move: held off (can act=%d, in attack=%d, reloading=%d)", canAct, rt_obj_in_attack(obj_dude), rt_reload_active);
            }
        }
        rt_move_active = false;
        return;
    }

    if (!stick) {
        if (rt_move_active) {
            if (anim_busy(obj_dude)) {
                register_clear(obj_dude);
            }
            rt_move_active = false;
        }
        return;
    }

    int dir = rt_rotation_from_vector(stickX, stickY);
    if (dir == -1) {
        return;
    }

    // Hysteresis: near the border between two hex directions keep the
    // current one, so small stick wobble doesn't restart the walk cycle.
    if (rt_move_active && dir != rt_move_dir) {
        float cx;
        float cy;
        float nx;
        float ny;
        if (rt_rotation_vector(rt_move_dir, &cx, &cy) && rt_rotation_vector(dir, &nx, &ny)) {
            float currentDot = cx * stickX + cy * stickY;
            float newDot = nx * stickX + ny * stickY;
            if (newDot - currentDot < 0.15f * sqrtf(stickX * stickX + stickY * stickY)) {
                dir = rt_move_dir;
            }
        }
    }

    // In combat, starting a run needs a full AP and it lasts until AP is out;
    // outside combat running costs nothing.
    bool wantRun = rt_run_toggled
        && (!rt_player_in_combat() || (rt_move_active && rt_move_run ? rt_dude_ap > 0.0f : rt_dude_ap >= 1.0f));
    bool busy = anim_busy(obj_dude);

    // Only re-path when something changed or the current path ended: one
    // movement request at a time, no needless animation restarts.
    // A walk that stopped making progress (blocked by a critter that is
    // itself waiting) is planned again.
    if (obj_dude->tile != rt_move_progress_tile) {
        rt_move_progress_tile = obj_dude->tile;
        rt_move_progress_at = rt_now;
    }
    bool stalled = rt_move_active && busy && rt_now - rt_move_progress_at > RT_MOVE_STALL_MS;
    if (stalled) {
        rt_log("move: stalled at tile %d, planning again", obj_dude->tile);
        rt_move_progress_at = rt_now;
    }

    bool needIssue = !rt_move_active
        || dir != rt_move_dir
        || wantRun != rt_move_run
        || !busy
        || stalled;

    if (!needIssue || rt_now < rt_next_move_issue_at) {
        return;
    }

    int dest = rt_find_move_dest(dir);
    if (dest == -1) {
        // Slide along obstacles using the neighboring directions.
        float bestDot = 0.0f;
        for (int delta = -1; delta <= 1; delta += 2) {
            int alt = (dir + delta + ROTATION_COUNT) % ROTATION_COUNT;
            int altDest = rt_find_move_dest(alt);
            float rx;
            float ry;
            if (altDest != -1 && rt_rotation_vector(alt, &rx, &ry)) {
                float dot = rx * stickX + ry * stickY;
                if (dot > bestDot) {
                    bestDot = dot;
                    dest = altDest;
                }
            }
        }
    }

    if (dest == -1) {
        rt_log("move: no free tile toward direction %d", dir);
        // Blocked: just face the requested direction.
        if (!busy && obj_dude->rotation != dir) {
            Rect rect;
            if (obj_set_rotation(obj_dude, dir, &rect) == 0) {
                tile_refresh_rect(&rect, obj_dude->elevation);
            }
        }
        rt_move_active = false;
        rt_next_move_issue_at = rt_now + 150;
        return;
    }

    if (busy && register_clear(obj_dude) == -2) {
        static unsigned int lastLockedLog = 0;
        if (rt_now - lastLockedLog >= 500) {
            lastLockedLog = rt_now;
            rt_log("move: can't replace the current %s (locked sequence)", FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING ? "run" : "movement");
        }
        return;
    }

    rt_log("move: issue %s to %d (toggle %d, combat %d, ap %.2f)", wantRun ? "run" : "walk", dest, rt_run_toggled, rt_player_in_combat(), rt_dude_ap);
    register_begin(ANIMATION_REQUEST_RESERVED);
    if (wantRun) {
        register_object_run_to_tile(obj_dude, dest, obj_dude->elevation, -1, 0);
    } else {
        register_object_move_to_tile(obj_dude, dest, obj_dude->elevation, -1, 0);
    }

    if (register_end() == -1) {
        rt_next_move_issue_at = rt_now + 200;
        return;
    }

    if (wantRun && !perk_level(PERK_SILENT_RUNNING) && is_pc_flag(PC_FLAG_SNEAKING)) {
        // As in the original: running ends sneaking (Silent Running aside).
        pc_flag_off(PC_FLAG_SNEAKING);
        rt_print("You stop sneaking to run.");
    }

    rt_move_active = true;
    rt_move_dir = dir;
    rt_move_run = wantRun;
    rt_move_dest = dest;
    rt_move_progress_tile = obj_dude->tile;
    rt_move_progress_at = rt_now;
    rt_next_move_issue_at = rt_now + 80;
    rt_stats.moveRequests++;
}

// -----------------------------------------------------------------------------
// Target lock
// -----------------------------------------------------------------------------

static bool rt_is_hostile_to_player(Object* critter)
{
    if (critter == obj_dude || !rt_is_alive(critter) || rt_is_player_team(critter)) {
        return false;
    }

    auto it = rt_states.find(critter);
    if (it != rt_states.end()
        && it->second.engaged
        && rt_is_known_critter(it->second.target)
        && rt_is_player_team(it->second.target)) {
        return true;
    }

    // Fighting back against the player team counts too.
    Object* whoHitMe = critter->data.critter.combat.whoHitMe;
    return whoHitMe != NULL
        && rt_is_known_critter(whoHitMe)
        && rt_is_alive(whoHitMe)
        && rt_is_player_team(whoHitMe);
}

static bool rt_lock_is_valid(Object* target)
{
    return rt_is_known_critter(target)
        && rt_is_alive(target)
        && (target->flags & OBJECT_HIDDEN) == 0
        && target->elevation == obj_dude->elevation
        && !rt_is_player_team(target)
        && obj_dist(obj_dude, target) <= RT_DISENGAGE_DISTANCE;
}

// The visible gameplay area: the map window above the interface bar.
static bool rt_get_play_area(int* width, int* height)
{
    *width = scr_size.lrx - scr_size.ulx + 1;
    *height = scr_size.lry - scr_size.uly + 1 - INTERFACE_BAR_HEIGHT;
    return *width > 0 && *height > 0;
}

// Screen position of the center of the object's hex; true if it is inside
// the gameplay area.
static bool rt_screen_pos(Object* obj, int* x, int* y)
{
    if (!rt_view_tile_center(obj->tile, obj->elevation, x, y)) {
        return false;
    }

    int width;
    int height;
    if (!rt_get_play_area(&width, &height)) {
        return false;
    }

    return *x >= 0 && *x < width && *y >= 0 && *y < height;
}

// Enemies and neutral NPCs that are close and on screen can be locked.
// Companions never can.
static bool rt_is_lock_candidate(Object* critter)
{
    if (critter == obj_dude || !rt_lock_is_valid(critter)) {
        return false;
    }

    if (obj_dist(obj_dude, critter) > RT_TARGET_MAX_DISTANCE) {
        return false;
    }

    int x;
    int y;
    return rt_screen_pos(critter, &x, &y);
}

static Object* rt_find_lock_candidate()
{
    // The nearest enemy; the nearest neutral NPC only when no enemy is
    // around. Targets in plain view win over blocked ones.
    Object* best = NULL;
    int bestScore = 0;
    for (Object* critter : rt_critters) {
        if (!rt_is_lock_candidate(critter)) {
            continue;
        }

        int score = obj_dist(obj_dude, critter);
        if (combat_is_shot_blocked(obj_dude, obj_dude->tile, critter->tile, critter, NULL)) {
            score += 100;
        }

        if (!rt_is_hostile_to_player(critter)) {
            score += 1000;
        }

        if (best == NULL || score < bestScore) {
            best = critter;
            bestScore = score;
        }
    }

    return best;
}

// The nearest lock candidate from `from` in the flicked screen direction,
// or NULL when there is none.
static Object* rt_find_switch_target(Object* from, float fx, float fy)
{
    int cx;
    int cy;
    rt_screen_pos(from, &cx, &cy);

    Object* best = NULL;
    float bestScore = 0.0f;
    for (Object* critter : rt_critters) {
        if (critter == from || !rt_is_lock_candidate(critter)) {
            continue;
        }

        int x;
        int y;
        rt_screen_pos(critter, &x, &y);

        float dx = (float)(x - cx);
        float dy = (float)(y - cy);
        float length = sqrtf(dx * dx + dy * dy);
        if (length < 1.0f) {
            continue;
        }

        float cosine = (dx * fx + dy * fy) / length;
        if (cosine < RT_SWITCH_CONE_COS) {
            continue;
        }

        // Close targets well aligned with the flick win.
        float score = length * (2.0f - cosine);
        if (best == NULL || score < bestScore) {
            best = critter;
            bestScore = score;
        }
    }

    return best;
}

// Moves the lock to the nearest candidate in the flicked screen direction,
// or keeps it when there is none.
static void rt_switch_lock(float fx, float fy)
{
    Object* best = rt_find_switch_target(rt_lock, fx, fy);
    if (best == NULL) {
        rt_stats.lockSwitchMisses++;
        rt_log("lock: no target in direction (%.2f, %.2f), keeping %s", fx, fy, critter_name(rt_lock));
        return;
    }

    rt_stats.lockSwitches++;
    rt_log("lock: switch (%.2f, %.2f)", fx, fy);
    rt_set_lock(best);
}

static void rt_set_lock(Object* target)
{
    if (target == rt_lock) {
        return;
    }

    // Held fire and a buffered press never carry over to another target
    // (nor the presses counted for its timing).
    rt_timing_clear("target changed");
    rt_lock = target;
    rt_fire_hold = false;
    rt_attack_buffered_until = 0;
    rt_throw_buffered_until = 0;

    if (target == NULL) {
        rt_log("lock: none");
        return;
    }

    rt_stats.locks++;

    bool hostile = rt_is_hostile_to_player(target);
    rt_print("Locked: %s%s", critter_name(target), hostile ? "" : " (not hostile)");
    rt_log("lock: %s at distance %d (%s)", critter_name(target), obj_dist(obj_dude, target), hostile ? "hostile" : "neutral");

    // Turning to face the target is not an attack and alerts no one.
    if (!anim_busy(obj_dude) && !rt_obj_in_attack(obj_dude)) {
        register_begin(ANIMATION_REQUEST_RESERVED);
        register_object_turn_towards(obj_dude, target->tile);
        register_end();
    }
}

// -----------------------------------------------------------------------------
// Lock-on indicator
// -----------------------------------------------------------------------------

// The original crosshair cursor art with the hit chance to its right, as the
// vanilla combat cursor shows it (see `gmouse_3d_build_to_hit_frame`).
static bool rt_indicator_build(const char* text, int color)
{
    if (rt_indicator_buf == NULL) {
        CacheEntry* handle;
        Art* art = art_ptr_lock(gmouse_get_crosshair_fid(), &handle);
        if (art == NULL) {
            return false;
        }

        rt_indicator_cross_width = art_frame_width(art, 0, 0);
        rt_indicator_cross_height = art_frame_length(art, 0, 0);
        art_ptr_unlock(handle);

        int oldFont = text_curr();
        text_font(101);
        int textHeight = text_height();
        text_font(oldFont);

        rt_indicator_width = rt_indicator_cross_width + 90;
        rt_indicator_height = std::max(rt_indicator_cross_height, textHeight + 2);
        rt_indicator_buf = new unsigned char[rt_indicator_width * rt_indicator_height];
        rt_indicator_built_color = -1;
    }

    if (color == rt_indicator_built_color && strcmp(text, rt_indicator_built_text) == 0) {
        return true;
    }

    memset(rt_indicator_buf, 0, rt_indicator_width * rt_indicator_height);

    CacheEntry* handle;
    Art* art = art_ptr_lock(gmouse_get_crosshair_fid(), &handle);
    if (art == NULL) {
        return false;
    }

    buf_to_buf(art_frame_data(art, 0, 0),
        rt_indicator_cross_width,
        rt_indicator_cross_height,
        rt_indicator_cross_width,
        rt_indicator_buf + rt_indicator_width * ((rt_indicator_height - rt_indicator_cross_height) / 2),
        rt_indicator_width);
    art_ptr_unlock(handle);

    int oldFont = text_curr();
    text_font(101);
    int textY = (rt_indicator_height - text_height()) / 2;
    int textX = rt_indicator_cross_width + 2;
    text_to_buf(rt_indicator_buf + rt_indicator_width * textY + textX,
        text,
        rt_indicator_width - textX - 1,
        rt_indicator_width,
        color);
    buf_outline(rt_indicator_buf + rt_indicator_cross_width,
        rt_indicator_width - rt_indicator_cross_width,
        rt_indicator_height,
        rt_indicator_width,
        colorTable[0]);
    text_font(oldFont);

    SDL_strlcpy(rt_indicator_built_text, text, sizeof(rt_indicator_built_text));
    rt_indicator_built_color = color;
    return true;
}

static void rt_update_indicator()
{
    RtIndicator next;
    memset(&next, 0, sizeof(next));
    next.toHit = -1;

    int color = colorTable[32767];
    if (rt_lock != NULL && rt_is_known_critter(rt_lock) && obj_dude->elevation == map_elevation) {
        Rect bounds;
        obj_bound(rt_lock, &bounds);
        next.visible = true;
        next.x = (bounds.ulx + bounds.lrx) / 2;
        next.y = (bounds.uly + bounds.lry) / 2;

        // The same checks and hit chance the attack itself uses, for the
        // equipped weapon and the selected attack mode.
        int hitMode;
        if (rt_player_hit_mode(&hitMode) != 0) {
            next.badShot = -1;
            SDL_strlcpy(next.text, "No weapon", sizeof(next.text));
        } else {
            next.badShot = rt_check_shot(obj_dude, rt_lock, hitMode);
            switch (next.badShot) {
            case COMBAT_BAD_SHOT_OK:
                // A buffered RT press shows the hit chance its timing would give.
                rt_timing_committing = !rt_is_throw(hitMode) ? rt_timing_preview() : 0;
                next.toHit = determine_to_hit(obj_dude, rt_lock, HIT_LOCATION_UNCALLED, hitMode);
                rt_timing_committing = 0;
                snprintf(next.text, sizeof(next.text), "%d%%", next.toHit);
                // Close combat and throws cost AP: say so while there isn't
                // enough.
                if (rt_attack_costs_ap(hitMode) && rt_dude_ap + 0.0001f < (float)rt_attack_ap_cost(obj_dude, hitMode, false)) {
                    char cost[16];
                    snprintf(next.text, sizeof(next.text), "%d%%  %s AP needed", next.toHit, rt_ap_text(rt_attack_ap_cost(obj_dude, hitMode, false), cost, sizeof(cost)));
                    color = colorTable[(31 << 10) | (24 << 5) | 0];
                }
                break;
            case COMBAT_BAD_SHOT_OUT_OF_RANGE:
                SDL_strlcpy(next.text, "Out of range", sizeof(next.text));
                break;
            case COMBAT_BAD_SHOT_AIM_BLOCKED:
                SDL_strlcpy(next.text, "Blocked", sizeof(next.text));
                break;
            case COMBAT_BAD_SHOT_NO_AMMO:
                SDL_strlcpy(next.text, "No ammo", sizeof(next.text));
                break;
            default:
                SDL_strlcpy(next.text, "Can't attack", sizeof(next.text));
                break;
            }
        }

        if (next.toHit < 0) {
            color = colorTable[31744];
        }
    }

    Rect bounds = { 0, 0, -1, -1 };
    if (next.visible) {
        if (!rt_indicator_build(next.text, color)) {
            next.visible = false;
        } else {
            obj_bound(rt_lock, &bounds);
        }
    }

    bool textChanged = strcmp(next.text, rt_indicator.text) != 0;
    rt_indicator = next;
    rt_overlay_set(&rt_indicator_overlay, next.visible, &bounds, textChanged);
    if (rt_indicator.visible) {
        rt_indicator.x = rt_indicator_overlay.shown.ulx + rt_indicator_cross_width / 2;
        rt_indicator.y = rt_indicator_overlay.shown.uly + rt_indicator_height / 2;
    }
}

// Where `overlay` goes in the play area: the crosshair on the center of its
// object, the label centered above it (below when there is no room), kept
// inside the play area.
static void rt_overlay_layout(const RtOverlay* overlay, Rect* rect)
{
    rect->ulx = 0;
    rect->uly = 0;
    rect->lrx = -1;
    rect->lry = -1;
    if (!overlay->visible) {
        return;
    }

    Rect bounds;
    rt_view_world_rect_to_display(&(overlay->world), &bounds);

    if (overlay->kind == RT_OVERLAY_INDICATOR) {
        int x = (bounds.ulx + bounds.lrx) / 2;
        int y = (bounds.uly + bounds.lry) / 2;
        rect->ulx = x - rt_indicator_cross_width / 2;
        rect->uly = y - rt_indicator_height / 2;
        rect->lrx = rect->ulx + rt_indicator_width - 1;
        rect->lry = rect->uly + rt_indicator_height - 1;
        return;
    }

    int width;
    int height;
    rt_get_play_area(&width, &height);
    int x = (bounds.ulx + bounds.lrx) / 2 - rt_label_width / 2;
    int y = bounds.uly - rt_label_height - 2;
    if (y < 2) {
        y = bounds.lry + 4;
    }
    x = std::max(2, std::min(x, width - rt_label_width - 2));
    y = std::max(2, std::min(y, height - rt_label_height - 2));
    rect->ulx = x;
    rect->uly = y;
    rect->lrx = x + rt_label_width - 1;
    rect->lry = y + rt_label_height - 1;
}

// Moves the overlay to where its object is now. With `invalidate` the areas
// it left and entered are shown again right away; `force` redraws it in
// place (new content).
static void rt_overlay_sync(RtOverlay* overlay, bool invalidate, bool force)
{
    Rect rect;
    rt_overlay_layout(overlay, &rect);
    bool same = rect.ulx == overlay->shown.ulx
        && rect.uly == overlay->shown.uly
        && rect.lrx == overlay->shown.lrx
        && rect.lry == overlay->shown.lry;
    if (same && !force) {
        return;
    }

    Rect old = overlay->shown;
    overlay->shown = rect;
    if (invalidate) {
        if (old.ulx <= old.lrx && old.uly <= old.lry) {
            rt_view_present_display(&old);
        }
        if (!same && rect.ulx <= rect.lrx && rect.uly <= rect.lry) {
            rt_view_present_display(&rect);
        }
    }
}

// Shows `overlay` over the object with bounds `world` (or hides it).
static void rt_overlay_set(RtOverlay* overlay, bool visible, const Rect* world, bool contentChanged)
{
    overlay->visible = visible;
    if (visible) {
        overlay->world = *world;
    }
    rt_overlay_sync(overlay, true, contentChanged);
}

void rt_overlays_relayout(bool invalidate)
{
    rt_overlay_sync(&rt_indicator_overlay, invalidate, false);
    rt_overlay_sync(&rt_label, invalidate, false);
}

static void rt_overlay_draw(const RtOverlay* overlay, unsigned char* src, int srcPitch, unsigned char* buffer, int pitch, Rect* rect)
{
    if (src == NULL || overlay->shown.lrx < overlay->shown.ulx) {
        return;
    }

    Rect clipped;
    if (rect_inside_bound(&(overlay->shown), rect, &clipped) != 0) {
        return;
    }

    int srcX = clipped.ulx - overlay->shown.ulx;
    int srcY = clipped.uly - overlay->shown.uly;
    trans_buf_to_buf(src + srcPitch * srcY + srcX,
        clipped.lrx - clipped.ulx + 1,
        clipped.lry - clipped.uly + 1,
        srcPitch,
        buffer + pitch * clipped.uly + clipped.ulx,
        pitch);
}

void rt_render_overlay(unsigned char* buffer, int pitch, Rect* rect)
{
    rt_overlay_draw(&rt_label, rt_label_buf, rt_label_width, buffer, pitch, rect);
    rt_overlay_draw(&rt_indicator_overlay, rt_indicator_buf, rt_indicator_width, buffer, pitch, rect);
}

// -----------------------------------------------------------------------------
// Interaction label
// -----------------------------------------------------------------------------

// The highlighted object's name and what A does, above it, once the player
// is within reach of it. Drawn in the interaction color, unlike the lock-on
// crosshair.
static void rt_update_label(Object* target)
{
    char text[sizeof(rt_label_text)] = "";
    if (target != NULL && tile_dist(obj_dude->tile, target->tile) <= RT_INTERACT_REACH) {
        char actions[96];
        rt_context_describe(target, actions, sizeof(actions));
        snprintf(text, sizeof(text), "%s: A %s", rt_name(target), actions);
    }

    bool visible = text[0] != '\0';
    bool textChanged = strcmp(text, rt_label_text) != 0;
    if (textChanged) {
        SDL_strlcpy(rt_label_text, text, sizeof(rt_label_text));
    }

    if (visible) {
        int oldFont = text_curr();
        text_font(101);
        int lineHeight = text_height() + 1;

        char lines[2][sizeof(rt_label_text)];
        int lineCount = 0;
        const char* start = rt_label_text;
        while (start != NULL && lineCount < 2) {
            const char* end = strchr(start, '\n');
            size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
            memcpy(lines[lineCount], start, length);
            lines[lineCount][length] = '\0';
            lineCount++;
            start = end != NULL ? end + 1 : NULL;
        }

        if (textChanged || rt_label_buf == NULL) {
            int width = 0;
            for (int line = 0; line < lineCount; line++) {
                width = std::max(width, text_width(lines[line]));
            }
            rt_label_width = width + 4;
            rt_label_height = lineHeight * lineCount + 3;
            if (rt_label_width * rt_label_height > rt_label_capacity) {
                delete[] rt_label_buf;
                rt_label_capacity = rt_label_width * rt_label_height;
                rt_label_buf = new unsigned char[rt_label_capacity];
            }
            memset(rt_label_buf, 0, rt_label_width * rt_label_height);
            for (int line = 0; line < lineCount; line++) {
                int lineWidth = text_width(lines[line]);
                text_to_buf(rt_label_buf + rt_label_width * (1 + line * lineHeight) + (rt_label_width - lineWidth) / 2,
                    lines[line],
                    rt_label_width - 2,
                    rt_label_width,
                    line == 0 ? colorTable[32747] : colorTable[992]);
            }
            buf_outline(rt_label_buf, rt_label_width, rt_label_height, rt_label_width, colorTable[0]);
        }
        text_font(oldFont);
    }

    // Centered above the object, kept inside the play area.
    Rect bounds = { 0, 0, -1, -1 };
    if (visible) {
        obj_bound(target, &bounds);
    }
    rt_overlay_set(&rt_label, visible, &bounds, textChanged);
}

// -----------------------------------------------------------------------------
// Player attacks
// -----------------------------------------------------------------------------

static int rt_player_hit_mode(int* hitMode)
{
    bool aiming;
    if (intface_get_attack(hitMode, &aiming) == 0) {
        return 0;
    }
    return -1;
}

static bool rt_is_automatic(int hitMode)
{
    int anim = item_w_anim(obj_dude, hitMode);
    return anim == ANIM_FIRE_BURST || anim == ANIM_FIRE_CONTINUOUS;
}

// RT with a chem (drug) in the active item slot, or a chem picked from the
// favorites, uses it on the player through the same action as using it on
// yourself in the original game (the hands animation, then one item from the
// stack is taken). One use per press.
static void rt_request_chem(Object* chem)
{
    rt_chem_request_item = chem;
    rt_chem_requested_until = rt_now + RT_CHEM_BUFFER_MS;
}

static void rt_update_chem_request()
{
    if (rt_chem_requested_until == 0) {
        return;
    }

    Object* chem = rt_chem_request_item;
    if (chem == NULL || inven_rt_quantity(chem) == 0) {
        rt_chem_requested_until = 0;
        rt_chem_request_item = NULL;
        rt_print("You don't have that any more.");
        return;
    }

    // An attack involving the player, a reload or a chem still being used:
    // the press waits for it to end.
    if (rt_obj_in_attack(obj_dude) || rt_reload_active || rt_chem_active) {
        if (rt_now > rt_chem_requested_until) {
            rt_chem_requested_until = 0;
            rt_chem_request_item = NULL;
            rt_print("You can't use that right now.");
        }
        return;
    }

    rt_chem_requested_until = 0;
    rt_chem_request_item = NULL;
    rt_move_active = false;
    rt_set_interact_highlight(NULL);

    if (action_use_an_item_on_object(obj_dude, obj_dude, chem) == -1) {
        rt_log("chem: could not use %s", object_name(chem));
        rt_print("You can't use that right now.");
        return;
    }

    rt_chem_active = true;
    rt_stats.chemUses++;
    rt_print("You use the %s.", object_name(chem));
    rt_log("chem: using %s (%d in the stack)", object_name(chem), item_count(obj_dude, chem));
}

// A favorite chosen in the radial: weapons and armor wait for an attack,
// reload or chem animation of the player's to end, then change hands.
static void rt_update_favorite_request()
{
    if (rt_fav_pending_slot == -1) {
        return;
    }

    int rc = rt_favorites_activate(rt_fav_pending_slot);
    if (rc != RT_FAVORITES_WAIT) {
        rt_fav_pending_slot = -1;
        return;
    }

    if (rt_now > rt_fav_pending_until) {
        rt_fav_pending_slot = -1;
        rt_favorites_refuse("You can't do that right now.");
    }
}

// -----------------------------------------------------------------------------
// Timing of repeated manual attacks
// -----------------------------------------------------------------------------

// Whether the player's own attack (a swing, an attack in flight) or the
// recovery after it is in progress.
static bool rt_timing_player_recovering()
{
    if (rt_swing.active || rt_now < rt_dude_next_attack_at) {
        return true;
    }

    for (RtPendingAttack* pending : rt_pending) {
        if (pending->attack.attacker == obj_dude) {
            return true;
        }
    }
    return false;
}

// Forgets the presses counted for the next attack. With a `reason`, the
// buffered press goes too (cancelled: weapon change, menu, disconnect...).
static void rt_timing_clear(const char* reason)
{
    if (reason != NULL) {
        if (rt_timing_presses > 0 || rt_attack_buffered_until != 0) {
            rt_stats.timingClears++;
            rt_log("timing: presses and penalty cleared (%s)", reason);
        }
        rt_attack_buffered_until = 0;
    }

    rt_timing_presses = 0;
    rt_timing_press_at = 0;
    rt_timing_weapon = NULL;
    rt_timing_hit_mode = -1;
    rt_timing_committing = 0;
}

// The hit chance penalty of the next RT attack if the current attack's
// recovery ended at `readyAt`: extra presses during the attack (spam) and a
// buffered press made well before that end (early) cost accuracy; a press
// near the end, or a fresh press after it, costs nothing.
static int rt_timing_penalty_for(unsigned int readyAt, int* spamOut, int* leadOut)
{
    const RtRules* rules = rt_rules();
    int spam = rt_timing_presses > 1 ? rt_timing_presses - 1 : 0;
    int lead = rt_timing_press_at != 0 && readyAt > rt_timing_press_at ? (int)(readyAt - rt_timing_press_at) : 0;

    int early = 0;
    int window = rules->timingWindowMs;
    if (lead > window) {
        early = window > 0
            ? rules->timingEarlyPenalty * std::min(lead - window, window) / window
            : rules->timingEarlyPenalty;
    }

    if (spamOut != NULL) {
        *spamOut = spam;
    }
    if (leadOut != NULL) {
        *leadOut = lead;
    }
    return std::clamp(early + spam * rules->timingSpamPenalty, 0, rules->timingMaxPenalty);
}

// The penalty a buffered press would get if its attack started now.
static int rt_timing_preview()
{
    if (rt_attack_buffered_until == 0 || rt_now > rt_attack_buffered_until || rt_timing_presses == 0) {
        return 0;
    }
    return rt_timing_penalty_for(rt_timing_recovering ? rt_now : rt_timing_ready_at, NULL, NULL);
}

int rt_player_timing_penalty()
{
    return rt_timing_committing;
}

int rt_debug_timing_preview()
{
    return rt_timing_preview();
}

int rt_debug_recovery_left_ms()
{
    if (rt_swing.active) {
        return -1;
    }
    for (RtPendingAttack* pending : rt_pending) {
        if (pending->attack.attacker == obj_dude) {
            return -1;
        }
    }
    return rt_now < rt_dude_next_attack_at ? (int)(rt_dude_next_attack_at - rt_now) : 0;
}

// Bookkeeping for each tick: when the recovery ends, and what clears the
// counted presses (a menu, focus loss or controller disconnect in between,
// or another weapon or attack mode).
static void rt_update_timing(Object* activeItem, int hitMode)
{
    // (Held fire is released by the controller layer itself on focus loss
    // or a disconnect.)
    // (A menu is caught in `rt_tick`.)
    int interruptions = gamepad_get_interruption_count();
    if (interruptions != rt_timing_interruptions) {
        rt_timing_clear("focus loss or controller disconnect");
    }
    rt_timing_interruptions = interruptions;

    if ((rt_timing_presses > 0 || rt_attack_buffered_until != 0)
        && rt_timing_hit_mode != -1
        && (activeItem != rt_timing_weapon || hitMode != rt_timing_hit_mode)) {
        rt_timing_clear("weapon or attack mode changed");
    }

    bool recovering = rt_timing_player_recovering();
    if (rt_timing_recovering && !recovering) {
        rt_timing_ready_at = rt_now;
    }
    rt_timing_recovering = recovering;
}

static void rt_update_attack_input(bool pressAttack)
{
    Object* activeItem = NULL;
    if (intface_get_current_item(&activeItem) == 0 && activeItem != NULL && item_get_type(activeItem) == ITEM_TYPE_DRUG) {
        rt_fire_hold = false;
        rt_attack_buffered_until = 0;
        rt_timing_clear(NULL);
        if (pressAttack) {
            rt_request_chem(activeItem);
        }
        return;
    }

    int hitMode;
    bool hasHitMode = rt_player_hit_mode(&hitMode) == 0;
    rt_update_timing(activeItem, hasHitMode ? hitMode : -1);

    bool held = gamepad_is_held(GAMEPAD_BUTTON_RIGHT_TRIGGER);

    if (pressAttack) {
        // One buffered press at most; it expires if it can't run soon.
        // Presses during the current attack or its recovery are counted for
        // the next attack's timing (see `rt_timing_penalty_for`); further
        // presses never queue more attacks.
        if (rt_timing_recovering) {
            rt_timing_presses++;
            rt_timing_press_at = rt_now;
            rt_stats.timingBusyPresses++;
            if (rt_timing_presses > 1) {
                rt_stats.timingSpamPresses++;
            }
        } else {
            rt_timing_presses = 0;
            rt_timing_press_at = 0;
        }
        rt_timing_weapon = activeItem;
        rt_timing_hit_mode = hasHitMode ? hitMode : -1;

        rt_log("input: RT pressed (lock=%s%s)", rt_lock != NULL ? critter_name(rt_lock) : "none",
            rt_timing_recovering ? ", during the current attack" : "");
        rt_attack_buffered_until = rt_now + RT_ATTACK_BUFFER_MS;
        rt_fire_hold = true;
        rt_fire_pressed_at = rt_now;
        rt_fire_press_done = false;
    }

    if (!held) {
        rt_fire_hold = false;
    }

    bool automatic = hasHitMode && rt_is_automatic(hitMode);

    bool buffered = rt_attack_buffered_until != 0 && rt_now <= rt_attack_buffered_until;
    // A hold repeats only automatic attacks, and only once the press's own
    // attack has started and the trigger stayed down past the hold threshold,
    // so the press itself never turns into an extra shot or burst.
    bool repeat = rt_fire_hold
        && automatic
        && !buffered
        && rt_fire_press_done
        && rt_now - rt_fire_pressed_at >= RT_HOLD_REPEAT_MS;
    if (!buffered && !repeat) {
        if (rt_attack_buffered_until != 0) {
            // The buffered press expired: its presses no longer count.
            rt_timing_clear(NULL);
        }
        rt_attack_buffered_until = 0;
        return;
    }

    // The press's attack carries its timing penalty, taken now and used
    // once (not for held automatic fire or throws).
    int spam = 0;
    int lead = 0;
    int penalty = 0;
    if (buffered && hasHitMode && !rt_is_throw(hitMode)) {
        penalty = rt_timing_penalty_for(rt_timing_ready_at, &spam, &lead);
    }
    rt_timing_committing = penalty;
    int rc = rt_player_attack(pressAttack);
    rt_timing_committing = 0;

    if (rc == RT_PLAYER_ATTACK_STARTED) {
        if (buffered) {
            rt_stats.lastTimingPenalty = penalty;
            rt_stats.lastTimingSpam = spam;
            rt_stats.lastTimingLeadMs = lead;
            if (penalty > 0 || spam > 0 || lead > 0) {
                rt_log("timing: attack started %d ms after its press (before the recovery end), %d extra press(es): -%d%% to hit",
                    lead, spam, penalty);
            }
            rt_timing_clear(NULL);
        }
        rt_attack_buffered_until = 0;
        rt_fire_press_done = true;
    } else if (rc == RT_PLAYER_ATTACK_FAILED) {
        // Nothing to retry: no target, out of range, empty...
        rt_attack_buffered_until = 0;
        rt_fire_hold = false;
        rt_timing_clear(NULL);
    }
}

// RB: throws the right hand's thrown weapon at the locked target, the same
// attack as RT with it active (lock, range, line of sight, cooldown, 3 AP),
// while the active item stays in the hand it is in.
static void rt_update_throw_input()
{
    if (rt_throw_buffered_until == 0) {
        return;
    }

    if (!rt_is_thrown_weapon(inven_right_hand(obj_dude))) {
        rt_throw_buffered_until = 0;
        return;
    }

    if (rt_now > rt_throw_buffered_until) {
        rt_throw_buffered_until = 0;
        rt_stats.rbThrowRefused++;
        rt_print("You can't throw right now.");
        return;
    }

    int rc = rt_player_attack_with(HIT_MODE_RIGHT_WEAPON_PRIMARY, true);
    if (rc == RT_PLAYER_ATTACK_WAIT) {
        return;
    }

    rt_throw_buffered_until = 0;
    if (rc == RT_PLAYER_ATTACK_FAILED) {
        rt_stats.rbThrowRefused++;
    }
}

// The throw animation belongs to the thrown weapon's sprite set (grenades
// use the bare-handed one), not the active weapon's: switch to it for the
// throw. `rt_sync_weapon_sprite` puts the active weapon's back afterwards.
static void rt_set_throw_sprite(Object* item)
{
    if (item == NULL) {
        return;
    }

    int current = (obj_dude->fid & 0xF000) >> 12;
    int code = item_w_anim_code(item);
    int throwFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_THROW_ANIM, code, obj_dude->rotation + 1);
    if (!art_exists(throwFid)) {
        code = 0;
    }
    if (current == code) {
        return;
    }

    int fid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_STAND, code, obj_dude->rotation + 1);
    Rect rect;
    if (obj_change_fid(obj_dude, fid, &rect) == 0) {
        tile_refresh_rect(&rect, obj_dude->elevation);
    }
    rt_log("throw: sprite set %d -> %d for %s", current, code, object_name(item));
}

static int rt_player_attack(bool explicitPress)

{
    int hitMode;
    if (rt_player_hit_mode(&hitMode) != 0) {
        rt_print("The active item can't attack. Press RB to switch hands or X to reload.");
        return RT_PLAYER_ATTACK_FAILED;
    }

    return rt_player_attack_with(hitMode, false);
}

// An ordinary attack with `hitMode`: RT with the active item, or RB's throw
// of the right hand's thrown weapon (`rbThrow`), which leaves the active
// item as it is.
static int rt_player_attack_with(int hitMode, bool rbThrow)
{

    // Melee and unarmed: the lock is optional. A locked target in reach is
    // attacked as before; otherwise the press swings anyway, and the swing
    // hits whoever is in reach in front of the player at its impact.
    // Two-handed melee weapons always swing, so they can hit everyone in
    // their arc; the lock is what they aim at first.
    bool closeCombat = rt_is_close_combat(hitMode);
    if (closeCombat) {
        if (rt_reload_active || rt_holster_active) {
            return RT_PLAYER_ATTACK_WAIT;
        }
        if (rt_lock == NULL || rt_check_shot(obj_dude, rt_lock, hitMode) != COMBAT_BAD_SHOT_OK || rt_is_sweep(hitMode)) {
            return rt_player_swing(hitMode);
        }
    }

    // No lock, no ranged attack: RT never picks a target by itself.
    Object* target = rt_lock;
    if (target == NULL) {
        rt_print("No target locked. Press LT to lock on.");
        rt_stats.playerRejected++;
        rt_stats.lastRejectReason = -1;
        return RT_PLAYER_ATTACK_FAILED;
    }

    // No firing until a reload is complete.
    if (rt_reload_active || rt_holster_active) {
        return RT_PLAYER_ATTACK_WAIT;
    }

    // The lock never bypasses range, line of sight or ammunition rules.
    int shot = rt_check_shot(obj_dude, target, hitMode);
    if (shot != COMBAT_BAD_SHOT_OK) {
        Object* weapon = item_hit_with(obj_dude, hitMode);
        switch (shot) {
        case COMBAT_BAD_SHOT_NO_AMMO:
            gsound_play_sfx_file(gsnd_build_weapon_sfx_name(WEAPON_SOUND_EFFECT_OUT_OF_AMMO, weapon, hitMode, target));
            rt_print("Out of ammo. Press X to reload.");
            break;
        case COMBAT_BAD_SHOT_OUT_OF_RANGE:
            rt_print("%s is out of range.", critter_name(target));
            break;
        case COMBAT_BAD_SHOT_AIM_BLOCKED:
            rt_print("Your aim is blocked.");
            break;
        case COMBAT_BAD_SHOT_ARM_CRIPPLED:
        case COMBAT_BAD_SHOT_BOTH_ARMS_CRIPPLED:
            rt_print("You can't use that weapon with crippled arms.");
            break;
        default:
            break;
        }

        rt_log("player attack rejected: bad shot %d on %s (dist %d)", shot, critter_name(target), obj_dist(obj_dude, target));
        rt_stats.playerRejected++;
        rt_stats.lastRejectReason = shot;
        return RT_PLAYER_ATTACK_FAILED;
    }

    // Valid attack: wait for the previous one (or an enemy hit on the
    // player) to finish and for the weapon to be ready.
    if (rt_obj_in_attack(obj_dude) || rt_obj_in_attack(target) || rt_now < rt_dude_next_attack_at) {
        static unsigned int lastWaitLog = 0;
        if (rt_now - lastWaitLog >= 500) {
            lastWaitLog = rt_now;
            rt_log("player attack waits: player in attack=%d, target in attack=%d, cooldown left=%d ms",
                rt_obj_in_attack(obj_dude),
                rt_obj_in_attack(target),
                rt_now < rt_dude_next_attack_at ? (int)(rt_dude_next_attack_at - rt_now) : 0);
        }
        return RT_PLAYER_ATTACK_WAIT;
    }

    // Melee, unarmed attacks and throws cost AP (gunfire doesn't). The cost
    // is known before the attack: a throw takes the item from the hand.
    bool costsAp = rt_attack_costs_ap(hitMode);
    bool thrown = rt_is_throw(hitMode);
    double cost = costsAp ? rt_attack_ap_cost(obj_dude, hitMode, false) : 0.0;
    if (costsAp && !rt_attack_ap_ok(hitMode)) {
        return RT_PLAYER_ATTACK_FAILED;
    }

    if (rt_move_active) {
        rt_move_active = false;
    }

    Object* thrownItem = rbThrow ? item_hit_with(obj_dude, hitMode) : NULL;
    if (rbThrow) {
        rt_set_throw_sprite(thrownItem);
    }

    int rc = rt_attack(obj_dude, target, hitMode);
    if (rc == 0) {
        if (costsAp) {
            rt_spend_attack_ap(cost, thrown);
        }
        if (rbThrow) {
            Object* active = NULL;
            intface_get_current_item(&active);
            rt_stats.rbThrows++;
            rt_stats.lastRbThrowPid = thrownItem != NULL ? thrownItem->pid : -1;
            rt_stats.lastRbThrowActiveItem = active;
            rt_log("throw: RB threw %s at %s, active item %s", thrownItem != NULL ? object_name(thrownItem) : "?",
                critter_name(target), active != NULL ? object_name(active) : "none");
        }
        rt_set_interact_highlight(NULL);
        return RT_PLAYER_ATTACK_STARTED;
    }

    if (rc == RT_ATTACK_BUSY) {
        return RT_PLAYER_ATTACK_WAIT;
    }

    rt_print("You can't attack that.");
    return RT_PLAYER_ATTACK_FAILED;
}

// -----------------------------------------------------------------------------
// AIMED SHOT and favorites support
// -----------------------------------------------------------------------------

Object* rt_aim_default_target()
{
    if (rt_lock != NULL && rt_is_lock_candidate(rt_lock)) {
        return rt_lock;
    }

    // The nearest enemy (in plain view first); neutral NPCs only through the
    // lock, never picked automatically.
    Object* best = NULL;
    int bestScore = 0;
    for (Object* critter : rt_critters) {
        if (!rt_is_lock_candidate(critter) || !rt_is_hostile_to_player(critter)) {
            continue;
        }

        int score = obj_dist(obj_dude, critter);
        if (combat_is_shot_blocked(obj_dude, obj_dude->tile, critter->tile, critter, NULL)) {
            score += 100;
        }

        if (best == NULL || score < bestScore) {
            best = critter;
            bestScore = score;
        }
    }
    return best;
}

Object* rt_aim_switch_target(Object* from, float fx, float fy)
{
    return from != NULL ? rt_find_switch_target(from, fx, fy) : NULL;
}

bool rt_aim_target_valid(Object* target)
{
    return target != NULL && rt_lock_is_valid(target);
}

int rt_aim_check_shot(Object* target, int hitMode)
{
    return rt_check_shot(obj_dude, target, hitMode);
}

bool rt_aim_player_in_attack()
{
    return rt_obj_in_attack(obj_dude);
}

int rt_aim_attack(Object* target, int hitMode, int hitLocation, float apCost)
{
    if (rt_reload_active || rt_chem_active || rt_holster_active) {
        return RT_AIM_ATTACK_WAIT;
    }

    if (rt_check_shot(obj_dude, target, hitMode) != COMBAT_BAD_SHOT_OK) {
        return RT_AIM_ATTACK_FAILED;
    }

    // The previous attack (or an enemy hit on the player) finishes first,
    // and the weapon must be ready, as for an RT press.
    if (rt_obj_in_attack(obj_dude) || rt_obj_in_attack(target) || rt_now < rt_dude_next_attack_at) {
        return RT_AIM_ATTACK_WAIT;
    }

    if (rt_dude_ap + 0.0001f < apCost) {
        return RT_AIM_ATTACK_FAILED;
    }

    rt_move_active = false;
    int rc = rt_attack(obj_dude, target, hitMode, hitLocation);
    if (rc == RT_ATTACK_BUSY) {
        return RT_AIM_ATTACK_WAIT;
    }
    if (rc != 0) {
        return RT_AIM_ATTACK_FAILED;
    }

    // Spent exactly once, when the attack is committed (hit or miss).
    // AIMED SHOT pays its own cost only (no ordinary melee cost on top).
    rt_spend_ap(apCost);
    return RT_AIM_ATTACK_STARTED;
}

bool rt_player_can_change_equipment()
{
    return !rt_obj_in_attack(obj_dude)
        && !rt_reload_active
        && !rt_chem_active
        && !rt_holster_active
        && !rt_aimed_is_executing();
}

bool rt_is_holstering()
{
    return rt_holster_active;
}

void rt_use_item_on_self(Object* item)
{
    rt_request_chem(item);
}

static void rt_set_outline(Object** slot, Object* obj, int outlineType)
{
    if (*slot == obj) {
        return;
    }

    Rect rect;
    if (*slot != NULL) {
        Object* previous = *slot;
        if ((previous->outline & OUTLINE_TYPE_MASK) != 0) {
            obj_remove_outline(previous, &rect);
            tile_refresh_rect(&rect, previous->elevation);
        }
    }

    *slot = NULL;

    if (obj != NULL && obj_outline_object(obj, outlineType, &rect) == 0) {
        tile_refresh_rect(&rect, obj->elevation);
        *slot = obj;
    }
}

// Can the player walk to `obj` (doors that can be opened count as passable)?
// Objects behind a wall are only reachable by a long detour.
static bool rt_interact_reachable(Object* obj)
{
    int distance = tile_dist(obj_dude->tile, obj->tile);
    if (distance <= 1) {
        return true;
    }

    auto it = rt_reach_cache.find(obj);
    if (it != rt_reach_cache.end() && it->second.dudeTile == obj_dude->tile && it->second.objTile == obj->tile) {
        return it->second.reachable;
    }

    // The destination itself may be blocking (a door, a container, a
    // critter); `make_path` doesn't check it.
    int steps = make_path(obj_dude, obj_dude->tile, obj->tile, NULL, 0);
    bool reachable = steps > 0 && steps <= RT_INTERACT_MAX_DISTANCE + 2;
    rt_reach_cache[obj] = { obj_dude->tile, obj->tile, reachable };
    return reachable;
}

// Screen angle of `obj` around the player, clockwise from straight up.
static float rt_interact_angle(Object* obj)
{
    int x1;
    int y1;
    int x2;
    int y2;
    tile_coord(obj_dude->tile, &x1, &y1, obj_dude->elevation);
    tile_coord(obj->tile, &x2, &y2, obj->elevation);
    if (x1 == x2 && y1 == y2) {
        return 0.0f;
    }
    float angle = atan2f((float)(x2 - x1), (float)(y1 - y2));
    return angle < 0.0f ? angle + 6.2831853f : angle;
}

// Everything A can act on right now: in reach, not hidden, with an ordinary
// or skill interaction (see `rt_context.h`), and not behind a wall. Ordered
// clockwise around the player (then by distance), so D-pad left/right step
// through them predictably.
static void rt_collect_interactables()
{
    rt_interact_candidates.clear();

    for (Object* obj = obj_find_first_at(obj_dude->elevation); obj != NULL; obj = obj_find_next_at()) {
        if (obj == obj_dude || (obj->flags & OBJECT_HIDDEN) != 0) {
            continue;
        }

        if (tile_dist(obj_dude->tile, obj->tile) > RT_INTERACT_MAX_DISTANCE) {
            continue;
        }

        int type = FID_TYPE(obj->fid);
        if (type != OBJ_TYPE_ITEM && type != OBJ_TYPE_CRITTER && type != OBJ_TYPE_SCENERY) {
            continue;
        }

        if (!rt_context_is_interactable(obj)) {
            continue;
        }

        rt_interact_candidates.push_back(obj);
    }

    // The path check is the expensive part: only for what passed the rest.
    rt_interact_candidates.erase(std::remove_if(rt_interact_candidates.begin(), rt_interact_candidates.end(), [](Object* obj) {
        return !rt_interact_reachable(obj);
    }),
        rt_interact_candidates.end());

    std::sort(rt_interact_candidates.begin(), rt_interact_candidates.end(), [](Object* a, Object* b) {
        float angleA = rt_interact_angle(a);
        float angleB = rt_interact_angle(b);
        if (fabsf(angleA - angleB) > 0.01f) {
            return angleA < angleB;
        }
        int distanceA = tile_dist(obj_dude->tile, a->tile);
        int distanceB = tile_dist(obj_dude->tile, b->tile);
        if (distanceA != distanceB) {
            return distanceA < distanceB;
        }
        return a->id < b->id;
    });

    if (rt_interact_manual != NULL
        && std::find(rt_interact_candidates.begin(), rt_interact_candidates.end(), rt_interact_manual) == rt_interact_candidates.end()) {
        rt_log("interact: selection %s no longer in reach", rt_name(rt_interact_manual));
        rt_interact_manual = NULL;
    }
}

// What A acts on: the one picked with the D-pad while it stays highlighted,
// otherwise the candidate the player faces (the stick direction while
// moving), one within reach first, nearer ones first.
static Object* rt_find_interactable()
{
    if (rt_interact_manual != NULL) {
        return rt_interact_manual;
    }

    float fx;
    float fy;
    if (!rt_facing_vector(&fx, &fy)) {
        return NULL;
    }

    Object* best = NULL;
    float bestScore = 1000.0f;
    for (Object* obj : rt_interact_candidates) {
        int distance = tile_dist(obj_dude->tile, obj->tile);
        float facing = rt_facing_cos(obj, fx, fy);
        float score = distance + (1.0f - facing) * 1.5f;
        if (distance > RT_INTERACT_REACH) {
            score += 10.0f;
        }
        if (score < bestScore) {
            bestScore = score;
            best = obj;
        }
    }

    return best;
}

// The candidate `delta` steps (1 clockwise, -1 counter-clockwise) from
// `current` among those within RT_INTERACT_CYCLE_DISTANCE hexes of it (map
// hex distance, measured from the highlighted object each time, so distant
// objects are never chained together), or NULL.
static Object* rt_next_interaction(Object* current, int delta)
{
    int count = (int)rt_interact_candidates.size();
    int index = (int)(std::find(rt_interact_candidates.begin(), rt_interact_candidates.end(), current) - rt_interact_candidates.begin());
    if (current == NULL || index >= count) {
        return NULL;
    }

    for (int step = 1; step < count; step++) {
        Object* candidate = rt_interact_candidates[((index + delta * step) % count + count) % count];
        if (tile_dist(candidate->tile, current->tile) <= RT_INTERACT_CYCLE_DISTANCE) {
            return candidate;
        }
    }
    return NULL;
}

static bool rt_can_cycle_interaction(Object* current)
{
    return rt_next_interaction(current, 1) != NULL;
}

// D-pad left/right: the previous/next nearby interaction becomes the
// highlighted one. Only the highlight changes: no interaction, no movement,
// never the combat lock.
static void rt_cycle_interaction(int delta)
{
    Object* current = rt_find_interactable();
    Object* next = rt_next_interaction(current, delta);
    if (next == NULL) {
        return;
    }

    rt_interact_manual = next;
    rt_stats.interactCycles++;
    gsound_play_sfx_file("ib1p1xx1");
    rt_log("interact: selected %s (%d hexes from %s, %d from the player)",
        rt_name(next), tile_dist(next->tile, current->tile), rt_name(current), tile_dist(next->tile, obj_dude->tile));
}

const char* rt_get_interact_label()
{
    return rt_label.visible ? rt_label_text : "";
}

int rt_get_interact_candidate_count()
{
    return (int)rt_interact_candidates.size();
}

int rt_get_interact_candidates(Object** out, int capacity)
{
    int count = 0;
    for (Object* obj : rt_interact_candidates) {
        if (count >= capacity) {
            break;
        }
        out[count++] = obj;
    }
    return count;
}

static void rt_interact(Object* obj)
{
    rt_set_interact_highlight(NULL);
    rt_move_active = false;
    rt_context_interact(obj);
}

static void rt_set_interact_highlight(Object* obj)
{
    int outlineType = OUTLINE_TYPE_ITEM;
    if (obj != NULL && FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER) {
        outlineType = OUTLINE_TYPE_FRIENDLY;
    }

    rt_set_outline(&rt_interact_outlined, obj, outlineType);
}

// Hunting Rifle reload: its idle animation again, if a whole one still fits
// before the reload ends (the pose is held otherwise).
static void rt_play_reload_idle()
{
    if (rt_reload_idle_anim_ms == 0 || rt_now + rt_reload_idle_anim_ms > rt_reload_until || rt_now < rt_reload_idle_next_at) {
        return;
    }

    register_begin(ANIMATION_REQUEST_RESERVED);
    register_object_animate(obj_dude, ANIM_STAND, 0);
    if (register_end() == 0) {
        rt_reload_idle_next_at = rt_now + rt_reload_idle_anim_ms;
    }
}

// How long the animation `fid` plays at its own frame rate.
static unsigned int rt_art_length_ms(int fid)
{
    CacheEntry* handle;
    Art* art = art_ptr_lock(fid, &handle);
    if (art == NULL) {
        return 0;
    }
    unsigned int ms = (unsigned int)(art_frame_max_frame(art) * 1000 / art_frame_fps(art));
    art_ptr_unlock(handle);
    return ms;
}

static bool rt_has_ammo_for(Object* weapon)
{
    int index = -1;
    Object* ammo;
    while ((ammo = inven_find_type(obj_dude, ITEM_TYPE_AMMO, &index)) != NULL) {
        if (item_w_can_reload(weapon, ammo)) {
            return true;
        }
    }
    return false;
}

static bool rt_weapon_in_hand(Object* weapon)
{
    return weapon != NULL && (inven_right_hand(obj_dude) == weapon || inven_left_hand(obj_dude) == weapon);
}

// The single place where a player reload moves ammunition into the weapon.
static int rt_reload_transfer(Object* weapon)
{
    int before = item_w_curr_ammo(weapon);
    while (item_w_try_reload(obj_dude, weapon) != -1) {
    }

    rt_stats.reloadAmmoTransfers++;
    gsound_play_sfx_file(gsnd_build_weapon_sfx_name(WEAPON_SOUND_EFFECT_READY, weapon, HIT_MODE_RIGHT_WEAPON_PRIMARY, NULL));
    intface_update_items(false);
    intface_update_ammo_lights();

    int loaded = item_w_curr_ammo(weapon) - before;
    rt_log("reload: ammo transferred (+%d, now %d/%d)", loaded, item_w_curr_ammo(weapon), item_w_max_ammo(weapon));
    return loaded;
}

// Reloading plays the weapon's own holster animation, moves the ammunition
// once the weapon is put away, then draws it again. The weapon stays
// equipped throughout. If the sequence is cut short (e.g. by an enemy hit)
// before the ammunition moved, nothing moved; afterwards, the reload counts.
static int rt_reload()
{
    if (rt_reload_active) {
        rt_log("reload: already reloading");
        return RT_RELOAD_FAILED;
    }

    // A holster in progress finishes first.
    if (rt_holster_active) {
        return RT_RELOAD_WAIT;
    }

    Object* item = NULL;
    if (intface_get_current_item(&item) != 0 || item == NULL || item_get_type(item) != ITEM_TYPE_WEAPON || item_w_max_ammo(item) <= 0) {
        rt_print("The active item doesn't use ammo.");
        return RT_RELOAD_FAILED;
    }

    if (item_w_curr_ammo(item) >= item_w_max_ammo(item)) {
        rt_print("Your weapon is fully loaded.");
        return RT_RELOAD_FAILED;
    }

    if (!rt_has_ammo_for(item)) {
        rt_print("You have no ammo for this weapon.");
        return RT_RELOAD_FAILED;
    }

    // An attack involving the player (theirs, or an enemy hit on them) is
    // still playing: the reload starts as soon as it is over.
    if (rt_obj_in_attack(obj_dude)) {
        return RT_RELOAD_WAIT;
    }

    rt_reload_active = true;
    rt_reload_committed = false;
    rt_reload_finished = false;
    rt_reload_weapon = item;
    rt_reload_until = 0;
    rt_stats.reloadsStarted++;
    rt_move_active = false;
    rt_set_interact_highlight(NULL);

    rt_reload_commit_at = 0;
    rt_reload_started_at = rt_now;

    int code = item_w_anim_code(item);
    int putAwayFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_PUT_AWAY, code, obj_dude->rotation + 1);
    int takeOutFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_TAKE_OUT, code, obj_dude->rotation + 1);
    bool animated = code != 0 && art_exists(putAwayFid) && art_exists(takeOutFid);

    // The Hunting Rifle reloads with its armed idle animation instead (the
    // one it fidgets with when idle), played over and over for as long as
    // the holster and draw would take; the ammunition moves where the
    // holster would have ended.
    int standFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_STAND, code, obj_dude->rotation + 1);
    if (animated && item->pid == PROTO_ID_HUNTING_RIFLE && art_exists(standFid)) {
        if (anim_busy(obj_dude)) {
            register_clear(obj_dude);
        }

        if (obj_dude->fid != standFid) {
            Rect rect;
            if (obj_change_fid(obj_dude, standFid, &rect) == 0) {
                tile_refresh_rect(&rect, obj_dude->elevation);
            }
        }

        unsigned int putAwayMs = rt_art_length_ms(putAwayFid);
        unsigned int takeOutMs = rt_art_length_ms(takeOutFid);
        rt_reload_commit_at = rt_now + putAwayMs;
        rt_reload_until = rt_reload_commit_at + takeOutMs;
        rt_reload_idle_code = code;
        rt_reload_idle_anim_ms = rt_art_length_ms(standFid);
        rt_reload_idle_next_at = 0;
        rt_play_reload_idle();
        rt_stats.idleReloads++;
        rt_log("reload: started in the idle pose (%s %d/%d, %u ms, ammo after %u ms)",
            object_name(item), item_w_curr_ammo(item), item_w_max_ammo(item), putAwayMs + takeOutMs, putAwayMs);
        return RT_RELOAD_STARTED;
    }

    if (animated) {
        if (anim_busy(obj_dude)) {
            register_clear(obj_dude);
        }

        register_begin(ANIMATION_REQUEST_RESERVED);
        register_object_play_sfx(obj_dude, gsnd_build_character_sfx_name(obj_dude, ANIM_PUT_AWAY, CHARACTER_SOUND_EFFECT_UNUSED), 0);
        register_object_animate(obj_dude, ANIM_PUT_AWAY, 0);
        register_object_call(item, NULL, (AnimationCallback*)rt_reload_commit, -1);
        register_object_take_out(obj_dude, code, -1);
        register_object_call(item, NULL, (AnimationCallback*)rt_reload_finish, -1);
        if (register_end() == 0) {
            rt_log("reload: started (%s %d/%d)", object_name(item), item_w_curr_ammo(item), item_w_max_ammo(item));
            return RT_RELOAD_STARTED;
        }

        rt_log("reload: animation could not start, using a timed reload");
    } else {
        rt_log("reload: no holster/draw animation for %s (weapon code %d), using a timed reload", object_name(item), code);
    }

    // Fallback: no animation; the weapon is busy for a moment instead.
    rt_stats.reloadFallbacks++;
    rt_reload_committed = true;
    rt_reload_transfer(item);
    rt_reload_until = rt_now + RT_RELOAD_FALLBACK_MS;
    return RT_RELOAD_STARTED;
}

static int rt_reload_commit(void* a1, void* a2)
{
    Object* weapon = (Object*)a1;
    if (!rt_reload_active || rt_reload_committed || weapon != rt_reload_weapon) {
        return 0;
    }

    rt_reload_committed = true;
    if (!rt_weapon_in_hand(weapon)) {
        rt_log("reload: weapon left the hand, nothing loaded");
        return 0;
    }

    rt_reload_transfer(weapon);
    return 0;
}

static int rt_reload_finish(void* a1, void* a2)
{
    if (rt_reload_active && a1 == rt_reload_weapon) {
        rt_reload_finished = true;
    }
    return 0;
}

static void rt_update_reload()
{
    if (!rt_reload_active) {
        return;
    }

    if (rt_reload_commit_at != 0) {
        // Idle animation reload: the ammunition moves once, at its time, if
        // the weapon is still in hand. Any other animation of the player's
        // starting meanwhile (a heavy hit, a fall) cuts it short: nothing
        // moved before that time, and afterwards the reload counts (as with
        // the holster).
        bool cut = FID_ANIM_TYPE(obj_dude->fid) != ANIM_STAND
            || ((obj_dude->fid & 0xF000) >> 12) != rt_reload_idle_code;
        if (!cut && !rt_reload_committed && rt_now >= rt_reload_commit_at) {
            rt_reload_commit(rt_reload_weapon, NULL);
        }
        if (!cut && !anim_busy(obj_dude)) {
            rt_play_reload_idle();
        }
        if (cut) {
            rt_reload_commit_at = 0;
            rt_reload_until = 0;
            rt_reload_active = false;
            rt_reload_weapon = NULL;
            rt_stats.lastReloadMs = rt_now - rt_reload_started_at;
            if (rt_reload_committed) {
                rt_stats.reloadsCompleted++;
                rt_log("reload: complete (idle pose cut short after the ammo moved)");
            } else {
                rt_stats.reloadsInterrupted++;
                rt_print("Reload interrupted.");
                rt_log("reload: idle pose interrupted before the ammo moved (nothing loaded)");
            }
            return;
        }
        if (rt_now < rt_reload_until) {
            return;
        }

        rt_reload_commit_at = 0;
        rt_reload_until = 0;
        rt_reload_active = false;
        rt_reload_weapon = NULL;
        rt_stats.reloadsCompleted++;
        rt_stats.lastReloadMs = rt_now - rt_reload_started_at;
        rt_log("reload: complete (idle pose, %u ms)", rt_stats.lastReloadMs);
        return;
    }

    if (rt_reload_until != 0) {
        if (rt_now < rt_reload_until) {
            return;
        }

        rt_reload_until = 0;
        rt_reload_active = false;
        rt_reload_weapon = NULL;
        rt_stats.reloadsCompleted++;
        rt_stats.lastReloadMs = rt_now - rt_reload_started_at;
        rt_log("reload: complete (timed)");
        return;
    }

    if (!rt_reload_finished && anim_busy(obj_dude)) {
        return;
    }

    rt_reload_active = false;
    rt_reload_weapon = NULL;

    rt_stats.lastReloadMs = rt_now - rt_reload_started_at;
    if (rt_reload_finished) {
        rt_stats.reloadsCompleted++;
        rt_log("reload: complete");
    } else if (rt_reload_committed) {
        // Loaded, but the draw animation was cut short; the weapon sprite is
        // restored by `rt_sync_weapon_sprite`.
        rt_stats.reloadsCompleted++;
        rt_log("reload: complete (draw interrupted)");
    } else {
        rt_stats.reloadsInterrupted++;
        rt_print("Reload interrupted.");
        rt_log("reload: interrupted before the ammo moved (nothing loaded)");
    }
}

// X: a tap reloads, a hold (rt_holster_hold_ms) holsters. Nothing happens
// on the press itself; a hold acts once when it reaches the threshold and
// its release does nothing. A press interrupted on the way (a menu, focus
// loss, a disconnect: no gameplay tick for a while, or the hold no longer
// valid while the button is still down) is dropped.
static void rt_update_x_button(bool pressX)
{
    unsigned int now = SDL_GetTicks();
    if (pressX) {
        rt_x_pending = true;
        rt_x_pressed_at = now;
        rt_x_seen_at = now;
        rt_x_interruptions = gamepad_get_interruption_count();
        return;
    }

    if (!rt_x_pending) {
        return;
    }

    bool gap = now - rt_x_seen_at > 250;
    rt_x_seen_at = now;
    if (gap || rt_x_interruptions != gamepad_get_interruption_count()) {
        rt_x_pending = false;
        rt_log("input: X press dropped (interrupted)");
        return;
    }

    if (gamepad_is_held(GAMEPAD_BUTTON_X)) {
        if (now - rt_x_pressed_at >= (unsigned int)rt_holster_hold_ms) {
            rt_x_pending = false;
            rt_holster_requested_until = rt_now + RT_RELOAD_BUFFER_MS;
            rt_log("input: X held %u ms: holster", now - rt_x_pressed_at);
        }
        return;
    }

    rt_x_pending = false;
    if (gamepad_get_state()->buttons[GAMEPAD_BUTTON_X]) {
        // Still down but no longer a valid hold: interrupted.
        rt_log("input: X press dropped (hold interrupted)");
        return;
    }

    rt_stats.reloadTaps++;
    rt_reload_requested_until = rt_now + RT_RELOAD_BUFFER_MS;
}

// Holstering plays the weapon's own put-away animation and then puts the
// weapon back in the inventory (with its ammunition; favorites point at the
// item type, so they still find it). Unarmed afterwards. Cut short before
// the weapon was put away, nothing changes.
static void rt_update_holster()
{
    if (rt_holster_active) {
        if (rt_holster_committed || !anim_busy(obj_dude)) {
            if (!rt_holster_committed) {
                rt_print("Holster interrupted.");
                rt_log("holster: interrupted before the weapon was put away");
            }
            if (!anim_busy(obj_dude)) {
                rt_holster_active = false;
                rt_holster_weapon = NULL;
            }
        }
        return;
    }

    if (rt_holster_requested_until == 0) {
        return;
    }

    Object* item = NULL;
    if (intface_get_current_item(&item) != 0 || item == NULL || item_get_type(item) != ITEM_TYPE_WEAPON || !rt_weapon_in_hand(item)) {
        // Nothing in hand: holding X does nothing.
        rt_holster_requested_until = 0;
        rt_log("holster: nothing in hand");
        return;
    }

    if (rt_obj_in_attack(obj_dude) || rt_reload_active || rt_chem_active || rt_aimed_is_executing()) {
        if (rt_now > rt_holster_requested_until) {
            rt_holster_requested_until = 0;
            rt_print("You can't holster right now.");
        }
        return;
    }

    rt_holster_requested_until = 0;
    rt_holster_active = true;
    rt_holster_committed = false;
    rt_holster_weapon = item;
    rt_stats.holstersStarted++;
    rt_stop_walking();
    rt_set_interact_highlight(NULL);

    int code = item_w_anim_code(item);
    int putAwayFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_PUT_AWAY, code, obj_dude->rotation + 1);
    if (code != 0 && art_exists(putAwayFid)) {
        if (anim_busy(obj_dude)) {
            register_clear(obj_dude);
        }
        register_begin(ANIMATION_REQUEST_RESERVED);
        register_object_play_sfx(obj_dude, gsnd_build_character_sfx_name(obj_dude, ANIM_PUT_AWAY, CHARACTER_SOUND_EFFECT_UNUSED), 0);
        register_object_animate(obj_dude, ANIM_PUT_AWAY, 0);
        register_object_call(item, NULL, (AnimationCallback*)rt_holster_commit, -1);
        if (register_end() == 0) {
            rt_stats.holsterAnimations++;
            rt_log("holster: putting away %s", object_name(item));
            return;
        }
    }

    // No put-away art (or it couldn't start): put away at once.
    rt_log("holster: no put-away animation for %s (weapon code %d)", object_name(item), code);
    rt_holster_commit(item, NULL);
}

static int rt_holster_commit(void* a1, void* a2)
{
    Object* weapon = (Object*)a1;
    if (!rt_holster_active || rt_holster_committed || weapon != rt_holster_weapon) {
        return 0;
    }

    rt_holster_committed = true;
    if (!rt_weapon_in_hand(weapon)) {
        rt_log("holster: the weapon left the hand meanwhile");
        return 0;
    }

    inven_rt_unequip(weapon);
    rt_stats.holstersCompleted++;
    rt_print("You put away the %s.", object_name(weapon));
    rt_log("holster: %s put away (%d/%d rounds)", object_name(weapon), item_w_curr_ammo(weapon), item_w_max_ammo(weapon));
    return 0;
}

static void rt_update_draw()
{
    if (rt_draw_pending == NULL) {
        return;
    }
    if (rt_now > rt_draw_until || !rt_weapon_in_hand(rt_draw_pending)) {
        rt_draw_pending = NULL;
        return;
    }
    if (!anim_busy(obj_dude) && !rt_obj_in_attack(obj_dude)) {
        rt_play_draw(rt_draw_pending);
    }
}

void rt_play_draw(Object* weapon)
{
    if (weapon == NULL || item_get_type(weapon) != ITEM_TYPE_WEAPON) {
        return;
    }
    if (anim_busy(obj_dude) || rt_obj_in_attack(obj_dude)) {
        // Played once the current animation is over (see `rt_update_draw`).
        rt_draw_pending = weapon;
        rt_draw_until = rt_now + RT_RELOAD_BUFFER_MS;
        return;
    }
    rt_draw_pending = NULL;

    int code = item_w_anim_code(weapon);
    int takeOutFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_TAKE_OUT, code, obj_dude->rotation + 1);
    if (code == 0 || !art_exists(takeOutFid)) {
        rt_log("draw: no take-out art for %s (weapon code %d)", object_name(weapon), code);
        return;
    }

    register_begin(ANIMATION_REQUEST_RESERVED);
    register_object_take_out(obj_dude, code, -1);
    if (register_end() == 0) {
        rt_stats.drawsPlayed++;
        rt_log("draw: taking out %s", object_name(weapon));
    }
}

// R3: toggles between the default view and the wider one, keeping the
// player where it is on screen (as far as the map's scroll limits allow).
static void rt_toggle_zoom()
{
    float target = rt_view_get_target_zoom() > 1.0f ? 1.0f : rt_view_get_max_zoom();
    rt_view_set_zoom(target, rt_cam_zoom_ms, obj_dude->tile);
    rt_stats.zoomToggles++;
    rt_log("camera: %s view (zoom %.2f)", target > 1.0f ? "wider" : "default", target);
}

// Right stick camera panning while nothing is locked (with a lock the right
// stick switches targets instead; a push belongs to whichever it started
// with). The pan is analog and continuous: it starts as soon as the stick
// leaves its deadzone, its speed follows the deflection, a short smoothing
// keeps it fluid, and it stops as soon as the stick is back in the center.
// The view may leave the player behind; it stays where it was put until the
// player moves again. Returns true while the camera pans.
static bool rt_update_camera_pan(float dtMs)
{
    float sx;
    float sy;
    gamepad_get_look_stick(&sx, &sy);
    bool pushed = rt_rstick_owner == RT_RSTICK_PAN && (sx != 0.0f || sy != 0.0f);

    float targetX = pushed ? sx * rt_cam_pan_speed : 0.0f;
    float targetY = pushed ? sy * rt_cam_pan_speed : 0.0f;
    // Releasing settles faster than pushing, so the view stops promptly.
    float smoothing = pushed ? (float)rt_cam_pan_smoothing_ms : rt_cam_pan_smoothing_ms * 0.4f;
    float blend = smoothing > 0.0f ? 1.0f - expf(-dtMs / smoothing) : 1.0f;
    rt_pan_vx += (targetX - rt_pan_vx) * blend;
    rt_pan_vy += (targetY - rt_pan_vy) * blend;

    if (!pushed && fabsf(rt_pan_vx) < 60.0f && fabsf(rt_pan_vy) < 60.0f) {
        rt_pan_vx = 0.0f;
        rt_pan_vy = 0.0f;
    }

    if (rt_pan_vx == 0.0f && rt_pan_vy == 0.0f) {
        return false;
    }

    if (!rt_camera_free) {
        rt_camera_free = true;
        rt_log("camera: panning with the right stick");
    }

    // Play area pixels per second, converted to world pixels at this zoom.
    float zoom = rt_view_get_zoom();
    rt_view_move(rt_pan_vx * dtMs / 1000.0f * zoom, rt_pan_vy * dtMs / 1000.0f * zoom);
    rt_stats.cameraPans++;
    return true;
}

// Edge scrolling. The camera stays still while the player is inside an
// inner box of the gameplay area (the map above the interface bar). Near an
// edge it glides toward that edge, by the amount the player is past the box,
// until the player is back inside; the map's scroll limits stop it. The
// right stick can pan the view away (see `rt_update_camera_pan`); once the
// player moves again, the same scrolling brings the view back.
static void rt_update_camera()
{
    float dtMs = rt_camera_dt;

    int width;
    int height;
    if (!rt_get_play_area(&width, &height) || obj_dude->elevation != map_elevation) {
        rt_view_update(dtMs);
        return;
    }

    if (rt_update_camera_pan(dtMs)) {
        rt_camera_scrolling = false;
        rt_view_update(dtMs);
        return;
    }

    if (!rt_camera_edge_scroll) {
        rt_view_update(dtMs);
        return;
    }

    if (rt_camera_free) {
        float mx;
        float my;
        gamepad_get_move_stick(&mx, &my);
        if (mx == 0.0f && my == 0.0f) {
            rt_view_update(dtMs);
            return;
        }
        rt_camera_free = false;
        rt_log("camera: player moves, following again");
    }

    // The player as drawn (a walk in progress included).
    int wx;
    int wy;
    bool known = tile_coord(obj_dude->tile, &wx, &wy, obj_dude->elevation) == 0;
    int x;
    int y;
    rt_view_world_to_display(wx + 16 + obj_dude->x, wy + 8 + obj_dude->y, &x, &y);

    // A new map or elevation (or a player far away after a scripted move)
    // gets the camera centered on the player; otherwise it scrolls.
    bool far = !known || x < -width || x > 2 * width || y < -height || y > 2 * height;
    if (rt_last_camera_elevation != map_elevation || far) {
        bool onScreen = known && x >= 0 && x < width && y >= 0 && y < height;
        rt_last_camera_elevation = map_elevation;
        if (!onScreen && tile_set_center(obj_dude->tile, TILE_SET_CENTER_REFRESH_WINDOW) != -1) {
            rt_view_reset_pan();
            rt_stats.cameraRecenters++;
            rt_log("camera: player off screen, centered");
        }
        rt_camera_scrolling = false;
        rt_view_update(dtMs);
        return;
    }

    int marginX = width * rt_camera_margin_pct / 100;
    int marginY = height * rt_camera_margin_pct / 100;

    // Once scrolling, continue until the player is a little further inside,
    // so the camera doesn't stop and start right at the boundary.
    int insetX = rt_camera_scrolling ? 16 : 0;
    int insetY = rt_camera_scrolling ? 12 : 0;

    float overX = 0.0f;
    if (x < marginX + insetX) {
        overX = (float)(x - (marginX + (rt_camera_scrolling ? insetX : 0)));
    } else if (x > width - marginX - insetX) {
        overX = (float)(x - (width - marginX - (rt_camera_scrolling ? insetX : 0)));
    }

    float overY = 0.0f;
    if (y < marginY + insetY) {
        overY = (float)(y - (marginY + (rt_camera_scrolling ? insetY : 0)));
    } else if (y > height - marginY - insetY) {
        overY = (float)(y - (height - marginY - (rt_camera_scrolling ? insetY : 0)));
    }

    if (fabsf(overX) < 0.5f && fabsf(overY) < 0.5f) {
        rt_camera_scrolling = false;
        rt_camera_limit_logged = false;
        rt_view_update(dtMs);
        return;
    }

    float blend = rt_cam_follow_ms > 0 ? 1.0f - expf(-dtMs / rt_cam_follow_ms) : 1.0f;
    float zoom = rt_view_get_zoom();

    float panX1;
    float panY1;
    rt_view_get_pan(&panX1, &panY1);
    int center = tile_center_tile;

    // A far-away player (after a pan) comes back fast but without jumps.
    float stepX = overX * blend;
    float stepY = overY * blend;
    float maxStep = RT_CAMERA_FOLLOW_MAX_SPEED * dtMs / 1000.0f;
    float length = sqrtf(stepX * stepX + stepY * stepY);
    if (length > maxStep && length > 0.0f) {
        stepX *= maxStep / length;
        stepY *= maxStep / length;
    }
    rt_view_move(stepX * zoom, stepY * zoom);

    float panX2;
    float panY2;
    rt_view_get_pan(&panX2, &panY2);
    if (center != tile_center_tile || panX1 != panX2 || panY1 != panY2) {
        rt_stats.cameraScrolls++;
        rt_camera_scrolling = true;
    } else if (dtMs > 0.0f) {
        // At the map's scroll limit: stay put.
        if (!rt_camera_limit_logged) {
            rt_camera_limit_logged = true;
            rt_stats.cameraLimitStops++;
            rt_log("camera: stopped at the map scroll limit");
        }
        rt_camera_scrolling = false;
    }

    rt_view_update(dtMs);
}

} // namespace fallout
