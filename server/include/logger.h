#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>
#include <stdio.h>

typedef enum {
    LOG_SERVER,
    LOG_LOBBY,
    LOG_GAME,
    LOG_GENERAL
} log_component_t;

/**
 * @brief Initializes the logger. Creates a log directory and opens log files.
 * @param log_dir The path to the log directory. If NULL, defaults to "logs".
 * @return 0 on success, -1 on failure.
 */
int init_logger(const char* log_dir);

/**
 * @brief Logs a message to the appropriate component log file and the all.log file. This function is thread-safe.
 * @param component The log component.
 * @param file The source file from which the log originates.
 * @param line The line number in the source file.
 * @param fmt The format string for the message.
 * @param ... Variable arguments for the format string.
 */
void app_log(log_component_t component, const char* file, int line, const char* fmt, ...);

/**
 * @brief Closes all logger files.
 */
void close_logger();

// Macro to automatically pass file and line number
#define LOG(component, fmt, ...) app_log(component, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif // LOGGER_H
