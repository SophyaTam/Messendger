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
    /* Открываем файл для чтения */
    FILE* f = fopen("data/messages.log", "r");
    if (!f) return NULL;

    /* Выделяем буфер — пока фиксированный, позже будет динамический */
    char* result = malloc(4096);
    if (!result) {
        fclose(f);
        return NULL;
    }
    result[0] = '\0';

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Формат: время | отправитель | получатель | сообщение */
        char time_str[64], sender[32], receiver[32], message[900];
        if (sscanf(line, "%63[^|] | %31[^|] | %31[^|] | %899[^\n]",
            time_str, sender, receiver, message) == 4) {

            /* Убираем пробелы по краям */
            char* s = sender;
            while (*s == ' ') s++;
            char* e = s + strlen(s) - 1;
            while (e > s && *e == ' ') { *e = '\0'; e--; }

            char* r = receiver;
            while (*r == ' ') r++;
            e = r + strlen(r) - 1;
            while (e > r && *e == ' ') { *e = '\0'; e--; }

            /* Проверяем: сообщение между user1 и user2? */
            if ((strcmp(s, user1) == 0 && strcmp(r, user2) == 0) ||
                (strcmp(s, user2) == 0 && strcmp(r, user1) == 0)) {

                /* Добавляем к результату */
                if (strlen(result) + strlen(line) < 4000) {
                    strcat(result, "[");
                    strcat(result, time_str);
                    strcat(result, "] ");
                    strcat(result, s);
                    strcat(result, ": ");
                    strcat(result, message);
                    strcat(result, "\n");
                }
            }
        }
    }
    printf("[HISTORY DEBUG] Result length: %zu\n", strlen(result));
    printf("[HISTORY DEBUG] Result: '%s'\n", result);

    fclose(f);
    return result;
}

void history_close(void) {
    if (history_file) {
        fclose(history_file);
        history_file = NULL;
    }
}
