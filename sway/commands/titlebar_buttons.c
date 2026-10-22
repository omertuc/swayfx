#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "util.h"

static void arrange_title_bar_iter(struct sway_container *con, void *data) {
	container_arrange_title_bar(con);
}

struct cmd_results *cmd_titlebar_buttons(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "titlebar_buttons", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	config->titlebar_buttons = parse_boolean(argv[0], config->titlebar_buttons);

	if (!config->reading) {
		root_for_each_container(arrange_title_bar_iter, NULL);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
