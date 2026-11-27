#ifndef RAYA_READ_FRAME_H
#define RAYA_READ_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RAYA_FRAME_HEADER_SIZE 16u
#define RAYA_MAX_FRAME_PAYLOAD 16384u
#define RAYA_MAX_RESPONSE_BYTES 262144u
#define RAYA_MAX_DATA_FRAMES 16u
#define RAYA_FRAME_WIRE_CAPACITY (RAYA_FRAME_HEADER_SIZE + RAYA_MAX_FRAME_PAYLOAD)

typedef enum raya_frame_type {
    RAYA_FRAME_REQUEST = 1,
    RAYA_FRAME_RESPONSE_HEADER = 2,
    RAYA_FRAME_DATA = 3,
    RAYA_FRAME_END = 4,
    RAYA_FRAME_ERROR = 5,
    RAYA_FRAME_ACK = 6
} raya_frame_type;

typedef struct raya_frame_view {
    uint16_t type;
    uint32_t sequence;
    const uint8_t *payload;
    uint32_t payload_length;
} raya_frame_view;

typedef struct raya_response_tx {
    uint32_t total_bytes;
    uint32_t chunk_size;
    uint32_t sent_bytes;
    uint32_t next_sequence;
    uint32_t pending_sequence;
    bool header_sent;
    bool end_sent;
    bool awaiting_ack;
    bool complete;
    uint8_t header_payload[8];
} raya_response_tx;

typedef struct raya_response_rx {
    uint32_t total_bytes;
    uint32_t chunk_size;
    uint32_t received_bytes;
    uint32_t expected_sequence;
    uint32_t last_sequence;
    bool header_received;
    bool has_last_frame;
    bool ended;
    uint8_t ack_payload[4];
} raya_response_rx;

bool raya_frame_type_valid(uint16_t type);
bool raya_frame_encode(
    const raya_frame_view *frame,
    uint8_t *output,
    size_t output_capacity,
    size_t *written);
bool raya_frame_decode(
    const uint8_t *input,
    size_t input_length,
    raya_frame_view *frame);

bool raya_response_tx_init(
    raya_response_tx *tx,
    uint32_t total_bytes,
    uint32_t chunk_size);
bool raya_response_tx_next(
    raya_response_tx *tx,
    const uint8_t *data,
    uint32_t data_length,
    raya_frame_view *frame);
bool raya_response_tx_accept_ack(
    raya_response_tx *tx,
    const raya_frame_view *ack);

void raya_response_rx_init(raya_response_rx *rx);
bool raya_response_rx_accept(
    raya_response_rx *rx,
    const raya_frame_view *frame);
bool raya_response_rx_make_ack(
    raya_response_rx *rx,
    raya_frame_view *ack);

#endif
