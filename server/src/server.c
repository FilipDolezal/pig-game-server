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

// Function prototypes for internal use
void *client_handler_thread(void *arg);
void *game_thread_func(void *arg);

// --- Game Thread Function ---
void *game_thread_func(void *arg) {
    room_t *room = (room_t *)arg;
    game_state game;

    init_game(&game, room->players[0]->socket, room->players[1]->socket);

    char buffer[MSG_MAX_LEN];
    sprintf(buffer, "Game is starting! You are Player 1 (%s).", room->players[0]->nickname);
    send_payload(room->players[0]->socket, buffer);
    sprintf(buffer, "Game is starting! You are Player 2 (%s).", room->players[1]->nickname);
    send_payload(room->players[1]->socket, buffer);

    switch_player(&game);

    while (!game.game_over) {
        if (room->state == PAUSED) {
            // Handle paused state
            int disconnected_player_idx = -1;
            for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i) {
                if (room->players[i]->socket == -1) {
                    disconnected_player_idx = i;
                    break;
                }
            }

            if (disconnected_player_idx != -1) {
                if (time(NULL) - room->players[disconnected_player_idx]->disconnected_timestamp > RECONNECT_TIMEOUT) {
                    // Timeout reached
                    game.game_over = 1;
                    int winner_idx = 1 - disconnected_player_idx;
                    sprintf(buffer, "Your opponent failed to reconnect in time. You win!");
                    send_payload(room->players[winner_idx]->socket, buffer);
                    printf("Player %s timed out. Room %d is closing.\n", room->players[disconnected_player_idx]->nickname, room->id);
                    break; // Exit the while loop
                }
            } else {
                // Should not happen, but if it does, resume the game
                room->state = IN_PROGRESS;
            }
            sleep(1); // Wait before checking again
            continue; // Go to the next iteration of the while loop
        }

        int current_fd = game.player_fds[game.current_player];
        char command_buffer[MSG_MAX_LEN];

        if (receive_command(current_fd, command_buffer) > 0) {
            if (strcmp(command_buffer, CMD_ROLL) == 0) {
                handle_roll(&game);
            } else if (strcmp(command_buffer, CMD_HOLD) == 0) {
                handle_hold(&game);
            } else {
                sprintf(buffer, "Invalid command. Type 'roll' or 'hold'.");
                send_payload(current_fd, buffer);
            }
        } else {
            // Handle disconnection
            int disconnected_player_idx = game.current_player;
            int other_player_idx = 1 - disconnected_player_idx;

            room->state = PAUSED;
            room->players[disconnected_player_idx]->socket = -1;
            room->players[disconnected_player_idx]->disconnected_timestamp = time(NULL);

            sprintf(buffer, "Your opponent (%s) has disconnected. The game is paused. Waiting for them to reconnect...", room->players[disconnected_player_idx]->nickname);
            send_payload(room->players[other_player_idx]->socket, buffer);
            printf("Player %s disconnected. Game in room %d is paused.\n", room->players[disconnected_player_idx]->nickname, room->id);
        }
    }

    // Game over, clean up room and players
    for (int i = 0; i < MAX_PLAYERS_PER_ROOM; ++i) {
        if (room->players[i]->socket != -1) {
            close(room->players[i]->socket);
        }
        remove_player(room->players[i]);
    }

    room->state = WAITING;
    room->player_count = 0;
    room->players[0] = NULL;
    room->players[1] = NULL;

    printf("Game in room %d finished. Room is now available.\n", room->id);
    pthread_exit(NULL);
}

