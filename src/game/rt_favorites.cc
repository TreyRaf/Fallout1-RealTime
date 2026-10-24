#include "game/rt_favorites.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "game/art.h"
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

// Save data block appended after the original save handlers. Version 1 had
// eight slots, version 2 has four.
#define FAV_SAVE_MAGIC 0x52544656 // 'RTFV'
#define FAV_SAVE_VERSION_EIGHT 1
#define FAV_SAVE_VERSION 2

#define FAV_WIDTH 360
#define FAV_HEIGHT 300
#define FAV_BOX_WIDTH 96
#define FAV_BOX_HEIGHT 66
#define FAV_ART_WIDTH 70
#define FAV_ART_HEIGHT 38
#define FAV_CENTER_X (FAV_WIDTH / 2)
#define FAV_CENTER_Y 140
#define FAV_OFFSET_X 120
#define FAV_OFFSET_Y 82

// Every slot starts empty (-1).
static RtFavoritesState fav_state = { false, 0, { -1, -1, -1, -1 } };
static int fav_win = -1;
static unsigned char* fav_buf = NULL;
static bool fav_done = false;
static int fav_result = -1;
static int fav_assign_pid = -1;
static unsigned int fav_message_until = 0;
static bool fav_message_error = false;
// The slot the radial opens on.
static int fav_last_slot = 0;

static const char* fav_slot_names[RT_FAVORITES_COUNT] = { "Up", "Right", "Down", "Left" };
static const char* fav_old_slot_names[RT_FAVORITES_OLD_COUNT] = { "up", "up-right", "right", "down-right", "down", "down-left", "left", "up-left" };

static void fav_say(bool error, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(fav_state.lastMessage, sizeof(fav_state.lastMessage), format, args);
    va_end(args);
    fav_message_until = SDL_GetTicks() + 3500;
    fav_message_error = error;
    rt_log("favorites: %s", fav_state.lastMessage);
}

static void fav_print(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    display_print(buffer);
    SDL_strlcpy(fav_state.lastMessage, buffer, sizeof(fav_state.lastMessage));
    rt_log("ui: %s", buffer);
}

const RtFavoritesState* rt_favorites_get_state()
{
    return &fav_state;
}

const char* rt_favorites_slot_name(int slot)
{
    return slot >= 0 && slot < RT_FAVORITES_COUNT ? fav_slot_names[slot] : "";
}

void rt_favorites_reset()
{
    for (int slot = 0; slot < RT_FAVORITES_COUNT; slot++) {
        fav_state.slots[slot] = -1;
    }
    fav_last_slot = 0;
}

void rt_favorites_debug_set(int slot, int pid)
{
    if (slot >= 0 && slot < RT_FAVORITES_COUNT) {
        fav_state.slots[slot] = pid;
    }
}

static int fav_item_type(int pid)
{
    Proto* proto;
    if (pid == -1 || proto_ptr(pid, &proto) == -1) {
        return -1;
    }
    return proto->item.type;
}

static const char* fav_name(int pid)
{
    char* name = pid != -1 ? proto_name(pid) : NULL;
    return name != NULL ? name : "";
}

// Only item prototypes that exist.
static int fav_valid_pid(int pid)
{
    Proto* proto;
    if (pid != -1 && (PID_TYPE(pid)) == OBJ_TYPE_ITEM && proto_ptr(pid, &proto) != -1) {
        return pid;
    }
    return -1;
}

