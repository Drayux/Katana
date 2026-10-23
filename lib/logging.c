/** \file logging.c
 * Logger Rollster
 *
 * Asynchronous Logging Library for LibreSplit based on threads, circular
 * queues, hopes and dreams.
 */
#include "logging.h"

#include "environment.h"

#include <linux/limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>

/** \brief The Log Buffer
 *
 * Allows to memorize messages in a circular queue for the
 * logging thread to consume
 */
static struct {
	char message_queue[LOG_QUEUE_SIZE]
					  [LOG_STR_LEN]; /*!< The read circular queue */
	int head;						 /*!< Index of the head of the queue */
	int tail;						 /*!< Index of the tail of the queue */
	pthread_mutex_t lock;			 /*!< Lock to avoid race conditions */
	pthread_cond_t cond; /*!< Condition to signal between the logMessage
							function and the logging thread */
	FILE * logfile;
} logQueue = { 0 };

/*! Atomic bool used to keep the thread active, might be used for clean closing
 * in future */
static atomic_bool logging_active;

/**
 * Pops a message from the log queue, writing it into console
 * and the log file. Just a utility.
 *
 * @param logfile The File pointer to write into
 */
static void _pop_message(void)
{
	fprintf(stderr, "%s", logQueue.message_queue[logQueue.head]);

	// Log to file, if successfully initialized
	if (logQueue.logfile) {
		fprintf(logQueue.logfile, "%s", logQueue.message_queue[logQueue.head]);
		fflush(logQueue.logfile);
	}

	logQueue.head = (logQueue.head + 1) % LOG_QUEUE_SIZE;
}

/**
 * Initializes the log queue, ready to receive messages
 * 
 * @return 0 on success
 */
int logging_init(void)
{
	char const * const log_file_name = "/libresplit.log";
	char const * path_buf;
	char data_path[PATH_MAX + sizeof(log_file_name)];

	logQueue.head = 0;
	logQueue.tail = 0;
	pthread_mutex_init(&logQueue.lock, NULL);
	pthread_cond_init(&logQueue.cond, NULL);
	atomic_store(&logging_active, 1);

	prctl(PR_SET_NAME, "LS Logger", 0, 0, 0);

	path_buf = get_libresplit_data_folder_path();
	if (path_buf) {
		strcpy(data_path, path_buf);
		strcat(data_path, "/libresplit.log");
		free((void *) path_buf);
	}
	else {
		perror("Failed to determine log file directory");
		return 1;
	}

	// TODO: This is maybe not the best approach for this
	if (strlen(data_path) >= PATH_MAX) {
		fprintf(stderr, "Invalid log file directory (too long)");
		return 1;
	}

	logQueue.logfile = fopen(data_path, "a");
	if (!logQueue.logfile) {
		perror("Failed to open log file");
		return 1;
	}

	return 0;
}

/**
 * Function to close the logger thread.
 *
 * This is needed because while we're closing, we might be waiting for
 * the queue to fill up. If that's the case, we signal the thread to continue
 * after setting logging_active to false.
 */
void logging_shutdown(void)
{
	LOG_DEBUG("Shutting down logger...")

	atomic_store(&logging_active, 0);
	pthread_cond_signal(&logQueue.cond);

	logging_dumpall();
	if (logQueue.logfile) {
		fclose(logQueue.logfile);
	}
}

void logging_dumpall(void) {
	pthread_mutex_lock(&logQueue.lock);
	while (logQueue.head != logQueue.tail) {
		_pop_message();
	}
	pthread_mutex_unlock(&logQueue.lock);
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
	// Lock the mutex for writing
	pthread_mutex_lock(&logQueue.lock);
	// If the queue is full, wait (bottleneck)
	while ((logQueue.tail + 1) % LOG_QUEUE_SIZE == logQueue.head) {
		pthread_cond_wait(&logQueue.cond, &logQueue.lock);
	}
	// Create a timestamp for the log
	char timestamp[64];
	time_t current_time = time(NULL);
	struct tm * t = localtime(&current_time);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
	// There is space in the queue, add a new message
	va_list args;
	va_start(args, fmt);
	// Put the timestamp first...
	snprintf(
		logQueue.message_queue[logQueue.tail], LOG_STR_LEN, "%s | ", timestamp);
	// The remaining space is for the message
	vsnprintf(logQueue.message_queue[logQueue.tail] + strlen(timestamp) + 1,
		LOG_STR_LEN - sizeof(timestamp) - 1, fmt, args);
	va_end(args);
	logQueue.tail = (logQueue.tail + 1) % LOG_QUEUE_SIZE;

	// Signal a possibly waiting logging thread
	pthread_cond_signal(&logQueue.cond);
	// Unlock the mutex
	pthread_mutex_unlock(&logQueue.lock);
}

/**
 * The logging thread, writes the queued messages in the log.
 *
 * Works as a consumer
 *
 * @param arg Unused.
 */
void * loggingThread(void * arg)
{
	while (1) {
		pthread_mutex_lock(&logQueue.lock);
		while (logQueue.head == logQueue.tail) {
			// unlocks the mutex, waits for a signal
			pthread_cond_wait(&logQueue.cond, &logQueue.lock);
			if (!atomic_load(&logging_active)) {
				// TODO: probably don't need this??
				LOG_INFO("Logging thread exiting");
				pthread_mutex_unlock(&logQueue.lock);
				return NULL;
			}
		}
		_pop_message();
		// TODO: probably don't need this??
		pthread_mutex_unlock(&logQueue.lock);
	}
	LOG_WARN("Logging never initialized");
	return NULL;
}
