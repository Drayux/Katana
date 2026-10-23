#include "app_cmd.h"

#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

/*
 * This is a many to one model. The main thread is the only receiver.
 * When it receives a shutdown signal, it will exit its primary loop and
 * subsequently invoke the shutdown process for its child threads.
 */


// FOR NEXT TIME!
// Right now I am working on an event loop. The vision is a loop in main
// that returns 0 if a shutdown was received. Otherwise, perform the main
// logic, to be defined in the new timer.c. This will be a big state machine
// depending on the state of the timer. It will respond to events passed in
// from main to determine the action to perform.
//
// It's likely to take a bit of hammering, but I should eventually be able to
// merge the autosplitter logic and the core timing, as the autosplitter
// can also resolve these events.
//
// The mystery pieces are:
// - Clean ways to do the refresh rate -- maybe poll with the timeout?
// - Handling the unloaded state / how to load splits at all
//   ^^ i.e., is it intuitive to have a filepath passed as event data??
// - Not a mystery, but recall that the gui trivially can compute what time
//   to display every frame


/* Nonblocking pipe */
static struct {
    int readfd;
	int writefd;
} event;

int ctl_init(ctl_t *c) {
    int p[2];
    c->shutdown_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (c->shutdown_fd < 0 || pipe2(p, O_NONBLOCK | O_CLOEXEC) < 0) return -1;
    c->cmd_rd = p[0]; c->cmd_wr = p[1];
    return 0;
}

// Callable from any thread (GTK button callback, TUI key handler, ...)
bool ctl_send(ctl_t *c, cmd_t cmd) {
    uint8_t b = (uint8_t)cmd;
    return write(c->cmd_wr, &b, 1) == 1;      // false => pipe full (EAGAIN)
}

// Also safe from a signal handler (only write())
void ctl_shutdown(ctl_t *c) {
    uint64_t one = 1;
    (void)!write(c->shutdown_fd, &one, sizeof one);
}


// worker
struct pollfd p[2] = { {.fd = tfd, .events = POLLIN}, {.fd = s->efd, .events = POLLIN} };
while (poll(p, 2, -1) >= 0) {
    if (p[1].revents & POLLIN) break;      // shutdown
    if (p[0].revents & POLLIN) { /* read timerfd, scan region, start/stop timer */ }
}
