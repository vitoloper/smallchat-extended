/*
 * Circular buffer.
 * 
 * Copyright (c) 2025 vitoloper
 * 
 */

#include "circular_buffer.h"
#include <stdio.h>
#include <stdlib.h>

/* Allocate circular buffer. */
struct Circbuf *circbuf_alloc(int size) {
    /* Allocate space for struct */
    struct Circbuf *cb = (struct Circbuf *)malloc(sizeof(struct Circbuf));
    
    /* Allocate space for buffer and set indices and size. */
    cb->buf = (char *)malloc(sizeof(char) * size);
    cb->write_idx = 0;
    cb->read_idx = 0;
    cb->size = size;

    return cb;
}

/* Free allocated memory. */
void circbuf_free(struct Circbuf *cb) {
    free(cb->buf);
    free(cb);
}

/* Insert (push) an element. */
int circbuf_push(struct Circbuf *cb, char data) {
    /* Check if buffer is full. */
    if ((cb->write_idx + 1) % (cb->size) == cb->read_idx) {
        return 0;
    }

    /* Write data */
    cb->buf[cb->write_idx] = data;

    /* Increment write_idx */
    cb->write_idx = (cb->write_idx + 1) % cb->size;

    return 1;
}

/* Read (pop) an element. */
int circbuf_pop(struct Circbuf *cb, char *data) {
    /* Check if buffer is empty. */
    if(cb->read_idx == cb->write_idx) {
        return 0;
    }

    /* Read data */
    *data = cb->buf[cb->read_idx];

    /* Increment read_idx */
    cb->read_idx = (cb->read_idx + 1) % cb->size;

    return 1;
}

/* Push n elements from src linear array to cb. */
int circbuf_push_from_linear(struct Circbuf *cb, char *src, int n) {
    int i;

    for (i = 0; i < n && i < cb->size-1; i++) {
        /* Push element and check if successful. */
        if (circbuf_push(cb, src[i]) == 0) return i;
    }

    /* Return the number of elements. */
    return i;
}

/* Pop n elements from cb to dest linear array. */
int circbuf_pop_to_linear(char *dest, struct Circbuf *cb, int n) {
    int i = n;

    for(i = 0; i < n; i++) {
        /* Pop element and check if successful. */
        if (circbuf_pop(cb, &dest[i]) == 0) return i;
    }

    /* Return the number of elements. */
    return i;
}

/* Empty buffer. */
void circbuf_empty(struct Circbuf *cb) {
    cb->read_idx = cb->write_idx;
}

/* Get buffer length (number of stored elements). */
int circbuf_len(struct Circbuf * cb) {
    if (cb->write_idx == cb->read_idx) {
        return 0;
    } else if (cb->write_idx > cb->read_idx) {
        return cb->write_idx - cb->read_idx;
    } else {
        return cb->write_idx - cb->read_idx + cb->size;   
    }
}

/* Get buffer size. */
int circbuf_size(struct Circbuf *cb) {
    return cb->size;
}

/* Get buffer remaining space. */
int circbuf_space_left(struct Circbuf *cb) {
    return cb->size - 1 - circbuf_len(cb);
}

/* Print buffer data, indices and size. */
void circbuf_print_data(struct Circbuf *cb) {
    int i;
    int cb_len = circbuf_len(cb);
    int read_idx = cb->read_idx;

    printf("write_idx: %d\n",cb->write_idx);
    printf("read_idx: %d\n", cb->read_idx);
    printf("length: %d\n", cb_len);
    printf("size: %d\n", cb->size);

    printf("Data: ");
    if (cb_len == 0) {
        printf("(empty)");
    } else {
        for (i = 0; i < cb_len; i++) {
            switch (cb->buf[read_idx]) {
                case '\n':
                printf("\\n ");
                break;

                case '\r':
                printf("\\r ");
                break;

                case '\t':
                printf("\\t ");
                break;

                default:
                printf("%c ", cb->buf[read_idx]);
            }
            
            read_idx = (read_idx + 1) % cb->size;
        };
    }

    printf("\n");
}