#include "game/rt_context.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include "game/actions.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/display.h"
#include "game/item.h"
#include "game/object.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/game.h"
#include "game/map.h"
#include "game/realtime.h"
#include "game/skilldex.h"
#include "game/tile.h"
#include "game/scripts.h"
#include "game/skill.h"
#include "game/stat.h"
#include "platform_compat.h"
#include "plib/color/color.h"
#include "plib/db/db.h"
#include "plib/gnw/button.h"
#include "plib/gnw/gamepad.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

// Script opcodes (see `intextra.cc`).
#define RT_OP_HAS_SKILL 0x80AA
#define RT_OP_ROLL_VS_SKILL 0x80AC
#define RT_OP_SKILL_CONTEST 0x80AD
#define RT_OP_ACTION_BEING_USED 0x80FA
#define RT_OP_DUDE_OBJ 0x80BF
#define RT_OP_SELF_OBJ 0x80BC
#define RT_OP_STORE 0x8031
#define RT_OP_FETCH 0x8032
#define RT_OP_EQUAL 0x8033
#define RT_OP_NOT_EQUAL 0x8034
#define RT_OP_GREATER_THAN_EQUAL 0x8036
#define RT_OP_GREATER_THAN 0x8038
#define RT_VALUE_INT 0xC001

// Offsets in the compiled script (.int) format (see `allocateProgram`).
#define RT_INT_PROCEDURES_OFFSET 42
#define RT_INT_PROCEDURE_SIZE 24
#define RT_INT_PROCEDURE_BODY 16

#define RT_ACTION_DEFAULT 0
#define RT_ACTION_SKILL 1
#define RT_MAX_ACTIONS 8

typedef struct RtAction {
    int kind;
    int skill;
    char label[48];
} RtAction;

typedef struct RtInstruction {
    int opcode;
    int value;
} RtInstruction;

static int rt_context_collect(Object* obj, RtAction* actions, RtScriptSkills* scriptSkills);
static bool rt_context_default_label(Object* obj, char* label, size_t size);
static void rt_context_default(Object* obj);
static void rt_context_use_skill(Object* obj, int skill, const RtScriptSkills* scriptSkills);
static int rt_context_menu(const char* title, const RtAction* actions, int count);
static void rt_context_print(const char* format, ...);

static std::unordered_map<int, RtScriptSkills> rt_script_cache;
static RtContextStats rt_context_stats;
static int rt_context_menu_override = -2;

const RtContextStats* rt_context_get_stats()
{
    return &rt_context_stats;
}

void rt_context_set_menu_choice(int choice)
{
    rt_context_menu_override = choice;
}

void rt_context_debug_override(int scriptIndex, const RtScriptSkills* info)
{
    if (info != NULL) {
        rt_script_cache[scriptIndex] = *info;
    } else {
        rt_script_cache.erase(scriptIndex);
    }
}

static void rt_context_print(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    display_print(buffer);
    rt_log("ui: %s", buffer);
}

// -----------------------------------------------------------------------------
// Script analysis
// -----------------------------------------------------------------------------

static int rt_be16(const unsigned char* data)
{
    return (data[0] << 8) | data[1];
}

static int rt_be32(const unsigned char* data)
{
    return (int)(((unsigned int)data[0] << 24) | ((unsigned int)data[1] << 16) | ((unsigned int)data[2] << 8) | data[3]);
}

static bool rt_is_skill_value(const RtInstruction& instruction)
{
    return instruction.opcode == RT_VALUE_INT && instruction.value >= 0 && instruction.value < SKILL_COUNT;
}

static bool rt_is_int(const RtInstruction& instruction)
{
    return instruction.opcode == RT_VALUE_INT;
}

// The skill argument of has_skill/roll_vs_skill/skill_contest: the value
// pushed right after the (last) critter argument, usually dude_obj.
// `fromAction` is set when the script passes the skill being used.
static int rt_skill_argument(const std::vector<RtInstruction>& code, int callIndex, const std::vector<int>& actionVars, bool* fromAction)
{
    *fromAction = false;
    for (int whoIndex = callIndex - 2; whoIndex >= 0 && whoIndex >= callIndex - 6; whoIndex--) {
        int opcode = code[whoIndex].opcode;
        if (opcode != RT_OP_DUDE_OBJ && opcode != RT_OP_SELF_OBJ) {
            continue;
        }

        const RtInstruction& argument = code[whoIndex + 1];
        if (rt_is_skill_value(argument)) {
            return argument.value;
        }

        if (argument.opcode == RT_OP_ACTION_BEING_USED) {
            *fromAction = true;
        } else if (rt_is_int(argument) && whoIndex + 2 < callIndex && code[whoIndex + 2].opcode == RT_OP_FETCH
            && std::find(actionVars.begin(), actionVars.end(), argument.value) != actionVars.end()) {
            *fromAction = true;
        }
        return -1;
    }
    return -1;
}

