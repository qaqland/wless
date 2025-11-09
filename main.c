#include "wlr/util/box.h"
#include "xdg-shell-protocol.h"
#include <assert.h>
#include <getopt.h>
#include <pwd.h>
#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_swapchain_manager.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wordexp.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#define TODO(msg) (void)

struct output *output_first(bool single);

/// type

struct server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_session *session;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wl_list outputs; // output.link
	struct wl_listener new_output;

	struct wlr_output_layout *output_layout;
	struct wl_listener output_layout_add;
	struct wl_listener output_layout_change;
	struct wl_listener output_layout_destroy; // TODO

	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_output_layout;

	struct wlr_xdg_output_manager_v1 *xdg_output_manager_v1;
	struct wlr_output_manager_v1 *output_manager_v1;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	struct wlr_scene_tree *layer_background;
	struct wlr_scene_tree *layer_bottom;
	struct wlr_scene_tree *layer_monocle; // main
	struct wlr_scene_tree *layer_top;     // fullscreen shell?
	struct wlr_scene_tree *layer_overlay;

	struct wl_list clients; // client.link
	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;

	struct wl_listener xdg_toplevel_decoration;
} server;

struct output {
	struct wlr_output *wlr_output;
	struct client *current_client;
	struct wlr_box output_box;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_rect *scene_border[4]; // left, right, top, bottom

	// int border_padding = output->wlr_output->scale;

	// both would be destroyed when output is removed from output_layout
	// struct wlr_output_layout_output *output_layout_output;
	// struct wlr_scene_output *scene_output;

	struct wl_listener commit;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;

	struct wl_listener output_layout_output_destroy;

	struct wl_list link; // server.outputs
};

struct client {
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;

	struct output *output;

	struct wl_listener client_commit;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener request_fullscreen;

	struct wl_list link; // server.clients
};

struct key {
	uint32_t modifiers;
	xkb_keysym_t keysym;
	char *command;	     // need free
	struct wl_list link; // config.keybings
};

struct config {
	float color_bg[4];	   // background
	float color_fb[4];	   // focus border
	float color_nb[4];	   // normal border
	struct wl_list keybings;   // key.link
	struct wl_array start_cmd; // char *ptr
} config;

/// getopt

void opt_list_log(int argc, char **argv) {
	if (argc == 0) {
		wlr_log(WLR_DEBUG, "[opt] empty list, skip");
		return;
	}
	char buf[1024] = {0};
	int offset = 0;

	offset += snprintf(buf + offset, sizeof(buf) - offset, "{");
	for (int i = 0; i < argc; i++) {
		offset += snprintf(buf + offset, sizeof(buf) - offset,
				   "\"%s\", ", argv[i]);
	}
	offset += snprintf(buf + offset, sizeof(buf) - offset, "NULL}");

	wlr_log(WLR_DEBUG, "[opt] list: %s", buf);
}

void opt_list_from_file(const char *path, int *argc, char ***argv) {
	*argc = 0;
	*argv = NULL;
	FILE *fp = fopen(path, "r");
	if (!fp) {
		return;
	}
	char line[256];
	wordexp_t w = {0};
	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';
		const char *p = line;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (*p == '#' || *p == '\0') {
			continue;
		}
		if (wordexp(line, &w, WRDE_NOCMD | WRDE_REUSE) != 0) {
			continue;
		}
		*argv = realloc(*argv,
				(*argc + w.we_wordc + 1) * sizeof(char *));
		for (size_t i = 0; i < w.we_wordc; i++) {
			(*argv)[(*argc)++] = strdup(w.we_wordv[i]);
		}
		(*argv)[*argc] = NULL;
	}
	wordfree(&w);
	fclose(fp);
}

void opt_exec_cmd(const char *cmd) {
	wlr_log(WLR_INFO, "[exec] spawn %s", cmd);
	fflush(stdout);

	if (fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
	}
}

