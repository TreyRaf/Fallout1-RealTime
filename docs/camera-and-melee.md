# Camera, wider view, melee swings and ambient dialogue

## Controls (gameplay)

| Input | Action |
|---|---|
| Right stick, no lock | Pans the camera (analog: speed follows the deflection) |
| Right stick flick, with a lock | Switches the locked target (unchanged) |
| R3 (right-stick click) | Wider view on/off |
| RT, unarmed or melee weapon | One attack; with no target in reach it still swings |
| RT, ranged weapon | Needs a lock, as before |

A right-stick push belongs to whatever it started as: a push that began as a pan keeps panning even if a target gets locked meanwhile, and a push that began with a lock never pans. The stick has to return to the center before the next push counts.

## Camera

- **Rendering:** the map renders into an off-screen buffer larger than the play area. The play area shows part of that buffer, shifted by a pixel-precise pan and scaled by the zoom. At the default zoom with no pan, the image is the original one, pixel for pixel.
- **Right-stick panning:**
  - It starts on the first frame the stick is past its deadzone. There's no hold time, repeat delay or step timer.
  - The speed is proportional to the deflection: full deflection is `camera_pan_speed` play-area pixels per second.
  - Light smoothing (`camera_pan_smoothing_ms`) eases the start. Releasing settles 2.5 times faster, so the view stops within about 0.1 s.
  - Movement is measured in real time, so the speed doesn't depend on the frame rate.
  - Panning respects the map's scroll limits (and scroll blockers), as the original scrolling does.
- **Edge follow:** the camera stays still while the player is inside the inner box (`camera_margin_pct`).
  - When the player goes past it, the camera glides by the amount they're past it (`camera_follow_ms`), until they're back inside.
  - After a manual pan, the view stays where it was put until the player moves again.
  - A pan and the edge follow never run in the same frame.
  - A new map, or a player who ends up far off screen after a script moves them, still recenters at once.

## Wider view (R3)

- **What it does:** R3 toggles between the default view and a wider one that shows more of the map. The play area is filled at all times; the larger world buffer is rendered and scaled down, not shrunk with borders.
- **Image:** the scaling is bilinear. Palette colors that the game color-cycles (water, fire, screens) are never blended, so they keep animating.
- **Transition:** a short eased transition (`camera_zoom_transition_ms`). The player stays at the same spot on screen wherever the map's scroll limits allow it.
- **Back to default:** pressing R3 again returns to exactly zoom 1, the original image.
- **What stays the same size:** the HUD. Floating dialogue, the interaction label and the lock-on crosshair with its hit chance are drawn at their normal size where their objects appear, so they stay aligned and readable. The cutaway bubble scales with the world.
- **Pressing:** a held R3 counts once. R3 does nothing in menus, the Skilldex, the target picker or AIMED SHOT.
- **Starting amount:** 1.5 (`camera_zoom_out_pct=150`): 1.5 times as much map in each direction, 2.25 times the area. The allowed range is 110 to 200.
- **Map edges:** zoomed out, the view can extend past the original scroll limits near a map's edge, where the map is black or shows scenery the default view never reached.

## Cutaway bubble

The translucent area that shows the player behind walls and roofs is drawn at `cutaway_scale_pct` (200: twice the width and height of the original egg art), around the same center, with the art's own soft edge resampled. Only walls and scenery in front of the player are affected, as in the original.

## Melee and unarmed attacks

- **With a lock in reach:** a locked target within the attack's reach is attacked exactly as before.
- **Otherwise, the swing:** with no lock, or a lock out of reach, RT plays one attack animation toward the way the player faces.
  - **Punch or kick:** an unarmed swing is sometimes a kick, as in the original.
  - **Turning:** the player turns only to an enemy in the front arc, never toward a bystander. The player never walks toward a target.
- **The hit, at the impact frame:** at the animation's impact frame, the target is checked again: still alive, within reach (Bonus HtH Attacks included) and not behind a wall.
  - **Who can be hit:** an enemy in the front arc (the hex ahead and the two beside it), or anyone else only straight ahead. Never behind the player, and never the player's companions.
  - **The attempt:** the target gets an ordinary attack: the original to-hit roll, damage, armor and criticals. Damage is applied once, when the reactions finish.
  - **Bystanders:** attacking one makes them and their teammates hostile, as in the original.
  - **No contact:** a swing that meets nobody is just a swing.
- **Timing:** cooldowns apply as for any attack. A held RT doesn't repeat melee attacks.
- **Ranged weapons:** lock-on and firing are unchanged.

## Ambient dialogue

- **Following:** floating lines above NPCs follow the speaker as it walks: its drawn position, including the walk step in progress. They also follow the camera pan and zoom.
- **Unchanged:** duration, font, color, outline and expiry.
- **Several speakers:** each line is independent.
- **Edges:** a line stays inside the play area while its speaker is on screen.
- **Speaker gone:** a speaker that's removed, or that leaves the elevation, leaves its line where it last was until it expires.
- **Map change:** a new map clears all lines.
- **Conversations:** the dialogue screen is unchanged.

## Settings (`fallout.cfg`, `[realtime]`)

| Key | Default | Meaning |
|---|---|---|
| `camera_pan_deadzone_pct` | 20 | Right-stick deadzone (5–60) |
| `camera_pan_speed` | 1100 | Pan speed at full deflection, play-area pixels per second |
| `camera_pan_smoothing_ms` | 60 | Pan smoothing time constant (0 = none) |
| `camera_follow_ms` | 110 | Edge-follow glide time constant |
| `camera_margin_pct` | 25 | Inner box margin (unchanged) |
| `camera_zoom_out_pct` | 150 | Wider view amount, percent (110–200) |
| `camera_zoom_transition_ms` | 200 | Zoom transition length |
| `cutaway_scale_pct` | 200 | Cutaway size, percent of the original art (100–400) |
