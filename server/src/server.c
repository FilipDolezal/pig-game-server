#include "server.h"
#include "protocol.h"
#include "game.h"
#include "lobby.h"
#include "config.h"
#include "parser.h"
#include "logger.h"

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

	LOG(LOG_GAME, "Game thread started for room %d", room->id);

	// Seed the random number generator for this game thread
	game.rand_seed = time(NULL) ^ (intptr_t)room;

	init_game(&game, room->players[0]->socket, room->players[1]->socket);
	broadcast_game_start(room, game.current_player);

	while (!game.game_over)
	{
		// Lock the room's mutex to safely check and modify its state
		pthread_mutex_lock(&room->mutex);
		while (room->state == PAUSED)
		{
			LOG(LOG_GAME, "Game in room %d is paused, waiting for reconnect.", room->id);
			// Set a timeout for a player to reconnect
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += RECONNECT_TIMEOUT;

			// Wait for a signal that a player has reconnected, or for the timeout to expire
			const int result = pthread_cond_timedwait(&room->cond, &room->mutex, &ts);
			// If the wait timed out, the game is over
			if (result == ETIMEDOUT)
			{
				LOG(LOG_GAME, "Player in room %d timed out. Game over.", room->id);
				game.game_over = 1;
				// Determine the winner (the player who is still connected)
				const int winner_idx = room->players[0]->socket == -1 ? 1 : 0;
				// If the winner is still connected, notify them that their opponent timed out
				if (room->players[winner_idx]->socket != -1)
				{
					send_structured_message(
						room->players[winner_idx]->socket, S_GAME_WIN, 1,
						K_MSG, "Your opponent timed out."
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
			LOG(LOG_GAME, "Game in room %d was aborted.", room->id);
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
			LOG(LOG_GAME, "Select error: %s", strerror(errno));
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
					const int sending_player_idx = i;
					const int other_player_idx = 1 - i;
					player_t* sending_player = room->players[sending_player_idx];
					const player_t* other_player = room->players[other_player_idx];

					char command_buffer[MSG_MAX_LEN];
					if (receive_command(sending_player, command_buffer) > 0)
					{
						LOG(LOG_GAME, "Received from player %s: %s", sending_player->nickname, command_buffer);
						parsed_command_t cmd;
						if (parse_command(command_buffer, &cmd) != 0)
						{
							LOG(LOG_GAME, "Malformed command from player %s. Ignoring.", sending_player->nickname);
							// Malformed command from a client. In-game, we'll ignore it
							// rather than disconnecting the player, which would end the game
							// for the opponent.
							continue;
						}

						// Handle QUIT from any player at any time
						if (cmd.type == CMD_QUIT)
						{
							LOG(LOG_GAME, "Player %s quit game in room %d.", sending_player->nickname, room->id);
							send_structured_message(sending_player->socket, S_OK, 0);
							game.game_over = 1;
							game.game_winner = other_player_idx;
						}
						// Other commands are only valid if it's the sender's turn
						else if (i == game.current_player)
						{
							if (cmd.type == CMD_ROLL)
							{
								handle_roll(&game);
							}
							else if (cmd.type == CMD_HOLD)
							{
								handle_hold(&game);
							}
							else
							{
								LOG(
									LOG_GAME, "Player %s sent invalid command: %s",
									sending_player->nickname, command_buffer
								);
								send_error(sending_player->socket, E_INVALID_COMMAND);
								continue;
							}
						}
						else
						{
							// It's not this player's turn.
							LOG(
								LOG_GAME, "Player %s sent command when it wasn't their turn.",
								sending_player->nickname
							);
							send_error(sending_player->socket, E_INVALID_COMMAND);
							continue;
						}

						if (!game.game_over)
						{
							// Game continues, just broadcast state
							broadcast_game_state(room, &game);
						}
						else
						{
							// game is over, broadcast score + break loop
							broadcast_game_over(room, &game);

							break;
						}
					}
					else
					{
						LOG(
							LOG_GAME, "Player %s disconnected from game in room %d.", sending_player->nickname, room->id
						);
						// treating this as disconnect
						if (other_player->socket != -1)
						{
							// Notify the other player about the disconnection
							send_structured_message(other_player->socket, S_OPPONENT_DISCONNECTED, 0);
						}

						// Use the new thread-safe function to handle the disconnect
						handle_player_disconnect(sending_player);
						game.player_fds[i] = -1;

						// Lock the room mutex to update its state
						pthread_mutex_lock(&room->mutex);
						room->state = PAUSED;
						pthread_mutex_unlock(&room->mutex);
					}
				}
			}
		}
	}

	LOG(LOG_GAME, "Game in room %d finished. Returning players to lobby.", room->id);
	pthread_mutex_lock(&room->mutex);
	// After the game loop ends, send players back to the lobby

	for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i)
	{
		if (room->players[i])
		{
			// Reset state for all players who were in the game, connected or not.
			room->players[i]->state = LOBBY;
			room->players[i]->room_id = -1;
		}
	}
	// Reset the room to a WAITING state for new players

	room->state = WAITING;
	room->player_count = 0;
	room->players[0] = NULL;
	room->players[1] = NULL;

	// Wake up the client_handler_threads that are waiting for the game to end.
	pthread_cond_broadcast(&room->cond);
	pthread_mutex_unlock(&room->mutex);
	// Exit the thread
	pthread_exit(NULL);
}

