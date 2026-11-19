#ifndef RAYA_GENERATION_LEDGER_V2_H
#define RAYA_GENERATION_LEDGER_V2_H
#include <stdint.h>
#define RAYA_LEDGER_RECORD_SIZE 32u
typedef int (*raya_ledger_read_fn)(void*,unsigned,uint8_t[32]);
typedef int (*raya_ledger_write_fn)(void*,unsigned,const uint8_t[32]);
typedef int (*raya_ledger_flush_fn)(void*,unsigned);
typedef int (*raya_generation_random_fn)(void*,uint8_t*,unsigned);
typedef struct { raya_ledger_read_fn read_slot; raya_ledger_write_fn write_slot; raya_ledger_flush_fn flush_slot; void *context; } raya_ledger_storage;
typedef enum { RAYA_LEDGER_EMPTY=0,RAYA_LEDGER_READY=1,RAYA_LEDGER_DEGRADED=2 } raya_ledger_state;
typedef struct { raya_ledger_storage storage; raya_ledger_state state; uint64_t epoch; unsigned active_slot; } raya_generation_ledger;
int raya_ledger_record_encode(uint64_t,uint8_t[32]);
int raya_ledger_record_decode(const uint8_t[32],uint64_t*);
int raya_ledger_init_empty(raya_generation_ledger*,const raya_ledger_storage*);
int raya_ledger_load(raya_generation_ledger*,const raya_ledger_storage*);
int raya_ledger_reserve_next(raya_generation_ledger*,uint64_t*);
int raya_ledger_create_generation(raya_generation_ledger*,raya_generation_random_fn,void*,uint8_t[16]);
#endif


