#include <wlr/util/box.h>
#include <assert.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/wayland.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

// exit when 0, NULL or false
#define CHECK(var, ...)                                                                            \
	do {                                                                                       \
		if (!(var)) {                                                                      \
			wlr_log(WLR_ERROR, __VA_ARGS__);                                           \
			exit(EXIT_FAILURE);                                                        \
		}                                                                                  \
	} while (0)

#define STR(name) #name

enum {
	WS_CONFIG,
	WS_COLOR_BG,

	// internal use
	WS_SERVER_PID,
};

float color_bg[4];

struct ws_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_output_layout;
	struct wlr_scene_rect *bg_scene;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels; // struct ws_toplevel.link

	struct wl_listener xdg_toplevel_decoration;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion_relative;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;

	struct wlr_output_manager_v1 *output_manager_v1;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	struct wlr_output_layout *output_layout;
	struct wl_listener output_layout_change;

	struct wl_list outputs; // struct ws_output.link
	struct wl_listener new_output;
	struct ws_output *focused_output;
};

struct ws_server server;

struct ws_output {
	struct wl_list link; // struct ws_server.outputs

	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct ws_toplevel {
	struct wl_list link; // struct ws_server.toplevels

	struct ws_output *output;

	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_fullscreen;
};

struct ws_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct ws_xdg_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener request_mode;
};

struct ws_keyboard {
	struct wl_list link;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

// TODO don't read config from environment
const char *env_or(const char *env, const char *dvalue) {
	char *value = getenv(env);
	// check if env_var is NULL or empty
	return value && strlen(value) ? value : dvalue;
}

void color_hex2rgba(const char *hex, float (*color)[4]) {
	if (*hex == '#') {
		hex++;
	}
	unsigned short r, g, b; // set default
	r = g = b = 0;		// black
	unsigned short a = 255; // opaque

	sscanf(hex, "%2hx%2hx%2hx%2hx", &r, &g, &b, &a);
	(*color)[0] = r / 255.0f;
	(*color)[1] = g / 255.0f;
	(*color)[2] = b / 255.0f;
	(*color)[3] = a / 255.0f;
}

void focus_toplevel(struct ws_toplevel *toplevel, struct wlr_surface *surface) {
	if (toplevel == NULL) {
		return;
	}
	struct wlr_seat *seat = server.seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;

	if (prev_surface == surface) {
		return;
	}
	if (prev_surface) {
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
		}
	}
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server.toplevels, &toplevel->link);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(seat, toplevel->xdg_toplevel->base->surface,
					       keyboard->keycodes, keyboard->num_keycodes,
					       &keyboard->modifiers);
	}
}

void keyboard_handle_modifiers(struct wl_listener *listener, void *) {
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	struct ws_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to
	 * the same seat. You can swap out the underlying wlr_keyboard like this
	 * and wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(server.seat, keyboard->wlr_keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(server.seat, &keyboard->wlr_keyboard->modifiers);
}

bool key_binding(xkb_keysym_t sym) {
	// xkb_keysym_from_name();
	switch (sym) {
	case XKB_KEY_Escape:
		wl_display_terminate(server.wl_display);
		break;
	case XKB_KEY_Tab:
		/* Cycle to the next toplevel */
		if (wl_list_length(&server.toplevels) < 2) {
			break;
		}
		struct ws_toplevel *next_toplevel =
			wl_container_of(server.toplevels.prev, next_toplevel, link);
		focus_toplevel(next_toplevel, next_toplevel->xdg_toplevel->base->surface);
		break;
	case XKB_KEY_Return:
		if (fork() == 0) {
			execl("/usr/bin/foot", "/usr/bin/foot", NULL);
		}
		break;
	default:
		return false;
	}
	return true;
}

void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct ws_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server.seat;

	/* Translate libinput keycode -> xkbcommon */
	xkb_keycode_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_ALT) && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		/* If Alt is held down and this button was _pressed_, we attempt
		 * to process it as a compositor keybinding. */
		for (int i = 0; i < nsyms; i++) {
			handled = key_binding(syms[i]);
		}
	}

	if (!handled) {
		/* Otherwise, we pass it along to the client. */
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

void keyboard_destroy(struct wl_listener *listener, void *) {
	/* This event is raised by the keyboard base wlr_input_device to signal
	 * the destruction of the wlr_keyboard. It will no longer receive events
	 * and should be destroyed.
	 */
	struct ws_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

void input_new_keyboard(struct ws_server *server, struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct ws_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	wl_list_insert(&server->keyboards, &keyboard->link);
}

void input_new_pointer(struct ws_server *server, struct wlr_input_device *device) {
	/* We don't do anything special with pointers. All of our pointer
	 * handling is proxied through wlr_cursor. On another compositor, you
	 * might take this opportunity to do libinput configuration on the
	 * device to set acceleration, etc. */
	wlr_cursor_attach_input_device(server->cursor, device);
}

void server_handle_new_input(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct ws_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		input_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		input_new_pointer(server, device);
		break;
	default:
		break;
	}
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In TinyWL we always have a cursor, even
	 * if there are no pointer devices, so we always include that
	 * capability. */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct ws_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x,
				       event->hotspot_y);
	}
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct ws_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

