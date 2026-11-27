#include "read-frame.h"
#include "relative-path.h"

#include <string.h>

static const uint8_t RAYA_MAGIC[4] = { 'R', 'Y', 'A', '2' };
static const uint16_t RAYA_PROTOCOL_VERSION = 2u;

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static void write_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write_u32_le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

static bool response_bounds_valid(uint32_t total_bytes, uint32_t chunk_size)
{
    uint32_t data_frames;

    if (total_bytes > RAYA_MAX_RESPONSE_BYTES || chunk_size == 0 ||
        chunk_size > RAYA_MAX_FRAME_PAYLOAD)
        return false;

    data_frames = total_bytes == 0 ? 0 :
        (total_bytes + chunk_size - 1u) / chunk_size;
    return data_frames <= RAYA_MAX_DATA_FRAMES;
}

bool raya_frame_type_valid(uint16_t type)
{
    return type >= RAYA_FRAME_REQUEST && type <= RAYA_FRAME_ACK;
}

bool raya_frame_encode(
    const raya_frame_view *frame,
    uint8_t *output,
    size_t output_capacity,
    size_t *written)
{
    size_t required;

    if (written != NULL)
        *written = 0;

    if (frame == NULL || output == NULL || written == NULL ||
        !raya_frame_type_valid(frame->type) ||
        frame->payload_length > RAYA_MAX_FRAME_PAYLOAD ||
        (frame->payload_length != 0 && frame->payload == NULL))
        return false;

    required = RAYA_FRAME_HEADER_SIZE + (size_t)frame->payload_length;
    if (output_capacity < required)
        return false;

    memcpy(output, RAYA_MAGIC, sizeof(RAYA_MAGIC));
    write_u16_le(output + 4, RAYA_PROTOCOL_VERSION);
    write_u16_le(output + 6, frame->type);
    write_u32_le(output + 8, frame->sequence);
    write_u32_le(output + 12, frame->payload_length);

    if (frame->payload_length != 0)
        memcpy(output + RAYA_FRAME_HEADER_SIZE, frame->payload,
            frame->payload_length);

    *written = required;
    return true;
}

bool raya_frame_decode(
    const uint8_t *input,
    size_t input_length,
    raya_frame_view *frame)
{
    uint32_t payload_length;
    uint16_t type;

    if (frame == NULL)
        return false;

    frame->type = 0;
    frame->sequence = 0;
    frame->payload = NULL;
    frame->payload_length = 0;

    if (input == NULL || input_length < RAYA_FRAME_HEADER_SIZE)
        return false;

    if (memcmp(input, RAYA_MAGIC, sizeof(RAYA_MAGIC)) != 0 ||
        read_u16_le(input + 4) != RAYA_PROTOCOL_VERSION)
        return false;

    type = read_u16_le(input + 6);
    payload_length = read_u32_le(input + 12);

    if (!raya_frame_type_valid(type) ||
        payload_length > RAYA_MAX_FRAME_PAYLOAD ||
        input_length != RAYA_FRAME_HEADER_SIZE + (size_t)payload_length)
        return false;

    frame->type = type;
    frame->sequence = read_u32_le(input + 8);
    frame->payload_length = payload_length;
    frame->payload = payload_length == 0 ? NULL : input + RAYA_FRAME_HEADER_SIZE;
    return true;
}

bool raya_response_tx_init(
    raya_response_tx *tx,
    uint32_t total_bytes,
    uint32_t chunk_size)
{
    if (tx == NULL || !response_bounds_valid(total_bytes, chunk_size))
        return false;

    memset(tx, 0, sizeof(*tx));
    tx->total_bytes = total_bytes;
    tx->chunk_size = chunk_size;
    return true;
}

