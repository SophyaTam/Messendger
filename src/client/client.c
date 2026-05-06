#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "socket_utils.h"

static int server_fd;  /* Сокет сервера (общий для обоих потоков) */

void* receiver_thread(void* arg) {
    (void)arg;
    char buffer[1024];
    int in_history = 0;

    while (1) {
        int received = receive_message(server_fd, buffer, sizeof(buffer));
        if (received <= 0) {
            printf("\n[CLIENT] Server disconnected\n");
            exit(0);
        }
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strlen(buffer) == 0) continue;

        if (strcmp(buffer, "HISTORY_BEGIN") == 0) {
            in_history = 1;
            printf("\n=== История переписки ===\n");
        }
        else if (strcmp(buffer, "HISTORY_END") == 0) {
            in_history = 0;
            printf("=== Конец истории ===\n> ");
            fflush(stdout);
        }
        else if (strcmp(buffer, "HISTORY_EMPTY") == 0) {
            printf("\n(История пуста)\n> ");
            fflush(stdout);
        }
        else if (in_history) {
            printf("%s\n", buffer);
        }
        else if (strncmp(buffer, "[От ", 4) == 0) {
            printf("\r\033[K%s\n> ", buffer);
            fflush(stdout);
        }
        else if (strncmp(buffer, "LIST ", 5) == 0) {
            printf("\r\033[KОнлайн: %s\n> ", buffer + 5);
            fflush(stdout);
        }
        else if (strncmp(buffer, "OK", 2) == 0) {
            printf("\r\033[K[OK]\n> ");
            fflush(stdout);
        }
        else if (strncmp(buffer, "ERROR", 5) == 0) {
            printf("\r\033[K[Ошибка] %s\n> ", buffer + 6);
            fflush(stdout);
        }
        else {
            printf("\r\033[K[Сервер] %s\n> ", buffer);
            fflush(stdout);
        }
    }
    return NULL;
}
int main() {
    printf("[CLIENT] Connecting...\n");
    server_fd = connect_to_server("127.0.0.1", 7777);
    if (server_fd < 0) {
        fprintf(stderr, "[CLIENT] Failed to connect\n");
        return 1;
    }

    /* --- Аутентификация --- */
    char username[32], password[32];
    printf("Login: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';

    printf("Password: ");
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = '\0';

    char command[128];
    snprintf(command, sizeof(command), "LOGIN %s %s\n", username, password);
    send_message(server_fd, command);

    char response[256];
    int received = receive_message(server_fd, response, sizeof(response));
    if (received > 0) {
        response[strcspn(response, "\n")] = '\0';
        printf("[Сервер] %s\n", response);
        if (strncmp(response, "OK", 2) != 0) {
            close_socket(server_fd);
            return 1;
        }
    }

    /* --- Запуск потока-приёмника --- */
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receiver_thread, NULL);

    /* --- Основной цикл: ввод команд --- */
    printf("\nКоманды: /msg Имя Текст | /list | /quit | /help\n");
    char input[1024];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0) continue;

        /* /quit или /exit */
        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            send_message(server_fd, "EXIT\n");
            break;
        }
        /* /list */
        else if (strcmp(input, "/list") == 0) {
            send_message(server_fd, "LIST\n");
        }
        /* /msg user text */
        else if (strncmp(input, "/msg ", 5) == 0) {
            /* Отправляем как SEND user text */
            char send_cmd[1280];
            snprintf(send_cmd, sizeof(send_cmd), "SEND %s\n", input + 5);
            send_message(server_fd, send_cmd);
        }
        /* /help */
        else if (strcmp(input, "/help") == 0) {
            printf("Команды:\n");
            printf("  /msg Имя Текст — отправить личное сообщение\n");
            printf("  /list           — список онлайн-пользователей\n");
            printf("  /quit           — выйти\n");
            printf("  /help           — эта справка\n");
            printf("  /history Имя    — показать историю переписки\n");
        }
        /* /history user */
        else if (strncmp(input, "/history ", 9) == 0) {
            char hist_cmd[64];
            snprintf(hist_cmd, sizeof(hist_cmd), "HISTORY %s\n", input + 9);
            send_message(server_fd, hist_cmd);
        }
        else {
            printf("Неизвестная команда. /help для справки.\n");
        }
    }

    close_socket(server_fd);
    printf("[CLIENT] Done.\n");
    return 0;
}