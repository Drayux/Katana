#include "ipc_server.h"

#include "environment.h"
#include "logging.h"

#include <arpa/inet.h>
#include <glib.h>
#include <linux/limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern atomic_bool exit_requested;

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

/**
 * Command execution function that runs on the main thread
 */
static gboolean execute_command_on_main_thread(gpointer data)
{
	CommandData * cmd_data = (CommandData *) data;

	// Call the main.c function to handle the command
	handle_ctl_command(cmd_data->command);

	g_free(cmd_data);
	return FALSE; // Remove from idle queue
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
static void * ipc_server_thread(void * arg)
{
	char * socket_path;
	int server_fd;

	prctl(PR_SET_NAME, "LS CTL Server", 0, 0, 0);

	server_fd = ipc_create_listener();
	if (server_fd < 0) {
		return NULL;
	}

	while (1) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(server_fd, &fds);

		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 50 * 1000; // 50ms

		int ret = select(server_fd + 1, &fds, NULL, NULL, &tv);

		if (ret < 0) {
			perror("select failed");
			continue;
		}

		if (ret == 0) { // Timeout
			if (atomic_load(&exit_requested)) {
				LOG_DEBUG("Exit requested, shutting down Server Thread");
				break;
			}
		}

		// If we reach here, the listening socket is readable
		if (FD_ISSET(server_fd, &fds)) {

			int client_fd = accept(server_fd, NULL, NULL);
			if (client_fd < 0) {
				perror("Failed to accept client connection");
				continue; // do not stop the server on accept errors
			}

			CTLMessage * msg = NULL;
			int result = receive_message(client_fd, &msg);

			if (result == 0) {
				if (msg->length == sizeof(CTLCommand)) {
					// CTLCommand command = *(CTLCommand *) msg->message;
					// CommandData * cmd_data = g_malloc(sizeof(CommandData));
					// cmd_data->command = command;

					// Queue command execution on main thread
					// g_idle_add(execute_command_on_main_thread, cmd_data);

					printf("Received data: %d\n", *((int *) msg->message));
				}
				else {
					printf("Invalid message length: %u (expected %zu)\n",
						msg->length, sizeof(CTLCommand));
				}

				free(msg);
			}
			else {
				printf("Client closed without sending a full message.\n");
			}

			close(client_fd);
		}
	}

	LOG_INFO("IPC server thread exiting");
	close(server_fd);
	if ((socket_path = ipc_get_socket_path())) {
		unlink(socket_path);
		free(socket_path);
	}

	return NULL;
}

// Launches the IPC server, and returns the thread, if successful.
void ipc_server_thread_start(pthread_t * thread)
{
	if (!thread) {
		LOG_ERR("NULL arg passed");
		return;
	}
	pthread_create(thread, NULL, &ipc_server_thread, NULL);
}
