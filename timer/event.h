#pragma once

#include "common.h"
#include "ipc_common.h"

/* Internal event struct (see ipc.h:app_cmd_t) */
typedef struct {
	timestamp_t timestamp;
	app_cmd_t opcode;
	//
	int seqnum; // track send order (timestamp only for timer state transitions)
	int payload; // -1 if none, else an event heap index (interal to event.c)
} event_t;

int event_send(event_t * ev, void * payload, size_t len);
int event_send_cmd(app_cmd_t opcode);
void * event_ref_payload(event_t * ev);
void event_unref_payload(event_t * ev);
//
/* Optional event initializer. Not hurtful to have, but the main use case was
 * for the event queue idea which would have been called internally as a copy
 * if not by the caller earlier on. Since I'm scrapping that, I don't think I
 * need this, but I want to hold onto the placeholder just in case. */
// event_t * event_create(app_cmd_t opcode);
// ^^ destroyed with free(io_buf)
//
int event_poll(event_t * ev_out);
int event_init(void);
void event_shutdown(void);
