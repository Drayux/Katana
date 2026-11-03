/** \file logging.c
 * Logger Rollster
 *
 * Asynchronous Logging Library for LibreSplit based on threads, circular
 * queues, hopes and dreams.
 */
#include "logging.h"

#include "environment.h"

#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>


/*
 * Implements the main structure for thread-safe logging. Two goals of this
 * logger are to avoid blocking whenever possible, and to provide helpful
 * output as to which thread reported the message. In this model, every
 * such reporting thread will be called a `reporter_t`.
 */
// typedef char[LOG_LINE_LEN] log_entry_t;
typedef struct _reporter_typedef reporter_t;
struct _reporter_typedef {
	pthread_t id;
	char * name;
	int msg_recv; // pipe
	int msg_send; ///
	reporter_t * next;
	bool tail;
};

/* SHARED/GLOBAL CONTEXT */
static struct {
	pthread_t logging_thread;
	pthread_mutex_t lock; // lock when modifying or traversing the reporters list
	atomic_bool active; // dedicated logger thread is running

	reporter_t * reporters; // Linked list, always ends in an "empty" tail
	FILE * logfile;
} shared_ctx = { 0 };

/* PER-THREAD CONTEXT */
// release memory from linked list in main thread, not this pointer!
thread_local static reporter_t * local_reporter = NULL;

/**
 * Pops a message from the log queue, writing it into console
 * and the log file. Just a utility.
 *
 * Private function:
 *  - caller asserts msg is non-null and NUL-terminated
 *  - caller asserts thread safety
 */
static inline void _emit_message(char const * const msg)
{
	fprintf(stderr, "%s", msg);
	if (shared_ctx.logfile) {
		fprintf(shared_ctx.logfile, "%s", msg);
		fflush(shared_ctx.logfile);
	}
}

/* Returns the matching reporter or an empty one if it doesn't exist.
 * Check for empty via the "tail" struct member.
 *
 * Private function:
 *  - caller asserts pthread id is valid
 *  - caller locks the reporters_t mutex
 */
static reporter_t * _find_reporter(pthread_t id)
{
	reporter_t * telescope = shared_ctx.reporters;

	/* initialization check */
	if (!telescope) {
		/* new tail */
		telescope = calloc(1, sizeof(reporter_t));
		ASSERT_ALLOC(telescope, NULL);

		telescope->tail = true;
		shared_ctx.reporters = telescope;
	}

	/* search if the reporter already exists */
	else {
		// Looks for matching ID or end of list
		// End of list is the target insertion point
		/* NOTE: Thread already locked if we get to this loop */
		while (!telescope->tail) {
			if (pthread_equal(telescope->id, id)) {
				/* eurika! */
				return telescope;
			}
			telescope = telescope->next;
			if (!telescope) {
				LOG_ERROR("Malformed log reporter list: reached NULL without tail");
				return NULL; // dev error
			}
		}
	}

	/* reached the end, point at an "empty" reporter */
	return telescope;
}

/**
 * Log consumer thread: writes the queued messages in the log.
 */
