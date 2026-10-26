#ifndef FALLOUT_GAME_OPTIONS_H_
#define FALLOUT_GAME_OPTIONS_H_

#include "plib/db/db.h"

namespace fallout {

int do_options();
int PauseWindow(bool is_world_map);
int init_options_menu();
int save_options(DB_FILE* stream);
int load_options(DB_FILE* stream);
void IncGamma();
void DecGamma();

// RT: automated tests. The options menu and the preferences are open, and
// a preference's value (switch position or slider value).
bool options_rt_is_menu_open();
bool options_rt_is_prefs_open();
int options_rt_debug_pref(int index);

} // namespace fallout

#endif /* FALLOUT_GAME_OPTIONS_H_ */
