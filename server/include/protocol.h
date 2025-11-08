#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MSG_MAX_LEN 256
#include <stdio.h>

// Forward declaration to break circular dependency
struct player_s;
typedef struct player_s player_t;


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
} server_error_t;



// Message Keys

#define K_COMMAND "command"

#define K_MSG "msg"

#define K_NICKNAME "nickname"

#define K_ROOM_ID "room_id"

#define K_PLAYER_COUNT "player_count"

#define K_MAX_PLAYERS "max_players"

#define K_STATE "state"

#define K_OPPONENT_NICK "opponent_nick"

#define K_YOUR_TURN "your_turn"

#define K_MY_SCORE "my_score"

#define K_OPP_SCORE "opp_score"

#define K_TURN_SCORE "turn_score"

#define K_CURRENT_PLAYER "current_player"

#define K_NUMBER "number"

#define K_ROLL "roll"


int send_error(int socket, server_error_t error);

int send_structured_message(int socket, server_command_t command, int num_args, ...);

ssize_t receive_command(player_t* player, char* out_command_buffer);

#endif // PROTOCOL_H
