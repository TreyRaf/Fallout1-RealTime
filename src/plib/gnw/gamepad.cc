#include "plib/gnw/gamepad.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "plib/gnw/gnw.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"

namespace fallout {

#define GAMEPAD_STICK_DEADZONE 0.24f
#define GAMEPAD_TRIGGER_PRESS 0.35f
#define GAMEPAD_TRIGGER_RELEASE 0.25f
#define GAMEPAD_CURSOR_SPEED 900.0f
#define GAMEPAD_REPEAT_DELAY 350
#define GAMEPAD_REPEAT_RATE 120
#define GAMEPAD_SCAN_INTERVAL 1000
#define GAMEPAD_NAV_POINTS_CAPACITY 256

static void gamepad_open_first();
static void gamepad_read_physical(GamepadState* state);
static void gamepad_apply_deadzone(float* x, float* y);
static void gamepad_interrupt_gameplay_input();
static void gamepad_handle_menu_press(int button);
static void gamepad_handle_menu_repeat(unsigned int now);
static void gamepad_menu_navigate(int button);

static bool gamepad_initialized = false;
static SDL_GameController* gamepad_controller = NULL;
static char gamepad_name[128] = "";
static int gamepad_detected_count = 0;
static unsigned int gamepad_last_scan = 0;
static unsigned int gamepad_last_update = 0;

static bool gamepad_gameplay_mode = false;
static bool gamepad_focused = true;
static bool gamepad_suppress_held = false;
static GamepadHook* gamepad_hook = NULL;
static bool gamepad_override_active = false;
static GamepadState gamepad_override_state;
static GamepadState gamepad_state;
static bool gamepad_was_connected = false;
static bool gamepad_previous_buttons[GAMEPAD_BUTTON_COUNT];
static bool gamepad_pending_presses[GAMEPAD_BUTTON_COUNT];
static bool gamepad_hold_valid[GAMEPAD_BUTTON_COUNT];
static bool gamepad_stick_gated = false;
static int gamepad_interruptions = 0;

static float gamepad_cursor_accum_x = 0.0f;
static float gamepad_cursor_accum_y = 0.0f;
static int gamepad_mouse_dx = 0;
static int gamepad_mouse_dy = 0;
static bool gamepad_mouse_left = false;
static bool gamepad_mouse_right = false;

static int gamepad_repeat_button = -1;
static unsigned int gamepad_repeat_next = 0;

void gamepad_init()
{
    if (gamepad_initialized) {
        return;
    }

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        return;
    }

    memset(&gamepad_state, 0, sizeof(gamepad_state));
    memset(gamepad_previous_buttons, 0, sizeof(gamepad_previous_buttons));
    memset(gamepad_pending_presses, 0, sizeof(gamepad_pending_presses));
    memset(gamepad_hold_valid, 0, sizeof(gamepad_hold_valid));

    gamepad_initialized = true;
    gamepad_open_first();
}

void gamepad_exit()
{
    if (!gamepad_initialized) {
        return;
    }

    if (gamepad_controller != NULL) {
        SDL_GameControllerClose(gamepad_controller);
        gamepad_controller = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    gamepad_initialized = false;
}

static void gamepad_open_first()
{
    gamepad_detected_count = 0;

    int count = SDL_NumJoysticks();
    for (int index = 0; index < count; index++) {
        if (!SDL_IsGameController(index)) {
            continue;
        }

        gamepad_detected_count++;

        if (gamepad_controller == NULL) {
            gamepad_controller = SDL_GameControllerOpen(index);
            if (gamepad_controller != NULL) {
                const char* name = SDL_GameControllerName(gamepad_controller);
                SDL_strlcpy(gamepad_name, name != NULL ? name : "Unknown controller", sizeof(gamepad_name));
            }
        }
    }
}

static void gamepad_apply_deadzone(float* x, float* y)
{
    float magnitude = sqrtf(*x * *x + *y * *y);
    if (magnitude < GAMEPAD_STICK_DEADZONE) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }

    float scaled = (magnitude - GAMEPAD_STICK_DEADZONE) / (1.0f - GAMEPAD_STICK_DEADZONE);
    if (scaled > 1.0f) {
        scaled = 1.0f;
    }

    *x = *x / magnitude * scaled;
    *y = *y / magnitude * scaled;
}

