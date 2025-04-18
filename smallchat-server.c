/* smallchat-server.c -- Read clients input, send to all the other connected clients.
 *
 * Original work Copyright (c) 2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Modified work Copyright (c) 2025, vitoloper
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the project name of nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/select.h>
#include <unistd.h>

#include "chatlib.h"
#include "circular_buffer.h"

/* ============================ Data structures =================================
 * The minimal stuff we can afford to have. This example must be simple
 * even for people that don't know a lot of C.
 * =========================================================================== */

#define MAX_CLIENTS 1000 // This is actually the higher file descriptor.
#define SERVER_PORT 7711

#define READBUF_SIZE 128
#define MSG_SEP '\n' /* Message separator (buffer reads until this char is found) */

/* This structure represents a connected client. There is very little
 * info about it: the socket descriptor and the nick name, if set, otherwise
 * the first byte of the nickname is set to 0 if not set.
 * The client can set its nickname with /nick <nickname> command. */
struct client {
    int fd;     // Client socket.
    char *nick; // Nickname of the client.
    struct Circbuf *read_cb; // Circular buffer
};

/* This global structure encapsulates the global state of the chat. */
struct chatState {
    int serversock;     // Listening server socket.
    int numclients;     // Number of connected clients right now.
    int maxclient;      // The greatest 'clients' slot populated.
    struct client *clients[MAX_CLIENTS]; // Clients are set in the corresponding
                                         // slot of their socket descriptor.
};

struct chatState *Chat; // Global chat state (initialized at startup).

/* ====================== Small chat core implementation ========================
 * Here the idea is very simple: we accept new connections, read what clients
 * write us and fan-out (that is, send-to-all) the message to everybody
 * with the exception of the sender. And that is, of course, the most
 * simple chat system ever possible.
 * =========================================================================== */

/* Create a new client bound to 'fd'. This is called when a new client
 * connects. As a side effect updates the global Chat state. */
struct client *createClient(int fd) {
    char nick[32]; // Used to create an initial nick for the user.
    int nicklen = snprintf(nick, sizeof(nick), "user:%d", fd);
    struct client *c = chatMalloc(sizeof(*c));

    socketSetNonBlockNoDelay(fd); // Pretend this will not fail.

    c->fd = fd;
    c->nick = chatMalloc(nicklen+1);
    memcpy(c->nick,nick,nicklen);

    /* Allocate circular buffer. */
    c->read_cb = circbuf_alloc(READBUF_SIZE);
    assert(c->read_cb != NULL);
    
    assert(Chat->clients[c->fd] == NULL); // This should be available.
    Chat->clients[c->fd] = c;

    /* We need to update the max client set if needed. */
    if (c->fd > Chat->maxclient) Chat->maxclient = c->fd;
    
    Chat->numclients++;

    return c;
}

/* Free a client, associated resources, and unbind it from the global
 * state in Chat. */
void freeClient(struct client *c) {
    free(c->nick);
    close(c->fd);
    circbuf_free(c->read_cb);
    Chat->clients[c->fd] = NULL;
    Chat->numclients--;
    
    if (Chat->maxclient == c->fd) {
        /* Ooops, this was the max client set. Let's find what is
         * the new highest slot used. */
        int j;

        for (j = Chat->maxclient-1; j >= 0; j--) {
            if (Chat->clients[j] != NULL) {
                Chat->maxclient = j;
                break;
            }
        }

        if (j == -1) Chat->maxclient = -1; // We no longer have clients.
    }

    free(c);
}

/* Allocate and init the global stuff. */
void initChat(void) {
    Chat = chatMalloc(sizeof(*Chat));
    memset(Chat,0,sizeof(*Chat));

    /* No clients at startup, of course. */
    Chat->maxclient = -1;
    Chat->numclients = 0;

    /* Create our listening socket, bound to the given port. This
     * is where our clients will connect. */
    Chat->serversock = createTCPServer(SERVER_PORT);
    if (Chat->serversock == -1) {
        perror("Creating listening socket");
        exit(1);
    }
}

/* Send the specified string to all connected clients but the one
 * having as socket descriptor 'excluded'. If you want to send something
 * to every client just set excluded to an impossible socket: -1. */
void sendMsgToAllClientsBut(int excluded, char *s, size_t len) {
    for (int j = 0; j <= Chat->maxclient; j++) {
        if (Chat->clients[j] == NULL ||
            Chat->clients[j]->fd == excluded) continue;

        /* Important: we don't do ANY BUFFERING. We just use the kernel
         * socket buffers. If the content does not fit, we don't care.
         * This is needed in order to keep this program simple. */
        write(Chat->clients[j]->fd,s,len);
    }
}

/* The main() function implements the main chat logic:
 * 1. Accept new clients connections if any.
 * 2. Check if any client sent us some new message.
 * 3. Send the message to all the other clients. */
