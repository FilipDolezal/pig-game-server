#ifndef PARSER_H
#define PARSER_H

// Enum for all possible client commands
typedef enum
{
	CMD_UNKNOWN,
	CMD_LOGIN,
	CMD_RESUME,
	CMD_LIST_ROOMS,
	CMD_JOIN_ROOM,
	CMD_LEAVE_ROOM,
	CMD_ROLL,
	CMD_HOLD,
	CMD_QUIT
} client_command_t;

// A structure to hold a parsed command argument (key-value pair)
#define MAX_ARGS 5 // A command can have up to 5 arguments

typedef struct
{
	char* key;
	char* value;
} command_arg;

// A structure to hold a fully parsed command
typedef struct
{
	client_command_t type;
	command_arg args[MAX_ARGS];
	int arg_count;
} parsed_command_t;

/**
 * @brief Parses a raw command buffer into a structured parsed_command_t.
 *
 * This function tokenizes the input buffer and populates the output struct.
 * It modifies the input buffer by replacing delimiters with null terminators.
 *
 * @param buffer The mutable character buffer containing the raw command from the client.
 * @param out_cmd A pointer to the struct that will be filled with the parsed data.
 * @return 0 on success, -1 on parsing failure (malformed command).
 */
int parse_command(char* buffer, parsed_command_t* out_cmd);

/**
 * @brief Searches for the value of a specific key in a parsed command's arguments.
 *
 * @param cmd A pointer to the parsed command.
 * @param key The key to search for.
 * @return A pointer to the value string if the key is found, otherwise NULL.
 */
const char* get_command_arg(const parsed_command_t* cmd, const char* key);


#endif //PARSER_H
