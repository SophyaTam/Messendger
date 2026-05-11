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
#include "crypto.h"
#include "group.h"

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
    history_log_event("SERVER_START", NULL, NULL);

    if (auth_init("data/passwd") < 0) {
        logger_write("Failed to load passwd file");
        return 1;
    }

    if (crypto_init("messenger2026key") < 0) {
        logger_write("Failed to init crypto");
    }

    if (history_init("data/messenger.db") < 0) {
        logger_write("Failed to init history file");
    }

    group_init(history_get_db());

    int tcp_fd = create_server_socket(PORT);
    int unix_fd = create_unix_server_socket("/tmp/messenger.sock");

    if (tcp_fd < 0 || unix_fd < 0) {
        logger_write("Failed to create sockets");
        return 1;
    }

    struct {
        int fd;
        char username[32];
        int logged_in;
    } clients[MAX_CLIENTS];
    int client_count = 0;

    printf("[SERVER] Entering main loop...\n");

    while (running) {
        struct pollfd fds[MAX_CLIENTS + 2];
        int nfds = 0;

        fds[nfds].fd = tcp_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        fds[nfds].fd = unix_fd;
        fds[nfds].events = POLLIN;
        nfds++;

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

                int client_idx = -1;
                for (int j = 0; j < client_count; j++) {
                    if (clients[j].fd == client_fd) {
                        client_idx = j;
                        break;
                    }
                }

                if (received <= 0) {
                    if (client_idx >= 0 && clients[client_idx].username[0] != '\0') {
                        history_log_event("USER_LOGOUT", clients[client_idx].username, NULL);
                    }
                    logger_write("Client disconnected (fd=%d)", client_fd);
                    close_socket(client_fd);
                    if (client_idx >= 0) {
                        clients[client_idx] = clients[client_count - 1];
                        client_count--;
                    }
                    continue;
                }

                buffer[strcspn(buffer, "\n")] = '\0';

                printf("[SERVER] Received from fd=%d: %s\n", client_fd, buffer);

                if (strncmp(buffer, "ENC:SEND ", 9) == 0) {
                    char prefix[16], recipient[32], hex_cipher[1024];
                    if (sscanf(buffer, "%15s %31s %1023s", prefix, recipient, hex_cipher) == 3) {
                        char* plaintext = crypto_decrypt(hex_cipher);
                        if (plaintext) {
                            snprintf(buffer, 1024, "SEND %s %s", recipient, plaintext);
                            free(plaintext);
                        }
                    }
                }

                if (strncmp(buffer, "REGISTER ", 9) == 0) {
                    char username[32], password[32];
                    if (sscanf(buffer + 9, "%31s %31s", username, password) == 2) {
                        int ret = auth_register(username, password);
                        if (ret == 0) {
                            send_message(client_fd, "OK Registered\n");
                            logger_write("New user registered: %s", username);
                        }
                        else if (ret == -1) {
                            send_message(client_fd, "ERROR Username already exists\n");
                        }
                        else {
                            send_message(client_fd, "ERROR Cannot register\n");
                        }
                    }
                    else {
                        send_message(client_fd, "ERROR Format: REGISTER username password\n");
                    }
                }
                else if (strncmp(buffer, "LOGIN ", 6) == 0) {
                    char username[32], password[32];
                    if (sscanf(buffer + 6, "%31s %31s", username, password) == 2) {
                        int result = auth_check(username, password);
                        if (result == 1) {
                            strncpy(clients[client_idx].username, username,
                                sizeof(clients[client_idx].username) - 1);
                            clients[client_idx].logged_in = 1;
                            send_message(client_fd, "OK\n");
                            logger_write("User %s logged in (fd=%d)", username, client_fd);
                            history_log_event("USER_LOGIN", username, NULL);
                            /* Отправить офлайн-сообщения */
                            char* offline = history_get_offline(username);
                            if (offline && strlen(offline) > 0) {
                                send_message(client_fd, "OFFLINE_BEGIN\n");
                                char* line = strtok(offline, "\n");
                                while (line) {
                                    send_message(client_fd, line);
                                    send_message(client_fd, "\n");
                                    line = strtok(NULL, "\n");
                                }
                                send_message(client_fd, "OFFLINE_END\n");
                                free(offline);
                            }
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
                                    char plain_forward[1280];
                                    snprintf(plain_forward, sizeof(plain_forward),
                                        "[От %s] %s",
                                        clients[client_idx].username, message);
                                    char* encrypted_forward = crypto_encrypt(plain_forward);
                                    if (encrypted_forward) {
                                        char final_msg[1400];
                                        snprintf(final_msg, sizeof(final_msg),
                                            "ENC:%s\n", encrypted_forward);
                                        send_message(clients[j].fd, final_msg);
                                        free(encrypted_forward);
                                    }
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
                                history_save_offline(clients[client_idx].username, recipient, message);
                                send_message(client_fd, "OK Saved (user offline)\n");
                                logger_write("Offline message from %s to %s: %s",
                                    clients[client_idx].username, recipient, message);
                            }
                        }
                        else {
                            send_message(client_fd, "ERROR Format: SEND username message\n");
                        }
                    }
                }
                else if (strncmp(buffer, "EXIT", 4) == 0) {
                    send_message(client_fd, "BYE\n");
                    logger_write("Client %s disconnected (fd=%d)",
                        clients[client_idx].username, client_fd);
                    close_socket(client_fd);
                    clients[client_idx] = clients[client_count - 1];
                    client_count--;
                }

                else if (strncmp(buffer, "HISTORY", 7) == 0) {
                    if (!clients[client_idx].logged_in) {
                        send_message(client_fd, "ERROR Please login first\n");
                    }
                    else {
                        char* hist = history_get(clients[client_idx].username, clients[client_idx].username);
                        if (hist && strlen(hist) > 0) {
                            send_message(client_fd, "HISTORY_BEGIN\n");
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
                else if (strncmp(buffer, "GROUP_CREATE ", 13) == 0) {
                    if (!clients[client_idx].logged_in) {
                        send_message(client_fd, "ERROR Please login first\n");
                    }
                    else {
                        char group_name[32], password[32];
                        if (sscanf(buffer + 13, "%31s %31s", group_name, password) == 2) {
                            int ret = group_create(group_name, password, clients[client_idx].username);
                            if (ret == 0) {
                                char response[64];
                                snprintf(response, sizeof(response), "GROUP_CREATED %s\n", group_name);
                                send_message(client_fd, response);
                            }
                            else if (ret == -1) {
                                send_message(client_fd, "ERROR Group already exists\n");
                            }
                            else {
                                send_message(client_fd, "ERROR Cannot create group\n");
                            }
                        }
                        else {
                            send_message(client_fd, "ERROR Format: GROUP_CREATE group_name password\n");
                        }
                    }
                }
                else if (strncmp(buffer, "GROUP_JOIN ", 11) == 0) {
                    if (!clients[client_idx].logged_in) {
                        send_message(client_fd, "ERROR Please login first\n");
                    }
                    else {
                        char group_name[32], password[32];
                        if (sscanf(buffer + 11, "%31s %31s", group_name, password) == 2) {
                            int ret = group_join(group_name, password, clients[client_idx].username);
                            if (ret == 0) {
                                char response[64];
                                snprintf(response, sizeof(response), "GROUP_JOINED %s\n", group_name);
                                send_message(client_fd, response);
                            }
                            else if (ret == -1) {
                                send_message(client_fd, "ERROR Already in group\n");
                            }
                            else if (ret == -2) {
                                send_message(client_fd, "ERROR Wrong password\n");
                            }
                            else {
                                send_message(client_fd, "ERROR Group not found or full\n");
                            }
                        }
                        else {
                            send_message(client_fd, "ERROR Format: GROUP_JOIN group_name password\n");
                        }
                    }
                }
                else if (strncmp(buffer, "ENC:GROUP_MSG ", 14) == 0) {
                    char prefix[16], group_name[32], hex_cipher[1024];
                    if (sscanf(buffer, "%15s %31s %1023s", prefix, group_name, hex_cipher) == 3) {
                        char* plaintext = crypto_decrypt(hex_cipher);
                        if (plaintext) {
                            snprintf(buffer, 1024, "GROUP_MSG %s %s", group_name, plaintext);
                            free(plaintext);
                        }
                    }
                }

                if (strncmp(buffer, "GROUP_MSG ", 10) == 0) {
                    if (!clients[client_idx].logged_in) {
                        send_message(client_fd, "ERROR Please login first\n");
                    }
                    else {
                        char group_name[32];
                        char* text_start = buffer + 10;
                        char* space = strchr(text_start, ' ');
                        if (space) {
                            *space = '\0';
                            strncpy(group_name, text_start, sizeof(group_name) - 1);
                            char* message = space + 1;

                            if (group_is_member(group_name, clients[client_idx].username)) {
                                char* recipients = group_get_recipients(group_name, clients[client_idx].username);
                                if (recipients && strlen(recipients) > 0) {
                                    char* token = strtok(recipients, " ");
                                    while (token) {
                                        for (int j = 0; j < client_count; j++) {
                                            if (clients[j].logged_in && strcmp(clients[j].username, token) == 0) {
                                                char group_msg[1280];
                                                snprintf(group_msg, sizeof(group_msg),
                                                    "[Группа %s | %s] %s",
                                                    group_name, clients[client_idx].username, message);
                                                char* enc = crypto_encrypt(group_msg);
                                                if (enc) {
                                                    char final_msg[1400];
                                                    snprintf(final_msg, sizeof(final_msg), "ENC:%s\n", enc);
                                                    send_message(clients[j].fd, final_msg);
                                                    free(enc);
                                                }
                                            }
                                        }
                                        token = strtok(NULL, " ");
                                    }
                                    free(recipients);
                                    send_message(client_fd, "OK\n");
                                    history_save(clients[client_idx].username, group_name, message);
                                }
                                else {
                                    send_message(client_fd, "ERROR No other members in group\n");
                                    if (recipients) free(recipients);
                                }
                            }
                            else {
                                send_message(client_fd, "ERROR You are not in this group\n");
                            }
                        }
                        else {
                            send_message(client_fd, "ERROR Format: GROUP_MSG group_name message\n");
                        }
                    }
                }
                else {
                    send_message(client_fd, "UNKNOWN Unknown command\n");
                }
            }
        }
    }

    history_log_event("SERVER_STOP", NULL, NULL);
    logger_write("SERVER STOPPED");
    for (int i = 0; i < client_count; i++) {
        close_socket(clients[i].fd);
    }
    close_socket(tcp_fd);
    close_socket(unix_fd);
    unlink("/tmp/messenger.sock");
    auth_cleanup();
    group_cleanup();
    history_close();
    logger_close();

    printf("[SERVER] Done.\n");
    return 0;
}