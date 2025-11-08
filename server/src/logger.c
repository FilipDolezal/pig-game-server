#include "logger.h"
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

static FILE* log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* level_strings[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

int init_logger(const char* filename) {
    if (filename) {
        log_file = fopen(filename, "a");
        if (!log_file) {
            perror("Failed to open log file");
            return -1;
        }
    } else {
        log_file = stderr;
    }
    return 0;
}

void app_log(log_level_t level, const char* file, int line, const char* fmt, ...) {
    pthread_mutex_lock(&log_mutex);

    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log_file, "[%s] [%s] [%s:%d]: ", time_buf, level_strings[level], file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);

    pthread_mutex_unlock(&log_mutex);
}

void close_logger() {
    pthread_mutex_lock(&log_mutex);
    if (log_file && log_file != stderr) {
        fclose(log_file);
    }
    pthread_mutex_unlock(&log_mutex);
}
