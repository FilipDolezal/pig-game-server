#include <stdio.h>
#include <stdlib.h>
#include "server.h"
#include "config.h"

int main(int argc, char* argv[])
{
	int port = DEFAULT_PORT;

	if (argc > 1)
	{
		port = atoi(argv[1]);
	}

	printf("Starting server on port %d...\n", port);
	fflush(stdout);

	if (run_server(port) != 0)
	{
		fprintf(stderr, "Failed to run server\n");
		return 1;
	}

	return 0;
}