// color fallback
// ("242424", config.color[0]);
// ("616161", config.color[1]);
// ("24acd4", config.color[2]);
void opt_hex_color(const char *hex, float rgba[static 4]) {
	if (rgba[3] != 0) {
		return;
	}
	if (*hex == '#') {
		hex++;
	}
	unsigned short r = 0, g = 0, b = 0, a = 255;
	sscanf(hex, "%2hx%2hx%2hx%2hx", &r, &g, &b, &a);
	rgba[0] = r / 255.0f;
	rgba[1] = g / 255.0f;
	rgba[2] = b / 255.0f;
	rgba[3] = a / 255.0f;
}

xkb_keysym_t opt_name_keysym(const char *name) {
	assert(name);

	// some overrides
#define KEY_ALIAS_LIST                                                         \
	X(enter, XKB_KEY_Return)                                               \
	X(esc, XKB_KEY_Escape)

#define X(NAME, XKB_KEY)                                                       \
	if (strcasecmp(name, #NAME) == 0) {                                    \
		return XKB_KEY;                                                \
	}
	KEY_ALIAS_LIST
#undef X

	// use this flag to get lowercase key
	return xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
}

void opt_key_log(const struct key *key) {
	assert(key->modifiers);
	assert(key->keysym);

	char mod_buf[WLR_MODIFIER_COUNT + 1] = {0};
	for (int i = 0; i < WLR_MODIFIER_COUNT; i++) {
		mod_buf[WLR_MODIFIER_COUNT - i - 1] =
			key->modifiers & 1 << i ? '1' : '0';
	}

	char key_buf[64];
	xkb_keysym_get_name(key->keysym, key_buf, sizeof(key_buf));

	wlr_log(WLR_DEBUG, "[key] %s %5s: %s", mod_buf, key_buf, key->command);
}

// future: set MRU keys to first
void opt_key_add(const char *entry) {
	enum wlr_keyboard_modifier modifiers = {0};
	const char *key_str = NULL;
	const char *cmd_str = NULL;

	char *subopts[] = {
		[0] = "shift", [1] = "caps", [2] = "ctrl", [3] = "alt",
		[4] = "mod2",  [5] = "mod3", [6] = "logo", [7] = "mod5",
		[8] = "key",   [9] = "cmd",  NULL};

	char *buf = strdup(entry);
	char *token = buf;
	char *value;
	int opt_index;
	while (*token != '\0') {
		opt_index = getsubopt(&token, subopts, &value);
		if (opt_index >= 0 && opt_index < WLR_MODIFIER_COUNT) {
			modifiers |= 1 << opt_index;
			continue;
		}
		switch (opt_index) {
		case 8:
			key_str = value;
			break;
		case 9:
			cmd_str = value;
			break;
		case -1:
			wlr_log(WLR_ERROR, "unknown suboption: %s", value);
			goto out;
		}
	}

	if (!modifiers || !key_str || !cmd_str) {
		wlr_log(WLR_ERROR, "need mod,key=xx,cmd=yy");
		goto out;
	}

	xkb_keysym_t keysym = opt_name_keysym(key_str);
	if (keysym == XKB_KEY_NoSymbol) {
		wlr_log(WLR_ERROR, "failed to parse key from %s", key_str);
		goto out;
	}

	bool has_found = false;
	struct key *old_key;
	wl_list_for_each (old_key, &config.keybings, link) {
		if (old_key->modifiers == modifiers &&
		    old_key->keysym == keysym) {
			has_found = true;
			break;
		}
	}
	if (has_found) {
		free(old_key->command);
		old_key->command = strdup(cmd_str);
	} else {
		struct key *new_key = calloc(1, sizeof(*new_key));
		new_key->command = strdup(cmd_str);
		new_key->keysym = keysym;
		new_key->modifiers = modifiers;
		wl_list_insert(&config.keybings, &new_key->link);
	}

out:
	free(buf);
}

void opt_getopt_one(int argc, char **argv, bool from_file) {
	if (from_file) {
		argc++;
		argv--;
	}
	optind = 1;
	int c;
	char **start_cmd;
	while ((c = getopt(argc, argv, "dhvo:s:r:t:")) != -1) {
		switch (c) {
		case 'd':
			wlr_log_init(WLR_DEBUG, NULL);
			break;
		case 'h':
		case 'v': // ignore
			break;
		case 'o': // keybinding
			opt_key_add(optarg);
			break;
		case 's': // start-cmd
			start_cmd = wl_array_add(&config.start_cmd,
						 sizeof(start_cmd));
			*start_cmd = strdup(optarg);
			break;
		case 'r': // path of lanuncher
			break;
		case 't': // path of terminal
			break;
		}
	}

	if (!from_file) {
		return;
	}
	argc--;
	argv++;
	for (int i = 0; i < argc; i++) {
		free(argv[i]);
	}
	free(argv);
}

void opt_getopt_all(int margc, char **margv) {
	wlr_log_init(getenv("WLESS_DEBUG") ? WLR_DEBUG : WLR_INFO, NULL);
	wl_list_init(&config.keybings);
	wl_array_init(&config.start_cmd);

	// basic
	int c;
	opterr = 0;
	while ((c = getopt(margc, margv, "dhv")) != -1) {
		switch (c) {
		case 'd':
			wlr_log_init(WLR_DEBUG, NULL);
			break;
		case 'h':
		case 'v':
			fprintf(stdout, "%s\n", WLESS_VERSION);
			exit(EXIT_SUCCESS);
		default:
			// skip them now
			continue;
		}
	}

	// check
	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (!xdg) {
		// TODO print some basic help and links
		abort();
	}
	wlr_log(WLR_INFO, "[env] %s=%s", "XDG_RUNTIME_DIR", xdg);

	// support only one config file now
	// 1. WLESS_CONFIG
	// 2. XDG_CONFIG_HOME/wlessrc
	// 3. ~/.config/wlessrc (fallback)
	char *config_path = NULL;
	do {
		const char *env_path = getenv("WLESS_CONFIG");
		if (env_path) {
			config_path = strdup(env_path);
			break;
		}

		char buf_path[256] = {0};
		const char *xdg_path = getenv("XDG_CONFIG_HOME");
		if (xdg_path) {
			snprintf(buf_path, sizeof(buf_path), "%s/wlessrc",
				 xdg_path);
			config_path = strdup(buf_path);
			break;
		}

		struct passwd *pw = getpwuid(getuid());
		snprintf(buf_path, sizeof(buf_path), "%s/.config/wlessrc",
			 pw->pw_dir);
		config_path = strdup(buf_path);
	} while (0);

	int fargc;
	char **fargv;
	opt_list_from_file(config_path, &fargc, &fargv);
	free(config_path);
	opt_list_log(fargc, fargv);

	// file
	opt_getopt_one(fargc, fargv, true);

	// cli
	opt_getopt_one(margc, margv, false);

	// log
	struct key *key;
	wl_list_for_each_reverse (key, &config.keybings, link) {
		opt_key_log(key);
	}
	char **start_cmd;
	wl_array_for_each(start_cmd, &config.start_cmd) {
		wlr_log(WLR_DEBUG, "[opt] start_cmd: %s", *start_cmd);
	}
}

/// client

struct output *client_output(struct client *client) {
	assert(client);

	struct wlr_surface *surface = client->xdg_toplevel->base->surface;
	struct wl_list *current_outputs = &surface->current_outputs;

	if (wl_list_empty(current_outputs)) {
		return NULL;
	}

	// return the first one
	struct wlr_surface_output *surface_output =
		wl_container_of(current_outputs->next, surface_output, link);
	struct output *output =
		wl_container_of(surface_output->output, output, wlr_output);
	return output;
}

// the focused output may have an empty client
struct client *client_first(bool is_free) {
	struct client *client;
	wl_list_for_each (client, &server.clients, link) {
		if (!is_free) {
			return client;
		}
		if (!client_output(client)) {
			return client;
		}
	}
	return NULL;
}

void client_position(struct client *client, struct output *output) {
	int width = client->xdg_toplevel->pending.width;
	int height = client->xdg_toplevel->pending.height;
	struct wlr_box *output_box = &output->output_box;

	int x = (output_box->width - width) / 2 + output_box->x;
	int y = (output_box->height - height) / 2 + output_box->y;
	wlr_scene_node_set_position(&client->scene_tree->node, x, y);

	// output_set_border
}

// emit: surface_handle_commit
void toplevel_client_commit_notify(struct wl_listener *listener, void *data) {
	struct client *client =
		wl_container_of(listener, client, client_commit);
	(void) data;

	// XXX
	assert(client->output);

	struct wlr_xdg_surface *xdg_surface = client->xdg_toplevel->base;
	if (xdg_surface->initial_commit) {
		return;
	}
	if (!xdg_surface->surface->mapped) {
		return;
	}
	// TODO check update_geometry, some would update in map
	// https://gitlab.freedesktop.org/wlroots/wlroots/-/merge_requests/4788
	struct wlr_xdg_toplevel *xdg_toplevel = client->xdg_toplevel;
	if (xdg_toplevel->pending.width == xdg_toplevel->current.width &&
	    xdg_toplevel->pending.height == xdg_toplevel->current.height) {
		return;
	}

	client_position(client, client->output);
}

// commit -> map -> commit -> commit
void toplevel_commit_notify(struct wl_listener *listener, void *data) {
	struct client *client = wl_container_of(listener, client, commit);
	(void) data;

	struct wlr_xdg_surface *xdg_surface = client->xdg_toplevel->base;
	if (!xdg_surface->initial_commit) {
		return;
	}
	struct output *output = output_first(false);
	int32_t width = 0, height = 0;
	if (output) {
		width = output->output_box.width;
		height = output->output_box.height;
	} else {
		wlr_log(WLR_ERROR,
			"[surface] initial_commit to an empty output?");
	}
	wlr_xdg_toplevel_set_size(client->xdg_toplevel, width, height);
}

// emit: wlr_surface_map
void toplevel_map_notify(struct wl_listener *listener, void *data) {
	struct client *client = wl_container_of(listener, client, map);
	wl_list_insert(&server.clients, &client->link);

	// TODO
	// check scene_xdg_surface_update_position
	// TODO
	// use wlr_scene_tree from output?
	client->scene_tree = wlr_scene_xdg_surface_create(
		&server.scene->tree, client->xdg_toplevel->base);
	client->scene_tree->node.data = client;
	client->xdg_toplevel->base->data = client->scene_tree;
}

// emit: wlr_surface_unmap
void toplevel_unmap_notify(struct wl_listener *listener, void *data) {
	struct client *client = wl_container_of(listener, client, unmap);
	wl_list_remove(&client->link);

	struct client *client_next = client_first(false);
	TODO(focus on client) client_next;
}

void toplevel_request_fullscreen_notify(struct wl_listener *listener,
					void *data) {
	struct client *client =
		wl_container_of(listener, client, request_fullscreen);
	(void) data;

	if (client->xdg_toplevel->base->initial_commit) {
		return;
	}

	wlr_xdg_toplevel_set_fullscreen(
		client->xdg_toplevel,
		client->xdg_toplevel->requested.fullscreen);
}

void toplevel_destroy(struct wl_listener *listener, void *data) {
	struct client *client = wl_container_of(listener, client, destroy);
	(void) data;

	wl_list_remove(&client->map.link);
	wl_list_remove(&client->unmap.link);
	wl_list_remove(&client->commit.link);
	wl_list_remove(&client->request_fullscreen.link);
	wl_list_remove(&client->destroy.link);

	free(client);
}

void new_xdg_toplevel_notify(struct wl_listener *listener, void *data) {
	(void) listener;
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct client *client = calloc(1, sizeof(*client));
	client->xdg_toplevel = xdg_toplevel;

	client->client_commit.notify = toplevel_client_commit_notify;
	wl_signal_add(&xdg_toplevel->base->surface->events.client_commit,
		      &client->client_commit);

	client->commit.notify = toplevel_commit_notify;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit,
		      &client->commit);

	client->map.notify = toplevel_map_notify;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &client->map);

	client->unmap.notify = toplevel_unmap_notify;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap,
		      &client->unmap);

	client->request_fullscreen.notify = toplevel_request_fullscreen_notify;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen,
		      &client->request_fullscreen);

	client->destroy.notify = toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &client->destroy);
}

