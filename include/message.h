#pragma once

#include "common.h"

typedef struct {
	size_t capacity;
	size_t filled;
	uint8_t buf[];
} message_buf_t;
