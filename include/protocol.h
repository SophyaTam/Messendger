#ifndef PROTOCOL_H
#define PROTOCOL_H

/* Парсинг команды SEND: извлекает получателя и текст */
int protocol_parse_send(const char* buffer, char* recipient, char* message);

/* Формирование ответа сервера */
void protocol_make_ok(char* out);
void protocol_make_error(char* out, const char* reason);
void protocol_make_user_list(char* out, const char* users);

#endif