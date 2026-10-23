#include "timer.h"

#include <string.h>

// Variable length struct so we only need one release
// splits are 1-indexed -- the final split time will always be stored
// at index 0, len excludes this.
// (So 3 enumerated splits means the final split is at data[3])
/* Why 1-index splits?
 * - Easier loop logic for delta calculation
 * - Most games start at "level 1"
 * - Lua (where the auto splitter is) 1-indexes arrays, so this becomes a
 *   1-to-1 lookup
 */
typedef struct {
	size_t len;
	split_t data[];
} timer_splits_t;

/* Singleton struct that tracks all timing.
 * Counter to intuition, the timing can be tracked as a simple state machine,
 * polling the system clock during transitions. */
typedef struct {
	timer_state_t state;
	timestamp_t start_ts; // absolute timestamp at run start
	timestamp_t state_ts; // timestamp of last state transition
	timestamp_t load_delta; // aggregate of all load time
	timestamp_t pause_delta; // aggregate of all "pause" time
	// NOTE: Considering how to handle start delays...maybe just set it once
	// and subtract it from all timestamps?
	// TODO: maybe a category_t here to track the "game" stuff
	// > attempt_count
	// > finished_count
	// > etc.
	size_t split_idx; // current split
	timer_splits_t * splits;
} timer_ctx_t;

/* --- */

static timer_ctx_t timer_ctx = { 0 };


/* --- Internal context management --- */

// Releases all dynamic resources
static void timer_clear(void)
{
	NULL_FREE(timer_ctx.splits);
	memset(&timer_ctx, 0, sizeof(timer_ctx_t));
}

static timer_splits_t * timer_splits_create(size_t count)
{
	size_t alloc_count = count + 1u; // (count > 0) ? count : 1;
	size_t alloc_size = sizeof(timer_splits_t) + (sizeof(split_t) * alloc_count);
	timer_splits_t * ret = malloc(alloc_size);
	ASSERT_ALLOC(ret, NULL);

	memset(ret, 0, alloc_size);
	while (alloc_count--) {
		// Reuse the count var to initialize defaults for each split
		ret->data[alloc_count].skipped = true;
	}
	return ret;
}

static timestamp_t timer_validate_timestamp(timestamp_t transition_ts)
{
	if (!transition_ts) {
		// Timestamp param set to 0 used for "split now"
		return timestamp_now();
	}

	if (transition_ts > timer_ctx.start_ts) {
		LOG_ERRORF("Timestamp goes back in time\n"\
			"\t-- start timestamp: %ull"\
			"\t-- split timestamp: %ull",
			timer_ctx.start_ts, transition_ts);
		return 0;
	}
	return transition_ts;
}

static void timer_aggregate_pauses(timestamp_t transition_ts)
{
	if (timer_ctx.state == TIMER_STATE_PAUSE) {
		timer_ctx.pause_delta += (transition_ts - timer_ctx.state_ts);
	}
	else if (timer_ctx.state == TIMER_STATE_LOAD) {
		timer_ctx.load_delta += (transition_ts - timer_ctx.state_ts);
	}
}


/* --- State machine transitions --- */

// Initialize the timer (aka load the splits)
// TODO: not sure what the interface should be, arg is a placeholder
timer_state_t timer_init(void * arg)
{
	(void) arg;

	size_t split_count = 5; // TODO: just for testing

	switch (timer_ctx.state) {
		case TIMER_STATE_INIT:
		case TIMER_STATE_READY:
		case TIMER_STATE_STOP:
			LOG_INFO("Initializing timer");
			timer_clear();

			timer_ctx.splits = timer_splits_create(split_count);
			if (timer_ctx.splits) {
				timer_ctx.state = TIMER_STATE_READY;
			}
			break;
		default:
			LOG_DEBUG("Timer transition: `init` ignored");
	}

	return timer_ctx.state;
}

