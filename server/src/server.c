#include "server.h"
#include "protocol.h"
#include "game.h"
#include "lobby.h"
#include "config.h"
#include "parser.h"

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
					send_structured_message(
						room->players[winner_idx]->socket, S_GAME_WIN, 1, K_MSG, "Your opponent timed out."
					);
				}
				break; // Exit the PAUSED loop
			}
		}
		// Unlock the room's mutex
		pthread_mutex_unlock(&room->mutex);

		// After a potential pause, player sockets might have changed (reconnect).
		// Update the game's file descriptors from the room's player data.
		for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i)
		{
			if (room->players[i])
			{
				game.player_fds[i] = room->players[i]->socket;
			}
		}

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

		fd_set read_fds;
		int max_fd = -1;

		FD_ZERO(&read_fds);

		for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i)
		{
			if (game.player_fds[i] != -1)
			{
				FD_SET(game.player_fds[i], &read_fds);
				if (game.player_fds[i] > max_fd)
				{
					max_fd = game.player_fds[i];
				}
			}
		}

		if (max_fd == -1)
		{
			// Both players disconnected, game will be paused and eventually timeout.
			continue;
		}

		// Set a short timeout for select() to make the loop non-blocking
		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		// Wait for activity on any of the player sockets
		const int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

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

					if (receive_command(sending_player, command_buffer) > 0)
					{
						parsed_command_t cmd;
						if (parse_command(command_buffer, &cmd) != 0)
						{
							// Malformed command from a client. In-game, we'll ignore it
							// rather than disconnecting the player, which would end the game
							// for the opponent.
							continue;
						}

						// Handle QUIT from any player at any time
						if (cmd.type == CMD_QUIT)
						{
							game.game_over = 1;
							send_structured_message(sending_player->socket, S_GAME_LOSE, 0);
							if (other_player->socket != -1)
							{
								send_structured_message(other_player->socket, S_GAME_WIN, 0);
							}
							// Break the inner for-loop to proceed to game cleanup
							break;
						}

						// Other commands are only valid if it's the sender's turn
						if (i == game.current_player)
						{
							switch (cmd.type)
							{
								case CMD_ROLL:
									handle_roll(&game);
									broadcast_game_state(room, &game);
									break;
								case CMD_HOLD:
									handle_hold(&game);
									broadcast_game_state(room, &game);
									break;
								default:
									// Ignore unknown or out-of-turn commands.
									break;
							}
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
						game.player_fds[i] = -1;
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

	send_structured_message(
		curr->socket, S_GAME_START, 2,
		K_OPPONENT_NICK, next->nickname,
		K_YOUR_TURN, "1"
	);

	send_structured_message(
		next->socket, S_GAME_START, 2,
		K_OPPONENT_NICK, curr->nickname,
		K_YOUR_TURN, "0"
	);
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

	send_structured_message(
		curr->socket, S_GAME_STATE, 4,
		K_MY_SCORE, curr_score,
		K_OPP_SCORE, next_score,
		K_TURN_SCORE, turn_score,
		K_ROLL, roll_result,
		K_CURRENT_PLAYER, curr->nickname
	);

	send_structured_message(
		next->socket, S_GAME_STATE, 4,
		K_MY_SCORE, next_score,
		K_OPP_SCORE, curr_score,
		K_TURN_SCORE, turn_score,
		K_ROLL, roll_result,
		K_CURRENT_PLAYER, curr->nickname
	);
}

// Forward declarations for helper functions
static player_t* handle_login_and_reconnect(player_t* player);
static void handle_main_loop(player_t* player);

void* client_handler_thread(void* arg)
{
	player_t* player = (player_t*)arg;

	player = handle_login_and_reconnect(player);

	if (player)
	{
		handle_main_loop(player);
	}

	// Thread exit is handled within the helpers on error, or here on normal completion.
	pthread_exit(NULL);
}

static player_t* handle_login_and_reconnect(player_t* player)
{
	const int client_socket = player->socket;
	send_structured_message(client_socket, S_WELCOME, 0);

	char buffer[MSG_MAX_LEN];
	char nickname[NICKNAME_LEN] = {0};

	// --- LOGIN ---
	if (receive_command(player, buffer) <= 0)
	{
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	parsed_command_t cmd;
	if (parse_command(buffer, &cmd) != 0 || cmd.type != CMD_LOGIN)
	{
		send_error(client_socket, E_INVALID_COMMAND);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	const char* nick_val = get_command_arg(&cmd, K_NICKNAME);
	if (nick_val)
	{
		strncpy(nickname, nick_val, NICKNAME_LEN - 1);
	}

	if (nickname[0] == '\0')
	{
		send_error(client_socket, E_INVALID_NICKNAME);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	// --- RECONNECT or NEW PLAYER ---
	player_t* reconnecting_player = find_disconnected_player(nickname);
	if (reconnecting_player)
	{
		// This is a reconnecting player. We need to transfer control to the old player slot.
		reconnecting_player->socket = client_socket; // Give the new socket to the old player object.

		// Copy any data read after the LOGIN command from the temp buffer to the real buffer.
		memcpy(reconnecting_player->read_buffer, player->read_buffer, player->buffer_len);
		reconnecting_player->buffer_len = player->buffer_len;

		// The temporary player object created by `add_player` is no longer needed.
		remove_player(player);

		// This thread will now manage the reconnecting player.
		player = reconnecting_player;

		send_structured_message(client_socket, S_GAME_PAUSED, 0);

		room_t* room = get_room(player->room_id);
		const int other_idx = (room->players[0] == player) ? 1 : 0;

		if (receive_command(player, buffer) > 0)
		{
			parsed_command_t resume_cmd;
			if (parse_command(buffer, &resume_cmd) == 0 && resume_cmd.type == CMD_RESUME)
			{
				pthread_mutex_lock(&room->mutex);
				room->state = IN_PROGRESS;
				pthread_cond_signal(&room->cond);
				pthread_mutex_unlock(&room->mutex);

				send_structured_message(client_socket, S_OK, 0);

				if (room->players[other_idx]->socket != -1)
				{
					send_structured_message(room->players[other_idx]->socket, S_OPPONENT_RECONNECTED, 0);
				}
			}
			else
			{
				// Failed to send RESUME, abort game.
				pthread_mutex_lock(&room->mutex);
				room->state = ABORTED;
				pthread_cond_signal(&room->cond);
				pthread_mutex_unlock(&room->mutex);
				remove_player(player);
				close(client_socket);
				return NULL;
			}
		}
		else
		{
			// Disconnected before sending RESUME.
			room_t* room = get_room(player->room_id);
			pthread_mutex_lock(&room->mutex);
			room->state = ABORTED;
			pthread_cond_signal(&room->cond);
			pthread_mutex_unlock(&room->mutex);
			remove_player(player);
			close(client_socket);
			return NULL;
		}
	}
	else // This is a new player.
	{
		// Just update the nickname in the player object we were given.
		strcpy(player->nickname, nickname);
		send_structured_message(client_socket, S_OK, 0);
	}
	return player;
}

static void handle_main_loop(player_t* player)
{
	const int client_socket = player->socket;
	char buffer[MSG_MAX_LEN];

	while (player && player->socket != -1)
	{
		if (player->state == LOBBY)
		{
			if (receive_command(player, buffer) <= 0)
			{
				remove_player(player);
				close(client_socket);
				return;
			}

			parsed_command_t lobby_cmd;
			if (parse_command(buffer, &lobby_cmd) != 0)
			{
				send_error(client_socket, E_INVALID_COMMAND);
				remove_player(player);
				close(client_socket);
				return;
			}

			switch (lobby_cmd.type)
			{
				case CMD_LIST_ROOMS:
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
							case WAITING: strcpy(state_str, "WAITING"); break;
							case FULL: strcpy(state_str, "FULL"); break;
							case IN_PROGRESS: strcpy(state_str, "IN_PROGRESS"); break;
							case PAUSED: strcpy(state_str, "PAUSED"); break;
							case ABORTED: strcpy(state_str, "ABORTED"); break;
						}

						send_structured_message(client_socket, S_ROOM_INFO, 4,
												K_ROOM_ID, id_str,
												K_PLAYER_COUNT, p_count_str,
												K_MAX_PLAYERS, max_p_str,
												K_STATE, state_str
						);
					}
					break;
				}
				case CMD_JOIN_ROOM:
				{
					const char* room_id_str = get_command_arg(&lobby_cmd, K_ROOM_ID);
					if (!room_id_str)
					{
						send_error(client_socket, E_INVALID_COMMAND);
						remove_player(player);
						close(client_socket);
						return;
					}
					const int room_id = atoi(room_id_str);
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
					break;
				}
				case CMD_LEAVE_ROOM:
				{
					leave_room(player);
					send_structured_message(client_socket, S_OK, 0);
					break;
				}
				default:
				{
					send_error(client_socket, E_INVALID_COMMAND);
					remove_player(player);
					close(client_socket);
					return;
				}
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
		const int client_socket = accept(server_fd, NULL, NULL);
		if (client_socket < 0)
		{
			perror("accept()");
			continue;
		}

		player_t* player = add_player(client_socket);
		if (!player)
		{
			send_error(client_socket, E_SERVER_FULL);
			close(client_socket);
			continue;
		}

		// Create a new thread to handle the client connection
		pthread_t tid;
		if (pthread_create(&tid, NULL, client_handler_thread, (void*)player) != 0)
		{
			perror("pthread_create");
			remove_player(player); // Rollback the add_player
			close(client_socket);
		}
		else
		{
			// Detach the thread so its resources are automatically released on exit
			pthread_detach(tid);
		}
	}
}
