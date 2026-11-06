#include "common.h"

#include "app_cmd.h"
#include "ui.h"

#include <curses.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* TODO:
 * This is a bit of a dummy implementation of a UI in the current state.
 *
 * End goal, abstract UIs to a plugin. Each plugin would include the ui.h
 * header, which should provide a "UI struct" of sorts, enumerating function
 * pointers akin to the following.
 *
 * Because of the enriched IPC, the timer can be ran completely headless. This
 * is the reason for making UIs entirely optional.
 */

static int tui_init(void);
static int tui_release(void);
static int tui_main(int notify_fd);
// static int tui_notify(void);
static int tui_shutdown(void);

/* --- EXPORTED --- */
frontend_t const tui_frontend = {
	.prepare = tui_init,
	.destroy = tui_release,
	.run = tui_main,
	// .notify = tui_notify,
	.stop = tui_shutdown,
};
/* --- */


/* --- INTERNAL --- */

static struct {
	WINDOW * timer_title;
	WINDOW * timer_window;
	WINDOW * log_title;
	WINDOW * log_window;
	WINDOW * status;

	int stderr_pipe[2];
	atomic_bool active;
} ctx = { 0 };

/* -- Component helpers -- */
static void title_set_content(WINDOW * bar_win, const char * title);
 
static void layout(void)
{
	if (ctx.log_title) delwin(ctx.log_title);
	if (ctx.log_window) delwin(ctx.log_window);
	if (ctx.timer_title) delwin(ctx.timer_title);
	if (ctx.timer_window) delwin(ctx.timer_window);
	if (ctx.status) delwin(ctx.status);

	/* --- */

	int const rows = LINES;
	int const cols = COLS;

	int const body_height = (rows > 4) ? rows - 3 : 2;
	int const log_height = body_height / 3;
	int const timer_height = body_height - log_height; // TODO: can be zero, make log hidden by default

	/* --- */

	ctx.timer_title = newwin(1, cols, 1 + log_height, 0);
	title_set_content(ctx.timer_title, "KATANA");

	ctx.timer_window = newwin(timer_height, cols, 2 + log_height, 0);
    scrollok(ctx.timer_window, TRUE);

	ctx.log_title = newwin(1, cols, 0, 0);
	title_set_content(ctx.log_title, "DEBUG LOG");

	ctx.log_window = newwin(log_height, cols, 1, 0);
    scrollok(ctx.log_window, TRUE);

	ctx.status = newwin(1, cols, 2 + log_height + timer_height, 0);
    keypad(ctx.status, TRUE);
    nodelay(ctx.status, TRUE); /* set non-blocking */
}

static void title_set_content(WINDOW * bar_win, const char * title)
{
	werase(bar_win);
	wattron(bar_win, A_REVERSE);
	mvwprintw(bar_win, 0, 0, " %-*s", getmaxx(bar_win) - 1, title);
	wattroff(bar_win, A_REVERSE);
	wnoutrefresh(bar_win);
}

/* TODO: Not 100% sure on the exchange of data, but there will likely be a form
 * of "status/state" structure made available by the timer. Perhaps it's best
 * to receive this structure and parse accordingly.
 *
 * Question is if I want this function to do the parsing, or just render
 * whatever I send it. I overthink these questions..... */
static void status_redraw(char * input)
{
    werase(ctx.status);
    mvwprintw(ctx.status, 0, 0, "KATANA > %s", input ? input : "");
    wnoutrefresh(ctx.status);
}

static void log_draw_all(void)
{
	/* Half-placeholder function to get the log scrollback and draw everything */

	char ** sb = logging_get_scrollback(-20); // TODO
	char * line;

    // werase(ctx.log_window);

	for (int i = 0; i < 20; ++i) {
		line = sb[i];
		if (!line) {
			break;
		}
		// waddnstr(ctx.log_window, line, strlen(line));
		printf("log message: %s\n", line);
	}

	free(sb);
	// wnoutrefresh(ctx.log_window);
}
static void log_append(char * msg, ssize_t len)
{
	if (msg && len > 0) {
		waddnstr(ctx.log_window, msg, len);
	}
	wnoutrefresh(ctx.log_window);
}

/* --- */

static int handle_wake(void) {
	if (atomic_load(&ctx.active)) {
		return -1;
	}

	LOG_DEBUG("UI wake but nothing to do");
	return 0;
}

/* --- FRONTEND API IMPL. --- */

static int tui_init(void)
{
	setlocale(LC_ALL, "");

	if (pipe(ctx.stderr_pipe) < 0) {
        return -1;
	}
	dup2(ctx.stderr_pipe[1], STDERR_FILENO);
	// fflush(stderr);

	atomic_store(&ctx.active, true);

	// TODO: Check for errors here (probably)
    // initscr();
    // cbreak();
    // noecho();

	// layout();
	log_draw_all();
	// status_redraw(NULL);

	// doupdate();

	return 0;
}

