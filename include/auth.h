#ifndef AUTH_H
#define AUTH_H

int auth_init(const char *passwd_file);
int auth_check(const char *username, const char *password);
void sha256_hash(const char *input, char *output_hex);
void auth_cleanup(void);

#endif
