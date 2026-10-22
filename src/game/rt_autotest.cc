#include "game/rt_autotest.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL.h>

#include "game/anim.h"
#include "game/combat.h"
#include "game/combatai.h"
#include "game/critter.h"
#include "game/game.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/proto.h"
#include "game/realtime.h"
#include "game/stat.h"
#include "game/tile.h"
#include "platform_compat.h"
#include "plib/gnw/gamepad.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"

namespace fallout {

#define AT_TOTAL_TIMEOUT 900000

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
    C_LOCK_MARKER,
    C_LOCK_PERSIST,
    C_UNLOCK,
    C_LOCK_DEATH,
    C_LOCK_NOT_FRIENDLY,
    C_UNARMED,
    C_MELEE,
    C_SINGLE,
    C_BURST,
    C_AUTO,
    C_EMPTY,
    C_RELOAD,
    C_RANGE,
    C_BLOCKED,
    C_NO_TARGET,
    C_DAMAGE_ONCE,
    C_ENEMY_AI,
    C_COMPANION,
    C_SIMULTANEOUS,
    C_INTERACT,
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
    "LT locks the nearest living hostile",
    "Red hex marker under the locked target",
    "Lock persists after release and while moving",
    "LT again unlocks (marker hidden)",
    "Lock clears when the target dies",
    "Lock never selects a friendly character",
    "Unarmed: RT press = one attack, hold doesn't repeat",
    "Melee: RT press = one attack, hold doesn't repeat",
    "Single fire: RT press = one shot (1 round), hold doesn't repeat",
    "Burst: RT press = one full burst",
    "Automatic: holding RT repeats bursts at a limited rate, stops on release",
    "Empty weapon: RT fails safely",
    "X reloads the active weapon",
    "Melee reach not bypassed; no auto-walk",
    "Blocked line of fire is not bypassed",
    "No lock and no hostile ahead: RT fails safely",
    "Damage applied exactly once per hit",
    "Enemies attack on their own",
    "Companion attacks hostiles on its own",
    "Player, enemy and companion attacks overlap in time",
    "A interacts (loot) and the UI closes with B",
    "Controller disconnect stops input; no phantom actions on reconnect",
    "Focus loss pauses and clears held input",
    "Save game with the controller (pause menu)",
    "Load game with the controller; clean state after load",
    "Map transition leaves no stuck combat/input state",
    "Turn-based combat never started",
};

static int at_results[C_COUNT];
static char at_notes[C_COUNT][200];

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

static bool at_enabled = false;
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
static Object* at_lock_watch = NULL;
static int at_mouse_x[4];
static int at_mouse_y[4];
static unsigned int at_pause_sim_start;
static unsigned int at_pause_real_start;

// -----------------------------------------------------------------------------
// Framework
// -----------------------------------------------------------------------------

