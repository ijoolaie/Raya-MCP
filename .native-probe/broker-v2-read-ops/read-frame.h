#ifndef RAYA_READ_FRAME_H
#define RAYA_READ_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define RAYA_FRAME_HEADER_SIZE 16u
#define RAYA_MAX_FRAME_PAYLOAD 16384u
#define RAYA_MAX_RESPONSE_BYTES 262144u
#define RAYA_MAX_DATA_FRAMES 16u
#define RAYA_FRAME_WIRE_CAPACITY (RAYA_FRAME_HEADER_SIZE + RAYA_MAX_FRAME_PAYLOAD)
#define RAYA_MAX_ROOT_ID_BYTES 64u
#define RAYA_REQUEST_FIXED_BYTES 20u
#define RAYA_MAX_REQUEST_PATH_CHARS ((RAYA_MAX_FRAME_PAYLOAD - RAYA_REQUEST_FIXED_BYTES - RAYA_MAX_ROOT_ID_BYTES) / 2u)

typedef enum raya_frame_type {
    RAYA_FRAME_REQUEST = 1,
    RAYA_FRAME_RESPONSE_HEADER = 2,
    RAYA_FRAME_DATA = 3,
    RAYA_FRAME_END = 4,
    RAYA_FRAME_ERROR = 5,
    RAYA_FRAME_ACK = 6
} raya_frame_type;

typedef enum raya_operation {
    RAYA_OPERATION_READ_FILE = 1,
    RAYA_OPERATION_LIST_DIRECTORY = 2,
    RAYA_OPERATION_GET_FILE_INFO = 3,
    RAYA_OPERATION_READ_FILE_CHUNK = 4,
    RAYA_OPERATION_READ_FILE_TAIL = 5,
    RAYA_OPERATION_WORKSPACE_ROOTS = 6
} raya_operation;

typedef enum raya_error_code {
    RAYA_ERROR_BAD_REQUEST = 1,
    RAYA_ERROR_UNKNOWN_ROOT = 2,
    RAYA_ERROR_NOT_FOUND = 3,
    RAYA_ERROR_ACCESS_DENIED = 4,
    RAYA_ERROR_UNSUPPORTED_OPERATION = 5,
    RAYA_ERROR_RESOURCE_LIMIT = 6,
    RAYA_ERROR_INTERNAL = 7
} raya_error_code;

typedef struct raya_frame_view {
    uint16_t type;
    uint32_t sequence;
    const uint8_t *payload;
    uint32_t payload_length;
} raya_frame_view;

typedef struct raya_request {
    uint16_t operation;
    char root_id[RAYA_MAX_ROOT_ID_BYTES + 1u];
    wchar_t relative_path[RAYA_MAX_REQUEST_PATH_CHARS + 1u];
    uint64_t offset;
    uint32_t length;
} raya_request;

typedef struct raya_error_info {
    uint32_t code;
    bool retryable;
} raya_error_info;

typedef struct raya_deadline {
    uint64_t expires_at_ms;
} raya_deadline;

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

bool raya_request_encode_payload(
    const raya_request *request,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *written);
bool raya_request_decode_frame(
    const raya_frame_view *frame,
    raya_request *request);

bool raya_error_encode_payload(
    const raya_error_info *error,
    uint8_t payload[8]);
bool raya_error_decode_frame(
    const raya_frame_view *frame,
    raya_error_info *error);
bool raya_error_make_ack(
    const raya_frame_view *error_frame,
    uint8_t ack_payload[4],
    raya_frame_view *ack);

bool raya_deadline_init(
    raya_deadline *deadline,
    uint64_t now_ms,
    uint32_t timeout_ms);
uint32_t raya_deadline_remaining_ms(
    const raya_deadline *deadline,
    uint64_t now_ms);
uint32_t raya_deadline_frame_budget_ms(
    const raya_deadline *deadline,
    uint64_t now_ms,
    uint32_t per_frame_timeout_ms);

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
