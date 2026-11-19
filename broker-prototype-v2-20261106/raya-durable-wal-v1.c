#include "raya-durable-wal-v1.h"
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib,"bcrypt.lib")

typedef struct {
 ULONGLONG sequence;
 DWORD kind;
 BYTE digest[32];
 int accepted;
 int terminal;
 struct { DWORD attempt; DWORD state; } effect[2];
} wal_operation;

static ULONGLONG get64(const BYTE *p) {
 ULONGLONG v=0; unsigned i;
 for(i=0;i<8;i++) v|=((ULONGLONG)p[i])<<(8u*i);
 return v;
}
static DWORD get32(const BYTE *p) {
 return (DWORD)p[0]|((DWORD)p[1]<<8)|((DWORD)p[2]<<16)|((DWORD)p[3]<<24);
}
static void put64(BYTE *p, ULONGLONG v) {
 unsigned i; for(i=0;i<8;i++) p[i]=(BYTE)(v>>(8u*i));
}
static void put32(BYTE *p, DWORD v) {
 p[0]=(BYTE)v; p[1]=(BYTE)(v>>8); p[2]=(BYTE)(v>>16); p[3]=(BYTE)(v>>24);
}
static int zero_bytes(const BYTE *p,size_t n) {
 size_t i; BYTE v=0; for(i=0;i<n;i++) v|=p[i]; return v==0;
}
static int sha256(const BYTE *data, ULONG length, BYTE out[32]) {
 BCRYPT_ALG_HANDLE alg=NULL; BCRYPT_HASH_HANDLE hash=NULL;
 PUCHAR object=NULL; DWORD object_size=0,got=0; NTSTATUS st; int ok=0;
 st=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0); if(st<0) goto done;
 st=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&object_size,sizeof(object_size),&got,0);
 if(st<0 || got!=sizeof(object_size) || object_size==0) goto done;
 object=(PUCHAR)malloc(object_size); if(!object) goto done;
 st=BCryptCreateHash(alg,&hash,object,object_size,NULL,0,0); if(st<0) goto done;
 st=BCryptHashData(hash,(PUCHAR)data,length,0); if(st<0) goto done;
 st=BCryptFinishHash(hash,out,32,0); if(st<0) goto done;
 ok=1;
