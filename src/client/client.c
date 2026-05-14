#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "socket_utils.h"
#include "crypto.h"

static int server_fd;
static char last_sender[32] = "";
static char last_message[1024] = "";

void* receiver_thread(void* arg) {
    (void)arg;
    char buffer[4096];
    int in_history = 0;

    while (1) {
        int received = receive_message(server_fd, buffer, sizeof(buffer));
        if (received <= 0) {
            printf("\n[CLIENT] Server disconnected\n");
            exit(0);
        }
        buffer[received] = '\0';

        char* line = strtok(buffer, "\n");
        while (line) {
            if (strlen(line) == 0) { line = strtok(NULL, "\n"); continue; }

            if (strncmp(line, "ENC:", 4) == 0) {
                char* decrypted = crypto_decrypt(line + 4);
                if (decrypted) {
                    char* from_pos = strstr(decrypted, "[");
                    if (from_pos && strncmp(from_pos, "[", 1) == 0) {
                        from_pos = strstr(decrypted, "[From ");
                        if (!from_pos) from_pos = strstr(decrypted, "[От ");
                        if (!from_pos) from_pos = strstr(decrypted, "[");
                    }
                    if (from_pos && (strncmp(from_pos, "[From ", 6) == 0 || strncmp(from_pos, "[От ", 4) == 0)) {
                        int prefix_len = (from_pos[1] == 'F') ? 6 : 4;
                        char* sender_start = from_pos + prefix_len;
                        char* sender_end = strchr(sender_start, ']');
                        if (sender_end) {
                            int name_len = sender_end - sender_start;
                            if (name_len > 31) name_len = 31;
                            memcpy(last_sender, sender_start, name_len);
                            last_sender[name_len] = '\0';
                            int start = 0;
                            while (start < name_len &&
                                !((last_sender[start] >= 'a' && last_sender[start] <= 'z') ||
                                    (last_sender[start] >= 'A' && last_sender[start] <= 'Z') ||
                                    (last_sender[start] >= '0' && last_sender[start] <= '9'))) {
                                start++;
                            }
                            if (start > 0 && start < name_len) {
                                memmove(last_sender, last_sender + start, name_len - start + 1);
                            }
                            else if (start >= name_len) {
                                last_sender[0] = '\0';
                            }
                            char* msg_start = sender_end + 1;
                            if (*msg_start == ' ') msg_start++;
                            strncpy(last_message, msg_start, sizeof(last_message) - 1);
                            last_message[sizeof(last_message) - 1] = '\0';
                        }
                    }
                    printf("\r\033[K%s\n> ", decrypted);
                    free(decrypted);
                }
                line = strtok(NULL, "\n");
                continue;
            }

            /* Уведомление о выключении сервера */
            if (strncmp(line, "SERVER_SHUTDOWN", 15) == 0) {
                printf("\r\033[K[Сервер] %s\n", line);
                fflush(stdout);
                exit(0);
            }

            if (strcmp(line, "HISTORY_BEGIN") == 0) {
                in_history = 1;
                printf("\n=== История переписки ===\n");
            }
            else if (strcmp(line, "HISTORY_END") == 0) {
                in_history = 0;
                printf("=== Конец истории ===\n> ");
                fflush(stdout);
            }
            else if (strcmp(line, "HISTORY_EMPTY") == 0) {
                printf("\n(История пуста)\n> ");
                fflush(stdout);
            }
            if (strcmp(line, "OFFLINE_BEGIN") == 0) {
                in_history = 1;
                printf("\n=== Пропущенные сообщения ===\n");
            }
            else if (strcmp(line, "OFFLINE_END") == 0) {
                in_history = 0;
                printf("=== Конец ===\n> ");
                fflush(stdout);
            }
            else if (in_history) {
                printf("%s\n", line);
            }
            else if (strncmp(line, "LIST ", 5) == 0) {
                printf("\r\033[KOнлайн: %s\n> ", line + 5);
            }
            else if (strncmp(line, "OK", 2) == 0 || strncmp(line, "GROUP_CREATED", 13) == 0 || strncmp(line, "GROUP_JOINED", 12) == 0) {
                printf("\r\033[K[OK] %s\n> ", line);
            }
            else if (strncmp(line, "ERROR", 5) == 0) {
                printf("\r\033[K[Oшибка] %s\n> ", line + 6);
            }
            else if (strncmp(line, "UNKNOWN", 7) == 0) {
                /* Игнорируем */
            }
            else {
                printf("\r\033[K[Сервер] %s\n> ", line);
            }
            fflush(stdout);
            line = strtok(NULL, "\n");
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    printf("[CLIENT] Connecting...\n");
    const char* server_ip = "127.0.0.1";
    if (argc > 1) server_ip = argv[1];
    server_fd = connect_to_server(server_ip, 7777);
    if (server_fd < 0) {
        fprintf(stderr, "[CLIENT] Failed to connect\n");
        return 1;
    }

    crypto_init("messenger2026key");

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

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receiver_thread, NULL);

    printf("\nКоманды: /msg Имя Текст | /list | /quit | /help | /history\n");
    printf("  /group create Имя Пароль | /group join Имя Пароль | /group msg Имя Текст\n");
    printf("  /reply Текст | /forward Имя\n");

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
        else if (strncmp(input, "/reply ", 7) == 0) {
            if (strlen(last_sender) == 0) {
                printf("Нет сообщений для ответа.\n");
            }
            else {
                char* text = input + 7;
                char* enc_text = crypto_encrypt(text);
                if (enc_text) {
                    char send_cmd[1280];
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:SEND %s %s\n", last_sender, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        else if (strncmp(input, "/thread ", 8) == 0) {
            char* rest = input + 8;
            char* space = strchr(rest, ' ');
            if (space) {
                *space = '\0';
                char* recipient = rest;
                char* text = space + 1;
                char* enc_text = crypto_encrypt(text);
                if (enc_text) {
                    char send_cmd[1280];
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:THREAD %s %s\n", recipient, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        else if (strncmp(input, "/forward ", 9) == 0) {
            if (strlen(last_message) == 0) {
                printf("Нет сообщений для пересылки.\n");
            }
            else {
                char* recipient = input + 9;
                char fwd[1280];
                snprintf(fwd, sizeof(fwd), "[Переслано от %s] %s", last_sender, last_message);
                char* enc_text = crypto_encrypt(fwd);
                if (enc_text) {
                    char send_cmd[2560];
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:SEND %s %s\n", recipient, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        else if (strcmp(input, "/help") == 0) {
            printf("Команды:\n");
            printf("  /msg Имя Текст              — отправить личное сообщение\n");
            printf("  /reply Текст                — ответить на последнее сообщение\n");
            printf("  /forward Имя                — переслать последнее сообщение\n");
            printf("  /list                       — список онлайн-пользователей\n");
            printf("  /quit                       — выйти\n");
            printf("  /help                       — эта справка\n");
            printf("  /history                    — показать историю переписки\n");
            printf("  /group create Имя Пароль    — создать группу с паролем\n");
            printf("  /group join Имя Пароль      — войти в группу\n");
            printf("  /group msg Имя Текст        — сообщение в группу\n");
            printf("  /register Имя Пароль        — зарегистрироваться (до входа)\n");
            printf("  /thread ID Имя Текст        — ответить в ветку сообщения\n");
        }
        else if (strcmp(input, "/history") == 0) {
            send_message(server_fd, "HISTORY\n");
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