static void rt_scan_body(const std::vector<RtInstruction>& code, RtScriptSkills* out)
{
    bool readsAction = false;
    bool understood = false;
    int count = (int)code.size();

    // Variables the skill being used is stored in: action_being_used, push
    // variable number, store.
    std::vector<int> actionVars;
    for (int index = 0; index + 2 < count; index++) {
        if (code[index].opcode == RT_OP_ACTION_BEING_USED && rt_is_int(code[index + 1]) && code[index + 2].opcode == RT_OP_STORE) {
            actionVars.push_back(code[index + 1].value);
        }
    }

    for (int index = 0; index < count; index++) {
        int opcode = code[index].opcode;

        // The skill being used, read directly or from its variable.
        bool actionValue = opcode == RT_OP_ACTION_BEING_USED;
        int valueEnd = index;
        if (!actionValue && rt_is_int(code[index]) && index + 1 < count && code[index + 1].opcode == RT_OP_FETCH
            && std::find(actionVars.begin(), actionVars.end(), code[index].value) != actionVars.end()) {
            actionValue = true;
            valueEnd = index + 1;
        }

        if (actionValue) {
            readsAction = true;

            // (skill being used) == SKILL_X, either operand order.
            int skill = -1;
            if (valueEnd + 2 < count && rt_is_skill_value(code[valueEnd + 1])
                && (code[valueEnd + 2].opcode == RT_OP_EQUAL || code[valueEnd + 2].opcode == RT_OP_NOT_EQUAL)) {
                skill = code[valueEnd + 1].value;
            } else if (index >= 1 && valueEnd + 1 < count && rt_is_skill_value(code[index - 1])
                && (code[valueEnd + 1].opcode == RT_OP_EQUAL || code[valueEnd + 1].opcode == RT_OP_NOT_EQUAL)) {
                skill = code[index - 1].value;
            }

            if (skill != -1) {
                out->skills[skill].handled = true;
                understood = true;
            } else if (valueEnd + 2 < count && code[valueEnd + 1].opcode == RT_VALUE_INT && code[valueEnd + 2].opcode == RT_OP_STORE) {
                // Stored into a variable; the comparisons are read above.
                understood = understood || !actionVars.empty();
            }
            continue;
        }

        bool fromAction;
        if (opcode == RT_OP_HAS_SKILL) {
            // has_skill(who, SKILL_X) >= N  /  > N
            int skill = rt_skill_argument(code, index, actionVars, &fromAction);
            if (skill != -1 && index + 2 < count && rt_is_int(code[index + 1])) {
                int threshold = -1;
                if (code[index + 2].opcode == RT_OP_GREATER_THAN_EQUAL) {
                    threshold = code[index + 1].value;
                } else if (code[index + 2].opcode == RT_OP_GREATER_THAN) {
                    threshold = code[index + 1].value + 1;
                }

                if (threshold > 0) {
                    RtSkillInfo* info = &(out->skills[skill]);
                    if (info->requirement == -1 || threshold < info->requirement) {
                        info->requirement = threshold;
                    }
                }
            }
        } else if (opcode == RT_OP_ROLL_VS_SKILL || opcode == RT_OP_SKILL_CONTEST) {
            int skill = rt_skill_argument(code, index, actionVars, &fromAction);
            if (skill != -1) {
                out->skills[skill].roll = true;
            } else {
                // The roll is against the skill being used (or can't be
                // read): every handled skill rolls.
                for (int other = 0; other < SKILL_COUNT; other++) {
                    out->skills[other].roll = true;
                }
            }
        }
    }

    out->unknownForm = readsAction && !understood;
}

static bool rt_scan_script_file(const char* path, RtScriptSkills* out)
{
    DB_FILE* stream = db_fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }

    long size = db_filelength(stream);
    std::vector<unsigned char> data(size > 0 ? size : 0);
    if (size > 0) {
        db_fread(data.data(), 1, size, stream);
    }
    db_fclose(stream);

    if (size < RT_INT_PROCEDURES_OFFSET + 4) {
        return false;
    }

    const unsigned char* procedures = data.data() + RT_INT_PROCEDURES_OFFSET;
    int procedureCount = rt_be32(procedures);
    long identifiersOffset = RT_INT_PROCEDURES_OFFSET + 4 + (long)procedureCount * RT_INT_PROCEDURE_SIZE;
    if (procedureCount <= 0 || identifiersOffset + 4 > size) {
        return false;
    }

    const unsigned char* identifiers = data.data() + identifiersOffset;
    int body = -1;
    for (int index = 0; index < procedureCount; index++) {
        const unsigned char* procedure = procedures + 4 + index * RT_INT_PROCEDURE_SIZE;
        long nameOffset = rt_be32(procedure);
        if (identifiersOffset + nameOffset >= size) {
            continue;
        }

        const char* name = (const char*)(identifiers + nameOffset);
        if (SDL_strncasecmp(name, "use_skill_on_p_proc", 20) == 0) {
            body = rt_be32(procedure + RT_INT_PROCEDURE_BODY);
            break;
        }
    }

    if (body <= 0 || body >= size) {
        return true;
    }

    out->hasProc = true;

    // The procedure ends where the next one in the file begins.
    long end = size;
    for (int index = 0; index < procedureCount; index++) {
        int other = rt_be32(procedures + 4 + index * RT_INT_PROCEDURE_SIZE + RT_INT_PROCEDURE_BODY);
        if (other > body && other < end) {
            end = other;
        }
    }

    std::vector<RtInstruction> code;
    long pos = body;
    while (pos + 2 <= end) {
        RtInstruction instruction;
        instruction.opcode = rt_be16(data.data() + pos);
        instruction.value = 0;
        pos += 2;

        if ((instruction.opcode & 0x8000) == 0) {
            break;
        }

        // Values (int, float, strings) carry a 4-byte operand.
        if ((instruction.opcode & 0xF000) != 0x8000) {
            if (pos + 4 > end) {
                break;
            }
            instruction.value = rt_be32(data.data() + pos);
            pos += 4;
        }

        code.push_back(instruction);
    }

    // Diagnostics: RT_SCAN_DUMP=name1,name2 logs the procedure's bytecode.
    const char* dump = SDL_getenv("RT_SCAN_DUMP");
    if (dump != NULL) {
        const char* base = strrchr(path, 92);
        base = base != NULL ? base + 1 : path;
        char name[32];
        SDL_strlcpy(name, base, sizeof(name));
        char* dot = strchr(name, '.');
        if (dot != NULL) {
            *dot = 0;
        }
        if (SDL_strcasestr(dump, name) != NULL) {
            char line[1024];
            line[0] = 0;
            for (const RtInstruction& instruction : code) {
                char part[32];
                if ((instruction.opcode & 0xF000) != 0x8000) {
                    snprintf(part, sizeof(part), " %X:%d", instruction.opcode, instruction.value);
                } else {
                    snprintf(part, sizeof(part), " %X", instruction.opcode);
                }
                if (strlen(line) + strlen(part) >= sizeof(line) - 1) {
                    rt_log("scan dump %s:%s", name, line);
                    line[0] = 0;
                }
                SDL_strlcat(line, part, sizeof(line));
            }
            rt_log("scan dump %s:%s", name, line);
        }
    }

    rt_scan_body(code, out);
    return true;
}

