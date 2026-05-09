#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

static sqlite3* db = NULL;

int history_init(const char* db_filename) {
    int rc = sqlite3_open(db_filename, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sender TEXT NOT NULL,"
        "  receiver TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  timestamp TEXT NOT NULL"
        ");";

    char* err = NULL;
    rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    printf("[HISTORY] Database initialized: %s\n", db_filename);
    return 0;
}

void history_save(const char* sender, const char* receiver, const char* message) {
    if (!db) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    const char* sql = "INSERT INTO messages (sender, receiver, message, timestamp) "
        "VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, time_str, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "Insert error: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
}

char* history_get(const char* user1, const char* user2) {
    if (!db) return NULL;

    const char* sql =
        "SELECT sender, message, timestamp FROM messages "
        "WHERE (sender = ? AND receiver = ?) "
        "   OR (sender = ? AND receiver = ?) "
        "ORDER BY id ASC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, user1, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user2, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user2, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user1, -1, SQLITE_STATIC);

    char* result = malloc(4096);
    if (!result) {
        sqlite3_finalize(stmt);
        return NULL;
    }
    result[0] = '\0';

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* sender = (const char*)sqlite3_column_text(stmt, 0);
        const char* message = (const char*)sqlite3_column_text(stmt, 1);
        const char* timestamp = (const char*)sqlite3_column_text(stmt, 2);

        char line[1024];
        snprintf(line, sizeof(line), "[%s] %s: %s\n", timestamp, sender, message);

        if (strlen(result) + strlen(line) < 4000) {
            strcat(result, line);
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

void history_close(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
        printf("[HISTORY] Database closed\n");
    }
}

sqlite3* history_get_db(void) {
    return db;
}