#pragma once

#include "common.h"
#include "timer_common.h"

#include <stddef.h>

typedef enum {
	APP_CMD_SCAN = -1, // timeout
	APP_CMD_QUIT = 0,
	APP_CMD_START,
	APP_CMD_SPLIT,
	// APP_CMD_SKIP,
	// APP_CMD_UNDO,
	APP_CMD_STOP,
	APP_CMD_LOAD,
	APP_CMD_SAVE,
	// TODO: The rest of the split commands
} app_cmd_op_t;

typedef struct {
	app_cmd_op_t op;
	timestamp_t ts;
	size_t buf_len;
	byte buf[];
} app_cmd_t;

/*
 *
typedef struct {
	int efd;
	atomic_bool flag;
} shutdown_t;

static inline void shutdown_request(shutdown_t *s) {   // async-signal-safe
    atomic_store(&s->flag, true);
    uint64_t one = 1; (void)!write(s->efd, &one, sizeof one);
}
 *
 */

ssize_t app_cmd_send(app_cmd_t * send_cmd, bool try_again);
app_cmd_t * app_cmd_poll(void);

static inline ssize_t app_cmd_send_yield(app_cmd_t * cmd)
{
	return app_cmd_send(cmd, true);
}

static inline ssize_t app_cmd_send_drop(app_cmd_t * cmd)
{
	return app_cmd_send(cmd, false);
}

