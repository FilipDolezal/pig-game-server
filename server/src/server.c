#include "server.h"
#include "protocol.h"
#include "game.h"
#include "lobby.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>

void* game_thread_func(void* arg)
{
	room_t* room = (room_t*)arg;
	game_state game;

	init_game(&game, room->players[0]->socket, room->players[1]->socket);

	send_structured_message(room->players[0]->socket, S_GAME_START, 2, K_OPPONENT_NICK, room->players[1]->nickname,
	                        K_YOUR_TURN, "1");
	send_structured_message(room->players[1]->socket, S_GAME_START, 2, K_OPPONENT_NICK, room->players[0]->nickname,
	                        K_YOUR_TURN, "0");

	switch_player(&game);
	send_game_state(room, &game);

	while (!game.game_over)
	{
		if (room->state == PAUSED)
		{
			int disconnected_player_idx = (room->players[0]->socket == -1) ? 0 : 1;
			if (time(NULL) - room->players[disconnected_player_idx]->disconnected_timestamp > RECONNECT_TIMEOUT)
			{
				game.game_over = 1;
				int winner_idx = 1 - disconnected_player_idx;
				if (room->players[winner_idx]->socket != -1)
				{
					send_structured_message(room->players[winner_idx]->socket, S_GAME_WIN, 1, K_MSG,
					                        "Opponent timed out.");
				}
				break;
			}
			sleep(1);
			continue;
		}

		int current_fd = game.player_fds[game.current_player];
		char command_buffer[MSG_MAX_LEN];

		if (receive_command(current_fd, command_buffer) > 0)
		{
			char* command = strtok(command_buffer, "|");
			if (command)
			{
				if (strcmp(command, C_ROLL) == 0)
				{
					handle_roll(&game);
					send_ack(current_fd, C_ROLL, NULL);
				}
				else if (strcmp(command, C_HOLD) == 0)
				{
					handle_hold(&game);
					send_ack(current_fd, C_HOLD, NULL);
				}
				else if (strcmp(command, C_QUIT) == 0)
				{
					game.game_over = 1;
					send_ack(current_fd, C_QUIT, NULL);
					int other_player_idx = 1 - game.current_player;
					if (room->players[other_player_idx]->socket != -1)
					{
						send_structured_message(room->players[other_player_idx]->socket, S_GAME_WIN, 1, K_MSG,
						                        "Opponent quit.");
					}
				}
				send_game_state(room, &game);
			}
		}
		else
		{
			int disconnected_player_idx = game.current_player;
			int other_player_idx = 1 - disconnected_player_idx;
			room->state = PAUSED;
			room->players[disconnected_player_idx]->socket = -1;
			room->players[disconnected_player_idx]->disconnected_timestamp = time(NULL);
			if (room->players[other_player_idx]->socket != -1)
			{
				send_structured_message(room->players[other_player_idx]->socket, S_OPPONENT_DISCONNECTED, 1, K_MSG,
				                        "Opponent disconnected.");
			}
		}
	}

	for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i)
	{
		if (room->players[i] && room->players[i]->socket != -1)
		{
			close(room->players[i]->socket);
		}
		remove_player(room->players[i]);
	}
	room->state = WAITING;
	room->player_count = 0;
	pthread_exit(NULL);
}

void send_game_state(room_t* room, game_state* game)
{
	char p1_score_str[10], p2_score_str[10], turn_score_str[10];
	sprintf(p1_score_str, "%d", game->scores[0]);
	sprintf(p2_score_str, "%d", game->scores[1]);
	sprintf(turn_score_str, "%d", game->turn_score);

	for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i)
	{
		if (room->players[i] && room->players[i]->socket != -1)
		{
			send_structured_message(room->players[i]->socket, S_GAME_STATE, 4,
			                        K_P1_SCORE, p1_score_str,
			                        K_P2_SCORE, p2_score_str,
			                        K_TURN_SCORE, turn_score_str,
			                        K_CURRENT_PLAYER, room->players[game->current_player]->nickname
			);
		}
	}
}

