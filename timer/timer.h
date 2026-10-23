#pragma once

#include "common.h"
#include "timer_common.h"

typedef enum {
	TIMER_STATE_INIT = 0,
	TIMER_STATE_READY,
	TIMER_STATE_ACTIVE, // aka "running"
	TIMER_STATE_PAUSE, // tracks manual pauses
	TIMER_STATE_LOAD, // tracks load time
	TIMER_STATE_STOP, // timer not running, mid-run
	TIMER_STATE_DONE,
} timer_state_t;
// NOTE: (not sure where to put this yet, probably with the autosplitter loop) 
// Load time takes precedence. AKA if both are "true" then the load delta will
// be incremented and NOT the pause delta

timer_state_t timer_init(void * arg);
timer_state_t timer_split(timestamp_t ts, size_t idx);
timer_state_t timer_split_next(timestamp_t ts);
timer_state_t timer_pause(timestamp_t ts);
timer_state_t timer_load(timestamp_t ts);
timer_state_t timer_stop(void);

/* TODO: As a feature for more casual players, challenge runs, or just practice,
 * being able to resume from a split would be a nice feature to have. (Paired
 * with a save_partial_run() mechanism once I get to the savefiles portion.) */
// timer_state_t timer_reset_split(void);

/* Wrapper functions to call a transition "right now" */
static inline timer_state_t timer_split_next_now(void)
{
	return timer_split_next(0);
}
static inline timer_state_t timer_pause_now(void)
{
	return timer_pause(0);
}
static inline timer_state_t timer_load_now(void)
{
	return timer_load(0);
}
