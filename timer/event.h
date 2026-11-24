#pragma once

#include "common.h"
#include "ipc_common.h"

/* Internal event struct (see ipc.h:app_cmd_t) */
typedef struct {
	timestamp_t timestamp;
	app_cmd_t opcode;
	ssize_t len; /* ssize_t should always be larger than uint16_t (on any desktop) */
	//
	uint8_t * payload;
} event_t;

int event_send(event_t * ev, bool try_again);
//
int event_poll(event_t * ev_out);
int event_init(void);
void event_shutdown(void);
