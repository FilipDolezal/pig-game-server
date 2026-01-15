/*
 * protocol.c - Message serialization and TCP buffering
 *
 * Message format: COMMAND|key1:value1|key2:value2\n
 * Example: GAME_STATE|my_score:10|opp_score:5|turn_score:3|roll:4|your_turn:1\n
 *
 * TCP can split/combine messages, so receive_command() buffers partial reads
 * and extracts complete lines (ending with \n).
 */

#include "protocol.h"
#include "lobby.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>

static const char* server_command_strings[] = {
	[S_OK] = "OK",
	[S_ERROR] = "ERROR",
	[S_WELCOME] = "WELCOME",
	[S_GAME_PAUSED] = "GAME_PAUSED",
	[S_ROOM_INFO] = "ROOM_INFO",
	[S_GAME_START] = "GAME_START",
	[S_GAME_STATE] = "GAME_STATE",
	[S_GAME_WIN] = "GAME_WIN",
	[S_GAME_LOSE] = "GAME_LOSE",
	[S_OPPONENT_DISCONNECTED] = "OPPONENT_DISCONNECTED",
	[S_OPPONENT_RECONNECTED] = "OPPONENT_RECONNECTED",
	[S_DISCONNECTED] = "DISCONNECTED",
};

static const char* server_error_strings[] = {
	[E_INVALID_COMMAND] = "INVALID_COMMAND",
	[E_INVALID_NICKNAME] = "INVALID_NICKNAME",
	[E_SERVER_FULL] = "SERVER_FULL",
	[E_ROOM_FULL] = "ROOM_FULL",
	[E_GAME_IN_PROGRESS] = "GAME_IN_PROGRESS",
	[E_CANNOT_JOIN] = "CANNOT_JOIN",
	[E_OPPONENT_QUIT] = "OPPONENT_QUIT",
	[E_OPPONENT_TIMEOUT] = "OPPONENT_TIMEOUT",
	[E_NICKNAME_IN_USE] = "NICKNAME_IN_USE",
};

int send_error(const int socket, const char* command_str, const server_error_t error)
{
	return command_str != NULL
		? send_structured_message(socket, S_ERROR, 2, K_MSG, server_error_strings[error], K_CMD, command_str)
		: send_structured_message(socket, S_ERROR, 1, K_MSG, server_error_strings[error]);
}

int send_structured_message(const int socket, const server_command_t command, const int num_args, ...)
{
	char buffer[MSG_MAX_LEN];
	int offset = snprintf(buffer, MSG_MAX_LEN, "%s", server_command_strings[command]);

	va_list args;
	va_start(args, num_args);
	for (int i = 0; i < num_args; ++i)
	{
		const char* key = va_arg(args, const char *);
		const char* value = va_arg(args, const char *);
		offset += snprintf(buffer + offset, MSG_MAX_LEN - offset, "|%s:%s", key, value);
	}
	va_end(args);

	strcat(buffer, "\n");
	return send(socket, buffer, strlen(buffer), 0);
}

ssize_t receive_command(player_t* player, char* out_command_buffer, size_t buffer_size)
{
	// Search for a newline in the existing buffer
	const char* newline_ptr = strchr(player->read_buffer, '\n');

	// If no newline, read more data from the socket
	while (newline_ptr == NULL)
	{
		// Check if buffer is full before reading
		if (player->buffer_len >= sizeof(player->read_buffer) - 1)
		{
			// Protocol violation or garbage in buffer. Clear it and report error.
			player->buffer_len = 0;
			player->read_buffer[0] = '\0';
			return -2; // Special error for "line too long" or un-parsable buffer
		}

		const ssize_t bytes_read = read(
			player->socket,
			player->read_buffer + player->buffer_len,
			sizeof(player->read_buffer) - 1 - player->buffer_len
		);

		if (bytes_read > 0)
		{
			player->buffer_len += bytes_read;
			player->read_buffer[player->buffer_len] = '\0'; // Null-terminate
			newline_ptr = strchr(player->read_buffer, '\n');
		}
		else if (bytes_read == 0)
		{
			// Graceful disconnect (peer closed connection)
			return 0;
		}
		else
		{
			// read() returned -1 - check errno
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// Socket timeout - not a disconnect, just no data available
				return -3;
			}
			// Other error (connection reset, etc.) - treat as disconnect
			return -1;
		}
	}

	// A full command (ending in \n) is in the buffer. Extract it.
	size_t cmd_len = newline_ptr - player->read_buffer;

	// Trim trailing \r if it exists (for CRLF line endings)
	if (cmd_len > 0 && player->read_buffer[cmd_len - 1] == '\r')
	{
		cmd_len--;
	}

	// Check if output buffer is large enough
	if (cmd_len >= buffer_size)
	{
		// Command too long for output buffer. Consume it and report error.
		player->buffer_len -= (newline_ptr - player->read_buffer + 1);
		memmove(player->read_buffer, newline_ptr + 1, player->buffer_len);
		player->read_buffer[player->buffer_len] = '\0';
		return -2;
	}

	// Copy the command to the output buffer and null-terminate it
	strncpy(out_command_buffer, player->read_buffer, cmd_len);
	out_command_buffer[cmd_len] = '\0';

	// Remove the extracted command (and the \n) from the player's buffer
	// by shifting the remaining data to the beginning.
	player->buffer_len -= (newline_ptr - player->read_buffer + 1);
	memmove(player->read_buffer, newline_ptr + 1, player->buffer_len);
	player->read_buffer[player->buffer_len] = '\0';

	// Update last activity timestamp on successful command receive
	player->last_activity = time(NULL);

	return cmd_len; // Return length of the command
}
