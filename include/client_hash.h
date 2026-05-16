#ifndef CLIENT_HASH_H
#define CLIENT_HASH_H

#define HASH_SIZE 256

/** Узел хеш-таблицы */
typedef struct ClientNode {
    char username[32];
    int fd;
    int logged_in;
    struct ClientNode* next;
} ClientNode;

//Инициализировать хеш-таблицу
void hash_init(void);

// Добавить клиента в таблицу 
ClientNode* hash_add(const char* username, int fd, int logged_in);

//Найти клиента по fd

ClientNode* hash_find_by_fd(int fd);

// Найти fd онлайн-клиента по имени

int hash_find_fd_by_name(const char* username);

//Удалить клиента из таблицы по fd

void hash_remove(int fd);

//Получить список онлайн-пользователей
char* hash_get_online_list(void);

//Обновить статус клиента (имя, logged_in)

void hash_update(int fd, const char* username, int logged_in);

//Получить количество онлайн-клиентов
int hash_count(void);

//Очистить таблицу
void hash_cleanup(void);

#endif