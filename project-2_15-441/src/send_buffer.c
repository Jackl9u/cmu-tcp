#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "send_buffer.h"

/* seqnum and index */

uint32_t seqnum_to_index_send(send_buffer_t* send_buffer, uint32_t seqnum) {
    assert(seqnum >= send_buffer->last_byte_acked_seqnum);
    assert(seqnum < send_buffer->last_byte_acked_seqnum + send_buffer->capacity);
    return (send_buffer->last_byte_acked_index + (seqnum - send_buffer->last_byte_acked_seqnum)) % send_buffer->capacity;
}

uint32_t get_next_byte_written_seqnum(send_buffer_t* send_buffer) {
    uint32_t next_byte_written_seqnum;
    if (send_buffer->next_byte_written_index > send_buffer->last_byte_acked_index) {
        // no warp around
        next_byte_written_seqnum = send_buffer->next_byte_written_index;
        next_byte_written_seqnum -= send_buffer->last_byte_acked_index;
        next_byte_written_seqnum += send_buffer->last_byte_acked_seqnum;
    } else {
        // wrap around
        next_byte_written_seqnum = send_buffer->capacity-1 - send_buffer->last_byte_acked_index;
        next_byte_written_seqnum += send_buffer->next_byte_written_index + 1;
        next_byte_written_seqnum += send_buffer->last_byte_acked_seqnum;
    }
    return next_byte_written_seqnum;
}

uint32_t get_last_byte_sent_seqnum(send_buffer_t* send_buffer) {
    uint32_t last_byte_sent_seqnum;
    if (send_buffer->last_byte_sent_index >= send_buffer->last_byte_acked_index) {
        // no wrap around
        last_byte_sent_seqnum = send_buffer->last_byte_sent_index;
        last_byte_sent_seqnum -= send_buffer->last_byte_acked_index;
        last_byte_sent_seqnum += send_buffer->last_byte_acked_seqnum;
    } else {
        // wrap around
        last_byte_sent_seqnum = send_buffer->capacity-1 - send_buffer->last_byte_acked_index;
        last_byte_sent_seqnum += send_buffer->last_byte_sent_index+1;
        last_byte_sent_seqnum += send_buffer->last_byte_acked_seqnum;
    }
    return last_byte_sent_seqnum;
}

/* memcpy */

void safe_memcpy_to_sendbuf(send_buffer_t* send_buffer, uint32_t len, uint8_t* data) {
    uint32_t start_index = send_buffer->next_byte_written_index;
    if (start_index + len - 1 <= send_buffer->capacity-1) {
        // can directly write
        memcpy(send_buffer->buffer+start_index, data, len);
    } else {
        // wrap around
        uint32_t tail_len = send_buffer->capacity - start_index;
        memcpy(send_buffer->buffer+start_index, data, tail_len);
        memcpy(send_buffer->buffer, data+tail_len, len-tail_len);
    }
}

void safe_memcpy_from_sendbuf(send_buffer_t* send_buffer, uint32_t start_index, uint32_t len, uint8_t* data) {
    uint32_t end_index = start_index + len - 1;
    if (end_index > send_buffer->last_byte_acked_index) {
        // no wrap around
        memcpy(data, send_buffer->buffer+start_index, len);
    } else {
        // wrap around happens
        uint32_t tail_len = send_buffer->capacity - start_index;
        memcpy(data, send_buffer->buffer+start_index, tail_len);
        memcpy(data+tail_len, send_buffer->buffer, len-tail_len);
    }
}

/* ********************************** */
/* *********** Interface *********** */
/* ********************************** */

send_buffer_t* send_buffer_create(uint32_t capacity) {
    send_buffer_t* send_buf = malloc(sizeof(send_buffer_t));
    send_buf->capacity = capacity;
    send_buf->last_byte_acked_seqnum = 0;
    send_buf->buffer = malloc(capacity * sizeof(uint8_t));
    return send_buf;
}

void send_buffer_initialize(send_buffer_t* send_buffer, uint32_t isn) {
    assert(send_buffer->last_byte_acked_seqnum == 0);
    send_buffer->last_byte_acked_seqnum = isn;
    send_buffer->last_byte_acked_index = 0;
    send_buffer->last_byte_sent_index = 0;
    send_buffer->next_byte_written_index = 1;
}

uint32_t send_buffer_max_write(send_buffer_t* send_buffer) {
    uint32_t next_byte_written_seqnum = get_next_byte_written_seqnum(send_buffer);
    uint32_t buf_size = next_byte_written_seqnum - send_buffer->last_byte_acked_seqnum;
    return send_buffer->capacity - buf_size;
}

// return the maximum number of "never-sent bytes" that we can send
uint32_t send_buffer_max_new_dump(send_buffer_t* send_buffer) {
    uint32_t next_byte_written_seqnum = get_next_byte_written_seqnum(send_buffer);
    uint32_t last_byte_sent_seqnum = get_last_byte_sent_seqnum(send_buffer);
    return next_byte_written_seqnum - last_byte_sent_seqnum - 1;
}

void send_buffer_write(send_buffer_t* send_buffer, uint8_t* buf, uint32_t len) {
    assert(len <= send_buffer_max_write(send_buffer));
    if (len == 0) {
        return;
    }

    // copy data to buffer
    safe_memcpy_to_sendbuf(send_buffer, len, buf);

    // update internal states
    send_buffer->next_byte_written_index += len;
    send_buffer->next_byte_written_index %= send_buffer->capacity;
}

// this dump function should support a start_index since we may dump from last_sent_index
// or dump from last_ack_index in case of a timeout
void send_buffer_dump(send_buffer_t* send_buffer, uint32_t start_index, uint32_t len, uint8_t* data) {
    if (len == 0) {
        return;
    }

    // copy data from buffer
    safe_memcpy_from_sendbuf(send_buffer, start_index, len, data);

    // update internal states
    send_buffer->last_byte_sent_index = 0; /* use a index_to_seqnum function */
}

void send_buffer_clean(send_buffer_t* send_buffer) {
    free(send_buffer->buffer);
    free(send_buffer);
}

void send_buffer_update_ack(send_buffer_t* send_buffer, uint32_t hdr_ack) {
    hdr_ack -= 1;
    if (hdr_ack > send_buffer->last_byte_acked_seqnum) {
        uint32_t idx = seqnum_to_index_send(send_buffer, hdr_ack);
        send_buffer->last_byte_acked_seqnum = hdr_ack;
        send_buffer->last_byte_acked_index = idx;
    }
}