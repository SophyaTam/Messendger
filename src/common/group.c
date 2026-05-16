#include "group.h"
#include "auth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Массив групп в памяти и счётчик
static Group groups[MAX_GROUPS];
static int group_count = 0;
static sqlite3* db = NULL;      // Указатель на базу данных (передаётся из history.c)

// Инициализация: создаём таблицы в SQLite и загружаем группы в память
int group_init(sqlite3* database) {
    db = database;

    // Таблица групп: имя + хеш пароля
    const char* sql_groups =
        "CREATE TABLE IF NOT EXISTS groups ("
        "  name TEXT PRIMARY KEY,"
        "  password_hash TEXT NOT NULL"
        ");";

    // Таблица участников групп
    const char* sql_members =
        "CREATE TABLE IF NOT EXISTS group_members ("
        "  group_name TEXT NOT NULL,"
        "  username TEXT NOT NULL,"
        "  PRIMARY KEY (group_name, username)"
        ");";
   
    // Выполняем создание таблиц
    char* err = NULL;
    if (sqlite3_exec(db, sql_groups, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err);
        sqlite3_free(err);
        return -1;
    }
    if (sqlite3_exec(db, sql_members, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err);
        sqlite3_free(err);
        return -1;
    }

    // Загружаем все группы из базы в память
    const char* sql = "SELECT name, password_hash FROM groups;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            const char* hash = (const char*)sqlite3_column_text(stmt, 1);
            if (group_count < MAX_GROUPS) {
                strncpy(groups[group_count].name, name, sizeof(groups[group_count].name) - 1);
                strncpy(groups[group_count].password_hash, hash, sizeof(groups[group_count].password_hash) - 1);
                groups[group_count].member_count = 0;
                group_count++;
            }
        }
        sqlite3_finalize(stmt);
    }

    // Загружаем участников групп
    const char* sql_m = "SELECT group_name, username FROM group_members;";
    if (sqlite3_prepare_v2(db, sql_m, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* gname = (const char*)sqlite3_column_text(stmt, 0);
            const char* uname = (const char*)sqlite3_column_text(stmt, 1);
            // Ищем группу в памяти и добавляем участника
            for (int i = 0; i < group_count; i++) {
                if (strcmp(groups[i].name, gname) == 0 &&
                    groups[i].member_count < MAX_MEMBERS) {
                    strncpy(groups[i].members[groups[i].member_count], uname,
                        sizeof(groups[i].members[groups[i].member_count]) - 1);
                    groups[i].member_count++;
                    break;
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    printf("[GROUP] Loaded %d groups from database\n", group_count);
    return 0;
}

// Создать новую группу с паролем: 0 — успех, -1 — уже существует, -2 — нет места
int group_create(const char* name, const char* password, const char* creator) {
    // Проверяем, нет ли уже группы с таким именем
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) return -1;
    }
    if (group_count >= MAX_GROUPS) return -2;

    // Хешируем пароль
    char hash[65];
    sha256_hash(password, hash);

    // Сохраняем группу в базу
    const char* sql = "INSERT INTO groups (name, password_hash) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Добавляем создателя как первого участника
    const char* sql2 = "INSERT INTO group_members (group_name, username) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, creator, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Сохраняем в память
    strncpy(groups[group_count].name, name, sizeof(groups[group_count].name) - 1);
    strncpy(groups[group_count].password_hash, hash, sizeof(groups[group_count].password_hash) - 1);
    strncpy(groups[group_count].members[0], creator, sizeof(groups[group_count].members[0]) - 1);
    groups[group_count].member_count = 1;
    group_count++;

    printf("[GROUP] Created '%s' by %s\n", name, creator);
    return 0;
}

// Войти в группу (проверка пароля): 0 — успех, -1 — уже в группе, -2 — неверный пароль, -3 — группа полна, -4 — не найдена
int group_join(const char* name, const char* password, const char* username) {
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) {
            // Проверяем пароль
            char hash[65];
            sha256_hash(password, hash);
            if (strcmp(groups[i].password_hash, hash) != 0) return -2;

            // Проверяем, не участник ли уже
            for (int j = 0; j < groups[i].member_count; j++) {
                if (strcmp(groups[i].members[j], username) == 0) return -1;
            }
            if (groups[i].member_count >= MAX_MEMBERS) return -3;

            // Сохраняем в базу
            const char* sql = "INSERT INTO group_members (group_name, username) VALUES (?, ?);";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }

            // Добавляем в память
            strncpy(groups[i].members[groups[i].member_count], username,
                sizeof(groups[i].members[groups[i].member_count]) - 1);
            groups[i].member_count++;
            printf("[GROUP] %s joined '%s'\n", username, name);
            return 0;
        }
    }
    return -4;
}

// Получить список всех участников группы (строка через пробел)
char* group_list_members(const char* name) {
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) {
            char* result = malloc(512);
            result[0] = '\0';
            for (int j = 0; j < groups[i].member_count; j++) {
                if (j > 0) strcat(result, " ");
                strcat(result, groups[i].members[j]);
            }
            return result;
        }
    }
    return NULL;
}

// Проверить, состоит ли пользователь в группе
int group_is_member(const char* name, const char* username) {
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) {
            for (int j = 0; j < groups[i].member_count; j++) {
                if (strcmp(groups[i].members[j], username) == 0) return 1;
            }
        }
    }
    return 0;
}

// Получить список участников группы, кроме отправителя (для рассылки)
char* group_get_recipients(const char* name, const char* sender) {
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) {
            char* result = malloc(512);
            result[0] = '\0';
            // Собираем всех, кроме sender
            for (int j = 0; j < groups[i].member_count; j++) {
                if (strcmp(groups[i].members[j], sender) != 0) {
                    if (strlen(result) > 0) strcat(result, " ");
                    strcat(result, groups[i].members[j]);
                }
            }
            return result;
        }
    }
    return NULL;
}

// Очистка памяти (сбрасываем счётчик, группы остаются в базе)
void group_cleanup(void) {
    group_count = 0;
}