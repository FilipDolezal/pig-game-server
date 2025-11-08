#include "lobby.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global arrays for players and rooms
static player_t players[MAX_PLAYERS];
static room_t rooms[MAX_ROOMS];
static int player_count = 0;
static pthread_mutex_t lobby_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declaration for static helper function
static void remove_player_from_room(room_t* room, player_t* player);

void init_lobby()
{
	pthread_mutex_lock(&lobby_mutex);
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		players[i].socket = -1;
		players[i].nickname[0] = '\0';
		players[i].state = LOBBY;
		players[i].room_id = -1;
	}
	for (int i = 0; i < MAX_ROOMS; ++i)
	{
		rooms[i].id = i;
		rooms[i].state = WAITING;
		rooms[i].player_count = 0;
		for (int j = 0; j < MAX_PLAYERS_PER_ROOM; j++)
		{
			rooms[i].players[j] = NULL;
		}
		pthread_mutex_init(&rooms[i].mutex, NULL);
		pthread_cond_init(&rooms[i].cond, NULL);
	}
	player_count = 0;
	pthread_mutex_unlock(&lobby_mutex);
}

player_t* add_player(const int socket)
{
	pthread_mutex_lock(&lobby_mutex);
	if (player_count >= MAX_PLAYERS)
	{
		pthread_mutex_unlock(&lobby_mutex);
		return NULL;
	}
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (players[i].socket == -1)
		{
			players[i].socket = socket;
			players[i].state = LOBBY;
			players[i].nickname[0] = '\0';
			players[i].room_id = -1;
			players[i].buffer_len = 0;
			players[i].read_buffer[0] = '\0';
			player_count++;
			pthread_mutex_unlock(&lobby_mutex);
			return &players[i];
		}
	}
	pthread_mutex_unlock(&lobby_mutex);
	return NULL;
}

void remove_player(player_t* player)
{
	pthread_mutex_lock(&lobby_mutex);
	if (player && player->socket != -1)
	{
		// If player was in a room, remove them from there first.
		if (player->room_id != -1)
		{
			room_t* room = &rooms[player->room_id];
			remove_player_from_room(room, player);
		}

		player->socket = -1;
		player->state = LOBBY; // Reset state
		player->room_id = -1;
		player_count--;
	}
	pthread_mutex_unlock(&lobby_mutex);
}

int join_room(int room_id, player_t* player)
{
	pthread_mutex_lock(&lobby_mutex);

	if (
		room_id < 0 ||
		room_id >= MAX_ROOMS ||
		rooms[room_id].state == IN_PROGRESS ||
		rooms[room_id].player_count >= MAX_PLAYERS_PER_ROOM
	)
	{
		pthread_mutex_unlock(&lobby_mutex);
		return -1;
	}

	rooms[room_id].players[rooms[room_id].player_count++] = player;
	player->state = IN_GAME;
	player->room_id = room_id;

	if (rooms[room_id].player_count == MAX_PLAYERS_PER_ROOM)
	{
		rooms[room_id].state = IN_PROGRESS;
	}

	pthread_mutex_unlock(&lobby_mutex);
	return 0;
}

room_t* get_room(const int room_id)
{
	if (room_id < 0 || room_id >= MAX_ROOMS)
	{
		return NULL;
	}
	return &rooms[room_id];
}

player_t* find_disconnected_player(const char* nickname)
{
	pthread_mutex_lock(&lobby_mutex);
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (players[i].socket == -1 && players[i].state == IN_GAME && strcmp(players[i].nickname, nickname) == 0)
		{
			pthread_mutex_unlock(&lobby_mutex);
			return &players[i];
		}
	}
	pthread_mutex_unlock(&lobby_mutex);
	return NULL;
}

static void remove_player_from_room(room_t* room, player_t* player)
{
	if (!room || !player) return;

	int player_idx = -1;
	for (int i = 0; i < room->player_count; ++i)
	{
		if (room->players[i] == player)
		{
			player_idx = i;
			break;
		}
	}

	if (player_idx != -1)
	{
		// Shift remaining players to fill the gap
		for (int i = player_idx; i < room->player_count - 1; ++i)
		{
			room->players[i] = room->players[i + 1];
		}
		room->players[room->player_count - 1] = NULL; // Clear the last pointer
		room->player_count--;
	}
}

void leave_room(player_t* player)
{
	pthread_mutex_lock(&lobby_mutex);
	if (player->state == IN_GAME && player->room_id != -1)
	{
		room_t* room = &rooms[player->room_id];
		// A player can only leave a room if it's waiting for players
		if (room->state == WAITING)
		{
			remove_player_from_room(room, player);
			player->state = LOBBY;
			player->room_id = -1;
			if (room->player_count == 0)
			{
				room->state = WAITING;
			}
		}
	}
	pthread_mutex_unlock(&lobby_mutex);
}
