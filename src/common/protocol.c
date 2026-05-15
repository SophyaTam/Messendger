#include "protocol.h"
#include <stdio.h>
#include <string.h>

// Разобрать команду SEND: извлечь получателя и текст сообщения
int protocol_parse_send(const char* buffer, char* recipient, char* message) {
   
    const char* p = buffer + 5; // Пропускаем префикс "SEND " (5 символов)

    
    int i = 0;      // Читаем имя получателя (символы до первого пробела, максимум 31)
    while (*p && *p != ' ' && i < 31) {
        recipient[i++] = *p++;
    }
    recipient[i] = '\0';        // Завершаем строку получателя

    if (*p != ' ') return -1;  // Нет пробела — нет текста сообщения
    p++;  // Пропускаем пробел

    // Всё оставшееся — текст сообщения (максимум 1023 символа)
    strncpy(message, p, 1023);
    message[1023] = '\0';

    return 0;
}

// Сформировать ответ OK
void protocol_make_ok(char* out) {
    sprintf(out, "OK\n");
}

// Сформировать ответ ERROR с причиной
void protocol_make_error(char* out, const char* reason) {
    sprintf(out, "ERROR %s\n", reason);
}

// Сформировать ответ LIST со списком пользователей
void protocol_make_user_list(char* out, const char* users) {
    sprintf(out, "LIST %s\n", users);
}