#ifndef AUTH_H
#define AUTH_H
/**
 * Загрузить базу пользователей из файла passwd
 * @param passwd_file Путь к файлу с логинами и паролями (формат: login:password)
 * @return 0 при успехе, -1 при ошибке
 */
int auth_init(const char *passwd_file);
/**
 * Проверить логин и пароль пользователя
 * @param username Логин
 * @param password Пароль (открытым текстом, хешируется внутри)
 * @return 1 - успех, 0 - неверный пароль, -1 - пользователь не найден
 */
int auth_check(const char *username, const char *password);
/**
 * Вычислить SHA-256 хеш строки
 * @param input Исходная строка
 * @param output_hex Буфер (минимум 65 байт) для hex-строки хеша
 */
void sha256_hash(const char *input, char *output_hex);
// свободить память, занятую модулем аутентификации
void auth_cleanup(void);
//Зарегистрировать нового пользователя (добавляет в файл passwd) 
int auth_register(const char* username, const char* password);

#endif
