#include <assert.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/session.h>
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
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#define STR(name) #name

#define VERSION "0.0.1"
#define USAGE "wless -s "

#define CLEANMASK(mask) (mask & ~WLR_MODIFIER_CAPS)

struct ws_server {
	// base
	struct wl_display *wl_display;
	struct wl_event_loop *wl_event_loop;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_session *session;

	// scene
	struct wlr_scene *scene;
	struct wlr_scene_rect *scene_background;
	struct wlr_scene_output_layout *scene_output_layout;

	// xdg_toplevel
	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels; // ws_toplevel.link

	struct ws_toplevel *win_toplevel; // Win/Alt+Tab

	struct wl_listener xdg_toplevel_decoration;

	// seat
	struct wlr_seat *seat;
	struct wl_list pointers;
	struct wl_list keyboards;
	struct wl_list keyboard_groups;
	struct wl_listener new_input;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *xcursor_manager;
	struct wl_listener cursor_motion_relative;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;

	// output
	struct wlr_output_manager_v1 *output_manager_v1;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	struct wlr_output_layout *output_layout;
	struct wl_listener output_layout_change;

	struct wl_list outputs; // ws_output.link
	struct wl_listener new_output;

	struct ws_output *focused_output;
};

// global instance
struct ws_server s;

struct ws_output {
	struct wl_list link; // ws_server.outputs

