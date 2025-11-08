#ifndef SERVER_H
#define SERVER_H
#include "lobby.h"
#include "game.h"

/** Runs the game server, handling client connections and game sessions. */
int run_server(int port);

void broadcast_game_start(const room_t* room, int first_to_act);

void broadcast_game_state(const room_t* room, const game_state* game);

void* client_handler_thread(void* arg);

void* game_thread_func(void* arg);

#endif // SERVER_H
