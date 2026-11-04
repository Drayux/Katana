#include "ipc_server.h"

#include "environment.h"
#include "logging.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int notify_fd = 0;

/**
 * Structure to pass command data to main thread
 */
typedef struct CommandData {
	CTLCommand command; /*!< The command to send to the main thread */
} CommandData;

/**
 * External functions from main.c to handle commands
 *
 * @param command The command to be handled.
 *
 * TODO: Stubbed out for now, figure out how to plug this in after refactoring
 * the timer logic.
 * That said, it should be something a handler can subscribe to, so that
 * plugins can create CTL comamnds as well
 */
static void handle_ctl_command(CTLCommand command)
{
	// if (!g_app) {
	// LOG_INFO("No application instance available to handle commands");
	// return;
	// }

	// LSAppWindow * win = ls_get_main_app_window();
	// if (!win) {
	// LOG_INFO("No window available to handle commands");
	// return;
	// }

	switch (command) {
		case CTL_CMD_START_SPLIT:
			LOG_DEBUG("Split requested via Server Command");
			// timer_start_split(win);
			break;
		case CTL_CMD_STOP_RESET:
			LOG_DEBUG("Run Stop/Reset requested via Server Command");
			// timer_stop_or_reset(win);
			break;
		case CTL_CMD_CANCEL:
			LOG_DEBUG("Run Cancellation requested via Server Command");
			// timer_cancel_run(win);
			break;
		case CTL_CMD_UNSPLIT:
			LOG_DEBUG("Unsplit requested via Server Command");
			// timer_unsplit(win);
			break;
		case CTL_CMD_SKIP:
			LOG_DEBUG("Skip requested via Server Command");
			// timer_skip(win);
			break;
		case CTL_CMD_EXIT:
			LOG_DEBUG("Exit requested via Server Command");
			// ls_app_window_quit(win);
			break;
		default:
			LOG_INFOF("Unknown CTL command: %d", command);
			break;
	}
}

// Forward declarations for command handlers

/**
 * Receives a message from the socket.
 *
 * @param sockfd The socket file descriptor.
 * @param out Pointer to the CTLMessage struct used as output.
 *
 * @return Zero if a full message was received, non-zero otherwise.
 */
static int receive_message(int sockfd, CTLMessage ** out)
{
	// Read header (blocking)
	uint32_t len = 0;
	ssize_t n = read(sockfd, &len, sizeof(len));
	if (n == 0)
		return 1; // client closed connection immediately
	len = ntohl(len);
	if (n != sizeof(len))
		return -1;

	CTLMessage * msg = malloc(sizeof(CTLMessage) + len);
	if (!msg) {
		fprintf(stderr, "Failed to allocate memory for control message.\n");
		return -1;
	}
	msg->length = len;

	size_t received = 0;
	while (received < len) {
		n = read(sockfd, msg->message + received, len - received);
		if (n <= 0) {
			free(msg);
			return -1;
		}
		received += n;
	}

	*out = msg;
	return 0;
}

/**
 * The remote control server thread.
 *
 * @param arg The arguments to pass to the thread
 */
static void * ipc_server_main(void * arg)
{
	int ipc_server_fd;
	int ready;

	prctl(PR_SET_NAME, "LS CTL Server", 0, 0, 0);

	notify_fd = eventfd(0, O_NONBLOCK | O_CLOEXEC);
    if (notify_fd < 0) {
		LOG_ERROR("eventfd fail: could not create UI thread wake event fd");
		return NULL;
	}

	ipc_server_fd = ipc_create_listener();
	if (ipc_server_fd < 0) {
		close(notify_fd);
		return NULL;
	}

	struct pollfd fds[2] = {
		{
			.fd = notify_fd,
			.events = POLLIN
		},
		{
			.fd = ipc_server_fd,
			.events = POLLIN | POLLHUP | POLLERR
		},
	};

	while (1) {
		ready = poll(fds, 2, -1);
		if (ready < 0) {
			if (errno == EINTR) {
				continue;
			}
			LOG_ERROR("poll error");
			perror("poll");
			break;
		}

		/* Unlike logger thread, prioritize shutdown first */
		if (fds[0].revents & POLLIN) {
			/* IPC thread only notified for shutdown */
			CLEAR_EVENT(fds[0].fd);
			break;
		}

		if (fds[1].revents & POLLIN) {
			int client_fd = accept(ipc_server_fd, NULL, NULL);
			if (client_fd < 0) {
				perror("accept fail");
				continue; // do not stop the server on accept errors
			}

			CTLMessage * msg = NULL;
			int result = receive_message(client_fd, &msg);

			if (result == 0) {
				if (msg->length == sizeof(CTLCommand)) {
					// TODO: I think I wanted to clean up this a bit to match
					// more closely with that of app_cmd.
					// CTLCommand command = *(CTLCommand *) msg->message;
					// CommandData * cmd_data = g_malloc(sizeof(CommandData));
					// cmd_data->command = command;

					// Queue command execution on main thread
					// g_idle_add(execute_command_on_main_thread, cmd_data);

					LOG_DEBUGF("Received data: %d", *((int *) msg->message));
				}
				else {
					LOG_WARNF("Invalid message length: %u (expected %zu)",
						msg->length, sizeof(CTLCommand));
				}
				free(msg);
			}
			else {
				LOG_DEBUG("IPC client closed before message completed");
			}

			close(client_fd);
		}
		else if (fds[1].revents & (POLLHUP | POLLERR)) {
			LOG_ERROR("IPC socket failure (TODO not sure what all could happen here)");
			break;
		}
	}

	LOG_INFO("IPC server thread exiting");
    if (notify_fd > 2) {
		close(notify_fd);
	}
    if (ipc_server_fd > 2) {
		close(ipc_server_fd);
	}

	return NULL;
}

// Launches the IPC server, and returns the thread, if successful.
void ipc_server_thread_start(pthread_t * thread)
{
	if (!thread) {
		LOG_ERR("IPC server thread is NULL");
		return;
	}
	pthread_create(thread, NULL, &ipc_server_main, NULL);
}

void ipc_server_thread_shutdown(void)
{
	char * socket_path;

	SET_EVENT(notify_fd);

	if ((socket_path = ipc_get_socket_path())) {
		unlink(socket_path);
		free(socket_path);
	}
}
