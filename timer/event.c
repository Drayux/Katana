/*
 * This is a many to one model. The main thread is the only receiver.
 * When it receives a shutdown signal, it will exit its primary loop and
 * subsequently invoke the shutdown process for its child threads.
 */

#include "event.h"

#include "environment.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <pthread.h>
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

static struct {
	int pipe_fd; // listen
	int sock_fd; // listen
	int event_fd; // write
} ctx;

static event_t * read_event(int recv_fd);
static event_t * read_ipc_msg(int recv_fd);
static event_t * handle_socket(int sock_fd);

static event_t * read_event(int recv_fd)
{
	static event_t ev_buf;

	ssize_t bytes_read = 0;
	ssize_t total_read = 0;

	/* TODO: While functional, I don't love that I did this differently than
	 * the IPC version. This one totals the entire struct, whereas IPC totals
	 * only the payload. */
	while (total_read < sizeof(event_t)) {
		bytes_read = read(recv_fd, (uint8_t *) &ev_buf, sizeof(event_t) - total_read);
		if (!bytes_read) {
			LOG_ERROR("Received app event with no data");
			return NULL;
		}
		else if (bytes_read < 0) {
			switch (errno) {
				case EINTR:
				case EAGAIN:
					continue;
			}
			break;
		}
		total_read += bytes_read;
	}

	if (total_read != sizeof(event_t)) {
		LOG_ERRORF("Incomplete event: "
			"%ld bytes read does not match %ld expected",
			total_read, sizeof(event_t));
		ev_buf.payload = NULL; // better to risk a memory leak than a segfault
		ev_buf.len = -1; // indicates an error occurred
	}

	return &ev_buf;
}

/* Read an IPC message from an FD, convert into an event_t. */
/* 
 * TODO: This is the method that would detect "status request" commands, which
 * necessitate a reply. I have not yet decided on this implementation! I think
 * I'd prefer to have a dedicated "IPC send thread" to which such replies may
 * be deferred.
 */
static event_t * read_ipc_msg(int recv_fd)
{
	static ipc_msg_t cmd_buf;
	static event_t ev_buf;

	uint8_t * payload_buf = NULL;
	ssize_t payload_len = 0;

	ssize_t bytes_read = 0;
	ssize_t total_read = 0;

	bytes_read = read(recv_fd, (uint8_t *) &cmd_buf, sizeof(ipc_msg_t));
	if (!bytes_read) {
		/* connection closed immediately */
		LOG_DEBUG("File closed before read");
		return NULL;
	}
	else if (bytes_read < 0) {
		switch (errno) {
			case EINTR:
			case EAGAIN:
				/* Try again */
				// TOOD: Hopefully this becomes a stack-friendly tail recursion
				return read_ipc_msg(recv_fd);
		}
		// perror("read");
		LOG_DEBUG("Unexpected error during IPC read");
		return NULL;
	}

	payload_len = (ssize_t) ntohs(cmd_buf.len);

	do {
		if (!payload_len) {
			break;
		}
		else if (payload_len > KATANA_IPC_MAX_PAYLOAD) {
			LOG_WARNF("IPC reported payload %ld exceeds maximum %u",
				payload_len, KATANA_IPC_MAX_PAYLOAD);
			payload_len = -1;
			break;
		}

		payload_buf = malloc(payload_len);
		if (!payload_buf) {
			LOG_ERROR("No memory for IPC payload");
			break;
		}

		while (total_read < payload_len) {
			// TODO: I probably want the same error checking above?
			bytes_read = read(recv_fd, payload_buf, payload_len - total_read);
			if (!bytes_read) {
				break; // inner
			}
			else if (bytes_read < 0) {
				switch (errno) {
					case EINTR:
					case EAGAIN:
						continue;
				}
				break; // inner
			}
			total_read += bytes_read;
		}
	}
	while (false);

	if (total_read != payload_len) {
		LOG_WARNF("Incomplete IPC message: "
			"%ld bytes read does not match %ld expected",
			total_read, payload_len);
		free(payload_buf);
		payload_buf = NULL;
		payload_len = -1; // indicates an error occurred
	}

	// ev_buf.timestamp = ntohll(cmd_buf.ts); // TODO!!!
	ev_buf.timestamp = cmd_buf.ts;
	ev_buf.opcode = ntohs(cmd_buf.op);
	ev_buf.len = payload_len;
	ev_buf.payload = payload_buf; // NULL OK; event_poll caller releases

	LOG_DEBUGF("New event received: %s (%ld bytes)",
		app_cmd_to_str(ev_buf.opcode), payload_len);

	return &ev_buf;
}

