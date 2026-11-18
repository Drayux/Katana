#include "io_common.h"

#include <sched.h>
#include <string.h>
#include <unistd.h>

io_buf_t * io_buf_init(size_t const capacity)
{
	void * new;
	io_buf_t rval;

	if (!capacity) {
		return NULL;
	}

	new = malloc(sizeof(io_buf_t) + capacity);
	ASSERT_ALLOC(new, NULL);

	memcpy(new, &capacity, sizeof(size_t));

	rval = (io_buf_t *) new;
	rval->filled = 0;

	return rval;
}

/* Returns 0 on success, 1 if not sent but no on_fail handler
 * - Caller asserts send_fd is valid
 * - ~~Caller asserts buf is non-NULL~~
 *
 * Drains any bytes sent, full message passed to on_fail */
int io_write(int send_fd, io_buf * buf,
	io_handler_t on_success,
	io_handler_t on_fail)
{
	ssize_t total_sent = 0;
	ssize_t bytes_sent = 0;

	char * msg_ptr;
	ssize_t send_len;

	int cb_status;

	if (!buf || !buf->capacity || !buf->filled) {
		/* nothing to send */
		// TODO: Use the new macros for interface function error values
		return -1;
	}
	else if (buf->filled > buf->capacity) {
		LOG_ERROR("IO buffer length exceeds its capacity: drop excess bytes");
		buf->filled = buf->capacity;
	}

	msg_ptr = (char *) buf->_buf;

	while (total_sent < buf->filled) {
		send_len = (buf->filled - total_sent) > IO_BLOCK_SIZE ?
			IO_BLOCK_SIZE : (buf->filled - total_sent);
		//
		bytes_sent = write(send_fd, msg_ptr, send_len);

		if (bytes_sent < 0) {
			if (errno == EINTR) {
				/* Always try again if interrupted */
				continue;
			}

			/* Drop the entire buffer by default */
			cb_status = on_fail ? on_fail(buf->_buf, buf->filled) : -1;

			if (errno != EAGAIN) {
				// TODO: strerr is not thread safe, but we expect this to be exceptional
				LOG_ERRORF("write fail: %s", strerror(errno));
				// return buf->filled; // Also TODO: Should we still try again like below?
			}
			else if (cb_status > 0) {
				// TODO: I think this has the risk of yielding, and then
				// another thread can begin a write, causing an unexpected
				// merge of commands
				sched_yield();

				// TODO pt2: I don't have a good way to handle multiple retries
				// on the on_fail handler. For all we know, the wrong
				// implementation could indefinitely loop.
				continue;
			}

			if (cb_status < 0) {
				/* Drop the entire write */
				buf->filled = 0;
			}
			else if (!cb_status) {
				/* Drop only what was sent */
				buf->filled -= total_sent;
				memmove(buf->_buf, &(buf->_buf[total_sent]), buf->filled);
			}
			// else { /* Leave buffer untouched, but don't try again */ }

			return buf->filled; // number of bytes remaining
		}

		/* Successful write call */
		total_sent += bytes_sent;
		msg_ptr += bytes_sent;
	}

	cb_status = on_success ? on_success(buf->_buf, buf->filled) : 0;

	/* Symmetry with on_fail: positive leaves write buf as-is, negative drops
	 * the entire thing, which here is the same as dropping what was written
	 * (aka everything) */
	if (cb_status <= 0) {
		buf->filled = 0;
	}

	return buf->filled;
}

/* io_write would be more than suitable for handling this logic as an alias.
 * That said, it is common that writes are simply a static string and require
 * no additional complexity. Thus, this is a bit of mostly repeated logic for
 * the benefit of a write routine that consumes no additional memory from the
 * side of the program.
 *
 * - Assumes msg is NUL-terminated, caller to assert this
 * - Caller should enclose this in a mutex when multiple threads might write
 *   to the same send_fd and strlen(msg) > IO_BLOCK_SIZE
 *
 * Returns number of bytes left to write (aka 0 on success, positive on err)
 * */
int io_write_msg(int send_fd, char const * msg, bool try_again)
{
	size_t const total_len = msg ? strlen(msg) + 1 : 0;
	ssize_t send_len;

	ssize_t total_sent = 0;
	ssize_t bytes_sent = 0;

	if (!total_len) {
		return 0;
	}

	while (total_sent < total_len) {
		send_len = (buf->filled - total_sent) > IO_BLOCK_SIZE ?
			IO_BLOCK_SIZE : (buf->filled - total_sent);
		//
		bytes_sent = write(send_fd, msg, total_len - total_sent);

		if (bytes_sent < 0) {
			if (errno == EINTR) {
				/* Always try again if interrupted */
				continue;
			}

			if (errno != EAGAIN) {
				LOG_ERRORF("write fail: %s", strerror(errno));
			}
			else if (try_again) {
				/* Send fd is blocked by internal mutex */
				sched_yield();
				continue;
			}

			return total_len - total_sent;
		}

		/* Successful write call */
		total_sent += bytes_sent;
		msg += bytes_sent;
	}

	return 0;
}
