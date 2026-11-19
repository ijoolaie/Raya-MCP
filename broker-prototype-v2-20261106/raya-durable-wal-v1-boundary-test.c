#include "raya-durable-wal-v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static BYTE ns[16], payload[32], meta[32];
static BYTE *buf;
static size_t used, cap;
static unsigned checks, failures;
#define T(x) do { ++checks; if(!(x)) { ++failures; printf("FAIL %s:%d %s\\n",__FILE__,__LINE__,#x); } } while(0)
static int add(DWORD type, ULONGLONG op, DWORD kind, DWORD effect, DWORD attempt, DWORD disp, int with_meta) {
 raya_wal_record_fields_v1 f; BYTE *p; size_t n;
 if(used+192>cap) return 0;
 memset(&f,0,sizeof(f)); f.record_type=type; f.journal_sequence=(ULONGLONG)(used/192)+1;
 memcpy(f.name_space,ns,16); f.operation_sequence=op; f.operation_kind=kind;
 f.effect_kind=effect; f.attempt=attempt; f.disposition=disp;
 if(type!=RAYA_WAL_HEADER) memcpy(f.payload_digest,payload,32);
 if(with_meta) memcpy(f.metadata_digest,meta,32);
 if(used) memcpy(f.previous_record_digest,buf+used-64,32);
 p=buf+used; n=used;
 if(!raya_wal_record_encode_v1(&f,p)) return 0;
 used=n+192; return 1;
}
static void reset(void) { used=0; memset(buf,0,cap); }
static raya_wal_scan_status_v1 scan(raya_wal_scan_result_v1 *r) {
 return raya_wal_scan_v1(buf,used,ns,r);
}
int main(void) {
 raya_wal_scan_result_v1 r; ULONGLONG i;
 cap=(size_t)RAYA_WAL_V1_MAX_RECORDS*192u+192u;
 buf=(BYTE*)malloc(cap); if(!buf) return 2;
 memset(ns,0xA5,16); memset(payload,0x31,32); memset(meta,0x72,32);
 /* Exact maximum record count: header + reserve + accept + 2,046
    determinate retry attempts (begin/result) + terminal = 4,096. */
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 T(add(RAYA_WAL_ID_RESERVE,1,1,0,0,0,0));
 T(add(RAYA_WAL_REQUEST_ACCEPTED,1,1,0,0,0,0));
 for(i=1;i<=2046;i++) {
  T(add(RAYA_WAL_EFFECT_BEGIN,1,1,RAYA_WAL_EFFECT_ISSUE,(DWORD)i,0,0));
  T(add(RAYA_WAL_EFFECT_RESULT,1,1,RAYA_WAL_EFFECT_ISSUE,(DWORD)i,RAYA_WAL_RESULT_PROVEN_NOT_COMMITTED,0));
 }
 T(add(RAYA_WAL_OPERATION_TERMINAL,1,1,0,0,0,0));
 T(used==(size_t)4096*192u);
 T(scan(&r)==RAYA_WAL_VALID_COMPLETE);
 T(r.records_scanned==4096 && r.last_journal_sequence==4096);
 T(r.valid_bytes==used && r.operations_tracked==1 && r.unresolved_effects==0);
 /* A complete extra record must fail closed at the record limit. */
 T(add(RAYA_WAL_ID_RESERVE,2,1,0,0,0,0));
 T(scan(&r)==RAYA_WAL_LIMIT_EXCEEDED);
 T(r.records_scanned==4096 && r.last_journal_sequence==4096);
 T(r.valid_bytes==(size_t)4096*192u && r.operations_tracked==1);
 /* Exactly 128 operations are accepted; the 129th is a limit result. */
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 for(i=1;i<=128;i++) T(add(RAYA_WAL_ID_RESERVE,i,1,0,0,0,0));
 T(scan(&r)==RAYA_WAL_VALID_COMPLETE);
 T(r.operations_tracked==128 && r.records_scanned==129 && r.valid_bytes==used);
 T(add(RAYA_WAL_ID_RESERVE,129,1,0,0,0,0));
 T(scan(&r)==RAYA_WAL_LIMIT_EXCEEDED);
 T(r.operations_tracked==128 && r.records_scanned==129 && r.valid_bytes==(size_t)129*192u);
 /* ULONGLONG maximum is valid once; no larger reservation can follow it. */
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 T(add(RAYA_WAL_ID_RESERVE,~(ULONGLONG)0,1,0,0,0,0));
 T(scan(&r)==RAYA_WAL_VALID_COMPLETE && r.operations_tracked==1);
 T(add(RAYA_WAL_ID_RESERVE,1,1,0,0,0,0));
 T(scan(&r)==RAYA_WAL_CORRUPT);
 /* Both effect kinds have independent attempt counters and can complete. */
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 T(add(RAYA_WAL_ID_RESERVE,9,2,0,0,0,0));
 T(add(RAYA_WAL_REQUEST_ACCEPTED,9,2,0,0,0,0));
 T(add(RAYA_WAL_EFFECT_BEGIN,9,2,RAYA_WAL_EFFECT_ISSUE,1,0,0));
 T(add(RAYA_WAL_EFFECT_RESULT,9,2,RAYA_WAL_EFFECT_ISSUE,1,RAYA_WAL_RESULT_PROVEN_NOT_COMMITTED,0));
 T(add(RAYA_WAL_EFFECT_BEGIN,9,2,RAYA_WAL_EFFECT_ISSUE,2,0,0));
 T(add(RAYA_WAL_EFFECT_RESULT,9,2,RAYA_WAL_EFFECT_ISSUE,2,RAYA_WAL_RESULT_COMMITTED,0));
 T(add(RAYA_WAL_EFFECT_BEGIN,9,2,RAYA_WAL_EFFECT_HANDOFF,1,0,0));
 T(add(RAYA_WAL_EFFECT_RESULT,9,2,RAYA_WAL_EFFECT_HANDOFF,1,RAYA_WAL_RESULT_COMMITTED,0));
 T(add(RAYA_WAL_OPERATION_TERMINAL,9,2,0,0,0,0));
 T(scan(&r)==RAYA_WAL_VALID_COMPLETE && r.unresolved_effects==0);
 /* Reconciliation must bind the exact unresolved effect and attempt. */
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 T(add(RAYA_WAL_ID_RESERVE,10,2,0,0,0,0));
 T(add(RAYA_WAL_REQUEST_ACCEPTED,10,2,0,0,0,0));
 T(add(RAYA_WAL_EFFECT_BEGIN,10,2,RAYA_WAL_EFFECT_ISSUE,1,0,0));
 T(add(RAYA_WAL_EFFECT_RESULT,10,2,RAYA_WAL_EFFECT_ISSUE,1,RAYA_WAL_RESULT_UNKNOWN,0));
 T(add(RAYA_WAL_RECONCILIATION,10,2,RAYA_WAL_EFFECT_ISSUE,2,RAYA_WAL_RESULT_PROVEN_NOT_COMMITTED,1));
 T(scan(&r)==RAYA_WAL_CORRUPT);
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 T(add(RAYA_WAL_ID_RESERVE,11,2,0,0,0,0));
 T(add(RAYA_WAL_REQUEST_ACCEPTED,11,2,0,0,0,0));
 T(add(RAYA_WAL_EFFECT_BEGIN,11,2,RAYA_WAL_EFFECT_ISSUE,1,0,0));
 T(add(RAYA_WAL_EFFECT_RESULT,11,2,RAYA_WAL_EFFECT_ISSUE,1,RAYA_WAL_RESULT_UNKNOWN,0));
 T(add(RAYA_WAL_RECONCILIATION,11,2,RAYA_WAL_EFFECT_HANDOFF,1,RAYA_WAL_RESULT_COMMITTED,1));
 T(scan(&r)==RAYA_WAL_CORRUPT);
 /* Current terminal rule accepts either single effect plane independently.
  * These fixtures expose a policy question; they do not approve that policy. */
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 T(add(RAYA_WAL_ID_RESERVE,20,2,0,0,0,0));
 T(add(RAYA_WAL_REQUEST_ACCEPTED,20,2,0,0,0,0));
 T(add(RAYA_WAL_EFFECT_BEGIN,20,2,RAYA_WAL_EFFECT_ISSUE,1,0,0));
 T(add(RAYA_WAL_EFFECT_RESULT,20,2,RAYA_WAL_EFFECT_ISSUE,1,RAYA_WAL_RESULT_COMMITTED,0));
 T(add(RAYA_WAL_OPERATION_TERMINAL,20,2,0,0,0,0));
 T(scan(&r)==RAYA_WAL_VALID_COMPLETE);
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 T(add(RAYA_WAL_ID_RESERVE,21,2,0,0,0,0));
 T(add(RAYA_WAL_REQUEST_ACCEPTED,21,2,0,0,0,0));
 T(add(RAYA_WAL_EFFECT_BEGIN,21,2,RAYA_WAL_EFFECT_HANDOFF,1,0,0));
 T(add(RAYA_WAL_EFFECT_RESULT,21,2,RAYA_WAL_EFFECT_HANDOFF,1,RAYA_WAL_RESULT_COMMITTED,0));
 T(add(RAYA_WAL_OPERATION_TERMINAL,21,2,0,0,0,0));
 T(scan(&r)==RAYA_WAL_VALID_COMPLETE);
 reset(); T(add(RAYA_WAL_HEADER,0,0,0,0,0,0));
 T(add(RAYA_WAL_ID_RESERVE,22,2,0,0,0,0));
 T(add(RAYA_WAL_REQUEST_ACCEPTED,22,2,0,0,0,0));
 T(add(RAYA_WAL_OPERATION_TERMINAL,22,2,0,0,0,0));
 T(scan(&r)==RAYA_WAL_CORRUPT);
 printf("checks=%u failures=%u\\n",checks,failures);
 free(buf); return failures?1:0;
}
