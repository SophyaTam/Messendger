#include "socket_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Создать TCP-сокет, привязать к порту, начать слушать
// port — номер порта (7777)
// Возвращает файловый дескриптор сокета или -1 при ошибке
int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    // Разрешаем переиспользовать адрес после перезапуска сервера
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Заполняем структуру адреса: IPv4, любой IP, указанный порт
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;      // Слушать на всех сетевых интерфейсах
    addr.sin_port = htons(port);            // Порт в сетевом порядке байт

    // Привязываем сокет к адресу
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    // Переводим сокет в режим прослушивания (очередь до 10 подключений)
    if (listen(fd, 10) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    printf("[SOCKET] TCP server listening on port %d (fd=%d)\n", port, fd);
    return fd;
}

// Создать Unix Domain Socket (для локального режима) path — путь к файлу сокета (/tmp/messenger.sock)
int create_unix_server_socket(const char* path) {
    // Создаём сокет: Unix-домен, потоковый
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    unlink(path);  // Удаляем старый файл сокета, если остался

    // Заполняем структуру адреса: Unix-семейство, путь к файлу
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 10) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    printf("[SOCKET] Unix server listening on %s (fd=%d)\n", path, fd);
    return fd;
}

// Принять входящее подключение от клиента server_fd — дескриптор слушающего сокета
// Возвращает новый дескриптор для общения с клиентом
int accept_client(int server_fd) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    // accept() блокируется, пока не подключится клиент
    int client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
    if (client_fd < 0) {
        perror("accept");
        return -1;
    }
    printf("[SOCKET] New client connected (fd=%d)\n", client_fd);
    return client_fd;
}

// Подключиться к TCP-серверу (клиентская функция)
// ip — IP-адрес сервера, port — порт
int connect_to_server(const char* ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);     // Преобразуем IP-строку в структуру

    // Устанавливаем соединение с сервером
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    printf("[SOCKET] Connected to %s:%d (fd=%d)\n", ip, port, fd);
    return fd;
}

// Подключиться к Unix-серверу (клиентская функция, локальный режим)
int connect_to_unix_server(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    printf("[SOCKET] Connected to Unix socket %s (fd=%d)\n", path, fd);
    return fd;
}

// Отправить сообщение в сокет
// fd — дескриптор сокета, message — строка для отправки
int send_message(int fd, const char* message) {
    int len = strlen(message);
    int sent = send(fd, message, len, 0);  // Системный вызов send()
    if (sent < 0) {
        perror("send");
    }
    return sent;
}

// Принять сообщение из сокета
// fd — дескриптор, buffer — куда записать, buffer_size — размер буфера
// Возвращает количество принятых байт, 0 — соединение закрыто, -1 — ошибка
int receive_message(int fd, char* buffer, int buffer_size) {
    int received = recv(fd, buffer, buffer_size - 1, 0);
    if (received < 0) {
        perror("recv");
        return -1;
    }
    if (received == 0) {
        return 0;
    }
    buffer[received] = '\0';   // Завершаем строку
    return received;
}

// Закрыть сокет
void close_socket(int fd) {
    if (fd >= 0) {
        close(fd);     // Системный вызов close()
        printf("[SOCKET] Closed fd=%d\n", fd);
    }
}