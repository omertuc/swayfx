#ifndef _SWAY_ICON_NODE_H
#define _SWAY_ICON_NODE_H
#include <scenefx/types/wlr_scene.h>

enum sway_icon {
	SWAY_ICON_MINIMIZE,
	SWAY_ICON_MAXIMIZE,
	SWAY_ICON_RESTORE,
	SWAY_ICON_CLOSE,
};

/**
 * A square icon drawn with lines, like a title bar button's, re-rendered at
 * the scale of the outputs it's on so it stays sharp.
 */
struct sway_icon_node {
	enum sway_icon icon;
	int size;
	float color[4];

	struct wlr_scene_node *node;
};

struct sway_icon_node *sway_icon_node_create(struct wlr_scene_tree *parent,
		enum sway_icon icon, int size, float color[4]);

void sway_icon_node_set_icon(struct sway_icon_node *node, enum sway_icon icon);

void sway_icon_node_set_size(struct sway_icon_node *node, int size);

void sway_icon_node_set_color(struct sway_icon_node *node, float color[4]);

#endif