void broadcast_game_over(const room_t* room, const game_state* game)
{
	if (game->game_winner > -1)
	{
		const player_t* winner = room->players[game->game_winner];
		const player_t* looser = room->players[1 - game->game_winner];

		send_structured_message(winner->socket, S_GAME_WIN, 0);
		if (looser->socket != -1)
		{
			send_structured_message(looser->socket, S_GAME_LOSE, 0);
		}
	}
	// todo else broadcast game over without winner/loser
}

void broadcast_game_start(const room_t* room, const int first_to_act)
{
	const player_t* curr = room->players[first_to_act];
	const player_t* next = room->players[1 - first_to_act];

	LOG(
		LOG_GAME, "Starting game in room %d between %s and %s. %s goes first.",
		room->id, curr->nickname, next->nickname, curr->nickname
	);

	send_structured_message(
		curr->socket, S_GAME_START, 2,
		K_OPP_NICK, next->nickname,
		K_YOUR_TURN, "1"
	);

	send_structured_message(
		next->socket, S_GAME_START, 2,
		K_OPP_NICK, curr->nickname,
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
		K_CURRENT, curr->nickname
	);

	send_structured_message(
		next->socket, S_GAME_STATE, 4,
		K_MY_SCORE, next_score,
		K_OPP_SCORE, curr_score,
		K_TURN_SCORE, turn_score,
		K_ROLL, roll_result,
		K_CURRENT, curr->nickname
	);
}

// Forward declarations for helper functions
static player_t* handle_login_and_reconnect(player_t* player);
static void handle_main_loop(player_t* player);

void* client_handler_thread(void* arg)
{
	player_t* player = (player_t*)arg;
	const int client_socket = player->socket;
	LOG(LOG_SERVER, "New client handler thread started for socket %d.", client_socket);

	player = handle_login_and_reconnect(player);

	if (player)
	{
		handle_main_loop(player);
	}

	LOG(LOG_SERVER, "Client handler thread for socket %d is exiting.", client_socket);
	// Thread exit is handled within the helpers on error, or here on normal completion.
	pthread_exit(NULL);
}

