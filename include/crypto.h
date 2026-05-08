#ifndef CRYPTO_H
#define CRYPTO_H

/* Инициализировать шифрование (установить ключ) */
int crypto_init(const char* key);

/* Зашифровать текст (возвращает hex-строку, нужно освободить через free) */
char* crypto_encrypt(const char* plaintext);

/* Расшифровать hex-строку в текст (нужно освободить через free) */
char* crypto_decrypt(const char* hex_ciphertext);

#endif