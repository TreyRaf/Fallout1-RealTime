#include "game/rt_pipboy.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include <SDL.h>

#include "game/art.h"
#include "game/automap.h"
#include "game/critter.h"
#include "game/editor.h"
#include "game/game.h"
#include "game/game_vars.h"
#include "game/gmovie.h"
#include "game/gsound.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/pipboy.h"
#include "game/protinst.h"
#include "game/proto.h"
#include "game/realtime.h"
#include "game/rt_favorites.h"
#include "game/party.h"
#include "game/rt_rules.h"
#include "game/skill.h"
#include "game/stat.h"
#include "game/trait.h"
#include "game/wordwrap.h"
#include "game/worldmap.h"
#include "plib/color/color.h"
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

// The screen area of the Pip-Boy 2000 artwork (as the original uses it).
#define PB_PITCH PIPBOY_RT_WINDOW_WIDTH
#define PB_X PIPBOY_RT_SCREEN_X
#define PB_Y PIPBOY_RT_SCREEN_Y
#define PB_W PIPBOY_RT_SCREEN_WIDTH
#define PB_H PIPBOY_RT_SCREEN_HEIGHT

// The device's button panel (left side): Stats, Inventory, Map, Data, Close.
#define PB_BUTTON_X 53
#define PB_BUTTON_Y 341
#define PB_BUTTON_STEP 27
#define PB_EVENT_BUTTON 0x7200

#define PB_ITEM_ART_WIDTH 70
#define PB_ITEM_ART_HEIGHT 50

typedef enum PbCategory {
    PB_CATEGORY_ALL,
    PB_CATEGORY_WEAPONS,
    PB_CATEGORY_APPAREL,
    PB_CATEGORY_AID,
    PB_CATEGORY_AMMO,
    PB_CATEGORY_MISC,
    PB_CATEGORY_COUNT,
} PbCategory;

static const char* pb_tab_names[RT_PIPBOY_TAB_COUNT] = { "STATS", "INVENTORY", "MAP", "DATA" };
static const char* pb_stats_pages[] = { "Status", "S.P.E.C.I.A.L.", "Skills", "Perks" };
static const char* pb_inventory_pages[PB_CATEGORY_COUNT] = { "All", "Weapons", "Apparel", "Aid", "Ammo", "Misc" };
static const char* pb_map_pages[] = { "Local", "Automaps", "World" };
static const char* pb_data_pages[] = { "Quests", "Holodisks", "Archives", "Rest" };
static const int pb_page_counts[RT_PIPBOY_TAB_COUNT] = { 4, PB_CATEGORY_COUNT, 3, 4 };
static const char* pb_damage_names[7] = { "Normal", "Laser", "Fire", "Plasma", "Electrical", "EMP", "Explosion" };

static RtPipboyState pb_state;
static bool pb_open = false;
static bool pb_close = false;
// Y on the Skills page: open the Skilldex after closing.
static bool pb_open_skilldex = false;
static int pb_win = -1;
static unsigned char* pb_buf = NULL;
static unsigned char* pb_bg = NULL;

static int pb_tab = RT_PIPBOY_TAB_INVENTORY;
static int pb_page[RT_PIPBOY_TAB_COUNT] = { 0, 0, 0, 0 };
static int pb_cursor[RT_PIPBOY_TAB_COUNT][8];
static int pb_scroll[RT_PIPBOY_TAB_COUNT][8];
static int pb_mode = RT_PIPBOY_MODE_NONE;

static int pb_line = 12;
static int pb_green;
static int pb_bright;
static int pb_dim;
static int pb_bar;
static int pb_red;

static char pb_message[128];
static unsigned int pb_message_until = 0;

// Sub-screen data.
static std::vector<int> pb_choices;
static int pb_choice_cursor = 0;
static int pb_choice_scroll = 0;
static int pb_pending_perk = -1;
static int pb_mutate_lose = -1;
static Object* pb_item = NULL;
static int pb_quantity = 1;
static int pb_quantity_max = 1;
static std::vector<std::string> pb_text_lines;
static std::string pb_text_title;
static int pb_text_scroll = 0;
static int pb_skill_added[SKILL_COUNT];

// Map views.
static unsigned char* pb_map_buf = NULL;
static int pb_map_zoom = 1;
static int pb_map_pan_x = 0;
static int pb_map_pan_y = 0;
static bool pb_map_details = true;
static bool pb_scanner = false;
static bool pb_map_centered = false;
static int pb_saved_map = -1;
static int pb_saved_elevation = 0;
static unsigned char* pb_world_art = NULL;
static CacheEntry* pb_world_key = NULL;
static int pb_world_width = 0;
static int pb_world_height = 0;
static int pb_world_zoom = 4;
static int pb_world_x = 0;
static int pb_world_y = 0;
static bool pb_world_centered = false;

typedef struct PbRow {
    int kind;
    int id;
    Object* item;
} PbRow;

static std::vector<PbRow> pb_rows;

static void pb_loop();
static void pb_render();
static void pb_handle_key(int key);

const RtPipboyState* rt_pipboy_get_state()
{
    return &pb_state;
}

bool rt_pipboy_is_open()
{
    return pb_open;
}

static void pb_say(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(pb_message, sizeof(pb_message), format, args);
    va_end(args);
    pb_message_until = SDL_GetTicks() + 3500;
    SDL_strlcpy(pb_state.lastMessage, pb_message, sizeof(pb_state.lastMessage));
    rt_log("pipboy: %s", pb_message);
}

// -----------------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------------

static void pb_restore(int x, int y, int width, int height)
{
    buf_to_buf(pb_bg + PB_PITCH * y + x, width, height, PB_PITCH, pb_buf + PB_PITCH * y + x, PB_PITCH);
}

static void pb_text(int x, int y, const char* text, int color, int width = PB_W)
{
    if (text == NULL || y < PB_Y || y + text_height() > PB_Y + PB_H) {
        return;
    }
    int maxWidth = PB_X + PB_W - x;
    if (width < maxWidth) {
        maxWidth = width;
    }
    if (maxWidth <= 0) {
        return;
    }
    text_to_buf(pb_buf + PB_PITCH * y + x, text, maxWidth, PB_PITCH, color);
}

static void pb_text_right(int right, int y, const char* text, int color)
{
    pb_text(right - text_width(text), y, text, color);
}

static void pb_fill(int x, int y, int width, int height, int color)
{
    buf_fill(pb_buf + PB_PITCH * y + x, width, height, PB_PITCH, color);
}

static void pb_hline(int y, int color)
{
    draw_line(pb_buf, PB_PITCH, PB_X + 4, y, PB_X + PB_W - 5, y, color);
}

// Word-wraps `text` to `width` pixels into `lines`.
static void pb_wrap(const char* text, int width, std::vector<std::string>& lines)
{
    if (text == NULL || text[0] == '\0') {
        lines.push_back("");
        return;
    }

    short beginnings[WORD_WRAP_MAX_COUNT];
    short count;
    if (word_wrap(text, width, beginnings, &count) != 0) {
        lines.push_back(text);
        return;
    }

    for (int line = 0; line < count - 1; line++) {
        std::string part(text + beginnings[line], text + beginnings[line + 1]);
        while (!part.empty() && (part.back() == '\n' || part.back() == ' ')) {
            part.pop_back();
        }
        lines.push_back(part);
    }
}

static int pb_body_top()
{
    return PB_Y + 2 + pb_line * 2 + 6;
}

static int pb_footer_y()
{
    return PB_Y + PB_H - pb_line - 2;
}

static int pb_body_bottom()
{
    return pb_footer_y() - pb_line - 4;
}

static void pb_footer(const char* prompts)
{
    int y = pb_footer_y();
    pb_hline(y - 3, pb_dim);

    // Long prompt lines get tighter spacing rather than being cut off.
    std::string line = prompts;
    for (const char* gap : { "   ", "  " }) {
        if (text_width(line.c_str()) <= PB_W - 12) {
            break;
        }
        size_t at;
        while ((at = line.find(gap)) != std::string::npos) {
            line.replace(at, strlen(gap), gap + 1);
        }
    }
    pb_text(PB_X + 6, y, line.c_str(), pb_green);

    if (pb_message[0] != '\0' && SDL_GetTicks() < pb_message_until) {
        pb_text(PB_X + 6, y - pb_line - 2, pb_message, pb_bright);
    }
}

// A selectable list with scrolling.
static void pb_draw_list(int top, int visible, int count, int cursor, int scroll, const std::vector<std::string>& labels, const std::vector<std::string>* values = NULL)
{
    for (int index = scroll; index < count && index < scroll + visible; index++) {
        int y = top + (index - scroll) * pb_line;
        int color = pb_green;
        if (index == cursor) {
            pb_fill(PB_X + 2, y - 1, PB_W - 4, pb_line, pb_bar);
            color = pb_bright;
        }
        pb_text(PB_X + 8, y, labels[index].c_str(), color, PB_W - 80);
        if (values != NULL && index < (int)values->size()) {
            pb_text_right(PB_X + PB_W - 10, y, (*values)[index].c_str(), color);
        }
    }

    if (scroll > 0) {
        pb_text(PB_X + PB_W - 10, top - pb_line + 2, "^", pb_bright);
    }
    if (scroll + visible < count) {
        pb_text(PB_X + PB_W - 10, top + visible * pb_line - 2, "v", pb_bright);
    }
}

static void pb_list_move(int key, int count, int visible, int* cursor, int* scroll)
{
    if (count <= 0) {
        *cursor = 0;
        *scroll = 0;
        return;
    }

    switch (key) {
    case KEY_ARROW_UP:
        *cursor = *cursor > 0 ? *cursor - 1 : count - 1;
        break;
    case KEY_ARROW_DOWN:
        *cursor = *cursor < count - 1 ? *cursor + 1 : 0;
        break;
    case KEY_ARROW_LEFT:
        *cursor = std::max(0, *cursor - visible);
        break;
    case KEY_ARROW_RIGHT:
        *cursor = std::min(count - 1, *cursor + visible);
        break;
    }

    if (*cursor >= count) {
        *cursor = count - 1;
    }
    if (*cursor < *scroll) {
        *scroll = *cursor;
    }
    if (*cursor >= *scroll + visible) {
        *scroll = *cursor - visible + 1;
    }
    if (*scroll < 0) {
        *scroll = 0;
    }
}

static void pb_draw_panel()
{
    // The device's button panel, relabelled for the four sections.
    unsigned char* up;
    unsigned char* down;
    int width;
    int height;
    bool haveButtons = pipboy_rt_get_frame(PIPBOY_RT_FRM_LITTLE_RED_BUTTON_UP, &up, &width, &height)
        && pipboy_rt_get_frame(PIPBOY_RT_FRM_LITTLE_RED_BUTTON_DOWN, &down, &width, &height);

    static const char* labels[5] = { "STATS", "INVENTORY", "MAP", "DATA", "CLOSE" };
    int plate = colorTable[(5 << 10) | (5 << 5) | 4];
    int amber = colorTable[(30 << 10) | (25 << 5) | 8];

    int oldFont = text_curr();
    text_font(103);
    for (int index = 0; index < 5; index++) {
        int y = PB_BUTTON_Y + PB_BUTTON_STEP * index;
        pb_restore(PB_BUTTON_X, y, width, height);
        if (haveButtons) {
            bool active = index == pb_tab;
            trans_buf_to_buf(active ? down : up, width, height, width, pb_buf + PB_PITCH * y + PB_BUTTON_X, PB_PITCH);
        }

        int labelX = PB_BUTTON_X + width + 4;
        buf_fill(pb_buf + PB_PITCH * (y - 1) + labelX - 1, 98, 18, PB_PITCH, plate);
        int color = index == pb_tab ? colorTable[(31 << 10) | (31 << 5) | 16] : amber;
        text_to_buf(pb_buf + PB_PITCH * (y + 2) + labelX + 1, labels[index], 96, PB_PITCH, color);
    }
    text_font(oldFont);
}

