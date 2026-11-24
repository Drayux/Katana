/*
 * This is a many to one model. The main thread is the only receiver.
 * When it receives a shutdown signal, it will exit its primary loop and
 * subsequently invoke the shutdown process for its child threads.
 */

#include "event.h"

#include "environment.h"
#include "io_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
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

/* Initialization protection as it is possible that a publisher thread is fully
 * initialized before the event consumer loop. */
static atomic_bool ready = false;
static pthread_rwlock_t fd_lock = PTHREAD_RWLOCK_INITIALIZER;

static event_t * read_event(int recv_fd); /* message handler for event pipe */
static int read_ipc_msg(ipc_msg_t * msg_ref, int send_fd); /* message handler for ipc sock */



/* FOR NEXT TIME!
 * - My order of development will be to "perfect" the event sending in the
 *   current (event pipe) form. 
 * - After this, I will implement the event queue version instead -- I had an
 * unfortunately-timed epiphany that short blocks via mutexes are no longer
 * nearly as important with my new design. Of course, we don't want to miss
 * an autosplitter scan, but it is allowable that the threads block for short
 * atomic sections. Especially when we're going from no action -> some action
 * at all, main was blocked on the poll anyway.
 * - Finally, I will implement the new version of the IPC handler (function
 *   skeleton in ipc_common.c) which accepts a connection in a detached thread.
 *   It will read in ipc_msg_t formatted messages and convert them into event_t
 *   structs and forward that to the event queue with the new methods in part 2
 *
 * For part one, I'm seeking to solve the case of the payload pointer. Few
 * internal events will need this (if any??) and it seems senseless to malloc
 * and free each time if it's a regular or semi-regular interval. Thus, I'm
 * considering an "event heap" of sorts in this file. Instead of a pointer,
 * an event would get a heap index. This heap would be reused as messages are
 * consumed. Because we'll use a block, we can also increment events
 * sequentially. Unordered events may be dropped, and heap indexes less than the
 * current can be "garbage collected" if we determine the event was dropped.
 */



/* Send an event via the internal pipe, can be called by any thread, thus
 * the event fd needs to be protected with a lock. */
// TODO: payload support
int event_send(event_t * ev, bool try_again)
{
	const size_t len = sizeof(event_t);
	int bytes_remaining;

	if (!(atomic_load(&ready))) {
		LOG_WARN("drop event: fd not ready");
		return -1;
	}

	pthread_rwlock_rdlock(&fd_lock); // ensure event_fd is valid
	bytes_remaining = io_write_msg(ctx.event_fd, ev, len, try_again);
	pthread_rwlock_unlock(&fd_lock);
	if (bytes_remaining) {
		LOG_ERRORF("incomplete event write: %s", app_cmd_to_str(ev->opcode));
	}

	return bytes_remaining;
}

/* --- */


/* Return value is the command op as an int, 0 if none but OK, negative on
 * failure. Main loop should begin shutdown immediately on the latter.
 * Former two can have any number of stages defined by the app itself (aka the
 * confirm save / confirm reload type of logic is not handled here.
 * This only provides the raw command.) */
int event_poll(event_t * ev_out)
{
	event_t * ev_ref = NULL;
	int status = 0;

	int ready;
	char * socket_path;

	struct pollfd fds[] = {
		// { .fd = ctx.notify_fd, .events = POLLIN },
		{
			.fd = ctx.pipe_fd,
			.events = POLLIN
		},
		{
			.fd = ctx.sock_fd,
			.events = POLLIN
		},
	};
	// int const num_fds = (ctx.sock_fd > STDERR_FILENO) ? 2 : 1;
	int const num_fds = 1; // TODO: ipc disabled for now

	/* Waits for something to read */
	// TODO consider using ppoll here to support a high refresh rate
	// autosplitter setting
	while (true) {
		ready = poll(fds, num_fds, -1);
		if (ready < 0) {
			if (errno != EINTR) {
				status = -errno;
				LOG_FATAL("unexpected poll error in main event loop");
				return status; // TODO new return value format
			}
			continue;
		}
		else if (fds[1].revents & (POLLERR | POLLHUP)) {
			LOG_WARN("poll error on IPC socket: closing");
			// pthread_rwlock_wrlock(&fd_lock); // NOTE: Currently not needed
			//                                     only accessed by polling thread
			DESTROY_FD(ctx.sock_fd);
			ipc_unlink_socket_path();
			//
			// pthread_rwlock_unlock(&fd_lock);
			continue;
		}
		break;
	}

	// TODO: On the case an internal and ipc event both arrive simultaneously,
	// I cannot yet assert what the best behavior would be. For now, every
	// event will be processed, but it may be prudent to drop those of
	// lower-priority.

	/* IPC SOCK */
	if (fds[1].revents) {
		if (fds[1].revents & POLLIN) {
			// ev_ref = ipc_handle(fds[1].fd, read_ipc_message);
			LOG_WARN("Skipped IPC event while it's under construction");
		}
		else {
			// I don't expect to get here, but it seemed prudent to track
			// so that I might correct it later if I do (TODO)
			LOG_ERRORF("unhandled IPC revent: %u", fds[1].revents);
			DESTROY_FD(ctx.sock_fd);
			ipc_unlink_socket_path();
			return (int) APP_CMD_ERROR; // -1
		}
	}
	/* EV PIPE */
	else if (fds[0].revents) {
		if (fds[0].revents & POLLIN) {
			ev_ref = read_event(fds[0].fd);
		}
		else {
			LOG_FATAL("broken event pipe");
			DESTROY_FD(ctx.pipe_fd);

			pthread_rwlock_wrlock(&fd_lock);
			//
			DESTROY_FD(ctx.event_fd);
			//
			pthread_rwlock_unlock(&fd_lock);
			return (int) APP_CMD_ERROR; // -1
		}
	}
	/* TIMEOUT */
	// else {  }

	if (ev_ref) {
		memcpy(ev_out, ev_ref, sizeof(event_t));
		return (int) ev_ref->opcode;
	}

	return (int) APP_CMD_SCAN; // 0
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
		LOG_FATAL("pipe failed: could not create app event handler");
		return -errno; // TODO: New error format
	}

	ipc_server_fd = ipc_listen();
	if (ipc_server_fd > STDERR_FILENO) {
		ctx.sock_fd = ipc_server_fd;
	}
	else {
		LOG_WARN("Failed to create IPC listener, running in standalone mode");
		return 1; // non-fatal error (also TODO new error format)
	}

	atomic_store(&ready, true);

	return 0;
}

