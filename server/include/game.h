#ifndef GAME_H
#define GAME_H

#include "config.h"

// Pig dice game state - tracks everything about an ongoing game
typedef struct
{
	int player_fds[2];     // socket fds for each player (index 0 and 1)
	int scores[2];         // banked points for each player
	int current_player;    // whose turn it is (0 or 1)
	int turn_score;        // points accumulated this turn (lost if you roll a 1)
	int roll_result;       // last dice roll (1-6)
	int game_over;         // 1 if game has ended
	int game_winner;       // index of winner, or -1 if no winner yet
	unsigned int rand_seed; // for thread-safe rand_r()
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
