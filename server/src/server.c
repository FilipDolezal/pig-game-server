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
#include <errno.h>

void* game_thread_func(void* arg)
{
	room_t* room = (room_t*)arg;
	game_state game;

	init_game(&game, room->players[0]->socket, room->players[1]->socket);
	broadcast_game_start(room, game.current_player);

	while (!game.game_over)
	{
		// Lock the room's mutex to safely check and modify its state
		pthread_mutex_lock(&room->mutex);
		while (room->state == PAUSED)
		{
			// Set a timeout for a player to reconnect
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += RECONNECT_TIMEOUT;

			// Wait for a signal that a player has reconnected, or for the timeout to expire
			const int result = pthread_cond_timedwait(&room->cond, &room->mutex, &ts);
			// If the wait timed out, the game is over
			if (result == ETIMEDOUT)
			{
				game.game_over = 1;
				// Determine the winner (the player who is still connected)
				const int winner_idx = room->players[0]->socket == -1 ? 1 : 0;
				// If the winner is still connected, notify them that their opponent timed out
				if (room->players[winner_idx]->socket != -1)
				{
					send_error(room->players[winner_idx]->socket, E_OPPONENT_TIMEOUT);
				}
				break; // Exit the PAUSED loop
			}
		}
		// Unlock the room's mutex
		pthread_mutex_unlock(&room->mutex);

		// If the room state was changed to ABORTED (e.g., by a player leaving), end the game
		if (room->state == ABORTED)
		{
			game.game_over = 1;
		}

		// If the game is over for any reason, break out of the main loop
		if (game.game_over)
		{
			break; // Exit the main game loop
		}

		// set of file descriptors to listen to them at the same time
		fd_set read_fds;

		// zero out the set
		FD_ZERO(&read_fds);

		// set up set with player sockets
		FD_SET(game.player_fds[0], &read_fds);
		FD_SET(game.player_fds[1], &read_fds);

		// Set a short timeout for select() to make the loop non-blocking
		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		// Wait for activity on any of the player sockets
		const int activity = select(
			// Find the highest file descriptor for select()
			(game.player_fds[0] > game.player_fds[1] ? game.player_fds[0] : game.player_fds[1]) + 1,
			&read_fds, NULL, NULL, &tv
		);

		// Handle select() errors
		if ((activity < 0) && (errno != EINTR))
		{
			printf("select error");
		}

		// If there was activity on a socket
		if (activity > 0)
		{
			// Iterate through the players to see who sent data
			for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
			{
				// Check if this player's socket has data to be read
				if (FD_ISSET(game.player_fds[i], &read_fds))
				{
					player_t* sending_player = room->players[i];
					const player_t* other_player = room->players[1 - i];
					char command_buffer[MSG_MAX_LEN];

					if (receive_command(game.player_fds[i], command_buffer) > 0)
					{
						// Parse the command
						const char* command = strtok(command_buffer, "|");

						if (strcmp(command, C_QUIT) == 0)
						{
							game.game_over = 1;
							send_structured_message(sending_player->socket, S_GAME_LOSE, 0);
							if (other_player->socket != -1)
							{
								send_structured_message(other_player->socket, S_GAME_WIN, 0);
							}
						}

						// Check if it's the current player's turn
						if(i == game.current_player)
						{
							// Handle the 'ROLL' command
							if (strcmp(command, C_ROLL) == 0)
							{
								handle_roll(&game);
							}
							// Handle the 'HOLD' command
							else if (strcmp(command, C_HOLD) == 0)
							{
								handle_hold(&game);
							}

							// After handling the command, broadcast the updated game state to both players
							broadcast_game_state(room, &game);
						}
					}
					else
					{
						// treating this as disconnect
						if (other_player->socket != -1)
						{
							// Notify the other player about the disconnection
							send_structured_message(other_player->socket, S_OPPONENT_DISCONNECTED, 0);
						}

						// Lock the room mutex to update its state
						pthread_mutex_lock(&room->mutex);

						// Set the room state to PAUSED
						room->state = PAUSED;
						// Mark the player's socket as invalid (-1)
						sending_player->socket = -1;
						// Record the time of disconnection
						sending_player->disconnected_timestamp = time(NULL);

						pthread_mutex_unlock(&room->mutex);
					}
				}
			}
		}
	}

	// After the game loop ends, send players back to the lobby
	for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i)
	{
		if (room->players[i])
		{
			if (room->players[i]->socket != -1)
			{
				room->players[i]->state = LOBBY;
				room->players[i]->room_id = -1;
			}
		}
	}
	// Reset the room to a WAITING state for new players
	room->state = WAITING;
	room->player_count = 0;
	room->players[0] = NULL;
	room->players[1] = NULL;
	// Exit the thread
	pthread_exit(NULL);
}

void broadcast_game_start(const room_t* room, const int first_to_act)
{
	const player_t* curr = room->players[first_to_act];
	const player_t* next = room->players[1 - first_to_act];
	send_structured_message(curr->socket, S_GAME_START, 2, K_OPPONENT_NICK, next->nickname, K_YOUR_TURN, "1");
	send_structured_message(next->socket, S_GAME_START, 2, K_OPPONENT_NICK, curr->nickname, K_YOUR_TURN, "0");
}

