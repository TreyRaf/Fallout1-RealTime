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
#include "game/skill.h"
#include "game/stat.h"
#include "game/tile.h"
#include "platform_compat.h"
#include "plib/gnw/gamepad.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"

namespace fallout {

#define RT_CONFIG_SECTION "realtime"

// Returned by `rt_attack` when the attacker or defender is already part of an
// attack sequence (Fallout's attack animations cannot overlap per object).
#define RT_ATTACK_BUSY 1

#define RT_PENDING_ATTACK_TIMEOUT 6000
#define RT_TARGET_MAX_DISTANCE 25
#define RT_AHEAD_CONE_COS 0.5f
#define RT_ATTACK_BUFFER_MS 1200
#define RT_PLAYER_RECOVERY_MS 700

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
} RtCritterState;

typedef struct RtPendingAttack {
    Attack attack;
    unsigned int startedAt;
} RtPendingAttack;

static void rt_init();
static void rt_open_log();
static void rt_rebuild_critter_list();
static bool rt_is_known_critter(Object* obj);
static bool rt_is_alive(Object* critter);
static bool rt_is_player_team(Object* critter);
static bool rt_obj_in_attack(Object* obj);
static int rt_attack(Object* attacker, Object* defender, int hitMode);
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
static Object* rt_find_hostile_ahead();
static void rt_set_lock(Object* target);
static void rt_update_lock_marker();
static bool rt_is_automatic(int hitMode);
static void rt_update_attack_input(bool pressAttack);
static int rt_player_attack(bool explicitPress);
static int rt_player_hit_mode(int* hitMode);
static Object* rt_find_interactable();
static void rt_interact(Object* obj);
static void rt_reload();
static void rt_set_interact_highlight(Object* obj);
static void rt_set_outline(Object** slot, Object* obj, int outlineType);
static void rt_update_camera();
static void rt_print(const char* format, ...);
static char* rt_name(Object* obj);
static int rt_hits(Object* obj);

static bool rt_initialized = false;
static bool rt_is_enabled = true;
static bool rt_camera_follow = true;
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
static unsigned int rt_dude_next_attack_at = 0;
static Object* rt_lock = NULL;
static Object* rt_lock_marker = NULL;
static unsigned int rt_attack_buffered_until = 0;
static bool rt_fire_hold = false;
static unsigned int rt_player_recovery_until = 0;
static Object* rt_interact_outlined = NULL;
static int rt_last_camera_tile = -1;

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
    if (config_get_value(&game_config, RT_CONFIG_SECTION, "camera_follow", &value)) {
        rt_camera_follow = value != 0;
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
    rt_log("config: camera_follow=%d run_drain_ms_per_ap=%d ap_regen_ms_per_ap=%d attack_cooldown_ms_per_ap=%d",
        rt_camera_follow, rt_run_drain_ms, rt_ap_regen_ms, rt_ms_per_ap);
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
    // The marker object is destroyed with the old map.
    rt_lock = NULL;
    rt_lock_marker = NULL;
    rt_attack_buffered_until = 0;
    rt_fire_hold = false;
    rt_interact_outlined = NULL;
    rt_last_camera_tile = -1;
    rt_dude_next_attack_at = 0;
    rt_player_recovery_until = 0;

    gamepad_reset_gameplay_input();

    rt_log("reset (map load)");
}

const RtStats* rt_get_stats()
{
    return &rt_stats;
}

Object* rt_get_lock()
{
    return rt_lock;
}

