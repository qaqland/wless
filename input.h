#ifndef WLESS_INPUT_H
#define WLESS_INPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>

struct ws_pointer {
	struct ws_server *server;

	struct wl_list link;
	struct wlr_pointer *wlr_pointer;

	struct wl_listener destroy;
};

struct ws_keyboard {
	struct ws_server *server;

	struct wl_list link;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct ws_client *client_at(struct ws_server *server, double lx, double ly,
			    struct wlr_surface **out_surface, double *sx,
			    double *sy);

void handle_cursor_motion_absolute(struct wl_listener *listener, void *data);
void handle_cursor_motion_relative(struct wl_listener *listener, void *data);
void handle_cursor_button(struct wl_listener *listener, void *data);
void handle_cursor_axis(struct wl_listener *listener, void *data);
void handle_cursor_frame(struct wl_listener *listener, void *data);

void handle_new_input(struct wl_listener *listener, void *data);

void handle_request_cursor(struct wl_listener *listener, void *data);
void handle_request_set_selection(struct wl_listener *listener, void *data);

#endif
