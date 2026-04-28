#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include <sys/types.h>

/* Создать TCP-сокет и привязать к порту (для сервера) */
int create_server_socket(int port);

/* Создать Unix Domain Socket (для локального режима) */
int create_unix_server_socket(const char* path);

/* Принять входящее подключение */
int accept_client(int server_fd);

/* Подключиться к серверу по TCP (для клиента) */
int connect_to_server(const char* ip, int port);

/* Подключиться к серверу через Unix-сокет (для клиента) */
int connect_to_unix_server(const char* path);

/* Отправить сообщение в сокет */
int send_message(int fd, const char* message);

/* Принять сообщение из сокета */
int receive_message(int fd, char* buffer, int buffer_size);

/* Закрыть сокет */
void close_socket(int fd);

#endif