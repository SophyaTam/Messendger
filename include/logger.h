#ifndef LOGGER_H
#define LOGGER_H

/* Инициализировать лог-файл */
int logger_init(const char* filename);

/* Записать сообщение в лог */
void logger_write(const char* format, ...);

/* Закрыть лог-файл */
void logger_close(void);

#endif