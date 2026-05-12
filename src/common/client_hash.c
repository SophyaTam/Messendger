#include "client_hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ClientNode* hash_table[HASH_SIZE];

/* Хеш-функция (djb2) */
static unsigned int hash_func(const char* str) {
    unsigned int h = 5381;
    if (!str || !*str) return 0;
    while (*str) h = ((h << 5) + h) + *str++;
    return h % HASH_SIZE;
}

void hash_init(void) {
    memset(hash_table, 0, sizeof(hash_table));
    printf("[HASH] Hash table initialized (size=%d)\n", HASH_SIZE);
}

ClientNode* hash_add(const char* username, int fd, int logged_in) {
    ClientNode* node = malloc(sizeof(ClientNode));
    if (!node) return NULL;
    node->fd = fd;
    node->logged_in = logged_in;
    node->username[0] = '\0';
    if (username) strncpy(node->username, username, sizeof(node->username) - 1);

    unsigned int idx = hash_func(username && username[0] ? username : NULL);
    node->next = hash_table[idx];
    hash_table[idx] = node;
    return node;
}

ClientNode* hash_find_by_fd(int fd) {
    for (int i = 0; i < HASH_SIZE; i++) {
        for (ClientNode* n = hash_table[i]; n; n = n->next)
            if (n->fd == fd) return n;
    }
    return NULL;
}

int hash_find_fd_by_name(const char* username) {
    if (!username || !username[0]) return -1;
    unsigned int idx = hash_func(username);
    for (ClientNode* n = hash_table[idx]; n; n = n->next)
        if (strcmp(n->username, username) == 0 && n->logged_in)
            return n->fd;
    return -1;
}

void hash_remove(int fd) {
    for (int i = 0; i < HASH_SIZE; i++) {
        ClientNode** prev = &hash_table[i];
        for (ClientNode* n = hash_table[i]; n; n = n->next) {
            if (n->fd == fd) {
                *prev = n->next;
                free(n);
                return;
            }
            prev = &n->next;
        }
    }
}

char* hash_get_online_list(void) {
    char* result = malloc(512);
    if (!result) return NULL;
    result[0] = '\0';
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

void hash_update(int fd, const char* username, int logged_in) {
    /* Удалить из старой корзины */
    for (int i = 0; i < HASH_SIZE; i++) {
        ClientNode** prev = &hash_table[i];
        for (ClientNode* n = hash_table[i]; n; n = n->next) {
            if (n->fd == fd) {
                *prev = n->next;
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

int hash_count(void) {
    int count = 0;
    for (int i = 0; i < HASH_SIZE; i++)
        for (ClientNode* n = hash_table[i]; n; n = n->next)
            if (n->logged_in) count++;
    return count;
}

void hash_cleanup(void) {
    for (int i = 0; i < HASH_SIZE; i++) {
        ClientNode* n = hash_table[i];
        while (n) {
            ClientNode* tmp = n;
            n = n->next;
            free(tmp);
        }
        hash_table[i] = NULL;
    }
}