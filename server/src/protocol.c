#include "protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

int send_nack(const int socket, const char* command, const char* msg)
{
    return send_structured_message(socket, S_NACK, 2, K_COMMAND, command, K_MSG, msg);
}

int send_ack(const int socket, const char* command, const char* msg)
{
    return msg
           ? send_structured_message(socket, S_ACK, 2, K_COMMAND, command, K_MSG, msg)
           : send_structured_message(socket, S_ACK, 1, K_COMMAND, command);
}

int send_structured_message(const int socket, const char* command, const int num_args, ...)
{
    char buffer[MSG_MAX_LEN];
    int offset = snprintf(buffer, MSG_MAX_LEN, "%s", command);

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