bool raya_response_tx_next(
    raya_response_tx *tx,
    const uint8_t *data,
    uint32_t data_length,
    raya_frame_view *frame)
{
    uint32_t remaining;
    uint32_t expected;

    if (tx == NULL || frame == NULL || tx->complete || tx->awaiting_ack)
        return false;

    frame->sequence = tx->next_sequence;
    frame->payload = NULL;
    frame->payload_length = 0;

    if (!tx->header_sent) {
        if (data_length != 0 || data != NULL)
            return false;

        write_u32_le(tx->header_payload, tx->total_bytes);
        write_u32_le(tx->header_payload + 4, tx->chunk_size);
        frame->type = RAYA_FRAME_RESPONSE_HEADER;
        frame->payload = tx->header_payload;
        frame->payload_length = sizeof(tx->header_payload);
        tx->header_sent = true;
    } else if (tx->sent_bytes < tx->total_bytes) {
        remaining = tx->total_bytes - tx->sent_bytes;
        expected = remaining < tx->chunk_size ? remaining : tx->chunk_size;
        if (data == NULL || data_length != expected)
            return false;

        frame->type = RAYA_FRAME_DATA;
        frame->payload = data;
        frame->payload_length = data_length;
        tx->sent_bytes += data_length;
    } else {
        if (data_length != 0 || data != NULL)
            return false;

        frame->type = RAYA_FRAME_END;
        tx->end_sent = true;
    }

    tx->pending_sequence = tx->next_sequence;
    tx->next_sequence++;
    tx->awaiting_ack = true;
    return true;
}

bool raya_response_tx_accept_ack(
    raya_response_tx *tx,
    const raya_frame_view *ack)
{
    if (tx == NULL || ack == NULL || !tx->awaiting_ack ||
        ack->type != RAYA_FRAME_ACK ||
        ack->sequence != tx->pending_sequence ||
        ack->payload_length != sizeof(uint32_t) ||
        ack->payload == NULL ||
        read_u32_le(ack->payload) != tx->pending_sequence)
        return false;

    tx->awaiting_ack = false;
    if (tx->end_sent)
        tx->complete = true;
    return true;
}

void raya_response_rx_init(raya_response_rx *rx)
{
    if (rx != NULL)
        memset(rx, 0, sizeof(*rx));
}

bool raya_response_rx_accept(
    raya_response_rx *rx,
    const raya_frame_view *frame)
{
    uint32_t expected;
    uint32_t remaining;
    uint32_t total_bytes;
    uint32_t chunk_size;

    if (rx == NULL || frame == NULL || rx->ended ||
        frame->sequence != rx->expected_sequence)
        return false;

    if (!rx->header_received) {
        if (frame->type != RAYA_FRAME_RESPONSE_HEADER ||
            frame->payload == NULL || frame->payload_length != 8)
            return false;

        total_bytes = read_u32_le(frame->payload);
        chunk_size = read_u32_le(frame->payload + 4);
        if (!response_bounds_valid(total_bytes, chunk_size))
            return false;

        rx->total_bytes = total_bytes;
        rx->chunk_size = chunk_size;
        rx->header_received = true;
    } else if (frame->type == RAYA_FRAME_DATA) {
        if (frame->payload == NULL || rx->received_bytes >= rx->total_bytes)
            return false;

        remaining = rx->total_bytes - rx->received_bytes;
        expected = remaining < rx->chunk_size ? remaining : rx->chunk_size;
        if (frame->payload_length != expected)
            return false;

        rx->received_bytes += frame->payload_length;
    } else if (frame->type == RAYA_FRAME_END) {
        if (frame->payload_length != 0 ||
            rx->received_bytes != rx->total_bytes)
            return false;

        rx->ended = true;
    } else {
        return false;
    }

    rx->last_sequence = frame->sequence;
    rx->has_last_frame = true;
    rx->expected_sequence++;
    return true;
}

bool raya_response_rx_make_ack(
    raya_response_rx *rx,
    raya_frame_view *ack)
{
    if (rx == NULL || ack == NULL || !rx->has_last_frame)
        return false;

    write_u32_le(rx->ack_payload, rx->last_sequence);
    ack->type = RAYA_FRAME_ACK;
    ack->sequence = rx->last_sequence;
    ack->payload = rx->ack_payload;
    ack->payload_length = sizeof(rx->ack_payload);
    return true;
}


static bool operation_valid(uint16_t operation)
{
    return operation == RAYA_OPERATION_READ_FILE ||
        operation == RAYA_OPERATION_LIST_DIRECTORY;
}

