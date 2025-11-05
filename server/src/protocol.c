#include "protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

const char* CMD_LOGIN = "LOGIN";
const char* CMD_LIST_ROOMS = "LIST_ROOMS";
const char* CMD_CREATE_ROOM = "CREATE_ROOM";
const char* CMD_JOIN_ROOM = "JOIN_ROOM";
const char* CMD_ROLL = "roll";
const char* CMD_HOLD = "hold";

ssize_t send_payload(const int socket, const char *payload) {
    char buffer[MSG_MAX_LEN + 1];
    strncpy(buffer, payload, MSG_MAX_LEN);
    buffer[MSG_MAX_LEN] = '\0';
    strcat(buffer, "\n");
    return send(socket, buffer, strlen(buffer), 0);
}

ssize_t receive_command(const int socket, char *buffer) {
    memset(buffer, 0, MSG_MAX_LEN);
    const ssize_t n = read(socket, buffer, MSG_MAX_LEN - 1);
    if (n > 0) {
        buffer[strcspn(buffer, "\r\n")] = 0;
    }
    return n;
}