/// output

struct output *output_first(bool single) {
	// what if the output is disabled in output_layout?
	// so we don't use server.outputs
	struct wl_list *outputs = &server.output_layout->outputs;
	if (wl_list_empty(outputs)) {
		return NULL;
	}
	if (single && outputs->next != outputs->prev) {
		return NULL;
	}
	// but only server.outputs has focus state
	struct output *output;
	wl_list_for_each (output, &server.outputs, link) {
		if (output->wlr_output->enabled) {
			return output;
		}
	}
	assert(false && "unreachable");
	return NULL;
}

const char *output_name(struct output *output) {
	const char *name = output->wlr_output->name;
	return name ? name : "OUTPUT";
}

void output_manager_send_config(void) {
	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();

	struct output *output;
	wl_list_for_each (output, &server.outputs, link) {
		struct wlr_output_configuration_head_v1 *config_head =
			wlr_output_configuration_head_v1_create(
				config, output->wlr_output);

		struct wlr_box output_box = output->output_box;

		// sway/desktop/output.c: We mark the output enabled
		// when it's switched off but not disabled
		config_head->state.enabled = !wlr_box_empty(&output_box);
		config_head->state.x = output_box.x;
		config_head->state.y = output_box.y;
	}
	wlr_output_manager_v1_set_configuration(server.output_manager_v1,
						config);
}

