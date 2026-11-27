#include "read-frame.h"

#include <stdio.h>
#include <string.h>

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s\n", label); ++failed; } \
} while (0)

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static raya_frame_view make_frame(
    uint16_t type, uint32_t sequence, const uint8_t *payload, uint32_t length)
{
    raya_frame_view frame;
    frame.type = type;
    frame.sequence = sequence;
    frame.payload = payload;
    frame.payload_length = length;
    return frame;
}

static void test_request_round_trip(void)
{
    raya_request source;
    raya_request decoded;
    uint8_t payload[RAYA_MAX_FRAME_PAYLOAD];
    size_t written = 0;
    raya_frame_view frame;

    memset(&source, 0, sizeof(source));
    source.operation = RAYA_OPERATION_READ_FILE;
    strcpy_s(source.root_id, sizeof(source.root_id), "fixture");
    wcscpy_s(source.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\guide.txt");

    CHECK("encode READ_FILE request payload",
        raya_request_encode_payload(&source, payload, sizeof(payload), &written));
    frame = make_frame(RAYA_FRAME_REQUEST, 0, payload, (uint32_t)written);
    CHECK("decode READ_FILE request frame",
        raya_request_decode_frame(&frame, &decoded));
    CHECK("request round-trip preserves operation and rootId",
        decoded.operation == RAYA_OPERATION_READ_FILE &&
        strcmp(decoded.root_id, "fixture") == 0);
    CHECK("request round-trip preserves relative UTF-16 path",
        wcscmp(decoded.relative_path, L"docs\\guide.txt") == 0);
}

static void test_list_root_request(void)
{
    raya_request source;
    raya_request decoded;
    uint8_t payload[RAYA_MAX_FRAME_PAYLOAD];
    size_t written = 0;
    raya_frame_view frame;

    memset(&source, 0, sizeof(source));
    source.operation = RAYA_OPERATION_LIST_DIRECTORY;
    strcpy_s(source.root_id, sizeof(source.root_id), "workspace");

    CHECK("encode root LIST_DIRECTORY request with empty relative path",
        raya_request_encode_payload(&source, payload, sizeof(payload), &written));
    frame = make_frame(RAYA_FRAME_REQUEST, 0, payload, (uint32_t)written);
    CHECK("decode root LIST_DIRECTORY request",
        raya_request_decode_frame(&frame, &decoded));
    CHECK("root LIST_DIRECTORY remains empty path",
        decoded.operation == RAYA_OPERATION_LIST_DIRECTORY &&
        decoded.relative_path[0] == L'\0');
}

static void test_request_encoder_rejects_invalid_values(void)
{
    raya_request request;
    uint8_t payload[RAYA_MAX_FRAME_PAYLOAD];
    size_t written = 0;

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path, RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"..\\secret.txt");
    CHECK("encoder rejects dot-dot traversal", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    wcscpy_s(request.relative_path, RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"C:\\secret.txt");
    CHECK("encoder rejects rooted path", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    wcscpy_s(request.relative_path, RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"dir/file.txt");
    CHECK("encoder rejects alternate separator", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    wcscpy_s(request.relative_path, RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"file.txt:stream");
    CHECK("encoder rejects alternate data stream", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    request.relative_path[0] = L'\0';
    CHECK("READ_FILE rejects empty path", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    request.operation = 99;
    CHECK("encoder rejects unknown operation", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    request.operation = RAYA_OPERATION_LIST_DIRECTORY;
    strcpy_s(request.root_id, sizeof(request.root_id), "bad/root");
    CHECK("encoder rejects rootId path separators", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    request.root_id[0] = '\0';
    CHECK("encoder rejects empty rootId", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    memset(request.root_id, 'a', sizeof(request.root_id));
    CHECK("encoder rejects unterminated/oversized rootId", !raya_request_encode_payload(&request, payload, sizeof(payload), &written));

    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    request.relative_path[0] = L'a';
    request.relative_path[1] = L'\0';
    CHECK("encoder rejects insufficient payload capacity",
        !raya_request_encode_payload(&request, payload, 1, &written));
}

static void test_request_decoder_rejects_invalid_frames(void)
{
    uint8_t payload[64] = { 0 };
    raya_request decoded;
    raya_frame_view frame;
    const char root[] = "fixture";

    put_u16(payload, RAYA_OPERATION_READ_FILE);
    put_u16(payload + 2, 7);
    put_u16(payload + 4, 8);
    put_u16(payload + 6, 0);
    memcpy(payload + 8, root, 7);
    const wchar_t path[] = L"file.txt";
    for (size_t i = 0; i < 8; ++i)
        put_u16(payload + 15 + i * 2u, (uint16_t)path[i]);

    frame = make_frame(RAYA_FRAME_REQUEST, 0, payload, 31);
    CHECK("decoder accepts manually encoded request", raya_request_decode_frame(&frame, &decoded));

    frame.type = RAYA_FRAME_DATA;
    CHECK("decoder rejects non-REQUEST frame", !raya_request_decode_frame(&frame, &decoded));
    frame.type = RAYA_FRAME_REQUEST;
    frame.sequence = 1;
    CHECK("decoder rejects nonzero request sequence", !raya_request_decode_frame(&frame, &decoded));
    frame.sequence = 0;

    put_u16(payload + 6, 1);
    CHECK("decoder rejects nonzero reserved field", !raya_request_decode_frame(&frame, &decoded));
    put_u16(payload + 6, 0);

    frame.payload_length = 30;
    CHECK("decoder rejects truncated request payload", !raya_request_decode_frame(&frame, &decoded));
    frame.payload_length = 32;
    CHECK("decoder rejects trailing request payload bytes", !raya_request_decode_frame(&frame, &decoded));
    frame.payload_length = 31;

    put_u16(payload, 99);
    CHECK("decoder rejects unknown operation code", !raya_request_decode_frame(&frame, &decoded));
    put_u16(payload, RAYA_OPERATION_READ_FILE);

    put_u16(payload + 2, 0);
    CHECK("decoder rejects empty rootId", !raya_request_decode_frame(&frame, &decoded));
    put_u16(payload + 2, 7);

    payload[8] = '/';
    CHECK("decoder rejects invalid rootId characters", !raya_request_decode_frame(&frame, &decoded));
    payload[8] = 'f';

    put_u16(payload + 4, 0);
    frame.payload_length = 15;
    CHECK("decoder rejects empty READ_FILE path", !raya_request_decode_frame(&frame, &decoded));
    put_u16(payload, RAYA_OPERATION_LIST_DIRECTORY);
    CHECK("decoder accepts empty LIST_DIRECTORY path",
        raya_request_decode_frame(&frame, &decoded));
    put_u16(payload, RAYA_OPERATION_READ_FILE);
    put_u16(payload + 4, 8);
    frame.payload_length = 31;

    put_u16(payload + 15, 0xd800u);
    put_u16(payload + 17, (uint16_t)L'i');
    CHECK("decoder rejects unpaired UTF-16 high surrogate",
        !raya_request_decode_frame(&frame, &decoded));

    put_u16(payload + 15, (uint16_t)L'.');
    put_u16(payload + 17, (uint16_t)L'.');
    put_u16(payload + 19, (uint16_t)L'\\');
    put_u16(payload + 21, (uint16_t)L's');
    put_u16(payload + 23, (uint16_t)L'e');
    put_u16(payload + 25, (uint16_t)L'c');
    put_u16(payload + 27, (uint16_t)L'r');
    put_u16(payload + 29, (uint16_t)L'e');
    put_u16(payload + 31, (uint16_t)L't');
    put_u16(payload + 4, 9);
    frame.payload_length = 33;
    CHECK("decoder rejects dot-dot traversal path",
        !raya_request_decode_frame(&frame, &decoded));
}

static void test_error_frame_contract(void)
{
    raya_error_info error = { RAYA_ERROR_ACCESS_DENIED, false };
    raya_error_info decoded;
    uint8_t payload[8];
    uint8_t ack_payload[4];
    raya_frame_view frame;
    raya_frame_view ack;

    CHECK("encode ACCESS_DENIED error payload",
        raya_error_encode_payload(&error, payload));
    frame = make_frame(RAYA_FRAME_ERROR, 0, payload, sizeof(payload));
    CHECK("decode ACCESS_DENIED error frame",
        raya_error_decode_frame(&frame, &decoded));
    CHECK("error code and retryable flag preserved",
        decoded.code == RAYA_ERROR_ACCESS_DENIED && !decoded.retryable);
    CHECK("error frame produces matching ACK",
        raya_error_make_ack(&frame, ack_payload, &ack) &&
        ack.type == RAYA_FRAME_ACK && ack.sequence == 0 &&
        ack.payload_length == 4 && ack.payload[0] == 0);

    error.code = RAYA_ERROR_RESOURCE_LIMIT;
    error.retryable = true;
    CHECK("encode retryable RESOURCE_LIMIT error",
        raya_error_encode_payload(&error, payload));
    frame = make_frame(RAYA_FRAME_ERROR, 0, payload, sizeof(payload));
    CHECK("decode retryable error",
        raya_error_decode_frame(&frame, &decoded) &&
        decoded.code == RAYA_ERROR_RESOURCE_LIMIT && decoded.retryable);

    error.code = 99;
    CHECK("encoder rejects unknown error code",
        !raya_error_encode_payload(&error, payload));

    frame.type = RAYA_FRAME_DATA;
    CHECK("decoder rejects non-ERROR frame",
        !raya_error_decode_frame(&frame, &decoded));
    frame.type = RAYA_FRAME_ERROR;
    frame.sequence = 1;
    CHECK("decoder rejects nonzero ERROR sequence",
        !raya_error_decode_frame(&frame, &decoded));
    frame.sequence = 0;
    frame.payload_length = 7;
    CHECK("decoder rejects short ERROR payload",
        !raya_error_decode_frame(&frame, &decoded));
    frame.payload_length = 8;

    put_u32(payload, 99);
    CHECK("decoder rejects unknown numeric error code",
        !raya_error_decode_frame(&frame, &decoded));
    put_u32(payload, RAYA_ERROR_INTERNAL);
    payload[4] = 2;
    CHECK("decoder rejects invalid retryable flag",
        !raya_error_decode_frame(&frame, &decoded));
    payload[4] = 0;
    payload[5] = 1;
    CHECK("decoder rejects nonzero reserved error bytes",
        !raya_error_decode_frame(&frame, &decoded));
    CHECK("ACK builder rejects malformed ERROR frame",
        !raya_error_make_ack(&frame, ack_payload, &ack));
}

int main(void)
{
    test_request_round_trip();
    test_list_root_request();
    test_request_encoder_rejects_invalid_values();
    test_request_decoder_rejects_invalid_frames();
    test_error_frame_contract();

    printf("REQUEST_ERROR_CONTRACT_TESTS: %u passed, %u failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