static void * log_consumer_loop(void * arg)
{
	size_t fds_len = 4; /* can be reasonably assumed */
	struct pollfd * fds = calloc(fds_len, sizeof(struct pollfd));
	ASSERT_ALLOC_GOTO(fds, consumer_exit);

	size_t fds_len_new; /* for realloc, if needed */
	struct pollfd * fds_new;

	int ready;
	int fd_idx;
	char msg[LOG_LINE_LEN];
	ssize_t msg_len;

	reporter_t * telescope;
	size_t fd_count;

	/* Store what went wrong if we need to shut down early */
	char const * err_msg = NULL;

	// TODO: Placeholder for adding the logging thread as a log reporter
	// (I might not want this, see consumer_exit.)

	pthread_mutex_lock(&shared_ctx.lock);
	while (1) {
		memset(fds, 0, fds_len * sizeof(struct pollfd));
		fd_count = 0;

		telescope = shared_ctx.reporters;
		while (telescope && !telescope->tail) {
			if (telescope->msg_recv) {
				/* dynamic realloc if necessary (should be rare) */
				if (fd_count >= fds_len) {
					fds_len_new = fds_len * 2;
					fds_new = realloc(fds, fds_len_new * sizeof(struct pollfd));
					if (!fds_new) {
						LOG_ERROR("no memory for fds realloc");
						break; // inner loop
					}
					memset(fds_new + fd_count, 0,
						(fds_len_new - fds_len) * sizeof(struct pollfd));

					fds_len = fds_len_new;
					fds = fds_new;
					fds_new = NULL;
				}
				/* --- */

				fds[fd_count].fd = telescope->msg_recv;
				fds[fd_count].events = POLLIN;
				++fd_count;
			}
			telescope = telescope->next;
		}

		// TODO: We probably block here, what kind of signal do I want to use for shutdown?
		/* TODO pt2: The main idea is "let the consumer hold the lock the entire
		 * time it's alive. But...if I have an odd thread without a reporter, I
		 * might as well let it emit while this thread is waiting. 
		 * 
		 * I think the way to do that might to have a double poll. The first is
		 * non-blocking, both are the same otherwise. If we hit EWOULDBLOCK, then
		 * unlock the mutex and sit on the blocking poll, else keep it locked.
		 *
		 * That said, that's such a rare scenario to matter....so I think this
		 * current solution is fine for now. (Worse case, we get a few log
		 * messages slightly out of order...only if I set up a thread wrong.)
		 */ 
		/* TODO pt3: If adding a thread after starting the logging thread, this
		 * poll will still be waiting on the old FD set, so it never sees the
		 * "added reporter" message until the next loop */

		pthread_mutex_unlock(&shared_ctx.lock);
		//
		ready = poll(fds, fd_count, -1);
		//
		pthread_mutex_lock(&shared_ctx.lock);

		if (ready < 0) {
			/* unexpected shutdown */
			err_msg = "poll fail";
			goto consumer_exit;
		}

		for (fd_idx = 0; fd_idx < fd_count; ++fd_idx) {
			if (fds[fd_idx].revents != 0) {
				if (!(fds[fd_idx].revents & POLLIN)) {
					err_msg = "pollfd returned error";
					goto consumer_exit;
				}

				msg_len = read(fds[fd_idx].fd, msg, sizeof(msg) - 1);
				if (msg_len < 0) {
					/* read error */
					// TODO: Consider setting msg_recv to 0 on this
					// occurance so we can keep going otherwise
					err_msg = "read pipe fail";
					goto consumer_exit;
				}

				msg[msg_len] = '\0';
				_emit_message(msg);
			}
		}
	}

consumer_exit:
	/* NOTE: Important to unset active before logging shutdown events (or they'll be missed) */
	// ^^ TODO: Actually, I could just deliberately never call "add_producer"
	// for the logger thread....both would work, not sure which is more
	// intuitive.
	/* NOTE: Technically possible for this thread to error at the same time
	 * we might (incorrectly) call start. This is gracefully ignored as this
	 * thread is guarnateed to exit by this point (will not be joined.) */
	atomic_store(&shared_ctx.active, false);

	if (err_msg) {
		LOG_ERRORF("logger: %s", err_msg);
	}
	LOG_INFO("Logging thread exiting");

	pthread_mutex_unlock(&shared_ctx.lock);
	return NULL;
}


/* --- PUBLIC INTERFACES --- */

/**
 * Initializes the log queue, ready to receive messages
 * 
 * @return 0 on success
 */