bool output_manager_update(struct wlr_output_configuration_v1 *config,
			   bool test_only) {
	bool is_ok = false;
	size_t states_len;
	struct wlr_backend_output_state *states =
		wlr_output_configuration_v1_build_state(config, &states_len);

	if (!states) {
		return false;
	}

	struct wlr_output_swapchain_manager swapchain_manager;
	wlr_output_swapchain_manager_init(&swapchain_manager, server.backend);

	is_ok = wlr_output_swapchain_manager_prepare(&swapchain_manager, states,
						     states_len);
	if (!is_ok || test_only) {
		goto out;
	}

	for (size_t i = 0; i < states_len; i++) {
		struct wlr_backend_output_state *backend_state = &states[i];
		struct wlr_output *wlr_output = backend_state->output;
		struct wlr_swapchain *swapchain =
			wlr_output_swapchain_manager_get_swapchain(
				&swapchain_manager, wlr_output);
		struct wlr_scene_output_state_options options = {
			.swapchain = swapchain,
		};
		struct wlr_output_state *state = &backend_state->base;
		struct wlr_scene_output *scene_output =
			wlr_scene_get_scene_output(server.scene, wlr_output);
		is_ok = wlr_scene_output_build_state(scene_output, state,
						     &options);
		if (!is_ok) {
			goto out;
		}
	}

	is_ok = wlr_backend_commit(server.backend, states, states_len);
	if (!is_ok) {
		goto out;
	}

	wlr_output_swapchain_manager_apply(&swapchain_manager);

	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each (head, &config->heads, link) {
		struct output *output = head->state.output->data;

		if (head->state.enabled) {
			wlr_output_layout_add(server.output_layout,
					      output->wlr_output, head->state.x,
					      head->state.y);
		} else {
			wlr_output_layout_remove(server.output_layout,
						 output->wlr_output);
		}
	}

out:
	wlr_output_swapchain_manager_finish(&swapchain_manager);
	for (size_t i = 0; i < states_len; i++) {
		wlr_output_state_finish(&states[i].base);
	}
	free(states);
	return is_ok;
}

