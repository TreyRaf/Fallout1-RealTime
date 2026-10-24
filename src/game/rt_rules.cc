#include "game/rt_rules.h"

#include <math.h>

#include <algorithm>

#include <SDL.h>

#include "game/config.h"
#include "game/gconfig.h"
#include "game/item.h"
#include "game/map_defs.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/scripts.h"
#include "game/stat.h"
#include "game/tile.h"

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

    double reach;
    if (config_get_double(&game_config, RT_RULES_SECTION, "perk_bonus_hth_reach", &reach) && reach >= 0.0 && reach <= 10.0) {
        rt_rules_values.hthReachBonus = reach;
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

} // namespace fallout