struct ws_toplevel *desktop_toplevel_at(struct ws_server *server, double lx, double ly,
					struct wlr_surface **surface, double *sx, double *sy) {
	/* This returns the topmost node in the scene at the given layout
	 * coords. We only care about surface nodes as we are specifically
	 * looking for a surface in the surface tree of a tinywl_toplevel. */
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;
	/* Find the node corresponding to the tinywl_toplevel at the root of
	 * this surface tree, it is the only one for which we set the data
	 * field. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree->node.data;
}

void process_cursor_motion(struct ws_server *server, uint32_t time) {
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct ws_toplevel *toplevel = desktop_toplevel_at(server, server->cursor->x,
							   server->cursor->y, &surface, &sx, &sy);
	if (!toplevel) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(seat);
	}
}

void server_cursor_motion_relative(struct wl_listener *listener, void *data) {
	struct ws_server *server = wl_container_of(listener, server, cursor_motion_relative);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct ws_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(struct wl_listener *listener, void *data) {
	struct ws_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct ws_toplevel *toplevel = desktop_toplevel_at(server, server->cursor->x,
							   server->cursor->y, &surface, &sx, &sy);
	focus_toplevel(toplevel, surface);
}

void server_cursor_axis(struct wl_listener *, void *data) {
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server.seat, event->time_msec, event->orientation,
				     event->delta, event->delta_discrete, event->source,
				     event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *) {
	struct ws_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

////////////////////////////////////////////////////////////////////////////////

void xdg_toplevel_position(struct ws_toplevel *toplevel) {
	// skip unmapped toplevel
	if (!toplevel->output) {
		return;
	}
	struct wlr_box output_box;
	wlr_output_layout_get_box(server.output_layout, toplevel->output->wlr_output, &output_box);
	// TODO
	// is_primary
	if (toplevel->xdg_toplevel->parent == NULL) {
	}
	if (toplevel->scene_tree) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, output_box.x,
					    output_box.y);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, output_box.width,
					  output_box.height);
		/* wlr_log(WLR_DEBUG, "set position width: %d, height: %d", output_box.width, */
		/* 	output_box.height); */
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
	}
}

void xdg_toplevel_position_all() {
	struct ws_toplevel *toplevel;
	wl_list_for_each (toplevel, &server.toplevels, link) {
		xdg_toplevel_position(toplevel);
	}

	// set background
	struct wlr_box output_box;
	wlr_output_layout_get_box(server.output_layout, NULL, &output_box);
	wlr_scene_rect_set_size(server.bg_scene, output_box.width, output_box.height);
	wlr_scene_node_set_position(&server.bg_scene->node, output_box.x, output_box.y);
	wlr_scene_node_lower_to_bottom(&server.bg_scene->node);
}

////////////////////////////////////////////////////////////////////////////////

void update_output_manager_config(void) {
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	struct ws_output *output;
	wl_list_for_each (output, &server.outputs, link) {
		struct wlr_output_configuration_head_v1 *config_head =
			wlr_output_configuration_head_v1_create(config, output->wlr_output);
		struct wlr_box output_box;
		wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);
		config_head->state.enabled = !wlr_box_empty(&output_box);
		config_head->state.x = output_box.x;
		config_head->state.y = output_box.y;
	}
	wlr_output_manager_v1_set_configuration(server.output_manager_v1, config);
}

// ws_server.output_layout_change (server.output_layout->events.change)
void handle_output_layout_change(struct wl_listener *, void *) {
	xdg_toplevel_position_all();
	update_output_manager_config();
}

