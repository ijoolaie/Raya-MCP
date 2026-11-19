#include "raya-durable-wal-v1.h"
#include <stdio.h>
#include <string.h>
static unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
static BYTE journal[192*256];
static size_t used;
static BYTE ns[16];
static void append(DWORD type,ULONGLONG op,DWORD kind,DWORD effect,DWORD attempt,DWORD disp,
 const BYTE *payload,const BYTE *metadata,const BYTE *previous_override) {
 raya_wal_record_fields_v1 f; BYTE *out=journal+used;
 memset(&f,0,sizeof(f)); f.record_type=type; f.journal_sequence=(ULONGLONG)(used/192)+1;
 memcpy(f.name_space,ns,16); f.operation_sequence=op; f.operation_kind=kind;
 f.effect_kind=effect; f.attempt=attempt; f.disposition=disp;
 if(payload) memcpy(f.payload_digest,payload,32);
 if(metadata) memcpy(f.metadata_digest,metadata,32);
 if(previous_override) memcpy(f.previous_record_digest,previous_override,32);
 else if(used) memcpy(f.previous_record_digest,journal+used-64,32);
 CHECK(raya_wal_record_encode_v1(&f,out)); used+=192;
}
static void valid_journal(void) {
 BYTE payload[32],meta[32]; memset(payload,0x31,sizeof(payload));
 memset(meta,0x72,sizeof(meta)); used=0; memset(journal,0,sizeof(journal));
 append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 append(RAYA_WAL_ID_RESERVE,7,3,0,0,0,payload,NULL,NULL);
 append(RAYA_WAL_REQUEST_ACCEPTED,7,3,0,0,0,payload,NULL,NULL);
 append(RAYA_WAL_EFFECT_BEGIN,7,3,RAYA_WAL_EFFECT_ISSUE,1,0,payload,NULL,NULL);
 append(RAYA_WAL_EFFECT_RESULT,7,3,RAYA_WAL_EFFECT_ISSUE,1,RAYA_WAL_RESULT_UNKNOWN,payload,NULL,NULL);
 append(RAYA_WAL_RECONCILIATION,7,3,RAYA_WAL_EFFECT_ISSUE,1,RAYA_WAL_RESULT_PROVEN_NOT_COMMITTED,payload,meta,NULL);
 append(RAYA_WAL_EFFECT_BEGIN,7,3,RAYA_WAL_EFFECT_ISSUE,2,0,payload,NULL,NULL);
 append(RAYA_WAL_EFFECT_RESULT,7,3,RAYA_WAL_EFFECT_ISSUE,2,RAYA_WAL_RESULT_COMMITTED,payload,NULL,NULL);
 append(RAYA_WAL_OPERATION_TERMINAL,7,3,0,0,0,payload,NULL,NULL);
}
static raya_wal_scan_status_v1 scan(const BYTE *p,size_t n,raya_wal_scan_result_v1 *r) {
 return raya_wal_scan_v1(p,n,ns,r);
}
int main(void) {
 raya_wal_scan_result_v1 r; BYTE saved[sizeof(journal)],bad[sizeof(journal)+191];
 BYTE wrong[32]; size_t i; DWORD old_used;
 memset(ns,0xA5,sizeof(ns)); valid_journal();
 CHECK(used==9u*192u);
 CHECK(scan(journal,used,&r)==RAYA_WAL_VALID_COMPLETE);
 CHECK(r.records_scanned==9 && r.operations_tracked==1 && r.unresolved_effects==0);
 CHECK(r.last_journal_sequence==9 && r.valid_bytes==used);
 memcpy(saved,journal,used); CHECK(memcmp(saved,journal,used)==0);
 for(i=1;i<192;i++) {
  memcpy(bad,journal,used); memset(bad+used,0xCC,i);
  CHECK(scan(bad,used+i,&r)==RAYA_WAL_INCOMPLETE_FINAL_RECORD);
  CHECK(r.valid_bytes==used && r.records_scanned==9);
 }
 memcpy(bad,journal,used); bad[128]^=1;
 CHECK(scan(bad,used,&r)==RAYA_WAL_CORRUPT);
 memcpy(bad,journal,used); bad[96+192*2]^=1;
 /* Chain mutation without recomputing digest is still corruption. */
 CHECK(scan(bad,used,&r)==RAYA_WAL_CORRUPT);
 CHECK(scan(NULL,used,&r)==RAYA_WAL_INVALID_ARGUMENT);
 CHECK(scan(journal,0,&r)==RAYA_WAL_EMPTY);
 CHECK(scan(journal,used,&r)==RAYA_WAL_VALID_COMPLETE);
 CHECK(scan(journal,used+1u,&r)==RAYA_WAL_INCOMPLETE_FINAL_RECORD);
 CHECK(r.valid_bytes==used && r.records_scanned==9);
 /* Wrong expected namespace. */
 memset(wrong,0x5A,sizeof(wrong));
 CHECK(raya_wal_scan_v1(journal,used,wrong,&r)==RAYA_WAL_CORRUPT);
 /* First record cannot be anything but header. */
 memcpy(bad,journal,used); bad[6]=RAYA_WAL_ID_RESERVE;
 CHECK(scan(bad,used,&r)==RAYA_WAL_CORRUPT);
 /* Unknown version and malformed fixed size. */
 memcpy(bad,journal,used); bad[4]=2;
 CHECK(scan(bad,used,&r)==RAYA_WAL_CORRUPT);
 memcpy(bad,journal,used); bad[8]=0;
 CHECK(scan(bad,used,&r)==RAYA_WAL_CORRUPT);
 /* Invalid header namespace / nonzero header payload. */
 memcpy(bad,journal,used); bad[24]=0;
 CHECK(scan(bad,used,&r)==RAYA_WAL_CORRUPT);
 memcpy(bad,journal,used); bad[64]=1;
 CHECK(scan(bad,used,&r)==RAYA_WAL_CORRUPT);
 /* Record encoder rejects invalid type-specific fields. */
 {
  raya_wal_record_fields_v1 f; BYTE out[192]; memset(&f,0,sizeof(f));
  f.record_type=RAYA_WAL_EFFECT_BEGIN; f.journal_sequence=1;
  memcpy(f.name_space,ns,16); f.operation_sequence=1; f.operation_kind=1;
  memset(f.payload_digest,1,32); f.effect_kind=1; f.attempt=0;
  CHECK(!raya_wal_record_encode_v1(&f,out));
  f.attempt=1; f.disposition=99; CHECK(!raya_wal_record_encode_v1(&f,out));
  f.disposition=0; CHECK(raya_wal_record_encode_v1(&f,out));
 }
 /* A validly hashed record with a broken predecessor link is corrupt. */
 used=0; append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32],broken[32]; memset(p,0x21,32); memset(broken,0xBC,32);
   append(RAYA_WAL_ID_RESERVE,11,1,0,0,0,p,NULL,broken);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_CORRUPT);
 /* UNKNOWN is a valid quarantined terminal effect, not a replay instruction. */
 used=0; append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32]; memset(p,0x22,32);
   append(RAYA_WAL_ID_RESERVE,12,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_REQUEST_ACCEPTED,12,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_EFFECT_BEGIN,12,1,1,1,0,p,NULL,NULL);
   append(RAYA_WAL_EFFECT_RESULT,12,1,1,1,RAYA_WAL_RESULT_UNKNOWN,p,NULL,NULL);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_VALID_COMPLETE);
 CHECK(r.unresolved_effects==1);
 /* A result without an in-progress matching attempt is invalid. */
 used=0; append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32]; memset(p,0x23,32);
   append(RAYA_WAL_ID_RESERVE,13,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_REQUEST_ACCEPTED,13,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_EFFECT_RESULT,13,1,1,1,RAYA_WAL_RESULT_COMMITTED,p,NULL,NULL);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_CORRUPT);
 /* A duplicate reservation is structurally valid but transition-invalid. */
 used=0; append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32]; memset(p,0x44,32);
   append(RAYA_WAL_ID_RESERVE,2,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_ID_RESERVE,2,1,0,0,0,p,NULL,NULL);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_CORRUPT);
 /* Reservation sequences are strictly increasing within one namespace journal. */
 used=0; append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32]; memset(p,0x54,32);
   append(RAYA_WAL_ID_RESERVE,20,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_ID_RESERVE,19,1,0,0,0,p,NULL,NULL);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_CORRUPT);
 /* A begin without acceptance is rejected. */
 used=0; append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32]; memset(p,0x55,32);
   append(RAYA_WAL_ID_RESERVE,3,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_EFFECT_BEGIN,3,1,1,1,0,p,NULL,NULL);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_CORRUPT);
 /* In-progress effect cannot be terminalized. */
 used=0; append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32]; memset(p,0x66,32);
   append(RAYA_WAL_ID_RESERVE,4,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_REQUEST_ACCEPTED,4,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_EFFECT_BEGIN,4,1,1,1,0,p,NULL,NULL);
   append(RAYA_WAL_OPERATION_TERMINAL,4,1,0,0,0,p,NULL,NULL);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_CORRUPT);
 /* Unknown result cannot be followed by a blind retry. */
 used=0; append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32]; memset(p,0x77,32);
   append(RAYA_WAL_ID_RESERVE,5,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_REQUEST_ACCEPTED,5,1,0,0,0,p,NULL,NULL);
   append(RAYA_WAL_EFFECT_BEGIN,5,1,1,1,0,p,NULL,NULL);
   append(RAYA_WAL_EFFECT_RESULT,5,1,1,1,RAYA_WAL_RESULT_UNKNOWN,p,NULL,NULL);
   append(RAYA_WAL_EFFECT_BEGIN,5,1,1,2,0,p,NULL,NULL);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_CORRUPT);
 /* Operation table exhaustion is LIMIT_EXCEEDED, never partial validity. */
 used=0; memset(journal,0,sizeof(journal));
 append(RAYA_WAL_HEADER,0,0,0,0,0,NULL,NULL,NULL);
 { BYTE p[32]; ULONGLONG k; memset(p,0x29,32);
   for(k=1;k<=129;k++) append(RAYA_WAL_ID_RESERVE,k,1,0,0,0,p,NULL,NULL);
 }
 CHECK(scan(journal,used,&r)==RAYA_WAL_LIMIT_EXCEEDED);
 /* Scanner input remains byte-for-byte unchanged. */
 valid_journal(); memcpy(saved,journal,used); old_used=(DWORD)used;
 CHECK(scan(journal,used,&r)==RAYA_WAL_VALID_COMPLETE);
 CHECK(memcmp(saved,journal,old_used)==0);
 printf("checks=%u failures=%u\n",checks,failures);
 return failures?1:0;
}
