#include "event.h"

#include "environment.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <unistd.h>

/*
 * This is a many to one model. The main thread is the only receiver.
 * When it receives a shutdown signal, it will exit its primary loop and
 * subsequently invoke the shutdown process for its child threads.
 */
// ^^ move this to the main handler function maybe

static struct {
	int pipe_fd;
	int sock_fd;
} ctx;

/**
 * Receives a message from the socket.
 *
 * @param sockfd The socket file descriptor.
 * @param out Pointer to the CTLMessage struct used as output.
 *
 * @return Zero if a full message was received, non-zero otherwise.
 */
static int receive_message(int sockfd, CTLMessage ** out)
{
	// Read header (blocking)
	uint32_t len = 0;
	ssize_t n = read(sockfd, &len, sizeof(len));
	if (n == 0)
		return 1; // client closed connection immediately
	len = ntohl(len);
	if (n != sizeof(len))
		return -1;

	CTLMessage * msg = malloc(sizeof(CTLMessage) + len);
	if (!msg) {
		fprintf(stderr, "Failed to allocate memory for control message.\n");
		return -1;
	}
	msg->length = len;

	size_t received = 0;
	while (received < len) {
		n = read(sockfd, msg->message + received, len - received);
		if (n <= 0) {
			free(msg);
			return -1;
		}
		received += n;
	}

	*out = msg;
	return 0;
}

/**
 * The remote control server thread.
 *
 * @param arg The arguments to pass to the thread
 */
static void * ipc_server_main(void * arg)
{
	int ipc_server_fd;
	int ready;

	prctl(PR_SET_NAME, "LS CTL Server", 0, 0, 0);

	struct pollfd fds[2] = {
		// {
			// .fd = notify_fd,
			// .events = POLLIN
		// },
		{
			.fd = ipc_server_fd,
			.events = POLLIN | POLLHUP | POLLERR
		},
	};

	while (1) {
		ready = poll(fds, 2, -1);
		if (ready < 0) {
			if (errno == EINTR) {
				continue;
			}
			LOG_ERROR("poll error");
			perror("poll");
			break;
		}

		/* Unlike logger thread, prioritize shutdown first */
		if (fds[0].revents & POLLIN) {
			/* IPC thread only notified for shutdown */
			CLEAR_EVENT(fds[0].fd);
			break;
		}

		if (fds[1].revents & POLLIN) {
			int client_fd = accept(ipc_server_fd, NULL, NULL);
			if (client_fd < 0) {
				perror("accept fail");
				continue; // do not stop the server on accept errors
			}

			CTLMessage * msg = NULL;
			int result = receive_message(client_fd, &msg);

			if (result == 0) {
				// if (msg->length == sizeof(CTLCommand)) {
					// TODO: I think I wanted to clean up this a bit to match
					// more closely with that of app_cmd.
					// CTLCommand command = *(CTLCommand *) msg->message;
					// CommandData * cmd_data = g_malloc(sizeof(CommandData));
					// cmd_data->command = command;

					// Queue command execution on main thread
					// g_idle_add(execute_command_on_main_thread, cmd_data);

					LOG_DEBUGF("Received data: %d", *((int *) msg->message));
				// }
				// else {
					// LOG_WARNF("Invalid message length: %u (expected %zu)",
						// msg->length, sizeof(CTLCommand));
				// }
				free(msg);
			}
			else {
				LOG_DEBUG("IPC client closed before message completed");
			}

			close(client_fd);
		}
		else if (fds[1].revents & (POLLHUP | POLLERR)) {
			LOG_ERROR("IPC socket failure (TODO not sure what all could happen here)");
			break;
		}
	}

	LOG_INFO("IPC server thread exiting");
    if (notify_fd > STDERR_FILENO) {
		close(notify_fd);
	}
    if (ipc_server_fd > STDERR_FILENO) {
		close(ipc_server_fd);
	}

	return NULL;
}

/*
 * NOTE: return value ptr is static memory and thus valid ONLY until
 * app_cmd_poll is called again
 */
static app_cmd_t * parse_command(char * buf, size_t len)
{
	/* Reads the something when there is something */
	while (true) {
		len = read(event.readfd, cmd_buf.b, sizeof(cmd_buf.b));
		if (len < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN) {
				/* Pipe was empty, try again from the start */
				// TOOD: Hopefully this becomes a stack-friendly tail recursion
				return app_cmd_poll();
			}
			perror("read");
			return NULL; // TODO: maybe `goto shutdown` instead?
		}
		else if (len == 0) {
			printf("read 0 bytes\n");
			perror("read");
			return NULL; // TODO: maybe `goto shutdown` instead?
		}

		// Assert the number of bytes read
		if (len != sizeof(app_cmd_t) + cmd_buf.t.buf_len) {
			LOG_ERRORF(
				"Invalid cmd read len: %d read does not match %u reported",
				len - sizeof(app_cmd_t),
				cmd_buf.t.buf_len
			);
		}
		// loop is only here so we can try again if interrupted
		break;
	}
	
	return &cmd_buf.t;
}