////////////////////////////////////////////////////////////////////////////////

bool output_apply_config_head(struct ws_output *output,
			      struct wlr_output_configuration_head_v1 *head, bool test_only) {
	struct wlr_output_state state = {0};
	wlr_output_head_v1_state_apply(&head->state, &state);

	bool is_ok = true;
	if (test_only) {
		is_ok = wlr_output_test_state(output->wlr_output, &state);
	} else {
		is_ok = wlr_output_commit_state(output->wlr_output, &state);
	}
	if (test_only || !is_ok) {
		goto finish;
	}

	if (!head->state.enabled) {
		wlr_output_layout_remove(server.output_layout, output->wlr_output);
		goto finish;
	}

	struct wlr_output_layout_output *l_output = wlr_output_layout_add(
		server.output_layout, output->wlr_output, head->state.x, head->state.y);
	wlr_scene_output_layout_add_output(server.scene_output_layout, l_output,
					   output->scene_output);
finish:
	wlr_output_state_finish(&state);
	return is_ok;
}

void output_manager_apply_config(struct wlr_output_configuration_v1 *config, bool test_only) {
	struct wlr_output_configuration_head_v1 *head;
	bool is_ok = true;
	wl_list_for_each (head, &config->heads, link) {
		struct ws_output *output = head->state.output->data;
		is_ok &= output_apply_config_head(output, head, test_only);
	}
	if (is_ok) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
}

// ws_server.output_manager_test (server.output_manager_v1->events.test)
void handle_output_manager_test(struct wl_listener *, void *data) {
	struct wlr_output_configuration_v1 *config = data;
	output_manager_apply_config(config, true);
}

// ws_server.output_manager_apply (server.output_manager_v1->events.apply)
void handle_output_manager_apply(struct wl_listener *, void *data) {
	struct wlr_output_configuration_v1 *config = data;
	output_manager_apply_config(config, false);
}

////////////////////////////////////////////////////////////////////////////////

// ws_output.frame (wlr_output->events.frame)
void handle_output_frame(struct wl_listener *listener, void *) {
	struct ws_output *output = wl_container_of(listener, output, frame);

	// TODO skip offline output, but would this event happen?
	if (!output->wlr_output->enabled || !output->scene_output) {
		return;
	}

	wlr_scene_output_commit(output->scene_output, NULL);

	struct timespec now = {0};
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(output->scene_output, &now);
}

// ws_output.request_state (wlr_output->events.request_state)
void handle_output_request_state(struct wl_listener *listener, void *data) {
	struct ws_output *output = wl_container_of(listener, output, request_state);
	struct wlr_output_event_request_state *event = data;

	if (wlr_output_commit_state(output->wlr_output, event->state)) {
		update_output_manager_config();
	}
}

// ws_output.destroy (wlr_output->events.destroy)
void handle_output_destroy(struct wl_listener *listener, void *) {
	struct ws_output *output = wl_container_of(listener, output, destroy);
	/* bool was_nested = wlr_output_is_wl(output->wlr_output); */

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);

	// wlr_scene_output_layout should remove scene_output automatically
	wlr_output_layout_remove(server.output_layout, output->wlr_output);
	free(output);

	/* if (wl_list_empty(&server.outputs) && was_nested) { */
	if (wl_list_empty(&server.outputs)) {
		exit(EXIT_SUCCESS);
	} else {
		server.focused_output = (struct ws_output *) server.outputs.next;
	}

	struct ws_toplevel *toplevel;
	wl_list_for_each (toplevel, &server.toplevels, link) {
		if (toplevel->output == output) {
			toplevel->output = server.focused_output;
			xdg_toplevel_position(toplevel); // FIXME need?
		}
	}
}

// ws_server.new_output (server.backend->events.new_output)
void handle_new_output(struct wl_listener *, void *data) {
	struct wlr_output *wlr_output = data;

	// FIXME check
	wlr_output_init_render(wlr_output, server.allocator, server.renderer);

	// output state (mode)
	struct wlr_output_state state;
	wlr_output_state_init(&state);

	// switch on first, load config later
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (!mode) {
		wlr_log(WLR_ERROR, "wlr_output_preferred_mode");
	}
	wlr_output_state_set_mode(&state, mode);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	// ws_output
	struct ws_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	wlr_output->data = output;

	wl_list_insert(&server.outputs, &output->link);

	// move focus to the newest output
	server.focused_output = output;

	output->frame.notify = handle_output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->request_state.notify = handle_output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	output->destroy.notify = handle_output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	// TODO leave it unused?
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(server.output_layout, wlr_output);

	output->scene_output = wlr_scene_output_create(server.scene, wlr_output);
	wlr_scene_output_layout_add_output(server.scene_output_layout, l_output,
					   output->scene_output);
}

