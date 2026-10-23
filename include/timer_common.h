#pragma once

#include <time.h>

/* All time values are stored as unsigned 64-bit integers in ns. */
// timestamp_t not time_t else we conflict with <time.h>
// ns definitely excessive, but 64 bits gives plenty of head room and gives the
// user more power to customize if they so please.
// (plenty of room means some 540 years)
typedef unsigned long long timestamp_t;
#define TIMESTAMP_MAX ((timestamp_t) -1)


/* TODO: This is likely to move before long */
#include <stdbool.h>
#include <stddef.h>

typedef struct {
	bool skipped; // ignore the following when true (init as true)
	timestamp_t split_ts; // aggregate delta (i.e. total time if timer start_ts is 0ns)
	timestamp_t load_delta; // load time so far
	timestamp_t pause_delta; // pause time so far
	// timestamp_t split_ts;
	// TODO: This probably needs some form of the following (thinking in terms
	// of what would may appear in a splits.kdl)
	// > type (i.e. main, sub, objective)
	// > tracked_ts
} split_t;
/***/

// returns the run-absolute timestamp in nanoseconds (1 seconds = 1 billion ns)
static inline timestamp_t timestamp_now(void)
{
	struct timespec timespec;

	// Monotonic clock is time elapsed since system boot
	// NOTE: Does not tick while system is asleep
	clock_gettime(CLOCK_MONOTONIC, &timespec);

	return timespec.tv_sec * 1000000000ll + timespec.tv_nsec;
}
