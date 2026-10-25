#include "game/rt_aimed.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "game/anim.h"
#include "game/art.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/display.h"
#include "game/game.h"
#include "game/gsound.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/proto.h"
#include "game/realtime.h"
#include "game/rt_view.h"
#include "game/stat.h"
#include "game/tile.h"
#include "game/trait.h"
#include "plib/color/color.h"
#include "plib/gnw/gamepad.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

// The original called shot window (combat.cc) and the panel added below it.
#define AIM_ART_WIDTH 424
#define AIM_ART_HEIGHT 309
#define AIM_PANEL_HEIGHT 134
#define AIM_WIDTH AIM_ART_WIDTH
#define AIM_HEIGHT (AIM_ART_HEIGHT + AIM_PANEL_HEIGHT)

// A queued attack that can't start for this long is skipped.
#define AIM_ENTRY_TIMEOUT_MS 6000
// How long the next queued attack holds back enemy attacks on the player.
#define AIM_ENTRY_PRIORITY_MS 1200

static RtAimedState aim_state;
static int aim_win = -1;
static unsigned char* aim_buf = NULL;
static bool aim_done = false;
static bool aim_fire = false;
static Object* aim_outlined = NULL;
static unsigned int aim_message_until = 0;
static bool aim_message_error = false;
static unsigned int aim_entry_since = 0;

static void aim_say(bool error, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(aim_state.message, sizeof(aim_state.message), format, args);
    va_end(args);
    aim_message_until = SDL_GetTicks() + 4000;
    aim_message_error = error;
    rt_log("aimed: %s", aim_state.message);
}

// Feedback in gameplay: the message window, as other real-time actions do.
static void aim_print(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    display_print(buffer);
    SDL_strlcpy(aim_state.message, buffer, sizeof(aim_state.message));
    rt_log("ui: %s", buffer);
}

const RtAimedState* rt_aimed_get_state()
{
    return &aim_state;
}

bool rt_aimed_is_executing()
{
    return aim_state.executing;
}

const char* rt_aimed_part_name(Object* critter, int hitLocation)
{
    char* name = combat_rt_loc_name(critter, hitLocation);
    return name != NULL && name[0] != '\0' ? name : NULL;
}

static const char* aim_weapon_name(int weaponPid)
{
    if (weaponPid == -1) {
        return "Unarmed";
    }
    char* name = proto_name(weaponPid);
    return name != NULL ? name : "Weapon";
}

static const char* aim_target_name(Object* target)
{
    return target != NULL ? critter_name(target) : "(none)";
}

// Why the current weapon or attack mode can't aim (`item_w_called_shot`
// gives no reason), or NULL if it can.
static const char* aim_unsupported_reason(int hitMode, char* buffer, size_t size)
{
    if (item_w_called_shot(obj_dude, hitMode)) {
        return NULL;
    }

    if (trait_level(TRAIT_FAST_SHOT)) {
        return "Your Fast Shot trait doesn't allow aimed shots.";
    }

    int anim = item_w_anim(obj_dude, hitMode);
    if (anim == ANIM_FIRE_BURST || anim == ANIM_FIRE_CONTINUOUS) {
        return "Burst fire can't be aimed. Press Back to change the attack mode.";
    }

    Object* weapon = item_hit_with(obj_dude, hitMode);
    snprintf(buffer, size, "The %s can't make aimed attacks.", weapon != NULL ? object_name(weapon) : "weapon");
    return buffer;
}

static const char* aim_bad_shot_text(int badShot)
{
    switch (badShot) {
    case COMBAT_BAD_SHOT_OUT_OF_RANGE:
        return "Out of range";
    case COMBAT_BAD_SHOT_AIM_BLOCKED:
        return "Your aim is blocked";
    case COMBAT_BAD_SHOT_NO_AMMO:
        return "Out of ammo";
    case COMBAT_BAD_SHOT_ALREADY_DEAD:
        return "Target is dead";
    case COMBAT_BAD_SHOT_ARM_CRIPPLED:
    case COMBAT_BAD_SHOT_BOTH_ARMS_CRIPPLED:
        return "Your arms are crippled";
    }
    return "Can't attack";
}

