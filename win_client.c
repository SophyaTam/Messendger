/*
 * win_client.c — Клиент мессенджера для Windows
 * Компиляция: cl win_client.c ws2_32.lib libcrypto.lib /Fe:win_client.exe
 * Протокол совместим с сервером на Linux.
 * Поддерживает русские буквы (UTF-8) и AES-128 шифрование.
 */

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/sha.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libcrypto.lib")

#define SERVER_PORT 7777
#define BUFFER_SIZE 4096

static SOCKET server_sock = INVALID_SOCKET;
static int running = 1;

/* ========== AES-128 шифрование ========== */
static unsigned char aes_key[16];
static unsigned char aes_iv[16];
static int crypto_ready = 0;

void crypto_init(const char* key_str) {
    memset(aes_key, 0, 16);
    strncpy((char*)aes_key, key_str, 16);
    memcpy(aes_iv, aes_key, 16);
    crypto_ready = 1;
}

char* crypto_decrypt(const char* hex_ciphertext) {
    if (!crypto_ready || !hex_ciphertext) return NULL;
    int hex_len = (int)strlen(hex_ciphertext);
    int ciphertext_len = hex_len / 2;
    unsigned char* ciphertext = malloc(ciphertext_len);
    if (!ciphertext) return NULL;
    for (int i = 0; i < ciphertext_len; i++)
        sscanf(hex_ciphertext + i * 2, "%2hhx", &ciphertext[i]);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { free(ciphertext); return NULL; }
    unsigned char* plaintext = malloc(ciphertext_len + 1);
    if (!plaintext) { free(ciphertext); EVP_CIPHER_CTX_free(ctx); return NULL; }
    int out_len;
    EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, aes_key, aes_iv);
    EVP_DecryptUpdate(ctx, plaintext, &out_len, ciphertext, ciphertext_len);
    int total_len = out_len;
    EVP_DecryptFinal_ex(ctx, plaintext + total_len, &out_len);
    total_len += out_len;
    plaintext[total_len] = '\0';
    EVP_CIPHER_CTX_free(ctx);
    free(ciphertext);
    return (char*)plaintext;
}

char* crypto_encrypt(const char* plaintext) {
    if (!crypto_ready || !plaintext) return NULL;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return NULL;
    int len = (int)strlen(plaintext);
    int ciphertext_len = len + 16;
    unsigned char* ciphertext = malloc(ciphertext_len);
    if (!ciphertext) { EVP_CIPHER_CTX_free(ctx); return NULL; }
    int out_len;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, aes_key, aes_iv);
    EVP_EncryptUpdate(ctx, ciphertext, &out_len, (unsigned char*)plaintext, len);
    int total_len = out_len;
    EVP_EncryptFinal_ex(ctx, ciphertext + total_len, &out_len);
    total_len += out_len;
    EVP_CIPHER_CTX_free(ctx);
    char* hex = malloc(total_len * 2 + 1);
    if (!hex) { free(ciphertext); return NULL; }
    for (int i = 0; i < total_len; i++)
        sprintf(hex + i * 2, "%02x", ciphertext[i]);
    hex[total_len * 2] = '\0';
    free(ciphertext);
    return hex;
}

/* ========== Чтение строки в UTF-8 ========== */
int read_line_utf8(char* buf, int size) {
    wchar_t wbuf[1024];
    DWORD chars_read = 0;
    HANDLE h_input = GetStdHandle(STD_INPUT_HANDLE);
    if (!ReadConsoleW(h_input, wbuf, 1024, &chars_read, NULL)) return 0;
    if (chars_read == 0) return 0;
    if (wbuf[chars_read - 1] == L'\n') { chars_read--; wbuf[chars_read] = 0; }
    if (wbuf[chars_read - 1] == L'\r') { chars_read--; wbuf[chars_read] = 0; }
    int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, chars_read, buf, size - 1, NULL, NULL);
    if (len <= 0) return 0;
    buf[len] = '\0';
    return 1;
}

SOCKET connect_to_server(const char* ip, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock); return INVALID_SOCKET;
    }
    printf("[CLIENT] Connected to %s:%d\n", ip, port);
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
    return sock;
}

int send_message(SOCKET sock, const char* message) {
    int len = (int)strlen(message);
    int total = 0;
    while (total < len) {
        int sent = send(sock, message + total, len - total, 0);
        if (sent == SOCKET_ERROR) return -1;
        total += sent;
    }
    return total;
}

int receive_message(SOCKET sock, char* buffer, int size) {
    int received = recv(sock, buffer, size - 1, 0);
    if (received == SOCKET_ERROR) return -1;
    if (received == 0) return 0;
    buffer[received] = '\0';
    return received;
}

