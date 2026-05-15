#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>

// Структура пользователя: имя + SHA-256 хеш пароля (64 символа hex + '\0')
typedef struct {
    char username[32];
    char hash[65];
} User;

// Массив пользователей и их количество
static User *users = NULL;
static int user_count = 0;


// Вычисление SHA-256 хеша строки  input — исходная строка, output_hex — буфер на 65 байт для hex-результата
void sha256_hash(const char *input, char *output_hex) {
    unsigned char hash[SHA256_DIGEST_LENGTH];           // 32 байта сырого хеша
    SHA256((unsigned char *)input, strlen(input), hash);        // Вычисляем SHA-256
    // Преобразуем каждый байт в два hex-символа
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output_hex + (i * 2), "%02x", hash[i]);
    }
    output_hex[64] = '\0';          // Завершаем строку
}

// Загрузка пользователей из файла passwd (формат: login:password)
int auth_init(const char *passwd_file) {
    FILE *f = fopen(passwd_file, "r");
    if (!f) {
        printf("[AUTH] Cannot open passwd file: %s\n", passwd_file);
        return -1;
    }

    // Считаем количество строк (пользователей)
    user_count = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strlen(line) > 1) user_count++;
    }
    rewind(f);      // Возвращаемся в начало файла

    // Выделяем память под массив пользователей
    users = malloc(sizeof(User) * user_count);
    if (!users) {
        fclose(f);
        return -1;
    }

    // Читаем и парсим каждую строку
    int i = 0;
    while (fgets(line, sizeof(line), f) && i < user_count) {
        line[strcspn(line, "\n")] = '\0';       // Убираем \n
        char *colon = strchr(line, ':');        // Ищем разделитель ':'
        if (!colon) continue;                   // Пропускаем битые строки
        *colon = '\0';                          // Разделяем на логин и пароль
        strncpy(users[i].username, line, sizeof(users[i].username) - 1);
        sha256_hash(colon + 1, users[i].hash);  // Хешируем пароль и сохраняем
        i++;
    }

    fclose(f);
    printf("[AUTH] Loaded %d users from %s\n", user_count, passwd_file);
    return 0;
}

// Проверка логина и пароля: 1 — успех, 0 — неверный пароль, -1 — пользователь не найден
int auth_check(const char *username, const char *password) {
    char hash[65];
    sha256_hash(password, hash);            // Хешируем введённый пароль

    // Ищем пользователя и сверяем хеши
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            if (strcmp(users[i].hash, hash) == 0) {
                return 1;
            } else {
                return 0;
            }
        }
    }
    return -1;
}

// Регистрация нового пользователя: 0 — успех, -1 — уже существует, -2 — ошибка файла
int auth_register(const char* username, const char* password) {
    // Проверяем, нет ли уже такого пользователя
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) return -1;
    }

    // Дописываем в файл passwd (пароль сохраняется открытым)
    FILE* f = fopen("data/passwd", "a");
    if (!f) return -2;

    fprintf(f, "%s:%s\n", username, password);
    fclose(f);

    // Добавляем в память (хеш пароля)
    users = realloc(users, sizeof(User) * (user_count + 1));
    strncpy(users[user_count].username, username, sizeof(users[user_count].username) - 1);
    sha256_hash(password, users[user_count].hash);
    user_count++;

    printf("[AUTH] Registered new user: %s\n", username);
    return 0;
}

// Освобождение памяти
void auth_cleanup(void) {
    if (users) {
        free(users);
        users = NULL;
        user_count = 0;
    }
}

