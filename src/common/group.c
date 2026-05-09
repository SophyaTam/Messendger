#include "group.h"
#include "auth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static Group groups[MAX_GROUPS];
static int group_count = 0;
static sqlite3* db = NULL;

int group_init(sqlite3* database) {
    db = database;

    const char* sql_groups =
        "CREATE TABLE IF NOT EXISTS groups ("
        "  name TEXT PRIMARY KEY,"
        "  password_hash TEXT NOT NULL"
        ");";

    const char* sql_members =
        "CREATE TABLE IF NOT EXISTS group_members ("
        "  group_name TEXT NOT NULL,"
        "  username TEXT NOT NULL,"
        "  PRIMARY KEY (group_name, username)"
        ");";

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

    const char* sql_m = "SELECT group_name, username FROM group_members;";
    if (sqlite3_prepare_v2(db, sql_m, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* gname = (const char*)sqlite3_column_text(stmt, 0);
            const char* uname = (const char*)sqlite3_column_text(stmt, 1);
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

int group_create(const char* name, const char* password, const char* creator) {
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) return -1;
    }
    if (group_count >= MAX_GROUPS) return -2;

    char hash[65];
    sha256_hash(password, hash);

    const char* sql = "INSERT INTO groups (name, password_hash) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    const char* sql2 = "INSERT INTO group_members (group_name, username) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, creator, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    strncpy(groups[group_count].name, name, sizeof(groups[group_count].name) - 1);
    strncpy(groups[group_count].password_hash, hash, sizeof(groups[group_count].password_hash) - 1);
    strncpy(groups[group_count].members[0], creator, sizeof(groups[group_count].members[0]) - 1);
    groups[group_count].member_count = 1;
    group_count++;

    printf("[GROUP] Created '%s' by %s\n", name, creator);
    return 0;
}

int group_join(const char* name, const char* password, const char* username) {
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) {
            char hash[65];
            sha256_hash(password, hash);
            if (strcmp(groups[i].password_hash, hash) != 0) return -2;

            for (int j = 0; j < groups[i].member_count; j++) {
                if (strcmp(groups[i].members[j], username) == 0) return -1;
            }
            if (groups[i].member_count >= MAX_MEMBERS) return -3;

            const char* sql = "INSERT INTO group_members (group_name, username) VALUES (?, ?);";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }

            strncpy(groups[i].members[groups[i].member_count], username,
                sizeof(groups[i].members[groups[i].member_count]) - 1);
            groups[i].member_count++;
            printf("[GROUP] %s joined '%s'\n", username, name);
            return 0;
        }
    }
    return -4;
}

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

char* group_get_recipients(const char* name, const char* sender) {
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) {
            char* result = malloc(512);
            result[0] = '\0';
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

void group_cleanup(void) {
    group_count = 0;
}