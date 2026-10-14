#include <assert.h>
#include <time.h>
#include "sway/animation_manager.h"
#include "sway/config.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/node.h"
#include "sway/tree/root.h"

struct animation_manager {
	float tick_time;
	struct wl_event_source *tick;
	struct wl_list animations;
} animation_manager;

struct animation init_animation(void *data) {
	return (struct animation){
		.data = data,
		.progress = 0.0f,
		.start_nsec = 0,
		.multiplier = 0.0f,
		.initialized = false,
		.update = NULL,
		.complete = NULL,
	};
}

static float ease_out_cubic(float p) {
	return pow(p - 1, 3) + 1;
}

static int64_t get_monotonic_nsec(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

static int animation_timer() {
	// Progress follows wall-clock time, so a slow frame skips ahead instead of
	// stretching the animation past animation_duration_ms
	int64_t now = get_monotonic_nsec();
	float duration_nsec = config->animation_duration_ms * 1000000.0f;
	struct animation *animation, *tmp;
	wl_list_for_each_reverse_safe(animation, tmp, &animation_manager.animations, link) {
		animation->progress = duration_nsec > 0 ?
			MIN((now - animation->start_nsec) / duration_nsec, 1.0f) : 1.0f;
		animation->multiplier = ease_out_cubic(animation->progress);

		if (animation->update) {
			animation->update(animation->data);
		}

		if (animation->progress == 1.0f) {
			finish_animation(animation);
			if (animation->complete) {
				animation->complete(animation->data);
			}
		}
	}

	if (!wl_list_empty(&animation_manager.animations)) {
		wl_event_source_timer_update(animation_manager.tick,
				animation_manager.tick_time);
	}
	return 0;
}

void add_animation(struct animation *animation, void (*update_callback)(void *),
		void (*complete_callback)(void *)) {
	if (!config->animation_duration_ms) {
		if (complete_callback) {
			complete_callback(animation->data);
		}
		return;
	}
	// remove previous instances of this animation
	if (animation->initialized) {
		wl_list_remove(&animation->link);
	}

	animation->progress = 0.0f;
	animation->start_nsec = get_monotonic_nsec();
	animation->multiplier = 0.0f;
	animation->initialized = true;
	animation->update = update_callback;
	animation->complete = complete_callback;
	wl_list_insert(&animation_manager.animations, &animation->link);
}

void finish_animation(struct animation *animation) {
	if (animation->initialized) {
		wl_list_remove(&animation->link);
		animation->initialized = false;
	}
	animation->progress = 1.0f;
	animation->multiplier = 1.0f;
}

void start_animations() {
	if (!config->animation_duration_ms) {
		return;
	}
	assert(animation_manager.tick);

	wl_event_source_timer_update(animation_manager.tick, 1);
}

static float get_fastest_output_refresh_ms() {
	float fastest_output_refresh_ms = 16.6667; // fallback to 60 Hz
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		if (output->refresh_nsec > 0) {
			float output_refresh_ms = output->refresh_nsec / 1000000.0;
			fastest_output_refresh_ms = MIN(fastest_output_refresh_ms, output_refresh_ms);
		}
	}
	return fastest_output_refresh_ms;
}

void refresh_animation_manager_timing() {
	if (!config || !root) {
		return;
	}
	animation_manager.tick_time = get_fastest_output_refresh_ms();
}

void animation_manager_init(struct sway_server *server) {
	animation_manager.tick = wl_event_loop_add_timer(server->wl_event_loop,
			animation_timer, NULL);
	wl_list_init(&animation_manager.animations);
	refresh_animation_manager_timing();
}

static float lerp(float a, float b, float t) {
	return a * (1.0 - t) + b * t;
}

float get_animated_value(float from, float to, const struct animation *animation) {
	if (!config->animation_duration_ms || !animation->initialized) {
		return to;
	}
	return lerp(from, to, animation->multiplier);
}

