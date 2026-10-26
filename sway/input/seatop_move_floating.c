#include <math.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"

// How much of a dragged container stays on the workspace with
// titlebar_buttons, on each side but the top (which keeps the title bar)
#define MIN_VISIBLE 64
// How close to a screen edge the cursor snaps the container to it
#define SNAP_EDGE 4
// How far the cursor goes before a maximized or snapped container is restored
#define UNSNAP_THRESHOLD 8

struct seatop_move_floating_event {
	struct sway_container *con;
	double dx, dy; // cursor offset in container
	double start_cx, start_cy; // cursor at the start
	// Maximized or snapped when the move started, so restored on the way
	bool unsnap;
	// Geometry to restore a snap to: the container's from before the move
	struct wlr_box start;
	// With titlebar_buttons: where the container goes when it's dropped with
	// the cursor at a screen edge, and the preview of that
	bool snapping;
	enum sway_window_state snap;
	struct sway_workspace *snap_ws;
	struct wlr_box snap_box;
	struct wlr_scene_tree *preview;
	struct wlr_scene_rect *preview_rect;
	struct wlr_scene_blur *preview_blur;
};

static void handle_end(struct sway_seat *seat) {
	struct seatop_move_floating_event *e = seat->seatop_data;
	if (e->preview) {
		wlr_scene_node_destroy(&e->preview->node);
		e->preview = NULL;
	}
}

static void finalize_move(struct sway_seat *seat) {
	struct seatop_move_floating_event *e = seat->seatop_data;

	if (e->snapping) {
		// Into the target workspace first, so the snap fills that one
		struct wlr_box *box = &e->snap_box;
		container_floating_move_to(e->con,
			box->x + (box->width - e->con->pending.width) / 2,
			box->y + (box->height - e->con->pending.height) / 2);
		container_window_state(e->con, e->snap);
		// Restoring goes back to the size and place from before the drag
		if (!wlr_box_empty(&e->start)) {
			e->con->maximize_restore = e->start;
		}
	} else {
		// We "move" the container to its own location
		// so it discovers its output again.
		container_floating_move_to(e->con, e->con->pending.x, e->con->pending.y);
	}
	transaction_commit_dirty();

	e->con->animation_state.seat_is_moving_float = false;

	seatop_begin_default(seat);
}

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wl_pointer_button_state state) {
	if (seat->cursor->pressed_button_count == 0) {
		finalize_move(seat);
	}
}

static void handle_tablet_tool_tip(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state) {
	if (state == WLR_TABLET_TOOL_TIP_UP) {
		finalize_move(seat);
	}
}
static struct sway_workspace *workspace_at(double cx, double cy,
		struct wlr_box *output_box) {
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(root->output_layout, cx, cy);
	struct sway_output *output = wlr_output ? output_from_wlr_output(wlr_output) : NULL;
	if (!output) {
		return NULL;
	}
	if (output_box) {
		wlr_output_layout_get_box(root->output_layout, wlr_output, output_box);
	}
	return output_get_active_workspace(output);
}

// With titlebar_buttons, the container can go past the edges of the workspace
// under the cursor but not leave it: some of it stays on each side, and its
// title bar stays below the top (and the bar), so it can be grabbed again.
static void clamp_to_workspace(struct sway_container *con,
		double cx, double cy, double *x, double *y) {
	struct sway_workspace *ws = workspace_at(cx, cy, NULL);
	if (!ws) {
		return;
	}
	struct wlr_box area;
	workspace_get_box(ws, &area);
	*x = fmax(area.x - con->pending.width + MIN_VISIBLE,
		fmin(*x, area.x + area.width - MIN_VISIBLE));
	*y = fmax(area.y, fmin(*y, area.y + area.height - MIN_VISIBLE));
}

static void hide_preview(struct seatop_move_floating_event *e) {
	e->snapping = false;
	if (e->preview) {
		wlr_scene_node_set_enabled(&e->preview->node, false);
	}
}