static bool error_code_valid(uint32_t code)
{
    return code >= RAYA_ERROR_BAD_REQUEST && code <= RAYA_ERROR_INTERNAL;
}

static size_t bounded_string_length(const char *value, size_t maximum)
{
    size_t length = 0;
    while (length < maximum && value[length] != '\0')
        ++length;
    return length;
}

static size_t bounded_wide_length(const wchar_t *value, size_t maximum)
{
    size_t length = 0;
    while (length < maximum && value[length] != L'\0')
        ++length;
    return length;
}

static bool root_id_valid(const char *root_id, size_t length)
{
    if (root_id == NULL || length == 0 || length > RAYA_MAX_ROOT_ID_BYTES)
        return false;

    for (size_t i = 0; i < length; ++i) {
        char ch = root_id[i];
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-'))
            return false;
    }
    return true;
}

static bool utf16_path_valid(const wchar_t *path, size_t length)
{
    size_t components = 0;

    for (size_t i = 0; i < length; ++i) {
        uint16_t code = (uint16_t)path[i];
        if (code == 0)
            return false;

        if (code >= 0xd800u && code <= 0xdbffu) {
            if (i + 1 >= length)
                return false;
            uint16_t low = (uint16_t)path[i + 1];
            if (low < 0xdc00u || low > 0xdfffu)
                return false;
            ++i;
        } else if (code >= 0xdc00u && code <= 0xdfffu) {
            return false;
        }
    }

    return raya_validate_relative_path(path, &components) && components != 0;
}

bool raya_request_encode_payload(
    const raya_request *request,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *written)
{
    size_t root_length;
    size_t path_length;
    size_t required;

    if (written != NULL)
        *written = 0;

    if (request == NULL || payload == NULL || written == NULL ||
        !operation_valid(request->operation))
        return false;

    root_length = bounded_string_length(request->root_id,
        RAYA_MAX_ROOT_ID_BYTES + 1u);
    path_length = bounded_wide_length(request->relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u);

    if (root_length == 0 || root_length > RAYA_MAX_ROOT_ID_BYTES ||
        path_length > RAYA_MAX_REQUEST_PATH_CHARS ||
        !root_id_valid(request->root_id, root_length))
        return false;

    if (request->operation == RAYA_OPERATION_READ_FILE && path_length == 0)
        return false;
    if (path_length != 0 &&
        !utf16_path_valid(request->relative_path, path_length))
        return false;

    required = RAYA_REQUEST_FIXED_BYTES + root_length + path_length * 2u;
    if (payload_capacity < required || required > RAYA_MAX_FRAME_PAYLOAD)
        return false;

    write_u16_le(payload, request->operation);
    write_u16_le(payload + 2, (uint16_t)root_length);
    write_u16_le(payload + 4, (uint16_t)path_length);
    write_u16_le(payload + 6, 0);
    memcpy(payload + RAYA_REQUEST_FIXED_BYTES, request->root_id, root_length);

    for (size_t i = 0; i < path_length; ++i)
        write_u16_le(payload + RAYA_REQUEST_FIXED_BYTES + root_length + i * 2u,
            (uint16_t)request->relative_path[i]);

    *written = required;
    return true;
}

