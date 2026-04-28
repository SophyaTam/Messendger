#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "socket_utils.h"

int main() {
    printf("[CLIENT] Connecting to server...\n");

    /* Подключение через TCP */
    int fd = connect_to_server("127.0.0.1", 7777);
    if (fd < 0) {
        fprintf(stderr, "[CLIENT] Failed to connect\n");
        return 1;
    }

    /* Тестовое сообщение */
    send_message(fd, "Hello from client!\n");

    /* Получить ответ */
    char buffer[1024];
    int received = receive_message(fd, buffer, sizeof(buffer));
    if (received > 0) {
        printf("[CLIENT] Server replied: %s", buffer);
    }

    close_socket(fd);
    printf("[CLIENT] Done.\n");
    return 0;
}