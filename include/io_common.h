#pragma once

#include "common.h"

#include <errno.h>
// #include <linux/limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define IO_BLOCK_SIZE 4096 /* Max size for a single I/O operation */
// ^^ TODO: Consider using PIPE_BUF instead

/* Structure for preserving partial message transmissions */
typedef struct {
	size_t const capacity; /* max number of bytes in buf */
	size_t filled; /* number of preserved bytes in buf */
	int status;
	char _buf[];
} io_buf_t;

/* Message handler type. Return 0 to drain the message from the buffer,
 * negative to clear the entire buffer, and positive to leave it as-is. */
typedef int (* io_handler_t) (io_buf_t * const buf);
/* msg lifetime not to exceed the handler: strdup if necessary;
 * msg will be NUL-terminated */
typedef int (* io_msg_handler_t) (char const * msg, size_t len);

io_buf_t * io_buf_create(size_t capacity);
// ^^ destroyed with free(io_buf)

/* Fills the io_buf with as many bytes can be read in one pass */
int io_read(int recv_fd, io_buf_t * buf,
	io_handler_t on_msg, io_handler_t on_fail);

/* Returns number of left-over bytes, thus a "complete" read is 0; negative on
 * failure. */
int io_read_delim(int recv_fd, char delim, io_buf_t * buf,
	io_msg_handler_t on_msg, io_handler_t on_fail);
// TODO: Consider a io_read_delim_all version of the above ^^
// TODO: Consider version with a multi-byte delimiter

int io_write(int send_fd, io_buf_t * buf,
	io_handler_t on_success, io_handler_t on_fail);


/* -- "Direct" versions, no new memory allocations -- */

int io_read_msg(int recv_fd, void * buf, size_t len, bool try_again);

/* Alias of io_write_buf that creates an io_buf on the stack, if msg_len is 0,
 * it will be assumed NUL-terminated. */
int io_write_msg(int send_fd, void * msg, size_t len, bool try_again);


/* -- Event FD utility -- */

/* Cleans up the multitude of eventfd notifications                /
 * This exists because of the explicit 8-byte eventfd I/O         */
#define SET_EVENT(fd)                                              \
    do {                                                           \
        uint8_t _wake[8] = { 1 };                                  \
		(void) !write((fd), _wake, 8);                             \
    } while (0)
#define CLEAR_EVENT(fd)                                            \
	do {                                                           \
		ssize_t _len;                                              \
		uint8_t _buf[8];                                           \
		while ((_len = read((fd), _buf, 8))) {                     \
			if (_len < 0) {                                        \
				if (errno != EAGAIN) {                             \
					LOG_ERROR("read: notify fd error");            \
				}                                                  \
				break;                                             \
			}                                                      \
			else if (_len != 8) {                                  \
				LOG_ERRORF("read: unexpected event len %d", _len); \
			}                                                      \
		}                                                          \
	} while (0)

#define DESTROY_FD(fd)                                             \
    do {                                                           \
		if ((fd) > STDERR_FILENO) {                                \
			close((fd));                                           \
			(fd) = -1;                                             \
		}                                                          \
    } while (0)
