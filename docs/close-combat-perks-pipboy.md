# Attack AP, throws, hit reactions, enemy attacks, RT timing, counterattacks, sweeps, reloads, Quick Pockets, perk artwork, Pip-Boy controls

Values come from `[realtime]` in `fallout.cfg` (defaults shown).

| Key | Default | Meaning |
|---|---|---|
| `melee_sweep_arc_deg` | 120 | Two-handed melee: width of the forward arc a swing hits, centered on the way the player faces |
| `melee_sweep_max_targets` | 3 | Two-handed melee: most targets per swing (1–8) |
| `melee_counter_window_ms` | 3000 | How long an enemy keeps its counterattack priority after the player's close-combat attack missed it |
| `ranged_counter_window_ms` | 6000 | The same after a missed shot or throw (longer: the enemy may first have to get within its weapon's range) |
| `hit_reaction_speed` | 2 | Playback speed of NPCs' ordinary hit reactions (1–4; 1 = original). Falls, knockdowns, deaths, dodges, attacks and movement keep their speed |
| `timing_window_ms` | 300 | RT timing: a press this close to the end of the current attack's recovery is well timed (no penalty) |
| `timing_early_penalty` | 10 | RT timing: hit chance lost by an earlier buffered press (in full at twice the window, scaled in between) |
| `timing_spam_penalty` | 5 | RT timing: hit chance lost per extra press during the current attack |
| `timing_max_penalty` | 25 | RT timing: most the early and spam penalties take together |
| `ap_punch` | 0.5 | Fists and kicks (no weapon) |
| `ap_unarmed_weapon` | 1 | Brass Knuckles, Spiked Knuckles, Power Fist |
| `ap_light_melee` | 1 | Knife, Combat Knife, Club |
| `ap_heavy_melee` | 2 | Crowbar, Spear, Cattle Prod, Ripper, Sledgehammer, Super Sledge |
| `ap_throw` | 3 | Every throw |
| `ap_aimed_pistol` | 3 | AIMED SHOT with a pistol |
| `ap_aimed_rifle` | 4 | AIMED SHOT with a rifle, SMG or shotgun |
| `ap_aimed_big_gun` | 5 | AIMED SHOT with a big gun |
| `ap_aimed_close_extra` | 1 | An aimed melee or unarmed attack costs this much more than an ordinary one |
| `perk_quick_pockets_bonus` | 5 | Quick Pockets: bonus per rank, in steal-modifier points |
| `perk_quick_pockets_grace_ms` | 2000 | Quick Pockets: the full bonus lasts this long |
| `perk_quick_pockets_decay_ms` | 8000 | Quick Pockets: then it falls to 0 over this long |
| `perk_quick_pockets_reset_s` | 60 | Quick Pockets: reopening the same target within this many seconds (world time) resumes the old timer |

## Attack AP costs

These replace the original weapon AP costs for what the player pays (`rt_attack_ap_cost`):

| Attack | Ordinary | AIMED SHOT |
|---|---|---|
| Fists, kicks (no weapon) | 0.5 | 1.5 |
| Brass Knuckles, Spiked Knuckles, Power Fist | 1 | 2 |
| Knife, Combat Knife, Club | 1 | 2 |
| Crowbar, Spear, Cattle Prod, Ripper, Sledgehammer, Super Sledge | 2 | 3 |
| Pistols: 10mm, 14mm, .223, Desert Eagle .44, 9mm Mauser, Laser, Plasma, Alien Blaster | 0 | 3 |
| Rifles, SMGs, shotguns: 10mm SMG, Hunting, Assault, Sniper, Laser, Plasma, Turbo Plasma Rifle, Shotgun, Combat Shotgun, both Red Ryders | 0 | 4 |
| Big guns: Flamer, Minigun, Rocket Launcher, Gatling Laser | 0 | 5 |
| Throws: Rock, Throwing Knife, the three grenades, Molotov Cocktail, Flare, and the Spear's throw | 3 | can't be aimed |

- **Groups:** the weapons are listed by item. Weapon animations don't match the groups (the 14mm and .223 pistols use SMG animations, the Flamer a rifle one). A weapon from a mod falls back to its attack type, its hands and the Big Guns skill.
- **Other modes:** the second attack of a weapon costs by its kind: the Knife's thrust 1, the Sledgehammer's thrust 2, the Rock's punch 1, the Throwing Knife's swing 1, a burst 0.
- **Gunfire:** ordinary shots and bursts cost no AP. Guns cost AP only as an AIMED SHOT, and the figure above is the whole cost (nothing is added).
- **Aimed melee:** the ordinary cost + 1 (`ap_aimed_close_extra`). Burst and flame attacks still can't be aimed.
- **Half points:** AP is kept with fractions, so two punches cost 1 AP. Messages and the AIMED SHOT panel show halves ("0.5", "9.5"), rounded down.
- **When it's spent:** once, when the attack is committed: the swing animation starts, the lock-on attack or the throw is registered. Hit, miss or an empty swing all cost the same.
- **Too little AP:** the attack doesn't happen and the message "Not enough Action Points: this attack needs N." appears (at most once a second). A press made during the previous attack's cooldown still waits up to 1.2 s, so AP regenerating meanwhile can pay for it.
- **Attack rate unchanged:** cooldowns still come from the original AP costs (110 ms per original AP, Fast Shot included), and so does the enemies' wait between attacks. Cheaper attacks cost less AP, not faster swings. Enemies don't pay AP.
- **What doesn't change:**
  - Lock-on stays optional.
  - AIMED SHOT pays only its own cost: no ordinary attack cost is added on top.
  - Sprinting is free outside combat, and the combat sprint drain is unchanged.
- **HUD:**
  - The item slot shows the cost: "AP 0.5" with fists, "AP 2" with a Crowbar. Ordinary gunfire shows no number (it costs nothing).
  - With a lock and too little AP for a melee attack or a throw, the lock indicator reads "75%  2 AP needed" in amber.

## Thrown weapons and RB

- **Which items:** weapons whose primary attack is a throw: Rock, Throwing Knife, the three grenades, Molotov Cocktail, Flare. The Spear is a melee weapon with a secondary throw, so it can still be held in either hand.
- **Right hand only:** thrown weapons only go in the right hand (item 2).
  - The Pip-Boy offers no "Equip left" for them; equipping one into the right hand says "RB throws it".
  - A favorite holding a thrown weapon puts it in the right hand, whatever hand is active.
  - A thrown weapon is never the active item: if the right hand is active when one goes there, the left hand becomes active.
  - One found in the left hand anyway (an old save, a script) moves to an empty right hand, or back to the inventory, with a message.
- **RB throws** the right hand's thrown weapon at the locked target, without changing the active item:
  - the same rules as any throw: a lock, range, line of sight, the attack cooldown, 3 AP;
  - the throw uses the thrown weapon's animation set (grenades: the bare-handed one), then the active weapon's sprite comes back;
  - the next item of the stack fills the right hand, as in the original;
  - one throw per press; holding RB doesn't repeat.
- **RB without a thrown weapon** in the right hand switches hands, as before.
- **The lock indicator** keeps showing the active item's hit chance (a thrown weapon in the right hand doesn't change it).
- **Not aimed:** LB with a throw mode active (the Spear's) says "Throws can't be aimed. Press RB to throw."

## Enemy hit reactions

- **Faster reactions:** an NPC's ordinary hit reaction (hit from the front or back) plays at `hit_reaction_speed` (2×). Nothing else is sped up: falls, knockdowns, knockouts, deaths, dodges, attacks, projectiles and movement keep their speed. The player's own reactions are unchanged.
- **Back to pursuit at once:** the attacker's animation often goes on after the target's reaction, for example the rest of a punch or kick, or lowering a gun. A target whose ordinary reaction is over may move again right away, before the attack lands. It can't attack until then, and the damage is still applied when the attack lands.
  - Its finished part of the attacker's animation sequence is detached from it (`anim_rt_detach_finished`), so the end of that sequence doesn't stop the target's walk or reset its pose.
  - Deaths, knockdowns, knockouts, knockback and burning keep the target until the attack lands, as before. A target the hit kills when it lands, having been hurt meanwhile, stops walking first.
- **No queue:** a target is in at most one attack at a time (a new attack waits until the last one landed), so repeated hits never stack up reactions.

## Enemy attacks on the player

- **Only valid attacks:** before an enemy commits an attack it needs all of:
  - its weapon's range or melee reach, and ammunition;
  - a clear shot (no wall or scenery the shot can't pass) and, for ranged attacks, a clear view: no wall or scenery that light doesn't pass, such as a tent or a building (`obj_sight_blocking_at`);
  - a hit chance above 0%. Any positive chance is enough.
- **Otherwise it repositions:** it walks toward the player (around obstacles) and checks again as both move, attacking as soon as it has a valid attack and its cooldown allows. It doesn't take a place in the attack slot queue meanwhile, and no impossible attack is started or queued.
- **The player isn't held by ordinary attacks:** an enemy attack with an ordinary outcome (a hit with no knockdown, knockout, knockback, fire, death or weapon loss, or a miss) leaves the player out of the enemy's animation.
  - The player keeps walking, attacking, reloading or using items while the enemy aims and fires.
  - On a hit, the player's own short hit reaction plays at the impact if the player is standing. A walk, attack, reload, chem use or holster of theirs is never interrupted (no reaction plays then). A miss plays none.
  - The damage is applied when the attack lands, as before.
  - Heavy outcomes (knockdown, knockout, knockback, fire, death, weapon loss) play in the enemy's sequence and hold the player as before. Such an attack waits while the player is in an attack of their own.
- **One enemy attack at a time:** still one committed enemy attack on the player at a time (the slot), with the 0.7 s recovery after each. Enemies may now attack while the player's own attack plays, and a buffered RT or RB press, reload or chem use no longer holds them back (only AIMED SHOT planning does).

## RT timing and spam

- **One buffered attack:** a press during the current attack or its recovery is kept for 1.2 s and starts the next attack when the player is ready. More presses never queue more attacks.
- **Timing modifier:** taken when that next attack is committed, used once, and only ever lowers the normal hit chance (skills, weapon, range, armor and the rest still set it):
  - a fresh press after the recovery: no penalty;
  - a press within `timing_window_ms` of the recovery's end (the attack's end plus its cooldown, not its impact frame): no penalty;
  - an earlier buffered press: up to `timing_early_penalty`;
  - each extra press during the attack: `timing_spam_penalty`;
  - at most `timing_max_penalty` in all.
- **What it applies to:** RT attacks with melee, unarmed, single shots and bursts (each press). Not held automatic fire (its repeats aren't presses), throws, or AIMED SHOT.
- **Cleared** (presses, penalty and the buffered press) by a weapon or attack mode change, a new target, the player being unable to act (death, knockdown), AIMED SHOT, and a menu, focus loss or controller disconnect.
- **Costs:** AP and ammunition are only spent by attacks actually committed.
- **Indicator:** while a press is buffered, the lock-on hit chance includes its current timing penalty.
- **Enemies keep going:** the player's presses don't restart enemy reactions, freeze AI or reserve the attack slot; the enemy being hit may move as soon as its reaction is over and attacks between the player's attacks.

## Hunting Rifle reload

- The Hunting Rifle reloads with its armed idle animation (the one it fidgets with when idle) instead of the holster and draw: replayed while a whole one fits, then the pose is held.
- It takes as long as the holster and draw would (2.25 s with the default art). The ammunition moves once, where the holster would have ended; there is no firing until the reload is over.
- Another animation of the player's starting meanwhile (a heavy hit, a fall) ends it: nothing is loaded before the ammunition moved; afterwards the reload counts. Ordinary enemy hits don't interrupt it.
- Other weapons reload with their holster and draw animations as before.

## Counterattacks after a miss

- **Trigger:** when any ordinary (non-aimed) attack of the player's misses an enemy, that enemy gets the next chance to attack the player:
  - a melee or unarmed swing;
  - a shot, or a burst none of whose rounds hit it;
  - a throw, unless its blast still damaged it.
- **What the enemy does:** its usual attack with whatever it holds.
  - A gun in range and in sight: it shoots back from where it stands.
  - Out of range or out of sight: it moves until it can attack, then attacks. A melee or unarmed enemy closes in and hits.
- **Window:** 3 s after a missed swing (`melee_counter_window_ms`), 6 s after a missed shot or throw (`ranged_counter_window_ms`), so an enemy has time to get within range.
- **What's guaranteed:** an attempt, with its own to-hit roll and damage; not a hit.
- **Scheduler:** it goes through the existing one-attack-at-a-time slot.
  - An enemy attack already committed is never interrupted.
  - When the slot is free and the enemy is ready, it goes before any other waiting enemy.
  - It doesn't wait for a buffered player press, reload or chem use (no enemy does); it does wait for AIMED SHOT, as every enemy does.
- **Limits:** the enemy still needs reach, line of sight and its own cooldown, and must be alive and not knocked down or out.
  - The request expires after its window, or as soon as the enemy can't fight. It never blocks the queue.
  - One request per enemy at a time; a duplicate is ignored.
  - One per swing or shot: a sweep that misses several enemies gives one counterattack, to the first miss resolved.
  - A swing that meets nobody gives none. AIMED SHOT misses give none. Other enemies nearby get nothing from someone else's miss.
  - The missed enemy targets the player for that attack.

## Two-handed melee sweeps

- **Which weapons:** the weapon's two-handed flag (`WEAPON_TWO_HAND`) plus a melee attack in the hit mode used. In Fallout 1 data that is the **Sledgehammer** and the **Super Sledge**.
  - Not two-handed firearms (Hunting Rifle, Minigun and the like: ranged attacks).
  - Not the Spear (it isn't flagged two-handed; its secondary attack is a throw anyway).
  - Not one-handed melee weapons (Knife, Club, Cattle Prod, Ripper) or unarmed weapons (Power Fist, Spiked Knuckles).
  - No extra metadata was needed.
- **The arc:** 120° (`melee_sweep_arc_deg`), centered on the way the player faces at the impact frame.
  - With a locked target in reach, the player first turns to face it, wherever it stands; otherwise the facing is kept (turning to an enemy in the front arc, as for every swing).
  - Angles are measured on true hex geometry, so the three hexes in front (straight ahead and 60° either side) are in the arc.
- **Who is hit:** every target that is all of these, up to `melee_sweep_max_targets`:
  - a living critter, not the player's team;
  - on the player's elevation;
  - in the arc and within the weapon's reach, with Bonus HtH Attacks included;
  - not behind a wall or scenery (also checked inside the weapon's own reach, past the adjacent hexes);
  - an enemy, the locked target, or a bystander standing straight ahead (as for one-handed swings, a bystander beside the swing is never hit).
  - Order: the target aimed at first, then the closest, then the best aligned.
- **Each target separately:** its own to-hit roll, armor, damage and criticals (a full ordinary attack each). Each target is attacked at most once per swing; a target busy with another attack is retried until the impact wait (400 ms) ends, then skipped.
- **One AP charge** per swing, however many targets.
- **Consequences:** hitting a bystander makes them and their team hostile, as before.
- **AIMED SHOT:** body-part attacks queued with AIMED SHOT stay single attacks on their target.

## Quick Pockets (replaced)

- **Old effect removed:** the inventory's 4 AP cost is no longer lowered (it is a flat 4 in turn-based combat, which real time doesn't use).
- **New effect:** stealing quickly gives a bonus that fades the longer the player takes.
  - The bonus is `perk_quick_pockets_bonus` per rank (5, 10 or 15) added to the original steal modifier.
  - It raises the player's steal roll and lowers the owner's roll to notice by the same amount, as the original's own modifiers do (e.g. +20 for a knocked-out target).
  - Full for the first 2 s, then linear down to 0 at 10 s: 5 per rank at 0–2 s, 3 at 4.5 s, 2 at 6 s, 0 from 10 s (rounded down).
  - Afterwards the chance is exactly the normal one (Steal skill and every original modifier). Waiting never takes it below that.
- **Caps:** the steal roll is still capped at 95%, criticals still apply, and the owner can still notice, so a quick theft isn't guaranteed. At the cap the bonus still helps through the owner's roll.
- **Timing:**
  - The timer starts when the steal screen becomes usable and runs on real time while it is open, even though the world is paused.
  - It keeps running when switching sides, moving between items and inside the quantity selector; nothing restarts it.
  - Each actual theft (A on an item, or the confirmed amount of a stack) recalculates the bonus at that moment.
  - The chance shown in the panel follows the bonus as it fades ("Take unnoticed: 82%  (Quick +5)"; once it has faded, the line shows the Steal skill again as before). It is computed from the original rolls without rolling.
- **Suspended:**
  - while the window has no focus;
  - while the controller the session started with is disconnected;
  - across frames the screen doesn't run (a modal dialog such as the quit confirmation): such a gap counts at most 100 ms, like the world clock.
- **Reopening:** closing and reopening the same target's steal screen within 60 s of world time resumes the old elapsed time, so the bonus doesn't come back. After 60 s it starts fresh.
  - World time doesn't pass in menus or the Pip-Boy; resting passes it.
  - The remembered times are saved with the game.
- **Scope:** only genuine steal attempts (the steal screen opened by Steal on a living critter). Looting containers and corpses, trading, and moving the player's own items never time a bonus.
  - Fallout 1 has no separate owned-container theft roll, so there is nothing else to apply it to.
  - Detection, hostility, the owner's script and item restrictions are the original ones.
- **Prerequisites kept:** level 2, Agility 5, three ranks.

## Perk artwork in the Pip-Boy

- **Where it shows:**
  - In the perk chooser (Stats → Perks → "Choose a new perk") the highlighted perk shows its card: the original illustration on the right, and the name, rank ("Takes rank 2 of 3" or "Single rank"), requirements ("Requires: Level 2, Agility 5") and description in the column left of it.
  - The confirmation screen and owned perks on the Perks page show the same card ("Rank 1 of 3").
- **Mapping:** the original character screen's own: skilldex.lst entry perk + 72 (`DrawCard2` in editor.cc).
  - All 53 selectable perks have their artwork. Modified perks (Quick Pockets, Bonus HtH Attacks, Bonus Move and the others) keep their original illustration.
  - A missing image would show a "No artwork" box and be logged once ("pipboy: no artwork for perk …").
- **Layout:** the artwork is scaled down, never up, to fit 104×90 with its proportions kept. The text column ends 6 px before it and the card ends above the footer and message line, so it never covers text or prompts.
- **Purchase:** highlighting or previewing never takes a perk. A opens the "Take this perk?" confirmation (A Yes, B No); B, LT/RT or closing the Pip-Boy leave without taking it.

## Pip-Boy controls

| Input | Action |
|---|---|
| LT / RT | Previous / next main tab (Stats, Inventory, Map, Data), one per pull. A held trigger doesn't repeat. |
| Left stick left/right | Previous / next subcategory in the tab (Status/S.P.E.C.I.A.L./Skills/Perks, All/Weapons/…, Local/Automaps/World, Quests/Holodisks/Archives/Rest) |
| Left stick up/down, D-pad | Move in the list or options; D-pad left/right jump a page in long lists |
| A | Select / confirm |
| B | Back one level; closes the Pip-Boy from a tab's own page |
| X, Y | Contextual as before (X drop / zoom / undo, Y favorite / detail / Skilldex) |
| Right stick, D-pad | Pan the maps |
| Start | Close |

- **Removed:** left-stick main tab switching, and LB/RB page switching (LB/RB now only scroll a page in text views). Keyboard Q/E still switch tabs.
- **One subcategory per push:** a push counts when the stick goes at least 60% to one side and no more than 1:2 off horizontal (about 27°). Holding doesn't repeat; the stick has to come back inside 30% first.
  - A push that starts vertically is scrolling until the stick is centered, even if it drifts sideways.
  - A diagonal push does nothing.
- **Nested controls first:**
  - The quantity selector takes left/right as its amount.
  - The action menu, perk and tag choosers, text views and a saved automap ignore sideways pushes. They don't change the subcategory or cancel.
- **Maps:** the D-pad and the right stick pan. Left stick up/down pans too, and left/right switches subcategory only with a deliberate horizontal push.
- **No combat leaks:** presses made in the Pip-Boy never reach gameplay, so LT/RT there never lock on or attack, even if still held when it closes.
- **Prompts:** `LT  RT` next to the main tabs, `LS < >` next to the subcategories; footers say "LS Page" / "LS Category" and "LT/RT Tab".

## Saves

- **Quick Pockets:** remembered steal times go into the rules block (`RTRL`, now version 2): object id, elapsed time and the world time since closing. Version 1 saves load with none.
- **Nothing duplicated:** AP costs, counterattack requests, RB throws and sweeps keep no saved state. Counterattacks owed are cleared with the map.