int logging_init(void)
{
	pthread_mutexattr_t lock_attr;

	/* Recursive lock allows the logger thread to log itself during shutdown */
    pthread_mutexattr_init(&lock_attr);
    pthread_mutexattr_settype(&lock_attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&shared_ctx.lock, &lock_attr);
    pthread_mutexattr_destroy(&lock_attr);

	atomic_store(&shared_ctx.active, false);

	shared_ctx.reporters = NULL;
	shared_ctx.logfile = NULL;

	int rval = 0;
	char logfile_path[PATH_MAX + 1] = { 0 };
	char const * xdgdata_path;

	prctl(PR_SET_NAME, "Katana Logger", 0, 0, 0);

	/* -- Open log file -- */
	xdgdata_path = get_libresplit_data_folder_path();
	if (xdgdata_path) {
		strncpy(logfile_path, xdgdata_path, sizeof(logfile_path));
		free((void *) xdgdata_path); // TODO: This is dubiously typed

		/* This will fill up to data_path[PATH_MAX] with path bytes (leaves the
		 * NUL byte at the end.) We know the path is was truncated if
		 * data_path[PATH_MAX] is not also a NUL byte. */
		strncat(logfile_path, "/katana.log",
			sizeof(logfile_path) - strlen(logfile_path) - 1);
	}
	else {
		perror("Failed to determine log file directory");
		rval = 1;
	}

	if (rval == 0 && strlen(logfile_path) >= PATH_MAX) {
		fprintf(stderr, "Invalid log file directory (too long)");
		rval = 1;
	}
	else {
		shared_ctx.logfile = fopen(logfile_path, "a");
	}

	if (!shared_ctx.logfile) {
		perror("fopen fail");
		rval = 1;
	}

	/* -- Main thread needs a reporter -- */
	if (logging_add_reporter("main")) {
		rval = 1;
	}
	return rval;
}

/**
 * Clean up logger resources (shuts down thread if not already.)
 *
 * (This function alone technically can be called from any thread, but is
 * intended for the main thread as other threads will be unable to log to the
 * file once the logger thread stops.)
 */
void logging_shutdown(void)
{
	FILE * logfile_ref;
	reporter_t * telescope = shared_ctx.reporters;
	reporter_t * chopping_block;

	logging_thread_stop();

	/* Grab exclusive control of the file pointer */
	pthread_mutex_lock(&shared_ctx.lock);
	logfile_ref = shared_ctx.logfile;
	shared_ctx.logfile = NULL;
	pthread_mutex_unlock(&shared_ctx.lock);

	/* Any stray thread will only emit to stderr, now.
	 * This *shouldn't* happen, but it is not undefined behavior. */

	if (logfile_ref) {
		fclose(shared_ctx.logfile);
	}

	/* Clean up reporters */
	while (telescope) {
		NULL_FREE(telescope->name);
		if (telescope->msg_recv > 0) {
			close(telescope->msg_recv);
		}
		if (telescope->msg_send > 0) {
			close(telescope->msg_send);
		}
		chopping_block = telescope;
		telescope = chopping_block->next;
		NULL_FREE(chopping_block);
	}
	shared_ctx.reporters = NULL;
}

/* Start the dedicated log thread. Any messages queued before this will be
 * waiting on the reporter associated with the main thread
 * returns 0 on success (rval of pthread_create) */
int logging_thread_start(void)
{
	int rval;

	if (atomic_load(&shared_ctx.active)) {
		/* Thread already running -- start/stop should only be called from the
		 * main thread. If this is upheld, we won't ever race to restart before
		 * the previous thread exits. (pthread_join in the stop routine.) */
		return -1;
	}

	rval = pthread_create(
		&shared_ctx.logging_thread, NULL,
		&log_consumer_loop, NULL);
	if (!rval) {
		atomic_store(&shared_ctx.active, true);
	}
	return rval;
}

int logging_thread_stop(void)
{
	int rval = 0;
	if (atomic_load(&shared_ctx.active)) {
		atomic_store(&shared_ctx.active, false);
		// TODO: placeholder for signal the log thread (we're in main rn)
		// ^^ this signal should ALWAYS make the thread exit and not depend on active

		rval = pthread_join(shared_ctx.logging_thread, NULL);
		/* any threads trying to log but blocked during shutdown will wake now */
	}
	return rval;
}