bool rt_context_scan_script_index(int scriptIndex, RtScriptSkills* out)
{
    auto it = rt_script_cache.find(scriptIndex);
    if (it != rt_script_cache.end()) {
        *out = it->second;
        return true;
    }

    RtScriptSkills result;
    memset(&result, 0, sizeof(result));
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        result.skills[skill].requirement = -1;
    }

    char path[COMPAT_MAX_PATH];
    if (!scr_rt_script_path(scriptIndex, path, sizeof(path)) || !rt_scan_script_file(path, &result)) {
        return false;
    }

    rt_script_cache[scriptIndex] = result;
    *out = result;
    return true;
}

bool rt_context_get_script_skills(Object* obj, RtScriptSkills* out)
{
    if (obj->sid == -1) {
        return false;
    }

    Script* script;
    if (scr_ptr(obj->sid, &script) == -1) {
        return false;
    }

    return rt_context_scan_script_index(script->scr_script_idx, out);
}

// -----------------------------------------------------------------------------
// Actions
// -----------------------------------------------------------------------------

static bool rt_context_default_label(Object* obj, char* label, size_t size)
{
    switch (FID_TYPE(obj->fid)) {
    case OBJ_TYPE_ITEM:
        SDL_strlcpy(label, item_get_type(obj) == ITEM_TYPE_CONTAINER ? "Open" : "Pick up", size);
        return true;
    case OBJ_TYPE_CRITTER:
        if (critter_is_dead(obj)) {
            SDL_strlcpy(label, "Loot", size);
            return true;
        }
        if (obj_action_can_talk_to(obj) && !rt_is_engaged(obj)) {
            SDL_strlcpy(label, "Talk", size);
            return true;
        }
        return false;
    case OBJ_TYPE_SCENERY:
        if (proto_action_can_use(obj->pid)) {
            SDL_strlcpy(label, obj_is_a_portal(obj) ? "Open/Close" : "Use", size);
            return true;
        }
        return false;
    }

    return false;
}

// What A does with the skill, as shown before pressing it. Fallout 1's
// trap scripts (vault traps, trapped lockers, boxes and doors) all disarm;
// arming exists only for explosives, from the inventory.
static const char* rt_skill_verb(int skill)
{
    switch (skill) {
    case SKILL_TRAPS:
        return "Disarm trap";
    case SKILL_STEAL:
        return "Steal";
    case SKILL_REPAIR:
        return "Repair";
    }
    return skill_name(skill);
}

static void rt_context_add_skill(RtAction* actions, int* count, int skill)
{
    for (int index = 0; index < *count; index++) {
        if (actions[index].kind == RT_ACTION_SKILL && actions[index].skill == skill) {
            return;
        }
    }

    if (*count >= RT_MAX_ACTIONS) {
        return;
    }

    RtAction* action = &(actions[*count]);
    action->kind = RT_ACTION_SKILL;
    action->skill = skill;
    snprintf(action->label, sizeof(action->label), "%s (%s %d%%)", rt_skill_verb(skill), skill_name(skill), skill_level(obj_dude, skill));
    (*count)++;
}

// Whether A steals from `obj`: only while sneaking, from a living critter
// that isn't fighting the player's team.
static bool rt_context_can_steal_from(Object* obj)
{
    return is_pc_flag(PC_FLAG_SNEAKING)
        && FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER
        && obj != obj_dude
        && !critter_is_dead(obj)
        && !rt_is_engaged(obj)
        && !rt_is_hostile(obj);
}

