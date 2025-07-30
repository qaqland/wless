#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>
#include <wordexp.h>

#include "action.h"
#include "client.h"
#include "input.h"
#include "output.h"
#include "server.h"
#include "wless.h"

static struct ws_config *parse_args(int argc, char *argv[]) {
	struct ws_config *config = calloc(1, sizeof(*config));
	assert(config);

	wl_list_init(&config->start_cmds);
	struct ws_start_cmd *cmd = NULL;
	wlr_log_init(WLR_INFO, NULL);

	int c;
	while ((c = getopt(argc, argv, "ds:hv")) != -1) {
		switch (c) {
		case 'd':
			wlr_log_init(WLR_DEBUG, NULL);
			break;
		case 's':
			cmd = calloc(1, sizeof(*cmd));
			cmd->command = optarg;
			wl_list_insert(config->start_cmds.prev, &cmd->link);
			wlr_log(WLR_INFO, "add startup: %s", optarg);
			break;
		case 'h':
			goto print_help;
		case 'v':
			goto print_version;
		default:
			goto print_error;
		}
	}

	if (optind != argc) {
		goto print_error;
	}

	return config;

print_help:
	printf("TODO: help\n");
	exit(EXIT_SUCCESS);

print_version:
	printf("TODO: version\n");
	exit(EXIT_SUCCESS);

print_error:
	printf("TODO: help\n");
	exit(EXIT_FAILURE);
}