// --- Client Handler Thread Function ---
void *client_handler_thread(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[MSG_MAX_LEN];
    send_payload(client_socket, "Welcome! Please login with: LOGIN <nickname>");

    char nickname[NICKNAME_LEN] = {0};
    while (nickname[0] == '\0') {
        if (receive_command(client_socket, buffer) <= 0) {
            printf("Client disconnected before login.\n");
            close(client_socket);
            pthread_exit(NULL);
        }
        char command[MSG_MAX_LEN];
        char arg1[MSG_MAX_LEN];
        int items = sscanf(buffer, "%s %s", command, arg1);
        if (items >= 2 && strcmp(command, CMD_LOGIN) == 0) {
            strncpy(nickname, arg1, NICKNAME_LEN - 1);
        } else {
            send_payload(client_socket, "Please login first with: LOGIN <nickname>");
        }
    }

    player_t *player = find_disconnected_player(nickname);
    if (player) {
        // Reconnecting player
        player->socket = client_socket;
        room_t *room = get_room(player->room_id);
        room->state = IN_PROGRESS;
        sprintf(buffer, "Reconnected to your game in room %d!", player->room_id);
        send_payload(client_socket, buffer);
        
        // Notify the other player
        int other_player_idx = -1;
        for(int i=0; i<MAX_PLAYERS_PER_ROOM; ++i) {
            if(room->players[i] != player) {
                other_player_idx = i;
                break;
            }
        }
        if(other_player_idx != -1) {
            sprintf(buffer, "Player %s has reconnected! The game will resume.", player->nickname);
            send_payload(room->players[other_player_idx]->socket, buffer);
        }

        pthread_exit(NULL); // This thread is done, game thread handles it now
    }

    // New player
    player = add_player(client_socket);
    if (!player) {
        send_payload(client_socket, "Server is full. Connection rejected.");
        close(client_socket);
        pthread_exit(NULL);
    }
    strcpy(player->nickname, nickname);

    sprintf(buffer, "Welcome, %s! Type LIST_ROOMS, CREATE_ROOM, or JOIN_ROOM <id>.", player->nickname);
    send_payload(client_socket, buffer);

    while (player->state == LOBBY) {
        if (receive_command(client_socket, buffer) <= 0) {
            printf("Player %s disconnected from lobby.\n", player->nickname);
            remove_player(player);
            close(client_socket);
            pthread_exit(NULL);
        }

        char command[MSG_MAX_LEN];
        char arg1[MSG_MAX_LEN];
        int items = sscanf(buffer, "%s %s", command, arg1);

        if (items >= 1 && strcmp(command, CMD_LIST_ROOMS) == 0) {
            get_room_list(buffer, BUFFER_SIZE);
            send_payload(client_socket, buffer);
        } else if (items >= 1 && strcmp(command, CMD_CREATE_ROOM) == 0) {
            int room_id = create_room(player);
            if (room_id != -1) {
                sprintf(buffer, "Room %d created. Waiting for another player...", room_id);
                send_payload(client_socket, buffer);
            } else {
                send_payload(client_socket, "Could not create room. Max rooms reached.");
            }
        } else if (items >= 2 && strcmp(command, CMD_JOIN_ROOM) == 0) {
            int room_id = atoi(arg1);
            if (join_room(room_id, player) == 0) {
                room_t *room = get_room(room_id);
                if (room->player_count == MAX_PLAYERS_PER_ROOM) {
                    pthread_create(&room->game_thread, NULL, game_thread_func, (void *)room);
                    pthread_detach(room->game_thread);
                } else {
                    sprintf(buffer, "Joined room %d. Waiting for another player...", room_id);
                    send_payload(client_socket, buffer);
                }
            } else {
                send_payload(client_socket, "Could not join room. Invalid ID or room is full/in-game.");
            }
        } else {
            send_payload(client_socket, "Unknown command. Type LIST_ROOMS, CREATE_ROOM, or JOIN_ROOM <id>.");
        }
    }

    pthread_exit(NULL);
}

// --- Main Server Loop ---
int run_server(int port) {
    int server_fd;
    struct sockaddr_in server_addr;

    init_lobby();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket()");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt()");
        close(server_fd);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind()");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, MAX_PLAYERS) < 0) {
        perror("listen()");
        close(server_fd);
        return -1;
    }

    printf("Server listening on port %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) {
            fprintf(stderr, "Failed to allocate memory for client socket.\n");
            continue;
        }
        *client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_socket < 0) {
            perror("accept()");
            free(client_socket);
            continue;
        }

        printf("New client connected from %s.\n", inet_ntoa(client_addr.sin_addr));

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler_thread, (void *)client_socket) != 0) {
            perror("pthread_create");
            close(*client_socket);
            free(client_socket);
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}