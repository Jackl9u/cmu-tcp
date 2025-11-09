#ifndef PROJECT_2_15_441_INC_SEND_BUFFER_H_
#define PROJECT_2_15_441_INC_SEND_BUFFER_H_

#include <stdint.h>

typedef struct {
    uint32_t capacity;
    uint32_t last_byte_acked_seqnum;
    uint32_t last_byte_acked_index;       // the index of the last byte that is received by the other side; i.e. ackNum_from_receiver - 1
    uint32_t last_byte_sent_index;        // the index of the last byte that is sent (not yet acked)
    uint32_t last_byte_written_index;     // the index of the last byte that is written by the application
    uint8_t* buffer;
} send_buffer_t;

// create a send_buffer with specified capacity
send_buffer_t* send_buffer_create(uint32_t capacity);

// is it possible to write 'len' more bytes to the buffer?
bool send_buffer_can_write(send_buffer_t* send_buffer, uint32_t len);

void send_buffer_write(send_buffer_t* send_buffer, uint8_t* data, uint32_t len);

// seq_num : the sequence num that is actually received and acked, not the raw ack_num in tcp header,
// but ack_num - 1
void send_buffer_ack(send_buffer_t* send_buffer, uint32_t seq_num);

// send 'len' bytes starting from last_byte_sent_index
void send_buffer_send(send_buffer_t* send_buffer, uint32_t len);

#endif  // PROJECT_2_15_441_INC_SEND_BUFFER_H_