static xkb_keysym_t str2keysym(const char *word) {
	assert(word);

	// some overrides
#define KEY_ALIAS_LIST                                                         \
	X(ENTER, XKB_KEY_Return)                                               \
	X(ESC, XKB_KEY_Escape)

#define X(NAME, XKB_KEY)                                                       \
	if (strcmp(word, #NAME) == 0) {                                        \
		return XKB_KEY;                                                \
	}
	KEY_ALIAS_LIST
#undef X

	// use this flag to get lowercase key
	xkb_keysym_t key =
		xkb_keysym_from_name(word, XKB_KEYSYM_CASE_INSENSITIVE);
	if (key == XKB_KEY_NoSymbol) {
		wlr_log(WLR_INFO, "failed to parse key from %s", word);
	}
	return key;
}

static void parse_entry(struct ws_config *config, const char *entry,
			enum ws_config_t type) {
	const char *const equal = strchr(entry, '=');
	if (!equal || equal - entry > 15) {
		wlr_log(WLR_INFO, "failed to parse %s", entry);
		return;
	}
	char key[16] = {0};
	strncpy(key, entry, equal - entry);

	xkb_keysym_t keysym;
	struct ws_key_bind *keybind;
	switch (type) {
	case KEY_JUMP:
		// WLESS_JUMP_A=firefox
		keysym = str2keysym(key);
		if (keysym == XKB_KEY_NoSymbol) {
			break;
		}
		// force-exec
		keybind = wl_array_add(&config->keybinds, sizeof(*keybind));
		keybind->modifiers = WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT;
		keybind->command = equal + 1;
		keybind->keysym = keysym;
		keybind->function = action_exec;
		// jump-or-exec
		keybind = wl_array_add(&config->keybinds, sizeof(*keybind));
		keybind->modifiers = WLR_MODIFIER_LOGO;
		keybind->command = equal + 1;
		keybind->keysym = keysym;
		keybind->function = action_jump;
		break;

	case KEY_EXEC:
		// WLESS_EXEC_L=./some-script.sh
		keysym = str2keysym(key);
		if (keysym == XKB_KEY_NoSymbol) {
			break;
		}
		// always-exec
		keybind = wl_array_add(&config->keybinds, sizeof(*keybind));
		keybind->modifiers = WLR_MODIFIER_LOGO;
		keybind->command = equal + 1;
		keybind->keysym = keysym;
		keybind->function = action_exec;
		break;
	}
}

static void parse_envs(struct ws_config *config) {
	const char *xdg_runtime_dir = NULL;
	wl_array_init(&config->keybinds);

	extern char **environ;
	for (char **env = environ; *env; env++) {
		const char *entry = *env;
		if (strncmp(entry, "XDG_RUNTIME_DIR",
			    strlen("XDG_RUNTIME_DIR")) == 0) {
			wlr_log(WLR_INFO, "env: %s", entry);
			xdg_runtime_dir = entry;
			continue;
		}

		if (strncmp(entry, "WLESS_", strlen("WLESS_")) == 0) {
			wlr_log(WLR_DEBUG, "env: %s", entry);
			entry += strlen("WLESS_");
		} else {
			continue;
		}

		if (strncmp(entry, "JUMP_", strlen("JUMP_")) == 0) {
			entry += strlen("JUMP_");
			parse_entry(config, entry, KEY_JUMP);
			continue;
		}
		if (strncmp(entry, "EXEC_", strlen("EXEC_")) == 0) {
			entry += strlen("EXEC_");
			parse_entry(config, entry, KEY_EXEC);
			continue;
		}

		// TODO: other settings
	}

	// https://github.com/swaywm/sway/issues/7202
	if (!xdg_runtime_dir) {
		wlr_log(WLR_ERROR, "XDG_RUNTIME_DIR is not set");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[]) {
	// test

	// init
	struct ws_server s = {.magic = 6};
	s.config = parse_args(argc, argv); // exit
	parse_envs(s.config);

	// signal
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	// main
	s.wl_display = wl_display_create();
	if (!s.wl_display) {
		wlr_log(WLR_ERROR, "failed to create wl_display");
		goto err_wl_display;
	}

	// FIXME: check other usage? or just put it in next function
	s.wl_event_loop = wl_display_get_event_loop(s.wl_display);

	s.backend = wlr_backend_autocreate(s.wl_event_loop, &s.session);
	if (!s.backend) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		goto err_backend;
	}

	s.renderer = wlr_renderer_autocreate(s.backend);
	if (!s.renderer) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		goto err_renderer;
	}

	if (!wlr_renderer_init_wl_display(s.renderer, s.wl_display)) {
		wlr_log(WLR_ERROR, "failed to init display");
		goto err_init_display;
	}

	s.allocator = wlr_allocator_autocreate(s.backend, s.renderer);
	if (!s.allocator) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		goto err_allocator;
	}

	// FIXME
	wlr_compositor_create(s.wl_display, 6, s.renderer);
	wlr_subcompositor_create(s.wl_display);
	wlr_data_device_manager_create(s.wl_display);

	// output
	wl_list_init(&s.outputs);

	// emit: new_output_reemit()
	// data: struct wlr_output *
	s.new_output.notify = handle_new_output;
	wl_signal_add(&s.backend->events.new_output, &s.new_output);

	s.output_layout = wlr_output_layout_create(s.wl_display);

	// emit: output_layout_reconfigure()
	// data: struct wlr_output_layout *
	s.output_layout_change.notify = handle_output_layout_change;
	wl_signal_add(&s.output_layout->events.change, &s.output_layout_change);

	// scene
	s.scene = wlr_scene_create();
	s.scene_output_layout =
		wlr_scene_attach_output_layout(s.scene, s.output_layout);

	// FIXME firefox
	wlr_xdg_output_manager_v1_create(s.wl_display, s.output_layout);
	wlr_viewporter_create(s.wl_display);
	wlr_presentation_create(s.wl_display, s.backend, 2);

	// wlr-randr
	s.output_manager_v1 = wlr_output_manager_v1_create(s.wl_display);

	// emit: config_handle_apply()
	// data: struct wlr_output_configuration_v1 *
	s.output_manager_apply.notify = handle_output_manager_apply;
	wl_signal_add(&s.output_manager_v1->events.apply,
		      &s.output_manager_apply);

	// emit: config_handle_test()
	// data: struct wlr_output_configuration_v1 *
	s.output_manager_test.notify = handle_output_manager_test;
	wl_signal_add(&s.output_manager_v1->events.test,
		      &s.output_manager_test);

	// client
	wl_list_init(&s.clients);

	// TODO: check XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION
	s.xdg_shell = wlr_xdg_shell_create(s.wl_display, 3);

	// emit: create_xdg_toplevel()
	// data: struct wlr_xdg_toplevel *
	s.new_xdg_toplevel.notify = handle_new_xdg_toplevel;
	wl_signal_add(&s.xdg_shell->events.new_toplevel, &s.new_xdg_toplevel);

	// emit: create_xdg_popup()
	// data: struct wlr_xdg_popup*
	s.new_xdg_popup.notify = handle_new_xdg_popup;
	wl_signal_add(&s.xdg_shell->events.new_popup, &s.new_xdg_popup);

	// TODO: layer_shell

	// xdg_decoration
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(s.wl_display);

	// emit: decoration_manager_handle_get_toplevel_decoration()
	// data: struct wlr_xdg_toplevel_decoration_v1 *
	s.xdg_toplevel_decoration.notify = handle_new_xdg_toplevel_decoration;
	wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration,
		      &s.xdg_toplevel_decoration);

	// cursor
	s.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(s.cursor, s.output_layout);

	s.xcursor_manager = wlr_xcursor_manager_create(NULL, 24);

	// emit: handle_pointer_motion_absolute()
	// data: struct wlr_pointer_motion_absolute_event *
	s.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
	wl_signal_add(&s.cursor->events.motion_absolute,
		      &s.cursor_motion_absolute);

	// emit: handle_pointer_motion()
	// data: struct wlr_pointer_motion_event *
	s.cursor_motion_relative.notify = handle_cursor_motion_relative;
	wl_signal_add(&s.cursor->events.motion, &s.cursor_motion_relative);

	// emit: handle_pointer_button()
	// data: struct wlr_pointer_button_event *
	s.cursor_button.notify = handle_cursor_button;
	wl_signal_add(&s.cursor->events.button, &s.cursor_button);

	// emit: handle_pointer_axis()
	// data: struct wlr_pointer_axis_event *
	s.cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&s.cursor->events.axis, &s.cursor_axis);

	// emit: handle_pointer_frame()
	// data: struct wlr_cursor *
	s.cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&s.cursor->events.frame, &s.cursor_frame);

	// input
	wl_list_init(&s.keyboards);
	wl_list_init(&s.pointers);

	// emit: handle_device_added()
	// data: struct wlr_input_device *
	s.new_input.notify = handle_new_input;
	wl_signal_add(&s.backend->events.new_input, &s.new_input);

	s.seat = wlr_seat_create(s.wl_display, "seat0");

	// emit: pointer_set_cursor()
	// data: struct wlr_seat_pointer_request_set_cursor_event*
	s.request_cursor.notify = handle_request_cursor;
	wl_signal_add(&s.seat->events.request_set_cursor, &s.request_cursor);

	// emit: wlr_seat_request_set_selection()
	// data: struct wlr_seat_request_set_selection_event*
	s.request_set_selection.notify = handle_request_set_selection;
	wl_signal_add(&s.seat->events.request_set_selection,
		      &s.request_set_selection);

	const char *socket = wl_display_add_socket_auto(s.wl_display);
	if (!socket) {
		wlr_log(WLR_ERROR, "[main] failed to add wl_display socket");
		goto err_add_socket;
	}

	if (!wlr_backend_start(s.backend)) {
		wlr_log(WLR_ERROR, "[main] failed to start wlr_backend");
		goto err_start_backend;
	}

	setenv("WAYLAND_DISPLAY", socket, true);

	struct ws_start_cmd *cmd;
	wl_list_for_each_reverse (cmd, &s.config->start_cmds, link) {
		action_exec(&s, cmd->command);
	}

	wl_display_run(s.wl_display);

	exit(EXIT_SUCCESS);

err_start_backend:
err_add_socket:
err_allocator:
err_init_display:
	wlr_renderer_destroy(s.renderer);

err_renderer:
	wlr_backend_destroy(s.backend);

err_backend:
	wl_display_destroy(s.wl_display);

err_wl_display:
	exit(EXIT_FAILURE);
}
