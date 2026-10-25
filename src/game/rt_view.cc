#include "game/rt_view.h"

#include <math.h>
#include <string.h>

#include <algorithm>

#include "game/config.h"
#include "game/gconfig.h"
#include "game/map.h"
#include "game/realtime.h"
#include "game/textobj.h"
#include "game/tile.h"
#include "plib/color/color.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/memory.h"

namespace fallout {

#define RT_VIEW_SECTION "realtime"

// Room around the largest zoomed-out view for the sub-step pan.
#define RT_VIEW_MARGIN 64

// Half an engine scroll step: the pan is kept within it by taking steps.
#define RT_VIEW_HALF_STEP_X 16.0f
#define RT_VIEW_HALF_STEP_Y 12.0f

// Palette entries the color cycler animates; never blended, so water, fire
// and screens keep animating when zoomed out.
#define RT_VIEW_CYCLE_FIRST 229
#define RT_VIEW_CYCLE_LAST 254

static int view_win = -1;
static unsigned char* view_display_buf = NULL;
static int view_width = 0;
static int view_height = 0;

static unsigned char* view_world_buf = NULL;
static int view_world_width = 0;
static int view_world_height = 0;

static float view_pan_x = 0.0f;
static float view_pan_y = 0.0f;
static float view_zoom = 1.0f;
static float view_max_zoom = 1.5f;

static bool view_zooming = false;
static float view_zoom_from = 1.0f;
static float view_zoom_to = 1.0f;
static float view_zoom_elapsed = 0.0f;
static float view_zoom_duration = 0.0f;
static int view_zoom_anchor_tile = -1;
static float view_zoom_anchor_dx = 0.0f;
static float view_zoom_anchor_dy = 0.0f;

static bool view_dirty = false;

static RtViewStats view_stats;

// Per-column sample positions for the scaled copy.
static int* view_sample_x = NULL;
static unsigned char* view_sample_fx = NULL;

bool rt_view_init(int displayWin, unsigned char* displayBuffer, int width, int height)
{
    int pct = 150;
    if (config_get_value(&game_config, RT_VIEW_SECTION, "camera_zoom_out_pct", &pct)) {
        pct = std::max(110, std::min(pct, 200));
    }
    view_max_zoom = pct / 100.0f;

    view_win = displayWin;
    view_display_buf = displayBuffer;
    view_width = width;
    view_height = height;

    // The extra width and height are even, so at zoom 1 the play area is
    // exactly the original window, offset by whole pixels.
    view_world_width = (int)ceilf(width * view_max_zoom) + 2 * RT_VIEW_MARGIN;
    view_world_height = (int)ceilf(height * view_max_zoom) + 2 * RT_VIEW_MARGIN;
    if (((view_world_width - width) & 1) != 0) {
        view_world_width++;
    }
    if (((view_world_height - height) & 1) != 0) {
        view_world_height++;
    }

    view_world_buf = (unsigned char*)mem_malloc(view_world_width * view_world_height);
    view_sample_x = (int*)mem_malloc(sizeof(*view_sample_x) * width);
    view_sample_fx = (unsigned char*)mem_malloc(width);
    if (view_world_buf == NULL || view_sample_x == NULL || view_sample_fx == NULL) {
        return false;
    }
    memset(view_world_buf, 0, view_world_width * view_world_height);

    view_pan_x = 0.0f;
    view_pan_y = 0.0f;
    view_zoom = 1.0f;
    view_zooming = false;
    memset(&view_stats, 0, sizeof(view_stats));
    return true;
}

void rt_view_exit()
{
    if (view_world_buf != NULL) {
        mem_free(view_world_buf);
        view_world_buf = NULL;
    }
    if (view_sample_x != NULL) {
        mem_free(view_sample_x);
        view_sample_x = NULL;
    }
    if (view_sample_fx != NULL) {
        mem_free(view_sample_fx);
        view_sample_fx = NULL;
    }
}

unsigned char* rt_view_world_buffer()
{
    return view_world_buf;
}

int rt_view_world_width()
{
    return view_world_width;
}

int rt_view_world_height()
{
    return view_world_height;
}

int rt_view_display_width()
{
    return view_width;
}

int rt_view_display_height()
{
    return view_height;
}

// World point shown at the center of the play area. At zoom 1 the pan is
// rounded to whole pixels, so the default view is never resampled.
static void rt_view_center(float* cx, float* cy)
{
    float px = view_pan_x;
    float py = view_pan_y;
    if (view_zoom == 1.0f) {
        px = floorf(px + 0.5f);
        py = floorf(py + 0.5f);
    }
    *cx = view_world_width / 2 + px;
    *cy = view_world_height / 2 + py;
}

void rt_view_world_to_display(int wx, int wy, int* dx, int* dy)
{
    float cx;
    float cy;
    rt_view_center(&cx, &cy);
    *dx = (int)floorf((wx - cx) / view_zoom + view_width / 2 + 0.5f);
    *dy = (int)floorf((wy - cy) / view_zoom + view_height / 2 + 0.5f);
}

void rt_view_display_to_world(int dx, int dy, int* wx, int* wy)
{
    float cx;
    float cy;
    rt_view_center(&cx, &cy);
    *wx = (int)floorf((dx - view_width / 2) * view_zoom + cx + 0.5f);
    *wy = (int)floorf((dy - view_height / 2) * view_zoom + cy + 0.5f);
}

void rt_view_world_rect_to_display(const Rect* world, Rect* display)
{
    float cx;
    float cy;
    rt_view_center(&cx, &cy);
    display->ulx = (int)floorf((world->ulx - cx) / view_zoom + view_width / 2);
    display->uly = (int)floorf((world->uly - cy) / view_zoom + view_height / 2);
    display->lrx = (int)ceilf((world->lrx + 1 - cx) / view_zoom + view_width / 2) - 1;
    display->lry = (int)ceilf((world->lry + 1 - cy) / view_zoom + view_height / 2) - 1;
    if (view_zoom != 1.0f) {
        // Scaled pixels near the edge also sample their neighbors.
        display->ulx--;
        display->uly--;
        display->lrx++;
        display->lry++;
    }
}

void rt_view_display_rect_to_world(const Rect* display, Rect* world)
{
    float cx;
    float cy;
    rt_view_center(&cx, &cy);
    world->ulx = (int)floorf((display->ulx - view_width / 2) * view_zoom + cx);
    world->uly = (int)floorf((display->uly - view_height / 2) * view_zoom + cy);
    world->lrx = (int)ceilf((display->lrx + 1 - view_width / 2) * view_zoom + cx) - 1;
    world->lry = (int)ceilf((display->lry + 1 - view_height / 2) * view_zoom + cy) - 1;
    if (view_zoom != 1.0f) {
        world->ulx--;
        world->uly--;
        world->lrx++;
        world->lry++;
    }
}

bool rt_view_tile_center(int tile, int elevation, int* x, int* y)
{
    int wx;
    int wy;
    if (tile_coord(tile, &wx, &wy, elevation) != 0) {
        return false;
    }
    rt_view_world_to_display(wx + 16, wy + 8, x, y);
    return true;
}

// Copies (and at other zooms resamples) a display rect from the world
// buffer.
static void rt_view_scale(const Rect* rect)
{
    float cx;
    float cy;
    rt_view_center(&cx, &cy);

    int width = rect->lrx - rect->ulx + 1;

    if (view_zoom == 1.0f) {
        int ox = (int)cx - view_width / 2;
        int oy = (int)cy - view_height / 2;
        for (int y = rect->uly; y <= rect->lry; y++) {
            memcpy(view_display_buf + view_width * y + rect->ulx,
                view_world_buf + view_world_width * (y + oy) + rect->ulx + ox,
                width);
        }
        return;
    }

    // Bilinear: the four nearest world pixels are averaged in RGB and mapped
    // back to the palette.
    unsigned char red[256];
    unsigned char green[256];
    unsigned char blue[256];
    for (int index = 0; index < 256; index++) {
        red[index] = cmap[index * 3];
        green[index] = cmap[index * 3 + 1];
        blue[index] = cmap[index * 3 + 2];
    }

    for (int x = rect->ulx; x <= rect->lrx; x++) {
        float wx = (x + 0.5f - view_width / 2) * view_zoom + cx - 0.5f;
        int x0 = (int)floorf(wx);
        int fx = (int)((wx - x0) * 256.0f);
        if (x0 < 0) {
            x0 = 0;
            fx = 0;
        } else if (x0 > view_world_width - 2) {
            x0 = view_world_width - 2;
            fx = 255;
        }
        view_sample_x[x] = x0;
        view_sample_fx[x] = (unsigned char)std::min(fx, 255);
    }

    for (int y = rect->uly; y <= rect->lry; y++) {
        float wy = (y + 0.5f - view_height / 2) * view_zoom + cy - 0.5f;
        int y0 = (int)floorf(wy);
        int fy = (int)((wy - y0) * 256.0f);
        if (y0 < 0) {
            y0 = 0;
            fy = 0;
        } else if (y0 > view_world_height - 2) {
            y0 = view_world_height - 2;
            fy = 255;
        }
        fy = std::min(fy, 255);

        const unsigned char* row0 = view_world_buf + view_world_width * y0;
        const unsigned char* row1 = row0 + view_world_width;
        unsigned char* dest = view_display_buf + view_width * y + rect->ulx;

        for (int x = rect->ulx; x <= rect->lrx; x++) {
            int x0 = view_sample_x[x];
            int fx = view_sample_fx[x];
            int a = row0[x0];
            int b = row0[x0 + 1];
            int c = row1[x0];
            int d = row1[x0 + 1];

            if (a == b && a == c && a == d) {
                *dest++ = (unsigned char)a;
                continue;
            }

            bool cycling = (a >= RT_VIEW_CYCLE_FIRST && a <= RT_VIEW_CYCLE_LAST)
                || (b >= RT_VIEW_CYCLE_FIRST && b <= RT_VIEW_CYCLE_LAST)
                || (c >= RT_VIEW_CYCLE_FIRST && c <= RT_VIEW_CYCLE_LAST)
                || (d >= RT_VIEW_CYCLE_FIRST && d <= RT_VIEW_CYCLE_LAST);
            if (cycling) {
                // Nearest of the four.
                *dest++ = (unsigned char)(fy < 128 ? (fx < 128 ? a : b) : (fx < 128 ? c : d));
                continue;
            }

            int wa = (256 - fx) * (256 - fy);
            int wb = fx * (256 - fy);
            int wc = (256 - fx) * fy;
            int wd = fx * fy;
            // 6-bit palette components, rounded down to 5 bits.
            int r = (red[a] * wa + red[b] * wb + red[c] * wc + red[d] * wd + 32768) >> 17;
            int g = (green[a] * wa + green[b] * wb + green[c] * wc + green[d] * wd + 32768) >> 17;
            int bl = (blue[a] * wa + blue[b] * wb + blue[c] * wc + blue[d] * wd + 32768) >> 17;
            *dest++ = colorTable[(std::min(r, 31) << 10) | (std::min(g, 31) << 5) | std::min(bl, 31)];
        }
    }
}

void rt_view_present_display(const Rect* displayRect)
{
    if (view_world_buf == NULL) {
        return;
    }

    Rect rect = *displayRect;
    rect.ulx = std::max(rect.ulx, 0);
    rect.uly = std::max(rect.uly, 0);
    rect.lrx = std::min(rect.lrx, view_width - 1);
    rect.lry = std::min(rect.lry, view_height - 1);
    if (rect.ulx > rect.lrx || rect.uly > rect.lry) {
        return;
    }

    rt_view_scale(&rect);
    text_object_render_display(view_display_buf, view_width, &rect);
    rt_render_overlay(view_display_buf, view_width, &rect);
    win_draw_rect(view_win, &rect);
    view_stats.presents++;
}

void rt_view_present_all()
{
    Rect rect = { 0, 0, view_width - 1, view_height - 1 };
    rt_view_present_display(&rect);
    view_stats.fullPresents++;
}

void rt_view_blit(Rect* worldRect)
{
    Rect display;
    rt_view_world_rect_to_display(worldRect, &display);
    rt_view_present_display(&display);
}

// Whether the engine could take a scroll step that way (map limits and
// scroll blockers), without taking it.
static bool rt_view_can_step(int dx, int dy)
{
    return map_can_scroll(dx, dy);
}

// Takes one engine step and keeps the view where it was: the pan absorbs
// the shift. Returns false when the step can't be taken.
static bool rt_view_step(int dx, int dy)
{
    int refTile = tile_center_tile;
    int x1;
    int y1;
    tile_coord(refTile, &x1, &y1, map_elevation);

    if (map_scroll_now(dx, dy) != 0) {
        view_stats.blockedSteps++;
        return false;
    }

    int x2;
    int y2;
    tile_coord(refTile, &x2, &y2, map_elevation);
    view_pan_x += (float)(x2 - x1);
    view_pan_y += (float)(y2 - y1);
    view_stats.engineSteps++;
    return true;
}

static void rt_view_normalize()
{
    // At most a few steps per call, so a long jump can't loop forever.
    for (int guard = 0; guard < 16; guard++) {
        int dx = view_pan_x > RT_VIEW_HALF_STEP_X ? 1 : (view_pan_x < -RT_VIEW_HALF_STEP_X ? -1 : 0);
        int dy = view_pan_y > RT_VIEW_HALF_STEP_Y ? 1 : (view_pan_y < -RT_VIEW_HALF_STEP_Y ? -1 : 0);
        if (dx == 0 && dy == 0) {
            break;
        }

        bool stepped = false;
        if (dx != 0 && rt_view_can_step(dx, 0)) {
            stepped = rt_view_step(dx, 0);
        }
        if (dy != 0 && rt_view_can_step(0, dy)) {
            stepped = rt_view_step(0, dy) || stepped;
        }
        if (!stepped) {
            break;
        }
    }

    // A direction the engine can't scroll to can't be panned to either,
    // just as the original camera stops at the scroll limit.
    if (view_pan_x > 0.0f && !rt_view_can_step(1, 0)) {
        view_pan_x = 0.0f;
    } else if (view_pan_x < 0.0f && !rt_view_can_step(-1, 0)) {
        view_pan_x = 0.0f;
    }
    if (view_pan_y > 0.0f && !rt_view_can_step(0, 1)) {
        view_pan_y = 0.0f;
    } else if (view_pan_y < 0.0f && !rt_view_can_step(0, -1)) {
        view_pan_y = 0.0f;
    }

    float limit = RT_VIEW_MARGIN - 2.0f;
    view_pan_x = std::max(-limit, std::min(view_pan_x, limit));
    view_pan_y = std::max(-limit, std::min(view_pan_y, limit));
}

void rt_view_move(float dx, float dy)
{
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }

