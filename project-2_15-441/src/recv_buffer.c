#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "recv_buffer.h"

/* min / max */

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

/* seqnum and index */

uint32_t seqnum_to_index_recv(recv_buffer_t* recv_buffer, uint32_t seqnum) {
    assert(seqnum >= recv_buffer->last_byte_read_seqnum);
    assert(seqnum < recv_buffer->last_byte_read_seqnum + recv_buffer->capacity);
    return (recv_buffer->last_byte_read_index + (seqnum - recv_buffer->last_byte_read_seqnum)) % recv_buffer->capacity;
}

uint32_t get_next_byte_expected_seqnum(recv_buffer_t* recv_buffer) {
    uint32_t next_byte_expected_seqnum;
    if (recv_buffer->next_byte_expected_index > recv_buffer->last_byte_read_index) {
        // no wrap-around
        next_byte_expected_seqnum = recv_buffer->next_byte_expected_index;
        next_byte_expected_seqnum -= recv_buffer->last_byte_read_index;
        next_byte_expected_seqnum += recv_buffer->last_byte_read_seqnum;
    } else {
        // wrap around happens
        next_byte_expected_seqnum = recv_buffer->capacity-1 - recv_buffer->last_byte_read_index;
        next_byte_expected_seqnum += recv_buffer->next_byte_expected_index + 1;
        next_byte_expected_seqnum += recv_buffer->last_byte_read_seqnum;
    }
    return next_byte_expected_seqnum;
}

/* memcpy */

void safe_memcpy_to_recvbuf(recv_buffer_t* recv_buffer, uint32_t start_index, uint32_t len, uint8_t* data) {
    if (start_index + len -1 <= recv_buffer->capacity-1) {
        // can directly copy
        memcpy(recv_buffer->buffer+start_index, data, len);
    } else {
        // wrap around
        uint32_t tail_len = recv_buffer->capacity - start_index;
        memcpy(recv_buffer->buffer+start_index, data, tail_len);
        memcpy(recv_buffer->buffer, data+tail_len, len-tail_len);
    }
}

void safe_memcpy_from_recvbuf(recv_buffer_t* recv_buffer, uint32_t len, uint8_t* data) {
    uint32_t end_seq = recv_buffer->last_byte_read_seqnum + len;
    uint32_t end_index = seqnum_to_index_recv(recv_buffer, end_seq);
    if (end_index > recv_buffer->last_byte_read_index) {
        // no wrap around
        memcpy(data, recv_buffer->buffer + recv_buffer->last_byte_read_index+1, len);
    } else {
        // wrap around
        uint32_t tail_len = recv_buffer->capacity-1 - recv_buffer->last_byte_read_index;
        memcpy(data, recv_buffer->buffer + recv_buffer->last_byte_read_index+1, tail_len);
        memcpy(data+tail_len, recv_buffer->buffer, len-tail_len);
    }
}

/* segment */

