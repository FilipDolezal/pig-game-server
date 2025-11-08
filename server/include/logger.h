#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>
#include <stdio.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

/**
 * @brief Initializes the logger with a file path.
 * @param filename The path to the log file. If NULL, logs to stderr.
 * @return 0 on success, -1 on failure.
 */
int init_logger(const char* filename);

/**
 * @brief Logs a message. This function is thread-safe.
 * @param level The log level.
 * @param file The source file from which the log originates.
 * @param line The line number in the source file.
 * @param fmt The format string for the message.
 * @param ... Variable arguments for the format string.
 */
void app_log(log_level_t level, const char* file, int line, const char* fmt, ...);

/**
 * @brief Closes the logger file.
 */
void close_logger();

// Macro to automatically pass file and line number
#define LOG(level, fmt, ...) app_log(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif // LOGGER_H