/* Accepts a connection and passes it to the command handler */
static event_t * handle_socket(int sock_fd)
{
	int client_fd;
	event_t * rval;

	if ((client_fd = accept(sock_fd, NULL, NULL)) <= STDERR_FILENO) {
		if (client_fd < 0) {
			// perror("accept fail");
			LOG_ERROR("Failed accept incoming IPC conenction");
		}
		else {
			LOG_ERROR("Invalid IPC client fd during accept");
		}
		return NULL;
	}

	rval = read_ipc_msg(client_fd);
	
	/* TODO: Not sure if I want this permanently, but helpful for debug */
	// NOTE: We are assuming that one IPC client fd sends exactly one command!
	char x;
	if (read(client_fd, &x, 1) > 0) {
		LOG_WARN("IPC message exceeds reported length");
	}
	/* --- */

	close(client_fd);
	return rval;
}


/* --- PUBLIC API --- */

/* Return value is the command op as an int, 0 if none but OK, negative on
 * failure. Main loop should begin shutdown immediately on the latter.
 * Former two can have any number of stages defined by the app itself (aka the
 * confirm save / confirm reload type of logic should not be handled here.`
 * This only provides the raw command.) */
int event_poll(event_t * ev_out)
{
	event_t * ev_ref = NULL;
	int status = 0;

	int ready;
	char * socket_path;

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
	// int const num_fds = (ctx.sock_fd > STDERR_FILENO) ? 2 : 1;
	int const num_fds = 1;

	/* Waits for something to read */
	// TODO consider using ppoll here to support a high refresh rate
	// autosplitter setting
	while((ready = poll(fds, num_fds, -1))) {
		if (ready < 0) {
			if (errno == EINTR) {
				continue;
			}
		}
		else if (fds[0].revents & (POLLERR | POLLHUP));
		else if (fds[1].revents & (POLLERR | POLLHUP)) {
			LOG_WARN("Poll error on IPC socket, closing");

			// TODO: Another spot to consider the fd race condition
			// TODO pt2: Same logic as shutdown function, maybe consolidate
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

		/* poll() did not complete successfully */
		status = -errno;

		// perror("poll fail");
		LOG_FATAL("Unexpected poll error in main event loop");
		return status;
	}
	
	if (fds[1].revents & POLLIN) { /* IPC SOCK */
		LOG_DEBUG("IPC socket ready");
		ev_ref = handle_socket(fds[1].fd);
	}
	else if (fds[0].revents & POLLIN) { /* PIPE */
		LOG_DEBUG("Event pipe ready");
		ev_ref = read_event(fds[0].fd);
	}

	if (ev_ref) {
		memcpy(ev_out, ev_ref, sizeof(event_t));
		return (int) ev_ref->opcode;
	}

	return (int) APP_CMD_SCAN; // 0
}

/* TODO: I copied most of this from ipc:write_all (static as of writing this.)
 * I haven't decided if and how I'd like to consolodate these, if at all. */
int event_send(event_t * ev, bool try_again)
{
	const size_t bytes_expected = sizeof(event_t);
	ssize_t bytes_sent = 0;

	/* Unless we're blocked, only make one write attempt --
	 * Events on the internal pipe should not be partial
	 * (TODO: How to better force this?) */
	// do {
		// if (bytes_sent < 0) {
			// if (errno == EAGAIN) {
				// break;
			// }
			// else if (errno == EINTR) {
				// /* Always try again if interrupted */
				// continue;
			// }
			// return -errno;
		// }
		// break;
	// } while (true);

	bytes_sent = write(ctx.event_fd, (uint8_t *) ev, bytes_expected);
	LOG_DEBUGF("Sent %lu bytes", bytes_sent); // TODO remove this maybe

	if (bytes_sent != bytes_expected) {
		LOG_ERROR("Incomplete event write");
		return 1;
	}

	return 0;
}

/* Prepare event handler loop --
 * Returns 0 on success, -errno on failure
 *
 * NOTE: Event needs no mutex because it is intended to be released in main
 * after the slave threads are joined */
int event_init(void)
{
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
		ctx.event_fd = pipe_fds[1];
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
		LOG_WARN("Failed to create IPC listener, running in standalone mode");
		return 1; // non-fatal error
	}

	return 0;
}

void event_shutdown(void)
{
	char * socket_path;

	// SET_EVENT(notify_fd);

	if (ctx.sock_fd > STDERR_FILENO) {
		close(ctx.sock_fd);
		ctx.sock_fd = -1;

		if ((socket_path = ipc_get_socket_path())) {
			unlink(socket_path);
			free(socket_path);
		}
	}

	if (ctx.pipe_fd > STDERR_FILENO) { // read end
		close(ctx.pipe_fd);
	}
	if (ctx.event_fd > STDERR_FILENO) { // write end
		close(ctx.event_fd);
	}
}
