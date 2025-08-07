#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <varlink.h>

#include "ipc.h"

int get_ipc_address(char *address, size_t size) {
	const char *wayland_display = getenv("WAYLAND_DISPLAY");
	const char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
	assert(wayland_display);
	assert(xdg_runtime_dir);

	return snprintf(address, size, "unix:%s/land.qaq.wless.%s",
			xdg_runtime_dir, wayland_display);
}

int handle_varlink_events(int fd, uint32_t mask, void *data) {}
