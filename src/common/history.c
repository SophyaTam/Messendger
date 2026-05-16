#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

static sqlite3* db = NULL; // Указатель на базу данных SQLite

// Инициализация базы данных: создаём таблицы messages и events
int history_init(const char* db_filename) {
    // Открываем (или создаём) файл базы данных
    int rc = sqlite3_open(db_filename, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // Таблица сообщений: id, отправитель, получатель, текст, время, статус доставки, родитель (для тредов)
    const char* sql_messages =
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sender TEXT NOT NULL,"
        "  receiver TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  timestamp TEXT NOT NULL,"
        "  delivered INTEGER DEFAULT 1,"        // 1 = доставлено, 0 = офлайн
        "  parent_id INTEGER DEFAULT 0"         // 0 = корневое сообщение, >0 = ответ в тред
        ");";

    char* err = NULL;
    rc = sqlite3_exec(db, sql_messages, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    // Таблица событий: входы, выходы, старт/стоп сервера
    const char* sql_events =
        "CREATE TABLE IF NOT EXISTS events ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type TEXT NOT NULL,"      // SERVER_START, USER_LOGIN, USER_LOGOUT, SERVER_STOP
        "  username TEXT,"      // Имя пользователя (может быть NULL)
        "  data TEXT,"          // Дополнительные данные 
        "  timestamp TEXT NOT NULL"
        ");";
    rc = sqlite3_exec(db, sql_events, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    printf("[HISTORY] Database initialized: %s\n", db_filename);
    return 0;
}

// Сохранить сообщение в историю (delivered = 1, parent_id = 0)
void history_save(const char* sender, const char* receiver, const char* message) {
    if (!db) return;

    // Получаем текущее время
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    // SQL-запрос с параметрами (защита от SQL-инъекций)
    const char* sql = "INSERT INTO messages (sender, receiver, message, timestamp) "
        "VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    // Подставляем параметры
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, time_str, -1, SQLITE_STATIC);

    // Выполняем запрос
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "Insert error: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);     // Освобождаем prepared statement
}

// Записать событие в базу (вход, выход, старт/стоп сервера)
void history_log_event(const char* type, const char* username, const char* data) {
    if (!db) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    const char* sql = "INSERT INTO events (type, username, data, timestamp) "
        "VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username ? username : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, data ? data : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, time_str, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// Получить историю сообщений пользователя возвращаем строку с форматированной историей (нужно free())
char* history_get(const char* user1, const char* user2) {
    if (!db) return NULL;

    // Все сообщения, где пользователь — отправитель или получатель
    const char* sql =
        "SELECT id, sender, message, timestamp, parent_id FROM messages "
        "WHERE sender = ?1 OR receiver = ?1 "
        "ORDER BY id ASC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, user1, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user2, -1, SQLITE_STATIC);

    char* result = malloc(4096);
    if (!result) {
        sqlite3_finalize(stmt);
        return NULL;
    }
    result[0] = '\0';

    // Форматируем каждую строку
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* sender = (const char*)sqlite3_column_text(stmt, 1);
        const char* message = (const char*)sqlite3_column_text(stmt, 2);
        (void)sqlite3_column_text(stmt, 3);  // timestamp не используется в выводе
        int parent_id = sqlite3_column_int(stmt, 4);

        char line[1024];
        if (parent_id > 0)
            snprintf(line, sizeof(line), "  [%d → %d] %s: %s\n", id, parent_id, sender, message); // Сообщение в треде — показываем с отступом и ссылкой на родителя
        else
            snprintf(line, sizeof(line), "[%d] %s: %s\n", id, sender, message);  // Обычное сообщение

        if (strlen(result) + strlen(line) < 4000) strcat(result, line);
    }

    sqlite3_finalize(stmt);
    return result;
}

// Сохранить офлайн-сообщение (delivered = 0)
void history_save_offline(const char* sender, const char* receiver, const char* message) {
    if (!db) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    const char* sql = "INSERT INTO messages (sender, receiver, message, timestamp, delivered) "
        "VALUES (?, ?, ?, ?, 0);";   // delivered = 0 — офлайн
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, time_str, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// Получить офлайн-сообщения для пользователя и отметить их как доставленные
char* history_get_offline(const char* username) {
    if (!db) return NULL;

    // Ищем сообщения с delivered = 0 для этого получателя
    const char* sql = "SELECT id, sender, message, timestamp FROM messages "
        "WHERE receiver = ? AND delivered = 0 "
        "ORDER BY id ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    char* result = malloc(4096);
    if (!result) { sqlite3_finalize(stmt); return NULL; }
    result[0] = '\0';

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* sender = (const char*)sqlite3_column_text(stmt, 1);
        const char* message = (const char*)sqlite3_column_text(stmt, 2);
        const char* timestamp = (const char*)sqlite3_column_text(stmt, 3);

        char line[1024];
        snprintf(line, sizeof(line), "[%s] [From %s] %s\n", timestamp, sender, message);

        if (strlen(result) + strlen(line) < 4000) {
            strcat(result, line);
        }

        // Отмечаем как доставленное
        const char* update = "UPDATE messages SET delivered = 1 WHERE id = ?;";
        sqlite3_stmt* ustmt;
        if (sqlite3_prepare_v2(db, update, -1, &ustmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(ustmt, 1, id);
            sqlite3_step(ustmt);
            sqlite3_finalize(ustmt);
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

// Сохранить сообщение в тред (с parent_id)
void history_save_thread(const char* sender, const char* receiver, const char* message, int parent_id) {
    if (!db) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    const char* sql = "INSERT INTO messages (sender, receiver, message, timestamp, parent_id) "
        "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, time_str, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, parent_id);       // ID родительского сообщения
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}


// Закрыть базу данных
void history_close(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
        printf("[HISTORY] Database closed\n");
    }
}

// Вернуть указатель на базу для использования в других модулях (group.c)
sqlite3* history_get_db(void) {
    return db;
}