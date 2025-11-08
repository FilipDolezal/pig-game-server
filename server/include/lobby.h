#ifndef LOBBY_H
#define LOBBY_H

#include <pthread.h>
#include <time.h>
#include "config.h"

typedef enum
{
	LOBBY,
	IN_GAME
} player_state;

typedef enum
{
	WAITING,
	IN_PROGRESS,
	PAUSED,
	ABORTED
} room_state;

typedef struct player_s
{
	int socket;
	char nickname[NICKNAME_LEN];
	player_state state;
	int room_id;
	time_t disconnected_timestamp;
	char read_buffer[MSG_MAX_LEN * 2];
	size_t buffer_len;
} player_t;

typedef struct room_s
{
	int id;
	room_state state;
	player_t* players[MAX_PLAYERS_PER_ROOM];
	int player_count;
	pthread_t game_thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} room_t;

extern player_t* players;
extern room_t* rooms;

// Function declarations
/**
 * @brief Initializes the lobby, allocating memory for players and rooms.
 */
void init_lobby();

/**
 * @brief Adds a new player to the lobby.
 * @param socket The socket file descriptor for the new player.
 * @return A pointer to the newly created player_t object, or NULL if the server is full.
 */
player_t* add_player(int socket);

/**
 * @brief Removes a player from the lobby and any room they were in.
 * @param player A pointer to the player_t object to remove.
 */
void remove_player(player_t* player);

/**
 * @brief Adds a player to a game room.
 * @param room_id The ID of the room to join.
 * @param player A pointer to the player_t object joining the room.
 * @return 0 on success, -1 on failure (e.g., room is full or in progress).
 */
int join_room(int room_id, player_t* player);

/**
 * @brief Retrieves a pointer to a room by its ID.
 * @param room_id The ID of the room to retrieve.
 * @return A pointer to the room_t object, or NULL if the ID is invalid.
 */
room_t* get_room(int room_id);

/**
 * @brief Finds a disconnected player by their nickname.
 * @param nickname The nickname of the player to find.
 * @return A pointer to the player_t object if found, otherwise NULL.
 */
player_t* find_disconnected_player(const char* nickname);

/**
 * @brief Removes a player from a waiting room.
 * @param player A pointer to the player_t object to remove from the room.
 */
void leave_room(player_t* player);

#endif // LOBBY_H