static app_cmd_t * handle_pipe_fd(int pipe_fd)
{
}

static app_cmd_t * handle_sock_fd(int sock_fd)
{
}


/* --- PUBLIC API --- */

// returns pipe writefd on success, -errno on failure
// (intended to be fed to ipc_set_send_fd)
int event_init(void)
{
	int rval = -1;
    int pipe_fds[2];
	int ipc_server_fd;

	/*
	 * TODO: This was originally an "emergency thread wake"
	 * I don't currently have a use for it, but I wanted to hold onto it until
	 * I assert that I don't need it.
	 *
	notify_fd = eventfd(0, O_NONBLOCK | O_CLOEXEC);
    if (notify_fd < 0) {
		LOG_ERROR("eventfd fail: could not create UI thread wake event fd");
		return NULL;
	}
	 */

    if (pipe2(pipe_fds, O_NONBLOCK | O_CLOEXEC) == 0) {
		ctx.pipe_fd = pipe_fds[0];
		rval = pipe_fds[1];
	}
	else {
		// perror("pipe fail");
		LOG_FATAL("pipe failed, could not create app event handler");
		return -errno;
	}

	ipc_server_fd = ipc_listen();
	if (ipc_server_fd > STDERR_FILENO) {
		ctx.sock_fd = ipc_server_fd;
	}
	else {
		/* Not fatal, but likely annoying */
		LOG_WARN("Failed to create IPC listener, running in standalone mode");
	}

	return rval;
}

/* Return value is the command op as an int, 0 if none but OK, negative on
 * failure. Main loop should begin shutdown immediately on the latter.
 * Former two can have any number of stages defined by the app itself (aka the
 * confirm save / confirm reload type of logic should not be handled here.`
 * This only provides the raw command.) */
int event_poll(app_cmd_t * cmd)
{
	int status = 0;
	int ready;
	ssize_t len;
	app_cmd_t event;

	// TODO: Do I want to assert that init was called? (Further refer to ui.c)

	struct pollfd fds[] = {
		{
			.fd = ctx.pipe_fd,
			.events = POLLIN | POLLHUP | POLLERR 
		},
		{
			.fd = ctx.sock_fd,
			.events = POLLIN | POLLHUP | POLLERR
		},
		// {
			// .fd = ctx.notify_fd,
			// .events = POLLIN | POLLERR /* | POLLNVAL */
		// },
	};
	int num_fds = (ctx.sock_fd > STDERR_FILENO) ? 2 : 1;

	/* Waits for something to read */
	// TODO consider using ppoll here to support a high refresh rate
	// autosplitter setting
	while((ready = poll(&fds, num_fds, -1))) {
		if (ready < 0) {
			if (errno == EINTR) {
				continue;
			}
		}
		else if (fds[0].revents & (POLLERR | POLLHUP));
		else if (fds[1].revents & (POLLERR | POLLHUP)) {
			LOG_WARN("Poll error on IPC socket, closing");

			// TODO: Another spot to consider the fd race condition
			// TODO pt2: Same logic as shutdown, maybe consolidate
			close(ctx.sock_fd);
			ctx.sock_fd = -1;
			if ((socket_path = ipc_get_socket_path())) {
				unlink(socket_path);
				free(socket_path);
			}

			break;
		}
		else {
			/* Normal path */
			break;
		}

		// poll() did not complete successfully
		status = -errno;

		// perror("poll fail");
		LOG_FATAL("Unexpected poll error in main event loop");
		return status;
	}
	
	if (fds[0].revents & POLLIN) { /* PIPE */
		event = handle_pipe_fd(fds[0].fd);
	}
	else if (fds[1].revents & POLLIN) { /* IPC SOCK */
		event = handle_sock_fd(fds[1].fd);
	}

	/* TODO: aaaa this is not going to work with the dynamic array. I think
	 * the correct answer is to force a known size maybe? I could also do a
	 * realloc, but that means that main needs to keep track of how big this
	 * is and ugh that's a mess. */
	if (event) {
		// memcpy(cmd, event, sizeof(app_cmd_t) + event->buf_len);
		memcpy(cmd, event, sizeof(app_cmd_t)); // partial for now, see above
		return (int) event->op;
	}

	return 0;
}

void event_shutdown(void)
{
	char * socket_path;

	// SET_EVENT(notify_fd);

	// TODO: Consider protecting this with a mutex like we have for other 
	// file descriptors. The challenge is that I want to avoid threads blocking
	// because the send call is wrapped in a mutex block, just for this close
	// routine.
	if (ctx.sock_fd > STDERR_FILENO) {
		close(ctx.sock_fd);
		ctx.sock_fd = -1;

		if ((socket_path = ipc_get_socket_path())) {
			unlink(socket_path);
			free(socket_path);
		}
	}

	/* NOTE: The write end still needs to be closed by whatever holds it. */
	if (ctx.pipe_fd > STDERR_FILENO) {
		close(ctx.pipe_fd);
	}
}
