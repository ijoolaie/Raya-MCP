#include "response-v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x,n) do { if(!(x)){printf("FAIL %s\n",n);return 1;} } while(0)
int main(void) {
    unsigned char small[128], large[RAYA_V2_FRAME_HEADER_SIZE+RAYA_V2_MAX_FRAME_PAYLOAD];
    unsigned char payload[RAYA_V2_MAX_FRAME_PAYLOAD];
    size_t written=0;
    raya_v2_frame f;
    const unsigned char msg[]={'o','k'};
    const unsigned char binary_msg[]={0x00u,0x80u,0xffu};
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_RESPONSE,7,msg,2,small,sizeof(small),&written),"encode response");
    CHECK(written==RAYA_V2_FRAME_HEADER_SIZE+2,"encoded length");
    CHECK(raya_v2_frame_decode(small,written,&f),"decode response");
    CHECK(f.request_id==7 && f.payload_length==2 && memcmp(f.payload,msg,2)==0,"response fields");
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_RESPONSE,8,NULL,0,small,sizeof(small),&written),"zero payload");
    CHECK(raya_v2_frame_decode(small,written,&f) && f.payload_length==0,"zero payload decode");
    memset(payload,'Z',sizeof(payload));
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_RESPONSE,9,payload,sizeof(payload),large,sizeof(large),&written),"max payload");
    CHECK(written==sizeof(large) && raya_v2_frame_decode(large,written,&f),"max payload decode");
    CHECK(f.payload_length==RAYA_V2_MAX_FRAME_PAYLOAD,"max payload length");
    CHECK(!raya_v2_frame_encode(RAYA_V2_FRAME_RESPONSE,10,payload,RAYA_V2_MAX_FRAME_PAYLOAD+1u,large,sizeof(large),&written),"reject oversized encode");
    CHECK(!raya_v2_frame_decode(small,RAYA_V2_FRAME_HEADER_SIZE-1,&f),"reject truncated header");
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_RESPONSE,11,msg,2,small,sizeof(small),&written),"prepare truncated payload");
    CHECK(!raya_v2_frame_decode(small,written-1,&f),"reject truncated payload");
    CHECK(!raya_v2_frame_decode(small,written+1,&f),"reject trailing bytes");
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_REQUEST,12,msg,2,small,sizeof(small),&written),"encode request");
    CHECK(raya_v2_frame_decode(small,written,&f),"decode request");
    CHECK(f.type==RAYA_V2_FRAME_REQUEST && f.request_id==12 && f.payload_length==2,"request fields");
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_RESPONSE,12,msg,2,small,sizeof(small),&written),"encode response after request");
    CHECK(raya_v2_frame_decode(small,written,&f) && f.type==RAYA_V2_FRAME_RESPONSE && f.request_id==12,"response matching ID");
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_REQUEST,0x01020304u,binary_msg,sizeof(binary_msg),small,sizeof(small),&written),"encode binary request");
    CHECK(small[0]=='R' && small[1]=='Y' && small[2]=='2' && small[3]=='R' && small[4]==2 && small[5]==RAYA_V2_FRAME_REQUEST,"verify common header fields");
    CHECK(small[8]==1 && small[9]==2 && small[10]==3 && small[11]==4 && small[12]==0 && small[13]==0 && small[14]==0 && small[15]==3,"verify big-endian fields");
    CHECK(raya_v2_frame_decode(small,written,&f) && f.payload_length==sizeof(binary_msg),"decode binary request");
    CHECK(memcmp(f.payload,binary_msg,sizeof(binary_msg))==0,"preserve binary payload");
    small[0]='X';
    CHECK(!raya_v2_frame_decode(small,written,&f),"reject bad magic");
    CHECK(!raya_v2_frame_encode(3u,14,msg,1,small,sizeof(small),&written),"reject unknown frame type on encode");
    CHECK(!raya_v2_frame_encode(RAYA_V2_FRAME_RESPONSE,0,msg,2,small,sizeof(small),&written),"reject zero request ID");
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_REQUEST,16,msg,2,small,sizeof(small),&written),"prepare zero ID decode test");
    small[8]=0; small[9]=0; small[10]=0; small[11]=0;
    CHECK(!raya_v2_frame_decode(small,written,&f),"reject zero request ID on decode");
    CHECK(raya_v2_frame_encode(RAYA_V2_FRAME_RESPONSE,15,msg,2,small,sizeof(small),&written),"prepare malformed header");
    small[4]=3;
    CHECK(!raya_v2_frame_decode(small,written,&f),"reject unsupported version");
    small[4]=2; small[5]=9;
    CHECK(!raya_v2_frame_decode(small,written,&f),"reject unknown frame type");
    small[5]=RAYA_V2_FRAME_RESPONSE; small[6]=1;
    CHECK(!raya_v2_frame_decode(small,written,&f),"reject nonzero reserved bits");
    small[6]=0; small[15]=3;
    CHECK(!raya_v2_frame_decode(small,written,&f),"reject declared length mismatch");
    printf("PASS response-v2 framing cases: 34\n");
    return 0;
}
