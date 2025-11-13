#include <assert.h>

#include "send_buffer.h"

uint32_t get_next_byte_written_seqnum(send_buffer_t* send_buffer) {
    uint32_t next_byte_written_seqnum;
    if (send_buffer->next_byte_written_index > send_buffer->last_byte_acked_index) {
        // no warp around
        
    } else {
        // wrap around

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

}

uint32_t send_buffer_max_send(send_buffer_t* send_buffer) {

}