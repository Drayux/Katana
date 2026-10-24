#include "ui.h"

#include "app_cmd.h"
#include "gui/app_window.h"
#include "gui/timer.h"

#include <gtk/gtk.h>
#include <poll.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static LSApp * g_app = NULL;
static int wakeup[2] = { -1, -1 };
static atomic_bool shutdown_requested = false;

static int run_gtk_ui(void)
{
	int status;

	g_app = ls_app_new();

	/* Blocking call */
	status = g_application_run(G_APPLICATION(g_app), 0, NULL);
	/* *** */

	g_clear_object(&g_app);
	return status;
}

/* This is the most basic of the basic UI modes.
 *
 * It simply watches stdin and maps key binds to app commands.
 * This mode exists primarily for testing purposes. */
static int run_terminal_ui(void)
{
	char buf[16];
	size_t len = 0;
	int err;

	usleep(1000); // wait a bit before showing anything (lazy fix for now)
	fputs("KATANA > ", stdout);
	fflush(stdout);

	while (true) {
		struct pollfd fds[2] = {
			{
				.fd = STDIN_FILENO,
				.events = POLLIN | POLLHUP | POLLERR
			},
			{
				.fd = wakeup[0],
				.events = POLLIN
			},
		};

		err = poll(fds, 2, -1);
		if (err < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("poll");
			break;
		}

		/* Wakeup not triggered by stdin */
		if (fds[1].revents & POLLIN) {
			printf("wake notification\n");
			if (atomic_load(&shutdown_requested)) {
				break;
			}
			else {
				fputs("KATANA > ", stdout);
				fflush(stdout);
				continue;
			}
		}

		if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
			ssize_t n = read(STDIN_FILENO, buf + len, sizeof(buf) - len);
			if (n < 0) {
				if (errno == EINTR || errno == EAGAIN) {
					continue;
				}
				perror("read");
				break;
			}
			if (n == 0) {
				fputs("(CTRL-D)\n", stdout);
				break;
			}
			len += (size_t) n;

			/* Dispatch every complete line in the buffer. */
			char * start = buf;
			char * nl;
			while ((nl = memchr(start, '\n', (size_t) (buf + len - start)))) {
				*nl = '\0';
				/* --- Now `start` holds a NUL-terminated C string */
				app_cmd_t cmd = {
					.ts = 0,
					.buf_len = 0,
				};

				if (*start == '\0') {
					cmd.op = APP_CMD_SPLIT;
					app_cmd_send_yield(&cmd);
					// sched_yield();
				}
				else if (!strcmp(start, "quit")) {
					cmd.op = APP_CMD_QUIT;
					app_cmd_send_yield(&cmd);

					// TODO: This is technically optional, this thread does
					// not need to shut itself down, however, it raises an
					// important design question!
					//
					// If something in the thread loop goes wrong and we exit,
					// if the thread is the only (perhaps only *main*) method
					// of shutdown, should this thread exiting mean that the
					// timer should always follow suit?
					//
					// The program can keep timing a run even without a UI,
					// but very few users will know this and *not* panic about
					// losing their run.

					// goto ui_shutdown;
				}
				else {
					printf("entered: %s\n", start);
				}

				/* --- */
				start = nl + 1;
			}
			len = (size_t) (buf + len - start);
			memmove(buf, start, len);

			if (len == sizeof(buf)) {
				fprintf(stderr, "line too long, discarded\n");
				len = 0;
			}
		}
	}

// ui_shutdown:
	// Only relevant if we break ourself
	atomic_store(&shutdown_requested, true);

	// Shutdown the wakeup pipe
	if (wakeup[0] > 0) {
		close(wakeup[0]);
	}
	if (wakeup[1] > 0) {
		close(wakeup[1]);
	}

	return 0;
}

static void * ui_main(void * arg)
{
	(void) run_terminal_ui();
	// (void) run_gtk_ui();

	LOG_INFO("UI thread exiting");
	return NULL;
}


/* --- */

void ui_notify(void)
{
	// TODO: Should I assert the state of the FD before trying to write?
	char x = 'x';
	(void) !write(wakeup[1], &x, 1);
}

void ui_thread_start(pthread_t * thread)
{
	if (!thread) {
		LOG_ERR("UI thread is NULL");
		return;
	}
	pthread_create(thread, NULL, &ui_main, NULL);
}

void ui_thread_shutdown(void)
{
	if (!atomic_load(&shutdown_requested)) {
		atomic_store(&shutdown_requested, true);

		// TODO: Possible resource race condition, this could be called after
		// the UI thread starts shutting itself down (from the handler loop.)
		// - < User sends `quit` >
		// - Enter this if-block
		// - Other thread wakes to receive `quit`
		// - Other thread breaks, closes pipe FD
		// - This thread resumes, calls notify on closed FD
		ui_notify();
	}
}
