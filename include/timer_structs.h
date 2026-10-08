#pragma once

/**
 * @brief time structure for storing both the real time and game time
 * for any time representation. (i.e. splits, segments, wr etc)
 */
typedef struct ls_time {
    long long real_time; /*!< Real time means the actual real world elapsed time */
    long long game_time; /*!< Game time is the internal time controlled either by the autosplitter, or derived from real_time - load_time */
} ls_time;
