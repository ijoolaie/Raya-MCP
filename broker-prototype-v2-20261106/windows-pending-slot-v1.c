#include "windows-pending-slot-v1.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib,"bcrypt.lib")
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static uint32_t get32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put64(uint8_t*p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(56u-8u*i));}
static uint64_t get64(const uint8_t*p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v=(v<<8)|p[i];return v;}
static uint32_t crc32(const uint8_t*p,unsigned n){uint32_t c=0xFFFFFFFFu;unsigned i,j;for(i=0;i<n;i++){c^=p[i];for(j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xEDB88320u);}return ~c;}
static int all_zero(const uint8_t*p,unsigned n){unsigned i;uint8_t v=0;for(i=0;i<n;i++)v|=p[i];return v==0;}
static int sha256(const uint8_t *data, ULONG length, uint8_t out[32]){
 BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;PUCHAR object=NULL;DWORD object_size=0,got=0;NTSTATUS st;int ok=0;
 st=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0);if(st<0)goto done;
 st=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&object_size,sizeof(object_size),&got,0);if(st<0||got!=sizeof(object_size)||object_size==0)goto done;
 object=(PUCHAR)malloc(object_size);if(!object)goto done;
 st=BCryptCreateHash(alg,&hash,object,object_size,NULL,0,0);if(st<0)goto done;
 st=BCryptHashData(hash,(PUCHAR)data,length,0);if(st<0)goto done;
 st=BCryptFinishHash(hash,out,32,0);if(st<0)goto done;ok=1;
done:
 if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);if(object){SecureZeroMemory(object,object_size);free(object);}return ok;
}
int raya_pending_slot_encode(const uint8_t record[128],uint64_t revision,uint8_t out[256]){
 raya_pending_state state;uint64_t e,n,w;uint8_t digest[32];
 if(!record||!out||revision==0||!raya_pending_record_decode(record,&state,&e,&n,&w))return 0;
 memset(out,0,256);memcpy(out,"RYPS",4);put32(out+4,1);put64(out+8,revision);put64(out+16,~revision);memcpy(out+24,record,128);
 if(!sha256(out,152,digest)){SecureZeroMemory(out,256);return 0;}
 memcpy(out+152,digest,32);put32(out+184,crc32(out,184));put32(out+188,~get32(out+184));SecureZeroMemory(digest,sizeof(digest));return 1;
}
int raya_pending_slot_decode(const uint8_t slot[256],uint64_t *revision,raya_pending_state *state){
 uint64_t rev,e,n,w;raya_pending_state embedded;uint8_t digest[32];uint32_t crc;unsigned i;
 if(!slot||!revision||!state||memcmp(slot,"RYPS",4)||get32(slot+4)!=1)return 0;
 rev=get64(slot+8);if(!rev||get64(slot+16)!=~rev)return 0;
 if(!raya_pending_record_decode(slot+24,&embedded,&e,&n,&w))return 0;
 if(!sha256(slot,152,digest))return 0;
 if(memcmp(digest,slot+152,32)){SecureZeroMemory(digest,sizeof(digest));return 0;}SecureZeroMemory(digest,sizeof(digest));
 crc=get32(slot+184);if(crc!=crc32(slot,184)||get32(slot+188)!=~crc)return 0;
 for(i=192;i<256;i++)if(slot[i])return 0;
 *revision=rev;*state=embedded;return 1;
}
raya_slot_select_result raya_pending_slots_select(const uint8_t slots[2][256],unsigned *selected,uint64_t *revision,raya_pending_state *state){
 uint64_t rev[2];raya_pending_state st[2];int valid[2],zero[2];unsigned i;
 if(!slots||!selected||!revision||!state)return RAYA_SLOT_SELECT_ERROR;
 for(i=0;i<2;i++){zero[i]=all_zero(slots[i],256);valid[i]=raya_pending_slot_decode(slots[i],&rev[i],&st[i]);if(!zero[i]&&!valid[i])return RAYA_SLOT_SELECT_ERROR;if(valid[i]&&rev[i]==UINT64_MAX)return RAYA_SLOT_SELECT_ERROR;}
 if(zero[0]&&zero[1])return RAYA_SLOT_SELECT_UNPROVISIONED;
 if(valid[0]&&zero[1])i=0;else if(zero[0]&&valid[1])i=1;
 else if(rev[0]>rev[1])i=0;else if(rev[1]>rev[0])i=1;
 else {if(memcmp(slots[0],slots[1],256)!=0)return RAYA_SLOT_SELECT_ERROR;i=0;}
 *selected=i;*revision=rev[i];*state=st[i];return RAYA_SLOT_SELECT_VALID;
}
