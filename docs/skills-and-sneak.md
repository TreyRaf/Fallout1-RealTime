# Skilldex, Sneak and contextual skills

## Controls (gameplay)

| Button | Action |
|---|---|
| View (Back) | Open the Skilldex |
| R3 (right-stick click) | Toggle Sneak |
| Right-stick flick | Switch the locked target (unchanged) |
| Y | Cycle the attack mode: single/burst, punch/kick, swing/thrust (moved from View) |
| A | Contextual action on the highlighted object, within 1 hex |

These presses only work in gameplay. In menus, dialogue, the Skilldex, the target picker and AIMED SHOT, the controller drives the menu, and R3 does nothing.

## Skilldex

- **The list:** this is the original Skilldex window. Its numbers are your skill levels.
  - D-pad up/down moves a highlight frame; the last row is CANCEL.
  - A uses the highlighted skill; B closes the window.
- **Target picker:** a skill that needs a target opens a controller picker instead of the mouse crosshair.
  - Candidates are within 8 hexes, on your level, not hidden and not behind walls. They are outlined one at a time.
  - D-pad or a right-stick flick picks the next target, A uses the skill, B returns to the list.
  - Gameplay is paused throughout.
- **Picker text:** the top line shows the skill and the target. The second line keeps your skill percentage apart from any chance on that target:
  - **First Aid / Doctor:** "Chance here N%" is the original roll, d100 against skill + critical chance. Doctor shows it per check. Robots show 0%, and an uninjured target shows "Not injured" for First Aid. Uses left today shows the original 3-per-day limit. A target's own script can still take over the skill use, as in the original.
  - **Steal:** the chance is per item, shown in the steal screen.
  - **Lockpick, Traps, Science, Repair:** Fallout 1 runs these only in object scripts. The text says whether the script rolls against your skill (its modifiers aren't readable, so no percentage is invented) or needs a fixed level.
- **What each skill can target:**
  - First Aid and Doctor: yourself (listed first) and living critters.
  - Steal: living critters not fighting you.
  - Lockpick, Traps, Science, Repair: objects whose script handles the skill.
- **Not during a fight:** First Aid, Doctor and Steal are refused while a hostile is engaged with you ("You cannot do that in combat."), as in the original.
- **From the Pip-Boy:** on the Skills page, Y closes the Pip-Boy and opens the Skilldex.

## Sneak

- **Toggling:** R3, or Sneak in the Skilldex, uses the original Sneak toggle, including its detection checks. Sneaking never guarantees you go unseen.
- **Animation:** Fallout 1 has no sneaking animation type (its 65 animation types include none), so the normal walk is kept.
- **Running:** running ends sneaking unless you have Silent Running, as in the original ("You stop sneaking to run."). Turning Sneak on also turns the run toggle off.
- **HUD:** the original SNEAK box appears above the interface bar, next to the LEVEL box when a level up is pending; the game draws both side by side. The level-up state is never touched.
- **Persistence:** the sneak state is saved with the character, and the box comes back after menus and loading.

## Contextual actions (A, within 1 hex)

- **Steal:** while sneaking, A on a living critter that isn't fighting you is **Steal**, labelled before you press it ("Name: A Steal").
  - A runs the original steal flow and opens the steal screen.
  - For the highlighted item, the screen shows the chance it goes unnoticed (take or plant), next to your Steal skill. The chance is computed from the original steal and catch rolls, including criticals from day 2, without rolling.
  - The original per-item checks, detection and owner reactions apply. Nothing is taken by sneaking up to or highlighting someone.
  - Outside Sneak, A keeps its usual action (Talk, Loot and so on).
- **Disarm trap:**
  - Objects whose script handles Traps offer **Disarm trap**. In Fallout 1 that is 7 scripts: the vault laser, plasma, boom and alarm traps, and a trapped locker, box and door. All of them disarm.
  - Hidden (undetected) objects are never offered.
  - The object's script makes its own roll and applies its own consequences.
- **Arming:** this exists only for dynamite and plastic explosives, from the inventory. In the Pip-Boy their "Use" action reads **Arm (set timer)**.
  - The timer window works with the controller: D-pad or LT/RT change the time, A sets it, B cancels.
  - The original Traps roll decides whether the explosive goes off early.
- **Repair:** objects whose script handles Repair offer **Repair**: field generators, force-field emitters, the vault computer, robot controls and so on.
  - A runs the script's check, and fixed requirements the script checks are enforced before any attempt.
  - No durability system is added, and plain scenery isn't made repairable.
- **Unchanged:**
  - **Lockpick:** works as before.
  - **First Aid and Doctor:** never on A, only through the Skilldex. Healing items still work through the inventory and favorites.
- **Several actions:** when more than one action applies to an object, the small action menu appears. A runs exactly one action.
