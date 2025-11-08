#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "server.h"
#include "config.h"
#include "lobby.h"
#include "logger.h"

int MAX_ROOMS = 5;
int MAX_PLAYERS = 10;

int main(const int argc, char* argv[])
{
	int port = DEFAULT_PORT;
	char* address = "0.0.0.0";
	char* log_file = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "p:r:a:l:")) != -1) {
		switch (opt) {
			case 'p':
				MAX_PLAYERS = atoi(optarg);
				break;
			case 'r':
				MAX_ROOMS = atoi(optarg);
				break;
			case 'a':
				address = optarg;
				break;
			case 'l':
				log_file = optarg;
				break;
			default:
				fprintf(stderr, "Usage: %s [-a address] [-p max_players] [-r max_rooms] [-l logfile] [port]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (optind < argc)
	{
		port = atoi(argv[optind]);
	}

	if (init_logger(log_file) != 0) {
		exit(EXIT_FAILURE);
	}

	init_lobby();

	LOG(LOG_INFO, "Starting server on %s:%d, max players %d, max rooms %d", address, port, MAX_PLAYERS, MAX_ROOMS);

	if (run_server(port, address) != 0)
	{
		LOG(LOG_ERROR, "Failed to run server");
		close_logger();
		return 1;
	}

	close_logger();
	return 0;
}
