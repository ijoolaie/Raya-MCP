#ifndef RAYA_SUPERVISOR_CONTROL_V2_H
#define RAYA_SUPERVISOR_CONTROL_V2_H
#include <stddef.h>
#include <stdint.h>
#define RAYA_CTL_MAX_PAYLOAD 49u
#define RAYA_CTL_ID_SIZE 16u
#define RAYA_CTL_GENERATION_CACHE_CAPACITY 128u
typedef enum { RAYA_CTL_HELLO=1, RAYA_CTL_CHALLENGE=2, RAYA_CTL_AUTH_BINDING=3, RAYA_CTL_READY=4, RAYA_CTL_PING=5, RAYA_CTL_PONG=6, RAYA_CTL_STOP=7, RAYA_CTL_STOPPED=8, RAYA_CTL_FATAL=9 } raya_ctl_opcode;
typedef enum { RAYA_CTL_DISCONNECTED=0, RAYA_CTL_CONNECTED, RAYA_CTL_AUTHENTICATING, RAYA_CTL_READY_STATE, RAYA_CTL_STOP_REQUESTED, RAYA_CTL_STOPPED_STATE, RAYA_CTL_PROCESS_EXITED, RAYA_CTL_DEGRADED, RAYA_CTL_CLOSED } raya_ctl_state;
typedef enum { RAYA_CTL_EV_CONNECT=1, RAYA_CTL_EV_HELLO_VALID, RAYA_CTL_EV_CHALLENGE_VALID, RAYA_CTL_EV_BINDING_VALID, RAYA_CTL_EV_IDENTITY_VERIFIED, RAYA_CTL_EV_READY_VALID, RAYA_CTL_EV_PING_VALID, RAYA_CTL_EV_PONG_VALID, RAYA_CTL_EV_STOP_SENT, RAYA_CTL_EV_STOPPED_VALID, RAYA_CTL_EV_PROCESS_EXIT_CONFIRMED, RAYA_CTL_EV_CLOSE, RAYA_CTL_EV_FAILURE } raya_ctl_event;
typedef struct { raya_ctl_state state; int challenge_seen; int binding_seen; int identity_verified; } raya_ctl_session;
typedef struct { uint8_t entries[RAYA_CTL_GENERATION_CACHE_CAPACITY][16]; size_t count; int exhausted; } raya_ctl_generation_cache;
typedef struct { uint8_t generation[16]; uint8_t worker_nonce[16]; uint8_t supervisor_nonce[16]; int hello_seen; int challenge_issued; int binding_seen; int identity_verified; int ready_seen; } raya_ctl_transcript;
int raya_ctl_payload_valid(const uint8_t *p,size_t n);
int raya_ctl_u32_read(const uint8_t *p,size_t n,uint32_t *out);
void raya_ctl_u32_write(uint8_t out[4],uint32_t value);
int raya_ctl_nonce_equal(const uint8_t a[16],const uint8_t b[16]);
int raya_ctl_accept_request_id(uint32_t previous,uint32_t incoming);
int raya_ctl_step(raya_ctl_session *session,raya_ctl_event event,int peer_identity_verified);
void raya_ctl_generation_cache_init(raya_ctl_generation_cache *cache);
int raya_ctl_generation_reserve(raya_ctl_generation_cache *cache,const uint8_t generation[16]);
int raya_ctl_transcript_init_guarded(raya_ctl_transcript *t,raya_ctl_generation_cache *cache,const uint8_t generation[16],const uint8_t supervisor_nonce[16]);
int raya_ctl_transcript_init(raya_ctl_transcript *t,const uint8_t generation[16],const uint8_t supervisor_nonce[16]);
int raya_ctl_accept_hello(raya_ctl_transcript *t,const uint8_t *p,size_t n);
int raya_ctl_build_challenge(raya_ctl_transcript *t,uint8_t out[33]);
int raya_ctl_accept_binding(raya_ctl_transcript *t,const uint8_t *p,size_t n);
int raya_ctl_mark_identity_verified(raya_ctl_transcript *t,int verified);
int raya_ctl_accept_ready(raya_ctl_transcript *t,const uint8_t *p,size_t n);
#endif