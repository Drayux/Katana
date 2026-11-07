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
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>


/*
 * Implements the main structure for thread-safe logging. Two goals of this
 * logger are to avoid blocking whenever possible, and to provide helpful
 * output as to which thread reported the message. In this model, every
 * such reporting thread will be called a `reporter_t`.
 */
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
	char ** scrollback;
	ssize_t scrollback_idx; // Negative indicates buffer is full, so we've looped
	int notify_fd;
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
	size_t sb_idx; /* scrollback buf working index */
	size_t sb_line_len; /* scrollback buf new line length */
	char * sb_line_ref; /* holder for the working sb line */

	fprintf(stderr, "%s", msg);

	if (shared_ctx.logfile) {
		fprintf(shared_ctx.logfile, "%s", msg);
		fflush(shared_ctx.logfile);
	}

	/* NOTE - about the scrollback index:
	 * Negative means "fresh" / no loop -- begins at -LOG_HIST_LEN
	 * Thus, if we store 256 items, first index is -256. To use this,
	 * scrollback[ (idx + LOG_HIST_LEN) % LOG_HIST_LEN is used ] Thus:
	 *    -256 -> sb[0]
	 * 	   -10 -> sb[246] // arbitrary middle value
	 * 	    -1 -> sb[255]
	 *       0 -> sb[0] // modulo rollover
	 *       1 -> sb[1]
	 *      10 -> sb[10]
	 *     255 -> sb[255]
	 *   ( 256 -> sb[0] // double rollover )
	 * Note that double rollover would not be seen, as we will always store
	 * `computed_index % LOG_HIST_LEN` to guarantee numerical stability. */
	if (shared_ctx.scrollback) {
		sb_idx = (shared_ctx.scrollback_idx + LOG_HIST_LEN) % LOG_HIST_LEN;
		sb_line_len = strlen(msg);
		sb_line_ref = shared_ctx.scrollback[sb_idx];

		if (shared_ctx.scrollback_idx >= 0) {
			/* Scrollback buffer has looped */
			if (sb_line_len > strlen(sb_line_ref)) {
				sb_line_ref = realloc(sb_line_ref, sb_line_len + 1);
				if (errno) {
					/* give up if no memory */
					fprintf(stderr, "No memory for logger scrollback realloc\n");
					return;
				}
			}
		}
		else {
			sb_line_ref = malloc(sb_line_len + 1);
			if (!sb_line_ref) {
				/* give up if no memory */
				fprintf(stderr, "No memory for logger scrollback alloc\n");
				return;
			}
		}

		// printf("Writing at %d: %s", sb_idx, msg); /// TODO remove after testing
		memcpy(sb_line_ref, msg, sb_line_len + 1);

		shared_ctx.scrollback[sb_idx] = sb_line_ref;
		shared_ctx.scrollback_idx = (shared_ctx.scrollback_idx + 1) % LOG_HIST_LEN;
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
	size_t fds_len = 6; /* can be reasonably assumed */
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

	pthread_mutex_lock(&shared_ctx.lock);

	/* set up a notify fd pipe -- always the first fd in the list */
    if ((shared_ctx.notify_fd = eventfd(0, O_NONBLOCK | O_CLOEXEC)) < 0) {
		LOG_ERROR("eventfd fail: could not create logger thread wake event fd");
		goto consumer_exit;
	}
	fds->fd = shared_ctx.notify_fd;
	fds->events = POLLIN;

	logging_add_reporter("logger");

	while (1) {
		memset(fds + 1, 0, (fds_len - 1) * sizeof(struct pollfd));
		fd_count = 1;

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

		/* TODO: I'm indifferent about this implementation.
		 * The main idea is "let the consumer hold the lock the entire time
		 * that it's alive. But, if I have an odd thread without a reporter, I
		 * might as well let it emit while this thread is waiting. */ 
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

		/* TODO: I'm not sure if this dumps everything or not. While that many
		 * messages piling up is a concern of its own, if it does not, said
		 * piled messages would be dropped */
		for (fd_idx = 1; fd_idx < fd_count; ++fd_idx) {
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

		if (fds[0].revents & POLLIN) {
			/* wake requested */
			CLEAR_EVENT(fds[0].fd);
			if (!atomic_load(&shared_ctx.active)) {
				goto consumer_exit;
			}
		}
	}

consumer_exit:
	/* NOTE: Technically possible for this thread to error at the same time
	 * that we might (incorrectly) call start. This is gracefully ignored as
	 * this thread is guaranteed to exit by this point (will not be joined.) */
	atomic_store(&shared_ctx.active, false);

	if (err_msg) {
		LOG_ERRORF("logger: %s", err_msg);
	}
	LOG_INFO("Logging thread exiting");
	NULL_FREE(fds);

	if (shared_ctx.notify_fd > STDERR_FILENO) {
		close(shared_ctx.notify_fd);
	}
	shared_ctx.notify_fd = 0; /* unset the field, we know it's never stdin */

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
	shared_ctx.scrollback = NULL;
	shared_ctx.scrollback_idx = -LOG_HIST_LEN;

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

#if defined(LOG_HIST_LEN) && (LOG_HIST_LEN > 0)
	/* Create scrollback buffer (array of char pointers) */
	shared_ctx.scrollback = calloc(LOG_HIST_LEN, sizeof(char *));
	ASSERT_ALLOC(shared_ctx.scrollback, 1);
#endif
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
	reporter_t * telescope;
	reporter_t * chopping_block;

	logging_thread_stop();

	/* Grab exclusive control of the shared context */
	pthread_mutex_lock(&shared_ctx.lock);
	telescope = shared_ctx.reporters;
	shared_ctx.reporters = NULL;
	logfile_ref = shared_ctx.logfile;
	shared_ctx.logfile = NULL;
	pthread_mutex_unlock(&shared_ctx.lock);
	// TODO: If it's safe to assume no threads are calling LOG_x by this point,
	// perhaps I do not need to lock this above at all.
	pthread_mutex_destroy(&shared_ctx.lock);

	/* Any stray thread will only emit to stderr, now.
	 * This *shouldn't* happen, but it is not undefined behavior. */

	if (logfile_ref) {
		fclose(logfile_ref);
	}

	/* Clean up reporters */
	// NOTE: Do not use NULL_FREE here! This would invoke LOG_x *after*
	// shutdown has started.
	while (telescope) {
		chopping_block = telescope;
		if (chopping_block->msg_recv > STDERR_FILENO) {
			close(telescope->msg_recv);
		}
		if (chopping_block->msg_send > STDERR_FILENO) {
			close(telescope->msg_send);
		}
		if (chopping_block->name) {
			free(telescope->name);
		}
		telescope = chopping_block->next;
		free(chopping_block);
	}
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
		logging_thread_notify();

		rval = pthread_join(shared_ctx.logging_thread, NULL);
		/* any threads trying to log but blocked during shutdown will wake now */
	}
	return rval;
}