static void gamepad_read_physical(GamepadState* state)
{
    bool previousLeftTrigger = state->buttons[GAMEPAD_BUTTON_LEFT_TRIGGER];
    bool previousRightTrigger = state->buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER];

    memset(state, 0, sizeof(*state));

    if (gamepad_controller == NULL) {
        return;
    }

    state->connected = true;

    state->leftX = SDL_GameControllerGetAxis(gamepad_controller, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
    state->leftY = SDL_GameControllerGetAxis(gamepad_controller, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
    state->rightX = SDL_GameControllerGetAxis(gamepad_controller, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
    state->rightY = SDL_GameControllerGetAxis(gamepad_controller, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
    gamepad_apply_deadzone(&(state->leftX), &(state->leftY));
    gamepad_apply_deadzone(&(state->rightX), &(state->rightY));

    state->leftTrigger = SDL_GameControllerGetAxis(gamepad_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
    state->rightTrigger = SDL_GameControllerGetAxis(gamepad_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;

    static const SDL_GameControllerButton mapping[] = {
        SDL_CONTROLLER_BUTTON_A,
        SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_LEFTSTICK,
        SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    };

    for (int index = 0; index < GAMEPAD_BUTTON_LEFT_TRIGGER; index++) {
        state->buttons[index] = SDL_GameControllerGetButton(gamepad_controller, mapping[index]) != 0;
    }

    // Hysteresis prevents trigger chatter around the threshold.
    state->buttons[GAMEPAD_BUTTON_LEFT_TRIGGER] = state->leftTrigger >= (previousLeftTrigger ? GAMEPAD_TRIGGER_RELEASE : GAMEPAD_TRIGGER_PRESS);
    state->buttons[GAMEPAD_BUTTON_RIGHT_TRIGGER] = state->rightTrigger >= (previousRightTrigger ? GAMEPAD_TRIGGER_RELEASE : GAMEPAD_TRIGGER_PRESS);
}

// Drops everything the gameplay context may still act on.
static void gamepad_interrupt_gameplay_input()
{
    memset(gamepad_pending_presses, 0, sizeof(gamepad_pending_presses));
    memset(gamepad_hold_valid, 0, sizeof(gamepad_hold_valid));
    gamepad_stick_gated = true;
}

void gamepad_update()
{
    if (!gamepad_initialized) {
        return;
    }

    unsigned int now = SDL_GetTicks();
    float dt = gamepad_last_update != 0 ? (now - gamepad_last_update) / 1000.0f : 0.0f;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    gamepad_last_update = now;

    if (gamepad_hook != NULL) {
        gamepad_hook();
    }

    if (gamepad_controller != NULL && !SDL_GameControllerGetAttached(gamepad_controller)) {
        SDL_GameControllerClose(gamepad_controller);
        gamepad_controller = NULL;
        gamepad_name[0] = '\0';
    }

    if (gamepad_controller == NULL && now - gamepad_last_scan > GAMEPAD_SCAN_INTERVAL) {
        gamepad_last_scan = now;
        gamepad_open_first();
    }

    if (gamepad_override_active) {
        gamepad_state = gamepad_override_state;
        if (!gamepad_state.connected) {
            memset(&gamepad_state, 0, sizeof(gamepad_state));
        }
    } else {
        SDL_GameControllerUpdate();
        gamepad_read_physical(&gamepad_state);
    }

    if (!gamepad_focused) {
        // Unfocused: no input at all.
        bool connected = gamepad_state.connected;
        memset(&gamepad_state, 0, sizeof(gamepad_state));
        gamepad_state.connected = connected;
    }

    if (gamepad_was_connected && !gamepad_state.connected) {
        gamepad_interruptions++;
        gamepad_interrupt_gameplay_input();
    }

    // Buttons already held when the controller (re)connects or focus returns
    // are not new presses.
    if ((!gamepad_was_connected && gamepad_state.connected) || gamepad_suppress_held) {
        memcpy(gamepad_previous_buttons, gamepad_state.buttons, sizeof(gamepad_previous_buttons));
        gamepad_suppress_held = false;
    }
    gamepad_was_connected = gamepad_state.connected;

    if (!gamepad_gameplay_mode) {
        // Leaving gameplay (menus, dialogue) invalidates held gameplay input.
        memset(gamepad_hold_valid, 0, sizeof(gamepad_hold_valid));
        memset(gamepad_pending_presses, 0, sizeof(gamepad_pending_presses));
        gamepad_stick_gated = true;
    } else if (gamepad_state.connected && gamepad_focused
        && gamepad_state.leftX == 0.0f && gamepad_state.leftY == 0.0f) {
        // Only a real neutral reading releases the gate (a disconnected or
        // unfocused controller reads as neutral too).
        gamepad_stick_gated = false;
    }

    for (int button = 0; button < GAMEPAD_BUTTON_COUNT; button++) {
        bool down = gamepad_state.buttons[button];
        if (down && !gamepad_previous_buttons[button]) {
            if (gamepad_gameplay_mode) {
                gamepad_pending_presses[button] = true;
                gamepad_hold_valid[button] = true;
            } else {
                gamepad_handle_menu_press(button);
            }
        }

        if (!down) {
            gamepad_hold_valid[button] = false;
        }

        gamepad_previous_buttons[button] = down;
    }

    if (!gamepad_gameplay_mode) {
        gamepad_handle_menu_repeat(now);
    } else {
        gamepad_repeat_button = -1;
    }

    // Virtual mouse: menus only. In gameplay the sticks belong to movement.
    if (!gamepad_gameplay_mode) {
        float cx = gamepad_state.leftX;
        float cy = gamepad_state.leftY;
        if (cx == 0.0f && cy == 0.0f) {
            cx = gamepad_state.rightX;
            cy = gamepad_state.rightY;
        }

        float magnitude = sqrtf(cx * cx + cy * cy);
        if (magnitude > 0.0f) {
            // Quadratic response gives fine control near the center.
            float speed = GAMEPAD_CURSOR_SPEED * magnitude * magnitude * dt;
            gamepad_cursor_accum_x += cx / magnitude * speed;
            gamepad_cursor_accum_y += cy / magnitude * speed;
        }

        int dx = (int)gamepad_cursor_accum_x;
        int dy = (int)gamepad_cursor_accum_y;
        gamepad_cursor_accum_x -= dx;
        gamepad_cursor_accum_y -= dy;
        gamepad_mouse_dx += dx;
        gamepad_mouse_dy += dy;

        gamepad_mouse_left = gamepad_state.buttons[GAMEPAD_BUTTON_A];
        gamepad_mouse_right = gamepad_state.buttons[GAMEPAD_BUTTON_X];
    } else {
        // Latched at poll time so mouse reads outside `get_input` agree.
        gamepad_mouse_left = false;
        gamepad_mouse_right = false;
    }
}

static void gamepad_handle_menu_press(int button)
{
    switch (button) {
    case GAMEPAD_BUTTON_B:
    case GAMEPAD_BUTTON_BACK:
        GNW_add_input_buffer(KEY_ESCAPE);
        break;
    case GAMEPAD_BUTTON_START:
        GNW_add_input_buffer(KEY_RETURN);
        break;
    case GAMEPAD_BUTTON_LEFT_SHOULDER:
        GNW_add_input_buffer(KEY_PAGE_UP);
        break;
    case GAMEPAD_BUTTON_RIGHT_SHOULDER:
        GNW_add_input_buffer(KEY_PAGE_DOWN);
        break;
    case GAMEPAD_BUTTON_DPAD_UP:
    case GAMEPAD_BUTTON_DPAD_DOWN:
    case GAMEPAD_BUTTON_DPAD_LEFT:
    case GAMEPAD_BUTTON_DPAD_RIGHT:
        gamepad_repeat_button = button;
        gamepad_repeat_next = SDL_GetTicks() + GAMEPAD_REPEAT_DELAY;
        gamepad_menu_navigate(button);
        break;
    }
}

static void gamepad_handle_menu_repeat(unsigned int now)
{
    if (gamepad_repeat_button == -1) {
        return;
    }

    if (!gamepad_state.buttons[gamepad_repeat_button]) {
        gamepad_repeat_button = -1;
        return;
    }

    if (now < gamepad_repeat_next) {
        return;
    }

    gamepad_repeat_next = now + GAMEPAD_REPEAT_RATE;
    gamepad_menu_navigate(gamepad_repeat_button);
}

// Moves the cursor to the nearest button in the D-pad direction. Screens
// without a button in that direction (lists, the world map) get arrow keys.
static void gamepad_menu_navigate(int button)
{
    int dirX = 0;
    int dirY = 0;
    int key;
    switch (button) {
    case GAMEPAD_BUTTON_DPAD_UP:
        dirY = -1;
        key = KEY_ARROW_UP;
        break;
    case GAMEPAD_BUTTON_DPAD_DOWN:
        dirY = 1;
        key = KEY_ARROW_DOWN;
        break;
    case GAMEPAD_BUTTON_DPAD_LEFT:
        dirX = -1;
        key = KEY_ARROW_LEFT;
        break;
    default:
        dirX = 1;
        key = KEY_ARROW_RIGHT;
        break;
    }

    int xs[GAMEPAD_NAV_POINTS_CAPACITY];
    int ys[GAMEPAD_NAV_POINTS_CAPACITY];
    int count = win_get_nav_points(xs, ys, GAMEPAD_NAV_POINTS_CAPACITY);

    int mouseX;
    int mouseY;
    mouse_get_position(&mouseX, &mouseY);

    int best = -1;
    int bestScore = 0;
    for (int index = 0; index < count; index++) {
        int vx = xs[index] - mouseX;
        int vy = ys[index] - mouseY;
        int along = vx * dirX + vy * dirY;
        if (along <= 4) {
            continue;
        }

        int across = abs(dirX != 0 ? vy : vx);
        int score = along + across * 2;
        if (best == -1 || score < bestScore) {
            best = index;
            bestScore = score;
        }
    }

    if (best != -1) {
        mouse_set_position(xs[best], ys[best]);
    } else {
        GNW_add_input_buffer(key);
    }
}

void gamepad_set_gameplay_mode(bool enabled)
{
    gamepad_gameplay_mode = enabled;
}

bool gamepad_is_gameplay_mode()
{
    return gamepad_gameplay_mode;
}

void gamepad_set_focus(bool focused)
{
    if (gamepad_focused && !focused) {
        gamepad_interruptions++;
        gamepad_interrupt_gameplay_input();
    } else if (!gamepad_focused && focused) {
        gamepad_suppress_held = true;
    }
    gamepad_focused = focused;
}

const GamepadState* gamepad_get_state()
{
    return &gamepad_state;
}

bool gamepad_is_connected()
{
    return gamepad_state.connected;
}

bool gamepad_consume_press(GamepadButton button)
{
    if (gamepad_pending_presses[button]) {
        gamepad_pending_presses[button] = false;
        return true;
    }
    return false;
}

bool gamepad_is_held(GamepadButton button)
{
    return gamepad_state.buttons[button] && gamepad_hold_valid[button];
}

void gamepad_get_move_stick(float* x, float* y)
{
    if (gamepad_stick_gated) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }

    *x = gamepad_state.leftX;
    *y = gamepad_state.leftY;
}

void gamepad_reset_gameplay_input()
{
    gamepad_interrupt_gameplay_input();
}

int gamepad_get_interruption_count()
{
    return gamepad_interruptions;
}

void gamepad_get_mouse_delta(int* dx, int* dy, bool* leftButton, bool* rightButton)
{
    *dx = gamepad_mouse_dx;
    *dy = gamepad_mouse_dy;
    gamepad_mouse_dx = 0;
    gamepad_mouse_dy = 0;

    *leftButton = gamepad_mouse_left;
    *rightButton = gamepad_mouse_right;
}

void gamepad_set_override(const GamepadState* state)
{
    gamepad_override_state = *state;
    gamepad_override_active = true;
}

void gamepad_clear_override()
{
    gamepad_override_active = false;
}

void gamepad_set_hook(GamepadHook* hook)
{
    gamepad_hook = hook;
}

const char* gamepad_get_name()
{
    return gamepad_name;
}

int gamepad_get_detected_count()
{
    return gamepad_detected_count;
}

} // namespace fallout
