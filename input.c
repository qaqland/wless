#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "action.h"
#include "client.h"
#include "input.h"
// #include "output.h"
#include "server.h"

struct ws_client *client_at(struct ws_server *server, double lx, double ly,
			    struct wlr_surface **out_surface, double *sx,
			    double *sy) {
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);

	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer =
		wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	if (out_surface) {
		*out_surface = scene_surface->surface;
	}

	// wlr_scene_node.data is set in handle_xdg_toplevel_map
	struct wlr_scene_tree *tree = node->parent;
	while (tree && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree->node.data;
}

void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct wlr_pointer_button_event *event = data;
	struct ws_server *server =
		wl_container_of(listener, server, cursor_button);

	if (event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
		wlr_log(WLR_DEBUG, "event not pressed?");
	}
	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
				       event->button, event->state);
}

void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct wlr_pointer_axis_event *event = data;
	struct ws_server *server =
		wl_container_of(listener, server, cursor_axis);

	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
				     event->orientation, event->delta,
				     event->delta_discrete, event->source,
				     event->relative_direction);
}

static void process_cursor_motion(struct ws_server *server, uint32_t time) {
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct ws_client *client =
		client_at(server, server->cursor->x, server->cursor->y,
			  &surface, &sx, &sy);
	if (client) {
		client_focus(client);
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->xcursor_manager,
				       "default");
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

void handle_cursor_motion_relative(struct wl_listener *listener, void *data) {
	struct wlr_pointer_motion_event *event = data;
	struct ws_server *server =
		wl_container_of(listener, server, cursor_motion_relative);

	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
			event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct wlr_pointer_motion_absolute_event *event = data;
	struct ws_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);

	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
				 event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

void handle_cursor_frame(struct wl_listener *listener, void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, cursor_frame);
	struct wlr_cursor *cursor = data;
	(void) cursor; // TODO

	// FIXME seat? cursor?
	wlr_seat_pointer_notify_frame(server->seat);
}

static void update_seat_caps(struct ws_server *server) {
	uint32_t caps = 0;

	// TODO keyboard_groups vs keyboards
	if (wl_list_empty(&server->keyboards)) {
		// TODO wlr_log
	} else {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}

	if (wl_list_empty(&server->pointers)) {
		wlr_cursor_unset_image(server->cursor);
	} else {
		caps |= WL_SEAT_CAPABILITY_POINTER;
	}

	wlr_seat_set_capabilities(server->seat, caps);

	// lazy load cursor image, set it in process_cursor_motion
}

static void keyboard_handle_modifiers(struct wl_listener *listener,
				      void *data) {
	// key first, then modifiers
	struct ws_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	assert(keyboard->wlr_keyboard == data);

	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
					   &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct ws_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;

	struct wlr_seat *seat = keyboard->server->seat;

	xkb_keycode_t keycode = event->keycode + 8;
	xkb_keysym_t keysym = xkb_state_key_get_one_sym(
		keyboard->wlr_keyboard->xkb_state, keycode);
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	bool handled = false;
	switch (event->state) {
	case WL_KEYBOARD_KEY_STATE_PRESSED:
		handled = action_main(keyboard->server, modifiers, keysym);
		break;
	case WL_KEYBOARD_KEY_STATE_RELEASED:
		if (keysym == XKB_KEY_Alt_L || keysym == XKB_KEY_Super_L) {
			wlr_log(WLR_DEBUG, "[key] RELEASE: Alt_L / Super_L");
			// for alt+tab and super+tab
			action_focus_done(keyboard->server);
		}
		break;
	case WL_KEYBOARD_KEY_STATE_REPEATED:
		// TODO
		break;
	}

	if (handled) {
		static char name_buf[64];
		xkb_keysym_get_name(keysym, name_buf, 64);
		wlr_log(WLR_DEBUG, "[key] HANDLED: %s", name_buf);
		return;
	}

	// pass unhandled key to client
	wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
				     event->state);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	struct ws_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	struct wlr_keyboard *wlr_keyboard =
		wlr_keyboard_from_input_device(data);
	assert(keyboard->wlr_keyboard == wlr_keyboard);

	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);

	update_seat_caps(keyboard->server);
	free(keyboard);
}

static void input_new_keyboard(struct ws_server *server,
			       struct wlr_input_device *device) {
	assert(server->magic == 6);

	struct ws_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (!keyboard) {
		return;
	}
	keyboard->server = server;

	struct wlr_keyboard *wlr_keyboard =
		wlr_keyboard_from_input_device(device);
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(
		context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	wl_list_insert(&server->keyboards, &keyboard->link);

	// emit: wlr_keyboard_notify_modifiers()
	// emit: wlr_keyboard_notify_key()
	// data: struct wlr_keyboard *
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

	// emit: wlr_keyboard_notify_key()
	// data: struct wlr_keyboard_key_event *
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

	// emit: wlr_input_device_finish()
	// data: struct wlr_input_device *
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);
}

void pointer_handle_destroy(struct wl_listener *listener, void *data) {
	struct ws_pointer *pointer =
		wl_container_of(listener, pointer, destroy);
	struct wlr_pointer *wlr_pointer = wlr_pointer_from_input_device(data);
	assert(wlr_pointer == pointer->wlr_pointer);

	wl_list_remove(&pointer->destroy.link);
	wl_list_remove(&pointer->link);

	update_seat_caps(pointer->server);
	free(pointer);
}

static void input_new_pointer(struct ws_server *server,
			      struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);

	struct ws_pointer *pointer = calloc(1, sizeof(*pointer));
	if (!pointer) {
		return;
	}
	pointer->server = server;

	struct wlr_pointer *wlr_pointer = wlr_pointer_from_input_device(device);
	pointer->wlr_pointer = wlr_pointer;

	wl_list_insert(&server->pointers, &pointer->link);

	// emit: wlr_input_device_finish()
	// data: struct wlr_input_device *
	pointer->destroy.notify = pointer_handle_destroy;
	wl_signal_add(&device->events.destroy, &pointer->destroy);
}

void handle_new_input(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct ws_server *server = wl_container_of(listener, server, new_input);

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		input_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		input_new_pointer(server, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_SWITCH:
	case WLR_INPUT_DEVICE_TABLET:
	case WLR_INPUT_DEVICE_TABLET_PAD:
		wlr_log(WLR_ERROR, "[input] not implemented");
		break;
	}
	update_seat_caps(server);
}

void handle_request_cursor(struct wl_listener *listener, void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;

	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
				       event->hotspot_x, event->hotspot_y);
	}
}

void handle_request_set_selection(struct wl_listener *listener, void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, request_set_selection);

	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}
