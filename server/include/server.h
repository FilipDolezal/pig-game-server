#ifndef SERVER_H
#define SERVER_H
#include "lobby.h"
#include "game.h"

/**
 * @brief Runs the main server loop, accepting and handling client connections.
 * @param port The port number to listen on.
 * @param address The IP address to bind to.
 * @return 0 on success, -1 on error.
 */
int run_server(int port, const char* address);

/**
 * @brief Sends GAME_WIN/GAME_LOSE messages to players when the game ends.
 * @param room The room where the game finished.
 * @param game The final game state (contains winner info).
 */
void broadcast_game_over(const room_t* room, const game_state* game);

/**
 * @brief Broadcasts the start of a game to both players in a room.
 * @param room The room where the game is starting.
 * @param first_to_act The index of the player who will act first.
 */
void broadcast_game_start(const room_t* room, int first_to_act);

/**
 * @brief Broadcasts the current state of the game to both players.
 * @param room The room where the game is being played.
 * @param game The current state of the game.
 */
void broadcast_game_state(const room_t* room, const game_state* game);

/**
* @brief Send user an OK response with the game status on RESUME
*  @param player The resuming player
 * @param room The room where the game is being played.
 * @param game The current state of the game.
*/
void send_game_state(const player_t* player, const room_t* room, const game_state* game);

/**
 * @brief The main thread function for handling a single client connection.
 * @param arg A pointer to the player_t object for the connected client.
 * @return NULL.
 */
void* client_handler_thread(void* arg);

/**
 * @brief The main thread function for managing a single game session.
 * @param arg A pointer to the room_t object for the game.
 * @return NULL.
 */
void* game_thread_func(void* arg);

#endif // SERVER_H
