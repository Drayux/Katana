#pragma once

#include "logging.h"

#include <stdlib.h> // For null free

#include <errno.h>  // For set / clear event
#include <stdint.h> //

typedef unsigned char byte;

/* Cleans up out-of-memory bailout                               */
#define ASSERT_ALLOC(ptr, rval)                                   \
    do {                                                          \
        if ((ptr) == NULL) {                                      \
            LOG_ERRORF("alloc: no memory for `%s`", #ptr);        \
            return (rval);                                        \
        }                                                         \
    } while (0)
#define ASSERT_ALLOC_GOTO(ptr, label)                             \
    do {                                                          \
        if ((ptr) == NULL) {                                      \
            LOG_ERRORF("alloc: no memory for `%s`", #ptr);        \
            goto label;                                           \
        }                                                         \
    } while (0)

/* Cleans up the ever-common "if (ptr) free(ptr);"               */
#define NULL_FREE(ptr)                                            \
    do {                                                          \
        if ((ptr) == NULL) {                                      \
            LOG_DEBUG("free: `" #ptr "` is NULL");                \
        }                                                         \
		else {                                                    \
            free(ptr);                                            \
        }                                                         \
    } while (0)

/* Cleans up the multitude of eventfd notifications               /
 * This exists because of the explicit 8-byte eventfd I/O        */
// TODO: This probably is not the best spot for this
#define SET_EVENT(fd)                                             \
    do {                                                          \
        uint8_t _wake[8] = { 1 };                                 \
		(void) !write((fd), _wake, 8);                            \
    } while (0)
#define CLEAR_EVENT(fd)                                           \
	do {                                                          \
		ssize_t _len;                                             \
		uint8_t _buf[8];                                          \
		while ((_len = read((fd), _buf, 8))) {                    \
			if (_len < 0) {                                       \
				if (errno != EAGAIN) {                            \
					LOG_ERROR("read: notify fd error");           \
				}                                                 \
				break;                                            \
			}                                                     \
			else if (_len != 8) {                                 \
				LOG_ERRORF("read: unexpected event len %d", _len);\
			}                                                     \
		}                                                         \
	} while (0)

