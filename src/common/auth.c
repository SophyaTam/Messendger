#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>

/* Структура для хранения одного пользователя */
typedef struct {
    char username[32];
    char hash[65];  /* SHA-256 в hex = 64 символа + '\0' */
} User;

static User* users = NULL;
static int user_count = 0;

/* SHA-256: строка -> hex-строка (64 символа) */
void sha256_hash(const char* input, char* output_hex) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input, strlen(input), hash);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output_hex + (i * 2), "%02x", hash[i]);
    }
    output_hex[64] = '\0';
}

/* Загрузить passwd в память */
int auth_init(const char* passwd_file) {
    FILE* f = fopen(passwd_file, "r");
    if (!f) {
        printf("[AUTH] Cannot open passwd file: %s\n", passwd_file);
        return -1;
    }

    /* Сначала посчитаем строки */
    user_count = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strlen(line) > 1) user_count++;
    }
    rewind(f);

    /* Выделим память */
    users = malloc(sizeof(User) * user_count);
    if (!users) {
        fclose(f);
        return -1;
    }

    /* Читаем */
    int i = 0;
    while (fgets(line, sizeof(line), f) && i < user_count) {
        /* Убрать \n в конце */
        line[strcspn(line, "\n")] = '\0';

        /* Формат: username:password */
        char* colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        strncpy(users[i].username, line, sizeof(users[i].username) - 1);

        /* Хешируем пароль и сохраняем хеш */
        sha256_hash(colon + 1, users[i].hash);

        i++;
    }

    fclose(f);
    printf("[AUTH] Loaded %d users from %s\n", user_count, passwd_file);
    return 0;
}

/* Проверить логин/пароль */
int auth_check(const char* username, const char* password) {
    /* Вычислить хеш введённого пароля */
    char hash[65];
    sha256_hash(password, hash);

    /* Искать пользователя */
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            /* Нашли — сверяем хеш */
            if (strcmp(users[i].hash, hash) == 0) {
                return 1;  /* Успех */
            }
            else {
                return 0;  /* Неверный пароль */
            }
        }
    }

    return -1;  /* Пользователь не найден */
}

void auth_cleanup(void) {
    if (users) {
        free(users);
        users = NULL;
        user_count = 0;
    }
}