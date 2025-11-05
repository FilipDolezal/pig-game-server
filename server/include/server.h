#ifndef SERVER_H
#define SERVER_H
#include <lobby.h>

/** Runs the game server, handling client connections and game sessions. */
int run_server(int port);

void send_game_state(room_t* room, game_state* game);

void* client_handler_thread(void* arg);

void* game_thread_func(void* arg);

#endif // SERVER_H
