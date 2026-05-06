#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE* history_file = NULL;

int history_init(const char* filename) {
    history_file = fopen(filename, "a");
    if (!history_file) {
        perror("history_init");
        return -1;
    }
    return 0;
}

void history_save(const char* sender, const char* receiver, const char* message) {
    if (!history_file) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    /* Формат: время | отправитель | получатель | сообщение */
    fprintf(history_file, "%s | %s | %s | %s\n", time_str, sender, receiver, message);
    fflush(history_file);
}

char* history_get(const char* user1, const char* user2) {
    FILE* f = fopen("data/messages.log", "r");
    if (!f) return NULL;

    char* result = malloc(4096);
    if (!result) {
        fclose(f);
        return NULL;
    }
    result[0] = '\0';

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';

        char time_str[64], sender[32], receiver[32], message[900];

        /* Ручной парсинг вместо sscanf */
        char* p = line;

        /* Читаем время (до первого |) */
        char* sep = strstr(p, " | ");
        if (!sep) continue;
        *sep = '\0';
        strncpy(time_str, p, sizeof(time_str) - 1);
        p = sep + 3;
        while (*p == ' ') p++;

        /* Читаем отправителя (до второго |) */
        sep = strstr(p, " | ");
        if (!sep) continue;
        *sep = '\0';
        strncpy(sender, p, sizeof(sender) - 1);
        p = sep + 3;
        while (*p == ' ') p++;

        /* Читаем получателя (до третьего |) */
        sep = strstr(p, " | ");
        if (!sep) continue;
        *sep = '\0';
        strncpy(receiver, p, sizeof(receiver) - 1);
        p = sep + 3;
        while (*p == ' ') p++;

        /* Остальное — сообщение */
        strncpy(message, p, sizeof(message) - 1);

        /* Обрезаем пробелы в конце sender и receiver */
        char* e = sender + strlen(sender) - 1;
        while (e >= sender && *e == ' ') { *e = '\0'; e--; }
        e = receiver + strlen(receiver) - 1;
        while (e >= receiver && *e == ' ') { *e = '\0'; e--; }

        /* Проверяем: сообщение между user1 и user2? */
        if ((strcmp(sender, user1) == 0 && strcmp(receiver, user2) == 0) ||
            (strcmp(sender, user2) == 0 && strcmp(receiver, user1) == 0)) {

            if (strlen(result) + strlen(line) < 4000) {
                strcat(result, "[");
                strcat(result, time_str);
                strcat(result, "] ");
                strcat(result, sender);
                strcat(result, ": ");
                strcat(result, message);
                strcat(result, "\n");
            }
        }
    }
    printf("[HISTORY DEBUG] Result length: %zu\n", strlen(result));
    printf("[HISTORY DEBUG] Result: '%s'\n", result);

    fclose(f);

    fclose(f);
    return result;
}

void history_close(void) {
    if (history_file) {
        fclose(history_file);
        history_file = NULL;
    }
}