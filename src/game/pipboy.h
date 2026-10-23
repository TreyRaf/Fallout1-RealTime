#ifndef FALLOUT_GAME_PIPBOY_H_
#define FALLOUT_GAME_PIPBOY_H_

#include "game/art.h"
#include "game/message.h"
#include "plib/db/db.h"
#include "plib/gnw/rect.h"

namespace fallout {

typedef enum PipboyOpenIntent {
    PIPBOY_OPEN_INTENT_UNSPECIFIED = 0,
    PIPBOY_OPEN_INTENT_REST = 1,
} PipboyOpenIntent;

typedef void(PipboyRenderProc)(int a1);

int pipboy(int intent);
void pip_init();
int save_pipboy(DB_FILE* stream);
int load_pipboy(DB_FILE* stream);

// RT: Pieces of the original Pip-Boy used by the unified Pip-Boy
// (rt_pipboy.cc): its window, artwork, messages, clock, quest and holodisk
// tables and rest logic.
#define PIPBOY_RT_WINDOW_WIDTH 640
#define PIPBOY_RT_WINDOW_HEIGHT 480
#define PIPBOY_RT_SCREEN_X 254
#define PIPBOY_RT_SCREEN_Y 46
#define PIPBOY_RT_SCREEN_WIDTH 350
#define PIPBOY_RT_SCREEN_HEIGHT 410
#define PIPBOY_RT_REST_OPTION_COUNT 13

int pipboy_rt_begin(int* window, unsigned char** buffer, unsigned char** background);
void pipboy_rt_end();
void pipboy_rt_draw_clock();
const char* pipboy_rt_getmsg(int id);
int pipboy_rt_quest_location_count();
int pipboy_rt_quests_per_location();
int pipboy_rt_quest_gvar(int location, int quest);
int pipboy_rt_holodisk_count();
int pipboy_rt_holodisk_gvar(int holodisk);
bool pipboy_rt_can_rest();
// Runs rest option 0..12 (messages 302.., as the original alarm clock);
// returns true when an event interrupted it and the Pip-Boy must close.
bool pipboy_rt_rest(int option);
bool pipboy_rt_get_frame(int frm, unsigned char** data, int* width, int* height);
#define PIPBOY_RT_FRM_LITTLE_RED_BUTTON_UP 0
#define PIPBOY_RT_FRM_LITTLE_RED_BUTTON_DOWN 1
#define PIPBOY_RT_FRM_LOGO 9

} // namespace fallout

#endif /* FALLOUT_GAME_PIPBOY_H_ */
