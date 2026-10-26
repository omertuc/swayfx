#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/container.h"

static const char *const states[] = {
	[WINDOW_STATE_MAXIMIZE] = "maximize",
	[WINDOW_STATE_RESTORE] = "restore",
	[WINDOW_STATE_MINIMIZE] = "minimize",
	[WINDOW_STATE_UP] = "up",
	[WINDOW_STATE_DOWN] = "down",
	[WINDOW_STATE_LEFT] = "left",
	[WINDOW_STATE_RIGHT] = "right",
};

struct cmd_results *cmd_window_state(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "window_state", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	struct sway_container *con = config->handler_context.container;
	if (!con || !con->view) {
		return cmd_results_new(CMD_FAILURE, "No current window");
	}
	for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
		if (strcasecmp(argv[0], states[i]) == 0) {
			if (!container_window_state(con, i)) {
				return cmd_results_new(CMD_FAILURE,
					"Only floating windows can be snapped to a half");
			}
			return cmd_results_new(CMD_SUCCESS, NULL);
		}
	}
	return cmd_results_new(CMD_INVALID, "Expected 'window_state "
		"maximize|restore|minimize|up|down|left|right'");
}
