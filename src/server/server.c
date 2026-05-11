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

#define MAX_CLIENTS 10
#define PORT 7777

static int running = 1;

/* Мьютекс для синхронизации доступа к списку клиентов */
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Структура для передачи в поток */
typedef struct {
    int fd;
} ClientThreadArgs;

/* Список клиентов (общий для всех потоков) */
typedef struct {
    int fd;
    char username[32];
    int logged_in;
} ClientInfo;

static ClientInfo clients[MAX_CLIENTS];
static int client_count = 0;

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

    int tcp_fd = create_server_socket(PORT);
    int unix_fd = create_unix_server_socket("/tmp/messenger.sock");

    if (tcp_fd < 0 || unix_fd < 0) {
        logger_write("Failed to create sockets");
        return 1;
    }

    printf("[SERVER] Entering main loop (multi-threaded)...\n");

    /* Главный цикл — только принимает подключения */
    while (running) {
        struct pollfd fds[2];
        fds[0].fd = tcp_fd;
        fds[0].events = POLLIN;
        fds[1].fd = unix_fd;
        fds[1].events = POLLIN;

        int ret = poll(fds, 2, 1000); /* Таймаут 1 секунда для проверки running */
        if (ret < 0) {
            if (running) perror("poll");
            break;
        }
        if (ret == 0) continue;

        if (fds[0].revents & POLLIN) {
            int client_fd = accept_client(tcp_fd);
            if (client_fd >= 0) {
                pthread_mutex_lock(&clients_mutex);
                if (client_count < MAX_CLIENTS) {
                    clients[client_count].fd = client_fd;
                    clients[client_count].username[0] = '\0';
                    clients[client_count].logged_in = 0;
                    client_count++;
                    logger_write("Client connected via TCP (fd=%d)", client_fd);

                    /* Создаём поток для клиента */
                    ClientThreadArgs* args = malloc(sizeof(ClientThreadArgs));
                    args->fd = client_fd;
                    pthread_t tid;
                    pthread_create(&tid, NULL, handle_client, args);
                    pthread_detach(tid); /* Поток сам освободится */
                }
                else {
                    send_message(client_fd, "ERROR Server full\n");
                    close_socket(client_fd);
                }
                pthread_mutex_unlock(&clients_mutex);
            }
        }

        if (fds[1].revents & POLLIN) {
            int client_fd = accept_client(unix_fd);
            if (client_fd >= 0) {
                pthread_mutex_lock(&clients_mutex);
                if (client_count < MAX_CLIENTS) {
                    clients[client_count].fd = client_fd;
                    clients[client_count].username[0] = '\0';
                    clients[client_count].logged_in = 0;
                    client_count++;
                    logger_write("Client connected via Unix (fd=%d)", client_fd);

                    ClientThreadArgs* args = malloc(sizeof(ClientThreadArgs));
                    args->fd = client_fd;
                    pthread_t tid;
                    pthread_create(&tid, NULL, handle_client, args);
                    pthread_detach(tid);
                }
                else {
                    send_message(client_fd, "ERROR Server full\n");
                    close_socket(client_fd);
                }
                pthread_mutex_unlock(&clients_mutex);
            }
        }
    }

    history_log_event("SERVER_STOP", NULL, NULL);
    logger_write("SERVER STOPPED");

    /* Закрываем всех клиентов */
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        close_socket(clients[i].fd);
    }
    client_count = 0;
    pthread_mutex_unlock(&clients_mutex);

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

