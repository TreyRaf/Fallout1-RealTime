#ifndef FALLOUT_PLIB_GNW_GAMEPAD_H_
#define FALLOUT_PLIB_GNW_GAMEPAD_H_

namespace fallout {

// RT: Game controller support (SDL GameController API).
//
// Two input contexts:
// - Menu context (default): the sticks drive a virtual mouse cursor, the
//   D-pad jumps the cursor between buttons, A/X are left/right mouse buttons
//   and B/Start/Back/LB/RB become keys, so every vanilla UI is usable.
// - Gameplay context: set by the main game loop around its `get_input` call.
//   Nothing is translated; `realtime.cc` reads presses and holds directly.
//
// Gameplay input is guarded against leaking across contexts: a button only
// counts as held if it was pressed in gameplay and not interrupted (menu,
// focus loss, disconnect) since, and the left stick must return to neutral
// after a menu before it moves the player again.

typedef enum GamepadButton {
    GAMEPAD_BUTTON_A,
    GAMEPAD_BUTTON_B,
    GAMEPAD_BUTTON_X,
    GAMEPAD_BUTTON_Y,
    GAMEPAD_BUTTON_BACK,
    GAMEPAD_BUTTON_START,
    GAMEPAD_BUTTON_LEFT_STICK,
    GAMEPAD_BUTTON_RIGHT_STICK,
    GAMEPAD_BUTTON_LEFT_SHOULDER,
    GAMEPAD_BUTTON_RIGHT_SHOULDER,
    GAMEPAD_BUTTON_DPAD_UP,
    GAMEPAD_BUTTON_DPAD_DOWN,
    GAMEPAD_BUTTON_DPAD_LEFT,
    GAMEPAD_BUTTON_DPAD_RIGHT,
    // Triggers are exposed as digital buttons too.
    GAMEPAD_BUTTON_LEFT_TRIGGER,
    GAMEPAD_BUTTON_RIGHT_TRIGGER,
    GAMEPAD_BUTTON_COUNT,
} GamepadButton;

typedef struct GamepadState {
    bool connected;
    // Normalized to [-1, 1], radial deadzone applied. Y axis points down.
    float leftX;
    float leftY;
    float rightX;
    float rightY;
    // Normalized to [0, 1].
    float leftTrigger;
    float rightTrigger;
    bool buttons[GAMEPAD_BUTTON_COUNT];
} GamepadState;

void gamepad_init();
void gamepad_exit();

// Polls the device (called from `GNW95_process_message`).
void gamepad_update();

void gamepad_set_gameplay_mode(bool enabled);

// Menu key mode: while enabled (outside gameplay), the controller drives a
// screen with keys instead of the virtual mouse: A = Enter, B = Escape,
// LB/RB = Page Up/Down, D-pad and left stick = arrow keys (with repeat), and
// X, Y, LT, RT, Start, Back = the GAMEPAD_KEY_* codes below. Used by the
// Pip-Boy.
#define GAMEPAD_KEY_X 0x7001
#define GAMEPAD_KEY_Y 0x7002
#define GAMEPAD_KEY_LT 0x7003
#define GAMEPAD_KEY_RT 0x7004
#define GAMEPAD_KEY_START 0x7005
#define GAMEPAD_KEY_BACK 0x7006
void gamepad_set_menu_key_mode(bool enabled);
bool gamepad_is_menu_key_mode();
bool gamepad_is_gameplay_mode();

// Window focus changes (from the SDL event loop).
void gamepad_set_focus(bool focused);

// Current (physical or overridden) state.
const GamepadState* gamepad_get_state();
bool gamepad_is_connected();

// Returns true once for every press of `button` that happened in the
// gameplay context and has not been consumed yet.
bool gamepad_consume_press(GamepadButton button);

// True while `button` is held since a gameplay press, without interruption.
bool gamepad_is_held(GamepadButton button);

// Left stick for gameplay movement; zero until the stick returns to neutral
// after a menu, focus loss or reconnect.
void gamepad_get_move_stick(float* x, float* y);

// Returns true once per right stick flick in the gameplay context, with the
// unit direction (Y points down). The stick must return to neutral between
// flicks; menus, focus loss and disconnects drop pending flicks.
bool gamepad_consume_flick(float* x, float* y);

// Menu key mode: the same flicks, for a screen that switches targets (the
// aimed shot screen). The stick must be neutral after the mode starts.
bool gamepad_consume_menu_flick(float* x, float* y);

// Right stick for camera panning; zero until it returns to neutral after a
// menu, focus loss or reconnect.
void gamepad_get_look_stick(float* x, float* y);

// Drops pending presses and holds and gates the stick (e.g. on map load).
void gamepad_reset_gameplay_input();

// Number of focus losses and controller disconnects seen (diagnostics).
int gamepad_get_interruption_count();

// Virtual mouse contribution, consumed by `dxinput_get_mouse_state`.
void gamepad_get_mouse_delta(int* dx, int* dy, bool* leftButton, bool* rightButton);

// Lets an automated test drive the gamepad. While an override is active the
// physical device is ignored; `connected` in the override is honored, so
// disconnects can be simulated.
void gamepad_set_override(const GamepadState* state);
void gamepad_clear_override();

// Called at the start of every poll; lets an automated test drive menus.
typedef void(GamepadHook)();
void gamepad_set_hook(GamepadHook* hook);

const char* gamepad_get_name();
int gamepad_get_detected_count();

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_GAMEPAD_H_ */
