#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "socket_utils.h"
#include "crypto.h"

static int server_fd;

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

        if (strncmp(buffer, "ENC:", 4) == 0) {
            char* decrypted = crypto_decrypt(buffer + 4);
            if (decrypted) {
                strncpy(buffer, decrypted, sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';
                free(decrypted);
            }
        }

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
        else if (strncmp(buffer, "[От ", 4) == 0 || strncmp(buffer, "[Группа ", 8) == 0) {
            printf("\r\033[K%s\n> ", buffer);
            fflush(stdout);
        }
        else if (strncmp(buffer, "LIST ", 5) == 0) {
            printf("\r\033[KОнлайн: %s\n> ", buffer + 5);
            fflush(stdout);
        }
        else if (strncmp(buffer, "OK", 2) == 0 || strncmp(buffer, "GROUP_CREATED", 13) == 0 || strncmp(buffer, "GROUP_JOINED", 12) == 0) {
            printf("\r\033[K[OK] %s\n> ", buffer);
            fflush(stdout);
        }
        else if (strncmp(buffer, "ERROR", 5) == 0) {
            printf("\r\033[K[Ошибка] %s\n> ", buffer + 6);
            fflush(stdout);
        }
        else if (strncmp(buffer, "UNKNOWN", 7) == 0) {
            /* Игнорируем UNKNOWN */
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

    crypto_init("messenger2026key");

    /* --- Аутентификация или регистрация --- */
    char username[32], password[32];
    int logged_in = 0;

    while (!logged_in) {
        printf("Login (/register для создания аккаунта): ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = '\0';

        if (strncmp(username, "/register ", 10) == 0) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "REGISTER %s\n", username + 10);
            send_message(server_fd, cmd);
            char resp[256];
            int r = receive_message(server_fd, resp, sizeof(resp));
            if (r > 0) {
                resp[strcspn(resp, "\n")] = '\0';
                printf("[Сервер] %s\n", resp);
            }
            continue;
        }

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
            if (strncmp(response, "OK", 2) == 0) {
                logged_in = 1;
            }
        }
    }

    /* --- Запуск потока-приёмника --- */
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receiver_thread, NULL);

    printf("\nКоманды: /msg Имя Текст | /list | /quit | /help | /history Имя\n");
    printf("  /group create Имя Пароль | /group join Имя Пароль | /group msg Имя Текст\n");

    char input[1024];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';
        if (strlen(input) == 0) continue;

        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            send_message(server_fd, "EXIT\n");
            break;
        }
        else if (strcmp(input, "/list") == 0) {
            send_message(server_fd, "LIST\n");
        }
        else if (strncmp(input, "/msg ", 5) == 0) {
            char* rest = input + 5;
            char* space = strchr(rest, ' ');
            if (space) {
                *space = '\0';
                char* recipient = rest;
                char* text = space + 1;
                char* enc_text = crypto_encrypt(text);
                if (enc_text) {
                    char send_cmd[1280];
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:SEND %s %s\n", recipient, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        else if (strcmp(input, "/help") == 0) {
            printf("Команды:\n");
            printf("  /msg Имя Текст              — отправить личное сообщение\n");
            printf("  /list                        — список онлайн-пользователей\n");
            printf("  /quit                        — выйти\n");
            printf("  /help                        — эта справка\n");
            printf("  /history Имя                 — показать историю переписки\n");
            printf("  /group create Имя Пароль     — создать группу с паролем\n");
            printf("  /group join Имя Пароль       — войти в группу\n");
            printf("  /group msg Имя Текст         — сообщение в группу\n");
            printf("  /register Имя Пароль         — зарегистрироваться (до входа)\n");
        }
        else if (strncmp(input, "/history ", 9) == 0) {
            char hist_cmd[64];
            snprintf(hist_cmd, sizeof(hist_cmd), "HISTORY %s\n", input + 9);
            send_message(server_fd, hist_cmd);
        }
        else if (strncmp(input, "/group create ", 14) == 0) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "GROUP_CREATE %s\n", input + 14);
            send_message(server_fd, cmd);
        }
        else if (strncmp(input, "/group join ", 12) == 0) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "GROUP_JOIN %s\n", input + 12);
            send_message(server_fd, cmd);
        }
        else if (strncmp(input, "/group msg ", 11) == 0) {
            char* rest = input + 11;
            char* space = strchr(rest, ' ');
            if (space) {
                *space = '\0';
                char* group_name = rest;
                char* text = space + 1;
                char* enc_text = crypto_encrypt(text);
                if (enc_text) {
                    char send_cmd[1280];
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:GROUP_MSG %s %s\n", group_name, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        else {
            printf("Неизвестная команда. /help для справки.\n");
        }
    }

    close_socket(server_fd);
    printf("[CLIENT] Done.\n");
    return 0;
}