#include <math.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"

struct seatop_move_floating_event {
	struct sway_container *con;
	double dx, dy; // cursor offset in container
};

static void finalize_move(struct sway_seat *seat) {
	struct seatop_move_floating_event *e = seat->seatop_data;

	// We "move" the container to its own location
	// so it discovers its output again.
	container_floating_move_to(e->con, e->con->pending.x, e->con->pending.y);
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
// With titlebar_buttons, keep the container inside the workspace under the
// cursor, so it can't be dragged partly off the screen or under the bar.
// Containers bigger than the workspace stick to its top left.
static void clamp_to_workspace(struct sway_container *con,
		double cx, double cy, double *x, double *y) {
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(root->output_layout, cx, cy);
	struct sway_output *output = wlr_output ? output_from_wlr_output(wlr_output) : NULL;
	struct sway_workspace *ws = output ? output_get_active_workspace(output) : NULL;
	if (!ws) {
		return;
	}
	struct wlr_box area;
	workspace_get_box(ws, &area);
	*x = fmax(area.x, fmin(*x, area.x + area.width - con->pending.width));
	*y = fmax(area.y, fmin(*y, area.y + area.height - con->pending.height));
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_move_floating_event *e = seat->seatop_data;
	struct wlr_cursor *cursor = seat->cursor->cursor;

	e->con->animation_state.seat_is_moving_float = true;

	double x = cursor->x - e->dx;
	double y = cursor->y - e->dy;
	if (config->titlebar_buttons) {
		clamp_to_workspace(e->con, cursor->x, cursor->y, &x, &y);
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

	con->animation_state.seat_is_moving_float = true;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	container_raise_floating(con);
	transaction_commit_dirty();

	cursor_set_image(cursor, "grab", NULL);
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
