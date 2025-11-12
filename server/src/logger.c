#include "logger.h"
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

#define NUM_LOG_FILES 4 // server, lobby, game, all

static FILE* log_files[NUM_LOG_FILES];
static FILE* all_log_file;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* component_filenames[] = {
    [LOG_SERVER] = "server.log",
    [LOG_LOBBY] = "lobby.log",
    [LOG_GAME] = "game.log",
    [LOG_GENERAL] = "general.log"
};

static const char* component_strings[] = {
    [LOG_SERVER] = "SERVER",
    [LOG_LOBBY] = "LOBBY",
    [LOG_GAME] = "GAME",
    [LOG_GENERAL] = "GENERAL"
};

int init_logger(const char* log_dir) {
    const char* dir_name = (log_dir && *log_dir) ? log_dir : "logs";
    char path_buffer[256];

    // If the provided path is relative, prepend the project source directory
    if (dir_name[0] != '/') {
        snprintf(path_buffer, sizeof(path_buffer), "%s/%s", PROJECT_SOURCE_DIR, dir_name);
    } else {
        strncpy(path_buffer, dir_name, sizeof(path_buffer));
    }

    // Create log directory if it doesn't exist
    if (mkdir(path_buffer, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create log directory");
        return -1;
    }

    char file_path_buffer[512];

    // Open component-specific log files
    for (int i = 0; i < NUM_LOG_FILES - 1; ++i) {
        snprintf(file_path_buffer, sizeof(file_path_buffer), "%s/%s", path_buffer, component_filenames[i]);
        log_files[i] = fopen(file_path_buffer, "a");
        if (!log_files[i]) {
            perror("Failed to open component log file");
            return -1;
        }
    }

    // Open the "all" log file
    snprintf(file_path_buffer, sizeof(file_path_buffer), "%s/all.log", path_buffer);
    all_log_file = fopen(file_path_buffer, "a");
    if (!all_log_file) {
        perror("Failed to open all.log file");
        return -1;
    }

    return 0;
}

void app_log(log_component_t component, const char* file, int line, const char* fmt, ...) {
    pthread_mutex_lock(&log_mutex);

    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    char message_body[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message_body, sizeof(message_body), fmt, args);
    va_end(args);

    // Prepare the log message for component-specific file
    char component_log_entry[2048];
    snprintf(component_log_entry, sizeof(component_log_entry), "[%s] [%s:%d]: %s\n", time_buf, file, line, message_body);

    // Write to the specific component log file
    if (component >= 0 && component < NUM_LOG_FILES) {
        FILE* component_file = log_files[component];
        if (component_file) {
            fprintf(component_file, "%s", component_log_entry);
            fflush(component_file);
        }
    }

    // Prepare the log message for all.log
    char all_log_entry[2048];
    snprintf(all_log_entry, sizeof(all_log_entry), "[%s] [%s] [%s:%d]: %s\n", time_buf, component_strings[component], file, line, message_body);

    // Write to the "all" log file
    if (all_log_file) {
        fprintf(all_log_file, "%s", all_log_entry);
        fflush(all_log_file);
    }

    pthread_mutex_unlock(&log_mutex);
}

void close_logger() {
    pthread_mutex_lock(&log_mutex);
    for (int i = 0; i < NUM_LOG_FILES; ++i) {
        if (log_files[i]) {
            fclose(log_files[i]);
        }
    }
    if (all_log_file) {
        fclose(all_log_file);
    }
    pthread_mutex_unlock(&log_mutex);
}
