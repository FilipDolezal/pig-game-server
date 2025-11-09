#include "game.h"
#include "protocol.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void init_game(game_state* game, const int p1_fd, const int p2_fd)
{
	game->player_fds[0] = p1_fd;
	game->player_fds[1] = p2_fd;
	game->scores[0] = 0;
	game->scores[1] = 0;
	game->current_player = rand_r(&game->rand_seed) % 2;
	game->turn_score = 0;
	game->game_over = 0;
	game->game_winner = -1;
	game->roll_result = 0;
}

void handle_roll(game_state* game)
{
	const int roll = (rand_r(&game->rand_seed) % 6) + 1;
	game->roll_result = roll;

	if (roll == 1)
	{
		game->turn_score = 0;
		switch_player(game);
	}
	else
	{
		game->turn_score += roll;

		if (game->scores[game->current_player] + game->turn_score >= WINNING_SCORE)
		{
			game->game_over = 1;
			game->game_winner = game->current_player;
		}
	}
}

void handle_hold(game_state* game)
{
	game->scores[game->current_player] += game->turn_score;
	game->turn_score = 0;
	game->roll_result = 0;
	switch_player(game);
}

void switch_player(game_state* game)
{
	game->current_player = 1 - game->current_player;
}
