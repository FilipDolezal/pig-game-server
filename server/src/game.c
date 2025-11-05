#include "game.h"
#include "protocol.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void init_game(game_state *game, int p1_fd, int p2_fd) {
    game->player_fds[0] = p1_fd;
    game->player_fds[1] = p2_fd;
    game->scores[0] = 0;
    game->scores[1] = 0;
    game->current_player = 0;
    game->turn_score = 0;
    game->game_over = 0;
    srand(time(NULL));
}

void handle_roll(game_state *game) {
    int roll = (rand() % 6) + 1;
    char buffer[MSG_MAX_LEN];

    if (roll == 1) {
        game->turn_score = 0;
        sprintf(buffer, "You rolled a 1 and lost all points from this turn!");
        send_payload(game->player_fds[game->current_player], buffer);
        switch_player(game);
    } else {
        game->turn_score += roll;
        sprintf(buffer, "You rolled a %d. Your score for this turn is %d.", roll, game->turn_score);
        send_payload(game->player_fds[game->current_player], buffer);
    }
}

void handle_hold(game_state *game) {
    game->scores[game->current_player] += game->turn_score;
    game->turn_score = 0;
    char buffer[MSG_MAX_LEN];
    sprintf(buffer, "You held your points. Your total score is now %d.", game->scores[game->current_player]);
    send_payload(game->player_fds[game->current_player], buffer);
    check_winner(game);
    if (!game->game_over) {
        switch_player(game);
    }
}

void switch_player(game_state *game) {
    game->current_player = 1 - game->current_player;
    char buffer_your_turn[MSG_MAX_LEN];
    char buffer_opponent_turn[MSG_MAX_LEN];

    sprintf(buffer_your_turn, "It's your turn to play! Current score: P1 %d - P2 %d. Your turn score: %d.", 
            game->scores[0], game->scores[1], game->turn_score);
    send_payload(game->player_fds[game->current_player], buffer_your_turn);

    sprintf(buffer_opponent_turn, "It's your opponent's turn. Current score: P1 %d - P2 %d.", 
            game->scores[0], game->scores[1]);
    send_payload(game->player_fds[1 - game->current_player], buffer_opponent_turn);
}

void check_winner(game_state *game) {
    if (game->scores[game->current_player] >= WINNING_SCORE) {
        game->game_over = 1;
        char buffer_win[MSG_MAX_LEN];
        char buffer_lose[MSG_MAX_LEN];

        sprintf(buffer_win, "Congratulations, you won the game with %d points!", game->scores[game->current_player]);
        send_payload(game->player_fds[game->current_player], buffer_win);

        sprintf(buffer_lose, "You lost the game. Your opponent won with %d points.", game->scores[game->current_player]);
        send_payload(game->player_fds[1 - game->current_player], buffer_lose);
    }
}
