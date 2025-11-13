#ifndef PROJECT_2_15_441_INC_RECV_BUFFER_H_
#define PROJECT_2_15_441_INC_RECV_BUFFER_H_

#include <stdint.h>
#include <stdbool.h>

// typedef struct segment_t segment_t;

// a data segment on the buffer
typedef struct segment_t {  
    uint32_t start_seqnum_inclusive;
    uint32_t end_seqnum_inclusive;
    struct segment_t* prev;
    struct segment_t* next;
} segment_t;

typedef struct {
    uint32_t capacity;
    uint32_t last_byte_read_seqnum;
    uint32_t last_byte_read_index;       // the index of the buffer array, on which the byte was the last one read/consumed
    uint32_t next_byte_expected_index;   // the index of the buffer array to put the next in-order byte
    uint8_t* buffer;
    segment_t* start;
    segment_t* end;
} recv_buffer_t;

recv_buffer_t* recv_buffer_create(uint32_t capacity);

void recv_buffer_initialize(recv_buffer_t* recv_buffer, uint32_t isn);

// return the maximum amout of data that can be read from this buffer
// we cannot read the out-of-order packets
uint32_t recv_buffer_max_read(recv_buffer_t* recv_buffer);

// return the maximum number of bytes from the next_expected_seq_num (ack to the other party)
// that this buffer can hold. out-of-order bytes are overwritten since they are not covered by ack
uint32_t recv_buffer_max_receive(recv_buffer_t* recv_buffer);

// read len bytes from the buffer starting at last_byte_read_index + 1
void recv_buffer_read(recv_buffer_t* recv_buffer, uint8_t* buf, uint32_t len);

// 0 : can receive
// 1 : don't receive : too long
// 2 : don't receive : seqnum <= last_byte_read_seqnum (already read/processed)
// 3 : don't receive : the packet was already successfully received
uint8_t recv_buffer_can_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len);

// receive 'len' bytes of data starting at 'seqnum'
// also update out_of_order_segments
void recv_buffer_receive(recv_buffer_t* recv_buffer, uint32_t seqnum, uint32_t len, uint8_t* data);

#endif  // PROJECT_2_15_441_INC_RECV_BUFFER_H_