# Pig Gameplay

## Game Objective

The first player to score 100 or more points wins.

## Players

2 or more players.

## Game State

-   `players`: list of player objects
    -   `id`: player's unique identifier
    -   `score`: player's total score
-   `current_player_id`: the ID of the player whose turn it is
-   `turn_total`: the score accumulated in the current turn
-   `game_over`: boolean, true if a player has won

## Player Actions

-   `roll`: Roll the die.
-   `hold`: End the current turn and add `turn_total` to the player's `score`.

## Game Flow

1.  A player starts their turn. `turn_total` is 0.
2.  The player chooses an action: `roll` or `hold`.
3.  **If `roll`:**
    -   A random number between 1 and 6 is generated (the die roll).
    -   **If the roll is 1:**
        -   `turn_total` is reset to 0.
        -   The player's turn ends.
        -   It becomes the next player's turn.
    -   **If the roll is not 1:**
        -   The roll value is added to `turn_total`.
        -   The player can choose to `roll` again or `hold`.
4.  **If `hold`:**
    -   The `turn_total` is added to the current player's `score`.
    -   `turn_total` is reset to 0.
    -   **If the player's `score` is >= 100:**
        -   The player wins.
        -   `game_over` becomes true.
    -   **Else:**
        -   The player's turn ends.
        -   It becomes the next player's turn.

## Protocol Messages (Client -> Server)

-   `JOIN <nickname>`
-   `CREATE_ROOM <room_name>`
-   `JOIN_ROOM <room_name>`
-   `LIST_ROOMS`
-   `START_GAME`
-   `ROLL`
-   `HOLD`
-   `LEAVE_ROOM`

## Protocol Messages (Server -> Client)

-   `OK`
-   `ERROR <message>`
-   `ROOM_CREATED <room_name>`
-   `ROOM_JOINED <room_name>`
-   `ROOM_LIST <room1,room2,...>`
-   `GAME_STARTED <player1_nick,player2_nick,...>`
-   `TURN <player_nick>`
-   `ROLLED <number>`
-   `BUST`
-   `SCORE_UPDATE <player1_score,player2_score,...>`
-   `GAME_OVER <winner_nick>`
-   `PLAYER_LEFT <player_nick>`
