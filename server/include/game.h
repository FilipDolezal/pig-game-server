#ifndef GAME_H
#define GAME_H

#include "protocol.h"
#include "config.h"

typedef struct
{
    int player_fds[2];
    int scores[2];
    int current_player;
    int turn_score;
    int game_over;
} game_state;

void init_game(game_state* game, int p1_fd, int p2_fd);
void handle_roll(game_state* game);
void handle_hold(game_state* game);
void switch_player(game_state* game);
void check_winner(game_state* game);

#endif // GAME_H
