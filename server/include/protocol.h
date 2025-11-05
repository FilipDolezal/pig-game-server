#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MSG_MAX_LEN 256
#include <stdio.h>

// Commands from Client to Server

#define C_LOGIN "LOGIN"

#define C_RESUME "RESUME"

#define C_LIST_ROOMS "LIST_ROOMS"

#define C_JOIN_ROOM "JOIN_ROOM"

#define C_LEAVE_ROOM "LEAVE_ROOM"

#define C_ROLL "ROLL"

#define C_HOLD "HOLD"

#define C_QUIT "QUIT"


// Commands from Server to Client

#define S_ACK "ACK"

#define S_NACK "NACK"

#define S_WELCOME "WELCOME"

#define S_GAME_PAUSED "GAME_PAUSED"

#define S_ROOM_INFO "ROOM_INFO"

#define S_JOIN_OK "JOIN_OK"

#define S_GAME_START "GAME_START"

#define S_GAME_STATE "GAME_STATE"

#define S_GAME_WIN "GAME_WIN"

#define S_GAME_LOSE "GAME_LOSE"

#define S_OPPONENT_DISCONNECTED "OPPONENT_DISCONNECTED"


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

#define K_P1_SCORE "p1_score"

#define K_P2_SCORE "p2_score"

#define K_TURN_SCORE "turn_score"

#define K_CURRENT_PLAYER "current_player"

#define K_NUMBER "number"

int send_ack(int socket, const char* command, const char* msg);
int send_nack(int socket, const char* command, const char* msg);
int send_structured_message(int socket, const char* command, int num_args, ...);
ssize_t receive_command(const int socket, char* buffer);

#endif // PROTOCOL_H
