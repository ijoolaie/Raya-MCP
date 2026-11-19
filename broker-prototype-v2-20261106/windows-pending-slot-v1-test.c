#include "windows-pending-slot-v1.h"
#include <stdio.h>
#include <string.h>
#define C(x) do{tests++;if(!(x)){printf("FAIL line %d\n",__LINE__);return 1;}}while(0)
static unsigned tests;
int main(void){
 uint8_t ns[16]={1},op[16]={2},dg[32]={3},rec[128],a[256],b[256],slots[2][256];
 uint64_t rev; raya_pending_state state; unsigned selected,i; raya_slot_select_result sr;
 C(raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,0,rec));
 C(raya_pending_slot_encode(rec,1,a));C(!memcmp(a,"RYPS",4));C(a[15]==1);C(a[23]==0xFE);
 C(raya_pending_slot_decode(a,&rev,&state));C(rev==1&&state==RAYA_PENDING_PREPARED);
 C(!raya_pending_slot_encode(rec,0,b));C(!raya_pending_slot_encode(rec,1,NULL));
 C(!raya_pending_slot_decode(a,NULL,&state));C(!raya_pending_slot_decode(a,&rev,NULL));
 memcpy(b,a,256);C(raya_pending_slot_decode(b,&rev,&state));C(rev==1);
 memset(slots,0,sizeof(slots));sr=raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state);C(sr==RAYA_SLOT_SELECT_UNPROVISIONED);
 memcpy(slots[1],a,256);sr=raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state);C(sr==RAYA_SLOT_SELECT_VALID&&selected==1&&rev==1);
 C(raya_pending_slot_encode(rec,2,b));memcpy(slots[0],a,256);memcpy(slots[1],b,256);
 sr=raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state);C(sr==RAYA_SLOT_SELECT_VALID&&selected==1&&rev==2);
 memcpy(slots[0],b,256);memcpy(slots[1],a,256);sr=raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state);C(sr==RAYA_SLOT_SELECT_VALID&&selected==0&&rev==2);
 memcpy(slots[1],b,256);sr=raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state);C(sr==RAYA_SLOT_SELECT_VALID&&selected==0);
 C(raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_TOMBSTONED,8,rec));
 C(raya_pending_slot_encode(rec,2,b));memcpy(slots[0],b,256);memcpy(slots[1],b,256);
 C(raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state)==RAYA_SLOT_SELECT_VALID&&selected==0&&rev==2);
 C(raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,0,rec));C(raya_pending_slot_encode(rec,2,b));memcpy(slots[1],b,256);
 C(raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state)==RAYA_SLOT_SELECT_ERROR);
 C(raya_pending_slot_encode(rec,2,b));memcpy(slots[0],a,256);memcpy(slots[1],b,256);
 memcpy(slots[1],b,256);slots[1][30]^=1;C(raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state)==RAYA_SLOT_SELECT_ERROR);
 C(raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,0,rec));C(raya_pending_slot_encode(rec,1,a));
 for(i=0;i<256;i++){memcpy(b,a,256);b[i]^=1;C(!raya_pending_slot_decode(b,&rev,&state));}
 memcpy(b,a,256);memset(b,0,256);C(!raya_pending_slot_decode(b,&rev,&state));
 memcpy(b,a,256);b[7]=0;C(!raya_pending_slot_decode(b,&rev,&state));
 memcpy(b,a,256);b[15]=0;C(!raya_pending_slot_decode(b,&rev,&state));
 memcpy(b,a,256);b[16]^=1;C(!raya_pending_slot_decode(b,&rev,&state));
 memcpy(b,a,256);b[192]=1;C(!raya_pending_slot_decode(b,&rev,&state));
 memcpy(b,a,256);b[184]^=1;C(!raya_pending_slot_decode(b,&rev,&state));
 memcpy(b,a,256);b[24]^=1;C(!raya_pending_slot_decode(b,&rev,&state));
 C(raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_TOMBSTONED,8,rec));
 C(raya_pending_slot_encode(rec,9,b));C(raya_pending_slot_decode(b,&rev,&state)&&rev==9&&state==RAYA_PENDING_TOMBSTONED);
 memcpy(slots[0],a,256);memcpy(slots[1],b,256);C(raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state)==RAYA_SLOT_SELECT_VALID&&selected==1&&state==RAYA_PENDING_TOMBSTONED);
 memcpy(slots[0],a,256);memcpy(slots[1],a,256);slots[1][24+32]^=1;C(raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state)==RAYA_SLOT_SELECT_ERROR);
 C(raya_pending_slot_encode(rec,UINT64_MAX,a));C(raya_pending_slot_decode(a,&rev,&state)&&rev==UINT64_MAX);
 memcpy(slots[0],a,256);memset(slots[1],0,256);C(raya_pending_slots_select((const uint8_t (*)[256])slots,&selected,&rev,&state)==RAYA_SLOT_SELECT_ERROR);
 printf("PASS windows-pending-slot-v1 assertions: %u\n",tests);return 0;
}
