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
#include "game/rt_context.h"
#include "game/rt_pipboy.h"
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
#define RT_CAMERA_PAN_STEP_MS 40
// An RT press with a chem waits this long for an attack involving the player.
#define RT_CHEM_BUFFER_MS 1500
// A walk that hasn't moved a critter for this long is stalled.
#define RT_MOVE_STALL_MS 1500
// A reload press waits this long for an attack involving the player to end.
#define RT_RELOAD_BUFFER_MS 1500

// Results of `rt_reload`.
#define RT_RELOAD_STARTED 0
#define RT_RELOAD_WAIT 1
#define RT_RELOAD_FAILED 2

// Results of `rt_player_attack`.
#define RT_PLAYER_ATTACK_STARTED 0
#define RT_PLAYER_ATTACK_WAIT 1
#define RT_PLAYER_ATTACK_FAILED 2
#define RT_INTERACT_MAX_DISTANCE 3
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
} RtPendingAttack;

static void rt_init();
static void rt_open_log();
static void rt_rebuild_critter_list();
static bool rt_is_known_critter(Object* obj);
static bool rt_is_alive(Object* critter);
static bool rt_is_player_team(Object* critter);
static bool rt_obj_in_attack(Object* obj);
static int rt_attack(Object* attacker, Object* defender, int hitMode);
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
static void rt_update_reload();
static bool rt_get_play_area(int* width, int* height);
static bool rt_is_automatic(int hitMode);
static void rt_update_chem_input(bool pressAttack, Object* chem);
static void rt_update_attack_input(bool pressAttack);
static int rt_player_attack(bool explicitPress);
static int rt_player_hit_mode(int* hitMode);
static Object* rt_find_interactable();
static void rt_interact(Object* obj);
static int rt_reload();
static void rt_set_interact_highlight(Object* obj);
static void rt_set_outline(Object** slot, Object* obj, int outlineType);
static void rt_update_camera();
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
static Object* rt_lock = NULL;
// Lock-on indicator: crosshair art plus hit chance, drawn over the map.
static RtIndicator rt_indicator;
static Rect rt_indicator_rect;
// Camera position when the indicator was drawn: map scrolling shifts the
// drawn screen (indicator included), so the old image moves with it.
static int rt_indicator_ref_tile = -1;
static int rt_indicator_ref_x = 0;
static int rt_indicator_ref_y = 0;
static bool rt_indicator_drawn = false;
static unsigned char* rt_indicator_buf = NULL;
static int rt_indicator_width = 0;
static int rt_indicator_height = 0;
static int rt_indicator_cross_width = 0;
static int rt_indicator_cross_height = 0;
static char rt_indicator_built_text[32] = "";
static int rt_indicator_built_color = -1;
static unsigned int rt_attack_buffered_until = 0;
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
static unsigned int rt_camera_next_pan_at = 0;

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

