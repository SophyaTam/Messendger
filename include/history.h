#ifndef HISTORY_H
#define HISTORY_H

#include <sqlite3.h>

/* Инициализировать хранилище истории */
int history_init(const char* filename);

/* Сохранить сообщение */
void history_save(const char* sender, const char* receiver, const char* message);

/* Запросить историю между двумя пользователями */
char* history_get(const char* user1, const char* user2);

/* Закрыть хранилище */
void history_close(void);

sqlite3* history_get_db(void);

void history_log_event(const char* type, const char* username, const char* data);
#endif