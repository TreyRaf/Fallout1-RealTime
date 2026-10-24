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
#include "game/critter.h"
#include "game/display.h"
#include "game/item.h"
#include "game/object.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/game.h"
#include "game/realtime.h"
#include "game/scripts.h"
#include "game/skill.h"
#include "game/stat.h"
#include "platform_compat.h"
#include "plib/color/color.h"
#include "plib/db/db.h"
#include "plib/gnw/button.h"
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
    snprintf(action->label, sizeof(action->label), "%s (%d%%)", skill_name(skill), skill_level(obj_dude, skill));
    (*count)++;
}

static int rt_context_collect(Object* obj, RtAction* actions, RtScriptSkills* scriptSkills)
{
    int count = 0;

    if (rt_context_default_label(obj, actions[0].label, sizeof(actions[0].label))) {
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
        SDL_strlcat(buffer, actions[index].kind == RT_ACTION_SKILL ? skill_name(actions[index].skill) : actions[index].label, size);
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

    if (info != NULL && info->requirement > 0 && !info->roll) {
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
