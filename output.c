#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "client.h"
#include "input.h"
#include "output.h"
#include "wless.h"

const char *output_name(struct ws_output *output) {
	if (!output) {
		return "NULL";
	}
	const char *name = output->wlr_output->name;
	return name ? name : "EMPTY";
}

struct ws_output *output_only(struct ws_server *server) {
	// most time we have only one output
	static struct ws_output *only = NULL;
	if (!server) {
		return only;
	}

	// pass server to update
	assert(server->magic == 6);
	if (server->outputs.next == server->outputs.prev) {
		only = wl_container_of(server->outputs.next, only, link);
	} else {
		only = NULL;
	}

	return only;
}

// unlike client, output_zero and output_now are equivalent, both can obtain the
// current focused output.
struct ws_output *output_now(struct ws_server *server) {
	assert(server->magic == 6);

	struct ws_output *output = output_only(NULL);
	if (output) {
		return output;
	}

	if (wl_list_empty(&server->outputs)) {
		wlr_log(WLR_INFO, "the outputs is empty");
		return NULL;
	}

	output = wl_container_of(server->outputs.next, output, link);
	return output;
}

void output_focus(struct ws_output *output) {
	assert(output);
	struct ws_server *server = output->server;
	assert(server->magic == 6);

	if (output == output_now(server)) {
		return;
	}
	wl_list_remove(&output->link); // safe?
	wl_list_insert(&server->outputs, &output->link);
	return;
}

struct ws_client *output_client(struct ws_output *output) {
	int center_x = output->output_box.x + output->output_box.width / 2;
	int center_y = output->output_box.y + output->output_box.height / 2;
	return client_at(output->server, center_x, center_y, NULL, NULL, NULL);
}

void output_position(struct ws_server *server) {
	assert(server->magic == 6);

	if (wl_list_empty(&server->outputs)) {
		return;
	}

	// FIXME
	// why kanshi trigged this function run three times?

	// update output geometry: output->output_box
	struct ws_output *output;
	wl_list_for_each (output, &server->outputs, link) {
		struct wlr_box *layout_box = &output->output_box;
		wlr_output_layout_get_box(server->output_layout,
					  output->wlr_output, layout_box);
		wlr_log(WLR_INFO, "output %s (%p): (%4d, %4d, %4d, %4d)",
			output_name(output), (void *) output->wlr_output,
			layout_box->x, layout_box->y, layout_box->width,
			layout_box->height);
	}

	// if only one output, we can appoint it
	struct ws_output *selected_output = output_only(NULL);
	if (selected_output) {
		assert(selected_output->server->magic == 6);
	}

	// it's the duty of the output_destory to move the on-output client.
	struct ws_client *client;
	wl_list_for_each (client, &server->clients, link) {
		client_position(client, selected_output);
	}
}

static void output_manager_update_config(struct ws_server *server) {
	assert(server->magic == 6);

	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();

	struct ws_output *output;
	wl_list_for_each (output, &server->outputs, link) {
		// auto: config_head_handle_output_destroy
		struct wlr_output_configuration_head_v1 *config_head =
			wlr_output_configuration_head_v1_create(
				config, output->wlr_output);

		struct wlr_box output_box = output->output_box;
		// TODO why define state.enable twice?
		// config_head->state.enabled = output->enabled;
		config_head->state.enabled = !wlr_box_empty(&output_box);
		config_head->state.x = output_box.x;
		config_head->state.y = output_box.y;
	}
	// auto: head_handle_output_destroy
	wlr_output_manager_v1_set_configuration(server->output_manager_v1,
						config);
}

void handle_output_layout_change(struct wl_listener *listener, void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, output_layout_change);
	assert(server->output_layout == data);

	output_only(server);
	output_position(server);
	output_manager_update_config(server);
}

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct ws_output *output = wl_container_of(listener, output, frame);
	assert(output->wlr_output == data);

	wlr_scene_output_commit(output->scene_output, NULL);

	struct timespec now = {0};
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(output->scene_output, &now);
}

static void output_handle_request_state(struct wl_listener *listener,
					void *data) {
	struct ws_output *output =
		wl_container_of(listener, output, request_state);
	struct wlr_output_event_request_state *event = data;

	if (wlr_output_commit_state(output->wlr_output, event->state)) {
		output_manager_update_config(output->server);
	}
}