done:
 if(hash) BCryptDestroyHash(hash);
 if(alg) BCryptCloseAlgorithmProvider(alg,0);
 if(object) { SecureZeroMemory(object,object_size); free(object); }
 return ok;
}
static int valid_fields(const raya_wal_record_fields_v1 *f) {
 if(!f || f->record_type<RAYA_WAL_HEADER || f->record_type>RAYA_WAL_RECONCILIATION) return 0;
 if(f->journal_sequence==0 || zero_bytes(f->name_space,16)) return 0;
 if(f->record_type==RAYA_WAL_HEADER)
  return f->journal_sequence==1 && f->operation_sequence==0 && f->operation_kind==0 &&
   f->effect_kind==0 && f->attempt==0 && f->disposition==0 &&
   zero_bytes(f->payload_digest,32) && zero_bytes(f->metadata_digest,32) &&
   zero_bytes(f->previous_record_digest,32);
 if(f->operation_sequence==0 || f->operation_kind<1 || f->operation_kind>4 ||
    zero_bytes(f->payload_digest,32)) return 0;
 switch(f->record_type) {
 case RAYA_WAL_ID_RESERVE:
 case RAYA_WAL_REQUEST_ACCEPTED:
 case RAYA_WAL_OPERATION_TERMINAL:
  return f->effect_kind==0 && f->attempt==0 && f->disposition==0 &&
   zero_bytes(f->metadata_digest,32);
 case RAYA_WAL_EFFECT_BEGIN:
  return f->effect_kind>=RAYA_WAL_EFFECT_ISSUE && f->effect_kind<=RAYA_WAL_EFFECT_HANDOFF &&
   f->attempt>0 && f->disposition==0 && zero_bytes(f->metadata_digest,32);
 case RAYA_WAL_EFFECT_RESULT:
  return f->effect_kind>=RAYA_WAL_EFFECT_ISSUE && f->effect_kind<=RAYA_WAL_EFFECT_HANDOFF &&
   f->attempt>0 && f->disposition>=RAYA_WAL_RESULT_COMMITTED &&
   f->disposition<=RAYA_WAL_RESULT_UNKNOWN && zero_bytes(f->metadata_digest,32);
 case RAYA_WAL_RECONCILIATION:
  return f->effect_kind>=RAYA_WAL_EFFECT_ISSUE && f->effect_kind<=RAYA_WAL_EFFECT_HANDOFF &&
   f->attempt>0 && (f->disposition==RAYA_WAL_RESULT_COMMITTED ||
   f->disposition==RAYA_WAL_RESULT_PROVEN_NOT_COMMITTED) &&
   !zero_bytes(f->metadata_digest,32);
 default: return 0;
 }
}
int raya_wal_record_encode_v1(const raya_wal_record_fields_v1 *f,BYTE out[192]) {
 BYTE digest[32];
 if(!out || !valid_fields(f)) return 0;
 memset(out,0,192); memcpy(out,"RYW1",4);
 out[4]=1; out[5]=0; out[6]=(BYTE)f->record_type; out[7]=0;
 put32(out+8,192); put32(out+12,192); put64(out+16,f->journal_sequence);
 memcpy(out+24,f->name_space,16); put64(out+40,f->operation_sequence);
 put32(out+48,f->operation_kind); put32(out+52,f->effect_kind);
 put32(out+56,f->attempt); put32(out+60,f->disposition);
 memcpy(out+64,f->payload_digest,32); memcpy(out+96,f->previous_record_digest,32);
 memcpy(out+160,f->metadata_digest,32);
 if(!sha256(out,192,digest)) { SecureZeroMemory(out,192); return 0; }
 memcpy(out+128,digest,32); SecureZeroMemory(digest,sizeof(digest)); return 1;
}
static int find_operation(wal_operation *ops,DWORD count,ULONGLONG seq) {
 DWORD i; for(i=0;i<count;i++) if(ops[i].sequence==seq) return (int)i; return -1;
}
static int apply_record(wal_operation *ops,DWORD *count,const BYTE *r) {
 DWORD type=(DWORD)r[6] | ((DWORD)r[7]<<8);
 ULONGLONG seq=get64(r+40); DWORD kind=get32(r+48),effect=get32(r+52);
 DWORD attempt=get32(r+56),disp=get32(r+60); int ix; wal_operation *op;
 if(type==RAYA_WAL_HEADER) return *count==0;
 ix=find_operation(ops,*count,seq);
 if(type==RAYA_WAL_ID_RESERVE) {
  DWORD i;
  if(ix>=0 || *count>=RAYA_WAL_V1_MAX_OPERATIONS) return 0;
  /* Namespace sequence reservations must be strictly increasing in journal order. */
  for(i=0;i<*count;i++) if(ops[i].sequence>=seq) return 0;
  op=&ops[*count]; memset(op,0,sizeof(*op)); op->sequence=seq; op->kind=kind;
  memcpy(op->digest,r+64,32); ++*count; return 1;
 }
 if(ix<0) return 0;
 op=&ops[ix];
 if(op->kind!=kind || memcmp(op->digest,r+64,32)!=0 || op->terminal) return 0;
 switch(type) {
 case RAYA_WAL_REQUEST_ACCEPTED:
  if(op->accepted) return 0; op->accepted=1; return 1;
 case RAYA_WAL_EFFECT_BEGIN:
  if(!op->accepted || effect<1 || effect>2) return 0;
  if(op->effect[effect-1].state==0) {
   if(attempt!=1) return 0;
  } else if(op->effect[effect-1].state==3) {
   if(op->effect[effect-1].attempt==MAXDWORD ||
      attempt!=op->effect[effect-1].attempt+1) return 0;
  } else return 0;
  op->effect[effect-1].attempt=attempt; op->effect[effect-1].state=1; return 1;
 case RAYA_WAL_EFFECT_RESULT:
  if(!op->accepted || effect<1 || effect>2 ||
     op->effect[effect-1].state!=1 || op->effect[effect-1].attempt!=attempt) return 0;
  op->effect[effect-1].state=(disp==RAYA_WAL_RESULT_COMMITTED)?2:
   (disp==RAYA_WAL_RESULT_PROVEN_NOT_COMMITTED)?3:4; return 1;
 case RAYA_WAL_RECONCILIATION:
  if(!op->accepted || effect<1 || effect>2 ||
     op->effect[effect-1].state!=4 || op->effect[effect-1].attempt!=attempt) return 0;
  op->effect[effect-1].state=(disp==RAYA_WAL_RESULT_COMMITTED)?2:3; return 1;
 case RAYA_WAL_OPERATION_TERMINAL:
  if(!op->accepted || (op->effect[0].state!=2 && op->effect[0].state!=3 &&
      op->effect[1].state!=2 && op->effect[1].state!=3)) return 0;
  if(op->effect[0].state==1 || op->effect[0].state==4 ||
     op->effect[1].state==1 || op->effect[1].state==4) return 0;
  op->terminal=1; return 1;
 default: return 0;
 }
}
raya_wal_scan_status_v1 raya_wal_scan_v1(const BYTE *bytes,size_t length,
 const BYTE expected_namespace[16],raya_wal_scan_result_v1 *result) {
 wal_operation *ops=NULL; BYTE copy[192],digest[32],ns[16],prev[32];
 size_t offset=0; DWORD count=0,records=0,unresolved=0; ULONGLONG last_seq=0;
 raya_wal_scan_status_v1 status=RAYA_WAL_CORRUPT;
 if(!result) return RAYA_WAL_INVALID_ARGUMENT;
 memset(result,0,sizeof(*result)); result->status=RAYA_WAL_INVALID_ARGUMENT;
 if(!bytes || !expected_namespace || zero_bytes(expected_namespace,16))
  return result->status;
 if(length==0) { result->status=RAYA_WAL_EMPTY; return result->status; }
 /* Enforce the record bound while scanning, not from total input length,
  * so LIMIT_EXCEEDED still reports the exact validated prefix. */
 ops=(wal_operation*)calloc(RAYA_WAL_V1_MAX_OPERATIONS,sizeof(*ops));
 if(!ops) { result->status=RAYA_WAL_LIMIT_EXCEEDED; return result->status; }
 memset(ns,0,sizeof(ns)); memset(prev,0,sizeof(prev));
 while(offset<length) {
  DWORD type; ULONGLONG journal_seq;
  if(length-offset<192) { status=RAYA_WAL_INCOMPLETE_FINAL_RECORD; break; }
  if(records>=RAYA_WAL_V1_MAX_RECORDS) { status=RAYA_WAL_LIMIT_EXCEEDED; break; }
  memcpy(copy,bytes+offset,192);
  if(memcmp(copy,"RYW1",4)!=0 || copy[4]!=1 || copy[5]!=0 ||
     copy[7]!=0 || get32(copy+8)!=192 || get32(copy+12)!=192) goto done;
  type=(DWORD)copy[6]; journal_seq=get64(copy+16);
  if(type<RAYA_WAL_HEADER || type>RAYA_WAL_RECONCILIATION ||
     journal_seq!=(ULONGLONG)records+1) goto done;
  if(records==0) {
   if(type!=RAYA_WAL_HEADER || memcmp(copy+96,(BYTE[32]){0},32)!=0 ||
      memcmp(copy+24,expected_namespace,16)!=0) goto done;
   memcpy(ns,copy+24,16);
  } else {
   if(type==RAYA_WAL_HEADER || memcmp(copy+24,ns,16)!=0 ||
      memcmp(copy+96,prev,32)!=0) goto done;
  }
  memset(copy+128,0,32);
  if(!sha256(copy,192,digest)) { status=RAYA_WAL_CRYPTO_ERROR; goto done; }
  if(memcmp(digest,bytes+offset+128,32)!=0) goto done;
  memcpy(copy,bytes+offset,192);
  {
   raya_wal_record_fields_v1 f;
   memset(&f,0,sizeof(f)); f.record_type=type; f.journal_sequence=journal_seq;
   memcpy(f.name_space,copy+24,16); f.operation_sequence=get64(copy+40);
   f.operation_kind=get32(copy+48); f.effect_kind=get32(copy+52);
   f.attempt=get32(copy+56); f.disposition=get32(copy+60);
   memcpy(f.payload_digest,copy+64,32); memcpy(f.previous_record_digest,copy+96,32);
   memcpy(f.metadata_digest,copy+160,32);
   if(!valid_fields(&f)) goto done;
   if(type==RAYA_WAL_ID_RESERVE && count>=RAYA_WAL_V1_MAX_OPERATIONS &&
      find_operation(ops,count,f.operation_sequence)<0) {
    status=RAYA_WAL_LIMIT_EXCEEDED; goto done;
   }
   if(!apply_record(ops,&count,copy)) goto done;
  }
  memcpy(prev,bytes+offset+128,32); last_seq=journal_seq;
  ++records; offset+=192;
 }
 if(offset==length) status=RAYA_WAL_VALID_COMPLETE;
done:
 if(status==RAYA_WAL_VALID_COMPLETE || status==RAYA_WAL_INCOMPLETE_FINAL_RECORD ||
    status==RAYA_WAL_LIMIT_EXCEEDED) {
  DWORD i,j; for(i=0;i<count;i++) for(j=0;j<2;j++)
   if(ops[i].effect[j].state==1 || ops[i].effect[j].state==4) ++unresolved;
  result->valid_bytes=offset; result->last_journal_sequence=last_seq;
  memcpy(result->name_space,ns,16); memcpy(result->last_record_digest,prev,32);
  result->records_scanned=records; result->operations_tracked=count;
  result->unresolved_effects=unresolved;
 } else {
  result->valid_bytes=offset; result->records_scanned=records;
 }
 result->status=status;
 SecureZeroMemory(digest,sizeof(digest)); SecureZeroMemory(copy,sizeof(copy));
 SecureZeroMemory(prev,sizeof(prev)); free(ops); return status;
}