void output_manager_test_notify(struct wl_listener *listener, void *data) {
	(void) listener;
	struct wlr_output_configuration_v1 *config = data;

	(output_manager_update(config, true)
		 ? wlr_output_configuration_v1_send_succeeded
		 : wlr_output_configuration_v1_send_failed)(config);
	wlr_output_configuration_v1_destroy(config);
}

void output_manager_apply_notify(struct wl_listener *listener, void *data) {
	(void) listener;
	struct wlr_output_configuration_v1 *config = data;

	(output_manager_update(config, false)
		 ? wlr_output_configuration_v1_send_succeeded
		 : wlr_output_configuration_v1_send_failed)(config);
	wlr_output_configuration_v1_destroy(config);
}

void output_set_box(struct output *output, const struct wlr_box output_box) {
	if (!output->wlr_output->enabled) {
		return;
	}
	if (wlr_box_empty(&output_box)) {
		struct wlr_output_state state = {0};
		// no init, no finish
		wlr_output_state_set_enabled(&state, false);
		wlr_output_commit_state(output->wlr_output, &state);
	}
	bool same_size = output_box.width == output->output_box.width &&
			 output_box.height == output->output_box.height;
	if (same_size) {
		return;
	}
	output->output_box = output_box;
	wlr_log(WLR_INFO, "[output] output_arrange %s: %dx%d",
		output_name(output), output_box.width,
		output->output_box.height);
}

