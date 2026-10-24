# Fallout1-RealTime character rules

These rules replace the original Fallout 1 rules where they conflict.
The code lives in:

- `src/game/rt_rules.cc`: the balance values, cadence, companion limit, exact hex distance, and long-range and sprint helpers.
- `stat.cc`, `skill.cc`, `perk.cc` and `editor.cc`.
- `combat.cc`, `item.cc`, `party.cc` and `int/support/intextra.cc`.

## Balance values (fallout.cfg, `[realtime]`)

| Key | Default | Effect |
|---|---|---|
| `perk_bonus_hth_reach` | `1.5` | Bonus HtH Attacks: extra melee/unarmed reach in hexes, measured between hex centers |
| `perk_bonus_move_drain_pct` | `50` | Bonus Move: lower sprint AP drain in combat (%) |
| `perk_rate_of_fire_range_pct` | `25` | Bonus Rate of Fire: smaller distance penalty for guns and energy weapons (%) |
| `perk_heave_ho_range_pct` | `20` | Heave Ho!: smaller distance penalty for throwing, per rank (%) |

Missing keys use the defaults. Perk descriptions show whatever values are configured.

## SPECIAL formulas (before → after)

| Value | Original Fallout 1 | Fallout1-RealTime |
|---|---|---|
| Armor Class (base) | AG | **EN**. Armor, perk and trait modifiers are unchanged; Kamikaze still removes the base. |
| Action Points | 5 + AG/2 | unchanged |
| Small Guns | 35 + AG | unchanged |
| Big Guns | 10 + AG | unchanged |
| Energy Weapons | 10 + AG | **10 + IN** |
| Unarmed | 65 + (AG + ST)/2 | **65 + ST**. Agility's half moves to Strength, so Strength counts in full: (ST + ST)/2. |
| Melee Weapons | 55 + (AG + ST)/2 | **55 + ST** (same) |
| Throwing | 40 + AG | **40 + ST** |
| Sneak | 25 + AG | **25**. Loses Agility; no replacement. |
| Lockpick | 20 + (PE + AG)/2 | unchanged |
| Steal | 20 + AG | unchanged |
| Traps | 20 + (PE + AG)/2 | unchanged |
| Hit Points, Carry Weight, Melee Damage, Sequence (2×PE), Healing Rate, Critical Chance, Rad/Poison Resistance | — | unchanged |
| Other skills | — | unchanged |
| Companions | no limit | **min(5, floor(effective CH / 2))** |

All skill values still add invested skill points, the tag bonus, traits, perks and the difficulty modifier, as before. The player's derived statistics are recalculated:

- whenever a SPECIAL base or bonus changes, as before;
- when a save loads, so old saves get Endurance-based AC.

### Agility kept outside the formulas

These uses of Agility were left as they are, because they are not skill or derived-stat formulas:

- the critical-hit tables' saving rolls (knockdown, crippling) against AG;
- scripts' own attribute checks;
- perk prerequisites;
- trait modifiers to Agility (Small Frame, Gifted).

## Perks

### Cadence

- **Award schedule:** one selection at levels 2, 4, 6, 8 and so on. Skilled no longer delays perks, and the original cap of 7 perks is removed.
- **Pending selections:** these are a count and are saved. The save byte gets a format bit; old saves stored only 0 or 1.
- **Old saves:** spent = selections the original rule awarded up to the last processed level, minus the one pending. Pending now = selections the new rule awards for those levels, minus the spent ones. Perks already taken are never removed.
- **No duplicates:** each level is processed once (`last_level`), so reopening the Pip-Boy or loading a save never awards twice.

### Minimum levels

Only the changes are listed. Every other perk keeps its original level, rank limit and prerequisites.

| Perk | Old level | New level | Notes |
|---|---|---|---|
| Animal Friend | 9 | 4 | specified |
| Action Boy | 12 | 4 | specified |
| Awareness | 3 | 2 | specified |
| Better Criticals | 9 | 6 | specified |
| Bonus HtH Damage | 3 | 2 | specified |
| Bonus Rate of Fire | 9 | 8 | specified; new effect |
| Dodger | 9 | 10 | specified |
| Friendly Foe | 6 | 4 | specified; new effect |
| Toughness | 3 | 2 | specified |
| Scrounger | 9 | 8 | specified |
| Educated | 6 | 4 | specified |
| Presence | 3 | 4 | specified |
| Smooth Talker | 3 | 4 | specified |
| Earlier Sequence | 3 | 2 | narrow: Sequence barely matters without turns |
| Faster Healing | 3 | 2 | modest: +1 Healing Rate per rank |
| Night Vision | 3 | 2 | narrow: only in dark areas |
| Strong Back | 3 | 4 | broadly useful: +50 lb per rank, all game long |
| Survivalist | 3 | 2 | narrow utility: Outdoorsman only, needs Outdoorsman 40% |
| Healer | 3 | 2 | utility: more HP from First Aid/Doctor |
| Scout | 3 | 2 | narrow utility: world map reveal |
| Quick Pockets | 3 | 2 | very narrow: inventory access has no AP cost in real time |
| Swift Learner | 3 | 4 | broadly useful: +5% XP per rank, compounding |

No selectable perk is left at level 3.

Effects changed but levels kept, because none of these was a level-3 perk:

| Perk | Level | Ranks | Change |
|---|---|---|---|
| Bonus HtH Attacks | 6 | 1 | effect changed |
| Bonus Move | 6 | 3 → **1** | effect changed; its new effect doesn't stack |
| Heave Ho! | 6 | 3 | effect changed |

### Replaced effects

