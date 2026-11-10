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
static int tui_main(int listen_fd);
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

typedef enum {
	// TODO: Hidden by default
	INFO_VIEW_HELP = 1,
	INFO_VIEW_LOG,
	INFO_VIEW_DEBUG,
} info_view_t;

static struct {
	atomic_bool active;

	int stderr_pipe[2];

	WINDOW * timer_title;
	WINDOW * timer_window;
	WINDOW * info_title;
	WINDOW * info_window;
	WINDOW * status;

	info_view_t info_view_setting;
} ctx = { 0 };

/* -- Component helpers -- */
static void title_set_content(WINDOW * bar_win, const char * title);
static void info_redraw(WINDOW * info_title, WINDOW * info_window);
static void status_redraw(WINDOW * status_window, char * input);
 
static void layout(void)
{
	if (ctx.info_title) delwin(ctx.info_title);
	if (ctx.info_window) delwin(ctx.info_window);
	if (ctx.timer_title) delwin(ctx.timer_title);
	if (ctx.timer_window) delwin(ctx.timer_window);
	if (ctx.status) delwin(ctx.status);

	/* --- */

	int const rows = LINES;
	int const cols = COLS;

	int const body_height = (rows > 4) ? rows - 3 : 2;
	int const info_height = body_height / 3;
	int const timer_height = body_height - info_height; // TODO: can be zero, make log hidden by default

	/* --- */

	ctx.timer_title = newwin(1, cols, 1 + info_height, 0);
	title_set_content(ctx.timer_title, "KATANA");

	ctx.timer_window = newwin(timer_height, cols, 2 + info_height, 0);
    scrollok(ctx.timer_window, TRUE);

	ctx.info_title = newwin(1, cols, 0, 0);
	ctx.info_window = newwin(info_height, cols, 1, 0);
	info_redraw(ctx.info_title, ctx.info_window);

	ctx.status = newwin(1, cols, 2 + info_height + timer_height, 0);
    keypad(ctx.status, TRUE);
    nodelay(ctx.status, TRUE); /* set non-blocking */
	status_redraw(ctx.status, NULL);
}

static void title_set_content(WINDOW * bar_win, const char * title)
{
	werase(bar_win);
	wattron(bar_win, A_REVERSE);
	mvwprintw(bar_win, 0, 0, " %-*s", getmaxx(bar_win) - 1, title);
	wattroff(bar_win, A_REVERSE);
	wnoutrefresh(bar_win);
}

static void help_draw_all(WINDOW * info_window)
{
	// TODO: Ideally this could be parsed rather than hard-coded
	char const * help_str = "Press a key to invoke the corresponding command:\n"
		" > q  --  Quit\n"
		" > h  --  Show help pane (this!)\n"
		" > [SPACE]  --  Start / split timer\n\n"
		"   --  (debug) --\n"
		" > l  --  Show log output\n"
		" > [ESC]  --  Log something\n"
		;

    werase(info_window);
	waddnstr(info_window, help_str, strlen(help_str));
	wnoutrefresh(info_window);
}


static void log_draw_all(WINDOW * info_window)
{
	// char ** sb = logging_get_scrollback(-LINES); // TODO pull this from ctx maybe
	char ** sb = logging_get_scrollback(-LINES);
	char * line;

    werase(info_window);

	if (!sb) {
		line = "< Could not get log scrollback >";
		waddnstr(info_window, line, strlen(line));
		return;
	}

	for (int i = 0; i < LINES; ++i) {
		line = sb[i];
		if (!line) {
			break;
		}
		waddnstr(info_window, line, strlen(line));
	}

	free(sb);
	wnoutrefresh(info_window);
}
static void log_append(char * msg, ssize_t len)
{
	if (ctx.info_view_setting == INFO_VIEW_LOG) {
		if (msg && len > 0) {
			waddnstr(ctx.info_window, msg, len);
		}
		wnoutrefresh(ctx.info_window);
	}
}

// reminder that we pass in params so I can populate a new info window before
// actually using it
static void info_redraw(WINDOW * info_title, WINDOW * info_window)
{
	char const * new_title = "UNKNOWN";

	switch (ctx.info_view_setting) {
		case INFO_VIEW_HELP:
			help_draw_all(info_window);

			new_title = "HELP";
			break;
		case INFO_VIEW_LOG:
			scrollok(info_window, TRUE);

			/* TODO: Don't quite love this passing, it's a bit inconsistent
			 * with what is responsible for triggering the refresh */
			log_draw_all(info_window);

			new_title = "DEBUG LOG";
			break;
		case INFO_VIEW_DEBUG:
			new_title = "DEBUG INFO";
			break;
	}
	/* ....though, it is consistent with this interface */
	title_set_content(info_title, new_title);
}

/* TODO: Not 100% sure on the exchange of data, but there will likely be a form
 * of "status/state" structure made available by the timer. Perhaps it's best
 * to receive this structure and parse accordingly.
 *
 * Question is if I want this function to do the parsing, or just render
 * whatever I send it. I overthink these questions..... */
static void status_redraw(WINDOW * status_window, char * input)
{
    werase(status_window);
    mvwprintw(status_window, 0, 0, "KATANA > %s", input ? input : "");
    wnoutrefresh(status_window);
}