////////////////////////////////////////////////////////////////////////////////

// ws_toplevel.map (xdg_toplevel->base->surface->events.map)
void handle_xdg_toplevel_map(struct wl_listener *listener, void *) {
	struct ws_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	toplevel->scene_tree = wlr_scene_subsurface_tree_create(
		&server.scene->tree, toplevel->xdg_toplevel->base->surface);
	toplevel->scene_tree->node.data = toplevel;
	toplevel->xdg_toplevel->base->data = toplevel->scene_tree;
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_insert(&server.toplevels, &toplevel->link);

	xdg_toplevel_position(toplevel);
	focus_toplevel(toplevel, toplevel->xdg_toplevel->base->surface);
	wlr_log(WLR_DEBUG, "call xdg_toplevel map");
}

// ws_toplevel.unmap (xdg_toplevel->base->surface->events.unmap)
void handle_xdg_toplevel_unmap(struct wl_listener *listener, void *) {
	struct ws_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	wl_list_remove(&toplevel->link);
}

// ws_toplevel.commit (xdg_toplevel->base->surface->events.commit)
void handle_xdg_toplevel_commit(struct wl_listener *listener, void *) {
	// commit -> map -> commit -> commit
	struct ws_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	if (toplevel->xdg_toplevel->base->initial_commit) {
		// TODO more function
		toplevel->output = server.focused_output;
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}
	wlr_log(WLR_DEBUG, "call xdg_toplevel commit");
}

// ws_toplevel.destroy (xdg_toplevel->events.destroy)
void handle_xdg_toplevel_destroy(struct wl_listener *listener, void *) {
	struct ws_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	free(toplevel);
}

// ws_toplevel.request_fullscreen (xdg_toplevel->events.request_fullscreen)
void handle_xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *) {
	struct ws_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
	if (!toplevel->xdg_toplevel->base->initial_commit) {
		return;
	}
	xdg_toplevel_position(toplevel);
	wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel,
					toplevel->xdg_toplevel->requested.fullscreen);
}

// ws_server.new_xdg_toplevel (server.xdg_shell->events.new_toplevel)
void handle_new_xdg_toplevel(struct wl_listener *, void *data) {
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct ws_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (!toplevel) {
		return;
	}
	toplevel->xdg_toplevel = xdg_toplevel;
	/* toplevel->scene_tree = */
	/* 	wlr_scene_xdg_surface_create(&server.scene->tree, xdg_toplevel->base); */

	toplevel->map.notify = handle_xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = handle_xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = handle_xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);
	toplevel->destroy.notify = handle_xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);
	toplevel->request_fullscreen.notify = handle_xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

////////////////////////////////////////////////////////////////////////////////