Object* rt_get_lock_marker()
{
    return rt_lock_marker;
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

    // Controller gameplay: no 3D map cursor, no edge scrolling.
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
    rt_update_engagements();
    rt_update_knockdowns();
    rt_update_dude((float)realDelta);
    rt_update_ai();
    rt_update_camera();
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

    pending->startedAt = rt_now;
    rt_pending.push_back(pending);

    rt_stats.attacksStarted++;
    if (attacker == obj_dude) {
        rt_stats.playerAttacks++;
        critter_set_who_hit_me(attacker, defender);

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
            delete pending;
            rt_pending.erase(rt_pending.begin() + index);
            continue;
        }

        bool busy = anim_busy(attack->attacker) || anim_busy(attack->defender);
        for (int extra = 0; extra < attack->extrasLength && !busy; extra++) {
            busy = rt_is_known_critter(attack->extras[extra]) && anim_busy(attack->extras[extra]);
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

        if (target == obj_dude && rt_now < rt_player_recovery_until) {
            state->nextActionAt = rt_player_recovery_until + roll_random(0, 200);
            return;
        }

        // The player gets the next window when an attack of theirs is waiting.
        if (target == obj_dude && rt_attack_buffered_until != 0 && rt_now <= rt_attack_buffered_until) {
            state->nextActionAt = rt_now + 150;
            return;
        }

        int rc = rt_attack(critter, target, hitMode);
        if (rc == 0) {
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
    state->nextActionAt = rt_now + 200;
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

    // The lock follows its target and drops it when it becomes invalid.
    if (rt_lock != NULL && !rt_lock_is_valid(rt_lock)) {
        rt_log("lock: cleared (%s)", rt_is_known_critter(rt_lock) && !rt_is_alive(rt_lock) ? "target died" : "target invalid");
        rt_set_lock(NULL);
    }
    rt_update_lock_marker();

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
        GNW_add_input_buffer(KEY_LOWERCASE_I);
    }
    if (pressRB) {
        GNW_add_input_buffer(KEY_LOWERCASE_B);
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
                rt_print("No hostile target nearby.");
            }
        }
    }

    if (pressX) {
        rt_reload();
    }

    rt_update_attack_input(pressAttack);

    // Interaction.
    Object* interactable = rt_find_interactable();
    rt_set_interact_highlight(interactable);

    if (pressA) {
        if (interactable != NULL) {
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

    if (!canAct || rt_obj_in_attack(obj_dude)) {
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
    bool needIssue = !rt_move_active
        || dir != rt_move_dir
        || wantRun != rt_move_run
        || !busy;

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

static Object* rt_find_lock_candidate()
{
    // Nearest living hostile; ones in plain view win over blocked ones.
    Object* best = NULL;
    int bestScore = 0;
    for (Object* critter : rt_critters) {
        if (!rt_is_hostile_to_player(critter) || (critter->flags & OBJECT_HIDDEN) != 0) {
            continue;
        }

        int distance = obj_dist(obj_dude, critter);
        if (distance > RT_TARGET_MAX_DISTANCE) {
            continue;
        }

        int score = distance;
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

static Object* rt_find_hostile_ahead()
{
    float fx;
    float fy;
    if (!rt_facing_vector(&fx, &fy)) {
        return NULL;
    }

    Object* best = NULL;
    int bestDistance = 0;
    for (Object* critter : rt_critters) {
        if (!rt_is_hostile_to_player(critter) || (critter->flags & OBJECT_HIDDEN) != 0) {
            continue;
        }

        int distance = obj_dist(obj_dude, critter);
        if (distance > RT_TARGET_MAX_DISTANCE) {
            continue;
        }

        // Directly ahead: within about 60 degrees of the facing direction.
        // Adjacent enemies count as ahead when the player isn't steering.
        float facing = rt_facing_cos(critter, fx, fy);
        if (facing < RT_AHEAD_CONE_COS && distance > 1) {
            continue;
        }

        if (best == NULL || distance < bestDistance) {
            best = critter;
            bestDistance = distance;
        }
    }

    return best;
}

static void rt_set_lock(Object* target)
{
    if (target == rt_lock) {
        return;
    }

    rt_lock = target;
    rt_fire_hold = false;
    rt_update_lock_marker();

    if (target == NULL) {
        rt_log("lock: none");
        return;
    }

    rt_stats.locks++;

    int hitMode;
    if (rt_player_hit_mode(&hitMode) == 0) {
        // Vanilla hit chance, the same value the attack roll uses.
        int toHit = determine_to_hit(obj_dude, target, HIT_LOCATION_UNCALLED, hitMode);
        rt_print("Locked: %s (%d%% to hit)", critter_name(target), toHit);
    } else {
        rt_print("Locked: %s", critter_name(target));
    }
    rt_log("lock: %s at distance %d", critter_name(target), obj_dist(obj_dude, target));

    if (!anim_busy(obj_dude) && !rt_obj_in_attack(obj_dude)) {
        register_begin(ANIMATION_REQUEST_RESERVED);
        register_object_turn_towards(obj_dude, target->tile);
        register_end();
    }
}

// The red hex marker: the same flat hex art and red outline as the vanilla
// 3D mouse cursor, placed under the locked target.
static void rt_update_lock_marker()
{
    Rect rect;

    if (rt_lock == NULL) {
        if (rt_lock_marker != NULL && (rt_lock_marker->flags & OBJECT_HIDDEN) == 0) {
            if (obj_turn_off(rt_lock_marker, &rect) == 0) {
                tile_refresh_rect(&rect, rt_lock_marker->elevation);
            }
        }
        return;
    }

    if (rt_lock_marker == NULL) {
        int fid = art_id(OBJ_TYPE_INTERFACE, 1, 0, 0, 0);
        if (obj_new(&rt_lock_marker, fid, -1) != 0) {
            rt_lock_marker = NULL;
            return;
        }

        rt_lock_marker->flags |= OBJECT_LIGHT_THRU | OBJECT_NO_SAVE | OBJECT_SHOOT_THRU | OBJECT_NO_BLOCK;
        obj_outline_object(rt_lock_marker, OUTLINE_PALETTED | OUTLINE_TYPE_2, NULL);
        obj_toggle_flat(rt_lock_marker, NULL);
        obj_turn_off(rt_lock_marker, NULL);
    }

    if (rt_lock_marker->tile != rt_lock->tile || rt_lock_marker->elevation != rt_lock->elevation) {
        Rect oldRect;
        obj_bound(rt_lock_marker, &oldRect);
        if (obj_move_to_tile(rt_lock_marker, rt_lock->tile, rt_lock->elevation, &rect) == 0) {
            rect_min_bound(&rect, &oldRect, &rect);
            tile_refresh_rect(&rect, rt_lock->elevation);
        }
    }

    if ((rt_lock_marker->flags & OBJECT_HIDDEN) != 0) {
        if (obj_turn_on(rt_lock_marker, &rect) == 0) {
            tile_refresh_rect(&rect, rt_lock_marker->elevation);
        }
    }
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

static void rt_update_attack_input(bool pressAttack)
{
    bool held = gamepad_is_held(GAMEPAD_BUTTON_RIGHT_TRIGGER);

    if (pressAttack) {
        // One buffered press at most; it expires if it can't run soon.
        rt_log("input: RT pressed (lock=%s)", rt_lock != NULL ? critter_name(rt_lock) : "none");
        rt_attack_buffered_until = rt_now + RT_ATTACK_BUFFER_MS;
        rt_fire_hold = true;
    }

    if (!held) {
        rt_fire_hold = false;
    }

    int hitMode;
    bool automatic = rt_player_hit_mode(&hitMode) == 0 && rt_is_automatic(hitMode);

    bool buffered = rt_attack_buffered_until != 0 && rt_now <= rt_attack_buffered_until;
    bool repeat = rt_fire_hold && automatic && !pressAttack;
    if (!buffered && !repeat) {
        rt_attack_buffered_until = 0;
        return;
    }

    int rc = rt_player_attack(pressAttack);
    if (rc == RT_PLAYER_ATTACK_STARTED) {
        rt_attack_buffered_until = 0;
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

    Object* target = rt_lock;
    if (target == NULL) {
        target = rt_find_hostile_ahead();
        if (target == NULL) {
            rt_print("No hostile target ahead.");
            rt_stats.playerRejected++;
            return RT_PLAYER_ATTACK_FAILED;
        }
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

        switch (FID_TYPE(obj->fid)) {
        case OBJ_TYPE_ITEM:
            break;
        case OBJ_TYPE_CRITTER:
            if (rt_is_alive(obj)) {
                if (!obj_action_can_talk_to(obj) || rt_is_engaged(obj)) {
                    continue;
                }
            }
            break;
        case OBJ_TYPE_SCENERY:
            if (!proto_action_can_use(obj->pid)) {
                continue;
            }
            break;
        default:
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

    switch (FID_TYPE(obj->fid)) {
    case OBJ_TYPE_ITEM:
        rt_log("interact: pick up/open %s", object_name(obj));
        action_get_an_object(obj_dude, obj);
        break;
    case OBJ_TYPE_CRITTER:
        if (rt_is_alive(obj)) {
            rt_log("interact: talk to %s", critter_name(obj));
            action_talk_to(obj_dude, obj);
        } else {
            rt_log("interact: loot %s", critter_name(obj));
            action_loot_container(obj_dude, obj);
        }
        break;
    case OBJ_TYPE_SCENERY:
        rt_log("interact: use %s", object_name(obj));
        action_use_an_object(obj_dude, obj);
        break;
    }
}

static void rt_set_interact_highlight(Object* obj)
{
    int outlineType = OUTLINE_TYPE_ITEM;
    if (obj != NULL && FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER) {
        outlineType = OUTLINE_TYPE_FRIENDLY;
    }

    rt_set_outline(&rt_interact_outlined, obj, outlineType);
}

static void rt_reload()
{
    Object* item = NULL;
    if (intface_get_current_item(&item) != 0 || item == NULL || item_get_type(item) != ITEM_TYPE_WEAPON || item_w_max_ammo(item) <= 0) {
        rt_print("The active item doesn't use ammo.");
        return;
    }

    if (item_w_curr_ammo(item) >= item_w_max_ammo(item)) {
        rt_print("Your weapon is fully loaded.");
        return;
    }

    bool reloaded = false;
    while (item_w_try_reload(obj_dude, item) != -1) {
        reloaded = true;
    }

    if (!reloaded) {
        rt_print("You have no ammo for this weapon.");
        return;
    }

    gsound_play_sfx_file(gsnd_build_weapon_sfx_name(WEAPON_SOUND_EFFECT_READY, item, HIT_MODE_RIGHT_WEAPON_PRIMARY, NULL));
    intface_update_items(false);
    intface_update_ammo_lights();

    // Reloading takes time: no attacks for a moment.
    if (rt_dude_next_attack_at < rt_now + 1000) {
        rt_dude_next_attack_at = rt_now + 1000;
    }

    rt_log("reload: %d/%d", item_w_curr_ammo(item), item_w_max_ammo(item));
}

static void rt_update_camera()
{
    if (!rt_camera_follow) {
        return;
    }

    if (obj_dude->elevation != map_elevation || obj_dude->tile == rt_last_camera_tile) {
        return;
    }

    rt_last_camera_tile = obj_dude->tile;
    tile_set_center(obj_dude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
}

} // namespace fallout