    view_pan_x += dx;
    view_pan_y += dy;
    rt_view_normalize();
    view_dirty = true;
}

void rt_view_reset_pan()
{
    if (view_pan_x != 0.0f || view_pan_y != 0.0f) {
        view_pan_x = 0.0f;
        view_pan_y = 0.0f;
        view_dirty = true;
    }
}

void rt_view_get_pan(float* x, float* y)
{
    *x = view_pan_x;
    *y = view_pan_y;
}

void rt_view_set_zoom(float zoom, int transitionMs, int anchorTile)
{
    zoom = std::max(1.0f, std::min(zoom, view_max_zoom));
    if (zoom == view_zoom_to && (view_zooming || zoom == view_zoom)) {
        return;
    }

    view_stats.zoomChanges++;
    view_zoom_from = view_zoom;
    view_zoom_to = zoom;
    view_zoom_elapsed = 0.0f;
    view_zoom_duration = (float)std::max(transitionMs, 0);
    view_zoom_anchor_tile = anchorTile;

    int x;
    int y;
    if (anchorTile != -1 && rt_view_tile_center(anchorTile, map_elevation, &x, &y)) {
        view_zoom_anchor_dx = (float)x;
        view_zoom_anchor_dy = (float)y;
    } else {
        view_zoom_anchor_tile = -1;
    }

    view_zooming = true;
    rt_view_update(0.0f);
}

