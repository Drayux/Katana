#include "ipc_common.h"

#include "common.h"
#include "environment.h"

#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// TODO: This is another function where I want a consistent string retrieval
// pattern. Not sure if it's worht the memory to make a bunch of static
// path_max buffers?
char * ipc_get_socket_path(void)
{
	size_t len;
	char runtime_dir[PATH_MAX];
	char socket_path[PATH_MAX];
	char * rptr;
	char const * format;

	getXDGruntimeDir(runtime_dir, sizeof(runtime_dir));
	if ((len = strlen(runtime_dir)) == 0) {
		fprintf(stderr, "Failed to get LibreSplit socket path.\n");
		return NULL;
	}

	format = (runtime_dir[len - 1] == '/') ? "%s%s" : "%s/%s";
	len = snprintf(socket_path, PATH_MAX, format, runtime_dir, LIBRESPLIT_SOCK_NAME);

	rptr = malloc(len + 1);
	ASSERT_ALLOC(rptr, NULL);

	strcpy(rptr, socket_path);
	return rptr;
}

// Checks if the socket exists and connects to it if so. Returns socket fd on
// success, -1 otherwise.
// Call close on returned fd when done only on success
int ipc_connect(void)
{
	int sockfd;
	char * sockpath;
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};

	sockpath = ipc_get_socket_path();
	if (!sockpath) {
		LOG_ERR("Invalid IPC socket path");
		return -1;
	}
	strcpy(addr.sun_path, sockpath);
	free(sockpath);

	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		LOG_ERR("Failed to create IPC socket");
		return -1;
	}

	if (connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
		// No error to log:
		// This is a normal path when checking if the socket exists
		close(sockfd);
		return -1;
	}

	return sockfd;
}

// Same as above but for a listening socket
int ipc_create_listener(void)
{
	int sockfd;
	char * sockpath;
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};

	sockpath = ipc_get_socket_path();
	if (!sockpath) {
		LOG_ERR("Invalid IPC socket path");
		return -1;
	}
	strcpy(addr.sun_path, sockpath);
	unlink(sockpath); // TODO: Test to be 100% sure this always returns
					  // what we expect (no symlinks, etc.)

	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		LOG_ERR("Failed to create IPC socket");
		free(sockpath);
		return -1;
	}

	if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0);
	else if (listen(sockfd, 20) < 0); // backlog for many short connections
	else {
		// Success path
		LOG_INFOF("Created IPC listener socket %s", sockpath);
		free(sockpath);
		return sockfd;
	}

	LOG_ERR("Failed to bind IPC socket");
	close(sockfd);
	free(sockpath);
	return -1;
}
