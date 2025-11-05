#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MSG_MAX_LEN 256

typedef enum {
    // General Messages
    MSG_OK,
    MSG_ERROR,

    // Client to Server
    MSG_LOGIN, // Payload: <nickname>
    MSG_LIST_ROOMS,
    MSG_CREATE_ROOM,
    MSG_JOIN_ROOM, // Payload: <room_id>
    MSG_LEAVE_GAME,
    MSG_ROLL,
    MSG_HOLD,

    // Server to Client
    MSG_WELCOME,
    MSG_ROOM_LIST,
    MSG_ROOM_CREATED,
    MSG_JOIN_OK,
    MSG_WAITING_FOR_OPPONENT,
    MSG_GAME_START,
    MSG_YOUR_TURN,
    MSG_OPPONENT_TURN,
    MSG_ROLLED,
    MSG_BUST,
    MSG_HELD,
    MSG_SCORE_UPDATE,
    MSG_GAME_WIN,
    MSG_GAME_LOSE,
    MSG_OPPONENT_DISCONNECTED
} message_type;

typedef struct {
    message_type type;
    char payload[MSG_MAX_LEN];
} message;

int send_payload(int socket, const char *payload);
int receive_command(int socket, char *buffer);

#endif // PROTOCOL_H