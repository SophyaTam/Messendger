#include "crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

static unsigned char key[16];  // AES-128 ключ (16 байт) 
static unsigned char iv[16];   // Вектор инициализации 
static int initialized = 0;      // Флаг: инициализирован ли модуль

// Инициализация шифрования
// key_str — строка-ключ (до 16 символов), если NULL — генерируется случайный
int crypto_init(const char* key_str) {
    // Если ключ передан — используем его 
    if (key_str && strlen(key_str) > 0) {
        // Преобразуем строку в 16-байтный ключ 
        memset(key, 0, 16);
        strncpy((char*)key, key_str, 16);
        // Генерируем IV из ключа (упрощённо) 
        memcpy(iv, key, 16);
    }
    else {
        // Генерируем криптографически стойкий случайный ключ
        if (!RAND_bytes(key, sizeof(key))) {
            fprintf(stderr, "[CRYPTO] Failed to generate key\n");
            return -1;
        }
        memcpy(iv, key, 16);
    }

    initialized = 1;
    printf("[CRYPTO] Encryption initialized\n");
    return 0;
}

// Зашифровать текст (AES-128-CBC)
// plaintext — исходный текст, возвращает hex-строку шифротекста (нужно free())
char* crypto_encrypt(const char* plaintext) {
    if (!initialized) return NULL;

    // Создаём контекст шифрования OpenSSL
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return NULL;

    // Выделяем память под шифротекст (исходный + один блок 16 байт)
    int len = strlen(plaintext);
    int ciphertext_len = len + 16;
    unsigned char* ciphertext = malloc(ciphertext_len);
    if (!ciphertext) {
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    // Шифрование в три шага: Init → Update → Final
    int out_len;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);  // Шаг 1: инициализация
    EVP_EncryptUpdate(ctx, ciphertext, &out_len,            // Шаг 2: шифруем данные
        (unsigned char*)plaintext, len);
    int total_len = out_len;
    EVP_EncryptFinal_ex(ctx, ciphertext + total_len, &out_len);  // Шаг 3: финализация
    total_len += out_len;
    EVP_CIPHER_CTX_free(ctx);       // Освобождаем контекст

    // Преобразуем бинарный шифротекст в hex-строку (для передачи текстом)
    char* hex = malloc(total_len * 2 + 1);       // Каждый байт → 2 символа hex
    if (!hex) {
        free(ciphertext);
        return NULL;
    }
    for (int i = 0; i < total_len; i++) {
        sprintf(hex + i * 2, "%02x", ciphertext[i]);
    }
    hex[total_len * 2] = '\0';

    free(ciphertext);
    return hex;
}

// Расшифровать hex-строку в текст (AES-128-CBC) hex_ciphertext — hex-строка, возвращает исходный текст (нужно free())
char* crypto_decrypt(const char* hex_ciphertext) {
    if (!initialized) return NULL;

    // Hex-строка → бинарные данные
    int hex_len = strlen(hex_ciphertext);
    int ciphertext_len = hex_len / 2;  // Два hex-символа = 1 байт
    unsigned char* ciphertext = malloc(ciphertext_len);
    if (!ciphertext) return NULL;

    // Hex-строку → бинарные данные
    for (int i = 0; i < ciphertext_len; i++) {
        sscanf(hex_ciphertext + i * 2, "%2hhx", &ciphertext[i]);
    }

    // Создаём контекст расшифровки
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        free(ciphertext);
        return NULL;
    }

    // Память под расшифрованный текст
    unsigned char* plaintext = malloc(ciphertext_len + 1);
    if (!plaintext) {
        free(ciphertext);
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    // Расшифровка в три шага: Init → Update → Final
    int out_len;
    EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv); //Init
    EVP_DecryptUpdate(ctx, plaintext, &out_len, ciphertext, ciphertext_len); //Update
    int total_len = out_len;
    EVP_DecryptFinal_ex(ctx, plaintext + total_len, &out_len); //Final
    total_len += out_len;
    plaintext[total_len] = '\0';   // Завершаем строку

    EVP_CIPHER_CTX_free(ctx);
    free(ciphertext);

    return (char*)plaintext;
}