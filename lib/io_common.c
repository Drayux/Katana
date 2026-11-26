#include "io_common.h"

#include <errno.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>

/* TODO: For now, all of these interfaces assume that the socket is set up with
 * the non-blocking flag. I'm not sure how I want to handle this internally, be
 * it with an `io_open` function, or perhaps a `#define IO_FLAGS` but it's
 * worth consideration. */

io_buf_t * io_buf_create(size_t capacity)
{
	void * new;
	io_buf_t * rval;

	if (!capacity) {
		return NULL;
	}

	new = malloc(sizeof(io_buf_t) + capacity);
	ASSERT_ALLOC(new, NULL);

	memcpy(new, &capacity, sizeof(size_t));

	rval = (io_buf_t *) new;
	rval->filled = 0;
	rval->status = 0; // OK

	return rval;
}

/* Drain a specified number of bytes from the io buffer into out_buf, NULL OK
 * -- returns length of bytes drained
 *
 * Caller should assert that out_buf can store len bytes */
size_t io_drain(char * out_buf, io_buf_t * io_buf, size_t len)
{
	size_t const drain_len = (len > io_buf->filled) ?
		io_buf->filled : len;

	if (out_buf) {
		memcpy(out_buf, io_buf->_buf, drain_len);
	}

	io_buf->filled -= drain_len;
	if (io_buf->filled > 0) {
		memmove(io_buf->_buf, io_buf->_buf + drain_len, io_buf->filled);
	}

	return drain_len;
}

/* Returns leftover bytes in buffer (read_len on success if no handler given)
 * - Caller asserts send_fd is valid
 *
 * Drains any bytes sent, full message passed to on_fail */
int io_read(int recv_fd, io_buf_t * buf,
	io_handler_t on_success,
	io_handler_t on_fail)
{
	ssize_t total_read = 0;
	ssize_t bytes_read = 0;

	char * msg_ptr;
	ssize_t read_len; // length to try to read

	int rd_status;
	int cb_status;

	if (!buf) {
		/* no room for a read */
		return -1; // TODO: Use new macros for interface function error values
	}

	msg_ptr = (char *) buf->_buf + buf->filled;
	buf->status = (int) IO_OK; // 0

	while ((read_len = buf->capacity - buf->filled) > 0) {
		read_len = read_len < IO_BLOCK_SIZE ?
			read_len : IO_BLOCK_SIZE;
		//
		bytes_read = read(recv_fd, msg_ptr, read_len);

		if (bytes_read < 0) {
			rd_status = errno;
			if (rd_status == EINTR) {
				continue;
			}
			else if (rd_status == EAGAIN || rd_status == EWOULDBLOCK) {
				break; /* most reads are complete once hitting EAGAIN */
			}

			// TODO: strerr is not thread safe, but we expect this to be exceptional
			LOG_ERRORF("read fail: %s", strerror(rd_status));

			/* Preserve original buffer by default */
			buf->status = -rd_status;
			cb_status = on_fail ? on_fail(buf) : 0;

			if (cb_status < 0) {
				buf->filled = 0; // drop entire buffer
			}
			else if (!cb_status) {
				buf->filled -= total_read; // drop only this call
			}
			// else { /* Leave buffer untouched */ }

			return buf->filled;
		}

		/* -- Successful read call -- */
		total_read += bytes_read;
		msg_ptr += bytes_read;
		buf->filled += bytes_read;
	}

	if (buf->capacity == buf->filled) {
		buf->status = (int) IO_FULL;
	}
	cb_status = on_success ? on_success(buf) : 1;

	/* Symmetry with on_fail: positive leaves read buf as-is so the caller may
	 * use its contents, thus the default if no handler is set. */
	if (cb_status <= 0) {
		buf->filled = 0;
	}

	return buf->filled;
}

/* Returns number of left-over bytes, thus a "complete" read is 0; negative on
 * failure.
 *
 * The idea is to preserve the buf in the caller; subsequent reads will start
 * where the buffer was left off.
 *
 * Since the return value returns bytes left, waiting for a "complete" read is
 * as easy as putting this inside a while() condition. */