void handle_xdg_popup_commit(struct wl_listener *listener, void *) {
	struct ws_popup *popup = wl_container_of(listener, popup, commit);
	if (popup->xdg_popup->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

void handle_xdg_popup_destroy(struct wl_listener *listener, void *) {
	struct ws_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

// ws_server.new_xdg_popup (server.xdg_shell->events.new_popup)
void handle_new_xdg_popup(struct wl_listener *, void *data) {
	struct wlr_xdg_popup *xdg_popup = data;
	struct ws_popup *popup = calloc(1, sizeof(*popup));
	if (!popup) {
		return;
	}
	popup->xdg_popup = xdg_popup;

	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = handle_xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = handle_xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

////////////////////////////////////////////////////////////////////////////////

// ws_xdg_decoration.destroy (wlr_decoration->events.destroy)
void xdg_decoration_handle_destroy(struct wl_listener *listener, void *) {
	struct ws_xdg_decoration *xdg_decoration =
		wl_container_of(listener, xdg_decoration, destroy);
	wl_list_remove(&xdg_decoration->destroy.link);
	wl_list_remove(&xdg_decoration->commit.link);
	wl_list_remove(&xdg_decoration->request_mode.link);
	free(xdg_decoration);
}

// ws_xdg_decoration.commit (wlr_decoration->toplevel->base->surface->events.commit)
void xdg_decoration_handle_commit(struct wl_listener *listener, void *) {
	struct ws_xdg_decoration *xdg_decoration =
		wl_container_of(listener, xdg_decoration, commit);
	if (xdg_decoration->wlr_decoration->toplevel->base->initial_commit) {
		wlr_xdg_toplevel_decoration_v1_set_mode(
			xdg_decoration->wlr_decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
}

// ws_xdg_decoration.request_mode (wlr_decoration->events.request_mode)
void xdg_decoration_handle_request_mode(struct wl_listener *listener, void *) {
	struct ws_xdg_decoration *xdg_decoration =
		wl_container_of(listener, xdg_decoration, request_mode);
	if (xdg_decoration->wlr_decoration->toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(
			xdg_decoration->wlr_decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
}

// ws_server.xdg_toplevel_decoration (xdg_decoration_manager->events.new_toplevel_decoration)
void handle_xdg_toplevel_decoration(struct wl_listener *, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;
	struct ws_xdg_decoration *xdg_decoration = calloc(1, sizeof(*xdg_decoration));
	if (!xdg_decoration) {
		return;
	}

	xdg_decoration->wlr_decoration = wlr_decoration;

	xdg_decoration->destroy.notify = xdg_decoration_handle_destroy;
	wl_signal_add(&wlr_decoration->events.destroy, &xdg_decoration->destroy);

	xdg_decoration->commit.notify = xdg_decoration_handle_commit;
	wl_signal_add(&wlr_decoration->toplevel->base->surface->events.commit,
		      &xdg_decoration->commit);

	xdg_decoration->request_mode.notify = xdg_decoration_handle_request_mode;
	wl_signal_add(&wlr_decoration->events.request_mode, &xdg_decoration->request_mode);
}

////////////////////////////////////////////////////////////////////////////////

struct ws_config_key {
	uint32_t modifiers;
	char key;
	char func[];
};

void if_key_client(int argc, char *argv[]) {
	char *pid_read = getenv(STR(WS_SERVER_PID));
	if (!pid_read) {
		return;
	}
	pid_t pid = atoi(pid_read);
	struct ws_config_key k = {0};
	int c;
	while ((c = getopt(argc, argv, "aslk:")) != -1) {
		switch (c) {
		case 'a':
			k.modifiers |= WLR_MODIFIER_ALT;
			break;
		case 's':
			k.modifiers |= WLR_MODIFIER_SHIFT;
			break;
		case 'l':
			k.modifiers |= WLR_MODIFIER_LOGO;
			break;
		case 'k':
			// TODO convert to xkb?
			k.key = *optarg;
			break;
		default:
			goto error;
		}
	}

	if (optind == argc) {
		goto error;
	}
	write(STDOUT_FILENO, &k, sizeof(k));

	for (; optind < argc; optind++) {
		write(STDOUT_FILENO, argv[optind], strlen(argv[optind]));
		write(STDOUT_FILENO, " ", 1);
	}
	kill(pid, SIGUSR1);
	exit(EXIT_SUCCESS);

error:
	kill(pid, SIGUSR2);
	exit(EXIT_FAILURE);
}

void print_usage(void) {
	const char usage[] = "wless [-hv] [-s command]\n";
	printf(usage);
}

void print_version(void) {
	// version from macro
	printf("0.0.1\n");
}

void cleanup(void) {
	if (server.wl_display)
		wl_display_destroy_clients(server.wl_display);
	/* wlr_scene_node_destroy(&server.scene->tree.node); */
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_output_layout_destroy(server.output_layout);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	if (server.wl_display)
		wl_display_destroy(server.wl_display);
}

void ws_signal_empty(int) {
	// empty, use for awake sleep
}

void ws_signal_panic(int) {
	// panic, terminal all
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	int ret = 0;

	if_key_client(argc, argv);

	enum wlr_log_importance log_level = WLR_INFO;
	char *startup_cmd = NULL;
	int c;
	while ((c = getopt(argc, argv, "ds:hv")) != -1) {
		switch (c) {
		case 'd':
			log_level = WLR_DEBUG;
			break;
		case 's':
			startup_cmd = optarg;
			break;
		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
		case 'v':
			print_version();
			exit(EXIT_SUCCESS);
		default:
			print_usage();
			exit(EXIT_FAILURE);
		}
	}
	if (optind != argc) {
		/* no position arguments! */
		print_usage();
		exit(EXIT_FAILURE);
	}

	wlr_log_init(log_level, NULL);

	CHECK(getenv("XDG_RUNTIME_DIR"), "XDG_RUNTIME_DIR");

	const char *ws_color_bg = env_or(STR(WS_COLOR_BG), "#292b38");
	color_hex2rgba(ws_color_bg, &color_bg);

	const char *ws_config = env_or(STR(WS_CONFIG), "/etc/wless/keys.rc");

	int piperw[2];
	pipe(piperw);
	pid_t pid = fork();
	if (pid == 0) {
		char pid_buf[20] = {0};
		snprintf(pid_buf, sizeof(pid_buf), "%d", getppid());
		setenv(STR(WS_SERVER_PID), pid_buf, 1);
		close(piperw[0]);		// client no read
		dup2(piperw[1], STDOUT_FILENO); // redirect stdout to pipe
		close(piperw[1]);		// close original pipe
		execl("/bin/sh", "sh", ws_config, NULL);
		// __builtin_unreachable();
	}
	CHECK(pid > 0, "fork()");
	close(piperw[1]);		  // server no write
	signal(SIGUSR1, ws_signal_empty); // awake to read
	signal(SIGUSR2, ws_signal_panic); // report an error
	signal(SIGCHLD, SIG_IGN);

	ssize_t nbytes = 0;
	char read_buf[1024] = {0};
	do {
		struct timespec tv = {.tv_nsec = 100000000}; // 0.1s
		int rt = nanosleep(&tv, NULL);
		nbytes = read(piperw[0], read_buf, 1024);
		// exit when timeout or read done
		if (nbytes == 0 || rt == 0) {
			break;
		}
		struct ws_config_key *k = calloc(1, nbytes);
		memcpy(k, read_buf, nbytes);
		// TODO add key to server.keys
		free(k);
	} while (nbytes > 0);
	close(piperw[0]);

	ret = atexit(cleanup);
	CHECK(ret == 0, "atexit cleanup");

	server.wl_display = wl_display_create();
	CHECK(server.wl_display, "wl_display_create");

	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	CHECK(server.backend, "wlr_backend");

	server.renderer = wlr_renderer_autocreate(server.backend);
	CHECK(server.renderer, "wlr_renderer");

	ret = wlr_renderer_init_wl_display(server.renderer, server.wl_display);
	CHECK(ret, "wlr_renderer_init_wl_display");

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	CHECK(server.allocator, "wlr_allocator_autocreate");

	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	server.output_layout = wlr_output_layout_create(server.wl_display);
	server.output_layout_change.notify = handle_output_layout_change;
	wl_signal_add(&server.output_layout->events.change, &server.output_layout_change);

	wl_list_init(&server.outputs);
	server.new_output.notify = handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.scene = wlr_scene_create();
	server.bg_scene = wlr_scene_rect_create(&server.scene->tree, 0, 0, color_bg);
	server.scene_output_layout =
		wlr_scene_attach_output_layout(server.scene, server.output_layout);

	server.output_manager_v1 = wlr_output_manager_v1_create(server.wl_display);
	server.output_manager_apply.notify = handle_output_manager_apply;
	wl_signal_add(&server.output_manager_v1->events.apply, &server.output_manager_apply);
	server.output_manager_test.notify = handle_output_manager_test;
	wl_signal_add(&server.output_manager_v1->events.test, &server.output_manager_test);

	wl_list_init(&server.toplevels);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_toplevel.notify = handle_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = handle_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(server.wl_display);
	server.xdg_toplevel_decoration.notify = handle_xdg_toplevel_decoration;
	wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration,
		      &server.xdg_toplevel_decoration);

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server.cursor_motion_relative.notify = server_cursor_motion_relative;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion_relative);

	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);

	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);

	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);

	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	wl_list_init(&server.keyboards);
	server.new_input.notify = server_handle_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");

	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);

	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	CHECK(socket, "wl_display_add_socket_auto");

	ret = wlr_backend_start(server.backend);
	CHECK(ret, "wlr_backend_start");

	setenv("WAYLAND_DISPLAY", socket, true);
	wlr_log(WLR_INFO, "WAYLAND_DISPLAY=%s", socket);
	if (startup_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
		}
	}
	wl_display_run(server.wl_display);

	exit(EXIT_SUCCESS);
}
