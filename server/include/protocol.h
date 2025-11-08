#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include "lobby.h"

// Commands from Client to Server

#define C_LOGIN "LOGIN"

#define C_RESUME "RESUME"

#define C_LIST_ROOMS "LIST_ROOMS"

#define C_JOIN_ROOM "JOIN_ROOM"

#define C_LEAVE_ROOM "LEAVE_ROOM"

#define C_ROLL "ROLL"

#define C_HOLD "HOLD"

#define C_QUIT "QUIT"

typedef enum
{
	S_OK,
	S_ERROR,
	S_WELCOME,
	S_GAME_PAUSED,
	S_ROOM_LIST,
	S_ROOM_INFO,
	S_JOIN_OK,
	S_GAME_START,
	S_GAME_STATE,
	S_GAME_WIN,
	S_GAME_LOSE,
	S_OPPONENT_DISCONNECTED,
	S_OPPONENT_RECONNECTED,
} server_command_t;

typedef enum
{
	E_INVALID_COMMAND,
	E_INVALID_NICKNAME,
	E_SERVER_FULL,
	E_ROOM_FULL,
	E_GAME_IN_PROGRESS,
	E_CANNOT_JOIN,
	E_OPPONENT_QUIT,
	E_OPPONENT_TIMEOUT,
	E_NICKNAME_IN_USE,
} server_error_t;


// Message Keys

#define K_CMD "cmd"

#define K_MSG "msg"

#define K_NICK "nick"

#define K_ROOM "room"

#define K_MAX "max"

#define K_STATE "state"

#define K_OPP_NICK "opp_nick"

#define K_YOUR_TURN "your_turn"

#define K_MY_SCORE "my_score"

#define K_OPP_SCORE "opp_score"

#define K_TURN_SCORE "turn_score"

#define K_CURRENT "current"

#define K_COUNT "count"

#define K_ROLL "roll"


/**
 * @brief Sends an error message to a client.
 * @param socket The socket file descriptor of the client.
 * @param error The error to send.
 * @return The number of bytes sent, or -1 on error.
 */
int send_error(int socket, server_error_t error);

/**
 * @brief Sends a structured message to a client.
 * @param socket The socket file descriptor of the client.
 * @param command The command to send.
 * @param num_args The number of key-value arguments to follow.
 * @param ... A variable number of key-value pairs (const char* key, const char* value).
 * @return The number of bytes sent, or -1 on error.
 */
int send_structured_message(int socket, server_command_t command, int num_args, ...);

/**
 * @brief Receives a command from a client, handling partial reads.
 * @param player A pointer to the player_t object.
 * @param out_command_buffer The buffer to store the received command.
 * @return The number of bytes in the command, 0 on disconnect, -1 on error, -2 on buffer full.
 */
ssize_t receive_command(player_t* player, char* out_command_buffer);

#endif // PROTOCOL_H
