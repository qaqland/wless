#ifndef WLESS_ACTION_H
#define WLESS_ACTION_H

#include <assert.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include "wless.h"

// switch window
void action_next_window(struct ws_server *server, const char *command);
void action_prev_window(struct ws_server *server, const char *command);
void action_next_window_local(struct ws_server *server, const char *command);
void action_prev_window_local(struct ws_server *server, const char *command);

void action_focus_done(struct ws_server *server);

void action_switch_display(struct ws_server *server, const char *command);
void action_shift_client(struct ws_server *server, const char *command);

// others
void action_exec(struct ws_server *server, const char *command);
void action_jump(struct ws_server *server, const char *command);
void action_close_window(struct ws_server *server, const char *command);
void action_quit(struct ws_server *server, const char *command);

bool action_main(struct ws_server *server, uint32_t modifiers,
		 xkb_keysym_t keysym);

extern const struct ws_key_bind action_keybinds[];

#endif
