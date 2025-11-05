#include "game.h"
#include "protocol.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void init_game(game_state* game, int p1_fd, int p2_fd)
{
    game->player_fds[0] = p1_fd;
    game->player_fds[1] = p2_fd;
    game->scores[0] = 0;
    game->scores[1] = 0;
    game->current_player = 0;
    game->turn_score = 0;
    game->game_over = 0;
    srand(time(NULL));
}

void handle_roll(game_state* game)
{
    int roll = (rand() % 6) + 1;

    if (roll == 1)
    {
        game->turn_score = 0;
        switch_player(game);
    }
    else
    {
        game->turn_score += roll;
    }
}

void handle_hold(game_state* game)
{
    game->scores[game->current_player] += game->turn_score;
    game->turn_score = 0;
    check_winner(game);
    if (!game->game_over)
    {
        switch_player(game);
    }
}

void switch_player(game_state* game)
{
    game->current_player = 1 - game->current_player;
}

void check_winner(game_state* game)
{
    if (game->scores[game->current_player] >= WINNING_SCORE)
    {
        game->game_over = 1;
    }
}
