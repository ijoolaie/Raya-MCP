#include "read-frame.h"

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
