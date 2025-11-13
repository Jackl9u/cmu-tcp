#ifndef PROJECT_2_15_441_INC_SEND_BUFFER_H_
#define PROJECT_2_15_441_INC_SEND_BUFFER_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t capacity;
    uint32_t last_byte_acked_seqnum;
    uint32_t last_byte_acked_index;       // the index of the last byte that is received by the other side; i.e. ackNum_from_receiver - 1
    uint32_t last_byte_sent_index;        // the index of the last byte that is sent (not yet acked)
    uint32_t next_byte_written_index;     // the index of the buffer to write the next byte from application
    uint8_t* buffer;
} send_buffer_t;

send_buffer_t* send_buffer_create(uint32_t capacity);

void send_buffer_initialize(send_buffer_t* send_buffer, uint32_t isn);

// maximum number of bytes the can be written into the send_buffer
uint32_t send_buffer_max_write(send_buffer_t* send_buffer);

// return the maximum number of bytes that can be dumped (last_byte_written_index - last_byte_acked_index)
uint32_t send_buffer_max_dump(send_buffer_t* send_buffer);

// called by the application. write the 'len' bytes in 'buf' to send_buffer
void send_buffer_write(send_buffer_t* send_buffer, uint8_t* buf, uint32_t len);

// dump 'len' bytes starting from 'last_byte_acked_index' into 'data' for sendto()
void send_buffer_dump(send_buffer_t* send_buffer, uint32_t len, uint8_t* data);

#endif  // PROJECT_2_15_441_INC_SEND_BUFFER_H_