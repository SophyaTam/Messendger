#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include "socket_utils.h"
#include "logger.h"
#include "auth.h"
#include "protocol.h"
#include "history.h"
#include "crypto.h"
#include "group.h"
#include "client_hash.h"

#define MAX_CLIENTS 10
#define PORT 7777

static int running = 1;

/* Мьютекс для синхронизации доступа к хеш-таблице */
static pthread_mutex_t hash_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Структура для передачи в поток */
typedef struct {
    int fd;
} ClientThreadArgs;

/* Прототип функции обработки клиента */
void* handle_client(void* arg);

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
    hash_init();

    int tcp_fd = create_server_socket(PORT);
    int unix_fd = create_unix_server_socket("/tmp/messenger.sock");

    if (tcp_fd < 0 || unix_fd < 0) {
        logger_write("Failed to create sockets");
        return 1;
    }

    printf("[SERVER] Entering main loop (multi-threaded, hash table)...\n");

    while (running) {
        struct pollfd fds[2];
        fds[0].fd = tcp_fd;
        fds[0].events = POLLIN;
        fds[1].fd = unix_fd;
        fds[1].events = POLLIN;

        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            if (running) perror("poll");
            break;
        }
        if (ret == 0) continue;

        if (fds[0].revents & POLLIN) {
            int client_fd = accept_client(tcp_fd);
            if (client_fd >= 0) {
                pthread_mutex_lock(&hash_mutex);
                if (hash_count() < MAX_CLIENTS) {
                    hash_add("", client_fd, 0);
                    logger_write("Client connected via TCP (fd=%d)", client_fd);
                    pthread_mutex_unlock(&hash_mutex);

                    ClientThreadArgs* args = malloc(sizeof(ClientThreadArgs));
                    args->fd = client_fd;
                    pthread_t tid;
                    pthread_create(&tid, NULL, handle_client, args);
                    pthread_detach(tid);
                }
                else {
                    pthread_mutex_unlock(&hash_mutex);
                    send_message(client_fd, "ERROR Server full\n");
                    close_socket(client_fd);
                }
            }
        }

        if (fds[1].revents & POLLIN) {
            int client_fd = accept_client(unix_fd);
            if (client_fd >= 0) {
                pthread_mutex_lock(&hash_mutex);
                if (hash_count() < MAX_CLIENTS) {
                    hash_add("", client_fd, 0);
                    logger_write("Client connected via Unix (fd=%d)", client_fd);
                    pthread_mutex_unlock(&hash_mutex);

                    ClientThreadArgs* args = malloc(sizeof(ClientThreadArgs));
                    args->fd = client_fd;
                    pthread_t tid;
                    pthread_create(&tid, NULL, handle_client, args);
                    pthread_detach(tid);
                }
                else {
                    pthread_mutex_unlock(&hash_mutex);
                    send_message(client_fd, "ERROR Server full\n");
                    close_socket(client_fd);
                }
            }
        }
    }

    history_log_event("SERVER_STOP", NULL, NULL);
    logger_write("SERVER STOPPED");

    /* Хеш-таблица очищается в hash_cleanup() */

    close_socket(tcp_fd);
    close_socket(unix_fd);
    unlink("/tmp/messenger.sock");
    auth_cleanup();
    group_cleanup();
    hash_cleanup();
    history_close();
    logger_close();

    printf("[SERVER] Done.\n");
    return 0;
}