void rt_autotest_parse_args(int argc, char** argv)
{
    for (int index = 1; index < argc; index++) {
        if (SDL_strcasecmp(argv[index], "--rt-autotest") == 0) {
            at_enabled = true;
        }
    }

    if (!at_enabled) {
        return;
    }

    for (int index = 0; index < C_COUNT; index++) {
        at_results[index] = -1;
        at_notes[index][0] = '\0';
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

    char buffer[600];
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

    if (anim_busy(obj_dude)) {
        return false;
    }

    if (inven_wield(obj_dude, item, intface_is_item_right_hand()) == -1) {
        return false;
    }

    intface_update_items(false);
    at_log("harness: equipped %s", object_name(item));
    return true;
}

static int at_current_anim()
{
    int hitMode;
    bool aiming;
    if (intface_get_attack(&hitMode, &aiming) != 0) {
        return -1;
    }
    return item_w_anim(obj_dude, hitMode);
}

// Test fixture: the opening cave has no burst weapon, so one is created from
// the game's own prototypes (the first weapon with single + burst modes).
static Object* at_create_burst_weapon()
{
    for (int index = 1; index < 600; index++) {
        int pid = (OBJ_TYPE_ITEM << 24) | index;
        Proto* proto;
        if (proto_ptr(pid, &proto) == -1 || proto->item.type != ITEM_TYPE_WEAPON) {
            continue;
        }

        int primary = proto->item.extendedFlags & 0xF;
        int secondary = (proto->item.extendedFlags & 0xF0) >> 4;
        // 6 = single fire, 7 = burst (see `attack_anim` in item.cc).
        if (primary != 6 || secondary != 7) {
            continue;
        }

        Object* weapon;
        if (obj_pid_new(&weapon, pid) == -1) {
            continue;
        }

        Rect rect;
        if (item_add_force(obj_dude, weapon, 1) != 0) {
            continue;
        }
        obj_disconnect(weapon, &rect);
        item_w_set_curr_ammo(weapon, item_w_max_ammo(weapon));

        int ammoPid = proto->item.data.weapon.ammoTypePid;
        Object* ammo;
        if (ammoPid > 0 && obj_pid_new(&ammo, ammoPid) != -1) {
            if (item_add_force(obj_dude, ammo, 3) == 0) {
                obj_disconnect(ammo, &rect);
            }
        }

        at_log("harness: created test weapon %s (pid 0x%08X, %d rounds) with ammo",
            object_name(weapon), pid, item_w_max_ammo(weapon));
        return weapon;
    }

    return NULL;
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
        at_pad.leftX = 0.0f;
        at_pad.leftY = 0.0f;
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
        at_pad.leftX = 0.0f;
        at_pad.leftY = 0.0f;
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

    if (elapsed >= 1200) {
        at_check(C_RUN_RECLICK, at_flag_a, "a new L3 click runs again=%d", at_flag_a);
        if (rt_is_run_toggled()) {
            at_press(GAMEPAD_BUTTON_LEFT_STICK);
        }
        at_pad.leftX = 0.0f;
        at_pad.leftY = 0.0f;
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
        at_check(C_Y_LB, !yOpenedMenu && lb == 1, "Y opened a menu=%d, LB targeting requests=%d", yOpenedMenu, lb);
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
        at_count_a = at_min_hostile_distance();
        at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        return false;
    }

    if (elapsed >= 400) {
        Object* lock = rt_get_lock();
        Object* marker = rt_get_lock_marker();
        bool nearest = lock != NULL && obj_dist(obj_dude, lock) <= at_count_a + 1;
        at_check(C_LOCK, lock != NULL && rt_is_hostile(lock) && nearest,
            "locked=%s hostile=%d distance=%d nearest hostile=%d",
            lock != NULL ? critter_name(lock) : "(none)",
            lock != NULL && rt_is_hostile(lock),
            lock != NULL ? obj_dist(obj_dude, lock) : -1,
            at_count_a);
        at_check(C_LOCK_MARKER,
            lock != NULL && marker != NULL && (marker->flags & OBJECT_HIDDEN) == 0 && marker->tile == lock->tile,
            "marker visible=%d on target tile=%d",
            marker != NULL && (marker->flags & OBJECT_HIDDEN) == 0,
            marker != NULL && lock != NULL && marker->tile == lock->tile);
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

    at_stick_open_direction();
    if (elapsed >= 1200) {
        at_pad.leftX = 0.0f;
        at_pad.leftY = 0.0f;
        Object* lock = rt_get_lock();
        Object* marker = rt_get_lock_marker();
        if (at_obj_a != NULL && critter_is_dead(at_obj_a)) {
            at_log("lock target died during the persistence test; no verdict");
            return true;
        }
        at_check(C_LOCK_PERSIST, lock != NULL && lock == at_obj_a && marker != NULL && marker->tile == lock->tile,
            "lock same=%d marker follows=%d", lock == at_obj_a, marker != NULL && lock != NULL && marker->tile == lock->tile);
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
        Object* marker = rt_get_lock_marker();
        at_check(C_UNLOCK, rt_get_lock() == NULL && (marker == NULL || (marker->flags & OBJECT_HIDDEN) != 0),
            "lock=%s marker hidden=%d", rt_get_lock() != NULL ? critter_name(rt_get_lock()) : "(none)",
            marker == NULL || (marker->flags & OBJECT_HIDDEN) != 0);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Steps: attacks
// -----------------------------------------------------------------------------

typedef enum AtWeaponKind {
    AT_UNARMED,
    AT_MELEE,
    AT_SINGLE,
    AT_BURST,
} AtWeaponKind;

static AtWeaponKind at_kind;
static int at_phase;
static int at_retries;
static Object* at_target;
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
    default: {
        if (at_burst_weapon == NULL) {
            at_burst_weapon = at_create_burst_weapon();
        }
        at_wield(at_burst_weapon);
        if (at_burst_weapon != NULL && item_w_curr_ammo(at_burst_weapon) == 0) {
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
    return at_kind == AT_UNARMED || at_kind == AT_MELEE ? 1 : 5;
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
            Object* rat = at_nearest(at_filter_enemy_team, 60);
            if (rat != NULL) {
                at_approach(rat, 4);
            }
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

// One weapon kind: equip, approach a hostile, single press, then hold.
static bool step_attack_kind(unsigned int elapsed, bool first, bool timedOut)
{
    static const AutotestCheck checks[] = { C_UNARMED, C_MELEE, C_SINGLE, C_BURST };
    static const char* shots[] = { "10_unarmed", "11_melee", "12_single", "13_burst" };
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
        if (!at_prepare_target()) {
            break;
        }
        at_counter_before = at_kind_counter();
        if (at_kind == AT_BURST) {
            // Fixture: a 6 HP rat dies to the first burst (which correctly
            // interrupts automatic fire), so toughen the target.
            stat_set_bonus(at_target, STAT_MAXIMUM_HIT_POINTS, 200);
            critter_adjust_hits(at_target, 200);
            at_log("harness: %s toughened to %d HP for the automatic-fire test", critter_name(at_target), critter_get_hits(at_target));
        }
        at_pad.buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = true;
        at_phase = 4;
        at_phase_start = now;
        break;
    case 4:
        // Hold RT for 3 seconds.
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
    }
    return false;
}

static bool step_unarmed(unsigned int elapsed, bool first, bool timedOut)
{
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

static bool step_empty_reload(unsigned int elapsed, bool first, bool timedOut)
{
    Object* weapon = at_burst_weapon != NULL ? at_burst_weapon : at_find_inventory("10mm Pistol");
    if (first) {
        at_phase = 0;
        if (weapon == NULL) {
            at_check(C_EMPTY, false, "no ranged weapon");
            return true;
        }
        return false;
    }

    unsigned int now = SDL_GetTicks();
    switch (at_phase) {
    case 0:
        if (!at_wield(weapon) || elapsed < 500) {
            break;
        }
        at_heal();
        at_target = at_nearest(at_filter_hostile, 25);
        if (at_target == NULL) {
            at_target = at_nearest(at_filter_enemy_team, 25);
        }
        if (at_target != NULL) {
            rt_debug_set_lock(at_target);
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
            rt_get_stats()->playerAttacks == at_count_a && rt_get_stats()->lastRejectReason == COMBAT_BAD_SHOT_NO_AMMO,
            "attacks started=%d, reject reason=%d (no ammo=%d)",
            rt_get_stats()->playerAttacks - at_count_a,
            rt_get_stats()->lastRejectReason,
            COMBAT_BAD_SHOT_NO_AMMO);
        at_press(GAMEPAD_BUTTON_X);
        at_phase = 2;
        at_time_a = now;
        break;
    case 2:
        if (now - at_time_a < 800) {
            break;
        }
        at_check(C_RELOAD, item_w_curr_ammo(weapon) > 0, "%s ammo after X: %d/%d",
            object_name(weapon), item_w_curr_ammo(weapon), item_w_max_ammo(weapon));
        return true;
    }
    return false;
}

static bool at_filter_far_enemy(Object* critter)
{
    int distance = obj_dist(obj_dude, critter);
    return at_filter_enemy_team(critter) && distance >= 3 && distance <= 15;
}

static bool step_range(unsigned int elapsed, bool first, bool timedOut)
{
    unsigned int now = SDL_GetTicks();
    if (first) {
        at_phase = 0;
        return false;
    }

    switch (at_phase) {
    case 0:
        if (!at_wield(at_find_inventory("Knife")) || elapsed < 600) {
            break;
        }
        at_target = at_nearest(at_filter_far_enemy, 15);
        if (at_target == NULL) {
            if (timedOut) {
                at_log("range test: no critter 3-15 tiles away");
                return true;
            }
            break;
        }
        rt_debug_set_lock(at_target);
        at_tile_a = obj_dude->tile;
        at_count_a = rt_get_stats()->playerAttacks;
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
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

    switch (at_phase) {
    case 0:
        if (!at_wield(pistol) || elapsed < 600) {
            break;
        }
        if (item_w_curr_ammo(pistol) == 0) {
            item_w_set_curr_ammo(pistol, item_w_max_ammo(pistol));
        }
        {
            int hitMode;
            bool aiming;
            at_blocked_range = intface_get_attack(&hitMode, &aiming) == 0 ? item_w_range(obj_dude, hitMode) : 20;
        }
        at_target = at_nearest(at_filter_blocked, at_blocked_range);
        if (at_target == NULL) {
            at_log("blocked test: no critter with a blocked line of fire within weapon range (%d tiles)", at_blocked_range);
            return true;
        }
        rt_debug_set_lock(at_target);
        at_count_a = rt_get_stats()->playerAttacks;
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 1;
        at_time_a = now;
        break;
    case 1:
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

static bool at_filter_companion_candidate(Object* critter)
{
    return at_filter_enemy_team(critter) && !rt_is_hostile(critter);
}

static bool step_companion(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        // Test fixture: a rat joins the player's team to act as a companion.
        at_companion = at_nearest(at_filter_companion_candidate, 30);
        if (at_companion == NULL) {
            at_companion = at_nearest(at_filter_enemy_team, 30);
        }
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
        return false;
    }

    at_heal();

    if (!at_flag_a) {
        // Go unarmed so the player keeps fighting at close quarters.
        at_flag_a = at_unarmed() || elapsed > 3000;
    }

    // Bring the player (and with it the fight) close to the companion.
    if (!critter_is_dead(at_companion) && obj_dist(obj_dude, at_companion) > 3) {
        at_approach(at_companion, 2);
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

static bool step_no_target(unsigned int elapsed, bool first, bool timedOut)
{
    // Face away from every hostile; with no lock RT must fail safely.
    if (first) {
        rt_debug_set_lock(NULL);
        at_phase = 0;
        return false;
    }

    unsigned int now = SDL_GetTicks();
    switch (at_phase) {
    case 0: {
        at_heal();
        Object* hostile = at_nearest(at_filter_hostile, 25);
        if (hostile != NULL && obj_dist(obj_dude, hostile) <= 1) {
            // Someone is adjacent (always counts as ahead); wait.
            if (timedOut) {
                at_log("no-target test: hostiles stayed adjacent");
                return true;
            }
            break;
        }

        if (anim_busy(obj_dude)) {
            break;
        }

        // Face directly away from the nearest hostile.
        int rotation = hostile != NULL ? (tile_dir(obj_dude->tile, hostile->tile) + 3) % ROTATION_COUNT : obj_dude->rotation;
        Rect rect;
        if (obj_set_rotation(obj_dude, rotation, &rect) == 0) {
            tile_refresh_rect(&rect, obj_dude->elevation);
        }
        at_count_a = rt_get_stats()->playerAttacks;
        at_count_b = rt_get_stats()->playerRejected;
        at_press(GAMEPAD_BUTTON_RIGHT_TRIGGER);
        at_phase = 1;
        at_time_a = now;
        break;
    }
    case 1:
        if (now - at_time_a < 800) {
            break;
        }
        at_check(C_NO_TARGET,
            rt_get_stats()->playerAttacks == at_count_a && rt_get_stats()->playerRejected > at_count_b,
            "attacks=%d rejected=%d", rt_get_stats()->playerAttacks - at_count_a, rt_get_stats()->playerRejected - at_count_b);
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
    at_check(C_DAMAGE_ONCE, stats->damageMismatches == 0 && stats->attacksResolved == stats->attacksStarted,
        "attacks started=%d resolved=%d damage mismatches=%d",
        stats->attacksStarted, stats->attacksResolved, stats->damageMismatches);
    at_check(C_ENEMY_AI, stats->attacksStarted - stats->playerAttacks - stats->companionAttacks > 0,
        "enemy attacks=%d, damage to player=%d",
        stats->attacksStarted - stats->playerAttacks - stats->companionAttacks, stats->damageToPlayer);
    return true;
}

static bool step_interact(unsigned int elapsed, bool first, bool timedOut)
{
    if (first) {
        at_phase = 0;
        at_obj_a = NULL;

        // The nearest corpse within 8 tiles.
        Object** list;
        int count = obj_create_list(-1, obj_dude->elevation, OBJ_TYPE_CRITTER, &list);
        int best = 9;
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
        rt_debug_set_lock(NULL);
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
        at_expect_menu("15_loot", NULL);
        at_press(GAMEPAD_BUTTON_A);
        at_phase = 1;
        at_time_a = SDL_GetTicks();
        break;
    case 1:
        if ((at_menu_seen && SDL_GetTicks() - at_time_a > 300) || timedOut) {
            at_check(C_INTERACT, at_menu_seen && at_menu_done, "loot UI opened=%d closed with B=%d", at_menu_seen, at_menu_done);
            at_menu_expected = false;
            return true;
        }
        break;
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
            at_pad.leftX = 0.0f;
            at_pad.leftY = 0.0f;
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
            at_pad.leftX = 0.0f;
            at_pad.leftY = 0.0f;
            bool restored = at_tile_a == at_saved_tile;
            bool movesAgain = obj_dude->tile != at_tile_b;
            at_check(C_LOAD, restored && at_flag_a && movesAgain && rt_get_lock() == NULL,
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
        at_press(GAMEPAD_BUTTON_LEFT_TRIGGER);
        at_phase = 3;
        at_time_a = SDL_GetTicks();
        return false;
    }

    if (at_phase == 3 && since > 500) {
        at_flag_b = rt_get_lock() == NULL;
        at_tile_b = obj_dude->tile;
        at_phase = 4;
        at_time_a = SDL_GetTicks();
        return false;
    }

    if (at_phase == 4) {
        at_stick_open_direction();
        if (since > 1500) {
            at_pad.leftX = 0.0f;
            at_pad.leftY = 0.0f;
            bool moved = obj_dude->tile != at_tile_b;
            at_check(C_MAP_CHANGE, at_flag_a && at_flag_b && moved,
                "map=%s, clean state (no lock/pending/phantom fire)=%d, LT with no hostiles locks nothing=%d, can move=%d",
                map_data.name, at_flag_a, at_flag_b, moved);
            at_screenshot("20_vault13");
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Script
// -----------------------------------------------------------------------------

static const AtStep at_steps[] = {
    { "wait for map", step_wait_load, 10000 },
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
    { "find a hostile", step_find_hostile, 60000 },
    { "LT lock", step_lock, 3000 },
    { "lock persists", step_lock_persist, 3000 },
    { "LT unlock", step_unlock, 3000 },
    { "unarmed attacks", step_unarmed, 60000 },
    { "melee attacks", step_melee, 60000 },
    { "single-fire attacks", step_single, 60000 },
    { "burst and automatic fire", step_burst, 60000 },
    { "empty weapon and reload", step_empty_reload, 10000 },
    { "melee reach", step_range, 15000 },
    { "blocked line of fire", step_blocked, 8000 },
    { "companion and simultaneous actions", step_companion, 45000 },
    { "no target ahead", step_no_target, 15000 },
    { "combat summary", step_combat_summary, 8000 },
    { "interaction", step_interact, 20000 },
    { "controller disconnect", step_disconnect, 12000 },
    { "focus loss", step_focus, 12000 },
    { "save game", step_save, 30000 },
    { "load game", step_load, 40000 },
    { "map transition", step_map_change, 30000 },
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

    if (critter_is_dead(obj_dude)) {
        at_log("player died");
        at_finish();
        return;
    }

    if (now - at_start_time > AT_TOTAL_TIMEOUT || at_step_index >= AT_STEP_COUNT) {
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
    at_log("stats: player unarmed=%d melee=%d single=%d burst=%d other=%d rejected=%d; companion=%d; max in flight=%d (%d attackers)",
        stats->playerUnarmed, stats->playerMelee, stats->playerSingle, stats->playerBurst, stats->playerOther,
        stats->playerRejected, stats->companionAttacks, stats->maxConcurrentAttacks, stats->maxConcurrentAttackers);
    at_log("player: hp=%d/%d dead=%d", critter_get_hits(obj_dude), stat_level(obj_dude, STAT_MAXIMUM_HIT_POINTS), critter_is_dead(obj_dude));
    at_log("RESULT: %d passed, %d failed, %d not run", passed, failed, notRun);

    gamepad_clear_override();
    gamepad_set_hook(NULL);

    at_finished = true;
    game_user_wants_to_quit = 2;
}

} // namespace fallout
