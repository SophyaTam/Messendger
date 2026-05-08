CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include
LDFLAGS = -lssl -lcrypto -lsqlite3

COMMON_SRC = src/common/socket_utils.c src/common/protocol.c src/common/logger.c src/common/auth.c src/common/history.c src/common/crypto.c

SERVER_SRC = src/server/server.c $(COMMON_SRC)
CLIENT_SRC = src/client/client.c $(COMMON_SRC)

SERVER_OUT = bin/server
CLIENT_OUT = bin/client

.PHONY: all clean

all: bin $(SERVER_OUT) $(CLIENT_OUT)

bin:
	mkdir -p bin

$(SERVER_OUT): $(SERVER_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CLIENT_OUT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f bin/server bin/client
	rm -f logs/*.log