void segment_disconnect(segment_t* seg) {
    if (seg->prev != NULL) {
        seg->prev->next = seg->next;
    }
    if (seg->next != NULL) {
        seg->next->prev = seg->prev;
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

void segment_clean(segment_t* start, segment_t* end) {
    if (start == NULL || end == NULL) {
        return;
    }

    if (start == end) {
        free(start);
        return;
    }

    while (start != end) {
        start = start->next;
        free(start->prev);
    }
    free(start);
}

/* ********************************** */
/* *********** Interface *********** */
/* ********************************** */

recv_buffer_t* recv_buffer_create(uint32_t capacity) {
    recv_buffer_t* recv_buf = malloc(sizeof(recv_buffer_t));
    recv_buf->capacity = capacity;
    recv_buf->last_byte_read_seqnum = 0;
    recv_buf->buffer = malloc(capacity * sizeof(uint8_t));
    recv_buf->start = NULL;
    recv_buf->end = NULL;
    return recv_buf;
}

void recv_buffer_initialize(recv_buffer_t* recv_buffer, uint32_t other_isn) {
    printf("recv_buffer->last_byte_read_seqnum : %d\n", recv_buffer->last_byte_read_seqnum);
    assert(recv_buffer->last_byte_read_seqnum == 0);
    recv_buffer->last_byte_read_seqnum = other_isn;
    recv_buffer->last_byte_read_index = 0;
    recv_buffer->next_byte_expected_index = 1;
}

uint32_t recv_buffer_max_read(recv_buffer_t* recv_buffer) {
    uint32_t next_byte_expected_seqnum = get_next_byte_expected_seqnum(recv_buffer);
    return next_byte_expected_seqnum - recv_buffer->last_byte_read_seqnum - 1;
}

uint32_t recv_buffer_max_receive(recv_buffer_t* recv_buffer) {
    uint32_t next_byte_expected_seqnum = get_next_byte_expected_seqnum(recv_buffer);
    uint32_t buf_size = next_byte_expected_seqnum - recv_buffer->last_byte_read_seqnum;
    return recv_buffer->capacity - buf_size;
}

void recv_buffer_read(recv_buffer_t* recv_buffer, uint8_t* buf, uint32_t len) {
    assert(len <= recv_buffer_max_read(recv_buffer));
    if (len == 0) {
        return;
    }
    
    // copy data to buffer
    safe_memcpy_from_recvbuf(recv_buffer, len, buf);

    // update internal states
    recv_buffer->last_byte_read_seqnum += len;
    recv_buffer->last_byte_read_index = (recv_buffer->last_byte_read_index + len) % recv_buffer->capacity;
}

uint8_t recv_buffer_can_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len) {
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
 
void recv_buffer_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len, uint8_t* data) {
    assert(recv_buffer_can_receive(recv_buffer, seqnum, len) == 0);

    uint32_t next_expected_seq = get_next_byte_expected_seqnum(recv_buffer);
    if (seqnum <= next_expected_seq && recv_buffer->start == NULL) {
        // inorder data, no segmention existed
        // directly write into the buffer 
        uint32_t start_index = seqnum_to_index_recv(recv_buffer, seqnum);
        safe_memcpy_to_recvbuf(recv_buffer, start_index, len, data);
        recv_buffer->next_byte_expected_index = seqnum_to_index_recv(recv_buffer, seqnum+len);
        return;
    }

    if (recv_buffer->start == NULL) {
        // no segmention existed, but out-of-order data
        uint32_t start_index = seqnum_to_index_recv(recv_buffer, seqnum);
        safe_memcpy_to_recvbuf(recv_buffer, start_index, len, data);

        segment_t* seg = malloc(sizeof(segment_t));
        seg->start_seqnum_inclusive = seqnum;
        seg->end_seqnum_inclusive = seqnum + len - 1;
        recv_buffer->start = seg;
        recv_buffer->end = seg;
        return;
    } 

    // segmention existed, don't care if in-order
    segment_t* seg = segment_merge(recv_buffer->start, recv_buffer->end, seqnum, seqnum + len - 1);
    if (seg->start_seqnum_inclusive <= next_expected_seq) {
        // the merged block can be further merged with the existing in-order data
        recv_buffer->next_byte_expected_index = seqnum_to_index_recv(recv_buffer, seg->end_seqnum_inclusive+1);
        if (seg->prev == NULL) {
            recv_buffer->start = seg->next;
        }
        if (seg->next == NULL) {
            recv_buffer->end = seg->prev;
        }
        segment_disconnect(seg);
        free(seg);
    } else {
        // cannot be merged with the existing in-order data
        if (seg->prev == NULL) {
            recv_buffer->start = seg;
        }
        if (seg->next == NULL) {
            recv_buffer->end = seg;
        }
    }

    uint32_t start_index = seqnum_to_index_recv(recv_buffer, seqnum);
    safe_memcpy_to_recvbuf(recv_buffer, start_index, len, data);    
}

void recv_buffer_clean(recv_buffer_t* recv_buffer) {
    free(recv_buffer->buffer);
    segment_clean(recv_buffer->start, recv_buffer->end);
    free(recv_buffer);
}