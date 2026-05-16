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

//Получить указатель на базу данных для других модулей (group)
sqlite3* history_get_db(void);

//Записать событие в базу (вход, выход, старт/стоп сервера)
void history_log_event(const char* type, const char* username, const char* data);

/* Получить и отметить доставленными офлайн-сообщения для пользователя */
void history_save_offline(const char* sender, const char* receiver, const char* message);

//Сохранить сообщение в ветку
void history_save_thread(const char* sender, const char* receiver, const char* message, int parent_id);

// Получить и отметить как доставленные офлайн-сообщения
char* history_get_offline(const char* username);
#endif