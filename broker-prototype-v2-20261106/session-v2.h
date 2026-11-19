#ifndef RAYA_SESSION_V2_H
#define RAYA_SESSION_V2_H
#include <stdint.h>
#include "response-v2.h"

#define RAYA_V2_SESSION_CLIENT 1u
#define RAYA_V2_SESSION_SERVER 2u

typedef enum {
    RAYA_V2_SESSION_OPEN = 1,
    RAYA_V2_SESSION_WAITING = 2,
    RAYA_V2_SESSION_CLOSED = 3,
    RAYA_V2_SESSION_EXHAUSTED = 4
} raya_v2_session_state;

typedef struct {
    uint8_t role;
    raya_v2_session_state state;
    uint32_t next_request_id;
    uint32_t active_request_id;
} raya_v2_session;

/* A session is single-threaded; callers must serialize access externally.
   Frame-taking APIs require a frame produced by a successful frame decoder;
   this layer checks sequencing/type/ID, not payload storage or frame encoding. */
void raya_v2_session_init(raya_v2_session *session, uint8_t role);
/* Client: reserves the next ID before encoding/sending one REQUEST. */
int raya_v2_session_begin_request(raya_v2_session *session, uint32_t *request_id);
/* Client: accepts only the RESPONSE for the active request; any mismatch closes. */
int raya_v2_session_accept_response(raya_v2_session *session, const raya_v2_frame *frame);
/* Server: accepts exactly the next REQUEST; any mismatch closes. */
int raya_v2_session_accept_request(raya_v2_session *session, const raya_v2_frame *frame);
/* Server: call only after the complete RESPONSE frame has been sent. */
int raya_v2_session_complete_request(raya_v2_session *session, const raya_v2_frame *response);
/* On any transport/partial-frame failure, close the session and discard it. */
void raya_v2_session_close(raya_v2_session *session);
#endif
