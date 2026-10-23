#pragma once

typedef enum {
	APP_CMD_QUIT = 0,
	APP_CMD_START,
	APP_CMD_STOP,
	APP_CMD_RELOAD,
	// TODO: The rest of the split commands
} app_cmd_t;

typedef struct {
	int efd;
	atomic_bool flag;
} shutdown_t;

static inline void shutdown_request(shutdown_t *s) {   // async-signal-safe
    atomic_store(&s->flag, true);
    uint64_t one = 1; (void)!write(s->efd, &one, sizeof one);
}

// for next time, I started working on the new timer type so I had a "visual"
// for figuring out what the state machine should look like ^^