// Ammunition a single aimed attack takes: one round, or for a thrown weapon
// the item itself. Aimed attacks are never bursts.
static int aim_rounds_per_attack(Object* weapon, int hitMode)
{
    if (weapon == NULL) {
        return 0;
    }
    if (item_w_subtype(weapon, hitMode) == ATTACK_TYPE_THROW) {
        return 1;
    }
    return item_w_max_ammo(weapon) > 0 ? 1 : 0;
}

static int aim_rounds_available(Object* weapon, int hitMode)
{
    if (item_w_subtype(weapon, hitMode) == ATTACK_TYPE_THROW) {
        return inven_rt_quantity(weapon);
    }
    return item_w_curr_ammo(weapon);
}

static void aim_recount()
{
    aim_state.reservedAp = 0;
    aim_state.reservedRounds = 0;
    for (int index = aim_state.next; index < aim_state.queueLength; index++) {
        aim_state.reservedAp += aim_state.queue[index].apCost;
        aim_state.reservedRounds += aim_state.queue[index].rounds;
    }
    aim_state.availableAp = (int)rt_get_dude_ap() - aim_state.reservedAp;
}

// Body parts and hit chances for the current target.
static void aim_refresh_target()
{
    Object* target = aim_state.target;
    aim_state.badShot = rt_aim_check_shot(target, aim_state.hitMode);
    for (int index = 0; index < RT_AIMED_PARTS; index++) {
        int location = combat_rt_called_shot_location(index);
        aim_state.locations[index] = location;
        aim_state.available[index] = rt_aimed_part_name(target, location) != NULL;
        aim_state.toHit[index] = aim_state.badShot == COMBAT_BAD_SHOT_OK && aim_state.available[index]
            ? determine_to_hit(obj_dude, target, location, aim_state.hitMode)
            : -1;
    }

    // Keep the selection on a body part this creature has.
    if (!aim_state.available[aim_state.selected]) {
        for (int index = 0; index < RT_AIMED_PARTS; index++) {
            if (aim_state.available[index]) {
                aim_state.selected = index;
                break;
            }
        }
    }
    aim_recount();
}

static void aim_set_outline(Object* target)
{
    Rect rect;
    if (aim_outlined != NULL) {
        if ((aim_outlined->outline & OUTLINE_TYPE_MASK) != 0) {
            obj_remove_outline(aim_outlined, &rect);
            tile_refresh_rect(&rect, aim_outlined->elevation);
        }
        aim_outlined = NULL;
    }
    if (target != NULL && (target->outline & OUTLINE_TYPE_MASK) == 0 && obj_outline_object(target, OUTLINE_TYPE_HOSTILE, &rect) == 0) {
        tile_refresh_rect(&rect, target->elevation);
        aim_outlined = target;
    }
}

// -----------------------------------------------------------------------------
// Window
// -----------------------------------------------------------------------------

static void aim_text(int x, int y, const char* text, int color, int width = AIM_WIDTH)
{
    if (width > AIM_WIDTH - x - 2) {
        width = AIM_WIDTH - x - 2;
    }
    if (width > 0) {
        text_to_buf(aim_buf + AIM_WIDTH * y + x, text, width, AIM_WIDTH, color);
    }
}

