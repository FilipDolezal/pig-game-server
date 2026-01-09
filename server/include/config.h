#ifndef CONFIG_H
#define CONFIG_H

#define DEFAULT_PORT 12345
#define MSG_MAX_LEN 256
#define MAX_PLAYERS_PER_ROOM 2
#define NICKNAME_LEN 32
#define WINNING_SCORE 30
#define RECONNECT_TIMEOUT 20
#define PING_INTERVAL 10        // Client should send PING at least every 10 seconds
#define IDLE_TIMEOUT 20         // Disconnect after 20 seconds of no activity

extern int MAX_ROOMS;
extern int MAX_PLAYERS;

#endif // CONFIG_H
