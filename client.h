#ifndef WLESS_CLIENT_H
#define WLESS_CLIENT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "output.h"
#include "wless.h"

struct ws_client {
	struct ws_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct ws_output *output;

	struct wl_list link; // ws_server.clients

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_fullscreen;

	char *lowercase_app_id; // jump-or-exec
	struct wl_listener set_app_id;
};

struct ws_client_popup {
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

const char *client_app_id(struct ws_client *client);
const char *client_title(struct ws_client *client);

struct ws_output *client_output(struct ws_client *client);

void client_raise(struct ws_client *client);
void client_focus(struct ws_client *client);

struct ws_client *client_zero(struct ws_server *server);
struct ws_client *client_now(struct ws_server *server);
struct ws_client *client_at(struct ws_server *server, double lx, double ly,
			    struct wlr_surface **out_surface, double *sx,
			    double *sy);

void client_position(struct ws_client *client, struct ws_output *output);

void handle_new_xdg_toplevel(struct wl_listener *, void *data);
void handle_new_xdg_popup(struct wl_listener *, void *data);
void handle_new_xdg_toplevel_decoration(struct wl_listener *, void *data);

#endif
