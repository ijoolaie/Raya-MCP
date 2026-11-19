#include "raya-durable-wal-v1.h"
#include <stdio.h>
#include <string.h>
static BYTE b[192*16], ns[16], pd[32], md[32];
static size_t used;
static unsigned checks, failures;
#define T(x) do { ++checks; if(!(x)) { ++failures; printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
static void add(DWORD type,DWORD kind,DWORD effect,DWORD attempt,DWORD disp,int meta) {
 raya_wal_record_fields_v1 f; BYTE *p=b+used;
 memset(&f,0,sizeof(f)); f.record_type=type; f.journal_sequence=used/192+1;
 memcpy(f.name_space,ns,16); f.operation_sequence=(type==RAYA_WAL_HEADER)?0:1; f.operation_kind=(type==RAYA_WAL_HEADER)?0:kind;
 f.effect_kind=effect; f.attempt=attempt; f.disposition=disp;
 if(type!=RAYA_WAL_HEADER) memcpy(f.payload_digest,pd,32);
 if(meta) memcpy(f.metadata_digest,md,32);
 if(used) memcpy(f.previous_record_digest,b+used-64,32);
 if(!raya_wal_record_encode_v1(&f,p)) { ++failures; printf("ENCODE FAIL type=%lu\n",(unsigned long)type); return; }
 used+=192;
}
static raya_wal_scan_status_v1 run(int issue,int handoff,DWORD kind) {
 raya_wal_scan_result_v1 r; used=0; memset(b,0,sizeof(b));
 add(RAYA_WAL_HEADER,0,0,0,0,0);
 add(RAYA_WAL_ID_RESERVE,kind,0,0,0,0);
 add(RAYA_WAL_REQUEST_ACCEPTED,kind,0,0,0,0);
 if(issue) { add(RAYA_WAL_EFFECT_BEGIN,kind,RAYA_WAL_EFFECT_ISSUE,1,0,0); add(RAYA_WAL_EFFECT_RESULT,kind,RAYA_WAL_EFFECT_ISSUE,1,(DWORD)issue,0); }
 if(handoff) { add(RAYA_WAL_EFFECT_BEGIN,kind,RAYA_WAL_EFFECT_HANDOFF,1,0,0); add(RAYA_WAL_EFFECT_RESULT,kind,RAYA_WAL_EFFECT_HANDOFF,1,(DWORD)handoff,0); }
 add(RAYA_WAL_OPERATION_TERMINAL,kind,0,0,0,0);
 return raya_wal_scan_v1(b,used,ns,&r);
}
int main(void) {
 unsigned k; memset(ns,0xA5,16); memset(pd,0x31,32); memset(md,0x72,32);
 T(run(0,0,1)==RAYA_WAL_CORRUPT);
 T(run(RAYA_WAL_RESULT_COMMITTED,0,1)==RAYA_WAL_VALID_COMPLETE);
 T(run(RAYA_WAL_RESULT_PROVEN_NOT_COMMITTED,0,1)==RAYA_WAL_VALID_COMPLETE);
 T(run(RAYA_WAL_RESULT_UNKNOWN,0,1)==RAYA_WAL_CORRUPT);
 T(run(0,RAYA_WAL_RESULT_COMMITTED,1)==RAYA_WAL_VALID_COMPLETE);
 T(run(RAYA_WAL_RESULT_COMMITTED,RAYA_WAL_RESULT_COMMITTED,1)==RAYA_WAL_VALID_COMPLETE);
 T(run(RAYA_WAL_RESULT_COMMITTED,RAYA_WAL_RESULT_UNKNOWN,1)==RAYA_WAL_CORRUPT);
 for(k=1;k<=4;k++) T(run(RAYA_WAL_RESULT_COMMITTED,0,k)==RAYA_WAL_VALID_COMPLETE);
 printf("checks=%u failures=%u\n",checks,failures);
 return failures?1:0;
}