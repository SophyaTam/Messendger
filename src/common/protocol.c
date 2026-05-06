#include "protocol.h"
#include <stdio.h>
#include <string.h>

/* SEND username text... */
int protocol_parse_send(const char* buffer, char* recipient, char* message) {
    /* Пропускаем "SEND " */
    const char* p = buffer + 5;

    /* Читаем получателя (до пробела) */
    int i = 0;
    while (*p && *p != ' ' && i < 31) {
        recipient[i++] = *p++;
    }
    recipient[i] = '\0';

    if (*p != ' ') return -1;  /* Нет текста сообщения */
    p++;  /* Пропускаем пробел */

    /* Остальное — текст сообщения */
    strncpy(message, p, 1023);
    message[1023] = '\0';

    return 0;
}

void protocol_make_ok(char* out) {
    sprintf(out, "OK\n");
}

void protocol_make_error(char* out, const char* reason) {
    sprintf(out, "ERROR %s\n", reason);
}

void protocol_make_user_list(char* out, const char* users) {
    sprintf(out, "LIST %s\n", users);
}