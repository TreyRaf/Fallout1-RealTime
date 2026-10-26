#ifndef FALLOUT_PLIB_GNW_GAMEPAD_H_
#define FALLOUT_PLIB_GNW_GAMEPAD_H_

#include "plib/gnw/rect.h"

struct SDL_Renderer;

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
// Stick tabs (see `gamepad_set_stick_tabs`): one per deliberate horizontal
// push of the left stick (the Pip-Boy's subcategories).
#define GAMEPAD_KEY_LS_LEFT 0x7007
#define GAMEPAD_KEY_LS_RIGHT 0x7008
void gamepad_set_menu_key_mode(bool enabled);
bool gamepad_is_menu_key_mode();

// Sets the menu key mode for a scope and restores the previous mode after.
class GamepadKeyModeScope {
public:
    explicit GamepadKeyModeScope(bool enabled)
        : previous(gamepad_is_menu_key_mode())
    {
        gamepad_set_menu_key_mode(enabled);
    }
    ~GamepadKeyModeScope() { gamepad_set_menu_key_mode(previous); }

private:
    bool previous;
};

// Menu key mode: while enabled, a horizontal push of the left stick gives
// one GAMEPAD_KEY_LS_LEFT/RIGHT instead of arrow keys (the Pip-Boy's
// subcategories; its main tabs are on LT/RT).
// The push must start near the center and stay within ~27 degrees of
// horizontal; the stick has to come back near the center before the next
// one counts. A push that starts vertically or diagonally is scrolling
// (up/down arrows with repeat, or nothing) until the stick is centered.
// The D-pad keeps its arrow keys.
// Any `gamepad_set_menu_key_mode` call turns stick tabs off.
void gamepad_set_stick_tabs(bool enabled);
bool gamepad_is_stick_tabs();
bool gamepad_is_gameplay_mode();

// Window focus changes (from the SDL event loop).
void gamepad_set_focus(bool focused);
bool gamepad_is_focused();

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
// Radial deadzone of the right stick (0..1); the stick reads 0 inside it and
// is rescaled to 0..1 outside.
void gamepad_set_look_deadzone(float deadzone);

// Drops pending presses and holds and gates the stick (e.g. on map load).
void gamepad_reset_gameplay_input();

// Number of focus losses and controller disconnects seen (diagnostics).
int gamepad_get_interruption_count();
// How many times input was read outside gameplay (a menu, dialogue or another
// modal screen was open) so far.
int gamepad_get_modal_poll_count();

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

// Button menus (virtual mouse context): the D-pad and the left stick move
// the cursor from button to button, A clicks, B is Escape. While the
// controller drives a menu (until the mouse moves) the button under the
// cursor gets a selection frame, drawn by `renderPresent`, and a newly
// opened screen puts the cursor on its nearest button.
bool gamepad_get_menu_highlight(Rect* rect);
void gamepad_render_menu_highlight(SDL_Renderer* renderer);
// The physical mouse moved (the mouse drives the menus again).
void gamepad_note_mouse_moved();
// The cursor was moved onto a button by the program (navigation): A's
// click waits until the window system has caught up.
void gamepad_note_cursor_jump();
// A choice screen whose entries must never be picked by accident (dialogue
// responses). While the controller drives the menus, nothing is selected:
// the cursor is parked at (parkX, parkY), no frame is drawn and A does
// nothing until the D-pad or the left stick moves (which selects the entry
// at (firstX, firstY)) or the right stick moves the cursor. In every case
// an A press already under way, or made before the selection, doesn't
// click: A must be released and pressed again. Call it for every new set
// of entries; it ends with the screen (`gamepad_end_unselected_choice`),
// on any selection, when the mouse moves and in gameplay.
void gamepad_begin_unselected_choice(int parkX, int parkY, int firstX, int firstY);
void gamepad_end_unselected_choice();
bool gamepad_is_choice_unselected();
// Pointer screens (the world map): the left stick moves the cursor as the
// right stick does. Returns the previous setting.
bool gamepad_set_menu_pointer(bool enabled);
// A screen's own navigation (lists of adjustable controls): for a D-pad or
// stick move in (dirX, dirY) from the cursor at (x, y), in screen
// coordinates, the hook returns GAMEPAD_NAV_DEFAULT (nearest button in that
// direction), GAMEPAD_NAV_MOVE with (*outX, *outY) set (the cursor goes
// there) or GAMEPAD_NAV_KEY (the screen gets the arrow key, e.g. to change
// the selected slider). NULL removes it.
#define GAMEPAD_NAV_DEFAULT 0
#define GAMEPAD_NAV_MOVE 1
#define GAMEPAD_NAV_KEY 2
typedef int(GamepadMenuNavHook)(int dirX, int dirY, int x, int y, int* outX, int* outY);
void gamepad_set_menu_nav_hook(GamepadMenuNavHook* hook);
// True once if the last Escape came from B (or Back) within half a second,
// for screens where B must not do what the Escape key does.
bool gamepad_consume_menu_back();

const char* gamepad_get_name();
int gamepad_get_detected_count();

} // namespace fallout

#endif /* FALLOUT_PLIB_GNW_GAMEPAD_H_ */