static void pb_draw_header()
{
    // Main tabs.
    int x = PB_X + 6;
    int y = PB_Y + 2;
    for (int tab = 0; tab < RT_PIPBOY_TAB_COUNT; tab++) {
        int width = text_width(pb_tab_names[tab]);
        if (tab == pb_tab) {
            pb_fill(x - 3, y - 1, width + 6, pb_line, pb_bar);
            pb_text(x, y, pb_tab_names[tab], pb_bright);
        } else {
            pb_text(x, y, pb_tab_names[tab], pb_dim);
        }
        x += width + 18;
    }
    pb_text_right(PB_X + PB_W - 6, y, "LT  RT", pb_dim);

    // Pages of the tab.
    const char** pages;
    switch (pb_tab) {
    case RT_PIPBOY_TAB_STATS:
        pages = pb_stats_pages;
        break;
    case RT_PIPBOY_TAB_INVENTORY:
        pages = pb_inventory_pages;
        break;
    case RT_PIPBOY_TAB_MAP:
        pages = pb_map_pages;
        break;
    default:
        pages = pb_data_pages;
        break;
    }

    x = PB_X + 6;
    y += pb_line + 2;
    for (int page = 0; page < pb_page_counts[pb_tab]; page++) {
        int width = text_width(pages[page]);
        int color = page == pb_page[pb_tab] ? pb_bright : pb_dim;
        if (page == pb_page[pb_tab]) {
            draw_line(pb_buf, PB_PITCH, x, y + pb_line - 2, x + width, y + pb_line - 2, pb_bright);
        }
        pb_text(x, y, pages[page], color);
        x += width + 10;
    }
    pb_text_right(PB_X + PB_W - 6, y, "LS < >", pb_dim);
    pb_hline(y + pb_line + 1, pb_dim);
}

// -----------------------------------------------------------------------------
// Stats
// -----------------------------------------------------------------------------

static void pb_stats_status(int top, int bottom)
{
    std::vector<std::string> labels;
    std::vector<std::string> values;
    char text[128];

    auto add = [&](const char* label, const char* value) {
        labels.push_back(label);
        values.push_back(value != NULL ? value : "");
    };

    int level = stat_pc_get(PC_STAT_LEVEL);
    int xp = stat_pc_get(PC_STAT_EXPERIENCE);
    int next = stat_pc_min_exp();
    int previous = 1000 * level * (level - 1) / 2;

    add(critter_name(obj_dude), NULL);
    snprintf(text, sizeof(text), "%d", level);
    add(stat_pc_name(PC_STAT_LEVEL), text);
    snprintf(text, sizeof(text), "%d / %d", xp, next);
    add(stat_pc_name(PC_STAT_EXPERIENCE), text);
    labels.push_back("#progress");
    values.push_back(std::to_string(next > previous ? (xp - previous) * 100 / (next - previous) : 100));

    snprintf(text, sizeof(text), "%d / %d", critter_get_hits(obj_dude), stat_level(obj_dude, STAT_MAXIMUM_HIT_POINTS));
    add(stat_name(STAT_CURRENT_HIT_POINTS), text);
    snprintf(text, sizeof(text), "%d / %d", (int)rt_get_dude_ap(), stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS));
    add("Action Points", text);

    int unspent = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
    if (unspent > 0) {
        snprintf(text, sizeof(text), "%d (Skills page)", unspent);
        add("Skill points to spend", text);
    }
    if (editor_rt_has_free_perk()) {
        snprintf(text, sizeof(text), "%d (Perks page)", editor_rt_free_perks());
        add("Perks to choose", text);
    }
    level = stat_pc_get(PC_STAT_LEVEL);
    if (level < PC_LEVEL_MAX) {
        snprintf(text, sizeof(text), "%d", level + 2 - level % 2);
        add("Next perk at level", text);
    }
    snprintf(text, sizeof(text), "%d / %d", partyMemberCompanionCount(), rt_companion_limit());
    add("Companions (Charisma)", text);

    add("", NULL);
    static const int derived[] = {
        STAT_ARMOR_CLASS,
        STAT_MELEE_DAMAGE,
        STAT_CARRY_WEIGHT,
        STAT_SEQUENCE,
        STAT_HEALING_RATE,
        STAT_CRITICAL_CHANCE,
        STAT_RADIATION_RESISTANCE,
        STAT_POISON_RESISTANCE,
    };
    for (int stat : derived) {
        bool percent = stat == STAT_CRITICAL_CHANCE || stat == STAT_RADIATION_RESISTANCE || stat == STAT_POISON_RESISTANCE;
        snprintf(text, sizeof(text), percent ? "%d%%" : "%d", stat_level(obj_dude, stat));
        add(stat_name(stat), text);
    }

    add("", NULL);
    add("Damage      Threshold / Resistance", NULL);
    for (int type = 0; type < 7; type++) {
        snprintf(text, sizeof(text), "%d / %d%%",
            stat_level(obj_dude, STAT_DAMAGE_THRESHOLD + type),
            stat_level(obj_dude, STAT_DAMAGE_RESISTANCE + type));
        add(pb_damage_names[type], text);
    }

    add("", NULL);
    add("Conditions", NULL);
    int conditions = 0;
    int poison = stat_level(obj_dude, STAT_CURRENT_POISON_LEVEL);
    int radiation = stat_level(obj_dude, STAT_CURRENT_RADIATION_LEVEL);
    if (poison > 0) {
        snprintf(text, sizeof(text), "%d", poison);
        add("  Poisoned", text);
        conditions++;
    }
    if (radiation > 0) {
        snprintf(text, sizeof(text), "%d", radiation);
        add("  Radiated", text);
        conditions++;
    }
    int results = obj_dude->data.critter.combat.results;
    static const struct {
        int flag;
        const char* name;
    } flags[] = {
        { DAM_BLIND, "  Eye damage" },
        { DAM_CRIP_ARM_RIGHT, "  Crippled right arm" },
        { DAM_CRIP_ARM_LEFT, "  Crippled left arm" },
        { DAM_CRIP_LEG_RIGHT, "  Crippled right leg" },
        { DAM_CRIP_LEG_LEFT, "  Crippled left leg" },
    };
    for (const auto& entry : flags) {
        if ((results & entry.flag) != 0) {
            add(entry.name, NULL);
            conditions++;
        }
    }
    if (conditions == 0) {
        add("  None", NULL);
    }

    int visible = (bottom - top) / pb_line;
    int* scroll = &(pb_scroll[RT_PIPBOY_TAB_STATS][0]);
    int count = (int)labels.size();
    if (*scroll > std::max(0, count - visible)) {
        *scroll = std::max(0, count - visible);
    }
    pb_state.listCount = count;

    for (int index = *scroll; index < count && index < *scroll + visible; index++) {
        int y = top + (index - *scroll) * pb_line;
        if (labels[index] == "#progress") {
            // Progress to the next level.
            int percent = atoi(values[index].c_str());
            int width = PB_W - 30;
            draw_box(pb_buf, PB_PITCH, PB_X + 12, y + 2, PB_X + 12 + width, y + pb_line - 3, pb_dim);
            pb_fill(PB_X + 13, y + 3, std::max(0, std::min(width - 1, (width - 1) * percent / 100)), pb_line - 6, pb_green);
            continue;
        }
        int color = index == 0 ? pb_bright : pb_green;
        pb_text(PB_X + 8, y, labels[index].c_str(), color);
        pb_text_right(PB_X + PB_W - 10, y, values[index].c_str(), color);
    }
    if (*scroll + visible < count) {
        pb_text(PB_X + PB_W - 10, bottom - pb_line, "v", pb_bright);
    }
}

static void pb_draw_description(int top, int bottom, const char* title, const char* text)
{
    pb_hline(top - 3, pb_dim);
    pb_text(PB_X + 8, top, title, pb_bright);
    std::vector<std::string> lines;
    pb_wrap(text, PB_W - 20, lines);
    int y = top + pb_line;
    for (const std::string& line : lines) {
        if (y + pb_line > bottom) {
            break;
        }
        pb_text(PB_X + 8, y, line.c_str(), pb_green);
        y += pb_line;
    }
}

// Perk card: the original artwork (skilldex.lst, see `perk_rt_pic`) at the
// right, scaled down to fit this box with its proportions kept; name, rank,
// requirements and description in the column left of it.
#define PB_CARD_ART_WIDTH 104
#define PB_CARD_ART_HEIGHT 90
// Room a perk card takes below a list.
#define PB_PERK_CARD_HEIGHT 172

// Perks whose artwork was reported missing (logged once each).
static bool pb_perk_art_missing[PERK_COUNT];

// Draws a line of the card, counting it against the artwork and the footer.
static int pb_card_line(int x, int y, const char* text, int color, int width, int bottom, const Rect* art)
{
    if (y + pb_line > bottom) {
        return y;
    }
    pb_text(x, y, text, color, width);
    int right = x + std::min(width, text_width(text));
    bool overArt = art != NULL && x < art->lrx && right > art->ulx && y < art->lry && y + text_height() > art->uly;
    if (overArt || y + text_height() > pb_body_bottom()) {
        pb_state.perkTextOverlaps++;
    }
    pb_state.perkTextLines++;
    return y + pb_line;
}

static void pb_draw_perk_card(int top, int bottom, int perk, const char* rank, const char* title = NULL)
{
    pb_hline(top - 3, pb_dim);
    pb_state.perkCard = perk;
    pb_state.perkArtFid = -1;
    pb_state.perkTextOverlaps = 0;
    pb_state.perkTextLines = 0;

    // The artwork box: right side, from the top of the card.
    int boxHeight = std::min(PB_CARD_ART_HEIGHT, bottom - top);
    Rect box;
    box.ulx = PB_X + PB_W - PB_CARD_ART_WIDTH - 8;
    box.uly = top;
    box.lrx = box.ulx + PB_CARD_ART_WIDTH;
    box.lry = box.uly + boxHeight;

    int fid = art_id(OBJ_TYPE_SKILLDEX, perk_rt_pic(perk), 0, 0, 0);
    if (perk_rt_pic(perk) != -1 && art_exists(fid)) {
        CacheEntry* handle;
        int width;
        int height;
        if (art_lock(fid, &handle, &width, &height) != NULL) {
            // Scaled down to the box (never up), centered in it.
            int drawWidth = width;
            int drawHeight = height;
            if (drawWidth > PB_CARD_ART_WIDTH || drawHeight > boxHeight) {
                if (width * boxHeight >= height * PB_CARD_ART_WIDTH) {
                    drawWidth = PB_CARD_ART_WIDTH;
                    drawHeight = height * PB_CARD_ART_WIDTH / width;
                } else {
                    drawHeight = boxHeight;
                    drawWidth = width * boxHeight / height;
                }
            }
            art_ptr_unlock(handle);
            int x = box.ulx + (PB_CARD_ART_WIDTH - drawWidth) / 2;
            int y = box.uly + (boxHeight - drawHeight) / 2;
            scale_art(fid, pb_buf + PB_PITCH * y + x, drawWidth, drawHeight, PB_PITCH);
            pb_state.perkArtFid = fid;
            pb_state.perkArtX = x;
            pb_state.perkArtY = y;
            pb_state.perkArtWidth = drawWidth;
            pb_state.perkArtHeight = drawHeight;
        }
    }
    if (pb_state.perkArtFid == -1) {
        draw_box(pb_buf, PB_PITCH, box.ulx, box.uly, box.lrx - 1, box.lry - 1, pb_dim);
        pb_text(box.ulx + 6, box.uly + boxHeight / 2 - pb_line / 2, "No artwork", pb_dim, PB_CARD_ART_WIDTH - 12);
        pb_state.perkArtX = box.ulx;
        pb_state.perkArtY = box.uly;
        pb_state.perkArtWidth = PB_CARD_ART_WIDTH;
        pb_state.perkArtHeight = boxHeight;
        if (perk >= 0 && perk < PERK_COUNT && !pb_perk_art_missing[perk]) {
            pb_perk_art_missing[perk] = true;
            rt_log("pipboy: no artwork for perk %d (%s), skilldex index %d", perk, perk_name(perk), perk_rt_pic(perk));
        }
    }

    // Text column, left of the artwork.
    Rect art = { box.ulx - 6, box.uly, box.lrx, box.lry };
    int x = PB_X + 8;
    int width = art.ulx - x;
    int y = top;
    y = pb_card_line(x, y, title != NULL ? title : perk_name(perk), pb_bright, width, bottom, &art);
    if (rank != NULL) {
        y = pb_card_line(x, y, rank, pb_green, width, bottom, &art);
    }
    char requirements[160];
    perk_rt_requirements(perk, requirements, sizeof(requirements));
    std::vector<std::string> lines;
    pb_wrap((std::string("Requires: ") + requirements).c_str(), width, lines);
    for (const std::string& line : lines) {
        y = pb_card_line(x, y, line.c_str(), pb_dim, width, bottom, &art);
    }
    y += 3;
    lines.clear();
    pb_wrap(perk_description(perk), width, lines);
    for (const std::string& line : lines) {
        y = pb_card_line(x, y, line.c_str(), pb_green, width, bottom, &art);
    }
}

