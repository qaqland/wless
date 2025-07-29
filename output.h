#ifndef WLESS_OUTPUT_H
#define WLESS_OUTPUT_H

#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct ws_output {
	struct ws_server *server;
	struct wlr_output *wlr_output; // wlr_output->data = output;
	struct wlr_scene_output *scene_output;
	struct wlr_box output_box;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;

	struct wl_list link; // ws_server.outputs
};

const char *output_name(struct ws_output *output);
struct ws_client *output_client(struct ws_output *output);
struct ws_output *output_now(struct ws_server *server);
void output_focus(struct ws_output *output);

void handle_new_output(struct wl_listener *listener, void *data);

void handle_output_layout_change(struct wl_listener *listener, void *data);

void handle_output_manager_test(struct wl_listener *listener, void *data);
void handle_output_manager_apply(struct wl_listener *listener, void *data);
#endif
