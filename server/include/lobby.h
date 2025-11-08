#ifndef LOBBY_H
#define LOBBY_H

#include <pthread.h>
#include <time.h>
#include "config.h"
#include "protocol.h" // For MSG_MAX_LEN

typedef enum
{
	LOBBY,
	IN_GAME
} player_state;

typedef enum
{
	WAITING,
	FULL,
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
	int buffer_len;
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

// Function declarations
void init_lobby();
player_t* add_player(int socket);
void remove_player(player_t* player);
int join_room(int room_id, player_t* player);
room_t* get_room(int room_id);
player_t* find_disconnected_player(const char* nickname);
void leave_room(player_t* player);

#endif // LOBBY_H
