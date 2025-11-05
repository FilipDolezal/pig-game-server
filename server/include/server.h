#ifndef SERVER_H
#define SERVER_H

int run_server(int port);
void *client_handler_thread(void *arg);
void *game_thread_func(void *arg);

#endif // SERVER_H
