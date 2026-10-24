#include "app_cmd.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

/*
 * This is a many to one model. The main thread is the only receiver.
 * When it receives a shutdown signal, it will exit its primary loop and
 * subsequently invoke the shutdown process for its child threads.
 */

static union {
	byte b[4096]; // force PIPE_BUF bytes for static cmd_buf
	app_cmd_t t;
} cmd_buf;

// FOR NEXT TIME!
// Right now I am working on an event loop. The vision is a loop in main
// that returns 0 if a shutdown was received. Otherwise, perform the main
// logic, to be defined in the new timer.c. This will be a big state machine
// depending on the state of the timer. It will respond to events passed in
// from main to determine the action to perform.
//
// It's likely to take a bit of hammering, but I should eventually be able to
// merge the autosplitter logic and the core timing, as the autosplitter
// can also resolve these events.
//
// The mystery pieces are:
// - Clean ways to do the refresh rate -- maybe poll with the timeout?
// - Handling the unloaded state / how to load splits at all
//   ^^ i.e., is it intuitive to have a filepath passed as event data??
// - Not a mystery, but recall that the gui trivially can compute what time
//   to display every frame


/* Nonblocking pipe */
static struct {
	bool initialized;
    int readfd;
	int writefd;
} event = { 0 };

/* --- Internal context management --- */

// returns 0 on success
static int app_cmd_init(void)
{
    int p[2];

    if (pipe2(p, O_NONBLOCK | O_CLOEXEC) == 0) {
		event.readfd = p[0];
		event.writefd = p[1];
		event.initialized = true;
		return 0;
	}
	else {
		perror("pipe2 fail");
		return -errno;
	}
}

/* --- Public interfaces --- */

ssize_t app_cmd_send(app_cmd_t * send_cmd, bool try_again)
{
	size_t send_len;
	ssize_t written;

	// TODO: Not sure yet how to handle events being sent before we've
	// initalized
	// Similarly, I'm not sure threads will "give up" correctly when closed,
	// either.
	if (!event.initialized) {
		LOG_ERROR("Main loop not initialized; ignoring app cmd");
		return 0;
	}

	if (!send_cmd) {
		LOG_ERROR("Send NULl app command");
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

// return value is valid only until app_cmd_poll is called again
app_cmd_t * app_cmd_poll(void) {
	struct pollfd pfd = {
		.fd = event.readfd,
		.events = POLLIN
	};
	int err;
	ssize_t len;

	if (!event.initialized) {
		if (app_cmd_init()) {
			// syscall failure
			return NULL;
		}
	}

	/* Waits for something to read */
	// TODO consider using ppoll here to support a high refresh rate
	// autosplitter setting
	while((err = poll(&pfd, 1, -1))) {
		if (err == -1) {
			if (errno == EINTR) {
				continue;
			}
		}
		else if (pfd.revents & (POLLERR | POLLNVAL));
		else if (pfd.revents & POLLIN) {
			/* Success path */
			break;
		}

		// poll() did not complete successfully
		perror("poll fail");
		return NULL; // TODO: maybe `goto shutdown` instead?
	}

	/* Reads the something when there is something */
	while (true) {
		len = read(event.readfd, cmd_buf.b, sizeof(cmd_buf.b));
		if (len == -1) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN) {
				/* Pipe was empty, try again from the start */
				// TOOD: Hopefully this becomes a stack-friendly tail recursion
				return app_cmd_poll();
			}
			perror("read fail");
			return NULL; // TODO: maybe `goto shutdown` instead?
		}
		else if (len == 0) {
			perror("read fail (fd closed)");
			return NULL; // TODO: maybe `goto shutdown` instead?
		}

		// Assert the number of bytes read
		if (len != sizeof(app_cmd_t) + cmd_buf.t.buf_len) {
			LOG_ERRORF(
				"Invalid cmd read len: %d read does not match %u reported",
				len,
				cmd_buf.t.buf_len
			);
		}
		// loop is only here so we can try again if interrupted
		break;
	}
	
	return &cmd_buf.t;
}
