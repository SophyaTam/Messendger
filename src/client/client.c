#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "socket_utils.h"

int main() {
    printf("[CLIENT] Connecting to server...\n");

    int fd = connect_to_server("127.0.0.1", 7777);
    if (fd < 0) {
        fprintf(stderr, "[CLIENT] Failed to connect\n");
        return 1;
    }

    /* --- Аутентификация --- */
    char username[32], password[32];
    printf("Login: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';

    printf("Password: ");
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = '\0';

    /* Формируем команду LOGIN */
    char command[128];
    snprintf(command, sizeof(command), "LOGIN %s %s\n", username, password);
    send_message(fd, command);

    /* Ждём ответ */
    char response[256];
    int received = receive_message(fd, response, sizeof(response));
    if (received > 0) {
        response[strcspn(response, "\n")] = '\0';
        printf("[SERVER] %s\n", response);
    }

    close_socket(fd);
    printf("[CLIENT] Done.\n");
    return 0;
}