// Like Windows: with the cursor at the left or right edge of a screen, the
// container is going to fill that half of it, and at the top all of it. The
// preview of that hangs off the container's own scene tree, below its
// contents, so it's over the other windows but under the one being dragged.
static void update_snap(struct seatop_move_floating_event *e,
		double cx, double cy, double x, double y) {
	struct wlr_box ob;
	struct sway_workspace *ws = workspace_at(cx, cy, &ob);
	if (!ws) {
		hide_preview(e);
		return;
	}
	if (cx < ob.x + SNAP_EDGE) {
		e->snap = WINDOW_STATE_LEFT;
	} else if (cx >= ob.x + ob.width - SNAP_EDGE) {
		e->snap = WINDOW_STATE_RIGHT;
	} else if (cy < ob.y + SNAP_EDGE) {
		e->snap = WINDOW_STATE_MAXIMIZE;
	} else {
		hide_preview(e);
		return;
	}
	e->snapping = true;
	e->snap_ws = ws;
	workspace_snap_box(ws, e->snap, &e->snap_box);

	if (!e->preview) {
		const float *indicator = config->border_colors.focused.indicator;
		float color[4] = {
			indicator[0] * .35,
			indicator[1] * .35,
			indicator[2] * .35,
			indicator[3] * .35,
		};
		e->preview = wlr_scene_tree_create(e->con->scene_tree);
		if (!e->preview) {
			return;
		}
		e->preview_blur = wlr_scene_blur_create(e->preview, 0, 0);
		e->preview_rect = wlr_scene_rect_create(e->preview, 0, 0, color);
		if (!e->preview_blur || !e->preview_rect) {
			wlr_scene_node_destroy(&e->preview->node);
			e->preview = NULL;
			return;
		}
	}
	struct wlr_box *box = &e->snap_box;
	wlr_scene_node_lower_to_bottom(&e->preview->node);
	// The container's tree is about to be at x, y
	wlr_scene_node_set_position(&e->preview->node, box->x - x, box->y - y);
	wlr_scene_rect_set_size(e->preview_rect, box->width, box->height);
	wlr_scene_blur_set_size(e->preview_blur, box->width, box->height);
	int radius = MIN(config->corner_radius,
		MIN(box->width / 2, box->height / 2));
	wlr_scene_rect_set_corner_radii(e->preview_rect, corner_radii_all(radius));
	wlr_scene_blur_set_corner_radii(e->preview_blur, corner_radii_all(radius));
	wlr_scene_node_set_enabled(&e->preview->node, true);
}

// Dragging a maximized or snapped container away gives it its own size back,
// with the cursor as far along the title bar as it was
static void unsnap(struct seatop_move_floating_event *e, double cx, double cy) {
	double moved = hypot(cx - e->start_cx, cy - e->start_cy);
	if (!e->unsnap || moved < UNSNAP_THRESHOLD) {
		return;
	}
	e->unsnap = false;
	double old_width = e->con->pending.width;
	container_window_state(e->con, WINDOW_STATE_RESTORE);
	if (old_width > 0) {
		e->dx *= e->con->pending.width / old_width;
	}
	e->start = (struct wlr_box){
		.x = e->con->pending.x,
		.y = e->con->pending.y,
		.width = e->con->pending.width,
		.height = e->con->pending.height,
	};
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_move_floating_event *e = seat->seatop_data;
	struct wlr_cursor *cursor = seat->cursor->cursor;

	e->con->animation_state.seat_is_moving_float = true;

	if (config->titlebar_buttons) {
		unsnap(e, cursor->x, cursor->y);
	}
	double x = cursor->x - e->dx;
	double y = cursor->y - e->dy;
	if (config->titlebar_buttons) {
		clamp_to_workspace(e->con, cursor->x, cursor->y, &x, &y);
		update_snap(e, cursor->x, cursor->y, x, y);
	}
	container_floating_move_to(e->con, x, y);
	transaction_commit_dirty();
}

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_move_floating_event *e = seat->seatop_data;
	if (e->con == con) {
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.end = handle_end,
	.pointer_motion = handle_pointer_motion,
	.tablet_tool_tip = handle_tablet_tool_tip,
	.unref = handle_unref,
};

void seatop_begin_move_floating(struct sway_seat *seat,
		struct sway_container *con) {
	seatop_end(seat);

	struct sway_cursor *cursor = seat->cursor;
	struct seatop_move_floating_event *e =
		calloc(1, sizeof(struct seatop_move_floating_event));
	if (!e) {
		return;
	}
	e->con = con;
	e->dx = cursor->cursor->x - con->pending.x;
	e->dy = cursor->cursor->y - con->pending.y;
	e->start_cx = cursor->cursor->x;
	e->start_cy = cursor->cursor->y;
	e->unsnap = container_floating_snapped(con);
	if (!e->unsnap) {
		e->start = (struct wlr_box){
			.x = con->pending.x,
			.y = con->pending.y,
			.width = con->pending.width,
			.height = con->pending.height,
		};
	}

	con->animation_state.seat_is_moving_float = true;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	container_raise_floating(con);
	transaction_commit_dirty();

	cursor_set_image(cursor, "grab", NULL);
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
