#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "recv_buffer.h"

recv_buffer_t* recv_buffer_create(uint32_t capacity) {
    recv_buffer_t* recv_buf = malloc(sizeof(recv_buffer_t));
    recv_buf->capacity = capacity;
    recv_buf->last_byte_read_seqnum = 0;
    recv_buf->last_byte_read_index = -1;
    recv_buf->next_byte_expected_index = -1;
    recv_buf->last_byte_rcvd_index = -1;
    recv_buf->buffer = malloc(capacity * sizeof(uint8_t));
    recv_buf->out_of_order_segments = NULL;
    return recv_buf;
}

uint32_t recv_buffer_max_read(recv_buffer_t* recv_buffer) {
    if (recv_buffer->last_byte_read_index == -1) {
        // buffer is uninitialized
        return 0;
    }

    assert(recv_buffer->next_byte_expected_index != -1);
    if (recv_buffer->next_byte_expected_index > recv_buffer->last_byte_read_index) {
        // no wrap around
        return (uint32_t)(recv_buffer->next_byte_expected_index - recv_buffer->last_byte_read_index - 1);
    } else {
        // wrap around happens
        uint32_t second_half = recv_buffer->capacity - 1 - recv_buffer->last_byte_read_index;
        uint32_t first_half = recv_buffer->next_byte_expected_index;
        return second_half + first_half;
    }
}

void recv_buffer_read(recv_buffer_t* recv_buffer, uint8_t* buf, uint32_t len) {
    if (len == 0) {
        return;
    }
    
    uint32_t max_read = recv_buffer_max_read(recv_buffer);
    assert(len <= max_read);

    if (recv_buffer->next_byte_expected_index > recv_buffer->last_byte_read_index) {
        // no wrap around
        uint32_t start = recv_buffer->last_byte_read_index + 1;
        assert(start < recv_buffer->capacity);
        memcpy(buf, recv_buffer->buffer + start, len);
        
        recv_buffer->last_byte_read_seqnum += len;
        
        recv_buffer->last_byte_read_index += len;
        assert(recv_buffer->last_byte_read_index < recv_buffer->capacity);
    } else {
        // wrap around happens
        uint32_t start = (recv_buffer->last_byte_read_index + 1) % recv_buffer->capacity;
        uint32_t second_half_len = recv_buffer->capacity-1 - recv_buffer->last_byte_read_index;
        memcpy(buf, recv_buffer->buffer + start, second_half_len);

        uint32_t first_half_len = len - second_half_len;
        memcpy(buf+second_half_len, recv_buffer->buffer, first_half_len);

        recv_buffer->last_byte_read_seqnum += len;

        recv_buffer->last_byte_read_index += len;
        recv_buffer->last_byte_read_index %= recv_buffer->capacity;
    }
}

uint32_t recv_buffer_max_receive(recv_buffer_t* recv_buffer) {
    if (recv_buffer->last_byte_read_index == -1) {
        return recv_buffer->capacity;
    }

    uint32_t num_to_be_consumed = 
}

bool recv_buffer_can_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len) {
    if (recv_buffer->last_byte_read_index == -1) {
        return len <= recv_buffer->capacity;
    }
    
    uint32_t start_index = seqnum - recv_buffer->last_byte_read_seqnum + recv_buffer->last_byte_read_index;

}