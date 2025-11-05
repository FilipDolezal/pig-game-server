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

// Global arrays for players and rooms, managed by lobby.c
// These are declared extern in lobby.h

// Function prototypes for internal use
void *client_handler_thread(void *arg);
void *game_thread_func(void *arg);

// --- Game Thread Function --- 
void *game_thread_func(void *arg) {
    room_t *room = (room_t *)arg;
    game_state game;

    // Initialize game with players from the room
    init_game(&game, room->players[0]->socket, room->players[1]->socket);

    // Notify players that the game is starting
    char buffer[MSG_MAX_LEN];
    sprintf(buffer, "Game is starting! You are Player 1 (%s).", room->players[0]->nickname);
    send_payload(room->players[0]->socket, buffer);
    sprintf(buffer, "Game is starting! You are Player 2 (%s).", room->players[1]->nickname);
    send_payload(room->players[1]->socket, buffer);

    // Start the first turn
    switch_player(&game); 

    while (!game.game_over) {
        int current_fd = game.player_fds[game.current_player];
        char command_buffer[MSG_MAX_LEN];

        if (receive_command(current_fd, command_buffer) > 0) {
            if (strcmp(command_buffer, "roll") == 0) {
                handle_roll(&game);
            } else if (strcmp(command_buffer, "hold") == 0) {
                handle_hold(&game);
            } else {
                sprintf(buffer, "Invalid command. Type 'roll' or 'hold'.");
                send_payload(current_fd, buffer);
            }
        } else {
            // Handle disconnection
            game.game_over = 1;
            int disconnected_player_idx = game.current_player;
            int other_player_idx = 1 - disconnected_player_idx;
            
            sprintf(buffer, "Your opponent (%s) has disconnected. You win!", room->players[disconnected_player_idx]->nickname);
            send_payload(game.player_fds[other_player_idx], buffer);
            printf("Player %s disconnected.\n", room->players[disconnected_player_idx]->nickname);
        }
    }

    // Game over, clean up room and players
    close(room->players[0]->socket);
    close(room->players[1]->socket);
    remove_player(room->players[0]);
    remove_player(room->players[1]);

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
    free(arg); // Free the dynamically allocated socket descriptor

    player_t *player = add_player(client_socket);
    if (!player) {
        send_payload(client_socket, "Server is full. Connection rejected.");
        close(client_socket);
        pthread_exit(NULL);
    }

    char buffer[MSG_MAX_LEN];
    send_payload(client_socket, "Welcome to the lobby! Please login with: LOGIN <nickname>");

    // State: Awaiting Login
    while (player->nickname[0] == '\0') { // Nickname not set
        if (receive_command(client_socket, buffer) <= 0) {
            printf("Client disconnected before login.\n");
            remove_player(player);
            close(client_socket);
            pthread_exit(NULL);
        }
        if (strncmp(buffer, "LOGIN ", 6) == 0) {
            char *nickname = buffer + 6;
            if (strlen(nickname) > 0 && strlen(nickname) < NICKNAME_LEN) {
                // Check if nickname is already taken (simplified check)
                // For a real game, this would involve iterating through all active players
                strcpy(player->nickname, nickname);
                sprintf(buffer, "Welcome, %s! Type LIST_ROOMS, CREATE_ROOM, or JOIN_ROOM <id>.", player->nickname);
                send_payload(client_socket, buffer);
            } else {
                send_payload(client_socket, "Invalid nickname. Max length 31 characters.");
            }
        } else {
            send_payload(client_socket, "Please login first with: LOGIN <nickname>");
        }
    }

    // State: In Lobby, processing commands
    while (player->state == LOBBY) {
        if (receive_command(client_socket, buffer) <= 0) {
            printf("Player %s disconnected from lobby.\n", player->nickname);
            remove_player(player);
            close(client_socket);
            pthread_exit(NULL);
        }

        if (strcmp(buffer, "LIST_ROOMS") == 0) {
            get_room_list(buffer, BUFFER_SIZE);
            send_payload(client_socket, buffer);
        } else if (strcmp(buffer, "CREATE_ROOM") == 0) {
            int room_id = create_room(player);
            if (room_id != -1) {
                sprintf(buffer, "Room %d created. Waiting for another player...", room_id);
                send_payload(client_socket, buffer);
            } else {
                send_payload(client_socket, "Could not create room. Max rooms reached.");
            }
        } else if (strncmp(buffer, "JOIN_ROOM ", 10) == 0) {
            int room_id = atoi(buffer + 10);
            if (join_room(room_id, player) == 0) {
                room_t *room = get_room(room_id);
                if (room->player_count == MAX_PLAYERS_PER_ROOM) {
                    // Room is full, start game thread
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

    // If player state changes to IN_GAME, this thread's lobby duties are done.
    // The game_thread_func will handle game communication.
    // This thread will now just wait for the game to end or player to disconnect.
    // For now, we'll just exit this thread. A more robust solution might keep it alive
    // to handle post-game lobby return or in-game chat.
    pthread_exit(NULL);
}

// --- Main Server Loop --- 
int run_server(int port) {
    int server_fd;
    struct sockaddr_in server_addr;

    init_lobby(); // Initialize the lobby system

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

    if (listen(server_fd, MAX_PLAYERS) < 0) { // Listen for MAX_PLAYERS connections
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
        pthread_detach(tid); // Detach thread to clean up resources automatically
    }

    close(server_fd);
    return 0;
}
