#ifndef CHATLIB_H
#define CHATLIB_H

/* Networking. */
int createTCPServer(int port);
int socketSetNonBlockNoDelay(int fd);
int acceptClient(int server_socket);
int TCPConnect(char *addr, int port, int nonblock);

/* Allocation. */
void *chatMalloc(size_t size);
void *chatRealloc(void *ptr, size_t size);

/* Testing. */
ssize_t limitedRead(int fd, void *buf, size_t count, size_t limit);

#endif // CHATLIB_H