int rt_favorites_migrate(const int oldSlots[RT_FAVORITES_OLD_COUNT], int slots[RT_FAVORITES_COUNT], int* moved, char* report, size_t size)
{
    report[0] = '\0';
    *moved = 0;
    int dropped = 0;

    // The cardinal slots keep their direction.
    for (int slot = 0; slot < RT_FAVORITES_COUNT; slot++) {
        slots[slot] = oldSlots[slot * 2];
    }

    // Diagonal slots: up-right, down-right, down-left, up-left.
    for (int old = 1; old < RT_FAVORITES_OLD_COUNT; old += 2) {
        int pid = oldSlots[old];
        if (pid == -1) {
            continue;
        }

        bool present = false;
        for (int slot = 0; slot < RT_FAVORITES_COUNT; slot++) {
            present = present || slots[slot] == pid;
        }
        if (present) {
            // Already has a direction: nothing is lost.
            continue;
        }

        // The neighbors: earlier one clockwise first (up-right -> up, right).
        int first = old / 2;
        int second = (first + 1) % RT_FAVORITES_COUNT;
        int target = slots[first] == -1 ? first : (slots[second] == -1 ? second : -1);

        char line[96];
        if (target != -1) {
            slots[target] = pid;
            (*moved)++;
            snprintf(line, sizeof(line), "%s moved from %s to %s. ", fav_name(pid), fav_old_slot_names[old], fav_old_slot_names[target * 2]);
        } else {
            dropped++;
            snprintf(line, sizeof(line), "%s (%s) dropped: %s and %s are taken. ", fav_name(pid), fav_old_slot_names[old],
                fav_old_slot_names[first * 2], fav_old_slot_names[second * 2]);
        }
        SDL_strlcat(report, line, size);
    }
    return dropped;
}

int rt_favorites_save(DB_FILE* stream)
{
    if (db_fwriteInt(stream, FAV_SAVE_MAGIC) == -1 || db_fwriteInt(stream, FAV_SAVE_VERSION) == -1) {
        return -1;
    }
    for (int slot = 0; slot < RT_FAVORITES_COUNT; slot++) {
        if (db_fwriteInt(stream, fav_state.slots[slot]) == -1) {
            return -1;
        }
    }
    return 0;
}

int rt_favorites_load(DB_FILE* stream)
{
    rt_favorites_reset();
    fav_state.migratedMoved = 0;
    fav_state.migratedDropped = 0;

    int magic;
    int version;
    if (db_freadInt(stream, &magic) == -1 || magic != FAV_SAVE_MAGIC || db_freadInt(stream, &version) == -1) {
        rt_log("favorites: none in this save");
        return -1;
    }

    if (version == FAV_SAVE_VERSION_EIGHT) {
        int oldSlots[RT_FAVORITES_OLD_COUNT];
        for (int old = 0; old < RT_FAVORITES_OLD_COUNT; old++) {
            if (db_freadInt(stream, &(oldSlots[old])) == -1) {
                return -1;
            }
            oldSlots[old] = fav_valid_pid(oldSlots[old]);
        }

        char report[512];
        int slots[RT_FAVORITES_COUNT];
        fav_state.migratedDropped = rt_favorites_migrate(oldSlots, slots, &(fav_state.migratedMoved), report, sizeof(report));
        memcpy(fav_state.slots, slots, sizeof(slots));
        if (report[0] != '\0') {
            // The player is told; nothing is overwritten silently.
            fav_print("Favorites now use four directions. %s", report);
        }
        rt_log("favorites: eight-slot save migrated (%d dropped): %s", fav_state.migratedDropped, report);
        return 0;
    }

    if (version != FAV_SAVE_VERSION) {
        rt_log("favorites: unknown save version %d", version);
        return -1;
    }

    for (int slot = 0; slot < RT_FAVORITES_COUNT; slot++) {
        int pid;
        if (db_freadInt(stream, &pid) == -1) {
            rt_favorites_reset();
            return -1;
        }
        fav_state.slots[slot] = fav_valid_pid(pid);
    }
    rt_log("favorites: loaded from the save");
    return 0;
}

bool rt_favorites_is_eligible(Object* item, const char** reason)
{
    static char buffer[128];
    *reason = NULL;

    if (item == NULL || item->pid == PROTO_ID_MONEY) {
        *reason = "Caps can't be a favorite.";
        return false;
    }

    switch (item_get_type(item)) {
    case ITEM_TYPE_WEAPON:
    case ITEM_TYPE_ARMOR:
    case ITEM_TYPE_DRUG:
        return true;
    case ITEM_TYPE_AMMO:
        *reason = "Ammunition can't be a favorite (X reloads).";
        return false;
    case ITEM_TYPE_MISC:
        if (proto_action_can_use(item->pid) || proto_action_can_use_on(item->pid)) {
            return true;
        }
        break;
    }

    snprintf(buffer, sizeof(buffer), "The %s has nothing to activate.", object_name(item));
    *reason = buffer;
    return false;
}