void* client_handler_thread(void* arg)
{
	const int client_socket = *(int*)arg;
	free(arg);
	player_t* player = NULL;

	send_structured_message(client_socket, S_WELCOME, 1,
		K_MSG, "Welcome! Please login with LOGIN|nickname:<your_nick>"
	);

	char buffer[MSG_MAX_LEN];
	char nickname[NICKNAME_LEN] = {0};

	// TODO: musíš to celé wrapnout do nekonečného cyklu

	if (receive_command(client_socket, buffer) <= 0)
	{
		close(client_socket);
		pthread_exit(NULL);
	}

	char* command = strtok(buffer, "|");
	if (command && strcmp(command, C_LOGIN) == 0)
	{
		char* payload = strtok(NULL, "");
		if (payload)
		{
			char* key = strtok(payload, ":");
			char* value = strtok(NULL, ":");
			if (key && value && strcmp(key, K_NICKNAME) == 0)
			{
				strncpy(nickname, value, NICKNAME_LEN - 1);
			}
		}
	}

	if (nickname[0] == '\0')
	{
		send_nack(client_socket, C_LOGIN, "Invalid login format.");
		close(client_socket);
		pthread_exit(NULL);
	}

	player = find_disconnected_player(nickname);
	if (player)
	{
		send_structured_message(client_socket, S_GAME_PAUSED, 1,
			K_MSG, "You have a game in progress. Send RESUME to rejoin or QUIT to abandon."
		);

		if (receive_command(client_socket, buffer) > 0)
		{
			command = strtok(buffer, "|");
			if (command && strcmp(command, C_RESUME) == 0)
			{
				player->socket = client_socket;
				room_t* room = get_room(player->room_id);
				room->state = IN_PROGRESS;
				send_ack(client_socket, C_RESUME, "Reconnected!");
				int other_idx = (room->players[0] == player) ? 1 : 0;

				if (room->players[other_idx]->socket != -1)
				{
					send_structured_message(room->players[other_idx]->socket, S_WELCOME, 1,
						K_MSG, "Opponent reconnected."
					);
				}
			}
			else
			{
				// End the paused game
				room_t* room = get_room(player->room_id);
				int other_idx = (room->players[0] == player) ? 1 : 0;
				if (room->players[other_idx]->socket != -1)
				{
					send_structured_message(room->players[other_idx]->socket, S_GAME_WIN, 1, K_MSG,
					                        "Opponent abandoned the game.");
					close(room->players[other_idx]->socket);
				}
				remove_player(room->players[0]);
				remove_player(room->players[1]);
				room->state = WAITING;
				room->player_count = 0;
				close(client_socket);
			}
		}
		pthread_exit(NULL);
	}

	player = add_player(client_socket);
	if (!player)
	{
		send_nack(client_socket, C_LOGIN, "Server is full.");
		close(client_socket);
		pthread_exit(NULL);
	}
	strcpy(player->nickname, nickname);
	send_ack(client_socket, C_LOGIN, NULL);

	while (player->state == LOBBY)
	{
		if (receive_command(client_socket, buffer) <= 0)
		{
			remove_player(player);
			close(client_socket);
			pthread_exit(NULL);
		}
		command = strtok(buffer, "|");
		if (!command) continue;

		if (strcmp(command, C_LIST_ROOMS) == 0)
		{
			send_structured_message(client_socket, S_ACK, 2,
				K_COMMAND, C_LIST_ROOMS,
				K_NUMBER, MAX_ROOMS
			);

			for (int i = 0; i < MAX_ROOMS; ++i)
			{
				const room_t* r = get_room(i);
				char id_str[4], p_count_str[4], max_p_str[4], state_str[15];
				sprintf(id_str, "%d", r->id);
				sprintf(p_count_str, "%d", r->player_count);
				sprintf(max_p_str, "%d", MAX_PLAYERS_PER_ROOM);

				switch (r->state)
				{
					case WAITING: strcpy(state_str, "WAITING");
						break;
					case FULL: strcpy(state_str, "FULL");
						break;
					case IN_PROGRESS: strcpy(state_str, "IN_PROGRESS");
						break;
					case PAUSED: strcpy(state_str, "PAUSED");
						break;
				}

				send_structured_message(client_socket, S_ROOM_INFO, 4,
					K_ROOM_ID, id_str,
					K_PLAYER_COUNT, p_count_str,
				    K_MAX_PLAYERS, max_p_str,
				    K_STATE, state_str
				);
			}
		}
		else if (strcmp(command, C_JOIN_ROOM) == 0)
		{
			char* p = strtok(NULL, "");
			char* k = strtok(p, ":");
			char* v = strtok(NULL, ":");
			if (k && v && strcmp(k, K_ROOM_ID) == 0)
			{
				int room_id = atoi(v);
				if (join_room(room_id, player) == 0)
				{
					send_ack(client_socket, C_JOIN_ROOM, NULL);
					room_t* room = get_room(room_id);
					if (room->player_count == MAX_PLAYERS_PER_ROOM)
					{
						pthread_create(&room->game_thread, NULL, game_thread_func, (void*)room);
						pthread_detach(room->game_thread);
					}
				}
				else
				{
					send_nack(client_socket, C_JOIN_ROOM, "Cannot join room.");
				}
			}
		}
		else if (strcmp(command, C_LEAVE_ROOM) == 0)
		{
			leave_room(player);
			send_ack(client_socket, C_LEAVE_ROOM, NULL);
		}
	}
	pthread_exit(NULL);
}

int run_server(const int port)
{
	// Structure to hold server address information
	struct sockaddr_in server_addr;

	init_lobby();

	// Create a socket for the server
	const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		perror("socket()");
		return -1;
	}

	// Set socket options to allow reusing the address, preventing "Address already in use" errors
	const int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		perror("setsockopt()");
		close(server_fd);
		return -1;
	}

	// Initialize server address structure
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET; // Use IPv4
	server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all available network interfaces
	server_addr.sin_port = htons(port); // Convert port number to network byte order

	// Bind the socket to the specified IP address and port
	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
	{
		perror("bind()");
		close(server_fd);
		return -1;
	}

	// Listen for incoming connections, with a maximum backlog of MAX_PLAYERS
	if (listen(server_fd, MAX_PLAYERS) < 0)
	{
		perror("listen()");
		close(server_fd);
		return -1;
	}

	printf("Server listening on port %d...\n", port);

	while (1)
	{
		// Accept a new client connection
		int* client_socket = malloc(sizeof(int));
		*client_socket = accept(server_fd, NULL, NULL);
		if (*client_socket < 0)
		{
			perror("accept()");
			free(client_socket);
			continue;
		}

		// Create a new thread to handle the client connection
		pthread_t tid;

		if (pthread_create(&tid, NULL, client_handler_thread, (void*)client_socket) != 0)
		{
			perror("pthread_create");
			close(*client_socket);
			free(client_socket);
		}

		// Detach the thread so its resources are automatically reclaimed upon termination
		pthread_detach(tid);
	}
}
