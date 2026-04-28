CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include

SERVER_OUT = bin/server
CLIENT_OUT = bin/client

.PHONY: all clean

all: bin $(SERVER_OUT) $(CLIENT_OUT)

bin:
	mkdir -p bin

$(SERVER_OUT): src/server/server.c
	$(CC) $(CFLAGS) $^ -o $@

$(CLIENT_OUT): src/client/client.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f bin/server bin/client
	rm -f logs/*.log
