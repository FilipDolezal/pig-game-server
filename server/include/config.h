#ifndef CONFIG_H
#define CONFIG_H

// Network
#define DEFAULT_PORT 12345
#define MSG_MAX_LEN 256          // max bytes per protocol message

// Lobby limits
#define MAX_PLAYERS_PER_ROOM 2
#define NICKNAME_LEN 32

// Game rules
#define WINNING_SCORE 30         // points needed to win

// Timing (all in seconds)
#define RECONNECT_TIMEOUT 20     // how long we wait for a disconnected player to come back
#define PING_INTERVAL 10         // client should ping at least this often
#define IDLE_TIMEOUT 20          // kick player after this much inactivity

// Set at runtime based on command line args (defaults in main.c)
extern int MAX_ROOMS;
extern int MAX_PLAYERS;

#endif // CONFIG_H
