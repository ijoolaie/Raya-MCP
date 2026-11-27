#include "read-frame.h"

#include <stdio.h>
#include <string.h>

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s\n", label); ++failed; } \
} while (0)

static void write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static raya_frame_view make_frame(uint16_t type, uint32_t sequence,
    const uint8_t *payload, uint32_t length)
{
    raya_frame_view frame;
    frame.type = type;
    frame.sequence = sequence;
    frame.payload = payload;
    frame.payload_length = length;
    return frame;
}

static void test_wire_encoding(void)
{
    uint8_t payload[] = { 0x10, 0x20, 0x30 };
    uint8_t wire[RAYA_FRAME_WIRE_CAPACITY];
    size_t written = 0;
    raya_frame_view input = make_frame(RAYA_FRAME_DATA, 0x01020304u,
        payload, sizeof(payload));
    raya_frame_view decoded;

    CHECK("encode valid frame",
        raya_frame_encode(&input, wire, sizeof(wire), &written));
    CHECK("wire magic and version exact",
        written == 19 && memcmp(wire, "RYA2", 4) == 0 &&
        wire[4] == 2 && wire[5] == 0);
    CHECK("wire type sequence and length little-endian",
        wire[6] == 3 && wire[7] == 0 &&
        wire[8] == 4 && wire[9] == 3 &&
        wire[10] == 2 && wire[11] == 1 &&
        wire[12] == 3 && wire[13] == 0 &&
        wire[14] == 0 && wire[15] == 0);
    CHECK("decode round trip",
        raya_frame_decode(wire, written, &decoded) &&
        decoded.type == input.type &&
        decoded.sequence == input.sequence &&
        decoded.payload_length == sizeof(payload) &&
        memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

static void test_malformed_frames(void)
{
    uint8_t wire[RAYA_FRAME_WIRE_CAPACITY];
    uint8_t payload[4] = { 1, 2, 3, 4 };
    size_t written = 0;
    raya_frame_view input = make_frame(RAYA_FRAME_ERROR, 7, payload, 4);
    raya_frame_view decoded;

    raya_frame_encode(&input, wire, sizeof(wire), &written);
    CHECK("reject truncated header", !raya_frame_decode(wire, 15, &decoded));
    CHECK("reject truncated payload", !raya_frame_decode(wire, written - 1, &decoded));

    wire[0] = 'X';
    CHECK("reject bad magic", !raya_frame_decode(wire, written, &decoded));
    wire[0] = 'R';
    wire[4] = 3;
    CHECK("reject unsupported version", !raya_frame_decode(wire, written, &decoded));
    wire[4] = 2;
    wire[6] = 99;
    wire[7] = 0;
    CHECK("reject unknown frame type", !raya_frame_decode(wire, written, &decoded));
    wire[6] = RAYA_FRAME_ERROR;
    wire[7] = 0;
    wire[12] = 5;
    CHECK("reject declared length mismatch", !raya_frame_decode(wire, written, &decoded));
    wire[12] = 4;
    CHECK("reject trailing bytes", !raya_frame_decode(wire, written + 1, &decoded));

    write_u32(wire + 12, RAYA_MAX_FRAME_PAYLOAD + 1u);
    CHECK("reject oversized declared payload", !raya_frame_decode(wire,
        RAYA_FRAME_HEADER_SIZE + RAYA_MAX_FRAME_PAYLOAD + 1u, &decoded));
}

static void test_encoder_limits(void)
{
    uint8_t payload[RAYA_MAX_FRAME_PAYLOAD];
    uint8_t wire[RAYA_FRAME_WIRE_CAPACITY];
    size_t written = 0;
    raya_frame_view frame = make_frame(RAYA_FRAME_DATA, 0, payload,
        RAYA_MAX_FRAME_PAYLOAD);

    CHECK("encode maximum data frame",
        raya_frame_encode(&frame, wire, sizeof(wire), &written) &&
        written == sizeof(wire));
    CHECK("reject insufficient output capacity",
        !raya_frame_encode(&frame, wire, sizeof(wire) - 1, &written));
    frame.payload_length = RAYA_MAX_FRAME_PAYLOAD + 1u;
    CHECK("reject oversized encoder payload",
        !raya_frame_encode(&frame, wire, sizeof(wire), &written));
    frame.payload_length = 1;
    frame.payload = NULL;
    CHECK("reject missing payload pointer", !raya_frame_encode(&frame,
        wire, sizeof(wire), &written));
}

static bool transfer_response(uint32_t total, uint32_t chunk)
{
    static uint8_t body[RAYA_MAX_RESPONSE_BYTES];
    uint8_t wire[RAYA_FRAME_WIRE_CAPACITY];
    raya_response_tx tx;
    raya_response_rx rx;
    uint32_t offset = 0;
    unsigned frame_count = 0;

    for (uint32_t i = 0; i < total; ++i)
        body[i] = (uint8_t)(i * 31u + 7u);

    if (!raya_response_tx_init(&tx, total, chunk))
        return false;
    raya_response_rx_init(&rx);

    while (!tx.complete) {
        raya_frame_view outgoing;
        raya_frame_view incoming;
        raya_frame_view ack;
        size_t wire_length = 0;
        uint32_t data_length = 0;
        const uint8_t *data = NULL;

        if (tx.header_sent && tx.sent_bytes < tx.total_bytes) {
            uint32_t remaining = tx.total_bytes - tx.sent_bytes;
            data_length = remaining < tx.chunk_size ? remaining : tx.chunk_size;
            data = body + offset;
        }

        if (!raya_response_tx_next(&tx, data, data_length, &outgoing) ||
            !raya_frame_encode(&outgoing, wire, sizeof(wire), &wire_length) ||
            !raya_frame_decode(wire, wire_length, &incoming) ||
            !raya_response_rx_accept(&rx, &incoming) ||
            !raya_response_rx_make_ack(&rx, &ack) ||
            !raya_response_tx_accept_ack(&tx, &ack))
            return false;

        if (incoming.type == RAYA_FRAME_DATA)
            offset += incoming.payload_length;
        ++frame_count;
    }

    return rx.ended && rx.received_bytes == total &&
        offset == total && frame_count == (total == 0 ? 2u :
        2u + (total + chunk - 1u) / chunk);
}

static void test_response_boundaries(void)
{
    CHECK("zero-byte response completes with header and END",
        transfer_response(0, RAYA_MAX_FRAME_PAYLOAD));
    CHECK("one-byte response completes",
        transfer_response(1, RAYA_MAX_FRAME_PAYLOAD));
    CHECK("exactly one chunk completes",
        transfer_response(RAYA_MAX_FRAME_PAYLOAD, RAYA_MAX_FRAME_PAYLOAD));
    CHECK("chunk plus one uses two DATA frames",
        transfer_response(RAYA_MAX_FRAME_PAYLOAD + 1u, RAYA_MAX_FRAME_PAYLOAD));
    CHECK("exact 256 KiB response uses sixteen DATA frames",
        transfer_response(RAYA_MAX_RESPONSE_BYTES, RAYA_MAX_FRAME_PAYLOAD));

    raya_response_tx tx;
    CHECK("reject response above 256 KiB",
        !raya_response_tx_init(&tx, RAYA_MAX_RESPONSE_BYTES + 1u,
            RAYA_MAX_FRAME_PAYLOAD));
    CHECK("reject zero chunk size",
        !raya_response_tx_init(&tx, 1, 0));
    CHECK("reject oversized chunk size",
        !raya_response_tx_init(&tx, 1, RAYA_MAX_FRAME_PAYLOAD + 1u));
    CHECK("reject response requiring more than sixteen data frames",
        !raya_response_tx_init(&tx, RAYA_MAX_RESPONSE_BYTES, RAYA_MAX_FRAME_PAYLOAD - 1u));
}

static void test_ack_and_sequence_guards(void)
{
    raya_response_tx tx;
    raya_response_rx rx;
    raya_frame_view frame;
    raya_frame_view ack;
    uint8_t body[] = { 0xaa, 0xbb };
    uint8_t wrong_ack_payload[4] = { 1, 0, 0, 0 };

    raya_response_tx_init(&tx, sizeof(body), 1);
    raya_response_rx_init(&rx);

    CHECK("sender emits response header",
        raya_response_tx_next(&tx, NULL, 0, &frame));
    CHECK("sender blocks next frame until ACK",
        !raya_response_tx_next(&tx, NULL, 0, &frame));

    CHECK("receiver accepts header",
        raya_response_rx_accept(&rx, &frame));
    CHECK("receiver creates matching ACK",
        raya_response_rx_make_ack(&rx, &ack));
    CHECK("sender rejects ACK with wrong payload sequence",
        !raya_response_tx_accept_ack(&tx,
            &(raya_frame_view){ RAYA_FRAME_ACK, 0, wrong_ack_payload, 4 }));
    CHECK("sender rejects future ACK sequence",
        !raya_response_tx_accept_ack(&tx,
            &(raya_frame_view){ RAYA_FRAME_ACK, 1, wrong_ack_payload, 4 }));
    CHECK("sender rejects truncated ACK payload",
        !raya_response_tx_accept_ack(&tx,
            &(raya_frame_view){ RAYA_FRAME_ACK, 0, wrong_ack_payload, 3 }));
    CHECK("sender rejects non-ACK frame as acknowledgement",
        !raya_response_tx_accept_ack(&tx,
            &(raya_frame_view){ RAYA_FRAME_DATA, 0, wrong_ack_payload, 4 }));
    CHECK("sender accepts exact ACK", raya_response_tx_accept_ack(&tx, &ack));
    CHECK("sender rejects duplicate ACK", !raya_response_tx_accept_ack(&tx, &ack));

    CHECK("sender emits first data frame",
        raya_response_tx_next(&tx, body, 1, &frame));
    CHECK("receiver rejects skipped sequence",
        !raya_response_rx_accept(&rx,
            &(raya_frame_view){ RAYA_FRAME_DATA, 2, body, 1 }));
    CHECK("receiver accepts correct sequence",
        raya_response_rx_accept(&rx, &frame));
    CHECK("receiver ACKs first data frame",
        raya_response_rx_make_ack(&rx, &ack));
    CHECK("sender rejects stale ACK sequence",
        !raya_response_tx_accept_ack(&tx,
            &(raya_frame_view){ RAYA_FRAME_ACK, 0, wrong_ack_payload, 4 }));
    CHECK("sender accepts first data ACK",
        raya_response_tx_accept_ack(&tx, &ack));

    CHECK("sender emits second data frame",
        raya_response_tx_next(&tx, body + 1, 1, &frame));
    CHECK("receiver accepts second data frame",
        raya_response_rx_accept(&rx, &frame));
    raya_response_rx_make_ack(&rx, &ack);
    raya_response_tx_accept_ack(&tx, &ack);

    CHECK("sender emits END",
        raya_response_tx_next(&tx, NULL, 0, &frame));
    CHECK("receiver accepts END only after full body",
        raya_response_rx_accept(&rx, &frame));
    CHECK("receiver rejects duplicate END",
        !raya_response_rx_accept(&rx, &frame));
    CHECK("receiver rejects DATA after END",
        !raya_response_rx_accept(&rx,
            &(raya_frame_view){ RAYA_FRAME_DATA, 4, body, 1 }));
    raya_response_rx_make_ack(&rx, &ack);
    CHECK("sender completes only after END ACK",
        raya_response_tx_accept_ack(&tx, &ack) && tx.complete);
    CHECK("sender rejects future ACK after completion",
        !raya_response_tx_accept_ack(&tx, &ack));
}

static void test_receiver_rejects_invalid_order(void)
{
    raya_response_rx rx;
    uint8_t header_payload[8] = { 1, 0, 0, 0, 1, 0, 0, 0 };
    uint8_t one_byte = 0x5a;
    raya_frame_view frame;

    raya_response_rx_init(&rx);
    frame = make_frame(RAYA_FRAME_DATA, 0, &one_byte, 1);
    CHECK("reject DATA before response header",
        !raya_response_rx_accept(&rx, &frame));

    frame = make_frame(RAYA_FRAME_RESPONSE_HEADER, 0, header_payload, 8);
    CHECK("accept valid response header",
        raya_response_rx_accept(&rx, &frame));

    frame = make_frame(RAYA_FRAME_END, 1, NULL, 0);
    CHECK("reject END before all response bytes",
        !raya_response_rx_accept(&rx, &frame));

    frame = make_frame(RAYA_FRAME_DATA, 1, &one_byte, 1);
    CHECK("accept expected DATA frame",
        raya_response_rx_accept(&rx, &frame));
    CHECK("reject any frame after END only once END received",
        raya_response_rx_accept(&rx,
            &(raya_frame_view){ RAYA_FRAME_END, 2, NULL, 0 }) &&
        !raya_response_rx_accept(&rx,
            &(raya_frame_view){ RAYA_FRAME_ERROR, 3, &one_byte, 1 }));
}

static void test_receiver_rejects_invalid_header_bounds(void)
{
    raya_response_rx rx;
    uint8_t header[8];
    raya_frame_view frame;

    write_u32(header, RAYA_MAX_RESPONSE_BYTES);
    write_u32(header + 4, RAYA_MAX_FRAME_PAYLOAD - 1u);
    raya_response_rx_init(&rx);
    frame = make_frame(RAYA_FRAME_RESPONSE_HEADER, 0, header, sizeof(header));
    CHECK("receiver rejects response requiring more than sixteen chunks",
        !raya_response_rx_accept(&rx, &frame) && !rx.header_received);

    write_u32(header, RAYA_MAX_RESPONSE_BYTES + 1u);
    write_u32(header + 4, RAYA_MAX_FRAME_PAYLOAD);
    raya_response_rx_init(&rx);
    CHECK("receiver rejects response above 256 KiB",
        !raya_response_rx_accept(&rx, &frame) && !rx.header_received);

    write_u32(header, 1);
    write_u32(header + 4, RAYA_MAX_FRAME_PAYLOAD + 1u);
    raya_response_rx_init(&rx);
    CHECK("receiver rejects oversized chunk declaration",
        !raya_response_rx_accept(&rx, &frame) && !rx.header_received);
}
int main(void)
{
    test_wire_encoding();
    test_malformed_frames();
    test_encoder_limits();
    test_response_boundaries();
    test_ack_and_sequence_guards();
    test_receiver_rejects_invalid_order();
    test_receiver_rejects_invalid_header_bounds();

    printf("READ_FRAME_TESTS: %u passed, %u failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