static void aim_render()
{
    int green = colorTable[992];
    int red = colorTable[31744];
    int bright = colorTable[32747];
    int dim = colorTable[8804];
    Object* target = aim_state.target;

    CacheEntry* handle;
    unsigned char* data = art_ptr_lock_data(art_id(OBJ_TYPE_INTERFACE, 118, 0, 0, 0), 0, 0, &handle);
    if (data != NULL) {
        buf_to_buf(data, AIM_ART_WIDTH, AIM_ART_HEIGHT, AIM_ART_WIDTH, aim_buf, AIM_WIDTH);
        art_ptr_unlock(handle);
    } else {
        buf_fill(aim_buf, AIM_ART_WIDTH, AIM_ART_HEIGHT, AIM_WIDTH, colorTable[0]);
    }

    // The creature's own body part picture.
    data = art_ptr_lock_data(art_id(OBJ_TYPE_CRITTER, target->fid & 0xFFF, ANIM_CALLED_SHOT_PIC, 0, 0), 0, 0, &handle);
    if (data != NULL) {
        buf_to_buf(data, 170, 225, 170, aim_buf + AIM_WIDTH * 31 + 128, AIM_WIDTH);
        art_ptr_unlock(handle);
    }

    // The original's cancel button (B and LB cancel here).
    data = art_ptr_lock_data(art_id(OBJ_TYPE_INTERFACE, 8, 0, 0, 0), 0, 0, &handle);
    if (data != NULL) {
        trans_buf_to_buf(data, 15, 16, 15, aim_buf + AIM_WIDTH * 268 + 170, AIM_WIDTH);
        art_ptr_unlock(handle);
    }

    for (int index = 0; index < RT_AIMED_PARTS; index++) {
        int y = combat_rt_called_shot_row_y(index % 4);
        bool left = index < 4;
        combat_rt_print_tohit(aim_buf + AIM_WIDTH * y + (left ? 33 : 373), AIM_WIDTH, aim_state.available[index] ? aim_state.toHit[index] : -1);

        const char* name = rt_aimed_part_name(target, aim_state.locations[index]);
        if (name == NULL) {
            continue;
        }

        int color = index == aim_state.selected ? red : green;
        int nameWidth = text_width(name);
        int x = left ? 74 : 351 - nameWidth;
        aim_text(x, y, name, color);
        if (index == aim_state.selected) {
            draw_line(aim_buf, AIM_WIDTH, x, y + text_height(), x + nameWidth, y + text_height(), red);
        }

        // How many attacks on this body part are queued.
        int count = 0;
        for (int entry = aim_state.next; entry < aim_state.queueLength; entry++) {
            if (aim_state.queue[entry].target == target && aim_state.queue[entry].hitLocation == aim_state.locations[index]) {
                count++;
            }
        }
        if (count > 0) {
            char mark[8];
            snprintf(mark, sizeof(mark), "x%d", count);
            aim_text(left ? x + nameWidth + 4 : x - text_width(mark) - 4, y, mark, bright);
        }
    }

    // The panel: target, cost, AP, ammunition, queue and controls.
    unsigned char* panel = aim_buf + AIM_WIDTH * AIM_ART_HEIGHT;
    buf_fill(panel, AIM_WIDTH, AIM_PANEL_HEIGHT, AIM_WIDTH, colorTable[0]);
    draw_box(panel, AIM_WIDTH, 0, 0, AIM_WIDTH - 1, AIM_PANEL_HEIGHT - 1, green);

    int line = text_height() + 2;
    int y = AIM_ART_HEIGHT + 4;
    char text[160];

    Object* weapon = item_hit_with(obj_dude, aim_state.hitMode);
    snprintf(text, sizeof(text), "AIMED SHOT: %s (HP %d/%d)", aim_target_name(target), critter_get_hits(target), stat_level(target, STAT_MAXIMUM_HIT_POINTS));
    aim_text(8, y, text, bright, 300);
    if (aim_state.badShot != COMBAT_BAD_SHOT_OK) {
        aim_text(AIM_WIDTH - 8 - text_width(aim_bad_shot_text(aim_state.badShot)), y, aim_bad_shot_text(aim_state.badShot), red);
    }
    y += line;

    snprintf(text, sizeof(text), "%s: %d AP per aimed attack", weapon != NULL ? object_name(weapon) : "Unarmed", aim_state.apCost);
    aim_text(8, y, text, green, 260);
    int rounds = aim_rounds_per_attack(weapon, aim_state.hitMode);
    if (rounds > 0) {
        bool thrown = item_w_subtype(weapon, aim_state.hitMode) == ATTACK_TYPE_THROW;
        snprintf(text, sizeof(text), "%s %d (%d reserved)", thrown ? "Carried" : "Ammo", aim_rounds_available(weapon, aim_state.hitMode), aim_state.reservedRounds);
        aim_text(AIM_WIDTH - 8 - text_width(text), y, text, green);
    }
    y += line;

    snprintf(text, sizeof(text), "AP %d of %d free (%d reserved)", aim_state.availableAp, (int)rt_get_dude_ap(), aim_state.reservedAp);
    aim_text(8, y, text, aim_state.availableAp >= aim_state.apCost ? green : red);
    y += line + 2;

    draw_line(aim_buf, AIM_WIDTH, 6, y - 2, AIM_WIDTH - 7, y - 2, dim);
    if (aim_state.queueLength == 0) {
        aim_text(8, y, "Queue empty: pick a body part and press A.", dim);
    } else {
        // The last entries that fit.
        int rows = 4;
        int first = aim_state.queueLength > rows ? aim_state.queueLength - rows : 0;
        for (int index = first; index < aim_state.queueLength; index++) {
            const RtAimedEntry* entry = &(aim_state.queue[index]);
            const char* part = rt_aimed_part_name(entry->target, entry->hitLocation);
            snprintf(text, sizeof(text), "%d. %s - %s - %s - %d AP (%d%%)",
                index + 1, aim_target_name(entry->target), part != NULL ? part : "?", aim_weapon_name(entry->weaponPid), entry->apCost, entry->toHit);
            aim_text(8, y + (index - first) * line, text, entry->target == target ? bright : green);
        }
    }
    y = AIM_ART_HEIGHT + AIM_PANEL_HEIGHT - line * 2 - 4;

    if (aim_state.message[0] != '\0' && SDL_GetTicks() < aim_message_until) {
        aim_text(8, y, aim_state.message, aim_message_error ? red : bright);
    }
    y += line;
    aim_text(8, y, "D-pad Part  A Queue  X Undo  RS Target  RT Fire  B/LB Cancel", green);

    win_draw(aim_win);
}

