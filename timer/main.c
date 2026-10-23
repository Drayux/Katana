#include "environment.h"
#include "gui.h"
#include "ipc_server.h"
#include "logging.h"

#include "plugins/plugin_loading.h"
#include "plugins/plugin_utils.h"

/**
 * LibreSplit's auto splitter thread.
 *
 * TODO: This is all moving with the thread changes.... delete eventually
 *
static void * ls_auto_splitter(void * arg)
{
	prctl(PR_SET_NAME, "LS LASR", 0, 0, 0);
	while (1) {
		atomic_store(&auto_splitter_running, true);
		if (atomic_load(&auto_splitter_enabled)
			&& auto_splitter_file[0] != '\0') {
			run_auto_splitter();
		}
		atomic_store(&auto_splitter_running, false);
		if (atomic_load(&exit_requested)) {
			LOG_DEBUG("Exit requested, shutting down Auto Splitter Thread");
			return 0;
		}
		usleep(50000);
	}
	unregister_luac_functions();
	return NULL;
}
 *
 */

/**
 * === PROGRAM ENTRY ===
 *
 * Ensure program environment and run mode, then load and initialize any
 * requested functionality.
 */
int main(int argc, char * argv[])
{
	int status = 0; // OK
	char const * envvar;
	int sockfd;
	app_cmd_t cmd;

	/* GTK handles its own thread, so we don't need this for now. When I come
	 * back to revising the windowing logic, I will probably want it to take
	 * as similar a form as possible, so I've left this stubbed in comments. */
	// pthread_t gui_thread;

	pthread_t ipc_thread;
	pthread_t logging_thread;

	logging_init();
	LOG_INFOF("Welcome to Katana timer, version %s", APP_VERSION);

	/* Check if app is running as root */
	if (geteuid() == 0) {
		LOG_WARN("Process owner detected as root");
		printf("Running as root is not recommended and disabled by default."
			   "Overriding this risks system security and may cause unintended"
			   "file ownership problems. Proceed with caution.");
		envvar = getenv("BYPASS_ROOT_PROTECTION");
		if (!envvar || strcmp(envvar, "1")) {
			LOG_FATAL("Bypass root protection not enabled");
			// Threads not yet started
			logging_dumpall();
			return 1;
		}
	}

	/* Check the options / run mode */
	// TODO: Options for CLI stuff (help, plugin exclusion, etc.)

	/* Validate other environment conditions */
	// TODO: Depending on what CLI gets implemented, this could be
	// conditionally bypassed
	check_directories();

	/* Check for IPC socket
	 * If present, it is likely that an instance is already open */
	// TODO: As with most things I write, this appears to work, but it could
	// use more testing.
	if ((sockfd = ipc_connect()) >= 0) {
		close(sockfd);
		LOG_FATAL(
			"Katana is already open, and more than one instance introduces "
			"a lot of bugs that you don't want to deal with. Exiting."
		);
		// Threads not yet started
		logging_dumpall();
		return 1;
	}
	LOG_DEBUG("Init IPC server thread");
	ipc_server_thread_start(&ipc_thread);

	/* Spawn a thread to handle asyncronous log output */
	LOG_DEBUG("Init logging thread");
	pthread_create(&logging_thread, NULL, &loggingThread, NULL);

	/* Prepare and register selected plugins */
	init_external_lasr_functions();
	init_timer_registries();
	load_plugins();

	/* Spawn the GUI thread
	 * TODO: Windowing logic eventually be moved into a default plugin, and a
	 * TUI will be the fallback */
	LOG_DEBUG("Init GUI thread");
	// gui_thread_start(&gui_thread);
	(void) gui_start();

	// pthread_t t1; // Auto-splitter thread
	// pthread_create(&t1, NULL, &ls_auto_splitter, NULL);
	// pthread_join(t1, NULL);

	LOG_INFO("Starting application loop");
	while ((cmd = app_cmd_poll())) {

		/* This portion is still a very rough WIP.
		 *
		 * Ultimately, the core timer is just a simple state machine, it needs
		 * no incremental stepping. Thus, the direction I'm heading is a new
		 * timer which is just that: a singleton state machine.
		 *
		 * To handle various thread inputs to the timer (i.e. GUI key event,
		 * IPC socket, autosplitter, plugin, etc., instead of locking the
		 * timer in a mutex or polling every thread individually, any thread
		 * with an event ready need just trigger a shared pipe.
		 *
		 * Because of the potenial made available with the lua runtime, I will
		 * be moving that logic to the main thread, here.
		 */

		switch (cmd) {
			default:
				LOG_ERRORF("Unsupported main loop event: %d", (int) cmd);
				continue;

			case APP_CMD_START:
				break;
			case APP_CMD_STOP:
				break;
			case APP_CMD_RELOAD:
				break;
		}
	}

shutdown:
	/* Trigger other threads to stop */
	logging_shutdown();

	// pthread_join(gui_thread, NULL);
	pthread_join(ipc_thread, NULL);
	pthread_join(logging_thread, NULL);

	return status;
}