static int tui_release(void)
{
	return 0;
}

/* NOTE: I'm having a tough time designing the hierarcy of order here.
 * For now, be sure to exit if notify_fd throws an error so we don't listen
 * on a stale FD. That said, I have .run with an fd and yet a dedicated
 * .notify handler, so we could just create and store that internally? */
static int tui_main(int notify_fd)
{
	// ssize_t read_len;
	// char input;
	int ready;
	int status = 0;

	struct pollfd fds[] = {
		{
			.fd = notify_fd,
			.events = POLLIN // TODO: Investigate canon events for something like this
		},
		{
			.fd = ctx.stderr_pipe[0],
			.events = POLLIN
		},
		{
			.fd = STDIN_FILENO,
			.events = POLLIN | POLLHUP | POLLERR
		},
	};

	while (true) {
		/* --- Blocking --- */
		if ((ready = poll(fds, 3, -1)) < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("poll");
			goto shutdown;
		}
		/* --- */
		if (fds[2].revents & (POLLHUP | POLLERR)) {
			/* Kill the UI if stdin closes or errors, it would be unresponsive */
			status = errno;
			perror("io");
			goto shutdown;
		}

		/* Check for a wakeup event (not stdin) */
		if (fds[0].revents & POLLIN) {
			CLEAR_EVENT(fds[0].fd);
			if (handle_wake() < 0) {
				goto shutdown;
			}
		}

		/* Grab any new log output */
		if (fds[1].revents & POLLIN) {
			char log_buf[1024];
			ssize_t len;

			// while ((len = read(fds[1].fd, log_buf, sizeof(log_buf))) > 0) {
				// waddnstr(ctx.log_window, log_buf, len);
			// }
			len = read(fds[1].fd, log_buf, sizeof(log_buf));
			log_append(log_buf, len);
			// log_draw_all();
		}

		/* Read data from stdin */
        int ch;
        while ((ch = wgetch(ctx.status)) != ERR) {
            switch (ch) {
				case KEY_RESIZE:
					layout();
					fprintf(stderr, "resize\n");
					break;

				case '\n':
				case '\r':
				case KEY_ENTER:
					fprintf(stderr, "newline\n");
					break;

				case '\b':
				case 127:
				case KEY_BACKSPACE:
					fprintf(stderr, "backspace\n");
					break;

				default:
					// if (ch >= 32 && ch < 127 && len < sizeof line - 1) {
						// line[len++] = ch;
						// line[len] = '\0';
					// }
					// char buf[2] = { 0 };
					// if (ch >= 32 && ch < 127 && len < sizeof line - 1) {
						// buf[0] = ch;
						// refresh_input_line(buf);
					// }
					break;
            }
        }
		char buf[2] = { 0 };
		if (ch >= 32 && ch < 127) {
			buf[0] = ch;
		} else {
			buf[0] = '\0';
		}
		status_redraw(buf); /* AKA _redraw_input */
        doupdate();

		// if (fds[2].revents & POLLIN) {
			// /* Every UI interaction is 1 byte */
			// while ((read_len = read(STDIN_FILENO, &input, 1))) {
				// if (read_len < 0) {
					// switch (errno) {
						// default:
							// perror("read");
							// fallthrough
						// case EAGAIN:
							// TODO: maybe `goto ui_shutdown;`
							// break;

						// case EINTR:
							// /* OK - try again */
							// continue;
					// }
					// break; /* inner loop */
				// }
				// char buf[2] = { 0 };
				// buf[0] = input;

				// LOG_INFOF("Received char: %s", buf);
				// printf("Received char: %s\n", buf);
				// refresh_input_line(buf); /* AKA _redraw_input */
				// doupdate();
			// }

			// if (!read_len) {
				// /* empty, CTRL-D or something like that */
				// ui_terminal_step(NULL);
				// TODO: There's a chance CTRL-Z will kill this maybe?

				// LOG_INFO("Received EOF, exiting UI loop");
				// break;

				// LOG_INFO("Received EOF");
				// continue;
			// }
		// }
	}

shutdown:
	// atomic_store(&shutdown_requested, true);
    endwin();
	return status;
}

static int tui_shutdown(void)
{
	atomic_store(&ctx.active, false);
	return 0;
}

// HOlding onto this just until I extract the app_cmd stuff
static void _old_prompt_logic(void)
{
	char buf[16] = { 0 }; // NOTE: would be populated from stdin read
	size_t len = 16; // NOTE: would be length of populated buffer
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