void output_layout_output_destroy_notify(struct wl_listener *listener,
					 void *data) {
	(void) listener;
	struct wlr_output_layout_output *output_layout_output = data;
	struct output *output = output_layout_output->output->data;

	wl_list_remove(&output->output_layout_output_destroy.link);

	// if output_first has empty client, move client to it)
	if (!output->current_client) {
		return;
	}
	struct output *output_0 = output_first(false);
	if (output_0->current_client) {
		return;
	}
	// TODO
	// move client to output_first and arrange
}

void output_layout_add_notify(struct wl_listener *listener, void *data) {
	(void) listener;
	struct wlr_output_layout_output *output_layout_output = data;
	struct output *output = output_layout_output->output->data;

	// TODO
	// maybe we can set this in output_layout_change_notify
	// output_set_box

	output->output_layout_output_destroy.notify =
		output_layout_output_destroy_notify;
	wl_signal_add(&output_layout_output->events.destroy,
		      &output->output_layout_output_destroy);

	// FIXME safe to set it here?
	struct wlr_scene_output *scene_output =
		wlr_scene_output_create(server.scene, output->wlr_output);
	wlr_scene_output_layout_add_output(server.scene_output_layout,
					   output_layout_output, scene_output);

	// move client[0] to this output if it's the first output
	if (!output_first(true)) {
		return;
	}
	if (wl_list_empty(&server.clients)) {
		return;
	}

	(void) output;
	// TODO
	// move client[0] to this output
}

// FIXME remove? https://github.com/swaywm/sway/pull/8326
void output_layout_change_notify(struct wl_listener *listener, void *data) {
	(void) listener;
	struct wlr_output_layout *output_layout = data;
	struct wlr_box output_box = {0};
	struct output *output;
	wl_list_for_each (output, &server.outputs, link) {
		wlr_output_layout_get_box(output_layout, output->wlr_output,
					  &output_box);
		output_set_box(output, output_box);
	}
	output_manager_send_config();
}