int main(void) {
    char tmpbuf[READBUF_SIZE];
    char readbuf[READBUF_SIZE];
    int readbuf_idx;

    /* Initialize the global Chat state. */
    initChat();

    while(1) {
        fd_set readfds;
        struct timeval tv;
        int retval;

        FD_ZERO(&readfds);

        /* When we want to be notified by select() that there is
         * activity? If the listening socket has pending clients to accept
         * or if any other client wrote anything. */
        FD_SET(Chat->serversock, &readfds);
        for (int j = 0; j <= Chat->maxclient; j++) {
            if (Chat->clients[j]) FD_SET(j, &readfds);
        }

        /* Set a timeout for select(), see later why this may be useful
         * in the future (not now). */
        tv.tv_sec = 1; // 1 sec timeout
        tv.tv_usec = 0;

        /* Select wants as first argument the maximum file descriptor
         * in use plus one. It can be either one of our clients or the
         * server socket itself. */
        int maxfd = Chat->maxclient;
        if (maxfd < Chat->serversock) maxfd = Chat->serversock;

        retval = select(maxfd+1, &readfds, NULL, NULL, &tv);

        if (retval == -1) {
            perror("select() error");
            exit(1);
        } else if (retval) {
            /* If the listening socket is "readable", it actually means
             * there are new clients connections pending to accept. */
            if (FD_ISSET(Chat->serversock, &readfds)) {
                int fd = acceptClient(Chat->serversock);
                struct client *c = createClient(fd);
                
                /* Send a welcome message. */
                char *welcome_msg =
                    "Welcome to Simple Chat! "
                    "Use /nick <nick> to set your nick.\n";
                write(c->fd,welcome_msg,strlen(welcome_msg));

                printf("Connected client fd=%d\n", fd);
            }

            /* Here for each connected client, check if there are pending
             * data the client sent us. */
            for (int j = 0; j <= Chat->maxclient; j++) {
                /* Check if client actually exists. */
                if (Chat->clients[j] == NULL) continue;

                if (FD_ISSET(j, &readfds)) {
                    /* Here we just hope that there is a well formed
                     * message waiting for us. But it is entirely possible
                     * that we read just half a message. In a normal program
                     * that is not designed to be that simple, we should try
                     * to buffer reads until the end-of-the-line (or another
                     * message separator) is reached. */

                    /* Remaining space in circular buffer, also taking into account 
                     * the space for the final null char. */
                    int count = circbuf_space_left(Chat->clients[j]->read_cb) - 1;

                    /* Read data in a temp buffer and then push it in 
                     * client circular buffer.*/
                    int nread = read(j, tmpbuf, count);
                    circbuf_push_from_linear(Chat->clients[j]->read_cb, tmpbuf, nread);
                    tmpbuf[nread] = '\0';

                    /* printf("Client fd=%d\n", j); */
                    /* circbuf_print_data(Chat->clients[j]->read_cb); */

                    if (nread <= 0) {
                        /* Error or short read means that the socket
                         * was closed. */
                        printf("Disconnected client fd=%d, nick=%s\n",
                            j, Chat->clients[j]->nick);
                        freeClient(Chat->clients[j]);
                    } else {
                        /* Count the number of MSG_SEP occurrencies in tmpbuf */
                        int sep_occur = 0;
                        for (int i = 0; i < nread; i++) {
                            if (tmpbuf[i] == MSG_SEP) sep_occur++;
                        };

                        /* Buffering reads until MSG_SEP is received or read_cb is 
                         * full (taking into account the null-char space). */
                        if (sep_occur > 0 || circbuf_space_left(Chat->clients[j]->read_cb) <= 1) {

                            /* The client sent us a message. We need to
                            * relay this message to all the other clients
                            * in the chat. */
                            struct client *c = Chat->clients[j];

                            /* Process messages. 
                             * Example (suppose 'A' is the separator):
                             * "niceAtoAmeetAyou"
                             * 
                             * 'A' occurs 3 times, so we send 3 messages
                             * niceA, toA, meetA
                             * "you" is kept in circular buffer and is not sent.
                             * 
                             */

                            /* If sep_occur is 0 and we are here, it means cb is full. 
                             * So, set sep_occur to 1 to send the whole buffer once. */
                            if (sep_occur == 0) sep_occur = 1;

                            for (; sep_occur > 0; sep_occur--) {
                                readbuf_idx = 0;
                                do {
                                    circbuf_pop(c->read_cb, &readbuf[readbuf_idx]);
                                } while (readbuf[readbuf_idx++] != MSG_SEP);
                                readbuf[readbuf_idx] = '\0';
                                // printf("readbuf: %s\n", readbuf);

                                /* If the user message starts with "/", we
                                * process it as a client command. So far
                                * only the /nick <newnick> command is implemented. */
                                if (readbuf[0] == '/') {
                                    /* Remove any trailing newline. */
                                    char *p;
                                    p = strchr(readbuf,'\r'); if (p) *p = 0;
                                    p = strchr(readbuf,'\n'); if (p) *p = 0;
                                    /* Check for an argument of the command, after
                                    * the space. */
                                    char *arg = strchr(readbuf,' ');
                                    if (arg) {
                                        *arg = 0; /* Terminate command name. */
                                        arg++; /* Argument is 1 byte after the space. */
                                    }

                                    if (!strcmp(readbuf,"/nick") && arg) {
                                        free(c->nick);
                                        int nicklen = strlen(arg);
                                        c->nick = chatMalloc(nicklen+1);
                                        memcpy(c->nick,arg,nicklen+1);
                                    } else {
                                        /* Unsupported command. Send an error. */
                                        char *errmsg = "Unsupported command\n";
                                        write(c->fd,errmsg,strlen(errmsg));
                                    }
                                } else {
                                    /* Create a message to send everybody (and show
                                    * on the server console) in the form:
                                    *   nick> some message. */
                                    char msg[256];
                                    int msglen = snprintf(msg, sizeof(msg),
                                        "%s> %s", c->nick, readbuf);

                                    /* snprintf() return value may be larger than
                                    * sizeof(msg) in case there is no room for the
                                    * whole output. */
                                    if (msglen >= (int)sizeof(msg))
                                        msglen = sizeof(msg)-1;
                                    
                                    printf("%s", msg);

                                    /* Send it to all the other clients. */
                                    sendMsgToAllClientsBut(j, msg, msglen);
                                }
                            }
                        } else {
                            /* Do nothing, keep buffering with the next read. */
                        }
                    }
                }
            }
        } else {
            /* Timeout occurred. We don't do anything right now, but in
             * general this section can be used to wakeup periodically
             * even if there is no clients activity. */
        }
    }

    return 0;
}