static int rt_context_collect(Object* obj, RtAction* actions, RtScriptSkills* scriptSkills)
{
    int count = 0;

    if (rt_context_can_steal_from(obj)) {
        // Sneaking: A steals instead of talking.
        rt_context_add_skill(actions, &count, SKILL_STEAL);
    } else if (rt_context_default_label(obj, actions[0].label, sizeof(actions[0].label))) {
        actions[0].kind = RT_ACTION_DEFAULT;
        actions[0].skill = -1;
        count = 1;
    }

    bool hasScript = rt_context_get_script_skills(obj, scriptSkills);
    if (!hasScript) {
        memset(scriptSkills, 0, sizeof(*scriptSkills));
    }

    int type = FID_TYPE(obj->fid);
    bool aliveCritter = type == OBJ_TYPE_CRITTER && !critter_is_dead(obj);

    // Skills the object's script reacts to. Never Steal (nor First Aid or
    // Doctor: those stay in the Skilldex), and nothing on a critter that is
    // fighting.
    if (scriptSkills->hasProc && !(aliveCritter && rt_is_engaged(obj))) {
        static const int skills[] = {
            SKILL_LOCKPICK,
            SKILL_TRAPS,
            SKILL_SCIENCE,
            SKILL_REPAIR,
        };

        for (int skill : skills) {
            if (!scriptSkills->skills[skill].handled) {
                continue;
            }

            // No lock to pick on an unlocked door or container. (Opening
            // stays the first choice on a locked one: door scripts such as
            // Door.int keep their own "picked" state and only unlock the
            // door when it is used.)
            if (skill == SKILL_LOCKPICK && obj_is_lockable(obj) && !obj_is_locked(obj)) {
                continue;
            }

            rt_context_add_skill(actions, &count, skill);
        }
    }

    return count;
}

bool rt_context_is_interactable(Object* obj)
{
    RtAction actions[RT_MAX_ACTIONS];
    RtScriptSkills scriptSkills;
    return rt_context_collect(obj, actions, &scriptSkills) > 0;
}

void rt_context_describe(Object* obj, char* buffer, size_t size)
{
    RtAction actions[RT_MAX_ACTIONS];
    RtScriptSkills scriptSkills;
    int count = rt_context_collect(obj, actions, &scriptSkills);

    buffer[0] = '\0';
    for (int index = 0; index < count; index++) {
        if (index > 0) {
            SDL_strlcat(buffer, " | ", size);
        }
        SDL_strlcat(buffer, actions[index].kind == RT_ACTION_SKILL ? rt_skill_verb(actions[index].skill) : actions[index].label, size);
    }
}

static void rt_context_default(Object* obj)
{
    rt_context_stats.defaultActions++;

    switch (FID_TYPE(obj->fid)) {
    case OBJ_TYPE_ITEM:
        rt_log("interact: pick up/open %s", object_name(obj));
        action_get_an_object(obj_dude, obj);
        break;
    case OBJ_TYPE_CRITTER:
        if (!critter_is_dead(obj)) {
            rt_log("interact: talk to %s", critter_name(obj));
            action_talk_to(obj_dude, obj);
        } else {
            rt_log("interact: loot %s", critter_name(obj));
            action_loot_container(obj_dude, obj);
        }
        break;
    case OBJ_TYPE_SCENERY:
        rt_log("interact: use %s", object_name(obj));
        action_use_an_object(obj_dude, obj);
        break;
    }
}

// Runs the original skill use (the object's script, or the engine's own
// handling), after telling the player what kind of check it is.
static void rt_context_use_skill(Object* obj, int skill, const RtScriptSkills* scriptSkills)
{
    int level = skill_level(obj_dude, skill);
    const char* name = skill_name(skill);
    const RtSkillInfo* info = scriptSkills->hasProc && scriptSkills->skills[skill].handled
        ? &(scriptSkills->skills[skill])
        : NULL;

    if (skill == SKILL_STEAL) {
        // The original steal flow (the steal screen, with the original
        // checks per item, detection and the owner's reaction).
        rt_context_print("Steal %d%%: the chance per item is shown in the steal screen.", level);
    } else if (info != NULL && info->requirement > 0 && !info->roll) {
        if (level < info->requirement) {
            // A fixed threshold the script checks: below it the attempt
            // can't succeed, so it isn't made.
            rt_context_stats.skillBlocked++;
            rt_context_print("%s requires %s %d%%. Yours is %d%%.", object_name(obj), name, info->requirement, level);
            return;
        }

        rt_context_print("%s %d%% (needs %d%%).", name, level, info->requirement);
    } else if (info == NULL || info->roll) {
        // The engine's own Lockpick, First Aid and Doctor handling, and
        // scripts using roll_vs_skill, roll against the skill.
        if (info != NULL && info->requirement > 0) {
            rt_context_print("%s %d%% (needs %d%%): success depends on a skill roll.", name, level, info->requirement);
        } else {
            rt_context_print("%s %d%%: success depends on a skill roll.", name, level);
        }
    } else {
        rt_context_print("Using %s (%d%%).", name, level);
    }

    rt_context_stats.skillUses++;
    rt_log("interact: %s on %s (skill %d%%, requirement %d, roll %d)",
        name,
        FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER ? critter_name(obj) : object_name(obj),
        level,
        info != NULL ? info->requirement : -1,
        info != NULL ? info->roll : 1);
    action_use_skill_on(obj_dude, obj, skill);
}

