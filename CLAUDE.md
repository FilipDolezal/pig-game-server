# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a multiplayer "Pig" dice game with a C server and Java client. The goal is to be the first player to reach 30 points (configurable in `config.h`).

## Build and Run Commands

### Server (C)
```bash
cd server
cmake -B cmake-build-debug -S .
cmake --build cmake-build-debug

# Run the server
./cmake-build-debug/server [port]
# Example: ./cmake-build-debug/server 12345
```

### Client (Java/Gradle)
```bash
cd client/ups-sp-client
./gradlew build
./gradlew run
```

## Architecture

### Server Components (`server/src/`)
- **main.c** - Entry point, parses port argument
- **server.c** - Main server loop, client handler threads, game thread management, socket I/O
- **lobby.c** - Player and room management (add/remove players, join/leave rooms, handle disconnects/reconnects)
- **game.c** - Pig game logic (roll, hold, win conditions)
- **protocol.c** - Message serialization/parsing, structured message sending
- **parser.c** - Command string parsing
- **logger.c** - Logging utilities

### Key Data Structures (`server/include/`)
- **player_t** - Player state (socket, nickname, room_id, connection buffer)
- **room_t** - Room state (players array, game thread, mutex/cond for synchronization)
- **game_state** - Game state (scores, current player, turn score, roll result)

### State Machines
- **player_state**: `LOBBY` -> `IN_GAME`
- **room_state**: `WAITING` -> `IN_PROGRESS` -> `PAUSED`/`ABORTED`

### Threading Model
- One thread per connected client (`client_handler_thread`)
- One thread per active game (`game_thread_func`)
- Rooms use pthread mutex/cond for synchronization between client and game threads

## Protocol

See `docs/pig_gameplay.md` for the full protocol specification. Key commands:
- Client->Server: `LOGIN`, `RESUME`, `LIST_ROOMS`, `JOIN_ROOM`, `LEAVE_ROOM`, `ROLL`, `HOLD`, `QUIT`, `PING`
- Server->Client: `OK`, `ERROR`, `GAME_START`, `GAME_STATE`, `GAME_WIN`, `GAME_LOSE`, `OPPONENT_DISCONNECTED`

### Heartbeat/PING
Clients must send `PING` at least every `PING_INTERVAL` (10s) to avoid idle timeout. Server responds with `OK|cmd:PING`. Any command resets the idle timer. Players are disconnected after `IDLE_TIMEOUT` (20s) of inactivity.

## Configuration (`server/include/config.h`)
- `DEFAULT_PORT`: 12345
- `MSG_MAX_LEN`: 256 bytes
- `MAX_PLAYERS_PER_ROOM`: 2
- `WINNING_SCORE`: 30
- `RECONNECT_TIMEOUT`: 20 seconds (in-game reconnect window)
- `PING_INTERVAL`: 10 seconds (client heartbeat interval)
- `IDLE_TIMEOUT`: 20 seconds (disconnect after inactivity)
