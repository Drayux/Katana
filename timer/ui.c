#include "ui.h"

#include "ipc.h"
#include "frontends/frontends.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

static struct {
	pthread_t * thread;
	pthread_mutex_t lock; // notify_fd lock
	frontend_t * frontend;
	int notify_fd;
	bool headless;
} ctx = {
	// TODO: Indifferent about this interface. For now, assume headless mode
	// unless ui_load is called. (Though, it may be better for main to
	// conconditionally call ui_load? Do I want headless to be a proper "mode"
	// or just the fallback? Interally NULL -> headless just makes sense, but
	// to a settings menu, livesplit would make sense as the default.)
	.headless = true,
};

static void * ui_main(void * arg)
{
	logging_add_reporter("ui");

	/* Nothing to run without a frontend loaded */
	if (!ctx.frontend) {
		if (ctx.headless) {
			// TODO: Maybe better to put this in main after resolving the options?
			// ^^ no need to start the thread at all if we know this early, hence the error
			LOG_ERROR("Running in headless mode, UI thread exiting");
		}
		else {
			LOG_FATAL("Missing frontend but not in headless mode");
		}
		return NULL;
	}

	if (ctx.frontend->prepare) {
		if (ctx.frontend->prepare()) {
			/* UI failed to start */
			return NULL;
		}
	}
	if (ctx.frontend->run) {
		/* This call should be blocking */
		ctx.frontend->run(ctx.notify_fd);
		/* --- */
	}
	else {
		LOG_ERROR("Selected UI frontend has no run call");
	}

	LOG_INFO("Frontend UI exiting");

	pthread_mutex_lock(&ctx.lock);
	if (ctx.frontend->destroy) {
		ctx.frontend->destroy();
	}

	if (ctx.notify_fd > STDERR_FILENO) {
		close(ctx.notify_fd);
		ctx.notify_fd = 0;
	}
	pthread_mutex_unlock(&ctx.lock);

	return NULL;
}


/* --- UI PUBLIC INTERFACE --- */

// TODO: (not ideal to put here but alas) I'm not sure if I want to assert that
// this was called before calling thread_start or not. It is technically
// necessary in the current state. Is an extra context var "initialized"
// worthwhile? Or is this one that we just want to expect the developer to
// call correctly.
//
// Whatever the decision, I want to make it consistent across all the threads
// (IPC and UI)
int ui_init(char const * frontend_name)
{
	ctx.headless = false;
	ctx.frontend = frontend_select(frontend_name);

	if (!ctx.notify_fd) {
		ctx.notify_fd = eventfd(0, O_NONBLOCK | O_CLOEXEC);
		if (ctx.notify_fd < 0) {
			LOG_ERROR("eventfd fail: could not create UI thread wake event fd");
			return -1;
		}
	}
	
	pthread_mutex_init(&ctx.lock, NULL);

	return ctx.frontend ? 0 : -1;
}

/*
 * NOTE: Thread object is seperate so the main routine can join multiple
 * threads at once.
 *
 * NOTE pt2: We're asserting that thread_start is called before any thread
 * might try to notify it or close it. This is the case for now, but a race
 * condition could arise if that gets changed. (Because of the attempt to 
 * lock the mutex.) */
int ui_thread_start(pthread_t * thread)
{
	int status;

	if (!thread) {
		LOG_ERR("UI thread is NULL");
		return -1;
	}
	else if (!ctx.notify_fd) {
		// TODO (see the comment above ui_init)
		// We technically don't need this much at all, it is possible to run
		// without UI notify. Do we *want* to support that? I don't think so,
		// but alas, notify_fd may not be the best indiciator of if the init
		// method has been called or not.
		LOG_ERR("UI module not yet initialized");
		return -1;
	}
	else if (ctx.thread) {
		LOG_ERR("UI thread already running");
		return -1;
	}

	status = pthread_create(thread, NULL, &ui_main, NULL);
	if (!status) {
		/* success */
		ctx.thread = thread;
	}
	return status;
}

// TODO
void ui_thread_shutdown(void)
{
	// if (!atomic_load(&shutdown_requested)) {
		// atomic_store(&shutdown_requested, true);

		// TODO: Possible resource race condition, this could be called after
		// the UI thread starts shutting itself down (from the handler loop.)
		// - < User sends `quit` >
		// - Enter this if-block
		// - Other thread wakes to receive `quit`
		// - Other thread breaks, closes pipe FD
		// - This thread resumes, calls notify on closed FD
		ui_notify();
	// }
}

void ui_notify(void)
{
	if (!pthread_mutex_trylock(&ctx.lock)) {
		if (ctx.notify_fd > STDERR_FILENO) {
			SET_EVENT(ctx.notify_fd);
		}
		pthread_mutex_unlock(&ctx.lock);
	}
	else {
		/* This only happens if we're changing the state of notify_fd, which
		 * means we're either too early or too late for it to matter. */
		LOG_DEBUG("UI wake dropped");
	}
}
