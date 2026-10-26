#ifndef FALLOUT_GAME_RT_RULES_H_
#define FALLOUT_GAME_RT_RULES_H_

#include <stddef.h>

#include "game/object_types.h"
#include "plib/db/db.h"

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
//
// Replaced traits:
//
//   trait_kamikaze_ap=10            Kamikaze: extra maximum Action Points
//   trait_kamikaze_armor_ac_pct=50  Kamikaze: share of worn armor's Armor
//                                   Class that counts (rounded down)
//   trait_fast_shot_rate_pct=125    Fast Shot: attack rate in percent (125 =
//                                   intervals divided by 1.25)
//   trait_bloody_mess_crit=3        Bloody Mess: extra critical chance (%)
//   trait_skilled_skill_bonus=10    Skilled: bonus to every skill (%)
//   trait_skilled_xp_pct=85         Skilled: share of positive XP awards
//
// Quick Pockets (replaced: a timed steal bonus, see `rt_quick_pockets_*`):
//
//   perk_quick_pockets_bonus=5        bonus per rank to the steal modifier
//                                     (percentage points; the modifier
//                                     raises the steal roll and lowers the
//                                     owner's catch roll, as the original's
//                                     own modifiers do)
//   perk_quick_pockets_grace_ms=2000  full bonus for this long
//   perk_quick_pockets_decay_ms=8000  then it falls to 0 over this long
//   perk_quick_pockets_reset_s=60     reopening the same target's steal
//                                     screen within this many seconds of
//                                     world time resumes the old timer
//
// Close combat:
//
//   melee_sweep_arc_deg=120           two-handed melee: the forward arc
//                                     (centered on the swing) a swing can
//                                     hit several targets in
//   melee_sweep_max_targets=3         two-handed melee: most targets per
//                                     swing
//   melee_counter_window_ms=3000      a missed close-combat attack gives
//                                     that enemy priority for its next
//                                     attack for this long
//   ranged_counter_window_ms=6000     the same after a missed shot or
//                                     throw (longer: the enemy may first
//                                     have to get within its own range)
//
// Attack AP costs (what the player pays; see `rt_attack_ap_cost`):
//
//   ap_punch=0.5                      fists and kicks, no weapon
//   ap_unarmed_weapon=1               Brass/Spiked Knuckles, Power Fist
//   ap_light_melee=1                  Knife, Combat Knife, Club
//   ap_heavy_melee=2                  Crowbar, Spear, Cattle Prod, Ripper,
//                                     Sledgehammer, Super Sledge
//   ap_throw=3                        every throw (never aimed)
//   ap_aimed_pistol=3                 guns: only AIMED SHOT costs AP
//   ap_aimed_rifle=4                  rifles, SMGs, shotguns
//   ap_aimed_big_gun=5                Flamer, Minigun, Rocket Launcher,
//                                     Gatling Laser
//   ap_aimed_close_extra=1            an aimed melee/unarmed attack costs
//                                     this much more than an ordinary one
//
// Enemy hit reactions:
//
//   hit_reaction_speed=2              playback speed of the ordinary hit
//                                     reactions of NPCs (not falls,
//                                     knockdowns, deaths, dodges, attacks or
//                                     movement); 1 = original
//
// Timing of repeated manual attacks (RT presses with melee, unarmed, single
// shots and bursts; not held automatic fire, throws or AIMED SHOT). The
// penalty comes off the hit chance of the next attack only:
//
//   timing_window_ms=300              a press this close to the end of the
//                                     current attack's recovery is well
//                                     timed (no penalty)
//   timing_early_penalty=10           an earlier buffered press: up to this
//                                     much (all of it at twice the window)
//   timing_spam_penalty=5             each extra press during the current
//                                     attack
//   timing_max_penalty=25             most the two take together

typedef struct RtRules {
    double hthReachBonus;
    int moveDrainPct;
    int rateOfFireRangePct;
    int heaveHoRangePct;
    int kamikazeAp;
    int kamikazeArmorAcPct;
    int fastShotRatePct;
    int bloodyMessCrit;
    int skilledSkillBonus;
    int skilledXpPct;
    int quickPocketsBonus;
    int quickPocketsGraceMs;
    int quickPocketsDecayMs;
    int quickPocketsResetS;
    int sweepArcDeg;
    int sweepMaxTargets;
    int counterWindowMs;
    int rangedCounterWindowMs;
    double apPunch;
    double apUnarmedWeapon;
    double apLightMelee;
    double apHeavyMelee;
    double apThrow;
    double apAimedPistol;
    double apAimedRifle;
    double apAimedBigGun;
    double apAimedCloseExtra;
    double hitReactionSpeed;
    int timingWindowMs;
    int timingEarlyPenalty;
    int timingSpamPenalty;
    int timingMaxPenalty;
} RtRules;

