#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "recv_buffer.h"
#include "cmu_tcp.h"

recv_buffer_t* recv_buffer_create(uint32_t capacity) {
    recv_buffer_t* recv_buf = malloc(sizeof(recv_buffer_t));
    recv_buf->capacity = capacity;
    recv_buf->last_byte_read_seqnum = 0;
    recv_buf->last_byte_read_index = -1;
    recv_buf->next_byte_expected_index = -1;
    recv_buf->buffer = malloc(capacity * sizeof(uint8_t));
    recv_buf->start = NULL;
    recv_buf->end = NULL;
    return recv_buf;
}

// return the maximum amout of data that can be read from this buffer
// we cannot read the out-of-order packets
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
        return recv_buffer->capacity-1;
    }

    uint32_t num_to_be_consumed = recv_buffer_max_read(recv_buffer);
    // we always want to have a already-read byte on the buffer
    // in case when 
    // recv_buffer->last_byte_read_index == recv_buffer->next_byte_expected_index
    // we cannot receive anymore before consuming some bytes, because otherwise 
    // there will be no byte that is not read 
    return (recv_buffer->capacity-1 - num_to_be_consumed);
}

uint32_t get_next_byte_expected_seqnum(recv_buffer_t* recv_buffer) {
    uint32_t next_byte_expected_seqnum;
    if (recv_buffer->next_byte_expected_index > recv_buffer->last_byte_read_index) {
        // no wrap-around
        next_byte_expected_seqnum = recv_buffer->next_byte_expected_index - recv_buffer->last_byte_read_index + recv_buffer->last_byte_read_seqnum;
    } else {
        // wrap around happens
        next_byte_expected_seqnum = recv_buffer->capacity-1 - recv_buffer->last_byte_read_index;
        next_byte_expected_seqnum += recv_buffer->next_byte_expected_index + 1;
        next_byte_expected_seqnum += recv_buffer->last_byte_read_seqnum;
    }
    return next_byte_expected_seqnum;
}

// 0 : can receive
// 1 : don't receive : too long
// 2 : don't receive : seqnum <= last_byte_read_seqnum (already read/processed)
// 3 : don't receive : the packet was already successfully received
uint8_t recv_buffer_can_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len) {
    if (recv_buffer->last_byte_read_index = -1) {
        // recv_buffer is uninitialized
        if (len < recv_buffer->capacity) {
            return 0;
        } else {
            return 1;
        }
    }

    if (seqnum <= recv_buffer->last_byte_read_seqnum) {
        return 2;
    }
    
    uint32_t last_seq = seqnum + len - 1;
    uint32_t next_byte_expected_seqnum = get_next_byte_expected_seqnum(recv_buffer);
    if (last_seq < next_byte_expected_seqnum) {
        return 3;
    }

    if (last_seq - recv_buffer->last_byte_read_seqnum >= recv_buffer->capacity) {
        return 1;
    } else {
        return 0;
    }
}

uint32_t seqnum_to_index(recv_buffer_t* recv_buffer, uint32_t seqnum) {
    assert(seqnum >= recv_buffer->last_byte_read_seqnum);
    assert(seqnum < recv_buffer->last_byte_read_seqnum + recv_buffer->capacity);

    return (recv_buffer->last_byte_read_index + (seqnum - recv_buffer->last_byte_read_seqnum)) % recv_buffer->capacity;
}

uint32_t min(uint32_t a, uint32_t b) {
    if (a < b) {
        return a;
    } else {
        return b;
    }
}

uint32_t max(uint32_t a, uint32_t b) {
    if (a < b) {
        return b;
    } else {
        return a;
    }
}