int rt_favorites_slot_of(int pid)
{
    for (int slot = 0; slot < RT_FAVORITES_COUNT; slot++) {
        if (pid != -1 && fav_state.slots[slot] == pid) {
            return slot;
        }
    }
    return -1;
}

Object* rt_favorites_find_item(int slot)
{
    int pid = slot >= 0 && slot < RT_FAVORITES_COUNT ? fav_state.slots[slot] : -1;
    if (pid == -1 || obj_dude == NULL) {
        return NULL;
    }

    // An item not already equipped first (so a spare weapon or a stack is
    // used before the one in hand).
    Inventory* inventory = &(obj_dude->data.inventory);
    Object* equipped = NULL;
    for (int index = 0; index < inventory->length; index++) {
        Object* item = inventory->items[index].item;
        if (item->pid != pid) {
            continue;
        }
        if ((item->flags & OBJECT_EQUIPPED) == 0) {
            return item;
        }
        if (equipped == NULL) {
            equipped = item;
        }
    }
    return equipped;
}

int rt_favorites_quantity(int slot)
{
    int pid = slot >= 0 && slot < RT_FAVORITES_COUNT ? fav_state.slots[slot] : -1;
    if (pid == -1 || obj_dude == NULL) {
        return 0;
    }

    int quantity = 0;
    Inventory* inventory = &(obj_dude->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item->pid == pid) {
            quantity += inventory->items[index].quantity;
        }
    }
    return quantity;
}

// What activating the slot does, for the radial.
static const char* fav_action_label(int pid)
{
    switch (fav_item_type(pid)) {
    case ITEM_TYPE_WEAPON:
        return "Ready in hand";
    case ITEM_TYPE_ARMOR:
        return "Wear";
    default:
        return "Use";
    }
}

// -----------------------------------------------------------------------------
// Assignment radial (Pip-Boy)
// -----------------------------------------------------------------------------

static void fav_slot_center(int slot, int* x, int* y)
{
    static const int dx[RT_FAVORITES_COUNT] = { 0, 1, 0, -1 };
    static const int dy[RT_FAVORITES_COUNT] = { -1, 0, 1, 0 };
    *x = FAV_CENTER_X + dx[slot] * FAV_OFFSET_X;
    *y = FAV_CENTER_Y + dy[slot] * FAV_OFFSET_Y;
}

static void fav_text(int x, int y, const char* text, int color, int width)
{
    if (x < 1) {
        x = 1;
    }
    if (width > FAV_WIDTH - x - 1) {
        width = FAV_WIDTH - x - 1;
    }
    if (width > 0 && y >= 0 && y + text_height() < FAV_HEIGHT) {
        text_to_buf(fav_buf + FAV_WIDTH * y + x, text, width, FAV_WIDTH, color);
    }
}

static void fav_text_centered(int cx, int y, const char* text, int color, int width)
{
    int textWidth = text_width(text);
    if (textWidth > width) {
        textWidth = width;
    }
    fav_text(cx - textWidth / 2, y, text, color, width);
}

