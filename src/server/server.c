#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include "socket_utils.h"
#include "logger.h"
#include "auth.h"
#include "protocol.h"
#include "history.h"

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

    if (logger_init("logs/server.log") < 0) {
        fprintf(stderr, "Failed to init logger\n");
        return 1;
    }
    logger_write("SERVER STARTED");

    if (auth_init("data/passwd") < 0) {
        logger_write("Failed to load passwd file");
        return 1;
    }

    int hist_ret = history_init("data/messenger.db");
    printf("[DEBUG] history_init returned: %d\n", hist_ret);
    if (hist_ret < 0) {
        logger_write("Failed to init history file");
    }

    int tcp_fd = create_server_socket(PORT);
    int unix_fd = create_unix_server_socket("/tmp/messenger.sock");

    if (tcp_fd < 0 || unix_fd < 0) {
        logger_write("Failed to create sockets");
        return 1;
    }

    struct {
        int fd;
        char username[32];
        int logged_in;     /* 1 — авторизован, 0 — нет */
    } clients[MAX_CLIENTS];
    int client_count = 0;

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

        for (int i = 0; i < nfds && running; i++) {
            if (!(fds[i].revents & POLLIN)) continue;

            if (fds[i].fd == tcp_fd) {
                int client_fd = accept_client(tcp_fd);
                if (client_fd >= 0 && client_count < MAX_CLIENTS) {
                    clients[client_count].fd = client_fd;
                    clients[client_count].username[0] = '\0';
                    clients[client_count].logged_in = 0;
                    client_count++;
                    logger_write("Client connected via TCP (fd=%d)", client_fd);
                }
            }
            else if (fds[i].fd == unix_fd) {
                int client_fd = accept_client(unix_fd);
                if (client_fd >= 0 && client_count < MAX_CLIENTS) {
                    clients[client_count].fd = client_fd;
                    clients[client_count].username[0] = '\0';
                    clients[client_count].logged_in = 0;
                    client_count++;
                    logger_write("Client connected via Unix (fd=%d)", client_fd);
                }
            }
            else {
                char buffer[1024];
                int client_fd = fds[i].fd;
                int received = receive_message(client_fd, buffer, sizeof(buffer));

                /* Найдём индекс клиента в массиве */
                int client_idx = -1;
                for (int j = 0; j < client_count; j++) {
                    if (clients[j].fd == client_fd) {
                        client_idx = j;
                        break;
                    }
                }

                if (received <= 0) {
                    logger_write("Client disconnected (fd=%d)", client_fd);
                    close_socket(client_fd);
                    if (client_idx >= 0) {
                        clients[client_idx] = clients[client_count - 1];
                        client_count--;
                    }
                }
                else {
                    buffer[strcspn(buffer, "\n")] = '\0';
                    printf("[SERVER] Received from fd=%d: %s\n", client_fd, buffer);

                    /* --- LOGIN --- */
                    if (strncmp(buffer, "LOGIN ", 6) == 0) {
                        char username[32], password[32];
                        if (sscanf(buffer + 6, "%31s %31s", username, password) == 2) {
                            int result = auth_check(username, password);
                            if (result == 1) {
                                strncpy(clients[client_idx].username, username,
                                    sizeof(clients[client_idx].username) - 1);
                                clients[client_idx].logged_in = 1;
                                send_message(client_fd, "OK\n");
                                logger_write("User %s logged in (fd=%d)", username, client_fd);
                            }
                            else if (result == 0) {
                                send_message(client_fd, "ERROR Wrong password\n");
                            }
                            else {
                                send_message(client_fd, "ERROR User not found\n");
                            }
                        }
                        else {
                            send_message(client_fd, "ERROR Format: LOGIN username password\n");
                        }
                    }
                    /* --- LIST --- */
                    else if (strncmp(buffer, "LIST", 4) == 0) {
                        char userlist[512] = "";
                        for (int j = 0; j < client_count; j++) {
                            if (clients[j].logged_in) {
                                if (strlen(userlist) > 0) strcat(userlist, " ");
                                strcat(userlist, clients[j].username);
                            }
                        }
                        char response[1024];
                        protocol_make_user_list(response, userlist);
                        send_message(client_fd, response);
                        logger_write("LIST sent to fd=%d", client_fd);
                    }
                    /* --- SEND --- */
                    else if (strncmp(buffer, "SEND ", 5) == 0) {
                        if (!clients[client_idx].logged_in) {
                            send_message(client_fd, "ERROR Please login first\n");
                        }
                        else {
                            char recipient[32], message[1024];
                            if (protocol_parse_send(buffer, recipient, message) == 0) {
                                int found = 0;
                                for (int j = 0; j < client_count; j++) {
                                    if (clients[j].logged_in &&
                                        strcmp(clients[j].username, recipient) == 0) {
                                        char forward[1280];
                                        snprintf(forward, sizeof(forward),
                                            "[От %s] %s\n",
                                            clients[client_idx].username, message);
                                        send_message(clients[j].fd, forward);
                                        found = 1;
                                        break;
                                    }
                                }
                                if (found) {
                                    send_message(client_fd, "OK\n");
                                    logger_write("Message from %s to %s: %s",
                                        clients[client_idx].username,
                                        recipient, message);
                                    history_save(clients[client_idx].username,
                                        recipient, message);
                                }
                                else {
                                    send_message(client_fd, "ERROR User offline\n");
                                }
                            }
                            else {
                                send_message(client_fd, "ERROR Format: SEND username message\n");
                            }
                        }
                    }
                    

                    /* --- EXIT --- */
                    else if (strncmp(buffer, "EXIT", 4) == 0) {
                        send_message(client_fd, "BYE\n");
                        logger_write("Client %s disconnected (fd=%d)",
                            clients[client_idx].username, client_fd);
                        close_socket(client_fd);
                        clients[client_idx] = clients[client_count - 1];
                        client_count--;
                    }
                    /* --- HISTORY --- */
                    else if (strncmp(buffer, "HISTORY ", 8) == 0) {
                        if (!clients[client_idx].logged_in) {
                            send_message(client_fd, "ERROR Please login first\n");
                        }
                        else {
                            char other_user[32];
                            if (sscanf(buffer + 8, "%31s", other_user) == 1) {
                                char* hist = history_get(clients[client_idx].username,
                                    other_user);
                                if (hist && strlen(hist) > 0) {
                                    /* Отправляем историю — каждая строка отдельно */
                                    send_message(client_fd, "HISTORY_BEGIN\n");

                                    /* Разбиваем историю на строки и отправляем */
                                    char* line = strtok(hist, "\n");
                                    while (line) {
                                        send_message(client_fd, line);
                                        send_message(client_fd, "\n");
                                        line = strtok(NULL, "\n");
                                    }
                                    send_message(client_fd, "HISTORY_END\n");
                                    free(hist);
                                }
                                else {
                                    send_message(client_fd, "HISTORY_EMPTY\n");
                                    if (hist) free(hist);
                                }
                            }
                        }
                    }
                    /* --- Неизвестная команда --- */
                    else {
                        send_message(client_fd, "UNKNOWN Unknown command\n");
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
    auth_cleanup();
    logger_close();

    printf("[SERVER] Done.\n");
    history_close();

    return 0;
}