	struct ws_toplevel *cur_toplevel;

	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct ws_toplevel {
	struct wl_list link; // ws_server.toplevels

	// struct ws_output *output;

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

struct ws_pointer {
	struct wl_list link;
	struct wlr_pointer *wlr_pointer;

	struct wl_listener destroy;
};

struct ws_keyboard {
	struct wl_list link;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct ws_key_bind {
	uint32_t modifiers;
	xkb_keysym_t keysym;
	void (*func)(void);
};

static void color_hex2rgba(const char *hex, float (*color)[4]) {
	unsigned short r, g, b, a;
	r = g = b = 0;
	a = 255;

	hex += (*hex == '#');

	sscanf(hex, "%2hx%2hx%2hx%2hx", &r, &g, &b, &a);
	(*color)[0] = r / 255.0f;
	(*color)[1] = g / 255.0f;
	(*color)[2] = b / 255.0f;
	(*color)[3] = a / 255.0f;
}

static struct ws_output *toplevel_visible_on(struct ws_toplevel *toplevel) {
	struct ws_output *output;
	wl_list_for_each (output, &s.outputs, link) {
		if (toplevel == output->cur_toplevel) {
			return output;
		}
	}
	return NULL;
}

static void focus_toplevel(struct ws_toplevel *toplevel) {
	s.focused_output = toplevel_visible_on(toplevel);

	if (!toplevel) {
		return;
	}
	struct wlr_seat *seat = s.seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;

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
	wl_list_insert(&s.toplevels, &toplevel->link);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (!keyboard) {
		return;
	}
	wlr_seat_keyboard_notify_enter(
		seat, toplevel->xdg_toplevel->base->surface, keyboard->keycodes,
		keyboard->num_keycodes, &keyboard->modifiers);
}

void xdg_toplevel_position(struct ws_output *output) {
	struct ws_toplevel *toplevel = output->cur_toplevel;
	if (!toplevel) {
		return;
	}
	if (!toplevel->scene_tree) {
		return;
	}
	struct wlr_box output_box;
	wlr_output_layout_get_box(s.output_layout, output->wlr_output,
				  &output_box);

	int new_x, new_y;

	if (toplevel->xdg_toplevel->parent) {
		// switch to new in wlroots-0.19
		// https://gitlab.freedesktop.org/wlroots/wlroots/-/merge_requests/4788
		struct wlr_xdg_surface *surface = toplevel->xdg_toplevel->base;
		struct wlr_box box;
		wlr_xdg_surface_get_geometry(surface, &box);
		new_x = (output_box.width - box.width) / 2 + output_box.x;
		new_y = (output_box.height - box.height) / 2 + output_box.y;
	} else {
		// normally, a toplevel can cover the whole output
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
					  output_box.width, output_box.height);
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
		new_x = output_box.x;
		new_y = output_box.y;
	}
	wlr_scene_node_set_position(&toplevel->scene_tree->node, new_x, new_y);
}

void xdg_toplevel_position_all() {
	struct ws_output *output;
	wl_list_for_each (output, &s.outputs, link) {
		xdg_toplevel_position(output);
	}

	// set background
	struct wlr_box output_box;
	wlr_output_layout_get_box(s.output_layout, NULL, &output_box);
	wlr_scene_rect_set_size(s.scene_background, output_box.width,
				output_box.height);
	wlr_scene_node_set_position(&s.scene_background->node, output_box.x,
				    output_box.y);
	wlr_scene_node_lower_to_bottom(&s.scene_background->node);
}

void update_seat_caps(void) {
	// TODO call it when device destroy
	uint32_t caps = 0;

	// TODO keyboard_groups vs keyboards
	if (!wl_list_empty(&s.keyboard_groups)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	if (!wl_list_empty(&s.pointers)) {
		caps |= WL_SEAT_CAPABILITY_POINTER;
	}
	wlr_seat_set_capabilities(s.seat, caps);

	// lazy loading cursor image, set in process_cursor_motion
	// handle_cursor_motion_absolute or handle_cursor_motion_relative
	wlr_cursor_unset_image(s.cursor);
}

////////////////////////////////////////////////////////////////////////////////

void keyboard_handle_modifiers(struct wl_listener *listener, void *) {
	// key first, then modifiers
	struct ws_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(s.seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(s.seat,
					   &keyboard->wlr_keyboard->modifiers);
}

static void key_focus_start(bool same_output, bool next) {
	assert(s.focused_output);

	if (wl_list_length(&s.toplevels) < 2) {
		return;
	}
	struct ws_toplevel *tmp_toplevel =
		s.win_toplevel
			? s.win_toplevel
			: wl_container_of(s.toplevels.next, tmp_toplevel, link);
	struct ws_output *output;

	while (true) {
		struct wl_list *list = next ? tmp_toplevel->link.next
					    : tmp_toplevel->link.prev;
		if (list == &s.toplevels) {
			// skip head list
			list = next ? list->next : list->prev;
		}
		tmp_toplevel = wl_container_of(list, tmp_toplevel, link);
		output = toplevel_visible_on(tmp_toplevel);
		if (!output) {
			output = s.focused_output;
			output->cur_toplevel = tmp_toplevel;
			break; // catch "clean" toplevel
		}
		if (same_output && output != s.focused_output) {
			continue; // skip "other" toplevel
		}
		break;
	}

	if (s.win_toplevel == tmp_toplevel) {
		return;
	}

	s.win_toplevel = tmp_toplevel;
	wlr_scene_node_raise_to_top(&s.win_toplevel->scene_tree->node);
	xdg_toplevel_position(output);
}

static void key_focus_done() {
	if (!s.win_toplevel) {
		return;
	}
	focus_toplevel(s.win_toplevel);
	s.win_toplevel = NULL;
}

static void key_focus_same_next() {
	// same_output, next
	key_focus_start(true, true);
}

static void key_focus_same_previous() {
	// same_output, previous
	key_focus_start(true, false);
}

static void key_focus_next() {
	// next
	key_focus_start(false, true);
}

static void key_focus_previous() {
	// previous
	key_focus_start(false, false);
}

static void key_close_window() {
	struct ws_output *output = s.focused_output;
	if (!output) {
		return;
	}
	struct ws_toplevel *toplevel = output->cur_toplevel;
	if (!toplevel) {
		return;
	}
	output->cur_toplevel = NULL;
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);

	key_focus_same_next();
	key_focus_done();
}

static void key_quit() {
	//
	wlr_log(WLR_INFO, "quit");
	wl_display_terminate(s.wl_display);
}

static void key_spawn_client(const char *exe) {
	if (!exe) {
		return;
	}
	if (fork() == 0) {
		execlp(exe, exe, NULL);
	}
}

static void key_spawn_foot() {
	wlr_log(WLR_INFO, "spawn foot");
	const char *foot = "foot";
	key_spawn_client(foot);
}

// TODO:
// - tidy function name
// - add focus-output-next
// - add focus-output-next-move

#define KEY_FUNC_LIST                                                          \
	X("focus-same-next", key_focus_same_next)                              \
	X("focus-same-previous", key_focus_same_previous)

static void (*name2func(const char *name))(void) {
	if (!name) {
		printf(""
#define X(STR, FUNC) STR "\n"
		       KEY_FUNC_LIST
#undef X
		);
		return NULL;
	}
#define X(STR, FUNC)                                                           \
	if (strcmp(STR, name) == 0) {                                          \
		return FUNC;                                                   \
	}
	KEY_FUNC_LIST
#undef X
	return NULL;
}

const struct ws_key_bind keys[] = {
	{
		WLR_MODIFIER_ALT,
		XKB_KEY_Tab,
		key_focus_next,
	},
	{
		WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT,
		XKB_KEY_Tab,
		key_focus_previous,
	},
	{
		WLR_MODIFIER_LOGO,
		XKB_KEY_Tab,
		key_focus_same_next,
	},
	{
		WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT,
		XKB_KEY_Tab,
		key_focus_same_previous,
	},
	{
		WLR_MODIFIER_LOGO,
		XKB_KEY_Return,
		key_spawn_foot,
	},
	{
		WLR_MODIFIER_LOGO,
		XKB_KEY_w,
		key_close_window,
	},
	{
		WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT,
		XKB_KEY_Escape,
		key_quit,
	},
};

static bool key_bindings(uint32_t modifiers, xkb_keysym_t keysym) {
	// xkb_keysym_from_name() can used to build config

	// https://github.com/search?q=XKB_KEY_ISO_Left_Tab&type=code
	if (keysym == XKB_KEY_ISO_Left_Tab) {
		keysym = XKB_KEY_Tab;
	}
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		const struct ws_key_bind *key = &keys[i];
		if (CLEANMASK(key->modifiers) != CLEANMASK(modifiers) ||
		    key->keysym != keysym) {
			continue;
		}
		key->func();
		return true;
	}

	return false;
}

#define VT_LIST X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12)

static bool key_change_vt(uint32_t modifiers, xkb_keysym_t keysym) {
	if (modifiers != (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL)) {
		return false;
	}
	int vt = 0;
	switch (keysym) {
#define X(NUM)                                                                 \
	case XKB_KEY_XF86Switch_VT_##NUM:                                      \
		vt = NUM;                                                      \
		break;
		VT_LIST
#undef X
	default:
		return false;
	}
	wlr_session_change_vt(s.session, vt);
	return true;
}

void keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct ws_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = s.seat;

	xkb_keycode_t keycode = event->keycode + 8;
	xkb_keysym_t keysym = xkb_state_key_get_one_sym(
		keyboard->wlr_keyboard->xkb_state, keycode);
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	bool handled = false;
	switch (event->state) {
	case WL_KEYBOARD_KEY_STATE_PRESSED:
		handled = key_bindings(modifiers, keysym) ||
			  key_change_vt(modifiers, keysym);
		break;
	case WL_KEYBOARD_KEY_STATE_RELEASED:
		if (keysym == XKB_KEY_Alt_L || keysym == XKB_KEY_Super_L) {
			key_focus_done();
		}
		break;
	}

	if (handled) {
		return;
	}

	// pass unhandled key to client
	wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
				     event->state);
}