// "Rank 1 of 3" for a perk owned, "Takes rank 2 of 3" for one to choose.
static std::string pb_perk_rank_text(int perk, bool choosing)
{
    int owned = perk_level(perk);
    int max = perk_max_rank(perk);
    char text[64];
    if (choosing) {
        snprintf(text, sizeof(text), max > 1 ? "Takes rank %d of %d" : "Single rank", owned + 1, max);
    } else {
        snprintf(text, sizeof(text), "Rank %d of %d", owned, std::max(max, owned));
    }
    return text;
}

static void pb_build_perk_rows()
{
    pb_rows.clear();
    if (editor_rt_has_free_perk()) {
        pb_rows.push_back({ 0, -1, NULL });
    }
    for (int perk = 0; perk < PERK_COUNT; perk++) {
        if (perk_level(perk) > 0) {
            pb_rows.push_back({ 1, perk, NULL });
        }
    }
    int trait1;
    int trait2;
    trait_get(&trait1, &trait2);
    if (trait1 != -1) {
        pb_rows.push_back({ 2, trait1, NULL });
    }
    if (trait2 != -1) {
        pb_rows.push_back({ 2, trait2, NULL });
    }
}

static bool pb_is_tagged(int skill)
{
    int tags[NUM_TAGGED_SKILLS];
    skill_get_tags(tags, NUM_TAGGED_SKILLS);
    for (int tag : tags) {
        if (tag == skill) {
            return true;
        }
    }
    return false;
}

static void pb_render_choices(int top, int bottom, const char* title)
{
    // Perks get a card with their artwork, which needs more room.
    bool perkCard = pb_mode == RT_PIPBOY_MODE_CHOOSE_PERK;
    int listBottom = perkCard ? bottom - PB_PERK_CARD_HEIGHT : bottom - pb_line * 6;
    pb_text(PB_X + 8, top, title, pb_bright);
    std::vector<std::string> labels;
    for (int choice : pb_choices) {
        switch (pb_mode) {
        case RT_PIPBOY_MODE_CHOOSE_PERK: {
            std::string label = perk_name(choice);
            if (perk_level(choice) > 0) {
                label += " (" + std::to_string(perk_level(choice)) + ")";
            }
            labels.push_back(label);
            break;
        }
        case RT_PIPBOY_MODE_CHOOSE_TAG:
            labels.push_back(skill_name(choice));
            break;
        default:
            labels.push_back(trait_name(choice));
            break;
        }
    }
    int visible = (listBottom - top - pb_line - 2) / pb_line;
    pb_list_move(0, (int)labels.size(), visible, &pb_choice_cursor, &pb_choice_scroll);
    pb_draw_list(top + pb_line + 2, visible, (int)labels.size(), pb_choice_cursor, pb_choice_scroll, labels);
    pb_state.listCount = (int)labels.size();
    pb_state.cursor = pb_choice_cursor;

    if (pb_choice_cursor < (int)pb_choices.size()) {
        int choice = pb_choices[pb_choice_cursor];
        if (perkCard) {
            // Highlighting only shows the card; A then asks to confirm.
            pb_draw_perk_card(listBottom + 4, bottom, choice, pb_perk_rank_text(choice, true).c_str());
            return;
        }
        const char* description;
        switch (pb_mode) {
        case RT_PIPBOY_MODE_CHOOSE_PERK:
            description = perk_description(choice);
            break;
        case RT_PIPBOY_MODE_CHOOSE_TAG:
            description = skill_description(choice);
            break;
        default:
            description = trait_description(choice);
            break;
        }
        pb_draw_description(listBottom + 4, bottom, labels[pb_choice_cursor].c_str(), description);
    }
}

static void pb_render_stats(int top, int bottom)
{
    int page = pb_page[RT_PIPBOY_TAB_STATS];
    int* cursor = &(pb_cursor[RT_PIPBOY_TAB_STATS][page]);
    int* scroll = &(pb_scroll[RT_PIPBOY_TAB_STATS][page]);

    if (pb_mode == RT_PIPBOY_MODE_CHOOSE_PERK) {
        pb_render_choices(top, bottom, "Choose a perk");
        pb_footer("A Take perk   B Back");
        return;
    }
    if (pb_mode == RT_PIPBOY_MODE_CHOOSE_TAG) {
        pb_render_choices(top, bottom, "Tag!: choose a skill to tag");
        pb_footer("A Tag skill   B Back");
        return;
    }
    if (pb_mode == RT_PIPBOY_MODE_MUTATE_LOSE) {
        pb_render_choices(top, bottom, "Mutate!: choose a trait to lose");
        pb_footer("A Lose trait   B Back");
        return;
    }
    if (pb_mode == RT_PIPBOY_MODE_MUTATE_GAIN) {
        pb_render_choices(top, bottom, "Mutate!: choose a new trait");
        pb_footer("A Take trait   B Back");
        return;
    }
    if (pb_mode == RT_PIPBOY_MODE_CONFIRM_PERK) {
        pb_text(PB_X + 8, top, "Take this perk?", pb_bright);
        pb_draw_perk_card(top + pb_line * 2, bottom, pb_pending_perk, pb_perk_rank_text(pb_pending_perk, true).c_str());
        pb_footer("A Yes   B No");
        return;
    }

    switch (page) {
    case 0:
        pb_stats_status(top, bottom);
        pb_footer("D-pad Scroll   B Close   LS Page   LT/RT Tab");
        break;
    case 1: {
        std::vector<std::string> labels;
        std::vector<std::string> values;
        for (int stat = STAT_STRENGTH; stat <= STAT_LUCK; stat++) {
            int value = stat_level(obj_dude, stat);
            labels.push_back(stat_name(stat));
            values.push_back(std::to_string(value) + "  " + stat_level_description(value));
        }
        int listBottom = bottom - pb_line * 6;
        pb_list_move(0, 7, 7, cursor, scroll);
        pb_draw_list(top, 7, 7, *cursor, 0, labels, &values);
        pb_draw_description(listBottom + 4, bottom, labels[*cursor].c_str(), stat_description(STAT_STRENGTH + *cursor));
        pb_state.listCount = 7;
        pb_footer("D-pad Select   B Close   LS Page   LT/RT Tab");
        break;
    }
    case 2: {
        int unspent = stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS);
        char header[64];
        snprintf(header, sizeof(header), "Skill points: %d", unspent);
        pb_text(PB_X + 8, top, header, unspent > 0 ? pb_bright : pb_green);
        std::vector<std::string> labels;
        std::vector<std::string> values;
        for (int skill = 0; skill < SKILL_COUNT; skill++) {
            std::string label = pb_is_tagged(skill) ? "* " : "  ";
            label += skill_name(skill);
            if (pb_skill_added[skill] > 0) {
                label += " (+" + std::to_string(pb_skill_added[skill]) + ")";
            }
            labels.push_back(label);
            values.push_back(std::to_string(skill_level(obj_dude, skill)) + "%");
        }
        int listTop = top + pb_line + 2;
        int listBottom = bottom - pb_line * 5;
        int visible = (listBottom - listTop) / pb_line;
        pb_list_move(0, SKILL_COUNT, visible, cursor, scroll);
        pb_draw_list(listTop, visible, SKILL_COUNT, *cursor, *scroll, labels, &values);
        pb_draw_description(listBottom + 4, bottom, skill_name(*cursor), skill_description(*cursor));
        pb_state.listCount = SKILL_COUNT;
        pb_footer(unspent > 0 || pb_skill_added[*cursor] > 0 ? "A +1 point   X Undo   Y Skilldex   B Close   LS Page" : "D-pad Select   Y Skilldex   B Close   LS Page   LT/RT Tab");
        break;
    }
    case 3: {
        pb_build_perk_rows();
        std::vector<std::string> labels;
        for (const PbRow& row : pb_rows) {
            if (row.kind == 0) {
                labels.push_back(editor_rt_free_perks() > 1
                        ? "> Choose a new perk (" + std::to_string(editor_rt_free_perks()) + ")"
                        : std::string("> Choose a new perk"));
            } else if (row.kind == 1) {
                std::string label = perk_name(row.id);
                if (perk_level(row.id) > 1) {
                    label += " (" + std::to_string(perk_level(row.id)) + ")";
                }
                labels.push_back(label);
            } else {
                labels.push_back(std::string("Trait: ") + trait_name(row.id));
            }
        }
        if (labels.empty()) {
            pb_text(PB_X + 8, top, "No perks or traits yet.", pb_green);
            pb_footer("B Close   LS Page   LT/RT Tab");
            pb_state.listCount = 0;
            break;
        }
        int listBottom = bottom - PB_PERK_CARD_HEIGHT;
        int visible = (listBottom - top) / pb_line;
        pb_list_move(0, (int)labels.size(), visible, cursor, scroll);
        pb_draw_list(top, visible, (int)labels.size(), *cursor, *scroll, labels);
        const PbRow& row = pb_rows[*cursor];
        if (row.kind == 0) {
            pb_draw_description(listBottom + 4, bottom, "New perk", "You have reached a level that grants a perk. Press A to choose it.");
        } else if (row.kind == 1) {
            pb_draw_perk_card(listBottom + 4, bottom, row.id, pb_perk_rank_text(row.id, false).c_str());
        } else {
            pb_draw_description(listBottom + 4, bottom, trait_name(row.id), trait_description(row.id));
        }
        pb_state.listCount = (int)labels.size();
        pb_footer(row.kind == 0 ? "A Choose perk   B Close   LS Page" : "D-pad Select   B Close   LS Page   LT/RT Tab");
        break;
    }
    }
}

static void pb_start_choices(int mode, const std::vector<int>& choices)
{
    pb_mode = mode;
    pb_choices = choices;
    pb_choice_cursor = 0;
    pb_choice_scroll = 0;
}

static void pb_take_perk(int choiceA, int choiceB)
{
    int perk = pb_pending_perk;
    int rc = editor_rt_take_perk(perk, choiceA, choiceB);
    if (rc == 0) {
        gsound_play_sfx_file("ib1p1xx1");
        pb_say("Perk taken: %s.", perk_name(perk));
        intface_update_hit_points(false);
        intface_update_ac(false);
    } else {
        pb_say("The perk could not be taken (%d).", rc);
    }
    pb_mode = RT_PIPBOY_MODE_NONE;
    pb_pending_perk = -1;
}