// D-pad navigation over the two columns, skipping missing body parts.
static void aim_navigate(int key)
{
    int column = aim_state.selected / 4;
    int row = aim_state.selected % 4;

    if (key == KEY_ARROW_UP || key == KEY_ARROW_DOWN) {
        int step = key == KEY_ARROW_UP ? 3 : 1;
        for (int tries = 0; tries < 4; tries++) {
            row = (row + step) % 4;
            if (aim_state.available[column * 4 + row]) {
                break;
            }
        }
    } else if (key == KEY_ARROW_LEFT || key == KEY_ARROW_RIGHT) {
        int other = 1 - column;
        // The same row, or the nearest one with a body part.
        int best = -1;
        for (int distance = 0; distance < 4 && best == -1; distance++) {
            for (int sign = -1; sign <= 1; sign += 2) {
                int candidate = row + sign * distance;
                if (candidate >= 0 && candidate < 4 && aim_state.available[other * 4 + candidate]) {
                    best = candidate;
                    break;
                }
            }
        }
        if (best == -1) {
            return;
        }
        column = other;
        row = best;
    } else {
        return;
    }

    int selected = column * 4 + row;
    if (aim_state.available[selected] && selected != aim_state.selected) {
        aim_state.selected = selected;
        gsound_play_sfx_file("ib1p1xx1");
    }
}

static void aim_queue_selected()
{
    Object* target = aim_state.target;
    int index = aim_state.selected;
    const char* part = rt_aimed_part_name(target, aim_state.locations[index]);

    aim_refresh_target();
    if (aim_state.badShot != COMBAT_BAD_SHOT_OK) {
        aim_state.refusals++;
        aim_say(true, "Can't queue: %s.", aim_bad_shot_text(aim_state.badShot));
        return;
    }

    if (aim_state.queueLength >= RT_AIMED_QUEUE_MAX) {
        aim_state.refusals++;
        aim_say(true, "The queue is full.");
        return;
    }

    if (aim_state.availableAp < aim_state.apCost) {
        aim_state.refusals++;
        aim_say(true, "Not enough AP: an aimed attack needs %d, %d left.", aim_state.apCost, aim_state.availableAp);
        return;
    }

    Object* weapon = item_hit_with(obj_dude, aim_state.hitMode);
    int rounds = aim_rounds_per_attack(weapon, aim_state.hitMode);
    if (rounds > 0 && aim_rounds_available(weapon, aim_state.hitMode) - aim_state.reservedRounds < rounds) {
        aim_state.refusals++;
        aim_say(true, item_w_subtype(weapon, aim_state.hitMode) == ATTACK_TYPE_THROW ? "Nothing left to throw." : "Not enough ammunition for another shot.");
        return;
    }

    RtAimedEntry* entry = &(aim_state.queue[aim_state.queueLength++]);
    entry->target = target;
    entry->hitLocation = aim_state.locations[index];
    entry->hitMode = aim_state.hitMode;
    entry->weaponPid = weapon != NULL ? weapon->pid : -1;
    entry->apCost = aim_state.apCost;
    entry->rounds = rounds;
    entry->toHit = aim_state.toHit[index];
    aim_state.queued++;
    aim_recount();

    gsound_play_sfx_file("icsxxxx1");
    aim_say(false, "Queued: %s, %s (%d%%, %d AP).", aim_target_name(target), part != NULL ? part : "?", entry->toHit, entry->apCost);
}