void broadcast_game_state(const room_t* room, const game_state* game)
{
	const player_t* curr = room->players[game->current_player];
	const player_t* next = room->players[1 - game->current_player];

	char curr_score[10], next_score[10], turn_score[10], roll_result[10];
	sprintf(curr_score, "%d", game->scores[game->current_player]);
	sprintf(next_score, "%d", game->scores[1 - game->current_player]);
	sprintf(turn_score, "%d", game->turn_score);
	sprintf(roll_result, "%d", game->roll_result);

	send_structured_message(curr->socket, S_GAME_STATE, 4,
	                        K_MY_SCORE, curr_score,
	                        K_OPP_SCORE, next_score,
	                        K_TURN_SCORE, turn_score,
	                        K_ROLL, roll_result,
	                        K_CURRENT_PLAYER, curr->nickname
	);

	send_structured_message(next->socket, S_GAME_STATE, 4,
	                        K_MY_SCORE, next_score,
	                        K_OPP_SCORE, curr_score,
	                        K_TURN_SCORE, turn_score,
	                        K_ROLL, roll_result,
	                        K_CURRENT_PLAYER, curr->nickname
	);
}

void* client_handler_thread(void* arg)
{
	const int client_socket = *(int*)arg;
	free(arg);
	player_t* player = NULL;

	send_structured_message(client_socket, S_WELCOME, 0);

	char buffer[MSG_MAX_LEN];
	char nickname[NICKNAME_LEN] = {0};

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
			const char* key = strtok(payload, ":");
			const char* value = strtok(NULL, ":");
			if (key && value && strcmp(key, K_NICKNAME) == 0)
			{
				strncpy(nickname, value, NICKNAME_LEN - 1);
			}
		}
	}

	if (nickname[0] == '\0')
	{
		send_error(client_socket, E_INVALID_NICKNAME);
		close(client_socket);
		pthread_exit(NULL);
	}

	// lookup nickname in the disconnected player pool
	player = find_disconnected_player(nickname);

	if (player)
	{
		send_structured_message(client_socket, S_GAME_PAUSED, 0);

		room_t* room = get_room(player->room_id);
		const int other_idx = (room->players[0] == player) ? 1 : 0;

		if (receive_command(client_socket, buffer) > 0)
		{
			command = strtok(buffer, "|");

			if (command && strcmp(command, C_RESUME) == 0)
			{
				pthread_mutex_lock(&room->mutex);
				player->socket = client_socket;
				room->state = IN_PROGRESS;
				pthread_cond_signal(&room->cond);
				pthread_mutex_unlock(&room->mutex);

				send_structured_message(client_socket, S_OK, 0);

				if (room->players[other_idx]->socket != -1)
				{
					send_structured_message(room->players[other_idx]->socket, S_OPPONENT_RECONNECTED, 0);
				}

				pthread_join(room->game_thread, NULL);
			}
			else
			{
				// End the paused game
				pthread_mutex_lock(&room->mutex);
				room->state = ABORTED;
				pthread_cond_signal(&room->cond);
				pthread_mutex_unlock(&room->mutex);

				pthread_join(room->game_thread, NULL);

				if (room->players[other_idx]->socket != -1)
				{
					send_error(room->players[other_idx]->socket, E_OPPONENT_QUIT);
					close(room->players[other_idx]->socket);
				}

				remove_player(room->players[0]);
				remove_player(room->players[1]);

				room->player_count = 0;
				close(client_socket);
			}
		}
		pthread_exit(NULL);
	}

	player = add_player(client_socket);
	if (!player)
	{
		send_error(client_socket, E_SERVER_FULL);
		close(client_socket);
		pthread_exit(NULL);
	}
	strcpy(player->nickname, nickname);
	send_structured_message(client_socket, S_OK, 0);

	while (player->socket != -1)
	{
		if (player->state == LOBBY)
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
				char num_rooms_str[4];
				sprintf(num_rooms_str, "%d", MAX_ROOMS);
				send_structured_message(client_socket, S_ROOM_LIST, 1, K_NUMBER, num_rooms_str);

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
						case ABORTED: strcpy(state_str, "ABORTED");
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
				const char* k = strtok(p, ":");
				const char* v = strtok(NULL, ":");
				if (k && v && strcmp(k, K_ROOM_ID) == 0)
				{
					const int room_id = atoi(v);
					if (join_room(room_id, player) == 0)
					{
						send_structured_message(client_socket, S_JOIN_OK, 0);
						room_t* room = get_room(room_id);
						if (room->player_count == MAX_PLAYERS_PER_ROOM)
						{
							pthread_create(&room->game_thread, NULL, game_thread_func, (void*)room);
						}
					}
					else
					{
						send_error(client_socket, E_CANNOT_JOIN);
					}
				}
			}
			else if (strcmp(command, C_LEAVE_ROOM) == 0)
			{
				leave_room(player);
				send_structured_message(client_socket, S_OK, 0);
			}
		}
		else if (player->state == IN_GAME)
		{
			room_t* room = get_room(player->room_id);
			if (room && (room->state == IN_PROGRESS || room->state == PAUSED || room->state == ABORTED))
			{
				pthread_join(room->game_thread, NULL);
			}
			else
			{
				// Waiting for another player to join and start the game
				sleep(1);
			}
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
	}
}
