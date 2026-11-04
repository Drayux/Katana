#pragma once

#include "logging.h"

#include <stdlib.h>

/* Cleans up out-of-memory bailout                         */
#define ASSERT_ALLOC(ptr, rval)                             \
    do {                                                    \
        if ((ptr) == NULL) {                                \
            LOG_ERRORF("alloc: no memory for `%s`", #ptr);  \
            return (rval);                                  \
        }                                                   \
    } while (0)
#define ASSERT_ALLOC_GOTO(ptr, label)                       \
    do {                                                    \
        if ((ptr) == NULL) {                                \
            LOG_ERRORF("alloc: no memory for `%s`", #ptr);  \
            goto label;                                     \
        }                                                   \
    } while (0)

/* Cleans up the ever-common "if (ptr) free(ptr);"         */
#define NULL_FREE(ptr)                                      \
    do {                                                    \
        if ((ptr) == NULL) {                                \
            LOG_DEBUG("free: `" #ptr "` is NULL");          \
        }                                                   \
		else {                                              \
            free(ptr);                                      \
        }                                                   \
    } while (0)

typedef unsigned char byte;