// return the merged block
// assume 'start' != NULL && 'end' != NULL
segment_t* segment_merge(segment_t* start, segment_t* end, uint32_t seg_start_seq, uint32_t seg_end_seq) {
    segment_t* left_end = start;
    while (left_end != NULL && left_end->end_seqnum_inclusive < seg_start_seq) {
        left_end = left_end->next;
    }

    segment_t* right_end = end;
    while (right_end != NULL && right_end->start_seqnum_inclusive > seg_end_seq) {
        right_end = right_end->prev;
    }

    segment_t* seg = malloc(sizeof(segment_t));

    if (left_end == NULL) {
        // the segment doesn't intersect with any existing segment
        // should be placed on the right-most
        seg->start_seqnum_inclusive = seg_start_seq;
        seg->end_seqnum_inclusive = seg_end_seq;
        seg->prev = end;
        end->next = seg;
        
        return seg;
    }

    if (right_end == NULL) {
        // the segment doesn't intersect with any existing segment
        // should be placed on the left-most
        seg->start_seqnum_inclusive = seg_start_seq;
        seg->end_seqnum_inclusive = seg_end_seq;
        seg->next = start;
        start->prev = seg;

        return seg;
    }


    if (right_end->next == left_end) {
        // the segment doesn't intersect with any existing segment
        // it is in-between [right_end, left_end]
        // can be directly inserted
        seg->start_seqnum_inclusive = seg_start_seq;
        seg->end_seqnum_inclusive = seg_end_seq;
        seg->next = left_end;
        left_end->prev = seg;
        seg->prev = right_end;
        right_end->next = seg;

        return seg;
    }

    seg->start_seqnum_inclusive = min(left_end->start_seqnum_inclusive, seg_start_seq);
    seg->end_seqnum_inclusive = max(right_end->end_seqnum_inclusive, seg_end_seq);
    seg->next = right_end->next;
    if (right_end->next != NULL) {
        right_end->next->prev = seg;
    }
    seg->prev = left_end->prev;
    if (left_end->prev != NULL) {
        left_end->prev->next = seg;
    }

    // clean up left_end, ..., right_end
    if (left_end == right_end) {
        free(left_end);
    } else {
        left_end = left_end->next;
        while (left_end != right_end) {
            free(left_end->prev);
            left_end = left_end->next;
        }
        free(right_end->prev);
        free(right_end);
    }
    
    return seg;
}

void segment_disconnect(segment_t* seg) {
    if (seg->prev != NULL) {
        seg->prev->next = seg->next;
    }
    if (seg->next != NULL) {
        seg->next->prev = seg->prev;
    }
} 

void safe_memcpy(recv_buffer_t* recv_buffer, uint32_t start, uint32_t len, uint8_t* data) {
    
}

void recv_buffer_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len, uint8_t* data) {
    uint32_t valid = recv_buffer_can_receive(recv_buffer, seqnum, len);
    assert(valid == 0);

    // special case 1 : when the recv_buffer is uninitialized
    if (recv_buffer->last_byte_read_index == -1) {
        recv_buffer->last_byte_read_seqnum = seqnum-1;
        recv_buffer->last_byte_read_index = 0;
        recv_buffer->next_byte_expected_index = (len+1) % recv_buffer->capacity;
        memcpy(recv_buffer->buffer+1, data, len);
        return;
    }

    // special case 2 : continuous in-order data
    uint32_t next_expected_seq = get_next_byte_expected_seqnum(recv_buffer);
    if (seqnum <= next_expected_seq && recv_buffer->start == NULL) {
        // directly write into the buffer 
        uint32_t start_index = seqnum_to_index(recv_buffer, seqnum);
        memcpy(recv_buffer->buffer+start_index, data, len);
        recv_buffer->next_byte_expected_index = seqnum_to_index(recv_buffer, seqnum + len);
        return;
    }

    if (recv_buffer->start == NULL) {
        // seqnum > next_expected_seq
        uint32_t start_index = seqnum_to_index(recv_buffer, seqnum);
        memcpy(recv_buffer->buffer+start_index, data, len);
        segment_t* seg = malloc(sizeof(segment_t));
        seg->start_seqnum_inclusive = seqnum;
        seg->end_seqnum_inclusive = seqnum + len - 1;
        recv_buffer->start = seg;
        recv_buffer->end = seg;
    } else {
        segment_t* seg = segment_merge(recv_buffer->start, recv_buffer->end, seqnum, seqnum + len - 1);
        if (seg->start_seqnum_inclusive <= get_next_byte_expected_seqnum(recv_buffer)) {
            recv_buffer->next_byte_expected_index = seqnum_to_index(recv_buffer, seg->end_seqnum_inclusive+1);
            if (seg->prev == NULL) {
                recv_buffer->start = seg->next;
            }
            if (seg->next == NULL) {
                recv_buffer->end = seg->prev;
            }
            segment_disconnect(seg);
            free(seg);
        } else {
            if (seg->prev == NULL) {
                recv_buffer->start = seg;
            }
            if (seg->next == NULL) {
                recv_buffer->end = seg;
            }
        }

        // wrap around ?
        uint32_t start_index = seqnum_to_index(seqnum);
        memcpy(recv_buffer, )
    }

    // copy data to receive buffer
    // 1) : need to deal with the initial case when the buffer is uninitialzied



    





}