/*
 * Circular buffer.
 * 
 * Copyright (c) 2025 vitoloper
 * 
 */

#ifndef CIRCBUFLIB_H
#define CIRCBUFLIB_H

struct Circbuf {
    char *buf;
    int write_idx;
    int read_idx;
    int size;   // if size is N, actual capacity is N-1
};

struct Circbuf *circbuf_alloc(int len);
void circbuf_free(struct Circbuf *cb);
int circbuf_push(struct Circbuf *cb, char data);
int circbuf_pop(struct Circbuf *cb, char *data);
int circbuf_push_from_linear(struct Circbuf *cb, char *src, int n);
int circbuf_pop_to_linear(char *dest, struct Circbuf *cb, int n);
void circbuf_empty(struct Circbuf *cb);
int circbuf_len(struct Circbuf * cb);
int circbuf_size(struct Circbuf *cb);
int circbuf_space_left(struct Circbuf *cb);
void circbuf_print_data(struct Circbuf *cb);

#endif // CIRCBUFLIB_H