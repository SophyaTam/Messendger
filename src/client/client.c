#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "socket_utils.h"
#include "crypto.h"

static int server_fd; // Сокет соединения с сервером
static char last_sender[32] = ""; // Последний отправитель для /reply
static char last_message[1024] = ""; // Последнее сообщение для /forward

// Поток приёма сообщений от сервера
void* receiver_thread(void* arg) {
    (void)arg;
    char buffer[4096];
    int in_history = 0;

   //цикл для входа 
    while (1) {
        // Принимаем данные от сервера через сокет server_fd — файловый дескриптор сокета (номер соединения) buffer — массив, куда будут записаны принятые байты
        // sizeof(buffer) — максимальный размер буфера (4096 байт) received — количество реально принятых байт
        //   > 0  — успешно принято байт  = 0  — сервер закрыл соединение  < 0  — ошибка приёма
        int received = receive_message(server_fd, buffer, sizeof(buffer));
        if (received <= 0) {
            printf("\n[CLIENT] Server disconnected\n");
            exit(0);
        }
        // Добавляем символ конца строки '\0' в конец принятых данных, buffer можно использовать как обычную C-строку (printf, strtok и т.д.)
        buffer[received] = '\0';

        char* line = strtok(buffer, "\n");
        // Перебираем все строки, полученные от сервера
        while (line) {
            if (strlen(line) == 0) { line = strtok(NULL, "\n"); continue; } // Пропускаем пустые строки

            // Проверяем, зашифровано ли сообщение (префикс "ENC:")
            if (strncmp(line, "ENC:", 4) == 0) {
                // Расшифровываем данные после "ENC:" (hex -> текст)
                char* decrypted = crypto_decrypt(line + 4);
                if (decrypted) {
                    char* from_pos = strstr(decrypted, "[");  // Ищем символ '[' в расшифрованном тексте
                    // Пытаемся найти "[From " или "[От " 
                    if (from_pos && strncmp(from_pos, "[", 1) == 0) {
                        from_pos = strstr(decrypted, "[From ");
                        if (!from_pos) from_pos = strstr(decrypted, "[От ");
                        if (!from_pos) from_pos = strstr(decrypted, "[");
                    }
                    // Если это личное сообщение [From X] или [От X]
                    if (from_pos && (strncmp(from_pos, "[From ", 6) == 0 || strncmp(from_pos, "[От ", 4) == 0)) {
                        int prefix_len = (from_pos[1] == 'F') ? 6 : 4;  // Определяем длину префикса: 6 для "[From ", 4 для "[От "
                        char* sender_start = from_pos + prefix_len;  // Указатель на начало имени отправителя (после префикса)
                        char* sender_end = strchr(sender_start, ']');   // Ищем закрывающую скобку ']'
                        if (sender_end) {
                            int name_len = sender_end - sender_start;// Вычисляем длину имени
                            if (name_len > 31) name_len = 31;
                            memcpy(last_sender, sender_start, name_len); // Копируем имя отправителя для /reply
                            last_sender[name_len] = '\0';
                            int start = 0;
                            // Очищаем имя от мусорных символов в начале
                            while (start < name_len &&
                                !((last_sender[start] >= 'a' && last_sender[start] <= 'z') ||
                                    (last_sender[start] >= 'A' && last_sender[start] <= 'Z') ||
                                    (last_sender[start] >= '0' && last_sender[start] <= '9'))) {
                                start++;
                            }
                            // Убираем мусорные символы из начала имени
                            if (start > 0 && start < name_len) {
                                memmove(last_sender, last_sender + start, name_len - start + 1);
                            }
                            else if (start >= name_len) {
                                last_sender[0] = '\0';
                            }
                            // Сохраняем текст сообщения для /forward
                            char* msg_start = sender_end + 1; // Текст после ']'
                            if (*msg_start == ' ') msg_start++;  // Пропускаем пробел
                            strncpy(last_message, msg_start, sizeof(last_message) - 1);
                            last_message[sizeof(last_message) - 1] = '\0';
                        }
                    }
                    printf("\r\033[K%s\n> ", decrypted);  // Выводим расшифрованное сообщение на экран
                    free(decrypted);  // Освобождаем память
                }
                line = strtok(NULL, "\n"); // Следующая строка
                continue;
            }

            /* Уведомление о выключении сервера */
            if (strncmp(line, "SERVER_SHUTDOWN", 15) == 0) {
                printf("\r\033[K[Сервер] %s\n", line);
                fflush(stdout);
                exit(0);
            }
            // Начало блока истории — включаем режим
            if (strcmp(line, "HISTORY_BEGIN") == 0) {
                in_history = 1;
                printf("\n=== История переписки ===\n");
            }
            // Конец блока истории — выключаем режим
            else if (strcmp(line, "HISTORY_END") == 0) {
                in_history = 0;
                printf("=== Конец истории ===\n> ");
                fflush(stdout);
            }
            // История пуста
            else if (strcmp(line, "HISTORY_EMPTY") == 0) {
                printf("\n(История пуста)\n> ");
                fflush(stdout);
            }
            // Начало блока офлайн-сообщений — включаем режим
            if (strcmp(line, "OFFLINE_BEGIN") == 0) {
                in_history = 1;
                printf("\n=== Пропущенные сообщения ===\n");
            }
            // Конец блока офлайн-сообщений — выключаем режим
            else if (strcmp(line, "OFFLINE_END") == 0) {
                in_history = 0;
                printf("=== Конец ===\n> ");
                fflush(stdout);
            }
            // Если мы в режиме истории/офлайн — просто выводим строку
            else if (in_history) {
                printf("%s\n", line);
            }
            // Ответ на LIST — список онлайн-пользователей
            else if (strncmp(line, "LIST ", 5) == 0) {
                printf("\r\033[KOнлайн: %s\n> ", line + 5);
            }
            // OK или подтверждение групповых операций
            else if (strncmp(line, "OK", 2) == 0 || strncmp(line, "GROUP_CREATED", 13) == 0 || strncmp(line, "GROUP_JOINED", 12) == 0) {
                printf("\r\033[K[OK] %s\n> ", line);
            }
            // Ошибка от сервера
            else if (strncmp(line, "ERROR", 5) == 0) {
                printf("\r\033[K[Oшибка] %s\n> ", line + 6);
            }
            else if (strncmp(line, "UNKNOWN", 7) == 0) {
                /* Игнорируем */
            }
            // Любое другое сообщение — выводим как есть
            else {
                printf("\r\033[K[Сервер] %s\n> ", line);
            }
            fflush(stdout); //сбрасываем буфер
            line = strtok(NULL, "\n"); // следующая строка
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    printf("[CLIENT] Connecting...\n");
    const char* server_ip = "127.0.0.1";
    if (argc > 1) server_ip = argv[1];
    // Подключаемся к серверу по TCP 7777 — порт, на котором сервер слушает подключения
    // server_fd — файловый дескриптор сокета (целое число > 0)  через него клиент будет отправлять и принимать данные  если < 0 — подключение не удалось
    server_fd = connect_to_server(server_ip, 7777);
    if (server_fd < 0) {
        fprintf(stderr, "[CLIENT] Failed to connect\n");
        return 1;
    }
    // Инициализация шифрования AES-128 с общим ключом "messenger2026key" (16 символов = 128 бит), должен совпадать с ключом на сервере
    crypto_init("messenger2026key");

    char username[32], password[32];
    int logged_in = 0;

    while (!logged_in) {
        printf("Login (/register для создания аккаунта): ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = '\0';

        // Если пользователь хочет зарегистрироваться
        if (strncmp(username, "/register ", 10) == 0) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "REGISTER %s\n", username + 10);  // Формируем команду
            send_message(server_fd, cmd);  // Отправляем на сервер
            char resp[256];
            int r = receive_message(server_fd, resp, sizeof(resp));  // Ждём ответ
            if (r > 0) {
                resp[strcspn(resp, "\n")] = '\0';
                printf("[Сервер] %s\n", resp);    // Выводим результат
            }
            continue;
        }

        // Запрашиваем пароль
        printf("Password: ");
        fgets(password, sizeof(password), stdin);
        password[strcspn(password, "\n")] = '\0';

        // Формируем и отправляем команду LOGIN
        char command[128];
        snprintf(command, sizeof(command), "LOGIN %s %s\n", username, password);
        send_message(server_fd, command);

        // Ждём ответ сервера
        char response[256];
        int received = receive_message(server_fd, response, sizeof(response));
        if (received > 0) {
            response[strcspn(response, "\n")] = '\0';
            printf("[Сервер] %s\n", response);  // Выводим ответ
            if (strncmp(response, "OK", 2) == 0) {  // Если сервер ответил "OK" — вход успешен, выходим из цикла
                logged_in = 1;
            }
        }
    }

   // Создаём поток для приёма сообщений от сервера, recv_thread — идентификатор потока, receiver_thread — функция, которая будет выполняться в потоке
   // Поток работает параллельно с основным циклом ввода
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receiver_thread, NULL);

    printf("\nКоманды: /msg Имя Текст | /list | /quit | /help | /history\n");
    printf("  /group create Имя Пароль | /group join Имя Пароль | /group msg Имя Текст\n");
    printf("  /reply Текст | /forward Имя\n");

    char input[1024];// Буфер для ввода команд пользователя

    // Основной цикл: читаем команды с клавиатуры и отправляем на сервер
    while (1) {
        printf("> ");   // Приглашение для ввода
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break; // Читаем строку, если EOF — выход
        input[strcspn(input, "\n")] = '\0';
        if (strlen(input) == 0) continue;  // Пустая строка — пропускаем

        // --- Выход ---
        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            send_message(server_fd, "EXIT\n");    // Сообщаем серверу о выходе
            break;
        }

        // --- Список онлайн-пользователей ---
        else if (strcmp(input, "/list") == 0) {
            send_message(server_fd, "LIST\n");
        }

        // --- Отправка личного сообщения ---
        else if (strncmp(input, "/msg ", 5) == 0) {
            char* rest = input + 5;  // Указатель на "имя текст"
            char* space = strchr(rest, ' ');    // Ищем пробел между именем и текстом
            if (space) {
                *space = '\0';     // Разделяем строку: rest = имя, space+1 = текст
                char* recipient = rest;
                char* text = space + 1;
                char* enc_text = crypto_encrypt(text);   // Шифруем текст сообщения
                if (enc_text) {
                    char send_cmd[1280];
                    // Формируем команду ENC:SEND получатель шифротекст
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:SEND %s %s\n", recipient, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        // --- Ответ на последнее сообщение (/reply) ---
        else if (strncmp(input, "/reply ", 7) == 0) {
            if (strlen(last_sender) == 0) {
                printf("Нет сообщений для ответа.\n");
            }
            else {
                char* text = input + 7;
                char* enc_text = crypto_encrypt(text);
                if (enc_text) {
                    char send_cmd[1280];
                    // Отправляем тому, от кого было последнее сообщение
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:SEND %s %s\n", last_sender, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        // --- Ответ в ветку (/thread) ---
        else if (strncmp(input, "/thread ", 8) == 0) {
            char* rest = input + 8;   // "получатель текст"
            char* space = strchr(rest, ' ');
            if (space) {
                *space = '\0';
                char* recipient = rest;
                char* text = space + 1;
                char* enc_text = crypto_encrypt(text);
                if (enc_text) {
                    char send_cmd[1280];
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:THREAD %s %s\n", recipient, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        // --- Пересылка последнего сообщения (/forward) ---
        else if (strncmp(input, "/forward ", 9) == 0) {
            if (strlen(last_message) == 0) {
                printf("Нет сообщений для пересылки.\n");
            }
            else {
                char* recipient = input + 9;
                char fwd[1280];
                // Формируем пересылаемое сообщение с пометкой
                snprintf(fwd, sizeof(fwd), "[Переслано от %s] %s", last_sender, last_message);
                char* enc_text = crypto_encrypt(fwd);
                if (enc_text) {
                    char send_cmd[2560];
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:SEND %s %s\n", recipient, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        else if (strcmp(input, "/help") == 0) {
            printf("Команды:\n");
            printf("  /msg Имя Текст              — отправить личное сообщение\n");
            printf("  /reply Текст                — ответить на последнее сообщение\n");
            printf("  /forward Имя                — переслать последнее сообщение\n");
            printf("  /list                       — список онлайн-пользователей\n");
            printf("  /quit                       — выйти\n");
            printf("  /help                       — эта справка\n");
            printf("  /history                    — показать историю переписки\n");
            printf("  /group create Имя Пароль    — создать группу с паролем\n");
            printf("  /group join Имя Пароль      — войти в группу\n");
            printf("  /group msg Имя Текст        — сообщение в группу\n");
            printf("  /register Имя Пароль        — зарегистрироваться (до входа)\n");
            printf("  /thread ID Имя Текст        — ответить в ветку сообщения\n");
        }
        // История сообщений 
        else if (strcmp(input, "/history") == 0) {
            send_message(server_fd, "HISTORY\n");
        }
        // Создание группы 
        else if (strncmp(input, "/group create ", 14) == 0) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "GROUP_CREATE %s\n", input + 14);
            send_message(server_fd, cmd);
        }
        //Вход в группу 
        else if (strncmp(input, "/group join ", 12) == 0) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "GROUP_JOIN %s\n", input + 12);
            send_message(server_fd, cmd);
        }
        //Сообщение в группу 
        else if (strncmp(input, "/group msg ", 11) == 0) {
            char* rest = input + 11;
            char* space = strchr(rest, ' ');
            if (space) {
                *space = '\0';
                char* group_name = rest;
                char* text = space + 1;
                char* enc_text = crypto_encrypt(text);
                if (enc_text) {
                    char send_cmd[1280];
                    snprintf(send_cmd, sizeof(send_cmd), "ENC:GROUP_MSG %s %s\n", group_name, enc_text);
                    send_message(server_fd, send_cmd);
                    free(enc_text);
                }
            }
        }
        //Неизвестнаякоманда
        else {
            printf("Неизвестная команда. /help для справки.\n");
        }
    }

    close_socket(server_fd); //Закрываем соединение с сервером
    printf("[CLIENT] Done.\n");
    return 0;
}