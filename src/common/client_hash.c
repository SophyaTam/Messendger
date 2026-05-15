#include "client_hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ClientNode* hash_table[HASH_SIZE];  // Хеш-таблица: массив указателей на головы связных списков

// Хеш-функция djb2 — равномерно распределяет строки по 256 корзинам возвращает индекс от 0 до HASH_SIZE-1
static unsigned int hash_func(const char* str) {
    unsigned int h = 5381;
    if (!str || !*str) return 0;  // Пустая строка → корзина 0
    while (*str) h = ((h << 5) + h) + *str++;   // h = h * 33 + символ
    return h % HASH_SIZE;   // Остаток от деления на размер таблицы
}

// Инициализация хеш-таблицы (заполняем нулями)
void hash_init(void) {
    memset(hash_table, 0, sizeof(hash_table));
    printf("[HASH] Hash table initialized (size=%d)\n", HASH_SIZE);
}

// Добавить клиента в хеш-таблицу
// username — имя (может быть пустым), fd — сокет, logged_in — статус авторизации
ClientNode* hash_add(const char* username, int fd, int logged_in) {
    ClientNode* node = malloc(sizeof(ClientNode));   // Создаём новый узел
    if (!node) return NULL;
    node->fd = fd;
    node->logged_in = logged_in;
    node->username[0] = '\0';
    if (username) strncpy(node->username, username, sizeof(node->username) - 1);

    // Вставляем в начало цепочки (быстрее, чем в конец)
    unsigned int idx = hash_func(username && username[0] ? username : NULL);
    node->next = hash_table[idx];
    hash_table[idx] = node;
    return node;
}

// Найти клиента по файловому дескриптору (полный перебор всех корзин)
// Используется при отключении клиента
ClientNode* hash_find_by_fd(int fd) {
    for (int i = 0; i < HASH_SIZE; i++) {
        for (ClientNode* n = hash_table[i]; n; n = n->next)
            if (n->fd == fd) return n;
    }
    return NULL;
}

// Найти fd онлайн-клиента по имени (использует хеш, быстро) возвращает fd или -1 если не найден
int hash_find_fd_by_name(const char* username) {
    if (!username || !username[0]) return -1;
    unsigned int idx = hash_func(username);  // Вычисляем корзину
    // Ищем в цепочке только этой корзины (а не во всей таблице)
    for (ClientNode* n = hash_table[idx]; n; n = n->next)
        if (strcmp(n->username, username) == 0 && n->logged_in)
            return n->fd;
    return -1;
}

// Удалить клиента из таблицы по fd
void hash_remove(int fd) {
    for (int i = 0; i < HASH_SIZE; i++) {
        ClientNode** prev = &hash_table[i];         // Указатель на указатель
        for (ClientNode* n = hash_table[i]; n; n = n->next) {
            if (n->fd == fd) {
                *prev = n->next;            // Вырезаем узел из цепочки
                free(n);                    // Освобождаем память
                return;
            }
            prev = &n->next;                // Идём дальше
        }
    }
}

// Получить список всех онлайн-пользователей (строка через пробел)
// Возвращает строку, которую нужно освободить через free()
char* hash_get_online_list(void) {
    char* result = malloc(512);
    if (!result) return NULL;
    result[0] = '\0';
    // Обходим всю таблицу, собираем имена авторизованных пользователей
    for (int i = 0; i < HASH_SIZE; i++) {
        for (ClientNode* n = hash_table[i]; n; n = n->next) {
            if (n->logged_in && n->username[0]) {
                if (strlen(result) > 0) strcat(result, " ");
                strcat(result, n->username);
            }
        }
    }
    return result;
}

// Обновить данные клиента (имя и/или статус авторизации)
// При смене имени узел перемещается в другую корзину
void hash_update(int fd, const char* username, int logged_in) {
    /* Удалить из старой корзины */
    for (int i = 0; i < HASH_SIZE; i++) {
        ClientNode** prev = &hash_table[i];
        for (ClientNode* n = hash_table[i]; n; n = n->next) {
            if (n->fd == fd) {
                *prev = n->next;         // Вырезаем из цепочки
                n->next = NULL;
                /* Обновить данные */
                n->logged_in = logged_in;
                if (username) strncpy(n->username, username, sizeof(n->username) - 1);
                /* Добавить в правильную корзину */
                unsigned int idx = hash_func(username && username[0] ? username : NULL);
                n->next = hash_table[idx];
                hash_table[idx] = n;
                return;
            }
            prev = &n->next;
        }
    }
}

// Посчитать количество онлайн-клиентов
int hash_count(void) {
    int count = 0;
    for (int i = 0; i < HASH_SIZE; i++)
        for (ClientNode* n = hash_table[i]; n; n = n->next)
            if (n->logged_in) count++;
    return count;
}

// Очистить хеш-таблицу и освободить всю память
void hash_cleanup(void) {
    for (int i = 0; i < HASH_SIZE; i++) {
        ClientNode* n = hash_table[i];
        while (n) {
            ClientNode* tmp = n;
            n = n->next;
            free(tmp);              // Освобождаем каждый узел
        }
        hash_table[i] = NULL;
    }
}