void output_layout_destroy_notify(struct wl_listener *listener, void *data) {
	(void) listener;
	(void) data;

	wl_list_remove(&server.output_layout_add.link);
	wl_list_remove(&server.output_layout_change.link);
	wl_list_remove(&server.output_layout_destroy.link);
}

// emit: wlr_output_commit_state
// emit: wlr_scene_output_commit
void output_commit_notify(struct wl_listener *listener, void *data) {
	struct output *output = wl_container_of(listener, output, commit);
	struct wlr_output_event_commit *event = data;

	enum wlr_output_state_field flag =
		WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_ENABLED |
		WLR_OUTPUT_STATE_SCALE | WLR_OUTPUT_STATE_TRANSFORM |
		WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;

	if (event->state->committed & flag) {
		output_manager_send_config();
	}
}

void output_frame_notify(struct wl_listener *listener, void *data) {
	struct output *output = wl_container_of(listener, output, frame);
	struct wlr_output *wlr_output = data;

	assert(wlr_output->enabled);

	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(server.scene, wlr_output);

	// FIXME check client_set_size
	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now = {0};
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

// emit: wlr_output_send_request_state()
// from: wlroots/backend/{wayland,x11}/output.c
void output_request_state_notify(struct wl_listener *listener, void *data) {
	struct output *output =
		wl_container_of(listener, output, request_state);
	struct wlr_output_event_request_state *event = data;

	assert(output->wlr_output == event->output);
	wlr_output_commit_state(output->wlr_output, event->state);
}

void output_destroy(struct wl_listener *listener, void *data) {
	struct output *output = wl_container_of(listener, output, destroy);
	assert(output->wlr_output == data);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);

	wlr_output_layout_remove(server.output_layout, output->wlr_output);
	free(output);
}

// only borders have color
void output_set_color(struct output *output, const float color[static 4]) {
	for (int i = 0; i < 4; i++) {
		wlr_scene_rect_set_color(output->scene_border[i], color);
	}
}

void output_set_border(struct output *output, struct wlr_box *client_box) {
	int padding = output->wlr_output->scale;
	struct wlr_box border_box = {0};

	if (!client_box || wlr_box_empty(client_box)) {
		border_box = output->output_box;
	} else {
		border_box.x = client_box->x - padding;
		border_box.y = client_box->y - padding;
		border_box.width = client_box->width + padding * 2;
		border_box.height = client_box->height + padding * 2;
	}

	wlr_scene_rect_set_size(output->scene_border[0], padding,
				border_box.height);
	wlr_scene_rect_set_size(output->scene_border[1], padding,
				border_box.height);
	wlr_scene_rect_set_size(output->scene_border[2],
				border_box.width - padding * 2, padding);
	wlr_scene_rect_set_size(output->scene_border[3],
				border_box.width - padding * 2, padding);

	wlr_scene_node_set_position(&output->scene_border[0]->node,
				    border_box.x, border_box.y);
	wlr_scene_node_set_position(&output->scene_border[1]->node,
				    border_box.x + border_box.width - padding,
				    border_box.y);
	wlr_scene_node_set_position(&output->scene_border[2]->node,
				    border_box.x + padding, border_box.y);
	wlr_scene_node_set_position(&output->scene_border[3]->node,
				    border_box.x + padding,
				    border_box.y + border_box.height - padding);
}

void new_output_notify(struct wl_listener *listener, void *data) {
	(void) listener;
	struct wlr_output *wlr_output = data;

	wlr_log(WLR_INFO, "[output] new_output: %s", wlr_output->name);

	// FIXME safe
	wlr_output_init_render(wlr_output, server.allocator, server.renderer);

	// state
	struct wlr_output_state state = {0};
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true); // default is 0
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	// output
	struct output *output = calloc(1, sizeof(*output));
	wlr_output->data = output;
	output->wlr_output = wlr_output;
	wl_list_insert(&server.outputs, &output->link); // before output_layout

	// notify
	output->commit.notify = output_commit_notify;
	wl_signal_add(&wlr_output->events.commit, &output->commit);

	output->frame.notify = output_frame_notify;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->request_state.notify = output_request_state_notify;
	wl_signal_add(&wlr_output->events.request_state,
		      &output->request_state);

	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	// layout, see output_layout_add_notify
	wlr_output_layout_add_auto(server.output_layout, wlr_output);

	// border
	// TODO move this to output_layout_add_notify?
	output->scene_tree = wlr_scene_tree_create(&server.scene->tree);
	for (int i = 0; i < 4; i++) {
		output->scene_border[i] = wlr_scene_rect_create(
			output->scene_tree, 0, 0, config.color_nb);
	}
}