void rt_context_interact(Object* obj)
{
    RtAction actions[RT_MAX_ACTIONS];
    RtScriptSkills scriptSkills;
    int count = rt_context_collect(obj, actions, &scriptSkills);
    if (count == 0) {
        rt_log("interact: nothing to do with %s", object_name(obj));
        return;
    }

    rt_context_stats.interactions++;

    int choice = 0;
    if (count > 1) {
        const char* title = FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER ? critter_name(obj) : object_name(obj);
        choice = rt_context_menu(title, actions, count);
        if (choice < 0) {
            rt_context_stats.menuCancels++;
            rt_log("interact: menu cancelled");
            return;
        }
    }

    if (actions[choice].kind == RT_ACTION_DEFAULT) {
        rt_context_default(obj);
    } else {
        rt_context_use_skill(obj, actions[choice].skill, &scriptSkills);
    }
}

// -----------------------------------------------------------------------------
// Choice menu
// -----------------------------------------------------------------------------

#define RT_MENU_ENTER_CODE 0x5100
#define RT_MENU_CHOOSE_CODE 0x5200
#define RT_MENU_PADDING 10

static void rt_context_menu_draw(int win, int width, int rowHeight, int titleHeight, const char* title, const RtAction* actions, int count, int selected)
{
    unsigned char* buffer = win_get_buf(win);
    int height = titleHeight + rowHeight * (count + 1) + RT_MENU_PADDING;

    buf_fill(buffer, width, height, width, colorTable[0]);
    draw_box(buffer, width, 0, 0, width - 1, height - 1, colorTable[992]);

    text_to_buf(buffer + width * 6 + (width - text_width(title)) / 2, title, width - 4, width, colorTable[32747]);

    for (int index = 0; index <= count; index++) {
        const char* label = index < count ? actions[index].label : "Cancel";
        int y = titleHeight + index * rowHeight;
        int color = index == selected ? colorTable[32747] : colorTable[992];
        if (index == selected) {
            text_to_buf(buffer + width * (y + 2) + RT_MENU_PADDING - 8, ">", 8, width, color);
        }
        text_to_buf(buffer + width * (y + 2) + RT_MENU_PADDING, label, width - RT_MENU_PADDING * 2, width, color);
    }

    win_draw(win);
}