void event_shutdown(void)
{
	// SET_EVENT(notify_fd);

	pthread_rwlock_wrlock(&fd_lock);
	//
	DESTROY_FD(ctx.event_fd); // event pipe write end
	//
	pthread_rwlock_unlock(&fd_lock);

	/* NOTE: Currently only the polling thread references or knows about these
	 * FDs. Thus, the lock isn't necessary for these. */
	DESTROY_FD(ctx.pipe_fd); // event pipe read end
	DESTROY_FD(ctx.sock_fd);
	ipc_unlink_socket_path();
}

/* -- INTERNAL -- */

// TODO: Payload support
static event_t * read_event(int recv_fd)
{
	static io_buf_t * ev_buf = NULL;
	static event_t new_ev;

	int len; // len because we have no on_message CB to io_read()

	// TODO: This won't be freed at program exit in the current form. Clean up
	// after testing (or refactoring to the queue system.)
	if (!ev_buf) {
		ev_buf = io_buf_create(sizeof(event_t));
	}

	/* Prefer io_read as pipe writes are atomic under PIPE_LEN bytes */
	len = io_read(recv_fd, ev_buf, NULL, NULL);
	if (len < 0) {
		// error logged by internal func
		return NULL;
		// ^^ TODO: Maybe return error via the event itself?
	}

	if (len < sizeof(event_t)) {
		LOG_ERRORF("drop partial app event: " \
			"%ld bytes read does not match %ld expected",
			len, sizeof(event_t));
		return NULL;
	}
	else if (len > sizeof(event_t)) {
		LOG_ERROR("received event larger than expected");
		// keep going
	}

	memcpy(&new_ev, ev_buf->_buf, sizeof(event_t));
	ev_buf->filled = 0;

	// TODO: This value has a dubiously limited lifetime (currently static)
	return &new_ev;
}

// NEW STUFF! This should become the callback routine used by the handler
/* Read an IPC message from an FD, convert into an event_t. */
static int read_ipc_msg(ipc_msg_t * msg_ref, int send_fd)
{
	static ipc_msg_t cmd_buf;
	static event_t ev_buf;

	uint8_t * payload_buf = NULL;
	ssize_t payload_len = 0;

	ssize_t bytes_read = 0;
	ssize_t total_read = 0;

	// bytes_read = read(recv_fd, (uint8_t *) &cmd_buf, sizeof(ipc_msg_t));
	if (!bytes_read) {
		/* connection closed immediately */
		LOG_DEBUG("File closed before read");
		return -1;
	}
	else if (bytes_read < 0) {
		switch (errno) {
			case EINTR:
			case EAGAIN:
				/* Try again */
				// TOOD: Hopefully this becomes a stack-friendly tail recursion
				// return read_ipc_msg(recv_fd);
				return -1;
		}
		// perror("read");
		LOG_DEBUG("Unexpected error during IPC read");
		return -1;
	}

	// payload_len = (ssize_t) ntohs(cmd_buf.len);
	payload_len = 0;

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
			// bytes_read = read(recv_fd, payload_buf, payload_len - total_read);
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

	return 0;
}
