# Controller menus, Pip-Boy tabs and replaced traits

## Pip-Boy tabs

Replaced: LT/RT now switch the main tabs and the left stick switches subcategories. See [close-combat-perks-pipboy.md](close-combat-perks-pipboy.md#pip-boy-controls).

## Menus: dialogue, character creation, pause menu, main menu

| Input | Action |
|---|---|
| D-pad or left stick | Move the selection to the nearest button in that direction (repeats while held) |
| A | Activate the selected button |
| B | Cancel or go back (see each screen) |
| Right stick | Free cursor, for the rare thing that isn't a button |

- **Selection:** the selected button has an amber frame. When a screen or dialog opens, the selection jumps to the nearest button. The frame shows while the controller is in use and disappears when the mouse moves.
- **No double activations:**
  - After the selection jumps, A waits until the window system has caught up (up to 3 frames), so a quick A is never lost and never lands on the old button.
  - Presses made in a menu never reach gameplay.
  - A press made in gameplay (A held while a screen opens) never clicks a button: A must be released and pressed again in the menu.
- **World and town maps:** the left stick moves the cursor there, as before; the D-pad still jumps between buttons.

### Screens

- **Main menu:**
  - **B:** selects Exit instead of quitting at once; A on Exit quits. The Escape key still quits directly.
- **Character selection:**
  - **Buttons:** Previous, Next, Take, Modify, Create.
  - **B:** goes back to the main menu.
- **Character creation:**
  - **Buttons:** SPECIAL + and −, the tag box next to each skill, the trait boxes, Name, Age, Sex, Options, Done and Cancel are all buttons.
  - **B:** asks "Discard this character and go back?" (Yes/No; B answers No). The Cancel button and the Escape key still leave at once, as before.
  - **Done:** keeps its original checks (unspent points, tag skills).
- **Text entry (name, save description):**

  | Input | Action |
  |---|---|
  | Up/Down | Change the last letter (A–Z, a–z, 0–9, space, `-`, `'`, `.`) |
  | Right | Add a letter |
  | Left or X | Delete the last letter |
  | Y | Switch the last letter's case |
  | A | Accept |
  | B | Cancel |

  A line with these controls appears under the field while a controller is connected. File-name fields skip characters that aren't allowed.
- **Dialogue:** the options are buttons. When a conversation opens, and whenever a new set of options appears, none is selected: there is no frame, and A does nothing. The D-pad or the left stick selects the first option, then moves between them, and A chooses the selected one. The A press that started the conversation, or chose the previous option, never chooses one: A must be released and pressed again. B never ends a conversation (the original has no Escape there either); the review window's Done is a button. Barter keeps its own controls.
- **Pause menu (Start):** Save, Load, Preferences, Exit and Done are buttons. B closes it.
- **Preferences:**
  - **Up/Down:** go through every setting as one list (the switches column by column, then the sliders, the player-speed box, then Default, Done and Cancel).
  - **Left/Right:** change the selected switch or slider (a tenth of a slider's range per step). On the bottom row they move between Default, Done and Cancel.
  - **A:** turns a switch to its next position.
  - **B:** cancels, restoring the old settings.
- **Save/Load:**
  - **Keys:** Up/Down pick a slot, A saves or loads it, B cancels.
  - **Save description:** the text entry above.
- **Confirmation dialogs (overwrite, quit, discard):** the Yes/No buttons are framed and selected with the D-pad, as on every other screen. B answers No.

## Replaced traits

The values come from `[realtime]` in `fallout.cfg` (defaults shown). Trait descriptions show the configured values.

| Trait | Key | Default |
|---|---|---|
| Kamikaze | `trait_kamikaze_ap` | 10 |
| | `trait_kamikaze_armor_ac_pct` | 50 |
| Fast Shot | `trait_fast_shot_rate_pct` | 125 |
| Bloody Mess | `trait_bloody_mess_crit` | 3 |
| Skilled | `trait_skilled_skill_bonus` | 10 |
| | `trait_skilled_xp_pct` | 85 |

### Kamikaze

- **AP:** +10 maximum Action Points.
  - Current AP is never topped up by this: it regenerates to the new maximum.
  - Running, AIMED SHOT reservations, regeneration and every display use the new maximum.
- **Armor Class:**
  - No natural Armor Class: the Endurance base (this project's AC formula) is 0, as with the original trait.
  - Worn armor gives half its Armor Class, rounded down, so 15 counts as 7.
  - Perks, drugs and other bonuses count in full. Damage Threshold and Resistance are untouched.
  - Example: Endurance 8, Leather Armor (AC 15), a +7 bonus: 30 without the trait, 14 with it (7 + 7).
- **Removed:** the original +5 Sequence.
- **HUD:** it shows up to 10 AP lights. AP above 10 turn lights yellow from the left: 14 AP shows 4 yellow and 6 green, 20 AP shows all yellow.

### Fast Shot

- **No aimed shots:** body-part AIMED SHOT is not available. LB explains why: "Fast Shot: no aimed shots. LT lock and RT attacks still work." Ordinary LT lock-on is unchanged.
- **Faster attacks:** every attack interval is divided by 1.25 (×100/125, rounded down to the millisecond). This covers single shots, bursts, automatic fire, throws, melee and unarmed attacks, including swings with no lock.
  - **What is shortened:** the player's attack animations, the target's hit or dodge reaction to them, and the cooldown before the next attack, all by the same factor.
  - **Events stay on their frames:** impact events are counted in frames, so they stay on their frames and damage lands once.
  - **What isn't:** death animations, movement, reloads, holstering, drawing and item use keep their speed.
- **Unchanged rules:** a press is still one attack, and only automatic weapons repeat while held.
- **Removed:** the old −1 AP cost.

### Bloody Mess

- **Kept:** the violent-death effects are unchanged.
- **Criticals:** +3 percentage points of critical chance, within the stat's 100% cap.
- **Armor destruction:** worn armor is destroyed when all of these are true:
  1. The killer is the player and has Bloody Mess.
  2. The killing attack is a critical hit (its target, not the extra targets of a burst).
  3. The death animation is a gory one, not a plain fall: big hole, charred, chunks of flesh, dancing, electrified, sliced, burned, melted, exploded or fire dance.
  4. The victim wears armor.
- **When it happens:** once, when the death animation ends, and before a death "to nothing" drops the inventory, so the armor is never dropped or duplicated.
- **What stays:** carried spare armor, other loot and a creature's natural armor (part of its stats).

### Skilled

- **Skill bonus:** Skilled's original benefit was +10% to every skill, and it is kept.
  - Like every trait modifier it is added whenever a skill is looked up, never stored, so it counts once however often the game is saved, loaded or recalculated.
  - Skilled had no other benefit in Fallout 1.
- **XP:**
  - Positive experience awards (combat, quests, skills, scripts, stealing) are ×0.85.
  - The fraction left over is kept in hundredths and added to the next award, so 1 XP awards still add up (20 × 1 XP = 17 XP).
  - Swift Learner applies first.
  - Negative adjustments and experience already earned are untouched.
  - The kill and steal messages show the points actually gained.
- **Perks:** one selection every 2 levels from level 2, as for everyone. The original delay is gone.

## Saves

- **Nothing to convert:** trait effects are computed from the current traits and the armor worn, so existing characters get the new effects without anything being stored or added twice.
- **XP fraction:** saved in a block after the favorites block (`RTRL`, version 1). Older saves load with a fraction of 0.

## Automated tests

- **Gameplay run (`--rt-autotest`):**
  - Pip-Boy tabs and subcategories: three checks (see close-combat-perks-pipboy.md).
  - Kamikaze: 20 armor, Endurance and bonus combinations, plus AP.
  - Fast Shot: rules, rates for knife swings, knife attacks and pistol shots, frame timing, one shot per press.
  - Bloody Mess: critical chance; gory critical, non-critical, minimal-violence, no-trait and creature kills.
  - Skilled: skills, XP sequence, perks at 2 and 4.
  - Trait texts, and traits after save/load.
  - Dialogue options with the controller: nothing selected when a conversation or a new reply opens, A held from the talk press or pressed without a selection chooses nothing, the D-pad selects, A chooses.
  - Enemy hit reactions: only NPC hit reactions faster, no queue on repeated hits, the target moves again once its reaction is over, a distant enemy walks on at once, kills and knockdowns unchanged.
  - Vault 15 rope (VAULTBUR.MAP): no rope, a carried rope, a double press, climbing down, a rope in the active hand, and the ropes after saving, loading and a map change.
- **Menu run (`--rt-autotest-menus`):** the controller alone goes through:
  - the main menu and character selection;
  - character creation: SPECIAL, tags, traits, the name, B's question, Done;
  - the pause menu, Preferences, and a save with its description and the overwrite question.
