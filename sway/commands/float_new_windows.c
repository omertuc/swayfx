#include <stdlib.h>
#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"

struct cmd_results *cmd_float_new_windows(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "float_new_windows", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (argc == 1 && strcmp(argv[0], "disable") == 0) {
		config->float_new_width = 0;
		config->float_new_height = 0;
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	char *end;
	int width = argc == 2 ? strtol(argv[0], &end, 10) : 0;
	if (argc != 2 || *end != '\0' || width <= 0) {
		return cmd_results_new(CMD_INVALID,
				"Expected 'float_new_windows <width> <height>|disable'");
	}
	int height = strtol(argv[1], &end, 10);
	if (*end != '\0' || height <= 0) {
		return cmd_results_new(CMD_INVALID,
				"Expected 'float_new_windows <width> <height>|disable'");
	}

	config->float_new_width = width;
	config->float_new_height = height;
	return cmd_results_new(CMD_SUCCESS, NULL);
}
