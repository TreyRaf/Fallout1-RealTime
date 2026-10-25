#include "game/textobj.h"

#include <string.h>

#include <algorithm>

#include "game/gconfig.h"
#include "game/map.h"
#include "game/object.h"
#include "game/rt_view.h"
#include "game/tile.h"
#include "game/wordwrap.h"
#include "plib/gnw/debug.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/input.h"
#include "plib/gnw/memory.h"
#include "plib/gnw/text.h"

namespace fallout {

// The maximum number of text objects that can exist at the same time.
#define TEXT_OBJECTS_MAX_COUNT 20

typedef enum TextObjectFlags {
    TEXT_OBJECT_MARKED_FOR_REMOVAL = 0x01,
    TEXT_OBJECT_UNBOUNDED = 0x02,
} TextObjectFlags;

typedef struct TextObject {
    int flags;
    Object* owner;
    unsigned int time;
    int linesCount;
    int sx;
    int sy;
    int tile;
    int x;
    int y;
    int width;
    int height;
    unsigned char* data;
    // RT: the speaker's offsets when it spoke, the anchor offset that
    // follows its walk, and where the text is drawn in the play area.
    int ownerX;
    int ownerY;
    int offsetX;
    int offsetY;
    Rect shown;
} TextObject;

static void text_object_bk();
static void text_object_layout(TextObject* textObject, Rect* rect);
static bool text_object_rect_is_empty(const Rect* rect);

// 0x508324
static int text_object_index = 0;

// 0x508328
static unsigned int text_object_base_delay = 3500;

// 0x50832C
static unsigned int text_object_line_delay = 1399;

// 0x665210
static TextObject* text_object_list[TEXT_OBJECTS_MAX_COUNT];

// 0x665260
static int display_width;

// 0x665264
static int display_height;

// 0x665268
static unsigned char* display_buffer;

// 0x66526C
static bool text_object_enabled;

// 0x665270
static bool text_object_initialized;

// 0x49CD80
int text_object_init(unsigned char* windowBuffer, int width, int height)
{
    double textBaseDelay;
    double textLineDelay;

    if (text_object_initialized) {
        return -1;
    }

    display_buffer = windowBuffer;
    display_width = width;
    display_height = height;
    text_object_index = 0;

    add_bk_process(text_object_bk);

    if (!config_get_double(&game_config, GAME_CONFIG_PREFERENCES_KEY, GAME_CONFIG_TEXT_BASE_DELAY_KEY, &textBaseDelay)) {
        textBaseDelay = 3.5;
    }

    if (!config_get_double(&game_config, GAME_CONFIG_PREFERENCES_KEY, GAME_CONFIG_TEXT_LINE_DELAY_KEY, &textLineDelay)) {
        textLineDelay = 1.399993896484375;
    }

    text_object_base_delay = (unsigned int)(textBaseDelay * 1000.0);
    text_object_line_delay = (unsigned int)(textLineDelay * 1000.0);

    text_object_enabled = true;
    text_object_initialized = true;

    return 0;
}

// 0x49CE64
int text_object_reset()
{
    int index;

    if (!text_object_initialized) {
        return -1;
    }

    for (index = 0; index < text_object_index; index++) {
        mem_free(text_object_list[index]->data);
        mem_free(text_object_list[index]);
    }

    text_object_index = 0;
    add_bk_process(text_object_bk);

    return 0;
}

// 0x49CEC8
void text_object_exit()
{
    if (text_object_initialized) {
        text_object_reset();
        remove_bk_process(text_object_bk);
        text_object_initialized = false;
    }
}

// 0x49CEEC
void text_object_disable()
{
    text_object_enabled = false;
}

// 0x49CEF8
void text_object_enable()
{
    text_object_enabled = true;
}

// NOTE: Unused.
//
// 0x49CF04
int text_object_is_enabled()
{
    return text_object_enabled;
}

// 0x49CF0C
void text_object_set_base_delay(double value)
{
    if (value < 1.0) {
        value = 1.0;
    }

    text_object_base_delay = (int)(value * 1000.0);
}

// 0x49CF50
unsigned int text_object_get_base_delay()
{
    return text_object_base_delay / 1000;
}

// 0x49CF64
void text_object_set_line_delay(double value)
{
    if (value < 0.0) {
        value = 0.0;
    }

    text_object_line_delay = (int)(value * 1000.0);
}

// 0x49CFA0
unsigned int text_object_get_line_delay()
{
    return text_object_line_delay / 1000;
}

// 0x49CFB4
int text_object_create(Object* object, char* string, int font, int color, int a5, Rect* rect)
{
    if (!text_object_initialized) {
        return -1;
    }

    if (text_object_index >= TEXT_OBJECTS_MAX_COUNT - 1) {
        return -1;
    }

    if (string == NULL) {
        return -1;
    }

    if (*string == '\0') {
        return -1;
    }

    TextObject* textObject = (TextObject*)mem_malloc(sizeof(*textObject));
    if (textObject == NULL) {
        return -1;
    }

    memset(textObject, 0, sizeof(*textObject));

    int oldFont = text_curr();
    text_font(font);

    short beginnings[WORD_WRAP_MAX_COUNT];
    short count;
    if (word_wrap(string, 200, beginnings, &count) != 0) {
        text_font(oldFont);
        return -1;
    }

    textObject->linesCount = count - 1;
    if (textObject->linesCount < 1) {
        debug_printf("**Error in text_object_create()\n");
    }

    textObject->width = 0;

    for (int index = 0; index < textObject->linesCount; index++) {
        char* ending = string + beginnings[index + 1];
        char* beginning = string + beginnings[index];
        if (ending[-1] == ' ') {
            --ending;
        }

        char c = *ending;
        *ending = '\0';

        // NOTE: Calls [text_width] twice, probably result of using min/max macro
        int width = text_width(beginning);
        if (width >= textObject->width) {
            textObject->width = width;
        }

        *ending = c;
    }

    textObject->height = (text_height() + 1) * textObject->linesCount;

    if (a5 != -1) {
        textObject->width += 2;
        textObject->height += 2;
    }

    int size = textObject->width * textObject->height;
    textObject->data = (unsigned char*)mem_malloc(size);
    if (textObject->data == NULL) {
        text_font(oldFont);
        return -1;
    }

    memset(textObject->data, 0, size);

    unsigned char* dest = textObject->data;
    int skip = textObject->width * (text_height() + 1);

    if (a5 != -1) {
        dest += textObject->width;
    }

    for (int index = 0; index < textObject->linesCount; index++) {
        char* beginning = string + beginnings[index];
        char* ending = string + beginnings[index + 1];
        if (ending[-1] == ' ') {
            --ending;
        }

        char c = *ending;
        *ending = '\0';

        int width = text_width(beginning);
        text_to_buf(dest + (textObject->width - width) / 2, beginning, textObject->width, textObject->width, color);

        *ending = c;

        dest += skip;
    }

    if (a5 != -1) {
        buf_outline(textObject->data, textObject->width, textObject->height, textObject->width, a5);
    }

    if (object != NULL) {
        textObject->tile = object->tile;
        // RT: the text follows the speaker from here on.
        textObject->ownerX = object->x;
        textObject->ownerY = object->y;
    } else {
        textObject->flags |= TEXT_OBJECT_UNBOUNDED;
        textObject->tile = tile_center_tile;
    }

    text_object_remove(object);

    textObject->owner = object;
    textObject->time = get_bk_time();

    // RT: placed in the play area, drawn over the map at its normal size.
    text_object_layout(textObject, &(textObject->shown));
    textObject->x = textObject->shown.ulx;
    textObject->y = textObject->shown.uly;

    if (rect != NULL) {
        // Callers refresh this (world) area; the text itself is shown below.
        if (text_object_rect_is_empty(&(textObject->shown))) {
            rect->ulx = 0;
            rect->uly = 0;
            rect->lrx = 0;
            rect->lry = 0;
        } else {
            rt_view_display_rect_to_world(&(textObject->shown), rect);
        }
    }

    text_object_list[text_object_index] = textObject;
    text_object_index++;

    if (!text_object_rect_is_empty(&(textObject->shown))) {
        rt_view_present_display(&(textObject->shown));
    }

    text_font(oldFont);

    return 0;
}

// RT: where the text's anchor is in the play area. The anchor follows the
// speaker (its rendered position, including the offset of a walk in
// progress); a speaker that left the elevation or was removed leaves the
// text where it last was.
static bool text_object_anchor(TextObject* textObject, int* x, int* y)
{
    Object* owner = textObject->owner;
    if (owner != NULL && (textObject->flags & TEXT_OBJECT_UNBOUNDED) == 0) {
        if (owner->tile == -1 || owner->elevation != map_elevation) {
            textObject->owner = NULL;
        } else {
            textObject->tile = owner->tile;
            textObject->offsetX = owner->x - textObject->ownerX;
            textObject->offsetY = owner->y - textObject->ownerY;
        }
    }

    int tileScreenX;
    int tileScreenY;
    if (tile_coord(textObject->tile, &tileScreenX, &tileScreenY, map_elevation) != 0) {
        return false;
    }

    rt_view_world_to_display(tileScreenX + 16 + textObject->offsetX, tileScreenY + textObject->offsetY, x, y);
    return true;
}

// RT: the text's rect in the play area: centered above the speaker's head
// (60 pixels above its hex at the current zoom), at its normal size, and
// kept inside the play area while the speaker is on screen.
static void text_object_layout(TextObject* textObject, Rect* rect)
{
    int anchorX;
    int anchorY;
    if (!text_object_anchor(textObject, &anchorX, &anchorY)) {
        rect->ulx = 0;
        rect->uly = 0;
        rect->lrx = -1;
        rect->lry = -1;
        return;
    }

    int width = rt_view_display_width();
    int height = rt_view_display_height();

    int x = anchorX - textObject->width / 2;
    int y = anchorY;
    if ((textObject->flags & TEXT_OBJECT_UNBOUNDED) == 0) {
        y -= textObject->height + (int)(60.0f / rt_view_get_zoom());
    }

    if (anchorX >= 0 && anchorX < width && anchorY >= 0 && anchorY < height) {
        x = std::max(0, std::min(x, width - textObject->width));
        y = std::max(0, std::min(y, height - textObject->height));
    }

    rect->ulx = x;
    rect->uly = y;
    rect->lrx = x + textObject->width - 1;
    rect->lry = y + textObject->height - 1;
}

static bool text_object_rect_is_empty(const Rect* rect)
{
    return rect->lrx < rect->ulx || rect->lry < rect->uly;
}

// RT: draws the texts over the play area (display coordinates); called by
// `rt_view` whenever part of the play area is shown.
void text_object_render_display(unsigned char* buffer, int pitch, const Rect* rect)
{
    if (!text_object_initialized) {
        return;
    }

    for (int index = 0; index < text_object_index; index++) {
        TextObject* textObject = text_object_list[index];
        textObject->x = textObject->shown.ulx;
        textObject->y = textObject->shown.uly;
        if (text_object_rect_is_empty(&(textObject->shown))) {
            continue;
        }

        Rect textObjectRect;
        if (rect_inside_bound(&(textObject->shown), rect, &textObjectRect) == 0) {
            trans_buf_to_buf(textObject->data + textObject->width * (textObjectRect.uly - textObject->y) + (textObjectRect.ulx - textObject->x),
                textObjectRect.lrx - textObjectRect.ulx + 1,
                textObjectRect.lry - textObjectRect.uly + 1,
                textObject->width,
                buffer + pitch * textObjectRect.uly + textObjectRect.ulx,
                pitch);
        }
    }
}

// RT: moves each text to where its anchor is now. With `invalidate`, the
// areas it left and entered are shown again right away; otherwise the
// caller redraws the whole play area.
void text_object_relayout(bool invalidate)
{
    if (!text_object_initialized) {
        return;
    }

    for (int index = 0; index < text_object_index; index++) {
        TextObject* textObject = text_object_list[index];
        Rect rect;
        text_object_layout(textObject, &rect);
        if (rect.ulx == textObject->shown.ulx
            && rect.uly == textObject->shown.uly
            && rect.lrx == textObject->shown.lrx
            && rect.lry == textObject->shown.lry) {
            continue;
        }

        Rect old = textObject->shown;
        textObject->shown = rect;
        if (invalidate) {
            if (!text_object_rect_is_empty(&old)) {
                rt_view_present_display(&old);
            }
            if (!text_object_rect_is_empty(&rect)) {
                rt_view_present_display(&rect);
            }
        }
    }
}

// RT: where the newest text of `owner` (NULL: an unbounded one) is drawn in
// the play area; for the harness.
bool text_object_get_rect(Object* owner, Rect* rect)
{
    for (int index = text_object_index - 1; index >= 0; index--) {
        TextObject* textObject = text_object_list[index];
        if (textObject->owner == owner && (textObject->flags & TEXT_OBJECT_MARKED_FOR_REMOVAL) == 0) {
            *rect = textObject->shown;
            return true;
        }
    }
    return false;
}

// RT: the object is going away; its texts stay where they are until they
// expire.
void text_object_owner_gone(Object* object)
{
    for (int index = 0; index < text_object_index; index++) {
        TextObject* textObject = text_object_list[index];
        if (textObject->owner == object) {
            textObject->owner = NULL;
        }
    }
}

// 0x49D438
int text_object_count()
{
    return text_object_index;
}

// 0x49D440
static void text_object_bk()
{
    if (!text_object_enabled) {
        return;
    }

    bool textObjectsRemoved = false;
    Rect dirtyRect;

    for (int index = 0; index < text_object_index; index++) {
        TextObject* textObject = text_object_list[index];

        unsigned int delay = text_object_line_delay * textObject->linesCount + text_object_base_delay;
        if ((textObject->flags & TEXT_OBJECT_MARKED_FOR_REMOVAL) != 0 || (elapsed_tocks(get_bk_time(), textObject->time) > delay)) {
            if (!text_object_rect_is_empty(&(textObject->shown))) {
                if (textObjectsRemoved) {
                    rect_min_bound(&dirtyRect, &(textObject->shown), &dirtyRect);
                } else {
                    rectCopy(&dirtyRect, &(textObject->shown));
                    textObjectsRemoved = true;
                }
            }

            mem_free(textObject->data);
            mem_free(textObject);

            memmove(&(text_object_list[index]), &(text_object_list[index + 1]), sizeof(*text_object_list) * (text_object_index - index - 1));

            text_object_index--;
            index--;
        }
    }

    if (textObjectsRemoved) {
        rt_view_present_display(&dirtyRect);
    }

    // RT: texts follow their speakers.
    text_object_relayout(true);
}

// Marks text objects attached to `object` for removal.
//
// 0x49D848
void text_object_remove(Object* object)
{
    int index;

    for (index = 0; index < text_object_index; index++) {
        // RT: a NULL owner means the unbounded texts, not texts whose speaker
        // is gone.
        if (text_object_list[index]->owner == object
            && (object != NULL || (text_object_list[index]->flags & TEXT_OBJECT_UNBOUNDED) != 0)) {
            text_object_list[index]->flags |= TEXT_OBJECT_MARKED_FOR_REMOVAL;
        }
    }
}

} // namespace fallout
