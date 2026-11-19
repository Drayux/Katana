#pragma once

/* NOTE: This file might be better described as IPC client, rather than "common"
 * The name was selected as this is the functionality that is common amongst
 * any katana IPC, where everything uses the client functionality, but only the
 * main program hosts a server. */

#include "common.h"
#include "timer_common.h"

#include <stddef.h>
#include <stdint.h>

#define KATANA_IPC_PATH "katana.sock"
#define KATANA_IPC_MAX_PAYLOAD (512u)

typedef enum {
	APP_CMD_SCAN = 0, // poll timeout (maybe, this is a TODO idea)
	APP_CMD_QUIT,     // issues a quit command (might not exit right away)
	APP_CMD_START,
	APP_CMD_SPLIT,
	APP_CMD_SKIP,
	APP_CMD_UNDO,
	APP_CMD_STOP,
	APP_CMD_LOAD,
	APP_CMD_SAVE,
	// TODO: The rest of the split commands
} app_cmd_t;

/* TODO -- considering the alternative:
#define APP_CMD_SCAN 0
#define APP_CMD_QUIT 1
#define APP_CMD_START 2
// ...
typedef uint16_t app_cmd_t
 */

/* IPC event struct -- Katana converts this into an event_t after read. */
typedef struct __attribute__((__packed__)) {
	timestamp_t ts;
	uint16_t op; // app_cmd_t
	uint16_t len;
	//
	uint8_t buf[];
} ipc_msg_t;

int ipc_send(ipc_msg_t * msg, bool try_again);
int ipc_listen(void);
int ipc_connect(void);
void ipc_set_send_fd(int fd);
char * ipc_get_socket_path(void);

// TODO: Considering a couple extra versions if need be:
// - ipc_send_cmd (wrapper to create a command object, send it, and free it)
// - ipc_new_msg (public subroutine of the above to make a message struct)

static inline ssize_t ipc_send_yield(ipc_msg_t * msg)
{
	return ipc_send(msg, true);
}

static inline ssize_t ipc_send_drop(ipc_msg_t * msg)
{
	return ipc_send(msg, false);
}

static inline char const * app_cmd_to_str(app_cmd_t cmd)
{
	switch (cmd) {
		case APP_CMD_SCAN:
			return "NONE";
		case APP_CMD_QUIT:
			return "QUIT";
		case APP_CMD_START:
			return "START";
		case APP_CMD_SPLIT:
			return "SPLIT";
		case APP_CMD_SKIP:
			return "SKIP";
		case APP_CMD_UNDO:
			return "UNDO";
		case APP_CMD_STOP:
			return "STOP";
		case APP_CMD_LOAD:
			return "LOAD";
		case APP_CMD_SAVE:
			return "SAVE";

		default:
			return "INVALID";
	}
}
