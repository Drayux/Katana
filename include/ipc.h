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

/* Available Libresplitctl commands */
// typedef enum CTLCommand {
	// CTL_CMD_START_SPLIT, /*!< Start or split */
	// CTL_CMD_STOP_RESET,	 /*!< Stop or reset */
	// CTL_CMD_CANCEL,		 /*!< Cancel run */
	// CTL_CMD_UNSPLIT,	 /*!< Undo split */
	// CTL_CMD_SKIP,		 /*!< Skip split */
	// CTL_CMD_EXIT,		 /*!< Exit */
// } CTLCommand;
//
/* A remote Libresplitctl message */
// typedef struct __attribute__((__packed__)) CTLMessage {
	// uint32_t length;	  /*!< Size of message in bytes */
	// CTLCommand message[]; /*!< The message sent */
// } CTLMessage;

typedef enum {
	APP_CMD_SCAN = 0, // poll timeout (maybe, this is a TODO idea)
	APP_CMD_QUIT,     // issues a quit, but don't exit right away
					  // (we might have unsaved splits or a running timer)
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

ssize_t ipc_send(app_cmd_t * send_cmd, bool try_again);
int ipc_connect(void);
int ipc_listen(void);
char * ipc_get_socket_path(void);
void ipc_set_send_fd(int fd);

static inline ssize_t ipc_send_yield(app_cmd_t * cmd)
{
	return ipc_send(cmd, true);
}

static inline ssize_t ipc_send_drop(app_cmd_t * cmd)
{
	return ipc_send(cmd, false);
}