// A small modal list: D-pad (or the left stick) moves between entries, A
// picks one, B cancels. The game is paused while it is open.
static int rt_context_menu(const char* title, const RtAction* actions, int count)
{
    rt_context_stats.menus++;
    rt_log("menu: %s (%d choices)", title, count);

    if (rt_context_menu_override != -2) {
        int choice = rt_context_menu_override;
        rt_log("menu: test override picks %d", choice);
        return choice >= 0 && choice < count ? choice : -1;
    }

    int oldFont = text_curr();
    text_font(101);

    int rowHeight = text_height() + 6;
    int titleHeight = text_height() + 14;
    int width = text_width(title) + RT_MENU_PADDING * 2;
    for (int index = 0; index < count; index++) {
        int labelWidth = text_width(actions[index].label) + RT_MENU_PADDING * 2;
        if (labelWidth > width) {
            width = labelWidth;
        }
    }
    if (width < 140) {
        width = 140;
    }

    int height = titleHeight + rowHeight * (count + 1) + RT_MENU_PADDING;
    int screenWidth = scr_size.lrx - scr_size.ulx + 1;
    int screenHeight = scr_size.lry - scr_size.uly + 1 - 100;
    int winX = (screenWidth - width) / 2;
    int winY = (screenHeight - height) / 2;
    int win = win_add(winX, winY, width, height, colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (win == -1) {
        text_font(oldFont);
        return -1;
    }

    // One button per row (and Cancel), so the controller's D-pad can jump
    // between them and A clicks; keys work too.
    for (int index = 0; index <= count; index++) {
        int y = titleHeight + index * rowHeight;
        win_register_button(win, 4, y, width - 8, rowHeight, RT_MENU_ENTER_CODE + index, -1, -1, RT_MENU_CHOOSE_CODE + index, NULL, NULL, NULL, 0);
    }

    // The cursor sits on the highlighted row, so A always picks what is
    // highlighted.
    int selected = 0;
    mouse_set_position(winX + width / 2, winY + titleHeight + rowHeight / 2);
    rt_context_menu_draw(win, width, rowHeight, titleHeight, title, actions, count, selected);

    int choice = -1;
    while (true) {
        sharedFpsLimiter.mark();

        int key = get_input();
        if (key == KEY_ESCAPE || game_user_wants_to_quit != 0) {
            break;
        }

        if (key >= RT_MENU_CHOOSE_CODE && key <= RT_MENU_CHOOSE_CODE + count) {
            choice = key - RT_MENU_CHOOSE_CODE;
            break;
        }

        int previous = selected;
        if (key >= RT_MENU_ENTER_CODE && key <= RT_MENU_ENTER_CODE + count) {
            selected = key - RT_MENU_ENTER_CODE;
        } else if (key == KEY_ARROW_UP || key == KEY_ARROW_DOWN) {
            selected = key == KEY_ARROW_UP ? (selected + count) % (count + 1) : (selected + 1) % (count + 1);
            mouse_set_position(winX + width / 2, winY + titleHeight + selected * rowHeight + rowHeight / 2);
        } else if (key == KEY_RETURN) {
            choice = selected;
            break;
        }

        if (selected != previous) {
            rt_context_menu_draw(win, width, rowHeight, titleHeight, title, actions, count, selected);
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    win_delete(win);
    text_font(oldFont);

    // The last row is Cancel.
    if (choice >= count) {
        choice = -1;
    }

    rt_log("menu: choice %d", choice);
    return choice;
}

// -----------------------------------------------------------------------------
// Skilldex (manual skill use with a controller-picked target)
// -----------------------------------------------------------------------------

#define RT_SKILL_TARGET_RANGE 8

static RtSkilldexState rt_skilldex_state;

const RtSkilldexState* rt_skilldex_get_state()
{
    return &rt_skilldex_state;
}

static int rt_skilldex_skill(int rc)
{
    switch (rc) {
    case SKILLDEX_RC_SNEAK:
        return SKILL_SNEAK;
    case SKILLDEX_RC_LOCKPICK:
        return SKILL_LOCKPICK;
    case SKILLDEX_RC_STEAL:
        return SKILL_STEAL;
    case SKILLDEX_RC_TRAPS:
        return SKILL_TRAPS;
    case SKILLDEX_RC_FIRST_AID:
        return SKILL_FIRST_AID;
    case SKILLDEX_RC_DOCTOR:
        return SKILL_DOCTOR;
    case SKILLDEX_RC_SCIENCE:
        return SKILL_SCIENCE;
    case SKILLDEX_RC_REPAIR:
        return SKILL_REPAIR;
    }
    return -1;
}

static const char* rt_context_name(Object* obj)
{
    if (obj == obj_dude) {
        return "Yourself";
    }
    return FID_TYPE(obj->fid) == OBJ_TYPE_CRITTER ? critter_name(obj) : object_name(obj);
}

// Whether `obj` is something the skill can be used on from the Skilldex: what
// the original crosshair accepted and something would react to.
static bool rt_skill_accepts(int skill, Object* obj)
{
    int type = FID_TYPE(obj->fid);
    bool critter = type == OBJ_TYPE_CRITTER;
    bool alive = critter && !critter_is_dead(obj);

    switch (skill) {
    case SKILL_FIRST_AID:
    case SKILL_DOCTOR:
        return alive;
    case SKILL_STEAL:
        return obj != obj_dude && alive && !rt_is_engaged(obj) && !rt_is_hostile(obj);
    case SKILL_LOCKPICK:
    case SKILL_TRAPS:
    case SKILL_SCIENCE:
    case SKILL_REPAIR: {
        if (obj == obj_dude || (critter && rt_is_engaged(obj))) {
            return false;
        }
        // Fallout 1 implements these only in object scripts.
        RtScriptSkills scriptSkills;
        return rt_context_get_script_skills(obj, &scriptSkills) && scriptSkills.hasProc && scriptSkills.skills[skill].handled;
    }
    }
    return false;
}

void rt_skill_odds_text(int skill, Object* target, char* out, size_t size)
{
    int level = skill_level(obj_dude, skill);
    const char* name = skill_name(skill);

    switch (skill) {
    case SKILL_FIRST_AID:
    case SKILL_DOCTOR: {
        // `skill_use` rolls d100 against skill + critical chance.
        int uses = skill_rt_uses_left(skill);
        if (critter_body_type(target) == BODY_TYPE_ROBOTIC) {
            snprintf(out, size, "%s %d%%. It can't help a robot.", name, level);
        } else if (skill == SKILL_FIRST_AID && critter_get_hits(target) >= stat_level(target, STAT_MAXIMUM_HIT_POINTS)) {
            snprintf(out, size, "%s %d%%. Not injured.", name, level);
        } else {
            int crit = stat_level(obj_dude, STAT_CRITICAL_CHANCE);
            int chance = std::clamp(level + crit, 0, 100);
            snprintf(out, size, "%s %d%%. Chance here %d%% (skill + %d%% critical chance)%s. Uses left today: %d.",
                name, level, chance, crit, skill == SKILL_DOCTOR ? " per check" : "", uses);
        }
        return;
    }
    case SKILL_STEAL:
        snprintf(out, size, "%s %d%%. The chance per item is shown in the steal screen.", name, level);
        return;
    }

    RtScriptSkills scriptSkills;
    const RtSkillInfo* info = rt_context_get_script_skills(target, &scriptSkills) && scriptSkills.hasProc && scriptSkills.skills[skill].handled
        ? &(scriptSkills.skills[skill])
        : NULL;
    if (info != NULL && info->requirement > 0 && !info->roll) {
        snprintf(out, size, "%s %d%%. Needs %d%%: %s.", name, level, info->requirement, level >= info->requirement ? "you meet it" : "too low");
    } else if (info != NULL && info->roll) {
        snprintf(out, size, "%s %d%%. Its script rolls against your skill (its modifiers aren't known).", name, level);
    } else {
        snprintf(out, size, "%s %d%%. Its script decides the outcome.", name, level);
    }
}

static void rt_skilldex_collect(int skill, std::vector<Object*>* targets)
{
    targets->clear();
    if (skill == SKILL_FIRST_AID || skill == SKILL_DOCTOR) {
        targets->push_back(obj_dude);
    }

    for (Object* obj = obj_find_first_at(obj_dude->elevation); obj != NULL; obj = obj_find_next_at()) {
        if (obj == obj_dude || (obj->flags & OBJECT_HIDDEN) != 0) {
            continue;
        }
        int type = FID_TYPE(obj->fid);
        if (type != OBJ_TYPE_ITEM && type != OBJ_TYPE_CRITTER && type != OBJ_TYPE_SCENERY) {
            continue;
        }
        if (tile_dist(obj_dude->tile, obj->tile) > RT_SKILL_TARGET_RANGE || !rt_skill_accepts(skill, obj)) {
            continue;
        }
        // Not through walls (critters in between don't block).
        if (combat_is_shot_blocked(obj_dude, obj_dude->tile, obj->tile, obj, NULL)) {
            continue;
        }
        targets->push_back(obj);
    }

    std::stable_sort(targets->begin(), targets->end(), [](Object* a, Object* b) {
        return (a == obj_dude ? -1 : tile_dist(obj_dude->tile, a->tile)) < (b == obj_dude ? -1 : tile_dist(obj_dude->tile, b->tile));
    });
}

static void rt_skilldex_outline(Object* obj, bool on, int* savedOutline)
{
    Rect rect;
    if (on) {
        *savedOutline = obj->outline;
        obj_remove_outline(obj, NULL);
        obj_outline_object(obj, OUTLINE_TYPE_ITEM, NULL);
        obj_turn_on_outline(obj, &rect);
    } else {
        obj_remove_outline(obj, &rect);
        if (*savedOutline != 0) {
            obj->outline = *savedOutline;
        }
    }
    obj_bound(obj, &rect);
    tile_refresh_rect(&rect, obj->elevation);
}

static void rt_skilldex_draw(int win, int width, int height, int skill, Object* target, int index, int count)
{
    unsigned char* buffer = win_get_buf(win);
    buf_fill(buffer, width, height, width, colorTable[0]);
    draw_box(buffer, width, 0, 0, width - 1, height - 1, colorTable[992]);

    int line = text_height() + 3;
    char text[200];
    snprintf(text, sizeof(text), "%s: %s (%d of %d)", skill_name(skill), rt_context_name(target), index + 1, count);
    text_to_buf(buffer + width * 5 + 8, text, width - 16, width, colorTable[32747]);

    // Your skill on one line, what it means on this target on the next.
    char skillPart[200];
    SDL_strlcpy(skillPart, rt_skilldex_state.odds, sizeof(skillPart));
    const char* rest = "";
    char* split = strstr(skillPart, ". ");
    if (split != NULL) {
        split[1] = '\0';
        rest = rt_skilldex_state.odds + (split - skillPart) + 2;
    }
    text_to_buf(buffer + width * (5 + line) + 8, skillPart, width - 16, width, colorTable[992]);
    text_to_buf(buffer + width * (5 + line * 2) + 8, rest, width - 16, width, colorTable[992]);

    const char* hint = "D-pad/RS Next target   A Use   B Back";
    text_to_buf(buffer + width * (5 + line * 3) + (width - text_width(hint)) / 2, hint, width - 16, width, colorTable[992]);
    win_draw(win);
}

// Controller target selection: the candidates are outlined one at a time;
// D-pad or a right-stick flick moves to the next, A uses the skill, B goes
// back to the Skilldex. Gameplay is paused meanwhile.
static Object* rt_skilldex_pick(int skill)
{
    std::vector<Object*> targets;
    rt_skilldex_collect(skill, &targets);
    if (targets.empty()) {
        rt_context_print("Nothing nearby to use %s on.", skill_name(skill));
        return NULL;
    }

    bool wasEnabled = map_disable_bk_processes();
    int centerWas = tile_center_tile;
    int oldFont = text_curr();
    bool keyModeWas = gamepad_is_menu_key_mode();
    gamepad_set_menu_key_mode(true);
    text_font(101);

    int width = 600;
    int height = (text_height() + 3) * 4 + 10;
    int screenWidth = scr_size.lrx - scr_size.ulx + 1;
    int win = win_add((screenWidth - width) / 2, 8, width, height, colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);

    int index = 0;
    int saved = 0;
    Object* chosen = NULL;
    bool redraw = true;

    rt_skilldex_state.picking = true;
    rt_skilldex_state.skill = skill;
    rt_skilldex_state.candidates = (int)targets.size();
    rt_skilldex_state.picks++;

    while (win != -1) {
        sharedFpsLimiter.mark();

        if (redraw) {
            Object* target = targets[index];
            rt_skilldex_state.target = target;
            rt_skill_odds_text(skill, target, rt_skilldex_state.odds, sizeof(rt_skilldex_state.odds));
            // The view on the target while it is picked.
            tile_set_center(target->tile, TILE_SET_CENTER_REFRESH_WINDOW);
            rt_skilldex_outline(target, true, &saved);
            rt_skilldex_draw(win, width, height, skill, target, index, (int)targets.size());
            rt_log("skilldex: %s target %d/%d %s: %s", skill_name(skill), index + 1, (int)targets.size(), rt_context_name(target), rt_skilldex_state.odds);
            redraw = false;
        }

        int key = get_input();
        float flickX;
        float flickY;
        bool flick = gamepad_consume_menu_flick(&flickX, &flickY);
        int step = 0;
        if (key == KEY_ARROW_RIGHT || key == KEY_ARROW_DOWN || (flick && (flickX > 0.0f || flickY > 0.0f))) {
            step = 1;
        } else if (key == KEY_ARROW_LEFT || key == KEY_ARROW_UP || flick) {
            step = -1;
        }

        if (key == KEY_ESCAPE || game_user_wants_to_quit != 0) {
            rt_skilldex_outline(targets[index], false, &saved);
            break;
        }
        if (key == KEY_RETURN) {
            rt_skilldex_outline(targets[index], false, &saved);
            chosen = targets[index];
            break;
        }
        if (step != 0 && targets.size() > 1) {
            rt_skilldex_outline(targets[index], false, &saved);
            index = (index + step + (int)targets.size()) % (int)targets.size();
            redraw = true;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (win != -1) {
        win_delete(win);
    }
    tile_set_center(centerWas, TILE_SET_CENTER_REFRESH_WINDOW);
    gamepad_set_menu_key_mode(keyModeWas);
    text_font(oldFont);
    if (wasEnabled) {
        map_enable_bk_processes();
    }
    rt_skilldex_state.picking = false;
    rt_skilldex_state.target = NULL;
    return chosen;
}

void rt_skilldex_run()
{
    rt_skilldex_state.opens++;
    while (true) {
        int rc = skilldex_select();
        int skill = rt_skilldex_skill(rc);
        if (skill == -1) {
            rt_log("skilldex: closed");
            return;
        }

        if (skill == SKILL_SNEAK) {
            rt_toggle_sneak();
            return;
        }

        // The original rule: no healing and no theft during a fight.
        if ((skill == SKILL_FIRST_AID || skill == SKILL_DOCTOR || skill == SKILL_STEAL) && rt_player_in_combat()) {
            rt_context_print("You cannot do that in combat.");
            continue;
        }

        Object* target = rt_skilldex_pick(skill);
        if (target == NULL) {
            // B: back to the Skilldex.
            continue;
        }

        char odds[200];
        rt_skill_odds_text(skill, target, odds, sizeof(odds));
        rt_context_print("%s", odds);
        rt_skilldex_state.uses++;
        rt_skilldex_state.lastSkill = skill;
        rt_skilldex_state.lastTarget = target;
        rt_log("skilldex: %s on %s", skill_name(skill), rt_context_name(target));
        action_use_skill_on(obj_dude, target, skill);
        return;
    }
}

// Diagnostics: logs every call to one of `opcodes` in the script with the
// procedure it is in and the instructions around it.
int rt_context_dump_opcodes(int scriptIndex, const int* opcodes, int opcodeCount)
{
    char path[COMPAT_MAX_PATH];
    if (!scr_rt_script_path(scriptIndex, path, sizeof(path))) {
        return 0;
    }

    DB_FILE* stream = db_fopen(path, "rb");
    if (stream == NULL) {
        return 0;
    }
    long size = db_filelength(stream);
    std::vector<unsigned char> data(size > 0 ? size : 0);
    if (size > 0) {
        db_fread(data.data(), 1, size, stream);
    }
    db_fclose(stream);
    if (size < RT_INT_PROCEDURES_OFFSET + 4) {
        return 0;
    }

    const unsigned char* procedures = data.data() + RT_INT_PROCEDURES_OFFSET;
    int procedureCount = rt_be32(procedures);
    long identifiersOffset = RT_INT_PROCEDURES_OFFSET + 4 + (long)procedureCount * RT_INT_PROCEDURE_SIZE;
    if (procedureCount <= 0 || identifiersOffset + 4 > size) {
        return 0;
    }

    int hits = 0;
    for (int index = 0; index < procedureCount; index++) {
        const unsigned char* procedure = procedures + 4 + index * RT_INT_PROCEDURE_SIZE;
        long nameOffset = rt_be32(procedure);
        const char* name = identifiersOffset + nameOffset < size ? (const char*)(data.data() + identifiersOffset + nameOffset) : "?";
        long body = rt_be32(procedure + RT_INT_PROCEDURE_BODY);
        if (body <= 0 || body >= size) {
            continue;
        }
        long end = size;
        for (int other = 0; other < procedureCount; other++) {
            int start = rt_be32(procedures + 4 + other * RT_INT_PROCEDURE_SIZE + RT_INT_PROCEDURE_BODY);
            if (start > body && start < end) {
                end = start;
            }
        }

        std::vector<RtInstruction> code;
        long pos = body;
        while (pos + 2 <= end) {
            RtInstruction instruction;
            instruction.opcode = rt_be16(data.data() + pos);
            instruction.value = 0;
            pos += 2;
            if ((instruction.opcode & 0x8000) == 0) {
                break;
            }
            if ((instruction.opcode & 0xF000) != 0x8000) {
                if (pos + 4 > end) {
                    break;
                }
                instruction.value = rt_be32(data.data() + pos);
                pos += 4;
            }
            code.push_back(instruction);
        }

        for (size_t at = 0; at < code.size(); at++) {
            bool match = false;
            for (int op = 0; op < opcodeCount; op++) {
                if (code[at].opcode == opcodes[op]) {
                    match = true;
                }
            }
            if (!match) {
                continue;
            }
            hits++;
            char line[1024];
            line[0] = 0;
            size_t from = at > 24 ? at - 24 : 0;
            size_t to = std::min(code.size(), at + 8);
            for (size_t i = from; i < to; i++) {
                char part[32];
                if ((code[i].opcode & 0xF000) != 0x8000) {
                    snprintf(part, sizeof(part), " %X:%d", code[i].opcode, code[i].value);
                } else {
                    snprintf(part, sizeof(part), i == at ? " [%X]" : " %X", code[i].opcode);
                }
                SDL_strlcat(line, part, sizeof(line));
            }
            rt_log("opdump %s %s:%s", path, name, line);
        }
    }

    return hits;
}

} // namespace fallout