/// main
int main(int argc, char **argv) {
	opt_getopt_all(argc, argv);

	return 0;

	server.wl_display = wl_display_create();
	if (!server.wl_display) {
		wlr_log(WLR_ERROR, "[init] failed to create wl_display");
		goto err_create_display;
	}

	server.backend = wlr_backend_autocreate(
		wl_display_get_event_loop(server.wl_display), &server.session);
	if (!server.backend) {
		wlr_log(WLR_ERROR, "[init] failed to create wlr_backend");
		goto err_create_backend;
	}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (!server.renderer) {
		wlr_log(WLR_ERROR, "[init] failed to create wlr_renderer");
		goto err_create_renderer;
	}

	// FIXME safe
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	server.allocator =
		wlr_allocator_autocreate(server.backend, server.renderer);
	if (!server.allocator) {
		wlr_log(WLR_ERROR, "[init] failed to create wlr_allocator");
		goto err_create_allocator;
	}

	// output
	wl_list_init(&server.outputs);
	server.new_output.notify = new_output_notify;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.output_layout = wlr_output_layout_create(server.wl_display);
	server.output_layout_add.notify = output_layout_add_notify;
	wl_signal_add(&server.output_layout->events.add,
		      &server.output_layout_add);
	server.output_layout_change.notify = output_layout_change_notify;
	wl_signal_add(&server.output_layout->events.change,
		      &server.output_layout_change);
	server.output_layout_destroy.notify = output_layout_destroy_notify;
	wl_signal_add(&server.output_layout->events.destroy,
		      &server.output_layout_destroy);

	server.scene = wlr_scene_create();
	server.scene_output_layout = wlr_scene_attach_output_layout(
		server.scene, server.output_layout);

	server.xdg_output_manager_v1 = wlr_xdg_output_manager_v1_create(
		server.wl_display, server.output_layout);

	server.output_manager_v1 =
		wlr_output_manager_v1_create(server.wl_display);
	server.output_manager_test.notify = output_manager_test_notify;
	wl_signal_add(&server.output_manager_v1->events.test,
		      &server.output_manager_test);
	server.output_manager_apply.notify = output_manager_apply_notify;
	wl_signal_add(&server.output_manager_v1->events.apply,
		      &server.output_manager_apply);

	// client
	wl_list_init(&server.clients);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_toplevel.notify = new_xdg_toplevel_notify;
	wl_signal_add(&server.xdg_shell->events.new_toplevel,
		      &server.new_xdg_toplevel);

	// wl_display_add_destroy_listener
	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	// FIXME

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
	}
	if (!wlr_backend_start(server.backend)) {
	}

	setenv("WAYLAND_DISPLAY", socket, true);

	char **start_cmd;
	wl_array_for_each(start_cmd, &config.start_cmd) {
		opt_exec_cmd(*start_cmd);
	}

	wl_display_run(server.wl_display);

	exit(EXIT_SUCCESS);

	wlr_allocator_destroy(server.allocator);
err_create_allocator:
	wlr_renderer_destroy(server.renderer);
err_create_renderer:
	wlr_backend_destroy(server.backend);
err_create_backend:
	wl_display_destroy(server.wl_display);
err_create_display:
	exit(EXIT_FAILURE);
}
