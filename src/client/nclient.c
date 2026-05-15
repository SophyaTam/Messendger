/*
 * nclient.c — Ncurses messenger client
 * Build: gcc nclient.c ../common/socket_utils.c ../common/crypto.c -o nclient -lncursesw -lssl -lcrypto -I../../include
 */

#include <ncurses.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "../../include/socket_utils.h"
#include "../../include/crypto.h"

#define PORT 7777           // Порт сервера
#define MAX_MSG 200         // Максимум хранимых сообщений в истории чата
#define MSG_LEN 256         // Максимальная длина одного сообщения

static int server_fd;       // Сокет соединения с сервером
static int running = 1;     // Флаг работы клиента (0 = выход)
static WINDOW* chat_win, * input_win, * help_win;   // Окна ncurses: чат, поле ввода, строка подсказки
// Кольцевой буфер сообщений для отображения
static char messages[MAX_MSG][MSG_LEN];
static int msg_count = 0;            // Количество сообщений
static int selected_msg = -1;        // Индекс выбранного (подсвеченного) сообщения
static char input_buf[256];          // Буфер ввода команды
static int input_pos = 0;            // Текущая позиция в буфере ввода
static int scroll_pos = 0;           // Позиция скролла окна чата

// Добавить сообщение в историю чата
void add_message(const char* text) {
    if (msg_count < MAX_MSG && strlen(text) > 0) {
        strncpy(messages[msg_count], text, MSG_LEN - 1);
        messages[msg_count][MSG_LEN - 1] = '\0';
        msg_count++;
        selected_msg = msg_count - 1;       // Авто-выбор последнего сообщения
        scroll_pos = msg_count - 1;          // Скролл вниз
    }
}

// Перерисовать окно чата с учётом позиции скролла
void redraw_chat() {
    int ch, cw;
    getmaxyx(chat_win, ch, cw);     // Размеры окна чата
    // Корректируем позицию скролла
    if (scroll_pos > msg_count - ch) scroll_pos = msg_count - ch;
    if (scroll_pos < 0) scroll_pos = 0;
    werase(chat_win);
    // Выводим видимые сообщения
    for (int i = 0; i < ch && i + scroll_pos < msg_count; i++) {
        int idx = i + scroll_pos;
        if (idx == selected_msg) wattron(chat_win, A_REVERSE);      // Подсветка выбранного
        mvwprintw(chat_win, i, 0, "%-*s", cw, messages[idx]);
        if (idx == selected_msg) wattroff(chat_win, A_REVERSE);
    }
    wrefresh(chat_win);
}

// Поток приёма сообщений от сервера
void* receiver_thread(void* arg) {
    (void)arg;
    char buffer[4096];
    while (running) {
        int received = receive_message(server_fd, buffer, sizeof(buffer));
        if (received <= 0) { running = 0; break; }
        buffer[received] = '\0';
        // Разбиваем ответ на строки
        char* line = strtok(buffer, "\n");
        while (line) {
            if (strlen(line) == 0) { line = strtok(NULL, "\n"); continue; }
            // Расшифровка зашифрованных сообщений
            if (strncmp(line, "ENC:", 4) == 0) {
                char* dec = crypto_decrypt(line + 4);
                if (dec) { add_message(dec); free(dec); }
                line = strtok(NULL, "\n"); continue;
            }
            // Пропускаем служебные маркеры
            if (strcmp(line, "HISTORY_BEGIN") == 0 || strcmp(line, "HISTORY_END") == 0 ||
                strcmp(line, "HISTORY_EMPTY") == 0 || strcmp(line, "OFFLINE_BEGIN") == 0 ||
                strcmp(line, "OFFLINE_END") == 0 || strcmp(line, "BYE") == 0 ||
                strcmp(line, "UNKNOWN Unknown command") == 0 ||
                (strncmp(line, "OK", 2) == 0 && strlen(line) <= 4)) {
                line = strtok(NULL, "\n"); continue;
            }
            // Обычное сообщение — добавляем в чат
            add_message(line);
            line = strtok(NULL, "\n");
        }
        redraw_chat();
        wrefresh(input_win);
    }
    return NULL;
}

