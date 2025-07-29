#ifndef WLESS_SERVER_H
#define WLESS_SERVER_H

#include <wayland-server-core.h>

struct ws_server {
	// config
	struct ws_config *config;

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
	struct wl_list clients; // ws_client.link

	// TODO
	// struct ws_client *cur_client;
	// struct ws_client *win_client; // Win/Alt+Tab
	// struct wl_list *win_link;     // Win/Alt+Tab
	// struct ws_output *cur_output; // use output_current instead

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;

	struct wl_listener xdg_toplevel_decoration;

	// seat
	struct wlr_seat *seat;
	struct wl_list pointers;
	struct wl_list keyboards;
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

	// magic number
	int magic;
};

#endif
