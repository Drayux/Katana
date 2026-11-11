#include "ipc.h"

#include "environment.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/limits.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int send_fd = 0;

/* Returns 0 on success, 1 if not sent but try_again not set, -errno if error */
static int write_all(int send_fd, uint8_t * msg, size_t msg_len, bool try_again)
{
	ssize_t total_sent = 0;
	ssize_t bytes_sent = 0;

	while (total_sent < msg_len) {
		bytes_sent = write(send_fd, msg, msg_len - total_sent);
		if (bytes_sent < 0) {
			if (errno == EINTR) {
				/* Always try again if interrupted */
				continue;
			}
			if (errno == EAGAIN) {
				/* Send fd is blocked */
				// TODO: I think this has the risk of yielding, and then
				// another thread can begin a write, causing an unexpected
				// merge of commands
				if (try_again) {
					sched_yield();
					continue;
				}
				else {
					return 0;
				}
			}
			return -errno;
		}

		total_sent += bytes_sent;
		msg += bytes_sent;

		if (total_sent == msg_len) {
			/* Complete write, nothing more to do */
			break;
		}
	}

	return 0;
}

/* NOTE: All host -> network conversions happen here, NOT BEFORE!
 * Katana should trust the values of this at any point outside this function.
 *
 * Return is the same as write_all ^^ */
int ipc_send(ipc_msg_t * msg, bool try_again)
{
	int status;
	ipc_msg_t proxy;

	if (send_fd <= STDERR_FILENO) {
		LOG_ERROR("No IPC send fd set");
		return -1;
	}
	else if (!msg) {
		LOG_ERROR("Send NULL msg");
		return -1;
	}

	if (msg->len > KATANA_IPC_MAX_PAYLOAD) {
		LOG_ERROR("App command exceeds maximum send size");
		return -1;
	}

	// proxy.ts = htonll(msg->ts); // TODO!!!
	proxy.ts = msg->ts;
	proxy.op = htons(msg->op);
	proxy.len = htons(msg->len);

	/* TODO:
	   // Thinking about refactoring ipc_set_send_fd into ipc_init
	if (try_again) {
		pthread_mutex_lock(LOCK);
	}
	else if (pthread_mutex_trylock(LOCK) < 0) {
		return 1;
	}
	 */
	/* Write just the header */
	/* TODO pt2: I think this implementation has a bug as it sits with the
	 * handler in event.c. When the first write is sent, the socket becomes
	 * ready, so the handler goes to read it. It exits immediately instead of
	 * waiting for a complete message to be sent. */
	status = write_all(send_fd, (uint8_t *) &proxy, sizeof(ipc_msg_t), try_again);
	//
	if (status == 0) {
		if (msg->len > 0) {
			/* Write just the payload */
			status = write_all(send_fd, msg->buf, msg->len, try_again);
		}
	}

	if (status < 0) {
		perror("write fail");
	}
	else if (status > 0) {
		LOG_DEBUGF("App command %s dropped because the pipe was busy",
			app_cmd_to_str(msg->op));
	}
	// pthread_mutex_unlock(LOCK);

	return status;
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
