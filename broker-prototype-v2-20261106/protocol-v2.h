#ifndef RAYA_PROTOCOL_V2_H
#define RAYA_PROTOCOL_V2_H
#include <stddef.h>
#define RAYA_V2_MAX_REQUEST 512
#define RAYA_V2_MAX_ROOT_ID 32
#define RAYA_V2_MAX_RELATIVE_PATH 384
typedef enum { RAYA_V2_OP_INVALID=0, RAYA_V2_OP_READ, RAYA_V2_OP_INFO, RAYA_V2_OP_TAIL } raya_v2_operation;
typedef struct {
    raya_v2_operation operation;
    char root_id[RAYA_V2_MAX_ROOT_ID+1];
    char relative_path[RAYA_V2_MAX_RELATIVE_PATH+1];
} raya_v2_request;
/* Parses exactly one ASCII request line, including its final LF. No filesystem access. */
int raya_v2_parse_request(const unsigned char *input, size_t length, raya_v2_request *out);
#endif
