all: smallchat-server
CFLAGS=-O2 -Wall -W -std=c99 -g

smallchat-server: smallchat-server.c chatlib.c circular_buffer.c
	$(CC) smallchat-server.c chatlib.c circular_buffer.c -o smallchat-server $(CFLAGS)

clean:
	rm -f smallchat-server