/* --- */

static int handle_wake(void)
{
	if (!atomic_load(&ctx.active)) {
		/* Shutdown started */
		return -1;
	}

	// NOTE this will become very common but it's weird at the present
	LOG_DEBUG("UI wake but nothing to do");

	return 0;
}

/* Return non-zero if the UI should stop (due to an error or user request) */
static int handle_key(int x)
{
	// TODO: This *can't* be permanent
	static app_cmd_t cmd;

	switch (x) {
		case KEY_RESIZE:
			LOG_DEBUG("Invoke layout resize");
			layout();
			break;

		case ' ':
		case '\n':
		case '\r':
		case KEY_ENTER:
			LOG_DEBUG("Invoke split");

			memset(&cmd, 0, sizeof(app_cmd_t));
			cmd.op = APP_CMD_SPLIT;
			app_cmd_send_yield(&cmd);
			break;

		case '\b':
		case 127:
		case KEY_BACKSPACE:
			LOG_DEBUG("Invoke <backspace action>");
			break;

			
		case 'h':
			LOG_DEBUG("Invoke help pane");
			ctx.info_view_setting = INFO_VIEW_HELP;
			info_redraw(ctx.info_title, ctx.info_window);
			break;

		case 'l':
			LOG_DEBUG("Invoke debug log pane");
			ctx.info_view_setting = INFO_VIEW_LOG;
			info_redraw(ctx.info_title, ctx.info_window);
			break;

		case 'q':
			LOG_DEBUG("Invoke quit action");

			memset(&cmd, 0, sizeof(app_cmd_t));
			cmd.op = APP_CMD_QUIT;
			app_cmd_send_yield(&cmd);
			break;

			/* NOTE / TODO: My original code was to hint to the UI early that
			 * it should exit. However, it is up to main to determeine if we
			 * should actually quit when receiving such a command. Thus, the
			 * UI thread will also be waiting for a formal shutdown
			 * notification, even if it is responsible for the command being
			 * received in the first place. */
			// return 1;
	}
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
    initscr();
    cbreak();
    noecho();

	ctx.info_view_setting = INFO_VIEW_LOG; // default for now

	layout();

	return 0;
}

static int tui_release(void)
{
	return 0;
}

/* NOTE: I'm having a tough time designing the hierarcy of order here.
 * For now, be sure to exit if notify_fd throws an error so we don't listen
 * on a stale FD. That said, I have .run with an fd and yet a dedicated
 * .notify handler, so we could just create and store that internally?
 *
 * TODO: Not sure what to do with the return value. For now, it'll return the
 * error code or 0 if everything seemed to run as expected. Entertaining the
 * idea of returning non-zero if the UI should signal program exit, or perhaps
 * if the UI should be restarted. (Maybe positive for one, negative for the
 * other??)
 */
static int tui_main(int listen_fd)
{
	int exit_status = 0;
	int ready;

	struct pollfd fds[] = {
		{
			.fd = listen_fd,
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
        doupdate();

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
			exit_status = errno;
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
			ssize_t len = read(fds[1].fd, log_buf, sizeof(log_buf));
			log_append(log_buf, len);

			// NOTE / TODO: I could add a status_redraw() here so the cursor
			// moves back to the bottom. Unimportant for now. See below.
		}

		/* Read data from stdin */
        int ch;
		int rval;
        while ((ch = wgetch(ctx.status)) != ERR) {
			rval = handle_key(ch);
			if (rval) {
				exit_status = (rval < 0) ? -1 : exit_status;
				goto shutdown;
			}
		}

		/* TODO: Tentative consdiering for the future making this a timeout poll
		 * Thus, the timer window and status line would be updated at whatever
		 * interval the timeout is for the "live" aspect of the timer.
		 *
		 * This status_redraw() call works in the meantime to force the cursor
		 * to the bottom, but it would become even more useful and deliberate
		 * in this final form, which would display the timer internal state. */

		// TODO this is just for debugging
		char buf[2] = { 0 };
		if (ch >= 32 && ch < 127) {
			buf[0] = ch;
		} else {
			buf[0] = '\0';
		}
		/////

		status_redraw(ctx.status, buf);
	}

shutdown:
	atomic_store(&ctx.active, false);

	/* NOTE: If we get here and the shutdown command has not already been
	 * invoked, then something went wrong. Thus, we keep running on the chance
	 * it helps debugging. The general design rule is "all threads will
	 * continue running until something tells them to stop or they encounter
	 * an error of their own." Currently, the user pressing Q (or X on gtk) is
	 * the only such invocation of the the former rule.
	 *
	 * TODO: This may prove to be unideal decision. If so, return nonzero and
	 * defer the app command to the base UI thread. */

    endwin();
	/* ^^ TODO after the UI closes, it might be worthwhile to dump any output
	 * log back to the terminal. If the scrollback is full, a
	 *  --- LOG TRUNCATED DURING TUI ---
	 * line would be acceptable before dumping what remains. */

	return exit_status;
}

static int tui_shutdown(void)
{
	atomic_store(&ctx.active, false);
	return 0;
}
