#ifndef FALLOUT_GAME_EDITOR_H_
#define FALLOUT_GAME_EDITOR_H_

#include "plib/db/db.h"

namespace fallout {

extern int character_points;

int editor_design(bool isCreationMode);
void CharEditInit();
int get_input_str(int win, int cancelKeyCode, char* text, int maxLength, int x, int y, int textColor, int backgroundColor, int flags);
bool isdoschar(int ch);
char* strmfe(char* dest, const char* name, const char* ext);
bool db_access(const char* fname);
char* AddSpaces(char* string, int length);
char* itostndn(int value, char* dest);
int editor_save(DB_FILE* stream);
int editor_load(DB_FILE* stream);

// RT: character progression without the character screen (unified Pip-Boy).
void editor_rt_update_level();
bool editor_rt_has_free_perk();
int editor_rt_free_perks();
void editor_rt_debug_get(int* lastLevel, int* freePerks);
void editor_rt_debug_set(int lastLevel, int freePerks);
int editor_rt_debug_convert(int lastLevel, int storedPending);
int editor_rt_take_perk(int perk, int choiceA, int choiceB);
void editor_reset();
void RedrwDMPrk();

} // namespace fallout

#endif /* FALLOUT_GAME_EDITOR_H_ */
