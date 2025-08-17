#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "client.h"
#include "output.h"
#include "wless.h"

const char *client_app_id(struct ws_client *client) {
	if (!client) {
		return "NULL";
	}
	const char *app_id = client->xdg_toplevel->app_id;
	return app_id ? app_id : "EMPTY";
}

const char *client_title(struct ws_client *client) {
	if (!client) {
		return "NULL";
	}
	const char *title = client->xdg_toplevel->title;
	return title ? title : "EMPTY";
}

struct ws_client *client_zero(struct ws_server *server) {
	assert(server->magic == 6);

	if (wl_list_empty(&server->clients)) {
		return NULL;
	}
	struct ws_client *client =
		wl_container_of(server->clients.next, client, link);
	wlr_log(WLR_DEBUG, "client_zero: %s", client_title(client));
	return client;
}

// when we have multiple outputs, server->clients.next might be located on other
// output. therefore, we define client_zero and client_now to distinguish
// between them.
struct ws_client *client_now(struct ws_server *server) {
	assert(server->magic == 6);

	struct ws_client *client = NULL;
	struct ws_output *output = output_now(server);
	if (output) {
		client = output_client(output);
	}
	wlr_log(WLR_DEBUG, "client_now: %s", client_title(client));
	return client;
}

static struct wlr_surface *client_surface(struct ws_client *client) {
	return client->xdg_toplevel->base->surface;
}

static struct ws_client *client_from_surface(struct wlr_surface *surface) {
	if (!surface) {
		return NULL;
	}

	struct wlr_surface *root_surface =
		wlr_surface_get_root_surface(surface);
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(root_surface);

	if (!xdg_surface) {
		return NULL;
	}

	switch (xdg_surface->role) {
	case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
		struct wlr_scene_tree *tree = xdg_surface->data;
		return tree->node.data;
	case WLR_XDG_SURFACE_ROLE_POPUP:
		if (!xdg_surface->popup) {
			return NULL;
		}
		if (!xdg_surface->popup->parent) {
			return NULL;
		}
		xdg_surface = wlr_xdg_surface_try_from_wlr_surface(
			xdg_surface->popup->parent);
		assert(xdg_surface); // TODO
		return client_from_surface(xdg_surface->popup->parent);
	case WLR_XDG_SURFACE_ROLE_NONE:
		return NULL;
	}

	// TODO
	return NULL;
}

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

void client_raise(struct ws_client *client) {
	if (!client) {
		return;
	}
	assert(client->scene_tree);
	assert(client->server->magic == 6);

	wlr_scene_node_raise_to_top(&client->scene_tree->node);
}

void client_blur(struct ws_client *client) {
	if (!client) {
		return;
	}

	assert(client->server->magic == 6);

	wlr_log(WLR_INFO, "client_blur: %s (%p)", client_title(client),
		(void *) client);
	wlr_xdg_toplevel_set_activated(client->xdg_toplevel, false);
}

// TODO
// unfocus/focuse 这里也许可以和鼠标、键盘等触发放在一起
// 或者 seat 那边的事件来自动做，避免手动调用

void client_focus(struct ws_client *new_client) {
	if (!new_client) {
		return;
	}
	struct ws_server *server = new_client->server;
	assert(server->magic == 6);

	struct wlr_surface *new_surface = client_surface(new_client);

	// TODO why don't we directly set it as client_zero
	// TODO test it on tinywl
	struct wlr_surface *old_surface =
		server->seat->keyboard_state.focused_surface;

	if (old_surface == new_surface) {
		return;
	}

	struct ws_client *old_client = client_from_surface(old_surface);
	client_blur(old_client);

	wl_list_remove(&new_client->link);
	wl_list_insert(&server->clients, &new_client->link);
	wlr_log(WLR_INFO, "client_focus: %s (%p)", client_title(new_client),
		(void *) new_client);

	wlr_xdg_toplevel_set_activated(new_client->xdg_toplevel, true);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(
			server->seat, new_surface, keyboard->keycodes,
			keyboard->num_keycodes, &keyboard->modifiers);
	}
}

