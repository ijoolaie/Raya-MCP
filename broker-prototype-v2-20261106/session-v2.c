#include "session-v2.h"

static void advance_after_success(raya_v2_session *s) {
    s->active_request_id = 0;
    if (s->next_request_id == UINT32_MAX) {
        s->state = RAYA_V2_SESSION_EXHAUSTED;
    } else {
        s->next_request_id++;
        s->state = RAYA_V2_SESSION_OPEN;
    }
}

void raya_v2_session_init(raya_v2_session *s, uint8_t role) {
    if (!s) return;
    s->role = role;
    s->state = (role == RAYA_V2_SESSION_CLIENT || role == RAYA_V2_SESSION_SERVER)
        ? RAYA_V2_SESSION_OPEN : RAYA_V2_SESSION_CLOSED;
    s->next_request_id = 1u;
    s->active_request_id = 0u;
}

void raya_v2_session_close(raya_v2_session *s) {
    if (!s) return;
    s->state = RAYA_V2_SESSION_CLOSED;
    s->active_request_id = 0u;
}

int raya_v2_session_begin_request(raya_v2_session *s, uint32_t *request_id) {
    if (!s) return 0;
    if (s->role != RAYA_V2_SESSION_CLIENT || s->state != RAYA_V2_SESSION_OPEN || !request_id) {
        raya_v2_session_close(s);
        return 0;
    }
    s->active_request_id = s->next_request_id;
    s->state = RAYA_V2_SESSION_WAITING;
    *request_id = s->active_request_id;
    return 1;
}

int raya_v2_session_accept_response(raya_v2_session *s, const raya_v2_frame *frame) {
    if (!s) return 0;
    if (s->role != RAYA_V2_SESSION_CLIENT || s->state != RAYA_V2_SESSION_WAITING ||
        !frame || frame->type != RAYA_V2_FRAME_RESPONSE ||
        frame->request_id == 0u || frame->request_id != s->active_request_id) {
        raya_v2_session_close(s);
        return 0;
    }
    advance_after_success(s);
    return 1;
}

int raya_v2_session_accept_request(raya_v2_session *s, const raya_v2_frame *frame) {
    if (!s) return 0;
    if (s->role != RAYA_V2_SESSION_SERVER || s->state != RAYA_V2_SESSION_OPEN ||
        !frame || frame->type != RAYA_V2_FRAME_REQUEST ||
        frame->request_id == 0u || frame->request_id != s->next_request_id) {
        raya_v2_session_close(s);
        return 0;
    }
    s->active_request_id = frame->request_id;
    s->state = RAYA_V2_SESSION_WAITING;
    return 1;
}

int raya_v2_session_complete_request(raya_v2_session *s, const raya_v2_frame *response) {
    if (!s) return 0;
    if (s->role != RAYA_V2_SESSION_SERVER || s->state != RAYA_V2_SESSION_WAITING ||
        !response || response->type != RAYA_V2_FRAME_RESPONSE ||
        response->request_id == 0u || response->request_id != s->active_request_id) {
        raya_v2_session_close(s);
        return 0;
    }
    advance_after_success(s);
    return 1;
}
