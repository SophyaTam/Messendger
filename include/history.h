#ifndef HISTORY_H
#define HISTORY_H

/* Инициализировать хранилище истории */
int history_init(const char* filename);

/* Сохранить сообщение */
void history_save(const char* sender, const char* receiver, const char* message);

/* Запросить историю между двумя пользователями */
/* Возвращает строку — нужно будет освободить через free() */
char* history_get(const char* user1, const char* user2);

/* Закрыть хранилище */
void history_close(void);

#endif