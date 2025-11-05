# UPS-SP Server

This is the server for the Pig dice game.

## Build

To build the server, run `make`.

## Run

To run the server, execute `./bin/server [port]`.

## How to Play

1.  **Start the Server:**
    Navigate to the `server/cmake-build-debug` directory and run the server:
    ```bash
    ./server 12345
    ```

2.  **Connect Clients:**
    Open two separate terminal windows. In each terminal, connect to the server using `netcat`:
    ```bash
    nc localhost 12345
    ```

3.  **Join the Lobby:**
    Once connected, you will be in the lobby. You must first log in with a nickname:
    ```
    LOGIN <your_nickname>
    ```
    Example:
    ```
    LOGIN player1
    ```

4.  **Create or Join a Game:**
    *   To see a list of available game rooms, type:
        ```
        LIST_ROOMS
        ```
    *   To create a new room, type:
        ```
        CREATE_ROOM
        ```
        The server will respond with the ID of the created room.
    *   To join an existing room, type:
        ```
        JOIN_ROOM <room_id>
        ```
        Example:
        ```
        JOIN_ROOM 0
        ```

5.  **Start Playing:**
    Once two players have joined the same room, the game will automatically begin.

6.  **In-Game Commands:**
    *   `roll`: Roll the dice.
    *   `hold`: End your turn and add your turn score to your total score.

7.  **Game Rules:**