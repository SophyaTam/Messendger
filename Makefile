# Компилятор и флаги
CC = gcc		# Компилятор C
CFLAGS = -Wall -Wextra -pthread -I./include   # Предупреждения, потоки, путь к заголовкам
LDFLAGS = -lssl -lcrypto -lsqlite3    # Библиотеки: OpenSSL, SQLite

# Общие исходники (используются и сервером, и клиентом)
COMMON_SRC = src/common/socket_utils.c src/common/protocol.c src/common/logger.c src/common/auth.c src/common/history.c src/common/crypto.c src/common/group.c src/common/client_hash.c

# Исходники сервера и клиента
SERVER_SRC = src/server/server.c $(COMMON_SRC)
CLIENT_SRC = src/client/client.c $(COMMON_SRC)

# Выходные файлы
SERVER_OUT = bin/server
CLIENT_OUT = bin/client

# Цели, не соответствующие файлам
.PHONY: all clean run-server run-client run-nclient run-win

run-server: all   # Запуск сервера (с предварительной сборкой)
	./bin/server

run-client: all   # Запуск консольного клиента (с предварительной сборкой)
	./bin/client

run-nclient:   # Компиляция и запуск графического клиента (ncurses)
	gcc src/client/nclient.c src/common/socket_utils.c src/common/crypto.c -o nclient -lncursesw -lssl -lcrypto -I./include
	./nclient

run-win:   # Подсказка для Windows-клиента
	@echo "Run WinClient.exe on Windows with: WinClient.exe <IP>"

all: bin $(SERVER_OUT) $(CLIENT_OUT)   # Сборка по умолчанию: сервер + консольный клиент

bin:   # Создать папку bin
	mkdir -p bin

$(SERVER_OUT): $(SERVER_SRC)   # Сборка сервера
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CLIENT_OUT): $(CLIENT_SRC)   # Сборка консольного клиента
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:   # Очистка
	rm -f bin/server bin/client
	rm -f logs/*.log