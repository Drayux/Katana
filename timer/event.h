#pragma once

#include "ipc.h"
#include "common.h"

/* Internal event struct (see ipc.h:app_cmd_t) */
typedef struct {
	timestamp_t timestamp;
	app_cmd_t opcode;
	ssize_t len; /* ssize_t should always be larger than uint16_t (on any desktop) */
	//
	uint8_t * payload;
} event_t;

// NOTE: About the size_t conversion ^^
// We do the conversion so it plays nice with later operations. It is certain
// to be larger than the max size, defined in ipc.h

int event_poll(event_t * ev_out);
int event_send(event_t * ev, bool try_again);
int event_init(void);
void event_shutdown(void);
