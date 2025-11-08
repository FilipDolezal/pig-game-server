# Project Overview

This project is a server for the "Pig" dice game.

*   **Server:** The server is written in C and uses a `Makefile` for building. It manages the game logic, player connections, and game state.
*   **Game:** The game is the dice game "Pig". The goal is to be the first player to reach 100 points.

## Building and Running

### Server

1.  **Build:**
    ```bash
    cd server
    make
    ```

2.  **Run:**
    ```bash
    ./cmake-build-debug/server [port]
    ```
    For example:
    ```bash
    ./cmake-build-debug/server 12345
    ```

## Development Conventions

### Server

*   The server code is written in C11.
*   It uses pthreads for multithreading.
*   The code is organized into several files: `server.c`, `lobby.c`, `game.c`, and `protocol.c`.
*   Header files are in the `include` directory.

### Protocol

The communication between the client and server follows a defined protocol. The messages are documented in `docs/pig_gameplay.md`.

## Requirements Analysis (as of 2025-11-08)

This section tracks the server's compliance with the requirements outlined in `docs/PozadavkyUPS.pdf`.

### ✔️ Met Requirements

-   **Core Architecture & Protocol:**
    -   [x] Server written in C.
    -   [x] Text-based protocol over TCP.
    -   [x] Uses standard BSD sockets for networking (no third-party libraries).
    -   [x] Code is well-structured into modules (`server`, `lobby`, `game`, `protocol`, `parser`).
-   **Stability & Robustness:**
    -   [x] Stable and does not crash on malformed input from clients.
    -   [x] Handles invalid network messages gracefully (disconnects in lobby, ignores in-game).
    -   [x] Designed for continuous operation without needing restarts.
-   **Game Flow & Player Handling:**
    -   [x] Implements a lobby where players can list and join rooms.
    -   [x] Supports 2-player games.
    -   [x] Returns players to the lobby after a game finishes.
-   **Disconnection Handling:**
    -   [x] Game correctly pauses when a player disconnects.
    -   [x] Game correctly resumes if the player reconnects within the time limit.
    -   [x] If a player times out, the opponent wins and is returned to the lobby.

### ⚠️ Partially Met / ❌ Not Met Requirements

-   **Configuration:**
    -   [⚠️] **Listening Port/IP:** The port is configurable via command-line, but the listening IP address is hardcoded (`INADDR_ANY`).
    -   [❌] **Max Players:** The total player limit (`MAX_PLAYERS`) is a hardcoded macro.
    -   [❌] **Max Rooms:** The room limit (`MAX_ROOMS`) is a hardcoded macro.
-   **Documentation & Logging:**
    -   [⚠️] **Code Documentation:** The code has some comments, but lacks sufficient detail to be considered fully documented.
    -   [❌] **Logging:** No formal logging mechanism exists beyond `printf` to stdout.

## Code Review Findings (as of 2025-11-08)

This section lists findings from the code review and suggestions for improvement.

### `game.c`
- [ ] **Thread-Safety of `rand()`:** The `rand()` function is not thread-safe and is re-seeded on every game start.
    - **Suggestion:** Seed `rand()` only once in `main()`. For better thread safety, consider using `rand_r()` or a dedicated, mutex-protected random number generator.

### `lobby.c`
- [ ] **Global Mutex Bottleneck:** A single `lobby_mutex` protects all global player and room operations, which could become a bottleneck.
    - **Suggestion (Low Priority):** For future scaling, consider more granular locking. The current implementation is fine for the project's scale.

### `protocol.c`
- [ ] ~~**Potential Buffer Overflow:** `send_structured_message` does not check if the total composed message length exceeds `MSG_MAX_LEN`, which could lead to truncated messages.~~
    - **User Note:** Marked as not a concern for now.
- [x] **Incomplete `read()` Handling:** `receive_command` assumes a full command arrives in a single `read()` call, which is not guaranteed over TCP.
    - **Suggestion (Low Priority):** For higher robustness, implement a loop that reads from the socket until a newline `\n` is found.

### `server.c`
- [x] **Minor Memory Leak:** In `run_server`, memory allocated for `client_socket` is not freed if `pthread_create` fails.
    - **Suggestion:** Add `free(client_socket);` in the `pthread_create` error handling block.
- [x] **Function Length:** `client_handler_thread` is very long and handles multiple distinct states.
    - **Suggestion (Stylistic):** Break out the logic for Login, Reconnect, and Lobby states into smaller static helper functions to improve readability.
- [x] **Detached Threads:** Threads created for clients are not explicitly detached, which can lead to resource leaks on some systems.
    - **Suggestion:** Call `pthread_detach(tid);` immediately after `pthread_create` in `run_server`.

## Code Review Findings (Round 2 - as of 2025-11-08)

This section lists new findings from a second code review.

### `config.h`
- [ ] **Redundant Macros:** Defines both `MAX_CLIENTS` and `MAX_PLAYERS` with the same value, but `MAX_CLIENTS` is unused.
    - **Suggestion:** Remove the `MAX_CLIENTS` macro to avoid confusion.

### `lobby.h`
- [ ] **Inconsistent Type:** `buffer_len` is an `int` while size comparisons use `size_t` (unsigned), causing compiler warnings.
    - **Suggestion:** Change `buffer_len` to `size_t`.

### `lobby.c`
- [x] **Dangling Pointer Risk:** `remove_player` does not remove a player from the `room->players` array, which can lead to a dangling pointer if the player was in a waiting room.
    - **Suggestion:** Make `remove_player` check `player->room_id` and clean up the player's reference from the corresponding room.
- [x] **Dead Code:** The function `get_room_list` is declared in `lobby.h` but not defined or used.
    - **Suggestion:** Remove the declaration from `lobby.h`.

### `parser.c`
- [ ] **Thread-Safety of `strtok`:** The parser uses `strtok`, which is not re-entrant and can be problematic in multithreaded applications.
    - **Suggestion (High Priority):** Replace `strtok` with the thread-safe `strtok_r`.

### `server.c`
- [ ] **Inefficient Polling:** The `handle_main_loop` uses `sleep(1)` to make the first player wait for a game to start, which is inefficient.
    - **Suggestion:** Use a condition variable (`room->cond`) to make the first player's thread wait efficiently and be woken up instantly when the game starts.

### `main.c`
- [ ] **Incorrect `rand()` Seeding:** The `rand()` function is still seeded inside `init_game`, which is called for every game.
    - **Suggestion:** Move `srand(time(NULL));` to be called only once at server startup in `main()`.