DWORD WINAPI receiver_thread(LPVOID param) {
    (void)param;
    char buffer[BUFFER_SIZE];
    while (running) {
        int received = receive_message(server_sock, buffer, sizeof(buffer));
        if (received <= 0) { printf("\n[CLIENT] Server disconnected\n"); running = 0; exit(0); }
        char* line = strtok(buffer, "\n");
        while (line) {
            if (strlen(line) == 0) { line = strtok(NULL, "\n"); continue; }

            /* Расшифровка ENC: */
            if (strncmp(line, "ENC:", 4) == 0) {
                char* decrypted = crypto_decrypt(line + 4);
                if (decrypted) {
                    printf("\r%s\n> ", decrypted);
                    free(decrypted);
                }
                line = strtok(NULL, "\n");
                continue;
            }

            if (strncmp(line, "OK", 2) == 0 && strlen(line) <= 4)
                printf("\r[OK]\n> ");
            else if (strncmp(line, "ERROR", 5) == 0)
                printf("\r[Error] %s\n> ", line + 6);
            else if (strncmp(line, "LIST ", 5) == 0)
                printf("\rOnline: %s\n> ", line + 5);
            else if (strcmp(line, "HISTORY_BEGIN") == 0)
                printf("\n=== History ===\n");
            else if (strcmp(line, "HISTORY_END") == 0)
                printf("=== End ===\n> ");
            else if (strcmp(line, "HISTORY_EMPTY") == 0)
                printf("\n(empty)\n> ");
            else if (strcmp(line, "OFFLINE_BEGIN") == 0)
                printf("\n=== Missed messages ===\n");
            else if (strcmp(line, "OFFLINE_END") == 0)
                printf("=== End ===\n> ");
            else if (strcmp(line, "BYE") == 0)
            {
                printf("[Server] Bye!\n"); running = 0; exit(0);
            }
            else if (strncmp(line, "GROUP_", 6) == 0)
                printf("\r[%s]\n> ", line);
            else if (strncmp(line, "UNKNOWN", 7) == 0)
            { /* Игнорируем */
            }
            else
                printf("\r%s\n> ", line);
            fflush(stdout);
            line = strtok(NULL, "\n");
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    const char* server_ip = "127.0.0.1";
    if (argc > 1) server_ip = argv[1];

    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);

    crypto_init("messenger2026key");

    printf("[CLIENT] Connecting to %s...\n", server_ip);
    server_sock = connect_to_server(server_ip, SERVER_PORT);
    if (server_sock == INVALID_SOCKET) { WSACleanup(); return 1; }

    char username[256], password[256];
    int logged_in = 0;
    while (!logged_in) {
        printf("Login (/register username password): ");
        if (!read_line_utf8(username, sizeof(username))) break;
        if (strncmp(username, "/register ", 10) == 0) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "REGISTER %s\n", username + 10);
            send_message(server_sock, cmd);
            char resp[256];
            int r = receive_message(server_sock, resp, sizeof(resp));
            if (r > 0) { resp[strcspn(resp, "\n")] = '\0'; printf("[Server] %s\n", resp); }
            continue;
        }
        printf("Password: ");
        if (!read_line_utf8(password, sizeof(password))) break;
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "LOGIN %s %s\n", username, password);
        send_message(server_sock, cmd);
        char resp[256];
        int r = receive_message(server_sock, resp, sizeof(resp));
        if (r > 0) { resp[strcspn(resp, "\n")] = '\0'; printf("[Server] %s\n", resp); if (strncmp(resp, "OK", 2) == 0) logged_in = 1; }
    }

    HANDLE thread = CreateThread(NULL, 0, receiver_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);

    printf("\nCommands: /msg, /list, /quit, /help, /history\n");
    printf("  /group create/join/msg\n");

    char input[1024];
    while (running) {
        printf("> ");
        fflush(stdout);
        if (!read_line_utf8(input, sizeof(input))) break;
        if (strlen(input) == 0) continue;

        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0)
        {
            send_message(server_sock, "EXIT\n"); break;
        }
        else if (strcmp(input, "/list") == 0)
        {
            send_message(server_sock, "LIST\n");
        }
        else if (strncmp(input, "/msg ", 5) == 0) {
            char* space = strchr(input + 5, ' ');
            if (space) {
                *space = '\0';
                char* enc_text = crypto_encrypt(space + 1);
                if (enc_text) {
                    char cmd[1536];
                    snprintf(cmd, sizeof(cmd), "ENC:SEND %s %s\n", input + 5, enc_text);
                    send_message(server_sock, cmd);
                    free(enc_text);
                }
            }
        }
        else if (strcmp(input, "/history") == 0)
        {
            send_message(server_sock, "HISTORY\n");
        }
        else if (strcmp(input, "/help") == 0) {
            printf("Commands: /msg, /list, /quit, /help, /history\n");
            printf("  /group create/join/msg, /register\n");
        }
        else if (strncmp(input, "/group create ", 14) == 0) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "GROUP_CREATE %s\n", input + 14);
            send_message(server_sock, cmd);
        }
        else if (strncmp(input, "/group join ", 12) == 0) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "GROUP_JOIN %s\n", input + 12);
            send_message(server_sock, cmd);
        }
        else if (strncmp(input, "/group msg ", 11) == 0) {
            char* space = strchr(input + 11, ' ');
            if (space) {
                *space = '\0';
                char* enc_text = crypto_encrypt(space + 1);
                if (enc_text) {
                    char cmd[1536];
                    snprintf(cmd, sizeof(cmd), "ENC:GROUP_MSG %s %s\n", input + 11, enc_text);
                    send_message(server_sock, cmd);
                    free(enc_text);
                }
            }
        }
        else
        {
            printf("Unknown command. /help for help.\n");
        }
    }

    closesocket(server_sock);
    WSACleanup();
    return 0;
}