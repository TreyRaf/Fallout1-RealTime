#include "game/rt_autotest.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include <SDL.h>

#include "game/anim.h"
#include "game/art.h"
#include "game/combat.h"
#include "game/combatai.h"
#include "game/critter.h"
#include "game/game.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/party.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/realtime.h"
#include "game/editor.h"
#include "game/game_vars.h"
#include "game/perk.h"
#include "game/pipboy.h"
#include "game/rt_aimed.h"
#include "game/rt_context.h"
#include "game/rt_favorites.h"
#include "game/rt_pipboy.h"
#include "game/rt_rules.h"
#include "game/worldmap.h"
#include "game/map_defs.h"
#include "int/support/intextra.h"
#include "game/scripts.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/tile.h"
#include "game/trait.h"
#include "platform_compat.h"
#include "plib/gnw/gamepad.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/svga.h"

namespace fallout {

#define AT_TOTAL_TIMEOUT 1500000

// -----------------------------------------------------------------------------
// Checks
// -----------------------------------------------------------------------------

typedef enum AutotestCheck {
    C_MAP,
    C_WALK,
    C_STOP,
    C_MOVE_REQUESTS,
    C_RUN,
    C_REGEN,
    C_EXHAUST,
    C_RUN_RECLICK,
    C_RUN_ZERO,
    C_RUN_NEUTRAL,
    C_RUN_COMBAT_START,
    C_RUN_COMBAT_END,
    C_INVENTORY_B,
    C_SCREENS,
    C_Y_LB,
    C_MENU_NAV,
    C_MENU_TRIGGERS,
    C_MENU_A,
    C_PAUSE,
    C_LOCK,
    C_INDICATOR,
    C_LOCK_PERSIST,
    C_UNLOCK,
    C_LOCK_DEATH,
    C_LOCK_NOT_FRIENDLY,
    C_SWITCH,
    C_SWITCH_KEEP,
    C_SWITCH_NO_CAMERA,
    C_LOCK_NEUTRAL,
    C_LOCK_NO_AGGRO,
    C_NO_LOCK_RT,
    C_HIT_UNARMED,
    C_HIT_MELEE,
    C_HIT_GUN,
    C_HIT_THROW,
    C_HIT_RANGE,
    C_HIT_BLOCKED,
    C_UNARMED,
    C_MELEE,
    C_SINGLE,
    C_BURST,
    C_AUTO,
    C_TAP_HOLD,
    C_THROW,
    C_NO_TRANSFER,
    C_EMPTY,
    C_RELOAD_ANIM,
    C_RELOAD_AMMO,
    C_RELOAD_BLOCKS_FIRE,
    C_RELOAD_INTERRUPT,
    C_RANGE,
    C_BLOCKED,
    C_SLOT_ONE,
    C_SLOT_FAIR,
    C_SLOT_FREE,
    C_DAMAGE_ONCE,
    C_ENEMY_AI,
    C_COMPANION,
    C_SIMULTANEOUS,
    C_INTERACT,
    C_SKILL_SUCCESS,
    C_SKILL_ROLL_FAIL,
    C_SKILL_REQUIRED,
    C_SKILL_MENU,
    C_CAMERA_STILL,
    C_CAMERA_EDGES,
    C_CAMERA_STEPS,
    C_CAMERA_PAN,
    C_CAMERA_RETURN,
    C_NO_HEAL_ON_A,
    C_CHEM,
    C_DISCONNECT,
    C_FOCUS,
    C_SAVE,
    C_LOAD,
    C_MAP_CHANGE,
    C_NO_TURN_BASED,
    C_PB_PAUSE,
    C_PB_NO_LEAK,
    C_PB_TABS,
    C_PB_REOPEN,
    C_PB_EQUIP,
    C_PB_USE,
    C_PB_DROP,
    C_PB_SKILL,
    C_PB_PERK,
    C_PB_MAP,
    C_PB_DATA,
    C_PB_SHORTCUTS,
    C_PB_PERSIST,
    C_PB_MAP_SAVED,
    C_AIM_NO_TARGET,
    C_AIM_ENTRY_LOCK,
    C_AIM_ENTRY_NEAREST,
    C_AIM_ANATOMY,
    C_AIM_QUEUE,
    C_AIM_EXECUTE,
    C_AIM_AP,
    C_AIM_ODDS,
    C_AIM_CANCEL,
    C_AIM_LIMITS,
    C_AIM_BLOCKED,
    C_AIM_UNSUPPORTED,
    C_AIM_SKIP_DEAD,
    C_AIM_INTERRUPT,
    C_MODAL_INTERRUPT,
    C_FAV_ASSIGN,
    C_FAV_USE,
    C_FAV_DEPLETED,
    C_FAV_NO_LEAK,
    C_FAV_PERSIST,
    C_INT_CYCLE,
    C_INT_NO_COMBAT,
    C_INT_CONTEXT,
    C_INT_ACTIVATE,
    C_INT_BARRIER,
    C_INT_LABEL,
    C_FAV_MIGRATE,
    C_LOOT_FOCUS,
    C_LOOT_NAV,
    C_LOOT_TRANSFER,
    C_LOOT_QUANTITY,
    C_LOOT_TAKE_ALL,
    C_LOOT_EMPTY,
    C_LOOT_SAFETY,
    C_LOOT_BIG_STACK,
    C_BARTER_NAV,
    C_BARTER_OFFER,
    C_BARTER_REMOVE,
    C_BARTER_TRADE,
    C_BARTER_CANCEL,
    C_BARTER_SAFETY,
    C_HOLSTER_TAP,
    C_HOLSTER_HOLD,
    C_HOLSTER_UNARMED,
    C_HOLSTER_REDRAW,
    C_HOLSTER_CONTEXT,
    C_LEVEL_ICON,
    C_RULE_SPECIAL_AG,
    C_RULE_SPECIAL_ST,
    C_RULE_SPECIAL_IN,
    C_RULE_SPECIAL_EN,
    C_RULE_CHA_LIMIT,
    C_RULE_PERK_CADENCE,
    C_RULE_PERK_TABLE,
    C_RULE_PERK_CONVERT,
    C_RULE_PERK_PERSIST,
    C_RULE_CHA_RECRUIT,
    C_RULE_REACH,
    C_RULE_REACH_BLOCKED,
    C_RULE_REACH_ATTACK,
    C_RULE_LONG_RANGE,
    C_RULE_LONG_RANGE_SHOWN,
    C_RULE_FRIENDLY_FOE,
    C_RULE_SPRINT,
    C_COUNT,
} AutotestCheck;

static const char* at_check_names[C_COUNT] = {
    "Opening cave map loaded",
    "Left stick walks with the walk animation",
    "Releasing the stick stops movement",
    "Straight walking issues few movement requests (no queue/restarts)",
    "L3 toggles running with the run animation; outside combat running costs no AP",
    "AP regenerates while not running",
    "AP exhaustion in combat returns to walking and clears the toggle",
    "Running again requires a new L3 click",
    "Outside combat a run starts and continues at 0 AP while AP regenerates",
    "Locking onto a non-hostile critter is not combat: running stays free",
    "Combat starting during a run drains AP until it is out",
    "Combat ending during a run stops the AP drain at once",
    "B opens the Pip-Boy, it stays open, B closes it",
    "D-pad with no favorites assigned opens nothing; R3 opens the Skilldex",
    "Y does nothing in gameplay; LB never changes the lock",
    "Menu: D-pad moves the cursor between buttons",
    "Menu: LT/RT do nothing (no lock, no attack)",
    "Menu: A activates the selected button",
    "Simulation pauses while a menu is open",
    "LT locks the nearest enemy",
    "Crosshair indicator with hit chance shown on the locked target",
    "Lock persists after release and while moving",
    "LT again unlocks (indicator hidden)",
    "Lock clears when the target dies",
    "Lock never selects a companion",
    "Right-stick flick switches once to a target in that direction",
    "Flick with no target in that direction keeps the lock",
    "Target switching doesn't move the camera",
    "With no enemy nearby, LT locks the nearest neutral NPC",
    "Locking a neutral NPC causes no attack or aggression",
    "RT without a lock attacks nothing (hostile adjacent, after unlock)",
    "Indicator = vanilla hit chance: unarmed",
    "Indicator = vanilla hit chance: melee",
    "Indicator = vanilla hit chance: guns (single and burst)",
    "Indicator = vanilla hit chance: throwing",
    "Indicator shows 'Out of range' instead of a percentage",
    "Indicator shows 'Blocked' instead of a percentage",
    "Unarmed: RT press = one attack, hold doesn't repeat",
    "Melee: RT press = one attack, hold doesn't repeat",
    "Single fire: RT press = one shot (1 round), hold doesn't repeat",
    "Burst: RT press = one full burst",
    "Automatic: holding RT repeats bursts at a limited rate, stops on release",
    "A press turning into a short hold adds no extra burst",
    "Throwing: RT press = one throw, hold doesn't repeat",
    "Held automatic fire doesn't move to another target when the lock is lost",
    "Empty weapon: RT fails safely",
    "X reload plays the holster and draw animations; weapon stays equipped",
    "Reload transfers ammunition exactly once",
    "No firing until the reload completes",
    "Interrupted reload loses or duplicates no ammunition",
    "Melee reach not bypassed; no auto-walk",
    "Blocked line of fire is not bypassed",
    "At most one committed enemy attack on the player at a time",
    "Several enemies take turns attacking the player (no starvation)",
    "Player and waiting enemies keep acting (no freeze, no turns)",
    "Damage applied exactly once per hit",
    "Enemies attack on their own",
    "Companion attacks hostiles on its own",
    "Player, enemy and companion attacks overlap in time",
    "A interacts (loot) and the UI closes with B",
    "A: skill action succeeds through the object's own script",
    "A: a failed skill roll is possible (no guaranteed success)",
    "A: unmet fixed skill requirement is reported, not attempted",
    "A: several applicable actions give a controller menu (A picks, B cancels)",
    "Camera stays still while the player moves inside the frame",
    "Camera scrolls toward each edge (left, right, up, down)",
    "Camera scrolls in small steps (no jumps or recentering)",
    "No lock: right stick pans the camera, even past the player",
    "After panning the view stays; walking brings it back in steps",
    "A never offers First Aid or Doctor",
    "RT with a chem in the active slot uses one on the player (hold doesn't repeat)",
    "Controller disconnect stops input; no phantom actions on reconnect",
    "Focus loss pauses and clears held input",
    "Save game with the controller (pause menu)",
    "Load game with the controller; clean state after load",
    "Map transition leaves no stuck combat/input state",
    "Turn-based combat never started",
    "Pip-Boy: game paused while open",
    "Pip-Boy: opened while walking, A/RT inside; closing leaks no attack, interaction, movement or reopen",
    "Pip-Boy: RT/LT switch tabs, RB/LB switch pages",
    "Pip-Boy: B reopens it on the last tab",
    "Pip-Boy: hold, put away and hold again a weapon (hand state changes)",
    "Pip-Boy: using a Stimpak heals and consumes it",
    "Pip-Boy: quantity picker drops part of a stack; Inspect (A menu) shows the text",
    "Pip-Boy: skill point spent, undone (X) and spent again",
    "Pip-Boy: perk chosen when one is due",
    "Pip-Boy: live automap with player marker; world map matches the revealed cells",
    "Pip-Boy: quests and holodisks from the game variables, readable",
    "Pip-Boy: i, c, Tab, p, z (keys and HUD buttons) open the Pip-Boy on their tab",
    "Pip-Boy: equipment, skills and perks survive save/load",
    "Pip-Boy: after a map change, its saved automap opens; the world map shows revealed land",
    "AIMED SHOT: LB with no enemy gives feedback and stays in gameplay",
    "AIMED SHOT: LB opens on the LT-locked target; the game is paused; B closes",
    "AIMED SHOT: without a lock, LB picks the nearest enemy, never a neutral NPC",
    "AIMED SHOT: body parts come from each creature's own anatomy (rat vs human)",
    "AIMED SHOT: D-pad parts, A queues (same part twice), X undoes, flick switches target; reserved AP shown",
    "AIMED SHOT: RT executes the queue in order on the planned targets and parts; controls blocked, then restored",
    "AIMED SHOT: AP reserved while planning, spent once per committed attack, no regeneration during execution",
    "AIMED SHOT: shown hit chance = resolution; body-part criticals from the original tables; no guaranteed hits/crits",
    "AIMED SHOT: B and LB cancel planning without spending AP",
    "AIMED SHOT: not enough AP or ammunition refuses another attack",
    "AIMED SHOT: blocked or out-of-range targets can't be queued",
    "AIMED SHOT: burst mode is refused with an explanation (no invented support)",
    "AIMED SHOT: a planned target that died is skipped and its AP released",
    "AIMED SHOT: B cancels the rest; knockdown and map reset end the queue; no AP stays reserved",
    "Controller disconnect / focus loss inside AIMED SHOT and the loot screen leave no stuck or phantom input",
    "Favorites: Y in the Pip-Boy assigns a direction (stick or D-pad, A), replaces, clears (X); B returns to the same item",
    "Favorites: a D-pad press uses a Stimpak / readies a weapon at once (no menu); holding uses one; empty direction does nothing",
    "Favorites: a depleted item fails safely with feedback (no menu, no other item) and works again when restocked",
    "Favorites: direct D-pad use leaks no movement, interaction or attack",
    "Favorites survive save/load",
    "Interaction: left/right step only to objects within 2 hexes (map distance) of the highlighted one (1, 2, 3 hexes apart)",
    "Interaction cycling doesn't change the lock or cause aggression",
    "D-pad: left/right cycle only when another object is within 2 hexes, otherwise favorites; one action per press",
    "A acts only within 1 hex: picks up the highlighted item; from 2+ hexes it refuses without moving",
    "Objects behind a wall are not offered for interaction",
    "Interaction label: name and action only within 1 hex; no D-pad helper text",
    "Favorites: eight-slot saves migrate to four directions without overwriting (moves and drops reported)",
    "Loot: opens focused on the loot side, first item highlighted",
    "Loot: LT/RT switch sides; up/down move and scroll a long list; each side keeps its selection",
    "Loot: A moves a single item at once, both ways (take and put in)",
    "Loot: a stack asks how many (D-pad changes it, A confirms); B cancels and moves nothing",
    "Loot: X takes all from the loot side (whichever side is focused), never deposits; what is too heavy stays",
    "Loot: an empty container is safe (A, X, D-pad), B closes",
    "Loot: no item duplicated or lost; equipped items not offered; no input leaks into gameplay",
    "Loot: a 50,000-cap stack moves in one go (Y = all); RT/LT change the amount by 100, held they repeat",
    "Barter: opens on the trader's items; LT/RT and LB/RB switch lists, each keeps its selection; up/down move",
    "Barter: A puts an item (stacks: quantity) on the offer without changing its owner",
    "Barter: A on an offer takes items back (quantity; B cancels)",
    "Barter: totals and acceptance shown; X refuses a poor offer (nothing moves) and makes an accepted trade",
    "Barter: leaving returns every offered item to its owner",
    "Barter: no item or cap duplicated or lost; no input leaks into gameplay",
    "X tapped reloads once (no holster)",
    "X held holsters the weapon once with the put-away animation; no reload before or on release",
    "After holstering: weapon kept with its ammo and favorite; RT attacks unarmed; X held unarmed does nothing",
    "Readying the weapon again from favorites plays the draw animation",
    "X keeps its menu meanings (loot Take All, AIMED SHOT undo); an interrupted hold does nothing",
    "Level-up icon: on after a level up, stays while a perk is pending, clears when done; persists across save/load",
    "Agility changes only AP, Small Guns, Big Guns, Lockpick, Steal and Traps",
    "Strength changes Melee Weapons, Unarmed and Throwing (Strength in full), Carry Weight, Melee Damage",
    "Intelligence changes Energy Weapons (and its original skills)",
    "Endurance gives Armor Class (armor and perk bonuses added on top)",
    "Charisma: companion limit floor(CH / 2), at most 5; Speech and Barter unchanged",
    "Perks: one selection at levels 2, 4, 6, 8 (none at odd levels), levels at once add up, no duplicates when processed again or after spending",
    "Perks: new minimum levels and ranks, no level-3 perks left, eligibility follows level, changed descriptions",
    "Perks: saves from the old cadence convert without lost or duplicate selections",
    "Perks: pending selections and processed levels survive save/load",
    "Charisma: party refuses companions over the limit, keeps existing ones when CH drops, ignores temporary followers, undoes the script join",
    "Bonus HtH Attacks: +1.5 hexes reach (hex-center distance) for player melee/unarmed, 2-hex weapons reach part of ring 4, not for others",
    "Bonus HtH Attacks: the longer reach is blocked by walls",
    "Bonus HtH Attacks: indicator, AIMED SHOT eligibility and the attack itself at 2 hexes",
    "Bonus Rate of Fire / Heave Ho!: smaller long-range penalty for guns, energy weapons, throwing; close range and throw range unchanged",
    "Long-range hit chance shown = the hit chance the attack rolls",
    "Friendly Foe: companion bursts and strays never hit the player; hostile fire still does",
    "Bonus Move: sprint drains 50% less in combat; no drain outside combat either way",
};

static int at_results[C_COUNT];
static char at_notes[C_COUNT][240];

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

typedef bool(AtStepFunc)(unsigned int elapsed, bool first, bool timedOut);

typedef struct AtStep {
    const char* name;
    AtStepFunc* func;
    unsigned int timeout;
} AtStep;

typedef bool(AtMenuScript)(unsigned int elapsed);

static void at_hook();
static void at_log(const char* format, ...);
static void at_check(AutotestCheck check, bool passed, const char* format, ...);
static void at_press(GamepadButton button);
static void at_release_all();
static void at_screenshot(const char* name);
static int at_screendump(int width, int height, unsigned char* data, unsigned char* palette);
static void at_expect_menu(const char* screenshot, AtMenuScript* script);
static void at_finish();
static void at_clear_hostiles();
static void at_record_save_state();
static bool at_aim_released();
static int at_clear_rats_near(int radius);
static void at_record_level_state();

static bool at_enabled = false;
// Optional: only run steps whose name contains this text (development aid).
static char at_only[64] = "";

// Perk state at save time (character rules).
static int at_rule_saved_perks = -1;
static int at_rule_saved_last_level = -1;
static bool at_finished = false;
static FILE* at_stream = NULL;
static bool at_screendump_registered = false;
static char at_shot_path[COMPAT_MAX_PATH];
static unsigned int at_start_time = 0;
static unsigned int at_last_select_key = 0;
static bool at_turn_based_seen = false;

static GamepadState at_pad;
static int at_press_frames[GAMEPAD_BUTTON_COUNT];
static int at_press_gap[GAMEPAD_BUTTON_COUNT];

static int at_step_index = 0;
static unsigned int at_step_start = 0;
static bool at_step_first = true;

// Menu expectations.
static bool at_menu_expected = false;
static bool at_menu_seen = false;
static bool at_menu_done = false;
static unsigned int at_menu_seen_at = 0;
static bool at_menu_shot = false;
static const char* at_menu_shot_name = NULL;
static AtMenuScript* at_menu_script = NULL;

// Focus test.
static bool at_focus_lost = false;
static unsigned int at_focus_lost_at = 0;
static bool at_focus_regained = false;
static int at_focus_polls = 0;

// Shared scratch values between steps.
static int at_tile_a;
static int at_tile_b;
static float at_ap_a;
static float at_ap_b;
static bool at_flag_a;
static bool at_flag_b;
static int at_count_a;
static int at_count_b;
static unsigned int at_time_a;
static Object* at_obj_a;
static Object* at_companion = NULL;
static Object* at_burst_weapon = NULL;
static Object* at_throw_weapon = NULL;
static int at_throw_pid = -1;
static Object* at_lock_watch = NULL;
static int at_mouse_x[4];
static int at_mouse_y[4];
static unsigned int at_pause_sim_start;
static unsigned int at_pause_real_start;
static int at_rat_pid = -1;
static int at_rat_team = -1;
static int at_phase;
static int at_retries;
static Object* at_target;

// -----------------------------------------------------------------------------
// Framework
// -----------------------------------------------------------------------------

void rt_autotest_parse_args(int argc, char** argv)
{
    for (int index = 1; index < argc; index++) {
        if (SDL_strcasecmp(argv[index], "--rt-autotest") == 0) {
            at_enabled = true;
        } else if (SDL_strncasecmp(argv[index], "--rt-autotest-only=", 19) == 0) {
            at_enabled = true;
            SDL_strlcpy(at_only, argv[index] + 19, sizeof(at_only));
        }
    }

    if (!at_enabled) {
        return;
    }

    for (int index = 0; index < C_COUNT; index++) {
        at_results[index] = -1;
        at_notes[index][0] = 0;
    }

    memset(&at_pad, 0, sizeof(at_pad));
    at_pad.connected = true;
    memset(at_press_frames, 0, sizeof(at_press_frames));

    gamepad_set_hook(at_hook);
}

bool rt_autotest_enabled()
{
    return at_enabled;
}

bool rt_autotest_finished()
{
    return at_finished;
}

static void at_log(const char* format, ...)
{
    if (at_stream == NULL) {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%sautotest.log", rt_get_log_dir());
        at_stream = compat_fopen(path, "wt");
        if (at_stream == NULL) {
            return;
        }
    }

    char buffer[700];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    fprintf(at_stream, "[%7u] %s\n", at_start_time != 0 ? SDL_GetTicks() - at_start_time : 0, buffer);
    fflush(at_stream);

    // Mirror into the realtime log so both timelines can be correlated.
    rt_log("autotest: %s", buffer);
}

static void at_check(AutotestCheck check, bool passed, const char* format, ...)
{
    // A failure is final; a check repeated in another situation can only
    // turn a pass into a failure.
    if (at_results[check] == 0) {
        return;
    }

    at_results[check] = passed ? 1 : 0;

    va_list args;
    va_start(args, format);
    vsnprintf(at_notes[check], sizeof(at_notes[check]), format, args);
    va_end(args);

    at_log("CHECK %s: %s -- %s", passed ? "PASS" : "FAIL", at_check_names[check], at_notes[check]);
}

static void at_press(GamepadButton button)
{
    // Held for a few polls so the press is seen regardless of frame timing.
    // A button still held from the previous press is let go first, so this
    // is a new press.
    if (at_press_frames[button] > 0 && at_press_gap[button] == 0) {
        at_press_frames[button] = 0;
        at_press_gap[button] = 2;
    } else if (at_press_gap[button] == 0) {
        at_press_frames[button] = 3;
    }
}

static void at_release_all()
{
    bool connected = at_pad.connected;
    memset(&at_pad, 0, sizeof(at_pad));
    at_pad.connected = connected;
    memset(at_press_frames, 0, sizeof(at_press_frames));
    memset(at_press_gap, 0, sizeof(at_press_gap));
}

static void at_expect_menu(const char* screenshot, AtMenuScript* script)
{
    at_menu_expected = true;
    at_menu_seen = false;
    at_menu_done = false;
    at_menu_shot = false;
    at_menu_shot_name = screenshot;
    at_menu_script = script;
}

// Default menu script: screenshot, then close with B.
static bool at_menu_close_with_b(unsigned int elapsed)
{
    if (elapsed > 1500) {
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }
    return false;
}

// Runs at the start of every gamepad poll, in menus and in gameplay.
static void at_hook()
{
    if (!at_enabled) {
        return;
    }

    if (!at_screendump_registered) {
        register_screendump(KEY_F12, at_screendump);
        at_screendump_registered = true;
    }

    unsigned int now = SDL_GetTicks();

    // Focus test: this runs inside `GNW95_lost_focus` while unfocused.
    if (at_focus_lost && !at_focus_regained) {
        at_focus_polls++;
        if (now - at_focus_lost_at > 1500) {
            at_focus_regained = true;
            SDL_Event event;
            memset(&event, 0, sizeof(event));
            event.type = SDL_WINDOWEVENT;
            event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
            SDL_PushEvent(&event);
        }
    }

    if (!gamepad_is_gameplay_mode()) {
        if (at_start_time == 0) {
            // Character selection: take the first premade character.
            if (now - at_last_select_key > 700) {
                at_last_select_key = now;
                GNW_add_input_buffer(KEY_LOWERCASE_T);
            }
        } else if (at_menu_expected && !at_menu_done) {
            if (!at_menu_seen) {
                at_menu_seen = true;
                at_menu_seen_at = now;
                at_log("menu: modal UI detected");
            } else {
                unsigned int inMenu = now - at_menu_seen_at;
                if (!at_menu_shot && at_menu_shot_name != NULL && inMenu > 900) {
                    at_menu_shot = true;
                    at_screenshot(at_menu_shot_name);
                }

                AtMenuScript* script = at_menu_script != NULL ? at_menu_script : at_menu_close_with_b;
                if (script(inMenu)) {
                    at_menu_done = true;
                }
            }
        }
    }

    GamepadState state = at_pad;
    for (int button = 0; button < GAMEPAD_BUTTON_COUNT; button++) {
        if (at_press_gap[button] > 0) {
            // Released for a moment before a repeated press.
            at_press_gap[button]--;
            if (at_press_gap[button] == 0) {
                at_press_frames[button] = 3;
            }
        } else if (at_press_frames[button] > 0) {
            state.buttons[button] = true;
            at_press_frames[button]--;
        }
    }
    state.leftTrigger = state.buttons[GAMEPAD_BUTTON_LEFT_TRIGGER] ? 1.0f : 0.0f;
    state.rightTrigger = state.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] ? 1.0f : 0.0f;

    gamepad_set_override(&state);
}

static int at_screendump(int width, int height, unsigned char* data, unsigned char* palette)
{
    FILE* stream = compat_fopen(at_shot_path, "wb");
    if (stream == NULL) {
        return -1;
    }

    int rowSize = (width + 3) & ~3;
    unsigned int fileSize = 14 + 40 + 1024 + rowSize * height;

    unsigned char header[54];
    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    memcpy(header + 2, &fileSize, 4);
    unsigned int offset = 14 + 40 + 1024;
    memcpy(header + 10, &offset, 4);
    unsigned int infoSize = 40;
    memcpy(header + 14, &infoSize, 4);
    memcpy(header + 18, &width, 4);
    memcpy(header + 22, &height, 4);
    unsigned short planes = 1;
    unsigned short bits = 8;
    memcpy(header + 26, &planes, 2);
    memcpy(header + 28, &bits, 2);
    fwrite(header, 1, sizeof(header), stream);

    for (int index = 0; index < 256; index++) {
        unsigned char quad[4];
        quad[0] = palette[index * 3 + 2] << 2;
        quad[1] = palette[index * 3 + 1] << 2;
        quad[2] = palette[index * 3] << 2;
        quad[3] = 0;
        fwrite(quad, 1, 4, stream);
    }

    unsigned char padding[3] = { 0, 0, 0 };
    for (int y = height - 1; y >= 0; y--) {
        fwrite(data + y * width, 1, width, stream);
        fwrite(padding, 1, rowSize - width, stream);
    }

    fclose(stream);
    return 0;
}

static void at_screenshot(const char* name)
{
    snprintf(at_shot_path, sizeof(at_shot_path), "%sautotest_%s.bmp", rt_get_log_dir(), name);
    dump_screen();
    at_log("screenshot: %s", at_shot_path);
}

// -----------------------------------------------------------------------------
// Harness helpers (test fixtures, not player abilities)
// -----------------------------------------------------------------------------

static void at_heal()
{
    int missing = stat_level(obj_dude, STAT_MAXIMUM_HIT_POINTS) - critter_get_hits(obj_dude);
    if (missing > 0) {
        critter_adjust_hits(obj_dude, missing);
        intface_update_hit_points(false);
    }
}

static void at_rotation_stick(int rotation, float* x, float* y)
{
    int neighbor = tile_num_in_direction(obj_dude->tile, rotation, 1);
    int x1;
    int y1;
    int x2;
    int y2;
    tile_coord(obj_dude->tile, &x1, &y1, obj_dude->elevation);
    tile_coord(neighbor, &x2, &y2, obj_dude->elevation);
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float length = sqrtf(dx * dx + dy * dy);
    *x = length > 0.0f ? dx / length : 1.0f;
    *y = length > 0.0f ? dy / length : 0.0f;
}

static int at_open_rotation(int* freeTiles)
{
    int best = 0;
    int bestFree = -1;
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        int tile = obj_dude->tile;
        int count = 0;
        for (int step = 0; step < 8; step++) {
            int next = tile_num_in_direction(tile, rotation, 1);
            // At the map edge the neighbor is the tile itself: no way there.
            if (next == -1 || next == tile || obj_blocking_at(obj_dude, next, obj_dude->elevation) != NULL) {
                break;
            }
            tile = next;
            count++;
        }

        if (count > bestFree) {
            bestFree = count;
            best = rotation;
        }
    }

    *freeTiles = bestFree;
    return best;
}

// Points the stick along the most open direction, re-evaluated only when the
// current one gets blocked (so the direction stays stable while walking).
static void at_stick_open_direction()
{
    static int rotation = -1;
    int next = rotation != -1 ? tile_num_in_direction(obj_dude->tile, rotation, 1) : -1;
    if (rotation == -1 || next == -1 || next == obj_dude->tile || obj_blocking_at(obj_dude, next, obj_dude->elevation) != NULL) {
        int freeTiles;
        rotation = at_open_rotation(&freeTiles);
    }
    at_rotation_stick(rotation, &(at_pad.leftX), &(at_pad.leftY));
}

static void at_stick_release()
{
    at_pad.leftX = 0.0f;
    at_pad.leftY = 0.0f;
}

typedef bool(AtCritterFilter)(Object* critter);

static Object* at_nearest(AtCritterFilter* filter, int maxDistance)
{
    Object* best = NULL;
    int bestDistance = maxDistance + 1;

    Object** list;
    int count = obj_create_list(-1, obj_dude->elevation, OBJ_TYPE_CRITTER, &list);
    for (int index = 0; index < count; index++) {
        Object* critter = list[index];
        if (critter == obj_dude || critter_is_dead(critter) || !filter(critter)) {
            continue;
        }

        int distance = obj_dist(obj_dude, critter);
        if (distance < bestDistance) {
            best = critter;
            bestDistance = distance;
        }
    }

    if (count > 0) {
        obj_delete_list(list);
    }

    return best;
}

static bool at_filter_hostile(Object* critter)
{
    return rt_is_hostile(critter);
}

static bool at_filter_enemy_team(Object* critter)
{
    return critter->data.critter.combat.team != obj_dude->data.critter.combat.team;
}

static bool at_screen_pos(Object* obj, int* x, int* y)
{
    if (tile_coord(obj->tile, x, y, obj->elevation) != 0) {
        return false;
    }
    *x += 16;
    *y += 8;
    int width = scr_size.lrx - scr_size.ulx + 1;
    int height = scr_size.lry - scr_size.uly + 1 - INTERFACE_BAR_HEIGHT;
    return *x >= 0 && *x < width && *y >= 0 && *y < height;
}

// Same rules as the lock: alive, not on the player's team, close, on screen.
static bool at_filter_lock_candidate(Object* critter)
{
    if (critter->data.critter.combat.team == obj_dude->data.critter.combat.team || isPartyMember(critter)) {
        return false;
    }
    if ((critter->flags & OBJECT_HIDDEN) != 0 || obj_dist(obj_dude, critter) > 25) {
        return false;
    }
    int x;
    int y;
    return at_screen_pos(critter, &x, &y);
}

static int at_min_hostile_distance()
{
    Object* nearest = at_nearest(at_filter_hostile, 25);
    return nearest != NULL ? obj_dist(obj_dude, nearest) : -1;
}

// Harness pathfinding stands in for the player steering around rocks.
static void at_approach(Object* target, int distance)
{
    if (anim_busy(obj_dude) || obj_dist(obj_dude, target) <= distance) {
        return;
    }

    register_begin(ANIMATION_REQUEST_RESERVED);
    register_object_move_to_object(obj_dude, target, -1, 0);
    register_end();
}

static Object* at_find_inventory(const char* name)
{
    Inventory* inventory = &(obj_dude->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        Object* item = inventory->items[index].item;
        if (SDL_strcasecmp(object_name(item), name) == 0) {
            return item;
        }
    }
    return NULL;
}

static bool at_unarmed()
{
    if (inven_left_hand(obj_dude) != NULL) {
        inven_unwield(obj_dude, 0);
    }
    if (inven_right_hand(obj_dude) != NULL) {
        inven_unwield(obj_dude, 1);
    }
    intface_update_items(false);
    return inven_left_hand(obj_dude) == NULL && inven_right_hand(obj_dude) == NULL;
}

static bool at_wield(Object* item)
{
    if (item == NULL) {
        return false;
    }

    Object* current = NULL;
    intface_get_current_item(&current);
    if (current == item) {
        return true;
    }

    if (anim_busy(obj_dude) || rt_is_reloading()) {
        return false;
    }

    // Non-weapons (a chem) go into the hand as the inventory screen puts
    // them there: by the hand flag. (`inven_wield` is for weapons and armor.)
    int rc = 0;
    if (item_get_type(item) == ITEM_TYPE_WEAPON) {
        rc = inven_wield(obj_dude, item, intface_is_item_right_hand());
    } else {
        bool right = intface_is_item_right_hand() != 0;
        Object* previous = right ? inven_right_hand(obj_dude) : inven_left_hand(obj_dude);
        if (previous != NULL) {
            previous->flags &= ~OBJECT_IN_ANY_HAND;
        }
        item->flags |= right ? OBJECT_IN_RIGHT_HAND : OBJECT_IN_LEFT_HAND;
    }
    intface_update_items(false);
    intface_get_current_item(&current);
    if (current != item) {
        at_log("harness: could not equip %s (rc %d)", object_name(item), rc);
        return false;
    }

    at_log("harness: equipped %s", object_name(item));
    return true;
}

static int at_current_hit_mode()
{
    int hitMode;
    bool aiming;
    if (intface_get_attack(&hitMode, &aiming) != 0) {
        return -1;
    }
    return hitMode;
}

static int at_current_anim()
{
    int hitMode = at_current_hit_mode();
    return hitMode != -1 ? item_w_anim(obj_dude, hitMode) : -1;
}

// Test fixtures: weapons created from the game's own prototypes.
static Object* at_create_weapon(int primaryMode, int secondaryMode, bool normalDamage, int quantity)
{
    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_ITEM << 24) | index;
        Proto* proto;
        if (proto_ptr(pid, &proto) == -1 || proto->item.type != ITEM_TYPE_WEAPON) {
            continue;
        }

        int primary = proto->item.extendedFlags & 0xF;
        int secondary = (proto->item.extendedFlags & 0xF0) >> 4;
        if (primary != primaryMode || (secondaryMode != -1 && secondary != secondaryMode)) {
            continue;
        }

        if (normalDamage && proto->item.data.weapon.damageType != DAMAGE_TYPE_NORMAL) {
            continue;
        }

        Object* weapon;
        if (obj_pid_new(&weapon, pid) == -1) {
            continue;
        }

        Rect rect;
        if (item_add_force(obj_dude, weapon, quantity) != 0) {
            continue;
        }
        obj_disconnect(weapon, &rect);
        if (item_w_max_ammo(weapon) > 0) {
            item_w_set_curr_ammo(weapon, item_w_max_ammo(weapon));
        }

        int ammoPid = proto->item.data.weapon.ammoTypePid;
        Object* ammo;
        if (item_w_max_ammo(weapon) > 0 && ammoPid > 0 && obj_pid_new(&ammo, ammoPid) != -1) {
            if (item_add_force(obj_dude, ammo, 3) == 0) {
                obj_disconnect(ammo, &rect);
            }
        }

        at_log("harness: created test weapon %s (pid 0x%08X, modes %d/%d, x%d)",
            object_name(weapon), pid, primary, secondary, quantity);
        return weapon;
    }

    return NULL;
}

// 6 = single fire, 7 = burst, 5 = throw (see `attack_anim` in item.cc).
static Object* at_create_burst_weapon()
{
    return at_create_weapon(6, 7, false, 1);
}

static Object* at_create_throw_weapon()
{
    return at_create_weapon(5, -1, true, 6);
}

static bool at_file_recent(const char* path, time_t since)
{
    struct stat info;
    return stat(path, &info) == 0 && info.st_size > 0 && info.st_mtime >= since;
}

// Watches the lock: when its target dies the lock must be gone right away.
static void at_watch_lock()
{
    static int deadTicks = 0;
    Object* lock = rt_get_lock();
    if (at_lock_watch != NULL && at_lock_watch != lock) {
        if (critter_is_dead(at_lock_watch)) {
            at_check(C_LOCK_DEATH, lock == NULL, "locked target died, lock now %s", lock == NULL ? "cleared" : critter_name(lock));
        }
        deadTicks = 0;
    } else if (lock != NULL && critter_is_dead(lock)) {
        // Death is applied after this watcher runs; allow a couple of ticks.
        if (++deadTicks > 3) {
            at_check(C_LOCK_DEATH, false, "lock kept on a dead target");
            deadTicks = 0;
        }
    }
    at_lock_watch = lock;
}

// A free tile at `minDistance`..`maxDistance` from the player, on screen.
static int at_free_tile(int minDistance, int maxDistance)
{
    for (int distance = minDistance; distance <= maxDistance; distance++) {
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            int tile = tile_num_in_direction(obj_dude->tile, rotation, distance);
            if (tile == -1 || obj_blocking_at(NULL, tile, obj_dude->elevation) != NULL) {
                continue;
            }
            int x;
            int y;
            if (tile_coord(tile, &x, &y, obj_dude->elevation) != 0) {
                continue;
            }
            int width = scr_size.lrx - scr_size.ulx + 1;
            int height = scr_size.lry - scr_size.uly + 1 - INTERFACE_BAR_HEIGHT;
            if (x < 20 || x > width - 40 || y < 20 || y > height - 30) {
                continue;
            }
            return tile;
        }
    }
    return -1;
}

// Test fixture: a rat (the cave's own critter prototype). Hostile ones are
// made hostile the way a script's attack() does.
static Object* at_spawn_rat_at(int tile, bool hostile)
{
    if (at_rat_pid == -1 || tile == -1) {
        return NULL;
    }

    Object* rat;
    if (obj_pid_new(&rat, at_rat_pid) == -1) {
        return NULL;
    }

    Rect rect;
    obj_move_to_tile(rat, tile, obj_dude->elevation, &rect);
    tile_refresh_rect(&rect, obj_dude->elevation);
    rat->data.critter.combat.team = at_rat_team;

    if (!hostile && rat->sid != -1) {
        // A calm test dummy: without the rat script, which attacks on sight.
        scr_remove(rat->sid);
        rat->sid = -1;
    }

    if (hostile) {
        STRUCT_664980 request;
        memset(&request, 0, sizeof(request));
        request.attacker = rat;
        request.defender = obj_dude;
        rt_combat_request(&request);
    }

    at_log("harness: spawned %s rat at %d tiles", hostile ? "a hostile" : "a", obj_dist(obj_dude, rat));
    return rat;
}

static Object* at_spawn_rat(int minDistance, int maxDistance, bool hostile)
{
    return at_spawn_rat_at(at_free_tile(minDistance, maxDistance), hostile);
}

// A free, on-screen tile within `maxDistance` that the player can't shoot
// at because scenery or a wall is in the way.
static int at_covered_tile(int maxDistance)
{
    int width = scr_size.lrx - scr_size.ulx + 1;
    int height = scr_size.lry - scr_size.uly + 1 - INTERFACE_BAR_HEIGHT;
    for (int distance = 3; distance <= maxDistance; distance++) {
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            int corner = tile_num_in_direction(obj_dude->tile, rotation, distance);
            for (int step = 0; step < distance && corner != -1; step++) {
                int tile = tile_num_in_direction(corner, (rotation + 2) % ROTATION_COUNT, step);
                int x;
                int y;
                if (tile == -1 || obj_blocking_at(NULL, tile, obj_dude->elevation) != NULL
                    || tile_coord(tile, &x, &y, obj_dude->elevation) != 0
                    || x < 20 || x > width - 40 || y < 20 || y > height - 30) {
                    continue;
                }
                if (combat_is_shot_blocked(obj_dude, obj_dude->tile, tile, NULL, NULL)) {
                    return tile;
                }
            }
        }
    }
    return -1;
}

// The indicator shows the vanilla hit chance for the current weapon/mode.
static bool at_indicator_matches(Object* target, char* note, size_t size)
{
    const RtIndicator* indicator = rt_get_indicator();
    int hitMode = at_current_hit_mode();
    int expected = hitMode != -1 ? determine_to_hit(obj_dude, target, HIT_LOCATION_UNCALLED, hitMode) : -1;
    char text[16];
    snprintf(text, sizeof(text), "%d%%", expected);

    Rect bounds;
    obj_bound(target, &bounds);
    bool onTarget = indicator->x >= bounds.ulx && indicator->x <= bounds.lrx
        && indicator->y >= bounds.uly && indicator->y <= bounds.lry;

    snprintf(note, size, "indicator '%s' (to-hit %d) at (%d,%d) on target=%d; vanilla determine_to_hit=%d%% (dist %d)",
        indicator->text, indicator->toHit, indicator->x, indicator->y, onTarget, expected, obj_dist(obj_dude, target));

    return indicator->visible
        && rt_get_lock() == target
        && indicator->badShot == COMBAT_BAD_SHOT_OK
        && indicator->toHit == expected
        && strcmp(indicator->text, text) == 0
        && onTarget;
}

static int at_ammo_rounds(Object* weapon)
{
    int rounds = 0;
    Inventory* inventory = &(obj_dude->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        Object* item = inventory->items[index].item;
        if (item_get_type(item) == ITEM_TYPE_AMMO && item_w_can_reload(weapon, item)) {
            rounds += (inventory->items[index].quantity - 1) * item_w_max_ammo(item) + item_w_curr_ammo(item);
        }
    }
    return rounds;
}

static int at_find_script(const char* name)
{
    for (int index = 0; index < 2000; index++) {
        char entry[16];
        if (scr_list_str(index, entry, sizeof(entry)) == -1) {
            break;
        }
        if (SDL_strcasecmp(entry, name) == 0) {
            return index;
        }
    }
    return -1;
}

static int at_lock_candidates(Object** out, int capacity)
{
    int count = 0;
    Object** list;
    int total = obj_create_list(-1, obj_dude->elevation, OBJ_TYPE_CRITTER, &list);
    for (int index = 0; index < total && count < capacity; index++) {
        Object* critter = list[index];
        if (critter != obj_dude && !critter_is_dead(critter) && at_filter_lock_candidate(critter)) {
            out[count++] = critter;
        }
    }
    if (total > 0) {
        obj_delete_list(list);
    }
    return count;
}

// -----------------------------------------------------------------------------
// Steps: movement
// -----------------------------------------------------------------------------

static bool step_wait_load(unsigned int elapsed, bool first, bool timedOut)
{
    if (elapsed < 3000) {
        return false;
    }

    at_log("map: %s elevation=%d player tile=%d hp=%d/%d maxAP=%d",
        map_data.name,
        obj_dude->elevation,
        obj_dude->tile,
        critter_get_hits(obj_dude),
        stat_level(obj_dude, STAT_MAXIMUM_HIT_POINTS),
        stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS));
    at_log("controller: %d physical controller(s) detected%s%s (this test uses simulated input)",
        gamepad_get_detected_count(),
        gamepad_get_detected_count() > 0 ? ": " : "",
        gamepad_get_name());
    at_check(C_MAP, SDL_strncasecmp(map_data.name, "V13ENT", 6) == 0, "map=%s", map_data.name);

    Object* rat = at_nearest(at_filter_enemy_team, 100);
    if (rat != NULL) {
        at_rat_pid = rat->pid;
        at_rat_team = rat->data.critter.combat.team;
        at_log("harness: rat prototype 0x%08X, team %d", at_rat_pid, at_rat_team);
    }

    at_screenshot("01_start");
    return true;
}

static bool step_walk(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_tile_a = obj_dude->tile;
        at_flag_a = false;
        at_count_a = rt_get_stats()->moveRequests;
        at_stick_open_direction();
        return false;
    }

    if (FID_ANIM_TYPE(obj_dude->fid) == ANIM_WALK) {
        at_flag_a = true;
    }

    if (elapsed >= 2000) {
        int moved = tile_dist(at_tile_a, obj_dude->tile);
        int requests = rt_get_stats()->moveRequests - at_count_a;
        at_check(C_WALK, moved >= 2 && at_flag_a, "moved %d tiles, walk animation=%d", moved, at_flag_a);
        at_check(C_MOVE_REQUESTS, requests >= 1 && requests <= 3, "%d movement request(s) for %d tiles", requests, moved);
        at_stick_release();
        return true;
    }
    return false;
}

static bool step_stop(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_tile_b = obj_dude->tile;
    }

    if (elapsed >= 800) {
        at_check(C_STOP, !anim_busy(obj_dude), "animation busy after release=%d (anim %d, tiles moved after release %d)",
            anim_busy(obj_dude) != 0, FID_ANIM_TYPE(obj_dude->fid), tile_dist(at_tile_b, obj_dude->tile));
        return true;
    }
    return false;
}

// Sprinting outside combat costs no AP.
static bool step_run(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_clear_rats_near(20);
        at_ap_a = rt_get_dude_ap();
        at_ap_b = at_ap_a;
        at_flag_a = false;
        at_flag_b = false;
        at_count_a = 0;
        if (!rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        at_stick_open_direction();
        return false;
    }

    at_stick_open_direction();
    if (FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING) {
        at_flag_a = true;
    }
    if (rt_get_dude_ap() < at_ap_b) {
        at_ap_b = rt_get_dude_ap();
    }
    if (rt_player_in_combat()) {
        at_count_a++;
    }
    at_clear_rats_near(0);

    if (elapsed > 1000 && !at_flag_b) {
        at_flag_b = true;
        at_screenshot("02_run");
    }

    if (elapsed >= 2500 || timedOut) {
        at_check(C_RUN, at_flag_a && at_ap_b >= at_ap_a - 0.01f && at_count_a == 0,
            "run animation=%d, AP %.2f, lowest while running %.2f, in combat %d ticks", at_flag_a, at_ap_a, at_ap_b, at_count_a);
        return true;
    }
    return false;
}

// Outside combat a run starts and continues at 0 AP, and AP regenerates.
static bool step_run_zero(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_clear_rats_near(20);
        at_stick_release();
        at_phase = 0;
        return false;
    }

    at_clear_rats_near(0);
    const RtStats* stats = rt_get_stats();
    switch (at_phase) {
    case 0:
        if (rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        at_phase = 1;
        break;
    case 1:
        if (elapsed < 400 || rt_is_run_toggled() || !at_aim_released()) {
            break;
        }
        rt_debug_set_ap(0.0f);
        at_flag_a = false;
        at_time_a = elapsed;
        at_ap_b = stats->sprintCalmRegen;
        at_press(GAMEPAD_BUTTON_LEFT_STICK);
        at_stick_open_direction();
        at_phase = 2;
        break;
    case 2:
        at_stick_open_direction();
        if (FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING) {
            at_flag_a = true;
        }
        if (elapsed - at_time_a >= 2000) {
            float regen = stats->sprintCalmRegen - at_ap_b;
            at_check(C_RUN_ZERO, at_flag_a && rt_is_run_toggled() && rt_get_dude_ap() > 1.0f && regen > 1.0f,
                "L3 at 0 AP: toggle on=%d, ran=%d, AP 0 -> %.2f while running (regenerated %.2f during the run)",
                rt_is_run_toggled(), at_flag_a, rt_get_dude_ap(), regen);
            return true;
        }
        break;
    }
    return false;
}

// Locking onto a non-hostile critter is not combat: the run stays free.
static bool step_run_neutral(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_clear_rats_near(20);
        at_target = at_spawn_rat(3, 6, false);
        if (at_target != NULL) {
            rt_debug_set_lock(at_target);
        }
        at_ap_a = rt_get_dude_ap();
        at_ap_b = at_ap_a;
        at_count_a = 0;
        at_flag_a = false;
        if (!rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        return false;
    }

    at_stick_open_direction();
    if (FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING) {
        at_flag_a = true;
    }
    if (rt_get_dude_ap() < at_ap_b) {
        at_ap_b = rt_get_dude_ap();
    }
    if (rt_player_in_combat()) {
        at_count_a++;
    }
    at_clear_rats_near(0);

    if (elapsed >= 2000 || timedOut) {
        bool locked = at_target != NULL && rt_get_lock() == at_target;
        at_check(C_RUN_NEUTRAL, at_target != NULL && locked && at_flag_a && at_count_a == 0 && at_ap_b >= at_ap_a - 0.01f,
            "locked a calm rat=%d, ran=%d, in combat %d ticks, AP %.2f, lowest %.2f",
            locked, at_flag_a, at_count_a, at_ap_a, at_ap_b);
        rt_debug_set_lock(NULL);
        at_stick_release();
        if (at_target != NULL && !critter_is_dead(at_target)) {
            Rect rect;
            anim_rt_end_all(at_target);
            obj_erase_object(at_target, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
        }
        at_target = NULL;
        return true;
    }
    return false;
}

// A hostile engages during a run: AP drains until it is out, then the
// player walks and the toggle clears (checked by the next step).
static bool step_run_combat_start(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_phase = 0;
        at_flag_a = false;
        at_flag_b = false;
        at_target = NULL;
        rt_debug_set_ap((float)stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS));
        if (!rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        return false;
    }

    at_heal();
    at_stick_open_direction();
    switch (at_phase) {
    case 0:
        // Running, out of combat, for a moment.
        if (elapsed < 800) {
            break;
        }
        at_ap_a = rt_get_dude_ap();
        at_flag_b = FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING && !rt_player_in_combat();
        at_target = at_spawn_rat(5, 8, true);
        at_time_a = elapsed;
        at_phase = 1;
        break;
    case 1:
        if (!rt_player_in_combat()) {
            if (elapsed - at_time_a > 5000) {
                at_check(C_RUN_COMBAT_START, false, "the hostile rat never engaged");
                at_stick_release();
                return true;
            }
            break;
        }
        at_ap_b = rt_get_dude_ap();
        at_time_a = elapsed;
        at_phase = 2;
        break;
    case 2:
        if (FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING) {
            at_flag_a = true;
        }
        if (rt_get_dude_ap() <= 0.0f || timedOut) {
            at_check(C_RUN_COMBAT_START, at_flag_b && at_flag_a && rt_get_dude_ap() <= 0.0f && at_ap_a >= at_ap_b - 0.01f,
                "running out of combat=%d (AP %.2f), combat began at AP %.2f, ran in combat=%d, AP now %.2f after %d ms",
                at_flag_b, at_ap_a, at_ap_b, at_flag_a, rt_get_dude_ap(), (int)(elapsed - at_time_a));
            // The next step reads `at_flag_b` as "ran after exhaustion".
            at_flag_b = false;
            return true;
        }
        break;
    }
    return false;
}

// Combat ends during a run: the drain stops at once.
static bool step_run_combat_end(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_phase = 0;
        at_time_a = elapsed;
        return false;
    }

    at_heal();
    at_stick_open_direction();
    switch (at_phase) {
    case 0:
        if (!rt_player_in_combat()) {
            at_spawn_rat(5, 8, true);
        }
        at_phase = 1;
        break;
    case 1:
        // In combat with enough AP to run.
        if (!rt_player_in_combat() || rt_get_dude_ap() < 3.0f) {
            if (elapsed - at_time_a > 20000) {
                at_check(C_RUN_COMBAT_END, false, "setup: in combat=%d, AP %.2f", rt_player_in_combat(), rt_get_dude_ap());
                at_stick_release();
                at_clear_rats_near(0);
                return true;
            }
            break;
        }
        at_flag_a = false;
        if (!rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        at_time_a = elapsed;
        at_phase = 2;
        break;
    case 2:
        // Running in combat (any moment of the window).
        if (FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING && rt_player_in_combat()) {
            at_flag_a = true;
        }
        if (elapsed - at_time_a < 1000 || (!at_flag_a && elapsed - at_time_a < 4000)) {
            break;
        }
        at_ap_a = rt_get_dude_ap();
        at_clear_rats_near(0);
        at_flag_b = !rt_player_in_combat();
        at_ap_b = rt_get_dude_ap();
        at_time_a = elapsed;
        at_phase = 3;
        break;
    case 3:
        if (rt_get_dude_ap() < at_ap_b) {
            at_ap_b = rt_get_dude_ap();
        }
        if (elapsed - at_time_a >= 1500) {
            at_check(C_RUN_COMBAT_END, at_flag_a && at_flag_b && at_ap_b >= at_ap_a - 0.01f && rt_get_dude_ap() > at_ap_a && rt_is_run_toggled(),
                "running in combat=%d at AP %.2f; combat over at once=%d; lowest AP after %.2f, AP now %.2f, still running=%d",
                at_flag_a, at_ap_a, at_flag_b, at_ap_b, rt_get_dude_ap(), rt_is_run_toggled());
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
            at_stick_release();
            return true;
        }
        break;
    }
    return false;
}

static bool step_exhausted(unsigned int elapsed, bool first, bool timedOut)
{
    // Keep pushing the stick: the player must walk, and keep walking even
    // after AP is back above 1 (a new click is required to run).
    // Still in combat (the hostile rat from the previous step).
    at_heal();
    at_stick_open_direction();

    int anim = FID_ANIM_TYPE(obj_dude->fid);
    if (elapsed > 300 && anim == ANIM_RUNNING) {
        at_flag_b = true;
    }

    if (elapsed >= 2500) {
        at_check(C_EXHAUST, !rt_is_run_toggled() && !at_flag_b,
            "toggle=%d, ran after exhaustion=%d, anim=%d", rt_is_run_toggled(), at_flag_b, anim);
        at_check(C_RUN_RECLICK, !at_flag_b && rt_get_dude_ap() >= 1.0f,
            "AP back to %.2f while still walking", rt_get_dude_ap());
        at_stick_release();
        return true;
    }
    return false;
}

static bool step_regen(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_ap_a = rt_get_dude_ap();
        return false;
    }

    if (elapsed >= 2000) {
        at_check(C_REGEN, rt_get_dude_ap() > at_ap_a, "AP %.2f -> %.2f after 2 s idle", at_ap_a, rt_get_dude_ap());
        return true;
    }
    return false;
}

static bool step_run_again(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_flag_a = false;
        at_press(GAMEPAD_BUTTON_LEFT_STICK);
        at_stick_open_direction();
        return false;
    }

    at_stick_open_direction();
    if (FID_ANIM_TYPE(obj_dude->fid) == ANIM_RUNNING) {
        at_flag_a = true;
    }

    if (at_flag_a || elapsed >= 4000) {
        at_check(C_RUN_RECLICK, at_flag_a, "a new L3 click runs again=%d", at_flag_a);
        if (rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        at_stick_release();
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: menus
// -----------------------------------------------------------------------------

static bool at_menu_inventory_script(unsigned int elapsed)
{
    if (elapsed > 1200) {
        // Still open well after the B press that opened it.
        at_flag_a = true;
    }

    if (elapsed > 1600) {
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }
    return false;
}

static bool step_inventory(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_flag_a = false;
        at_expect_menu("03_pipboy", at_menu_inventory_script);
        at_press(GAMEPAD_BUTTON_B);
        return false;
    }

    // Ticks only run in gameplay: once the menu was seen, it has closed.
    if ((at_menu_seen && elapsed > 300) || timedOut) {
        at_check(C_INVENTORY_B, at_menu_seen && at_flag_a && at_menu_done,
            "opened=%d stayed open=%d closed with B=%d", at_menu_seen, at_flag_a, at_menu_done);
        at_menu_expected = false;
        return true;
    }
    return false;
}
static bool step_screens(unsigned int elapsed, bool first, bool timedOut)
{
    // With no favorites assigned yet, D-pad up and down open nothing (they
    // activate a favorite directly); R3 opens the Skilldex.
    static const GamepadButton buttons[] = {
        GAMEPAD_BUTTON_DPAD_UP,
        GAMEPAD_BUTTON_DPAD_DOWN,
        GAMEPAD_BUTTON_RIGHT_STICK,
    };
    static const char* names[] = {
        "D-pad up",
        "D-pad down",
        "07_skilldex",
    };
    static int activationsBefore;

    unsigned int now = SDL_GetTicks();
    if (first) {
        at_count_a = 0;
        at_count_b = 0;
        at_time_a = 0;
        at_tile_a = (int)now;
        return false;
    }

    if (at_count_a >= 3) {
        at_check(C_SCREENS, at_count_b == 3, "%d of 3 presses did what they should (D-pad: no menu, nothing used; R3: Skilldex, closed with B)", at_count_b);
        at_menu_expected = false;
        return true;
    }

    bool skilldex = at_count_a == 2;
    if (at_time_a == 0) {
        if (now - (unsigned int)at_tile_a < 700) {
            return false;
        }
        activationsBefore = rt_favorites_get_state()->activations + rt_favorites_get_state()->refusals;
        at_expect_menu(skilldex ? names[at_count_a] : NULL, NULL);
        at_press(buttons[at_count_a]);
        at_time_a = now;
        return false;
    }

    if ((skilldex && at_menu_seen && now - at_time_a > 300) || now - at_time_a > (skilldex ? 5000u : 1500u)) {
        int activations = rt_favorites_get_state()->activations + rt_favorites_get_state()->refusals - activationsBefore;
        bool right = skilldex ? at_menu_seen && at_menu_done : !at_menu_seen && activations == 0;
        at_log("%s: menu opened=%d closed=%d, favorites activated %d time(s)", names[at_count_a], at_menu_seen, at_menu_done, activations);
        if (right) {
            at_count_b++;
        }
        at_menu_expected = false;
        at_count_a++;
        at_time_a = 0;
        at_tile_a = (int)now;
    }
    return false;
}
static bool step_y_lb(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_count_a = rt_get_stats()->targetingRequests;
        at_count_b = rt_aimed_get_state()->refusals;
        at_flag_a = false;
        at_flag_b = false;
        at_expect_menu(NULL, NULL);
        at_press(GAMEPAD_BUTTON_Y);
        return false;
    }

    if (elapsed > 800 && !at_flag_a) {
        at_flag_a = true;
        at_flag_b = at_menu_seen;
        at_menu_seen = false;
        // LB: AIMED SHOT (opens only with a target; the default script closes it).
        at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
    }

    if (elapsed >= 1800) {
        bool lbOpenedMenu = at_menu_seen;
        at_menu_expected = false;
        int lb = rt_get_stats()->targetingRequests - at_count_a;
        int refusals = rt_aimed_get_state()->refusals - at_count_b;
        at_check(C_Y_LB, !at_flag_b && lb == 1 && rt_get_lock() == NULL,
            "Y opened a menu=%d, LB presses=%d, LB locked something=%d", at_flag_b, lb, rt_get_lock() != NULL);
        // The no-target case is checked in the aimed shot entry step, where
        // no enemy is around for sure (here a rat may have noticed the player).
        at_log("LB: aimed shot screen opened=%d, refused=%d (%s)", lbOpenedMenu, refusals, rt_aimed_get_state()->message);
        return true;
    }
    return false;
}

// Pause menu: D-pad navigation, triggers ignored, A on a button.
static bool at_menu_pause_script(unsigned int elapsed)
{
    switch (at_count_a) {
    case 0:
        if (elapsed > 600) {
            mouse_get_position(&(at_mouse_x[0]), &(at_mouse_y[0]));
            at_press(GAMEPAD_BUTTON_DPAD_UP);
            at_count_a = 1;
        }
        break;
    case 1:
        if (elapsed > 900) {
            mouse_get_position(&(at_mouse_x[1]), &(at_mouse_y[1]));
            at_press(GAMEPAD_BUTTON_DPAD_UP);
            at_count_a = 2;
        }
        break;
    case 2:
        if (elapsed > 1200) {
            mouse_get_position(&(at_mouse_x[2]), &(at_mouse_y[2]));
            at_obj_a = rt_get_lock();
            at_count_b = rt_get_stats()->playerAttacks;
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            at_count_a = 3;
        }
        break;
    case 3:
        // Walk the cursor down to the last button (Done) and press A.
        if (elapsed > 1500 && elapsed < 2600 && (elapsed / 150) % 2 == 0) {
            at_press(GAMEPAD_BUTTON_DPAD_DOWN);
        }
        if (elapsed > 2900) {
            mouse_get_position(&(at_mouse_x[3]), &(at_mouse_y[3]));
            at_press(GAMEPAD_BUTTON_A);
            at_count_a = 4;
            at_time_a = SDL_GetTicks();
        }
        break;
    case 4:
        // A should have closed the menu; if not, back out with B.
        if (SDL_GetTicks() - at_time_a > 1500) {
            at_flag_b = true;
            at_press(GAMEPAD_BUTTON_B);
            return true;
        }
        break;
    }
    return false;
}

static bool step_pause_menu(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_count_a = 0;
        at_flag_b = false;
        at_pause_sim_start = rt_get_time();
        at_pause_real_start = SDL_GetTicks();
        at_expect_menu("08_pause_menu", at_menu_pause_script);
        at_press(GAMEPAD_BUTTON_START);
        return false;
    }

    // This tick runs only in gameplay, i.e. after the menu closed.
    if (at_menu_seen && elapsed > 200) {
        unsigned int simDelta = rt_get_time() - at_pause_sim_start;
        unsigned int realDelta = SDL_GetTicks() - at_pause_real_start;
        bool moved = (at_mouse_x[1] != at_mouse_x[0] || at_mouse_y[1] != at_mouse_y[0])
            && (at_mouse_x[2] != at_mouse_x[1] || at_mouse_y[2] != at_mouse_y[1]);
        at_check(C_MENU_NAV, moved, "cursor (%d,%d) -> (%d,%d) -> (%d,%d)",
            at_mouse_x[0], at_mouse_y[0], at_mouse_x[1], at_mouse_y[1], at_mouse_x[2], at_mouse_y[2]);
        at_check(C_MENU_TRIGGERS, rt_get_lock() == at_obj_a && rt_get_stats()->playerAttacks == at_count_b,
            "lock unchanged=%d, player attacks from menu triggers=%d", rt_get_lock() == at_obj_a, rt_get_stats()->playerAttacks - at_count_b);
        at_check(C_MENU_A, !at_flag_b, "A on the button at (%d,%d) closed the menu=%d", at_mouse_x[3], at_mouse_y[3], !at_flag_b);
        at_check(C_PAUSE, realDelta > 2500 && simDelta < 600, "real %u ms, simulation %u ms", realDelta, simDelta);
        at_menu_expected = false;
        return true;
    }

    if (timedOut) {
        at_check(C_MENU_NAV, false, "pause menu did not open");
        at_menu_expected = false;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: target lock
// -----------------------------------------------------------------------------

static bool step_find_hostile(unsigned int elapsed, bool first, bool timedOut)
{
    at_heal();

    Object* hostile = at_nearest(at_filter_hostile, 20);
    if (hostile != NULL) {
        at_log("hostile available: %s at %d", critter_name(hostile), obj_dist(obj_dude, hostile));
        register_clear(obj_dude);
        return true;
    }

    // Cave rats don't always notice the player (earlier steps remove rats
    // that engaged): after a while a hostile one is spawned.
    if (elapsed > 15000) {
        if (at_spawn_rat(4, 8, true) != NULL) {
            at_log("harness: no rat engaged on its own, spawned a hostile one");
        }
        return false;
    }

    // Walk towards the nearest rat until it notices the player.
    Object* rat = at_nearest(at_filter_enemy_team, 60);
    if (rat != NULL) {
        at_approach(rat, 4);
    }
    return false;
}

static bool step_lock(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        if (rt_get_lock() != NULL) {
            rt_debug_set_lock(NULL);
        }
        at_heal();
        at_count_a = at_min_hostile_distance();
        at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        return false;
    }

    if (elapsed >= 400) {
        Object* lock = rt_get_lock();
        bool nearest = lock != NULL && obj_dist(obj_dude, lock) <= at_count_a + 1;
        at_check(C_LOCK, lock != NULL && rt_is_hostile(lock) && nearest,
            "locked=%s hostile=%d distance=%d nearest enemy=%d",
            lock != NULL ? critter_name(lock) : "(none)",
            lock != NULL && rt_is_hostile(lock),
            lock != NULL ? obj_dist(obj_dude, lock) : -1,
            at_count_a);

        if (lock != NULL) {
            char note[240];
            const RtIndicator* indicator = rt_get_indicator();
            bool matches = at_indicator_matches(lock, note, sizeof(note));
            // Out of range or blocked is fine here too, as long as it says so.
            bool honest = indicator->visible && indicator->badShot != COMBAT_BAD_SHOT_OK && indicator->toHit == -1;
            at_check(C_INDICATOR, matches || honest, "%s", note);
        } else {
            at_check(C_INDICATOR, false, "no lock");
        }
        at_screenshot("09_lock");
        at_obj_a = lock;
        return true;
    }
    return false;
}

static bool step_lock_persist(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_stick_open_direction();
        return false;
    }

    at_heal();
    at_stick_open_direction();
    if (elapsed >= 1200) {
        at_stick_release();
        Object* lock = rt_get_lock();
        if (at_obj_a != NULL && critter_is_dead(at_obj_a)) {
            at_log("lock target died during the persistence test; no verdict");
            return true;
        }
        at_check(C_LOCK_PERSIST, lock != NULL && lock == at_obj_a && rt_get_indicator()->visible,
            "lock same=%d indicator visible=%d", lock == at_obj_a, rt_get_indicator()->visible);
        return true;
    }
    return false;
}

static bool step_unlock(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        if (rt_get_lock() == NULL) {
            // The previous target died; lock again so there's something to release.
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_count_a = 1;
        } else {
            at_count_a = 0;
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        }
        return false;
    }

    if (at_count_a == 1) {
        if (elapsed > 400) {
            at_count_a = 0;
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_step_start = SDL_GetTicks();
        }
        return false;
    }

    if (elapsed >= 400) {
        at_check(C_UNLOCK, rt_get_lock() == NULL && !rt_get_indicator()->visible,
            "lock=%s indicator visible=%d", rt_get_lock() != NULL ? critter_name(rt_get_lock()) : "(none)", rt_get_indicator()->visible);
        return true;
    }
    return false;
}

// RT without a lock never attacks, even with a hostile right next to the
// player (and so also right after unlocking).
static bool step_no_lock_rt(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        rt_debug_set_lock(NULL);
        at_phase = 0;
        return false;
    }

    at_heal();
    switch (at_phase) {
    case 0: {
        Object* hostile = at_nearest(at_filter_hostile, 25);
        if (hostile == NULL) {
            hostile = at_spawn_rat(1, 2, true);
        }
        if (hostile != NULL && obj_dist(obj_dude, hostile) > 1) {
            at_approach(hostile, 1);
            if (!timedOut) {
                break;
            }
        }
        at_obj_a = hostile;
        at_count_a = rt_get_stats()->playerAttacks;
        at_count_b = rt_get_stats()->playerRejected;
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 1;
        at_time_a = now;
        break;
    }
    case 1:
        if (now - at_time_a < 1200) {
            break;
        }
        at_check(C_NO_LOCK_RT,
            rt_get_stats()->playerAttacks == at_count_a && rt_get_stats()->playerRejected > at_count_b && rt_get_lock() == NULL,
            "hostile at %d tiles, no lock: attacks=%d, rejected=%d, lock acquired=%d",
            at_obj_a != NULL ? obj_dist(obj_dude, at_obj_a) : -1,
            rt_get_stats()->playerAttacks - at_count_a,
            rt_get_stats()->playerRejected - at_count_b,
            rt_get_lock() != NULL);
        return true;
    }
    return false;
}

// Right stick flicks: one switch per flick, in the flicked direction; none
// when nothing is there; the camera stays put.
static float at_switch_dx;
static float at_switch_dy;
static int at_switch_center;

static bool step_switch(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        at_retries = 0;
        at_release_all();
        return false;
    }

    at_heal();
    Object* candidates[32];
    int count = at_lock_candidates(candidates, 32);

    switch (at_phase) {
    case 0:
        // Three or more candidates on screen (spawn rats if needed).
        if (count < 3) {
            if (at_spawn_rat(3, 6, true) == NULL && timedOut) {
                at_log("switch test: not enough targets on screen");
                return true;
            }
            break;
        }
        if (rt_get_lock() == NULL) {
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        }
        at_phase = 1;
        at_time_a = now;
        break;
    case 1: {
        if (now - at_time_a < 400) {
            break;
        }
        Object* lock = rt_get_lock();
        if (lock == NULL) {
            at_phase = 0;
            break;
        }

        // Flick toward another candidate and hold the stick there.
        int lx;
        int ly;
        at_screen_pos(lock, &lx, &ly);
        Object* other = NULL;
        for (int index = 0; index < count; index++) {
            if (candidates[index] != lock) {
                other = candidates[index];
                break;
            }
        }
        if (other == NULL) {
            at_phase = 0;
            break;
        }
        int ox;
        int oy;
        at_screen_pos(other, &ox, &oy);
        float dx = (float)(ox - lx);
        float dy = (float)(oy - ly);
        float length = sqrtf(dx * dx + dy * dy);
        if (length < 1.0f) {
            break;
        }
        at_switch_dx = dx / length;
        at_switch_dy = dy / length;
        at_switch_center = tile_center_tile;
        at_obj_a = lock;
        at_count_a = rt_get_stats()->lockSwitches;
        at_count_b = rt_get_stats()->playerAttacks;
        at_pad.rightX = at_switch_dx;
        at_pad.rightY = at_switch_dy;
        at_log("switch: lock %s at (%d,%d), flicking toward (%d,%d)", critter_name(lock), lx, ly, ox, oy);
        at_phase = 2;
        at_time_a = now;
        break;
    }
    case 2:
        // Held for a full second: still only one switch.
        if (now - at_time_a < 1000) {
            break;
        }
        at_pad.rightX = 0.0f;
        at_pad.rightY = 0.0f;
        {
            Object* lock = rt_get_lock();
            int switches = rt_get_stats()->lockSwitches - at_count_a;
            bool inDirection = false;
            if (lock != NULL && at_obj_a != NULL && lock != at_obj_a) {
                int ax;
                int ay;
                int bx;
                int by;
                at_screen_pos(at_obj_a, &ax, &ay);
                at_screen_pos(lock, &bx, &by);
                float dx = (float)(bx - ax);
                float dy = (float)(by - ay);
                float length = sqrtf(dx * dx + dy * dy);
                // Critters keep moving; allow some slack around the 60 degree cone.
                inDirection = length > 0.0f && (dx * at_switch_dx + dy * at_switch_dy) / length >= 0.25f;
            }
            at_check(C_SWITCH, switches == 1 && lock != NULL && lock != at_obj_a && inDirection && rt_get_stats()->playerAttacks == at_count_b,
                "switches while held 1 s=%d, new lock=%s, in flick direction=%d, attacks=%d",
                switches, lock != NULL ? critter_name(lock) : "(none)", inDirection, rt_get_stats()->playerAttacks - at_count_b);
            at_check(C_SWITCH_NO_CAMERA, tile_center_tile == at_switch_center, "camera center %d -> %d", at_switch_center, tile_center_tile);
        }
        at_screenshot("21_switch");
        at_phase = 3;
        at_time_a = now;
        break;
    case 3: {
        if (now - at_time_a < 400) {
            break;
        }
        // Lock the rightmost candidate (harness), then flick right: there is
        // nothing further right to switch to.
        Object* rightmost = NULL;
        int rightmostX = -100000;
        for (int index = 0; index < count; index++) {
            int cx;
            int cy;
            at_screen_pos(candidates[index], &cx, &cy);
            if (cx > rightmostX) {
                rightmostX = cx;
                rightmost = candidates[index];
            }
        }
        if (rightmost == NULL) {
            at_log("switch test: no target left for the empty-direction flick");
            return true;
        }
        rt_debug_set_lock(rightmost);
        at_phase = 31;
        at_time_a = now;
        break;
    }
    case 31: {
        if (now - at_time_a < 200) {
            break;
        }
        Object* lock = rt_get_lock();
        if (lock == NULL) {
            at_log("switch test: lock lost before the empty-direction flick");
            return true;
        }
        int lx;
        int ly;
        at_screen_pos(lock, &lx, &ly);
        // Critters move: only flick while it is still clearly the rightmost.
        bool clear = true;
        for (int index = 0; index < count; index++) {
            int cx;
            int cy;
            at_screen_pos(candidates[index], &cx, &cy);
            // A margin, so a rat moving meanwhile can't get there first.
            if (candidates[index] != lock && cx > lx - 48) {
                clear = false;
            }
        }
        if (!clear) {
            if (++at_retries > 10) {
                at_log("switch test: no stable rightmost target; empty-direction flick not run");
                return true;
            }
            at_phase = 3;
            break;
        }
        at_switch_dx = 1.0f;
        at_switch_dy = 0.0f;
        at_obj_a = lock;
        at_count_a = rt_get_stats()->lockSwitchMisses;
        at_switch_center = tile_center_tile;
        at_pad.rightX = at_switch_dx;
        at_pad.rightY = at_switch_dy;
        at_phase = 4;
        at_time_a = now;
        break;
    }
    case 4:
        if (now - at_time_a < 300) {
            break;
        }
        at_pad.rightX = 0.0f;
        at_pad.rightY = 0.0f;
        if (at_obj_a != NULL && critter_is_dead(at_obj_a)) {
            at_log("switch test: target died during the empty-direction flick; no verdict");
            return true;
        }
        at_check(C_SWITCH_KEEP, rt_get_lock() == at_obj_a && rt_get_stats()->lockSwitchMisses == at_count_a + 1,
            "lock kept=%d, misses=%d", rt_get_lock() == at_obj_a, rt_get_stats()->lockSwitchMisses - at_count_a);
        at_check(C_SWITCH_NO_CAMERA, tile_center_tile == at_switch_center, "camera center %d -> %d", at_switch_center, tile_center_tile);
        return true;
    }

    if (timedOut) {
        at_release_all();
        at_log("switch test timed out in phase %d", at_phase);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: enemy attack slot
// -----------------------------------------------------------------------------

static std::unordered_map<Object*, int> at_slot_grants;
static std::unordered_map<Object*, unsigned int> at_slot_queued_since;
static unsigned int at_slot_max_continuous_wait;
static Object* at_slot_last_holder;
static bool at_slot_moved;
static bool at_slot_acted;
static int at_slot_grants_after_walk;
static int at_slot_player_attacks;

static bool step_slot(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        rt_debug_set_lock(NULL);
        at_slot_grants.clear();
        at_slot_queued_since.clear();
        at_slot_max_continuous_wait = 0;
        at_slot_last_holder = NULL;
        at_slot_moved = false;
        at_slot_acted = false;
        at_slot_player_attacks = rt_get_stats()->playerAttacks + rt_get_stats()->playerRejected;
        at_count_b = 0;
        at_count_a = rt_get_stats()->playerSlotGrants;
        at_tile_a = obj_dude->tile;
        at_flag_a = false;
        at_flag_b = false;
        // Fixture: several rats around the player at once.
        int spawned = 0;
        for (int index = 0; index < 4; index++) {
            if (at_spawn_rat(1, 3, true) != NULL) {
                spawned++;
            }
        }
        at_log("slot test: %d rats spawned around the player", spawned);
        at_unarmed();
        return false;
    }

    // The player only takes damage here (healed by the harness).
    at_heal();

    Object* holder = rt_get_player_slot_holder();
    if (holder != NULL && holder != at_slot_last_holder) {
        at_slot_grants[holder]++;
        at_slot_queued_since.erase(holder);
    }
    at_slot_last_holder = holder;

    Object* queue[32];
    int queued = rt_get_player_slot_queue(queue, 32);
    std::unordered_set<Object*> inQueue;
    for (int index = 0; index < queued; index++) {
        inQueue.insert(queue[index]);
        if (at_slot_queued_since.find(queue[index]) == at_slot_queued_since.end()) {
            at_slot_queued_since[queue[index]] = now;
        }
        unsigned int waited = now - at_slot_queued_since[queue[index]];
        if (waited > at_slot_max_continuous_wait) {
            at_slot_max_continuous_wait = waited;
        }
    }
    for (auto it = at_slot_queued_since.begin(); it != at_slot_queued_since.end();) {
        if (inQueue.find(it->first) == inQueue.end()) {
            it = at_slot_queued_since.erase(it);
        } else {
            ++it;
        }
    }

    // Seconds 5-8: the player walks a little (5-6.5 s), then fights back,
    // while enemies wait their turn.
    if (elapsed > 5000 && elapsed < 8000) {
        if (elapsed < 6500) {
            at_stick_open_direction();
        } else {
            at_stick_release();
        }
        if (queued > 0) {
            at_flag_a = true;
        }
        if (obj_dude->tile != at_tile_a) {
            at_slot_moved = true;
        }
        // ...and fights back: lock and attack.
        if (elapsed > 6500 && rt_get_lock() == NULL && at_count_b == 0) {
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_count_b = 1;
        }
        if (queued > 0 && (elapsed / 700) % 2 == 0) {
            at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        }
        // The game handled the input (an attack started, or was refused for
        // a reason such as range).
        if (queued > 0 && rt_get_stats()->playerAttacks + rt_get_stats()->playerRejected > at_slot_player_attacks) {
            at_slot_acted = true;
        }
    } else {
        at_stick_release();
    }
    if (elapsed > 8000 && !at_flag_b) {
        at_flag_b = true;
        at_slot_grants_after_walk = rt_get_stats()->playerSlotGrants;
    }

    if (elapsed < 16000) {
        return false;
    }

    at_stick_release();
    const RtStats* stats = rt_get_stats();
    int grants = stats->playerSlotGrants - at_count_a;
    int distinct = (int)at_slot_grants.size();
    char counts[200];
    counts[0] = 0;
    for (auto& entry : at_slot_grants) {
        char part[16];
        snprintf(part, sizeof(part), " %d", entry.second);
        SDL_strlcat(counts, part, sizeof(counts));
    }

    at_check(C_SLOT_ONE, stats->playerSlotOverlaps == 0,
        "overlapping enemy attacks on the player=%d (max queue %d)", stats->playerSlotOverlaps, stats->playerSlotMaxQueue);
    // One attack at a time takes about 2.5 s (animation plus the player's
    // recovery window), so a fair queue waits at most about that per enemy
    // ahead, plus one round for an enemy sent to the back because it could
    // not attack when its turn came.
    unsigned int fairWait = (stats->playerSlotMaxQueue + 1) * 2500 > 8000 ? (stats->playerSlotMaxQueue + 1) * 2500 : 8000;
    at_check(C_SLOT_FAIR, grants >= 4 && distinct >= 3 && stats->playerSlotMaxQueue >= 2 && at_slot_max_continuous_wait <= fairWait,
        "%d attacks on the player by %d enemies (per enemy:%s), longest continuous wait %u ms (bound %u for queue %d)",
        grants, distinct, counts, at_slot_max_continuous_wait, fairWait, stats->playerSlotMaxQueue);
    at_check(C_SLOT_FREE, at_flag_a && (at_slot_moved || at_slot_acted) && stats->playerSlotGrants > at_slot_grants_after_walk,
        "while enemies queued the player moved=%d, had an attack handled (started or refused)=%d; enemies kept attacking afterwards=%d",
        at_slot_moved, at_slot_acted, stats->playerSlotGrants - at_slot_grants_after_walk);
    at_screenshot("22_slot");
    return true;
}

// -----------------------------------------------------------------------------
// Steps: attacks
// -----------------------------------------------------------------------------

typedef enum AtWeaponKind {
    AT_UNARMED,
    AT_MELEE,
    AT_SINGLE,
    AT_BURST,
    AT_THROW,
} AtWeaponKind;

static AtWeaponKind at_kind;
static Object* at_toughened = NULL;

static void at_restore_toughness()
{
    if (at_toughened != NULL) {
        stat_set_bonus(at_toughened, STAT_MAXIMUM_HIT_POINTS, 0);
        if (!critter_is_dead(at_toughened)) {
            critter_adjust_hits(at_toughened, 0);
        }
        at_toughened = NULL;
        at_log("harness: rat hit point bonus removed");
    }
}
static int at_counter_before;
static unsigned int at_phase_start;

static int at_kind_counter()
{
    const RtStats* stats = rt_get_stats();
    switch (at_kind) {
    case AT_UNARMED:
        return stats->playerUnarmed;
    case AT_MELEE:
        return stats->playerMelee;
    case AT_SINGLE:
        return stats->playerSingle;
    case AT_THROW:
        return stats->playerThrow;
    default:
        return stats->playerBurst;
    }
}

static bool at_kind_ready()
{
    int anim = at_current_anim();
    switch (at_kind) {
    case AT_UNARMED:
        if (inven_left_hand(obj_dude) != NULL || inven_right_hand(obj_dude) != NULL) {
            at_unarmed();
            return false;
        }
        return anim == ANIM_THROW_PUNCH;
    case AT_MELEE:
        at_wield(at_find_inventory("Knife"));
        return anim == ANIM_THRUST_ANIM || anim == ANIM_SWING_ANIM;
    case AT_SINGLE: {
        Object* pistol = at_find_inventory("10mm Pistol");
        at_wield(pistol);
        if (pistol != NULL && item_w_curr_ammo(pistol) == 0) {
            item_w_set_curr_ammo(pistol, item_w_max_ammo(pistol));
        }
        return anim == ANIM_FIRE_SINGLE;
    }
    case AT_THROW: {
        if (at_throw_pid == -1) {
            at_throw_weapon = at_create_throw_weapon();
            at_throw_pid = at_throw_weapon != NULL ? at_throw_weapon->pid : -2;
        }
        // A throw turns the thrown item into the projectile and puts the rest
        // of the stack in the hand as a new object.
        at_throw_weapon = NULL;
        Inventory* inventory = &(obj_dude->data.inventory);
        for (int index = 0; index < inventory->length; index++) {
            if (inventory->items[index].item->pid == at_throw_pid) {
                at_throw_weapon = inventory->items[index].item;
                break;
            }
        }
        at_wield(at_throw_weapon);
        return anim == ANIM_THROW_ANIM;
    }
    default: {
        if (at_burst_weapon == NULL) {
            at_burst_weapon = at_create_burst_weapon();
        }
        at_wield(at_burst_weapon);
        if (at_burst_weapon != NULL && item_w_curr_ammo(at_burst_weapon) < item_w_max_ammo(at_burst_weapon) / 2) {
            item_w_set_curr_ammo(at_burst_weapon, item_w_max_ammo(at_burst_weapon));
        }
        Object* current = NULL;
        intface_get_current_item(&current);
        if (current == at_burst_weapon && anim != ANIM_FIRE_BURST) {
            // Back cycles the attack mode (single -> burst).
            static unsigned int lastPress = 0;
            if (SDL_GetTicks() - lastPress > 600) {
                lastPress = SDL_GetTicks();
                at_press(GAMEPAD_BUTTON_BACK);
            }
        }
        return anim == ANIM_FIRE_BURST;
    }
    }
}

static int at_kind_reach()
{
    return at_kind == AT_UNARMED || at_kind == AT_MELEE ? 1 : 4;
}

static bool at_target_usable(Object* target)
{
    return target != NULL && !critter_is_dead(target) && rt_is_hostile(target);
}

// Approaches a hostile within reach and locks it; true when ready.
static bool at_prepare_target()
{
    at_heal();
    if (!at_target_usable(at_target)) {
        at_target = at_nearest(at_filter_hostile, 25);
        if (at_target == NULL) {
            at_target = at_spawn_rat(2, 4, true);
            return false;
        }
    }

    static Object* approaching = NULL;
    static unsigned int approachSince = 0;
    if (obj_dist(obj_dude, at_target) > at_kind_reach()) {
        unsigned int now = SDL_GetTicks();
        if (approaching != at_target) {
            approaching = at_target;
            approachSince = now;
        } else if (now - approachSince > 8000) {
            // Harness: the two can't get to each other (cave rocks); another
            // rat right next to the player instead.
            at_log("harness: can't reach %s (%d tiles, player busy %d anim %d), using a new rat",
                critter_name(at_target), obj_dist(obj_dude, at_target), anim_busy(obj_dude) != 0, FID_ANIM_TYPE(obj_dude->fid));
            approaching = NULL;
            at_target = at_spawn_rat(1, 1, true);
            return false;
        }
        at_approach(at_target, at_kind_reach());
        return false;
    }
    approaching = NULL;

    // Shots and throws need a clear line: a target behind rocks is replaced
    // by a rat on a clear tile.
    if ((at_kind == AT_SINGLE || at_kind == AT_BURST || at_kind == AT_THROW)
        && combat_is_shot_blocked(obj_dude, obj_dude->tile, at_target->tile, at_target, NULL)) {
        for (int attempt = 0; attempt < 40; attempt++) {
            int tile = at_free_tile(2, 4);
            if (tile != -1 && !combat_is_shot_blocked(obj_dude, obj_dude->tile, tile, NULL, NULL)) {
                at_log("harness: %s is behind cover, using a new rat in the open", critter_name(at_target));
                at_target = at_spawn_rat_at(tile, true);
                break;
            }
        }
        return false;
    }

    if (anim_busy(obj_dude) && FID_ANIM_TYPE(obj_dude->fid) == ANIM_WALK) {
        register_clear(obj_dude);
    }
    rt_debug_set_lock(at_target);
    return true;
}

static void at_check_hit_chance(AutotestCheck check)
{
    if (at_target == NULL || critter_is_dead(at_target) || rt_get_lock() != at_target) {
        return;
    }
    char note[240];
    bool matches = at_indicator_matches(at_target, note, sizeof(note));
    at_check(check, matches, "%s", note);
}

// One weapon kind: equip, approach a hostile, single press, then hold.
static bool step_attack_kind(unsigned int elapsed, bool first, bool timedOut)
{
    static const AutotestCheck checks[] = { C_UNARMED, C_MELEE, C_SINGLE, C_BURST, C_THROW };
    static const AutotestCheck hitChecks[] = { C_HIT_UNARMED, C_HIT_MELEE, C_HIT_GUN, C_HIT_GUN, C_HIT_THROW };
    static const char* shots[] = { "10_unarmed", "11_melee", "12_single", "13_burst", "13_throw" };
    unsigned int now = SDL_GetTicks();

    if (first) {
        at_phase = 0;
        at_retries = 0;
        at_target = NULL;
        at_phase_start = now;
        at_release_all();
        if (rt_get_lock() != NULL) {
            rt_debug_set_lock(NULL);
        }
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_restore_toughness();
        at_check(at_phase >= 4 && at_kind == AT_BURST ? C_AUTO : checks[at_kind], false, "timed out in phase %d", at_phase);
        return true;
    }

    switch (at_phase) {
    case 0:
        // Equip and pick the mode (equipping animates; wait until idle).
        if (at_kind_ready()) {
            at_log("attack test %d: weapon ready (anim %d)", at_kind, at_current_anim());
            at_phase = 1;
            at_phase_start = now;
        } else if (now - at_phase_start > 10000) {
            at_check(checks[at_kind], false, "could not prepare the weapon (anim %d)", at_current_anim());
            return true;
        }
        break;
    case 1:
        if (!at_prepare_target()) {
            break;
        }
        at_phase = 10;
        break;
    case 10:
        // One tick later the indicator shows the locked target.
        at_check_hit_chance(hitChecks[at_kind]);
        if (at_kind == AT_SINGLE || at_kind == AT_BURST) {
            at_screenshot(at_kind == AT_SINGLE ? "12_indicator_gun" : "13_indicator_burst");
        }
        at_counter_before = at_kind_counter();
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 2;
        at_phase_start = now;
        break;
    case 2: {
        // Single press: exactly one attack of this kind.
        if (now - at_phase_start < 2500 || (rt_get_pending_count() > 0 && now - at_phase_start < 6000)) {
            break;
        }

        int delta = at_kind_counter() - at_counter_before;
        if (delta == 0 && at_retries < 3) {
            // The press could not start (target died, got out of reach...).
            at_log("attack test %d: press did not attack (reject reason %d), retrying", at_kind, rt_get_stats()->lastRejectReason);
            at_retries++;
            at_target = NULL;
            at_phase = 1;
            break;
        }

        const RtStats* stats = rt_get_stats();
        if (at_kind == AT_SINGLE) {
            at_check(checks[at_kind], delta == 1 && stats->lastPlayerAmmoUsed == 1,
                "press -> %d attack(s), rounds used %d", delta, stats->lastPlayerAmmoUsed);
        } else if (at_kind == AT_BURST) {
            at_check(checks[at_kind], delta == 1 && stats->lastPlayerRounds > 1 && stats->lastPlayerAmmoUsed == stats->lastPlayerRounds,
                "press -> %d burst(s) of %d rounds, ammo used %d", delta, stats->lastPlayerRounds, stats->lastPlayerAmmoUsed);
        } else if (at_kind == AT_THROW) {
            at_kind_ready();
            int left = at_throw_weapon != NULL ? item_count(obj_dude, at_throw_weapon) : 0;
            at_check(checks[at_kind], delta == 1, "press -> %d throw(s), %d left in inventory", delta, left);
        } else {
            at_check(checks[at_kind], delta == 1, "press -> %d attack(s)", delta);
        }
        at_screenshot(shots[at_kind]);
        at_phase = 3;
        at_phase_start = now;
        break;
    }
    case 3:
        // Hold test needs a live target in reach.
        if (at_kind == AT_THROW && !at_kind_ready()) {
            break;
        }
        if (!at_prepare_target()) {
            break;
        }
        at_phase = 11;
        break;
    case 11:
        // One tick after locking, as in phase 10.
        at_check_hit_chance(hitChecks[at_kind]);
        at_counter_before = at_kind_counter();
        if (at_kind == AT_BURST) {
            // Fixture: a 6 HP rat dies to the first burst (which correctly
            // interrupts automatic fire), so toughen the target. Critter stats
            // live on the prototype shared by all rats, so the bonus is
            // removed again when this test ends.
            stat_set_bonus(at_target, STAT_MAXIMUM_HIT_POINTS, 300);
            critter_adjust_hits(at_target, 300);
            at_toughened = at_target;
            at_log("harness: %s toughened to %d HP for the automatic-fire test", critter_name(at_target), critter_get_hits(at_target));
        }
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        at_phase = 4;
        at_phase_start = now;
        break;
    case 4:
        // Hold RT.
        at_heal();
        if (now - at_phase_start < (at_kind == AT_BURST ? 6000u : 3000u)) {
            break;
        }
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = false;
        at_count_a = at_kind_counter() - at_counter_before;
        at_phase = 5;
        at_phase_start = now;
        break;
    case 5: {
        // After release: nothing new may start.
        if (now - at_phase_start < 2500) {
            break;
        }

        int afterRelease = at_kind_counter() - at_counter_before - at_count_a;
        if (at_kind == AT_BURST) {
            at_check(C_AUTO, at_count_a >= 2 && afterRelease == 0,
                "held 6 s -> %d bursts, %d after release", at_count_a, afterRelease);
            at_phase = 6;
            at_phase_start = now;
            break;
        } else if (at_count_a == 0 && at_retries < 3) {
            // Lost the target before the first attack; retry the hold.
            at_retries++;
            at_target = NULL;
            at_phase = 3;
            break;
        } else {
            at_check(checks[at_kind], at_count_a == 1 && afterRelease == 0,
                "held 3 s -> %d attack(s), %d after release", at_count_a, afterRelease);
        }
        return true;
    }
    case 6:
        // Burst only: a press held for 400 ms (past the hold threshold, but
        // shorter than the weapon's cycle) is still exactly one burst.
        if (!at_prepare_target() || !at_kind_ready()) {
            break;
        }
        at_counter_before = at_kind_counter();
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        at_phase = 7;
        at_phase_start = now;
        break;
    case 7:
        if (now - at_phase_start < 400) {
            break;
        }
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = false;
        at_phase = 8;
        at_phase_start = now;
        break;
    case 8:
        if (now - at_phase_start < 3000) {
            break;
        }
        at_check(C_TAP_HOLD, at_kind_counter() - at_counter_before == 1,
            "RT held 400 ms -> %d burst(s)", at_kind_counter() - at_counter_before);
        at_restore_toughness();
        return true;
    }
    return false;
}

static bool step_unarmed(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        // Rats left from the slot and switching tests.
        at_clear_hostiles();
    }
    at_kind = AT_UNARMED;
    return step_attack_kind(elapsed, first, timedOut);
}

static bool step_melee(unsigned int elapsed, bool first, bool timedOut)
{
    at_kind = AT_MELEE;
    return step_attack_kind(elapsed, first, timedOut);
}

static bool step_single(unsigned int elapsed, bool first, bool timedOut)
{
    at_kind = AT_SINGLE;
    return step_attack_kind(elapsed, first, timedOut);
}

static bool step_burst(unsigned int elapsed, bool first, bool timedOut)
{
    at_kind = AT_BURST;
    return step_attack_kind(elapsed, first, timedOut);
}

static bool step_throw(unsigned int elapsed, bool first, bool timedOut)
{
    at_kind = AT_THROW;
    return step_attack_kind(elapsed, first, timedOut);
}

// Held automatic fire stops when the locked target dies; it does not move on
// to another enemy, and nothing is locked automatically.
static bool step_no_transfer(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        at_target = NULL;
        at_release_all();
        rt_debug_set_lock(NULL);
        at_kind = AT_BURST;
        return false;
    }

    at_heal();
    switch (at_phase) {
    case 0:
        if (!at_kind_ready()) {
            break;
        }
        // A fresh, weak target plus another enemy nearby.
        at_target = at_spawn_rat(2, 3, true);
        at_spawn_rat(2, 4, true);
        if (at_target == NULL) {
            if (timedOut) {
                return true;
            }
            break;
        }
        rt_debug_set_lock(at_target);
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
        if (at_burst_weapon != NULL && item_w_curr_ammo(at_burst_weapon) < item_w_max_ammo(at_burst_weapon)) {
            item_w_set_curr_ammo(at_burst_weapon, item_w_max_ammo(at_burst_weapon));
        }
        if (rt_get_lock() == at_target && !critter_is_dead(at_target)) {
            if (now - at_time_a > 20000) {
                at_log("no-transfer test: target survived 20 s");
                at_release_all();
                return true;
            }
            break;
        }
        // The lock is gone (target died): keep holding RT.
        at_count_a = rt_get_stats()->playerAttacks;
        at_obj_a = at_nearest(at_filter_hostile, 25);
        at_log("no-transfer test: target %s, lock now %s, other hostile at %d tiles",
            critter_is_dead(at_target) ? "died" : "invalid",
            rt_get_lock() != NULL ? critter_name(rt_get_lock()) : "(none)",
            at_obj_a != NULL ? obj_dist(obj_dude, at_obj_a) : -1);
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        if (now - at_time_a < 2500) {
            break;
        }
        at_release_all();
        at_check(C_NO_TRANSFER, rt_get_lock() == NULL && rt_get_stats()->playerAttacks == at_count_a && at_obj_a != NULL,
            "RT held 2.5 s after the kill: attacks=%d, new lock=%s, another hostile present=%d",
            rt_get_stats()->playerAttacks - at_count_a, rt_get_lock() != NULL ? critter_name(rt_get_lock()) : "(none)", at_obj_a != NULL);
        return true;
    }

    if (timedOut) {
        at_release_all();
        return true;
    }
    return false;
}

static bool step_empty(unsigned int elapsed, bool first, bool timedOut)
{
    Object* weapon = at_find_inventory("10mm Pistol");
    if (first) {
        at_phase = 0;
        at_target = NULL;
        if (weapon == NULL) {
            at_check(C_EMPTY, false, "no ranged weapon");
            return true;
        }
        return false;
    }

    unsigned int now = SDL_GetTicks();
    at_heal();
    switch (at_phase) {
    case 0:
        if (!at_wield(weapon) || elapsed < 500) {
            break;
        }
        at_kind = AT_SINGLE;
        if (!at_prepare_target()) {
            break;
        }
        item_w_set_curr_ammo(weapon, 0);
        intface_update_ammo_lights();
        at_count_a = rt_get_stats()->playerAttacks;
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
        if (now - at_time_a < 1000) {
            break;
        }
        at_check(C_EMPTY,
            rt_get_stats()->playerAttacks == at_count_a && rt_get_stats()->lastRejectReason == COMBAT_BAD_SHOT_NO_AMMO
                && strcmp(rt_get_indicator()->text, "No ammo") == 0,
            "attacks started=%d, reject reason=%d (no ammo=%d), indicator '%s'",
            rt_get_stats()->playerAttacks - at_count_a,
            rt_get_stats()->lastRejectReason,
            COMBAT_BAD_SHOT_NO_AMMO,
            rt_get_indicator()->text);
        return true;
    }
    return false;
}

// Reload: holster, ammo, draw. The weapon stays in hand, ammunition is
// conserved and moves once, and RT can't fire meanwhile.
static bool at_reload_saw_put_away;
static bool at_reload_saw_take_out;
static bool at_reload_left_hand;
static int at_reload_attacks_during;
static int at_reload_rounds_before;
static int at_reload_loaded_before;
static int at_reload_transfers_before;
static int at_reload_started_before;

static void at_reload_observe(Object* weapon)
{
    int anim = FID_ANIM_TYPE(obj_dude->fid);
    if (anim == ANIM_PUT_AWAY) {
        at_reload_saw_put_away = true;
    } else if (anim == ANIM_TAKE_OUT) {
        at_reload_saw_take_out = true;
    }
    if (inven_right_hand(obj_dude) != weapon && inven_left_hand(obj_dude) != weapon) {
        at_reload_left_hand = true;
    }
}

static void at_report_reload_fallbacks()
{
    // Weapons using ammunition whose holster or draw animation is missing for
    // the player's current appearance (those reload with the timed fallback).
    char missing[600];
    missing[0] = 0;
    int checked = 0;
    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_ITEM << 24) | index;
        Proto* proto;
        if (proto_ptr(pid, &proto) == -1 || proto->item.type != ITEM_TYPE_WEAPON || proto->item.data.weapon.ammoCapacity <= 0) {
            continue;
        }
        checked++;
        int code = proto->item.data.weapon.animationCode;
        int putAway = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_PUT_AWAY, code, 1);
        int takeOut = art_id(OBJ_TYPE_CRITTER, obj_dude->fid & 0xFFF, ANIM_TAKE_OUT, code, 1);
        if (code == 0 || !art_exists(putAway) || !art_exists(takeOut)) {
            char name[96];
            snprintf(name, sizeof(name), " %s (0x%08X, weapon code %d)", proto_name(pid), pid, code);
            SDL_strlcat(missing, name, sizeof(missing));
        }
    }
    at_log("reload: %d ammo weapons checked for holster/draw art (current appearance); missing:%s",
        checked, missing[0] != 0 ? missing : " none");
}

static bool step_reload(unsigned int elapsed, bool first, bool timedOut)
{
    Object* weapon = at_find_inventory("10mm Pistol");
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        at_kind = AT_SINGLE;
        at_target = NULL;
        if (weapon == NULL) {
            at_check(C_RELOAD_ANIM, false, "no pistol");
            return true;
        }
        at_report_reload_fallbacks();
        return false;
    }

    at_heal();
    switch (at_phase) {
    case 0:
        if (!at_wield(weapon) || anim_busy(obj_dude)) {
            break;
        }
        // No enemies around (their hits would interrupt the reload; that case
        // is tested below); a calm rat is locked for the RT press.
        at_clear_hostiles();
        at_target = at_spawn_rat(3, 5, false);
        if (at_target != NULL) {
            rt_debug_set_lock(at_target);
        }
        if (at_ammo_rounds(weapon) < 24) {
            // Fixture: make sure there is spare ammunition.
            Proto* proto;
            Object* ammo;
            Rect rect;
            if (proto_ptr(weapon->pid, &proto) != -1 && obj_pid_new(&ammo, proto->item.data.weapon.ammoTypePid) != -1
                && item_add_force(obj_dude, ammo, 2) == 0) {
                obj_disconnect(ammo, &rect);
            }
        }
        item_w_set_curr_ammo(weapon, 0);
        intface_update_ammo_lights();
        at_reload_saw_put_away = false;
        at_reload_saw_take_out = false;
        at_reload_left_hand = false;
        at_reload_attacks_during = 0;
        at_reload_rounds_before = at_ammo_rounds(weapon);
        at_reload_loaded_before = item_w_curr_ammo(weapon);
        at_reload_transfers_before = rt_get_stats()->reloadAmmoTransfers;
        at_count_a = rt_get_stats()->reloadsCompleted;
        at_count_b = rt_get_stats()->playerAttacks;
        at_reload_started_before = rt_get_stats()->reloadsStarted;
        at_flag_a = false;
        at_press(GAMEPAD_BUTTON_X);
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
        // Mid-reload: press RT; nothing may fire before the reload is done.
        at_reload_observe(weapon);
        if (!at_flag_a && rt_is_reloading()) {
            at_flag_a = true;
            at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        }
        if (rt_is_reloading()) {
            at_reload_attacks_during = rt_get_stats()->playerAttacks - at_count_b;
        }
        // (The press may wait for an enemy attack on the player to end.)
        if (rt_get_stats()->reloadsStarted > at_reload_started_before && !rt_is_reloading()) {
            at_screenshot("23_after_reload");
            int rounds = at_ammo_rounds(weapon);
            int loaded = item_w_curr_ammo(weapon);
            int transfers = rt_get_stats()->reloadAmmoTransfers - at_reload_transfers_before;
            // A buffered RT press may fire right after the reload; count its rounds.
            int fired = rt_get_stats()->playerAttacks - at_count_b - at_reload_attacks_during > 0 ? rt_get_stats()->lastPlayerAmmoUsed : 0;
            bool conserved = rounds + loaded + fired == at_reload_rounds_before + at_reload_loaded_before;
            at_check(C_RELOAD_ANIM, at_reload_saw_put_away && at_reload_saw_take_out && !at_reload_left_hand
                    && rt_get_stats()->reloadsCompleted == at_count_a + 1,
                "holster seen=%d, draw seen=%d, weapon left the hand=%d, took %u ms",
                at_reload_saw_put_away, at_reload_saw_take_out, at_reload_left_hand, now - at_time_a);
            at_check(C_RELOAD_AMMO, transfers == 1 && conserved && loaded > 0,
                "transfers=%d, loaded %d -> %d, spare rounds %d -> %d (conserved=%d)",
                transfers, at_reload_loaded_before, loaded, at_reload_rounds_before, rounds, conserved);
            at_check(C_RELOAD_BLOCKS_FIRE, at_reload_attacks_during == 0,
                "attacks started while reloading=%d", at_reload_attacks_during);
            at_phase = 2;
            at_time_a = now;
        } else if (now - at_time_a > 6000) {
            at_check(C_RELOAD_ANIM, false, "reload did not finish");
            return true;
        }
        break;
    case 2:
        // Interrupted reload: empty again, start, cut the holster short.
        if (now - at_time_a < 2000 || anim_busy(obj_dude) || rt_get_pending_count() > 0) {
            break;
        }
        rt_debug_set_lock(NULL);
        // Wandering rats would cut this reload short themselves.
        at_clear_hostiles();
        item_w_set_curr_ammo(weapon, 0);
        intface_update_ammo_lights();
        at_reload_rounds_before = at_ammo_rounds(weapon);
        at_count_a = rt_get_stats()->reloadsInterrupted;
        at_count_b = rt_get_stats()->reloadAmmoTransfers;
        at_reload_started_before = rt_get_stats()->reloadsStarted;
        at_press(GAMEPAD_BUTTON_X);
        at_phase = 3;
        at_time_a = now;
        break;
    case 3:
        // 150 ms into the holster animation.
        if (rt_get_stats()->reloadsStarted == at_reload_started_before) {
            if (now - at_time_a > 3000) {
                at_check(C_RELOAD_INTERRUPT, false, "reload did not start");
                return true;
            }
            at_time_a = now;
            break;
        }
        if (now - at_time_a < 150) {
            break;
        }
        if (!rt_is_reloading()) {
            at_check(C_RELOAD_INTERRUPT, false, "reload not running to interrupt");
            return true;
        }
        // Harness: stands in for an enemy hit cutting the animation short.
        at_log("harness: interrupting the reload animation (anim %d)", FID_ANIM_TYPE(obj_dude->fid));
        register_clear(obj_dude);
        at_phase = 4;
        at_time_a = now;
        break;
    case 4:
        if (now - at_time_a < 500) {
            break;
        }
        {
            bool interrupted = rt_get_stats()->reloadsInterrupted == at_count_a + 1;
            bool untouched = item_w_curr_ammo(weapon) == 0
                && at_ammo_rounds(weapon) == at_reload_rounds_before
                && rt_get_stats()->reloadAmmoTransfers == at_count_b;
            at_flag_a = interrupted && untouched;
            at_log("reload interrupt: interrupted=%d, ammo untouched=%d (loaded %d, spare %d -> %d)",
                interrupted, untouched, item_w_curr_ammo(weapon), at_reload_rounds_before, at_ammo_rounds(weapon));
        }
        // Reload again: completes, ammunition moves once.
        at_clear_hostiles();
        at_count_b = rt_get_stats()->reloadAmmoTransfers;
        at_count_a = rt_get_stats()->reloadsCompleted;
        at_press(GAMEPAD_BUTTON_X);
        at_phase = 5;
        at_time_a = now;
        break;
    case 5:
        if (rt_get_stats()->reloadsCompleted == at_count_a) {
            if (now - at_time_a > 6000) {
                at_check(C_RELOAD_INTERRUPT, false, "second reload did not finish");
                return true;
            }
            break;
        }
        {
            bool conserved = item_w_curr_ammo(weapon) + at_ammo_rounds(weapon) == at_reload_rounds_before;
            int transfers = rt_get_stats()->reloadAmmoTransfers - at_count_b;
            at_check(C_RELOAD_INTERRUPT, at_flag_a && conserved && transfers == 1 && item_w_curr_ammo(weapon) > 0,
                "interrupted reload left ammo untouched=%d; reload after it: transfers=%d, loaded %d, conserved=%d",
                at_flag_a, transfers, item_w_curr_ammo(weapon), conserved);
        }
        return true;
    }

    if (timedOut) {
        at_check(C_RELOAD_ANIM, false, "timed out in phase %d", at_phase);
        return true;
    }
    return false;
}

static bool at_filter_far_enemy(Object* critter)
{
    int distance = obj_dist(obj_dude, critter);
    return at_filter_enemy_team(critter) && distance >= 3 && distance <= 15 && at_filter_lock_candidate(critter);
}

static bool step_range(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        return false;
    }

    at_heal();
    switch (at_phase) {
    case 0:
        if (!at_wield(at_find_inventory("Knife")) || elapsed < 600) {
            break;
        }
        at_target = at_nearest(at_filter_far_enemy, 15);
        if (at_target == NULL) {
            at_target = at_spawn_rat(4, 6, false);
        }
        if (at_target == NULL) {
            if (timedOut) {
                at_log("range test: no critter 3-15 tiles away");
                return true;
            }
            break;
        }
        rt_debug_set_lock(at_target);
        at_phase = 1;
        break;
    case 1:
        at_check(C_HIT_RANGE,
            strcmp(rt_get_indicator()->text, "Out of range") == 0 && rt_get_indicator()->toHit == -1,
            "knife, target at %d tiles: indicator '%s'", obj_dist(obj_dude, at_target), rt_get_indicator()->text);
        at_tile_a = obj_dude->tile;
        at_count_a = rt_get_stats()->playerAttacks;
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        if (now - at_time_a < 1500) {
            break;
        }
        at_check(C_RANGE,
            rt_get_stats()->playerAttacks == at_count_a
                && rt_get_stats()->lastRejectReason == COMBAT_BAD_SHOT_OUT_OF_RANGE
                && obj_dude->tile == at_tile_a,
            "target at %d tiles: attacks=%d reason=%d moved=%d",
            obj_dist(obj_dude, at_target),
            rt_get_stats()->playerAttacks - at_count_a,
            rt_get_stats()->lastRejectReason,
            obj_dude->tile != at_tile_a);
        rt_debug_set_lock(NULL);
        return true;
    }
    return false;
}

static int at_blocked_range = 20;

static bool at_filter_blocked(Object* critter)
{
    return at_filter_enemy_team(critter)
        && obj_dist(obj_dude, critter) <= at_blocked_range
        && at_filter_lock_candidate(critter)
        && combat_is_shot_blocked(obj_dude, obj_dude->tile, critter->tile, critter, NULL);
}

static bool step_blocked(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    Object* pistol = at_find_inventory("10mm Pistol");
    if (first) {
        at_phase = 0;
        return false;
    }

    at_heal();
    switch (at_phase) {
    case 0:
        if (!at_wield(pistol) || elapsed < 600) {
            break;
        }
        if (item_w_curr_ammo(pistol) == 0) {
            item_w_set_curr_ammo(pistol, item_w_max_ammo(pistol));
        }
        {
            int hitMode = at_current_hit_mode();
            at_blocked_range = hitMode != -1 ? item_w_range(obj_dude, hitMode) : 20;
        }
        at_target = at_nearest(at_filter_blocked, at_blocked_range);
        if (at_target == NULL) {
            // Fixture: a rat placed behind cover.
            at_target = at_spawn_rat_at(at_covered_tile(at_blocked_range < 12 ? at_blocked_range : 12), false);
        }
        if (at_target == NULL) {
            at_log("blocked test: no covered tile within weapon range (%d tiles)", at_blocked_range);
            return true;
        }
        rt_debug_set_lock(at_target);
        at_phase = 1;
        break;
    case 1:
        at_check(C_HIT_BLOCKED, strcmp(rt_get_indicator()->text, "Blocked") == 0 && rt_get_indicator()->toHit == -1,
            "pistol, %s behind cover: indicator '%s'", critter_name(at_target), rt_get_indicator()->text);
        at_screenshot("24_blocked");
        at_count_a = rt_get_stats()->playerAttacks;
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        if (now - at_time_a < 1200) {
            break;
        }
        at_check(C_BLOCKED,
            rt_get_stats()->playerAttacks == at_count_a && rt_get_stats()->lastRejectReason == COMBAT_BAD_SHOT_AIM_BLOCKED,
            "%s at %d tiles behind cover: attacks=%d reason=%d",
            critter_name(at_target),
            obj_dist(obj_dude, at_target),
            rt_get_stats()->playerAttacks - at_count_a,
            rt_get_stats()->lastRejectReason);
        rt_debug_set_lock(NULL);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: companion, interaction, interruptions
// -----------------------------------------------------------------------------

static bool step_companion(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        // Test fixture: a rat joins the player's team to act as a companion.
        at_companion = at_spawn_rat(2, 3, false);
        if (at_companion == NULL) {
            at_check(C_COMPANION, false, "no critter available to act as a companion");
            return true;
        }

        combatai_switch_team(at_companion, obj_dude->data.critter.combat.team);
        critter_set_who_hit_me(at_companion, NULL);
        at_log("harness: %s at %d tiles joins the player team as a test companion",
            critter_name(at_companion), obj_dist(obj_dude, at_companion));
        at_count_a = rt_get_stats()->companionAttacks;
        at_flag_a = false;
        rt_debug_set_lock(NULL);
        return false;
    }

    at_heal();

    if (!at_flag_a) {
        // Go unarmed so the player keeps fighting at close quarters.
        at_flag_a = at_unarmed() || elapsed > 3000;
        if (at_flag_a) {
            // Enemies for the companion to fight.
            at_spawn_rat(2, 4, true);
            at_spawn_rat(2, 4, true);
        }
    }

    // The player keeps fighting meanwhile: lock and press RT now and then.
    if (elapsed % 1400 < 50) {
        if (rt_get_lock() == NULL) {
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        } else {
            at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        }
    }

    // Never lock the companion.
    if (rt_get_lock() == at_companion) {
        at_check(C_LOCK_NOT_FRIENDLY, false, "the companion was locked");
    }

    int companionAttacks = rt_get_stats()->companionAttacks - at_count_a;
    if ((companionAttacks >= 2 && rt_get_stats()->maxConcurrentAttackers >= 2) || timedOut) {
        const RtStats* stats = rt_get_stats();
        at_check(C_COMPANION, companionAttacks > 0, "companion attacks=%d (companion %s)",
            companionAttacks, critter_is_dead(at_companion) ? "died" : "alive");
        at_check(C_LOCK_NOT_FRIENDLY, true, "LT never locked the companion during the fight");
        at_check(C_SIMULTANEOUS, stats->maxConcurrentAttackers >= 2,
            "max attacks in flight=%d by %d different attackers",
            stats->maxConcurrentAttacks, stats->maxConcurrentAttackers);
        at_screenshot("14_companion");
        return true;
    }
    return false;
}

static bool step_combat_summary(unsigned int elapsed, bool first, bool timedOut)
{
    // Let everything in flight resolve first.
    if (rt_get_pending_count() > 0 && !timedOut) {
        return false;
    }

    const RtStats* stats = rt_get_stats();
    at_check(C_DAMAGE_ONCE, stats->damageMismatches == 0 && stats->attacksResolved + stats->attacksDropped == stats->attacksStarted,
        "attacks started=%d resolved=%d dropped (participant removed)=%d, damage mismatches=%d",
        stats->attacksStarted, stats->attacksResolved, stats->attacksDropped, stats->damageMismatches);
    at_check(C_ENEMY_AI, stats->attacksStarted - stats->playerAttacks - stats->companionAttacks > 0,
        "enemy attacks=%d, damage to player=%d",
        stats->attacksStarted - stats->playerAttacks - stats->companionAttacks, stats->damageToPlayer);
    return true;
}

// Removes every living rat (fixture cleanup before the quiet tests).
// Removes living rats within `radius` of the player, and any rat engaged
// with the player wherever it is (out-of-combat fixtures). Farther calm
// rats stay for the steps that need one.
static int at_clear_rats_near(int radius)
{
    Object** list;
    int count = obj_create_list(-1, obj_dude->elevation, OBJ_TYPE_CRITTER, &list);
    int removed = 0;
    for (int index = 0; index < count; index++) {
        Object* critter = list[index];
        if (critter != obj_dude && !critter_is_dead(critter) && at_filter_enemy_team(critter)
            && (obj_dist(obj_dude, critter) <= radius || rt_is_hostile(critter))) {
            if (rt_get_lock() == critter) {
                rt_debug_set_lock(NULL);
            }
            Rect rect;
            anim_rt_end_all(critter);
            obj_erase_object(critter, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            removed++;
        }
    }
    if (count > 0) {
        obj_delete_list(list);
    }
    if (removed != 0) {
        at_log("harness: removed %d rats near the player or engaged", removed);
    }
    return removed;
}

static void at_clear_hostiles()
{
    Object** list;
    int count = obj_create_list(-1, obj_dude->elevation, OBJ_TYPE_CRITTER, &list);
    int removed = 0;
    for (int index = 0; index < count; index++) {
        Object* critter = list[index];
        if (critter != obj_dude && !critter_is_dead(critter) && at_filter_enemy_team(critter)) {
            Rect rect;
            anim_rt_end_all(critter);
            obj_erase_object(critter, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            removed++;
        }
    }
    if (count > 0) {
        obj_delete_list(list);
    }
    rt_debug_set_lock(NULL);
    at_log("harness: removed %d living rats", removed);
}

static bool step_interact(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_phase = 0;
        at_obj_a = NULL;
        at_clear_hostiles();

        // The nearest corpse within 10 tiles.
        Object** list;
        int count = obj_create_list(-1, obj_dude->elevation, OBJ_TYPE_CRITTER, &list);
        int best = 11;
        for (int index = 0; index < count; index++) {
            if (list[index] != obj_dude && critter_is_dead(list[index]) && obj_dist(obj_dude, list[index]) < best) {
                best = obj_dist(obj_dude, list[index]);
                at_obj_a = list[index];
            }
        }
        if (count > 0) {
            obj_delete_list(list);
        }

        if (at_obj_a == NULL) {
            at_log("interact: no corpse nearby");
            return true;
        }
        return false;
    }

    switch (at_phase) {
    case 0:
        at_heal();
        if (obj_dist(obj_dude, at_obj_a) > 1 && elapsed < 10000) {
            at_approach(at_obj_a, 1);
            break;
        }
        if (anim_busy(obj_dude)) {
            break;
        }
        // Face it so it is what A picks.
        {
            Rect rect;
            if (obj_set_rotation(obj_dude, tile_dir(obj_dude->tile, at_obj_a->tile), &rect) == 0) {
                tile_refresh_rect(&rect, obj_dude->elevation);
            }
        }
        at_phase = 1;
        break;
    case 1:
        at_log("interact: A target is %s", rt_get_interact_target() != NULL ? object_name(rt_get_interact_target()) : "(none)");
        at_expect_menu("15_loot", NULL);
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 2;
        at_time_a = SDL_GetTicks();
        break;
    case 2:
        if ((at_menu_seen && SDL_GetTicks() - at_time_a > 300) || timedOut) {
            at_check(C_INTERACT, at_menu_seen && at_menu_done, "loot UI opened=%d closed with B=%d", at_menu_seen, at_menu_done);
            at_menu_expected = false;
            return true;
        }
        break;
    }
    return false;
}

// Contextual skills on a fixture: a locked door with the game's generic door
// script (Door.int, which picks locks with roll_vs_skill).
static Object* at_door = NULL;
static int at_door_script = -1;
static int at_saved_lockpick = 0;

// Door.int records a picked lock in its local variable 0 (its other
// procedures then let the door open), rather than calling obj_unlock.
static bool at_door_picked()
{
    ProgramValue value;
    if (at_door == NULL || at_door->sid == -1 || scr_get_local_var(at_door->sid, 0, value) == -1) {
        return false;
    }
    return (value.opcode & VALUE_TYPE_MASK) == VALUE_TYPE_INT && value.integerValue != 0;
}

static void at_door_reset()
{
    ProgramValue value;
    value.opcode = VALUE_TYPE_INT;
    value.integerValue = 0;
    if (at_door != NULL && at_door->sid != -1) {
        scr_set_local_var(at_door->sid, 0, value);
    }
    obj_lock(at_door);
}

static void at_set_lockpick_points(int points)
{
    Proto* proto;
    if (proto_ptr(obj_dude->pid, &proto) != -1) {
        proto->critter.data.skills[SKILL_LOCKPICK] = points;
    }
}

// Harness: moves the player to a free spot with nothing to interact with
// within 4 tiles (so the door fixture is what A picks).
static bool at_move_to_clear_spot()
{
    for (int distance = 0; distance <= 15; distance++) {
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            int corner = distance == 0 ? obj_dude->tile : tile_num_in_direction(obj_dude->tile, rotation, distance);
            int steps = distance == 0 ? 1 : distance;
            for (int step = 0; step < steps && corner != -1; step++) {
                int tile = distance == 0 ? corner : tile_num_in_direction(corner, (rotation + 2) % ROTATION_COUNT, step);
                if (tile == -1 || obj_blocking_at(obj_dude, tile, obj_dude->elevation) != NULL) {
                    continue;
                }

                bool clear = true;
                for (Object* obj = obj_find_first_at(obj_dude->elevation); obj != NULL && clear; obj = obj_find_next_at()) {
                    int type = FID_TYPE(obj->fid);
                    if (obj == obj_dude || (obj->flags & OBJECT_HIDDEN) != 0
                        || (type != OBJ_TYPE_ITEM && type != OBJ_TYPE_CRITTER && type != OBJ_TYPE_SCENERY)) {
                        continue;
                    }
                    if (tile_dist(tile, obj->tile) <= 4 && rt_context_is_interactable(obj)) {
                        clear = false;
                    }
                }
                if (!clear) {
                    continue;
                }

                if (tile != obj_dude->tile) {
                    Rect rect;
                    obj_move_to_tile(obj_dude, tile, obj_dude->elevation, &rect);
                    tile_set_center(tile, TILE_SET_CENTER_REFRESH_WINDOW);
                    at_log("harness: player moved %d tiles to a clear spot", distance);
                }
                return true;
            }
        }
    }
    return false;
}

static Object* at_create_locked_door()
{
    // A free tile next to the player.
    int rotation = -1;
    int tile = -1;
    for (int candidate = 0; candidate < ROTATION_COUNT; candidate++) {
        int next = tile_num_in_direction(obj_dude->tile, candidate, 1);
        if (next != -1 && obj_blocking_at(NULL, next, obj_dude->elevation) == NULL) {
            rotation = candidate;
            tile = next;
            break;
        }
    }
    if (tile == -1) {
        return NULL;
    }

    for (int index = 1; index < 3000; index++) {
        int pid = (OBJ_TYPE_SCENERY << 24) | index;
        Proto* proto;
        if (proto_ptr(pid, &proto) == -1 || proto->scenery.type != SCENERY_TYPE_DOOR) {
            continue;
        }

        Object* door;
        if (obj_pid_new(&door, pid) == -1) {
            continue;
        }

        Rect rect;
        obj_move_to_tile(door, tile, obj_dude->elevation, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        obj_lock(door);
        if (!obj_is_locked(door)) {
            obj_erase_object(door, &rect);
            continue;
        }

        if (obj_set_rotation(obj_dude, rotation, &rect) == 0) {
            tile_refresh_rect(&rect, obj_dude->elevation);
        }
        at_log("harness: locked door %s (pid 0x%08X) placed next to the player", object_name(door), pid);
        return door;
    }
    return NULL;
}

// The skill use (hands animation, script) has finished.
static bool at_idle_for(unsigned int since, unsigned int minimum)
{
    return SDL_GetTicks() - since > minimum && !anim_busy(obj_dude);
}

static bool at_menu_pick_script(unsigned int elapsed)
{
    if (elapsed > 500 && at_count_b == 0) {
        at_press(GAMEPAD_BUTTON_DPAD_DOWN);
        at_count_b = 1;
    }
    if (elapsed > 1100 && at_count_b == 1) {
        at_press(GAMEPAD_BUTTON_A);
        at_count_b = 2;
        return true;
    }
    return false;
}

static bool at_menu_cancel_script(unsigned int elapsed)
{
    if (elapsed > 800) {
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }
    return false;
}

static void at_skills_cleanup()
{
    rt_context_set_menu_choice(-2);
    at_set_lockpick_points(at_saved_lockpick);
    if (at_door_script != -1) {
        rt_context_debug_override(at_door_script, NULL);
    }
    if (at_door != NULL) {
        Rect rect;
        obj_erase_object(at_door, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        at_door = NULL;
    }
    at_menu_expected = false;
}

// A fresh locked door with Door.int next to the player.
static bool at_new_door()
{
    if (at_door != NULL) {
        Rect rect;
        obj_erase_object(at_door, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        at_door = NULL;
    }

    at_door = at_create_locked_door();
    if (at_door == NULL) {
        return false;
    }
    obj_new_sid_inst(at_door, SCRIPT_TYPE_ITEM, at_door_script);
    return true;
}

static bool step_skills(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    const RtContextStats* stats = rt_context_get_stats();

    if (first) {
        at_phase = 0;
        at_retries = 0;
        at_release_all();
        rt_debug_set_lock(NULL);
        Proto* proto;
        if (proto_ptr(obj_dude->pid, &proto) != -1) {
            at_saved_lockpick = proto->critter.data.skills[SKILL_LOCKPICK];
        }
        // Nothing else to interact with nearby: the test companion and the
        // remaining rats go, the player moves to a clear spot.
        if (at_companion != NULL && !critter_is_dead(at_companion)) {
            Rect rect;
            anim_rt_end_all(at_companion);
            obj_erase_object(at_companion, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
        }
        at_companion = NULL;
        at_clear_hostiles();
        register_clear(obj_dude);
        if (!at_move_to_clear_spot()) {
            at_log("skills: no clear spot found");
        }
        at_door_script = at_find_script("Door.int");
        if (at_door_script == -1 || !at_new_door()) {
            at_log("skills: no door fixture (script=%d)", at_door_script);
            at_skills_cleanup();
            return true;
        }
        return false;
    }

    if (timedOut) {
        at_log("skills: timed out in phase %d", at_phase);
        at_skills_cleanup();
        return true;
    }

    at_heal();
    switch (at_phase) {
    case 0: {
        if (elapsed < 500) {
            break;
        }
        char actions[128];
        rt_context_describe(at_door, actions, sizeof(actions));
        at_log("skills: A target=%s, locked door actions: %s",
            rt_get_interact_target() != NULL ? object_name(rt_get_interact_target()) : "(none)", actions);
        if (rt_get_interact_target() != at_door) {
            at_log("skills: the door is not what A picks; abandoning");
            at_skills_cleanup();
            return true;
        }
        at_flag_b = SDL_strcasestr(actions, "Open") != NULL && SDL_strcasestr(actions, "Lockpick") != NULL
            && SDL_strcasestr(actions, "Steal") == NULL;

        // Success: high Lockpick. The locked door offers Open/Close and
        // Lockpick (menu); Lockpick is picked here (the gamepad-driven menu
        // is tested below).
        at_set_lockpick_points(300);
        rt_context_set_menu_choice(1);
        at_count_a = stats->skillUses;
        at_count_b = stats->menus;
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 1;
        at_time_a = now;
        break;
    }
    case 1:
        if (!at_idle_for(at_time_a, 2500)) {
            break;
        }
        if (!at_door_picked() && at_retries < 3) {
            // A roll can fail even with high skill; try again.
            at_retries++;
            at_log("skills: lock not picked after attempt %d, retrying", at_retries);
            at_press(GAMEPAD_BUTTON_A);
            at_time_a = now;
            break;
        }
        at_flag_a = at_door_picked() && stats->skillUses > at_count_a && stats->menus > at_count_b;
        at_log("skills: Lockpick %d%%: Door.int reports the lock picked=%d after %d attempt(s); engine lock flag still set=%d",
            skill_level(obj_dude, SKILL_LOCKPICK), at_door_picked(), at_retries + 1, obj_is_locked(at_door));

        // Then Open: the door's own use_p_proc unlocks and opens it.
        rt_context_set_menu_choice(0);
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        if (!at_idle_for(at_time_a, 2500)) {
            break;
        }
        at_check(C_SKILL_SUCCESS, at_flag_a && at_flag_b && !obj_is_locked(at_door) && obj_is_open(at_door),
            "locked door offered Open/Close + Lockpick=%d; Lockpick %d%% picked it (Door.int)=%d after %d attempt(s); then Open opened it=%d",
            at_flag_b, skill_level(obj_dude, SKILL_LOCKPICK), at_flag_a, at_retries + 1, obj_is_open(at_door));

        // Failed roll: a new locked door, negative skill.
        if (!at_new_door()) {
            at_skills_cleanup();
            return true;
        }
        at_set_lockpick_points(-50);
        rt_context_set_menu_choice(1);
        at_count_a = stats->skillUses;
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 3;
        at_time_a = now;
        break;
    case 3:
        if (!at_idle_for(at_time_a, 2500)) {
            break;
        }
        at_check(C_SKILL_ROLL_FAIL, !at_door_picked() && obj_is_locked(at_door) && stats->skillUses == at_count_a + 1,
            "Lockpick %d%%: attempted=%d, Door.int reports the lock picked=%d (the script's roll failed)",
            skill_level(obj_dude, SKILL_LOCKPICK), stats->skillUses - at_count_a, at_door_picked());

        // Fixed requirement: Fallout 1's own scripts use rolls only, so the
        // door's analysis is replaced with a fixed 80% requirement.
        {
            RtScriptSkills info;
            memset(&info, 0, sizeof(info));
            info.hasProc = true;
            for (int skill = 0; skill < SKILL_COUNT; skill++) {
                info.skills[skill].requirement = -1;
            }
            info.skills[SKILL_LOCKPICK].handled = true;
            info.skills[SKILL_LOCKPICK].requirement = 80;
            rt_context_debug_override(at_door_script, &info);
        }
        at_set_lockpick_points(-20);
        at_count_a = stats->skillUses;
        at_count_b = stats->skillBlocked;
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 4;
        at_time_a = now;
        break;
    case 4:
        if (now - at_time_a < 400) {
            break;
        }
        // Blocked right away: no hands animation, no script run.
        at_flag_a = stats->skillBlocked == at_count_b + 1 && stats->skillUses == at_count_a && !anim_busy(obj_dude) && !at_door_picked();
        at_log("skills: requirement 80%% with Lockpick %d%%: blocked=%d, attempted=%d",
            skill_level(obj_dude, SKILL_LOCKPICK), stats->skillBlocked - at_count_b, stats->skillUses - at_count_a);
        // Meeting it lets the attempt through.
        at_set_lockpick_points(300);
        at_count_a = stats->skillUses;
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 5;
        at_time_a = now;
        break;
    case 5:
        if (!at_idle_for(at_time_a, 2500)) {
            break;
        }
        at_check(C_SKILL_REQUIRED, at_flag_a && stats->skillUses == at_count_a + 1,
            "below 80%%: blocked without attempting=%d; at %d%%: attempted=%d",
            at_flag_a, skill_level(obj_dude, SKILL_LOCKPICK), stats->skillUses - at_count_a);
        rt_context_debug_override(at_door_script, NULL);

        // The menu with the controller: a new locked door, D-pad + A.
        if (!at_new_door()) {
            at_skills_cleanup();
            return true;
        }
        rt_context_set_menu_choice(-2);
        at_count_a = stats->menus;
        at_tile_a = stats->defaultActions + stats->skillUses;
        at_count_b = 0;
        at_expect_menu("25_skill_menu", at_menu_pick_script);
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 6;
        at_time_a = now;
        break;
    case 6:
        if (!at_menu_seen || !at_idle_for(at_time_a, 2500)) {
            if (now - at_time_a > 8000) {
                at_check(C_SKILL_MENU, false, "menu did not appear");
                at_skills_cleanup();
                return true;
            }
            break;
        }
        at_menu_expected = false;
        at_flag_a = stats->menus == at_count_a + 1 && stats->defaultActions + stats->skillUses == at_tile_a + 1;
        at_log("skills: menu shown=%d, D-pad + A ran an action=%d", stats->menus - at_count_a, stats->defaultActions + stats->skillUses - at_tile_a);
        // Again, and cancel with B.
        at_count_a = stats->menuCancels;
        at_tile_a = stats->defaultActions + stats->skillUses;
        at_expect_menu(NULL, at_menu_cancel_script);
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 7;
        at_time_a = now;
        break;
    case 7:
        if (!at_menu_seen || now - at_time_a < 1500) {
            if (now - at_time_a > 8000) {
                at_check(C_SKILL_MENU, false, "second menu did not appear");
                at_skills_cleanup();
                return true;
            }
            break;
        }
        {
            // An unlocked door has nothing to pick: just Open/Close, no menu.
            obj_unlock(at_door);
            char actions[128];
            rt_context_describe(at_door, actions, sizeof(actions));
            bool plain = SDL_strcasecmp(actions, "Open/Close") == 0;
            at_check(C_SKILL_MENU,
                at_flag_a && at_flag_b && plain && stats->menuCancels == at_count_a + 1 && stats->defaultActions + stats->skillUses == at_tile_a,
                "locked door: menu without Steal=%d, D-pad + A ran the choice=%d, B cancelled=%d without acting=%d; unlocked door offers '%s'",
                at_flag_b, at_flag_a, stats->menuCancels - at_count_a, stats->defaultActions + stats->skillUses == at_tile_a, actions);
        }
        at_skills_cleanup();
        return true;
    }
    return false;
}

static bool step_disconnect(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        at_release_all();
        return false;
    }

    switch (at_phase) {
    case 0:
        // Walk and hold fire, then pull the plug.
        if (!at_wield(at_burst_weapon) || elapsed < 600) {
            break;
        }
        if (at_burst_weapon != NULL) {
            item_w_set_curr_ammo(at_burst_weapon, item_w_max_ammo(at_burst_weapon));
        }
        at_heal();
        at_stick_open_direction();
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
        at_stick_open_direction();
        if (now - at_time_a < 800) {
            break;
        }
        at_count_a = gamepad_get_interruption_count();
        at_pad.connected = false;
        at_log("disconnect: controller removed while moving and holding RT");
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        if (now - at_time_a < 400) {
            break;
        }
        // Measure from here: nothing new may start while disconnected.
        at_count_b = rt_get_stats()->playerAttacks;
        at_tile_a = rt_get_stats()->moveRequests;
        at_flag_a = false;
        at_phase = 3;
        at_time_a = now;
        break;
    case 3:
        if (anim_busy(obj_dude) && FID_ANIM_TYPE(obj_dude->fid) == ANIM_WALK) {
            at_flag_a = true;
        }
        if (now - at_time_a < 1200) {
            break;
        }
        at_flag_b = rt_get_stats()->playerAttacks == at_count_b && rt_get_stats()->moveRequests == at_tile_a && !at_flag_a;
        at_log("disconnect: attacks=%d move requests=%d walking=%d",
            rt_get_stats()->playerAttacks - at_count_b, rt_get_stats()->moveRequests - at_tile_a, at_flag_a);
        // Reconnect with the stick and RT still held.
        at_pad.connected = true;
        at_tile_b = obj_dude->tile;
        at_count_b = rt_get_stats()->playerAttacks;
        at_phase = 4;
        at_time_a = now;
        break;
    case 4:
        at_stick_open_direction();
        if (now - at_time_a < 1500) {
            break;
        }
        {
            bool quiet = rt_get_stats()->playerAttacks == at_count_b && obj_dude->tile == at_tile_b;
            at_check(C_DISCONNECT, at_flag_b && quiet && gamepad_get_interruption_count() > at_count_a,
                "stopped while disconnected=%d, no phantom move/fire after reconnect=%d (attacks %d, moved %d)",
                at_flag_b, quiet, rt_get_stats()->playerAttacks - at_count_b, obj_dude->tile != at_tile_b);
        }
        at_release_all();
        return true;
    }
    return false;
}

static bool step_focus(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        at_release_all();
        return false;
    }

    if (timedOut) {
        at_focus_lost = false;
        at_release_all();
        at_check(C_FOCUS, false, "focus test timed out in phase %d", at_phase);
        return true;
    }

    switch (at_phase) {
    case 0:
        if (elapsed < 500) {
            break;
        }
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        at_stick_open_direction();
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
        at_stick_open_direction();
        if (now - at_time_a < 500) {
            break;
        }
        at_count_b = (int)rt_get_time();
        at_focus_lost = true;
        at_focus_regained = false;
        at_focus_polls = 0;
        at_focus_lost_at = now;
        {
            SDL_Event event;
            memset(&event, 0, sizeof(event));
            event.type = SDL_WINDOWEVENT;
            event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
            SDL_PushEvent(&event);
        }
        at_log("focus: window focus lost (simulated SDL event) while moving and holding RT");
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        // Back in the main loop after focus returned.
        if (!at_focus_regained) {
            break;
        }
        at_flag_a = (int)rt_get_time() - at_count_b < 400;
        at_tile_a = obj_dude->tile;
        at_count_a = rt_get_stats()->playerAttacks;
        at_phase = 3;
        at_time_a = now;
        break;
    case 3:
        at_stick_open_direction();
        if (now - at_time_a < 1200) {
            break;
        }
        {
            bool quiet = rt_get_stats()->playerAttacks == at_count_a && obj_dude->tile == at_tile_a;
            at_check(C_FOCUS, at_focus_polls > 5 && at_flag_a && quiet,
                "paused during loss (%d polls, simulation advanced < 400 ms=%d), held RT/stick ignored after return=%d",
                at_focus_polls, at_flag_a, quiet);
        }
        at_focus_lost = false;
        at_release_all();
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: save, load, map transition
// -----------------------------------------------------------------------------

static time_t at_save_time;
static bool at_save_slot_existed = false;
static int at_saved_tile;

// Options window -> "SAVE GAME" (top button) -> slot list -> Enter -> comment -> Enter.
static bool at_menu_save_script(unsigned int elapsed)
{
    if (elapsed > 300 && elapsed < 1300 && (elapsed / 150) % 2 == 0) {
        at_press(GAMEPAD_BUTTON_DPAD_UP);
    }
    if (at_phase == 0 && elapsed > 1500) {
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 1;
    }
    if (at_phase == 1 && elapsed > 3500) {
        at_screenshot("16_save_screen");
        at_press(GAMEPAD_BUTTON_START);
        at_phase = at_save_slot_existed ? 2 : 4;
    }
    // "Save game already exists, overwrite?": D-pad to YES, then A.
    if (at_phase == 2 && elapsed > 4600) {
        at_screenshot("17_save_overwrite");
        at_press(GAMEPAD_BUTTON_DPAD_LEFT);
        at_phase = 3;
    }
    if (at_phase == 3 && elapsed > 5200) {
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 4;
    }
    if (at_phase == 4 && elapsed > 6800) {
        at_screenshot("17_save_comment");
        at_press(GAMEPAD_BUTTON_START);
        at_phase = 5;
    }
    if (at_phase >= 5 && elapsed > 9500 && (elapsed / 1000) % 2 == 0) {
        // Back out of anything still open.
        at_press(GAMEPAD_BUTTON_B);
    }
    return false;
}

static bool step_save(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_release_all();
        at_heal();
        at_phase = 0;
        at_count_a = 0;
        at_save_time = time(NULL) - 1;
        {
            struct stat info;
            at_save_slot_existed = stat("data\\SAVEGAME\\SLOT01\\SAVE.DAT", &info) == 0;
            at_log("save: slot 1 %s", at_save_slot_existed ? "exists (overwrite prompt expected)" : "is empty");
        }
        // Keep a lock and an attack in flight while saving.
        if (at_nearest(at_filter_hostile, 25) == NULL) {
            at_spawn_rat(2, 3, true);
        }
        if (rt_get_lock() == NULL) {
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        }
        return false;
    }

    if (at_count_a == 0) {
        if (elapsed > 400) {
            at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            at_count_a = 1;
        }
        return false;
    }

    if (at_count_a == 1) {
        if (elapsed > 500) {
            at_saved_tile = obj_dude->tile;
            at_record_save_state();
            at_log("save: lock=%s pending attacks=%d player tile=%d",
                rt_get_lock() != NULL ? critter_name(rt_get_lock()) : "(none)", rt_get_pending_count(), at_saved_tile);
            at_expect_menu(NULL, at_menu_save_script);
            at_press(GAMEPAD_BUTTON_START);
            at_count_a = 2;
        }
        return false;
    }

    // Back in gameplay after the menus closed.
    if (at_menu_seen && elapsed > 1500) {
        bool saved = at_file_recent("data\\SAVEGAME\\SLOT01\\SAVE.DAT", at_save_time);
        at_check(C_SAVE, saved, "SLOT01\\SAVE.DAT written=%d (player tile %d)", saved, at_saved_tile);
        at_menu_expected = false;
        return true;
    }

    if (timedOut) {
        at_check(C_SAVE, false, "save flow timed out");
        at_menu_expected = false;
        return true;
    }
    return false;
}

// Options window -> "LOAD GAME" (second button) -> slot list -> Enter.
static bool at_menu_load_script(unsigned int elapsed)
{
    if (elapsed > 300 && elapsed < 1300 && (elapsed / 150) % 2 == 0) {
        at_press(GAMEPAD_BUTTON_DPAD_UP);
    }
    if (at_phase == 0 && elapsed > 1500) {
        at_press(GAMEPAD_BUTTON_DPAD_DOWN);
        at_phase = 1;
    }
    if (at_phase == 1 && elapsed > 1900) {
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 2;
    }
    if (at_phase == 2 && elapsed > 3800) {
        at_screenshot("18_load_screen");
        at_press(GAMEPAD_BUTTON_START);
        at_phase = 3;
    }
    if (at_phase >= 3 && elapsed > 9000 && (elapsed / 1000) % 2 == 0) {
        at_press(GAMEPAD_BUTTON_B);
    }
    return false;
}

static bool step_load(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_release_all();
        at_phase = 0;
        at_count_a = 0;

        // Move away from the saved spot and keep fire held into the menu.
        at_stick_open_direction();
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        return false;
    }

    if (at_count_a == 0) {
        at_stick_open_direction();
        if (elapsed > 1200) {
            at_stick_release();
            at_log("load: player at tile %d before loading (saved at %d)", obj_dude->tile, at_saved_tile);
            at_expect_menu(NULL, at_menu_load_script);
            at_press(GAMEPAD_BUTTON_START);
            at_count_a = 1;
        }
        return false;
    }

    if (at_count_a == 1) {
        if (at_menu_seen) {
            at_count_a = 2;
            at_time_a = SDL_GetTicks();
            at_tile_a = obj_dude->tile;
            at_count_b = rt_get_stats()->playerAttacks;
            at_log("load: back in gameplay on %s at tile %d, lock=%s, pending=%d",
                map_data.name,
                obj_dude->tile,
                rt_get_lock() != NULL ? critter_name(rt_get_lock()) : "(none)",
                rt_get_pending_count());
        }
        return timedOut;
    }

    // RT is still held from before the load: it must not fire, and the
    // player must be able to move once the stick is used again.
    unsigned int since = SDL_GetTicks() - at_time_a;
    if (at_count_a == 2 && since > 1500) {
        at_flag_a = rt_get_stats()->playerAttacks == at_count_b;
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = false;
        at_tile_b = obj_dude->tile;
        at_count_a = 3;
    } else if (at_count_a == 3) {
        at_stick_open_direction();
        if (since > 3000) {
            at_stick_release();
            bool restored = at_tile_a == at_saved_tile;
            bool movesAgain = obj_dude->tile != at_tile_b;
            at_check(C_LOAD, restored && at_flag_a && movesAgain && rt_get_lock() == NULL && !rt_get_indicator()->visible,
                "position restored=%d (tile %d), no phantom fire=%d, can move=%d, lock cleared=%d",
                restored, at_tile_a, at_flag_a, movesAgain, rt_get_lock() == NULL);
            at_menu_expected = false;
            at_screenshot("19_after_load");
            return true;
        }
    }

    if (timedOut) {
        at_check(C_LOAD, false, "load flow timed out (phase %d)", at_count_a);
        at_menu_expected = false;
        return true;
    }
    return false;
}

static bool step_map_change(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_release_all();
        at_phase = 0;

        // A lock and held fire are active when the map changes.
        if (at_nearest(at_filter_hostile, 25) == NULL) {
            at_spawn_rat(2, 3, true);
        }
        if (rt_get_lock() == NULL) {
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        }
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        return false;
    }

    if (timedOut) {
        at_check(C_MAP_CHANGE, false, "map change timed out (phase %d)", at_phase);
        return true;
    }

    if (at_phase == 0) {
        if (elapsed < 600) {
            return false;
        }
        int map = map_match_map_name("VAULT13.MAP");
        at_log("map change: lock=%s, requesting VAULT13.MAP (index %d) via map_leave_map (harness)",
            rt_get_lock() != NULL ? critter_name(rt_get_lock()) : "(none)", map);
        if (map == -1) {
            at_check(C_MAP_CHANGE, false, "VAULT13.MAP not found");
            return true;
        }
        MapTransition transition;
        transition.map = map;
        transition.elevation = 0;
        transition.tile = -1;
        transition.rotation = 0;
        map_leave_map(&transition);
        at_phase = 1;
        return false;
    }

    if (at_phase == 1) {
        if (SDL_strncasecmp(map_data.name, "VAULT13", 7) != 0) {
            return false;
        }
        at_log("map change: now on %s", map_data.name);
        at_count_b = rt_get_stats()->playerAttacks;
        at_phase = 2;
        at_time_a = SDL_GetTicks();
        return false;
    }

    unsigned int since = SDL_GetTicks() - at_time_a;
    if (at_phase == 2 && since > 2000) {
        at_flag_a = rt_get_lock() == NULL && rt_get_pending_count() == 0 && rt_get_stats()->playerAttacks == at_count_b;
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = false;
        at_tile_b = obj_dude->tile;
        at_phase = 3;
        at_time_a = SDL_GetTicks();
        return false;
    }

    if (at_phase == 3) {
        at_stick_open_direction();
        if (since > 1500) {
            at_stick_release();
            bool moved = obj_dude->tile != at_tile_b;
            at_check(C_MAP_CHANGE, at_flag_a && moved,
                "map=%s, clean state (no lock/pending/phantom fire)=%d, can move=%d",
                map_data.name, at_flag_a, moved);
            at_screenshot("20_vault13");
            return true;
        }
    }
    return false;
}

// Vault 13: neutral NPCs.
static bool step_neutral(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        at_release_all();
        rt_debug_set_lock(NULL);
        return false;
    }

    switch (at_phase) {
    case 0: {
        Object* candidates[32];
        int count = at_lock_candidates(candidates, 32);
        if (count == 0) {
            // Walk toward the nearest NPC until one is close and on screen.
            Object* npc = at_nearest(at_filter_enemy_team, 60);
            if (npc != NULL) {
                at_approach(npc, 6);
            }
            if (timedOut) {
                at_log("neutral test: no NPC on screen");
                return true;
            }
            break;
        }
        if (anim_busy(obj_dude)) {
            register_clear(obj_dude);
        }
        Object* nearest = NULL;
        for (int index = 0; index < count; index++) {
            if (nearest == NULL || obj_dist(obj_dude, candidates[index]) < obj_dist(obj_dude, nearest)) {
                nearest = candidates[index];
            }
        }
        at_obj_a = nearest;
        at_count_a = rt_get_stats()->combatRequests;
        at_count_b = rt_get_stats()->attacksStarted;
        at_log("neutral test: %d NPC(s) on screen, nearest %s at %d tiles", count, critter_name(nearest), obj_dist(obj_dude, nearest));
        at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        at_phase = 1;
        at_time_a = now;
        break;
    }
    case 1:
        if (now - at_time_a < 500) {
            break;
        }
        {
            Object* lock = rt_get_lock();
            bool neutral = lock != NULL && !rt_is_hostile(lock);
            at_check(C_LOCK_NEUTRAL, neutral && obj_dist(obj_dude, lock) <= obj_dist(obj_dude, at_obj_a),
                "locked %s (neutral=%d) at %d tiles; nearest NPC at %d tiles; indicator '%s'",
                lock != NULL ? critter_name(lock) : "(none)", neutral,
                lock != NULL ? obj_dist(obj_dude, lock) : -1, obj_dist(obj_dude, at_obj_a), rt_get_indicator()->text);
            at_obj_a = lock;
        }
        at_screenshot("26_neutral_lock");
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        // Locked for 4 seconds: nobody turns hostile, nothing attacks.
        if (now - at_time_a < 4000) {
            break;
        }
        if (at_obj_a != NULL) {
            at_check(C_LOCK_NO_AGGRO,
                !rt_is_hostile(at_obj_a) && !rt_is_engaged(at_obj_a)
                    && rt_get_stats()->combatRequests == at_count_a && rt_get_stats()->attacksStarted == at_count_b,
                "%s after 4 s locked: hostile=%d engaged=%d, combat requests=%d, attacks=%d",
                critter_name(at_obj_a), rt_is_hostile(at_obj_a), rt_is_engaged(at_obj_a),
                rt_get_stats()->combatRequests - at_count_a, rt_get_stats()->attacksStarted - at_count_b);
        }
        // Switching among neutral NPCs.
        at_phase = 20;
        at_time_a = now;
        break;
    case 20: {
        Object* candidates[32];
        int count = at_lock_candidates(candidates, 32);
        Object* lock = rt_get_lock();
        Object* other = NULL;
        for (int index = 0; index < count && lock != NULL; index++) {
            if (candidates[index] != lock) {
                other = candidates[index];
                break;
            }
        }
        if (other == NULL) {
            at_log("neutral test: only one NPC on screen; no switching among NPCs");
            at_phase = 24;
            break;
        }
        int lx;
        int ly;
        int ox;
        int oy;
        at_screen_pos(lock, &lx, &ly);
        at_screen_pos(other, &ox, &oy);
        float dx = (float)(ox - lx);
        float dy = (float)(oy - ly);
        float length = sqrtf(dx * dx + dy * dy);
        at_switch_dx = length > 0.0f ? dx / length : 1.0f;
        at_switch_dy = length > 0.0f ? dy / length : 0.0f;
        at_obj_a = lock;
        at_tile_a = rt_get_stats()->lockSwitches;
        at_pad.rightX = at_switch_dx;
        at_pad.rightY = at_switch_dy;
        at_phase = 21;
        at_time_a = now;
        break;
    }
    case 21: {
        if (now - at_time_a < 600) {
            break;
        }
        at_pad.rightX = 0.0f;
        at_pad.rightY = 0.0f;
        Object* lock = rt_get_lock();
        bool inDirection = false;
        if (lock != NULL && at_obj_a != NULL && lock != at_obj_a) {
            int ax;
            int ay;
            int bx;
            int by;
            at_screen_pos(at_obj_a, &ax, &ay);
            at_screen_pos(lock, &bx, &by);
            float dx = (float)(bx - ax);
            float dy = (float)(by - ay);
            float length = sqrtf(dx * dx + dy * dy);
            inDirection = length > 0.0f && (dx * at_switch_dx + dy * at_switch_dy) / length >= 0.25f;
        }
        at_check(C_SWITCH, rt_get_stats()->lockSwitches == at_tile_a + 1 && lock != NULL && !rt_is_hostile(lock) && inDirection,
            "Vault 13: flick switched from %s to %s (neutral=%d, in direction=%d)",
            at_obj_a != NULL ? critter_name(at_obj_a) : "(none)", lock != NULL ? critter_name(lock) : "(none)",
            lock != NULL && !rt_is_hostile(lock), inDirection);
        at_phase = 22;
        at_time_a = now;
        break;
    }
    case 22: {
        // Rightmost NPC, flick right: nothing there, the lock stays.
        Object* candidates[32];
        int count = at_lock_candidates(candidates, 32);
        Object* rightmost = NULL;
        int rightmostX = -100000;
        for (int index = 0; index < count; index++) {
            int cx;
            int cy;
            at_screen_pos(candidates[index], &cx, &cy);
            if (cx > rightmostX) {
                rightmostX = cx;
                rightmost = candidates[index];
            }
        }
        if (rightmost == NULL) {
            at_phase = 24;
            break;
        }
        rt_debug_set_lock(rightmost);
        at_obj_a = rightmost;
        at_tile_a = rt_get_stats()->lockSwitchMisses;
        at_pad.rightX = 1.0f;
        at_pad.rightY = 0.0f;
        at_phase = 23;
        at_time_a = now;
        break;
    }
    case 23:
        if (now - at_time_a < 400) {
            break;
        }
        at_pad.rightX = 0.0f;
        at_pad.rightY = 0.0f;
        at_check(C_SWITCH_KEEP, rt_get_lock() == at_obj_a && rt_get_stats()->lockSwitchMisses == at_tile_a + 1,
            "Vault 13: rightmost NPC locked, flick right: lock kept=%d, misses=%d",
            rt_get_lock() == at_obj_a, rt_get_stats()->lockSwitchMisses - at_tile_a);
        at_check(C_LOCK_NO_AGGRO, rt_get_stats()->combatRequests == at_count_a && rt_get_stats()->attacksStarted == at_count_b,
            "after locking and switching between NPCs: combat requests=%d, attacks=%d",
            rt_get_stats()->combatRequests - at_count_a, rt_get_stats()->attacksStarted - at_count_b);
        at_phase = 24;
        break;
    case 24:
        // Unlock, then RT: no attack on anyone.
        if (rt_get_lock() != NULL) {
            at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        }
        at_phase = 3;
        at_time_a = now;
        break;
    case 3:
        if (now - at_time_a < 400) {
            break;
        }
        at_count_b = rt_get_stats()->playerAttacks;
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 4;
        at_time_a = now;
        break;
    case 4:
        if (now - at_time_a < 1000) {
            break;
        }
        at_check(C_NO_LOCK_RT, rt_get_lock() == NULL && rt_get_stats()->playerAttacks == at_count_b,
            "after unlocking the NPC, RT: attacks=%d, lock=%s",
            rt_get_stats()->playerAttacks - at_count_b, rt_get_lock() != NULL ? critter_name(rt_get_lock()) : "(none)");
        return true;
    }

    if (timedOut) {
        return true;
    }
    return false;
}

// Camera: still inside the inner box, scrolls near each edge.
static int at_cam_dir;
static int at_cam_ok;
static int at_cam_skipped;
static int at_cam_bad;
static int at_cam_last_x;
static int at_cam_last_y;
static int at_cam_max_jump;
static int at_cam_scrolls;
static int at_cam_recenters;
static int at_cam_start_x;
static int at_cam_start_y;
static bool at_cam_dir_ok[4];
static bool at_cam_final = false;
static int at_cam_center_before = -1;
static int at_cam_limits;

// Moves the camera (harness) so the player appears near the given screen
// position; false at a map scroll limit.
static bool at_camera_place(int px, int py)
{
    int dx;
    int dy;
    int cx;
    int cy;
    tile_coord(obj_dude->tile, &dx, &dy, obj_dude->elevation);
    tile_coord(tile_center_tile, &cx, &cy, obj_dude->elevation);
    dx += 16;
    dy += 8;
    int tile = tile_num(cx + 16 + (dx - px), cy + 8 + (dy - py), obj_dude->elevation);
    if (tile == -1) {
        return false;
    }
    return tile_set_center(tile, TILE_SET_CENTER_REFRESH_WINDOW) != -1;
}

static bool step_camera(unsigned int elapsed, bool first, bool timedOut)
{
    static const char* names[] = { "left", "right", "up", "down" };
    static const char* shots[] = { "27_camera_left", "27_camera_right", "27_camera_up", "27_camera_down" };
    unsigned int now = SDL_GetTicks();
    int width = scr_size.lrx - scr_size.ulx + 1;
    int height = scr_size.lry - scr_size.uly + 1 - INTERFACE_BAR_HEIGHT;
    int marginX = width * rt_get_camera_margin_pct() / 100;
    int marginY = height * rt_get_camera_margin_pct() / 100;

    if (first) {
        at_phase = 0;
        at_cam_dir = 0;
        at_cam_ok = 0;
        at_cam_skipped = 0;
        at_cam_bad = 0;
        at_release_all();
        rt_debug_set_lock(NULL);
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_check(C_CAMERA_EDGES, false, "timed out (%d ok, %d failed, %d skipped)", at_cam_ok, at_cam_bad, at_cam_skipped);
        return true;
    }

    int x;
    int y;
    bool onScreen = at_screen_pos(obj_dude, &x, &y);

    switch (at_phase) {
    case 0:
        // Centered, then walk a little: the camera must not move.
        if (rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        if (elapsed < 500 || anim_busy(obj_dude) || rt_is_run_toggled()) {
            break;
        }
        tile_set_center(obj_dude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
        at_tile_a = tile_center_tile;
        at_cam_center_before = tile_center_tile;
        at_screen_pos(obj_dude, &at_cam_last_x, &at_cam_last_y);
        at_tile_b = obj_dude->tile;
        at_flag_a = true;
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
        at_stick_open_direction();
        // A scroll is fine only if the player had reached the inner box edge
        // just before it (the scroll brings them back inside).
        if (tile_center_tile != at_cam_center_before) {
            if (at_cam_last_x >= marginX && at_cam_last_x <= width - marginX && at_cam_last_y >= marginY && at_cam_last_y <= height - marginY) {
                at_flag_a = false;
            }
            at_cam_center_before = tile_center_tile;
        }
        at_cam_last_x = x;
        at_cam_last_y = y;
        // An enemy hit briefly holds the player in place; allow for that.
        if (now - at_time_a < 1000 || (tile_dist(at_tile_b, obj_dude->tile) < 2 && now - at_time_a < 3000)) {
            break;
        }
        at_stick_release();
        at_check(C_CAMERA_STILL, at_flag_a && obj_dude->tile != at_tile_b,
            "player walked %d tiles; camera scrolled while the player was inside the inner box=%d (scrolled at all=%d)",
            tile_dist(at_tile_b, obj_dude->tile), !at_flag_a, tile_center_tile != at_tile_a);
        at_phase = 2;
        at_time_a = now;
        break;
    case 2: {
        // Put the player near one edge (outside the inner box).
        if (now - at_time_a < 600 || anim_busy(obj_dude)) {
            break;
        }
        if (at_cam_dir >= 4) {
            // A direction counts once verified on any map (map limits can
            // prevent setting one up); failures always count.
            int verified = at_cam_dir_ok[0] + at_cam_dir_ok[1] + at_cam_dir_ok[2] + at_cam_dir_ok[3];
            at_log("camera on %s: %d ok, %d failed, %d not set up; verified so far: left=%d right=%d up=%d down=%d",
                map_data.name, at_cam_ok, at_cam_bad, at_cam_skipped, at_cam_dir_ok[0], at_cam_dir_ok[1], at_cam_dir_ok[2], at_cam_dir_ok[3]);
            // Back to a normal view for the following tests.
            tile_set_center(obj_dude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            if (at_cam_bad > 0 || verified == 4 || at_cam_final) {
                at_check(C_CAMERA_EDGES, at_cam_bad == 0 && verified == 4,
                    "directions verified: left=%d right=%d up=%d down=%d; failures=%d (last map %s)",
                    at_cam_dir_ok[0], at_cam_dir_ok[1], at_cam_dir_ok[2], at_cam_dir_ok[3], at_cam_bad, map_data.name);
            }
            return true;
        }
        tile_set_center(obj_dude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
        int px = width / 2;
        int py = height / 2;
        switch (at_cam_dir) {
        case 0:
            px = marginX / 3;
            break;
        case 1:
            px = width - marginX / 3;
            break;
        case 2:
            py = marginY / 3;
            break;
        case 3:
            py = height - marginY / 3;
            break;
        }
        bool placed = at_camera_place(px, py);
        at_screen_pos(obj_dude, &x, &y);
        bool outside = (at_cam_dir == 0 && x < marginX) || (at_cam_dir == 1 && x > width - marginX)
            || (at_cam_dir == 2 && y < marginY) || (at_cam_dir == 3 && y > height - marginY);
        if (!placed || !outside) {
            at_log("camera %s: could not place the player near that edge (map limit; placed=%d at (%d,%d))", names[at_cam_dir], placed, x, y);
            at_cam_skipped++;
            at_cam_dir++;
            at_time_a = now;
            break;
        }
        at_cam_start_x = x;
        at_cam_start_y = y;
        at_cam_last_x = x;
        at_cam_last_y = y;
        at_cam_max_jump = 0;
        at_cam_scrolls = rt_get_stats()->cameraScrolls;
        at_cam_recenters = rt_get_stats()->cameraRecenters;
        at_cam_limits = rt_get_stats()->cameraLimitStops;
        at_flag_b = true;
        at_phase = 3;
        at_time_a = now;
        break;
    }
    case 3: {
        if (!onScreen) {
            at_flag_b = false;
        }
        int jump = abs(x - at_cam_last_x) + abs(y - at_cam_last_y);
        if (jump > at_cam_max_jump) {
            at_cam_max_jump = jump;
        }
        at_cam_last_x = x;
        at_cam_last_y = y;
        if (now - at_time_a > 60 && now - at_time_a < 90) {
            at_screenshot(shots[at_cam_dir]);
        }
        if (now - at_time_a < 2000) {
            break;
        }
        bool inside = x >= marginX - 16 && x <= width - marginX + 16 && y >= marginY - 12 && y <= height - marginY + 12;
        bool towards = (at_cam_dir == 0 && x > at_cam_start_x) || (at_cam_dir == 1 && x < at_cam_start_x)
            || (at_cam_dir == 2 && y > at_cam_start_y) || (at_cam_dir == 3 && y < at_cam_start_y);
        int scrolls = rt_get_stats()->cameraScrolls - at_cam_scrolls;
        // Scrolling may stop early at the map's scroll limit (map boundaries
        // are respected); the player must still be on screen.
        bool atLimit = rt_get_stats()->cameraLimitStops > at_cam_limits;
        bool good = towards && (inside || atLimit) && at_flag_b && scrolls > 0;
        at_log("camera %s: player (%d,%d) -> (%d,%d), %d scroll steps, largest per-frame shift %d px, recenters %d, stayed on screen=%d, stopped at map limit=%d",
            names[at_cam_dir], at_cam_start_x, at_cam_start_y, x, y, scrolls, at_cam_max_jump,
            rt_get_stats()->cameraRecenters - at_cam_recenters, at_flag_b, atLimit);
        if (good) {
            at_cam_ok++;
            at_cam_dir_ok[at_cam_dir] = true;
        } else {
            at_cam_bad++;
        }
        at_check(C_CAMERA_STEPS, at_cam_max_jump <= 40 && rt_get_stats()->cameraRecenters == at_cam_recenters,
            "largest per-frame shift %d px (one scroll step is 32x24), recenters %d",
            at_cam_max_jump, rt_get_stats()->cameraRecenters - at_cam_recenters);
        at_cam_dir++;
        at_phase = 2;
        at_time_a = now;
        break;
    }
    }
    return false;
}

static bool step_camera_cave(unsigned int elapsed, bool first, bool timedOut)
{
    at_cam_final = false;
    return step_camera(elapsed, first, timedOut);
}

static bool step_camera_vault(unsigned int elapsed, bool first, bool timedOut)
{
    at_cam_final = true;
    return step_camera(elapsed, first, timedOut);
}

// Right stick panning without a lock; the view stays until the player moves,
// then scrolls back.
static int at_pan_last_x;
static int at_pan_last_y;
static int at_pan_max_jump;

static bool step_camera_pan(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    int width = scr_size.lrx - scr_size.ulx + 1;
    int height = scr_size.lry - scr_size.uly + 1 - INTERFACE_BAR_HEIGHT;

    if (first) {
        at_phase = 0;
        at_release_all();
        rt_debug_set_lock(NULL);
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_check(C_CAMERA_PAN, false, "timed out in phase %d", at_phase);
        return true;
    }

    int x;
    int y;
    tile_coord(obj_dude->tile, &x, &y, obj_dude->elevation);
    x += 16;
    y += 8;

    switch (at_phase) {
    case 0:
        if (elapsed < 500 || anim_busy(obj_dude)) {
            break;
        }
        tile_set_center(obj_dude->tile, TILE_SET_CENTER_REFRESH_WINDOW);
        at_count_a = rt_get_stats()->cameraPans;
        at_count_b = rt_get_stats()->cameraRecenters;
        at_tile_b = obj_dude->tile;
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
        // No lock: push the right stick right for 1.5 s.
        if (now - at_time_a < 200) {
            at_cam_start_x = x;
            break;
        }
        at_pad.rightX = 1.0f;
        if (now - at_time_a < 1700) {
            break;
        }
        at_pad.rightX = 0.0f;
        at_check(C_CAMERA_PAN,
            rt_get_lock() == NULL && rt_get_stats()->cameraPans > at_count_a && x <= at_cam_start_x - 150
                && obj_dude->tile == at_tile_b && rt_get_stats()->cameraRecenters == at_count_b,
            "no lock, right stick held right 1.5 s: %d pan steps, player on screen x %d -> %d (out of frame=%d), player moved=%d, recenters=%d",
            rt_get_stats()->cameraPans - at_count_a, at_cam_start_x, x, x < 0 || x >= width,
            obj_dude->tile != at_tile_b, rt_get_stats()->cameraRecenters - at_count_b);
        at_screenshot("28_camera_pan");
        at_tile_a = tile_center_tile;
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        // Released: the view stays where it was put.
        if (now - at_time_a < 1200) {
            break;
        }
        at_flag_a = tile_center_tile == at_tile_a;
        at_pan_last_x = x;
        at_pan_last_y = y;
        at_pan_max_jump = 0;
        at_phase = 3;
        at_time_a = now;
        break;
    case 3: {
        // The player walks: the view comes back in scroll steps.
        at_stick_open_direction();
        int jump = abs(x - at_pan_last_x) + abs(y - at_pan_last_y);
        if (jump > at_pan_max_jump) {
            at_pan_max_jump = jump;
        }
        at_pan_last_x = x;
        at_pan_last_y = y;
        if (now - at_time_a < 2500) {
            break;
        }
        at_stick_release();
        bool inFrame = x >= 0 && x < width && y >= 0 && y < height;
        at_check(C_CAMERA_RETURN, at_flag_a && inFrame && at_pan_max_jump <= 40 && rt_get_stats()->cameraRecenters == at_count_b,
            "view stayed after release=%d; walking brought the player back in frame=%d (x %d), largest per-frame shift %d px, recenters=%d",
            at_flag_a, inFrame, x, at_pan_max_jump, rt_get_stats()->cameraRecenters - at_count_b);
        return true;
    }
    }
    return false;
}

// A never offers First Aid or Doctor; RT with a chem in the active slot uses
// one chem on the player (once per press).
static bool at_no_heal = false;
static int at_chem_pid = -1;

static int at_count_pid(int pid)
{
    int count = 0;
    Inventory* inventory = &(obj_dude->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item->pid == pid) {
            count += inventory->items[index].quantity;
        }
    }
    return count;
}

static Object* at_find_pid(int pid)
{
    Inventory* inventory = &(obj_dude->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item->pid == pid) {
            return inventory->items[index].item;
        }
    }
    return NULL;
}

static bool step_chem(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    const RtContextStats* context = rt_context_get_stats();

    if (first) {
        at_phase = 0;
        at_release_all();
        rt_debug_set_lock(NULL);
        at_clear_hostiles();
        register_clear(obj_dude);
        at_move_to_clear_spot();
        at_no_heal = true;
        critter_adjust_hits(obj_dude, -15);
        intface_update_hit_points(false);
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_no_heal = false;
        at_check(C_CHEM, false, "timed out in phase %d", at_phase);
        return true;
    }

    switch (at_phase) {
    case 0:
        if (elapsed < 400) {
            break;
        }
        // Injured with nothing nearby: A does nothing (no self-treatment).
        at_count_a = context->menus;
        at_count_b = context->skillUses;
        at_tile_b = context->interactions;
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 1;
        at_time_a = now;
        break;
    case 1: {
        if (now - at_time_a < 800) {
            break;
        }
        bool nothing = context->menus == at_count_a && context->skillUses == at_count_b && context->interactions == at_tile_b;
        // An injured, calm critter next to the player: no First Aid/Doctor.
        Object* rat = at_spawn_rat(1, 2, false);
        char actions[128] = "(no rat)";
        if (rat != NULL) {
            critter_adjust_hits(rat, -2);
            rt_context_describe(rat, actions, sizeof(actions));
        }
        bool noHealing = SDL_strcasestr(actions, "First") == NULL && SDL_strcasestr(actions, "Doctor") == NULL;
        at_check(C_NO_HEAL_ON_A, nothing && noHealing && rat != NULL,
            "injured player, A with nothing nearby did nothing=%d; injured rat offers '%s'", nothing, actions);

        // The chem fixture: Stimpaks in the active hand.
        if (at_chem_pid == -1) {
            for (int index = 1; index < 600 && at_chem_pid == -1; index++) {
                int pid = (OBJ_TYPE_ITEM << 24) | index;
                Proto* proto;
                if (proto_ptr(pid, &proto) != -1 && proto->item.type == ITEM_TYPE_DRUG
                    && SDL_strcasecmp(proto_name(pid), "Stimpak") == 0) {
                    at_chem_pid = pid;
                }
            }
        }
        Object* chem;
        Rect rect;
        if (at_chem_pid == -1 || obj_pid_new(&chem, at_chem_pid) == -1 || item_add_force(obj_dude, chem, 3) != 0) {
            at_no_heal = false;
            at_check(C_CHEM, false, "no Stimpak fixture");
            return true;
        }
        obj_disconnect(chem, &rect);
        at_log("harness: 3 Stimpaks added");
        at_phase = 2;
        at_time_a = now;
        break;
    }
    case 2:
        if (!at_wield(at_find_pid(at_chem_pid)) || anim_busy(obj_dude)) {
            break;
        }
        at_count_a = at_count_pid(at_chem_pid);
        at_count_b = critter_get_hits(obj_dude);
        at_tile_a = rt_get_stats()->chemUses;
        at_tile_b = rt_get_stats()->playerAttacks;
        // Press RT and keep holding it for 3 seconds.
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        at_phase = 3;
        at_time_a = now;
        break;
    case 3:
        if (now - at_time_a < 3000) {
            break;
        }
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = false;
        at_phase = 4;
        at_time_a = now;
        break;
    case 4:
        if (now - at_time_a < 1500 || anim_busy(obj_dude)) {
            break;
        }
        {
            int left = at_count_pid(at_chem_pid);
            int hp = critter_get_hits(obj_dude);
            Object* current = NULL;
            intface_get_current_item(&current);
            bool stillInHand = current != NULL && current->pid == at_chem_pid;
            at_check(C_CHEM,
                rt_get_stats()->chemUses == at_tile_a + 1 && left == at_count_a - 1 && hp > at_count_b
                    && rt_get_stats()->playerAttacks == at_tile_b && stillInHand,
                "RT held 3 s with Stimpaks (no lock): uses=%d, Stimpaks %d -> %d, hp %d -> %d, attacks=%d, rest of the stack still in hand=%d",
                rt_get_stats()->chemUses - at_tile_a, at_count_a, left, at_count_b, hp,
                rt_get_stats()->playerAttacks - at_tile_b, stillInHand);
        }
        at_screenshot("29_chem");
        at_no_heal = false;
        at_clear_hostiles();
        return true;
    }
    return false;
}

// Diagnostics (only with --rt-autotest-only=dump): logs the game's perk, stat
// and skill texts and every script call that touches party membership.
static bool step_dump(unsigned int elapsed, bool first, bool timedOut)
{
    if (SDL_strcasestr(at_only, "dump") == NULL) {
        return true;
    }

    for (int perk = 0; perk < PERK_COUNT; perk++) {
        at_log("dump perk %d %s | %s", perk, perk_name(perk), perk_description(perk));
    }
    for (int stat = 0; stat < STAT_COUNT; stat++) {
        at_log("dump stat %d %s | %s", stat, stat_name(stat), stat_description(stat));
    }
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        at_log("dump skill %d %s | %s | %s", skill, skill_name(skill), skill_attribute(skill), skill_description(skill));
    }
    for (int trait = 0; trait < TRAIT_COUNT; trait++) {
        at_log("dump trait %d %s | %s", trait, trait_name(trait), trait_description(trait));
    }

    static const int opcodes[] = { 0x8124, 0x8125, 0x814B };
    for (int index = 0; index < 2000; index++) {
        char name[16];
        if (scr_list_str(index, name, sizeof(name)) == -1) {
            break;
        }
        int hits = rt_context_dump_opcodes(index, opcodes, 3);
        if (hits != 0) {
            at_log("dump party script %d %s: %d calls", index, name, hits);
        }
    }
    return true;
}

// Scans every script in scripts.lst for skill handling (report only).
static bool step_script_scan(unsigned int elapsed, bool first, bool timedOut)
{
    int scripts = 0;
    int withProc = 0;
    int unknown = 0;
    int requirements = 0;
    int rolls = 0;
    for (int index = 0; index < 2000; index++) {
        char name[16];
        if (scr_list_str(index, name, sizeof(name)) == -1) {
            break;
        }
        scripts++;

        RtScriptSkills info;
        if (!rt_context_scan_script_index(index, &info) || !info.hasProc) {
            continue;
        }
        withProc++;
        if (info.unknownForm) {
            unknown++;
        }

        char line[400];
        line[0] = 0;
        for (int skill = 0; skill < SKILL_COUNT; skill++) {
            const RtSkillInfo* skillInfo = &(info.skills[skill]);
            if (!skillInfo->handled && skillInfo->requirement == -1) {
                continue;
            }
            char part[64];
            snprintf(part, sizeof(part), " %s%s%s", skill_name(skill), skillInfo->handled && skillInfo->roll ? "[roll]" : "", skillInfo->handled ? "" : "[not handled]");
            SDL_strlcat(line, part, sizeof(line));
            if (skillInfo->requirement != -1) {
                snprintf(part, sizeof(part), "[>=%d]", skillInfo->requirement);
                SDL_strlcat(line, part, sizeof(line));
                requirements++;
            }
            if (skillInfo->handled && skillInfo->roll) {
                rolls++;
            }
        }
        at_log("scan: %3d %-12s%s%s", index, name, line, info.unknownForm ? " (unrecognized form)" : "");
    }
    at_log("scan: %d scripts, %d with use_skill_on_p_proc, %d unrecognized, %d fixed requirements, %d rolls",
        scripts, withProc, unknown, requirements, rolls);
    return true;
}

// -----------------------------------------------------------------------------
// Script
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Steps: the unified Pip-Boy
// -----------------------------------------------------------------------------

// Menu scripts press one button at a time, with time for the press and the
// release to register.
static unsigned int at_pb_last_press = 0;
static int at_pb_phase = 0;
static int at_pb_tries = 0;
static unsigned int at_pb_phase_time = 0;

static Object* at_pb_weapon = NULL;
static Object* at_pb_ammo = NULL;
static int at_pb_ammo_pid = -1;
static int at_pb_ammo_before = 0;
static int at_pb_hp_before = 0;
static int at_pb_stims_before = 0;
static int at_pb_skill = -1;
static int at_pb_skill_before = 0;
static int at_pb_unspent_before = 0;
static int at_pb_perk = -1;
static unsigned int at_pb_rt_time = 0;
static unsigned int at_pb_game_time = 0;
static int at_pb_open_tab = -1;
static int at_pb_holodisk_gvar = -1;
static int at_pb_tab_moves = 0;
static int at_pb_page_moves = 0;
static bool at_pb_unequipped = false;
static bool at_pb_equipped = false;
static bool at_pb_used = false;
static bool at_pb_dropped = false;
static bool at_pb_inspected = false;
static bool at_pb_skill_ok = false;
static bool at_pb_perk_ok = false;
static int at_pb_quests = 0;
static int at_pb_quest_lines = 0;
static int at_pb_holodisks = 0;
static int at_pb_holodisk_lines = 0;
static int at_pb_local_pixels = 0;
static bool at_pb_local_player = false;
static int at_pb_world_pixels = 0;
static bool at_pb_world_drawn = false;
static int at_pb_saved_maps = -1;
static int at_pb_saved_pixels = 0;
static bool at_pb_paused = false;
static int at_pb_attacks = 0;
static int at_pb_interactions = 0;
static int at_pb_opens = 0;
static int at_pb_tile = 0;

// Persistence, checked after the save/load steps.
static int at_pb_saved_skill_value = -1;
static int at_pb_saved_perk_level = -1;
static int at_pb_saved_weapon_pid = -1;
static int at_pb_saved_unspent = -1;

static bool at_pb_ready(unsigned int gap = 170)
{
    return SDL_GetTicks() - at_pb_last_press >= gap;
}

static void at_pb_tap(GamepadButton button)
{
    at_press(button);
    at_pb_last_press = SDL_GetTicks();
}

static void at_pb_next(int phase)
{
    at_pb_phase = phase;
    at_pb_tries = 0;
    at_pb_phase_time = SDL_GetTicks();
}

static const RtPipboyState* at_pb()
{
    return rt_pipboy_get_state();
}

// Presses RT/LT until `tab` is shown; true once there.
static bool at_pb_goto_tab(int tab)
{
    if (at_pb()->tab == tab) {
        return true;
    }
    if (at_pb_ready()) {
        at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
    }
    return false;
}

static bool at_pb_goto_page(int page)
{
    if (at_pb()->page == page && at_pb()->mode == RT_PIPBOY_MODE_NONE) {
        return true;
    }
    if (at_pb_ready()) {
        at_pb_tap(GAMEPAD_BUTTON_RIGHT_SHOULDER);
    }
    return false;
}

static bool at_pb_goto_item(Object* item)
{
    if (at_pb()->cursorItem == item) {
        return true;
    }
    if (at_pb_ready() && at_pb_tries++ < 80) {
        at_pb_tap(GAMEPAD_BUTTON_DPAD_DOWN);
    }
    return false;
}

static bool at_pb_goto_choice(int choice)
{
    const RtPipboyState* state = at_pb();
    if (state->choiceCursor < state->choiceCount && state->choices[state->choiceCursor] == choice) {
        return true;
    }
    if (at_pb_ready() && at_pb_tries++ < 80) {
        at_pb_tap(GAMEPAD_BUTTON_DPAD_DOWN);
    }
    return false;
}

static int at_pb_stack(int pid)
{
    Inventory* inventory = &(obj_dude->data.inventory);
    int quantity = 0;
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item->pid == pid) {
            quantity += inventory->items[index].quantity;
        }
    }
    return quantity;
}

// The largest unequipped stack of `pid`.
static Object* at_pb_find_stack(int pid)
{
    Inventory* inventory = &(obj_dude->data.inventory);
    Object* best = NULL;
    int bestQuantity = 0;
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* entry = &(inventory->items[index]);
        if (entry->item->pid == pid && (entry->item->flags & OBJECT_EQUIPPED) == 0 && entry->quantity > bestQuantity) {
            best = entry->item;
            bestQuantity = entry->quantity;
        }
    }
    return best;
}

// The whole Pip-Boy, driven with controller buttons only.
static bool at_menu_pipboy_script(unsigned int elapsed)
{
    const RtPipboyState* state = at_pb();
    if (!state->open) {
        return false;
    }

    // Generous per-phase timeout: give up on the phase, move on.
    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 8000) {
        at_log("pipboy script: phase %d timed out (tab %d page %d mode %d cursor %d)", at_pb_phase, state->tab, state->page, state->mode, state->cursor);
        if (state->mode != RT_PIPBOY_MODE_NONE) {
            at_pb_tap(GAMEPAD_BUTTON_B);
        }
        at_pb_next(at_pb_phase == 90 ? 14 : at_pb_phase + 1);
        return false;
    }

    switch (at_pb_phase) {
    case 0:
        if (elapsed > 500) {
            at_pb_open_tab = state->tab;
            at_pb_rt_time = rt_get_time();
            at_pb_game_time = game_time();
            // The stick held when B was pressed is let go in the menu.
            at_pad.leftX = 0.0f;
            at_pad.leftY = 0.0f;
            at_pb_next(1);
        }
        break;
    case 1: {
        // RT then LT: one tab forward and back.
        static int before;
        if (at_pb_tries == 0 && at_pb_ready()) {
            before = state->tab;
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            at_pb_tries = 1;
        } else if (at_pb_tries == 1 && at_pb_ready(300)) {
            if (state->tab == (before + 1) % RT_PIPBOY_TAB_COUNT) {
                at_pb_tab_moves++;
            }
            at_pb_tap(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_pb_tries = 2;
        } else if (at_pb_tries == 2 && at_pb_ready(300)) {
            if (state->tab == before) {
                at_pb_tab_moves++;
            }
            at_pb_next(2);
        }
        break;
    }
    case 2:
        if (at_pb_goto_tab(RT_PIPBOY_TAB_INVENTORY)) {
            at_pb_next(3);
        }
        break;
    case 3: {
        // RB then LB: one page forward and back.
        static int before;
        if (at_pb_tries == 0 && at_pb_ready()) {
            before = state->page;
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_SHOULDER);
            at_pb_tries = 1;
        } else if (at_pb_tries == 1 && at_pb_ready(300)) {
            if (state->page == (before + 1) % 6) {
                at_pb_page_moves++;
            }
            at_pb_tap(GAMEPAD_BUTTON_LEFT_SHOULDER);
            at_pb_tries = 2;
        } else if (at_pb_tries == 2 && at_pb_ready(300)) {
            if (state->page == before) {
                at_pb_page_moves++;
            }
            at_pb_next(4);
        }
        break;
    }
    case 4:
        // Weapons: equip the test pistol in the right hand.
        if (at_pb_goto_page(1)) {
            at_pb_next(5);
        }
        break;
    case 5:
        if (at_pb_goto_item(at_pb_weapon) && at_pb_ready()) {
            at_screenshot("pb_02_inventory");
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(6);
        }
        break;
    case 6:
        if (state->mode == RT_PIPBOY_MODE_ACTIONS && at_pb_ready(250) && at_pb_goto_choice(RT_PIPBOY_ACTION_EQUIP_RIGHT) && at_pb_ready()) {
            at_screenshot("pb_03_actions");
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(7);
        }
        break;
    case 7:
        if (at_pb_ready(400)) {
            at_pb_equipped = inven_right_hand(obj_dude) == at_pb_weapon;
            at_log("pipboy: equip right -> right hand %s", inven_right_hand(obj_dude) != NULL ? object_name(inven_right_hand(obj_dude)) : "empty");
            at_pb_next(8);
        }
        break;
    case 8:
        // Put it away, then hold it again (kept for the save/load check).
        if (at_pb_goto_item(at_pb_weapon) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(9);
        }
        break;
    case 9:
        if (state->mode == RT_PIPBOY_MODE_ACTIONS && at_pb_ready(250) && at_pb_goto_choice(RT_PIPBOY_ACTION_UNEQUIP) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(10);
        }
        break;
    case 10:
        if (at_pb_ready(400)) {
            // Back in the inventory (possibly merged with a pistol already carried).
            at_pb_unequipped = inven_right_hand(obj_dude) == NULL && inven_rt_quantity(at_pb_weapon) >= 1
                && (at_pb_weapon->flags & OBJECT_EQUIPPED) == 0;
            at_pb_next(11);
        }
        break;
    case 11:
        if (at_pb_goto_item(at_pb_weapon) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(12);
        }
        break;
    case 12:
        if (state->mode == RT_PIPBOY_MODE_ACTIONS && at_pb_ready(250) && at_pb_goto_choice(RT_PIPBOY_ACTION_EQUIP_RIGHT) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(13);
        }
        break;
    case 13:
        // Inspect from the action menu (A, Inspect, A), back with B.
        if (at_pb_ready(400) && at_pb_goto_item(at_pb_weapon) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(90);
        }
        break;
    case 90:
        if (state->mode == RT_PIPBOY_MODE_ACTIONS && at_pb_ready(250) && at_pb_goto_choice(RT_PIPBOY_ACTION_INSPECT) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(14);
        }
        break;
    case 14:
        if (at_pb_ready(400)) {
            at_pb_inspected = state->mode == RT_PIPBOY_MODE_TEXT && state->listCount > 0;
            at_screenshot("pb_04_inspect");
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(15);
        }
        break;
    case 15:
        // Aid: use a Stimpak.
        if (at_pb_ready(300) && at_pb_goto_page(3)) {
            at_pb_hp_before = critter_get_hits(obj_dude);
            at_pb_stims_before = at_pb_stack(at_chem_pid);
            at_pb_next(16);
        }
        break;
    case 16: {
        Object* stim = at_pb_find_stack(at_chem_pid);
        if (stim != NULL && at_pb_goto_item(stim) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(17);
        }
        break;
    }
    case 17:
        if (state->mode == RT_PIPBOY_MODE_ACTIONS && at_pb_ready(250) && at_pb_goto_choice(RT_PIPBOY_ACTION_USE) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(18);
        }
        break;
    case 18:
        if (at_pb_ready(500)) {
            int hp = critter_get_hits(obj_dude);
            int stims = at_pb_stack(at_chem_pid);
            at_pb_used = hp > at_pb_hp_before && stims == at_pb_stims_before - 1;
            at_log("pipboy: Stimpak HP %d -> %d, Stimpaks %d -> %d", at_pb_hp_before, hp, at_pb_stims_before, stims);
            at_pb_next(19);
        }
        break;
    case 19:
        // Ammo: drop 2 of a stack with the quantity picker.
        if (at_pb_goto_page(4)) {
            at_pb_ammo = at_pb_ammo_pid != -1 ? at_pb_find_stack(at_pb_ammo_pid) : NULL;
            at_pb_ammo_before = at_pb_ammo_pid != -1 ? at_pb_stack(at_pb_ammo_pid) : 0;
            at_pb_next(at_pb_ammo != NULL && inven_rt_quantity(at_pb_ammo) >= 3 ? 20 : 24);
        }
        break;
    case 20:
        if (at_pb_goto_item(at_pb_ammo) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(21);
        }
        break;
    case 21:
        if (state->mode == RT_PIPBOY_MODE_QUANTITY && at_pb_ready(250)) {
            at_pb_tap(GAMEPAD_BUTTON_DPAD_RIGHT);
            at_pb_next(22);
        }
        break;
    case 22:
        if (at_pb_ready(300) && state->quantity == 2) {
            at_screenshot("pb_05_quantity");
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(23);
        }
        break;
    case 23:
        if (at_pb_ready(500)) {
            int after = at_pb_stack(at_pb_ammo_pid);
            at_pb_dropped = after == at_pb_ammo_before - 2;
            at_log("pipboy: ammo %d -> %d", at_pb_ammo_before, after);
            at_pb_next(24);
        }
        break;
    case 24:
        // Stats: spend a skill point, undo it with X, spend it again.
        if (at_pb_goto_tab(RT_PIPBOY_TAB_STATS)) {
            at_pb_next(25);
        }
        break;
    case 25:
        if (at_pb_goto_page(2) && at_pb_ready(300)) {
            at_pb_skill = state->cursor;
            at_pb_skill_before = skill_level(obj_dude, at_pb_skill);
            at_pb_unspent_before = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(26);
        }
        break;
    case 26:
        if (at_pb_ready(300)) {
            at_pb_skill_ok = skill_level(obj_dude, at_pb_skill) > at_pb_skill_before
                && stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS) == at_pb_unspent_before - 1;
            at_screenshot("pb_06_skills");
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(27);
        }
        break;
    case 27:
        if (at_pb_ready(300)) {
            at_pb_skill_ok = at_pb_skill_ok
                && skill_level(obj_dude, at_pb_skill) == at_pb_skill_before
                && stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS) == at_pb_unspent_before;
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(28);
        }
        break;
    case 28:
        // Perks: choose the perk that is due.
        if (at_pb_ready(300) && at_pb_goto_page(3)) {
            at_pb_next(29);
        }
        break;
    case 29:
        if (at_pb_ready(300)) {
            if (!editor_rt_has_free_perk()) {
                at_log("pipboy: no perk due");
                at_pb_next(34);
                break;
            }
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(30);
        }
        break;
    case 30:
        if (state->mode == RT_PIPBOY_MODE_CHOOSE_PERK && at_pb_ready(300)) {
            // A perk without follow-up choices.
            at_pb_perk = -1;
            for (int index = 0; index < state->choiceCount; index++) {
                int perk = state->choices[index];
                if (perk != PERK_TAG && perk != PERK_MUTATE) {
                    at_pb_perk = perk;
                    break;
                }
            }
            at_screenshot("pb_07_perks");
            at_pb_next(at_pb_perk != -1 ? 31 : 34);
        }
        break;
    case 31:
        if (at_pb_goto_choice(at_pb_perk) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(32);
        }
        break;
    case 32:
        if (state->mode == RT_PIPBOY_MODE_CONFIRM_PERK && at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(33);
        }
        break;
    case 33:
        if (at_pb_ready(400)) {
            at_pb_perk_ok = perk_level(at_pb_perk) == 1 && !editor_rt_has_free_perk() && state->mode == RT_PIPBOY_MODE_NONE;
            at_log("pipboy: perk %s level %d, still due=%d", perk_name(at_pb_perk), perk_level(at_pb_perk), editor_rt_has_free_perk());
            at_pb_next(34);
        }
        break;
    case 34:
        if (at_pb_ready(300) && at_pb_goto_page(0)) {
            at_screenshot("pb_01_stats");
            at_pb_next(35);
        }
        break;
    case 35:
        // Map: live automap, saved automaps, world map.
        if (at_pb_goto_tab(RT_PIPBOY_TAB_MAP) && at_pb_goto_page(0) && at_pb_ready(500)) {
            at_pb_local_pixels = state->mapPixels;
            at_pb_local_player = state->mapPlayer;
            at_screenshot("pb_08_map_local");
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(36);
        }
        break;
    case 36:
        if (at_pb_ready(300)) {
            at_screenshot("pb_09_map_zoomed");
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(37);
        }
        break;
    case 37:
        if (at_pb_ready(300) && at_pb_goto_page(1) && at_pb_ready(300)) {
            at_pb_saved_maps = state->listCount;
            if (state->listCount > 0) {
                at_pb_tap(GAMEPAD_BUTTON_A);
                at_pb_next(38);
            } else {
                at_pb_next(40);
            }
        }
        break;
    case 38:
        if (at_pb_ready(400)) {
            at_pb_saved_pixels = state->mode == RT_PIPBOY_MODE_SAVED_MAP ? state->mapPixels : 0;
            at_screenshot("pb_10_map_saved");
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(40);
        }
        break;
    case 40:
        if (at_pb_ready(300) && at_pb_goto_page(2) && at_pb_ready(400)) {
            at_pb_world_pixels = state->mapPixels;
            at_pb_world_drawn = state->mapPlayer;
            at_screenshot("pb_11_map_world");
            at_pb_next(41);
        }
        break;
    case 41:
        // Data: quests and holodisks from the game variables.
        if (at_pb_goto_tab(RT_PIPBOY_TAB_DATA) && at_pb_goto_page(0) && at_pb_ready(400)) {
            at_pb_quests = state->listCount;
            if (state->listCount > 0) {
                at_pb_tap(GAMEPAD_BUTTON_A);
                at_pb_next(42);
            } else {
                at_pb_next(43);
            }
        }
        break;
    case 42:
        if (at_pb_ready(400)) {
            at_pb_quest_lines = state->mode == RT_PIPBOY_MODE_TEXT ? state->listCount : 0;
            at_screenshot("pb_12_quests");
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(43);
        }
        break;
    case 43:
        if (at_pb_ready(300) && at_pb_goto_page(1) && at_pb_ready(400)) {
            at_pb_holodisks = state->listCount;
            if (state->listCount > 0) {
                at_pb_tap(GAMEPAD_BUTTON_A);
                at_pb_next(44);
            } else {
                at_pb_next(45);
            }
        }
        break;
    case 44:
        if (at_pb_ready(400)) {
            at_pb_holodisk_lines = state->mode == RT_PIPBOY_MODE_TEXT ? state->listCount : 0;
            at_screenshot("pb_13_holodisk");
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(45);
        }
        break;
    case 45:
        if (at_pb_ready(300) && at_pb_goto_page(3) && at_pb_ready(400)) {
            at_screenshot("pb_14_rest");
            at_pb_next(46);
        }
        break;
    case 46:
        // Pressing A and RT here must not leak into gameplay; then B closes
        // from the main page.
        if (at_pb_ready(300)) {
            at_pb_paused = rt_get_time() - at_pb_rt_time < 600 && game_time() == at_pb_game_time;
            at_log("pipboy: open %u ms; simulation advanced %u ms, game time %u -> %u", SDL_GetTicks() - at_pb_phase_time,
                rt_get_time() - at_pb_rt_time, at_pb_game_time, game_time());
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(47);
        }
        break;
    case 47:
        return true;
    }
    return false;
}

static bool step_pipboy(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_no_heal = true;
        at_phase = 0;
        return false;
    }

    switch (at_phase) {
    case 0: {
        // Fixtures: a pistol with spare ammo, Stimpaks, missing HP, a level
        // with a perk due, a holodisk.
        at_unarmed();
        at_pb_weapon = at_create_weapon(6, -1, true, 1);
        at_pb_ammo_pid = -1;
        if (at_pb_weapon != NULL) {
            Proto* proto;
            if (proto_ptr(at_pb_weapon->pid, &proto) != -1) {
                at_pb_ammo_pid = proto->item.data.weapon.ammoTypePid;
            }
        }

        if (at_chem_pid == -1) {
            for (int index = 1; index < 600 && at_chem_pid == -1; index++) {
                int pid = (OBJ_TYPE_ITEM << 24) | index;
                Proto* proto;
                if (proto_ptr(pid, &proto) != -1 && proto->item.type == ITEM_TYPE_DRUG
                    && SDL_strcasecmp(proto_name(pid), "Stimpak") == 0) {
                    at_chem_pid = pid;
                }
            }
        }
        Object* chem;
        Rect rect;
        if (at_chem_pid != -1 && obj_pid_new(&chem, at_chem_pid) != -1 && item_add_force(obj_dude, chem, 2) == 0) {
            obj_disconnect(chem, &rect);
        }

        critter_adjust_hits(obj_dude, -std::min(12, critter_get_hits(obj_dude) - 1));
        intface_update_hit_points(false);

        int level = stat_pc_get(PC_STAT_LEVEL);
        if (level < 3) {
            // Level 3 (3000 XP) brings skill points and a perk.
            stat_pc_add_experience(3000 - stat_pc_get(PC_STAT_EXPERIENCE));
        }

        at_pb_holodisk_gvar = pipboy_rt_holodisk_gvar(0);
        game_global_vars[at_pb_holodisk_gvar] = 1;

        at_log("harness: Pip-Boy fixtures: weapon=%s ammo pid=%d stimpak pid=%d hp=%d level=%d->%d holodisk gvar %d",
            at_pb_weapon != NULL ? object_name(at_pb_weapon) : "none", at_pb_ammo_pid, at_chem_pid,
            critter_get_hits(obj_dude), level, stat_pc_get(PC_STAT_LEVEL), at_pb_holodisk_gvar);

        at_pb_phase = 0;
        at_pb_tries = 0;
        at_pb_phase_time = SDL_GetTicks();
        at_pb_attacks = rt_get_stats()->playerAttacks;
        at_pb_interactions = rt_context_get_stats()->interactions;
        at_pb_opens = rt_pipboy_get_state()->opens;

        // Open with B while walking (the stick is still held when B is
        // pressed).
        at_stick_open_direction();
        at_expect_menu("pb_00_open", at_menu_pipboy_script);
        at_press(GAMEPAD_BUTTON_B);
        at_phase = 1;
        at_time_a = SDL_GetTicks();
        break;
    }
    case 1:
        // This runs in gameplay only: the Pip-Boy has closed.
        if (at_menu_seen) {
            at_stick_release();
            at_pb_tile = obj_dude->tile;
            at_time_a = SDL_GetTicks();
            at_phase = 2;
        } else if (SDL_GetTicks() - at_time_a > 5000) {
            at_check(C_PB_TABS, false, "B did not open the Pip-Boy");
            at_menu_expected = false;
            at_no_heal = false;
            return true;
        }
        break;
    case 2:
        if (SDL_GetTicks() - at_time_a < 1200) {
            break;
        }
        {
            const RtPipboyState* state = rt_pipboy_get_state();
            int attacks = rt_get_stats()->playerAttacks - at_pb_attacks;
            int interactions = rt_context_get_stats()->interactions - at_pb_interactions;
            int opens = state->opens - at_pb_opens;
            bool moved = obj_dude->tile != at_pb_tile;
            at_check(C_PB_PAUSE, at_pb_paused, "simulation and game clock stopped while open=%d", at_pb_paused);
            at_check(C_PB_NO_LEAK, attacks == 0 && interactions == 0 && opens == 1 && !moved && rt_get_lock() == NULL && !state->open,
                "attacks=%d interactions=%d opens=%d moved after close=%d lock=%d still open=%d",
                attacks, interactions, opens, moved, rt_get_lock() != NULL, state->open);
            at_check(C_PB_TABS, at_pb_tab_moves == 2 && at_pb_page_moves == 2,
                "RT/LT tab moves=%d of 2, RB/LB page moves=%d of 2", at_pb_tab_moves, at_pb_page_moves);
            at_check(C_PB_EQUIP, at_pb_weapon != NULL && at_pb_equipped && at_pb_unequipped && inven_right_hand(obj_dude) == at_pb_weapon,
                "equip right=%d, put away=%d, held again=%d", at_pb_equipped, at_pb_unequipped, inven_right_hand(obj_dude) == at_pb_weapon);
            at_check(C_PB_USE, at_pb_used, "Stimpak used from the Pip-Boy (HP before %d, now %d)", at_pb_hp_before, critter_get_hits(obj_dude));
            at_check(C_PB_DROP, at_pb_dropped && at_pb_inspected, "dropped 2 from a stack=%d, inspect text shown=%d", at_pb_dropped, at_pb_inspected);
            at_check(C_PB_SKILL, at_pb_skill_ok, "%s: point spent, undone and spent again=%d (unspent before %d, now %d)",
                at_pb_skill >= 0 ? skill_name(at_pb_skill) : "?", at_pb_skill_ok, at_pb_unspent_before, stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS));
            at_check(C_PB_PERK, at_pb_perk_ok, "perk %s chosen=%d", at_pb_perk >= 0 ? perk_name(at_pb_perk) : "none", at_pb_perk_ok);
            {
                // The world map shows exactly the cells the game has revealed.
                int seenCells = 0;
                for (int row = 0; row < WORLDMAP_RT_GRID_ROWS; row++) {
                    for (int column = 0; column < WORLDMAP_RT_GRID_COLUMNS; column++) {
                        if (worldmap_rt_cell(row, column) != 0) {
                            seenCells++;
                        }
                    }
                }
                at_check(C_PB_MAP, at_pb_local_pixels > 0 && at_pb_local_player && (seenCells > 0) == (at_pb_world_pixels > 0) && at_pb_world_drawn
                        && (at_pb_saved_maps <= 0 || at_pb_saved_pixels > 0),
                    "local automap %d px, player marker=%d; saved automaps %d (%d px); world map drawn=%d, %d revealed cells, %d px shown",
                    at_pb_local_pixels, at_pb_local_player, at_pb_saved_maps, at_pb_saved_pixels, at_pb_world_drawn, seenCells, at_pb_world_pixels);
            }
            at_check(C_PB_DATA, at_pb_quests > 0 && at_pb_quest_lines > 0 && at_pb_holodisks > 0 && at_pb_holodisk_lines > 0,
                "quest locations %d (%d lines), holodisks %d (%d lines)", at_pb_quests, at_pb_quest_lines, at_pb_holodisks, at_pb_holodisk_lines);

            at_pb_saved_skill_value = at_pb_skill >= 0 ? skill_level(obj_dude, at_pb_skill) : -1;
            at_pb_saved_perk_level = at_pb_perk >= 0 ? perk_level(at_pb_perk) : -1;
            at_pb_saved_weapon_pid = inven_right_hand(obj_dude) != NULL ? inven_right_hand(obj_dude)->pid : -1;
            at_pb_saved_unspent = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
        }
        at_menu_expected = false;
        at_no_heal = false;
        at_phase = 3;
        at_time_a = SDL_GetTicks();
        break;
    case 3:
        // Reopen with B: the last tab (Data) comes back.
        at_pb_opens = rt_pipboy_get_state()->opens;
        at_expect_menu(NULL, NULL);
        at_press(GAMEPAD_BUTTON_B);
        at_phase = 4;
        at_time_a = SDL_GetTicks();
        at_pb_open_tab = -1;
        break;
    case 4:
        if (rt_pipboy_get_state()->opens > at_pb_opens && at_pb_open_tab == -1) {
            // Read in the menu loop by the default script; the state keeps
            // the tab after closing.
            at_pb_open_tab = rt_pipboy_get_state()->tab;
        }
        if (at_menu_seen && SDL_GetTicks() - at_time_a > 300) {
            at_check(C_PB_REOPEN, rt_pipboy_get_state()->tab == RT_PIPBOY_TAB_DATA,
                "reopened on tab %d (Data is %d)", rt_pipboy_get_state()->tab, RT_PIPBOY_TAB_DATA);
            at_menu_expected = false;
            return true;
        }
        if (SDL_GetTicks() - at_time_a > 5000) {
            at_check(C_PB_REOPEN, false, "B did not reopen the Pip-Boy");
            at_menu_expected = false;
            return true;
        }
        break;
    }

    if (timedOut) {
        at_no_heal = false;
        at_menu_expected = false;
    }
    return false;
}

// Keyboard and HUD shortcuts (the HUD buttons send these keys) open the
// Pip-Boy on the matching tab, never the old screens.
static int at_pb_shortcut_tab = -1;
static int at_pb_shortcut_page = -1;

static bool at_menu_shortcut_script(unsigned int elapsed)
{
    if (elapsed > 600 && rt_pipboy_is_open()) {
        at_pb_shortcut_tab = rt_pipboy_get_state()->tab;
        at_pb_shortcut_page = rt_pipboy_get_state()->page;
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }
    if (elapsed > 3000) {
        // Some other screen: back out.
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }
    return false;
}

static bool step_pipboy_shortcuts(unsigned int elapsed, bool first, bool timedOut)
{
    static const int keys[] = { KEY_LOWERCASE_I, KEY_LOWERCASE_C, KEY_TAB, KEY_LOWERCASE_P, KEY_LOWERCASE_Z };
    static const int tabs[] = { RT_PIPBOY_TAB_INVENTORY, RT_PIPBOY_TAB_STATS, RT_PIPBOY_TAB_MAP, RT_PIPBOY_TAB_DATA, RT_PIPBOY_TAB_DATA };
    static const int pages[] = { -1, -1, RT_PIPBOY_PAGE_MAP_LOCAL, -1, RT_PIPBOY_PAGE_DATA_REST };
    static const char* names[] = { "i", "c", "Tab", "p", "z" };

    unsigned int now = SDL_GetTicks();
    if (first) {
        at_count_a = 0;
        at_count_b = 0;
        at_time_a = 0;
        at_tile_a = (int)now;
        return false;
    }

    if (at_count_a >= 5) {
        at_check(C_PB_SHORTCUTS, at_count_b == 5, "%d of 5 shortcuts opened the Pip-Boy on the right tab", at_count_b);
        at_menu_expected = false;
        return true;
    }

    if (at_time_a == 0) {
        if (now - (unsigned int)at_tile_a < 700) {
            return false;
        }
        at_pb_opens = rt_pipboy_get_state()->opens;
        at_pb_shortcut_tab = -1;
        at_pb_shortcut_page = -1;
        at_expect_menu(NULL, at_menu_shortcut_script);
        GNW_add_input_buffer(keys[at_count_a]);
        at_time_a = now;
        return false;
    }

    if ((at_menu_seen && at_menu_done && now - at_time_a > 300) || now - at_time_a > 6000) {
        bool opened = rt_pipboy_get_state()->opens == at_pb_opens + 1;
        bool tabOk = at_pb_shortcut_tab == tabs[at_count_a] && (pages[at_count_a] == -1 || at_pb_shortcut_page == pages[at_count_a]);
        at_log("shortcut %s: Pip-Boy opened=%d tab %d page %d (expected %d/%d)",
            names[at_count_a], opened, at_pb_shortcut_tab, at_pb_shortcut_page, tabs[at_count_a], pages[at_count_a]);
        if (opened && tabOk) {
            at_count_b++;
        }
        at_menu_expected = false;
        at_count_a++;
        at_time_a = 0;
        at_tile_a = (int)now;
    }
    return false;
}

// After the save/load steps: what was changed in the Pip-Boy came back.
static bool step_pipboy_persist(unsigned int elapsed, bool first, bool timedOut)
{
    if (!first) {
        return true;
    }

    if (at_pb_saved_weapon_pid == -1 && at_pb_saved_skill_value == -1) {
        at_check(C_PB_PERSIST, false, "the Pip-Boy step did not record anything");
        return true;
    }

    int skill = at_pb_skill >= 0 ? skill_level(obj_dude, at_pb_skill) : -1;
    int perk = at_pb_perk >= 0 ? perk_level(at_pb_perk) : -1;
    int weapon = inven_right_hand(obj_dude) != NULL ? inven_right_hand(obj_dude)->pid : -1;
    int unspent = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
    at_check(C_PB_PERSIST,
        skill == at_pb_saved_skill_value && perk == at_pb_saved_perk_level && weapon == at_pb_saved_weapon_pid && unspent == at_pb_saved_unspent,
        "after load: skill %d (saved %d), perk level %d (saved %d), right hand pid %d (saved %d), unspent %d (saved %d)",
        skill, at_pb_saved_skill_value, perk, at_pb_saved_perk_level, weapon, at_pb_saved_weapon_pid, unspent, at_pb_saved_unspent);
    return true;
}

// After the map transition: the map left behind is in the saved automaps,
// and the world map shows the land the game has revealed.
static bool at_menu_pipboy_maps_script(unsigned int elapsed)
{
    const RtPipboyState* state = at_pb();
    if (!state->open) {
        return false;
    }
    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 8000) {
        at_log("pipboy maps script: phase %d timed out", at_pb_phase);
        at_pb_tap(GAMEPAD_BUTTON_B);
        at_pb_next(at_pb_phase < 5 ? 5 : 9);
        return false;
    }

    switch (at_pb_phase) {
    case 0:
        if (elapsed > 500) {
            at_pb_next(1);
        }
        break;
    case 1:
        if (at_pb_goto_page(1) && at_pb_ready(400)) {
            at_pb_saved_maps = state->listCount;
            if (state->listCount > 0) {
                at_pb_tap(GAMEPAD_BUTTON_A);
                at_pb_next(2);
            } else {
                at_pb_next(5);
            }
        }
        break;
    case 2:
        if (at_pb_ready(400)) {
            at_pb_saved_pixels = state->mode == RT_PIPBOY_MODE_SAVED_MAP ? state->mapPixels : 0;
            at_screenshot("pb_10_map_saved");
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(5);
        }
        break;
    case 5:
        if (at_pb_ready(300) && at_pb_goto_page(2) && at_pb_ready(400)) {
            at_pb_world_pixels = state->mapPixels;
            at_pb_world_drawn = state->mapPlayer;
            at_screenshot("pb_11_map_world");
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(9);
        }
        break;
    case 9:
        return true;
    }
    return false;
}

static bool step_pipboy_maps(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        // What entering the world map does: reveal the land around the party.
        worldmap_rt_reveal_around_party();
        at_pb_saved_maps = -1;
        at_pb_saved_pixels = 0;
        at_pb_world_pixels = 0;
        at_pb_world_drawn = false;
        at_pb_phase = 0;
        at_pb_tries = 0;
        at_pb_phase_time = SDL_GetTicks();
        at_expect_menu(NULL, at_menu_pipboy_maps_script);
        GNW_add_input_buffer(KEY_TAB);
        return false;
    }

    if ((at_menu_seen && at_menu_done && elapsed > 300) || timedOut) {
        at_check(C_PB_MAP_SAVED, at_pb_saved_maps > 0 && at_pb_saved_pixels > 0 && at_pb_world_drawn && at_pb_world_pixels > 0,
            "saved automaps %d (shown %d px); world map drawn=%d with %d px of revealed land",
            at_pb_saved_maps, at_pb_saved_pixels, at_pb_world_drawn, at_pb_world_pixels);
        at_menu_expected = false;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: AIMED SHOT
// -----------------------------------------------------------------------------

// Fixtures: two calm, toughened rats in the line of fire, the premade
// character's 10mm pistol, and extra action points so that several aimed
// attacks fit in one queue.
static Object* at_aim_rats[2] = { NULL, NULL };
static Object* at_aim_human = NULL;
static Object* at_aim_pistol = NULL;
static int at_aim_ap_bonus = 0;
static bool at_aim_fixture = false;
static int at_aim_hp_rat_pid = -1;
static char at_aim_message[128];
// Disconnect inside the aimed shot screen (checked with the radial's focus loss).
static bool at_aim_disconnect_ok = false;
static int at_aim_flick_tries = 0;

static const RtAimedState* at_aim()
{
    return rt_aimed_get_state();
}

// A free tile at `minDistance`..`maxDistance`, on screen, that the player can
// shoot at, and not next to `avoid`.
static int at_aim_open_tile(int minDistance, int maxDistance, Object* avoid)
{
    int width = scr_size.lrx - scr_size.ulx + 1;
    int height = scr_size.lry - scr_size.uly + 1 - INTERFACE_BAR_HEIGHT;
    for (int distance = minDistance; distance <= maxDistance; distance++) {
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            int corner = tile_num_in_direction(obj_dude->tile, rotation, distance);
            for (int step = 0; step < distance && corner != -1; step++) {
                int tile = tile_num_in_direction(corner, (rotation + 2) % ROTATION_COUNT, step);
                int x;
                int y;
                if (tile == -1 || obj_blocking_at(NULL, tile, obj_dude->elevation) != NULL
                    || tile_coord(tile, &x, &y, obj_dude->elevation) != 0
                    || x < 40 || x > width - 60 || y < 40 || y > height - 40) {
                    continue;
                }
                if (avoid != NULL && tile_dist(tile, avoid->tile) < 2) {
                    continue;
                }
                if (combat_is_shot_blocked(obj_dude, obj_dude->tile, tile, NULL, NULL)) {
                    continue;
                }
                return tile;
            }
        }
    }
    return -1;
}

// Keeps two calm rats with plenty of hit points within pistol range.
static bool at_aim_ensure_rats()
{
    for (int index = 0; index < 2; index++) {
        Object* rat = at_aim_rats[index];
        bool usable = rat != NULL && !critter_is_dead(rat) && rt_aim_target_valid(rat)
            && obj_dist(obj_dude, rat) <= 8
            && (at_current_hit_mode() == -1 || rt_aim_check_shot(rat, at_current_hit_mode()) != COMBAT_BAD_SHOT_AIM_BLOCKED);
        if (usable) {
            continue;
        }

        // The one replaced goes, so flicks only find the two fixtures.
        if (rat != NULL && !critter_is_dead(rat)) {
            Rect rect;
            if (rt_get_lock() == rat) {
                rt_debug_set_lock(NULL);
            }
            anim_rt_end_all(rat);
            obj_erase_object(rat, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
        }
        rat = at_spawn_rat_at(at_aim_open_tile(3, 6, at_aim_rats[1 - index]), false);
        if (rat == NULL) {
            return false;
        }

        // Critter stats live on the prototype shared by all rats (removed
        // again when the aimed shot tests end).
        stat_set_bonus(rat, STAT_MAXIMUM_HIT_POINTS, 300);
        critter_adjust_hits(rat, 300);
        at_aim_hp_rat_pid = rat->pid;
        at_aim_rats[index] = rat;
        at_log("harness: aimed shot fixture rat %d at %d tiles (%d HP)", index, obj_dist(obj_dude, rat), critter_get_hits(rat));
    }
    return true;
}

static void at_aim_begin_fixture()
{
    if (!at_aim_fixture) {
        at_aim_fixture = true;
        at_aim_ap_bonus = stat_get_bonus(obj_dude, STAT_MAXIMUM_ACTION_POINTS);
    }
    // The test companion would stand in the line of fire.
    if (at_companion != NULL) {
        if (!critter_is_dead(at_companion)) {
            Rect rect;
            anim_rt_end_all(at_companion);
            obj_erase_object(at_companion, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            at_log("harness: test companion removed for the aimed shot tests");
        }
        at_companion = NULL;
    }
}

static void at_aim_end_fixture()
{
    if (at_aim_fixture) {
        stat_set_bonus(obj_dude, STAT_MAXIMUM_ACTION_POINTS, at_aim_ap_bonus);
        at_aim_fixture = false;
    }
    if (at_aim_hp_rat_pid != -1) {
        for (Object* rat : at_aim_rats) {
            if (rat != NULL && rat->pid == at_aim_hp_rat_pid) {
                stat_set_bonus(rat, STAT_MAXIMUM_HIT_POINTS, 0);
                break;
            }
        }
    }
    at_clear_hostiles();
    at_aim_rats[0] = NULL;
    at_aim_rats[1] = NULL;
    if (at_aim_human != NULL) {
        Rect rect;
        obj_erase_object(at_aim_human, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        at_aim_human = NULL;
    }
    rt_debug_set_lock(NULL);
    at_release_all();
}

// The pistol in hand, loaded, in single-shot mode.
static bool at_aim_pistol_ready()
{
    if (at_aim_pistol == NULL || inven_rt_quantity(at_aim_pistol) == 0) {
        at_aim_pistol = at_find_inventory("10mm Pistol");
    }
    if (!at_wield(at_aim_pistol)) {
        return false;
    }
    if (item_w_curr_ammo(at_aim_pistol) < item_w_max_ammo(at_aim_pistol)) {
        item_w_set_curr_ammo(at_aim_pistol, item_w_max_ammo(at_aim_pistol));
        intface_update_ammo_lights();
    }
    return at_current_anim() == ANIM_FIRE_SINGLE && !anim_busy(obj_dude);
}

// The last scripted press (LB, B...) is over, so the next one is a new press.
static bool at_aim_released()
{
    for (int button = 0; button < GAMEPAD_BUTTON_COUNT; button++) {
        if (at_press_frames[button] > 0 || at_press_gap[button] > 0) {
            return false;
        }
    }
    return SDL_GetTicks() - at_pb_last_press >= 400;
}

// Fixture: maximum AP set so exactly three aimed attacks with the current
// weapon fit (plus 2 AP), and AP filled up.
static void at_aim_full_ap()
{
    int cost = item_w_mp_cost(obj_dude, at_current_hit_mode(), true);
    int current = stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS);
    int wanted = 3 * cost + 2;
    if (current != wanted) {
        stat_set_bonus(obj_dude, STAT_MAXIMUM_ACTION_POINTS, stat_get_bonus(obj_dude, STAT_MAXIMUM_ACTION_POINTS) + wanted - current);
        at_log("harness: maximum AP set to %d (3 aimed attacks of %d AP + 2)", stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS), cost);
    }
    rt_debug_set_ap((float)stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS));
}

// The unit screen direction from `from` to `to`, for a right stick flick.
static void at_aim_direction(Object* from, Object* to, float* fx, float* fy)
{
    int x1;
    int y1;
    int x2;
    int y2;
    at_screen_pos(from, &x1, &y1);
    at_screen_pos(to, &x2, &y2);
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float length = sqrtf(dx * dx + dy * dy);
    *fx = length > 0.0f ? dx / length : 1.0f;
    *fy = length > 0.0f ? dy / length : 0.0f;
}

// A human critter prototype (fixture), for the anatomy comparison: one with
// all eight body parts whose names differ from the rat's somewhere.
static Object* at_aim_spawn_human()
{
    Object* rat = at_aim_rats[0];
    if (rat == NULL) {
        return NULL;
    }

    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_CRITTER << 24) | index;
        Proto* proto;
        if (proto_ptr(pid, &proto) == -1 || pid == rat->pid) {
            continue;
        }

        int fid = art_id(OBJ_TYPE_CRITTER, proto->fid & 0xFFF, ANIM_CALLED_SHOT_PIC, 0, 0);
        if (!art_exists(fid)) {
            continue;
        }

        Object* critter;
        if (obj_pid_new(&critter, pid) == -1) {
            continue;
        }

        int parts = 0;
        bool differs = false;
        for (int part = 0; part < RT_AIMED_PARTS; part++) {
            int location = combat_rt_called_shot_location(part);
            const char* name = rt_aimed_part_name(critter, location);
            const char* ratName = rt_aimed_part_name(rat, location);
            if (name != NULL) {
                parts++;
            }
            if ((name == NULL) != (ratName == NULL) || (name != NULL && strcmp(name, ratName) != 0)) {
                differs = true;
            }
        }

        if (parts == RT_AIMED_PARTS && differs && critter_body_type(critter) == BODY_TYPE_BIPED) {
            int tile = at_aim_open_tile(2, 3, NULL);
            if (tile != -1) {
                Rect rect;
                obj_move_to_tile(critter, tile, obj_dude->elevation, &rect);
                tile_refresh_rect(&rect, obj_dude->elevation);
                if (critter->sid != -1) {
                    scr_remove(critter->sid);
                    critter->sid = -1;
                }
                // A neutral bystander: not the player's team, not the rats'.
                critter->data.critter.combat.team = 77;
                at_log("harness: human fixture %s (pid 0x%08X) at %d tiles", critter_name(critter), pid, obj_dist(obj_dude, critter));
                return critter;
            }
        }

        Rect rect;
        obj_erase_object(critter, &rect);
    }
    return NULL;
}

static void at_aim_log_anatomy(Object* critter, const char* label)
{
    char line[400] = "";
    for (int part = 0; part < RT_AIMED_PARTS; part++) {
        const char* name = rt_aimed_part_name(critter, combat_rt_called_shot_location(part));
        SDL_strlcat(line, name != NULL ? name : "(none)", sizeof(line));
        SDL_strlcat(line, part < RT_AIMED_PARTS - 1 ? ", " : "", sizeof(line));
    }
    at_log("anatomy %s (%s): %s", label, critter_name(critter), line);
}

// Menu script state shared by the aimed shot scripts.
static Object* at_aim_seen_target = NULL;
static int at_aim_seen_available = 0;
static bool at_aim_seen_open = false;
static float at_aim_flick_x = 0.0f;
static float at_aim_flick_y = 0.0f;

// Opens the screen, notes what it shows, closes it with B.
static bool at_menu_aim_look_script(unsigned int elapsed)
{
    const RtAimedState* state = at_aim();
    if (elapsed > 600 && !at_aim_seen_open) {
        at_aim_seen_open = state->selecting;
        at_aim_seen_target = state->target;
        at_aim_seen_available = 0;
        for (int part = 0; part < RT_AIMED_PARTS; part++) {
            at_aim_seen_available += state->available[part] ? 1 : 0;
        }
    }
    if (elapsed > 1300) {
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }
    return false;
}

static int at_aim_entry_phase;
static unsigned int at_aim_time;
static float at_aim_ap_before;
static Object* at_aim_rat_names = NULL;

// Entry with and without a lock, and the anatomy of two creature types.
static bool step_aim_entry(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_clear_hostiles();
        at_release_all();
        at_aim_begin_fixture();
        at_aim_entry_phase = 20;
        at_aim_time = now;
        return false;
    }

    if (timedOut) {
        at_check(C_AIM_ENTRY_LOCK, at_results[C_AIM_ENTRY_LOCK] == 1, "timed out in phase %d", at_aim_entry_phase);
        at_menu_expected = false;
        return true;
    }

    at_heal();
    switch (at_aim_entry_phase) {
    case 20:
        // No enemy around (the rats were removed): LB refuses and stays in
        // gameplay.
        if (!at_aim_released() || !at_aim_pistol_ready()) {
            break;
        }
        rt_debug_set_lock(NULL);
        at_count_a = at_aim()->refusals;
        at_count_b = at_aim()->opens;
        at_expect_menu(NULL, NULL);
        at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
        at_aim_entry_phase = 21;
        at_aim_time = now;
        break;
    case 21:
        if (now - at_aim_time < 800) {
            break;
        }
        at_check(C_AIM_NO_TARGET, !at_menu_seen && at_aim()->opens == at_count_b && at_aim()->refusals == at_count_a + 1
                && !rt_aimed_is_executing() && strstr(at_aim()->message, "No enemy") != NULL,
            "LB with no enemy: screen opened=%d, refused=%d, message '%s'",
            at_menu_seen, at_aim()->refusals - at_count_a, at_aim()->message);
        at_menu_expected = false;
        at_aim_entry_phase = 0;
        break;
    case 0:
        if (!at_aim_released() || !at_aim_pistol_ready() || !at_aim_ensure_rats()) {
            break;
        }
        at_aim_full_ap();
        rt_debug_set_lock(at_aim_rats[0]);
        at_aim_ap_before = rt_get_dude_ap();
        at_aim_seen_open = false;
        at_aim_entry_phase = 1;
        at_aim_time = now;
        break;
    case 1:
        // A tick later (the lock is set), LB.
        at_expect_menu("aim_01_rat", at_menu_aim_look_script);
        at_count_a = at_aim()->opens;
        at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
        at_aim_entry_phase = 2;
        at_aim_time = now;
        break;
    case 2:
        if (!at_menu_seen || !at_menu_done || now - at_aim_time < 400) {
            break;
        }
        at_check(C_AIM_ENTRY_LOCK,
            at_aim_seen_open && at_aim_seen_target == at_aim_rats[0] && at_aim()->opens == at_count_a + 1
                && !at_aim()->selecting && !rt_aimed_is_executing() && rt_get_dude_ap() >= at_aim_ap_before - 0.01f,
            "opened=%d on the locked rat=%d, closed with B=%d, AP %.2f -> %.2f",
            at_aim_seen_open, at_aim_seen_target == at_aim_rats[0], !at_aim()->selecting, at_aim_ap_before, rt_get_dude_ap());
        at_aim_rat_names = at_aim_rats[0];
        at_aim_log_anatomy(at_aim_rats[0], "rat");
        at_menu_expected = false;

        // Without a lock: a neutral human stands closer than the rats; the
        // rats are made hostile (as if they had noticed the player).
        rt_debug_set_lock(NULL);
        at_aim_human = at_aim_spawn_human();
        for (Object* rat : at_aim_rats) {
            STRUCT_664980 request;
            memset(&request, 0, sizeof(request));
            request.attacker = rat;
            request.defender = obj_dude;
            rt_combat_request(&request);
        }
        at_aim_entry_phase = 3;
        at_aim_time = now;
        break;
    case 3:
        if (now - at_aim_time < 300) {
            break;
        }
        at_aim_seen_open = false;
        at_expect_menu("aim_02_nearest", at_menu_aim_look_script);
        at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
        at_aim_entry_phase = 4;
        at_aim_time = now;
        break;
    case 4: {
        if (!at_menu_seen || !at_menu_done || now - at_aim_time < 400) {
            break;
        }
        Object* nearest = at_nearest(at_filter_hostile, 25);
        bool hostileTarget = at_aim_seen_target != NULL && rt_is_hostile(at_aim_seen_target);
        at_check(C_AIM_ENTRY_NEAREST,
            at_aim_seen_open && hostileTarget && at_aim_seen_target != at_aim_human
                && (nearest == NULL || obj_dist(obj_dude, at_aim_seen_target) <= obj_dist(obj_dude, nearest) + 1),
            "no lock: target %s (hostile=%d, %d tiles), nearest hostile at %d tiles, neutral human %s at %d tiles not picked=%d",
            at_aim_seen_target != NULL ? critter_name(at_aim_seen_target) : "(none)", hostileTarget,
            at_aim_seen_target != NULL ? obj_dist(obj_dude, at_aim_seen_target) : -1,
            nearest != NULL ? obj_dist(obj_dude, nearest) : -1,
            at_aim_human != NULL ? critter_name(at_aim_human) : "(none)",
            at_aim_human != NULL ? obj_dist(obj_dude, at_aim_human) : -1,
            at_aim_seen_target != at_aim_human);
        at_menu_expected = false;

        if (at_aim_human == NULL) {
            at_check(C_AIM_ANATOMY, false, "no human critter prototype with a called shot picture found");
            at_aim_entry_phase = 9;
            break;
        }
        // A neutral NPC deliberately locked is a valid target.
        rt_debug_set_lock(at_aim_human);
        at_aim_entry_phase = 5;
        at_aim_time = now;
        break;
    }
    case 5:
        if (now - at_aim_time < 300) {
            break;
        }
        at_aim_seen_open = false;
        at_expect_menu("aim_03_human", at_menu_aim_look_script);
        at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
        at_aim_entry_phase = 6;
        at_aim_time = now;
        break;
    case 6: {
        if (!at_menu_seen || !at_menu_done || now - at_aim_time < 400) {
            break;
        }
        at_aim_log_anatomy(at_aim_human, "human");
        int humanParts = at_aim_seen_available;
        int differing = 0;
        int ratParts = 0;
        for (int part = 0; part < RT_AIMED_PARTS; part++) {
            int location = combat_rt_called_shot_location(part);
            const char* human = rt_aimed_part_name(at_aim_human, location);
            const char* rat = rt_aimed_part_name(at_aim_rat_names, location);
            ratParts += rat != NULL ? 1 : 0;
            if ((human == NULL) != (rat == NULL) || (human != NULL && strcmp(human, rat) != 0)) {
                differing++;
            }
        }
        at_check(C_AIM_ANATOMY, at_aim_seen_open && at_aim_seen_target == at_aim_human && differing > 0 && humanParts > 0,
            "human: %d body parts shown, rat: %d; %d of 8 names differ (from combat.msg per creature art)",
            humanParts, ratParts, differing);
        at_menu_expected = false;
        // The human never became hostile by being aimed at.
        if (rt_is_hostile(at_aim_human) || rt_is_engaged(at_aim_human)) {
            at_check(C_AIM_ANATOMY, false, "the neutral human turned hostile");
        }
        at_aim_entry_phase = 9;
        break;
    }
    case 9: {
        Rect rect;
        if (at_aim_human != NULL) {
            rt_debug_set_lock(NULL);
            obj_erase_object(at_aim_human, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            at_aim_human = NULL;
        }
        return true;
    }
    }
    return false;
}

// The queue: D-pad, A, X, a flick to the second rat, the AP limit, RT.
static int at_aim_q_selected_before;
static int at_aim_q_selected_after;
static bool at_aim_q_dpad_ok;
static bool at_aim_q_odds_ok;
static int at_aim_q_after_two;
static int at_aim_q_after_undo;
static int at_aim_q_reserved_one;
static int at_aim_q_free_one;
static bool at_aim_q_switched;
static int at_aim_q_refusals_before;
static bool at_aim_q_refused_ap;
static int at_aim_q_final_length;
static int at_aim_q_cost;
static int at_aim_q_free_start;
static RtAimedEntry at_aim_q_plan[RT_AIMED_QUEUE_MAX];
static int at_aim_q_favorites_before;
static int at_aim_q_cycles_before;

static bool at_menu_aim_queue_script(unsigned int elapsed)
{
    const RtAimedState* state = at_aim();
    if (!state->selecting) {
        return false;
    }

    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 6000) {
        at_log("aim queue script: phase %d timed out", at_pb_phase);
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }

    switch (at_pb_phase) {
    case 0:
        if (elapsed > 600) {
            at_aim_q_cost = state->apCost;
            at_aim_q_free_start = state->availableAp;
            at_aim_q_selected_before = state->selected;
            at_aim_q_favorites_before = rt_favorites_get_state()->opens;
            at_aim_q_cycles_before = rt_get_stats()->interactCycles;
            at_pb_tap(GAMEPAD_BUTTON_DPAD_DOWN);
            at_pb_next(1);
        }
        break;
    case 1:
        if (at_pb_ready(300)) {
            at_aim_q_selected_after = state->selected;
            at_aim_q_dpad_ok = at_aim_q_selected_after != at_aim_q_selected_before
                && rt_favorites_get_state()->opens == at_aim_q_favorites_before
                && rt_get_stats()->interactCycles == at_aim_q_cycles_before;
            // The shown hit chance is the original calculation for this part.
            int location = state->locations[state->selected];
            int expected = determine_to_hit(obj_dude, state->target, location, state->hitMode);
            at_aim_q_odds_ok = state->toHit[state->selected] == expected;
            at_log("aim: part %d -> %d (%s), shown %d%%, determine_to_hit %d%%",
                at_aim_q_selected_before, state->selected, rt_aimed_part_name(state->target, location), state->toHit[state->selected], expected);
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(2);
        }
        break;
    case 2:
        if (at_pb_ready(300)) {
            at_aim_q_reserved_one = state->reservedAp;
            at_aim_q_free_one = state->availableAp;
            // The same body part again.
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(3);
        }
        break;
    case 3:
        if (at_pb_ready(300)) {
            at_aim_q_after_two = state->queueLength;
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(4);
        }
        break;
    case 4:
        if (at_pb_ready(300)) {
            at_aim_q_after_undo = state->queueLength;
            // Flick the right stick toward the second rat.
            at_aim_direction(at_aim_rats[0], at_aim_rats[1], &at_aim_flick_x, &at_aim_flick_y);
            at_pad.rightX = at_aim_flick_x;
            at_pad.rightY = at_aim_flick_y;
            at_pb_last_press = SDL_GetTicks();
            at_pb_next(5);
        }
        break;
    case 5:
        if (at_pb_ready(150)) {
            at_pad.rightX = 0.0f;
            at_pad.rightY = 0.0f;
            at_pb_next(6);
        }
        break;
    case 6:
        if (at_pb_ready(400)) {
            at_aim_q_switched = state->target == at_aim_rats[1];
            // Another body part of the second rat, twice (AP allows three).
            at_pb_tap(GAMEPAD_BUTTON_DPAD_RIGHT);
            at_pb_next(7);
        }
        break;
    case 7:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(8);
        }
        break;
    case 8:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(9);
        }
        break;
    case 9:
        if (at_pb_ready(300)) {
            // One more than the AP allows.
            at_aim_q_refusals_before = state->refusals;
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(10);
        }
        break;
    case 10:
        if (at_pb_ready(400)) {
            at_aim_q_refused_ap = state->refusals == at_aim_q_refusals_before + 1 && strstr(state->message, "AP") != NULL;
            at_aim_q_final_length = state->queueLength;
            memcpy(at_aim_q_plan, state->queue, sizeof(at_aim_q_plan));
            at_log("aim: queue %d, reserved %d AP, free %d AP, message '%s'", state->queueLength, state->reservedAp, state->availableAp, state->message);
            at_screenshot("aim_04_queue");
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            at_pb_next(11);
        }
        break;
    case 11:
        return true;
    }
    return false;
}

static int at_aim_x_phase;
static float at_aim_x_ap_start;
static float at_aim_x_ap_max;
static int at_aim_x_aimed_before;
static int at_aim_x_committed_before;
static int at_aim_x_seen;
static bool at_aim_x_order_ok;
static int at_aim_x_ammo_before;
static int at_aim_x_moves_before;
static int at_aim_x_tile_before;
static int at_aim_x_attacks_before;
static bool at_aim_x_blocked_ok;
static int at_aim_x_last_aimed;

static bool step_aim_queue(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        at_aim_begin_fixture();
        at_aim_x_phase = 0;
        at_aim_time = now;
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_check(C_AIM_EXECUTE, false, "timed out in phase %d (executing %d, next %d of %d)",
            at_aim_x_phase, rt_aimed_is_executing(), at_aim()->next, at_aim()->queueLength);
        at_menu_expected = false;
        return true;
    }

    at_heal();
    const RtStats* stats = rt_get_stats();
    switch (at_aim_x_phase) {
    case 0:
        if (!at_aim_released() || !at_aim_pistol_ready() || !at_aim_ensure_rats() || rt_get_pending_count() > 0) {
            break;
        }
        at_aim_full_ap();
        rt_debug_set_lock(at_aim_rats[0]);
        at_aim_x_phase = 1;
        break;
    case 1:
        at_pb_next(0);
        at_pb_last_press = now;
        at_expect_menu(NULL, at_menu_aim_queue_script);
        at_aim_x_ammo_before = item_w_curr_ammo(at_aim_pistol);
        at_aim_x_aimed_before = stats->aimedAttacks;
        at_aim_x_committed_before = at_aim()->committed;
        at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
        at_aim_x_phase = 2;
        break;
    case 2:
        // The first gameplay tick after RT closed the screen.
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        at_aim_x_ap_start = rt_get_dude_ap();
        at_aim_x_ap_max = at_aim_x_ap_start;
        at_aim_x_seen = 0;
        at_aim_x_order_ok = true;
        at_aim_x_last_aimed = stats->aimedAttacks;
        at_aim_x_moves_before = stats->moveRequests;
        at_aim_x_tile_before = obj_dude->tile;
        at_aim_x_attacks_before = stats->playerAttacks;
        at_check(C_AIM_QUEUE,
            at_aim_q_dpad_ok && at_aim_q_reserved_one == at_aim_q_cost && at_aim_q_free_one == at_aim_q_free_start - at_aim_q_cost
                && at_aim_q_after_two == 2 && at_aim_q_after_undo == 1 && at_aim_q_switched && at_aim_q_final_length == 3,
            "D-pad moved the part=%d (no favorites/cycling), 1 queued: %d AP reserved, %d of %d free; same part twice -> %d, X -> %d, flick switched to rat 2=%d, final queue %d",
            at_aim_q_dpad_ok, at_aim_q_reserved_one, at_aim_q_free_one, at_aim_q_free_start, at_aim_q_after_two, at_aim_q_after_undo, at_aim_q_switched, at_aim_q_final_length);
        at_check(C_AIM_LIMITS, at_aim_q_refused_ap, "a 4th aimed attack (%d AP each) with %d AP free was refused=%d",
            at_aim_q_cost, at_aim_q_free_start - 3 * at_aim_q_cost, at_aim_q_refused_ap);
        if (!rt_aimed_is_executing()) {
            at_check(C_AIM_EXECUTE, false, "RT did not start the queue");
            return true;
        }
        // Try to walk and fire while the queue runs.
        at_stick_open_direction();
        at_aim_x_phase = 3;
        at_aim_time = now;
        break;
    case 3:
        if (now - at_aim_time < 700) {
            at_stick_open_direction();
            if ((now - at_aim_time) % 300 < 40) {
                at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            }
        } else {
            at_stick_release();
        }

        // Each aimed attack as it starts: planned target and body part, in
        // order.
        if (stats->aimedAttacks != at_aim_x_last_aimed) {
            at_aim_x_last_aimed = stats->aimedAttacks;
            const RtAimedEntry* planned = &(at_aim_q_plan[at_aim_x_seen]);
            bool same = stats->lastAimedDefender == planned->target && stats->lastAimedLocation == planned->hitLocation;
            at_log("aim: attack %d on %s, location %d (planned %s, %d), to-hit %d%%, flags 0x%X",
                at_aim_x_seen + 1, critter_name(stats->lastAimedDefender), stats->lastAimedLocation,
                critter_name(planned->target), planned->hitLocation, stats->lastAimedToHit, stats->lastAimedFlags);
            at_aim_x_order_ok = at_aim_x_order_ok && same;
            at_aim_x_seen++;
        }
        if (rt_get_dude_ap() > at_aim_x_ap_max) {
            at_aim_x_ap_max = rt_get_dude_ap();
        }

        if (rt_aimed_is_executing()) {
            break;
        }

        {
            int committed = at_aim()->committed - at_aim_x_committed_before;
            float spent = at_aim_x_ap_start - rt_get_dude_ap();
            float expected = (float)(committed * at_aim_q_cost);
            // AP never went up during execution; it went down by exactly the
            // committed attacks' cost.
            bool apOk = at_aim_x_ap_max <= at_aim_x_ap_start + 0.001f && fabsf(spent - expected) < 0.01f;
            at_aim_x_blocked_ok = stats->moveRequests == at_aim_x_moves_before
                && obj_dude->tile == at_aim_x_tile_before
                && stats->playerAttacks - at_aim_x_attacks_before == committed;
            at_check(C_AIM_AP, apOk && committed == 3,
                "AP %.2f at start, highest %.2f during execution, %.2f at the end: spent %.2f for %d committed attacks of %d AP",
                at_aim_x_ap_start, at_aim_x_ap_max, rt_get_dude_ap(), spent, committed, at_aim_q_cost);
            at_flag_a = at_aim_x_order_ok && at_aim_x_seen == 3 && committed == 3 && at_aim_x_blocked_ok;
            at_count_a = committed;
        }
        at_aim_x_phase = 4;
        at_aim_time = now;
        break;
    case 4:
        // Ammunition is taken when each attack lands.
        if (rt_get_pending_count() > 0 && now - at_aim_time < 5000) {
            break;
        }
        at_count_b = at_aim_x_ammo_before - item_w_curr_ammo(at_aim_pistol);
        // The rats (hostile now) would keep the player in hit reactions:
        // they go, so the walk shows the controls are back (new ones are
        // spawned for the next step).
        at_clear_hostiles();
        at_aim_rats[0] = NULL;
        at_aim_rats[1] = NULL;
        at_ap_a = rt_get_dude_ap();
        at_tile_a = obj_dude->tile;
        at_stick_open_direction();
        at_aim_x_phase = 5;
        at_aim_time = now;
        break;
    case 5:
        // Controls are back: the stick walks; AP regenerates again.
        at_stick_open_direction();
        if (now - at_aim_time < 1500) {
            break;
        }
        at_stick_release();
        {
            bool walks = obj_dude->tile != at_tile_a;
            bool regen = rt_get_dude_ap() > at_ap_a;
            at_check(C_AIM_EXECUTE, at_flag_a && at_count_b == at_count_a && walks && regen,
                "%d of 3 attacks started in planned order on planned targets/parts=%d; walking/RT blocked during execution=%d; rounds used %d; afterwards walks=%d, AP regenerates=%d",
                at_aim_x_seen, at_aim_x_order_ok, at_aim_x_blocked_ok, at_count_b, walks, regen);
        }
        at_screenshot("aim_05_after");
        return true;
    }
    return false;
}

// Hit chances and criticals: the original attack calculation (the one each
// queued attack uses) run many times per body part, without animations.
static bool step_aim_odds(unsigned int elapsed, bool first, bool timedOut)
{
    if (!first) {
        return true;
    }

    at_heal();
    if (!at_aim_ensure_rats() || at_aim_pistol == NULL) {
        at_check(C_AIM_ODDS, false, "no target or pistol for the calculation test");
        return true;
    }

    int hitMode = at_current_hit_mode();
    Object* target = at_aim_rats[0];

    // Fixture: the rat right next to the player, so range and darkness
    // don't push every body part to 0%.
    for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
        int tile = tile_num_in_direction(obj_dude->tile, rotation, 1);
        if (tile != -1 && obj_blocking_at(NULL, tile, obj_dude->elevation) == NULL) {
            Rect rect;
            anim_rt_end_all(target);
            obj_move_to_tile(target, tile, obj_dude->elevation, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            break;
        }
    }

    static const int locations[] = { HIT_LOCATION_TORSO, HIT_LOCATION_EYES, HIT_LOCATION_HEAD, HIT_LOCATION_LEFT_ARM, HIT_LOCATION_RIGHT_LEG };
    const int trials = 2000;
    int ammo = item_w_curr_ammo(at_aim_pistol);

    bool rateOk = true;
    bool effectsOk = true;
    bool notGuaranteed = true;
    int torsoCrits = 0;
    int torsoHits = 0;
    int torsoToHit = 0;
    int aimedCrits = 0;
    int aimedHits = 0;
    int aimedLocation = -1;
    bool penaltyOk = true;
    char summary[700] = "";

    // The original game rolls no criticals on the first day (see
    // `roll_check_critical`); this run is on day 1, so first that rule, then
    // the same calculations with the clock on day 2 (put back afterwards; no
    // game event runs in between).
    int savedTime = game_time();
    int dayOneCrits = 0;
    for (int trial = 0; trial < 500; trial++) {
        Attack attack;
        combat_ctd_init(&attack, obj_dude, target, hitMode, HIT_LOCATION_TORSO);
        if (combat_rt_compute_attack(&attack) != -1 && (attack.attackerFlags & DAM_CRITICAL) != 0) {
            dayOneCrits++;
        }
    }
    if (savedTime / GAME_TIME_TICKS_PER_DAY >= 1) {
        dayOneCrits = 0;
    }
    set_game_time(savedTime + GAME_TIME_TICKS_PER_DAY);

    for (int location : locations) {
        int toHit = determine_to_hit(obj_dude, target, location, hitMode);
        int hits = 0;
        int crits = 0;
        int cripLeg = 0;
        int cripArm = 0;
        int blind = 0;
        for (int trial = 0; trial < trials; trial++) {
            Attack attack;
            combat_ctd_init(&attack, obj_dude, target, hitMode, location);
            if (combat_rt_compute_attack(&attack) == -1) {
                continue;
            }
            // Misses can land on something else in the line of fire.
            if (attack.defender != target || (attack.attackerFlags & DAM_HIT) == 0) {
                continue;
            }
            hits++;
            if ((attack.attackerFlags & DAM_CRITICAL) != 0) {
                crits++;
            }
            if ((attack.defenderFlags & (DAM_CRIP_LEG_LEFT | DAM_CRIP_LEG_RIGHT)) != 0) {
                cripLeg++;
            }
            if ((attack.defenderFlags & (DAM_CRIP_ARM_LEFT | DAM_CRIP_ARM_RIGHT)) != 0) {
                cripArm++;
            }
            if ((attack.defenderFlags & DAM_BLIND) != 0) {
                blind++;
            }
        }

        // The roll is against the chance as computed; below 0 never hits.
        int expected = std::max(0, std::min(toHit, 95));
        int rate = hits * 100 / trials;
        bool legs = location == HIT_LOCATION_LEFT_LEG || location == HIT_LOCATION_RIGHT_LEG;
        bool arms = location == HIT_LOCATION_LEFT_ARM || location == HIT_LOCATION_RIGHT_ARM;
        bool eyesOrHead = location == HIT_LOCATION_EYES || location == HIT_LOCATION_HEAD;
        rateOk = rateOk && abs(rate - expected) <= 5;
        effectsOk = effectsOk && (legs || cripLeg == 0) && (arms || cripArm == 0) && (eyesOrHead || blind == 0);
        notGuaranteed = notGuaranteed && hits < trials && (hits == 0 || crits < hits);
        if (location == HIT_LOCATION_TORSO) {
            torsoCrits = crits;
            torsoHits = hits;
            torsoToHit = toHit;
        } else {
            // Every called body part is harder to hit than the torso.
            penaltyOk = penaltyOk && toHit < torsoToHit;
            if (hits > aimedHits) {
                aimedHits = hits;
                aimedCrits = crits;
                aimedLocation = location;
            }
        }

        char part[130];
        snprintf(part, sizeof(part), "%s: shown %d%%, hit %d%%, crits %d, crippled leg/arm %d/%d, blinded %d; ",
            rt_aimed_part_name(target, location), toHit, rate, crits, cripLeg, cripArm, blind);
        SDL_strlcat(summary, part, sizeof(summary));
    }

    set_game_time(savedTime);

    // The test computed attacks only: no ammunition moved.
    item_w_set_curr_ammo(at_aim_pistol, ammo);
    at_log("aim odds (%d trials per part, target adjacent): %s", trials, summary);

    // Aiming at a body part costs hit chance but raises the critical chance
    // (the original applies the body part penalty to both).
    bool critsOk = aimedHits >= 50 && torsoHits > 0 && aimedCrits * torsoHits > torsoCrits * aimedHits;
    at_check(C_AIM_ODDS, dayOneCrits == 0 && rateOk && effectsOk && notGuaranteed && penaltyOk && critsOk,
        "day 1: %d criticals (original: none before day 2); day 2: hit rate within 5 points of the shown chance=%d, effects only on their body part=%d, no guaranteed hit/crit=%d, body parts harder than the torso=%d, more criticals per hit on %s (%d/%d vs torso %d/%d)=%d",
        dayOneCrits, rateOk, effectsOk, notGuaranteed, penaltyOk,
        aimedLocation != -1 ? rt_aimed_part_name(target, aimedLocation) : "?", aimedCrits, aimedHits, torsoCrits, torsoHits, critsOk);
    return true;
}

// Cancel while planning, a dead target during execution, B during
// execution, knockdown and the map-load reset.
static bool at_aim_i_cancel_with_lb;
static int at_aim_i_cancelled_before;

static bool at_menu_aim_cancel_script(unsigned int elapsed)
{
    const RtAimedState* state = at_aim();
    if (!state->selecting) {
        return false;
    }
    switch (at_pb_phase) {
    case 0:
        if (elapsed > 500) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(1);
        }
        break;
    case 1:
        if (at_pb_ready(400)) {
            at_count_b = state->queueLength;
            at_pb_tap(at_aim_i_cancel_with_lb ? GAMEPAD_BUTTON_LEFT_SHOULDER : GAMEPAD_BUTTON_B);
            at_pb_next(2);
        }
        break;
    case 2:
        return true;
    }
    return false;
}

// Queues `count` attacks (A each), switching to the second rat first when
// `second` is set, then RT.
static int at_aim_i_count;
static bool at_aim_i_second;

static bool at_menu_aim_run_script(unsigned int elapsed)
{
    const RtAimedState* state = at_aim();
    if (!state->selecting) {
        return false;
    }
    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 6000) {
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }

    if (at_pb_phase == 0) {
        if (elapsed > 500) {
            at_pb_next(1);
        }
    } else if (at_pb_phase == 1) {
        if (at_pb_ready(300)) {
            if (state->queueLength == 1 && at_aim_i_second && state->target != at_aim_rats[1] && at_aim_flick_tries++ < 3) {
                at_aim_direction(state->target, at_aim_rats[1], &at_aim_flick_x, &at_aim_flick_y);
                at_pad.rightX = at_aim_flick_x;
                at_pad.rightY = at_aim_flick_y;
                at_pb_last_press = SDL_GetTicks();
                at_pb_next(2);
            } else if (state->queueLength < at_aim_i_count) {
                at_pb_tap(GAMEPAD_BUTTON_A);
            } else {
                at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
                at_pb_next(3);
            }
        }
    } else if (at_pb_phase == 2) {
        if (at_pb_ready(150)) {
            at_pad.rightX = 0.0f;
            at_pad.rightY = 0.0f;
            at_pb_next(1);
            at_pb_last_press = SDL_GetTicks();
        }
    } else {
        return true;
    }
    return false;
}

static int at_aim_i_phase;
static float at_aim_i_ap;
static int at_aim_i_committed;
static int at_aim_i_skipped;
static int at_aim_i_cost;
static int at_aim_i_attacks;

static void at_aim_i_open(AtMenuScript* script)
{
    at_aim_flick_tries = 0;
    at_pb_next(0);
    at_pb_last_press = SDL_GetTicks();
    at_expect_menu(NULL, script);
    at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
}

static bool step_aim_interrupt(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        at_aim_begin_fixture();
        at_aim_i_phase = 0;
        at_flag_a = false;
        at_flag_b = false;
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_menu_expected = false;
        obj_dude->data.critter.combat.results &= ~DAM_KNOCKED_DOWN;
        at_check(C_AIM_INTERRUPT, false, "timed out in phase %d", at_aim_i_phase);
        return true;
    }

    at_heal();
    const RtAimedState* state = at_aim();
    switch (at_aim_i_phase) {
    case 0:
    case 10:
    case 20:
    case 30:
    case 40:
        // Ready: pistol, rats, full AP, lock on the first rat, nothing in
        // flight.
        if (!at_aim_released() || !at_aim_pistol_ready() || !at_aim_ensure_rats() || rt_get_pending_count() > 0 || rt_aimed_is_executing()) {
            static unsigned int lastLog = 0;
            if (now - lastLog > 3000) {
                lastLog = now;
                at_log("aim interrupt: waiting (pistol in hand %d, anim %d, busy %d, pending %d, executing %d, rats %d/%d)",
                    at_aim_pistol != NULL && at_wield(at_aim_pistol), at_current_anim(), anim_busy(obj_dude) != 0,
                    rt_get_pending_count(), rt_aimed_is_executing(), at_aim_rats[0] != NULL, at_aim_rats[1] != NULL);
            }
            break;
        }
        at_aim_full_ap();
        rt_debug_set_lock(at_aim_rats[0]);
        at_aim_i_ap = rt_get_dude_ap();
        at_aim_i_committed = state->committed;
        at_aim_i_skipped = state->skipped;
        at_aim_i_attacks = rt_get_stats()->aimedAttacks;
        at_aim_i_cost = item_w_mp_cost(obj_dude, at_current_hit_mode(), true);
        at_aim_i_cancelled_before = state->cancelled;
        at_aim_i_phase++;
        break;

    // Cancel with B, then with LB: nothing spent.
    case 1:
    case 11:
        at_aim_i_cancel_with_lb = at_aim_i_phase == 11;
        at_aim_i_open(at_menu_aim_cancel_script);
        at_aim_i_phase++;
        break;
    case 2:
    case 12:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        {
            bool ok = at_count_b == 1
                && state->queueLength == 0 && !rt_aimed_is_executing()
                && rt_get_dude_ap() >= at_aim_i_ap - 0.001f
                && rt_get_stats()->aimedAttacks == at_aim_i_attacks
                && state->cancelled == at_aim_i_cancelled_before + 1;
            at_log("aim cancel with %s: queued %d before cancelling, queue now %d, AP %.2f -> %.2f, aimed attacks +%d",
                at_aim_i_cancel_with_lb ? "LB" : "B", at_count_b, state->queueLength, at_aim_i_ap, rt_get_dude_ap(),
                rt_get_stats()->aimedAttacks - at_aim_i_attacks);
            if (at_aim_i_phase == 2) {
                at_flag_a = ok;
            } else {
                at_check(C_AIM_CANCEL, at_flag_a && ok, "B cancel ok=%d, LB cancel ok=%d (one attack queued each time, no AP spent, no attack)", at_flag_a, ok);
            }
        }
        at_aim_i_phase = at_aim_i_phase == 2 ? 10 : 20;
        break;

    // A dead target is skipped: one attack on rat 1, one on rat 2; rat 2
    // dies (harness) once the first attack started.
    case 21:
        at_aim_i_count = 2;
        at_aim_i_second = true;
        at_aim_i_open(at_menu_aim_run_script);
        at_aim_i_phase = 22;
        break;
    case 22:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        at_aim_i_ap = rt_get_dude_ap();
        if (!rt_aimed_is_executing() || state->queueLength != 2 || state->queue[1].target != at_aim_rats[1]) {
            at_check(C_AIM_SKIP_DEAD, false, "queue not set up (executing %d, %d entries)", rt_aimed_is_executing(), state->queueLength);
            at_aim_i_phase = 30;
            break;
        }
        at_obj_a = at_aim_rats[1];
        at_aim_i_phase = 23;
        break;
    case 23:
        if (state->committed - at_aim_i_committed >= 1 && at_obj_a != NULL && !critter_is_dead(at_obj_a)) {
            critter_kill(at_obj_a, -1, true);
            at_log("harness: rat 2 killed while the first aimed attack plays");
        }
        if (rt_aimed_is_executing()) {
            break;
        }
        {
            int committed = state->committed - at_aim_i_committed;
            int skipped = state->skipped - at_aim_i_skipped;
            float spent = at_aim_i_ap - rt_get_dude_ap();
            at_check(C_AIM_SKIP_DEAD, committed == 1 && skipped == 1 && fabsf(spent - at_aim_i_cost) < 0.01f && state->reservedAp == 0,
                "committed %d, skipped %d (target dead), AP spent %.2f (one attack = %d), still reserved %d",
                committed, skipped, spent, at_aim_i_cost, state->reservedAp);
        }
        at_aim_rats[1] = NULL;
        at_aim_i_phase = 30;
        break;

    // B during execution: three queued, B after the first started.
    case 31:
        at_aim_i_count = 3;
        at_aim_i_second = false;
        at_aim_i_open(at_menu_aim_run_script);
        at_aim_i_phase = 32;
        break;
    case 32:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        at_aim_i_ap = rt_get_dude_ap();
        at_aim_i_phase = 33;
        break;
    case 33:
        if (state->committed - at_aim_i_committed >= 1) {
            at_press(GAMEPAD_BUTTON_B);
            at_aim_i_phase = 34;
            at_time_a = now;
        } else if (!rt_aimed_is_executing()) {
            at_aim_i_phase = 34;
            at_time_a = now;
        }
        break;
    case 34:
        // Checked on the first tick after the cancel (AP regenerates again
        // from then on).
        if (rt_aimed_is_executing() && now - at_time_a < 3000) {
            break;
        }
        {
            int committed = state->committed - at_aim_i_committed;
            float spent = at_aim_i_ap - rt_get_dude_ap();
            at_flag_b = !rt_aimed_is_executing() && committed >= 1 && committed < 3
                && fabsf(spent - committed * at_aim_i_cost) < 0.01f && state->reservedAp == 0;
            at_log("aim B during execution: committed %d of 3, AP spent %.2f, executing %d, reserved %d",
                committed, spent, rt_aimed_is_executing(), state->reservedAp);
        }
        // Afterwards an ordinary RT press attacks normally.
        at_aim_i_attacks = rt_get_stats()->playerAttacks;
        at_count_a = rt_get_stats()->aimedAttacks;
        at_aim_i_phase = 35;
        at_time_a = now;
        break;
    case 35:
        if (rt_get_pending_count() > 0 && now - at_time_a < 4000) {
            break;
        }
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_aim_i_phase = 36;
        at_time_a = now;
        break;
    case 36:
        if (now - at_time_a < 2000) {
            break;
        }
        at_flag_b = at_flag_b && rt_get_stats()->playerAttacks > at_aim_i_attacks && rt_get_stats()->aimedAttacks == at_count_a;
        at_log("aim: ordinary RT after cancelling -> attacks +%d (aimed +%d)",
            rt_get_stats()->playerAttacks - at_aim_i_attacks, rt_get_stats()->aimedAttacks - at_count_a);
        at_aim_i_phase = 40;
        break;

    // Knockdown during execution, then the map-load reset.
    case 41:
        at_aim_i_count = 3;
        at_aim_i_second = false;
        at_aim_i_open(at_menu_aim_run_script);
        at_aim_i_phase = 42;
        break;
    case 42:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        at_aim_i_phase = 43;
        break;
    case 43:
        if (state->committed - at_aim_i_committed >= 1 || !rt_aimed_is_executing()) {
            // Fixture: the player is knocked down (no animation needed).
            obj_dude->data.critter.combat.results |= DAM_KNOCKED_DOWN;
            at_aim_i_phase = 44;
            at_time_a = now;
        }
        break;
    case 44:
        if (now - at_time_a < 300) {
            break;
        }
        at_count_a = !rt_aimed_is_executing() && state->reservedAp == 0 ? 1 : 0;
        at_log("aim knockdown: executing %d, reserved %d, message '%s'", rt_aimed_is_executing(), state->reservedAp, state->message);
        obj_dude->data.critter.combat.results &= ~DAM_KNOCKED_DOWN;
        at_aim_i_phase = 45;
        at_time_a = now;
        break;
    case 45:
        if (rt_get_pending_count() > 0 && now - at_time_a < 4000) {
            break;
        }
        // Map load: queue two and reset as `map_load` does.
        if (!at_aim_released() || !at_aim_pistol_ready() || !at_aim_ensure_rats()) {
            break;
        }
        at_aim_full_ap();
        rt_debug_set_lock(at_aim_rats[0]);
        at_aim_i_count = 2;
        at_aim_i_second = false;
        at_aim_i_committed = state->committed;
        at_aim_i_open(at_menu_aim_run_script);
        at_aim_i_phase = 46;
        break;
    case 46:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        rt_reset();
        at_aim_i_phase = 47;
        at_time_a = now;
        break;
    case 47:
        if (now - at_time_a < 300) {
            break;
        }
        {
            bool resetOk = !rt_aimed_is_executing() && state->queueLength == 0 && state->reservedAp == 0;
            at_tile_a = obj_dude->tile;
            at_stick_open_direction();
            at_aim_i_phase = 48;
            at_time_a = now;
            at_count_b = resetOk ? 1 : 0;
        }
        break;
    case 48:
        at_stick_open_direction();
        if (now - at_time_a < 3000) {
            break;
        }
        at_stick_release();
        {
            bool walks = obj_dude->tile != at_tile_a;
            at_check(C_AIM_INTERRUPT, at_flag_b && at_count_a == 1 && at_count_b == 1 && walks,
                "B mid-queue cancelled the rest, AP only for committed, RT normal afterwards=%d; knockdown ended the queue=%d; map reset cleared it=%d; walks afterwards=%d",
                at_flag_b, at_count_a, at_count_b, walks);
        }
        return true;
    }
    return false;
}

// Not enough ammunition, blocked and out-of-range targets, burst mode, and
// a controller disconnect inside the screen.
static bool at_menu_aim_limit_script(unsigned int elapsed)
{
    const RtAimedState* state = at_aim();
    if (!state->selecting) {
        return false;
    }
    switch (at_pb_phase) {
    case 0:
        if (elapsed > 500) {
            at_count_a = state->refusals;
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(1);
        }
        break;
    case 1:
        if (at_pb_ready(350)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(2);
        }
        break;
    case 2:
        if (at_pb_ready(400)) {
            at_count_b = state->queueLength;
            at_count_a = state->refusals - at_count_a;
            SDL_strlcpy(at_aim_message, state->message, sizeof(at_aim_message));
            at_log("aim limit: badShot %d, queue %d, refusals +%d, message '%s'", state->badShot, state->queueLength, at_count_a, state->message);
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(3);
        }
        break;
    case 3:
        return true;
    }
    return false;
}

// Disconnect: the controller is pulled with RT held, plugged back in; then
// A queues one attack and B closes.
static bool at_menu_aim_disconnect_script(unsigned int elapsed)
{
    const RtAimedState* state = at_aim();
    if (!state->selecting) {
        return false;
    }
    switch (at_pb_phase) {
    case 0:
        if (elapsed > 500) {
            at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
            at_pad.buttons[GAMEPAD_BUTTON_DPAD_DOWN] = true;
            at_pb_next(1);
        }
        break;
    case 1:
        if (SDL_GetTicks() - at_pb_phase_time > 300) {
            at_pad.connected = false;
            at_pb_next(2);
        }
        break;
    case 2:
        if (SDL_GetTicks() - at_pb_phase_time > 800) {
            // Back, with RT and the D-pad still held: not new presses.
            at_pad.connected = true;
            at_count_a = state->queueLength;
            at_count_b = state->selected;
            at_pb_next(3);
        }
        break;
    case 3:
        if (SDL_GetTicks() - at_pb_phase_time > 600) {
            at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = false;
            at_pad.buttons[GAMEPAD_BUTTON_DPAD_DOWN] = false;
            at_flag_b = state->selecting && state->queueLength == at_count_a && state->selected == at_count_b;
            at_pb_last_press = SDL_GetTicks();
            at_pb_next(4);
        }
        break;
    case 4:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(5);
        }
        break;
    case 5:
        if (at_pb_ready(400)) {
            at_flag_b = at_flag_b && state->queueLength == at_count_a + 1;
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(6);
        }
        break;
    case 6:
        return true;
    }
    return false;
}

static int at_aim_l_phase;
static Object* at_aim_l_covered = NULL;
static bool at_aim_l_blocked_tested = false;

static bool step_aim_limits(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        at_aim_begin_fixture();
        at_aim_l_phase = 0;
        at_aim_l_covered = NULL;
        at_aim_l_blocked_tested = false;
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_menu_expected = false;
        at_check(C_AIM_BLOCKED, at_results[C_AIM_BLOCKED] == 1, "timed out in phase %d", at_aim_l_phase);
        at_aim_end_fixture();
        return true;
    }

    at_heal();
    const RtAimedState* state = at_aim();
    switch (at_aim_l_phase) {
    case 0:
        // One round left: the second attack has no ammunition.
        if (!at_aim_released() || !at_aim_pistol_ready() || !at_aim_ensure_rats() || rt_get_pending_count() > 0) {
            break;
        }
        at_aim_full_ap();
        item_w_set_curr_ammo(at_aim_pistol, 1);
        rt_debug_set_lock(at_aim_rats[0]);
        at_aim_l_phase = 1;
        break;
    case 1:
        at_aim_i_open(at_menu_aim_limit_script);
        at_aim_l_phase = 2;
        break;
    case 2:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        at_check(C_AIM_LIMITS, at_count_b == 1 && at_count_a == 1 && strstr(at_aim_message, "ammunition") != NULL,
            "1 round loaded: queued %d, refused %d ('%s')", at_count_b, at_count_a, at_aim_message);
        item_w_set_curr_ammo(at_aim_pistol, item_w_max_ammo(at_aim_pistol));

        // A rat behind cover.
        {
            int tile = at_covered_tile(8);
            at_aim_l_covered = tile != -1 ? at_spawn_rat_at(tile, false) : NULL;
        }
        if (at_aim_l_covered == NULL) {
            at_log("aim: no covered tile nearby; blocked case not tested");
            at_aim_l_phase = 5;
            break;
        }
        rt_debug_set_lock(at_aim_l_covered);
        at_aim_l_phase = 3;
        at_time_a = now;
        break;
    case 3:
        if (now - at_time_a < 200) {
            break;
        }
        at_aim_i_open(at_menu_aim_limit_script);
        at_aim_l_phase = 4;
        break;
    case 4:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        at_aim_l_blocked_tested = true;
        at_flag_a = state->badShot == COMBAT_BAD_SHOT_AIM_BLOCKED && at_count_b == 0 && at_count_a == 2;
        at_log("aim blocked: badShot %d, queued %d, refused %d", state->badShot, at_count_b, at_count_a);
        {
            Rect rect;
            rt_debug_set_lock(NULL);
            obj_erase_object(at_aim_l_covered, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            at_aim_l_covered = NULL;
        }
        at_aim_l_phase = 5;
        break;
    case 5:
        // Unarmed against a calm rat 3+ tiles away: out of range.
        if (!at_unarmed() || anim_busy(obj_dude)) {
            break;
        }
        at_aim_l_covered = at_spawn_rat_at(at_aim_open_tile(3, 6, NULL), false);
        if (at_aim_l_covered == NULL) {
            at_check(C_AIM_BLOCKED, false, "no free tile for the out-of-range rat");
            at_aim_l_phase = 8;
            break;
        }
        rt_debug_set_lock(at_aim_l_covered);
        at_aim_l_phase = 6;
        at_time_a = now;
        break;
    case 6:
        if (now - at_time_a < 200) {
            break;
        }
        at_aim_i_open(at_menu_aim_limit_script);
        at_aim_l_phase = 7;
        break;
    case 7:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        {
            bool range = state->badShot == COMBAT_BAD_SHOT_OUT_OF_RANGE && at_count_b == 0 && at_count_a == 2;
            // A blocked target is checked only where cover was found; the
            // note says so otherwise.
            bool blockedOk = !at_aim_l_blocked_tested || at_flag_a;
            at_check(C_AIM_BLOCKED, range && blockedOk,
                "blocked target tested=%d refused=%d; unarmed at %d tiles: out of range shown=%d, nothing queued=%d",
                at_aim_l_blocked_tested, at_flag_a, rt_get_lock() != NULL ? obj_dist(obj_dude, rt_get_lock()) : -1,
                state->badShot == COMBAT_BAD_SHOT_OUT_OF_RANGE, at_count_b == 0);
        }
        if (at_aim_l_covered != NULL) {
            Rect rect;
            rt_debug_set_lock(NULL);
            obj_erase_object(at_aim_l_covered, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            at_aim_l_covered = NULL;
        }
        at_aim_l_phase = 8;
        break;
    case 8:
        // Burst mode: refused with a reason.
        if (at_burst_weapon == NULL) {
            at_burst_weapon = at_create_burst_weapon();
        }
        if (!at_wield(at_burst_weapon) || anim_busy(obj_dude)) {
            break;
        }
        if (at_current_anim() != ANIM_FIRE_BURST) {
            static unsigned int lastPress = 0;
            if (now - lastPress > 600) {
                lastPress = now;
                at_press(GAMEPAD_BUTTON_BACK);
            }
            break;
        }
        at_count_a = state->refusals;
        at_expect_menu(NULL, NULL);
        at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
        at_aim_l_phase = 9;
        at_time_a = now;
        break;
    case 9:
        if (now - at_time_a < 800) {
            break;
        }
        at_check(C_AIM_UNSUPPORTED, !at_menu_seen && state->refusals == at_count_a + 1 && strstr(state->message, "Burst") != NULL,
            "burst mode: screen opened=%d, refused=%d, message '%s'", at_menu_seen, state->refusals - at_count_a, state->message);
        at_menu_expected = false;
        at_aim_l_phase = 10;
        break;
    case 10:
        // Disconnect inside the screen.
        if (!at_aim_released() || !at_aim_pistol_ready() || !at_aim_ensure_rats() || rt_get_pending_count() > 0) {
            break;
        }
        at_aim_full_ap();
        rt_debug_set_lock(at_aim_rats[0]);
        at_flag_b = false;
        at_count_a = 0;
        at_aim_i_attacks = rt_get_stats()->playerAttacks;
        at_aim_i_open(at_menu_aim_disconnect_script);
        at_aim_l_phase = 11;
        break;
    case 11:
        if (!at_menu_seen || at_aim()->selecting) {
            break;
        }
        at_menu_expected = false;
        at_time_a = now;
        at_aim_l_phase = 12;
        break;
    case 12:
        if (now - at_time_a < 800) {
            break;
        }
        // Recorded for the combined check with the radial's focus loss.
        at_flag_a = at_flag_b && !rt_aimed_is_executing() && state->queueLength == 0 && rt_get_stats()->playerAttacks == at_aim_i_attacks;
        at_log("aim disconnect: screen kept its state through the disconnect=%d, no attack/queue after closing=%d",
            at_flag_b, at_flag_a);
        at_aim_disconnect_ok = at_flag_a;
        at_aim_end_fixture();
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: favorites
// -----------------------------------------------------------------------------

static int at_fav_saved[RT_FAVORITES_COUNT];
static bool at_fav_saved_valid = false;
static Object* at_fav_stim = NULL;
static Object* at_fav_pistol = NULL;
static Object* at_fav_knife = NULL;
static Object* at_fav_cursor_item = NULL;
static int at_fav_opens = 0;
static int at_fav_refusals = 0;
static bool at_fav_ok[8];

static const RtFavoritesState* at_fav()
{
    return rt_favorites_get_state();
}

static bool at_fav_radial_open()
{
    return at_fav()->open;
}

static Object* at_fav_first_of(int pid, bool unequipped)
{
    Inventory* inventory = &(obj_dude->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        Object* item = inventory->items[index].item;
        if (item->pid == pid && (!unequipped || (item->flags & OBJECT_EQUIPPED) == 0)) {
            return item;
        }
    }
    return NULL;
}

// Pip-Boy (B), Inventory: Y assigns the Stimpak (stick right = Right), the
// pistol (D-pad up = Up), the knife over the pistol (replace), X clears
// the knife's slot, B backs out, the pistol again, the knife to the left
// (D-pad left = Left), and Y on ammunition is refused.
static bool at_menu_fav_assign_script(unsigned int elapsed)
{
    const RtPipboyState* state = at_pb();
    if (!state->open) {
        return false;
    }

    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 8000) {
        at_log("favorites assign script: phase %d timed out (radial open %d)", at_pb_phase, at_fav_radial_open());
        at_pad.leftX = 0.0f;
        at_pad.leftY = 0.0f;
        at_pad.buttons[GAMEPAD_BUTTON_DPAD_UP] = false;
        at_pad.buttons[GAMEPAD_BUTTON_DPAD_LEFT] = false;
        at_pb_tap(GAMEPAD_BUTTON_B);
        if (at_fav_radial_open() || at_pb_phase >= 27) {
            at_pb_next(at_pb_phase >= 27 ? 31 : at_pb_phase + 1);
        } else {
            at_pb_next(31);
        }
        return false;
    }

    const RtFavoritesState* fav = at_fav();
    switch (at_pb_phase) {
    case 0:
        if (elapsed > 500 && at_pb_goto_tab(RT_PIPBOY_TAB_INVENTORY)) {
            at_pb_next(1);
        }
        break;
    case 1:
        if (at_pb_ready(300) && at_pb_goto_page(3)) {
            at_pb_next(2);
        }
        break;
    case 2:
        if (at_pb_ready() && at_pb_goto_item(at_fav_stim) && at_pb_ready()) {
            at_fav_cursor_item = state->cursorItem;
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(3);
        }
        break;
    case 3:
        if (at_fav_radial_open() && at_pb_ready(400)) {
            at_screenshot("fav_01_assign");
            at_pad.leftX = 1.0f;
            at_pad.leftY = 0.0f;
            at_pb_last_press = SDL_GetTicks();
            at_pb_next(4);
        }
        break;
    case 4:
        if (at_pb_ready(300)) {
            at_pad.leftX = 0.0f;
            at_fav_ok[0] = fav->selected == RT_FAVORITES_RIGHT;
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(5);
        }
        break;
    case 5:
        if (!at_fav_radial_open() && at_pb_ready(400)) {
            at_fav_ok[0] = at_fav_ok[0] && fav->slots[RT_FAVORITES_RIGHT] == at_fav_stim->pid && state->cursorItem == at_fav_cursor_item;
            at_log("favorites: Stimpak -> Right=%d, Pip-Boy selection kept=%d", fav->slots[RT_FAVORITES_RIGHT] == at_fav_stim->pid, state->cursorItem == at_fav_cursor_item);
            at_pb_next(6);
        }
        break;
    case 6:
        if (at_pb_ready(300) && at_pb_goto_page(1)) {
            at_pb_next(7);
        }
        break;
    case 7:
        if (at_pb_ready() && at_pb_goto_item(at_fav_pistol) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(8);
        }
        break;
    case 8:
        if (at_fav_radial_open() && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
            at_pb_next(9);
        }
        break;
    case 9:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(10);
        }
        break;
    case 10:
        if (!at_fav_radial_open() && at_pb_ready(400)) {
            at_fav_ok[1] = fav->slots[0] == at_fav_pistol->pid;
            at_pb_next(11);
        }
        break;
    case 11:
        if (at_pb_ready() && at_pb_goto_item(at_fav_knife) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(12);
        }
        break;
    case 12:
        if (at_fav_radial_open() && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
            at_pb_next(13);
        }
        break;
    case 13:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(14);
        }
        break;
    case 14:
        if (!at_fav_radial_open() && at_pb_ready(400)) {
            // Replaced: the knife in slot 1, the pistol in none.
            at_fav_ok[2] = fav->slots[0] == at_fav_knife->pid && rt_favorites_slot_of(at_fav_pistol->pid) == -1;
            at_log("favorites: knife replaced the pistol in slot 1=%d (%s)", at_fav_ok[2], fav->lastMessage);
            at_fav_cursor_item = state->cursorItem;
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(16);
        }
        break;
    case 16:
        // The radial opens on the knife's slot; X clears it.
        if (at_fav_radial_open() && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(17);
        }
        break;
    case 17:
        if (at_pb_ready(300)) {
            at_fav_ok[3] = fav->slots[0] == -1 && at_fav_radial_open();
            at_screenshot("fav_02_cleared");
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(18);
        }
        break;
    case 18:
        if (!at_fav_radial_open() && at_pb_ready(400)) {
            at_fav_ok[3] = at_fav_ok[3] && fav->slots[0] == -1 && state->cursorItem == at_fav_cursor_item && state->open;
            at_log("favorites: X cleared slot 1, B went back to the same item=%d", at_fav_ok[3]);
            at_pb_next(19);
        }
        break;
    case 19:
        if (at_pb_ready() && at_pb_goto_item(at_fav_pistol) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(20);
        }
        break;
    case 20:
        if (at_fav_radial_open() && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
            at_pb_next(21);
        }
        break;
    case 21:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(22);
        }
        break;
    case 22:
        if (!at_fav_radial_open() && at_pb_ready(400) && at_pb_goto_item(at_fav_knife) && at_pb_ready()) {
            at_fav_ok[4] = fav->slots[0] == at_fav_pistol->pid;
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(23);
        }
        break;
    case 23:
        // D-pad left: the Left direction.
        if (at_fav_radial_open() && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_DPAD_LEFT);
            at_pb_next(24);
        }
        break;
    case 24:
        if (at_pb_ready(300)) {
            at_fav_ok[5] = fav->selected == RT_FAVORITES_LEFT;
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(25);
        }
        break;
    case 25:
        if (!at_fav_radial_open() && at_pb_ready(400)) {
            at_fav_ok[5] = at_fav_ok[5] && fav->slots[RT_FAVORITES_LEFT] == at_fav_knife->pid;
            at_pb_next(26);
        }
        break;
    case 26:
        if (at_pb_ready(300) && at_pb_goto_page(4)) {
            at_pb_next(27);
        }
        break;
    case 27:
        if (at_pb_ready(300)) {
            at_fav_opens = fav->opens;
            at_fav_refusals = fav->refusals;
            if (state->listCount > 0) {
                at_pb_tap(GAMEPAD_BUTTON_Y);
            }
            at_pb_next(28);
        }
        break;
    case 28:
        if (at_pb_ready(600)) {
            // The Pip-Boy checks eligibility first and says why (no radial).
            at_fav_ok[6] = state->listCount > 0 && fav->opens == at_fav_opens && !at_fav_radial_open()
                && strstr(state->lastMessage, "can't be a favorite") != NULL;
            at_log("favorites: Y on ammunition refused=%d ('%s')", at_fav_ok[6], state->lastMessage);
            at_pb_next(31);
        }
        break;
    case 31:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_B);
            return true;
        }
        break;
    }
    return false;
}

static bool step_fav_assign(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_release_all();
        at_heal();
        memset(at_fav_ok, 0, sizeof(at_fav_ok));
        at_fav_pistol = at_find_inventory("10mm Pistol");
        at_fav_knife = at_find_inventory("Knife");
        for (int index = 1; index < 600 && at_chem_pid == -1; index++) {
            int pid = (OBJ_TYPE_ITEM << 24) | index;
            Proto* proto;
            if (proto_ptr(pid, &proto) != -1 && proto->item.type == ITEM_TYPE_DRUG
                && SDL_strcasecmp(proto_name(pid), "Stimpak") == 0) {
                at_chem_pid = pid;
            }
        }
        at_fav_stim = at_chem_pid != -1 ? at_pb_find_stack(at_chem_pid) : NULL;
        if (at_fav_stim == NULL && at_chem_pid != -1) {
            Object* chem;
            if (obj_pid_new(&chem, at_chem_pid) != -1 && item_add_force(obj_dude, chem, 3) == 0) {
                at_fav_stim = at_pb_find_stack(at_chem_pid);
            }
        }
        if (at_fav_pistol == NULL || at_fav_knife == NULL || at_fav_stim == NULL) {
            at_check(C_FAV_ASSIGN, false, "fixtures missing (pistol %d, knife %d, Stimpak %d)",
                at_fav_pistol != NULL, at_fav_knife != NULL, at_fav_stim != NULL);
            return true;
        }
        at_flag_a = false;
        return false;
    }

    // The previous step's last press (B closing the Pip-Boy) is over first.
    if (!at_flag_a) {
        if (at_aim_released()) {
            at_flag_a = true;
            at_pb_next(0);
            at_pb_last_press = SDL_GetTicks();
            at_expect_menu(NULL, at_menu_fav_assign_script);
            at_press(GAMEPAD_BUTTON_B);
        }
        return false;
    }

    if ((at_menu_seen && at_menu_done && elapsed > 300) || timedOut) {
        at_check(C_FAV_ASSIGN,
            at_fav_ok[0] && at_fav_ok[1] && at_fav_ok[2] && at_fav_ok[3] && at_fav_ok[4] && at_fav_ok[5] && at_fav_ok[6],
            "Stimpak->Right (stick, selection kept)=%d, pistol->Up (D-pad)=%d, knife replaced pistol=%d, X cleared + B back to the item=%d, pistol again=%d, knife->Left (D-pad)=%d, ammo refused=%d",
            at_fav_ok[0], at_fav_ok[1], at_fav_ok[2], at_fav_ok[3], at_fav_ok[4], at_fav_ok[5], at_fav_ok[6]);
        at_menu_expected = false;
        return true;
    }
    return false;
}

// Direct favorites: a D-pad press activates that direction at once.
static int at_fav_u_phase;
static int at_fav_u_stims;
static int at_fav_u_hp;
static int at_fav_u_chem_uses;
static int at_fav_u_activations;
static int at_fav_u_refusals;
static int at_fav_u_attacks;
static int at_fav_u_interactions;
static int at_fav_u_moves;
static int at_fav_u_tile;
static unsigned int at_fav_u_time;
static bool at_fav_u_ok[8];
static Object* at_fav_removed[16];
static int at_fav_removed_quantity[16];
static int at_fav_removed_count = 0;
static Object* at_fav_u_active = NULL;

// One D-pad press in gameplay, watched for menus.
static void at_fav_press(GamepadButton button)
{
    at_expect_menu(NULL, NULL);
    at_press(button);
    at_fav_u_time = SDL_GetTicks();
}

static Object* at_fav_active_item()
{
    Object* active = NULL;
    intface_get_current_item(&active);
    return active;
}

static bool step_fav_use(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        at_clear_hostiles();
        rt_debug_set_lock(NULL);
        // Nothing to interact with nearby, so left/right are favorites too.
        at_move_to_clear_spot();
        memset(at_fav_u_ok, 0, sizeof(at_fav_u_ok));
        at_fav_u_phase = 0;
        if (at_fav()->slots[RT_FAVORITES_UP] == -1 || at_fav()->slots[RT_FAVORITES_RIGHT] == -1) {
            at_check(C_FAV_USE, false, "slots not assigned by the previous step");
            return true;
        }
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_menu_expected = false;
        at_check(C_FAV_USE, at_results[C_FAV_USE] == 1, "timed out in phase %d", at_fav_u_phase);
        return true;
    }

    const RtFavoritesState* fav = at_fav();
    int stimPid = fav->slots[RT_FAVORITES_RIGHT];
    switch (at_fav_u_phase) {
    case 0:
        // The knife in hand (so readying the pistol changes the hand), and a
        // few hit points missing for the Stimpak.
        if (!at_aim_released() || !at_wield(at_find_inventory("Knife")) || anim_busy(obj_dude)) {
            break;
        }
        at_heal();
        critter_adjust_hits(obj_dude, -15);
        intface_update_hit_points(false);
        at_fav_u_stims = at_count_pid(stimPid);
        at_fav_u_hp = critter_get_hits(obj_dude);
        at_fav_u_chem_uses = rt_get_stats()->chemUses;
        at_fav_u_attacks = rt_get_stats()->playerAttacks;
        at_fav_u_interactions = rt_context_get_stats()->interactions;
        at_fav_u_moves = rt_get_stats()->moveRequests;
        at_fav_u_tile = obj_dude->tile;
        // Right held for 1.5 s: one Stimpak, not one per frame.
        at_expect_menu(NULL, NULL);
        at_pad.buttons[GAMEPAD_BUTTON_DPAD_RIGHT] = true;
        at_fav_u_phase = 1;
        at_fav_u_time = now;
        break;
    case 1:
        if (now - at_fav_u_time < 1500) {
            break;
        }
        at_pad.buttons[GAMEPAD_BUTTON_DPAD_RIGHT] = false;
        at_fav_u_phase = 2;
        at_fav_u_time = now;
        break;
    case 2:
        // The Stimpak animation plays out.
        if (now - at_fav_u_time < 2500) {
            break;
        }
        {
            int stims = at_count_pid(stimPid);
            int hp = critter_get_hits(obj_dude);
            at_fav_u_ok[0] = !at_menu_seen && stims == at_fav_u_stims - 1 && hp > at_fav_u_hp
                && rt_get_stats()->chemUses == at_fav_u_chem_uses + 1;
            at_log("favorites: Right held 1.5 s: menu opened=%d, Stimpaks %d -> %d, HP %d -> %d, chem uses +%d",
                at_menu_seen, at_fav_u_stims, stims, at_fav_u_hp, hp, rt_get_stats()->chemUses - at_fav_u_chem_uses);
            bool quiet = rt_get_stats()->playerAttacks == at_fav_u_attacks
                && rt_context_get_stats()->interactions == at_fav_u_interactions
                && obj_dude->tile == at_fav_u_tile
                && rt_get_stats()->moveRequests == at_fav_u_moves;
            at_check(C_FAV_NO_LEAK, quiet, "after the direct use: attacks +%d, interactions +%d, moved=%d, movement requests +%d",
                rt_get_stats()->playerAttacks - at_fav_u_attacks, rt_context_get_stats()->interactions - at_fav_u_interactions,
                obj_dude->tile != at_fav_u_tile, rt_get_stats()->moveRequests - at_fav_u_moves);
        }
        at_menu_expected = false;
        at_heal();
        // Up: the pistol (waits for the Stimpak animation if it still plays).
        at_fav_press(GAMEPAD_BUTTON_DPAD_UP);
        at_fav_u_phase = 3;
        break;
    case 3: {
        if (now - at_fav_u_time < 2500) {
            break;
        }
        Object* active = at_fav_active_item();
        at_fav_u_ok[1] = !at_menu_seen && active != NULL && active->pid == fav->slots[RT_FAVORITES_UP];
        at_log("favorites: Up -> active item %s, menu opened=%d", active != NULL ? object_name(active) : "(none)", at_menu_seen);
        at_menu_expected = false;

        // An empty direction does nothing at all.
        int empty = fav->slots[RT_FAVORITES_DOWN] == -1 ? RT_FAVORITES_DOWN : (fav->slots[RT_FAVORITES_LEFT] == -1 ? RT_FAVORITES_LEFT : -1);
        if (empty == -1) {
            rt_favorites_debug_set(RT_FAVORITES_DOWN, -1);
            empty = RT_FAVORITES_DOWN;
        }
        at_fav_u_activations = fav->activations;
        at_fav_u_refusals = fav->refusals;
        at_fav_u_active = active;
        at_count_a = empty;
        at_fav_press(empty == RT_FAVORITES_DOWN ? GAMEPAD_BUTTON_DPAD_DOWN : GAMEPAD_BUTTON_DPAD_LEFT);
        at_fav_u_phase = 4;
        break;
    }
    case 4:
        if (now - at_fav_u_time < 800) {
            break;
        }
        at_fav_u_ok[2] = !at_menu_seen && fav->activations == at_fav_u_activations && fav->refusals == at_fav_u_refusals
            && at_fav_active_item() == at_fav_u_active;
        at_log("favorites: empty %s did nothing=%d", rt_favorites_slot_name(at_count_a), at_fav_u_ok[2]);
        at_menu_expected = false;
        at_check(C_FAV_USE, at_fav_u_ok[0] && at_fav_u_ok[1] && at_fav_u_ok[2],
            "Right (Stimpak) held 1.5 s used exactly one, no menu=%d; Up readied the pistol=%d; empty direction did nothing=%d",
            at_fav_u_ok[0], at_fav_u_ok[1], at_fav_u_ok[2]);

        // Depleted: every Stimpak taken out (kept aside, not destroyed).
        at_fav_removed_count = 0;
        for (int pass = 0; pass < 16; pass++) {
            Object* stim = at_fav_first_of(stimPid, false);
            if (stim == NULL || at_fav_removed_count >= 16) {
                break;
            }
            int quantity = item_count(obj_dude, stim);
            if (item_remove_mult(obj_dude, stim, quantity) != 0) {
                break;
            }
            at_fav_removed[at_fav_removed_count] = stim;
            at_fav_removed_quantity[at_fav_removed_count] = quantity;
            at_fav_removed_count++;
        }
        at_fav_u_refusals = fav->refusals;
        at_fav_u_activations = fav->activations;
        at_fav_u_active = at_fav_active_item();
        at_fav_press(GAMEPAD_BUTTON_DPAD_RIGHT);
        at_fav_u_phase = 5;
        break;
    case 5:
        if (now - at_fav_u_time < 800) {
            break;
        }
        at_fav_u_ok[3] = !at_menu_seen && fav->slots[RT_FAVORITES_RIGHT] == stimPid && rt_favorites_quantity(RT_FAVORITES_RIGHT) == 0
            && fav->refusals == at_fav_u_refusals + 1 && fav->activations == at_fav_u_activations
            && at_fav_active_item() == at_fav_u_active && strstr(fav->lastMessage, "don't have") != NULL;
        at_log("favorites: Right with no Stimpaks: refused=%d ('%s'), slot kept=%d", at_fav_u_ok[3], fav->lastMessage, fav->slots[RT_FAVORITES_RIGHT] == stimPid);
        at_menu_expected = false;
        // Restocked: works again.
        for (int index = 0; index < at_fav_removed_count; index++) {
            item_add_force(obj_dude, at_fav_removed[index], at_fav_removed_quantity[index]);
        }
        at_fav_u_stims = at_count_pid(stimPid);
        at_fav_u_chem_uses = rt_get_stats()->chemUses;
        at_fav_press(GAMEPAD_BUTTON_DPAD_RIGHT);
        at_fav_u_phase = 6;
        break;
    case 6:
        // Holstering the pistol, using the Stimpak and drawing again take a while.
        if (at_count_pid(stimPid) == at_fav_u_stims && now - at_fav_u_time < 6000) {
            break;
        }
        {
            int stims = at_count_pid(stimPid);
            bool used = stims == at_fav_u_stims - 1 && rt_get_stats()->chemUses == at_fav_u_chem_uses + 1;
            at_check(C_FAV_DEPLETED, at_fav_u_ok[3] && used && at_fav_u_stims > 0,
                "no Stimpaks: feedback only, no menu, no other item=%d; restocked to %d: used again=%d (now %d)",
                at_fav_u_ok[3], at_fav_u_stims, used, stims);
        }
        at_menu_expected = false;
        at_heal();
        return true;
    }
    return false;
}

// What the save holds, for the checks after loading (the favorites test
// changed the hand since the Pip-Boy test).
// Ammunition and inventory at save time too (checked after loading).
static int at_save_ammo = -1;
static int at_save_items = -1;
static int at_save_weight = -1;

static void at_record_save_state()
{
    Object* hand = inven_right_hand(obj_dude);
    at_save_ammo = hand != NULL && item_get_type(hand) == ITEM_TYPE_WEAPON ? item_w_curr_ammo(hand) : -1;
    at_save_items = 0;
    for (int index = 0; index < obj_dude->data.inventory.length; index++) {
        at_save_items += obj_dude->data.inventory.items[index].quantity;
    }
    at_save_weight = item_total_weight(obj_dude);
    at_pb_saved_weapon_pid = inven_right_hand(obj_dude) != NULL ? inven_right_hand(obj_dude)->pid : -1;
    // Later steps (level-up indicator) change progression after the Pip-Boy
    // test: compare against what the save holds.
    if (at_pb_skill >= 0) {
        at_pb_saved_skill_value = skill_level(obj_dude, at_pb_skill);
    }
    if (at_pb_perk >= 0) {
        at_pb_saved_perk_level = perk_level(at_pb_perk);
    }
    at_pb_saved_unspent = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
    memcpy(at_fav_saved, rt_favorites_get_state()->slots, sizeof(at_fav_saved));
    at_fav_saved_valid = true;
    at_record_level_state();
    at_rule_saved_perks = editor_rt_free_perks();
    int freePerks;
    editor_rt_debug_get(&at_rule_saved_last_level, &freePerks);
}

static bool step_fav_persist(unsigned int elapsed, bool first, bool timedOut)
{
    if (!first) {
        return true;
    }

    if (!at_fav_saved_valid) {
        at_check(C_FAV_PERSIST, false, "no favorites recorded at save time");
        return true;
    }

    const RtFavoritesState* fav = at_fav();
    bool same = true;
    int assigned = 0;
    char line[200] = "";
    for (int slot = 0; slot < RT_FAVORITES_COUNT; slot++) {
        same = same && fav->slots[slot] == at_fav_saved[slot];
        if (fav->slots[slot] != -1) {
            assigned++;
            char part[48];
            snprintf(part, sizeof(part), "%s:%s(%d) ", rt_favorites_slot_name(slot), proto_name(fav->slots[slot]), rt_favorites_quantity(slot));
            SDL_strlcat(line, part, sizeof(line));
        }
    }
    bool resolves = rt_favorites_find_item(RT_FAVORITES_UP) != NULL && rt_favorites_find_item(RT_FAVORITES_RIGHT) != NULL;
    at_check(C_FAV_PERSIST, same && assigned >= 2 && resolves,
        "after load: slots identical=%d, %d assigned [%s], items found in the loaded inventory=%d", same, assigned, line, resolves);

    // The eight-slot layout of earlier saves (the same function the loader
    // uses): up=A, up-right=B, right=C, down=D, down-left=E, up-left=F,
    // down-right and left empty. A, C, D keep their direction; E moves to
    // the free left; B (up and right taken) and F (left now taken, up
    // taken) are dropped and reported; a duplicate of A is not moved.
    int a = at_fav_saved[RT_FAVORITES_UP];
    int c = at_fav_saved[RT_FAVORITES_RIGHT];
    int items[4] = { -1, -1, -1, -1 };
    int found = 0;
    for (int index = 1; index < 600 && found < 4; index++) {
        int pid = (OBJ_TYPE_ITEM << 24) | index;
        Proto* proto;
        if (pid != a && pid != c && proto_ptr(pid, &proto) != -1) {
            items[found++] = pid;
        }
    }
    int oldSlots[RT_FAVORITES_OLD_COUNT] = { a, items[0], c, a, items[1], items[2], -1, items[3] };
    int slots[RT_FAVORITES_COUNT];
    int moved;
    char report[512];
    int dropped = rt_favorites_migrate(oldSlots, slots, &moved, report, sizeof(report));
    bool migrated = slots[RT_FAVORITES_UP] == a && slots[RT_FAVORITES_RIGHT] == c && slots[RT_FAVORITES_DOWN] == items[1]
        && slots[RT_FAVORITES_LEFT] == items[2] && moved == 1 && dropped == 2
        && strstr(report, "dropped") != NULL && strstr(report, "moved") != NULL;
    at_check(C_FAV_MIGRATE, migrated && found == 4,
        "8 slots -> Up/Right/Down/Left kept cardinals=%d, moved %d, dropped %d, report '%s'",
        slots[RT_FAVORITES_UP] == a && slots[RT_FAVORITES_RIGHT] == c && slots[RT_FAVORITES_DOWN] == items[1], moved, dropped, report);
    return true;
}

// -----------------------------------------------------------------------------
// Steps: interaction selection
// -----------------------------------------------------------------------------

// Fixture items around the player: P1 next to the player, P2 next to the
// player and 1 hex from P1, P3 next to the player on the other side (2 hexes
// from P1), P4 2 hexes from the player behind P3 (3 hexes from P1).
static Object* at_int_items[4] = { NULL, NULL, NULL, NULL };
static Object* at_int_rat = NULL;
static int at_int_phase;
static unsigned int at_int_time;
static int at_int_cycles;
static int at_int_favorites;
static int at_int_engagements;
static int at_int_interactions;
static bool at_int_ok[8];
static int at_int_pid = -1;
static Object* at_int_sequence[16];
static int at_int_sequence_count = 0;
static int at_int_presses = 0;

static bool at_int_on_map(Object* target)
{
    for (Object* obj = obj_find_first_at(obj_dude->elevation); obj != NULL; obj = obj_find_next_at()) {
        if (obj == target) {
            return true;
        }
    }
    return false;
}

static void at_int_erase(Object** obj)
{
    if (*obj != NULL && at_int_on_map(*obj)) {
        Rect rect;
        obj_erase_object(*obj, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
    }
    *obj = NULL;
}

// Test fixture: an item (ammunition) on the ground at `tile`.
static Object* at_int_place_item(int tile)
{
    Object* item;
    if (tile == -1 || at_int_pid == -1 || obj_pid_new(&item, at_int_pid) == -1) {
        return NULL;
    }
    Rect rect;
    obj_move_to_tile(item, tile, obj_dude->elevation, &rect);
    tile_refresh_rect(&rect, obj_dude->elevation);
    return item;
}

static int at_int_index(Object* obj)
{
    for (int index = 0; index < 4; index++) {
        if (at_int_items[index] != NULL && at_int_items[index] == obj) {
            return index;
        }
    }
    return -1;
}

static bool at_int_free(int tile)
{
    return tile != -1 && obj_blocking_at(NULL, tile, obj_dude->elevation) == NULL;
}

// Places the four items in the first orientation where all their tiles are
// free; true on success.
// The orientation (0-5) where all four item tiles around `spot` are free, or -1.
static int at_int_cluster_base(int spot)
{
    for (int base = 0; base < ROTATION_COUNT; base++) {
        int p1 = tile_num_in_direction(spot, base, 1);
        int p2 = tile_num_in_direction(spot, (base + 1) % ROTATION_COUNT, 1);
        int p3 = tile_num_in_direction(spot, (base + 3) % ROTATION_COUNT, 1);
        int p4 = tile_num_in_direction(spot, (base + 3) % ROTATION_COUNT, 2);
        if (at_int_free(p1) && at_int_free(p2) && at_int_free(p3) && at_int_free(p4)) {
            return base;
        }
    }
    return -1;
}

// Nothing else to interact with within 4 hexes of `spot`.
static bool at_int_spot_clear(int spot)
{
    for (Object* obj = obj_find_first_at(obj_dude->elevation); obj != NULL; obj = obj_find_next_at()) {
        int type = FID_TYPE(obj->fid);
        if (obj == obj_dude || (obj->flags & OBJECT_HIDDEN) != 0
            || (type != OBJ_TYPE_ITEM && type != OBJ_TYPE_CRITTER && type != OBJ_TYPE_SCENERY)) {
            continue;
        }
        if (tile_dist(spot, obj->tile) <= 4 && rt_context_is_interactable(obj)) {
            return false;
        }
    }
    return true;
}

// Places the four items around the player, moving the player (harness) to
// the nearest clear spot with room for them; true on success.
static bool at_int_place_cluster()
{
    int spot = -1;
    int base = -1;
    for (int distance = 0; distance <= 12 && spot == -1; distance++) {
        for (int rotation = 0; rotation < ROTATION_COUNT && spot == -1; rotation++) {
            int corner = distance == 0 ? obj_dude->tile : tile_num_in_direction(obj_dude->tile, rotation, distance);
            int steps = distance == 0 ? 1 : distance;
            for (int step = 0; step < steps && corner != -1 && spot == -1; step++) {
                int tile = distance == 0 ? corner : tile_num_in_direction(corner, (rotation + 2) % ROTATION_COUNT, step);
                if (tile == -1 || (tile != obj_dude->tile && obj_blocking_at(obj_dude, tile, obj_dude->elevation) != NULL)) {
                    continue;
                }
                int candidate = at_int_cluster_base(tile);
                if (candidate != -1 && at_int_spot_clear(tile)) {
                    spot = tile;
                    base = candidate;
                }
            }
        }
    }
    if (spot == -1) {
        return false;
    }

    Rect rect;
    if (spot != obj_dude->tile) {
        obj_move_to_tile(obj_dude, spot, obj_dude->elevation, &rect);
        tile_set_center(spot, TILE_SET_CENTER_REFRESH_WINDOW);
    }

    int p1 = tile_num_in_direction(spot, base, 1);
    int p2 = tile_num_in_direction(spot, (base + 1) % ROTATION_COUNT, 1);
    int p3 = tile_num_in_direction(spot, (base + 3) % ROTATION_COUNT, 1);
    int p4 = tile_num_in_direction(spot, (base + 3) % ROTATION_COUNT, 2);
    at_int_items[0] = at_int_place_item(p1);
    at_int_items[1] = at_int_place_item(p2);
    at_int_items[2] = at_int_place_item(p3);
    at_int_items[3] = at_int_place_item(p4);
    // Facing P1, so it is what is highlighted first.
    if (obj_set_rotation(obj_dude, base, &rect) == 0) {
        tile_refresh_rect(&rect, obj_dude->elevation);
    }
    at_log("harness: items P1..P4: P1-P2 %d hexes, P1-P3 %d, P1-P4 %d, P3-P4 %d; from the player %d %d %d %d",
        tile_dist(p1, p2), tile_dist(p1, p3), tile_dist(p1, p4), tile_dist(p3, p4),
        tile_dist(spot, p1), tile_dist(spot, p2), tile_dist(spot, p3), tile_dist(spot, p4));
    return at_int_items[0] != NULL && at_int_items[1] != NULL && at_int_items[2] != NULL && at_int_items[3] != NULL;
}


static void at_int_cleanup()
{
    for (Object*& item : at_int_items) {
        at_int_erase(&item);
    }
    rt_debug_set_lock(NULL);
    at_int_erase(&at_int_rat);
}

static bool step_int_cycle(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        at_clear_hostiles();
        rt_debug_set_lock(NULL);
        at_move_to_clear_spot();
        memset(at_int_ok, 0, sizeof(at_int_ok));
        if (at_aim_pistol == NULL) {
            at_aim_pistol = at_find_inventory("10mm Pistol");
        }
        Proto* proto;
        at_int_pid = at_aim_pistol != NULL && proto_ptr(at_aim_pistol->pid, &proto) != -1 ? proto->item.data.weapon.ammoTypePid : -1;
        if (!at_int_place_cluster()) {
            at_check(C_INT_CYCLE, false, "no free tiles for the four items around the player");
            at_int_cleanup();
            return true;
        }
        // A neutral rat out of reach, locked: cycling mustn't touch it.
        at_int_rat = at_spawn_rat(6, 8, false);
        if (at_int_rat != NULL) {
            rt_debug_set_lock(at_int_rat);
        }
        at_int_engagements = rt_get_stats()->engagements;
        at_int_phase = 0;
        at_int_time = now;
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_menu_expected = false;
        at_check(C_INT_CYCLE, at_results[C_INT_CYCLE] == 1, "timed out in phase %d", at_int_phase);
        at_int_cleanup();
        return true;
    }

    at_heal();
    switch (at_int_phase) {
    case 0: {
        if (now - at_int_time < 400 || !at_aim_released()) {
            break;
        }
        // P1 highlighted, in reach: its name and action, no helper text.
        Object* target = rt_get_interact_target();
        const char* label = rt_get_interact_label();
        at_int_ok[0] = target != NULL && at_int_index(target) != -1 && tile_dist(obj_dude->tile, target->tile) <= 1 && strstr(label, object_name(target)) != NULL && strstr(label, "A Pick up") != NULL
            && strstr(label, "D-pad") == NULL && strstr(label, "next") == NULL && strstr(label, "favorites") == NULL;
        at_log("interaction: highlighted P%d, label '%s'", at_int_index(target) + 1, label);
        at_screenshot("int_01_label");
        at_int_sequence[0] = target;
        at_int_sequence_count = 1;
        at_int_presses = 0;
        at_int_cycles = rt_get_stats()->interactCycles;
        at_int_favorites = rt_get_stats()->favoritesOpened;
        at_int_phase = 1;
        break;
    }
    case 1:
        // Right four times, then left four times.
        if (now - at_int_time < 350) {
            break;
        }
        if (at_int_presses > 0) {
            at_int_sequence[at_int_sequence_count++] = rt_get_interact_target();
        }
        if (at_int_presses < 8) {
            at_expect_menu(NULL, NULL);
            at_press(at_int_presses < 4 ? GAMEPAD_BUTTON_DPAD_RIGHT : GAMEPAD_BUTTON_DPAD_LEFT);
            at_int_presses++;
            at_int_time = now;
            break;
        }
        {
            bool within = true;
            bool farJump = false;
            char line[160] = "";
            for (int index = 1; index < at_int_sequence_count; index++) {
                Object* previous = at_int_sequence[index - 1];
                Object* next = at_int_sequence[index];
                within = within && previous != NULL && next != NULL && tile_dist(previous->tile, next->tile) <= 2;
                farJump = farJump || ((at_int_index(previous) == 0 && at_int_index(next) == 3) || (at_int_index(previous) == 3 && at_int_index(next) == 0));
                char part[16];
                snprintf(part, sizeof(part), "P%d ", at_int_index(next) + 1);
                SDL_strlcat(line, part, sizeof(line));
            }
            bool cyclesOnly = rt_get_stats()->interactCycles == at_int_cycles + 8 && rt_get_stats()->favoritesOpened == at_int_favorites && !at_menu_seen;
            at_int_ok[1] = within && !farJump && cyclesOnly;
            at_log("interaction: from the first highlight (P%d), right x4 then left x4: %s(each step within 2 hexes=%d, P1<->P4 (3 apart) directly=%d, only cycled=%d)",
                at_int_index(at_int_sequence[0]) + 1, line, within, farJump, cyclesOnly);
        }
        at_menu_expected = false;
        at_int_phase = 2;
        at_int_presses = 0;
        break;
    case 2:
        // Onto P4 (2 hexes from the player): highlighted, but no label and A
        // doesn't act from there.
        if (now - at_int_time < 350) {
            break;
        }
        if (rt_get_interact_target() != at_int_items[3]) {
            if (at_int_presses++ < 8) {
                at_press(GAMEPAD_BUTTON_DPAD_RIGHT);
                at_int_time = now;
            } else {
                at_log("interaction: could not highlight P4");
                at_int_phase = 4;
            }
            break;
        }
        at_int_ok[2] = rt_get_interact_label()[0] == '\0';
        at_int_interactions = rt_context_get_stats()->interactions;
        at_tile_a = obj_dude->tile;
        at_press(GAMEPAD_BUTTON_A);
        at_int_phase = 3;
        at_int_time = now;
        break;
    case 3:
        if (now - at_int_time < 1200) {
            break;
        }
        at_int_ok[3] = rt_context_get_stats()->interactions == at_int_interactions && obj_dude->tile == at_tile_a && at_int_on_map(at_int_items[3]);
        at_log("interaction: A on P4 (%d hexes away): interactions +%d, player moved=%d, still on the ground=%d",
            tile_dist(obj_dude->tile, at_int_items[3]->tile), rt_context_get_stats()->interactions - at_int_interactions,
            obj_dude->tile != at_tile_a, at_int_on_map(at_int_items[3]));
        at_int_phase = 4;
        at_int_presses = 0;
        break;
    case 4:
        // Back to P1 for the rest.
        if (now - at_int_time < 350) {
            break;
        }
        if (rt_get_interact_target() != at_int_items[0] && at_int_presses++ < 8) {
            at_press(GAMEPAD_BUTTON_DPAD_LEFT);
            at_int_time = now;
            break;
        }
        at_check(C_INT_LABEL, at_int_ok[0] && at_int_ok[2],
            "P1 (1 hex): label with name and 'A Pick up', no D-pad/favorites text=%d; P4 (2 hexes): no label=%d", at_int_ok[0], at_int_ok[2]);
        at_check(C_INT_NO_COMBAT, rt_get_lock() == at_int_rat && at_int_rat != NULL && !rt_is_engaged(at_int_rat)
                && rt_get_stats()->engagements == at_int_engagements,
            "lock still on the neutral rat=%d, rat engaged=%d, new engagements=%d",
            rt_get_lock() == at_int_rat, at_int_rat != NULL && rt_is_engaged(at_int_rat), rt_get_stats()->engagements - at_int_engagements);

        // Only P1 and P4 (3 hexes apart): left/right have nobody to step to
        // and use the favorites; up too.
        at_int_erase(&at_int_items[1]);
        at_int_erase(&at_int_items[2]);
        at_int_cycles = rt_get_stats()->interactCycles;
        at_int_favorites = rt_get_stats()->favoritesOpened;
        at_int_phase = 5;
        at_int_time = now;
        break;
    case 5:
        if (now - at_int_time < 400) {
            break;
        }
        at_expect_menu(NULL, NULL);
        at_press(GAMEPAD_BUTTON_DPAD_LEFT);
        at_int_phase = 6;
        at_int_time = now;
        break;
    case 6:
        if (now - at_int_time < 500) {
            break;
        }
        at_int_ok[4] = rt_get_stats()->interactCycles == at_int_cycles && rt_get_stats()->favoritesOpened == at_int_favorites + 1
            && rt_get_interact_target() == at_int_items[0] && !at_menu_seen;
        at_press(GAMEPAD_BUTTON_DPAD_UP);
        at_int_phase = 7;
        at_int_time = now;
        break;
    case 7:
        if (now - at_int_time < 500) {
            break;
        }
        {
            bool up = rt_get_stats()->interactCycles == at_int_cycles && rt_get_stats()->favoritesOpened == at_int_favorites + 2 && !at_menu_seen;
            at_check(C_INT_CONTEXT, at_int_ok[1] && at_int_ok[4] && up,
                "with neighbors within 2 hexes left/right only cycled=%d; P1 and P4 3 hexes apart: left went to favorites, highlight kept=%d; up went to favorites=%d (no menu)",
                at_int_ok[1], at_int_ok[4], up);
            at_check(C_INT_CYCLE, at_int_ok[1], "see the log: each step within 2 hexes of the highlighted item, never P1<->P4 (3 apart)");
        }
        at_menu_expected = false;
        // A on P1 (in reach) picks it up.
        at_obj_a = rt_get_interact_target();
        at_count_a = at_count_pid(at_int_pid);
        at_press(GAMEPAD_BUTTON_A);
        at_int_phase = 8;
        at_int_time = now;
        break;
    case 8:
        if ((at_int_on_map(at_obj_a) || anim_busy(obj_dude)) && now - at_int_time < 5000) {
            break;
        }
        {
            bool picked = at_obj_a == at_int_items[0] && !at_int_on_map(at_obj_a) && at_count_pid(at_int_pid) > at_count_a;
            at_check(C_INT_ACTIVATE, picked && at_int_ok[3],
                "A on P1 (1 hex): picked up=%d (ammo %d -> %d); A on P4 (2 hexes): refused without moving=%d",
                picked, at_count_a, at_count_pid(at_int_pid), at_int_ok[3]);
            at_int_items[0] = NULL;
        }
        at_int_cleanup();
        at_int_phase = 11;
        break;
    case 11: {
        // Barrier: a spot near a wall with a free tile 2 hexes away on the
        // other side (no short walk there), and one in the open.
        int found = -1;
        int hidden = -1;
        for (int distance = 0; distance <= 10 && found == -1; distance++) {
            for (int rotation = 0; rotation < ROTATION_COUNT && found == -1; rotation++) {
                int corner = distance == 0 ? obj_dude->tile : tile_num_in_direction(obj_dude->tile, rotation, distance);
                int steps = distance == 0 ? 1 : distance;
                for (int step = 0; step < steps && corner != -1 && found == -1; step++) {
                    int spot = distance == 0 ? corner : tile_num_in_direction(corner, (rotation + 2) % ROTATION_COUNT, step);
                    if (spot == -1 || obj_blocking_at(obj_dude, spot, obj_dude->elevation) != NULL) {
                        continue;
                    }
                    for (int side = 0; side < ROTATION_COUNT && found == -1; side++) {
                        int other = tile_num_in_direction(spot, side, 2);
                        if (other == -1 || obj_blocking_at(NULL, other, obj_dude->elevation) != NULL
                            || !combat_is_shot_blocked(obj_dude, spot, other, NULL, NULL)) {
                            continue;
                        }
                        int length = make_path(obj_dude, spot, other, NULL, 0);
                        bool openTile = false;
                        for (int around = 0; around < ROTATION_COUNT; around++) {
                            int next = tile_num_in_direction(spot, around, 1);
                            openTile = openTile || (next != -1 && next != other && obj_blocking_at(NULL, next, obj_dude->elevation) == NULL);
                        }
                        if (openTile && (length == 0 || length > 6)) {
                            found = spot;
                            hidden = other;
                        }
                    }
                }
            }
        }
        if (found == -1) {
            at_log("interaction: no wall with a free tile behind it nearby; barrier not tested");
            return true;
        }
        Rect rect;
        obj_move_to_tile(obj_dude, found, obj_dude->elevation, &rect);
        tile_set_center(found, TILE_SET_CENTER_REFRESH_WINDOW);
        at_int_items[0] = at_int_place_item(hidden);
        // The item in the open: right next to the player.
        for (int rotation = 0; rotation < ROTATION_COUNT && at_int_items[1] == NULL; rotation++) {
            int tile = tile_num_in_direction(found, rotation, 1);
            if (tile != -1 && tile != hidden && obj_blocking_at(NULL, tile, obj_dude->elevation) == NULL) {
                at_int_items[1] = at_int_place_item(tile);
            }
        }
        at_log("harness: wall test: player at a wall spot, item behind it %d hexes away (path %d), item in the open placed=%d",
            tile_dist(found, hidden), make_path(obj_dude, found, hidden, NULL, 0), at_int_items[1] != NULL);
        at_int_phase = 12;
        at_int_time = now;
        break;
    }
    case 12: {
        // After the pickup animation, with the candidates refreshed.
        if (now - at_int_time < 800 || anim_busy(obj_dude)) {
            break;
        }
        Object* candidates[8];
        int count = rt_get_interact_candidates(candidates, 8);
        bool hiddenOut = at_int_items[0] != NULL;
        bool openIn = false;
        for (int index = 0; index < count; index++) {
            hiddenOut = hiddenOut && candidates[index] != at_int_items[0];
            openIn = openIn || (at_int_items[1] != NULL && candidates[index] == at_int_items[1]);
        }
        at_screenshot("int_03_wall");
        at_check(C_INT_BARRIER, hiddenOut && openIn,
            "item 2 hexes away behind a wall offered=%d (should be 0); item in the open offered=%d",
            !hiddenOut, openIn);
        at_int_cleanup();
        return true;
    }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: loot screen
// -----------------------------------------------------------------------------

#define AT_LOOT_ITEMS 10

static Object* at_loot_corpse = NULL;
static Object* at_loot_single = NULL;
static int at_loot_single_pid = -1;
static Object* at_loot_stack = NULL;
static Object* at_loot_heavy = NULL;
static int at_loot_pids[AT_LOOT_ITEMS + 1];
static int at_loot_totals[AT_LOOT_ITEMS + 1];
static int at_loot_pid_count = 0;
static int at_loot_phase;
static unsigned int at_loot_time;
static int at_loot_presses;
static bool at_loot_ok[16];
static bool at_loot_quantity_seen = false;
static int at_loot_carry_bonus = 0;
static bool at_loot_carry_set = false;
static Object* at_loot_equipped[3];
static int at_loot_attacks;
static int at_loot_interactions;
static int at_loot_reloads;
static Object* at_loot_lock;
static bool at_loot_empty_test = false;
static int at_loot_player_before[AT_LOOT_ITEMS + 1];

static int at_count_pid_in(Object* owner, int pid)
{
    int quantity = 0;
    Inventory* inventory = &(owner->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item->pid == pid) {
            quantity += inventory->items[index].quantity;
        }
    }
    return quantity;
}

static const InvenRtLootState* at_loot()
{
    return inven_rt_loot_state();
}

// A dead rat next to the player (the corpse to loot).
static Object* at_loot_new_corpse()
{
    Object* rat = at_spawn_rat(1, 1, false);
    if (rat == NULL) {
        return NULL;
    }
    critter_kill(rat, -1, true);
    Rect rect;
    if (obj_set_rotation(obj_dude, tile_dir(obj_dude->tile, rat->tile), &rect) == 0) {
        tile_refresh_rect(&rect, obj_dude->elevation);
    }
    return rat;
}

// An empty human corpse next to the player (humans can hold items; the
// original rules refuse items for other body types).
static Object* at_loot_new_human_corpse()
{
    int tile = -1;
    for (int rotation = 0; rotation < ROTATION_COUNT && tile == -1; rotation++) {
        int candidate = tile_num_in_direction(obj_dude->tile, rotation, 1);
        if (candidate != -1 && obj_blocking_at(NULL, candidate, obj_dude->elevation) == NULL) {
            tile = candidate;
        }
    }
    if (tile == -1) {
        return NULL;
    }

    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_CRITTER << 24) | index;
        Proto* proto;
        Object* critter;
        if (proto_ptr(pid, &proto) == -1 || obj_pid_new(&critter, pid) == -1) {
            continue;
        }
        if (critter_body_type(critter) != BODY_TYPE_BIPED || critter->data.inventory.length != 0) {
            Rect rect;
            obj_erase_object(critter, &rect);
            continue;
        }
        Rect rect;
        obj_move_to_tile(critter, tile, obj_dude->elevation, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        if (critter->sid != -1) {
            scr_remove(critter->sid);
            critter->sid = -1;
        }
        critter->data.critter.combat.team = 77;
        critter_kill(critter, -1, true);
        if (obj_set_rotation(obj_dude, tile_dir(obj_dude->tile, tile), &rect) == 0) {
            tile_refresh_rect(&rect, obj_dude->elevation);
        }
        at_log("harness: empty corpse of a %s (pid 0x%08X) next to the player", critter_name(critter), pid);
        return critter;
    }
    return NULL;
}

// Fixture items in the corpse: one stack of 5, light single items, and the
// heaviest item there is.
static bool at_loot_fill(Object* corpse)
{
    at_loot_pid_count = 0;
    int heavyPid = -1;
    int heavyWeight = 0;
    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_ITEM << 24) | index;
        Proto* proto;
        if (proto_ptr(pid, &proto) == -1 || pid == PROTO_ID_MONEY || proto->item.type == ITEM_TYPE_CONTAINER || proto->item.type == ITEM_TYPE_KEY) {
            continue;
        }
        int weight = proto->item.weight;
        if (weight > heavyWeight && weight < 200) {
            heavyWeight = weight;
            heavyPid = pid;
        }
        if (weight >= 1 && weight <= 2 && at_loot_pid_count < AT_LOOT_ITEMS - 1 && at_count_pid_in(obj_dude, pid) == 0
            && (proto->item.type == ITEM_TYPE_MISC || proto->item.type == ITEM_TYPE_DRUG || proto->item.type == ITEM_TYPE_AMMO)) {
            at_loot_pids[at_loot_pid_count++] = pid;
        }
    }
    if (at_loot_pid_count < AT_LOOT_ITEMS - 1 || heavyPid == -1) {
        return false;
    }
    at_loot_pids[at_loot_pid_count++] = heavyPid;

    at_loot_single = NULL;
    at_loot_stack = NULL;
    at_loot_heavy = NULL;
    for (int index = 0; index < at_loot_pid_count; index++) {
        Object* item;
        if (obj_pid_new(&item, at_loot_pids[index]) == -1) {
            return false;
        }
        int quantity = index == 0 ? 5 : 1;
        Rect rect;
        if (item_add_force(corpse, item, quantity) != 0) {
            return false;
        }
        obj_disconnect(item, &rect);
        if (index == 0) {
            at_loot_stack = item;
        } else if (index == 1) {
            at_loot_single = item;
            at_loot_single_pid = item->pid;
        } else if (index == at_loot_pid_count - 1) {
            at_loot_heavy = item;
        }
    }

    for (int index = 0; index < at_loot_pid_count; index++) {
        at_loot_totals[index] = at_count_pid_in(obj_dude, at_loot_pids[index]) + at_count_pid_in(corpse, at_loot_pids[index]);
        at_loot_player_before[index] = at_count_pid_in(obj_dude, at_loot_pids[index]);
    }
    at_log("harness: corpse with %d stacks (stack of 5 %s, single %s, heavy %s weighing %d)",
        corpse->data.inventory.length, object_name(at_loot_stack), object_name(at_loot_single), object_name(at_loot_heavy), heavyWeight);
    return true;
}

static bool at_menu_loot_script(unsigned int elapsed)
{
    const InvenRtLootState* state = at_loot();
    if (!state->open) {
        return false;
    }

    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 8000) {
        at_log("loot script: phase %d timed out (side %d, selected %d/%d, quantity open %d)",
            at_pb_phase, state->side, state->selected[state->side], state->count[state->side], state->quantityOpen);
        at_pb_tap(GAMEPAD_BUTTON_B);
        if (!state->quantityOpen) {
            return true;
        }
        return false;
    }

    if (state->quantityOpen) {
        at_loot_quantity_seen = true;
    }

    Object* corpse = at_loot_corpse;
    switch (at_pb_phase) {
    case 0:
        if (elapsed > 600) {
            Object* first = corpse->data.inventory.length > 0 ? corpse->data.inventory.items[0].item : NULL;
            at_loot_ok[0] = state->side == INVEN_RT_LOOT_SIDE_LOOT && state->selected[INVEN_RT_LOOT_SIDE_LOOT] == 0
                && state->count[INVEN_RT_LOOT_SIDE_LOOT] == at_loot_pid_count && state->selectedItem == first && first != NULL;
            at_log("loot: opened on side %d, item %d of %d (%s)", state->side, state->selected[state->side],
                state->count[state->side], state->selectedItem != NULL ? object_name(state->selectedItem) : "(none)");
            at_screenshot("loot_01_open");
            // Equipped items aren't in the player's list.
            at_loot_ok[1] = true;
            Inventory* carried = &(obj_dude->data.inventory);
            for (int index = 0; index < carried->length; index++) {
                for (Object* equipped : at_loot_equipped) {
                    if (equipped != NULL && carried->items[index].item == equipped) {
                        at_loot_ok[1] = false;
                    }
                }
            }
            at_loot_presses = 0;
            at_pb_next(1);
        }
        break;
    case 1:
        // Down 7 times through the 10 loot stacks: the list scrolls.
        if (at_pb_ready(200)) {
            if (at_loot_presses < 7) {
                at_pb_tap(GAMEPAD_BUTTON_DPAD_DOWN);
                at_loot_presses++;
            } else {
                at_loot_ok[2] = state->selected[INVEN_RT_LOOT_SIDE_LOOT] == 7 && state->offset[INVEN_RT_LOOT_SIDE_LOOT] > 0;
                at_log("loot: after 7 x down: item %d, first row %d", state->selected[INVEN_RT_LOOT_SIDE_LOOT], state->offset[INVEN_RT_LOOT_SIDE_LOOT]);
                at_pb_tap(GAMEPAD_BUTTON_LEFT_TRIGGER);
                at_loot_presses = 0;
                at_pb_next(2);
            }
        }
        break;
    case 2:
        if (at_pb_ready(250)) {
            if (at_loot_presses == 0) {
                at_loot_ok[3] = state->side == INVEN_RT_LOOT_SIDE_PLAYER;
            }
            if (at_loot_presses < 2) {
                at_pb_tap(GAMEPAD_BUTTON_DPAD_DOWN);
                at_loot_presses++;
            } else {
                at_loot_ok[3] = at_loot_ok[3] && state->selected[INVEN_RT_LOOT_SIDE_PLAYER] == std::min(2, state->count[INVEN_RT_LOOT_SIDE_PLAYER] - 1);
                at_screenshot("loot_02_player_side");
                at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
                at_pb_next(3);
            }
        }
        break;
    case 3:
        if (at_pb_ready(250)) {
            // Back on the loot side, on the item it was on.
            at_loot_ok[3] = at_loot_ok[3] && state->side == INVEN_RT_LOOT_SIDE_LOOT && state->selected[INVEN_RT_LOOT_SIDE_LOOT] == 7;
            at_log("loot: LT -> player side, down x2 -> %d; RT -> loot side on item %d", state->selected[INVEN_RT_LOOT_SIDE_PLAYER], state->selected[INVEN_RT_LOOT_SIDE_LOOT]);
            at_pb_next(4);
        }
        break;
    case 4:
        // The single item: A moves it at once.
        if (state->selectedItem != at_loot_single) {
            if (at_pb_ready(150) && at_pb_tries++ < 30) {
                at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
            }
            break;
        }
        if (at_pb_ready(200)) {
            at_loot_quantity_seen = false;
            at_count_a = at_count_pid_in(obj_dude, at_loot_single_pid);
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(5);
        }
        break;
    case 5:
        if (at_pb_ready(500)) {
            at_loot_ok[4] = !at_loot_quantity_seen && at_count_pid_in(obj_dude, at_loot_single_pid) == at_count_a + 1
                && at_count_pid_in(corpse, at_loot_single_pid) == 0;
            at_log("loot: single %s taken at once=%d (%s)", proto_name(at_loot_single_pid), at_loot_ok[4], state->lastMessage);
            at_pb_next(6);
        }
        break;
    case 6:
        // The stack of 5: the quantity selector, up twice -> 3, A.
        if (state->selectedItem != at_loot_stack) {
            if (at_pb_ready(150) && at_pb_tries++ < 30) {
                at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
            }
            break;
        }
        if (at_pb_ready(200)) {
            at_count_a = at_count_pid_in(obj_dude, at_loot_stack->pid);
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(7);
        }
        break;
    case 7:
        if (state->quantityOpen && at_pb_ready(400)) {
            at_loot_ok[5] = state->quantityValue == 1 && state->quantityMax == 5;
            at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
            at_loot_presses = 1;
            at_pb_next(8);
        }
        break;
    case 8:
        if (at_pb_ready(250)) {
            if (at_loot_presses < 2) {
                at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
                at_loot_presses++;
            } else {
                at_loot_ok[5] = at_loot_ok[5] && state->quantityValue == 3;
                at_screenshot("loot_03_quantity");
                at_pb_tap(GAMEPAD_BUTTON_A);
                at_pb_next(9);
            }
        }
        break;
    case 9:
        if (!state->quantityOpen && at_pb_ready(500)) {
            at_loot_ok[5] = at_loot_ok[5] && at_count_pid_in(obj_dude, at_loot_stack->pid) == at_count_a + 3
                && at_count_pid_in(corpse, at_loot_stack->pid) == 2;
            at_log("loot: stack: 3 of 5 moved=%d (%s)", at_loot_ok[5], state->lastMessage);
            at_pb_next(10);
        }
        break;
    case 10:
        // The rest of the stack: A, then B cancels.
        if (state->selectedItem == NULL || state->selectedItem->pid != at_loot_stack->pid) {
            if (at_pb_ready(150) && at_pb_tries++ < 30) {
                at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
            }
            break;
        }
        if (at_pb_ready(200)) {
            at_count_a = at_count_pid_in(corpse, at_loot_stack->pid);
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(11);
        }
        break;
    case 11:
        if (state->quantityOpen && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(12);
        }
        break;
    case 12:
        if (!state->quantityOpen && at_pb_ready(400)) {
            at_loot_ok[6] = state->open && at_count_pid_in(corpse, at_loot_stack->pid) == at_count_a;
            at_log("loot: B in the quantity selector: still looting=%d, nothing moved=%d", state->open, at_count_pid_in(corpse, at_loot_stack->pid) == at_count_a);
            at_pb_tap(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_pb_next(13);
        }
        break;
    case 13:
        // Player side: the single item back into the corpse.
        if (state->side != INVEN_RT_LOOT_SIDE_PLAYER || !at_pb_ready(250)) {
            break;
        }
        if (state->selectedItem == NULL || state->selectedItem->pid != at_loot_single_pid) {
            if (at_pb_tries++ < 60) {
                at_pb_tap(state->selected[INVEN_RT_LOOT_SIDE_PLAYER] + 1 < state->count[INVEN_RT_LOOT_SIDE_PLAYER] && at_pb_tries < 40 ? GAMEPAD_BUTTON_DPAD_DOWN : GAMEPAD_BUTTON_DPAD_UP);
            }
            break;
        }
        at_count_a = at_count_pid_in(obj_dude, at_loot_single_pid);
        at_pb_tap(GAMEPAD_BUTTON_A);
        at_pb_next(14);
        break;
    case 14:
        if (at_pb_ready(500)) {
            // The corpse can hold only what the rat could carry (`item_add`):
            // over that the item stays with the player, with a message.
            bool moved = at_count_pid_in(obj_dude, at_loot_single_pid) == at_count_a - 1 && at_count_pid_in(corpse, at_loot_single_pid) == 1;
            bool stayed = at_count_pid_in(obj_dude, at_loot_single_pid) == at_count_a && at_count_pid_in(corpse, at_loot_single_pid) == 0;
            bool full = stayed && (critter_body_type(corpse) != BODY_TYPE_BIPED
                || item_total_weight(corpse) + item_weight(state->selectedItem != NULL ? state->selectedItem : at_loot_heavy) > stat_level(corpse, STAT_CARRY_WEIGHT));
            at_loot_ok[7] = !state->quantityOpen && (moved || (full && strstr(state->lastMessage, "no space") != NULL));
            at_loot_ok[11] = full;
            at_log("loot: single into the corpse (corpse %d of %d lbs, full=%d): moved=%d stayed=%d (%s)",
                item_total_weight(corpse), stat_level(corpse, STAT_CARRY_WEIGHT), full, moved, stayed, state->lastMessage);

            // Carry limit for Take All: everything light fits, the heavy item
            // doesn't (fixture: the carry weight bonus, removed afterwards).
            int lightWeight = 0;
            Inventory* loot = &(corpse->data.inventory);
            for (int index = 0; index < loot->length; index++) {
                if (loot->items[index].item != at_loot_heavy) {
                    lightWeight += item_weight(loot->items[index].item) * loot->items[index].quantity;
                }
            }
            int free = lightWeight + 1;
            if (item_weight(at_loot_heavy) > free) {
                at_loot_carry_bonus = stat_get_bonus(obj_dude, STAT_CARRY_WEIGHT);
                stat_set_bonus(obj_dude, STAT_CARRY_WEIGHT, at_loot_carry_bonus + item_total_weight(obj_dude) + free - stat_level(obj_dude, STAT_CARRY_WEIGHT));
                at_loot_carry_set = true;
                at_log("harness: carry weight set so %d more fits (light items %d, heavy %d)", free, lightWeight, item_weight(at_loot_heavy));
            }
            for (int index = 0; index < at_loot_pid_count; index++) {
                at_loot_player_before[index] = at_count_pid_in(obj_dude, at_loot_pids[index]);
            }
            // X with the player side focused: takes from the loot side only.
            at_count_b = obj_dude->data.inventory.length;
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(15);
        }
        break;
    case 15:
        if (at_pb_ready(600)) {
            Inventory* loot = &(corpse->data.inventory);
            bool onlyHeavy = loot->length == 1 && loot->items[0].item == at_loot_heavy;
            bool noDeposit = true;
            for (int index = 0; index < at_loot_pid_count; index++) {
                noDeposit = noDeposit && at_count_pid_in(obj_dude, at_loot_pids[index]) >= at_loot_player_before[index];
            }
            at_loot_ok[8] = at_loot_carry_set ? onlyHeavy && state->takeAllLeft == 1 && strstr(state->lastMessage, "too heavy") != NULL : loot->length == 0;
            at_loot_ok[8] = at_loot_ok[8] && noDeposit;
            at_log("loot: X: loot left %d (only the heavy item=%d), never deposited=%d, message '%s'",
                loot->length, onlyHeavy, noDeposit, state->lastMessage);
            at_screenshot("loot_04_take_all");
            if (at_loot_carry_set) {
                stat_set_bonus(obj_dude, STAT_CARRY_WEIGHT, at_loot_carry_bonus);
                at_loot_carry_set = false;
            }
            at_pb_tap(GAMEPAD_BUTTON_B);
            return true;
        }
        break;
    }
    return false;
}

// An empty corpse: A, X, D-pad, LT/RT do nothing harmful; a focus loss in
// the screen; B closes.
static bool at_menu_loot_empty_script(unsigned int elapsed)
{
    const InvenRtLootState* state = at_loot();
    if (!state->open) {
        return false;
    }

    static const GamepadButton presses[] = {
        GAMEPAD_BUTTON_A, GAMEPAD_BUTTON_X, GAMEPAD_BUTTON_DPAD_DOWN, GAMEPAD_BUTTON_DPAD_UP,
        GAMEPAD_BUTTON_LEFT_TRIGGER, GAMEPAD_BUTTON_DPAD_DOWN, GAMEPAD_BUTTON_RIGHT_TRIGGER, GAMEPAD_BUTTON_X
    };

    switch (at_pb_phase) {
    case 0:
        if (elapsed > 500) {
            at_loot_ok[9] = state->count[INVEN_RT_LOOT_SIDE_LOOT] == 0 && state->selectedItem == NULL && state->side == INVEN_RT_LOOT_SIDE_LOOT;
            at_count_a = obj_dude->data.inventory.length;
            at_loot_presses = 0;
            at_pb_next(1);
        }
        break;
    case 1:
        if (at_pb_ready(250)) {
            if (at_loot_presses < 8) {
                at_pb_tap(presses[at_loot_presses++]);
            } else {
                at_loot_ok[9] = at_loot_ok[9] && state->open && at_loot_corpse->data.inventory.length == 0;
                at_screenshot("loot_05_empty");
                // Focus lost while the screen is open.
                at_focus_lost = true;
                at_focus_regained = false;
                at_focus_polls = 0;
                at_focus_lost_at = SDL_GetTicks();
                SDL_Event event;
                memset(&event, 0, sizeof(event));
                event.type = SDL_WINDOWEVENT;
                event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
                SDL_PushEvent(&event);
                at_pb_next(2);
            }
        }
        break;
    case 2:
        if (at_focus_regained && SDL_GetTicks() - at_focus_lost_at > 2200) {
            at_loot_ok[10] = state->open && at_loot_corpse->data.inventory.length == 0;
            at_focus_lost = false;
            // Into the empty corpse from the player side, and back.
            at_pb_tap(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_pb_next(3);
        }
        break;
    case 3:
        if (state->side != INVEN_RT_LOOT_SIDE_PLAYER || !at_pb_ready(200)) {
            break;
        }
        if (state->selectedItem == NULL || state->selectedItem->pid != at_loot_single_pid) {
            if (at_pb_tries++ < 60) {
                at_pb_tap(state->selected[INVEN_RT_LOOT_SIDE_PLAYER] + 1 < state->count[INVEN_RT_LOOT_SIDE_PLAYER] && at_pb_tries < 40 ? GAMEPAD_BUTTON_DPAD_DOWN : GAMEPAD_BUTTON_DPAD_UP);
            }
            break;
        }
        at_count_a = at_count_pid_in(obj_dude, at_loot_single_pid);
        at_pb_tap(GAMEPAD_BUTTON_A);
        at_pb_next(4);
        break;
    case 4:
        if (at_pb_ready(500)) {
            at_loot_ok[12] = at_count_pid_in(at_loot_corpse, at_loot_single_pid) == 1 && at_count_pid_in(obj_dude, at_loot_single_pid) == at_count_a - 1;
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            at_pb_next(5);
        }
        break;
    case 5:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(6);
        }
        break;
    case 6:
        if (at_pb_ready(500)) {
            at_loot_ok[13] = at_count_pid_in(at_loot_corpse, at_loot_single_pid) == 0 && at_count_pid_in(obj_dude, at_loot_single_pid) == at_count_a;
            at_log("loot: empty corpse: put the %s in=%d, took it back=%d", proto_name(at_loot_single_pid), at_loot_ok[12], at_loot_ok[13]);
            at_pb_tap(GAMEPAD_BUTTON_B);
            return true;
        }
        break;
    }
    return false;
}

// The 50,000 caps: A opens the selector; RT tapped (+100), RT held (keeps
// adding 100s), LT tapped (-100), A confirms.
static int at_loot_caps_max = 0;
static int at_loot_caps_tap = 0;
static int at_loot_caps_held = 0;
static int at_loot_caps_value = 0;
static int at_loot_caps_all = 0;

static bool at_menu_loot_caps_script(unsigned int elapsed)
{
    const InvenRtLootState* state = at_loot();
    if (!state->open) {
        return false;
    }
    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 8000) {
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = false;
        at_pb_tap(GAMEPAD_BUTTON_B);
        return !state->quantityOpen;
    }

    switch (at_pb_phase) {
    case 0:
        if (elapsed > 500) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(1);
        }
        break;
    case 1:
        if (state->quantityOpen && at_pb_ready(400)) {
            at_loot_caps_max = state->quantityMax;
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            at_pb_next(2);
        }
        break;
    case 2:
        if (at_pb_ready(300)) {
            at_loot_caps_tap = state->quantityValue;
            // Held: after the repeat delay, 100 more every repeat.
            at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
            at_pb_last_press = SDL_GetTicks();
            at_pb_next(3);
        }
        break;
    case 3:
        if (at_pb_ready(1500)) {
            at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = false;
            at_pb_last_press = SDL_GetTicks();
            at_pb_next(4);
        }
        break;
    case 4:
        if (at_pb_ready(300)) {
            at_loot_caps_held = state->quantityValue;
            at_screenshot("loot_06_caps");
            at_pb_tap(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_pb_next(5);
        }
        break;
    case 5:
        if (at_pb_ready(300)) {
            at_loot_caps_value = state->quantityValue;
            at_log("loot: caps selector max %d, RT tap -> %d, RT held -> %d, LT tap -> %d",
                at_loot_caps_max, at_loot_caps_tap, at_loot_caps_held, at_loot_caps_value);
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(7);
        }
        break;
    case 7:
        // Y: the whole stack, confirmed with A.
        if (at_pb_ready(300)) {
            at_loot_caps_all = state->quantityValue;
            at_screenshot("loot_07_caps_all");
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(6);
        }
        break;
    case 6:
        if (!state->quantityOpen && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_B);
            return true;
        }
        break;
    }
    return false;
}

static void at_loot_open(AtMenuScript* script)
{
    at_pb_next(0);
    at_pb_last_press = SDL_GetTicks();
    at_expect_menu(NULL, script);
    at_press(GAMEPAD_BUTTON_A);
}

static bool step_loot(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        at_clear_hostiles();
        rt_debug_set_lock(NULL);
        at_move_to_clear_spot();
        memset(at_loot_ok, 0, sizeof(at_loot_ok));
        at_loot_corpse = at_loot_new_corpse();
        if (at_loot_corpse == NULL || !at_loot_fill(at_loot_corpse)) {
            at_check(C_LOOT_FOCUS, false, "could not set up the corpse fixture");
            return true;
        }
        at_loot_equipped[0] = inven_left_hand(obj_dude);
        at_loot_equipped[1] = inven_right_hand(obj_dude);
        at_loot_equipped[2] = inven_worn(obj_dude);
        at_loot_phase = 0;
        at_loot_time = now;
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_focus_lost = false;
        at_menu_expected = false;
        if (at_loot_carry_set) {
            stat_set_bonus(obj_dude, STAT_CARRY_WEIGHT, at_loot_carry_bonus);
            at_loot_carry_set = false;
        }
        at_check(C_LOOT_FOCUS, at_results[C_LOOT_FOCUS] == 1, "timed out in phase %d", at_loot_phase);
        return true;
    }

    at_heal();
    switch (at_loot_phase) {
    case 0:
        // A on the corpse opens the loot screen.
        if (now - at_loot_time < 500 || !at_aim_released() || anim_busy(obj_dude)) {
            break;
        }
        at_log("loot: A target is %s", rt_get_interact_target() != NULL ? object_name(rt_get_interact_target()) : "(none)");
        at_loot_open(at_menu_loot_script);
        at_loot_phase = 1;
        at_loot_time = now;
        break;
    case 1:
        if (!at_menu_seen || at_loot()->open) {
            break;
        }
        at_menu_expected = false;
        // Nothing done in the screen reaches the game afterwards.
        at_loot_attacks = rt_get_stats()->playerAttacks;
        at_loot_interactions = rt_context_get_stats()->interactions;
        at_loot_reloads = rt_get_stats()->reloadsStarted;
        at_loot_lock = rt_get_lock();
        at_loot_phase = 2;
        at_loot_time = now;
        break;
    case 2: {
        if (now - at_loot_time < 1500) {
            break;
        }
        bool conserved = true;
        char line[300] = "";
        for (int index = 0; index < at_loot_pid_count; index++) {
            int total = at_count_pid_in(obj_dude, at_loot_pids[index]) + at_count_pid_in(at_loot_corpse, at_loot_pids[index]);
            conserved = conserved && total == at_loot_totals[index];
            if (total != at_loot_totals[index]) {
                char part[64];
                snprintf(part, sizeof(part), "%s %d->%d ", proto_name(at_loot_pids[index]), at_loot_totals[index], total);
                SDL_strlcat(line, part, sizeof(line));
            }
        }
        bool quiet = rt_get_stats()->playerAttacks == at_loot_attacks && rt_context_get_stats()->interactions == at_loot_interactions
            && rt_get_stats()->reloadsStarted == at_loot_reloads && rt_get_lock() == at_loot_lock && !rt_is_reloading();
        at_check(C_LOOT_FOCUS, at_loot_ok[0], "opened on the loot side, first of %d items highlighted=%d", at_loot_pid_count, at_loot_ok[0]);
        at_check(C_LOOT_NAV, at_loot_ok[2] && at_loot_ok[3],
            "7 x down scrolled the 10-item loot list=%d; LT player side, down x2, RT back on the same loot item=%d", at_loot_ok[2], at_loot_ok[3]);
        at_check(C_LOOT_QUANTITY, at_loot_ok[5] && at_loot_ok[6],
            "stack of 5: selector opened on 1 of 5, up x2 -> 3, A moved exactly 3=%d; B in the selector moved nothing, loot stayed open=%d", at_loot_ok[5], at_loot_ok[6]);
        at_check(C_LOOT_TAKE_ALL, at_loot_ok[8],
            "X with the player side focused took the loot side's light items, left the too-heavy one with a message, deposited nothing=%d", at_loot_ok[8]);
        at_check(C_LOOT_SAFETY, conserved && at_loot_ok[1] && quiet && at_loot_ok[7],
            "item totals conserved=%d %s; equipped items not in the player's list=%d; no attack/interaction/reload/lock change after closing=%d; deposit into the rat corpse refused by the original rules (rats hold no items; over its carry weight=%d) with a message, nothing lost=%d",
            conserved, line, at_loot_ok[1], quiet, at_loot_ok[11], at_loot_ok[7]);

        // An empty corpse.
        Rect rect;
        obj_erase_object(at_loot_corpse, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        at_loot_corpse = at_loot_new_human_corpse();
        if (at_loot_corpse == NULL) {
            at_check(C_LOOT_EMPTY, false, "no empty corpse fixture");
            return true;
        }
        at_loot_phase = 3;
        at_loot_time = now;
        break;
    }
    case 3:
        if (now - at_loot_time < 500 || !at_aim_released() || anim_busy(obj_dude)) {
            break;
        }
        at_loot_open(at_menu_loot_empty_script);
        at_loot_phase = 4;
        break;
    case 4:
        if (!at_menu_seen || at_loot()->open) {
            break;
        }
        at_menu_expected = false;
        at_check(C_LOOT_EMPTY, at_loot_ok[9], "empty corpse: nothing selected, A/X/D-pad/LT/RT harmless, still open=%d; B closed it", at_loot_ok[9]);
        at_check(C_LOOT_TRANSFER, at_loot_ok[4] && at_loot_ok[12] && at_loot_ok[13],
            "single item taken at once (no quantity selector)=%d; from the player side put into the empty corpse=%d and taken back=%d",
            at_loot_ok[4], at_loot_ok[12], at_loot_ok[13]);
        at_check(C_MODAL_INTERRUPT, at_aim_disconnect_ok && at_loot_ok[10],
            "AIMED SHOT: disconnect with RT/D-pad held kept the screen's state, no phantom press=%d; loot screen open through a focus loss (%d polls), nothing moved, B closed=%d",
            at_aim_disconnect_ok, at_focus_polls, at_loot_ok[10]);
        {
            Rect rect;
            obj_erase_object(at_loot_corpse, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            at_loot_corpse = NULL;
        }
        // A corpse with 50,000 caps.
        at_loot_corpse = at_loot_new_human_corpse();
        if (at_loot_corpse == NULL) {
            at_check(C_LOOT_BIG_STACK, false, "no corpse fixture for the caps");
            return true;
        }
        {
            Object* caps;
            Rect rect;
            if (obj_pid_new(&caps, PROTO_ID_MONEY) == -1 || item_add_force(at_loot_corpse, caps, 50000) != 0) {
                at_check(C_LOOT_BIG_STACK, false, "could not put 50,000 caps in the corpse");
                return true;
            }
            obj_disconnect(caps, &rect);
        }
        at_count_a = item_caps_total(obj_dude);
        at_loot_phase = 5;
        at_loot_time = now;
        break;
    case 5:
        if (now - at_loot_time < 500 || !at_aim_released() || anim_busy(obj_dude)) {
            break;
        }
        at_loot_open(at_menu_loot_caps_script);
        at_loot_phase = 6;
        break;
    case 6:
        if (!at_menu_seen || at_loot()->open) {
            break;
        }
        at_menu_expected = false;
        {
            int taken = item_caps_total(obj_dude) - at_count_a;
            int left = item_caps_total(at_loot_corpse);
            bool moved = at_loot_caps_all == 50000 && taken == 50000 && left == 0;
            at_check(C_LOOT_BIG_STACK, at_loot_caps_max == 50000 && at_loot_caps_tap == 101 && at_loot_caps_held >= 101 + 500
                    && (at_loot_caps_held - 101) % 100 == 0 && at_loot_caps_value == at_loot_caps_held - 100 && moved,
                "selector max %d (stack 50000); RT tap 1 -> %d; RT held 1.5 s -> %d (by 100s); LT tap -> %d; Y -> %d; confirmed: took %d in one go, %d left in the corpse",
                at_loot_caps_max, at_loot_caps_tap, at_loot_caps_held, at_loot_caps_value, at_loot_caps_all, taken, left);
        }
        {
            Rect rect;
            obj_erase_object(at_loot_corpse, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            at_loot_corpse = NULL;
        }
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: barter screen
// -----------------------------------------------------------------------------

// Fixture: a calm human trader next to the player carrying a knife, a stack
// of 4 Stimpaks and 200 caps; the player gets 5,000 caps. The barter screen
// is opened directly (no trader in the maps the test visits).
static Object* at_barter_trader = NULL;
static Object* at_barter_knife = NULL;
static int at_barter_knife_pid = -1;
static int at_barter_stim_pid = -1;
static bool at_barter_ok[12];
static int at_barter_totals[3];
static int at_barter_dude_before[3];
static int at_barter_trader_before[3];
static int at_barter_presses = 0;
static int at_barter_phase;

static const InvenRtBarterState* at_barter()
{
    return inven_rt_barter_state();
}

static Object* at_barter_new_trader()
{
    int tile = -1;
    for (int rotation = 0; rotation < ROTATION_COUNT && tile == -1; rotation++) {
        int candidate = tile_num_in_direction(obj_dude->tile, rotation, 1);
        if (candidate != -1 && obj_blocking_at(NULL, candidate, obj_dude->elevation) == NULL) {
            tile = candidate;
        }
    }
    if (tile == -1) {
        return NULL;
    }

    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_CRITTER << 24) | index;
        Proto* proto;
        Object* critter;
        if (proto_ptr(pid, &proto) == -1 || obj_pid_new(&critter, pid) == -1) {
            continue;
        }
        if (critter_body_type(critter) != BODY_TYPE_BIPED || critter->data.inventory.length != 0) {
            Rect rect;
            obj_erase_object(critter, &rect);
            continue;
        }
        Rect rect;
        obj_move_to_tile(critter, tile, obj_dude->elevation, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        if (critter->sid != -1) {
            scr_remove(critter->sid);
            critter->sid = -1;
        }
        critter->data.critter.combat.team = 77;
        return critter;
    }
    return NULL;
}

static void at_barter_give(Object* owner, int pid, int quantity)
{
    Object* item;
    Rect rect;
    if (obj_pid_new(&item, pid) != -1 && item_add_force(owner, item, quantity) == 0) {
        obj_disconnect(item, &rect);
    }
}

static int at_barter_pids[3];

static void at_barter_count(int* dude, int* trader)
{
    for (int index = 0; index < 3; index++) {
        dude[index] = at_count_pid_in(obj_dude, at_barter_pids[index]);
        trader[index] = at_count_pid_in(at_barter_trader, at_barter_pids[index]);
    }
}

// Selects `pid` in the focused column (down until found); true once there.
static bool at_barter_goto(int pid)
{
    const InvenRtBarterState* state = at_barter();
    if (state->selectedItem != NULL && state->selectedItem->pid == pid) {
        return true;
    }
    if (at_pb_ready(150) && at_pb_tries++ < 40) {
        int column = state->column;
        bool down = state->selected[column] + 1 < state->count[column] && at_pb_tries < 25;
        at_pb_tap(down ? GAMEPAD_BUTTON_DPAD_DOWN : GAMEPAD_BUTTON_DPAD_UP);
    }
    return false;
}

static bool at_menu_barter_script(unsigned int elapsed)
{
    const InvenRtBarterState* state = at_barter();
    const InvenRtLootState* quantity = inven_rt_loot_state();
    if (!state->open) {
        return false;
    }

    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 8000) {
        at_log("barter script: phase %d timed out (column %d, selected %d/%d, quantity open %d)",
            at_pb_phase, state->column, state->selected[state->column], state->count[state->column], quantity->quantityOpen);
        at_pb_tap(GAMEPAD_BUTTON_B);
        return !quantity->quantityOpen;
    }

    int dude[3];
    int trader[3];
    switch (at_pb_phase) {
    case 0:
        if (elapsed > 600) {
            at_barter_ok[0] = state->column == INVEN_RT_BARTER_TRADER_ITEMS && state->selected[INVEN_RT_BARTER_TRADER_ITEMS] == 0
                && state->count[INVEN_RT_BARTER_TRADER_ITEMS] >= 3;
            at_screenshot("barter_01_open");
            at_pb_tap(GAMEPAD_BUTTON_DPAD_DOWN);
            at_pb_next(1);
        }
        break;
    case 1:
        if (at_pb_ready(250)) {
            at_barter_ok[0] = at_barter_ok[0] && state->selected[INVEN_RT_BARTER_TRADER_ITEMS] == 1;
            at_pb_tap(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_pb_next(2);
        }
        break;
    case 2:
        if (at_pb_ready(250)) {
            at_barter_ok[0] = at_barter_ok[0] && state->column == INVEN_RT_BARTER_PLAYER_ITEMS;
            at_pb_tap(GAMEPAD_BUTTON_DPAD_DOWN);
            at_pb_next(3);
        }
        break;
    case 3:
        if (at_pb_ready(250)) {
            at_barter_ok[0] = at_barter_ok[0] && state->selected[INVEN_RT_BARTER_PLAYER_ITEMS] == 1;
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_SHOULDER);
            at_pb_next(4);
        }
        break;
    case 4:
        if (at_pb_ready(250)) {
            at_barter_ok[0] = at_barter_ok[0] && state->column == INVEN_RT_BARTER_TRADER_OFFER;
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            at_pb_next(5);
        }
        break;
    case 5:
        if (at_pb_ready(250)) {
            at_barter_ok[0] = at_barter_ok[0] && state->column == INVEN_RT_BARTER_TRADER_ITEMS && state->selected[INVEN_RT_BARTER_TRADER_ITEMS] == 1;
            at_log("barter: navigation ok=%d", at_barter_ok[0]);
            at_pb_next(6);
        }
        break;
    case 6:
        // The knife (one) onto the trader's offer.
        if (!at_barter_goto(at_barter_knife_pid) || !at_pb_ready(200)) {
            break;
        }
        at_barter_count(at_barter_dude_before, at_barter_trader_before);
        at_pb_tap(GAMEPAD_BUTTON_A);
        at_pb_next(7);
        break;
    case 7:
        if (at_pb_ready(400)) {
            at_barter_count(dude, trader);
            // On the table: neither the player's nor in the trader's list.
            at_barter_ok[1] = !quantity->quantityOpen && state->count[INVEN_RT_BARTER_TRADER_OFFER] == 1
                && dude[0] == at_barter_dude_before[0] && trader[0] == at_barter_trader_before[0] - 1;
            at_pb_next(8);
        }
        break;
    case 8:
        // Two of the four Stimpaks.
        if (!at_barter_goto(at_barter_stim_pid) || !at_pb_ready(200)) {
            break;
        }
        at_pb_tap(GAMEPAD_BUTTON_A);
        at_pb_next(9);
        break;
    case 9:
        if (quantity->quantityOpen && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_DPAD_UP);
            at_pb_next(10);
        }
        break;
    case 10:
        if (at_pb_ready(300)) {
            at_barter_ok[1] = at_barter_ok[1] && quantity->quantityValue == 2;
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(11);
        }
        break;
    case 11:
        if (!quantity->quantityOpen && at_pb_ready(400)) {
            at_barter_count(dude, trader);
            at_barter_ok[1] = at_barter_ok[1] && dude[1] == at_barter_dude_before[1] && trader[1] == at_barter_trader_before[1] - 2
                && state->count[INVEN_RT_BARTER_TRADER_OFFER] == 2;
            at_log("barter: item and 2 Stimpaks on the trader's offer, nothing changed owner=%d", at_barter_ok[1]);
            // X with nothing offered by the player: refused, nothing moves.
            at_count_a = state->rejections;
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(12);
        }
        break;
    case 12:
        if (at_pb_ready(500)) {
            at_barter_count(dude, trader);
            at_barter_ok[2] = state->rejections == at_count_a + 1 && !state->accepts && state->trades == 0
                && dude[0] == at_barter_dude_before[0] && dude[1] == at_barter_dude_before[1];
            at_log("barter: poor offer refused=%d ('%s'), ask $%d, offer $%d", at_barter_ok[2], state->lastMessage, state->askValue, state->offerValue);
            at_pb_tap(GAMEPAD_BUTTON_LEFT_TRIGGER);
            at_pb_next(13);
        }
        break;
    case 13:
        // All the player's caps (Y in the quantity window) on the offer.
        if (state->column != INVEN_RT_BARTER_PLAYER_ITEMS || !at_barter_goto(PROTO_ID_MONEY) || !at_pb_ready(200)) {
            break;
        }
        at_pb_tap(GAMEPAD_BUTTON_A);
        at_pb_next(14);
        break;
    case 14:
        if (quantity->quantityOpen && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(15);
        }
        break;
    case 15:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(16);
        }
        break;
    case 16:
        if (!quantity->quantityOpen && at_pb_ready(400)) {
            at_barter_count(dude, trader);
            // Offered caps wait on the offer table: the trader has none of them yet.
            at_barter_ok[3] = state->accepts && state->offerValue >= state->askValue && trader[2] == at_barter_trader_before[2];
            at_screenshot("barter_02_offer");
            at_log("barter: caps offered: offer $%d, ask $%d, accepts=%d, trader's caps still %d (nothing handed over)",
                state->offerValue, state->askValue, state->accepts, trader[2]);
            // Take one Stimpak back off the trader's offer: B first, then 1.
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_SHOULDER);
            at_pb_next(17);
        }
        break;
    case 17:
        if (state->column != INVEN_RT_BARTER_TRADER_OFFER || !at_barter_goto(at_barter_stim_pid) || !at_pb_ready(200)) {
            break;
        }
        at_pb_tap(GAMEPAD_BUTTON_A);
        at_pb_next(18);
        break;
    case 18:
        if (quantity->quantityOpen && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_B);
            at_pb_next(19);
        }
        break;
    case 19:
        if (!quantity->quantityOpen && at_pb_ready(400)) {
            at_barter_count(dude, trader);
            at_barter_ok[4] = trader[1] == at_barter_trader_before[1] - 2;
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(20);
        }
        break;
    case 20:
        if (quantity->quantityOpen && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(21);
        }
        break;
    case 21:
        if (!quantity->quantityOpen && at_pb_ready(400)) {
            at_barter_count(dude, trader);
            at_barter_ok[4] = at_barter_ok[4] && trader[1] == at_barter_trader_before[1] - 1;
            at_log("barter: one Stimpak taken back off the offer (B in the selector moved nothing)=%d", at_barter_ok[4]);
            at_count_b = state->reviews;
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(22);
        }
        break;
    case 22:
        if (at_pb_ready(400)) {
            at_barter_ok[5] = state->reviews == at_count_b + 1 && strstr(state->lastMessage, "would accept") != NULL;
            at_log("barter: review: '%s'", state->lastMessage);
            at_count_a = state->trades;
            at_pb_tap(GAMEPAD_BUTTON_X);
            at_pb_next(23);
        }
        break;
    case 23:
        if (at_pb_ready(500)) {
            at_barter_count(dude, trader);
            // The exchange: the knife and a Stimpak to the player, the caps to
            // the trader.
            at_barter_ok[6] = state->trades == at_count_a + 1 && dude[0] == at_barter_dude_before[0] + 1
                && dude[1] == at_barter_dude_before[1] + 1 && dude[2] == 0 && trader[2] == at_barter_trader_before[2] + at_barter_dude_before[2];
            at_screenshot("barter_03_traded");
            at_log("barter: accepted trade=%d ('%s'): item %d->%d, Stimpaks %d->%d, caps %d->%d (trader %d->%d)",
                at_barter_ok[6], state->lastMessage, at_barter_dude_before[0], dude[0], at_barter_dude_before[1], dude[1],
                at_barter_dude_before[2], dude[2], at_barter_trader_before[2], trader[2]);
            // A new offer, then leave: everything goes back.
            at_barter_count(at_barter_dude_before, at_barter_trader_before);
            at_pb_tap(GAMEPAD_BUTTON_RIGHT_TRIGGER);
            at_pb_next(24);
        }
        break;
    case 24:
        if (state->column != INVEN_RT_BARTER_TRADER_ITEMS || !at_barter_goto(PROTO_ID_MONEY) || !at_pb_ready(200)) {
            break;
        }
        at_pb_tap(GAMEPAD_BUTTON_A);
        at_pb_next(25);
        break;
    case 25:
        if (quantity->quantityOpen && at_pb_ready(400)) {
            at_pb_tap(GAMEPAD_BUTTON_Y);
            at_pb_next(26);
        }
        break;
    case 26:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(27);
        }
        break;
    case 27:
        if (!quantity->quantityOpen && at_pb_ready(400)) {
            at_barter_ok[7] = state->count[INVEN_RT_BARTER_TRADER_OFFER] == 1;
            at_pb_tap(GAMEPAD_BUTTON_B);
            return true;
        }
        break;
    }
    return false;
}

static int at_barter_attacks;
static int at_barter_reloads;
static Object* at_barter_lock;
static int at_barter_interactions;

static bool step_barter(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_release_all();
        at_clear_hostiles();
        rt_debug_set_lock(NULL);
        at_move_to_clear_spot();
        memset(at_barter_ok, 0, sizeof(at_barter_ok));
        at_barter_trader = at_barter_new_trader();
        // A misc item the player doesn't carry (a trader keeps its first
        // weapon out of the trade, as in the original).
        at_barter_knife = NULL;
        for (int index = 1; index < 600 && at_barter_knife == NULL; index++) {
            int pid = (OBJ_TYPE_ITEM << 24) | index;
            Proto* proto;
            if (proto_ptr(pid, &proto) != -1 && proto->item.type == ITEM_TYPE_MISC && proto->item.cost > 0
                && proto->item.weight >= 1 && proto->item.weight <= 3 && at_count_pid_in(obj_dude, pid) == 0) {
                at_barter_knife_pid = pid;
                at_barter_knife = obj_dude;
            }
        }
        for (int index = 1; index < 600 && at_chem_pid == -1; index++) {
            int pid = (OBJ_TYPE_ITEM << 24) | index;
            Proto* proto;
            if (proto_ptr(pid, &proto) != -1 && proto->item.type == ITEM_TYPE_DRUG && SDL_strcasecmp(proto_name(pid), "Stimpak") == 0) {
                at_chem_pid = pid;
            }
        }
        if (at_barter_trader == NULL || at_barter_knife == NULL || at_chem_pid == -1) {
            at_check(C_BARTER_NAV, false, "fixtures missing (trader %d, knife %d, Stimpak %d)", at_barter_trader != NULL, at_barter_knife != NULL, at_chem_pid != -1);
            return true;
        }

        at_log("harness: the trader's item is a %s", proto_name(at_barter_knife_pid));
        at_barter_stim_pid = at_chem_pid;
        at_barter_pids[0] = at_barter_knife_pid;
        at_barter_pids[1] = at_barter_stim_pid;
        at_barter_pids[2] = PROTO_ID_MONEY;
        at_barter_give(at_barter_trader, at_barter_knife_pid, 1);
        at_barter_give(at_barter_trader, at_barter_stim_pid, 4);
        at_barter_give(at_barter_trader, PROTO_ID_MONEY, 200);
        // The player's caps: exactly 5,000.
        int caps = item_caps_total(obj_dude);
        if (caps < 5000) {
            at_barter_give(obj_dude, PROTO_ID_MONEY, 5000 - caps);
        } else if (caps > 5000) {
            item_caps_adjust(obj_dude, 5000 - caps);
        }
        for (int index = 0; index < 3; index++) {
            at_barter_totals[index] = at_count_pid_in(obj_dude, at_barter_pids[index]) + at_count_pid_in(at_barter_trader, at_barter_pids[index]);
        }
        at_log("harness: trader %s with a knife, 4 Stimpaks and 200 caps; the player has %d caps", critter_name(at_barter_trader), item_caps_total(obj_dude));
        at_barter_attacks = rt_get_stats()->playerAttacks;
        at_barter_reloads = rt_get_stats()->reloadsStarted;
        at_barter_lock = rt_get_lock();
        at_barter_interactions = rt_context_get_stats()->interactions;
        at_barter_phase = 0;
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_menu_expected = false;
        at_check(C_BARTER_NAV, at_results[C_BARTER_NAV] == 1, "timed out in phase %d", at_barter_phase);
        return true;
    }

    switch (at_barter_phase) {
    case 0:
        if (!at_aim_released() || anim_busy(obj_dude)) {
            break;
        }
        at_pb_next(0);
        at_pb_last_press = SDL_GetTicks();
        at_expect_menu(NULL, at_menu_barter_script);
        at_barter_phase = 1;
        // The barter screen as the dialogue opens it (modal).
        inven_rt_debug_barter(at_barter_trader);
        break;
    case 1: {
        at_menu_expected = false;
        int dude[3];
        int trader[3];
        at_barter_count(dude, trader);
        bool returned = dude[2] == at_barter_dude_before[2] && trader[2] == at_barter_trader_before[2];
        bool conserved = true;
        for (int index = 0; index < 3; index++) {
            conserved = conserved && dude[index] + trader[index] == at_barter_totals[index];
        }
        bool quiet = rt_get_stats()->playerAttacks == at_barter_attacks && rt_get_stats()->reloadsStarted == at_barter_reloads
            && rt_get_lock() == at_barter_lock && rt_context_get_stats()->interactions == at_barter_interactions;
        at_check(C_BARTER_NAV, at_barter_ok[0], "opened on the trader's items; down, LT, down, RB, RT kept each list's selection=%d", at_barter_ok[0]);
        at_check(C_BARTER_OFFER, at_barter_ok[1], "an item (A) and 2 of 4 Stimpaks (quantity) on the trader's offer, nobody's inventory changed hands=%d", at_barter_ok[1]);
        at_check(C_BARTER_REMOVE, at_barter_ok[4], "one Stimpak taken back off the offer; B in the selector moved nothing=%d", at_barter_ok[4]);
        at_check(C_BARTER_TRADE, at_barter_ok[2] && at_barter_ok[3] && at_barter_ok[5] && at_barter_ok[6],
            "X with no player offer refused, nothing moved=%d; caps offered: acceptance shown=%d; Y review=%d; X traded (item + Stimpak for the caps)=%d",
            at_barter_ok[2], at_barter_ok[3], at_barter_ok[5], at_barter_ok[6]);
        at_check(C_BARTER_CANCEL, at_barter_ok[7] && returned,
            "trader's 200 caps put on its offer, then B: caps back with the trader=%d (player %d, trader %d)", returned, dude[2], trader[2]);
        at_check(C_BARTER_SAFETY, conserved && quiet,
            "item/Stimpak/cap totals conserved=%d; no attack, reload, interaction or lock change=%d", conserved, quiet);
        Rect rect;
        obj_erase_object(at_barter_trader, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        at_barter_trader = NULL;
        return true;
    }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: X tap/hold (reload, holster)
// -----------------------------------------------------------------------------

static int at_hx_phase;
static unsigned int at_hx_time;
static Object* at_hx_pistol = NULL;
static int at_hx_ammo = 0;
static int at_hx_reloads = 0;
static int at_hx_holsters = 0;
static int at_hx_taps = 0;
static int at_hx_unarmed = 0;
static int at_hx_draws = 0;
static bool at_hx_ok[10];
static bool at_hx_put_away_seen = false;
static bool at_hx_take_out_seen = false;
static Object* at_hx_rat = NULL;

static bool at_menu_hx_close_script(unsigned int elapsed)
{
    if (elapsed > 600) {
        at_press(GAMEPAD_BUTTON_B);
        return true;
    }
    return false;
}

static bool step_holster(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        at_clear_hostiles();
        rt_debug_set_lock(NULL);
        at_move_to_clear_spot();
        memset(at_hx_ok, 0, sizeof(at_hx_ok));
        at_hx_pistol = at_find_inventory("10mm Pistol");
        at_hx_phase = 0;
        at_hx_time = now;
        if (at_hx_pistol == NULL) {
            at_check(C_HOLSTER_TAP, false, "no pistol");
            return true;
        }
        return false;
    }

    if (timedOut) {
        at_release_all();
        at_menu_expected = false;
        rt_favorites_debug_set(RT_FAVORITES_UP, -1);
        at_check(C_HOLSTER_HOLD, at_results[C_HOLSTER_HOLD] == 1, "timed out in phase %d", at_hx_phase);
        return true;
    }

    at_heal();
    const RtStats* stats = rt_get_stats();
    int anim = FID_ANIM_TYPE(obj_dude->fid);
    switch (at_hx_phase) {
    case 0:
        // The pistol in hand, two rounds short, ammunition carried.
        if (!at_aim_released() || !at_wield(at_hx_pistol) || anim_busy(obj_dude) || rt_is_reloading()) {
            break;
        }
        item_w_set_curr_ammo(at_hx_pistol, item_w_max_ammo(at_hx_pistol) - 2);
        at_hx_reloads = stats->reloadsStarted;
        at_hx_holsters = stats->holstersStarted;
        at_hx_taps = stats->reloadTaps;
        // A tap.
        at_press(GAMEPAD_BUTTON_X);
        at_hx_phase = 1;
        at_hx_time = now;
        break;
    case 1:
        if (now - at_hx_time < 2500 || rt_is_reloading()) {
            break;
        }
        at_hx_ok[0] = stats->reloadsStarted == at_hx_reloads + 1 && stats->reloadTaps == at_hx_taps + 1 && stats->holstersStarted == at_hx_holsters
            && item_w_curr_ammo(at_hx_pistol) == item_w_max_ammo(at_hx_pistol);
        at_check(C_HOLSTER_TAP, at_hx_ok[0], "X tapped: reloads +%d, holsters +%d, ammo %d/%d",
            stats->reloadsStarted - at_hx_reloads, stats->holstersStarted - at_hx_holsters, item_w_curr_ammo(at_hx_pistol), item_w_max_ammo(at_hx_pistol));
        // A hold, with the pistol a favorite (it must still be after).
        item_w_set_curr_ammo(at_hx_pistol, item_w_max_ammo(at_hx_pistol) - 3);
        at_hx_ammo = item_w_curr_ammo(at_hx_pistol);
        rt_favorites_debug_set(RT_FAVORITES_UP, at_hx_pistol->pid);
        at_hx_reloads = stats->reloadsStarted;
        at_hx_holsters = stats->holstersStarted;
        at_hx_put_away_seen = false;
        at_pad.buttons[GAMEPAD_BUTTON_X] = true;
        at_hx_phase = 2;
        at_hx_time = now;
        break;
    case 2:
        // Before the threshold nothing has happened yet.
        if (now - at_hx_time < 250) {
            break;
        }
        at_hx_ok[1] = stats->reloadsStarted == at_hx_reloads && stats->holstersStarted == at_hx_holsters;
        at_hx_phase = 3;
        break;
    case 3:
        if (anim == ANIM_PUT_AWAY) {
            at_hx_put_away_seen = true;
        }
        if (now - at_hx_time < 1400) {
            break;
        }
        at_pad.buttons[GAMEPAD_BUTTON_X] = false;
        at_hx_phase = 4;
        at_hx_time = now;
        break;
    case 4:
        if (anim == ANIM_PUT_AWAY) {
            at_hx_put_away_seen = true;
        }
        if (now - at_hx_time < 1500 || anim_busy(obj_dude)) {
            break;
        }
        {
            bool inHand = inven_right_hand(obj_dude) == at_hx_pistol || inven_left_hand(obj_dude) == at_hx_pistol;
            bool carried = inven_rt_quantity(at_hx_pistol) >= 1;
            at_hx_ok[2] = at_hx_ok[1] && stats->holstersStarted == at_hx_holsters + 1 && stats->holstersCompleted >= 1
                && stats->reloadsStarted == at_hx_reloads && at_hx_put_away_seen && !inHand;
            at_hx_ok[3] = carried && item_w_curr_ammo(at_hx_pistol) == at_hx_ammo && rt_favorites_find_item(RT_FAVORITES_UP) != NULL
                && rt_favorites_find_item(RT_FAVORITES_UP)->pid == at_hx_pistol->pid;
            at_screenshot("holster_01_unarmed");
            at_check(C_HOLSTER_HOLD, at_hx_ok[2],
                "X held 1.4 s: nothing before 0.4 s=%d, holsters +%d (put-away animation seen=%d), reloads +%d (none on release), pistol still in hand=%d",
                at_hx_ok[1], stats->holstersStarted - at_hx_holsters, at_hx_put_away_seen, stats->reloadsStarted - at_hx_reloads, inHand);
        }
        // Unarmed RT on a calm rat next to the player.
        at_hx_rat = at_spawn_rat(1, 1, false);
        if (at_hx_rat != NULL) {
            rt_debug_set_lock(at_hx_rat);
        }
        at_hx_unarmed = stats->playerUnarmed;
        at_hx_phase = 5;
        at_hx_time = now;
        break;
    case 5:
        if (now - at_hx_time < 300) {
            break;
        }
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_hx_phase = 6;
        at_hx_time = now;
        break;
    case 6:
        if (now - at_hx_time < 2500) {
            break;
        }
        at_hx_ok[4] = stats->playerUnarmed > at_hx_unarmed;
        // X held while unarmed: nothing.
        at_hx_holsters = stats->holstersStarted;
        at_hx_reloads = stats->reloadsStarted;
        at_pad.buttons[GAMEPAD_BUTTON_X] = true;
        at_hx_phase = 7;
        at_hx_time = now;
        break;
    case 7:
        if (now - at_hx_time < 1000) {
            break;
        }
        at_pad.buttons[GAMEPAD_BUTTON_X] = false;
        at_hx_phase = 8;
        at_hx_time = now;
        break;
    case 8:
        if (now - at_hx_time < 600) {
            break;
        }
        {
            bool nothing = stats->holstersStarted == at_hx_holsters && stats->reloadsStarted == at_hx_reloads;
            at_check(C_HOLSTER_UNARMED, at_hx_ok[3] && at_hx_ok[4] && nothing,
                "pistol in the inventory with its %d rounds and still the Up favorite=%d; RT attacked unarmed=%d; X held unarmed did nothing=%d",
                at_hx_ammo, at_hx_ok[3], at_hx_ok[4], nothing);
        }
        if (at_hx_rat != NULL) {
            Rect rect;
            rt_debug_set_lock(NULL);
            obj_erase_object(at_hx_rat, &rect);
            tile_refresh_rect(&rect, obj_dude->elevation);
            at_hx_rat = NULL;
        }
        // Up (the pistol favorite): readied with the draw animation.
        at_hx_draws = stats->drawsPlayed;
        at_hx_take_out_seen = false;
        at_press(GAMEPAD_BUTTON_DPAD_UP);
        at_hx_phase = 9;
        at_hx_time = now;
        break;
    case 9:
        if (anim == ANIM_TAKE_OUT) {
            at_hx_take_out_seen = true;
        }
        if (now - at_hx_time < 1500) {
            break;
        }
        {
            Object* active = NULL;
            intface_get_current_item(&active);
            at_check(C_HOLSTER_REDRAW, active == at_hx_pistol && stats->drawsPlayed == at_hx_draws + 1 && at_hx_take_out_seen,
                "Up readied %s, draw animations +%d (seen=%d)", active != NULL ? object_name(active) : "(nothing)",
                stats->drawsPlayed - at_hx_draws, at_hx_take_out_seen);
        }
        rt_favorites_debug_set(RT_FAVORITES_UP, -1);
        // An interrupted hold: X down, the controller pulled, back, released.
        at_hx_holsters = stats->holstersStarted;
        at_hx_reloads = stats->reloadsStarted;
        at_pad.buttons[GAMEPAD_BUTTON_X] = true;
        at_hx_phase = 10;
        at_hx_time = now;
        break;
    case 10:
        if (now - at_hx_time < 150) {
            break;
        }
        at_pad.connected = false;
        at_hx_phase = 11;
        at_hx_time = now;
        break;
    case 11:
        if (now - at_hx_time < 600) {
            break;
        }
        at_pad.connected = true;
        at_hx_phase = 12;
        at_hx_time = now;
        break;
    case 12:
        if (now - at_hx_time < 300) {
            break;
        }
        at_pad.buttons[GAMEPAD_BUTTON_X] = false;
        at_hx_phase = 13;
        at_hx_time = now;
        break;
    case 13:
        // Again through a menu: X down, B opens the Pip-Boy, it closes, X up.
        if (now - at_hx_time < 800) {
            break;
        }
        at_hx_ok[5] = stats->holstersStarted == at_hx_holsters && stats->reloadsStarted == at_hx_reloads;
        at_pad.buttons[GAMEPAD_BUTTON_X] = true;
        at_hx_phase = 14;
        at_hx_time = now;
        break;
    case 14:
        if (now - at_hx_time < 150) {
            break;
        }
        at_expect_menu(NULL, at_menu_hx_close_script);
        at_press(GAMEPAD_BUTTON_B);
        at_hx_phase = 15;
        at_hx_time = now;
        break;
    case 15:
        if (!at_menu_seen || !at_menu_done || now - at_hx_time < 300) {
            if (now - at_hx_time > 5000) {
                at_hx_phase = 16;
            }
            break;
        }
        at_menu_expected = false;
        at_pad.buttons[GAMEPAD_BUTTON_X] = false;
        at_hx_phase = 16;
        at_hx_time = now;
        break;
    case 16:
        if (now - at_hx_time < 1000) {
            break;
        }
        {
            bool menuOk = stats->holstersStarted == at_hx_holsters && stats->reloadsStarted == at_hx_reloads;
            // X in the loot screen (Take All) and AIMED SHOT (undo), checked in
            // their own steps earlier.
            bool menus = at_results[C_LOOT_TAKE_ALL] == 1 && at_results[C_AIM_QUEUE] == 1;
            at_check(C_HOLSTER_CONTEXT, at_hx_ok[5] && menuOk && (menus || at_only[0] != 0),
                "X held through a disconnect did nothing=%d; X held through the Pip-Boy did nothing=%d; loot X Take All and AIMED SHOT X undo passed=%d",
                at_hx_ok[5], menuOk, menus);
        }
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: level-up indicator
// -----------------------------------------------------------------------------

static int at_lvl_mode = 0;
static int at_lvl_phase;
static unsigned int at_lvl_time;
static bool at_lvl_ok[8];
static int at_lvl_unspent = 0;
static int at_lvl_perk = -1;
static bool at_lvl_flag_saved = false;
static bool at_lvl_perk_saved = false;

// Mode 0: Inventory tab only, B. Mode 1: Stats tab, B. Mode 2: choose the
// due perk on the Perks page, B.
static bool at_menu_level_script(unsigned int elapsed)
{
    const RtPipboyState* state = at_pb();
    if (!state->open) {
        return false;
    }
    if (at_pb_phase > 0 && SDL_GetTicks() - at_pb_phase_time > 8000) {
        at_pb_tap(GAMEPAD_BUTTON_B);
        return state->mode == RT_PIPBOY_MODE_NONE;
    }

    switch (at_pb_phase) {
    case 0:
        if (elapsed > 400 && at_pb_goto_tab(at_lvl_mode == 0 ? RT_PIPBOY_TAB_INVENTORY : RT_PIPBOY_TAB_STATS)) {
            at_pb_next(at_lvl_mode == 2 ? 1 : 9);
        }
        break;
    case 1:
        if (at_pb_ready(300) && at_pb_goto_page(3)) {
            at_pb_next(2);
        }
        break;
    case 2:
        if (at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(3);
        }
        break;
    case 3:
        if (state->mode == RT_PIPBOY_MODE_CHOOSE_PERK && at_pb_ready(300)) {
            at_lvl_perk = -1;
            for (int index = 0; index < state->choiceCount; index++) {
                int perk = state->choices[index];
                if (perk != PERK_TAG && perk != PERK_MUTATE) {
                    at_lvl_perk = perk;
                    break;
                }
            }
            at_pb_next(at_lvl_perk != -1 ? 4 : 9);
        }
        break;
    case 4:
        if (at_pb_goto_choice(at_lvl_perk) && at_pb_ready()) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(5);
        }
        break;
    case 5:
        if (state->mode == RT_PIPBOY_MODE_CONFIRM_PERK && at_pb_ready(300)) {
            at_pb_tap(GAMEPAD_BUTTON_A);
            at_pb_next(9);
        }
        break;
    case 9:
        if (at_pb_ready(500) && state->mode == RT_PIPBOY_MODE_NONE) {
            at_pb_tap(GAMEPAD_BUTTON_B);
            return true;
        }
        break;
    }
    return false;
}

static void at_lvl_open(int mode)
{
    at_lvl_mode = mode;
    at_pb_next(0);
    at_pb_last_press = SDL_GetTicks();
    at_expect_menu(NULL, at_menu_level_script);
    at_press(GAMEPAD_BUTTON_B);
}

static void at_lvl_gain(int levels)
{
    int target = stat_pc_get(PC_STAT_LEVEL) + levels;
    while (stat_pc_get(PC_STAT_LEVEL) < target && stat_pc_get(PC_STAT_LEVEL) < PC_LEVEL_MAX) {
        stat_pc_add_experience(stat_pc_min_exp() - stat_pc_get(PC_STAT_EXPERIENCE));
    }
}

static bool step_level_icon(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_release_all();
        memset(at_lvl_ok, 0, sizeof(at_lvl_ok));
        at_lvl_phase = 0;
        at_lvl_time = now;
        return false;
    }
    if (timedOut) {
        at_menu_expected = false;
        at_check(C_LEVEL_ICON, false, "timed out in phase %d", at_lvl_phase);
        return true;
    }

    bool flag = is_pc_flag(PC_FLAG_LEVEL_UP_AVAILABLE);
    // Perks come every 2 levels (see `rt_perk_selections_earned`).
    int progression = 2;
    switch (at_lvl_phase) {
    case 0: {
        if (!at_aim_released()) {
            break;
        }
        // Start without selections left over from earlier steps.
        {
            editor_rt_update_level();
            int lastLevel;
            int freePerks;
            editor_rt_debug_get(&lastLevel, &freePerks);
            editor_rt_debug_set(lastLevel, 0);
        }
        // The next level, one without a perk: an even level on the way is
        // processed and its selection cleared first.
        if ((stat_pc_get(PC_STAT_LEVEL) + 1) % progression == 0) {
            at_lvl_gain(1);
            editor_rt_update_level();
            int lastLevel;
            int freePerks;
            editor_rt_debug_get(&lastLevel, &freePerks);
            editor_rt_debug_set(lastLevel, 0);
        }
        at_lvl_gain(1);
        at_log("level icon: now level %d, icon on=%d, pending perks %d", stat_pc_get(PC_STAT_LEVEL), is_pc_flag(PC_FLAG_LEVEL_UP_AVAILABLE), editor_rt_free_perks());
        at_lvl_ok[0] = is_pc_flag(PC_FLAG_LEVEL_UP_AVAILABLE);
        at_lvl_open(0);
        at_lvl_phase = 1;
        break;
    }
    case 1:
        if (!at_menu_seen || rt_pipboy_is_open()) {
            break;
        }
        at_menu_expected = false;
        // Points to spend, Stats not looked at: still on.
        at_lvl_unspent = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
        at_lvl_ok[1] = flag && at_lvl_unspent > 0;
        at_log("level icon: after the Inventory tab: on=%d (unspent %d)", flag, at_lvl_unspent);
        at_lvl_open(1);
        at_lvl_phase = 2;
        break;
    case 2:
        if (!at_menu_seen || rt_pipboy_is_open()) {
            break;
        }
        at_menu_expected = false;
        // Stats seen, no perk due: off, the points stay (banked).
        at_lvl_ok[2] = !flag && stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS) == at_lvl_unspent;
        at_log("level icon: after the Stats tab: on=%d, unspent %d (was %d), pending perks %d", flag, stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS), at_lvl_unspent, editor_rt_free_perks());
        // Up to the next perk level.
        {
            int level = stat_pc_get(PC_STAT_LEVEL);
            int levels = progression - level % progression;
            at_lvl_gain(levels == 0 ? progression : levels);
        }
        at_lvl_open(1);
        at_lvl_phase = 3;
        break;
    case 3:
        if (!at_menu_seen || rt_pipboy_is_open()) {
            break;
        }
        at_menu_expected = false;
        // A perk due, not chosen: stays on.
        at_lvl_ok[3] = flag && editor_rt_has_free_perk();
        at_log("level icon: level %d, perk due=%d, after the Stats tab: on=%d", stat_pc_get(PC_STAT_LEVEL), editor_rt_has_free_perk(), flag);
        at_lvl_unspent = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
        at_lvl_open(2);
        at_lvl_phase = 4;
        break;
    case 4:
        if (!at_menu_seen || rt_pipboy_is_open()) {
            break;
        }
        at_menu_expected = false;
        at_lvl_ok[4] = !flag && !editor_rt_has_free_perk() && at_lvl_perk != -1 && perk_level(at_lvl_perk) >= 1
            && stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS) == at_lvl_unspent;
        at_log("level icon: perk %s chosen: on=%d, unspent %d", at_lvl_perk != -1 ? perk_name(at_lvl_perk) : "(none)", flag, stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS));
        at_screenshot("level_01_cleared");
        at_check(C_LEVEL_ICON, at_lvl_ok[0] && at_lvl_ok[1] && at_lvl_ok[2] && at_lvl_ok[3] && at_lvl_ok[4],
            "on after the level up=%d; still on after the Inventory tab with points left=%d; off after the Stats tab, points kept=%d; on while a perk is due=%d; off once chosen=%d (save/load checked later)",
            at_lvl_ok[0], at_lvl_ok[1], at_lvl_ok[2], at_lvl_ok[3], at_lvl_ok[4]);
        return true;
    }
    return false;
}

static void at_record_level_state()
{
    at_lvl_flag_saved = is_pc_flag(PC_FLAG_LEVEL_UP_AVAILABLE);
    at_lvl_perk_saved = editor_rt_has_free_perk();
}

static bool step_level_after_load(unsigned int elapsed, bool first, bool timedOut)
{
    if (!first) {
        return true;
    }
    // Ammunition and inventory as saved (equipment and progression: the
    // Pip-Boy persistence check).
    Object* hand = inven_right_hand(obj_dude);
    int ammo = hand != NULL && item_get_type(hand) == ITEM_TYPE_WEAPON ? item_w_curr_ammo(hand) : -1;
    int items = 0;
    for (int index = 0; index < obj_dude->data.inventory.length; index++) {
        items += obj_dude->data.inventory.items[index].quantity;
    }
    if (at_save_items != -1) {
        at_check(C_LOAD, ammo == at_save_ammo && items == at_save_items && item_total_weight(obj_dude) == at_save_weight,
            "after load: weapon ammo %d (saved %d), items %d (saved %d), weight %d (saved %d)",
            ammo, at_save_ammo, items, at_save_items, item_total_weight(obj_dude), at_save_weight);
    }

    bool same = is_pc_flag(PC_FLAG_LEVEL_UP_AVAILABLE) == at_lvl_flag_saved && editor_rt_has_free_perk() == at_lvl_perk_saved;
    if (at_results[C_LEVEL_ICON] == 1) {
        at_check(C_LEVEL_ICON, same, "after load: icon on=%d (saved %d), perk due=%d (saved %d)",
            is_pc_flag(PC_FLAG_LEVEL_UP_AVAILABLE), at_lvl_flag_saved, editor_rt_has_free_perk(), at_lvl_perk_saved);
    }
    return true;
}

// -----------------------------------------------------------------------------
// Steps: character rules (SPECIAL formulas, perks, companions, replaced perks)
// -----------------------------------------------------------------------------


// Prototype of the first item with this name, or -1.
static int at_item_pid_by_name(const char* name)
{
    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_ITEM << 24) | index;
        Proto* proto;
        if (proto_ptr(pid, &proto) != -1 && SDL_strcasecmp(proto_name(pid), name) == 0) {
            return pid;
        }
    }
    return -1;
}

// Index of a script in scripts.lst, or -1.
static int at_script_index(const char* script)
{
    for (int index = 0; index < 2000; index++) {
        char name[16];
        if (scr_list_str(index, name, sizeof(name)) == -1) {
            break;
        }
        if (SDL_strcasecmp(name, script) == 0) {
            return index;
        }
    }
    return -1;
}

static bool at_tile_free(int tile)
{
    return tile >= 0 && tile < HEX_GRID_SIZE && obj_blocking_at(NULL, tile, obj_dude->elevation) == NULL;
}

// A calm critter (script removed) whose prototype differs from the player's
// and from the `count` critters in `others` (the party keeps one member per
// prototype).
static Object* at_spawn_distinct_critter(Object** others, int count)
{
    int tile = at_free_tile(2, 7);
    if (tile == -1) {
        return NULL;
    }
    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_CRITTER << 24) | index;
        Proto* proto;
        if (pid == obj_dude->pid || proto_ptr(pid, &proto) == -1) {
            continue;
        }
        bool used = false;
        for (int other = 0; other < count; other++) {
            if (others[other] != NULL && others[other]->pid == pid) {
                used = true;
            }
        }
        if (used) {
            continue;
        }
        Object* critter;
        if (obj_pid_new(&critter, pid) == -1) {
            continue;
        }
        if (critter->sid != -1) {
            scr_remove(critter->sid);
            critter->sid = -1;
        }
        Rect rect;
        obj_move_to_tile(critter, tile, obj_dude->elevation, &rect);
        tile_refresh_rect(&rect, obj_dude->elevation);
        return critter;
    }
    return NULL;
}

// Every hex within `radius` of `center` is free and in the line of fire
// from it.
static bool at_open_around(int center, int radius)
{
    for (int distance = 1; distance <= radius; distance++) {
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            int corner = tile_num_in_direction(center, rotation, distance);
            for (int step = 0; step < distance; step++) {
                int tile = corner != -1 ? tile_num_in_direction(corner, (rotation + 2) % ROTATION_COUNT, step) : -1;
                if (tile == -1 || obj_blocking_at(obj_dude, tile, obj_dude->elevation) != NULL
                    || combat_is_shot_blocked(obj_dude, center, tile, NULL, NULL)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Moves the player to the nearest spot with open ground `radius` hexes
// around (character rules fixtures need room).
static bool at_move_to_open_spot(int radius)
{
    for (int distance = 0; distance <= 40; distance++) {
        for (int rotation = 0; rotation < ROTATION_COUNT; rotation++) {
            int corner = distance == 0 ? obj_dude->tile : tile_num_in_direction(obj_dude->tile, rotation, distance);
            int steps = distance == 0 ? 1 : distance;
            for (int step = 0; step < steps && corner != -1; step++) {
                int tile = distance == 0 ? corner : tile_num_in_direction(corner, (rotation + 2) % ROTATION_COUNT, step);
                if (tile == -1 || obj_blocking_at(obj_dude, tile, obj_dude->elevation) != NULL || !at_open_around(tile, radius)) {
                    continue;
                }
                if (tile != obj_dude->tile) {
                    Rect rect;
                    register_clear(obj_dude);
                    obj_move_to_tile(obj_dude, tile, obj_dude->elevation, &rect);
                    tile_set_center(tile, TILE_SET_CENTER_REFRESH_WINDOW);
                    at_log("harness: player moved %d tiles to open ground (radius %d)", distance, radius);
                }
                return true;
            }
        }
    }
    at_log("harness: no open ground of radius %d found", radius);
    return false;
}

static void at_erase(Object* obj)
{
    if (obj == NULL) {
        return;
    }
    if (FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER && isPartyMember(obj)) {
        partyMemberRemove(obj);
    }
    Rect rect;
    if (FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER) {
        anim_rt_end_all(obj);
    }
    obj_erase_object(obj, &rect);
    tile_refresh_rect(&rect, obj_dude->elevation);
}

// Makes the effective value of a SPECIAL attribute `value`; returns the
// previous bonus (restore with `stat_set_bonus`).
static int at_set_special(int stat, int value)
{
    int bonus = stat_get_bonus(obj_dude, stat);
    int effective = stat_level(obj_dude, stat);
    stat_set_bonus(obj_dude, stat, bonus + value - effective);
    return bonus;
}

#define AT_RULE_AC SKILL_COUNT
#define AT_RULE_AP (SKILL_COUNT + 1)
#define AT_RULE_CARRY (SKILL_COUNT + 2)
#define AT_RULE_MELEE (SKILL_COUNT + 3)
#define AT_RULE_VALUES (SKILL_COUNT + 4)

static void at_rule_values(int* values)
{
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        values[skill] = skill_level(obj_dude, skill);
    }
    values[AT_RULE_AC] = stat_level(obj_dude, STAT_ARMOR_CLASS);
    values[AT_RULE_AP] = stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS);
    values[AT_RULE_CARRY] = stat_level(obj_dude, STAT_CARRY_WEIGHT);
    values[AT_RULE_MELEE] = stat_level(obj_dude, STAT_MELEE_DAMAGE);
}

static const char* at_rule_value_name(int index)
{
    switch (index) {
    case AT_RULE_AC:
        return "AC";
    case AT_RULE_AP:
        return "AP";
    case AT_RULE_CARRY:
        return "Carry";
    case AT_RULE_MELEE:
        return "MeleeDmg";
    }
    return skill_name(index);
}

// Changes `stat` by 2 (down when it's 9 or 10) and compares every skill and
// derived value against `expected` (per 2 points). Returns true when all
// match; `note` lists the changes.
static bool at_rule_special(int stat, const int* expectedPer2, char* note, size_t size)
{
    int before[AT_RULE_VALUES];
    int after[AT_RULE_VALUES];
    at_rule_values(before);

    int level = stat_level(obj_dude, stat);
    int delta = level <= 8 ? 2 : -2;
    int bonus = stat_get_bonus(obj_dude, stat);
    stat_set_bonus(obj_dude, stat, bonus + delta);
    at_rule_values(after);
    stat_set_bonus(obj_dude, stat, bonus);

    bool ok = true;
    snprintf(note, size, "%s %d->%d:", stat_name(stat), level, level + delta);
    for (int index = 0; index < AT_RULE_VALUES; index++) {
        int change = after[index] - before[index];
        int expected = expectedPer2[index] * delta / 2;
        if (index == AT_RULE_AP && stat == STAT_AGILITY) {
            expected = (level + delta) / 2 - level / 2;
        } else if (index == AT_RULE_MELEE && stat == STAT_STRENGTH) {
            expected = std::max(level + delta - 5, 1) - std::max(level - 5, 1);
        }
        if (change != 0 || expected != 0) {
            char part[48];
            snprintf(part, sizeof(part), " %s %+d%s", at_rule_value_name(index), change, change == expected ? "" : "(!)");
            SDL_strlcat(note, part, size);
        }
        if (change != expected) {
            ok = false;
        }
    }
    at_log("rules: %s", note);
    return ok;
}

static bool step_rules_special(unsigned int elapsed, bool first, bool timedOut)
{
    char note[400];

    // Per +2 points of the attribute.
    int agility[AT_RULE_VALUES] = { 0 };
    agility[SKILL_SMALL_GUNS] = 2;
    agility[SKILL_BIG_GUNS] = 2;
    agility[SKILL_LOCKPICK] = 1;
    agility[SKILL_STEAL] = 2;
    agility[SKILL_TRAPS] = 1;
    at_check(C_RULE_SPECIAL_AG, at_rule_special(STAT_AGILITY, agility, note, sizeof(note)), "%s", note);

    int strength[AT_RULE_VALUES] = { 0 };
    strength[SKILL_UNARMED] = 2;
    strength[SKILL_MELEE_WEAPONS] = 2;
    strength[SKILL_THROWING] = 2;
    strength[AT_RULE_CARRY] = 50;
    at_check(C_RULE_SPECIAL_ST, at_rule_special(STAT_STRENGTH, strength, note, sizeof(note)), "%s", note);

    int intelligence[AT_RULE_VALUES] = { 0 };
    intelligence[SKILL_ENERGY_WEAPONS] = 2;
    intelligence[SKILL_FIRST_AID] = 1;
    intelligence[SKILL_DOCTOR] = 1;
    intelligence[SKILL_SCIENCE] = 4;
    intelligence[SKILL_REPAIR] = 2;
    intelligence[SKILL_OUTDOORSMAN] = 1;
    at_check(C_RULE_SPECIAL_IN, at_rule_special(STAT_INTELLIGENCE, intelligence, note, sizeof(note)), "%s", note);

    int endurance[AT_RULE_VALUES] = { 0 };
    endurance[AT_RULE_AC] = 2;
    endurance[SKILL_OUTDOORSMAN] = 1;
    bool enOk = at_rule_special(STAT_ENDURANCE, endurance, note, sizeof(note));
    // Armor still adds on top of Endurance.
    int ac = stat_level(obj_dude, STAT_ARMOR_CLASS);
    int expectedAc = stat_level(obj_dude, STAT_ENDURANCE) + stat_get_bonus(obj_dude, STAT_ARMOR_CLASS);
    char acNote[96];
    snprintf(acNote, sizeof(acNote), "; AC %d = EN %d + bonus (armor, perks) %d", ac, stat_level(obj_dude, STAT_ENDURANCE), stat_get_bonus(obj_dude, STAT_ARMOR_CLASS));
    SDL_strlcat(note, acNote, sizeof(note));
    at_check(C_RULE_SPECIAL_EN, enOk && ac == std::clamp(expectedAc, 0, 90), "%s", note);

    // Charisma: Speech and Barter as before, companions floor(CH / 2), max 5.
    int charisma[AT_RULE_VALUES] = { 0 };
    charisma[SKILL_SPEECH] = 4;
    charisma[SKILL_BARTER] = 4;
    bool chaSkills = at_rule_special(STAT_CHARISMA, charisma, note, sizeof(note));
    char limits[160] = "limits:";
    bool limitsOk = true;
    for (int value = 1; value <= 10; value++) {
        int bonus = at_set_special(STAT_CHARISMA, value);
        int limit = rt_companion_limit();
        stat_set_bonus(obj_dude, STAT_CHARISMA, bonus);
        char part[16];
        snprintf(part, sizeof(part), " %d:%d", value, limit);
        SDL_strlcat(limits, part, sizeof(limits));
        if (limit != std::min(value / 2, 5)) {
            limitsOk = false;
        }
    }
    at_check(C_RULE_CHA_LIMIT, chaSkills && limitsOk, "%s (CH:companions); %s", limits, note);
    return true;
}

static bool step_rules_perks(unsigned int elapsed, bool first, bool timedOut)
{
    int savedLevel = stat_pc_get(PC_STAT_LEVEL);
    int savedSp = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
    int savedLastLevel;
    int savedFree;
    editor_rt_debug_get(&savedLastLevel, &savedFree);

    // Level by level from 1 to 9, each level processed twice (a reopened
    // screen): selections at 2, 4, 6, 8 only.
    char line[200] = "selections after level:";
    bool cadenceOk = true;
    editor_rt_debug_set(1, 0);
    for (int level = 1; level <= 9; level++) {
        stat_pc_set(PC_STAT_LEVEL, level);
        editor_rt_update_level();
        int once = editor_rt_free_perks();
        editor_rt_update_level();
        int twice = editor_rt_free_perks();
        char part[16];
        snprintf(part, sizeof(part), " %d:%d", level, twice);
        SDL_strlcat(line, part, sizeof(line));
        if (once != twice || twice != level / 2) {
            cadenceOk = false;
        }
    }

    // Several levels at once (1 -> 7) add up.
    editor_rt_debug_set(1, 0);
    stat_pc_set(PC_STAT_LEVEL, 7);
    editor_rt_update_level();
    int jump = editor_rt_free_perks();

    // Spending one leaves the others; processing again adds nothing.
    int perks[PERK_COUNT];
    int count = perk_make_list(perks);
    int spent = -1;
    for (int index = 0; index < count && spent == -1; index++) {
        int perk = perks[index];
        if (perk != PERK_TAG && perk != PERK_MUTATE && perk != PERK_LIFEGIVER && perk != PERK_EDUCATED) {
            spent = perk;
        }
    }
    int rankBefore = spent != -1 ? perk_level(spent) : 0;
    bool took = spent != -1 && editor_rt_take_perk(spent, -1, -1) == 0;
    int afterSpend = editor_rt_free_perks();
    editor_rt_update_level();
    int afterReopen = editor_rt_free_perks();
    bool rankUp = spent != -1 && perk_level(spent) == rankBefore + 1;
    if (took) {
        perk_sub(spent);
    }

    at_check(C_RULE_PERK_CADENCE,
        cadenceOk && jump == 3 && took && rankUp && afterSpend == 2 && afterReopen == 2,
        "%s; level 1->7 at once: %d; took %s: %d left, %d after processing again",
        line, jump, spent != -1 ? perk_name(spent) : "(none)", afterSpend, afterReopen);

    // Eligibility follows the new minimum levels.
    bool eligibleOk = true;
    char eligible[200] = "";
    for (int level = 2; level <= 4; level++) {
        stat_pc_set(PC_STAT_LEVEL, level);
        count = perk_make_list(perks);
        for (int index = 0; index < count; index++) {
            if (perk_min_level(perks[index]) > level) {
                eligibleOk = false;
            }
        }
        char part[48];
        snprintf(part, sizeof(part), "%s%d eligible at level %d", level > 2 ? ", " : "", count, level);
        SDL_strlcat(eligible, part, sizeof(eligible));
    }

    stat_pc_set(PC_STAT_LEVEL, savedLevel);
    stat_pc_set(PC_STAT_UNSPENT_SKILL_POINTS, savedSp);

    static const int table[][3] = {
        { PERK_ANIMAL_FRIEND, 4, 1 },
        { PERK_ACTION_BOY, 4, 3 },
        { PERK_AWARENESS, 2, 1 },
        { PERK_BETTER_CRITICALS, 6, 1 },
        { PERK_BONUS_HTH_DAMAGE, 2, 3 },
        { PERK_BONUS_RATE_OF_FIRE, 8, 1 },
        { PERK_DODGER, 10, 2 },
        { PERK_FRIENDLY_FOE, 4, 1 },
        { PERK_TOUGHNESS, 2, 3 },
        { PERK_SCROUNGER, 8, 1 },
        { PERK_EDUCATED, 4, 3 },
        { PERK_PRESENCE, 4, 3 },
        { PERK_SMOOTH_TALKER, 4, 3 },
        { PERK_EARLIER_SEQUENCE, 2, 3 },
        { PERK_FASTER_HEALING, 2, 3 },
        { PERK_NIGHT_VISION, 2, 3 },
        { PERK_STRONG_BACK, 4, 3 },
        { PERK_SURVIVALIST, 2, 3 },
        { PERK_HEALER, 2, 3 },
        { PERK_SCOUT, 2, 1 },
        { PERK_QUICK_POCKETS, 2, 3 },
        { PERK_SWIFT_LEARNER, 4, 3 },
        { PERK_BONUS_HTH_ATTACKS, 6, 1 },
        { PERK_BONUS_MOVE, 6, 1 },
        { PERK_HEAVE_HO, 6, 3 },
    };
    bool tableOk = true;
    char wrong[200] = "";
    for (const auto& row : table) {
        if (perk_min_level(row[0]) != row[1] || perk_max_rank(row[0]) != row[2]) {
            tableOk = false;
            char part[64];
            snprintf(part, sizeof(part), " %s %d/%d", perk_name(row[0]), perk_min_level(row[0]), perk_max_rank(row[0]));
            SDL_strlcat(wrong, part, sizeof(wrong));
        }
    }
    int level3 = 0;
    for (int perk = 0; perk < PERK_COUNT; perk++) {
        if (perk_max_rank(perk) != -1 && perk_min_level(perk) == 3) {
            level3++;
        }
    }

    bool textOk = strstr(perk_description(PERK_BONUS_HTH_ATTACKS), "1.5 hexes") != NULL
        && strstr(perk_description(PERK_BONUS_MOVE), "50%") != NULL
        && strstr(perk_description(PERK_BONUS_RATE_OF_FIRE), "25%") != NULL
        && strstr(perk_description(PERK_HEAVE_HO), "20%") != NULL
        && strstr(perk_description(PERK_FRIENDLY_FOE), "never hurt you") != NULL
        && strstr(trait_description(TRAIT_SKILLED), "four levels") == NULL
        && strstr(stat_description(STAT_AGILITY), "Armor Class") == NULL
        && strstr(stat_description(STAT_ENDURANCE), "Armor Class") != NULL
        && strstr(stat_description(STAT_CHARISMA), "companions") != NULL
        && strcmp(skill_attribute(SKILL_ENERGY_WEAPONS), "+ IN") == 0
        && strcmp(skill_attribute(SKILL_MELEE_WEAPONS), "+ ST") == 0;
    at_log("rules: Bonus HtH Attacks: %s", perk_description(PERK_BONUS_HTH_ATTACKS));
    at_log("rules: Bonus Move: %s", perk_description(PERK_BONUS_MOVE));
    at_log("rules: Bonus Rate of Fire: %s", perk_description(PERK_BONUS_RATE_OF_FIRE));
    at_log("rules: Heave Ho!: %s", perk_description(PERK_HEAVE_HO));
    at_log("rules: Friendly Foe: %s", perk_description(PERK_FRIENDLY_FOE));

    at_check(C_RULE_PERK_TABLE, eligibleOk && tableOk && level3 == 0 && textOk,
        "%s; table %s%s; perks still at level 3: %d; descriptions updated=%d",
        eligible, tableOk ? "ok" : "wrong:", wrong, level3, textOk);

    // Old saves: (last level, stored byte, Skilled) -> pending now.
    static const int cases[][5] = {
        // level, stored, skilled, expected
        { 1, 0, 0, 0 },
        { 3, 0, 0, 0 },
        { 3, 1, 0, 1 },
        { 4, 0, 0, 1 },
        { 6, 1, 0, 2 },
        { 6, 0, 0, 1 },
        { 7, 0, 1, 2 },
        { 12, 0, 0, 2 },
        { 12, 1, 1, 4 },
    };
    bool convertOk = true;
    char conversions[300] = "";
    int trait1;
    int trait2;
    trait_get(&trait1, &trait2);
    for (const auto& row : cases) {
        bool skilled = row[2] != 0;
        if (skilled && trait1 != TRAIT_SKILLED && trait2 != TRAIT_SKILLED) {
            trait_set(TRAIT_SKILLED, trait1 != TRAIT_SKILLED ? trait1 : trait2);
        }
        int pending = editor_rt_debug_convert(row[0], row[1]);
        trait_set(trait1, trait2);
        char part[48];
        snprintf(part, sizeof(part), " L%d/%d%s->%d", row[0], row[1], skilled ? "S" : "", pending);
        SDL_strlcat(conversions, part, sizeof(conversions));
        if (pending != row[3]) {
            convertOk = false;
        }
    }
    at_check(C_RULE_PERK_CONVERT, convertOk, "old save (level/pending[S=Skilled]) -> pending now:%s", conversions);

    // Leave two pending selections for the save/load check.
    editor_rt_debug_set(savedLastLevel, std::max(savedFree, 2));
    at_log("rules: pending perk selections left for the save test: %d (were %d)", editor_rt_free_perks(), savedFree);
    return true;
}

static bool step_rules_companions(unsigned int elapsed, bool first, bool timedOut)
{
    static const char* scripts[] = { "Ian.int", "Tycho.int", "Katja.int", "Junkdog.int", "Tandi.int" };
    // Calm test critters given the followers' own scripts (their prototypes
    // carry none; maps attach them). Everything happens in this tick, before
    // the scripts get to run.
    Object* critters[5] = { NULL };
    for (int index = 0; index < 5; index++) {
        int scriptIndex = at_script_index(scripts[index]);
        Object* critter = scriptIndex != -1 ? at_spawn_distinct_critter(critters, index) : NULL;
        if (critter != NULL && obj_new_sid_inst(critter, SCRIPT_TYPE_CRITTER, scriptIndex) == -1) {
            at_erase(critter);
            critter = NULL;
        }
        critters[index] = critter;
    }
    Object* ian = critters[0];
    Object* tycho = critters[1];
    Object* katja = critters[2];
    Object* dogmeat = critters[3];
    Object* tandi = critters[4];
    if (ian == NULL || tycho == NULL || katja == NULL || dogmeat == NULL || tandi == NULL) {
        at_check(C_RULE_CHA_RECRUIT, false, "fixtures missing (Ian %d, Tycho %d, Katja %d, Dogmeat %d, Tandi %d)",
            ian != NULL, tycho != NULL, katja != NULL, dogmeat != NULL, tandi != NULL);
        for (Object* critter : critters) {
            at_erase(critter);
        }
        return true;
    }

    bool kinds = rt_is_companion(ian) && rt_is_companion(tycho) && rt_is_companion(katja) && rt_is_companion(dogmeat) && !rt_is_companion(tandi);
    int baseCount = partyMemberCompanionCount();
    int bonus = stat_get_bonus(obj_dude, STAT_CHARISMA);

    at_set_special(STAT_CHARISMA, 1);
    int ianAt1 = partyMemberAdd(ian);
    at_set_special(STAT_CHARISMA, 2);
    int ianAt2 = partyMemberAdd(ian);
    int tychoAt2 = partyMemberAdd(tycho);
    at_set_special(STAT_CHARISMA, 4);
    int tychoAt4 = partyMemberAdd(tycho);
    int countAt4 = partyMemberCompanionCount();

    // Charisma drops (a temporary penalty): nobody leaves, nobody new joins.
    at_set_special(STAT_CHARISMA, 1);
    bool stayed = isPartyMember(ian) && isPartyMember(tycho);
    int dogAt1 = partyMemberAdd(dogmeat);
    int tandiAt1 = partyMemberAdd(tandi);
    int countAt1 = partyMemberCompanionCount();

    // The script's join is undone: Katja's status variable keeps its value.
    int katjaVar = 244;
    int before = game_get_global_var(katjaVar);
    int refused = intextra_rt_debug_party_add(katja, katjaVar, before + 7);
    int afterRefusal = game_get_global_var(katjaVar);
    bool katjaOut = !isPartyMember(katja);
    at_set_special(STAT_CHARISMA, 6);
    int joined = intextra_rt_debug_party_add(katja, katjaVar, before);
    bool katjaIn = isPartyMember(katja);
    game_set_global_var(katjaVar, before);

    stat_set_bonus(obj_dude, STAT_CHARISMA, bonus);
    for (Object* critter : critters) {
        at_erase(critter);
    }

    bool ok = kinds
        && ianAt1 == PARTY_MEMBER_ADD_COMPANION_LIMIT && ianAt2 == 0
        && tychoAt2 == PARTY_MEMBER_ADD_COMPANION_LIMIT && tychoAt4 == 0 && countAt4 == baseCount + 2
        && stayed && dogAt1 == PARTY_MEMBER_ADD_COMPANION_LIMIT && tandiAt1 == 0 && countAt1 == baseCount + 2
        && refused >= 1 && afterRefusal == before && katjaOut
        && joined == -1 && katjaIn
        && partyMemberCompanionCount() == baseCount;
    at_check(C_RULE_CHA_RECRUIT, ok,
        "CH1 Ian %d; CH2 Ian %d, Tycho %d; CH4 Tycho %d (companions %d); CH1 again: kept=%d, Dogmeat %d, Tandi (temporary) %d, companions %d; "
        "script join refused: %d change(s) undone, Katja var %d->%d, out=%d; CH6 join %d in=%d",
        ianAt1, ianAt2, tychoAt2, tychoAt4, countAt4 - baseCount, stayed, dogAt1, tandiAt1, countAt1 - baseCount,
        refused, before, afterRefusal, katjaOut, joined, katjaIn);
    return true;
}

// Bonus HtH Attacks reach.
static Object* at_reach_rats[6];
static Object* at_reach_wall;
static int at_reach_count_a;

static int at_hex_walk(int tile, int rotation1, int steps1, int rotation2, int steps2)
{
    if (tile == -1) {
        return -1;
    }
    if (steps1 > 0) {
        tile = tile_num_in_direction(tile, rotation1, steps1);
    }
    if (tile != -1 && steps2 > 0) {
        tile = tile_num_in_direction(tile, rotation2, steps2);
    }
    return tile;
}

static bool step_rules_reach(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        memset(at_reach_rats, 0, sizeof(at_reach_rats));
        at_reach_wall = NULL;
        at_clear_hostiles();
        if (!at_move_to_open_spot(4)) {
            at_move_to_open_spot(3);
        }
        return false;
    }

    at_heal();
    switch (at_phase) {
    case 0: {
        if (!at_unarmed() || anim_busy(obj_dude)) {
            break;
        }

        // Two steps straight (2.0 apart), two steps with a turn (1.73),
        // three steps with a turn (2.65) and straight (3.0).
        int rotation = -1;
        int tiles[4];
        for (int r = 0; r < ROTATION_COUNT && rotation == -1; r++) {
            tiles[0] = at_hex_walk(obj_dude->tile, r, 2, 0, 0);
            tiles[1] = at_hex_walk(obj_dude->tile, r, 1, (r + 1) % ROTATION_COUNT, 1);
            tiles[2] = at_hex_walk(obj_dude->tile, r, 2, (r + 1) % ROTATION_COUNT, 1);
            tiles[3] = at_hex_walk(obj_dude->tile, r, 3, 0, 0);
            bool usable = true;
            for (int tile : tiles) {
                if (!at_tile_free(tile) || combat_is_shot_blocked(obj_dude, obj_dude->tile, tile, NULL, NULL)) {
                    usable = false;
                }
            }
            if (usable) {
                rotation = r;
            }
        }
        if (rotation == -1) {
            at_check(C_RULE_REACH, false, "no free hexes around the player");
            return true;
        }
        for (int index = 0; index < 4; index++) {
            at_reach_rats[index] = at_spawn_rat_at(tiles[index], false);
        }

        int hitMode = HIT_MODE_PUNCH;
        int without[4];
        int with[4];
        double distance[4];
        perk_rt_debug_set_rank(PERK_BONUS_HTH_ATTACKS, 0);
        for (int index = 0; index < 4; index++) {
            without[index] = combat_rt_check_reach(obj_dude, at_reach_rats[index], hitMode);
            distance[index] = rt_obj_distance_exact(obj_dude, at_reach_rats[index]);
        }
        perk_rt_debug_set_rank(PERK_BONUS_HTH_ATTACKS, 1);
        for (int index = 0; index < 4; index++) {
            with[index] = combat_rt_check_reach(obj_dude, at_reach_rats[index], hitMode);
        }
        // Critters other than the player don't get the reach.
        int ratReach = combat_rt_check_reach(at_reach_rats[0], obj_dude, HIT_MODE_PUNCH);

        // A 2-hex weapon (spear) with the perk: 3.46 in reach, 3.61 not.
        char spearNote[160] = "spear: not available";
        bool spearOk = true;
        int spearPid = at_item_pid_by_name("Spear");
        Object* spear = NULL;
        if (spearPid != -1 && obj_pid_new(&spear, spearPid) != -1) {
            Rect rect;
            if (item_add_force(obj_dude, spear, 1) == 0) {
                obj_disconnect(spear, &rect);
            }
            int near4 = -1;
            int far4 = -1;
            for (int r = 0; r < ROTATION_COUNT && near4 == -1; r++) {
                int a = at_hex_walk(obj_dude->tile, r, 2, (r + 1) % ROTATION_COUNT, 2);
                int b = at_hex_walk(obj_dude->tile, r, 3, (r + 1) % ROTATION_COUNT, 1);
                if (at_tile_free(a) && at_tile_free(b)
                    && !combat_is_shot_blocked(obj_dude, obj_dude->tile, a, NULL, NULL)
                    && !combat_is_shot_blocked(obj_dude, obj_dude->tile, b, NULL, NULL)) {
                    near4 = a;
                    far4 = b;
                }
            }
            if (at_wield(spear) && near4 != -1) {
                at_reach_rats[4] = at_spawn_rat_at(near4, false);
                at_reach_rats[5] = at_spawn_rat_at(far4, false);
                int mode = at_current_hit_mode();
                int nearReach = combat_rt_check_reach(obj_dude, at_reach_rats[4], mode);
                int farReach = combat_rt_check_reach(obj_dude, at_reach_rats[5], mode);
                spearOk = item_w_range(obj_dude, mode) == 2 && nearReach == COMBAT_BAD_SHOT_OK && farReach == COMBAT_BAD_SHOT_OUT_OF_RANGE;
                snprintf(spearNote, sizeof(spearNote), "spear (range %d) + perk: %d steps/%.2f -> %d, %d steps/%.2f -> %d",
                    item_w_range(obj_dude, mode),
                    tile_dist(obj_dude->tile, near4), rt_obj_distance_exact(obj_dude, at_reach_rats[4]), nearReach,
                    tile_dist(obj_dude->tile, far4), rt_obj_distance_exact(obj_dude, at_reach_rats[5]), farReach);
            } else {
                SDL_strlcpy(spearNote, "spear: could not set up", sizeof(spearNote));
            }
            at_unarmed();
        }

        bool ok = without[0] == COMBAT_BAD_SHOT_OUT_OF_RANGE && without[1] == COMBAT_BAD_SHOT_OUT_OF_RANGE
            && with[0] == COMBAT_BAD_SHOT_OK && with[1] == COMBAT_BAD_SHOT_OK
            && with[2] == COMBAT_BAD_SHOT_OUT_OF_RANGE && with[3] == COMBAT_BAD_SHOT_OUT_OF_RANGE
            && ratReach == COMBAT_BAD_SHOT_OUT_OF_RANGE && spearOk;
        at_check(C_RULE_REACH, ok,
            "unarmed, distances %.2f/%.2f/%.2f/%.2f hexes: without perk %d/%d/%d/%d, with perk %d/%d/%d/%d (0 = in reach, 2 = out); rat at 2.0: %d; %s",
            distance[0], distance[1], distance[2], distance[3],
            without[0], without[1], without[2], without[3],
            with[0], with[1], with[2], with[3], ratReach, spearNote);

        // Blocked: a wall between the player and a hex two steps away.
        char blockNote[160] = "";
        int blocked = -1;
        for (int r = 0; r < ROTATION_COUNT && blocked == -1; r++) {
            int middle = at_hex_walk(obj_dude->tile, r, 1, 0, 0);
            int end = at_hex_walk(obj_dude->tile, r, 2, 0, 0);
            if (!at_tile_free(end)) {
                continue;
            }
            if (!combat_is_shot_blocked(obj_dude, obj_dude->tile, end, NULL, NULL)) {
                if (!at_tile_free(middle)) {
                    continue;
                }
                // No wall here: put one in (the game's first wall prototype).
                for (int index = 1; index < 300 && at_reach_wall == NULL; index++) {
                    Object* wall;
                    if (obj_pid_new(&wall, (OBJ_TYPE_WALL << 24) | index) != -1) {
                        Rect rect;
                        obj_move_to_tile(wall, middle, obj_dude->elevation, &rect);
                        if (combat_is_shot_blocked(obj_dude, obj_dude->tile, end, NULL, NULL)) {
                            at_reach_wall = wall;
                        } else {
                            obj_erase_object(wall, &rect);
                        }
                    }
                }
                if (at_reach_wall == NULL) {
                    continue;
                }
            }
            blocked = end;
        }
        if (blocked != -1) {
            Object* rat = at_spawn_rat_at(blocked, false);
            int reach = combat_rt_check_reach(obj_dude, rat, HIT_MODE_PUNCH);
            snprintf(blockNote, sizeof(blockNote), "%s wall, target %.2f hexes away: %d (5 = blocked)",
                at_reach_wall != NULL ? "test" : "map", rt_obj_distance_exact(obj_dude, rat), reach);
            at_check(C_RULE_REACH_BLOCKED, reach == COMBAT_BAD_SHOT_AIM_BLOCKED, "%s", blockNote);
            at_erase(rat);
        } else {
            at_check(C_RULE_REACH_BLOCKED, false, "no hex to block");
        }
        if (at_reach_wall != NULL) {
            at_erase(at_reach_wall);
            at_reach_wall = NULL;
        }

        rt_debug_set_lock(at_reach_rats[0]);
        at_time_a = now;
        at_phase = 1;
        break;
    }
    case 1: {
        if (now - at_time_a < 400) {
            break;
        }
        char note[240];
        bool shown = at_indicator_matches(at_reach_rats[0], note, sizeof(note));
        int aim = rt_aim_check_shot(at_reach_rats[0], HIT_MODE_PUNCH);
        at_log("rules: reach lock: %s; aimed shot check %d", note, aim);
        at_flag_a = shown && aim == COMBAT_BAD_SHOT_OK;
        at_reach_count_a = rt_get_stats()->playerAttacks;
        at_screenshot("40_reach_lock");
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_time_a = now;
        at_phase = 2;
        break;
    }
    case 2: {
        if (now - at_time_a < 2000) {
            break;
        }
        int attacks = rt_get_stats()->playerAttacks - at_reach_count_a;
        at_check(C_RULE_REACH_ATTACK, at_flag_a && attacks >= 1,
            "target 2 hexes away: indicator and aimed shot ok=%d, attacks started=%d, last reject %d",
            at_flag_a, attacks, rt_get_stats()->lastRejectReason);
        rt_debug_set_lock(NULL);
        perk_rt_debug_set_rank(PERK_BONUS_HTH_ATTACKS, 0);
        for (Object*& rat : at_reach_rats) {
            if (rat != NULL && !critter_is_dead(rat)) {
                at_erase(rat);
            }
            rat = NULL;
        }
        at_clear_hostiles();
        return true;
    }
    }
    return false;
}

// Long-range accuracy (Bonus Rate of Fire, Heave Ho!).
static Object* at_lr_far;
static Object* at_lr_near;
static int at_lr_bonus_pe;
static char at_lr_note[400];
static bool at_lr_ok;
static Object* at_lr_throw;

// The hit chance for the current weapon with the perk at `rank`.
static int at_lr_chance(int perk, int rank, Object* target)
{
    int saved = perk_level(perk);
    perk_rt_debug_set_rank(perk, rank);
    int chance = determine_to_hit(obj_dude, target, HIT_LOCATION_UNCALLED, at_current_hit_mode());
    perk_rt_debug_set_rank(perk, saved);
    return chance;
}

// Checks the perk's effect on the current weapon: the far target's penalty
// shrinks by `pct` per rank, the near one is unchanged.
static bool at_lr_measure(const char* label, int perk, int maxRank, int pct)
{
    int perception = stat_level(obj_dude, STAT_PERCEPTION);
    int range = obj_dist(obj_dude, at_lr_far) - 2 * perception - 2 * perk_level(PERK_SHARPSHOOTER);
    int penalty = range > 0 ? 4 * range : 0;
    int base = at_lr_chance(perk, 0, at_lr_far);
    int nearBase = at_lr_chance(perk, 0, at_lr_near);
    bool ok = penalty > 0;
    char part[160];
    snprintf(part, sizeof(part), " %s (penalty %d at %d hexes, PE %d): %d%%", label, penalty, obj_dist(obj_dude, at_lr_far), perception, base);
    SDL_strlcat(at_lr_note, part, sizeof(at_lr_note));
    for (int rank = 1; rank <= maxRank; rank++) {
        int chance = at_lr_chance(perk, rank, at_lr_far);
        int reduction = std::min(penalty * std::min(pct * rank, 100) / 100, penalty);
        int expected = std::min(base + reduction, 95);
        snprintf(part, sizeof(part), " r%d %d%% (exp %d)", rank, chance, expected);
        SDL_strlcat(at_lr_note, part, sizeof(at_lr_note));
        if (chance != expected) {
            ok = false;
        }
    }
    int nearChance = at_lr_chance(perk, maxRank, at_lr_near);
    snprintf(part, sizeof(part), ", near %d->%d;", nearBase, nearChance);
    SDL_strlcat(at_lr_note, part, sizeof(at_lr_note));
    return ok && nearChance == nearBase;
}

static bool step_rules_long_range(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    Object* pistol = at_find_inventory("10mm Pistol");
    if (first) {
        at_phase = 0;
        at_lr_far = NULL;
        at_lr_near = NULL;
        at_lr_note[0] = 0;
        at_lr_ok = true;
        at_clear_hostiles();
        if (!at_move_to_open_spot(6)) {
            at_move_to_open_spot(5);
        }
        // Perception 1: targets beyond 2 hexes get the distance penalty.
        at_lr_bonus_pe = at_set_special(STAT_PERCEPTION, 1);
        return false;
    }

    at_heal();
    switch (at_phase) {
    case 0: {
        if (!at_wield(pistol) || anim_busy(obj_dude)) {
            if (timedOut) {
                at_check(C_RULE_LONG_RANGE, false, "no pistol");
                stat_set_bonus(obj_dude, STAT_PERCEPTION, at_lr_bonus_pe);
                return true;
            }
            break;
        }
        for (int attempt = 0; attempt < 40 && at_lr_far == NULL; attempt++) {
            int tile = at_free_tile(5, 9);
            if (tile != -1 && !combat_is_shot_blocked(obj_dude, obj_dude->tile, tile, NULL, NULL)) {
                at_lr_far = at_spawn_rat_at(tile, false);
            }
        }
        for (int attempt = 0; attempt < 40 && at_lr_near == NULL; attempt++) {
            int tile = at_free_tile(1, 2);
            if (tile != -1 && !combat_is_shot_blocked(obj_dude, obj_dude->tile, tile, NULL, NULL)) {
                at_lr_near = at_spawn_rat_at(tile, false);
            }
        }
        if (at_lr_far == NULL || at_lr_near == NULL) {
            at_check(C_RULE_LONG_RANGE, false, "no clear targets (far %d, near %d)", at_lr_far != NULL, at_lr_near != NULL);
            stat_set_bonus(obj_dude, STAT_PERCEPTION, at_lr_bonus_pe);
            at_clear_hostiles();
            return true;
        }

        at_lr_ok = at_lr_measure("10mm pistol, Bonus Rate of Fire", PERK_BONUS_RATE_OF_FIRE, 1, 25) && at_lr_ok;
        // Heave Ho! doesn't touch guns.
        int heave = at_lr_chance(PERK_HEAVE_HO, 3, at_lr_far);
        int none = at_lr_chance(PERK_HEAVE_HO, 0, at_lr_far);
        if (heave != none) {
            at_lr_ok = false;
            SDL_strlcat(at_lr_note, " Heave Ho! changed a gun!", sizeof(at_lr_note));
        }
        perk_rt_debug_set_rank(PERK_BONUS_RATE_OF_FIRE, 1);
        rt_debug_set_lock(at_lr_far);
        at_time_a = now;
        at_phase = 1;
        break;
    }
    case 1: {
        if (now - at_time_a < 400) {
            break;
        }
        // Displayed = rolled: the indicator shows `determine_to_hit`, the
        // same call the attack rolls against (`rt_attack`).
        char note[240];
        bool shown = at_indicator_matches(at_lr_far, note, sizeof(note));
        at_log("rules: long range lock with Bonus Rate of Fire: %s", note);
        at_flag_a = shown;
        at_screenshot("41_long_range_lock");
        rt_debug_set_lock(NULL);
        perk_rt_debug_set_rank(PERK_BONUS_RATE_OF_FIRE, 0);

        // Energy weapon.
        int laserPid = at_item_pid_by_name("Laser Pistol");
        Object* laser = NULL;
        if (laserPid != -1 && obj_pid_new(&laser, laserPid) != -1) {
            Rect rect;
            if (item_add_force(obj_dude, laser, 1) == 0) {
                obj_disconnect(laser, &rect);
            }
            item_w_set_curr_ammo(laser, item_w_max_ammo(laser));
        }
        at_obj_a = laser;
        at_phase = 2;
        at_time_a = now;
        break;
    }
    case 2: {
        if (at_obj_a != NULL && !at_wield(at_obj_a)) {
            if (now - at_time_a < 3000) {
                break;
            }
            SDL_strlcat(at_lr_note, " laser pistol: could not equip;", sizeof(at_lr_note));
            at_lr_ok = false;
        } else if (at_obj_a != NULL) {
            at_lr_ok = at_lr_measure("Laser Pistol, Bonus Rate of Fire", PERK_BONUS_RATE_OF_FIRE, 1, 25) && at_lr_ok;
        } else {
            SDL_strlcat(at_lr_note, " no laser pistol prototype;", sizeof(at_lr_note));
            at_lr_ok = false;
        }
        at_lr_throw = at_create_throw_weapon();
        at_phase = 3;
        at_time_a = now;
        break;
    }
    case 3: {
        if (at_lr_throw == NULL || !at_wield(at_lr_throw)) {
            if (now - at_time_a < 3000) {
                break;
            }
            SDL_strlcat(at_lr_note, " throwing weapon: could not equip;", sizeof(at_lr_note));
            at_lr_ok = false;
        } else {
            int mode = at_current_hit_mode();
            int rangeWithout = item_w_range(obj_dude, mode);
            perk_rt_debug_set_rank(PERK_HEAVE_HO, 3);
            int rangeWith = item_w_range(obj_dude, mode);
            perk_rt_debug_set_rank(PERK_HEAVE_HO, 0);
            char part[96];
            snprintf(part, sizeof(part), " throw range %d / %d with Heave Ho! 3 (3 x ST = %d);",
                rangeWithout, rangeWith, 3 * stat_level(obj_dude, STAT_STRENGTH));
            SDL_strlcat(at_lr_note, part, sizeof(at_lr_note));
            if (rangeWith != rangeWithout || obj_dist(obj_dude, at_lr_far) > rangeWithout) {
                at_lr_ok = false;
            }
            at_lr_ok = at_lr_measure(object_name(at_lr_throw), PERK_HEAVE_HO, 3, 20) && at_lr_ok;
            // Bonus Rate of Fire doesn't touch throwing.
            if (at_lr_chance(PERK_BONUS_RATE_OF_FIRE, 1, at_lr_far) != at_lr_chance(PERK_BONUS_RATE_OF_FIRE, 0, at_lr_far)) {
                at_lr_ok = false;
                SDL_strlcat(at_lr_note, " Bonus Rate of Fire changed a throw!", sizeof(at_lr_note));
            }
        }

        at_check(C_RULE_LONG_RANGE, at_lr_ok, "%s", at_lr_note);
        at_check(C_RULE_LONG_RANGE_SHOWN, at_flag_a, "indicator for the far target with Bonus Rate of Fire matches determine_to_hit (rolled by the attack)");
        stat_set_bonus(obj_dude, STAT_PERCEPTION, at_lr_bonus_pe);
        at_wield(pistol);
        at_clear_hostiles();
        return true;
    }
    }
    return false;
}

// Friendly Foe: bursts through the player.
static int at_ff_trials(Object* shooter, Object* target, int hitMode, int trials, int* targetHits)
{
    int dudeHits = 0;
    *targetHits = 0;
    Object* weapon = item_hit_with(shooter, hitMode);
    for (int trial = 0; trial < trials; trial++) {
        item_w_set_curr_ammo(weapon, item_w_max_ammo(weapon));
        Attack attack;
        combat_ctd_init(&attack, shooter, target, hitMode, HIT_LOCATION_UNCALLED);
        if (combat_rt_compute_attack(&attack) == -1) {
            continue;
        }
        bool hit = attack.defender == obj_dude && (attack.attackerFlags & DAM_HIT) != 0;
        for (int index = 0; index < attack.extrasLength; index++) {
            if (attack.extras[index] == obj_dude) {
                hit = true;
            }
        }
        if (hit) {
            dudeHits++;
        }
        if (attack.defender == target && (attack.attackerFlags & DAM_HIT) != 0) {
            (*targetHits)++;
        }
    }
    return dudeHits;
}

static bool step_rules_friendly_foe(unsigned int elapsed, bool first, bool timedOut)
{
    at_clear_hostiles();
    at_move_to_open_spot(3);

    int shooterTile = -1;
    int targetTile = -1;
    for (int attempt = 0; attempt < ROTATION_COUNT * 9 && shooterTile == -1; attempt++) {
        int r = attempt % ROTATION_COUNT;
        int a = at_hex_walk(obj_dude->tile, r, 1 + (attempt / ROTATION_COUNT) % 3, 0, 0);
        int b = at_hex_walk(obj_dude->tile, (r + 3) % ROTATION_COUNT, 2 + attempt / (ROTATION_COUNT * 3), 0, 0);
        if (at_tile_free(a) && at_tile_free(b)
            && !combat_is_shot_blocked(obj_dude, obj_dude->tile, a, NULL, NULL)
            && !combat_is_shot_blocked(obj_dude, obj_dude->tile, b, NULL, NULL)) {
            shooterTile = a;
            targetTile = b;
        }
    }

    Object* shooter = shooterTile != -1 ? at_spawn_rat_at(shooterTile, false) : NULL;
    Object* target = targetTile != -1 ? at_spawn_rat_at(targetTile, false) : NULL;
    Object* weapon = NULL;
    for (int index = 1; index < 600 && weapon == NULL && shooter != NULL; index++) {
        int pid = (OBJ_TYPE_ITEM << 24) | index;
        Proto* proto;
        if (proto_ptr(pid, &proto) == -1 || proto->item.type != ITEM_TYPE_WEAPON
            || (proto->item.extendedFlags & 0xF) != 6 || ((proto->item.extendedFlags & 0xF0) >> 4) != 7
            || proto->item.data.weapon.damageType != DAMAGE_TYPE_NORMAL) {
            continue;
        }
        if (obj_pid_new(&weapon, pid) == -1) {
            weapon = NULL;
            continue;
        }
        Rect rect;
        item_add_force(shooter, weapon, 1);
        obj_disconnect(weapon, &rect);
        weapon->flags |= OBJECT_IN_RIGHT_HAND;
    }
    if (shooter == NULL || target == NULL || weapon == NULL) {
        at_check(C_RULE_FRIENDLY_FOE, false, "fixtures missing (shooter %d, target %d, burst weapon %d)", shooter != NULL, target != NULL, weapon != NULL);
        at_erase(shooter);
        at_erase(target);
        return true;
    }

    // Rats are too weak for the gun's Strength requirement: a temporary
    // bonus (on the rat prototype, restored below).
    int strengthBonus = stat_get_bonus(shooter, STAT_STRENGTH);
    stat_set_bonus(shooter, STAT_STRENGTH, strengthBonus + 9);

    partyMemberAdd(shooter);
    bool inParty = isPartyMember(shooter) && shooter->data.critter.combat.team == obj_dude->data.critter.combat.team;
    at_log("rules: friendly foe shooter hit chance on the target %d%%, on the player %d%%",
        determine_to_hit(shooter, target, HIT_LOCATION_UNCALLED, HIT_MODE_RIGHT_WEAPON_SECONDARY),
        determine_to_hit(shooter, obj_dude, HIT_LOCATION_UNCALLED, HIT_MODE_RIGHT_WEAPON_SECONDARY));
    int trials = 200;
    int targetHits[4];

    perk_rt_debug_set_rank(PERK_FRIENDLY_FOE, 0);
    int without = at_ff_trials(shooter, target, HIT_MODE_RIGHT_WEAPON_SECONDARY, trials, &targetHits[0]);
    perk_rt_debug_set_rank(PERK_FRIENDLY_FOE, 1);
    int blocksBefore = combat_rt_friendly_foe_blocks();
    int with = at_ff_trials(shooter, target, HIT_MODE_RIGHT_WEAPON_SECONDARY, trials, &targetHits[1]);
    int singles = at_ff_trials(shooter, target, HIT_MODE_RIGHT_WEAPON_PRIMARY, trials, &targetHits[2]);
    int blocks = combat_rt_friendly_foe_blocks() - blocksBefore;

    // A party member that turns on the player isn't covered.
    partyMemberRemove(shooter);
    shooter->data.critter.combat.team = at_rat_team;
    int hostile = at_ff_trials(shooter, target, HIT_MODE_RIGHT_WEAPON_SECONDARY, trials, &targetHits[3]);
    perk_rt_debug_set_rank(PERK_FRIENDLY_FOE, 0);
    stat_set_bonus(shooter, STAT_STRENGTH, strengthBonus);

    at_erase(shooter);
    at_erase(target);

    at_check(C_RULE_FRIENDLY_FOE,
        inParty && without > 0 && with == 0 && singles == 0 && targetHits[1] > 0 && hostile > 0,
        "%s bursts through the player (%d each): without perk %d hit the player, with perk %d (target hit %d), single shots %d, "
        "shots passed %d; same shooter hostile: %d",
        object_name(weapon), trials, without, with, targetHits[1], singles, blocks, hostile);
    return true;
}

// Bonus Move: sprint drain in combat.
static float at_sprint_ms;
static float at_sprint_ap;
static float at_sprint_rate[2];
static Object* at_sprint_rat;
static float at_sprint_calm;

static bool step_rules_sprint(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        at_sprint_rat = NULL;
        at_sprint_rate[0] = -1.0f;
        at_sprint_rate[1] = -1.0f;
        at_sprint_calm = -1.0f;
        at_clear_hostiles();
        at_release_all();
        return false;
    }

    at_heal();
    float maxAp = (float)stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS);
    const RtStats* stats = rt_get_stats();
    switch (at_phase) {
    case 0:
        // A hostile rat engages the player.
        at_sprint_rat = at_spawn_rat(5, 8, true);
        at_time_a = now;
        at_phase = 1;
        break;
    case 1:
    case 4:
        // In combat, AP full: sprint for 1.5 s (rank 0, then rank 1).
        if (!rt_player_in_combat() || rt_get_dude_ap() < maxAp - 0.05f || anim_busy(obj_dude)) {
            if (now - at_time_a > 15000) {
                at_check(C_RULE_SPRINT, false, "phase %d: in combat=%d, AP %.2f of %.0f", at_phase, rt_player_in_combat(), rt_get_dude_ap(), maxAp);
                at_release_all();
                at_clear_hostiles();
                return true;
            }
            break;
        }
        perk_rt_debug_set_rank(PERK_BONUS_MOVE, at_phase == 1 ? 0 : 1);
        at_sprint_ms = stats->sprintCombatMs;
        at_sprint_ap = stats->sprintCombatAp;
        if (!rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        at_stick_open_direction();
        at_time_a = now;
        at_phase += 1;
        break;
    case 2:
    case 5:
        at_stick_open_direction();
        if (now - at_time_a < 1500 && rt_get_dude_ap() > 0.5f) {
            break;
        }
        at_stick_release();
        {
            float ms = stats->sprintCombatMs - at_sprint_ms;
            float ap = stats->sprintCombatAp - at_sprint_ap;
            at_sprint_rate[at_phase == 2 ? 0 : 1] = ms > 100.0f ? ap / ms : -1.0f;
            at_log("rules: combat sprint, Bonus Move %d: %.0f ms running, %.3f AP (%.5f AP/ms)", at_phase == 2 ? 0 : 1, ms, ap, ms > 0 ? ap / ms : 0.0f);
        }
        at_time_a = now;
        at_phase += 1;
        break;
    case 3:
        // Back to standing still and full AP.
        at_phase = 4;
        at_time_a = now;
        break;
    case 6:
        // Out of combat, with the perk: normal drain.
        at_clear_hostiles();
        at_time_a = now;
        at_phase = 7;
        break;
    case 7:
        if (rt_player_in_combat() || rt_get_dude_ap() < maxAp - 0.05f || anim_busy(obj_dude)) {
            if (now - at_time_a > 15000) {
                at_phase = 9;
            }
            break;
        }
        perk_rt_debug_set_rank(PERK_BONUS_MOVE, 1);
        at_sprint_ms = stats->sprintCalmMs;
        at_sprint_ap = stats->sprintCalmAp;
        if (!rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        at_stick_open_direction();
        at_time_a = now;
        at_phase = 8;
        break;
    case 8:
        at_stick_open_direction();
        if (now - at_time_a < 1500 && rt_get_dude_ap() > 0.5f) {
            break;
        }
        at_stick_release();
        perk_rt_debug_set_rank(PERK_BONUS_MOVE, 0);
        at_sprint_calm = stats->sprintCalmMs - at_sprint_ms > 100.0f
            ? (stats->sprintCalmAp - at_sprint_ap) / (stats->sprintCalmMs - at_sprint_ms)
            : -1.0f;
        at_log("rules: calm sprint with Bonus Move: %.0f ms, %.5f AP/ms", stats->sprintCalmMs - at_sprint_ms, at_sprint_calm);
        at_phase = 9;
        break;
    case 9: {
        perk_rt_debug_set_rank(PERK_BONUS_MOVE, 1);
        int calmPct = rt_sprint_drain_pct(false);
        int combatPct = rt_sprint_drain_pct(true);
        perk_rt_debug_set_rank(PERK_BONUS_MOVE, 0);
        float calmRate = at_sprint_calm;
        float normal = 1.0f / 400.0f;
        float ratio = at_sprint_rate[0] > 0.0f ? at_sprint_rate[1] / at_sprint_rate[0] : -1.0f;
        bool ok = at_sprint_rate[0] > 0.0f && at_sprint_rate[1] > 0.0f
            && fabsf(at_sprint_rate[0] - normal) < normal * 0.05f
            && fabsf(ratio - 0.5f) < 0.03f
            && calmPct == 100 && combatPct == 50
            && calmRate >= 0.0f && calmRate < 0.000001f;
        at_check(C_RULE_SPRINT, ok,
            "combat drain %.5f AP/ms without perk, %.5f with (ratio %.2f); outside combat with perk %.5f (no drain; normal combat drain %.5f); drain %% with perk: calm %d, combat %d",
            at_sprint_rate[0], at_sprint_rate[1], ratio, calmRate, normal, calmPct, combatPct);
        at_release_all();
        at_clear_hostiles();
        return true;
    }
    }
    return false;
}

static bool step_rules_perks_after_load(unsigned int elapsed, bool first, bool timedOut)
{
    int lastLevel;
    int freePerks;
    editor_rt_debug_get(&lastLevel, &freePerks);
    at_check(C_RULE_PERK_PERSIST,
        at_rule_saved_perks >= 2 && freePerks == at_rule_saved_perks && lastLevel == at_rule_saved_last_level,
        "pending perk selections %d (saved %d), last processed level %d (saved %d)",
        freePerks, at_rule_saved_perks, lastLevel, at_rule_saved_last_level);
    return true;
}

static const AtStep at_steps[] = {
    { "wait for map", step_wait_load, 10000 },
    { "script scan", step_script_scan, 5000 },
    { "dump", step_dump, 30000 },
    { "walk", step_walk, 5000 },
    { "stop", step_stop, 3000 },
    { "run outside combat (free)", step_run, 5000 },
    { "run at 0 AP outside combat", step_run_zero, 6000 },
    { "run with a neutral lock", step_run_neutral, 5000 },
    { "combat starts during a run", step_run_combat_start, 20000 },
    { "walk after exhaustion", step_exhausted, 5000 },
    { "AP regeneration", step_regen, 5000 },
    { "run again after new click", step_run_again, 5000 },
    { "combat ends during a run", step_run_combat_end, 30000 },
    { "Pip-Boy (B)", step_inventory, 10000 },
    { "D-pad screens", step_screens, 30000 },
    { "Y and LB", step_y_lb, 5000 },
    { "pause menu navigation", step_pause_menu, 15000 },
    { "camera edge scrolling (cave)", step_camera_cave, 40000 },
    { "right stick camera panning", step_camera_pan, 20000 },
    { "find a hostile", step_find_hostile, 60000 },
    { "LT lock", step_lock, 3000 },
    { "lock persists", step_lock_persist, 3000 },
    { "LT unlock", step_unlock, 3000 },
    { "RT without lock", step_no_lock_rt, 15000 },
    { "enemy attack slot", step_slot, 30000 },
    { "target switching", step_switch, 30000 },
    { "unarmed attacks", step_unarmed, 60000 },
    { "melee attacks", step_melee, 60000 },
    { "single-fire attacks", step_single, 60000 },
    { "burst and automatic fire", step_burst, 80000 },
    { "throwing", step_throw, 60000 },
    { "no fire transfer", step_no_transfer, 40000 },
    { "empty weapon", step_empty, 15000 },
    { "reload animation", step_reload, 30000 },
    { "melee reach", step_range, 15000 },
    { "blocked line of fire", step_blocked, 8000 },
    { "companion and simultaneous actions", step_companion, 45000 },
    { "aimed shot: entry and anatomy", step_aim_entry, 40000 },
    { "aimed shot: queue and execution", step_aim_queue, 60000 },
    { "aimed shot: hit chances and criticals", step_aim_odds, 20000 },
    { "aimed shot: cancel, dead target, interruptions", step_aim_interrupt, 120000 },
    { "aimed shot: limits, burst, disconnect", step_aim_limits, 90000 },
    { "combat summary", step_combat_summary, 8000 },
    { "interaction", step_interact, 20000 },
    { "interaction cycling (D-pad)", step_int_cycle, 40000 },
    { "loot screen (controller)", step_loot, 90000 },
    { "barter screen (controller)", step_barter, 90000 },
    { "X tap reload, hold holster", step_holster, 60000 },
    { "contextual skills", step_skills, 60000 },
    { "chems and no healing on A", step_chem, 30000 },
    { "controller disconnect", step_disconnect, 12000 },
    { "focus loss", step_focus, 12000 },
    { "Pip-Boy shortcuts", step_pipboy_shortcuts, 40000 },
    { "Pip-Boy tabs and actions", step_pipboy, 120000 },
    { "favorites assignment (Pip-Boy Y)", step_fav_assign, 90000 },
    { "favorites (direct D-pad)", step_fav_use, 60000 },
    { "level-up indicator", step_level_icon, 90000 },
    { "character rules: SPECIAL formulas", step_rules_special, 5000 },
    { "character rules: perk progression", step_rules_perks, 5000 },
    { "character rules: Charisma and companions", step_rules_companions, 5000 },
    { "character rules: Bonus HtH Attacks reach", step_rules_reach, 30000 },
    { "character rules: long-range accuracy", step_rules_long_range, 30000 },
    { "character rules: Friendly Foe", step_rules_friendly_foe, 10000 },
    { "character rules: Bonus Move sprint", step_rules_sprint, 60000 },
    { "save game", step_save, 30000 },
    { "load game", step_load, 40000 },
    { "Pip-Boy progress after load", step_pipboy_persist, 3000 },
    { "favorites after load", step_fav_persist, 3000 },
    { "level-up indicator after load", step_level_after_load, 3000 },
    { "perk selections after load", step_rules_perks_after_load, 3000 },
    { "map transition", step_map_change, 30000 },
    { "Pip-Boy saved automaps and world map", step_pipboy_maps, 30000 },
    { "neutral NPC lock", step_neutral, 40000 },
    { "camera edge scrolling (Vault 13)", step_camera_vault, 40000 },
};

#define AT_STEP_COUNT ((int)(sizeof(at_steps) / sizeof(at_steps[0])))

// `--rt-autotest-only=a|b` runs the steps whose names contain a or b.
static bool at_only_matches(const char* name)
{
    char filter[64];
    SDL_strlcpy(filter, at_only, sizeof(filter));
    for (char* part = strtok(filter, "|"); part != NULL; part = strtok(NULL, "|")) {
        if (SDL_strcasestr(name, part) != NULL) {
            return true;
        }
    }
    return false;
}

void rt_autotest_tick()
{
    if (!at_enabled || at_finished) {
        return;
    }

    unsigned int now = SDL_GetTicks();
    if (at_start_time == 0) {
        at_start_time = now;
        at_step_index = 0;
        at_step_start = now;
        at_step_first = true;
        at_log("gameplay started");
    }

    if (isInCombat()) {
        at_turn_based_seen = true;
    }

    at_watch_lock();

    // Harness: keep the player alive through long fights (healing mid-attack
    // also exercises the death re-decision when a hit lands).
    if (!critter_is_dead(obj_dude) && !at_no_heal) {
        at_heal();
    }

    if (critter_is_dead(obj_dude)) {
        at_log("player died");
        at_finish();
        return;
    }

    if (now - at_start_time > AT_TOTAL_TIMEOUT || at_step_index >= AT_STEP_COUNT) {
        at_finish();
        return;
    }

    // Development filter: skip steps not asked for (the first one loads the map).
    while (at_only[0] != 0 && at_step_index > 0 && at_step_index < AT_STEP_COUNT
        && !at_only_matches(at_steps[at_step_index].name)) {
        at_step_index++;
    }
    if (at_step_index >= AT_STEP_COUNT) {
        at_finish();
        return;
    }

    const AtStep* step = &(at_steps[at_step_index]);
    unsigned int elapsed = now - at_step_start;
    bool timedOut = elapsed > step->timeout;
    bool first = at_step_first;
    if (first) {
        at_log("--- step %d: %s", at_step_index + 1, step->name);
        elapsed = 0;
    }
    at_step_first = false;

    bool done = step->func(elapsed, first, timedOut);
    if (done || timedOut) {
        if (!done) {
            at_log("step '%s' timed out", step->name);
        }
        at_step_index++;
        at_step_start = SDL_GetTicks();
        at_step_first = true;
    }
}

static void at_finish()
{
    at_release_all();

    at_check(C_NO_TURN_BASED, !at_turn_based_seen, "isInCombat() seen=%d", at_turn_based_seen);

    const RtStats* stats = rt_get_stats();
    const RtContextStats* context = rt_context_get_stats();
    at_log("---------------------------------------------------------------");
    at_log("SUMMARY");
    int passed = 0;
    int failed = 0;
    int notRun = 0;
    for (int index = 0; index < C_COUNT; index++) {
        const char* status = at_results[index] == 1 ? "PASS" : (at_results[index] == 0 ? "FAIL" : "NOT RUN");
        if (at_results[index] == 1) {
            passed++;
        } else if (at_results[index] == 0) {
            failed++;
        } else {
            notRun++;
        }
        at_log("  [%-7s] %s -- %s", status, at_check_names[index], at_notes[index]);
    }
    at_log("stats: attacks started=%d resolved=%d hits=%d misses=%d kills=%d",
        stats->attacksStarted, stats->attacksResolved, stats->hits, stats->misses, stats->kills);
    at_log("stats: player unarmed=%d melee=%d single=%d burst=%d throw=%d other=%d rejected=%d; companion=%d; max in flight=%d (%d attackers)",
        stats->playerUnarmed, stats->playerMelee, stats->playerSingle, stats->playerBurst, stats->playerThrow, stats->playerOther,
        stats->playerRejected, stats->companionAttacks, stats->maxConcurrentAttacks, stats->maxConcurrentAttackers);
    at_log("stats: player slot grants=%d max queue=%d max wait=%d ms overlaps=%d; lock switches=%d misses=%d",
        stats->playerSlotGrants, stats->playerSlotMaxQueue, stats->playerSlotMaxWaitMs, stats->playerSlotOverlaps,
        stats->lockSwitches, stats->lockSwitchMisses);
    at_log("stats: reloads started=%d completed=%d interrupted=%d fallback=%d transfers=%d; camera scrolls=%d recenters=%d",
        stats->reloadsStarted, stats->reloadsCompleted, stats->reloadsInterrupted, stats->reloadFallbacks, stats->reloadAmmoTransfers,
        stats->cameraScrolls, stats->cameraRecenters);
    at_log("stats: interactions=%d default=%d skill uses=%d blocked=%d menus=%d cancelled=%d",
        context->interactions, context->defaultActions, context->skillUses, context->skillBlocked, context->menus, context->menuCancels);
    at_log("player: hp=%d/%d dead=%d", critter_get_hits(obj_dude), stat_level(obj_dude, STAT_MAXIMUM_HIT_POINTS), critter_is_dead(obj_dude));
    at_log("RESULT: %d passed, %d failed, %d not run", passed, failed, notRun);

    gamepad_clear_override();
    gamepad_set_hook(NULL);

    at_finished = true;
    game_user_wants_to_quit = 2;
}

} // namespace fallout