void keyboard_destroy(struct wl_listener *listener, void *) {
	/* This event is raised by the keyboard base wlr_input_device to
	 * signal the destruction of the wlr_keyboard. It will no longer
	 * receive events and should be destroyed.
	 */
	struct ws_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

void seat_request_cursor(struct wl_listener *, void *data) {
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		s.seat->pointer_state.focused_client;
	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(s.cursor, event->surface,
				       event->hotspot_x, event->hotspot_y);
	}
}

void seat_request_set_selection(struct wl_listener *, void *data) {
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(s.seat, event->source, event->serial);
}

////////////////////////////////////////////////////////////////////////////////

struct ws_toplevel *desktop_toplevel_at(double lx, double ly,
					struct wlr_surface **out_surface,
					double *sx, double *sy) {
	struct wlr_scene_node *node =
		wlr_scene_node_at(&s.scene->tree.node, lx, ly, sx, sy);
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

	*out_surface = scene_surface->surface;

	// wlr_scene_node.data is set in handle_xdg_toplevel_map
	struct wlr_scene_tree *tree = node->parent;
	while (tree && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree->node.data;
}

void handle_cursor_button(struct wl_listener *, void *data) {
	struct wlr_pointer_button_event *event = data;
	if (event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
		wlr_log(WLR_DEBUG, "event not pressed?");
	}
	// TODO move focus
	wlr_seat_pointer_notify_button(s.seat, event->time_msec, event->button,
				       event->state);
}

void handle_cursor_axis(struct wl_listener *, void *data) {
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(s.seat, event->time_msec,
				     event->orientation, event->delta,
				     event->delta_discrete, event->source,
				     event->relative_direction);
}

static void process_cursor_motion(uint32_t time) {
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct ws_toplevel *toplevel = desktop_toplevel_at(
		s.cursor->x, s.cursor->y, &surface, &sx, &sy);
	if (toplevel) {
		focus_toplevel(toplevel);
	} else {
		wlr_cursor_set_xcursor(s.cursor, s.xcursor_manager, "default");
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(s.seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(s.seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(s.seat);
	}
}

void handle_cursor_motion_relative(struct wl_listener *, void *data) {
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(s.cursor, &event->pointer->base, event->delta_x,
			event->delta_y);
	process_cursor_motion(event->time_msec);
}

void handle_cursor_motion_absolute(struct wl_listener *, void *data) {
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(s.cursor, &event->pointer->base, event->x,
				 event->y);
	process_cursor_motion(event->time_msec);
}

void handle_cursor_frame(struct wl_listener *, void *) {
	wlr_seat_pointer_notify_frame(s.seat);
}

// device
void handle_new_keyboard(struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard =
		wlr_keyboard_from_input_device(device);

	struct ws_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(
		context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

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

	wlr_seat_set_keyboard(s.seat, keyboard->wlr_keyboard);

	wl_list_insert(&s.keyboards, &keyboard->link);
}

void handle_new_pointer(struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(s.cursor, device);

	struct ws_pointer *pointer = calloc(1, sizeof(*pointer));
	struct wlr_pointer *wlr_pointer = wlr_pointer_from_input_device(device);
	pointer->wlr_pointer = wlr_pointer;

	wl_list_insert(&s.pointers, &pointer->link);

	// pointer->destroy.notify = handle_pointer_destroy;
	// wl_signal_add(&device->events.destroy, &pointer->destroy);
}

void handle_new_input(struct wl_listener *, void *data) {
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		handle_new_keyboard(device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		handle_new_pointer(device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_SWITCH:
	case WLR_INPUT_DEVICE_TABLET:
	case WLR_INPUT_DEVICE_TABLET_PAD:
		wlr_log(WLR_ERROR, "not implemented");
		break;
	}
	update_seat_caps();
}

////////////////////////////////////////////////////////////////////////////////

void update_output_manager_config(void) {
	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();
	struct ws_output *output;
	wl_list_for_each (output, &s.outputs, link) {
		struct wlr_output_configuration_head_v1 *config_head =
			wlr_output_configuration_head_v1_create(
				config, output->wlr_output);
		struct wlr_box output_box;
		wlr_output_layout_get_box(s.output_layout, output->wlr_output,
					  &output_box);
		config_head->state.enabled = !wlr_box_empty(&output_box);
		config_head->state.x = output_box.x;
		config_head->state.y = output_box.y;
	}
	wlr_output_manager_v1_set_configuration(s.output_manager_v1, config);
}

// ws_server.output_layout_change (server.output_layout->events.change)
void handle_output_layout_change(struct wl_listener *, void *) {
	xdg_toplevel_position_all();
	update_output_manager_config();
}

////////////////////////////////////////////////////////////////////////////////

bool output_apply_config_head(struct ws_output *output,
			      struct wlr_output_configuration_head_v1 *head,
			      bool test_only) {
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
		wlr_output_layout_remove(s.output_layout, output->wlr_output);
		goto finish;
	}

	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add(s.output_layout, output->wlr_output,
				      head->state.x, head->state.y);
	wlr_scene_output_layout_add_output(s.scene_output_layout, l_output,
					   output->scene_output);
finish:
	wlr_output_state_finish(&state);
	return is_ok;
}

void output_manager_apply_config(struct wlr_output_configuration_v1 *config,
				 bool test_only) {
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

// ws_server.output_manager_apply
// (server.output_manager_v1->events.apply)
void handle_output_manager_apply(struct wl_listener *, void *data) {
	struct wlr_output_configuration_v1 *config = data;
	output_manager_apply_config(config, false);
	xdg_toplevel_position_all();
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
	struct ws_output *output =
		wl_container_of(listener, output, request_state);
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
	wlr_output_layout_remove(s.output_layout, output->wlr_output);
	free(output);

	// if (wl_list_empty(&server.outputs) && was_nested)
	if (wl_list_empty(&s.outputs)) {
		exit(EXIT_SUCCESS);
	}
}

// ws_server.new_output (server.backend->events.new_output)
void handle_new_output(struct wl_listener *, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_output_init_render(wlr_output, s.allocator, s.renderer);

	// output state (mode)
	struct wlr_output_state state;
	wlr_output_state_init(&state);

	// switch on first, load config later
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode) {
		// FIXME what if output doesn't support mode?
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	// ws_output
	struct ws_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	wlr_output->data = output;

	wl_list_insert(&s.outputs, &output->link);

	// move focus to the newest output
	s.focused_output = output;

	output->frame.notify = handle_output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->request_state.notify = handle_output_request_state;
	wl_signal_add(&wlr_output->events.request_state,
		      &output->request_state);

	output->destroy.notify = handle_output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	// invoke wlr_output_create_global() internally
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(s.output_layout, wlr_output);

	output->scene_output = wlr_scene_output_create(s.scene, wlr_output);
	wlr_scene_output_layout_add_output(s.scene_output_layout, l_output,
					   output->scene_output);
}

////////////////////////////////////////////////////////////////////////////////

// ws_toplevel.map (xdg_toplevel->base->surface->events.map)
void handle_xdg_toplevel_map(struct wl_listener *listener, void *) {
	struct ws_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	toplevel->scene_tree = wlr_scene_xdg_surface_create(
		&s.scene->tree, toplevel->xdg_toplevel->base);

	// wlr_scene_node.data is used in desktop_toplevel_at
	toplevel->scene_tree->node.data = toplevel;

	// wlr_xdg_surface.data is used in handle_new_xdg_popup
	toplevel->xdg_toplevel->base->data = toplevel->scene_tree;

	// wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

	wl_list_insert(&s.toplevels, &toplevel->link);

	s.focused_output->cur_toplevel = toplevel;
	xdg_toplevel_position(s.focused_output);
	focus_toplevel(toplevel);
}

// ws_toplevel.unmap (xdg_toplevel->base->surface->events.unmap)
void handle_xdg_toplevel_unmap(struct wl_listener *listener, void *) {
	struct ws_toplevel *toplevel =
		wl_container_of(listener, toplevel, unmap);
	wl_list_remove(&toplevel->link);

	if (s.win_toplevel == toplevel) {
		s.win_toplevel = NULL;
		key_focus_same_next();
	}

	struct ws_output *output;
	wl_list_for_each (output, &s.outputs, link) {
		if (output->cur_toplevel == toplevel) {
			output->cur_toplevel = NULL;
			key_focus_same_next();
			key_focus_done();
		}
	}
}

// ws_toplevel.commit (xdg_toplevel->base->surface->events.commit)
void handle_xdg_toplevel_commit(struct wl_listener *listener, void *) {
	// commit -> map -> commit -> commit
	struct ws_toplevel *toplevel =
		wl_container_of(listener, toplevel, commit);
	if (toplevel->xdg_toplevel->base->initial_commit) {
		// TODO more function
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}
}

// ws_toplevel.destroy (xdg_toplevel->events.destroy)
void handle_xdg_toplevel_destroy(struct wl_listener *listener, void *) {
	struct ws_toplevel *toplevel =
		wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	free(toplevel);
}

// ws_toplevel.request_fullscreen (xdg_toplevel->events.request_fullscreen)
void handle_xdg_toplevel_request_fullscreen(struct wl_listener *listener,
					    void *) {
	struct ws_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	if (!toplevel->xdg_toplevel->base->initial_commit) {
		return;
	}
	// 鼠标或者什么操作应该会先激活窗口
	xdg_toplevel_position(s.focused_output);
	wlr_xdg_toplevel_set_fullscreen(
		toplevel->xdg_toplevel,
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

	toplevel->map.notify = handle_xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);

	toplevel->unmap.notify = handle_xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap,
		      &toplevel->unmap);

	toplevel->commit.notify = handle_xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit,
		      &toplevel->commit);

	toplevel->destroy.notify = handle_xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	toplevel->request_fullscreen.notify =
		handle_xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen,
		      &toplevel->request_fullscreen);
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

	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);

	// wlr_xdg_surface.data is set in handle_xdg_toplevel_map
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data =
		wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

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

// ws_xdg_decoration.commit
// (wlr_decoration->toplevel->base->surface->events.commit)
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

// ws_server.xdg_toplevel_decoration
// (xdg_decoration_manager->events.new_toplevel_decoration)
void handle_xdg_toplevel_decoration(struct wl_listener *, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;
	struct ws_xdg_decoration *xdg_decoration =
		calloc(1, sizeof(*xdg_decoration));
	if (!xdg_decoration) {
		return;
	}

	xdg_decoration->wlr_decoration = wlr_decoration;

	xdg_decoration->destroy.notify = xdg_decoration_handle_destroy;
	wl_signal_add(&wlr_decoration->events.destroy,
		      &xdg_decoration->destroy);

	xdg_decoration->commit.notify = xdg_decoration_handle_commit;
	wl_signal_add(&wlr_decoration->toplevel->base->surface->events.commit,
		      &xdg_decoration->commit);

	xdg_decoration->request_mode.notify =
		xdg_decoration_handle_request_mode;
	wl_signal_add(&wlr_decoration->events.request_mode,
		      &xdg_decoration->request_mode);
}

////////////////////////////////////////////////////////////////////////////////

// void cleanup(void) {
//   if (s.wl_display)
//     wl_display_destroy_clients(s.wl_display);
//   // wlr_scene_node_destroy(&server.scene->tree.node);
//   wlr_xcursor_manager_destroy(s.xcursor_manager);
//   wlr_output_layout_destroy(s.output_layout);
//   wlr_allocator_destroy(s.allocator);
//   wlr_renderer_destroy(s.renderer);
//   wlr_backend_destroy(s.backend);
//   if (s.wl_display)
//     wl_display_destroy(s.wl_display);
// }

int main(int argc, char *argv[]) {
	bool is_ok = true;

	enum wlr_log_importance log_level = WLR_INFO;
	char *start_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "ds:hv")) != -1) {
		switch (c) {
		case 'd':
			log_level = WLR_DEBUG;
			break;
		case 's':
			start_cmd = optarg;
			break;
		case 'h':
			printf(VERSION);
			goto quit;
		case 'v':
			printf(VERSION);
			goto quit;
		default:
			printf(VERSION);
			goto error_quit;
		}
	}

	wlr_log_init(log_level, NULL);

	// no position arguments!
	if (optind != argc) {
		fprintf(stdout, USAGE);
		goto error_quit;
	}

	const char *XDG_RUNTIME_DIR = getenv(STR(XDG_RUNTIME_DIR));
	if (!XDG_RUNTIME_DIR) {
		wlr_log(WLR_ERROR, "XDG_RUNTIME_DIR is NULL");
		goto error_quit;
	} else {
		wlr_log(WLR_INFO, "XDG_RUNTIME_DIR=%s", XDG_RUNTIME_DIR);
	}

	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGCHLD, &sa, NULL);

	const char *color_str = "#242424";
	float color_background[4];
	color_hex2rgba(color_str, &color_background);

	s.wl_display = wl_display_create();
	if (!s.wl_display) {
		wlr_log(WLR_ERROR, "Failed to create wl_display");
		goto error_quit;
	}

	s.wl_event_loop = wl_display_get_event_loop(s.wl_display);

	s.backend = wlr_backend_autocreate(s.wl_event_loop, &s.session);
	if (!s.backend) {
		wlr_log(WLR_ERROR, "Failed to create wlr_backend");
		goto error_quit;
	}

	s.renderer = wlr_renderer_autocreate(s.backend);
	if (!s.renderer) {
		wlr_log(WLR_ERROR, "Failed to create wlr_renderer");
		goto error_quit;
	}

	is_ok = wlr_renderer_init_wl_display(s.renderer, s.wl_display);
	if (!is_ok) {
		wlr_log(WLR_ERROR, "Failed to init display");
		goto error_quit;
	}

	s.allocator = wlr_allocator_autocreate(s.backend, s.renderer);
	if (!s.allocator) {
		wlr_log(WLR_ERROR, "Failed to create wlr_allocator");
		goto error_quit;
	}

	// who will call these destroy event?
	// wl_display_add_destroy_listener()

	wlr_compositor_create(s.wl_display, 6, s.renderer);

	wlr_subcompositor_create(s.wl_display);

	wlr_data_device_manager_create(s.wl_display);

	// new output
	wl_list_init(&s.outputs);
	s.new_output.notify = handle_new_output;
	wl_signal_add(&s.backend->events.new_output, &s.new_output);

	// output layout change
	s.output_layout = wlr_output_layout_create(s.wl_display);
	s.output_layout_change.notify = handle_output_layout_change;
	wl_signal_add(&s.output_layout->events.change, &s.output_layout_change);

	// scene graph
	s.scene = wlr_scene_create();
	s.scene_background =
		wlr_scene_rect_create(&s.scene->tree, 0, 0, color_background);
	s.scene_output_layout =
		wlr_scene_attach_output_layout(s.scene, s.output_layout);

	wlr_xdg_output_manager_v1_create(s.wl_display, s.output_layout);
	wlr_viewporter_create(s.wl_display);
	wlr_presentation_create(s.wl_display, s.backend);

	// wlr-randr
	s.output_manager_v1 = wlr_output_manager_v1_create(s.wl_display);
	s.output_manager_apply.notify = handle_output_manager_apply;
	wl_signal_add(&s.output_manager_v1->events.apply,
		      &s.output_manager_apply);
	s.output_manager_test.notify = handle_output_manager_test;
	wl_signal_add(&s.output_manager_v1->events.test,
		      &s.output_manager_test);

	wl_list_init(&s.toplevels);
	s.xdg_shell = wlr_xdg_shell_create(s.wl_display, 3);
	s.new_xdg_toplevel.notify = handle_new_xdg_toplevel;
	wl_signal_add(&s.xdg_shell->events.new_toplevel, &s.new_xdg_toplevel);
	s.new_xdg_popup.notify = handle_new_xdg_popup;
	wl_signal_add(&s.xdg_shell->events.new_popup, &s.new_xdg_popup);

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(s.wl_display);
	s.xdg_toplevel_decoration.notify = handle_xdg_toplevel_decoration;
	wl_signal_add(&xdg_decoration_manager->events.new_toplevel_decoration,
		      &s.xdg_toplevel_decoration);

	// pointer
	s.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(s.cursor, s.output_layout);

	s.xcursor_manager = wlr_xcursor_manager_create(NULL, 24);

	s.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
	wl_signal_add(&s.cursor->events.motion_absolute,
		      &s.cursor_motion_absolute);

	s.cursor_motion_relative.notify = handle_cursor_motion_relative;
	wl_signal_add(&s.cursor->events.motion, &s.cursor_motion_relative);

	s.cursor_button.notify = handle_cursor_button;
	wl_signal_add(&s.cursor->events.button, &s.cursor_button);

	s.cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&s.cursor->events.axis, &s.cursor_axis);

	s.cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&s.cursor->events.frame, &s.cursor_frame);

	wl_list_init(&s.keyboards);
	wl_list_init(&s.pointers);

	s.new_input.notify = handle_new_input;
	wl_signal_add(&s.backend->events.new_input, &s.new_input);

	s.seat = wlr_seat_create(s.wl_display, "seat0");

	s.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&s.seat->events.request_set_cursor, &s.request_cursor);

	s.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&s.seat->events.request_set_selection,
		      &s.request_set_selection);

	const char *socket = wl_display_add_socket_auto(s.wl_display);
	if (!socket) {
		wlr_log(WLR_ERROR, "Failed to add wl_display socket");
		goto error_quit;
	}

	is_ok = wlr_backend_start(s.backend);
	if (!is_ok) {
		wlr_log(WLR_ERROR, "Failed to start wlr_backend");
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	wlr_log(WLR_INFO, "WAYLAND_DISPLAY=%s", socket);
	if (start_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", start_cmd, NULL);
		}
	}
	wl_display_run(s.wl_display);
quit:
	exit(EXIT_SUCCESS);
error_quit:
	exit(EXIT_FAILURE);
}
