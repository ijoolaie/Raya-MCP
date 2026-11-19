#include "response-v2.h"
#include <string.h>

static void put_u32be(unsigned char *p, uint32_t v) {
    p[0]=(unsigned char)(v>>24); p[1]=(unsigned char)(v>>16);
    p[2]=(unsigned char)(v>>8); p[3]=(unsigned char)v;
}
static uint32_t get_u32be(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
        ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
int raya_v2_frame_encode(uint8_t type, uint32_t request_id,
    const unsigned char *payload, uint32_t payload_length,
    unsigned char *output, size_t output_capacity, size_t *written) {
    size_t total;
    if(written) *written=0;
    if(!output || !written || request_id==0) return 0;
    if(type!=RAYA_V2_FRAME_REQUEST && type!=RAYA_V2_FRAME_RESPONSE) return 0;
    if(payload_length>RAYA_V2_MAX_FRAME_PAYLOAD) return 0;
    if(payload_length!=0 && !payload) return 0;
    total=RAYA_V2_FRAME_HEADER_SIZE+(size_t)payload_length;
    if(output_capacity<total) return 0;
    output[0]='R'; output[1]='Y'; output[2]='2'; output[3]='R';
    output[4]=2; output[5]=type; output[6]=0; output[7]=0;
    put_u32be(output+8,request_id);
    put_u32be(output+12,payload_length);
    if(payload_length) memcpy(output+RAYA_V2_FRAME_HEADER_SIZE,payload,payload_length);
    *written=total;
    return 1;
}
int raya_v2_frame_decode(const unsigned char *input, size_t input_length,
    raya_v2_frame *out) {
    uint32_t request_id, payload_length;
    uint8_t type;
    size_t expected;
    if(!input || !out || input_length<RAYA_V2_FRAME_HEADER_SIZE) return 0;
    if(input[0]!='R'||input[1]!='Y'||input[2]!='2'||input[3]!='R') return 0;
    if(input[4]!=2 || input[6]!=0 || input[7]!=0) return 0;
    type=input[5];
    if(type!=RAYA_V2_FRAME_REQUEST && type!=RAYA_V2_FRAME_RESPONSE) return 0;
    request_id=get_u32be(input+8);
    payload_length=get_u32be(input+12);
    if(request_id==0 || payload_length>RAYA_V2_MAX_FRAME_PAYLOAD) return 0;
    expected=RAYA_V2_FRAME_HEADER_SIZE+(size_t)payload_length;
    if(input_length!=expected) return 0;
    out->type=type;
    out->request_id=request_id;
    out->payload=input+RAYA_V2_FRAME_HEADER_SIZE;
    out->payload_length=payload_length;
    return 1;
}