int io_read_delim(int recv_fd, char delim, io_buf_t * buf,
	io_msg_handler_t on_msg,
	io_handler_t on_fail)
{
	int total_read;
	int cb_status;

	char * delim_ptr;

	char * msg;
	size_t msg_len;

	total_read = io_read(recv_fd, buf, NULL, on_fail);
	if (!total_read) {
		return buf->status; // buf empty, all operations are the same
	}

	delim_ptr = memchr(buf->_buf, delim, buf->filled);
	if (!delim_ptr) {
		/* Partial read, neither success_cb nor fail_cb should be called */
		buf->status = (int) IO_PARTIAL;
		return buf->filled;
	}

	if (on_msg) {
		msg_len = delim_ptr - buf->_buf;
		msg = alloca(msg_len + 1);

		memcpy(msg, buf->_buf, msg_len);
		msg[msg_len] = '\0';

		cb_status = on_msg(msg, msg_len);
	}
	else {
		/* Preserve buffer contents if there is no handler */
		cb_status = 1;
	}

	if (cb_status < 0) {
		buf->filled = 0; // drop entire buffer
	}
	else if (!cb_status) {
		/* Drop only what was extracted */
		io_drain(NULL, buf, msg_len + 1);
	}
	// else { /* Leave buffer untouched */ }

	return buf->filled;
}

/* Returns leftover bytes in buffer (0 on success if no handler given)
 * - Caller asserts send_fd is valid
 *
 * Drains any bytes sent, full message passed to on_fail */
int io_write(int send_fd, io_buf_t * buf,
	io_handler_t on_success,
	io_handler_t on_fail)
{
	ssize_t total_sent = 0;
	ssize_t bytes_sent = 0;

	char * msg_ptr;
	ssize_t send_len;

	int wr_status;
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
	buf->status = (int) IO_OK; // 0

	while ((send_len = buf->filled - total_sent) > 0) {
		send_len = send_len < IO_BLOCK_SIZE ?
			send_len : IO_BLOCK_SIZE;
		//
		bytes_sent = write(send_fd, msg_ptr, send_len);

		if (bytes_sent < 0) {
			wr_status = errno;
			if (wr_status == EINTR) {
				/* Always try again if interrupted */
				continue;
			}

			/* Drop the entire buffer by default */
			// TODO: Should I handle EAGAIN differently? (try again logic)
			buf->status = -wr_status;
			cb_status = on_fail ? on_fail(buf) : -1;

			if (!(wr_status == EAGAIN || wr_status == EWOULDBLOCK)) {
				// TODO: strerr is not thread safe, but we expect this to be exceptional
				LOG_ERRORF("write fail: %s", strerror(wr_status));
				// return buf->filled; // Also TODO: Should we still try again like below?
			}
			/* else if (cb_status > 0) {
				// TODO: I think this has the risk of yielding, and then
				// another thread can begin a write, causing an unexpected
				// merge of commands
				sched_yield();
				continue;
			} */

			if (cb_status < 0) {
				buf->filled = 0; // drop the entire write
			}
			else if (!cb_status) {
				/* Drop only what was sent */
				buf->filled -= total_sent;
				memmove(buf->_buf, &(buf->_buf[total_sent]), buf->filled);
			}
			// else { /* Leave buffer untouched */ }

			return buf->filled; // number of bytes remaining
		}

		/* -- Successful write call -- */
		total_sent += bytes_sent;
		msg_ptr += bytes_sent;
	}

	cb_status = on_success ? on_success(buf) : 0;

	/* Symmetry with on_fail: positive leaves write buf as-is, negative drops
	 * the entire thing, which here is the same as dropping what was written
	 * (aka everything) */
	if (cb_status <= 0) {
		buf->filled = 0;
	}

	return buf->filled;
}

int io_read_msg(int recv_fd, void * buf, size_t len, bool try_again)
{
	// TODO (actually I may have no use for this at all)
	return -1;
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
int io_write_msg(int send_fd, void * msg, size_t len, bool try_again)
{
	size_t const total_len = len ? len :
		(msg ? strlen((char const *) msg) + 1 : 0);
	ssize_t send_len;

	ssize_t total_sent = 0;
	ssize_t bytes_sent = 0;

	if (!total_len) {
		return 0;
	}
	else if (send_fd < 0) {
		return total_len;
	}

	while ((send_len = total_len - total_sent) > 0) {
		send_len = send_len < IO_BLOCK_SIZE ?
			send_len : IO_BLOCK_SIZE;
		//
		bytes_sent = write(send_fd, msg, send_len);

		if (bytes_sent < 0) {
			if (errno == EINTR) {
				/* Always try again if interrupted */
				continue;
			}

			if (errno != EAGAIN) {
				LOG_ERRORF("write fail: %s", strerror(errno));
			}
			/* else if (try_again) {
				// Send fd is blocked by internal mutex
				sched_yield();
				continue;
			} */

			return total_len - total_sent;
		}

		/* -- Successful write call -- */
		total_sent += bytes_sent;
		msg += bytes_sent;
	}

	return 0;
}