static void aim_undo()
{
    if (aim_state.queueLength == 0) {
        aim_say(true, "The queue is empty.");
        return;
    }

    RtAimedEntry* entry = &(aim_state.queue[--aim_state.queueLength]);
    aim_recount();
    const char* part = rt_aimed_part_name(entry->target, entry->hitLocation);
    gsound_play_sfx_file("ib1p1xx1");
    aim_say(false, "Removed: %s, %s (%d AP back).", aim_target_name(entry->target), part != NULL ? part : "?", entry->apCost);
}

static void aim_switch_target(float fx, float fy)
{
    Object* next = rt_aim_switch_target(aim_state.target, fx, fy);
    if (next == NULL) {
        aim_say(true, "No other target in that direction.");
        return;
    }

    aim_state.target = next;
    aim_state.targetSwitches++;
    aim_set_outline(next);
    aim_refresh_target();
    gsound_play_sfx_file("ib1p1xx1");
    aim_say(false, "Target: %s.", aim_target_name(next));
}

static void aim_handle_key(int key)
{
    switch (key) {
    case KEY_ESCAPE:
    case KEY_PAGE_UP:
        aim_done = true;
        break;
    case KEY_RETURN:
        aim_queue_selected();
        break;
    case GAMEPAD_KEY_X:
    case KEY_LOWERCASE_X:
    case KEY_BACKSPACE:
        aim_undo();
        break;
    case GAMEPAD_KEY_RT:
    case KEY_LOWERCASE_F:
    case KEY_SPACE:
        if (aim_state.queueLength == 0) {
            aim_say(true, "Queue an attack with A first.");
        } else {
            aim_fire = true;
            aim_done = true;
        }
        break;
    case KEY_TAB:
        // Keyboard: the next target to the right.
        aim_switch_target(1.0f, 0.0f);
        break;
    default:
        aim_navigate(key);
        break;
    }
}

