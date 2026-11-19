/** \file logging.c
 * Logger Rollster
 *
 * Asynchronous Logging Library for LibreSplit based on threads, circular
 * queues, hopes and dreams.
 */
#include "logging.h"

#include "environment.h"
#include "io_common.h"

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

/* THREAD-LOCAL CONTEXT */
// release memory from linked list in main thread, not this pointer!
thread_local static reporter_t * local_reporter = NULL;
/* --- */

/* LOGGER CONTEXT */
static struct {
	pthread_mutex_t lock; // lock when modifying or traversing the reporters list
	atomic_bool active; // async logic ready (else, log actions should block)
	FILE * logfile;
	char ** scrollback;
	ssize_t scrollback_idx; // Negative indicates buffer is full, so we've looped
	reporter_t * reporters; // Linked list, always ends in an "empty" tail
} log_ctx = { 0 };

/* GLOBAL THREAD CONTEXT */
static struct {
	pthread_mutex_t lock;
	atomic_bool pending; // a thread was created that must be joined
	pthread_t consumer_thread;
	int notify_fd;
} thread_ctx = { 0 };

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

	if (log_ctx.logfile) {
		fprintf(log_ctx.logfile, "%s", msg);
		fflush(log_ctx.logfile);
	}

	/* Init as -LOG_HIST_LEN to indicate no loop has occurred, using
	 * scrollback[ (idx + LOG_HIST_LEN) % LOG_HIST_LEN ] this maps to 0.
	 *    -256 -> sb[0]
	 * 	   -10 -> sb[246] // arbitrary middle value
	 * 	    -1 -> sb[255]
	 *       0 -> sb[0]   // modulo rollover, first overwrite
	 *       1 -> sb[1]
	 *      10 -> sb[10]
	 *     255 -> sb[255]
	 *   ( 256 -> sb[0]   // double rollover, not stored, but hypothetical ) */
	if (log_ctx.scrollback) {
		sb_idx = (log_ctx.scrollback_idx + LOG_HIST_LEN) % LOG_HIST_LEN;
		sb_line_len = strlen(msg);
		sb_line_ref = log_ctx.scrollback[sb_idx];

		if (log_ctx.scrollback_idx >= 0) {
			/* Scrollback buffer has looped */
			if (sb_line_len > strlen(sb_line_ref)) {
				errno = 0;
				sb_line_ref = realloc(sb_line_ref, sb_line_len + 1);
				if (errno) {
					/* give up if no memory */
					// Old is still in sb, so it will be freed on normal shutdown
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

		// fprintf(stderr, "line: %lu %p\n", sb_idx, sb_line_ref);
		memcpy(sb_line_ref, msg, sb_line_len + 1);

		log_ctx.scrollback[sb_idx] = sb_line_ref;
		log_ctx.scrollback_idx = (log_ctx.scrollback_idx + 1) % LOG_HIST_LEN;
	}
}

/* io_read_delim callback wrapper for _emit_message */
static int _io_msg_cb(char const * const msg, size_t msg_len)
{
	(void) msg_len;
	_emit_message(msg);
	return 0; // drain emitted message, preserve anything left over
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
	reporter_t * telescope = log_ctx.reporters;

	/* initialization check */
	if (!telescope) {
		/* new tail */
		telescope = calloc(1, sizeof(reporter_t));
		ASSERT_ALLOC(telescope, NULL);

		telescope->tail = true;
		log_ctx.reporters = telescope;
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

/* NOTE / TODO: New version moved to "private" version where the mutex should
 * be locked by the caller */
void _thread_notify(bool shutdown)
{
	if (shutdown) {
		if (!atomic_exchange(&log_ctx.active, false)) {
			/* already inactive */
			return;
		}
	}
	else if (!atomic_load(&log_ctx.active)) {
		return;
	}

	if (thread_ctx.notify_fd > STDERR_FILENO) {
		SET_EVENT(thread_ctx.notify_fd);
	}
	else {
		LOG_ERROR("invalid notify fd");
	}
}

/**
 * Log consumer thread: writes the queued messages in the log.
 *
 * TODO: I think the assert_alloc_goto nested log statements  will be dropped
 * because active is true at the time of error.
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
	int pending_len;

	reporter_t * telescope;
	size_t fd_count;

	/* Store what went wrong if we need to shut down early */
	char const * err_msg = NULL;

	/* Static so that emitting does not consume additional memory */
	static io_buf_t * line_buf = NULL;

	if (!line_buf) {
		line_buf = io_buf_create(LOG_LINE_LEN);
		ASSERT_ALLOC_GOTO(line_buf, consumer_exit);
	}

	logging_add_reporter("logger"); // self

	/* Normally operating, the consumer thread should have near exclusive
	 * control over the internal state. This "excessive" lock is to prioritize
	 * the consumer thread, yielding only between the completion of a message.
	 * (I'm a bit indifferent about this... TODO?) */
	pthread_mutex_lock(&log_ctx.lock);

	/* always set the notify fd as the first fd in this list */
	fds->fd = thread_ctx.notify_fd;
	fds->events = POLLIN;

	while (true) {
		memset(fds + 1, 0, (fds_len - 1) * sizeof(struct pollfd));
		fd_count = 1;

		telescope = log_ctx.reporters;
		while (telescope && !telescope->tail) {
			if (telescope->msg_recv) {
				/* dynamic realloc if necessary (should be rare) */
				if (fd_count >= fds_len) {
					fds_len_new = fds_len * 2;
					errno = 0;
					fds_new = realloc(fds, fds_len_new * sizeof(struct pollfd));
					if (errno) {
						LOG_ERROR("no memory for fds realloc"); // OG still valid
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

		pthread_mutex_unlock(&log_ctx.lock);
		//
		ready = poll(fds, fd_count, -1);
		//
		pthread_mutex_lock(&log_ctx.lock);

		if (ready < 0) {
			/* unexpected shutdown */
			err_msg = "poll fail";
			goto consumer_exit;
		}

		for (fd_idx = 1; fd_idx < fd_count; ++fd_idx) {
			if (fds[fd_idx].revents != 0) {
				if (!(fds[fd_idx].revents & POLLIN)) {
					/* TODO: Instead of exiting, track which reporter errored
					 * and set it's fd to -1 so it's skipped later on. */
					err_msg = "reporter fd error";
					goto consumer_exit;
				}

				do {
					pending_len = io_read_delim(fds[fd_idx].fd, '\0', line_buf,
						_io_msg_cb,
						NULL);
				} while (pending_len);

				if (pending_len) {
					if (line_buf->status < 0) {
						err_msg = "read pipe fail";
						goto consumer_exit;
					}
					else if (line_buf->status > 0) {
						/* Pipe FD guarantees atomic write, thus, this means
						 * a critical bug, or something wrote to the wrong FD */
						LOG_ERROR("incomplete log message: dump pipe");

						line_buf->filled = 0;
						line_buf->status = 0;
					}
				}
			}
		}

		/* external wake, usually to exit */
		if (fds[0].revents & POLLIN) {
			CLEAR_EVENT(fds[0].fd);
			if (!atomic_load(&log_ctx.active)) {
				goto consumer_exit;
			}
		}
		else if (fds[0].revents & (POLLHUP | POLLERR)) {
			err_msg = "notify fd error";
			goto consumer_exit;
		}
	}

consumer_exit:
	atomic_store(&log_ctx.active, false);
	pthread_mutex_unlock(&log_ctx.lock);

	/* ^^ Logging events will again be blocking and write directly to the
	 * log file and stderr.
	 * NOTE: It is possible that a thread logs at the same time an error occurs
	 * and the message is dropped. This is acceptable because such a failure is
	 * already an exceptional condition. */

	if (err_msg) {
		LOG_ERRORF("logger: %s", err_msg);
	}
	else {
		LOG_INFO("Logging thread exiting");
	}

	NULL_FREE(fds);
	NULL_FREE(line_buf);

	return NULL;
}


/* --- PUBLIC INTERFACES --- */

/**
 * Initializes the log queue, ready to receive messages. Call before adding any
 * thread as a logging reporter.
 * 
 * @return 0 on success
 * TODO: Non-zero error statuses
 */
int logging_init(void)
{
	int rval = 0;

	char logfile_path[PATH_MAX + 1] = { 0 };
	char const * xdgdata_path;
	pthread_mutexattr_t lock_attr;

	prctl(PR_SET_NAME, "Katana Logger", 0, 0, 0);

	/* Recursive lock allows the logger thread to log itself during shutdown */
    pthread_mutexattr_init(&lock_attr);
    pthread_mutexattr_settype(&lock_attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&log_ctx.lock, &lock_attr);
    pthread_mutexattr_destroy(&lock_attr);

	atomic_store(&log_ctx.active, false);
	log_ctx.logfile = NULL;
	log_ctx.scrollback = NULL;
	log_ctx.reporters = NULL;

	atomic_store(&thread_ctx.pending, false);
	pthread_mutex_init(&thread_ctx.lock, NULL);
	thread_ctx.notify_fd = -1;

	/* -- Open log file -- */
	xdgdata_path = get_libresplit_data_folder_path();
	if (!xdgdata_path) {
		perror("Failed to determine log file directory");
		rval = 1;
	}
	else {
		strncpy(logfile_path, xdgdata_path, sizeof(logfile_path));
		free((void *) xdgdata_path); // TODO: This is dubiously typed

		/* Fills data_path up to PATH_MAX bytes (leaves NUL byte at the end.)
		 * Path was truncated if data_path[PATH_MAX] is not also a NUL byte. */
		strncat(logfile_path, "/katana.log",
			sizeof(logfile_path) - strlen(logfile_path) - 1);

		if (strlen(logfile_path) >= PATH_MAX) {
			fprintf(stderr, "Invalid log file directory (too long)");
			rval = 1;
		}
		else if (!(log_ctx.logfile = fopen(logfile_path, "a"))) {
			perror("fopen fail");
			rval = 1;
		}
	}

	/* -- Main thread needs a reporter -- */
	rval = logging_add_reporter("main") ? 1 : rval;

	/* Create scrollback buffer (if indicated) */
#if defined(LOG_HIST_LEN) && (LOG_HIST_LEN > 0)
	log_ctx.scrollback_idx = -LOG_HIST_LEN;
	log_ctx.scrollback = calloc(LOG_HIST_LEN, sizeof(char *)); /* outer layer of 2D array */
	ASSERT_ALLOC(log_ctx.scrollback, 1);
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
	char ** scrollback_ref;

	logging_consumer_stop();

	/* Clean up log file */
	logfile_ref = log_ctx.logfile;
	log_ctx.logfile = NULL;

	if (logfile_ref) {
		fclose(logfile_ref);
	}

	/* Clean up scrollback */
	scrollback_ref = log_ctx.scrollback;
	log_ctx.scrollback = NULL;

	if (scrollback_ref) {
		for (int idx = 0; idx < LOG_HIST_LEN; ++idx) {
			/* Every entry is asserted a valid allocation or NULL */
			NULL_FREE(scrollback_ref[idx]);
		}
	}

	/* Clean up reporters --
	 * NOTE: Do not use NULL_FREE here! This would invoke LOG_x *after*
	 * shutdown has started. */
	telescope = log_ctx.reporters;
	log_ctx.reporters = NULL;

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

	pthread_mutex_destroy(&log_ctx.lock);
	pthread_mutex_destroy(&thread_ctx.lock);
}

/* Start the dedicated log thread.
 * returns 0 on success (rval of pthread_create) */
int logging_consumer_start(void)
{
	int rval;

	// if (!pthread_mutex_trylock(&thread_ctx.lock)) {
		// return -1;
	// }
	pthread_mutex_lock(&thread_ctx.lock);

	if (atomic_exchange(&thread_ctx.pending, true)) {
		/* pending was already true */
		rval = -1;
		LOG_ERROR("log consumer already running");
		goto consumer_start_fail;
	}

	/* could move to init, see the related comment in consumer_stop */
	rval = thread_ctx.notify_fd = eventfd(0, O_NONBLOCK | O_CLOEXEC);
	if (rval < 0) {
		LOG_ERROR("eventfd fail: logging will be synchronous");
		goto consumer_start_fail;
	}

	rval = pthread_create(
		&thread_ctx.consumer_thread, NULL,
		&log_consumer_loop, NULL);
	if (rval) {
		LOG_ERROR("pthread_create fail: logging will be synchronous");
	}

consumer_start_fail:
	if (rval) {
		atomic_store(&thread_ctx.pending, false);
	}
	else {
		/* NOTE: Set this inside the mutex so that an early shutdown's notify
		 * is not skipped and subsequently hangs forever. */
		atomic_store(&log_ctx.active, true);
	}

	pthread_mutex_unlock(&thread_ctx.lock);
	return rval;
}

/* TODO: I've spent a week overthinking this.
 *
 * TLDR, I wanted to over-design the logger to be able to start and stop from
 * any thread, but it raised the question of the "known state" if start and
 * stop were called "at the same time."
 *
 * The best solution I figured was to force any call to consumer_stop to block
 * and wait for pending to become true (meaning that a valid logger thread
 * exists and can be joined) but this then fails to handle the case of a failed
 * start without the parent needing extra complexity to check the return value.
 * While this is possible, that case is the one use case I actually have,
 * rather than a restartable logger thread.
 *
 * In essence, for now, a trivial mutex is used to avoid crashing/hanging.
 * A minor resource leak is possible if one tries to stop and start in different
 * threads, but even still, the reference will always be held, and a final stop
 * will work. Only the state of the logger thread is possibly ambiguous.
 */
int logging_consumer_stop(void)
{
	int rval = 0;

	pthread_mutex_lock(&thread_ctx.lock);

	if (atomic_load(&thread_ctx.pending)) {
		_thread_notify(true);

		rval = pthread_join(thread_ctx.consumer_thread, NULL);
		atomic_store(&thread_ctx.pending, false);
	}

	/* NOTE: This doesn't technically have to be here...but symmetry makes a
	 * good 'default' spot until I have reason to move it */
	if (thread_ctx.notify_fd > STDERR_FILENO) {
		close(thread_ctx.notify_fd);
	}
	thread_ctx.notify_fd = -1;

	pthread_mutex_unlock(&thread_ctx.lock);
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
	int rval = 1; // syscall fail

	/* search to be sure that the reporter does not already exist */
	pthread_mutex_lock(&log_ctx.lock);
	ref = _find_reporter(id);
	if (!ref->tail) {
		rval = -1; // dev error
		LOG_ERROR("duplicated log reporter");
		goto reporter_fail;
	}

	/* new reporter (messages block needs to be allocated still) */
    if (pipe2(pipe_fds, O_NONBLOCK | O_CLOEXEC)) {
		LOG_ERROR("pipe fail");
		goto reporter_fail;
	}
	ref->id = id;
	ref->name = friendly_name ? strdup(friendly_name) : NULL; // null ok
	ref->msg_recv = pipe_fds[0];
	ref->msg_send = pipe_fds[1];

	/* new tail; no release on failure, always cleaned up with shutdown */
	tail = calloc(1, sizeof(reporter_t));
	ASSERT_ALLOC_GOTO(tail, reporter_fail);

	tail->tail = true;
	ref->next = tail;
	ref->tail = false;

	pthread_mutex_lock(&thread_ctx.lock);
	_thread_notify(false); /* If the logger is running, it is polling on the
							  previous fd set and won't see this one yet. */
	pthread_mutex_unlock(&thread_ctx.lock);
	pthread_mutex_unlock(&log_ctx.lock);

	/* save the reporter pointer in thread-scoped storage */
	local_reporter = ref;
	LOG_DEBUGF("Registered thread %s (%d) with the logger",
		friendly_name ? friendly_name : "null", (int) id);
	return 0;

reporter_fail:
	pthread_mutex_unlock(&log_ctx.lock);
	return rval;
}

/*
 * Gets the stored log buf either from line number X if rel_lines is positive,
 * or X number of the most recent lines if negative
 * 
 * Returned pointer must be unrefered with logging_unref_scrollback() !!
 */
// TODO: Consider introducing a build option "enable scrollback" because this
// is quite a bit of extra memory (relatively) for such a small feature.
char ** logging_ref_scrollback(int rel_lines)
{
	char ** adjusted_sb;
	ssize_t sb_idx;

	if (!log_ctx.scrollback) {
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

	/* UNLOCKED IN UNREF FUNCTION */
	pthread_mutex_lock(&log_ctx.lock);
	/* --- (or alloc fail) */

	sb_idx = log_ctx.scrollback_idx + rel_lines + LOG_HIST_LEN;
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
	ASSERT_ALLOC_GOTO(adjusted_sb, ref_scrollback_fail);

	// Slow version to assure correctness (TODO: remove once tested)
	// for (int i = 0; i < rel_lines; ++i) {
		// adjusted_sb[i] = log_ctx.scrollback[(sb_idx + i) % LOG_HIST_LEN];
		// if (adjusted_sb[i] == NULL) {
			// break;
		// }
	// }

	if (sb_idx + rel_lines > LOG_HIST_LEN) {
		size_t lines_to_end = LOG_HIST_LEN - sb_idx;
		/* oldest requested message -> end of SB circular buffer */
		memcpy(adjusted_sb, log_ctx.scrollback + sb_idx,
			lines_to_end * sizeof(char *));
		/* start of SB circular buffer -> newest requested message */
		memcpy(adjusted_sb + lines_to_end, log_ctx.scrollback,
			(rel_lines - lines_to_end) * sizeof(char *));
	}
	else {
		memcpy(adjusted_sb, log_ctx.scrollback + sb_idx,
			rel_lines * sizeof(char *));
	}

	return adjusted_sb;

ref_scrollback_fail:
	pthread_mutex_unlock(&log_ctx.lock);
	return NULL;
}

// TODO: Not 100% sure if this is the most intuitive implementation
// For now, this is parallel for whether the malloc above fails or not. If it
// does, then the mutex is unlocked early, so this does not unlock twice.
void logging_unref_scrollback(char *** scrollback_ref)
{
	if (scrollback_ref && *scrollback_ref) {
		/* LOCKED IN REF_SCROLLBACK */
		pthread_mutex_unlock(&log_ctx.lock);
		/* --- */

		free(*scrollback_ref);
		*scrollback_ref = NULL;
	}
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
	int status;

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
	if ((0)
		|| !local_reporter /* pre-init log message */
		|| local_reporter->msg_send <= STDERR_FILENO /* reporter closed */
		|| !atomic_load(&log_ctx.active) /* logging thread shutting down */
	) {
		/* Block until closed, then emit directly to stderr */
		pthread_mutex_lock(&log_ctx.lock);
		_emit_message(msg);
		pthread_mutex_unlock(&log_ctx.lock);
	}
	else {
		/* Enqueue message in this thread's pipe */
		status = io_write_msg(local_reporter->msg_send, msg, msg_len + 1, true);

		if (status) {
			/* Log line could not be sent, fall back to direct emit */
			pthread_mutex_lock(&log_ctx.lock);
			_emit_message(msg);
			pthread_mutex_unlock(&log_ctx.lock);
		}
	}
}
