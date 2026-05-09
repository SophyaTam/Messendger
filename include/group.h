#ifndef GROUP_H
#define GROUP_H

#include <sqlite3.h>

#define MAX_GROUPS 10
#define MAX_MEMBERS 10

typedef struct {
    char name[32];
    char password_hash[65];
    char members[MAX_MEMBERS][32];
    int member_count;
} Group;

int group_init(sqlite3* db);
int group_create(const char* name, const char* password, const char* creator);
int group_join(const char* name, const char* password, const char* username);
char* group_list_members(const char* name);
int group_is_member(const char* name, const char* username);
char* group_get_recipients(const char* name, const char* sender);
void group_cleanup(void);

#endif