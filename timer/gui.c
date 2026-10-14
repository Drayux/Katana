#include "gui.h"

#include "gui/app_window.h"
#include "gui/timer.h"

#include <gtk/gtk.h>
#include <stdatomic.h>

extern atomic_bool exit_requested;
static LSApp * g_app = NULL;

/*
static void * gui_thread(void * arg)
{
	g_app = ls_app_new();
	LOG_INFO("TEST PRE STARTING APP");
	// status = g_application_run(G_APPLICATION(g_app), 0, NULL);
	(void) g_application_run(G_APPLICATION(g_app), 0, NULL);
	LOG_INFO("TEST POST STARTING APP");

	while(atomic_load(&exit_requested) == 0) {
		// TODO: Right now just wait and see if it's alive. There's a lot about
		// GTK that's a mystery to me
		usleep(500000);
	}

	LOG_INFO("GUI thread exiting");
	g_clear_object(&g_app);

	return NULL;
}

void gui_thread_start(pthread_t * thread)
{
	if (!thread) {
		LOG_ERR("NULL arg passed");
		return;
	}
	pthread_create(thread, NULL, &gui_thread, NULL);
}
*/

int gui_start(void)
{
	int status;

	g_app = ls_app_new();

	/* Blocking call */
	status = g_application_run(G_APPLICATION(g_app), 0, NULL);
	/* *** */

	LOG_INFO("GUI exiting");
	g_clear_object(&g_app);

	return status;
}