int main() {
    // Подключение к серверу
    printf("Connecting...\n");
    server_fd = connect_to_server("127.0.0.1", PORT);
    if (server_fd < 0) { fprintf(stderr, "Failed to connect\n"); return 1; }
    crypto_init("messenger2026key");  // Инициализация шифрования

    // Инициализация ncurses
    setlocale(LC_ALL, "");
    initscr();                               // Запуск curses-режима
    noecho();                                // Не показывать вводимые символы
    keypad(stdscr, TRUE);                    // Включить спецклавиши (F1-F10, стрелки)
    curs_set(0);                             // Скрыть курсор
    start_color();                           // Включить цвета
    init_pair(1, COLOR_CYAN, COLOR_BLACK);   // Цвет заголовка
    init_pair(2, COLOR_YELLOW, COLOR_BLACK); // Цвет подсказки

    int h, w;
    getmaxyx(stdscr, h, w);     // Размеры терминала

    /* LOGIN Экран */
    char login[32] = "", pass[32] = "";
    int login_pos = 0, pass_pos = 0, active_field = 0;  // 0=логин, 1=пароль, 2=войти, 3=регистрация
    char status_msg[128] = "";          // Сообщение об ошибке

    while (1) {
        werase(stdscr);             // Очистка экрана
        // Заголовок
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(2, (w - 20) / 2, "MESSENGER v2.0");
        attroff(A_BOLD | COLOR_PAIR(1));
        // Поле логина
        mvprintw(5, (w - 30) / 2, "Login:");
        if (active_field == 0) { attron(A_REVERSE); mvprintw(5, (w - 30) / 2 + 7, "%-20s", login); attroff(A_REVERSE); }
        else mvprintw(5, (w - 30) / 2 + 7, "%-20s", login);
        // Поле пароля (звёздочки)
        mvprintw(7, (w - 30) / 2, "Password:");
        char masked[33]; memset(masked, '*', pass_pos); masked[pass_pos] = '\0';
        if (active_field == 1) { attron(A_REVERSE); mvprintw(7, (w - 30) / 2 + 10, "%-20s", masked); attroff(A_REVERSE); }
        else mvprintw(7, (w - 30) / 2 + 10, "%-20s", masked);
        // Кнопка Войти
        if (active_field == 2) attron(A_REVERSE);
        mvprintw(9, (w - 14) / 2, "[  Login  ]"); attroff(A_REVERSE);
        // Кнопка Регистрация
        if (active_field == 3) attron(A_REVERSE);
        mvprintw(11, (w - 18) / 2, "[  Register  ]"); attroff(A_REVERSE);
        // Статус и подсказка
        mvprintw(14, (w - strlen(status_msg)) / 2, "%s", status_msg);
        mvprintw(h - 2, (w - 42) / 2, "Tab/Arrows - switch | Enter - select | Esc - quit");
        refresh();
        // Обработка ввода на экране логина
        int ch = getch();
        if (ch == 27) { endwin(); return 0; }       // Esc — выход
        if (ch == '\t' || ch == KEY_DOWN) active_field = (active_field + 1) % 4;        // Tab/вниз
        else if (ch == KEY_UP) active_field = (active_field - 1 + 4) % 4;                // Вверх
        else if (ch == '\n') {
            if (active_field == 2) {        // Кнопка Войти
                char cmd[128]; snprintf(cmd, sizeof(cmd), "LOGIN %s %s\n", login, pass);
                send_message(server_fd, cmd);
                char resp[256]; receive_message(server_fd, resp, sizeof(resp)); resp[strcspn(resp, "\n")] = '\0';
                if (strncmp(resp, "OK", 2) == 0) break;         // Успех — выход из цикла логина
                else snprintf(status_msg, sizeof(status_msg), "Error: %s", resp);
            }
            else if (active_field == 3 && strlen(login) > 0 && strlen(pass) > 0) {  // Регистрация
                char cmd[128]; snprintf(cmd, sizeof(cmd), "REGISTER %s %s\n", login, pass);
                send_message(server_fd, cmd);
                char resp[256]; receive_message(server_fd, resp, sizeof(resp)); resp[strcspn(resp, "\n")] = '\0';
                snprintf(status_msg, sizeof(status_msg), "%s", resp);
            }
        }
        // Ввод символов в поля
        else if (active_field == 0 && (ch == KEY_BACKSPACE || ch == 127)) { if (login_pos > 0) login[--login_pos] = '\0'; }
        else if (active_field == 0 && ch >= 32 && ch < 127 && login_pos < 31) { login[login_pos++] = ch; login[login_pos] = '\0'; }
        else if (active_field == 1 && (ch == KEY_BACKSPACE || ch == 127)) { if (pass_pos > 0) pass[--pass_pos] = '\0'; }
        else if (active_field == 1 && ch >= 32 && ch < 127 && pass_pos < 31) { pass[pass_pos++] = ch; pass[pass_pos] = '\0'; }
    }

    /* CHAT */
    werase(stdscr); refresh();
    // Окно чата (вся верхняя часть)
    chat_win = newwin(h - 4, w, 0, 0);
    scrollok(chat_win, TRUE);
    // Строка подсказки
    help_win = newwin(1, w, h - 4, 0);
    wattron(help_win, COLOR_PAIR(2));
    mvwprintw(help_win, 0, 0, "F1:Reply F2:Fwd F3:Thread F5:Hist F6:Online F7:GrpCr F8:GrpJn F9:GrpMsg F10/Ctrl+C:Quit Ctrl+U/D:Scroll");
    wattroff(help_win, COLOR_PAIR(2));
    wrefresh(help_win);
    // Поле ввода
    input_win = newwin(3, w, h - 3, 0);
    keypad(input_win, TRUE);
    box(input_win, 0, 0); mvwprintw(input_win, 1, 2, "> "); wrefresh(input_win);
    redraw_chat();

    // Запуск потока приёма сообщений
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receiver_thread, NULL);
    input_pos = 0; input_buf[0] = '\0'; selected_msg = -1;

    // Главный цикл чата
    while (running) {
        int ch = wgetch(input_win);         // Читаем клавишу из поля ввода
        switch (ch) {
        case KEY_F(10): case 3: send_message(server_fd, "EXIT\n"); running = 0; break;  // Выход: F10 или Ctrl+C
        case KEY_F(5):  send_message(server_fd, "HISTORY\n"); break;  // F5 — история сообщений
        case 21: scroll_pos -= (h - 4); if (scroll_pos < 0) scroll_pos = 0; redraw_chat(); break;  // Ctrl+U — скролл вверх на страницу
        case 4:  scroll_pos += (h - 4); if (scroll_pos > msg_count - 1) scroll_pos = msg_count - 1; redraw_chat(); break;  // Ctrl+D — скролл вниз на страницу
        case KEY_F(1): strcpy(input_buf, "/reply "); input_pos = 7; werase(input_win); box(input_win, 0, 0); mvwprintw(input_win, 1, 2, "> %s", input_buf); break;  // F1 — ответить (вставляет "/reply ")
        case KEY_F(2): strcpy(input_buf, "/forward "); input_pos = 9; werase(input_win); box(input_win, 0, 0); mvwprintw(input_win, 1, 2, "> %s", input_buf); break;   // F2 — переслать (вставляет "/forward ")
        case KEY_F(3): strcpy(input_buf, "/thread "); input_pos = 8; werase(input_win); box(input_win, 0, 0); mvwprintw(input_win, 1, 2, "> %s", input_buf); break;     // F3 — тред (вставляет "/thread ")
        case KEY_F(7): strcpy(input_buf, "/group create "); input_pos = 14; werase(input_win); box(input_win, 0, 0); mvwprintw(input_win, 1, 2, "> %s", input_buf); break;       // F7 — создать группу
        case KEY_F(8): strcpy(input_buf, "/group join "); input_pos = 12; werase(input_win); box(input_win, 0, 0); mvwprintw(input_win, 1, 2, "> %s", input_buf); break;        // F8 — войти в группу
        case KEY_F(9): strcpy(input_buf, "/group msg "); input_pos = 11; werase(input_win); box(input_win, 0, 0); mvwprintw(input_win, 1, 2, "> %s", input_buf); break;          // F9 — сообщение в группу
        case '\n':   // Enter — отправить команду
            if (input_pos > 0) {
                input_buf[input_pos] = '\0';
                char send_buf[1280];
                // /msg — личное сообщение (шифруется)
                if (strncmp(input_buf, "/msg ", 5) == 0) {
                    char* sp = strchr(input_buf + 5, ' ');
                    if (sp) {
                        *sp = '\0'; char* enc = crypto_encrypt(sp + 1);
                        if (enc) {
                            snprintf(send_buf, sizeof(send_buf), "ENC:SEND %s %s\n", input_buf + 5, enc); send_message(server_fd, send_buf); free(enc);
                            char disp[MSG_LEN]; snprintf(disp, MSG_LEN, "[You -> %s] %s", input_buf + 5, sp + 1); add_message(disp); redraw_chat();
                        }
                    }
                }
                // /reply — ответ на выбранное сообщение
                else if (strncmp(input_buf, "/reply ", 7) == 0) {
                    char* enc = crypto_encrypt(input_buf + 7);
                    if (enc && selected_msg >= 0) {
                        char* m = messages[selected_msg]; char sender[32] = ""; char* br = strrchr(m, ']');
                        if (br && br > m + 1) {
                            char* last_space = NULL;
                            for (char* p = m; p < br; p++) if (*p == ' ') last_space = p;
                            if (last_space && last_space + 1 < br) {
                                int len = br - last_space - 1;
                                if (len > 0 && len < 31) { strncpy(sender, last_space + 1, len); sender[len] = '\0'; }
                            }
                        }
                        if (sender[0]) {
                            snprintf(send_buf, sizeof(send_buf), "ENC:SEND %s %s\n", sender, enc); send_message(server_fd, send_buf);
                            char disp[MSG_LEN]; snprintf(disp, MSG_LEN, "[You -> %s] %s", sender, input_buf + 7); add_message(disp); redraw_chat();
                        }
                        free(enc);
                    }
                    else if (enc) free(enc);
                }
                // /group create — создать группу
                else if (strncmp(input_buf, "/group create ", 14) == 0) {
                    char cmd[256];
                    snprintf(cmd, sizeof(cmd), "GROUP_CREATE %s\n", input_buf + 14);
                    send_message(server_fd, cmd);
                }
                // /group join — войти в группу
                else if (strncmp(input_buf, "/group join ", 12) == 0) {
                    char cmd[256];
                    snprintf(cmd, sizeof(cmd), "GROUP_JOIN %s\n", input_buf + 12);
                    send_message(server_fd, cmd);
                }
                // /thread — ответ в ветку
                else if (strncmp(input_buf, "/thread ", 8) == 0) {
                    char* recip = input_buf + 8; char* sp = strchr(recip, ' ');
                    if (sp) {
                        *sp = '\0'; char* enc = crypto_encrypt(sp + 1);
                        if (enc) {
                            snprintf(send_buf, sizeof(send_buf), "ENC:THREAD %s %s\n", recip, enc); send_message(server_fd, send_buf); free(enc);
                            char disp[MSG_LEN]; snprintf(disp, MSG_LEN, "[Thread -> %s] %s", recip, sp + 1); add_message(disp); redraw_chat();
                        }
                    }
                }
                // /forward — переслать выбранное сообщение
                else if (strncmp(input_buf, "/forward ", 9) == 0) {
                    if (selected_msg >= 0) {
                        char* m = messages[selected_msg]; char* br = strrchr(m, ']'); char* msg_text = br ? br + 2 : m;
                        char fwd[1280]; snprintf(fwd, sizeof(fwd), "[Fwd] %s", msg_text); char* enc = crypto_encrypt(fwd);
                        if (enc) {
                            snprintf(send_buf, sizeof(send_buf), "ENC:SEND %s %s\n", input_buf + 9, enc); send_message(server_fd, send_buf); free(enc);
                            char disp[MSG_LEN]; snprintf(disp, MSG_LEN, "[You -> %s] %s", input_buf + 9, fwd); add_message(disp); redraw_chat();
                        }
                    }
                }
                // /group msg — сообщение в группу
                else if (strncmp(input_buf, "/group msg ", 11) == 0) {
                    char* rest = input_buf + 11; char* sp = strchr(rest, ' ');
                    if (sp) {
                        *sp = '\0'; char* enc = crypto_encrypt(sp + 1);
                        if (enc) {
                            snprintf(send_buf, sizeof(send_buf), "ENC:GROUP_MSG %s %s\n", rest, enc); send_message(server_fd, send_buf); free(enc);
                            char disp[MSG_LEN]; snprintf(disp, MSG_LEN, "[Group -> %s] %s", rest, sp + 1); add_message(disp); redraw_chat();
                        }
                    }
                }  // Любая другая команда — апперкейс первых двух слов и отправка
                else if (input_buf[0] == '/') {
                    char cmd_upper[256];
                    strncpy(cmd_upper, input_buf + 1, sizeof(cmd_upper) - 1);
                    cmd_upper[sizeof(cmd_upper) - 1] = '\0';
                    char* sp = cmd_upper;
                    for (int w = 0; w < 2; w++) {
                        while (*sp == ' ') sp++;
                        while (*sp && *sp != ' ') { if (*sp >= 'a' && *sp <= 'z') *sp -= 32; sp++; }
                    }
                    snprintf(send_buf, sizeof(send_buf), "%s\n", cmd_upper);
                    send_message(server_fd, send_buf);
                }
                // Очистка поля ввода
                input_pos = 0; input_buf[0] = '\0';
                werase(input_win); box(input_win, 0, 0); mvwprintw(input_win, 1, 2, "> ");
            }
            break;
            // Backspace — удалить символ
        case KEY_BACKSPACE: case 127:
            if (input_pos > 0) { input_pos--; mvwprintw(input_win, 1, input_pos + 4, " "); wmove(input_win, 1, input_pos + 4); }
            break;
            // Обычные символы — добавить в буфер ввода
        default:
            if (ch >= 32 && input_pos < 240) { input_buf[input_pos++] = ch; input_buf[input_pos] = '\0'; mvwprintw(input_win, 1, 4, "%s", input_buf); }
        }
        wrefresh(input_win);
    }
    // Завершение работы
    endwin();
    close_socket(server_fd);
    printf("Done.\n");
    return 0;
}