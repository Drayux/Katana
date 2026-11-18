#pragma once

#include "common.h"

#define IO_BLOCK_SIZE 4096 /* Max size for a single I/O operation */

/* Structure for preserving partial message transmissions */
typedef struct {
	size_t const capacity; /* max number of bytes in buf */
	size_t filled; /* number of preserved bytes in buf */
	char _buf[];
} io_buf_t;

/* Message handler type. Return 0 to drain the message from the buffer,
 * negative to clear the entire buffer, and positive to leave it as-is.
 *
 * On read, delimiter string replaced with NUL byte before calling, so str
 * operations asserted safe in this case. */
typedef int (* io_handler_t) (char const * const msg, size_t len);

io_buf_t * io_buf_init(size_t const capacity);

/* Returns number of left-over bytes, thus a "complete" read is 0; negative on
 * failure.
 *
 * The idea is to preserve the buf in the caller; subsequent reads will start
 * where the buffer was left off.
 *
 * Since the return value returns bytes left, waiting for a "complete" read is
 * as easy as putting this inside a while() condition. */
int io_read_delim(int recv_fd, char * delim, io_buf_t * buf, io_handler_t on_msg);
// TODO: Consider a io_read_delim_all version of the above ^^

int io_write(int send_fd, io_buf * const buf, io_handler_t on_success, io_handler_t on_fail);

/* Alias of io_write_buf that creates an io_buf on the stack, if msg_len is 0,
 * it will be assumed NUL-terminated. */
int io_write_msg(int send_fd, char const * msg, size_t len, bool try_again);
