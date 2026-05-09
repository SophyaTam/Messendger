#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>

typedef struct {
    char username[32];
    char hash[65];
} User;

static User *users = NULL;
static int user_count = 0;

void sha256_hash(const char *input, char *output_hex) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)input, strlen(input), hash);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output_hex + (i * 2), "%02x", hash[i]);
    }
    output_hex[64] = '\0';
}

int auth_init(const char *passwd_file) {
    FILE *f = fopen(passwd_file, "r");
    if (!f) {
        printf("[AUTH] Cannot open passwd file: %s\n", passwd_file);
        return -1;
    }

    user_count = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strlen(line) > 1) user_count++;
    }
    rewind(f);

    users = malloc(sizeof(User) * user_count);
    if (!users) {
        fclose(f);
        return -1;
    }

    int i = 0;
    while (fgets(line, sizeof(line), f) && i < user_count) {
        line[strcspn(line, "\n")] = '\0';
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        strncpy(users[i].username, line, sizeof(users[i].username) - 1);
        sha256_hash(colon + 1, users[i].hash);
        printf("[AUTH DEBUG] user=%s pass=%s hash=%s\n", users[i].username, colon + 1, users[i].hash);
        i++;
    }

    fclose(f);
    printf("[AUTH] Loaded %d users from %s\n", user_count, passwd_file);
    return 0;
}

int auth_check(const char *username, const char *password) {
    char hash[65];
    sha256_hash(password, hash);

    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            if (strcmp(users[i].hash, hash) == 0) {
                return 1;
            } else {
                return 0;
            }
        }
    }
    return -1;
}

void auth_cleanup(void) {
    if (users) {
        free(users);
        users = NULL;
        user_count = 0;
    }
}
