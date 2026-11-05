#include "common.h"

#include "ui.h"

#include "gui/app_window.h"
#include "gui/timer.h"

#include <gtk/gtk.h>

static int create_app(void);
static int destroy_app(void);
static int run_app(int notify_fd);

/* --- EXPORTED --- */
frontend_t const livesplit_frontend = {
	.prepare = create_app,
	.destroy = destroy_app,
	.run = run_app,
	// .notify = tui_notify,
	// .stop = tui_shutdown,
};
/* --- */


/* --- INTERNAL --- */

static LSApp * g_app = NULL;

static int create_app(void)
{
	g_app = ls_app_new();
	return g_app ? 0 : -1; // success is 0
}

static int destroy_app(void)
{
	g_clear_object(&g_app);
	g_app = NULL;

	return 0;
}

static int run_app(int notify_fd)
{
	(void) notify_fd;

	int status;

	if (!g_app) {
		return -1;
	}

	/* Blocking call */
	status = g_application_run(G_APPLICATION(g_app), 0, NULL);
	/* --- */

	return status;
}
