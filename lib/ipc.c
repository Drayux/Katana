#include "ipc.h"

#include "environment.h"

#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int send_fd = 0;

ssize_t ipc_send(app_cmd_t * send_cmd, bool try_again) {
	size_t send_len;
	ssize_t written;

	if (send_fd <= STDERR_FILENO) {
		LOG_ERROR("No IPC send fd set");
		return 0;
	}
	else if (!send_cmd) {
		LOG_ERROR("Send NULL app command");
		return 0;
	}

	send_len = sizeof(app_cmd_t) + send_cmd->buf_len;

	if (send_len > sizeof(cmd_buf.b)) {
		/* While read / write support this, exceeding this size for our use
		 * case means that something went catastrophically wrong. */
		LOG_ERROR("App command exceeds maximum send size");
		return 0;
	}

	while (true) {
		written = write(event.writefd, (byte *) send_cmd, send_len);
		if (written == send_len) {
			/* Complete write, nothing more to do */
			return send_len;
		}

		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN) {
				// pipe is blocked right now
				if (try_again) {
					sched_yield();
					continue;
				}
				else {
					LOG_DEBUGF(
						"App command `%d` dropped because the pipe was busy",
						send_cmd->op
					);
					return 0;
				}
			}
		}
		// loop is only here so we can try again if interrupted or blocked
		break;
	}
	perror("write fail");
	return 0;
}

// Checks if the socket exists and connects to it if so. Returns socket fd on
// success, -errno otherwise.
// Call close on returned fd when done only on success
int ipc_connect(void)
{
	int status = 0; // OK
	int sockfd = -1;
	char * sockpath;
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};

	sockpath = ipc_get_socket_path();
	if (!sockpath) {
		LOG_ERROR("Invalid IPC socket path");
		return -1;
	}
	strcpy(addr.sun_path, sockpath);
	free(sockpath);

	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		status = -errno;

		LOG_ERROR("Failed to create IPC socket");
		return status;
	}

	if (connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
		status = -errno;

		/* No error to log: This is a normal path when checking if the socket
		 * exists */
		close(sockfd);
		return status;
	}

	return status ? status : sockfd;
}

// Creates a UNIX IPC socket where other processes can invoke katana commands
int ipc_listen(void)
{
	int status = 0; // OK
	int sockfd = -1;
	char * sockpath;
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};

	sockpath = ipc_get_socket_path();
	if (!sockpath) {
		LOG_ERROR("Invalid IPC socket path");
		return -1;
	}
	strcpy(addr.sun_path, sockpath);
	unlink(sockpath); // TODO: Test to be 100% sure this always returns
					  // what we expect (no symlinks, etc.)

	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		status = -errno;

		LOG_ERROR("Failed to create IPC socket");
		free(sockpath);
		return status;
	}

	if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0);
	else if (listen(sockfd, 20) < 0); // backlog for many short connections
	else {
		// Success path
		LOG_INFOF("Created IPC listener socket %s", sockpath);
		free(sockpath);
		return sockfd;
	}

	status = -errno;

	LOG_ERROR("Failed to bind IPC socket");
	close(sockfd);
	free(sockpath);

	return status ? status : sockfd;
}

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
	len = snprintf(socket_path, PATH_MAX, format, runtime_dir, KATANA_IPC_PATH);

	rptr = malloc(len + 1);
	ASSERT_ALLOC(rptr, NULL);

	strcpy(rptr, socket_path);
	return rptr;
}

/* Simple setter for the FD that should be used for IPC communcation.
 * In essence, this would be called after ipc_connect for any secondary tool
 * to interfce with the timer, or after event_server_init by the main program
 * for the other threads to interact with the main thread. */
void ipc_set_send_fd(int fd)
{
	if (send_fd > STDERR_FILENO) {
		LOG_ERROR("Overwriting existing send fd");
		close(send_fd);
	}
	send_fd = fd;
}
