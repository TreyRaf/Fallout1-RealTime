#include "game/rt_autotest.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
#include "game/rt_context.h"
#include "game/scripts.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/tile.h"
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
    C_COUNT,
} AutotestCheck;

static const char* at_check_names[C_COUNT] = {
    "Opening cave map loaded",
    "Left stick walks with the walk animation",
    "Releasing the stick stops movement",
    "Straight walking issues few movement requests (no queue/restarts)",
    "L3 toggles running with the run animation; AP drains",
    "AP regenerates while not running",
    "AP exhaustion returns to walking and clears the toggle",
    "Running again requires a new L3 click",
    "B opens inventory, stays open, B closes it",
    "D-pad opens Pip-Boy, map, character, skilldex",
    "Y does nothing; LB reports the pending targeting system",
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

static bool at_enabled = false;
// Optional: only run steps whose name contains this text (development aid).
static char at_only[64] = "";
static bool at_finished = false;
static FILE* at_stream = NULL;
static bool at_screendump_registered = false;
static char at_shot_path[COMPAT_MAX_PATH];
static unsigned int at_start_time = 0;
static unsigned int at_last_select_key = 0;
static bool at_turn_based_seen = false;

static GamepadState at_pad;
static int at_press_frames[GAMEPAD_BUTTON_COUNT];

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
    at_press_frames[button] = 3;
}

static void at_release_all()
{
    bool connected = at_pad.connected;
    memset(&at_pad, 0, sizeof(at_pad));
    at_pad.connected = connected;
    memset(at_press_frames, 0, sizeof(at_press_frames));
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
        if (at_press_frames[button] > 0) {
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
            if (next == -1 || obj_blocking_at(obj_dude, next, obj_dude->elevation) != NULL) {
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
    if (rotation == -1 || next == -1 || obj_blocking_at(obj_dude, next, obj_dude->elevation) != NULL) {
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
    if (elapsed >= 800) {
        at_check(C_STOP, !anim_busy(obj_dude), "animation busy after release=%d", anim_busy(obj_dude) != 0);
        return true;
    }
    return false;
}

static bool step_run(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_ap_a = rt_get_dude_ap();
        at_ap_b = at_ap_a;
        at_flag_a = false;
        at_flag_b = false;
        at_press(GAMEPAD_BUTTON_LEFT_STICK);
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

    if (elapsed > 1000 && !at_flag_b) {
        at_flag_b = true;
        at_screenshot("02_run");
    }

    if (rt_get_dude_ap() <= 0.0f || timedOut) {
        at_check(C_RUN, at_flag_a && at_ap_b < at_ap_a, "run animation=%d, AP %.2f -> %.2f", at_flag_a, at_ap_a, at_ap_b);
        at_flag_b = false;
        return true;
    }
    return false;
}

static bool step_exhausted(unsigned int elapsed, bool first, bool timedOut)
{
    // Keep pushing the stick: the player must walk, and keep walking even
    // after AP is back above 1 (a new click is required to run).
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
        at_expect_menu("03_inventory", at_menu_inventory_script);
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
    static const GamepadButton buttons[] = {
        GAMEPAD_BUTTON_DPAD_UP,
        GAMEPAD_BUTTON_DPAD_DOWN,
        GAMEPAD_BUTTON_DPAD_LEFT,
        GAMEPAD_BUTTON_DPAD_RIGHT,
    };
    static const char* names[] = {
        "04_pipboy",
        "05_automap",
        "06_character",
        "07_skilldex",
    };

    unsigned int now = SDL_GetTicks();
    if (first) {
        at_count_a = 0;
        at_count_b = 0;
        at_time_a = 0;
        at_tile_a = (int)now;
        return false;
    }

    if (at_count_a >= 4) {
        at_check(C_SCREENS, at_count_b == 4, "%d of 4 screens opened and closed with B", at_count_b);
        at_menu_expected = false;
        return true;
    }

    if (at_time_a == 0) {
        if (now - (unsigned int)at_tile_a < 700) {
            return false;
        }
        at_expect_menu(names[at_count_a], NULL);
        at_press(buttons[at_count_a]);
        at_time_a = now;
        return false;
    }

    if ((at_menu_seen && now - at_time_a > 300) || now - at_time_a > 5000) {
        at_log("screen %s: opened=%d closed=%d", names[at_count_a], at_menu_seen, at_menu_done);
        if (at_menu_seen && at_menu_done) {
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
        at_flag_a = false;
        at_expect_menu(NULL, NULL);
        at_press(GAMEPAD_BUTTON_Y);
        return false;
    }

    if (elapsed > 800 && !at_flag_a) {
        at_flag_a = true;
        at_press(GAMEPAD_BUTTON_LEFT_SHOULDER);
    }

    if (elapsed >= 1800) {
        bool yOpenedMenu = at_menu_seen;
        at_menu_expected = false;
        int lb = rt_get_stats()->targetingRequests - at_count_a;
        at_check(C_Y_LB, !yOpenedMenu && lb == 1 && rt_get_lock() == NULL,
            "Y opened a menu=%d, LB targeting requests=%d, LB locked something=%d", yOpenedMenu, lb, rt_get_lock() != NULL);
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

    if (obj_dist(obj_dude, at_target) > at_kind_reach()) {
        at_approach(at_target, at_kind_reach());
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
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 1;
        at_time_a = now;
        break;
    case 1: {
        if (now - at_time_a < 800) {
            break;
        }
        bool nothing = context->menus == at_count_a && context->skillUses == at_count_b && !anim_busy(obj_dude);
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

static const AtStep at_steps[] = {
    { "wait for map", step_wait_load, 10000 },
    { "script scan", step_script_scan, 5000 },
    { "walk", step_walk, 5000 },
    { "stop", step_stop, 3000 },
    { "run until exhausted", step_run, 15000 },
    { "walk after exhaustion", step_exhausted, 5000 },
    { "AP regeneration", step_regen, 5000 },
    { "run again after new click", step_run_again, 5000 },
    { "inventory (B)", step_inventory, 10000 },
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
    { "combat summary", step_combat_summary, 8000 },
    { "interaction", step_interact, 20000 },
    { "contextual skills", step_skills, 60000 },
    { "chems and no healing on A", step_chem, 30000 },
    { "controller disconnect", step_disconnect, 12000 },
    { "focus loss", step_focus, 12000 },
    { "save game", step_save, 30000 },
    { "load game", step_load, 40000 },
    { "map transition", step_map_change, 30000 },
    { "neutral NPC lock", step_neutral, 40000 },
    { "camera edge scrolling (Vault 13)", step_camera_vault, 40000 },
};

#define AT_STEP_COUNT ((int)(sizeof(at_steps) / sizeof(at_steps[0])))

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
        && SDL_strcasestr(at_steps[at_step_index].name, at_only) == NULL) {
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
