#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

static FILE* log_file = NULL;

int logger_init(const char* filename) {
    log_file = fopen(filename, "a");
    if (!log_file) {
        perror("fopen");
        return -1;
    }
    logger_write("LOGGER INITIALIZED");
    return 0;
}

void logger_write(const char* format, ...) {
    if (!log_file) return;

    /* Временная метка */
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    fprintf(log_file, "[%s] ", time_str);

    /* Сообщение */
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);  /* Гарантия записи на диск */
}

void logger_close(void) {
    if (log_file) {
        logger_write("LOGGER CLOSED");
        fclose(log_file);
        log_file = NULL;
    }
}