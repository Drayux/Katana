/*
 * LOGGING SUPPORT
 *
 * (TODO) I want to improve logging efforts across the entire project.
 * To start this, some rough definitions on what level to put where:
 *
 * FATAL: The program has reached a state where it cannot continue
 *        (Try to catch this, but there's a good chance we just crash.)
 * ERROR: Failed assertion (i.e. an unexpected free(0) even if we caught it
 *        instead of segfaulting) or a state that is certain to be broken.
 *  WARN: A failure path was taken, but major functions can continue.
 *  INFO: Verbose information about the state of operation (i.e. defaults
 *        loaded, timer started, CTL command received, etc.)
 * DEBUG: (Not sure what's worth putting here yet.)
 */

#pragma once

#include <pthread.h>
#include <stdatomic.h>

#define LOG_QUEUE_SIZE 100
#define LOG_STR_LEN 512

extern atomic_bool exit_requested;

/** \brief The Log Buffer
 *
 * Allows to memorize messages in a circular queue for the
 * logging thread to consume
 */
typedef struct LogQueue {
	char message_queue[LOG_QUEUE_SIZE]
					  [LOG_STR_LEN]; /*!< The read circular queue */
	int head;						 /*!< Index of the head of the queue */
	int tail;						 /*!< Index of the tail of the queue */
	pthread_mutex_t lock;			 /*!< Lock to avoid race conditions */
	pthread_cond_t cond; /*!< Condition to signal between the logMessage
							function and the logging thread */
} LogQueue;

void initLogQueue(void);

void logMessage(char const * fmt, ...);
void close_logger();

void * loggingThread(void * arg);

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_FATAL 4

/* Only show error and fatal by default */
#if !defined(LOG_LEVEL)
#define LOG_LEVEL LOG_LEVEL_ERROR
#endif

#define LOG__XSTR(x) #x
#define LOG__STR(x) LOG__XSTR(x)

#define LOG_STRING(file, line, level, message)                                 \
	file ": " line " | " level " - " message "\n"

#define LOG(T, message)                                                        \
	{                                                                          \
		logMessage(LOG_STRING(__FILE__, LOG__STR(__LINE__), #T, message));     \
	}

#define LOGF(T, fmt, ...)                                                      \
	{                                                                          \
		logMessage(                                                            \
			LOG_STRING(__FILE__, LOG__STR(__LINE__), #T, fmt), __VA_ARGS__);   \
	}

#if LOG_LEVEL == LOG_LEVEL_DEBUG
#define LOG_DEBUG(message) LOG([DEBUG], message);
#define LOG_DEBUGF(fmt, ...) LOGF([DEBUG], fmt, __VA_ARGS__);
#else
#define LOG_DEBUG(fmt, ...)
#define LOG_DEBUGF(fmt, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(message) LOG([INFO], message);
#define LOG_INFOF(fmt, ...) LOGF([INFO], fmt, __VA_ARGS__);
#else
#define LOG_INFO(message)
#define LOG_INFOF(fmt, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(message) LOG([WARN], message);
#define LOG_WARNF(fmt, ...) LOGF([WARN], fmt, __VA_ARGS__);
#else
#define LOG_WARN(message)
#define LOG_WARNF(fmt, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERR(message) LOG([ERROR], message);
#define LOG_ERRF(fmt, ...) LOGF([ERROR], fmt, __VA_ARGS__);
#else
#define LOG_ERR(message)
#define LOG_ERRF(fmt, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_FATAL
#define LOG_FATAL(message) LOG([FATAL], message);
#define LOG_FATALF(fmt, ...) LOGF([FATAL], fmt, __VA_ARGS__);
#else
#define LOG_FATAL(message)
#define LOG_FATALF(fmt, ...)
#endif
