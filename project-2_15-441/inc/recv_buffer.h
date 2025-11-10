#ifndef PROJECT_2_15_441_INC_RECV_BUFFER_H_
#define PROJECT_2_15_441_INC_RECV_BUFFER_H_

#include <stdint.h>
#include <stdbool.h>

// typedef struct segment_t segment_t;

// a data segment on the buffer
typedef struct {  
    uint32_t start_index_inclusive;
    uint32_t end_index_inclusive;
    struct segment_t* prev;
    struct segment_t* next;
} segment_t;

typedef struct {
    uint32_t capacity;
    uint32_t last_byte_read_seqnum;
    uint32_t last_byte_read_index;
    uint32_t next_byte_expected_index;
    uint32_t last_byte_rcvd_index;
    uint8_t* buffer;
    segment_t* out_of_order_segments;   // sort in order
} recv_buffer_t;

recv_buffer_t* recv_buffer_create(uint32_t capacity);

// return the maximum amout of data that can be read from this buffer
// we cannot read the out-of-order packets
uint32_t recv_buffer_max_read(recv_buffer_t* recv_buffer);

// read len bytes from the buffer starting at last_byte_read_index + 1
void recv_buffer_read(recv_buffer_t* recv_buffer, uint32_t len);

// is the buffer able to receive 'len' bytes of data starting at 'seqnum'
bool recv_buffer_can_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len);

// receive 'len' bytes of data starting at 'seqnum'
// also update out_of_order_segments
void recv_buffer_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len);

#endif  // PROJECT_2_15_441_INC_RECV_BUFFER_H_