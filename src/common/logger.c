#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

static FILE* log_file = NULL;  // Указатель на файл лога (открывается один раз при старте сервера)

// Инициализация: открыть файл лога в режиме append (дописывать в конец)
int logger_init(const char* filename) {
    log_file = fopen(filename, "a");  // "a" — все записи добавляются в конец файла
    if (!log_file) {
        perror("fopen");
        return -1;
    }
    logger_write("LOGGER INITIALIZED");  // Первая запись в лог
    return 0;
}

// Записать сообщение в лог с временной меткой
void logger_write(const char* format, ...) {
    if (!log_file) return;    // Если файл не открыт — молча выходим

    // Получаем текущее время и форматируем в строку
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    // Пишем временную метку
    fprintf(log_file, "[%s] ", time_str);

    // Пишем само сообщение (через va_list для поддержки формата)
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    // Завершаем строку и сбрасываем буфер на диск (гарантия сохранности)
    fprintf(log_file, "\n");
    fflush(log_file);  // Гарантия записи на диск 
}

// Закрыть файл лога
void logger_close(void) {
    if (log_file) {
        logger_write("LOGGER CLOSED");  // Последняя запись перед закрытием
        fclose(log_file);
        log_file = NULL;
    }
}