- **Bonus HtH Attacks:**
  - Melee and unarmed reach grows from the weapon range R to R + 1.5 hexes, measured between hex centers.
  - Hexes two steps away are 1.73 or 2.00 apart; hexes three steps away are 2.65 to 3.00 apart.
  - So a 1-hex weapon reaches every hex two steps away and none three steps away.
  - A 2-hex weapon (spear) reaches all of ring 3 and the ring-4 hexes 3.46 away, but not those 3.61 away.
  - The extra reach needs the same elevation and does not pass walls or scenery.
  - It applies to the player only (AI critters have no perks).
  - It applies to ordinary attacks, AIMED SHOT, the lock indicator and attack execution. Movement and interaction range are unchanged.
  - The old −1 AP cost is removed.
- **Bonus Move:**
  - Sprint AP drain is 50% lower while in combat.
  - "In combat" means a living hostile critter on the same elevation within 30 hexes is engaged with the player's team.
  - Outside combat sprinting costs nothing anyway (see Sprinting below). Attack and AIMED SHOT costs are unchanged.
  - The old 2 free move AP per rank (turn-based) is removed.
- **Bonus Rate of Fire:**
  - The penalty for targets beyond 2×PE hexes is 4% per hex (12% when blinded). For guns and energy weapons it is 25% smaller.
  - The bonus for close targets, maximum range, line of fire and the 95% cap are unchanged.
  - The old −1 AP cost is removed, and there are no extra shots.
- **Heave Ho!:**
  - The same penalty for thrown weapons is 20% smaller per rank: 20%, 40% or 60%.
  - Throwing range is again 3×ST; the old +2 ST for range is removed.
- **Friendly Foe:**
  - Shots from party members on the player's team, with guns or energy weapons, never hurt the player. This covers bursts, rounds that pass through, and missed shots.
  - It doesn't cover explosive damage, thrown weapons, melee, or a party member that turned hostile.
  - The old green team outline is removed.

#### Why 25% and 20%

- **Bonus Rate of Fire:** Sharpshooter rank 1 is worth a flat +8%. The 25% reduction gives +3% at 3 hexes beyond range and +8% at 8 hexes beyond, so it only equals Sharpshooter at range. That suits a level-8 perk that no longer speeds up firing.
- **Heave Ho!:** throws are already capped at 3×ST hexes, so the penalty never grows large. 20% per rank keeps one pick modest, and three picks (60%) are a real investment.

## Charisma and companions

- **Who counts:** recruitable companions are identified by their scripts: Ian, Dogmeat (`Junkdog.int`), Tycho and Katja.
- **Who doesn't count:** temporary followers that also use the party system are neither counted nor refused. These are Tandi during her rescue, the ending's invaders, and the critter running `Fire.int`.
- **Enforcement:** the limit lives in `partyMemberAdd`, the only way into the party besides the player, using effective Charisma at recruitment time.
- **When Charisma drops:** nobody is dismissed, but new companions are refused until the count is back under the limit.
- **Script undo:** recruitment scripts set their "joined" globals (Dogmeat's also gives 100 XP) before calling `party_add`, which returns nothing to the script. When the party refuses, the changes that procedure made are undone: global and local variables, and the XP unless it caused a level-up. The game then prints "X can't join you. Your Charisma lets you lead N companions."
- **Pip-Boy:** the Stats tab shows "Companions (Charisma) x / limit", "Perks to choose", and "Next perk at level".

## Texts

These texts are updated in memory; the game's .msg files are not changed:

- descriptions of Strength, Endurance, Charisma, Intelligence and Agility;
- the skill formula part for Energy Weapons, Unarmed, Melee Weapons, Throwing and Sneak;
- the Skilled trait (the "perk every four levels" sentence is removed);
- the five perks whose effects changed.

## Automated tests (`--rt-autotest`)

There are 17 new checks across 8 steps: `character rules: ...` and `perk selections after load`. They cover:

- SPECIAL deltas, with each attribute changed by 2;
- the Charisma limit table and recruitment;
- perk cadence (levels 1 to 9, repeated processing, a multi-level jump, spending a selection), the requirement table, descriptions, and the conversion of old saves;
- save/load of pending selections;
- reach at 1.73, 2.00, 2.65 and 3.00 hexes, a spear at 3.46 and 3.61, a test wall, and lock, AIMED SHOT and attack at 2 hexes;
- long-range chances for a pistol, a laser pistol and thrown weapons, and the indicator matching the rolled chance;
- 200-burst Friendly Foe trials;
- measured sprint drain in and out of combat.

`--rt-autotest-only=dump` logs the game's perk, stat, skill and trait texts and every script call to `party_add`, `party_remove` or `party_member_obj`.

## Sprinting and AP

These rules replace the earlier rule that every sprint drains AP.

- **Outside combat:** sprinting costs no AP. L3 turns running on even at 0 AP, and AP regenerates while sprinting (except during AIMED SHOT execution, as before).
- **In combat:** sprinting drains 1 AP per `run_drain_ms_per_ap` (400 ms), and Bonus Move reduces that.
  - Starting a run in combat needs 1 AP.
  - At 0 AP the player walks again and the toggle clears, so a new L3 click is needed.
- **Combat detection:** combat means a living hostile critter on the player's elevation, within 30 hexes, engaged with the player's team (`rt_player_in_combat`). A target lock alone doesn't count, so locking a neutral NPC never starts the drain.
- **Changes during a run:** the drain is checked every frame. It starts when a hostile engages mid-run and stops the frame combat ends.
- **AIMED SHOT AP costs:** unchanged.
