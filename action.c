#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "action.h"
#include "client.h"
#include "output.h"
#include "wless.h"

#define VT_LIST X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12)

static bool change_vt(struct ws_server *server, uint32_t modifiers,
		      xkb_keysym_t keysym) {
	assert(server->magic == 6);

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

	wlr_log(WLR_DEBUG, "[action] change to vt %d", vt);
	wlr_session_change_vt(server->session, vt);
	return true;
}

static void checkout_client(struct ws_server *server, bool local_check,
			    bool want_next) {
	assert(server->magic == 6);

	if (wl_list_empty(&server->clients)) {
		return;
	}

	struct ws_output *src_output = output_now(server);
	struct ws_output *des_output = NULL;

	struct ws_client *src_client = output_client(src_output);
	struct ws_client *des_client = NULL;

	if (!src_client && (local_check || output_only(NULL))) {
		wlr_log(WLR_INFO,
			"checkout_client early return, src_client: %s, "
			"src_output: %s",
			client_title(src_client), output_name(src_output));
		return;
	}

	struct wl_list *link = NULL;

	if (src_client) {
		link = &src_client->link;
	} else {
		link = &server->clients;
	}

	for (;;) {
		link = want_next ? link->next : link->prev;
		if (link == &server->clients) {
			continue;
		}
		des_client = wl_container_of(link, des_client, link);
		assert(des_client->server->magic == 6);

		des_output = client_output(des_client);
		wlr_log(WLR_DEBUG, "checking, des_client: %s, des_output: %s",
			client_title(des_client), output_name(des_output));
		if (!local_check) {
			break;
		}

		if (des_output == src_output) {
			break;
		}
	}

	if (des_client == src_client) {
		return;
	}

	output_focus(des_output);
	wlr_scene_node_raise_to_top(&des_client->scene_tree->node);
}

void action_next_window(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	checkout_client(server, false, true);
}

void action_prev_window(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	checkout_client(server, false, false);
}

void action_next_window_local(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	checkout_client(server, true, true);
}

void action_prev_window_local(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	checkout_client(server, true, false);
}

void action_focus_done(struct ws_server *server) {
	assert(server->magic == 6);

	struct ws_client *client = client_now(server);
	client_focus(client);
}

static void checkout_output(struct ws_server *server, bool move_client) {
	assert(server->magic == 6);

	if (output_only(NULL) || wl_list_empty(&server->outputs)) {
		return;
	}

	struct ws_client *client = client_now(server);
	struct ws_output *output =
		wl_container_of(server->outputs.prev, output, link);

	assert(output->server->magic == 6);

	if (move_client) {
		client_position(client, output);
		client_raise(client);
	}
	output_focus(output);
	client_focus(client_now(server));
}

// TODO
void action_switch_display(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	checkout_output(server, false);
}

void action_shift_client(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	checkout_output(server, true);
}

void action_exec(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(command);

	if (fork() == 0) {
		execlp("/bin/sh", "/bin/sh", "-c", command, NULL);
	}
	wlr_log(WLR_DEBUG, "[action] exec %s", command);
}

void action_jump(struct ws_server *server, const char *command) {
	assert(server->magic == 6);

	wlr_log(WLR_DEBUG, "[action] jump %s", command);

	// check if already running
	if (1) {
		action_exec(server, command);
		return;
	}

	// check if focus it
	// jump in turn

	// jump the first
}

void action_close_window(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	wlr_log(WLR_INFO, "[action] close-window");

	if (wl_list_empty(&server->clients)) {
		return;
	}

	struct ws_client *client = client_now(server);
	if (!client) {
		return;
	}

	// FIXME what happend
	wlr_xdg_toplevel_send_close(client->xdg_toplevel);

	// it is not blocked to wait close, only send event to client, so we
	// should not do more actions here
}

void action_quit(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	wlr_log(WLR_DEBUG, "[action] quit");

	// TODO: kill all clients?
	wl_display_terminate(server->wl_display);
}

const struct ws_key_bind action_keybinds[] = {
	{
		WLR_MODIFIER_ALT,
		XKB_KEY_Tab,
		action_next_window,
		NULL,
	},
	{
		WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT,
		XKB_KEY_Tab,
		action_prev_window,
		NULL,

	},
	{
		WLR_MODIFIER_LOGO,
		XKB_KEY_Tab,
		action_next_window_local,
		NULL,
	},
	{
		WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT,
		XKB_KEY_Tab,
		action_prev_window_local,
		NULL,

	},
	{
		WLR_MODIFIER_LOGO,
		XKB_KEY_period,
		action_switch_display,
		NULL,
	},
	{
		WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT,
		XKB_KEY_greater,
		action_shift_client,
		NULL,

	},
	{
		WLR_MODIFIER_LOGO,
		XKB_KEY_Return,
		action_exec,
		"foot",
	},
	{
		WLR_MODIFIER_LOGO,
		XKB_KEY_w,
		action_close_window,
		NULL,
	},
	{
		WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT,
		XKB_KEY_Escape,
		action_quit,
		NULL,
	},
};

bool action_main(struct ws_server *server, uint32_t modifiers,
		 xkb_keysym_t keysym) {
	assert(server->magic == 6);

	// clear CapsLock
	modifiers &= ~WLR_MODIFIER_CAPS;

	// ALT+CTRL+F1
	if (change_vt(server, modifiers, keysym)) {
		return true;
	}

	const struct ws_key_bind *key;

	// internal keybinds
	for (size_t i = 0; i < sizeof(action_keybinds) / sizeof(*key); i++) {
		key = &action_keybinds[i];
		if (key->keysym != keysym) {
			continue;
		}
		if (key->modifiers != modifiers) {
			continue;
		}
		key->function(server, key->command);
		return true;
	}

	// custome keybinds
	wl_array_for_each(key, &server->config->keybinds) {
		if (key->keysym != keysym) {
			continue;
		}
		if (key->modifiers != modifiers) {
			continue;
		}
		key->function(server, key->command);
		return true;
	}

	return false;
}
