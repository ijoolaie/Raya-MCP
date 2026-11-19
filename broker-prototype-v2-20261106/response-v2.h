#ifndef RAYA_RESPONSE_V2_H
#define RAYA_RESPONSE_V2_H
#include <stddef.h>
#include <stdint.h>

#define RAYA_V2_FRAME_HEADER_SIZE 16u
#define RAYA_V2_MAX_FRAME_PAYLOAD 65536u
#define RAYA_V2_FRAME_REQUEST 1u
#define RAYA_V2_FRAME_RESPONSE 2u

/* Decoded payload is a borrowed view into the input buffer. The caller must
   keep that buffer alive and unchanged while the frame/payload is in use. */
typedef struct {
    uint8_t type;
    uint32_t request_id;
    const unsigned char *payload;
    uint32_t payload_length;
} raya_v2_frame;

/* Encode requires output storage not to overlap the payload source. */
int raya_v2_frame_encode(uint8_t type, uint32_t request_id,
    const unsigned char *payload, uint32_t payload_length,
    unsigned char *output, size_t output_capacity, size_t *written);
int raya_v2_frame_decode(const unsigned char *input, size_t input_length,
    raya_v2_frame *out);
#endif
