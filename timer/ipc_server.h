#pragma once

#include "common.h"
#include "ipc_common.h"
// NOTE for future me, right now neither of these includes are needed in
// ipc_server.c or.h. I'm experiementing with a rigid header style and this is
// the result of that.

void ipc_server_thread_start(pthread_t * thread);
