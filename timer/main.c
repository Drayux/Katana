#include "common.h"
#include "environment.h"

#include "event.h"
#include "ui.h"

#include "plugins/plugin_loading.h"
#include "plugins/plugin_utils.h"

/*
 * NOTE for my future self (really not sure where best to place this!)
 * The source file hierarchy is such that anything at the top-level is an
 * interface need by or instance hosted by main. Everything else should have
 * additional non-main logic contained in a subdirectory.
 */

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
	int ipc_sock_fd;

	int event_op;
	event_t event;

	pthread_t ui_thread;

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
			return 1;
		}
	}

	/* Check the options / run mode */
	// TODO: Options for CLI stuff (help, plugin exclusion, etc.)

	/* Validate other environment conditions */
	// ^^ TODO: Depending on what CLI options are implemented, this could be
	// conditionally bypassed
	check_directories();

	/* Check for an existing IPC socket --
	 * If present, assume that an instance is already open */
	// (void) ipc_init(); // TODO
	if ((ipc_sock_fd = ipc_connect()) > STDERR_FILENO) {
		close(ipc_sock_fd);
		LOG_FATAL(
			"Katana is already open, and more than one instance introduces "
			"a lot of bugs that you don't want to deal with. Exiting.");
		return 1;
	}

	/* Spawn a thread to handle asyncronous log output */
	LOG_DEBUG("Start logging consumer thread");
	(void) logging_consumer_start();

	/* Prepare and register selected plugins */
	// init_external_lasr_functions();
	// init_timer_registries();
	// load_plugins();

	(void) event_init(); // TODO check errors

	/* Spawn the GUI thread */
	LOG_DEBUG("Start GUI thread");
	(void) ui_init(NULL); // TODO: sets TUI mode for now
						  // (waiting for settings/cli opts implementation)
	(void) ui_thread_start(&ui_thread);

	LOG_INFO("Enter main application loop");
	while ((event_op = event_poll(&event)) >= 0) {


		// FOR NEXT TIME!
		// The mystery pieces are:
		// - Clean ways to do the refresh rate -- maybe poll with the timeout?
		// - Handling the unloaded state / how to load splits at all
		//   ^^ i.e., is it intuitive to have a filepath passed as event data??
		// - Not a mystery, but recall that the gui trivially can compute what time
		//   to display every frame


		switch (event_op) {
			default:
				LOG_ERRORF("Unsupported main loop event: %d", event_op);
				continue;

			case APP_CMD_SCAN:
				LOG_INFO("Receive scan");
				/* TODO: try to resolve a command from the autosplitter */
				break;
			case APP_CMD_SPLIT:
				LOG_INFO("Receive split");
				break;
			case APP_CMD_STOP:
				LOG_INFO("Receive stop");
				break;
			case APP_CMD_LOAD:
				LOG_INFO("Receive load");
				break;

			case APP_CMD_QUIT:
				LOG_INFO("Receive quit");
				goto shutdown;
		}

		// (placeholder, moved from OG main)
		// pthread_create(&t1, NULL, &ls_auto_splitter, NULL);

		// TODO: Maybe a temporary interface, shows the TUI prompt for now
		// ALSO: It doesn't want to "work" more than once??
		ui_notify();

		// TODO: NULL_FREE(event->payload);
	}

shutdown:
	LOG_DEBUG("Leave main application loop");

	/* Wait for joinable threads */
	ui_thread_shutdown();
	pthread_join(ui_thread, NULL);

	/* Sequentially shutdown components with open files */
	event_shutdown();
	logging_shutdown();

	return status;
}