static void fav_render()
{
    int green = colorTable[992];
    int bright = colorTable[32747];
    int dim = colorTable[8804];
    int red = colorTable[31744];
    int bar = colorTable[(0 << 10) | (9 << 5) | 0];
    int black = colorTable[0];

    buf_fill(fav_buf, FAV_WIDTH, FAV_HEIGHT, FAV_WIDTH, black);
    draw_box(fav_buf, FAV_WIDTH, 0, 0, FAV_WIDTH - 1, FAV_HEIGHT - 1, green);
    draw_box(fav_buf, FAV_WIDTH, 2, 2, FAV_WIDTH - 3, FAV_HEIGHT - 3, dim);

    int line = text_height() + 2;
    fav_text(8, 6, "ASSIGN FAVORITE", bright, 160);
    fav_text(FAV_WIDTH - 8 - text_width(fav_name(fav_assign_pid)), 6, fav_name(fav_assign_pid), bright, 180);

    for (int slot = 0; slot < RT_FAVORITES_COUNT; slot++) {
        int cx;
        int cy;
        fav_slot_center(slot, &cx, &cy);
        int left = cx - FAV_BOX_WIDTH / 2;
        int top = cy - FAV_BOX_HEIGHT / 2;
        bool selected = slot == fav_state.selected;

        if (selected) {
            buf_fill(fav_buf + FAV_WIDTH * top + left, FAV_BOX_WIDTH, FAV_BOX_HEIGHT, FAV_WIDTH, bar);
        }
        draw_box(fav_buf, FAV_WIDTH, left, top, left + FAV_BOX_WIDTH - 1, top + FAV_BOX_HEIGHT - 1, selected ? bright : dim);
        if (selected) {
            draw_box(fav_buf, FAV_WIDTH, left + 1, top + 1, left + FAV_BOX_WIDTH - 2, top + FAV_BOX_HEIGHT - 2, bright);
        }

        fav_text(left + 3, top + 2, fav_slot_names[slot], dim, 40);

        int pid = fav_state.slots[slot];
        if (pid == -1) {
            fav_text_centered(cx, cy - text_height() / 2, "Empty", dim, FAV_BOX_WIDTH - 6);
            continue;
        }

        int quantity = rt_favorites_quantity(slot);
        Proto* proto;
        if (proto_ptr(pid, &proto) != -1) {
            unsigned char* dest = fav_buf + FAV_WIDTH * (top + 12) + cx - FAV_ART_WIDTH / 2;
            scale_art(proto->item.inventoryFid, dest, FAV_ART_WIDTH, FAV_ART_HEIGHT, FAV_WIDTH);
            if (quantity == 0) {
                // Not carried: the artwork is dimmed out with a dither.
                for (int y = 0; y < FAV_ART_HEIGHT; y++) {
                    for (int x = (y & 1); x < FAV_ART_WIDTH; x += 2) {
                        dest[FAV_WIDTH * y + x] = black;
                    }
                }
            }
        }

        fav_text_centered(cx, top + FAV_BOX_HEIGHT - line - 1, fav_name(pid), quantity > 0 ? green : dim, FAV_BOX_WIDTH - 6);
        char count[16];
        if (quantity == 0) {
            fav_text(left + FAV_BOX_WIDTH - 4 - text_width("none"), top + 2, "none", red, 40);
        } else if (quantity > 1 || fav_item_type(pid) == ITEM_TYPE_DRUG) {
            snprintf(count, sizeof(count), "x%d", quantity);
            fav_text(left + FAV_BOX_WIDTH - 4 - text_width(count), top + 2, count, bright, 40);
        }
    }

    // The selected direction, in the middle.
    int pid = fav_state.slots[fav_state.selected];
    int y = FAV_CENTER_Y - line;
    fav_text_centered(FAV_CENTER_X, y, fav_slot_names[fav_state.selected], bright, 130);
    if (pid == -1) {
        fav_text_centered(FAV_CENTER_X, y + line, "Empty", dim, 130);
    } else {
        int quantity = rt_favorites_quantity(fav_state.selected);
        char text[64];
        if (quantity == 0) {
            snprintf(text, sizeof(text), "Not carried");
        } else {
            snprintf(text, sizeof(text), "%s (%d)", fav_action_label(pid), quantity);
        }
        fav_text_centered(FAV_CENTER_X, y + line, text, quantity > 0 ? green : red, 130);
    }

    if (fav_state.lastMessage[0] != '\0' && SDL_GetTicks() < fav_message_until) {
        fav_text_centered(FAV_CENTER_X, FAV_HEIGHT - line * 2 - 6, fav_state.lastMessage, fav_message_error ? red : bright, FAV_WIDTH - 16);
    }

    fav_text_centered(FAV_CENTER_X, FAV_HEIGHT - line - 4, "Stick/D-pad Direction   A Assign   X Clear   B Back", green, FAV_WIDTH - 16);

    win_draw(fav_win);
}

// The direction a stick or D-pad reading points at, or -1 when neutral.
static int fav_slot_from_vector(float x, float y)
{
    if (x * x + y * y < 0.5f * 0.5f) {
        return -1;
    }
    if (fabsf(x) > fabsf(y)) {
        return x > 0.0f ? RT_FAVORITES_RIGHT : RT_FAVORITES_LEFT;
    }
    return y > 0.0f ? RT_FAVORITES_DOWN : RT_FAVORITES_UP;
}