void* handle_client(void* arg) {
    ClientThreadArgs* args = (ClientThreadArgs*)arg;
    int client_fd = args->fd;
    free(args);

    char buffer[1024];

    while (running) {
        int received = receive_message(client_fd, buffer, sizeof(buffer));
        if (received <= 0) {
            pthread_mutex_lock(&hash_mutex);
            ClientNode* node = hash_find_by_fd(client_fd);
            if (node && node->username[0] != '\0') {
                history_log_event("USER_LOGOUT", node->username, NULL);
                logger_write("Client %s disconnected (fd=%d)", node->username, client_fd);
            }
            hash_remove(client_fd);
            pthread_mutex_unlock(&hash_mutex);
            close_socket(client_fd);
            return NULL;
        }

        buffer[strcspn(buffer, "\n")] = '\0';
        printf("[SERVER] Received from fd=%d: %s\n", client_fd, buffer);

        /* Проверяем, жив ли ещё клиент */
        pthread_mutex_lock(&hash_mutex);
        ClientNode* my_node = hash_find_by_fd(client_fd);
        pthread_mutex_unlock(&hash_mutex);
        if (!my_node) { close_socket(client_fd); return NULL; }

        /* Расшифровка ENC:SEND */
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

        /* REGISTER */
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
        }
        /* LOGIN */
        else if (strncmp(buffer, "LOGIN ", 6) == 0) {
            char username[32], password[32];
            if (sscanf(buffer + 6, "%31s %31s", username, password) == 2) {
                int result = auth_check(username, password);
                if (result == 1) {
                    pthread_mutex_lock(&hash_mutex);
                    hash_update(client_fd, username, 1);
                    pthread_mutex_unlock(&hash_mutex);
                    send_message(client_fd, "OK\n");
                    logger_write("User %s logged in (fd=%d)", username, client_fd);
                    history_log_event("USER_LOGIN", username, NULL);
                    char* offline = history_get_offline(username);
                    if (offline && strlen(offline) > 0) {
                        send_message(client_fd, "OFFLINE_BEGIN\n");
                        char* line = strtok(offline, "\n");
                        while (line) { send_message(client_fd, line); send_message(client_fd, "\n"); line = strtok(NULL, "\n"); }
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
        }
        /* LIST */
        else if (strncmp(buffer, "LIST", 4) == 0) {
            pthread_mutex_lock(&hash_mutex);
            char* userlist = hash_get_online_list();
            pthread_mutex_unlock(&hash_mutex);
            char response[1024];
            protocol_make_user_list(response, userlist ? userlist : "");
            send_message(client_fd, response);
            if (userlist) free(userlist);
        }
        /* SEND */
        else if (strncmp(buffer, "SEND ", 5) == 0) {
            if (!my_node->logged_in) {
                send_message(client_fd, "ERROR Please login first\n");
            }
            else {
                char recipient[32], message[1024];
                if (protocol_parse_send(buffer, recipient, message) == 0) {
                    pthread_mutex_lock(&hash_mutex);
                    int recv_fd = hash_find_fd_by_name(recipient);
                    if (recv_fd >= 0) {
                        char plain_forward[1280];
                        snprintf(plain_forward, sizeof(plain_forward), "[От %s] %s",
                            my_node->username, message);
                        char* enc = crypto_encrypt(plain_forward);
                        if (enc) {
                            char final_msg[1400];
                            snprintf(final_msg, sizeof(final_msg), "ENC:%s\n", enc);
                            send_message(recv_fd, final_msg);
                            free(enc);
                        }
                        pthread_mutex_unlock(&hash_mutex);
                        send_message(client_fd, "OK\n");
                        history_save(my_node->username, recipient, message);
                    }
                    else {
                        pthread_mutex_unlock(&hash_mutex);
                        history_save_offline(my_node->username, recipient, message);
                        send_message(client_fd, "OK Saved (user offline)\n");
                    }
                }
            }
        }
        /* EXIT */
        else if (strncmp(buffer, "EXIT", 4) == 0) {
            send_message(client_fd, "BYE\n");
            pthread_mutex_lock(&hash_mutex);
            if (my_node->username[0] != '\0') {
                logger_write("Client %s disconnected (fd=%d)", my_node->username, client_fd);
            }
            hash_remove(client_fd);
            pthread_mutex_unlock(&hash_mutex);
            close_socket(client_fd);
            return NULL;
        }
        /* HISTORY */
        else if (strncmp(buffer, "HISTORY", 7) == 0) {
            if (!my_node->logged_in) {
                send_message(client_fd, "ERROR Please login first\n");
            }
            else {
                char* hist = history_get(my_node->username, my_node->username);
                if (hist && strlen(hist) > 0) {
                    send_message(client_fd, "HISTORY_BEGIN\n");
                    char* line = strtok(hist, "\n");
                    while (line) { send_message(client_fd, line); send_message(client_fd, "\n"); line = strtok(NULL, "\n"); }
                    send_message(client_fd, "HISTORY_END\n");
                    free(hist);
                }
                else {
                    send_message(client_fd, "HISTORY_EMPTY\n");
                }
            }
        }
        /* GROUP_CREATE */
        else if (strncmp(buffer, "GROUP_CREATE ", 13) == 0) {
            char group_name[32], password[32];
            if (sscanf(buffer + 13, "%31s %31s", group_name, password) == 2) {
                int ret = group_create(group_name, password, my_node->username);
                if (ret == 0) send_message(client_fd, "GROUP_CREATED\n");
                else if (ret == -1) send_message(client_fd, "ERROR Group already exists\n");
                else send_message(client_fd, "ERROR Cannot create\n");
            }
        }
        /* GROUP_JOIN */
        else if (strncmp(buffer, "GROUP_JOIN ", 11) == 0) {
            char group_name[32], password[32];
            if (sscanf(buffer + 11, "%31s %31s", group_name, password) == 2) {
                int ret = group_join(group_name, password, my_node->username);
                if (ret == 0) send_message(client_fd, "GROUP_JOINED\n");
                else if (ret == -1) send_message(client_fd, "ERROR Already in group\n");
                else if (ret == -2) send_message(client_fd, "ERROR Wrong password\n");
                else send_message(client_fd, "ERROR Group not found\n");
            }
        }
        /* ENC:GROUP_MSG */
        else if (strncmp(buffer, "ENC:GROUP_MSG ", 14) == 0) {
            char prefix[16], group_name[32], hex_cipher[1024];
            if (sscanf(buffer, "%15s %31s %1023s", prefix, group_name, hex_cipher) == 3) {
                char* pt = crypto_decrypt(hex_cipher);
                if (pt) { snprintf(buffer, 1024, "GROUP_MSG %s %s", group_name, pt); free(pt); }
            }
        }
        /* GROUP_MSG */
        if (strncmp(buffer, "GROUP_MSG ", 10) == 0) {
            char group_name[32];
            char* sp = strchr(buffer + 10, ' ');
            if (sp) {
                *sp = '\0';
                strncpy(group_name, buffer + 10, sizeof(group_name) - 1);
                char* msg = sp + 1;
                if (group_is_member(group_name, my_node->username)) {
                    char* rec = group_get_recipients(group_name, my_node->username);
                    if (rec && strlen(rec) > 0) {
                        char* tok = strtok(rec, " ");
                        while (tok) {
                            pthread_mutex_lock(&hash_mutex);
                            int recv_fd = hash_find_fd_by_name(tok);
                            if (recv_fd >= 0) {
                                char gm[1280];
                                snprintf(gm, sizeof(gm), "[Группа %s | %s] %s", group_name, my_node->username, msg);
                                char* enc = crypto_encrypt(gm);
                                if (enc) {
                                    char fm[1400];
                                    snprintf(fm, sizeof(fm), "ENC:%s\n", enc);
                                    send_message(recv_fd, fm);
                                    free(enc);
                                }
                            }
                            pthread_mutex_unlock(&hash_mutex);
                            tok = strtok(NULL, " ");
                        }
                        free(rec);
                        send_message(client_fd, "OK\n");
                        history_save(my_node->username, group_name, msg);
                    }
                    else { send_message(client_fd, "ERROR No members\n"); if (rec) free(rec); }
                }
                else { send_message(client_fd, "ERROR Not in group\n"); }
            }
        }
        else {
            send_message(client_fd, "UNKNOWN Unknown command\n");
        }
    }

    close_socket(client_fd);
    return NULL;
}