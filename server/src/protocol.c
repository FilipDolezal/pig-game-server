#include "protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static const char* server_command_strings[] = {
    [S_OK] = "OK",
    [S_ERROR] = "ERROR",
    [S_WELCOME] = "WELCOME",
    [S_GAME_PAUSED] = "GAME_PAUSED",
    [S_ROOM_LIST] = "ROOM_LIST",
    [S_ROOM_INFO] = "ROOM_INFO",
    [S_JOIN_OK] = "JOIN_OK",
    [S_GAME_START] = "GAME_START",
    [S_GAME_STATE] = "GAME_STATE",
    [S_GAME_WIN] = "GAME_WIN",
    [S_GAME_LOSE] = "GAME_LOSE",
    [S_OPPONENT_DISCONNECTED] = "OPPONENT_DISCONNECTED",
    [S_OPPONENT_RECONNECTED] = "OPPONENT_RECONNECTED",
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
};

int send_error(const int socket, const server_error_t error)
{
    return send_structured_message(socket, S_ERROR, 1, K_MSG, server_error_strings[error]);
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

ssize_t receive_command(const int socket, char* buffer)
{
    memset(buffer, 0, MSG_MAX_LEN);
    const ssize_t n = read(socket, buffer, MSG_MAX_LEN - 1);
    if (n > 0)
    {
        buffer[strcspn(buffer, "\r\n")] = 0;
    }
    return n;
}