static void fav_select(int slot)
{
    if (slot != -1 && slot != fav_state.selected) {
        fav_state.selected = slot;
        gsound_play_sfx_file("ib1p1xx1");
    }
}

static void fav_handle_key(int key)
{
    int slot = fav_state.selected;
    switch (key) {
    case KEY_ESCAPE:
        fav_done = true;
        return;
    // Arrow keys (keyboard, and the controller's D-pad and stick in key
    // mode) pick that direction.
    case KEY_ARROW_UP:
        fav_select(RT_FAVORITES_UP);
        return;
    case KEY_ARROW_RIGHT:
        fav_select(RT_FAVORITES_RIGHT);
        return;
    case KEY_ARROW_DOWN:
        fav_select(RT_FAVORITES_DOWN);
        return;
    case KEY_ARROW_LEFT:
        fav_select(RT_FAVORITES_LEFT);
        return;
    case KEY_RETURN: {
        // One slot per item type: assigning it elsewhere moves it.
        int previous = rt_favorites_slot_of(fav_assign_pid);
        if (previous != -1 && previous != slot) {
            fav_state.slots[previous] = -1;
        }
        int replaced = fav_state.slots[slot];
        fav_state.slots[slot] = fav_assign_pid;
        fav_state.assigns++;
        gsound_play_sfx_file("ib1p1xx1");
        if (replaced != -1 && replaced != fav_assign_pid) {
            fav_say(false, "%s: %s (replaced %s).", fav_slot_names[slot], fav_name(fav_assign_pid), fav_name(replaced));
        } else {
            fav_say(false, "%s: %s.", fav_slot_names[slot], fav_name(fav_assign_pid));
        }
        fav_result = slot;
        fav_done = true;
        return;
    }
    }

    if (key == GAMEPAD_KEY_X || key == KEY_LOWERCASE_X) {
        if (fav_state.slots[slot] == -1) {
            fav_say(true, "%s is already empty.", fav_slot_names[slot]);
        } else {
            fav_say(false, "%s cleared (%s).", fav_slot_names[slot], fav_name(fav_state.slots[slot]));
            fav_state.slots[slot] = -1;
            fav_state.clears++;
            gsound_play_sfx_file("ib1p1xx1");
        }
    }
}

