#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Gets the user's runtime directory.
 *
 * Falls back to /run/user/uid if XDG_RUNTIME_DIR is not set.
 *
 * @param buffer The buffer to write the runtime directory to.
 * @param size The size of the destination buffer.
 */
void getXDGruntimeDir(char * buffer, size_t size)
{
	char const * xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
	buffer[0] = '\0';
	if (xdg_runtime_dir) {
		strncpy(buffer, xdg_runtime_dir, size - 1);
		buffer[size - 1] = '\0';
		return;
	}

	int const uid = getuid();
	snprintf(buffer, size, "/run/user/%d", uid);
}
