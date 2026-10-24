#ifndef FALLOUT_GAME_RT_CONTEXT_H_
#define FALLOUT_GAME_RT_CONTEXT_H_

#include <stddef.h>

#include "game/object_types.h"
#include "game/skill_defs.h"

namespace fallout {

// RT: Contextual interaction (the A button).
//
// Picks the ordinary interaction (talk, use, open, pick up, loot) and/or the
// skill actions that apply to an object, so the Skilldex isn't needed:
// - Whatever skills the object's own script reacts to in its
//   `use_skill_on_p_proc` (read from the compiled script, see below). This
//   covers Lockpick, Traps, Science and Repair: Fallout 1 implements those
//   only in object scripts. Lockpick is offered on locked doors and
//   containers (next to Open, which runs the door's own lock logic) and on
//   objects the engine can't lock; not on unlocked doors and containers.
// Steal, First Aid and Doctor are never offered (they stay in the Skilldex).
// When more than one action applies, a small controller menu asks which one.
//
// Script reading: the compiled script's `use_skill_on_p_proc` is scanned for
// `action_being_used == SKILL_X` comparisons (the skills it handles), for
// `has_skill(who, SKILL_X) >= N` (a fixed requirement) and for
// `roll_vs_skill`/`skill_contest` (a probabilistic roll). Checks made in
// other procedures, or written in other forms, are not recognized.

typedef struct RtSkillInfo {
    // The object's script reacts to this skill.
    bool handled;
    // The script rolls against the skill (success isn't guaranteed).
    bool roll;
    // Minimum skill level the script checks for, or -1 when none is known.
    int requirement;
} RtSkillInfo;

typedef struct RtScriptSkills {
    bool hasProc;
    // `action_being_used` is read in a way the scan doesn't understand.
    bool unknownForm;
    RtSkillInfo skills[SKILL_COUNT];
} RtScriptSkills;

typedef struct RtContextStats {
    int interactions;
    int defaultActions;
    int skillUses;
    int skillBlocked;
    int menus;
    int menuCancels;
} RtContextStats;

// True if pressing A on `obj` would do something.
bool rt_context_is_interactable(Object* obj);

// Performs the contextual interaction (may open the choice menu).
void rt_context_interact(Object* obj);

// Script analysis for the object's script (cached); false if it has none.
bool rt_context_get_script_skills(Object* obj, RtScriptSkills* out);
bool rt_context_scan_script_index(int scriptIndex, RtScriptSkills* out);
int rt_context_dump_opcodes(int scriptIndex, const int* opcodes, int opcodeCount);

// Lists the actions A would offer for `obj`, e.g. "Open/Close | Lockpick".
void rt_context_describe(Object* obj, char* buffer, size_t size);

const RtContextStats* rt_context_get_stats();

// Automated tests: picks this menu entry instead of waiting for input
// (-1 cancels, -2 disables the override).
void rt_context_set_menu_choice(int choice);

// Automated tests: replaces the analysis of a script (NULL restores it).
void rt_context_debug_override(int scriptIndex, const RtScriptSkills* info);

} // namespace fallout

#endif /* FALLOUT_GAME_RT_CONTEXT_H_ */
