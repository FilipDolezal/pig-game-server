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