/* Обработка одного клиента (выполняется в отдельном потоке) */
void* handle_client(void* arg) {
    ClientThreadArgs* args = (ClientThreadArgs*)arg;
    int client_fd = args->fd;
    free(args);

    char buffer[1024];

    while (running) {
        int received = receive_message(client_fd, buffer, sizeof(buffer));
        if (received <= 0) {
            /* Клиент отключился */
            pthread_mutex_lock(&clients_mutex);
            for (int i = 0; i < client_count; i++) {
                if (clients[i].fd == client_fd) {
                    if (clients[i].username[0] != '\0') {
                        history_log_event("USER_LOGOUT", clients[i].username, NULL);
                        logger_write("Client %s disconnected (fd=%d)", clients[i].username, client_fd);
                    }
                    clients[i] = clients[client_count - 1];
                    client_count--;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_mutex);
            close_socket(client_fd);
            return NULL;
        }

        buffer[strcspn(buffer, "\n")] = '\0';
        printf("[SERVER] Received from fd=%d: %s\n", client_fd, buffer);

        /* --- Находим свой индекс в массиве клиентов --- */
        int my_idx = -1;
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < client_count; i++) {
            if (clients[i].fd == client_fd) { my_idx = i; break; }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (my_idx == -1) { close_socket(client_fd); return NULL; }

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
                    pthread_mutex_lock(&clients_mutex);
                    strncpy(clients[my_idx].username, username, sizeof(clients[my_idx].username) - 1);
                    clients[my_idx].logged_in = 1;
                    pthread_mutex_unlock(&clients_mutex);
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
            char userlist[512] = "";
            pthread_mutex_lock(&clients_mutex);
            for (int j = 0; j < client_count; j++) {
                if (clients[j].logged_in) {
                    if (strlen(userlist) > 0) strcat(userlist, " ");
                    strcat(userlist, clients[j].username);
                }
            }
            pthread_mutex_unlock(&clients_mutex);
            char response[1024];
            protocol_make_user_list(response, userlist);
            send_message(client_fd, response);
        }
        /* SEND */
        else if (strncmp(buffer, "SEND ", 5) == 0) {
            if (!clients[my_idx].logged_in) {
                send_message(client_fd, "ERROR Please login first\n");
            }
            else {
                char recipient[32], message[1024];
                if (protocol_parse_send(buffer, recipient, message) == 0) {
                    int found = 0;
                    pthread_mutex_lock(&clients_mutex);
                    for (int j = 0; j < client_count; j++) {
                        if (clients[j].logged_in && strcmp(clients[j].username, recipient) == 0) {
                            char plain_forward[1280];
                            snprintf(plain_forward, sizeof(plain_forward), "[От %s] %s",
                                clients[my_idx].username, message);
                            char* enc = crypto_encrypt(plain_forward);
                            if (enc) {
                                char final_msg[1400];
                                snprintf(final_msg, sizeof(final_msg), "ENC:%s\n", enc);
                                send_message(clients[j].fd, final_msg);
                                free(enc);
                            }
                            found = 1;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&clients_mutex);
                    if (found) {
                        send_message(client_fd, "OK\n");
                        history_save(clients[my_idx].username, recipient, message);
                    }
                    else {
                        history_save_offline(clients[my_idx].username, recipient, message);
                        send_message(client_fd, "OK Saved (user offline)\n");
                    }
                }
            }
        }
        /* EXIT */
        else if (strncmp(buffer, "EXIT", 4) == 0) {
            send_message(client_fd, "BYE\n");
            pthread_mutex_lock(&clients_mutex);
            if (clients[my_idx].username[0] != '\0') {
                logger_write("Client %s disconnected (fd=%d)", clients[my_idx].username, client_fd);
            }
            clients[my_idx] = clients[client_count - 1];
            client_count--;
            pthread_mutex_unlock(&clients_mutex);
            close_socket(client_fd);
            return NULL;
        }
        /* HISTORY */
        else if (strncmp(buffer, "HISTORY", 7) == 0) {
            if (!clients[my_idx].logged_in) {
                send_message(client_fd, "ERROR Please login first\n");
            }
            else {
                char* hist = history_get(clients[my_idx].username, clients[my_idx].username);
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
        /* GROUP_CREATE, GROUP_JOIN, GROUP_MSG */
        else if (strncmp(buffer, "GROUP_CREATE ", 13) == 0) {
            char group_name[32], password[32];
            if (sscanf(buffer + 13, "%31s %31s", group_name, password) == 2) {
                int ret = group_create(group_name, password, clients[my_idx].username);
                if (ret == 0) { send_message(client_fd, "GROUP_CREATED\n"); }
                else if (ret == -1) { send_message(client_fd, "ERROR Group already exists\n"); }
                else { send_message(client_fd, "ERROR Cannot create\n"); }
            }
        }
        else if (strncmp(buffer, "GROUP_JOIN ", 11) == 0) {
            char group_name[32], password[32];
            if (sscanf(buffer + 11, "%31s %31s", group_name, password) == 2) {
                int ret = group_join(group_name, password, clients[my_idx].username);
                if (ret == 0) { send_message(client_fd, "GROUP_JOINED\n"); }
                else if (ret == -1) { send_message(client_fd, "ERROR Already in group\n"); }
                else if (ret == -2) { send_message(client_fd, "ERROR Wrong password\n"); }
                else { send_message(client_fd, "ERROR Group not found\n"); }
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
        if (strncmp(buffer, "GROUP_MSG ", 10) == 0) {
            char group_name[32];
            char* sp = strchr(buffer + 10, ' ');
            if (sp) {
                *sp = '\0';
                strncpy(group_name, buffer + 10, sizeof(group_name) - 1);
                char* msg = sp + 1;
                if (group_is_member(group_name, clients[my_idx].username)) {
                    char* rec = group_get_recipients(group_name, clients[my_idx].username);
                    if (rec && strlen(rec) > 0) {
                        char* tok = strtok(rec, " ");
                        while (tok) {
                            pthread_mutex_lock(&clients_mutex);
                            for (int j = 0; j < client_count; j++) {
                                if (clients[j].logged_in && strcmp(clients[j].username, tok) == 0) {
                                    char gm[1280];
                                    snprintf(gm, sizeof(gm), "[Группа %s | %s] %s", group_name, clients[my_idx].username, msg);
                                    char* enc = crypto_encrypt(gm);
                                    if (enc) {
                                        char fm[1400];
                                        snprintf(fm, sizeof(fm), "ENC:%s\n", enc);
                                        send_message(clients[j].fd, fm);
                                        free(enc);
                                    }
                                }
                            }
                            pthread_mutex_unlock(&clients_mutex);
                            tok = strtok(NULL, " ");
                        }
                        free(rec);
                        send_message(client_fd, "OK\n");
                        history_save(clients[my_idx].username, group_name, msg);
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