void logging_thread_notify(void)
{
	/* mutex guarantees valid notify fd, it will not be closed if we hold it */
	pthread_mutex_lock(&shared_ctx.lock);
	if (shared_ctx.notify_fd > STDERR_FILENO) {
		SET_EVENT(shared_ctx.notify_fd);
	}
	pthread_mutex_unlock(&shared_ctx.lock);
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

	/* new tail; no release on failure, always cleaned up with shutdown */
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
	logging_thread_notify(); /* If the logger is running, it is polling on the
								previous set of reporters and won't see this. */
	return 0;
}

/**
 * Underlying function to all the LOG_* macros
 *
 * Works as a producer.
 *
 * @param[in] fmt The message to print in the log or the format string
 */
void __log_message(char const * fmt, ...)
{
	va_list args;

	char msg[LOG_LINE_LEN] = { 0 };
	size_t msg_len;
	ssize_t send_len;

	char thread[LOG_THREAD_LEN];
	size_t thread_len;
	size_t max_thread_len; // only calculated if needed

	time_t current_time = time(NULL);
	struct tm * ts = localtime(&current_time);

	/* Format the log message */
	msg_len = strftime(msg, sizeof(msg), "%Y-%m-%d %H:%M:%S | ", ts);

	if (local_reporter && local_reporter->name) {
		thread_len = snprintf(thread, sizeof(thread), "<%s> ", local_reporter->name);
		if (thread_len >= sizeof(thread)) {
			max_thread_len = sizeof(thread) - strlen("<..> ") - 1;
			thread[0] = '<';
			strncpy(thread + 1, local_reporter->name, max_thread_len);
			strcpy(thread + 1 + max_thread_len, "..> ");
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

/*
 * Gets the stored log buf either from line number X if rel_lines is positive,
 * or X number of the most recent lines if negative
 * 
 * Returned pointer must be freed by caller if non-NULL
 */
// TODO: Consider introducing a build option "enable scrollback" because this
// is quite a bit of extra memory (relatively) for such a small feature.
char ** logging_get_scrollback(int rel_lines)
{
	char ** adjusted_sb;
	ssize_t sb_idx;

	if (!shared_ctx.scrollback) {
		return NULL;
	}

	if (rel_lines > 0) {
		// TODO
		return NULL;
	}
	
	/* Cap the lines requested, else the math gets weird */
	// if (rel_lines < -LOG_HIST_LEN) {
		// rel_lines = -LOG_HIST_LEN;
	// }

	sb_idx = shared_ctx.scrollback_idx + rel_lines + LOG_HIST_LEN;
	if (sb_idx < 0) {
		/* requested more lines than we have available */
		rel_lines = sb_idx - rel_lines;
		sb_idx = 0;
	}
	else {
		rel_lines = -rel_lines;
		sb_idx = sb_idx % LOG_HIST_LEN;
	}
	// printf("needs %d lines, idx: %d\n", rel_lines, shared_ctx.scrollback_idx); /// TODO remove
	// -- rel_lines asserted positive now

	/* +1 is so that the caller can break on NULL whether there's enough lines
	 * to fill the request or not, else we'd need to report a length */
	adjusted_sb = calloc(rel_lines + 1, sizeof(char *));
	ASSERT_ALLOC(adjusted_sb, NULL);

	/* Lock so as not grab a mangled scrollback line pointer or line idx */
	// TOOD: This could almost certainly be faster with a two-step memcpy
	// Slow version is to assure correctness for testing
	pthread_mutex_lock(&shared_ctx.lock);
	for (int i = 0; i < rel_lines; ++i) {
		adjusted_sb[i] = shared_ctx.scrollback[(sb_idx + i) % LOG_HIST_LEN];
		if (adjusted_sb[i] == NULL) {
			break;
		}
	}
	pthread_mutex_unlock(&shared_ctx.lock);

	// NOTE! It is TECHNICALLY possible that a log line to which we've copied
	// the pointer is has realloc called on it, the main memory moves, and then
	// we hold a dead pointer. This would equate to an unchecked use-after-free.
	//
	// For now, I'm committing to the rationale "roll with it because it's
	// never going to happen" but this is important to know from a long term
	// design standpoint.
	//
	// Definitely should (TODO) fix this at some point, but I figure I'll wait
	// until I determine if I want to keep the implementation like this or not.
	// (Entertaining the idea of a singleton "adjusted buffer" which blocks
	// the logger until it's released. There would be an extra function for
	// that implemented here, in that case.)

	return adjusted_sb;
}