// Chem use with RT (see `rt_update_chem_input`).
static bool rt_chem_active = false;
static unsigned int rt_chem_requested_until = 0;

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

    rt_open_log();

    rt_log("Fallout1-RealTime: real-time layer %s", rt_is_enabled ? "ENABLED" : "disabled");
    rt_log("config: camera_edge_scroll=%d camera_margin_pct=%d run_drain_ms_per_ap=%d ap_regen_ms_per_ap=%d attack_cooldown_ms_per_ap=%d",
        rt_camera_edge_scroll, rt_camera_margin_pct, rt_run_drain_ms, rt_ap_regen_ms, rt_ms_per_ap);
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

    // Objects referenced by pending attacks may be gone after a map change.
    for (RtPendingAttack* pending : rt_pending) {
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
    rt_indicator_drawn = false;
    rt_attack_buffered_until = 0;
    rt_fire_hold = false;
    rt_fire_press_done = false;
    rt_interact_outlined = NULL;
    rt_interact_target = NULL;
    rt_last_camera_elevation = -1;
    rt_camera_free = false;
    rt_camera_scrolling = false;
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
    if (rt_lock == obj) {
        rt_lock = NULL;
        rt_fire_hold = false;
        rt_attack_buffered_until = 0;
        rt_log("lock: cleared (target removed)");
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

bool rt_is_hostile(Object* critter)
{
    return rt_is_hostile_to_player(critter);
}

void rt_debug_set_lock(Object* target)
{
    rt_set_lock(target);
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

    // Modal menus and dialogue block the main loop, so a long gap means the
    // game was paused. Clamping keeps cooldowns and AP from jumping ahead.
    if (realDelta > 100) {
        realDelta = 100;
    }
    rt_now += realDelta;

    if (obj_dude == NULL || critter_is_dead(obj_dude)) {
        return;
    }

    if (rt_dude_ap < 0.0f) {
        rt_dude_ap = (float)stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS);
    }

    rt_rebuild_critter_list();
    rt_resolve_attacks();
    rt_update_player_slot();
    rt_update_engagements();
    rt_update_knockdowns();
    rt_update_dude((float)realDelta);
    rt_update_ai();
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

static bool rt_obj_in_attack(Object* obj)
{
    for (RtPendingAttack* pending : rt_pending) {
        Attack* attack = &(pending->attack);
        if (attack->attacker == obj || attack->defender == obj) {
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

static int rt_attack(Object* attacker, Object* defender, int hitMode)
{
    if (rt_obj_in_attack(attacker) || rt_obj_in_attack(defender)) {
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
    combat_ctd_init(attack, attacker, defender, hitMode, HIT_LOCATION_UNCALLED);

    // Vanilla to-hit roll, critical and damage calculations.
    if (combat_rt_compute_attack(attack) == -1) {
        rt_log("attack: compute_attack failed for %s -> %s", critter_name(attacker), critter_name(defender));
        delete pending;
        return -1;
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
    // themselves.
    rt_stop_for_attack(attacker);
    rt_stop_for_attack(defender);
    for (int index = 0; index < attack->extrasLength; index++) {
        rt_stop_for_attack(attack->extras[index]);
    }

    if (action_attack(attack) == -1) {
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

    if (defender == obj_dude) {
        for (RtPendingAttack* other : rt_pending) {
            if (other->attack.defender == obj_dude && other->attack.attacker != attacker) {
                rt_stats.playerSlotOverlaps++;
            }
        }
    }

    rt_note_critical_deaths(pending);
    pending->startedAt = rt_now;
    rt_pending.push_back(pending);

    rt_stats.attacksStarted++;
    if (attacker == obj_dude) {
        rt_stats.playerAttacks++;
        critter_set_who_hit_me(attacker, defender);

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

    rt_log("attack: %s -> %s (hitMode=%d, anim=%d, rounds=%d, dist=%d) %s dmg=%d",
        critter_name(attacker),
        rt_name(attack->defender),
        attack->hitMode,
        item_w_anim(attacker, attack->hitMode),
        attack->ammoQuantity,
        obj_dist(attacker, defender),
        (attack->attackerFlags & DAM_HIT) != 0 ? "HIT" : "MISS",
        attack->defenderDamage);

    return 0;
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

        // A target that started walking meanwhile (e.g. moved by its script)
        // stops to take the hit.
        rt_stop_for_attack(attack->defender);
        for (int extra = 0; extra < attack->extrasLength; extra++) {
            if (rt_is_known_critter(attack->extras[extra])) {
                rt_stop_for_attack(attack->extras[extra]);
            }
        }

        bool busy = anim_busy(attack->attacker) || anim_busy(attack->defender);
        for (int extra = 0; extra < attack->extrasLength && !busy; extra++) {
            busy = rt_is_known_critter(attack->extras[extra]) && anim_busy(attack->extras[extra]);
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

        if (attack->attacker == obj_dude) {
            int hitMode = attack->hitMode;
            rt_dude_next_attack_at = rt_now + item_w_mp_cost(obj_dude, hitMode, false) * rt_ms_per_ap;
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

    if (item_w_range(attacker, hitMode) < obj_dist(attacker, defender)) {
        return COMBAT_BAD_SHOT_OUT_OF_RANGE;
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

static unsigned int rt_attack_cooldown(Object* attacker, int hitMode)
{
    // Measured from the start of the attack, so it includes the animation.
    return 900 + item_w_mp_cost(attacker, hitMode, false) * rt_ms_per_ap + roll_random(0, 300);
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

static void rt_update_ai()
{
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
    if (rt_obj_in_attack(critter)) {
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

    int shot = rt_check_shot(critter, target, hitMode);
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

            // The player gets the next window when an attack, a reload or a
            // chem use of theirs is waiting.
            if ((rt_attack_buffered_until != 0 && rt_now <= rt_attack_buffered_until)
                || (rt_reload_requested_until != 0 && rt_now <= rt_reload_requested_until)
                || (rt_chem_requested_until != 0 && rt_now <= rt_chem_requested_until)) {
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
    // Y is reserved; R3 is unused.
    gamepad_consume_press(GAMEPAD_BUTTON_Y);
    gamepad_consume_press(GAMEPAD_BUTTON_RIGHT_STICK);
    float flickX;
    float flickY;
    bool flick = gamepad_consume_flick(&flickX, &flickY);

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

    if (!canAct) {
        rt_fire_hold = false;
        rt_attack_buffered_until = 0;
        rt_set_interact_highlight(NULL);
        rt_move_active = false;
        return;
    }

    if (pressB) {
        GNW_add_input_buffer(RT_PIPBOY_KEY_OPEN);
    }
    if (pressRB) {
        if (rt_reload_active) {
            rt_log("input: RB ignored while reloading");
        } else {
            GNW_add_input_buffer(KEY_LOWERCASE_B);
        }
    }
    if (pressBack) {
        GNW_add_input_buffer(KEY_LOWERCASE_N);
    }
    if (pressUp) {
        GNW_add_input_buffer(KEY_LOWERCASE_P);
    }
    if (pressDown) {
        GNW_add_input_buffer(KEY_TAB);
    }
    if (pressLeft) {
        GNW_add_input_buffer(KEY_LOWERCASE_C);
    }
    if (pressRight) {
        GNW_add_input_buffer(KEY_LOWERCASE_S);
    }

    if (pressLB) {
        // Reserved for the dedicated targeting system (not designed yet).
        rt_print("Targeting system (LB) is not implemented yet.");
        rt_log("input: LB targeting system requested (pending feature)");
        rt_stats.targetingRequests++;
    }

    if (pressRun) {
        if (rt_run_toggled) {
            rt_run_toggled = false;
            rt_log("run: off");
        } else if (rt_dude_ap >= 1.0f) {
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

    if (pressX) {
        rt_reload_requested_until = rt_now + RT_RELOAD_BUFFER_MS;
    }
    if (rt_reload_requested_until != 0) {
        if (rt_reload() != RT_RELOAD_WAIT) {
            rt_reload_requested_until = 0;
        } else if (rt_now > rt_reload_requested_until) {
            rt_reload_requested_until = 0;
            rt_print("You can't reload right now.");
        }
    }

    rt_update_attack_input(pressAttack);

    // Interaction.
    Object* interactable = rt_find_interactable();
    rt_interact_target = interactable;
    rt_set_interact_highlight(interactable);

    if (pressA) {
        if (rt_reload_active) {
            rt_log("interact: ignored while reloading");
        } else if (interactable != NULL) {
            rt_interact(interactable);
        } else {
            rt_log("interact: nothing nearby");
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

    if (running) {
        rt_dude_ap -= dtMs / rt_run_drain_ms;
        if (rt_dude_ap <= 0.0f) {
            rt_dude_ap = 0.0f;
            if (rt_run_toggled) {
                // A new click is needed to run again.
                rt_run_toggled = false;
                rt_print("You are out of breath.");
                rt_log("run: AP exhausted, back to walking");
            }
        }
    } else {
        rt_dude_ap += dtMs / rt_ap_regen_ms;
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

static void rt_update_movement(bool canAct)
{
    float stickX;
    float stickY;
    gamepad_get_move_stick(&stickX, &stickY);
    bool stick = stickX != 0.0f || stickY != 0.0f;

    // No walking during a reload: the holster and draw animations play in place.
    if (!canAct || rt_obj_in_attack(obj_dude) || rt_reload_active || rt_chem_active) {
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

    // Starting a run needs a full AP; once running, it lasts until AP is out.
    bool wantRun = rt_run_toggled && (rt_move_active && rt_move_run ? rt_dude_ap > 0.0f : rt_dude_ap >= 1.0f);
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
        return;
    }

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

    if (wantRun && !perk_level(PERK_SILENT_RUNNING)) {
        pc_flag_off(PC_FLAG_SNEAKING);
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
    if (tile_coord(obj->tile, x, y, obj->elevation) != 0) {
        return false;
    }

    *x += 16;
    *y += 8;

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

// Moves the lock to the nearest candidate in the flicked screen direction,
// or keeps it when there is none.
static void rt_switch_lock(float fx, float fy)
{
    int cx;
    int cy;
    rt_screen_pos(rt_lock, &cx, &cy);

    Object* best = NULL;
    float bestScore = 0.0f;
    for (Object* critter : rt_critters) {
        if (critter == rt_lock || !rt_is_lock_candidate(critter)) {
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

    // Held fire and a buffered press never carry over to another target.
    rt_lock = target;
    rt_fire_hold = false;
    rt_attack_buffered_until = 0;

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
                next.toHit = determine_to_hit(obj_dude, rt_lock, HIT_LOCATION_UNCALLED, hitMode);
                snprintf(next.text, sizeof(next.text), "%d%%", next.toHit);
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

    Rect rect = { 0, 0, -1, -1 };
    if (next.visible) {
        if (!rt_indicator_build(next.text, color)) {
            next.visible = false;
        } else {
            rect.ulx = next.x - rt_indicator_cross_width / 2;
            rect.uly = next.y - rt_indicator_height / 2;
            rect.lrx = rect.ulx + rt_indicator_width - 1;
            rect.lry = rect.uly + rt_indicator_height - 1;
        }
    }

    bool scrolled = rt_indicator_drawn && rt_indicator_ref_tile != tile_center_tile;
    bool changed = scrolled
        || next.visible != rt_indicator_drawn
        || (next.visible
            && (rect.ulx != rt_indicator_rect.ulx
                || rect.uly != rt_indicator_rect.uly
                || strcmp(next.text, rt_indicator.text) != 0));
    rt_indicator = next;

    if (!changed) {
        return;
    }

    if (rt_indicator_drawn) {
        // Erase: the overlay must not be drawn while its old area redraws.
        rt_indicator_drawn = false;
        tile_refresh_rect(&rt_indicator_rect, map_elevation);

        // Where scrolling moved the old image to.
        int x;
        int y;
        if (scrolled && tile_coord(rt_indicator_ref_tile, &x, &y, map_elevation) == 0) {
            Rect shifted = rt_indicator_rect;
            rectOffset(&shifted, x - rt_indicator_ref_x, y - rt_indicator_ref_y);
            tile_refresh_rect(&shifted, map_elevation);
        }
    }

    rt_indicator_drawn = next.visible;
    if (next.visible) {
        rt_indicator_rect = rect;
        rt_indicator_ref_tile = tile_center_tile;
        tile_coord(rt_indicator_ref_tile, &rt_indicator_ref_x, &rt_indicator_ref_y, map_elevation);
        tile_refresh_rect(&rt_indicator_rect, map_elevation);
    }
}

void rt_render_overlay(unsigned char* buffer, int pitch, Rect* rect)
{
    if (!rt_indicator_drawn || rt_indicator_buf == NULL) {
        return;
    }

    Rect clipped;
    if (rect_inside_bound(&rt_indicator_rect, rect, &clipped) != 0) {
        return;
    }

    int srcX = clipped.ulx - rt_indicator_rect.ulx;
    int srcY = clipped.uly - rt_indicator_rect.uly;
    trans_buf_to_buf(rt_indicator_buf + rt_indicator_width * srcY + srcX,
        clipped.lrx - clipped.ulx + 1,
        clipped.lry - clipped.uly + 1,
        rt_indicator_width,
        buffer + pitch * clipped.uly + clipped.ulx,
        pitch);
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

// RT with a chem (drug) in the active item slot uses it on the player,
// through the same action as using it on yourself in the original game (the
// hands animation, then one item from the stack is taken). One use per press.
static void rt_update_chem_input(bool pressAttack, Object* chem)
{
    if (pressAttack) {
        rt_chem_requested_until = rt_now + RT_CHEM_BUFFER_MS;
    }

    if (rt_chem_requested_until == 0) {
        return;
    }

    // An attack involving the player, a reload or a chem still being used:
    // the press waits for it to end.
    if (rt_obj_in_attack(obj_dude) || rt_reload_active || rt_chem_active) {
        if (rt_now > rt_chem_requested_until) {
            rt_chem_requested_until = 0;
            rt_print("You can't use that right now.");
        }
        return;
    }

    rt_chem_requested_until = 0;
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

static void rt_update_attack_input(bool pressAttack)
{
    Object* activeItem = NULL;
    if (intface_get_current_item(&activeItem) == 0 && activeItem != NULL && item_get_type(activeItem) == ITEM_TYPE_DRUG) {
        rt_fire_hold = false;
        rt_attack_buffered_until = 0;
        rt_update_chem_input(pressAttack, activeItem);
        return;
    }
    rt_chem_requested_until = 0;

    bool held = gamepad_is_held(GAMEPAD_BUTTON_RIGHT_TRIGGER);

    if (pressAttack) {
        // One buffered press at most; it expires if it can't run soon.
        rt_log("input: RT pressed (lock=%s)", rt_lock != NULL ? critter_name(rt_lock) : "none");
        rt_attack_buffered_until = rt_now + RT_ATTACK_BUFFER_MS;
        rt_fire_hold = true;
        rt_fire_pressed_at = rt_now;
        rt_fire_press_done = false;
    }

    if (!held) {
        rt_fire_hold = false;
    }

    int hitMode;
    bool automatic = rt_player_hit_mode(&hitMode) == 0 && rt_is_automatic(hitMode);

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
        rt_attack_buffered_until = 0;
        return;
    }

    int rc = rt_player_attack(pressAttack);
    if (rc == RT_PLAYER_ATTACK_STARTED) {
        rt_attack_buffered_until = 0;
        rt_fire_press_done = true;
    } else if (rc == RT_PLAYER_ATTACK_FAILED) {
        // Nothing to retry: no target, out of range, empty...
        rt_attack_buffered_until = 0;
        rt_fire_hold = false;
    }
}

static int rt_player_attack(bool explicitPress)
{
    int hitMode;
    if (rt_player_hit_mode(&hitMode) != 0) {
        rt_print("The active item can't attack. Press RB to switch hands or X to reload.");
        return RT_PLAYER_ATTACK_FAILED;
    }

    // No lock, no attack: RT never picks a target by itself.
    Object* target = rt_lock;
    if (target == NULL) {
        rt_print("No target locked. Press LT to lock on.");
        rt_stats.playerRejected++;
        rt_stats.lastRejectReason = -1;
        return RT_PLAYER_ATTACK_FAILED;
    }

    // No firing until a reload is complete.
    if (rt_reload_active) {
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

    if (rt_move_active) {
        rt_move_active = false;
    }

    int rc = rt_attack(obj_dude, target, hitMode);
    if (rc == 0) {
        rt_set_interact_highlight(NULL);
        return RT_PLAYER_ATTACK_STARTED;
    }

    if (rc == RT_ATTACK_BUSY) {
        return RT_PLAYER_ATTACK_WAIT;
    }

    rt_print("You can't attack that.");
    return RT_PLAYER_ATTACK_FAILED;
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

static Object* rt_find_interactable()
{
    float fx;
    float fy;
    if (!rt_facing_vector(&fx, &fy)) {
        return NULL;
    }

    Object* best = NULL;
    float bestScore = 1000.0f;

    for (Object* obj = obj_find_first_at(obj_dude->elevation); obj != NULL; obj = obj_find_next_at()) {
        if (obj == obj_dude || (obj->flags & OBJECT_HIDDEN) != 0) {
            continue;
        }

        int distance = tile_dist(obj_dude->tile, obj->tile);
        if (distance > RT_INTERACT_MAX_DISTANCE) {
            continue;
        }

        int type = FID_TYPE(obj->fid);
        if (type != OBJ_TYPE_ITEM && type != OBJ_TYPE_CRITTER && type != OBJ_TYPE_SCENERY) {
            continue;
        }

        // Anything with an ordinary or skill interaction (see `rt_context.h`).
        if (!rt_context_is_interactable(obj)) {
            continue;
        }

        float facing = rt_facing_cos(obj, fx, fy);
        float score = distance + (1.0f - facing) * 1.5f;
        if (score < bestScore) {
            bestScore = score;
            best = obj;
        }
    }

    return best;
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

    int code = item_w_anim_code(item);
    int putAwayFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_PUT_AWAY, code, obj_dude->rotation + 1);
    int takeOutFid = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_TAKE_OUT, code, obj_dude->rotation + 1);
    bool animated = code != 0 && art_exists(putAwayFid) && art_exists(takeOutFid);

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

    if (rt_reload_until != 0) {
        if (rt_now < rt_reload_until) {
            return;
        }

        rt_reload_until = 0;
        rt_reload_active = false;
        rt_reload_weapon = NULL;
        rt_stats.reloadsCompleted++;
        rt_log("reload: complete (timed)");
        return;
    }

    if (!rt_reload_finished && anim_busy(obj_dude)) {
        return;
    }

    rt_reload_active = false;
    rt_reload_weapon = NULL;

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

// One engine scroll step (32x24 px), which respects the map's scroll limits;
// a blocked diagonal step falls back to either axis. Returns `map_scroll`'s
// result.
static int rt_camera_step(int dx, int dy)
{
    int rc = map_scroll(dx, dy);
    if (rc == -1 && dx != 0 && dy != 0) {
        rc = map_scroll(dx, 0);
        if (rc == -1) {
            rc = map_scroll(0, dy);
        }
    }
    return rc;
}

// Right stick camera panning while nothing is locked (with a lock the right
// stick switches targets instead). The view may leave the player behind; it
// stays where it was put until the player moves again. Returns true while
// the stick pans.
static bool rt_update_camera_pan()
{
    float sx;
    float sy;
    gamepad_get_look_stick(&sx, &sy);
    float magnitude = sqrtf(sx * sx + sy * sy);
    if (rt_lock != NULL || magnitude <= 0.0f) {
        return false;
    }

    // Eight directions; a fuller push scrolls faster.
    int dx = sx > 0.38f * magnitude ? 1 : (sx < -0.38f * magnitude ? -1 : 0);
    int dy = sy > 0.38f * magnitude ? 1 : (sy < -0.38f * magnitude ? -1 : 0);
    if (dx == 0 && dy == 0) {
        return false;
    }

    if (!rt_camera_free) {
        rt_camera_free = true;
        rt_log("camera: panning with the right stick");
    }

    if (rt_now < rt_camera_next_pan_at) {
        return true;
    }

    int rc = rt_camera_step(dx, dy);
    if (rc == 0) {
        rt_stats.cameraPans++;
        rt_camera_next_pan_at = rt_now + (unsigned int)(RT_CAMERA_PAN_STEP_MS / magnitude);
    }
    return true;
}

// Edge scrolling. The camera stays still while the player is inside an
// inner box of the gameplay area (the map above the interface bar). Near an
// edge it scrolls toward that edge in the engine's own scroll steps, which
// respect the map's scroll limits, until the player is back inside the box.
// The right stick can pan the view away (see `rt_update_camera_pan`); once
// the player moves again, the same scrolling brings the view back.
static void rt_update_camera()
{
    if (!rt_camera_edge_scroll || obj_dude->elevation != map_elevation) {
        return;
    }

    int width;
    int height;
    if (!rt_get_play_area(&width, &height)) {
        return;
    }

    if (rt_update_camera_pan()) {
        rt_camera_scrolling = false;
        return;
    }

    if (rt_camera_free) {
        float mx;
        float my;
        gamepad_get_move_stick(&mx, &my);
        if (mx == 0.0f && my == 0.0f) {
            return;
        }
        rt_camera_free = false;
        rt_log("camera: player moves, following again");
    }

    int x;
    int y;
    bool known = tile_coord(obj_dude->tile, &x, &y, obj_dude->elevation) == 0;
    x += 16;
    y += 8;

    // A new map or elevation (or a player far away after a scripted move)
    // gets the camera centered on the player; otherwise it scrolls.
    bool far = !known || x < -width || x > 2 * width || y < -height || y > 2 * height;
    if (rt_last_camera_elevation != map_elevation || far) {
        bool onScreen = known && x >= 0 && x < width && y >= 0 && y < height;
        rt_last_camera_elevation = map_elevation;
        if (!onScreen && tile_set_center(obj_dude->tile, TILE_SET_CENTER_REFRESH_WINDOW) != -1) {
            rt_stats.cameraRecenters++;
            rt_log("camera: player off screen, centered");
        }
        rt_camera_scrolling = false;
        return;
    }

    int marginX = width * rt_camera_margin_pct / 100;
    int marginY = height * rt_camera_margin_pct / 100;

    // Once scrolling, continue until the player is a little further inside,
    // so the camera doesn't stop and start right at the boundary.
    int insetX = rt_camera_scrolling ? 16 : 0;
    int insetY = rt_camera_scrolling ? 12 : 0;

    int dx = 0;
    if (x < marginX + insetX) {
        dx = -1;
    } else if (x > width - marginX - insetX) {
        dx = 1;
    }

    int dy = 0;
    if (y < marginY + insetY) {
        dy = -1;
    } else if (y > height - marginY - insetY) {
        dy = 1;
    }

    if (dx == 0 && dy == 0) {
        rt_camera_scrolling = false;
        return;
    }

    int rc = rt_camera_step(dx, dy);

    if (rc == 0) {
        rt_stats.cameraScrolls++;
        rt_camera_scrolling = true;
    } else if (rc == -1) {
        // At the map's scroll limit: stay put.
        if (rt_camera_scrolling) {
            rt_stats.cameraLimitStops++;
            rt_log("camera: stopped at the map scroll limit");
        }
        rt_camera_scrolling = false;
    }
}

} // namespace fallout
