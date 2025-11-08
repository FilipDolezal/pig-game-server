#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "server.h"
#include "config.h"
#include "lobby.h"

int MAX_ROOMS = 5;
int MAX_PLAYERS = 10;

int main(const int argc, char* argv[])
{
	int port = DEFAULT_PORT;
	char* address = "0.0.0.0";
	int opt;

	while ((opt = getopt(argc, argv, "p:r:a:")) != -1) {
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
			default:
				fprintf(stderr, "Usage: %s [-a address] [-p max_players] [-r max_rooms] [port]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (optind < argc)
	{
		port = atoi(argv[optind]);
	}

	init_lobby();

	printf("Starting server on %s:%d, max players %d, max rooms %d\n", address, port, MAX_PLAYERS, MAX_ROOMS);
	fflush(stdout);

	if (run_server(port, address) != 0)
	{
		fprintf(stderr, "Failed to run server\n");
		return 1;
	}

	return 0;
}
