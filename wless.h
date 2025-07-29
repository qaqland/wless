#ifndef WLESS_MAIN_H
#define WLESS_MAIN_H

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/log.h>
#include <wordexp.h>
#include <xkbcommon/xkbcommon.h>

#include "server.h"

// should be defined by outside
#define VERSION "0.0.1"

enum ws_config_t {
	KEY_JUMP,
	KEY_EXEC,
};

struct ws_start_cmd {
	char *command;

	// TODO: pid?

	struct wl_list link;
};

struct ws_config {
	// enum wlr_log_importance log_level;
	struct wl_list start_cmds;

	struct wl_array keybinds;
};

struct ws_key_bind {
	uint32_t modifiers;
	xkb_keysym_t keysym;
	void (*function)(struct ws_server *, char *);
	char *command;
};

#endif
