#include "game/rt_rules.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <unordered_map>

#include <SDL.h>

#include "game/combat_defs.h"
#include "game/config.h"
#include "game/gconfig.h"
#include "game/item.h"
#include "game/map_defs.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/proto_types.h"
#include "game/realtime.h"
#include "game/scripts.h"
#include "game/skill_defs.h"
#include "game/stat.h"
#include "game/tile.h"
#include "game/trait.h"
#include "plib/gnw/gamepad.h"

namespace fallout {

#define RT_RULES_SECTION "realtime"

// Party members that count against the Charisma limit, by script.
static const char* rt_companion_scripts[] = {
    "ian.int",
    "junkdog.int", // Dogmeat
    "tycho.int",
    "katja.int",
};

static RtRules rt_rules_values;
static bool rt_rules_loaded = false;

static void rt_rules_load()
{
    rt_rules_values.hthReachBonus = 1.5;
    rt_rules_values.moveDrainPct = 50;
    rt_rules_values.rateOfFireRangePct = 25;
    rt_rules_values.heaveHoRangePct = 20;
    rt_rules_values.kamikazeAp = 10;
    rt_rules_values.kamikazeArmorAcPct = 50;
    rt_rules_values.fastShotRatePct = 125;
    rt_rules_values.bloodyMessCrit = 3;
    rt_rules_values.skilledSkillBonus = 10;
    rt_rules_values.skilledXpPct = 85;
    rt_rules_values.quickPocketsBonus = 5;
    rt_rules_values.quickPocketsGraceMs = 2000;
    rt_rules_values.quickPocketsDecayMs = 8000;
    rt_rules_values.quickPocketsResetS = 60;
    rt_rules_values.sweepArcDeg = 120;
    rt_rules_values.sweepMaxTargets = 3;
    rt_rules_values.counterWindowMs = 3000;
    rt_rules_values.rangedCounterWindowMs = 6000;
    rt_rules_values.apPunch = 0.5;
    rt_rules_values.apUnarmedWeapon = 1.0;
    rt_rules_values.apLightMelee = 1.0;
    rt_rules_values.apHeavyMelee = 2.0;
    rt_rules_values.apThrow = 3.0;
    rt_rules_values.apAimedPistol = 3.0;
    rt_rules_values.apAimedRifle = 4.0;
    rt_rules_values.apAimedBigGun = 5.0;
    rt_rules_values.apAimedCloseExtra = 1.0;
    rt_rules_values.hitReactionSpeed = 2.0;
    rt_rules_values.timingWindowMs = 300;
    rt_rules_values.timingEarlyPenalty = 10;
    rt_rules_values.timingSpamPenalty = 5;
    rt_rules_values.timingMaxPenalty = 25;

    double reach;
    if (config_get_double(&game_config, RT_RULES_SECTION, "perk_bonus_hth_reach", &reach) && reach >= 0.0 && reach <= 10.0) {
        rt_rules_values.hthReachBonus = reach;
    }

    double speed;
    if (config_get_double(&game_config, RT_RULES_SECTION, "hit_reaction_speed", &speed) && speed >= 1.0 && speed <= 4.0) {
        rt_rules_values.hitReactionSpeed = speed;
    }

    struct {
        const char* key;
        int* value;
        int max;
    } timingKeys[] = {
        { "timing_window_ms", &(rt_rules_values.timingWindowMs), 2000 },
        { "timing_early_penalty", &(rt_rules_values.timingEarlyPenalty), 95 },
        { "timing_spam_penalty", &(rt_rules_values.timingSpamPenalty), 95 },
        { "timing_max_penalty", &(rt_rules_values.timingMaxPenalty), 95 },
    };
    for (auto& key : timingKeys) {
        int timing;
        if (config_get_value(&game_config, RT_RULES_SECTION, key.key, &timing) && timing >= 0 && timing <= key.max) {
            *(key.value) = timing;
        }
    }

    struct {
        const char* key;
        double* value;
    } apKeys[] = {
        { "ap_punch", &(rt_rules_values.apPunch) },
        { "ap_unarmed_weapon", &(rt_rules_values.apUnarmedWeapon) },
        { "ap_light_melee", &(rt_rules_values.apLightMelee) },
        { "ap_heavy_melee", &(rt_rules_values.apHeavyMelee) },
        { "ap_throw", &(rt_rules_values.apThrow) },
        { "ap_aimed_pistol", &(rt_rules_values.apAimedPistol) },
        { "ap_aimed_rifle", &(rt_rules_values.apAimedRifle) },
        { "ap_aimed_big_gun", &(rt_rules_values.apAimedBigGun) },
        { "ap_aimed_close_extra", &(rt_rules_values.apAimedCloseExtra) },
    };
    for (auto& entry : apKeys) {
        double ap;
        // Half points at most (the HUD shows one decimal: 0.5).
        if (config_get_double(&game_config, RT_RULES_SECTION, entry.key, &ap) && ap >= 0.0 && ap <= 20.0) {
            *(entry.value) = floor(ap * 2.0 + 0.5) / 2.0;
        }
    }

    int value;
    if (config_get_value(&game_config, RT_RULES_SECTION, "perk_bonus_move_drain_pct", &value) && value >= 0 && value <= 100) {
        rt_rules_values.moveDrainPct = value;
    }
    if (config_get_value(&game_config, RT_RULES_SECTION, "perk_rate_of_fire_range_pct", &value) && value >= 0 && value <= 100) {
        rt_rules_values.rateOfFireRangePct = value;
    }
    if (config_get_value(&game_config, RT_RULES_SECTION, "perk_heave_ho_range_pct", &value) && value >= 0 && value <= 100) {
        rt_rules_values.heaveHoRangePct = value;
    }

    struct {
        const char* key;
        int* value;
        int min;
        int max;
    } traitKeys[] = {
        { "trait_kamikaze_ap", &(rt_rules_values.kamikazeAp), 0, 20 },
        { "trait_kamikaze_armor_ac_pct", &(rt_rules_values.kamikazeArmorAcPct), 0, 100 },
        { "trait_fast_shot_rate_pct", &(rt_rules_values.fastShotRatePct), 100, 200 },
        { "trait_bloody_mess_crit", &(rt_rules_values.bloodyMessCrit), 0, 50 },
        { "trait_skilled_skill_bonus", &(rt_rules_values.skilledSkillBonus), 0, 50 },
        { "trait_skilled_xp_pct", &(rt_rules_values.skilledXpPct), 10, 100 },
        { "perk_quick_pockets_bonus", &(rt_rules_values.quickPocketsBonus), 0, 50 },
        { "perk_quick_pockets_grace_ms", &(rt_rules_values.quickPocketsGraceMs), 0, 60000 },
        { "perk_quick_pockets_decay_ms", &(rt_rules_values.quickPocketsDecayMs), 0, 120000 },
        { "perk_quick_pockets_reset_s", &(rt_rules_values.quickPocketsResetS), 0, 3600 },
        { "melee_sweep_arc_deg", &(rt_rules_values.sweepArcDeg), 0, 300 },
        { "melee_sweep_max_targets", &(rt_rules_values.sweepMaxTargets), 1, 8 },
        { "melee_counter_window_ms", &(rt_rules_values.counterWindowMs), 500, 10000 },
        { "ranged_counter_window_ms", &(rt_rules_values.rangedCounterWindowMs), 500, 20000 },
    };
    for (auto& entry : traitKeys) {
        if (config_get_value(&game_config, RT_RULES_SECTION, entry.key, &value) && value >= entry.min && value <= entry.max) {
            *(entry.value) = value;
        }
    }

    rt_rules_loaded = true;
}

const RtRules* rt_rules()
{
    if (!rt_rules_loaded) {
        rt_rules_load();
    }
    return &rt_rules_values;
}

void rt_rules_set(const RtRules* rules)
{
    if (rules == NULL) {
        rt_rules_load();
    } else {
        rt_rules_values = *rules;
        rt_rules_loaded = true;
    }
}

int rt_perk_selections_earned(int level)
{
    return level >= 2 ? level / 2 : 0;
}

int rt_perk_selections_original(int level, bool skilled)
{
    int progression = skilled ? 4 : 3;
    return level >= 2 ? level / progression : 0;
}

bool rt_is_companion(Object* critter)
{
    if (critter == NULL || critter == obj_dude || critter->sid == -1) {
        return false;
    }

    Script* script;
    if (scr_ptr(critter->sid, &script) == -1) {
        return false;
    }

    char name[16];
    if (scr_list_str(script->scr_script_idx, name, sizeof(name)) == -1) {
        return false;
    }

    for (const char* companion : rt_companion_scripts) {
        if (SDL_strcasecmp(name, companion) == 0) {
            return true;
        }
    }
    return false;
}

int rt_companion_limit()
{
    int charisma = stat_level(obj_dude, STAT_CHARISMA);
    return std::clamp(charisma / 2, 0, 5);
}

double rt_tile_distance_exact(int tile1, int tile2)
{
    // Hexes stand in columns: a column is offset half a hex from its
    // neighbors (even columns lower, see `tile_dir`), and neighboring columns
    // are sqrt(3)/2 apart.
    int column1 = tile1 % HEX_GRID_WIDTH;
    int column2 = tile2 % HEX_GRID_WIDTH;
    double y1 = tile1 / HEX_GRID_WIDTH + ((column1 & 1) == 0 ? 0.5 : 0.0);
    double y2 = tile2 / HEX_GRID_WIDTH + ((column2 & 1) == 0 ? 0.5 : 0.0);
    double dx = (column1 - column2) * 0.8660254037844386;
    double dy = y1 - y2;
    return sqrt(dx * dx + dy * dy);
}

double rt_obj_distance_exact(Object* object1, Object* object2)
{
    double distance = rt_tile_distance_exact(object1->tile, object2->tile);

    if ((object1->flags & OBJECT_MULTIHEX) != 0) {
        distance -= 1.0;
    }

    if ((object2->flags & OBJECT_MULTIHEX) != 0) {
        distance -= 1.0;
    }

    return std::max(distance, 0.0);
}

int rt_distance_penalty(Object* attacker, int attackType, int penalty)
{
    if (penalty <= 0 || attacker != obj_dude) {
        return std::max(penalty, 0);
    }

    int reduction = 0;
    if (attackType == ATTACK_TYPE_RANGED && perk_level(PERK_BONUS_RATE_OF_FIRE) != 0) {
        reduction = rt_rules()->rateOfFireRangePct;
    } else if (attackType == ATTACK_TYPE_THROW) {
        reduction = rt_rules()->heaveHoRangePct * perk_level(PERK_HEAVE_HO);
    }

    reduction = std::clamp(reduction, 0, 100);
    return penalty - penalty * reduction / 100;
}

double rt_reach_bonus(Object* attacker, int attackType)
{
    if (attacker != obj_dude || perk_level(PERK_BONUS_HTH_ATTACKS) == 0) {
        return 0.0;
    }

    if (attackType != ATTACK_TYPE_MELEE && attackType != ATTACK_TYPE_UNARMED) {
        return 0.0;
    }

    return rt_rules()->hthReachBonus;
}

int rt_sprint_drain_pct(bool inCombat)
{
    if (!inCombat || perk_level(PERK_BONUS_MOVE) == 0) {
        return 100;
    }

    return 100 - std::clamp(rt_rules()->moveDrainPct, 0, 100);
}

// Fallout 1's weapons by item pid, in the groups the AP costs use.
static const int rt_unarmed_pids[] = { 21, 234, 235 }; // Brass Knuckles, Spiked Knuckles, Power Fist
static const int rt_light_melee_pids[] = { 4, 5, 236 }; // Knife, Club, Combat Knife
static const int rt_heavy_melee_pids[] = { 6, 7, 20, 115, 116, 160 }; // Sledgehammer, Spear, Crowbar, Super Sledge, Ripper, Cattle Prod
// 10mm Pistol, Laser Pistol, Desert Eagle .44, 14mm Pistol, Plasma Pistol,
// Alien Blaster, 9mm Mauser, .223 Pistol.
static const int rt_pistol_pids[] = { 8, 16, 18, 22, 24, 120, 122, 241 };
// 10mm SMG, Hunting Rifle, Plasma Rifle, Assault Rifle, Shotgun, Laser
// Rifle, Sniper Rifle, both Red Ryders, Turbo Plasma Rifle, Combat Shotgun.
static const int rt_rifle_pids[] = { 9, 10, 15, 23, 94, 118, 143, 161, 162, 233, 242 };
static const int rt_big_gun_pids[] = { 11, 12, 13, 28 }; // Flamer, Minigun, Rocket Launcher, Gatling Laser

template <size_t N>
static bool rt_pid_listed(const int (&pids)[N], int pid)
{
    return std::find(pids, pids + N, pid) != pids + N;
}

bool rt_is_thrown_weapon(Object* item)
{
    return item != NULL
        && item_get_type(item) == ITEM_TYPE_WEAPON
        && item_w_subtype(item, HIT_MODE_RIGHT_WEAPON_PRIMARY) == ATTACK_TYPE_THROW;
}

RtWeaponClass rt_weapon_class(Object* weapon)
{
    if (weapon == NULL || item_get_type(weapon) != ITEM_TYPE_WEAPON) {
        return RT_WEAPON_CLASS_NONE;
    }

    int pid = weapon->pid;
    if (rt_pid_listed(rt_unarmed_pids, pid)) {
        return RT_WEAPON_CLASS_UNARMED;
    }
    if (rt_pid_listed(rt_light_melee_pids, pid)) {
        return RT_WEAPON_CLASS_LIGHT_MELEE;
    }
    if (rt_pid_listed(rt_heavy_melee_pids, pid)) {
        return RT_WEAPON_CLASS_HEAVY_MELEE;
    }
    if (rt_pid_listed(rt_pistol_pids, pid)) {
        return RT_WEAPON_CLASS_PISTOL;
    }
    if (rt_pid_listed(rt_rifle_pids, pid)) {
        return RT_WEAPON_CLASS_RIFLE;
    }
    if (rt_pid_listed(rt_big_gun_pids, pid)) {
        return RT_WEAPON_CLASS_BIG_GUN;
    }

    // Unlisted weapons (mods): by the primary attack.
    switch (item_w_subtype(weapon, HIT_MODE_RIGHT_WEAPON_PRIMARY)) {
    case ATTACK_TYPE_THROW:
        return RT_WEAPON_CLASS_THROWN;
    case ATTACK_TYPE_UNARMED:
        return RT_WEAPON_CLASS_UNARMED;
    case ATTACK_TYPE_MELEE:
        return item_w_is_2handed(weapon) || item_w_primary_mp_cost(weapon) > 3
            ? RT_WEAPON_CLASS_HEAVY_MELEE
            : RT_WEAPON_CLASS_LIGHT_MELEE;
    case ATTACK_TYPE_RANGED:
        if (item_w_skill(weapon, HIT_MODE_RIGHT_WEAPON_PRIMARY) == SKILL_BIG_GUNS) {
            return RT_WEAPON_CLASS_BIG_GUN;
        }
        return item_w_is_2handed(weapon) ? RT_WEAPON_CLASS_RIFLE : RT_WEAPON_CLASS_PISTOL;
    }
    return RT_WEAPON_CLASS_NONE;
}

double rt_attack_ap_cost(Object* critter, int hitMode, bool aimed)
{
    const RtRules* rules = rt_rules();

    if (hitMode == HIT_MODE_LEFT_WEAPON_RELOAD || hitMode == HIT_MODE_RIGHT_WEAPON_RELOAD) {
        return 0.0;
    }

    double extra = aimed ? rules->apAimedCloseExtra : 0.0;
    Object* weapon = item_hit_with(critter, hitMode);
    if (weapon == NULL || hitMode == HIT_MODE_PUNCH || hitMode == HIT_MODE_KICK) {
        return rules->apPunch + extra;
    }

    RtWeaponClass weaponClass = rt_weapon_class(weapon);
    switch (item_w_subtype(weapon, hitMode)) {
    case ATTACK_TYPE_THROW:
        // Never aimed (AIMED SHOT refuses throws).
        return rules->apThrow;
    case ATTACK_TYPE_RANGED:
        if (!aimed) {
            return 0.0;
        }
        switch (weaponClass) {
        case RT_WEAPON_CLASS_PISTOL:
            return rules->apAimedPistol;
        case RT_WEAPON_CLASS_BIG_GUN:
            return rules->apAimedBigGun;
        default:
            return rules->apAimedRifle;
        }
    case ATTACK_TYPE_UNARMED:
        // Also a thrown weapon's punch (the Rock's secondary attack).
        return rules->apUnarmedWeapon + extra;
    case ATTACK_TYPE_MELEE:
        // A thrown weapon's swing (the Throwing Knife's) counts as a knife.
        if (weaponClass == RT_WEAPON_CLASS_HEAVY_MELEE) {
            return rules->apHeavyMelee + extra;
        }
        return rules->apLightMelee + extra;
    }

    return 0.0;
}

const char* rt_ap_text(double ap, char* buffer, size_t size)
{
    // Rounded down to a half point: AP regenerating from 2.9 shows 2.5.
    double halves = floor(std::max(ap, 0.0) * 2.0 + 0.0001);
    if (fmod(halves, 2.0) == 0.0) {
        snprintf(buffer, size, "%d", (int)(halves / 2.0));
    } else {
        snprintf(buffer, size, "%.1f", halves / 2.0);
    }
    return buffer;
}

int rt_kamikaze_armor_ac_removed(int armorClass)
{
    if (armorClass <= 0) {
        return 0;
    }

    int pct = std::clamp(rt_rules()->kamikazeArmorAcPct, 0, 100);
    return armorClass - armorClass * pct / 100;
}

bool rt_fast_shot_active()
{
    return obj_dude != NULL && trait_level(TRAIT_FAST_SHOT) != 0 && rt_rules()->fastShotRatePct > 100;
}

unsigned int rt_fast_shot_interval(unsigned int ms)
{
    if (!rt_fast_shot_active()) {
        return ms;
    }

    return ms * 100 / (unsigned int)rt_rules()->fastShotRatePct;
}

// Hundredths of an experience point carried over between Skilled awards.
static int rt_xp_remainder = 0;

int rt_skilled_xp_award(int xp)
{
    if (xp <= 0 || trait_level(TRAIT_SKILLED) == 0) {
        return xp;
    }

    int pct = std::clamp(rt_rules()->skilledXpPct, 10, 100);
    long long total = (long long)xp * pct + rt_xp_remainder;
    rt_xp_remainder = (int)(total % 100);
    return (int)(total / 100);
}

int rt_skilled_xp_remainder()
{
    return rt_xp_remainder;
}

void rt_skilled_xp_set_remainder(int hundredths)
{
    rt_xp_remainder = std::clamp(hundredths, 0, 99);
}

// -----------------------------------------------------------------------------
// Quick Pockets
// -----------------------------------------------------------------------------

// A target whose steal screen was closed: the time used so far and when (world
// time, `rt_get_time`) it closed.
typedef struct RtQuickPocketsRecord {
    int elapsedMs;
    unsigned int closedAt;
} RtQuickPocketsRecord;

// By object id (ids survive saving and loading).
static std::unordered_map<int, RtQuickPocketsRecord> rt_qp_records;
static RtQuickPocketsState rt_qp_state;
static unsigned int rt_qp_last_tick = 0;
static bool rt_qp_pad_at_start = false;

int rt_quick_pockets_bonus_at(int elapsedMs)
{
    const RtRules* rules = rt_rules();
    int rank = obj_dude != NULL ? perk_level(PERK_QUICK_POCKETS) : 0;
    int maxBonus = std::max(rules->quickPocketsBonus, 0) * rank;
    if (maxBonus <= 0) {
        return 0;
    }

    int grace = std::max(rules->quickPocketsGraceMs, 0);
    int decay = std::max(rules->quickPocketsDecayMs, 0);
    if (elapsedMs <= grace) {
        return maxBonus;
    }
    if (decay == 0 || elapsedMs >= grace + decay) {
        return 0;
    }
    // Linear, rounded down: it reaches 0 exactly when the decay ends.
    return (int)((long long)maxBonus * (grace + decay - elapsedMs) / decay);
}

static void rt_qp_forget_expired(unsigned int now)
{
    unsigned int resetMs = (unsigned int)std::max(rt_rules()->quickPocketsResetS, 0) * 1000u;
    for (auto it = rt_qp_records.begin(); it != rt_qp_records.end();) {
        if (now - it->second.closedAt >= resetMs) {
            it = rt_qp_records.erase(it);
        } else {
            ++it;
        }
    }
}

void rt_quick_pockets_begin(Object* target)
{
    rt_quick_pockets_end();
    if (target == NULL) {
        return;
    }

    unsigned int now = rt_get_time();
    rt_qp_forget_expired(now);

    rt_qp_state.active = true;
    rt_qp_state.target = target;
    rt_qp_state.elapsedMs = 0;
    rt_qp_state.resumed = false;
    rt_qp_state.suspendedMs = 0;
    rt_qp_state.sessions++;
    auto it = rt_qp_records.find(target->id);
    if (it != rt_qp_records.end()) {
        rt_qp_state.elapsedMs = it->second.elapsedMs;
        rt_qp_state.resumed = true;
    }
    rt_qp_state.bonus = rt_quick_pockets_bonus_at(rt_qp_state.elapsedMs);
    rt_qp_last_tick = SDL_GetTicks();
    rt_qp_pad_at_start = gamepad_is_connected();
    rt_log("quick pockets: steal session on %s, %s at %d ms, bonus %d",
        object_name(target), rt_qp_state.resumed ? "resumed" : "fresh", rt_qp_state.elapsedMs, rt_qp_state.bonus);
}

void rt_quick_pockets_tick()
{
    if (!rt_qp_state.active) {
        return;
    }

    unsigned int tick = SDL_GetTicks();
    unsigned int delta = tick - rt_qp_last_tick;
    rt_qp_last_tick = tick;
    // A gap the steal screen didn't run through (a modal dialog, a hitch)
    // counts for at most 100 ms, as the world's own clock does.
    if (delta > 100) {
        rt_qp_state.suspendedMs += (int)(delta - 100);
        delta = 100;
    }

    bool focused = gamepad_is_focused();
    bool padLost = rt_qp_pad_at_start && !gamepad_is_connected();
    if (!focused || padLost) {
        rt_qp_state.suspendedMs += (int)delta;
        return;
    }

    rt_qp_state.elapsedMs = std::min(rt_qp_state.elapsedMs + (int)delta, 24 * 3600 * 1000);
    rt_qp_state.bonus = rt_quick_pockets_bonus_at(rt_qp_state.elapsedMs);
}

void rt_quick_pockets_end()
{
    if (!rt_qp_state.active) {
        return;
    }

    rt_quick_pockets_tick();
    if (rt_qp_state.target != NULL) {
        rt_qp_records[rt_qp_state.target->id] = { rt_qp_state.elapsedMs, rt_get_time() };
    }
    rt_log("quick pockets: steal session ends after %d ms (bonus %d)", rt_qp_state.elapsedMs, rt_qp_state.bonus);
    rt_qp_state.active = false;
    rt_qp_state.target = NULL;
}

int rt_quick_pockets_bonus(Object* target)
{
    if (!rt_qp_state.active || target == NULL || target != rt_qp_state.target) {
        return 0;
    }
    rt_quick_pockets_tick();
    return rt_qp_state.bonus;
}

void rt_quick_pockets_note_check(Object* target)
{
    rt_qp_state.checks++;
    rt_qp_state.lastCheckBonus = rt_quick_pockets_bonus(target);
    rt_qp_state.lastCheckElapsedMs = rt_qp_state.elapsedMs;
    rt_log("quick pockets: steal check at %d ms with bonus %d", rt_qp_state.lastCheckElapsedMs, rt_qp_state.lastCheckBonus);
}

const RtQuickPocketsState* rt_quick_pockets_state()
{
    return &rt_qp_state;
}

void rt_quick_pockets_reset()
{
    rt_qp_records.clear();
}

int rt_quick_pockets_record(int id)
{
    rt_qp_forget_expired(rt_get_time());
    auto it = rt_qp_records.find(id);
    return it != rt_qp_records.end() ? it->second.elapsedMs : -1;
}

#define RT_RULES_SAVE_MAGIC 0x5254524C // 'RTRL'
// 2: Quick Pockets records added.
#define RT_RULES_SAVE_VERSION 2

int rt_rules_save(DB_FILE* stream)
{
    if (db_fwriteInt(stream, RT_RULES_SAVE_MAGIC) == -1
        || db_fwriteInt(stream, RT_RULES_SAVE_VERSION) == -1
        || db_fwriteInt(stream, rt_xp_remainder) == -1) {
        return -1;
    }

    // Quick Pockets records, with the world time left before each expires
    // (the world clock starts over in every session).
    unsigned int now = rt_get_time();
    rt_qp_forget_expired(now);
    if (db_fwriteInt(stream, (int)rt_qp_records.size()) == -1) {
        return -1;
    }
    for (const auto& entry : rt_qp_records) {
        if (db_fwriteInt(stream, entry.first) == -1
            || db_fwriteInt(stream, entry.second.elapsedMs) == -1
            || db_fwriteInt(stream, (int)(now - entry.second.closedAt)) == -1) {
            return -1;
        }
    }
    return 0;
}

// Older saves end before this block (the remainder starts at 0); version 1
// has no Quick Pockets records.
int rt_rules_load(DB_FILE* stream)
{
    rt_xp_remainder = 0;
    rt_qp_records.clear();

    int magic;
    int version;
    if (db_freadInt(stream, &magic) == -1 || magic != RT_RULES_SAVE_MAGIC
        || db_freadInt(stream, &version) == -1 || version < 1 || version > RT_RULES_SAVE_VERSION) {
        return -1;
    }

    int remainder;
    if (db_freadInt(stream, &remainder) == -1) {
        return -1;
    }
    rt_skilled_xp_set_remainder(remainder);

    if (version >= 2) {
        int count;
        if (db_freadInt(stream, &count) == -1 || count < 0 || count > 10000) {
            return -1;
        }
        unsigned int now = rt_get_time();
        for (int index = 0; index < count; index++) {
            int id;
            int elapsedMs;
            int sinceClosed;
            if (db_freadInt(stream, &id) == -1
                || db_freadInt(stream, &elapsedMs) == -1
                || db_freadInt(stream, &sinceClosed) == -1) {
                return -1;
            }
            rt_qp_records[id] = { std::max(elapsedMs, 0), now - (unsigned int)std::max(sinceClosed, 0) };
        }
    }
    return 0;
}

} // namespace fallout
