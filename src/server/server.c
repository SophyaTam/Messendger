#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include "socket_utils.h"
#include "logger.h"
#include "auth.h"


#define MAX_CLIENTS 10
#define PORT 7777

static int running = 1;

void handle_signal(int sig) {
    (void)sig;
    printf("\n[SERVER] Received signal, shutting down...\n");
    running = 0;
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Инициализация логера */
    if (logger_init("logs/server.log") < 0) {
        fprintf(stderr, "Failed to init logger\n");
        return 1;
    }
    logger_write("SERVER STARTED");

    /* Загрузка пользователей */
    if (auth_init("data/passwd") < 0) {
        logger_write("Failed to load passwd file");
        return 1;
    }

    /* Создание сокетов */
    int tcp_fd = create_server_socket(PORT);
    int unix_fd = create_unix_server_socket("/tmp/messenger.sock");

    if (tcp_fd < 0 || unix_fd < 0) {
        logger_write("Failed to create sockets");
        return 1;
    }

    /* Массив клиентов */
    struct {
        int fd;
        char username[32];
    } clients[MAX_CLIENTS];
    int client_count = 0;

    /* Основной цикл */
    printf("[SERVER] Entering main loop...\n");

    while (running) {
        struct pollfd fds[MAX_CLIENTS + 2];
        int nfds = 0;

        /* Серверные сокеты */
        fds[nfds].fd = tcp_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        fds[nfds].fd = unix_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        /* Клиентские сокеты */
        for (int i = 0; i < client_count; i++) {
            fds[nfds].fd = clients[i].fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            if (running) perror("poll");
            break;
        }

        /* Обработка событий */
        for (int i = 0; i < nfds && running; i++) {
            if (!(fds[i].revents & POLLIN)) continue;

            if (fds[i].fd == tcp_fd) {
                /* Новое TCP-подключение */
                int client_fd = accept_client(tcp_fd);
                if (client_fd >= 0 && client_count < MAX_CLIENTS) {
                    clients[client_count].fd = client_fd;
                    strcpy(clients[client_count].username, "unknown");
                    client_count++;
                    logger_write("Client connected via TCP (fd=%d)", client_fd);
                }
            }
            else if (fds[i].fd == unix_fd) {
                /* Новое Unix-подключение */
                int client_fd = accept_client(unix_fd);
                if (client_fd >= 0 && client_count < MAX_CLIENTS) {
                    clients[client_count].fd = client_fd;
                    strcpy(clients[client_count].username, "unknown");
                    client_count++;
                    logger_write("Client connected via Unix (fd=%d)", client_fd);
                }
            }
            else {
                /* Данные от клиента */
                char buffer[1024];
                int client_fd = fds[i].fd;
                int received = receive_message(client_fd, buffer, sizeof(buffer));

                if (received <= 0) {
                    /* Отключение клиента */
                    logger_write("Client disconnected (fd=%d)", client_fd);
                    close_socket(client_fd);
                    for (int j = 0; j < client_count; j++) {
                        if (clients[j].fd == client_fd) {
                            clients[j] = clients[client_count - 1];
                            client_count--;
                            break;
                        }
                    }
                }
                else {
                    /* Убрать \n */
                    buffer[strcspn(buffer, "\n")] = '\0';

                    printf("[SERVER] Received: %s\n", buffer);

                    /* --- Обработка команды LOGIN --- */
                    if (strncmp(buffer, "LOGIN ", 6) == 0) {
                        char username[32], password[32];
                        if (sscanf(buffer + 6, "%31s %31s", username, password) == 2) {
                            int result = auth_check(username, password);
                            if (result == 1) {
                                /* Успех — сохраняем имя */
                                for (int j = 0; j < client_count; j++) {
                                    if (clients[j].fd == client_fd) {
                                        strncpy(clients[j].username, username, sizeof(clients[j].username) - 1);
                                        break;
                                    }
                                }
                                send_message(client_fd, "OK\n");
                                logger_write("User %s logged in (fd=%d)", username, client_fd);
                            }
                            else if (result == 0) {
                                send_message(client_fd, "ERROR Wrong password\n");
                                logger_write("Failed login for %s (wrong password)", username);
                            }
                            else {
                                send_message(client_fd, "ERROR User not found\n");
                                logger_write("Failed login for %s (not found)", username);
                            }
                        }
                        else {
                            send_message(client_fd, "ERROR Invalid format. Use: LOGIN username password\n");
                        }
                    }
                    /* --- Неизвестная команда --- */
                    else {
                        send_message(client_fd, "UNKNOWN Unknown command\n");
                        logger_write("Unknown command from fd=%d: %s", client_fd, buffer);
                    }
                }
            }
        }
    }

    /* Завершение */
    logger_write("SERVER STOPPED");
    for (int i = 0; i < client_count; i++) {
        close_socket(clients[i].fd);
    }
    close_socket(tcp_fd);
    close_socket(unix_fd);
    unlink("/tmp/messenger.sock");
    logger_close();

    printf("[SERVER] Done.\n");
    auth_cleanup();
    return 0;
}