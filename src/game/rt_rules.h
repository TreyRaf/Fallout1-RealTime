#ifndef FALLOUT_GAME_RT_RULES_H_
#define FALLOUT_GAME_RT_RULES_H_

#include "game/object_types.h"

namespace fallout {

// RT: Character rules of Fallout1-RealTime that replace original Fallout 1
// rules (perk cadence, companion limit, replaced perk effects). The balance
// values are read from the [realtime] section of fallout.cfg:
//
//   perk_bonus_hth_reach=1.5        Bonus HtH Attacks: extra melee/unarmed
//                                   reach in hexes (Euclidean, hex centers)
//   perk_bonus_move_drain_pct=50    Bonus Move: lower sprint AP drain while
//                                   in combat, in percent
//   perk_rate_of_fire_range_pct=25  Bonus Rate of Fire: smaller distance
//                                   penalty for guns and energy weapons, in
//                                   percent
//   perk_heave_ho_range_pct=20      Heave Ho!: smaller distance penalty for
//                                   throwing, in percent per rank

typedef struct RtRules {
    double hthReachBonus;
    int moveDrainPct;
    int rateOfFireRangePct;
    int heaveHoRangePct;
} RtRules;

const RtRules* rt_rules();

// Overrides a balance value (tests). Pass NULL to read fallout.cfg again.
void rt_rules_set(const RtRules* rules);

// Perk selections earned by `level`: one every 2 levels, the first at 2.
int rt_perk_selections_earned(int level);

// Perk selections the original rules awarded up to `level` (every 3 levels,
// every 4 with Skilled). Used to convert saves made before the change.
int rt_perk_selections_original(int level, bool skilled);

// Companions: recruitable followers (Ian, Dogmeat, Tycho, Katja) as opposed
// to temporary quest followers that also use the party system (Tandi, the
// invaders of the ending, ...).
bool rt_is_companion(Object* critter);

// floor(effective Charisma / 2), at most 5.
int rt_companion_limit();

// Distance between the centers of two hexes, in hexes (adjacent hexes are 1
// apart). Not rounded: hexes 2 steps away are 1.73 or 2.0 apart.
double rt_tile_distance_exact(int tile1, int tile2);

// Same as `obj_dist` (multihex objects count from their edge) but exact.
double rt_obj_distance_exact(Object* object1, Object* object2);

// The distance part of a ranged or thrown attack's penalty (always >= 0),
// reduced by Bonus Rate of Fire or Heave Ho! for the player.
int rt_distance_penalty(Object* attacker, int attackType, int penalty);

// Melee/unarmed reach bonus in hexes (Bonus HtH Attacks), 0 without it.
double rt_reach_bonus(Object* attacker, int attackType);

// Sprint AP drain multiplier in percent (100 = normal).
int rt_sprint_drain_pct(bool inCombat);

} // namespace fallout

#endif /* FALLOUT_GAME_RT_RULES_H_ */