void client_position(struct ws_client *client, struct ws_output *output) {
	if (!client) {
		return;
	}

	struct ws_server *server = client->server;
	assert(server->magic == 6);

	wlr_log(WLR_INFO, "client_position");

	if (output) {
		client->output = output;
	} else {
		output = client->output;
	}

	struct wlr_box output_box = output->output_box;

	// int margin = 0;
	int max_height = client->xdg_toplevel->current.max_height;
	int max_width = client->xdg_toplevel->current.max_width;

	struct wlr_box client_box = {0};
	client_box.height = (max_height < output_box.height && max_height != 0)
				    ? max_height
				    : output_box.height;
	client_box.width = (max_width < output_box.width && max_width != 0)
				   ? max_width
				   : output_box.width;

	client_box.x = output_box.x + (output_box.width - client_box.width) / 2;
	client_box.y =
		output_box.y + (output_box.height - client_box.height) / 2;

	wlr_xdg_toplevel_set_size(client->xdg_toplevel, client_box.width,
				  client_box.height);
	wlr_xdg_toplevel_set_maximized(client->xdg_toplevel, true);

	wlr_scene_node_set_position(&client->scene_tree->node, client_box.x,
				    client_box.y);
}

void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	assert(!data);
	struct ws_client *client = wl_container_of(listener, client, map);

	client->scene_tree = wlr_scene_xdg_surface_create(
		&client->server->scene->tree, client->xdg_toplevel->base);

	// wlr_scene_node.data is used in desktop_toplevel_at
	client->scene_tree->node.data = client;

	// wlr_xdg_surface.data is used in handle_new_xdg_popup
	client->xdg_toplevel->base->data = client->scene_tree;

	struct ws_server *server = client->server;
	assert(server->magic == 6);

	wl_list_insert(&server->clients, &client->link);

	client_position(client, output_now(server));
	// client_raise(client);
	client_focus(client);
}

void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	// unmap -> destroy
	assert(!data);
	struct ws_client *client = wl_container_of(listener, client, unmap);

	wl_list_remove(&client->link);

	struct ws_server *server = client->server;
	assert(server->magic == 6);

	struct ws_client *next_client = client_zero(server);
	if (!next_client) {
		return;
	}
	struct ws_output *next_output = next_client->output;
	assert(next_output);
	output_focus(next_output);
	client_focus(next_client);
}

void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	// commit -> map -> commit -> commit
	struct ws_client *client = wl_container_of(listener, client, commit);

	struct wlr_xdg_surface *xdg_surface = client->xdg_toplevel->base;
	assert(xdg_surface->surface == data);

	if (xdg_surface->initial_commit) {
		wlr_xdg_toplevel_set_size(client->xdg_toplevel, 0, 0);
	}
}

void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
	struct ws_client *client =
		wl_container_of(listener, client, request_fullscreen);
	assert(!data);

	if (!client->xdg_toplevel->base->initial_commit) {
		return;
	}
	// 鼠标或者什么操作应该会先激活窗口
	//
	// from cage, 但是我们的操作中会设置 maximize 不确定影响
	client_position(client, NULL);

	wlr_xdg_toplevel_set_fullscreen(
		client->xdg_toplevel,
		client->xdg_toplevel->requested.fullscreen);
}

void xdg_toplevel_set_app_id(struct wl_listener *listener, void *data) {
	struct ws_client *client =
		wl_container_of(listener, client, set_app_id);
	assert(!data);
	assert(client->server->magic == 6);

	const char *app_id = client->xdg_toplevel->app_id;
	if (!app_id) {
		return;
	}
	client->lowercase_app_id = calloc(1, strlen(app_id) + 1);
	for (int i = 0; app_id[i]; i++) {
		client->lowercase_app_id[i] = tolower(app_id[i]);
	}
}

void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	struct ws_client *client = wl_container_of(listener, client, destroy);
	assert(!data);

	struct ws_server *server = client->server;
	assert(server->magic == 6);

	wl_list_remove(&client->map.link);
	wl_list_remove(&client->unmap.link);
	wl_list_remove(&client->commit.link);
	wl_list_remove(&client->request_fullscreen.link);
	wl_list_remove(&client->set_app_id.link);
	wl_list_remove(&client->destroy.link);

	free(client);
}

