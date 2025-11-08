#include "parser.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>

// Helper to map command strings to enum values
static client_command_t get_command_type(const char* verb)
{
	if (strcmp(verb, C_LOGIN) == 0) return CMD_LOGIN;
	if (strcmp(verb, C_RESUME) == 0) return CMD_RESUME;
	if (strcmp(verb, C_LIST_ROOMS) == 0) return CMD_LIST_ROOMS;
	if (strcmp(verb, C_JOIN_ROOM) == 0) return CMD_JOIN_ROOM;
	if (strcmp(verb, C_LEAVE_ROOM) == 0) return CMD_LEAVE_ROOM;
	if (strcmp(verb, C_ROLL) == 0) return CMD_ROLL;
	if (strcmp(verb, C_HOLD) == 0) return CMD_HOLD;
	if (strcmp(verb, C_QUIT) == 0) return CMD_QUIT;
	return CMD_UNKNOWN;
}

int parse_command(char* buffer, parsed_command_t* out_cmd)
{
	// Initialize the output struct
	out_cmd->type = CMD_UNKNOWN;
	out_cmd->arg_count = 0;

	if (buffer == NULL)
	{
		return -1;
	}

	char* saveptr1;
	char* saveptr2;

	// The first token is the command verb
	const char* verb = strtok_r(buffer, "|", &saveptr1);
	if (verb == NULL)
	{
		return -1; // Empty command
	}

	out_cmd->type = get_command_type(verb);

	// Subsequent tokens are arguments
	char* token;
	while ((token = strtok_r(NULL, "|", &saveptr1)) != NULL)
	{
		if (out_cmd->arg_count >= MAX_ARGS)
		{
			return -1; // Too many arguments
		}

		char* key = strtok_r(token, ":", &saveptr2);
		if (key == NULL)
		{
			return -1; // Argument without a key
		}

		char* value = strtok_r(NULL, ":", &saveptr2);
		if (value == NULL)
		{
			return -1; // Argument without a value
		}

		out_cmd->args[out_cmd->arg_count].key = key;
		out_cmd->args[out_cmd->arg_count].value = value;
		out_cmd->arg_count++;
	}

	return 0; // Success
}

const char* get_command_arg(const parsed_command_t* cmd, const char* key)
{
	if (cmd == NULL || key == NULL)
	{
		return NULL;
	}
	for (int i = 0; i < cmd->arg_count; ++i)
	{
		if (strcmp(cmd->args[i].key, key) == 0)
		{
			return cmd->args[i].value;
		}
	}
	return NULL;
}
