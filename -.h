#ifndef HISTORY_H
#define HISTORY_H

/* Инициализировать базу данных */
int history_init(const char* db_filename);

/* Сохранить сообщение */
void history_save(const char* sender, const char* receiver, const char* message);

/* Запросить историю между двумя пользователями */
/* Возвращает строку — нужно будет освободить через free() */
char* history_get(const char* user1, const char* user2);

/* Закрыть базу данных */
void history_close(void);

#endif