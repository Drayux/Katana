#include "common.h"

#include "app_cmd.h"
#include "ui.h"

#include <curses.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
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
	SCREEN * screen;

	WINDOW * log_bar;
	WINDOW * log_window;
	WINDOW * timer_bar;
	WINDOW * timer_window;
	WINDOW * input_line;

	// int stdout_pipe[2];
	int stderr_pipe[2];
} ctx = { 0 };
 
static void _bar(WINDOW * bar_win, const char *title)
{
	werase(bar_win);
	wattron(bar_win, A_REVERSE);
	mvwprintw(bar_win, 0, 0, " %-*s", getmaxx(bar_win) - 1, title);
	wattroff(bar_win, A_REVERSE);
	wnoutrefresh(bar_win);
}

static void _layout(void)
{
	if (ctx.log_bar) delwin(ctx.log_bar);
	if (ctx.log_window) delwin(ctx.log_window);
	if (ctx.timer_bar) delwin(ctx.timer_bar);
	if (ctx.timer_window) delwin(ctx.timer_window);
	if (ctx.input_line) delwin(ctx.input_line);

	int rows = LINES;
	int cols = COLS;

	int body_height = (rows > 4) ? rows - 3 : 2; /* 2 title bars + 1 input line */
	int log_height = body_height / 2;
	int timer_height = body_height - log_height;

	ctx.log_bar = newwin(1, cols, 0, 0);
	ctx.log_window = newwin(log_height, cols, 1, 0);
	ctx.timer_bar = newwin(1, cols, 1 + log_height, 0);
	ctx.timer_window = newwin(timer_height, cols, 2 + log_height, 0);
	ctx.input_line = newwin(1, cols, 2 + log_height + timer_height, 0);

	scrollok(ctx.log_window, TRUE);
	scrollok(ctx.log_window, TRUE);
	keypad(ctx.input_line, TRUE);
	nodelay(ctx.input_line, TRUE);

	_bar(ctx.log_bar, "stderr (log)");
	_bar(ctx.timer_bar, "stdout / input");

	wnoutrefresh(ctx.log_window);
	wnoutrefresh(ctx.log_window);
}

static void _redraw(char * input)
{
    werase(ctx.input_line);
    mvwprintw(ctx.input_line, 0, 0, " > %.*s", 10, input ? input : "-");
    wnoutrefresh(ctx.input_line);
}

static int tui_init(void)
{
	// int ctx.stdout_pipe[2];
	// if (pipe(ctx.stdout_pipe) < 0 || pipe(ctx.stderr_pipe) < 0) {

	if (pipe(ctx.stderr_pipe) < 0) {
        return -1;
	}

	setlocale(LC_ALL, "");

	fflush(stdout);
	fflush(stderr);

	// int saved_out = dup(STDOUT_FILENO);
	// int saved_err = dup(STDERR_FILENO);
	// if (saved_out < 0 || saved_err < 0 || !tty) {

	FILE * tty = fdopen(dup(STDOUT_FILENO), "w");
	if (!tty) {
		// TODO: actually clean these up properly
		// ^^ I probably need to use that saved_out var for cleanup
		printf("couldn't prepare the curses env\n");
		return -1;
	}

	ctx.screen = newterm(NULL, tty, stdin);
	if (!ctx.screen) {
		printf("couldn't open the curses screen\n");
		return -1;
	}

	set_term(ctx.screen);
	cbreak();
	noecho();

	curs_set(1);
	_layout();

	// dup2(stdout_pipe[1], STDOUT_FILENO);
	dup2(ctx.stderr_pipe[1], STDERR_FILENO);
	setvbuf(stdout, NULL, _IOLBF, 0);

	return 0;
}

static int tui_release(void)
{
	return 0;
}

static int tui_main(int notify_fd)
{
	// NOTE: I'm having a tough time designing the hierarcy of order here.
	// For now, be sure to exit if notify_fd throws an error so we don't listen
	// on a stale FD. That said, I have run with an fd and yet a dedicated
	// .notify handler, so we could just create and store that internally?

	ssize_t read_len;
	char input;
	int ready;

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

	_redraw(NULL);
	doupdate();

	while (true) {
		ready = poll(fds, 3, -1);
		if (ready < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("poll");
			break;
		}
		if (fds[2].revents & (POLLHUP | POLLERR)) {
			/* Kill the UI if stdin closes or errors, it would be unresponsive */
			perror("read");
			// printf("poll: in %d, hup %d, err %d\n",
				// !!(fds[1].revents & POLLIN),
				// !!(fds[1].revents & POLLHUP),
				// !!(fds[1].revents & POLLERR));
			break;
		}
		/* Check for a wakeup event (not stdin) */
		if (fds[0].revents & POLLIN) {
			CLEAR_EVENT(fds[0].fd);
			// if (atomic_load(&shutdown_requested)) {
				// break;
			// }
			// LOG_DEBUG("UI wake but nothing to do");
			// continue; // not necessary, just here for testing the logic
			break;
		}

		/* Grab any new log output */
		if (fds[1].revents & POLLIN) {
			char log_buf[1024];
			ssize_t len;

			while ((len = read(fds[1].fd, log_buf, sizeof(log_buf))) > 0) {
				waddnstr(ctx.log_window, log_buf, len);
			}
			wnoutrefresh(ctx.log_window);
			doupdate();
		}

		/* Read data from stdin */
		if (fds[2].revents & POLLIN) {
			/* Every UI interaction is 1 byte */
			while ((read_len = read(STDIN_FILENO, &input, 1))) {
				if (read_len < 0) {
					switch (errno) {
						default:
							perror("read");
							// fallthrough
						case EAGAIN:
							// TODO: maybe `goto ui_shutdown;`
							break;

						case EINTR:
							/* OK - try again */
							continue;
					}
					break; /* inner loop */
				}
				char buf[2] = { 0 };
				buf[0] = input;

				LOG_INFOF("Received char: %s", buf);
				// printf("Received char: %s\n", buf);
				_redraw(buf);
				doupdate();
			}

			if (!read_len) {
				/* empty, CTRL-D or something like that */
				// ui_terminal_step(NULL);
				// TODO: There's a chance CTRL-Z will kill this maybe?
				LOG_INFO("Received EOF, exiting UI loop");
				break;
			}
		}
	}

// ui_shutdown:
	// atomic_store(&shutdown_requested, true);
	return 0;
}

static int tui_shutdown(void)
{
	return 0;
}

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
