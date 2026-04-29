#ifndef AUTH_H
#define AUTH_H

/* Загрузить базу пользователей из файла passwd */
int auth_init(const char* passwd_file);

/* Проверить логин и пароль */
/* Возвращает 1 при успехе, 0 при неверном пароле, -1 если пользователь не найден */
int auth_check(const char* username, const char* password);

/* Вычислить SHA-256 хеш строки (возвращает hex-строку) */
void sha256_hash(const char* input, char* output_hex);

/* Освободить память */
void auth_cleanup(void);

#endif