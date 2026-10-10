#pragma once

#include "logging.h"

#include <stdlib.h>

#define ASSERT_ALLOC(ptr, rval)                        \
    do {                                               \
        if ((ptr) == NULL) {                           \
            LOG_FATALF("No memory: alloc `%s`", #ptr); \
            return (rval);                             \
        }                                              \
    } while (0)