static void pb_stats_key(int key)
{
    int page = pb_page[RT_PIPBOY_TAB_STATS];
    int* cursor = &(pb_cursor[RT_PIPBOY_TAB_STATS][page]);
    int* scroll = &(pb_scroll[RT_PIPBOY_TAB_STATS][page]);
    int visible = (pb_body_bottom() - pb_body_top()) / pb_line - 6;

    if (pb_mode == RT_PIPBOY_MODE_CHOOSE_PERK || pb_mode == RT_PIPBOY_MODE_CHOOSE_TAG || pb_mode == RT_PIPBOY_MODE_MUTATE_LOSE || pb_mode == RT_PIPBOY_MODE_MUTATE_GAIN) {
        if (key == KEY_RETURN && pb_choice_cursor < (int)pb_choices.size()) {
            int choice = pb_choices[pb_choice_cursor];
            if (pb_mode == RT_PIPBOY_MODE_CHOOSE_PERK) {
                pb_pending_perk = choice;
                pb_mode = RT_PIPBOY_MODE_CONFIRM_PERK;
            } else if (pb_mode == RT_PIPBOY_MODE_CHOOSE_TAG) {
                pb_take_perk(choice, -1);
            } else if (pb_mode == RT_PIPBOY_MODE_MUTATE_LOSE) {
                pb_mutate_lose = choice;
                int trait1;
                int trait2;
                trait_get(&trait1, &trait2);
                std::vector<int> gain;
                for (int trait = 0; trait < TRAIT_COUNT; trait++) {
                    if (trait != trait1 && trait != trait2) {
                        gain.push_back(trait);
                    }
                }
                pb_start_choices(RT_PIPBOY_MODE_MUTATE_GAIN, gain);
            } else {
                pb_take_perk(pb_mutate_lose, choice);
            }
        } else {
            pb_list_move(key, (int)pb_choices.size(), visible, &pb_choice_cursor, &pb_choice_scroll);
        }
        return;
    }

    if (pb_mode == RT_PIPBOY_MODE_CONFIRM_PERK) {
        if (key != KEY_RETURN) {
            return;
        }
        if (pb_pending_perk == PERK_TAG && perk_level(PERK_TAG) == 0) {
            std::vector<int> skills;
            for (int skill = 0; skill < SKILL_COUNT; skill++) {
                if (!pb_is_tagged(skill)) {
                    skills.push_back(skill);
                }
            }
            pb_start_choices(RT_PIPBOY_MODE_CHOOSE_TAG, skills);
        } else if (pb_pending_perk == PERK_MUTATE) {
            int trait1;
            int trait2;
            trait_get(&trait1, &trait2);
            std::vector<int> owned;
            if (trait1 != -1) {
                owned.push_back(trait1);
            }
            if (trait2 != -1) {
                owned.push_back(trait2);
            }
            if (owned.empty()) {
                pb_mutate_lose = -1;
                std::vector<int> gain;
                for (int trait = 0; trait < TRAIT_COUNT; trait++) {
                    gain.push_back(trait);
                }
                pb_start_choices(RT_PIPBOY_MODE_MUTATE_GAIN, gain);
            } else {
                pb_start_choices(RT_PIPBOY_MODE_MUTATE_LOSE, owned);
            }
        } else {
            pb_take_perk(-1, -1);
        }
        return;
    }

    switch (page) {
    case 0:
        if (key == KEY_ARROW_UP && *scroll > 0) {
            (*scroll)--;
        } else if (key == KEY_ARROW_DOWN) {
            (*scroll)++;
        }
        break;
    case 1:
        pb_list_move(key, 7, 7, cursor, scroll);
        break;
    case 2:
        if (key == KEY_RETURN) {
            int rc = skill_inc_point(obj_dude, *cursor);
            if (rc == 0) {
                pb_skill_added[*cursor]++;
                gsound_play_sfx_file("ib1p1xx1");
            } else if (rc == -4) {
                pb_say("No skill points to spend.");
            } else if (rc == -3) {
                pb_say("%s is at its maximum.", skill_name(*cursor));
            }
        } else if (key == GAMEPAD_KEY_Y || key == KEY_LOWERCASE_Y) {
            // The Skilldex (manual skill use), once the Pip-Boy is closed.
            pb_open_skilldex = true;
            pb_close = true;
        } else if (key == GAMEPAD_KEY_X || key == KEY_LOWERCASE_X) {
            // Only points added since the Pip-Boy was opened can be taken
            // back, as on the original character screen.
            if (pb_skill_added[*cursor] > 0 && skill_dec_point(obj_dude, *cursor) == 0) {
                pb_skill_added[*cursor]--;
                gsound_play_sfx_file("ib1p1xx1");
            } else {
                pb_say("Only points added now can be taken back.");
            }
        } else {
            pb_list_move(key, SKILL_COUNT, visible, cursor, scroll);
        }
        break;
    case 3:
        pb_build_perk_rows();
        if (key == KEY_RETURN && *cursor < (int)pb_rows.size() && pb_rows[*cursor].kind == 0) {
            int perks[PERK_COUNT];
            int count = perk_make_list(perks);
            std::vector<int> choices(perks, perks + count);
            std::sort(choices.begin(), choices.end(), [](int a, int b) {
                return strcmp(perk_name(a), perk_name(b)) < 0;
            });
            if (choices.empty()) {
                pb_say("No perk is available to you right now.");
            } else {
                pb_start_choices(RT_PIPBOY_MODE_CHOOSE_PERK, choices);
            }
        } else {
            pb_list_move(key, (int)pb_rows.size(), visible, cursor, scroll);
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// Inventory
// -----------------------------------------------------------------------------

static int pb_item_category(Object* item)
{
    switch (item_get_type(item)) {
    case ITEM_TYPE_WEAPON:
        return PB_CATEGORY_WEAPONS;
    case ITEM_TYPE_ARMOR:
        return PB_CATEGORY_APPAREL;
    case ITEM_TYPE_DRUG:
        return PB_CATEGORY_AID;
    case ITEM_TYPE_AMMO:
        return PB_CATEGORY_AMMO;
    default:
        return PB_CATEGORY_MISC;
    }
}

static void pb_build_item_rows()
{
    pb_rows.clear();
    int category = pb_page[RT_PIPBOY_TAB_INVENTORY];
    Inventory* inventory = &(obj_dude->data.inventory);

    // Equipped items first, then the rest in inventory order.
    for (int pass = 0; pass < 2; pass++) {
        for (int index = 0; index < inventory->length; index++) {
            Object* item = inventory->items[index].item;
            bool equipped = (item->flags & OBJECT_EQUIPPED) != 0;
            if (equipped != (pass == 0)) {
                continue;
            }
            if (category != PB_CATEGORY_ALL && pb_item_category(item) != category) {
                continue;
            }
            pb_rows.push_back({ 3, inventory->items[index].quantity, item });
        }
    }
}

static Object* pb_active_item()
{
    return intface_is_item_right_hand() ? inven_right_hand(obj_dude) : inven_left_hand(obj_dude);
}

static std::string pb_item_label(Object* item, int quantity)
{
    std::string label;
    if ((item->flags & OBJECT_WORN) != 0) {
        label = "[Worn] ";
    } else if ((item->flags & OBJECT_IN_LEFT_HAND) != 0) {
        label = item == pb_active_item() ? "[Left*] " : "[Left] ";
    } else if ((item->flags & OBJECT_IN_RIGHT_HAND) != 0) {
        label = item == pb_active_item() ? "[Right*] " : "[Right] ";
    }
    label += object_name(item);
    if (quantity > 1) {
        label += " x" + std::to_string(quantity);
    }
    int favorite = rt_favorites_slot_of(item->pid);
    if (favorite != -1) {
        label += std::string("  [") + rt_favorites_slot_name(favorite) + "]";
    }
    return label;
}

static void pb_item_stats(Object* item, std::vector<std::string>& lines)
{
    Proto* proto;
    if (proto_ptr(item->pid, &proto) == -1) {
        return;
    }

    char text[160];
    switch (item_get_type(item)) {
    case ITEM_TYPE_WEAPON: {
        snprintf(text, sizeof(text), "Damage %d-%d (%s)  Range %d",
            proto->item.data.weapon.minDamage,
            proto->item.data.weapon.maxDamage,
            proto->item.data.weapon.damageType >= 0 && proto->item.data.weapon.damageType < 7 ? pb_damage_names[proto->item.data.weapon.damageType] : "?",
            proto->item.data.weapon.maxRange1);
        lines.push_back(text);
        if (proto->item.data.weapon.ammoCapacity > 0) {
            char* ammoName = proto->item.data.weapon.ammoTypePid > 0 ? proto_name(proto->item.data.weapon.ammoTypePid) : NULL;
            snprintf(text, sizeof(text), "Ammo %d/%d %s", item_w_curr_ammo(item), item_w_max_ammo(item), ammoName != NULL ? ammoName : "");
            lines.push_back(text);
        }
        snprintf(text, sizeof(text), "Min ST %d", proto->item.data.weapon.minStrength);
        lines.push_back(text);
        break;
    }
    case ITEM_TYPE_ARMOR:
        snprintf(text, sizeof(text), "AC %d   DT/DR Normal %d/%d%%  Laser %d/%d%%",
            proto->item.data.armor.armorClass,
            proto->item.data.armor.damageThreshold[0], proto->item.data.armor.damageResistance[0],
            proto->item.data.armor.damageThreshold[1], proto->item.data.armor.damageResistance[1]);
        lines.push_back(text);
        snprintf(text, sizeof(text), "Fire %d/%d%%  Plasma %d/%d%%  Explosion %d/%d%%",
            proto->item.data.armor.damageThreshold[2], proto->item.data.armor.damageResistance[2],
            proto->item.data.armor.damageThreshold[3], proto->item.data.armor.damageResistance[3],
            proto->item.data.armor.damageThreshold[6], proto->item.data.armor.damageResistance[6]);
        lines.push_back(text);
        break;
    case ITEM_TYPE_AMMO:
        snprintf(text, sizeof(text), "AC mod %d  DR mod %d  Dmg mod %d/%d",
            proto->item.data.ammo.armorClassModifier,
            proto->item.data.ammo.damageResistanceModifier,
            proto->item.data.ammo.damageMultiplier,
            proto->item.data.ammo.damageDivisor);
        lines.push_back(text);
        break;
    default:
        break;
    }

    snprintf(text, sizeof(text), "Weight %d   Value %d", item_weight(item), item_cost(item));
    lines.push_back(text);
}

static void pb_build_actions(Object* item, std::vector<int>& actions)
{
    int type = item_get_type(item);
    bool equipped = (item->flags & OBJECT_EQUIPPED) != 0;

    if (type == ITEM_TYPE_DRUG
        || ((type == ITEM_TYPE_MISC || type == ITEM_TYPE_WEAPON)
            && (proto_action_can_use(item->pid) || proto_action_can_use_on(item->pid)))) {
        actions.push_back(RT_PIPBOY_ACTION_USE);
    }
    if (type == ITEM_TYPE_ARMOR) {
        if ((item->flags & OBJECT_WORN) == 0) {
            actions.push_back(RT_PIPBOY_ACTION_WEAR);
        }
    } else if (item->pid != PROTO_ID_MONEY) {
        // Thrown weapons only go in the right hand (RB throws them).
        if ((item->flags & OBJECT_IN_LEFT_HAND) == 0 && !rt_is_thrown_weapon(item)) {
            actions.push_back(RT_PIPBOY_ACTION_EQUIP_LEFT);
        }
        if ((item->flags & OBJECT_IN_RIGHT_HAND) == 0) {
            actions.push_back(RT_PIPBOY_ACTION_EQUIP_RIGHT);
        }
    }
    if (equipped) {
        actions.push_back(RT_PIPBOY_ACTION_UNEQUIP);
    }
    if (type == ITEM_TYPE_WEAPON && item_w_can_unload(item)) {
        actions.push_back(RT_PIPBOY_ACTION_UNLOAD);
    }
    actions.push_back(RT_PIPBOY_ACTION_INSPECT);
    if (item->pid != PROTO_ID_MONEY) {
        actions.push_back(RT_PIPBOY_ACTION_DROP);
    }
}

static const char* pb_action_name(int action)
{
    switch (action) {
    case RT_PIPBOY_ACTION_USE:
        return "Use";
    case RT_PIPBOY_ACTION_EQUIP_LEFT:
        return "Hold in left hand";
    case RT_PIPBOY_ACTION_EQUIP_RIGHT:
        return "Hold in right hand";
    case RT_PIPBOY_ACTION_WEAR:
        return "Wear";
    case RT_PIPBOY_ACTION_UNEQUIP:
        return "Put away";
    case RT_PIPBOY_ACTION_UNLOAD:
        return "Unload";
    case RT_PIPBOY_ACTION_INSPECT:
        return "Inspect";
    case RT_PIPBOY_ACTION_DROP:
        return "Drop";
    }
    return "";
}

static void pb_examine_line(char* text)
{
    std::vector<std::string> lines;
    pb_wrap(text, PB_W - 20, lines);
    for (const std::string& line : lines) {
        pb_text_lines.push_back(line);
    }
}

static void pb_show_text(const std::string& title, const std::vector<std::string>& lines)
{
    pb_text_title = title;
    pb_text_lines = lines;
    pb_text_scroll = 0;
    pb_mode = RT_PIPBOY_MODE_TEXT;
}

static void pb_inspect(Object* item)
{
    pb_text_lines.clear();
    obj_examine_func(obj_dude, item, pb_examine_line);
    std::vector<std::string> extra;
    pb_item_stats(item, extra);
    pb_text_lines.push_back("");
    for (const std::string& line : extra) {
        pb_text_lines.push_back(line);
    }
    std::vector<std::string> lines = pb_text_lines;
    pb_show_text(object_name(item), lines);
}

// Runs an engine function that may open its own screen (a timer, a movie,
// a message box): those are driven with the regular controller menu input.
template <typename Function>
static auto pb_with_engine_ui(Function function) -> decltype(function())
{
    gamepad_set_menu_key_mode(false);
    auto result = function();
    gamepad_set_menu_key_mode(true);
    text_font(101);
    return result;
}

static void pb_do_action(int action)
{
    Object* item = pb_item;
    pb_mode = RT_PIPBOY_MODE_NONE;
    if (item == NULL || inven_rt_quantity(item) == 0) {
        return;
    }

    char name[64];
    SDL_strlcpy(name, object_name(item), sizeof(name));

    switch (action) {
    case RT_PIPBOY_ACTION_USE: {
        int rc = pb_with_engine_ui([&]() { return inven_rt_use(item); });
        if (rc == -1) {
            pb_say("Nothing happens.");
        } else {
            pb_say("You use the %s.", name);
        }
        break;
    }
    case RT_PIPBOY_ACTION_EQUIP_LEFT:
    case RT_PIPBOY_ACTION_EQUIP_RIGHT:
        if (inven_rt_equip(item, action == RT_PIPBOY_ACTION_EQUIP_LEFT ? INVEN_RT_SLOT_LEFT_HAND : INVEN_RT_SLOT_RIGHT_HAND) == 0) {
            if (rt_is_thrown_weapon(item)) {
                pb_say("%s in your right hand. RB throws it.", name);
            } else {
                pb_say("%s in your %s hand.", name, action == RT_PIPBOY_ACTION_EQUIP_LEFT ? "left" : "right");
            }
        } else if (rt_is_thrown_weapon(item)) {
            pb_say("Thrown weapons only go in the right hand.");
        }
        break;
    case RT_PIPBOY_ACTION_WEAR:
        if (inven_rt_equip(item, INVEN_RT_SLOT_ARMOR) == 0) {
            pb_say("You put on the %s.", name);
        }
        break;
    case RT_PIPBOY_ACTION_UNEQUIP:
        if (inven_rt_unequip(item) == 0) {
            pb_say("You put the %s away.", name);
        }
        break;
    case RT_PIPBOY_ACTION_UNLOAD: {
        int count = inven_rt_unload(item);
        pb_say(count > 0 ? "You unload the %s." : "The %s is empty.", name);
        break;
    }
    case RT_PIPBOY_ACTION_INSPECT:
        pb_inspect(item);
        return;
    case RT_PIPBOY_ACTION_DROP: {
        int quantity = inven_rt_quantity(item);
        if (quantity > 1) {
            pb_quantity = 1;
            pb_quantity_max = quantity;
            pb_mode = RT_PIPBOY_MODE_QUANTITY;
            return;
        }
        int dropped = pb_with_engine_ui([&]() { return inven_rt_drop(item, 1); });
        if (dropped > 0) {
            pb_say("You drop the %s.", name);
        }
        break;
    }
    }

    gsound_play_sfx_file("ib1p1xx1");
}

static void pb_render_inventory(int top, int bottom)
{
    int page = pb_page[RT_PIPBOY_TAB_INVENTORY];
    int* cursor = &(pb_cursor[RT_PIPBOY_TAB_INVENTORY][page]);
    int* scroll = &(pb_scroll[RT_PIPBOY_TAB_INVENTORY][page]);

    char header[96];
    snprintf(header, sizeof(header), "Weight %d/%d", item_total_weight(obj_dude), stat_level(obj_dude, STAT_CARRY_WEIGHT));
    pb_text(PB_X + 8, top, header, pb_green);
    snprintf(header, sizeof(header), "Caps %d", item_caps_total(obj_dude));
    pb_text_right(PB_X + PB_W - 10, top, header, pb_green);

    if (pb_mode == RT_PIPBOY_MODE_TEXT) {
        return;
    }

    pb_build_item_rows();
    int count = (int)pb_rows.size();
    int detailTop = bottom - PB_ITEM_ART_HEIGHT - pb_line * 2 - 6;
    int listTop = top + pb_line + 4;
    int visible = (detailTop - listTop - 4) / pb_line;
    pb_list_move(0, count, visible, cursor, scroll);
    pb_state.listCount = count;
    pb_state.cursor = *cursor;
    pb_state.cursorItem = count > 0 ? pb_rows[*cursor].item : NULL;

    if (count == 0) {
        pb_text(PB_X + 8, listTop, "Nothing here.", pb_dim);
        pb_footer("LS Category   LT/RT Tab   B Close");
        return;
    }

    std::vector<std::string> labels;
    std::vector<std::string> values;
    for (const PbRow& row : pb_rows) {
        labels.push_back(pb_item_label(row.item, row.id));
        values.push_back(std::to_string(item_weight(row.item)) + " wt  " + std::to_string(item_cost(row.item)) + " $");
    }
    pb_draw_list(listTop, visible, count, *cursor, *scroll, labels, &values);

    // The selected item: its artwork and numbers.
    Object* item = pb_rows[*cursor].item;
    pb_hline(detailTop - 3, pb_dim);
    int artY = detailTop + 2;
    scale_art(item_inv_fid(item), pb_buf + PB_PITCH * artY + PB_X + 6, PB_ITEM_ART_WIDTH, PB_ITEM_ART_HEIGHT, PB_PITCH);
    std::vector<std::string> lines;
    pb_item_stats(item, lines);
    int y = detailTop + 2;
    for (const std::string& line : lines) {
        pb_text(PB_X + PB_ITEM_ART_WIDTH + 14, y, line.c_str(), pb_green, PB_W - PB_ITEM_ART_WIDTH - 20);
        y += pb_line;
    }

    if (pb_mode == RT_PIPBOY_MODE_ACTIONS) {
        // The action menu, over the list.
        int width = 150;
        int x = PB_X + PB_W - width - 8;
        int height = (int)pb_choices.size() * pb_line + 8;
        int boxY = listTop + 4;
        pb_fill(x, boxY, width, height, colorTable[0]);
        draw_box(pb_buf, PB_PITCH, x, boxY, x + width - 1, boxY + height - 1, pb_green);
        for (int index = 0; index < (int)pb_choices.size(); index++) {
            int rowY = boxY + 4 + index * pb_line;
            int color = pb_green;
            if (index == pb_choice_cursor) {
                pb_fill(x + 2, rowY - 1, width - 4, pb_line, pb_bar);
                color = pb_bright;
            }
            const char* name = pb_action_name(pb_choices[index]);
            // Explosives: "Use" sets the timer and arms them (Traps roll).
            if (pb_choices[index] == RT_PIPBOY_ACTION_USE && pb_item != NULL
                && (pb_item->pid == PROTO_ID_DYNAMITE_I || pb_item->pid == PROTO_ID_PLASTIC_EXPLOSIVES_I)) {
                name = "Arm (set timer)";
            }
            pb_text(x + 8, rowY, name, color);
        }
        pb_footer("A Select   B Back");
        return;
    }

    if (pb_mode == RT_PIPBOY_MODE_QUANTITY) {
        int width = 170;
        int x = PB_X + (PB_W - width) / 2;
        int boxY = listTop + 30;
        pb_fill(x, boxY, width, pb_line * 3 + 8, colorTable[0]);
        draw_box(pb_buf, PB_PITCH, x, boxY, x + width - 1, boxY + pb_line * 3 + 7, pb_green);
        char text[64];
        pb_state.quantity = pb_quantity;
        snprintf(text, sizeof(text), "Drop how many?");
        pb_text(x + 8, boxY + 4, text, pb_green);
        snprintf(text, sizeof(text), "<  %d  >   of %d", pb_quantity, pb_quantity_max);
        pb_text(x + 8, boxY + 4 + pb_line * 1 + 4, text, pb_bright);
        pb_footer("Left/Right 1   Up/Down 10   A Drop   B Cancel");
        return;
    }

    pb_footer("A Actions   X Drop   Y Favorite   LS Category   B Close");
}

static void pb_inventory_key(int key)
{
    int page = pb_page[RT_PIPBOY_TAB_INVENTORY];
    int* cursor = &(pb_cursor[RT_PIPBOY_TAB_INVENTORY][page]);
    int* scroll = &(pb_scroll[RT_PIPBOY_TAB_INVENTORY][page]);

    if (pb_mode == RT_PIPBOY_MODE_ACTIONS) {
        if (key == KEY_RETURN && pb_choice_cursor < (int)pb_choices.size()) {
            pb_do_action(pb_choices[pb_choice_cursor]);
        } else if (key == KEY_ARROW_UP || key == KEY_ARROW_DOWN) {
            pb_list_move(key, (int)pb_choices.size(), 20, &pb_choice_cursor, &pb_choice_scroll);
        }
        return;
    }

    if (pb_mode == RT_PIPBOY_MODE_QUANTITY) {
        switch (key) {
        case KEY_ARROW_LEFT:
            pb_quantity = std::max(1, pb_quantity - 1);
            break;
        case KEY_ARROW_RIGHT:
            pb_quantity = std::min(pb_quantity_max, pb_quantity + 1);
            break;
        case KEY_ARROW_DOWN:
            pb_quantity = std::max(1, pb_quantity - 10);
            break;
        case KEY_ARROW_UP:
            pb_quantity = std::min(pb_quantity_max, pb_quantity + 10);
            break;
        case KEY_RETURN: {
            Object* item = pb_item;
            int quantity = pb_quantity;
            char name[64];
            SDL_strlcpy(name, object_name(item), sizeof(name));
            pb_mode = RT_PIPBOY_MODE_NONE;
            int dropped = pb_with_engine_ui([&]() { return inven_rt_drop(item, quantity); });
            pb_say("You drop %d x %s.", dropped, name);
            gsound_play_sfx_file("ib1p1xx1");
            break;
        }
        }
        return;
    }

    pb_build_item_rows();
    int count = (int)pb_rows.size();
    int visible = 18;

    if (count > 0 && *cursor < count) {
        Object* item = pb_rows[*cursor].item;
        if (key == KEY_RETURN) {
            pb_item = item;
            std::vector<int> actions;
            pb_build_actions(item, actions);
            pb_start_choices(RT_PIPBOY_MODE_ACTIONS, actions);
            return;
        }
        if (key == GAMEPAD_KEY_Y || key == KEY_LOWERCASE_Y) {
            // Favorites: the radial opens over the Pip-Boy; B comes back
            // to this same item.
            const char* reason;
            if (!rt_favorites_is_eligible(item, &reason)) {
                pb_say("%s", reason);
                return;
            }
            pb_with_engine_ui([&]() {
                rt_favorites_open_assign(item);
                return 0;
            });
            if (rt_favorites_slot_of(item->pid) != -1) {
                pb_say("%s", rt_favorites_get_state()->lastMessage);
            }
            return;
        }
        if (key == GAMEPAD_KEY_X || key == KEY_LOWERCASE_X) {
            pb_item = item;
            if (item->pid == PROTO_ID_MONEY) {
                pb_say("Caps can't be dropped here.");
            } else {
                pb_do_action(RT_PIPBOY_ACTION_DROP);
            }
            return;
        }
    }

    pb_list_move(key, count, visible, cursor, scroll);
}

// -----------------------------------------------------------------------------
// Map
// -----------------------------------------------------------------------------

static void pb_count_map_pixels(bool player)
{
    int pixels = 0;
    for (int index = 0; index < AUTOMAP_RT_SIZE * AUTOMAP_RT_SIZE; index++) {
        if (pb_map_buf[index] != 0) {
            pixels++;
        }
    }
    pb_state.mapPixels = pixels;
    pb_state.mapPlayer = player;
}

static void pb_blit_map(int top, int bottom, int playerX, int playerY, bool center)
{
    int viewWidth = PB_W - 12;
    int viewHeight = bottom - top - 4;
    int left = PB_X + 6;

    if (center && playerX >= 0) {
        pb_map_pan_x = playerX - viewWidth * pb_map_zoom / 2;
        pb_map_pan_y = playerY - viewHeight * pb_map_zoom / 2;
    }

    int maxPanX = std::max(0, AUTOMAP_RT_SIZE - viewWidth * pb_map_zoom);
    int maxPanY = std::max(0, AUTOMAP_RT_SIZE - viewHeight * pb_map_zoom);
    pb_map_pan_x = std::max(0, std::min(maxPanX, pb_map_pan_x));
    pb_map_pan_y = std::max(0, std::min(maxPanY, pb_map_pan_y));

    // Zoomed out, the whole 400x400 map is centered.
    int offsetX = 0;
    int offsetY = 0;
    if (pb_map_zoom > 1) {
        offsetX = std::max(0, (viewWidth - AUTOMAP_RT_SIZE / pb_map_zoom) / 2);
        offsetY = std::max(0, (viewHeight - AUTOMAP_RT_SIZE / pb_map_zoom) / 2);
    }

    for (int y = 0; y < viewHeight; y++) {
        int sy = pb_map_pan_y + (y - offsetY) * pb_map_zoom;
        if (sy < 0 || sy >= AUTOMAP_RT_SIZE) {
            continue;
        }
        unsigned char* dest = pb_buf + PB_PITCH * (top + 2 + y) + left;
        for (int x = 0; x < viewWidth; x++) {
            int sx = pb_map_pan_x + (x - offsetX) * pb_map_zoom;
            if (sx < 0 || sx >= AUTOMAP_RT_SIZE) {
                continue;
            }
            unsigned char pixel = pb_map_buf[AUTOMAP_RT_SIZE * sy + sx];
            if (pixel == 0 && pb_map_zoom > 1) {
                // Zoomed out, keep thin walls visible.
                pixel = pb_map_buf[AUTOMAP_RT_SIZE * sy + std::min(AUTOMAP_RT_SIZE - 1, sx + 1)];
                if (pixel == 0 && sy + 1 < AUTOMAP_RT_SIZE) {
                    pixel = pb_map_buf[AUTOMAP_RT_SIZE * (sy + 1) + sx];
                }
            }
            if (pixel != 0) {
                dest[x] = pixel;
            }
        }
    }

    if (playerX >= 0 && (SDL_GetTicks() / 400) % 2 == 0) {
        int px = left + offsetX + (playerX - pb_map_pan_x) / pb_map_zoom;
        int py = top + 2 + offsetY + (playerY - pb_map_pan_y) / pb_map_zoom;
        if (px > left + 2 && px < left + viewWidth - 3 && py > top + 3 && py < bottom - 3) {
            pb_fill(px - 2, py, 5, 1, pb_red);
            pb_fill(px, py - 2, 1, 5, pb_red);
        }
    }
}

static void pb_map_scroll_key(int key)
{
    int step = 24;
    switch (key) {
    case KEY_ARROW_LEFT:
        pb_map_pan_x -= step;
        break;
    case KEY_ARROW_RIGHT:
        pb_map_pan_x += step;
        break;
    case KEY_ARROW_UP:
        pb_map_pan_y -= step;
        break;
    case KEY_ARROW_DOWN:
        pb_map_pan_y += step;
        break;
    case GAMEPAD_KEY_X:
    case KEY_LOWERCASE_X:
        pb_map_zoom = pb_map_zoom == 1 ? 2 : 1;
        pb_map_centered = false;
        break;
    }
}

static void pb_render_local_map(int top, int bottom)
{
    int map = map_get_index_number();
    int elevation = map_elevation;
    char title[128];
    snprintf(title, sizeof(title), "%s - %s",
        map >= 0 ? map_get_short_name(map) : "?",
        map >= 0 ? map_get_elev_idx(map, elevation) : "");
    pb_text(PB_X + 8, top, title, pb_bright);

    if (map < 0 || !automap_rt_available(map, elevation)) {
        pb_text(PB_X + 8, top + pb_line * 2, "No automap is kept for this location.", pb_green);
        pb_footer("LS Page   LT/RT Tab   B Close");
        return;
    }

    memset(pb_map_buf, 0, AUTOMAP_RT_SIZE * AUTOMAP_RT_SIZE);
    int playerX;
    int playerY;
    automap_rt_render_live(pb_map_buf, elevation, pb_map_details, &playerX, &playerY);

    if (pb_scanner) {
        // Motion sensor: living critters.
        for (Object* obj = obj_find_first_at(elevation); obj != NULL; obj = obj_find_next_at()) {
            if (FID_TYPE(obj->fid) != OBJ_TYPE_CRITTER || obj == obj_dude || obj->tile == -1
                || (obj->flags & OBJECT_HIDDEN) != 0 || critter_is_dead(obj)) {
                continue;
            }
            int x = AUTOMAP_RT_SIZE - 2 - 2 * (obj->tile % 200);
            int y = 2 * (obj->tile / 200);
            if (x >= 0 && x + 1 < AUTOMAP_RT_SIZE && y >= 0 && y + 1 < AUTOMAP_RT_SIZE) {
                pb_map_buf[AUTOMAP_RT_SIZE * y + x] = pb_red;
                pb_map_buf[AUTOMAP_RT_SIZE * y + x + 1] = pb_red;
                pb_map_buf[AUTOMAP_RT_SIZE * (y + 1) + x] = pb_red;
            }
        }
    }

    pb_count_map_pixels(playerX >= 0);
    pb_blit_map(top + pb_line, bottom, playerX, playerY, !pb_map_centered);
    pb_map_centered = true;
    pb_footer(pb_scanner
            ? "D-pad/RS Pan   X Zoom   Y Detail   Motion sensor on"
            : "D-pad/RS Pan   X Zoom   Y Detail   LS Page   B Close");
}

typedef struct PbSavedMap {
    int map;
    int elevation;
} PbSavedMap;

static std::vector<PbSavedMap> pb_saved_maps;

static void pb_collect_saved_maps()
{
    pb_saved_maps.clear();
    AutomapHeader* header;
    if (ReadAMList(&header) == -1) {
        return;
    }
    for (int map = 0; map < AUTOMAP_MAP_COUNT; map++) {
        for (int elevation = 0; elevation < ELEVATION_COUNT; elevation++) {
            if (header->offsets[map][elevation] > 0) {
                pb_saved_maps.push_back({ map, elevation });
            }
        }
    }
}

static void pb_render_saved_maps(int top, int bottom)
{
    int* cursor = &(pb_cursor[RT_PIPBOY_TAB_MAP][1]);
    int* scroll = &(pb_scroll[RT_PIPBOY_TAB_MAP][1]);

    if (pb_mode == RT_PIPBOY_MODE_SAVED_MAP) {
        char title[128];
        snprintf(title, sizeof(title), "%s - %s", map_get_short_name(pb_saved_map), map_get_elev_idx(pb_saved_map, pb_saved_elevation));
        pb_text(PB_X + 8, top, title, pb_bright);
        memset(pb_map_buf, 0, AUTOMAP_RT_SIZE * AUTOMAP_RT_SIZE);
        automap_rt_render_saved(pb_map_buf, pb_saved_map, pb_saved_elevation);
        pb_count_map_pixels(false);
        pb_blit_map(top + pb_line, bottom, -1, -1, false);
        pb_footer("D-pad/RS Pan   X Zoom   B Back");
        return;
    }

    pb_collect_saved_maps();
    pb_text(PB_X + 8, top, "Automaps of places visited", pb_bright);
    std::vector<std::string> labels;
    for (const PbSavedMap& entry : pb_saved_maps) {
        labels.push_back(std::string(map_get_short_name(entry.map)) + " - " + map_get_elev_idx(entry.map, entry.elevation));
    }
    int count = (int)labels.size();
    pb_state.listCount = count;
    if (count == 0) {
        pb_text(PB_X + 8, top + pb_line * 2, "No automaps saved yet (they are saved when leaving a map).", pb_green);
        pb_footer("LS Page   LT/RT Tab   B Close");
        return;
    }
    int visible = (bottom - top - pb_line) / pb_line;
    pb_list_move(0, count, visible, cursor, scroll);
    pb_draw_list(top + pb_line + 2, visible, count, *cursor, *scroll, labels);
    pb_footer("A View   B Close   LS Page   LT/RT Tab");
}

static void pb_render_world(int top, int bottom)
{
    pb_text(PB_X + 8, top, "World map (travel from the world map)", pb_bright);

    if (pb_world_art == NULL) {
        pb_world_art = art_lock(art_id(OBJ_TYPE_INTERFACE, 135, 0, 0, 0), &pb_world_key, &pb_world_width, &pb_world_height);
    }
    if (pb_world_art == NULL) {
        pb_text(PB_X + 8, top + pb_line * 2, "World map artwork not found.", pb_red);
        return;
    }

    int viewWidth = PB_W - 12;
    int viewHeight = bottom - top - pb_line - 4;
    int left = PB_X + 6;
    int viewTop = top + pb_line + 2;

    int playerX;
    int playerY;
    worldmap_rt_position(&playerX, &playerY);
    if (!pb_world_centered) {
        pb_world_x = playerX - viewWidth * pb_world_zoom / 2;
        pb_world_y = playerY - viewHeight * pb_world_zoom / 2;
        pb_world_centered = true;
    }
    pb_world_x = std::max(0, std::min(std::max(0, pb_world_width - viewWidth * pb_world_zoom), pb_world_x));
    pb_world_y = std::max(0, std::min(std::max(0, pb_world_height - viewHeight * pb_world_zoom), pb_world_y));

    int fog = colorTable[0];
    int seenPixels = 0;
    for (int y = 0; y < viewHeight; y++) {
        int wy = pb_world_y + y * pb_world_zoom;
        if (wy >= pb_world_height) {
            break;
        }
        unsigned char* dest = pb_buf + PB_PITCH * (viewTop + y) + left;
        for (int x = 0; x < viewWidth; x++) {
            int wx = pb_world_x + x * pb_world_zoom;
            if (wx >= pb_world_width) {
                break;
            }
            int cell = worldmap_rt_cell(wy / WORLDMAP_RT_CELL_SIZE, wx / WORLDMAP_RT_CELL_SIZE);
            dest[x] = cell == 0 ? fog : pb_world_art[pb_world_width * wy + wx];
            if (cell != 0) {
                seenPixels++;
            }
        }
    }

    pb_state.mapPixels = seenPixels;
    pb_state.mapPlayer = true;
    if (seenPixels == 0) {
        // A new game: the world map reveals the land as you travel it.
        pb_text(left + 6, viewTop + 6, "Nothing explored yet: the land shows as", pb_green);
        pb_text(left + 6, viewTop + 6 + pb_line, "you travel the world map.", pb_green);
    }

    // Towns visited, and the player.
    for (int town = 0; town < TOWN_COUNT; town++) {
        if (!worldmap_rt_town_visited(town)) {
            continue;
        }
        int tx;
        int ty;
        worldmap_rt_town_position(town, &tx, &ty);
        int sx = left + (tx - pb_world_x) / pb_world_zoom;
        int sy = viewTop + (ty - pb_world_y) / pb_world_zoom;
        if (sx > left && sx < left + viewWidth - 4 && sy > viewTop && sy < viewTop + viewHeight - pb_line) {
            pb_fill(sx - 1, sy - 1, 3, 3, pb_bright);
            const char* name = pipboy_rt_getmsg(700 + 10 * town);
            pb_text(sx + 4, sy - pb_line / 2, name, pb_bright, left + viewWidth - sx - 4);
        }
    }
    int px = left + (playerX - pb_world_x) / pb_world_zoom;
    int py = viewTop + (playerY - pb_world_y) / pb_world_zoom;
    if ((SDL_GetTicks() / 400) % 2 == 0 && px > left + 2 && px < left + viewWidth - 3 && py > viewTop + 2 && py < viewTop + viewHeight - 3) {
        pb_fill(px - 3, py, 7, 1, pb_red);
        pb_fill(px, py - 3, 1, 7, pb_red);
    }

    pb_footer("D-pad/RS Pan   X Zoom   LS Page   B Close");
}

static void pb_render_map(int top, int bottom)
{
    switch (pb_page[RT_PIPBOY_TAB_MAP]) {
    case 0:
        pb_render_local_map(top, bottom);
        break;
    case 1:
        pb_render_saved_maps(top, bottom);
        break;
    default:
        pb_render_world(top, bottom);
        break;
    }
}

static void pb_map_key(int key)
{
    int page = pb_page[RT_PIPBOY_TAB_MAP];
    if (page == 0) {
        if (key == GAMEPAD_KEY_Y || key == KEY_LOWERCASE_Y) {
            pb_map_details = !pb_map_details;
        } else {
            pb_map_scroll_key(key);
        }
        return;
    }

    if (page == 1) {
        if (pb_mode == RT_PIPBOY_MODE_SAVED_MAP) {
            pb_map_scroll_key(key);
            return;
        }
        int* cursor = &(pb_cursor[RT_PIPBOY_TAB_MAP][1]);
        int* scroll = &(pb_scroll[RT_PIPBOY_TAB_MAP][1]);
        pb_collect_saved_maps();
        if (key == KEY_RETURN && *cursor < (int)pb_saved_maps.size()) {
            pb_saved_map = pb_saved_maps[*cursor].map;
            pb_saved_elevation = pb_saved_maps[*cursor].elevation;
            pb_map_pan_x = 0;
            pb_map_pan_y = 0;
            pb_mode = RT_PIPBOY_MODE_SAVED_MAP;
        } else {
            pb_list_move(key, (int)pb_saved_maps.size(), 25, cursor, scroll);
        }
        return;
    }

    int step = 24 * pb_world_zoom;
    switch (key) {
    case KEY_ARROW_LEFT:
        pb_world_x -= step;
        break;
    case KEY_ARROW_RIGHT:
        pb_world_x += step;
        break;
    case KEY_ARROW_UP:
        pb_world_y -= step;
        break;
    case KEY_ARROW_DOWN:
        pb_world_y += step;
        break;
    case GAMEPAD_KEY_X:
    case KEY_LOWERCASE_X:
        pb_world_zoom = pb_world_zoom == 4 ? 2 : 4;
        pb_world_centered = false;
        break;
    }
}

// -----------------------------------------------------------------------------
// Data
// -----------------------------------------------------------------------------

static int pb_quest_value(int location, int quest)
{
    int gvar = pipboy_rt_quest_gvar(location, quest);
    if (gvar == 0) {
        return 0;
    }
    int value = game_global_vars[gvar];
    // As the original status list: the water chip quest is always known.
    if (gvar == GVAR_FIND_WATER_CHIP) {
        value = value > 1 ? 2 : 1;
    }
    return value;
}

static void pb_build_quest_rows()
{
    pb_rows.clear();
    for (int location = 0; location < pipboy_rt_quest_location_count(); location++) {
        for (int quest = 0; quest < pipboy_rt_quests_per_location(); quest++) {
            if (pipboy_rt_quest_gvar(location, quest) == 0) {
                break;
            }
            if (pb_quest_value(location, quest) > 0) {
                pb_rows.push_back({ 4, location, NULL });
                break;
            }
        }
    }
}

static void pb_open_quests(int location)
{
    std::vector<std::string> lines;
    int number = 1;
    for (int quest = 0; quest < pipboy_rt_quests_per_location(); quest++) {
        if (pipboy_rt_quest_gvar(location, quest) == 0) {
            break;
        }
        int value = pb_quest_value(location, quest);
        if (value <= 0) {
            continue;
        }
        char text[1024];
        snprintf(text, sizeof(text), "%d. %s%s", number++, pipboy_rt_getmsg(701 + 10 * location + quest), value > 1 ? "  (done)" : "");
        std::vector<std::string> wrapped;
        pb_wrap(text, PB_W - 20, wrapped);
        for (const std::string& line : wrapped) {
            lines.push_back((value > 1 ? "~" : "") + line);
        }
        lines.push_back("");
    }
    pb_show_text(std::string(pipboy_rt_getmsg(700 + 10 * location)), lines);
}

static void pb_open_holodisk(int holodisk)
{
    std::vector<std::string> lines;
    for (int id = 1000 * holodisk + 1000; id < 1000 * holodisk + 1500; id++) {
        const char* text = pipboy_rt_getmsg(id);
        if (strcmp(text, "**END-DISK**") == 0) {
            break;
        }
        if (strcmp(text, "**END-PAR**") == 0) {
            lines.push_back("");
        } else {
            pb_wrap(text, PB_W - 20, lines);
        }
    }
    pb_show_text(pipboy_rt_getmsg(400 + holodisk), lines);
}

static void pb_render_data(int top, int bottom)
{
    int page = pb_page[RT_PIPBOY_TAB_DATA];
    int* cursor = &(pb_cursor[RT_PIPBOY_TAB_DATA][page]);
    int* scroll = &(pb_scroll[RT_PIPBOY_TAB_DATA][page]);
    int visible = (bottom - top - pb_line) / pb_line;
    std::vector<std::string> labels;

    switch (page) {
    case 0:
        pb_build_quest_rows();
        pb_text(PB_X + 8, top, pipboy_rt_getmsg(202), pb_bright);
        for (const PbRow& row : pb_rows) {
            labels.push_back(pipboy_rt_getmsg(700 + 10 * row.id));
        }
        if (labels.empty()) {
            pb_text(PB_X + 8, top + pb_line * 2, pipboy_rt_getmsg(203), pb_green);
        }
        pb_footer("A Open   B Close   LS Page   LT/RT Tab");
        break;
    case 1:
        pb_rows.clear();
        pb_text(PB_X + 8, top, pipboy_rt_getmsg(211), pb_bright);
        for (int holodisk = 0; holodisk < pipboy_rt_holodisk_count(); holodisk++) {
            if (game_global_vars[pipboy_rt_holodisk_gvar(holodisk)] != 0) {
                pb_rows.push_back({ 5, holodisk, NULL });
                labels.push_back(pipboy_rt_getmsg(400 + holodisk));
            }
        }
        if (labels.empty()) {
            pb_text(PB_X + 8, top + pb_line * 2, "No holodisks.", pb_green);
        }
        pb_footer("A Read   B Close   LS Page   LT/RT Tab");
        break;
    case 2:
        pb_rows.clear();
        pb_text(PB_X + 8, top, pipboy_rt_getmsg(206), pb_bright);
        for (int movie = MOVIE_VEXPLD; movie < MOVIE_COUNT; movie++) {
            if (gmovie_has_been_played(movie)) {
                pb_rows.push_back({ 6, movie, NULL });
                labels.push_back(pipboy_rt_getmsg(500 + movie));
            }
        }
        if (labels.empty()) {
            pb_text(PB_X + 8, top + pb_line * 2, "No recordings.", pb_green);
        }
        pb_footer("A Play   B Close   LS Page   LT/RT Tab");
        break;
    default: {
        pb_rows.clear();
        pb_text(PB_X + 8, top, pipboy_rt_getmsg(300), pb_bright);
        char text[96];
        snprintf(text, sizeof(text), "%s %d/%d", pipboy_rt_getmsg(301), critter_get_hits(obj_dude), stat_level(obj_dude, STAT_MAXIMUM_HIT_POINTS));
        pb_text_right(PB_X + PB_W - 10, top, text, pb_green);
        if (!pipboy_rt_can_rest()) {
            pb_text(PB_X + 8, top + pb_line * 2, pipboy_rt_getmsg(215), pb_red);
            pb_footer("LS Page   LT/RT Tab   B Close");
            pb_state.listCount = 0;
            return;
        }
        for (int option = 0; option < PIPBOY_RT_REST_OPTION_COUNT; option++) {
            pb_rows.push_back({ 7, option, NULL });
            labels.push_back(pipboy_rt_getmsg(302 + option));
        }
        pb_footer("A Rest   B Cancel rest / Close   LS Page");
        break;
    }
    }

    int count = (int)labels.size();
    pb_state.listCount = count;
    if (count > 0) {
        pb_list_move(0, count, visible, cursor, scroll);
        pb_draw_list(top + pb_line + 4, visible, count, *cursor, *scroll, labels);
    }
}

static void pb_data_key(int key)
{
    int page = pb_page[RT_PIPBOY_TAB_DATA];
    int* cursor = &(pb_cursor[RT_PIPBOY_TAB_DATA][page]);
    int* scroll = &(pb_scroll[RT_PIPBOY_TAB_DATA][page]);
    int count = (int)pb_rows.size();

    if (key == KEY_RETURN && *cursor < count) {
        const PbRow& row = pb_rows[*cursor];
        switch (row.kind) {
        case 4:
            pb_open_quests(row.id);
            break;
        case 5:
            pb_open_holodisk(row.id);
            break;
        case 6: {
            int movie = row.id;
            pb_with_engine_ui([&]() { return gmovie_play(movie, GAME_MOVIE_FADE_IN | GAME_MOVIE_FADE_OUT | GAME_MOVIE_PAUSE_MUSIC); });
            break;
        }
        case 7: {
            int option = row.id;
            gsound_play_sfx_file("ib1p1xx1");
            bool interrupted = pipboy_rt_rest(option);
            pipboy_rt_draw_clock();
            if (interrupted) {
                // Something happened (an encounter, a scripted event): the
                // game takes over, as with the original alarm clock.
                pb_close = true;
            } else {
                pb_say("You rest.");
            }
            break;
        }
        }
        return;
    }

    pb_list_move(key, count, 25, cursor, scroll);
}

// -----------------------------------------------------------------------------
// Text view (inspect, quests, holodisks)
// -----------------------------------------------------------------------------

static void pb_render_text(int top, int bottom)
{
    pb_text(PB_X + 8, top, pb_text_title.c_str(), pb_bright);
    int visible = (bottom - top - pb_line) / pb_line;
    int count = (int)pb_text_lines.size();
    if (pb_text_scroll > std::max(0, count - visible)) {
        pb_text_scroll = std::max(0, count - visible);
    }
    for (int index = pb_text_scroll; index < count && index < pb_text_scroll + visible; index++) {
        const std::string& line = pb_text_lines[index];
        int y = top + pb_line + 4 + (index - pb_text_scroll) * pb_line;
        // "~" marks completed quests: dimmed and struck through, as before.
        if (!line.empty() && line[0] == '~') {
            pb_text(PB_X + 8, y, line.c_str() + 1, pb_dim);
            draw_line(pb_buf, PB_PITCH, PB_X + 8, y + text_height() / 2, PB_X + 8 + text_width(line.c_str() + 1), y + text_height() / 2, pb_dim);
        } else {
            pb_text(PB_X + 8, y, line.c_str(), pb_green);
        }
    }
    if (pb_text_scroll > 0) {
        pb_text(PB_X + PB_W - 10, top + pb_line, "^", pb_bright);
    }
    if (pb_text_scroll + visible < count) {
        pb_text(PB_X + PB_W - 10, bottom - pb_line, "v", pb_bright);
    }
    pb_state.listCount = count;
    pb_footer("D-pad Scroll   B Back");
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------

static void pb_render()
{
    pb_restore(PB_X, PB_Y, PB_W, PB_H);
    pb_draw_panel();
    pb_draw_header();

    int top = pb_body_top();
    int bottom = pb_body_bottom();
    pb_state.listCount = 0;
    pb_state.cursorItem = NULL;
    pb_state.mapPixels = 0;
    pb_state.mapPlayer = false;
    pb_state.perkCard = -1;
    pb_state.perkArtFid = -1;

    if (pb_mode == RT_PIPBOY_MODE_TEXT) {
        if (pb_tab == RT_PIPBOY_TAB_INVENTORY) {
            pb_render_inventory(top, bottom);
            top += pb_line + 4;
        }
        pb_render_text(top, bottom);
    } else {
        switch (pb_tab) {
        case RT_PIPBOY_TAB_STATS:
            pb_render_stats(top, bottom);
            break;
        case RT_PIPBOY_TAB_INVENTORY:
            pb_render_inventory(top, bottom);
            break;
        case RT_PIPBOY_TAB_MAP:
            pb_render_map(top, bottom);
            break;
        default:
            pb_render_data(top, bottom);
            break;
        }
    }

    pb_state.tab = pb_tab;
    pb_state.page = pb_page[pb_tab];
    pb_state.mode = pb_mode;
    pb_state.choiceCount = std::min((int)pb_choices.size(), 64);
    for (int index = 0; index < pb_state.choiceCount; index++) {
        pb_state.choices[index] = pb_choices[index];
    }
    pb_state.choiceCursor = pb_choice_cursor;
    pb_state.mapPanX = pb_map_pan_x;
    pb_state.mapPanY = pb_map_pan_y;
    pb_state.worldX = pb_world_x;
    pb_state.worldY = pb_world_y;
    if (pb_mode == RT_PIPBOY_MODE_NONE) {
        pb_state.cursor = pb_cursor[pb_tab][pb_page[pb_tab]];
    }

    win_draw(pb_win);
}

static void pb_set_tab(int tab)
{
    pb_tab = (tab + RT_PIPBOY_TAB_COUNT) % RT_PIPBOY_TAB_COUNT;
    pb_mode = RT_PIPBOY_MODE_NONE;
    pb_map_centered = false;
    gsound_play_sfx_file("ib1p1xx1");
    rt_log("pipboy: tab %s", pb_tab_names[pb_tab]);
}

static void pb_set_page(int delta)
{
    int count = pb_page_counts[pb_tab];
    pb_page[pb_tab] = (pb_page[pb_tab] + delta + count) % count;
    pb_mode = RT_PIPBOY_MODE_NONE;
    pb_map_centered = false;
    gsound_play_sfx_file("ib1p1xx1");
}

// B: one level back; from a main page it closes the Pip-Boy.
static void pb_back()
{
    switch (pb_mode) {
    case RT_PIPBOY_MODE_NONE:
        pb_close = true;
        break;
    case RT_PIPBOY_MODE_CONFIRM_PERK:
    case RT_PIPBOY_MODE_CHOOSE_TAG:
    case RT_PIPBOY_MODE_MUTATE_LOSE:
    case RT_PIPBOY_MODE_MUTATE_GAIN:
        pb_mode = RT_PIPBOY_MODE_CHOOSE_PERK;
        pb_pending_perk = -1;
        {
            int perks[PERK_COUNT];
            int count = perk_make_list(perks);
            std::vector<int> choices(perks, perks + count);
            std::sort(choices.begin(), choices.end(), [](int a, int b) {
                return strcmp(perk_name(a), perk_name(b)) < 0;
            });
            pb_choices = choices;
        }
        break;
    default:
        pb_mode = RT_PIPBOY_MODE_NONE;
        break;
    }
}

static void pb_handle_key(int key)
{
    switch (key) {
    // Main tabs: LT/RT (one tab per pull; the press is an edge, so a held
    // trigger never repeats). Menu presses never reach gameplay, so they
    // can't fire or lock on either.
    case GAMEPAD_KEY_LT:
    case GAMEPAD_KEY_RT:
    case KEY_LOWERCASE_Q:
    case KEY_UPPERCASE_Q:
    case KEY_LOWERCASE_E:
    case KEY_UPPERCASE_E:
        pb_set_tab(pb_tab + (key == GAMEPAD_KEY_LT || key == KEY_LOWERCASE_Q || key == KEY_UPPERCASE_Q ? -1 : 1));
        return;
    // Subcategories: a horizontal push of the left stick (one per push, see
    // `gamepad_set_stick_tabs`), only on a tab's own page. A nested control
    // comes first: the quantity selector takes left/right as its amount,
    // the others (action menu, perk choices, text, a saved map) ignore it.
    case GAMEPAD_KEY_LS_LEFT:
    case GAMEPAD_KEY_LS_RIGHT:
        if (pb_mode == RT_PIPBOY_MODE_QUANTITY) {
            pb_inventory_key(key == GAMEPAD_KEY_LS_LEFT ? KEY_ARROW_LEFT : KEY_ARROW_RIGHT);
        } else if (pb_mode == RT_PIPBOY_MODE_NONE) {
            pb_set_page(key == GAMEPAD_KEY_LS_LEFT ? -1 : 1);
        } else {
            pb_state.nestedIgnored++;
        }
        return;
    case GAMEPAD_KEY_START:
        pb_close = true;
        return;
    case KEY_ESCAPE:
        pb_back();
        return;
    // LB/RB: no longer change pages or tabs; in a text view they scroll a
    // page.
    case KEY_PAGE_UP:
        if (pb_mode == RT_PIPBOY_MODE_TEXT) {
            pb_text_scroll = std::max(0, pb_text_scroll - 10);
        }
        return;
    case KEY_PAGE_DOWN:
        if (pb_mode == RT_PIPBOY_MODE_TEXT) {
            pb_text_scroll += 10;
        }
        return;
    }

    if (key >= PB_EVENT_BUTTON && key < PB_EVENT_BUTTON + 5) {
        int index = key - PB_EVENT_BUTTON;
        if (index == 4) {
            pb_close = true;
        } else {
            pb_set_tab(index);
        }
        return;
    }

    if (pb_mode == RT_PIPBOY_MODE_TEXT) {
        if (key == KEY_ARROW_UP && pb_text_scroll > 0) {
            pb_text_scroll--;
        } else if (key == KEY_ARROW_DOWN) {
            pb_text_scroll++;
        }
        return;
    }

    switch (pb_tab) {
    case RT_PIPBOY_TAB_STATS:
        pb_stats_key(key);
        break;
    case RT_PIPBOY_TAB_INVENTORY:
        pb_inventory_key(key);
        break;
    case RT_PIPBOY_TAB_MAP:
        pb_map_key(key);
        break;
    default:
        pb_data_key(key);
        break;
    }
}

// The HUD's LEVEL indicator (PC_FLAG_LEVEL_UP_AVAILABLE). The original
// cleared it whenever the character screen closed, which the Pip-Boy
// replaced, so nothing cleared it any more. It now follows what is still
// to do: on while a perk can be chosen; off once the level's skill points
// are all spent, or once the player has looked at the Stats tab and closed
// the Pip-Boy with points left (those are banked, as the original allowed).
static bool pb_stats_seen = false;

static bool pb_perk_pending()
{
    int perks[PERK_COUNT];
    return editor_rt_has_free_perk() && perk_make_list(perks) > 0;
}

static void pb_update_level_flag(bool closing)
{
    bool before = is_pc_flag(PC_FLAG_LEVEL_UP_AVAILABLE);
    if (pb_perk_pending()) {
        pc_flag_on(PC_FLAG_LEVEL_UP_AVAILABLE);
    } else if (before && (stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS) == 0 || (closing && pb_stats_seen))) {
        pc_flag_off(PC_FLAG_LEVEL_UP_AVAILABLE);
    }

    bool after = is_pc_flag(PC_FLAG_LEVEL_UP_AVAILABLE);
    if (after != before) {
        refresh_box_bar_win();
        rt_log("pipboy: level-up indicator %s (perk pending %d, unspent skill points %d)",
            after ? "on" : "off", pb_perk_pending(), stat_pc_get(PC_STAT_UNSPENT_SKILL_POINTS));
    }
}

static void pb_loop()
{
    unsigned int lastPan = SDL_GetTicks();
    while (!pb_close) {
        sharedFpsLimiter.mark();

        // Stick tabs are on whenever the Pip-Boy itself has the controller
        // (a screen opened from it turns them off).
        if (!gamepad_is_stick_tabs()) {
            gamepad_set_stick_tabs(true);
        }

        int key = get_input();
        if (game_user_wants_to_quit != 0) {
            break;
        }

        if (key != -1) {
            pb_handle_key(key);
        }

        // Right stick pans the maps.
        if (pb_tab == RT_PIPBOY_TAB_MAP) {
            const GamepadState* pad = gamepad_get_state();
            unsigned int now = SDL_GetTicks();
            float dt = (now - lastPan) / 1000.0f;
            if (pad->rightX != 0.0f || pad->rightY != 0.0f) {
                int dx = (int)(pad->rightX * 300.0f * dt);
                int dy = (int)(pad->rightY * 300.0f * dt);
                if (pb_page[RT_PIPBOY_TAB_MAP] == 2) {
                    pb_world_x += dx * pb_world_zoom;
                    pb_world_y += dy * pb_world_zoom;
                } else {
                    pb_map_pan_x += dx;
                    pb_map_pan_y += dy;
                }
            }
            lastPan = now;
        }

        if (pb_tab == RT_PIPBOY_TAB_STATS) {
            pb_stats_seen = true;
        }
        pb_update_level_flag(false);

        if (!pb_close) {
            pb_render();
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }
}

void rt_pipboy_open(int tab, int page)
{
    if (pb_open) {
        return;
    }

    if (tab >= 0 && tab < RT_PIPBOY_TAB_COUNT) {
        pb_tab = tab;
    }
    if (page >= 0 && page < pb_page_counts[pb_tab]) {
        pb_page[pb_tab] = page;
    }

    // Level-up bookkeeping (skill points, perk due), as the character
    // screen did when it opened.
    editor_rt_update_level();

    // The world (animations, background scripts) stops while it is open.
    bool isoWasEnabled = map_disable_bk_processes();

    if (pipboy_rt_begin(&pb_win, &pb_buf, &pb_bg) != 0) {
        rt_log("pipboy: could not open");
        if (isoWasEnabled) {
            map_enable_bk_processes();
        }
        return;
    }

    pb_open = true;
    pb_close = false;
    pb_open_skilldex = false;
    pb_mode = RT_PIPBOY_MODE_NONE;
    pb_map_centered = false;
    pb_world_centered = false;
    pb_message[0] = '\0';
    memset(pb_skill_added, 0, sizeof(pb_skill_added));
    pb_stats_seen = false;
    pb_map_buf = new unsigned char[AUTOMAP_RT_SIZE * AUTOMAP_RT_SIZE];

    pb_line = text_height() + 2;
    pb_green = colorTable[992];
    pb_bright = colorTable[32747];
    pb_dim = colorTable[8804];
    pb_bar = colorTable[(0 << 10) | (9 << 5) | 0];
    pb_red = colorTable[31744];

    for (int index = 0; index < 5; index++) {
        win_register_button(pb_win, PB_BUTTON_X - 3, PB_BUTTON_Y + PB_BUTTON_STEP * index - 2, 125, 20,
            -1, -1, -1, PB_EVENT_BUTTON + index, NULL, NULL, NULL, BUTTON_FLAG_TRANSPARENT);
    }

    // Controller-only: the cursor is hidden while the Pip-Boy is open.
    bool cursorWasHidden = mouse_hidden();
    mouse_hide();

    pb_state.open = true;
    pb_state.opens++;
    rt_log("pipboy: open on %s", pb_tab_names[pb_tab]);

    // The controller drives the Pip-Boy with keys; gameplay input (held
    // triggers, the stick) was dropped when the menu context began.
    gamepad_set_menu_key_mode(true);

    pb_render();
    Object* activeBefore = pb_active_item();
    pb_loop();

    if (!cursorWasHidden) {
        mouse_show();
    }
    gamepad_set_menu_key_mode(false);

    if (pb_world_art != NULL) {
        art_ptr_unlock(pb_world_key);
        pb_world_art = NULL;
    }
    delete[] pb_map_buf;
    pb_map_buf = NULL;

    pipboy_rt_end();
    pb_win = -1;
    pb_open = false;
    pb_scanner = false;
    pb_state.open = false;
    pb_state.closes++;
    pb_update_level_flag(true);

    if (isoWasEnabled) {
        map_enable_bk_processes();
    }

    inven_rt_refresh_appearance();
    // A weapon newly readied in the Pip-Boy is drawn, as from favorites.
    Object* activeAfter = pb_active_item();
    if (activeAfter != activeBefore && activeAfter != NULL) {
        rt_play_draw(activeAfter);
    }
    intface_redraw();
    rt_log("pipboy: closed");
    if (pb_open_skilldex) {
        pb_open_skilldex = false;
        GNW_add_input_buffer(KEY_LOWERCASE_S);
    }
}

void rt_pipboy_show_scanner()
{
    pb_scanner = true;
    if (pb_open) {
        pb_tab = RT_PIPBOY_TAB_MAP;
        pb_page[RT_PIPBOY_TAB_MAP] = RT_PIPBOY_PAGE_MAP_LOCAL;
        pb_mode = RT_PIPBOY_MODE_NONE;
        pb_map_centered = false;
        return;
    }
    rt_pipboy_open(RT_PIPBOY_TAB_MAP, RT_PIPBOY_PAGE_MAP_LOCAL);
}

} // namespace fallout