void rt_aimed_open()
{
    if (aim_state.executing || aim_state.selecting) {
        return;
    }

    int hitMode;
    bool aiming;
    if (intface_get_attack(&hitMode, &aiming) != 0) {
        aim_print("The active item can't attack.");
        return;
    }

    char reason[96];
    const char* unsupported = aim_unsupported_reason(hitMode, reason, sizeof(reason));
    if (unsupported != NULL) {
        aim_state.refusals++;
        aim_print("%s", unsupported);
        return;
    }

    Object* target = rt_aim_default_target();
    if (target == NULL) {
        aim_state.refusals++;
        aim_print("No enemy in sight for an aimed shot.");
        return;
    }

    memset(aim_state.queue, 0, sizeof(aim_state.queue));
    aim_state.queueLength = 0;
    aim_state.next = 0;
    aim_state.target = target;
    aim_state.selected = 0;
    aim_state.hitMode = hitMode;
    aim_state.apCost = item_w_mp_cost(obj_dude, hitMode, true);
    aim_state.message[0] = '\0';
    aim_state.opens++;
    aim_refresh_target();

    // The window goes to the side away from the target, so it stays visible.
    int screenWidth = scr_size.lrx - scr_size.ulx + 1;
    int screenHeight = scr_size.lry - scr_size.uly + 1;
    int x = (screenWidth - AIM_WIDTH) / 2;
    int tx;
    int ty;
    if (rt_view_tile_center(target->tile, target->elevation, &tx, &ty) && screenWidth >= AIM_WIDTH + 160) {
        x = tx < screenWidth / 2 ? screenWidth - AIM_WIDTH - 8 : 8;
    }
    int y = (screenHeight - AIM_HEIGHT) / 2;
    if (y < 0) {
        y = 0;
    }

    // The world stops while the attacks are planned.
    bool isoWasEnabled = map_disable_bk_processes();

    aim_win = win_add(x, y, AIM_WIDTH, AIM_HEIGHT, colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (aim_win == -1) {
        if (isoWasEnabled) {
            map_enable_bk_processes();
        }
        return;
    }
    aim_buf = win_get_buf(aim_win);

    aim_state.selecting = true;
    aim_done = false;
    aim_fire = false;
    aim_set_outline(target);
    rt_log("aimed: open on %s (%s, %d AP per attack, %d AP)",
        aim_target_name(target), aim_weapon_name(item_hit_with(obj_dude, hitMode) != NULL ? item_hit_with(obj_dude, hitMode)->pid : -1),
        aim_state.apCost, (int)rt_get_dude_ap());

    bool cursorWasHidden = mouse_hidden();
    mouse_hide();
    gamepad_set_menu_key_mode(true);

    int oldFont = text_curr();
    text_font(101);

    while (!aim_done) {
        sharedFpsLimiter.mark();

        int key = get_input();
        if (game_user_wants_to_quit != 0) {
            break;
        }
        if (key != -1) {
            aim_handle_key(key);
        }

        float fx;
        float fy;
        if (gamepad_consume_menu_flick(&fx, &fy)) {
            aim_switch_target(fx, fy);
        }

        // A target that died meanwhile can't stay selected (scripts are
        // paused, but be safe).
        if (!rt_aim_target_valid(aim_state.target)) {
            aim_done = true;
            aim_fire = false;
        }

        if (!aim_done) {
            aim_render();
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    text_font(oldFont);
    gamepad_set_menu_key_mode(false);
    if (!cursorWasHidden) {
        mouse_show();
    }

    win_delete(aim_win);
    aim_win = -1;
    aim_buf = NULL;
    aim_set_outline(NULL);
    aim_state.selecting = false;

    if (isoWasEnabled) {
        map_enable_bk_processes();
    }

    if (aim_fire && aim_state.queueLength > 0) {
        aim_state.executing = true;
        aim_state.next = 0;
        aim_state.executions++;
        aim_entry_since = SDL_GetTicks();
        aim_recount();
        aim_print("Aimed attacks: %d queued. B cancels the rest.", aim_state.queueLength);
        rt_log("aimed: executing %d attacks (%d AP reserved)", aim_state.queueLength, aim_state.reservedAp);
    } else {
        // Nothing was spent; the reservations just go away.
        aim_state.queueLength = 0;
        aim_state.cancelled++;
        aim_recount();
        aim_print("Aimed shot cancelled.");
    }
}

// -----------------------------------------------------------------------------
// Execution
// -----------------------------------------------------------------------------

static void aim_finish(const char* reason)
{
    if (!aim_state.executing) {
        return;
    }

    int left = aim_state.queueLength - aim_state.next;
    aim_state.executing = false;
    aim_state.next = aim_state.queueLength;
    aim_recount();
    rt_log("aimed: execution ended (%s), %d not started", reason, left);
}

static void aim_skip(const char* why)
{
    RtAimedEntry* entry = &(aim_state.queue[aim_state.next]);
    const char* part = rt_aimed_part_name(entry->target, entry->hitLocation);
    aim_print("Aimed attack %d skipped: %s.", aim_state.next + 1, why);
    rt_log("aimed: entry %d (%s) skipped: %s (released %d AP, %d rounds)",
        aim_state.next + 1, part != NULL ? part : "?", why, entry->apCost, entry->rounds);
    aim_state.skipped++;
    aim_state.next++;
    aim_entry_since = SDL_GetTicks();
    aim_recount();
}

void rt_aimed_update(bool canAct, bool pressB)
{
    if (!aim_state.executing) {
        return;
    }

    if (pressB) {
        int left = aim_state.queueLength - aim_state.next;
        aim_state.cancelled++;
        aim_finish("cancelled with B");
        aim_print(left > 0 ? "Remaining aimed attacks cancelled." : "Aimed attacks finished.");
        return;
    }

    if (!canAct) {
        int results = obj_dude->data.critter.combat.results;
        if ((results & (DAM_KNOCKED_OUT | DAM_KNOCKED_DOWN | DAM_DEAD)) != 0 || obj_dude->elevation != map_elevation) {
            aim_finish("interrupted");
            aim_print("Aimed attacks interrupted.");
        }
        return;
    }

    if (aim_state.next >= aim_state.queueLength) {
        // The last committed attack plays out before controls return.
        if (!rt_aim_player_in_attack()) {
            aim_finish("done");
        }
        return;
    }

    RtAimedEntry* entry = &(aim_state.queue[aim_state.next]);

    // Checked again right before committing; never redirected.
    if (!rt_aim_target_valid(entry->target)) {
        aim_skip(entry->target != NULL && critter_is_dead(entry->target) ? "the target is dead" : "the target is no longer valid");
        return;
    }

    Object* weapon = item_hit_with(obj_dude, entry->hitMode);
    int weaponPid = weapon != NULL ? weapon->pid : -1;
    if (weaponPid != entry->weaponPid) {
        aim_skip("the weapon changed");
        return;
    }

    int badShot = rt_aim_check_shot(entry->target, entry->hitMode);
    if (badShot != COMBAT_BAD_SHOT_OK) {
        aim_skip(aim_bad_shot_text(badShot));
        return;
    }

    int rc = rt_aim_attack(entry->target, entry->hitMode, entry->hitLocation, entry->apCost);
    if (rc == RT_AIM_ATTACK_STARTED) {
        const char* part = rt_aimed_part_name(entry->target, entry->hitLocation);
        rt_log("aimed: entry %d committed: %s, %s (%d AP spent, %.2f AP left)",
            aim_state.next + 1, aim_target_name(entry->target), part != NULL ? part : "?", entry->apCost, rt_get_dude_ap());
        aim_state.committed++;
        aim_state.next++;
        aim_entry_since = SDL_GetTicks();
        aim_recount();
    } else if (rc == RT_AIM_ATTACK_FAILED) {
        aim_skip("the attack could not start");
    } else if (SDL_GetTicks() - aim_entry_since > AIM_ENTRY_TIMEOUT_MS) {
        aim_skip("no clear moment to attack");
    }
}

// Each queued attack gets the same priority as an RT press: for a moment
// after it becomes the next one (RT_ATTACK_BUFFER_MS in realtime.cc), no
// enemy starts an attack on the player. After that the usual queue applies.
bool rt_aimed_wants_window()
{
    return aim_state.executing
        && aim_state.next < aim_state.queueLength
        && SDL_GetTicks() - aim_entry_since < AIM_ENTRY_PRIORITY_MS;
}

void rt_aimed_reset(const char* reason)
{
    if (aim_state.executing || aim_state.queueLength > 0) {
        rt_log("aimed: reset (%s)", reason);
    }
    aim_state.executing = false;
    aim_state.queueLength = 0;
    aim_state.next = 0;
    aim_state.target = NULL;
    aim_outlined = NULL;
    aim_recount();
}

void rt_aimed_object_erased(Object* obj)
{
    if (aim_outlined == obj) {
        aim_outlined = NULL;
    }
    if (aim_state.target == obj) {
        aim_state.target = NULL;
    }
    // Entries keep their place (and are skipped when their turn comes), but
    // never keep a pointer to a removed object.
    for (int index = 0; index < aim_state.queueLength; index++) {
        if (aim_state.queue[index].target == obj) {
            aim_state.queue[index].target = NULL;
        }
    }
}

} // namespace fallout
