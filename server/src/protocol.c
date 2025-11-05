#include "protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

int send_payload(int socket, const char *payload) {
    char buffer[MSG_MAX_LEN + 1];
    strncpy(buffer, payload, MSG_MAX_LEN);
    buffer[MSG_MAX_LEN] = '\0'; // Ensure null termination
    strcat(buffer, "\n");
    return send(socket, buffer, strlen(buffer), 0);
}

int receive_command(int socket, char *buffer) {
    memset(buffer, 0, MSG_MAX_LEN);
    int n = read(socket, buffer, MSG_MAX_LEN - 1);
    if (n > 0) {
        buffer[strcspn(buffer, "\r\n")] = 0; // Trim newline
    }
    return n;
}