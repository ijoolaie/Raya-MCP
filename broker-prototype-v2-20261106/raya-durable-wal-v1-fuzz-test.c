#include "raya-durable-wal-v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FUZZ_CASES 20000u
#define MAX_INPUT 4096u
#define GUARD 32u
static unsigned long long rng_state=0xD1B54A32D192ED03ULL;
static unsigned long long next_random(void) {
 rng_state^=rng_state>>12; rng_state^=rng_state<<25; rng_state^=rng_state>>27;
 return rng_state*2685821657736338717ULL;
}
static unsigned checks=0,failures=0;
#define VERIFY(x) do { ++checks; if(!(x)) { ++failures; if(failures<20) printf("FUZZ_FAIL line=%d case=%u: %s\n",__LINE__,case_no,#x); } } while(0)
int main(void) {
 BYTE storage[GUARD+MAX_INPUT+GUARD],snapshot[sizeof(storage)],ns[16];
 BYTE out[192],out_before[192]; unsigned case_no; 
 raya_wal_scan_result_v1 result; raya_wal_record_fields_v1 fields;
 memset(ns,0xA7,sizeof(ns));
 for(case_no=0;case_no<FUZZ_CASES;case_no++) {
  size_t length=(size_t)(next_random()%(MAX_INPUT+1u));
  size_t i; raya_wal_scan_status_v1 status;
  memset(storage,0xD3,sizeof(storage));
  for(i=0;i<length;i++) storage[GUARD+i]=(BYTE)next_random();
  memcpy(snapshot,storage,sizeof(storage));
  status=raya_wal_scan_v1(storage+GUARD,length,ns,&result);
  VERIFY(status>=RAYA_WAL_EMPTY && status<=RAYA_WAL_INVALID_ARGUMENT);
  VERIFY(result.status==status);
  VERIFY(result.valid_bytes<=length);
  VERIFY(memcmp(storage,snapshot,sizeof(storage))==0);
  if(length==0) VERIFY(status==RAYA_WAL_EMPTY);
  if(status==RAYA_WAL_VALID_COMPLETE)
   VERIFY(result.valid_bytes==length);
  if(status==RAYA_WAL_INCOMPLETE_FINAL_RECORD)
   VERIFY(length-result.valid_bytes< RAYA_WAL_V1_RECORD_BYTES);
  if(status==RAYA_WAL_CORRUPT)
   VERIFY(result.valid_bytes<=length);
  if(status==RAYA_WAL_LIMIT_EXCEEDED)
   VERIFY(result.records_scanned<=RAYA_WAL_V1_MAX_RECORDS);
  memset(&fields,0xFF,sizeof(fields));
  memset(out,0x5C,sizeof(out)); memcpy(out_before,out,sizeof(out));
  VERIFY(!raya_wal_record_encode_v1(&fields,out));
  VERIFY(memcmp(out,out_before,sizeof(out))==0);
 }
 printf("fuzz_cases=%u checks=%u failures=%u seed=0xD1B54A32D192ED03\n",
  FUZZ_CASES,checks,failures);
 return failures?1:0;
}
