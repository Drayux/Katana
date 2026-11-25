#include "ipc_common.h"

#include "environment.h"
#include "io_common.h"

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

struct conn_handle_args {
	int sock_fd;
	ipc_handler_t on_msg;
};
static int _conn_handle_loop(void * _args);

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

/* Accepts a connection in a detached thread and passes any messages to the
 * specified command handler routine */
int ipc_handle(int sock_fd, ipc_handler_t on_msg)
{
	int status;
	pthread_t handler_thread;
	struct conn_handle_args args = {
		.sock_fd = sock_fd,
		.on_msg = on_msg,
	};

	status = pthread_create(&handler_thread, NULL, &_conn_handle_loop, &args);
	if (!status) {
		status = pthread_detach(handler_thread);
	}

	return status;
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

/* NOTE: All host -> network conversions happen here, NOT BEFORE!
 * Katana should trust the values of this at any point outside this function. */
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
	// ^^ TODO pt3: now I'm refactoring io_write_msg, so this may need to be tweaked
	status = io_write_msg(send_fd, &proxy, sizeof(ipc_msg_t), try_again);
	//
	if (status == 0) {
		if (msg->len > 0) {
			/* Write just the payload */
			status = io_write_msg(send_fd, msg->buf, msg->len, try_again);
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

int ipc_unlink_socket_path(void)
{
	int rval = 1; // TODO: New return value format
	char * socket_path;

	if ((socket_path = ipc_get_socket_path())) {
		rval = unlink(socket_path);
		free(socket_path);
	}

	return rval;
}

/* Simple setter for the FD that should be used for IPC communcation.
 * In essence, this would be called after ipc_connect for any secondary tool
 * to interfce with the timer, or after event_server_init by the main program
 * for the other threads to interact with the main thread. */
// void ipc_set_send_fd(int fd)
// {
	// if (send_fd > STDERR_FILENO) {
		// LOG_ERROR("Overwriting existing send fd");
		// close(send_fd);
	// }
	// send_fd = fd;
// }

static void * _connection_handle_loop(void * _args)
{
	struct conn_handle_args const * args = _args;
	int sock_fd = args->sock_fd;
	ipc_handler_t on_msg = args->on_msg;

	int client_fd;
	io_buf_t * conn_buf = NULL;

	// errno = 0;
	if ((client_fd = accept(sock_fd, NULL, NULL)) <= STDERR_FILENO) {
		if (client_fd < 0) {
			// status = -errno;
			// perror("accept fail");
			LOG_ERROR("Failed accept incoming IPC conenction");
		}
		else {
			LOG_ERROR("Invalid IPC client fd during accept");
		}
		return NULL;
	}

	conn_buf = io_buf_create(KATANA_IPC_MAX_PAYLOAD);
	// ^^ TODO: This is plenty of space for now, but that may change

	/* Loop on the connection, passing received bytes to on_msg */
	// ^^ loop handles the ipc_msg format..i.e. makes sure it sees a complete message
	// ^^ loop may timeout unless it receives a keep-alive message
	while (!conn_buf->status) {
		io_read(client_fd, conn_buf, on_msg, NULL);
		if (conn_buf->status == IO_REPLY) {
			io_write(client_fd, conn_buf, NULL, NULL);
			// ^^ conn_buf->status is reset if this call succeeds
		}
	}

	/* --- */

	/* NOTE: No reporter for this thread since we don't expect it to last the
	 * lifetime of the program */
	LOG_INFOF("IPC connection exited with status %d", conn_buf->status);

	DESTROY_FD(client_fd);
	NULL_FREE(conn_buf);

	return NULL;
}
