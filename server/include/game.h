#ifndef GAME_H
#define GAME_H

#include "config.h"

typedef struct
{
	int player_fds[2];
	int scores[2];
	int current_player;
	int turn_score;
	int roll_result;
	int game_over;
	int game_winner;
	unsigned int rand_seed;
} game_state;

/**
 * @brief Initializes a new game state.
 * @param game A pointer to the game_state object to initialize.
 * @param p1_fd The file descriptor for player 1.
 * @param p2_fd The file descriptor for player 2.
 */
void init_game(game_state* game, int p1_fd, int p2_fd);

/**
 * @brief Handles a player's roll action.
 * @param game A pointer to the current game_state object.
 */
void handle_roll(game_state* game);

/**
 * @brief Handles a player's hold action.
 * @param game A pointer to the current game_state object.
 */
void handle_hold(game_state* game);

/**
 * @brief Switches the current player.
 * @param game A pointer to the current game_state object.
 */
void switch_player(game_state* game);

#endif // GAME_H