void rt_favorites_open_assign(Object* item)
{
    const char* reason;
    if (!rt_favorites_is_eligible(item, &reason)) {
        fav_state.refusals++;
        SDL_strlcpy(fav_state.lastMessage, reason, sizeof(fav_state.lastMessage));
        rt_log("favorites: %s", reason);
        return;
    }

    fav_assign_pid = item->pid;
    int current = rt_favorites_slot_of(item->pid);
    fav_state.selected = current != -1 ? current : fav_last_slot;
    fav_state.lastMessage[0] = '\0';
    fav_state.opens++;
    fav_done = false;
    fav_result = -1;

    int screenWidth = scr_size.lrx - scr_size.ulx + 1;
    int screenHeight = scr_size.lry - scr_size.uly + 1;
    int x = (screenWidth - FAV_WIDTH) / 2;
    int y = (screenHeight - INTERFACE_BAR_HEIGHT - FAV_HEIGHT) / 2;
    if (y < 0) {
        y = 0;
    }

    bool isoWasEnabled = map_disable_bk_processes();

    fav_win = win_add(x, y, FAV_WIDTH, FAV_HEIGHT, colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (fav_win == -1) {
        if (isoWasEnabled) {
            map_enable_bk_processes();
        }
        return;
    }
    fav_buf = win_get_buf(fav_win);
    fav_state.open = true;
    rt_log("favorites: assignment radial open for %s", fav_name(fav_assign_pid));

    bool wasKeyMode = gamepad_is_menu_key_mode();
    bool cursorWasHidden = mouse_hidden();
    mouse_hide();
    gamepad_set_menu_key_mode(true);

    int oldFont = text_curr();
    text_font(101);

    while (!fav_done) {
        sharedFpsLimiter.mark();

        int key = get_input();
        if (game_user_wants_to_quit != 0) {
            break;
        }
        if (key != -1) {
            fav_handle_key(key);
        }

        // The left stick picks a direction too.
        const GamepadState* pad = gamepad_get_state();
        fav_select(fav_slot_from_vector(pad->leftX, pad->leftY));

        if (!fav_done) {
            fav_render();
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    text_font(oldFont);
    gamepad_set_menu_key_mode(wasKeyMode);
    if (!cursorWasHidden) {
        mouse_show();
    }

    win_delete(fav_win);
    fav_win = -1;
    fav_buf = NULL;
    fav_state.open = false;

    if (isoWasEnabled) {
        map_enable_bk_processes();
    }

    if (fav_result != -1) {
        fav_last_slot = fav_result;
    }
    rt_log("favorites: assignment radial closed (%s)", fav_result != -1 ? "assigned" : "back");
}

// -----------------------------------------------------------------------------
// Activation (gameplay)
// -----------------------------------------------------------------------------

int rt_favorites_activate(int slot)
{
    int pid = slot >= 0 && slot < RT_FAVORITES_COUNT ? fav_state.slots[slot] : -1;
    if (pid == -1) {
        return RT_FAVORITES_EMPTY;
    }

    Object* item = rt_favorites_find_item(slot);
    if (item == NULL) {
        fav_state.refusals++;
        fav_print("You don't have any %s.", fav_name(pid));
        return RT_FAVORITES_FAILED;
    }

    char name[64];
    SDL_strlcpy(name, object_name(item), sizeof(name));
    int type = item_get_type(item);

    // An attack, reload or chem animation of the player's is playing: the
    // equipment change waits for it (chems wait in their own request).
    if (type != ITEM_TYPE_DRUG && !rt_player_can_change_equipment()) {
        return RT_FAVORITES_WAIT;
    }

    fav_state.activations++;
    switch (type) {
    case ITEM_TYPE_WEAPON: {
        bool right = intface_is_item_right_hand() != 0;
        Object* active = right ? inven_right_hand(obj_dude) : inven_left_hand(obj_dude);
        if (active != NULL && active->pid == item->pid) {
            fav_print("The %s is already in your hand.", name);
            return RT_FAVORITES_DONE;
        }
        if (inven_rt_equip(item, right ? INVEN_RT_SLOT_RIGHT_HAND : INVEN_RT_SLOT_LEFT_HAND) == 0) {
            gsound_play_sfx_file("ib1p1xx1");
            fav_print("You ready the %s.", name);
            return RT_FAVORITES_DONE;
        }
        fav_print("You can't ready the %s.", name);
        return RT_FAVORITES_FAILED;
    }
    case ITEM_TYPE_ARMOR: {
        Object* worn = inven_worn(obj_dude);
        if (worn != NULL && worn->pid == item->pid) {
            fav_print("You are already wearing the %s.", name);
            return RT_FAVORITES_DONE;
        }
        if (inven_rt_equip(item, INVEN_RT_SLOT_ARMOR) == 0) {
            gsound_play_sfx_file("ib1p1xx1");
            fav_print("You put on the %s.", name);
            return RT_FAVORITES_DONE;
        }
        fav_print("You can't put on the %s.", name);
        return RT_FAVORITES_FAILED;
    }
    case ITEM_TYPE_DRUG:
        // The same action as RT with a chem: the hands animation, then one
        // item of the stack is used.
        rt_use_item_on_self(item);
        return RT_FAVORITES_DONE;
    default:
        if (!proto_action_can_use(item->pid) && !proto_action_can_use_on(item->pid)) {
            fav_print("The %s can't be activated.", name);
            return RT_FAVORITES_FAILED;
        }
        // The item's own use action (as Use in the Pip-Boy: items used on
        // something are used on the player).
        if (inven_rt_use(item) == -1) {
            fav_print("Nothing happens.");
            return RT_FAVORITES_FAILED;
        }
        fav_print("You use the %s.", name);
        return RT_FAVORITES_DONE;
    }
}

void rt_favorites_refuse(const char* message)
{
    fav_state.refusals++;
    fav_print("%s", message);
}

} // namespace fallout
