#include <cairo.h>
#include <drm_fourcc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include "log.h"
#include "sway/sway_icon_node.h"

struct cairo_buffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
};

static void cairo_buffer_handle_destroy(struct wlr_buffer *wlr_buffer) {
	struct cairo_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
	cairo_surface_destroy(buffer->surface);
	free(buffer);
}

static bool cairo_buffer_handle_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct cairo_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
	*data = cairo_image_surface_get_data(buffer->surface);
	*stride = cairo_image_surface_get_stride(buffer->surface);
	*format = DRM_FORMAT_ARGB8888;
	return true;
}

static void cairo_buffer_handle_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	// This space is intentionally left blank
}

static const struct wlr_buffer_impl cairo_buffer_impl = {
	.destroy = cairo_buffer_handle_destroy,
	.begin_data_ptr_access = cairo_buffer_handle_begin_data_ptr_access,
	.end_data_ptr_access = cairo_buffer_handle_end_data_ptr_access,
};

struct icon_buffer {
	struct wlr_scene_buffer *buffer_node;
	struct sway_icon_node props;

	bool visible;
	float scale;

	struct wl_listener outputs_update;
	struct wl_listener destroy;
};

// The glyph takes the middle of the square, like Windows' and GNOME's title
// bar buttons, drawn with one logical pixel wide lines
static void draw_icon(cairo_t *cairo, enum sway_icon icon, double size,
		double scale) {
	double line = round(scale);
	double glyph = round(size * 0.4);
	// Keep one-pixel lines on pixel centers so they stay sharp
	double half = fmod(line, 2) / 2;
	double x0 = round((size - glyph) / 2) + half;
	double y0 = x0;
	double x1 = x0 + glyph - line, y1 = y0 + glyph - line;
	double mid = round(size / 2) + half;
	double step = round(glyph * 0.25);

	cairo_set_line_width(cairo, line);
	cairo_set_line_cap(cairo, CAIRO_LINE_CAP_SQUARE);
	cairo_set_line_join(cairo, CAIRO_LINE_JOIN_MITER);

	switch (icon) {
	case SWAY_ICON_MINIMIZE:
		cairo_move_to(cairo, x0, mid);
		cairo_line_to(cairo, x1, mid);
		break;
	case SWAY_ICON_MAXIMIZE:
		cairo_rectangle(cairo, x0, y0, x1 - x0, y1 - y0);
		break;
	case SWAY_ICON_RESTORE:
		// The front window, and the corner of the one behind it
		cairo_rectangle(cairo, x0, y0 + step, x1 - x0 - step, y1 - y0 - step);
		cairo_move_to(cairo, x0 + step, y0 + step);
		cairo_line_to(cairo, x0 + step, y0);
		cairo_line_to(cairo, x1, y0);
		cairo_line_to(cairo, x1, y1 - step);
		cairo_line_to(cairo, x1 - step, y1 - step);
		break;
	case SWAY_ICON_CLOSE:
		cairo_set_line_cap(cairo, CAIRO_LINE_CAP_ROUND);
		cairo_move_to(cairo, x0, y0);
		cairo_line_to(cairo, x1, y1);
		cairo_move_to(cairo, x1, y0);
		cairo_line_to(cairo, x0, y1);
		break;
	}
	cairo_stroke(cairo);
}

static void render_backing_buffer(struct icon_buffer *buffer) {
	if (!buffer->visible || buffer->props.size <= 0) {
		return;
	}

	float scale = buffer->scale;
	int size = ceil(buffer->props.size * scale);
	float *color = buffer->props.color;

	cairo_surface_t *surface = cairo_image_surface_create(
		CAIRO_FORMAT_ARGB32, size, size);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		sway_log(SWAY_ERROR, "cairo_image_surface_create failed");
		cairo_surface_destroy(surface);
		return;
	}
	struct cairo_buffer *cairo_buffer = calloc(1, sizeof(*cairo_buffer));
	if (!cairo_buffer) {
		cairo_surface_destroy(surface);
		return;
	}

	cairo_t *cairo = cairo_create(surface);
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_set_source_rgba(cairo, color[0], color[1], color[2], color[3]);
	draw_icon(cairo, buffer->props.icon, size, scale);
	cairo_destroy(cairo);
	cairo_surface_flush(surface);

	wlr_buffer_init(&cairo_buffer->base, &cairo_buffer_impl, size, size);
	cairo_buffer->surface = surface;
	wlr_scene_buffer_set_buffer(buffer->buffer_node, &cairo_buffer->base);
	wlr_buffer_drop(&cairo_buffer->base);
	wlr_scene_buffer_set_dest_size(buffer->buffer_node,
		buffer->props.size, buffer->props.size);
}

static void handle_outputs_update(struct wl_listener *listener, void *data) {
	struct icon_buffer *buffer = wl_container_of(listener, buffer, outputs_update);
	struct wlr_scene_outputs_update_event *event = data;

	float scale = 0;
	for (size_t i = 0; i < event->size; i++) {
		if (scale < event->active[i]->output->scale) {
			scale = event->active[i]->output->scale;
		}
	}

	buffer->visible = event->size > 0;
	if (scale != buffer->scale) {
		buffer->scale = scale;
		render_backing_buffer(buffer);
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct icon_buffer *buffer = wl_container_of(listener, buffer, destroy);
	wl_list_remove(&buffer->outputs_update.link);
	wl_list_remove(&buffer->destroy.link);
	free(buffer);
}

struct sway_icon_node *sway_icon_node_create(struct wlr_scene_tree *parent,
		enum sway_icon icon, int size, float color[4]) {
	struct icon_buffer *buffer = calloc(1, sizeof(*buffer));
	if (!buffer) {
		return NULL;
	}
	struct wlr_scene_buffer *node = wlr_scene_buffer_create(parent, NULL);
	if (!node) {
		free(buffer);
		return NULL;
	}

	buffer->buffer_node = node;
	buffer->props.node = &node->node;
	buffer->props.icon = icon;
	buffer->props.size = size;
	memcpy(buffer->props.color, color, sizeof(buffer->props.color));
	// Input goes to the button underneath
	node->point_accepts_input = NULL;

	buffer->destroy.notify = handle_destroy;
	wl_signal_add(&node->node.events.destroy, &buffer->destroy);
	buffer->outputs_update.notify = handle_outputs_update;
	wl_signal_add(&node->events.outputs_update, &buffer->outputs_update);

	// Last: giving it a size puts it on outputs, and the outputs_update that
	// sends is what renders it
	wlr_scene_buffer_set_dest_size(node, size, size);

	return &buffer->props;
}

void sway_icon_node_set_icon(struct sway_icon_node *node, enum sway_icon icon) {
	struct icon_buffer *buffer = wl_container_of(node, buffer, props);
	if (buffer->props.icon != icon) {
		buffer->props.icon = icon;
		render_backing_buffer(buffer);
	}
}

void sway_icon_node_set_size(struct sway_icon_node *node, int size) {
	struct icon_buffer *buffer = wl_container_of(node, buffer, props);
	if (buffer->props.size != size) {
		buffer->props.size = size;
		render_backing_buffer(buffer);
	}
}

void sway_icon_node_set_color(struct sway_icon_node *node, float color[4]) {
	struct icon_buffer *buffer = wl_container_of(node, buffer, props);
	if (memcmp(buffer->props.color, color, sizeof(buffer->props.color)) != 0) {
		memcpy(buffer->props.color, color, sizeof(buffer->props.color));
		render_backing_buffer(buffer);
	}
}