void handle_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, new_xdg_toplevel);
	assert(server->magic == 6);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct ws_client *client = calloc(1, sizeof(*client));
	if (!client) {
		return;
	}
	client->xdg_toplevel = xdg_toplevel;
	client->server = server;

	// emit: wlr_surface_map()
	// data: NULL
	client->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &client->map);

	// emit: wlr_surface_unmap()
	// data: NULL
	client->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap,
		      &client->unmap);

	// emit: surface_commit_state()
	// data: struct wlr_surface *
	client->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit,
		      &client->commit);

	// emit: xdg_toplevel_handle_set_fullscreen()
	// emit: xdg_toplevel_handle_unset_fullscreen()
	// data: NULL
	client->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen,
		      &client->request_fullscreen);

	// emit: xdg_toplevel_handle_set_app_id()
	// data: NULL
	client->set_app_id.notify = xdg_toplevel_set_app_id;
	wl_signal_add(&xdg_toplevel->events.set_app_id, &client->set_app_id);

	// emit: destroy_xdg_toplevel()
	// data: NULL
	client->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &client->destroy);
}

void xdg_popup_commit(struct wl_listener *listener, void *data) {
	struct ws_client_popup *popup =
		wl_container_of(listener, popup, commit);

	struct wlr_xdg_surface *xdg_surface = popup->xdg_popup->base;
	assert(xdg_surface->surface == data);

	if (xdg_surface->initial_commit) {
		wlr_xdg_surface_schedule_configure(xdg_surface);
	}
}

void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	struct ws_client_popup *popup =
		wl_container_of(listener, popup, destroy);
	assert(!data);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

void handle_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, new_xdg_popup);
	assert(server->magic == 6);
	struct wlr_xdg_popup *xdg_popup = data;

	struct ws_client_popup *popup = calloc(1, sizeof(*popup));
	if (!popup) {
		return;
	}
	popup->xdg_popup = xdg_popup;

	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	// FIXME maybe popup->parent->parent
	// TODO check the depth of its parent

	// wlr_xdg_surface.data is set in xdg_toplevel_map
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data =
		wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	// emit: surface_commit_state()
	// data: struct wlr_surface *
	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	// emit: destroy_xdg_popup()
	// data: NULL
	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void xdg_decoration_commit(struct wl_listener *listener, void *data) {
	struct ws_xdg_decoration *xdg_decoration =
		wl_container_of(listener, xdg_decoration, commit);

	struct wlr_xdg_surface *xdg_surface =
		xdg_decoration->wlr_decoration->toplevel->base;
	assert(xdg_surface->surface == data);

	if (xdg_surface->initial_commit) {
		wlr_xdg_toplevel_decoration_v1_set_mode(
			xdg_decoration->wlr_decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
}

void xdg_decoration_request_mode(struct wl_listener *listener, void *data) {
	struct ws_xdg_decoration *xdg_decoration =
		wl_container_of(listener, xdg_decoration, request_mode);
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;

	assert(wlr_decoration == xdg_decoration->wlr_decoration);

	if (xdg_decoration->wlr_decoration->toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(
			xdg_decoration->wlr_decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
}

void xdg_decoration_destroy(struct wl_listener *listener, void *data) {
	struct ws_xdg_decoration *xdg_decoration =
		wl_container_of(listener, xdg_decoration, destroy);
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;

	assert(wlr_decoration == xdg_decoration->wlr_decoration);

	wl_list_remove(&xdg_decoration->destroy.link);
	wl_list_remove(&xdg_decoration->commit.link);
	wl_list_remove(&xdg_decoration->request_mode.link);
	free(xdg_decoration);
}

void handle_new_xdg_toplevel_decoration(struct wl_listener *listener,
					void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, xdg_toplevel_decoration);
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;

	struct ws_xdg_decoration *xdg_decoration =
		calloc(1, sizeof(*xdg_decoration));
	if (!xdg_decoration) {
		return;
	}

	xdg_decoration->wlr_decoration = wlr_decoration;

	// emit: surface_commit_state
	// data: struct wlr_surface *
	xdg_decoration->commit.notify = xdg_decoration_commit;
	wl_signal_add(&wlr_decoration->toplevel->base->surface->events.commit,
		      &xdg_decoration->commit);

	// emit: toplevel_decoration_handle_set_mode()
	// data: struct wlr_xdg_toplevel_decoration_v1 *
	xdg_decoration->request_mode.notify = xdg_decoration_request_mode;
	wl_signal_add(&wlr_decoration->events.request_mode,
		      &xdg_decoration->request_mode);

	// emit: toplevel_decoration_handle_resource_destroy()
	// data: struct wlr_xdg_toplevel_decoration_v1 *
	xdg_decoration->destroy.notify = xdg_decoration_destroy;
	wl_signal_add(&wlr_decoration->events.destroy,
		      &xdg_decoration->destroy);
}