bool raya_request_decode_frame(
    const raya_frame_view *frame,
    raya_request *request)
{
    raya_request candidate;
    uint16_t operation;
    uint16_t root_length;
    uint16_t path_length;
    size_t expected_length;
    const uint8_t *payload;

    if (request == NULL)
        return false;
    memset(request, 0, sizeof(*request));

    if (frame == NULL || frame->type != RAYA_FRAME_REQUEST ||
        frame->sequence != 0 || frame->payload == NULL ||
        frame->payload_length < RAYA_REQUEST_FIXED_BYTES)
        return false;

    payload = frame->payload;
    operation = read_u16_le(payload);
    root_length = read_u16_le(payload + 2);
    path_length = read_u16_le(payload + 4);

    if (!operation_valid(operation) || read_u16_le(payload + 6) != 0 ||
        root_length == 0 || root_length > RAYA_MAX_ROOT_ID_BYTES ||
        path_length > RAYA_MAX_REQUEST_PATH_CHARS)
        return false;

    expected_length = RAYA_REQUEST_FIXED_BYTES + (size_t)root_length +
        (size_t)path_length * 2u;
    if (frame->payload_length != expected_length)
        return false;

    memset(&candidate, 0, sizeof(candidate));
    candidate.operation = operation;
    memcpy(candidate.root_id, payload + RAYA_REQUEST_FIXED_BYTES, root_length);
    candidate.root_id[root_length] = '\0';
    if (!root_id_valid(candidate.root_id, root_length))
        return false;

    for (size_t i = 0; i < path_length; ++i) {
        uint16_t code = read_u16_le(payload + RAYA_REQUEST_FIXED_BYTES +
            root_length + i * 2u);
        candidate.relative_path[i] = (wchar_t)code;
    }
    candidate.relative_path[path_length] = L'\0';

    if (operation == RAYA_OPERATION_READ_FILE && path_length == 0)
        return false;
    if (path_length != 0 &&
        !utf16_path_valid(candidate.relative_path, path_length))
        return false;

    *request = candidate;
    return true;
}

bool raya_error_encode_payload(
    const raya_error_info *error,
    uint8_t payload[8])
{
    if (error == NULL || payload == NULL || !error_code_valid(error->code))
        return false;

    write_u32_le(payload, error->code);
    payload[4] = error->retryable ? 1u : 0u;
    payload[5] = 0;
    payload[6] = 0;
    payload[7] = 0;
    return true;
}

bool raya_error_decode_frame(
    const raya_frame_view *frame,
    raya_error_info *error)
{
    raya_error_info candidate;

    if (error == NULL)
        return false;
    error->code = 0;
    error->retryable = false;

    if (frame == NULL || frame->type != RAYA_FRAME_ERROR ||
        frame->sequence != 0 || frame->payload == NULL ||
        frame->payload_length != 8)
        return false;

    candidate.code = read_u32_le(frame->payload);
    if (!error_code_valid(candidate.code) ||
        frame->payload[4] > 1u ||
        frame->payload[5] != 0 ||
        frame->payload[6] != 0 ||
        frame->payload[7] != 0)
        return false;

    candidate.retryable = frame->payload[4] != 0;
    *error = candidate;
    return true;
}

bool raya_error_make_ack(
    const raya_frame_view *error_frame,
    uint8_t ack_payload[4],
    raya_frame_view *ack)
{
    raya_error_info decoded;

    if (ack_payload == NULL || ack == NULL ||
        !raya_error_decode_frame(error_frame, &decoded))
        return false;

    write_u32_le(ack_payload, error_frame->sequence);
    ack->type = RAYA_FRAME_ACK;
    ack->sequence = error_frame->sequence;
    ack->payload = ack_payload;
    ack->payload_length = sizeof(uint32_t);
    return true;
}

bool raya_deadline_init(
    raya_deadline *deadline,
    uint64_t now_ms,
    uint32_t timeout_ms)
{
    if (deadline == NULL || timeout_ms == 0 ||
        now_ms > UINT64_MAX - (uint64_t)timeout_ms)
        return false;

    deadline->expires_at_ms = now_ms + (uint64_t)timeout_ms;
    return true;
}

uint32_t raya_deadline_remaining_ms(
    const raya_deadline *deadline,
    uint64_t now_ms)
{
    uint64_t remaining;

    if (deadline == NULL || now_ms >= deadline->expires_at_ms)
        return 0;

    remaining = deadline->expires_at_ms - now_ms;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

uint32_t raya_deadline_frame_budget_ms(
    const raya_deadline *deadline,
    uint64_t now_ms,
    uint32_t per_frame_timeout_ms)
{
    uint32_t remaining;

    if (per_frame_timeout_ms == 0)
        return 0;

    remaining = raya_deadline_remaining_ms(deadline, now_ms);
    return remaining < per_frame_timeout_ms ? remaining : per_frame_timeout_ms;
}