static void output_handle_destroy(struct wl_listener *listener, void *data) {
	struct ws_output *output = wl_container_of(listener, output, destroy);
	assert(output->wlr_output == data);

	struct ws_server *server = output->server;

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);

	wl_list_remove(&output->link);

	struct ws_output *new_output = output_now(server);
	if (!new_output) {
		wlr_log(WLR_INFO, "all outputs disabled, silence");
	}

	struct ws_client *client;
	wl_list_for_each (client, &server->clients, link) {
		if (client_output(client) != output) {
			continue;
		}
		client_position(client, new_output);
	}

	// auto:
	// - scene_output_layout_output_handle_layout_output_destroy
	// - scene_output_layout_handle_layout_change
	wlr_output_layout_remove(server->output_layout, output->wlr_output);

	free(output);
}

void handle_new_output(struct wl_listener *listener, void *data) {

	struct ws_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_log(WLR_INFO, "[output] new %s: %p", wlr_output->name,
		(void *) wlr_output);

	// TODO: headless output

	if (!wlr_output_init_render(wlr_output, server->allocator,
				    server->renderer)) {
		wlr_log(WLR_ERROR, "[output] failed to init render");
		goto err_init_render;
	}

	// state (mode)
	struct wlr_output_state state = {0};
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	} else {
		wlr_log(WLR_INFO, "[output] doesn't support modes");
	}
	// emit: wlr_output.events.commit
	// data: struct wlr_output_event_commit *
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	// ws_output
	struct ws_output *output = calloc(1, sizeof(*output));
	if (!output) {
		wlr_log(WLR_ERROR, "[output] failed to allocate output");
		goto err_allocate;
	}
	output->server = server;
	output->wlr_output = wlr_output;
	wlr_output->data = output;

	// server
	wl_list_insert(&server->outputs, &output->link);

	// auto: wlr_output_create_global
	// auto: wlr_output_destroy_global
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	if (!l_output) {
		wlr_log(WLR_ERROR, "failed to create output_layout_output");
		goto err_output_layout;
	}

	output->scene_output =
		wlr_scene_output_create(server->scene, wlr_output);
	if (!output->scene_output) {
		wlr_log(WLR_ERROR, "failed to create scene_output");
		goto err_scene_output;
	}

	// auto:
	// - scene_output_layout_output_handle_layout_output_destroy
	// - scene_output_layout_output_handle_scene_output_destroy
	wlr_scene_output_layout_add_output(server->scene_output_layout,
					   l_output, output->scene_output);

	// TODO
	// if there are lots of clients and this is the only one output, should
	// we check all clients and bring them back to this output?

	// emit: wlr_output_send_frame()
	// data: struct wlr_output *
	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	// emit: wlr_output_send_request_state()
	// data: struct wlr_output_event_request_state *
	output->request_state.notify = output_handle_request_state;
	wl_signal_add(&wlr_output->events.request_state,
		      &output->request_state);

	// emit: wlr_output_finish()
	// data: struct wlr_output *
	output->destroy.notify = output_handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	return;

err_scene_output:
	wlr_output_layout_remove(server->output_layout, output->wlr_output);

err_output_layout:
	free(output);

err_allocate:
err_init_render:
	return;
}

static bool
output_apply_config_head(struct wlr_output_configuration_head_v1 *head,
			 struct wlr_output_state *state, bool test_only) {
	struct ws_output *output = head->state.output->data;
	struct ws_server *server = output->server;

	wlr_output_head_v1_state_apply(&head->state, state);

	if (test_only) {
		return wlr_output_test_state(output->wlr_output, state);
	}

	if (!wlr_output_commit_state(output->wlr_output, state)) {
		return false;
	}

	if (!head->state.enabled) {
		wlr_output_layout_remove(server->output_layout,
					 output->wlr_output);
		return true;
	}

	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add(server->output_layout, output->wlr_output,
				      head->state.x, head->state.y);
	wlr_scene_output_layout_add_output(server->scene_output_layout,
					   l_output, output->scene_output);

	return true;
}

static void
output_manager_apply_config(struct wlr_output_configuration_v1 *config,
			    bool test_only) {
	struct wlr_output_configuration_head_v1 *head;
	bool is_ok = true;
	struct wlr_output_state state = {0};
	wl_list_for_each (head, &config->heads, link) {
		is_ok &= output_apply_config_head(head, &state, test_only);
		wlr_output_state_finish(&state);
	}

	(is_ok ? wlr_output_configuration_v1_send_succeeded
	       : wlr_output_configuration_v1_send_failed)(config);

	wlr_output_configuration_v1_destroy(config);
}

void handle_output_manager_test(struct wl_listener *listener, void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *config = data;
	output_manager_apply_config(config, true);
}

void handle_output_manager_apply(struct wl_listener *listener, void *data) {
	struct ws_server *server =
		wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *config = data;
	output_manager_apply_config(config, false);
	output_position(server);
}