float rt_view_get_zoom()
{
    return view_zoom;
}

float rt_view_get_target_zoom()
{
    return view_zooming ? view_zoom_to : view_zoom;
}

float rt_view_get_max_zoom()
{
    return view_max_zoom;
}

bool rt_view_is_zooming()
{
    return view_zooming;
}

void rt_view_update(float dtMs)
{
    if (view_zooming) {
        view_zoom_elapsed += dtMs;
        float t = view_zoom_duration > 0.0f ? view_zoom_elapsed / view_zoom_duration : 1.0f;
        if (t >= 1.0f) {
            view_zoom = view_zoom_to;
            view_zooming = false;
        } else {
            // Ease in and out.
            float eased = t * t * (3.0f - 2.0f * t);
            view_zoom = view_zoom_from + (view_zoom_to - view_zoom_from) * eased;
        }

        // The anchor (the player) stays where it was in the play area, as
        // far as the map's scroll limits allow.
        int wx;
        int wy;
        if (view_zoom_anchor_tile != -1 && tile_coord(view_zoom_anchor_tile, &wx, &wy, map_elevation) == 0) {
            float centerX = (wx + 16) - (view_zoom_anchor_dx - view_width / 2) * view_zoom;
            float centerY = (wy + 8) - (view_zoom_anchor_dy - view_height / 2) * view_zoom;
            view_pan_x = centerX - view_world_width / 2;
            view_pan_y = centerY - view_world_height / 2;
            rt_view_normalize();
        }
        view_dirty = true;
    }

    if (view_dirty) {
        view_dirty = false;
        text_object_relayout(false);
        rt_overlays_relayout(false);
        rt_view_present_all();
    }
}

const RtViewStats* rt_view_get_stats()
{
    return &view_stats;
}

} // namespace fallout
