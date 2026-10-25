#ifndef FALLOUT_GAME_RT_VIEW_H_
#define FALLOUT_GAME_RT_VIEW_H_

#include "plib/gnw/rect.h"

namespace fallout {

// The map view. The world (floor, objects, roofs) renders into a buffer
// larger than the play area; the play area shows part of it, offset by a
// sub-step camera pan and scaled by the zoom. Text drawn over the map
// (floating dialogue, the interaction label, the lock-on indicator) is drawn
// afterwards at its normal size, where its world anchor appears.
//
// At zoom 1 with no pan the view shows exactly what the original renderer
// did.

bool rt_view_init(int displayWin, unsigned char* displayBuffer, int width, int height);
void rt_view_exit();

unsigned char* rt_view_world_buffer();
int rt_view_world_width();
int rt_view_world_height();

// Tile engine refresh callback: shows a world rect that was redrawn.
void rt_view_blit(Rect* worldRect);

// Redraws part of the play area (display coordinates) from the world buffer
// plus the text drawn over it.
void rt_view_present_display(const Rect* displayRect);
void rt_view_present_all();

// Coordinate conversions between the world buffer (`tile_coord`,
// `obj_bound`) and the play area.
void rt_view_world_to_display(int wx, int wy, int* dx, int* dy);
void rt_view_display_to_world(int dx, int dy, int* wx, int* wy);
void rt_view_world_rect_to_display(const Rect* world, Rect* display);
void rt_view_display_rect_to_world(const Rect* display, Rect* world);
// Center of the hex in the play area; false when the tile is invalid.
bool rt_view_tile_center(int tile, int elevation, int* x, int* y);

int rt_view_display_width();
int rt_view_display_height();

// Moves the camera by a world-pixel amount (sub-step precise). Engine
// scroll steps are taken as needed and the map's scroll limits are kept.
void rt_view_move(float dx, float dy);
void rt_view_reset_pan();
void rt_view_get_pan(float* x, float* y);

// Zoom: 1 is the original view; larger shows more of the map. The change
// eases over `transitionMs`, keeping the world point at `anchorX/anchorY`
// (world coordinates) where it is on screen.
void rt_view_set_zoom(float zoom, int transitionMs, int anchorTile);
float rt_view_get_zoom();
float rt_view_get_target_zoom();
float rt_view_get_max_zoom();
bool rt_view_is_zooming();
void rt_view_update(float dtMs);

// Diagnostics for the harness.
typedef struct RtViewStats {
    int presents;
    int fullPresents;
    int engineSteps;
    int blockedSteps;
    int zoomChanges;
} RtViewStats;

const RtViewStats* rt_view_get_stats();

} // namespace fallout

#endif /* FALLOUT_GAME_RT_VIEW_H_ */
