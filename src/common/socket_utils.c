#include "socket_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    /* Переиспользовать адрес после перезапуска */
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

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

    printf("[SOCKET] TCP server listening on port %d (fd=%d)\n", port, fd);
    return fd;
}

int create_unix_server_socket(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    unlink(path); /* Удалить старый сокет-файл */

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

int accept_client(int server_fd) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
    if (client_fd < 0) {
        perror("accept");
        return -1;
    }
    printf("[SOCKET] New client connected (fd=%d)\n", client_fd);
    return client_fd;
}

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
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    printf("[SOCKET] Connected to %s:%d (fd=%d)\n", ip, port, fd);
    return fd;
}

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

int send_message(int fd, const char* message) {
    int len = strlen(message);
    int sent = send(fd, message, len, 0);
    if (sent < 0) {
        perror("send");
    }
    return sent;
}

int receive_message(int fd, char* buffer, int buffer_size) {
    int received = recv(fd, buffer, buffer_size - 1, 0);
    if (received < 0) {
        perror("recv");
        return -1;
    }
    if (received == 0) {
        /* Соединение закрыто */
        return 0;
    }
    buffer[received] = '\0';
    return received;
}

void close_socket(int fd) {
    if (fd >= 0) {
        close(fd);
        printf("[SOCKET] Closed fd=%d\n", fd);
    }
}