static player_t* handle_login_and_reconnect(player_t* player)
{
	char max_players_str[4];
	char max_rooms_str[4];
	sprintf(max_players_str, "%d", MAX_PLAYERS);
	sprintf(max_rooms_str, "%d", MAX_ROOMS);

	const int client_socket = player->socket;
	send_structured_message(client_socket, S_WELCOME, 2, K_PLAYERS, max_players_str, K_ROOMS, max_rooms_str);

	char buffer[MSG_MAX_LEN];
	char nickname[NICKNAME_LEN] = {0};

	// --- LOGIN ---
	if (receive_command(player, buffer) <= 0)
	{
		LOG(LOG_LOBBY, "Client on socket %d disconnected before login.", client_socket);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	parsed_command_t cmd;
	if (parse_command(buffer, &cmd) != 0 || cmd.type != CMD_LOGIN)
	{
		LOG(LOG_LOBBY, "Invalid login command from socket %d.", client_socket);
		send_error(client_socket, E_INVALID_COMMAND);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	const char* nick_val = get_command_arg(&cmd, K_NICK);
	if (nick_val)
	{
		strncpy(nickname, nick_val, NICKNAME_LEN - 1);
	}

	if (nickname[0] == '\0')
	{
		LOG(LOG_LOBBY, "Empty nickname from socket %d.", client_socket);
		send_error(client_socket, E_INVALID_NICKNAME);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	// Check if a player with this nickname is already active
	if (find_active_player_by_nickname(nickname))
	{
		LOG(LOG_LOBBY, "Player tried to connect with active nickname: %s", nickname);
		send_error(client_socket, E_NICKNAME_IN_USE);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	// --- RECONNECT or NEW PLAYER ---
	player_t* reconnecting_player = find_disconnected_player(nickname);
	if (reconnecting_player)
	{
		LOG(LOG_LOBBY, "Player %s is reconnecting.", nickname);
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
				LOG(LOG_LOBBY, "Player %s resumed game in room %d.", player->nickname, room->id);
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
				LOG(LOG_LOBBY, "Player %s failed to send RESUME. Aborting game.", player->nickname);
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
			LOG(LOG_LOBBY, "Player %s disconnected before resuming.", player->nickname);
			// Disconnected before sending RESUME.
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
		LOG(LOG_LOBBY, "New player %s logged in.", nickname);
		// Just update the nickname in the player object we were given.
		strcpy(player->nickname, nickname);
		send_structured_message(client_socket, S_OK, 0);
	}
	return player;
}

static void handle_main_loop(player_t* player)
{
	const int client_socket = player->socket;

	while (player->socket != -1)
	{
		if (player->state == LOBBY)
		{
			char buffer[MSG_MAX_LEN];
			if (receive_command(player, buffer) <= 0)
			{
				LOG(LOG_LOBBY, "Player %s disconnected from lobby.", player->nickname);
				remove_player(player);
				close(client_socket);
				return;
			}

			LOG(LOG_LOBBY, "Received from player %s in lobby: %s", player->nickname, buffer);
			parsed_command_t lobby_cmd;
			if (parse_command(buffer, &lobby_cmd) != 0)
			{
				LOG(LOG_LOBBY, "Malformed command from %s in lobby. Disconnecting.", player->nickname);
				send_error(client_socket, E_INVALID_COMMAND);
				remove_player(player);
				close(client_socket);
				return;
			}

			switch (lobby_cmd.type)
			{
				case CMD_LIST_ROOMS:
					{
						for (int i = 0; i < MAX_ROOMS; ++i)
						{
							const room_t* r = get_room(i);
							char id_str[4], p_count_str[4], state_str[15];
							sprintf(id_str, "%d", r->id);
							sprintf(p_count_str, "%d", r->player_count);

							switch (r->state)
							{
								case WAITING: strcpy(state_str, "WAITING");
									break;
								case IN_PROGRESS: strcpy(state_str, "IN_PROGRESS");
									break;
								case PAUSED: strcpy(state_str, "PAUSED");
									break;
								case ABORTED: strcpy(state_str, "ABORTED");
									break;
							}

							send_structured_message(
								client_socket, S_ROOM_INFO, 3,
								K_ROOM, id_str,
								K_COUNT, p_count_str,
								K_STATE, state_str
							);
						}
						break;
					}
				case CMD_JOIN_ROOM:
					{
						const char* room_id_str = get_command_arg(&lobby_cmd, K_ROOM);
						if (!room_id_str)
						{
							LOG(
								LOG_LOBBY, "JOIN_ROOM command from %s missing room ID. Disconnecting.", player->nickname
							);
							send_error(client_socket, E_INVALID_COMMAND);
							remove_player(player);
							close(client_socket);
							return;
						}
						const int room_id = atoi(room_id_str);
						LOG(LOG_LOBBY, "Player %s trying to join room %d.", player->nickname, room_id);
						if (join_room(room_id, player) == 0)
						{
							send_structured_message(client_socket, S_JOIN_OK, 0);
							room_t* room = get_room(room_id);
							if (room->player_count == MAX_PLAYERS_PER_ROOM)
							{
								pthread_create(&room->game_thread, NULL, game_thread_func, (void*)room);
								// Wake up the other waiting player in the room.
								pthread_mutex_lock(&room->mutex);
								pthread_cond_broadcast(&room->cond);
								pthread_mutex_unlock(&room->mutex);
							}
						}
						else
						{
							LOG(LOG_LOBBY, "Player %s failed to join room %d.", player->nickname, room_id);
							send_error(client_socket, E_CANNOT_JOIN);
						}
						break;
					}
				case CMD_LEAVE_ROOM:
					{
						LOG(LOG_LOBBY, "Player %s leaving room.", player->nickname);
						if (leave_room(player) == 0)
						{
							send_structured_message(client_socket, S_OK, 0);
						}
						else
						{
							send_error(client_socket, E_GAME_IN_PROGRESS);
						}
						break;
					}
				case CMD_EXIT:
					{
						LOG(LOG_LOBBY, "Player %s exiting from lobby.", player->nickname);
						remove_player(player);
						close(client_socket);
						return;
					}
				default:
					{
						LOG(LOG_LOBBY, "Invalid command from %s in lobby. Disconnecting.", player->nickname);
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
			if (room)
			{
				pthread_mutex_lock(&room->mutex);
				// This thread's player is in a game. It should wait until the game is over.
				// The game is over when the player's state is no longer IN_GAME.
				// We wait on the room's condition variable, which the game thread will
				// signal when the game ends.
				while (player->state == IN_GAME)
				{
					pthread_cond_wait(&room->cond, &room->mutex);
				}
				pthread_mutex_unlock(&room->mutex);
			}
		}
	}
}

int run_server(const int port, const char* address)
{
	// Structure to hold server address information
	struct sockaddr_in server_addr;

	init_lobby();

	// Create a socket for the server
	const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		LOG(LOG_SERVER, "socket() failed: %s", strerror(errno));
		return -1;
	}

	// Set socket options to allow reusing the address, preventing "Address already in use" errors
	const int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
	{
		LOG(LOG_SERVER, "setsockopt() failed: %s", strerror(errno));
		close(server_fd);
		return -1;
	}

	// Initialize server address structure
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET; // Use IPv4
	server_addr.sin_addr.s_addr = inet_addr(address); // Listen on the specified network interface
	server_addr.sin_port = htons(port); // Convert port number to network byte order

	// Bind the socket to the specified IP address and port
	if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
	{
		LOG(LOG_SERVER, "bind() failed: %s", strerror(errno));
		close(server_fd);
		return -1;
	}

	// Listen for incoming connections, with a maximum backlog of MAX_PLAYERS
	if (listen(server_fd, MAX_PLAYERS) < 0)
	{
		LOG(LOG_SERVER, "listen() failed: %s", strerror(errno));
		close(server_fd);
		return -1;
	}

	LOG(LOG_SERVER, "Server listening on port %d...", port);

	while (1)
	{
		// Accept a new client connection
		const int client_socket = accept(server_fd, NULL, NULL);
		if (client_socket < 0)
		{
			LOG(LOG_SERVER, "accept() failed: %s", strerror(errno));
			continue;
		}

		LOG(LOG_SERVER, "Accepted new connection on socket %d.", client_socket);

		player_t* player = add_player(client_socket);
		if (!player)
		{
			LOG(LOG_SERVER, "Server is full. Rejecting connection from socket %d.", client_socket);
			send_error(client_socket, E_SERVER_FULL);
			close(client_socket);
			continue;
		}

		// Create a new thread to handle the client connection
		pthread_t tid;
		if (pthread_create(&tid, NULL, client_handler_thread, (void*)player) != 0)
		{
			LOG(LOG_SERVER, "pthread_create() failed: %s", strerror(errno));
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