timer_state_t timer_split(timestamp_t ts, size_t idx)
{
	split_t * ref = NULL;
	timestamp_t delta = 0; // for resuming from stop state

	/* Validate inputs */
	if (!timer_ctx.splits) {
		// Cannot split if splits not initialized
		LOG_DEBUG("Timer transition: `split` ignored");
		return timer_ctx.state;
	}
	if (idx > timer_ctx.splits->len) {
		LOG_ERRORF("Cannot split to invalid split number: %u", idx);
		return timer_ctx.state;
	}
	if (!(ts = timer_validate_timestamp(ts))) {
		return timer_ctx.state;
	}

	switch (timer_ctx.state) {
		case TIMER_STATE_READY:
			LOG_INFO("Timer started");
			timer_ctx.state = TIMER_STATE_ACTIVE;
			timer_ctx.start_ts = ts;
			timer_ctx.state_ts = ts;
			timer_ctx.split_idx = idx;

			// TODO: Check for pause / load condition immediately
			// These auto splitter functions until the timer is running, but
			// some games may start real time from a load screen or cutscene

			break;

		case TIMER_STATE_PAUSE:
		case TIMER_STATE_LOAD:
			/* Aggregate pause/load time, if any */
			timer_aggregate_pauses(ts);
			// fallthrough
		case TIMER_STATE_ACTIVE:
			LOG_INFOF("Timer split %u at %ull", idx, ts);

			timer_ctx.state_ts = ts;

			if (idx == 0) {
				/* Run is complete */

				// TODO: Not quite sure how I want to use the 0-split here;
				// NOTE THAT SKIPPING THE LAST SPLIT LEAVES US INCOMPLETE RIGHT
				// NOW

				// Store the time data at the previous split
				ref = &(timer_ctx.splits->data[timer_ctx.split_idx]);
				ref->split_ts = ts;
				ref->load_delta = timer_ctx.load_delta;
				ref->pause_delta = timer_ctx.pause_delta;
				ref->skipped = false;

				// ref = &(timer_ctx.splits->data[0]);
				// ref->split_ts = ts;
				// ref->load_delta = timer_ctx.load_delta;
				// ref->pause_delta = timer_ctx.pause_delta;

				timer_ctx.state = TIMER_STATE_DONE;
			}
			else if (idx > timer_ctx.split_idx) {
				/* Normal case, we're advancing our split */

				// Store the time data at the previous split
				ref = &(timer_ctx.splits->data[timer_ctx.split_idx]);
				ref->split_ts = ts;
				ref->load_delta = timer_ctx.load_delta;
				ref->pause_delta = timer_ctx.pause_delta;
				ref->skipped = false;
			}
			else if (idx < timer_ctx.split_idx) {
				/* Splitting backwards, unset all splits between here and the
				 * new (earlier) index (inclusive) */
				for (int i = idx; i < timer_ctx.split_idx; ++i) {
					ref = &(timer_ctx.splits->data[i]);
					ref->skipped = true;
				}
			}
			else {
				LOG_DEBUG("Timer transition: `split` ignored");
			}
			//
			timer_ctx.split_idx = idx;
			break;

		case TIMER_STATE_STOP:
			/* Resuming a stopped timer is a bit troublesome.
			 * Since the timer isn't running, we don't want to track "stopped
			 * time" the same as a pause or load. Instead, we'll adjust the
			 * start time and split timestamps by the stop delta as though it
			 * was never stopped to begin with. */

			LOG_INFO("Timer resuming");

			if (idx == timer_ctx.split_idx) {
				delta = ts - timer_ctx.state_ts;
				timer_ctx.start_ts += delta;

				for (int i = 1; i < idx; ++i) {
					ref = &(timer_ctx.splits->data[i]);
					ref->split_ts += delta;
				}

				timer_ctx.state = TIMER_STATE_ACTIVE;
			}
			else {
				// TODO: For now, I'm ignoring any case where the split does
				// not match. In the future, this would be best implemented
				// by skipping splits forward or backwards depending.
				LOG_DEBUG("Timer transition: `resume` ignored");
			}
			break;

		default:
			LOG_DEBUG("Timer transition: `split` ignored");
	}

	return timer_ctx.state;
}

// Helper function to jump to the next split
timer_state_t timer_split_next(timestamp_t ts)
{
	size_t next_split = timer_ctx.split_idx + 1;

	/* Validation -- still pass through to consolidate error messages */
	if (!timer_ctx.splits) {
		return timer_split(0, 0);
	}

	if (next_split > timer_ctx.splits->len) {
		/* Final split! */
		return timer_split(ts, 0);
	}

	return timer_split(ts, next_split);
}

timer_state_t timer_pause(timestamp_t ts)
{
	if (!(ts = timer_validate_timestamp(ts))) {
		return timer_ctx.state;
	}

	switch (timer_ctx.state) {
		case TIMER_STATE_LOAD:
			/* Aggregate pause/load time, if any */
			timer_aggregate_pauses(ts);
			// fallthrough
		case TIMER_STATE_ACTIVE:
			LOG_INFO("Timer paused (game paused)");
			timer_ctx.state_ts = ts;
			timer_ctx.state = TIMER_STATE_PAUSE;
			break;

		case TIMER_STATE_PAUSE:
			// Nothing to do if already paused
		default:
			LOG_DEBUG("Timer transition: `pause` ignored");
	}

	return timer_ctx.state;
}

timer_state_t timer_load(timestamp_t ts)
{
	if (!(ts = timer_validate_timestamp(ts))) {
		return timer_ctx.state;
	}

	switch (timer_ctx.state) {
		case TIMER_STATE_PAUSE:
			/* Aggregate pause/load time, if any */
			timer_aggregate_pauses(ts);
			// fallthrough
		case TIMER_STATE_ACTIVE:
			LOG_INFO("Timer paused (loading)");
			timer_ctx.state_ts = ts;
			timer_ctx.state = TIMER_STATE_LOAD;
			break;

		case TIMER_STATE_LOAD:
			// Nothing to do if already loading
		default:
			LOG_DEBUG("Timer transition: `load` ignored");
	}

	return timer_ctx.state;
}

// No timestamp param for this one since pausing the timer itself is relatively
// discouraged. For runs that allow pausing, game pause is preferred.
// This is mostly just an intermediate state for run resets
timer_state_t timer_stop(void)
{
	timestamp_t ts = timestamp_now();

	switch (timer_ctx.state) {
		case TIMER_STATE_PAUSE:
		case TIMER_STATE_LOAD:
			/* Aggregate pause/load time, if any */
			timer_aggregate_pauses(ts);
			// fallthrough
		case TIMER_STATE_ACTIVE:
			LOG_INFO("Timer stopped");
			timer_ctx.state_ts = ts;
			timer_ctx.state = TIMER_STATE_STOP;
			break;

		default:
			LOG_DEBUG("Timer transition: `stop` ignored");
	}

	return timer_ctx.state;
}