// Register a new reporting thread
// returns 0 on success, nonzero on error
int logging_add_reporter(char const * friendly_name)
{
	pthread_t const id = pthread_self();

	reporter_t * ref;
	reporter_t * tail = NULL;

	int pipe_fds[2];

	/* search to be sure that the reporter does not already exist */
	// TODO: Possible design conflict-of-interest: allowing the thread to have
	// a deferred start means that we need to able to add a fully-functional
	// reporter with the chance we never use its pipe ever. Not necessarily
	// wrong, but is it ideal??
	pthread_mutex_lock(&shared_ctx.lock);
	ref = _find_reporter(id);
	if (!ref->tail) {
		pthread_mutex_unlock(&shared_ctx.lock);
		LOG_ERROR("Duplicated log reporter");
		return -1; // dev error
	}

	/* new reporter (messages block needs to be allocated still) */
    if (pipe2(pipe_fds, O_NONBLOCK | O_CLOEXEC)) {
		LOG_ERROR("pipe fail");
		return 1;
	}
	ref->id = id;
	ref->name = friendly_name ? strdup(friendly_name) : NULL; // null ok
	ref->msg_recv = pipe_fds[0];
	ref->msg_send = pipe_fds[1];

	/* new tail --  On the off chance this fails, the previous node will remain
	 * as a tail and thus be cleaned up like normal during logging_shutdown. */
	tail = calloc(1, sizeof(reporter_t));
	ASSERT_ALLOC(tail, 1);

	tail->tail = true;
	ref->next = tail;
	ref->tail = false;

	pthread_mutex_unlock(&shared_ctx.lock);

	/* save the reporter pointer in thread-scoped storage */
	local_reporter = ref;
	LOG_DEBUGF("Registered thread %s (%d) with the logger",
		friendly_name ? friendly_name : "null", (int) id);

	return 0;
}

/**
 * Underlying function to all the LOG_* macros
 *
 * Works as a producer.
 *
 * @param[in] fmt The message to print in the log or the format string
 */
void logMessage(char const * fmt, ...)
{
	va_list args;
	char msg[LOG_LINE_LEN] = { 0 };
	size_t msg_len;
	ssize_t send_len;

	char thread[64]; // TODO: arbitrary
	size_t thread_len;

	time_t current_time = time(NULL);
	struct tm * ts = localtime(&current_time);

	/* Format the log message */
	msg_len = strftime(msg, sizeof(msg), "%Y-%m-%d %H:%M:%S | ", ts);

	if (local_reporter && local_reporter->name) {
		thread_len = snprintf(thread, sizeof(thread), "<%s> ", local_reporter->name);
		if (thread_len >= sizeof(thread)) {
			// TODO: I should have a better way to handle a thread name being too long
			strcpy(thread, "< ... > ");
		}
		strncpy(msg + msg_len, thread, sizeof(msg) - msg_len - 1);
		msg_len = strlen(msg);
	}

	va_start(args, fmt);
	vsnprintf(msg + msg_len, sizeof(msg) - msg_len - 1, fmt, args);
	msg_len = strlen(msg);
	va_end(args);

	/* Enqueue or direct emit */
	if ( (0)
		|| !local_reporter /* pre-init log message */
		|| local_reporter->msg_send == 0 /* reporter closed */
		|| !atomic_load(&shared_ctx.active) /* logging thread shutting down */
	) {
		/* block until closed, then emit directly to stderr */
		pthread_mutex_lock(&shared_ctx.lock);
		_emit_message(msg);
		pthread_mutex_unlock(&shared_ctx.lock);
	}
	else {
		/* enqueue message in this thread's pipe */
		// printf("would enqueue %s\n", msg);
		send_len = write(local_reporter->msg_send, (byte *) msg, msg_len + 1);
		if (send_len != msg_len + 1) {
			// TODO: Not yet sure how I want to handle write errors here
			perror("write fail");
		}
	}
}
