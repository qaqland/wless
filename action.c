#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>
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

static struct ws_client *checkout_client(struct ws_server *server,
					 bool local_check, bool want_next) {
	assert(server->magic == 6);

	if (wl_list_empty(&server->clients)) {
		return NULL;
	}

	//	single	multiple
	// Alt	1	2
	// Win	3	4
	//
	// 1: now > next > ok
	// 2: now > zero > next > ok
	// 3: now > next > ok
	// 4: now > next > check > ok

	struct ws_client *src_client = client_now(server);
	struct ws_client *des_client = NULL;

	struct wl_list *link = NULL;

	struct ws_output *only_output = output_only(NULL);
	if (!src_client) {
		if (only_output) {
			wlr_log(WLR_ERROR, "when there is only one output, "
					   "src_client should not be empty");
			return NULL;
		}
		if (local_check) {
			// normal early return :)
			return NULL;
		}
		link = &server->clients;
	} else {
		link = &src_client->link;
	}

	struct ws_output *output = output_now(server);

	for (;;) {
		link = want_next ? link->next : link->prev;
		if (link == &server->clients) {
			continue;
		}
		des_client = wl_container_of(link, des_client, link);
		assert(des_client->server->magic == 6);

		if (!local_check) {
			break;
		}
		if (client_output(des_client) != output) {
			continue;
		}
		break;
	}

	if (des_client == src_client) {
		return NULL;
	} else {
		return des_client;
	}
}

void action_next_window(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	struct ws_client *client = checkout_client(server, false, true);
	client_raise(client);
}

void action_prev_window(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	struct ws_client *client = checkout_client(server, false, false);
	client_raise(client);
}

void action_next_window_local(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	struct ws_client *client = checkout_client(server, true, true);
	client_raise(client);
}

void action_prev_window_local(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);

	struct ws_client *client = checkout_client(server, true, false);
	client_raise(client);
}

void action_focus_done(struct ws_server *server) {
	assert(server->magic == 6);

	struct ws_client *client = client_now(server);
	if (!client) {
		return;
	}
	if (client == client_zero(server)) {
		return;
	}
	client_focus(client);
	struct ws_output *output = client_output(client);
	output_focus(output);
}

// TODO
void action_next_display(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);
}

void action_prev_display(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);
}

void action_move_next(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);
}
void action_move_prev(struct ws_server *server, const char *command) {
	assert(server->magic == 6);
	assert(!command);
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

	// TODO: more canonical
	if (keysym == XKB_KEY_ISO_Left_Tab) {
		keysym = XKB_KEY_Tab;
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
