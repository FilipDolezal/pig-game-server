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

// Forward declarations for helper functions
static void handle_game_input(room_t* room, game_state* game, int player_idx);
static void reset_room_after_game(room_t* room);
static player_t* handle_login_and_reconnect(player_t* player);
static void handle_lobby_command(player_t* player, const parsed_command_t* cmd);
static void handle_main_loop(player_t* player);


static void handle_game_input(room_t* room, game_state* game, const int sending_player_idx)
{
	const int other_player_idx = 1 - sending_player_idx;
	player_t* sending_player = room->players[sending_player_idx];
	const player_t* other_player = room->players[other_player_idx];

	char command_buffer[MSG_MAX_LEN];
	const ssize_t recv_result = receive_command(sending_player, command_buffer);

	if (recv_result == -3)
	{
		// Socket timeout - not a disconnect, just no data yet
		return;
	}

	if (recv_result > 0)
	{
		LOG(LOG_GAME, "Received from player %s: %s", sending_player->nickname, command_buffer);
		parsed_command_t cmd;
		if (parse_command(command_buffer, &cmd) != 0)
		{
			LOG(LOG_GAME, "Malformed command from player %s. Ignoring.", sending_player->nickname);
			// Malformed command from a client. In-game, we'll ignore it
			// rather than disconnecting the player, which would end the game
			// for the opponent.
			return;
		}

		// Late attempt at leaving the waiting room
		if (cmd.type == CMD_LEAVE_ROOM)
		{
			LOG(
				LOG_GAME, "Player %s attempted to leave the waiting room, while game was established",
				sending_player->nickname, room->id
			);
			send_error(sending_player->socket, C_LEAVE_ROOM, E_GAME_IN_PROGRESS);
		}
		// Handle QUIT from any player at any time
		else if (cmd.type == CMD_QUIT)
		{
			LOG(LOG_GAME, "Player %s quit game in room %d.", sending_player->nickname, room->id);
			send_structured_message(sending_player->socket, S_OK, 1, K_CMD, C_QUIT);
			game->game_over = 1;
			game->game_winner = other_player_idx;
		}
		// Handle GAME_STATE_REQUEST from any player at any time (non-turn-changing)
		else if (cmd.type == CMD_GAME_STATE_REQUEST)
		{
			send_game_state(sending_player, room, game);
			return;
		}
		// Handle PING from any player at any time (non-turn-changing)
		else if (cmd.type == CMD_PING)
		{
			send_structured_message(sending_player->socket, S_OK, 1, K_CMD, C_PING);
			return;
		}
		// Other commands are only valid if it's the sender's turn
		else if (sending_player_idx == game->current_player)
		{
			if (cmd.type == CMD_ROLL)
			{
				handle_roll(game);
			}
			else if (cmd.type == CMD_HOLD)
			{
				handle_hold(game);
			}
			else
			{
				LOG(
					LOG_GAME, "Player %s sent invalid command: %s",
					sending_player->nickname, command_buffer
				);
				send_error(sending_player->socket, NULL, E_INVALID_COMMAND);
				return;
			}
		}
		else
		{
			// It's not this player's turn.
			LOG(
				LOG_GAME, "Player %s sent command when it wasn't their turn.",
				sending_player->nickname
			);
			send_error(sending_player->socket, NULL, E_INVALID_COMMAND);
			return;
		}

		if (!game->game_over)
		{
			// Game continues, just broadcast state
			broadcast_game_state(room, game);
		}
		else
		{
			// game is over, broadcast final state, then broadcast winner/loser
			broadcast_game_state(room, game);
			broadcast_game_over(room, game);
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
		game->player_fds[sending_player_idx] = -1;

		// Lock the room mutex to update its state
		pthread_mutex_lock(&room->mutex);
		room->state = PAUSED;
		broadcast_room_update(room);
		pthread_mutex_unlock(&room->mutex);
	}
}

static void reset_room_after_game(room_t* room)
{
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
	room->state = WAITING;
	room->player_count = 0;
	room->players[0] = NULL;
	room->players[1] = NULL;
	broadcast_room_update(room);

	// Wake up the client_handler_threads that are waiting for the game to end.
	pthread_cond_broadcast(&room->cond);
	pthread_mutex_unlock(&room->mutex);
}

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
		if (room->state == PAUSED)
		{
			LOG(LOG_GAME, "Game in room %d is paused, waiting for player to resume.", room->id);
			const time_t pause_start = time(NULL);

			// Check if this is an actual disconnect (socket == -1) or idle timeout (socket still valid)
			// For actual disconnect: wait for LOGIN/RESUME flow to set room->state = IN_PROGRESS
			// For idle timeout: process messages and resume when idle player sends something
			int has_disconnected_player = 0;
			int idle_player_idx = -1;
			for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
			{
				if (room->players[i])
				{
					if (room->players[i]->socket == -1)
					{
						has_disconnected_player = 1;
					}
					else if (time(NULL) - room->players[i]->last_activity > IDLE_TIMEOUT)
					{
						idle_player_idx = i;
					}
				}
			}

			time_t last_debug_log = 0;
			while (room->state == PAUSED)
			{
				const time_t debug_now = time(NULL);
				if (debug_now - last_debug_log >= 2)
				{
					LOG(LOG_GAME, "DEBUG: Room %d PAUSED loop. has_disconnected_player=%d. Players[0]: %s (sock: %d), Players[1]: %s (sock: %d)",
						room->id, has_disconnected_player,
						room->players[0] ? room->players[0]->nickname : "NULL",
						room->players[0] ? room->players[0]->socket : -1,
						room->players[1] ? room->players[1]->nickname : "NULL",
						room->players[1] ? room->players[1]->socket : -1);
					last_debug_log = debug_now;
				}

				pthread_mutex_unlock(&room->mutex);

				// Check if total reconnect timeout has expired
				if (time(NULL) - pause_start >= RECONNECT_TIMEOUT)
				{
					LOG(LOG_GAME, "Reconnect timeout in room %d. Game over.", room->id);
					pthread_mutex_lock(&room->mutex);
					game.game_over = 1;

					// Determine the winner: the player who stayed active (not the idle/disconnected one)
					int winner_idx = -1;
					if (has_disconnected_player)
					{
						// Actual disconnect: winner is the one with valid socket
						for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
						{
							if (room->players[i] && room->players[i]->socket != -1)
							{
								winner_idx = i;
								break;
							}
						}
					}
					else if (idle_player_idx != -1)
					{
						// Idle timeout: winner is the OTHER player (not the idle one)
						winner_idx = 1 - idle_player_idx;
					}

					if (winner_idx != -1)
					{
						game.game_over = 1;
						game.game_winner = winner_idx;
						const int loser_idx = 1 - winner_idx;

						// Send GAME_WIN to winner
						if (room->players[winner_idx] && room->players[winner_idx]->socket != -1)
						{
							send_structured_message(
								room->players[winner_idx]->socket, S_GAME_WIN, 1,
								K_MSG, "Your opponent timed out."
							);
						}

						// Send DISCONNECTED to loser (if still connected)
						if (room->players[loser_idx] && room->players[loser_idx]->socket != -1)
						{
							// Send to the idle player
							send_structured_message(room->players[loser_idx]->socket, S_DISCONNECTED, 0);

							// Use helper to disconnect socket
							handle_player_disconnect(room->players[loser_idx]);
							game.player_fds[loser_idx] = -1;

							send_structured_message(
								room->players[loser_idx]->socket, S_GAME_LOSE, 1,
								K_MSG, "You timed out."
							);
						}
					}
					break;
				}

				if (has_disconnected_player)
				{
					// Actual disconnect case: don't read from sockets, wait for LOGIN/RESUME flow
					// Just process PING from the remaining connected player
					for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
					{
						if (room->players[i] && room->players[i]->socket != -1 && game.player_fds[i] != -1)
						{
							fd_set read_fds;
							struct timeval tv = {1, 0};
							FD_ZERO(&read_fds);
							FD_SET(room->players[i]->socket, &read_fds);

							if (select(room->players[i]->socket + 1, &read_fds, NULL, NULL, &tv) > 0)
							{
								char buffer[MSG_MAX_LEN];
								const ssize_t recv_result = receive_command(room->players[i], buffer);
								if (recv_result > 0)
								{
									parsed_command_t cmd;
									if (parse_command(buffer, &cmd) == 0 && cmd.type == CMD_PING)
									{
										send_structured_message(room->players[i]->socket, S_OK, 1, K_CMD, C_PING);
									}
									// Ignore other commands, wait for reconnection
								}
								else if (recv_result != -3)
								{
									// This player also disconnected (not just timeout)
									LOG(LOG_GAME, "Player %s also disconnected.", room->players[i]->nickname);
									handle_player_disconnect(room->players[i]);
									game.player_fds[i] = -1;
								}
							}
						}
					}
					usleep(100000); // 100ms to avoid busy loop
				}
				else
				{
					// Idle timeout case: process messages from all players
					for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
					{
						if (room->players[i] && room->players[i]->socket != -1)
						{
							fd_set read_fds;
							struct timeval tv = {1, 0};
							FD_ZERO(&read_fds);
							FD_SET(room->players[i]->socket, &read_fds);

							if (select(room->players[i]->socket + 1, &read_fds, NULL, NULL, &tv) > 0)
							{
								char buffer[MSG_MAX_LEN];
								const ssize_t recv_result = receive_command(room->players[i], buffer);
								if (recv_result > 0)
								{
									parsed_command_t cmd;
									if (parse_command(buffer, &cmd) == 0)
									{
										if (cmd.type == CMD_PING)
										{
											send_structured_message(room->players[i]->socket, S_OK, 1, K_CMD, C_PING);
										}

										// If the idle player sent a message, resume game
										if (i == idle_player_idx)
										{
											LOG(LOG_GAME, "Player %s is back, resuming game.", room->players[i]->nickname);
											const int other_idx = 1 - i;
											if (room->players[other_idx] && room->players[other_idx]->socket != -1)
											{
												send_structured_message(room->players[other_idx]->socket, S_OPPONENT_RECONNECTED, 0);
											}
											pthread_mutex_lock(&room->mutex);
											room->state = IN_PROGRESS;
											broadcast_room_update(room);
											pthread_mutex_unlock(&room->mutex);
										}
									}
								}
								else if (recv_result != -3)
								{
									// Player disconnected (not just timeout)
									LOG(LOG_GAME, "Player %s disconnected.", room->players[i]->nickname);
									handle_player_disconnect(room->players[i]);
									game.player_fds[i] = -1;
									has_disconnected_player = 1;
								}
							}
						}
					}
				}

				pthread_mutex_lock(&room->mutex);
			}
		}
		pthread_mutex_unlock(&room->mutex);

		// After a potential pause, player sockets might have changed (reconnect).
		// Update the game's file descriptors from the room's player data.
		for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i)
		{
			if (room->players[i] && game.player_fds[i] != room->players[i]->socket)
			{
				send_game_state(room->players[i], room, &game);
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
		struct timeval tv = {1, 0};

		// Wait for activity on any of the player sockets
		const int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

		if ((activity < 0) && (errno != EINTR))
		{
			LOG(LOG_GAME, "Select error: %s", strerror(errno));
		}

		if (activity > 0)
		{
			for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
			{
				if (FD_ISSET(game.player_fds[i], &read_fds))
				{
					handle_game_input(room, &game, i);
					if (game.game_over) break;
				}
			}
		}
		else if (activity == 0)
		{
			// Select timed out - check for idle players
			const time_t now = time(NULL);
			for (int i = 0; i < MAX_PLAYERS_PER_ROOM; i++)
			{
				if (room->players[i] && room->players[i]->socket != -1)
				{
					if (now - room->players[i]->last_activity > IDLE_TIMEOUT)
					{
						LOG(LOG_GAME, "Player %s timed out in game (idle %ld seconds).",
							room->players[i]->nickname, now - room->players[i]->last_activity);

						// Notify the other player about the disconnection
						const int other_idx = 1 - i;
						if (room->players[other_idx] && room->players[other_idx]->socket != -1)
						{
							send_structured_message(room->players[other_idx]->socket, S_OPPONENT_DISCONNECTED, 0);
						}

						// Keep socket open - player can resume by sending any message
						// Just pause the game
						pthread_mutex_lock(&room->mutex);
						room->state = PAUSED;
						broadcast_room_update(room);
						pthread_mutex_unlock(&room->mutex);
						break;
					}
				}
			}
		}
	}

	reset_room_after_game(room);
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

void send_game_state(const player_t* player, const room_t* room, const game_state* game)
{
	const int player_index = room->players[0] == player ? 0 : 1;

	char my_score[10], opp_score[10], turn_score[10], roll_result[10];
	sprintf(my_score, "%d", game->scores[player_index]);
	sprintf(opp_score, "%d", game->scores[1 - player_index]);
	sprintf(turn_score, "%d", game->turn_score);
	sprintf(roll_result, "%d", game->roll_result);

	send_structured_message(
		player->socket, S_GAME_STATE, 5,
		K_MY_SCORE, my_score,
		K_OPP_SCORE, opp_score,
		K_TURN_SCORE, turn_score,
		K_ROLL, roll_result,
		K_YOUR_TURN, player_index == game->current_player ? "1" : "0"
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
		curr->socket, S_GAME_STATE, 5,
		K_MY_SCORE, curr_score,
		K_OPP_SCORE, next_score,
		K_TURN_SCORE, turn_score,
		K_ROLL, roll_result,
		K_YOUR_TURN, "1"
	);

	send_structured_message(
		next->socket, S_GAME_STATE, 5,
		K_MY_SCORE, next_score,
		K_OPP_SCORE, curr_score,
		K_TURN_SCORE, turn_score,
		K_ROLL, roll_result,
		K_YOUR_TURN, "0"
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
	ssize_t login_result;
	while ((login_result = receive_command(player, buffer)) == -3)
	{
		// Socket timeout - keep waiting for LOGIN command
	}
	if (login_result <= 0)
	{
		LOG(LOG_LOBBY, "Client on socket %d disconnected before login.", client_socket);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	parsed_command_t cmd;
	if (parse_command(buffer, &cmd) != 0)
	{
		LOG(LOG_LOBBY, "Malformed login command from socket %d.", client_socket);
		send_error(client_socket, NULL, E_INVALID_COMMAND);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	if (cmd.type != CMD_LOGIN)
	{
		LOG(LOG_LOBBY, "Invalid command from socket %d, expected LOGIN.", client_socket);
		send_error(client_socket, NULL, E_INVALID_COMMAND);
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
		send_error(client_socket, C_LOGIN, E_INVALID_NICKNAME);
		remove_player(player);
		close(client_socket);
		return NULL;
	}

	// Check if a player with this nickname is already active
	player_t* active_player = find_active_player_by_nickname(nickname);
	if (active_player)
	{
		LOG(LOG_LOBBY, "Player tried to connect with active nickname: %s. Invalidating old session.", nickname);
		
		// Invalidate the old socket so the game/lobby thread detects disconnect
		if (active_player->socket != -1)
		{
			shutdown(active_player->socket, SHUT_RDWR);
			close(active_player->socket);
		}

		send_error(client_socket, C_LOGIN, E_NICKNAME_IN_USE);
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

		ssize_t resume_result;
		while ((resume_result = receive_command(player, buffer)) == -3)
		{
			// Socket timeout - keep waiting for RESUME command
		}

		if (resume_result > 0)
		{
			parsed_command_t resume_cmd;
			if (parse_command(buffer, &resume_cmd) == 0 && resume_cmd.type == CMD_RESUME)
			{
				LOG(LOG_LOBBY, "Player %s resumed game in room %d.", player->nickname, room->id);
				LOG(LOG_LOBBY, "Player %s resumed game in room %d. Signaling game thread.", player->nickname, room->id);
				pthread_mutex_lock(&room->mutex);
				room->state = IN_PROGRESS;
				broadcast_room_update(room);
				pthread_cond_broadcast(&room->cond); // Changed to broadcast
				pthread_mutex_unlock(&room->mutex);

				send_structured_message(client_socket, S_OK, 1, K_CMD, C_RESUME);

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
		send_structured_message(client_socket, S_OK, 2, K_CMD, C_LOGIN, K_NICK, nickname);
	}
	return player;
}

static void handle_lobby_command(player_t* player, const parsed_command_t* lobby_cmd)
{
	const int client_socket = player->socket;
	switch (lobby_cmd->type)
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
						case WAITING:
							strcpy(state_str, "WAITING");
							break;
						case IN_PROGRESS:
							strcpy(state_str, "IN_PROGRESS");
							break;
						case PAUSED:
							strcpy(state_str, "PAUSED");
							break;
						case ABORTED:
							strcpy(state_str, "ABORTED");
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
				const char* room_id_str = get_command_arg(lobby_cmd, K_ROOM);
				if (!room_id_str)
				{
					LOG(
						LOG_LOBBY, "JOIN_ROOM command from %s missing room ID. Disconnecting.", player->nickname
					);
					send_error(client_socket, C_JOIN_ROOM, E_INVALID_COMMAND);
					remove_player(player);
					close(client_socket);
					return;
				}
				const int room_id = atoi(room_id_str);
				LOG(LOG_LOBBY, "Player %s trying to join room %d.", player->nickname, room_id);
				if (join_room(room_id, player) == 0)
				{
					send_structured_message(client_socket, S_OK, 2, K_CMD, C_JOIN_ROOM, K_ROOM, room_id_str);
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
					send_error(client_socket, C_JOIN_ROOM, E_CANNOT_JOIN);
				}
				break;
			}
		case CMD_LEAVE_ROOM:
			{
				LOG(LOG_LOBBY, "Player %s leaving room.", player->nickname);
				if (leave_room(player) == 0)
				{
					send_structured_message(client_socket, S_OK, 1, K_CMD, C_LEAVE_ROOM);
				}
				else
				{
					send_error(client_socket, C_LEAVE_ROOM, E_GAME_IN_PROGRESS);
				}
				break;
			}
		case CMD_PING:
			{
				send_structured_message(client_socket, S_OK, 1, K_CMD, C_PING);
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
				send_error(client_socket, NULL, E_INVALID_COMMAND);
				remove_player(player);
				close(client_socket);
				return;
			}
	}
}

static void handle_main_loop(player_t* player)
{
	const int client_socket = player->socket;

	while (player->socket != -1)
	{
		// Check if the socket handled by this thread is still the active one for the player.
		// If not, it means a reconnection happened and this thread is obsolete.
		if (player->socket != client_socket)
		{
			LOG(LOG_SERVER, "Thread for socket %d detected player %s is now on socket %d. Exiting.",
				client_socket, player->nickname, player->socket);
			return;
		}

		if (player->state == LOBBY)
		{
			// Use select() with timeout to allow idle detection
			fd_set read_fds;
			struct timeval tv;
			tv.tv_sec = IDLE_TIMEOUT / 2;
			tv.tv_usec = 0;

			FD_ZERO(&read_fds);
			FD_SET(player->socket, &read_fds);

			const int activity = select(player->socket + 1, &read_fds, NULL, NULL, &tv);

			if (activity < 0 && errno != EINTR)
			{
				LOG(LOG_LOBBY, "Select error for player %s: %s", player->nickname, strerror(errno));
				remove_player(player);
				close(client_socket);
				return;
			}

			if (activity == 0)
			{
				// Timeout - check if player should be disconnected for inactivity
				if (time(NULL) - player->last_activity > IDLE_TIMEOUT)
				{
					LOG(
						LOG_LOBBY, "Player %s timed out in lobby (idle %ld seconds).",
						player->nickname, time(NULL) - player->last_activity
					);
					send_structured_message(client_socket, S_DISCONNECTED, 0);
					remove_player(player);
					close(client_socket);
					return;
				}
				continue; // Go back to waiting
			}

			// Data available - read command
			char buffer[MSG_MAX_LEN];
			const ssize_t recv_result = receive_command(player, buffer);
			if (recv_result == -3)
			{
				// Socket timeout - continue waiting
				continue;
			}
			if (recv_result <= 0)
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
				send_error(client_socket, NULL, E_INVALID_COMMAND);
				remove_player(player);
				close(client_socket);
				return;
			}
			handle_lobby_command(player, &lobby_cmd);
		}
		else if (player->state == IN_GAME)
		{
			room_t* room = get_room(player->room_id);
			if (room)
			{
				pthread_mutex_lock(&room->mutex);
				if (room->state == WAITING)
				{
					// Player is waiting for an opponent. Wait with a timeout to allow leaving.
					struct timespec ts;
					clock_gettime(CLOCK_REALTIME, &ts);
					ts.tv_sec += 5; // 1 second timeout

					const int wait_result = pthread_cond_timedwait(&room->cond, &room->mutex, &ts);
					pthread_mutex_unlock(&room->mutex); // Unlock after wait

					if (wait_result == ETIMEDOUT)
					{
						// Check for idle timeout
						if (time(NULL) - player->last_activity > IDLE_TIMEOUT)
						{
							LOG(
								LOG_LOBBY, "Player %s timed out in waiting room (idle %ld seconds).",
								player->nickname, time(NULL) - player->last_activity
							);
							send_structured_message(client_socket, S_DISCONNECTED, 0);
							leave_room(player);
							remove_player(player);
							close(client_socket);
							return;
						}

						// Timeout: check for commands without blocking.
						fd_set read_fds;
						struct timeval tv = {0, 0};
						FD_ZERO(&read_fds);
						FD_SET(player->socket, &read_fds);

						if (select(player->socket + 1, &read_fds, NULL, NULL, &tv) > 0)
						{
							char buffer[MSG_MAX_LEN];
							const ssize_t recv_result = receive_command(player, buffer);
							if (recv_result == -3)
							{
								// Socket timeout - continue waiting
								continue;
							}
							if (recv_result > 0)
							{
								parsed_command_t cmd;
								if (parse_command(buffer, &cmd) == 0)
								{
									if (cmd.type == CMD_LEAVE_ROOM)
									{
										if (leave_room(player) == 0)
										{
											send_structured_message(player->socket, S_OK, 1, K_CMD, C_LEAVE_ROOM);
										}
										else
										{
											send_error(player->socket, C_LEAVE_ROOM, E_GAME_IN_PROGRESS);
										}
									}
									else if (cmd.type == CMD_PING)
									{
										send_structured_message(player->socket, S_OK, 1, K_CMD, C_PING);
									}
									else
									{
										send_error(player->socket, NULL, E_INVALID_COMMAND);
									}
								}
								else
								{
									send_error(player->socket, NULL, E_INVALID_COMMAND);
								}
							}
							else
							{
								// Disconnected while waiting (recv_result <= 0, not -3)
								LOG(LOG_LOBBY, "Player %s disconnected from waiting room.", player->nickname);
								leave_room(player);
								remove_player(player);
								close(client_socket);
								return;
							}
						}
					}
					// If wait was successful, loop will continue and re-evaluate states.
				}
				else
				{
					// Game is IN_PROGRESS or PAUSED. Wait until game is over.
					while (player->state == IN_GAME)
					{
						pthread_cond_wait(&room->cond, &room->mutex);
					}
					pthread_mutex_unlock(&room->mutex);
				}
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

		// Set socket receive timeout to detect disconnections faster
		struct timeval recv_timeout;
		recv_timeout.tv_sec = 5;
		recv_timeout.tv_usec = 0;
		if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0)
		{
			LOG(LOG_SERVER, "setsockopt(SO_RCVTIMEO) failed: %s", strerror(errno));
		}

		LOG(LOG_SERVER, "Accepted new connection on socket %d.", client_socket);

		player_t* player = add_player(client_socket);
		if (!player)
		{
			LOG(LOG_SERVER, "Server is full. Rejecting connection from socket %d.", client_socket);
			send_error(client_socket, NULL, E_SERVER_FULL);
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