// Weapon groups for the attack AP costs. Listed by item for Fallout 1's
// weapons (animation codes and skills don't match the groups: the 14mm and
// .223 pistols use SMG animations, the Flamer a rifle one); unlisted weapons
// fall back to their attack type, hands and Big Guns skill.
typedef enum RtWeaponClass {
    RT_WEAPON_CLASS_NONE,
    RT_WEAPON_CLASS_UNARMED,
    RT_WEAPON_CLASS_LIGHT_MELEE,
    RT_WEAPON_CLASS_HEAVY_MELEE,
    RT_WEAPON_CLASS_THROWN,
    RT_WEAPON_CLASS_PISTOL,
    RT_WEAPON_CLASS_RIFLE,
    RT_WEAPON_CLASS_BIG_GUN,
} RtWeaponClass;

RtWeaponClass rt_weapon_class(Object* weapon);

// A thrown weapon (its primary attack is a throw: Rock, Throwing Knife,
// grenades, Molotov Cocktail, Flare). The player can only hold these in the
// right hand (item 2), and throws them with RB. The Spear is a melee
// weapon with a secondary throw, not a thrown weapon.
bool rt_is_thrown_weapon(Object* item);

// The AP the player pays for an attack with `hitMode`: melee and unarmed
// attacks and throws always cost AP; gunfire only as an AIMED SHOT (0
// otherwise). Reloads cost nothing here. Fractions are possible (0.5).
double rt_attack_ap_cost(Object* critter, int hitMode, bool aimed);

// "0.5", "1", "2.5": an AP amount for messages and the HUD (rounded down
// to a half point).
const char* rt_ap_text(double ap, char* buffer, size_t size);

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

// Kamikaze: the part of worn armor's Armor Class that doesn't count. The
// part that counts is rounded down: 15 at 50% counts as 7, so 8 is removed.
int rt_kamikaze_armor_ac_removed(int armorClass);

// Fast Shot: true for the player's attack animations (and the target's
// reaction to them), which play `fastShotRatePct` percent as fast.
bool rt_fast_shot_active();
// An attack interval of the player's, shortened by Fast Shot (`ms` / 1.25).
unsigned int rt_fast_shot_interval(unsigned int ms);

// Skilled: a positive experience award scaled by `skilledXpPct`. The
// fraction left over is kept (in hundredths of a point, saved with the game)
// and added to the next award, so small awards lose nothing to rounding.
// Returns `xp` unchanged without Skilled or for awards <= 0.
int rt_skilled_xp_award(int xp);
int rt_skilled_xp_remainder();
void rt_skilled_xp_set_remainder(int hundredths);

// Quick Pockets: a steal session's timer. It starts when the steal screen
// becomes usable and runs on real time while the screen (and its quantity
// selector) is open, even though the world is paused; it is suspended while
// the window has no focus, while the controller the session started with is
// disconnected, and across frames the screen doesn't run (a modal dialog:
// at most 100 ms of such a gap counts). Closing and reopening the same
// target within `quickPocketsResetS` seconds of world time resumes the old
// elapsed time instead of a fresh bonus.
void rt_quick_pockets_begin(Object* target);
void rt_quick_pockets_tick();
void rt_quick_pockets_end();
// The bonus to the steal modifier right now for `target` (0 outside its
// session, without the perk, or once it has decayed).
int rt_quick_pockets_bonus(Object* target);
// The bonus after `elapsedMs` at the player's current rank.
int rt_quick_pockets_bonus_at(int elapsedMs);
typedef struct RtQuickPocketsState {
    bool active;
    Object* target;
    int elapsedMs;
    int bonus;
    bool resumed;
    int sessions;
    int suspendedMs;
    // Steal checks made (`skill_check_stealing`), with the bonus and the
    // elapsed time the last one used.
    int checks;
    int lastCheckBonus;
    int lastCheckElapsedMs;
} RtQuickPocketsState;
// Records a steal check on `target` (the bonus it used).
void rt_quick_pockets_note_check(Object* target);
const RtQuickPocketsState* rt_quick_pockets_state();
// Forgets the remembered targets (new game, load).
void rt_quick_pockets_reset();
// The remembered elapsed time for the target with object `id` (-1 when
// none, or expired).
int rt_quick_pockets_record(int id);

// Save data block for the rules' own state (after the favorites block).
int rt_rules_save(DB_FILE* stream);
int rt_rules_load(DB_FILE* stream);

} // namespace fallout

#endif /* FALLOUT_GAME_RT_RULES_H_ */
