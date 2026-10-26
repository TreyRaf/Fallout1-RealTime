#ifndef FALLOUT_GAME_LOADSAVE_H_
#define FALLOUT_GAME_LOADSAVE_H_

#include "game/art.h"
#include "game/message.h"
#include "plib/db/db.h"
#include "plib/gnw/rect.h"

namespace fallout {

typedef enum LoadSaveMode {
    // Special case - loading game from main menu.
    LOAD_SAVE_MODE_FROM_MAIN_MENU,

    // Normal (full-screen) save/load screen.
    LOAD_SAVE_MODE_NORMAL,

    // Quick load/save.
    LOAD_SAVE_MODE_QUICK,
} LoadSaveMode;

void InitLoadSave();
void ResetLoadSave();
int SaveGame(int mode);
int LoadGame(int mode);
int isLoadingGame();
// RT: saves into `slot` (0-9) with `description`, without any screen (as a
// quick save does). A slot holding a save with another description is left
// alone (returns -2). 0 on success, -1 on failure.
int lsg_rt_autosave(int slot, const char* description);
// RT: loads `slot` (0-9) without any screen. 0 on success.
int lsg_rt_load(int slot);
void KillOldMaps();
int MapDirErase(const char* path, const char* a2);
int MapDirEraseFile(const char* a1, const char* a2);

} // namespace fallout

#endif /* FALLOUT_GAME_LOADSAVE_H_ */
