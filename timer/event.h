#pragma once

#include "ipc.h"
#include "common.h"
// NOTE for future me, right now neither of these includes are needed in
// ipc_server.c or.h. I'm experiementing with a rigid header style and this is
// the result of that.

int event_init(void);
void event_shutdown(void);
int event_poll(app_cmd_t * cmd);
