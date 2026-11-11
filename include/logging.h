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

#include "common.h"

#define LOG_LINE_LEN   256
#define LOG_THREAD_LEN  24
#define LOG_HIST_LEN    64

int logging_init(void);
void logging_shutdown(void);
int logging_consumer_start(void);
int logging_consumer_stop(void); // optional step: keeps log file open
void logging_thread_notify(void);
int logging_add_reporter(char const * friendly_name);
char ** logging_get_scrollback(int rel_lines);

/* Not intended to be called directly, prefer the macros below */
void __log_message(char const * fmt, ...);
/* --- */

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
	file ":" line " | " level " " message "\n"

// TODO: __FILE_NAME__ is unique to clang and gcc. We'd need a fall back to
// support toolchains, if this is a prudent goal.
#define LOG(T, message)                                                        \
	{                                                                          \
		__log_message(                                                         \
			LOG_STRING(__FILE_NAME__, LOG__STR(__LINE__), #T, message));       \
	}

#define LOGF(T, fmt, ...)                                                      \
	{                                                                          \
		__log_message(                                                         \
			LOG_STRING(__FILE_NAME__, LOG__STR(__LINE__), #T, fmt),            \
			__VA_ARGS__);                                                      \
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

// TODO: replacing this one with "ERROR"
// but both are present right now to let me phase them out
#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERR(message) LOG([ERROR], message);
#define LOG_ERRF(fmt, ...) LOGF([ERROR], fmt, __VA_ARGS__);
#else
#define LOG_ERR(message)
#define LOG_ERRF(fmt, ...)
#endif
///////

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(message) LOG([ERROR], message);
#define LOG_ERRORF(fmt, ...) LOGF([ERROR], fmt, __VA_ARGS__);
#else
#define LOG_ERROR(message)
#define LOG_ERRORF(fmt, ...)
#endif

#if LOG_LEVEL <= LOG_LEVEL_FATAL
#define LOG_FATAL(message) LOG([FATAL], message);
#define LOG_FATALF(fmt, ...) LOGF([FATAL], fmt, __VA_ARGS__);
#else
#define LOG_FATAL(message)
#define LOG_FATALF(fmt, ...)
#endif
