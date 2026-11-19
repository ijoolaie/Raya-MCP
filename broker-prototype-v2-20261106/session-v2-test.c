#include "session-v2.h"
#include <stdio.h>
#include <string.h>

static unsigned int checks;
#define CHECK(x, name) do { checks++; if (!(x)) { printf("FAIL %s\n", name); return 1; } } while (0)

int main(void) {
    raya_v2_session s;
    raya_v2_frame f;
    uint32_t id = 0;

    memset(&f, 0, sizeof(f));
    raya_v2_session_init(&s, RAYA_V2_SESSION_CLIENT);
    CHECK(s.state == RAYA_V2_SESSION_OPEN && s.next_request_id == 1u, "client initial state");
    CHECK(raya_v2_session_begin_request(&s, &id) && id == 1u, "client begins ID 1");
    CHECK(s.state == RAYA_V2_SESSION_WAITING, "client waits for response");
    f.type = RAYA_V2_FRAME_RESPONSE; f.request_id = 1u;
    CHECK(raya_v2_session_accept_response(&s, &f), "client accepts matching response");
    CHECK(s.state == RAYA_V2_SESSION_OPEN && s.next_request_id == 2u, "client advances after response");

    CHECK(raya_v2_session_begin_request(&s, &id) && id == 2u, "client begins ID 2");
    f.request_id = 1u;
    CHECK(!raya_v2_session_accept_response(&s, &f), "client rejects stale response ID");
    CHECK(s.state == RAYA_V2_SESSION_CLOSED, "stale response closes session");

    raya_v2_session_init(&s, RAYA_V2_SESSION_CLIENT);
    CHECK(raya_v2_session_begin_request(&s, &id), "client begins before overlap test");
    CHECK(!raya_v2_session_begin_request(&s, &id), "client rejects concurrent request");
    CHECK(s.state == RAYA_V2_SESSION_CLOSED, "concurrent request closes session");

    raya_v2_session_init(&s, RAYA_V2_SESSION_CLIENT);
    CHECK(raya_v2_session_begin_request(&s, &id), "client begins before wrong type test");
    f.type = RAYA_V2_FRAME_REQUEST; f.request_id = 1u;
    CHECK(!raya_v2_session_accept_response(&s, &f), "client rejects request frame as response");

    raya_v2_session_init(&s, RAYA_V2_SESSION_SERVER);
    f.type = RAYA_V2_FRAME_REQUEST; f.request_id = 1u;
    CHECK(raya_v2_session_accept_request(&s, &f), "server accepts first request");
    CHECK(s.state == RAYA_V2_SESSION_WAITING, "server enters pending state");
    f.type = RAYA_V2_FRAME_RESPONSE; f.request_id = 1u;
    CHECK(raya_v2_session_complete_request(&s, &f), "server completes sent response");
    CHECK(s.state == RAYA_V2_SESSION_OPEN && s.next_request_id == 2u, "server advances after response send");

    f.type = RAYA_V2_FRAME_REQUEST; f.request_id = 3u;
    CHECK(!raya_v2_session_accept_request(&s, &f), "server rejects skipped request ID");
    CHECK(s.state == RAYA_V2_SESSION_CLOSED, "skipped ID closes server session");

    raya_v2_session_init(&s, RAYA_V2_SESSION_SERVER);
    f.request_id = 1u;
    CHECK(raya_v2_session_accept_request(&s, &f), "server accepts request before duplicate");
    CHECK(!raya_v2_session_accept_request(&s, &f), "server rejects request while busy");
    CHECK(s.state == RAYA_V2_SESSION_CLOSED, "duplicate while busy closes server session");

    raya_v2_session_init(&s, RAYA_V2_SESSION_SERVER);
    f.type = RAYA_V2_FRAME_REQUEST; f.request_id = 1u;
    CHECK(raya_v2_session_accept_request(&s, &f), "server accepts request before wrong completion");
    f.type = RAYA_V2_FRAME_RESPONSE; f.request_id = 2u;
    CHECK(!raya_v2_session_complete_request(&s, &f), "server rejects wrong completion ID");

    raya_v2_session_init(&s, RAYA_V2_SESSION_SERVER);
    f.type = RAYA_V2_FRAME_REQUEST; f.request_id = 1u;
    CHECK(raya_v2_session_accept_request(&s, &f), "server accepts request before wrong response type");
    CHECK(!raya_v2_session_complete_request(&s, &f), "server rejects request frame as response");

    raya_v2_session_init(&s, RAYA_V2_SESSION_CLIENT);
    s.next_request_id = UINT32_MAX;
    CHECK(raya_v2_session_begin_request(&s, &id) && id == UINT32_MAX, "client reserves maximum ID");
    f.type = RAYA_V2_FRAME_RESPONSE; f.request_id = UINT32_MAX;
    CHECK(raya_v2_session_accept_response(&s, &f), "client accepts maximum ID response");
    CHECK(s.state == RAYA_V2_SESSION_EXHAUSTED, "client exhausts without ID wrap");

    raya_v2_session_init(&s, RAYA_V2_SESSION_SERVER);
    s.next_request_id = UINT32_MAX;
    f.type = RAYA_V2_FRAME_REQUEST; f.request_id = UINT32_MAX;
    CHECK(raya_v2_session_accept_request(&s, &f), "server accepts maximum ID");
    f.type = RAYA_V2_FRAME_RESPONSE;
    CHECK(raya_v2_session_complete_request(&s, &f), "server completes maximum ID");
    CHECK(s.state == RAYA_V2_SESSION_EXHAUSTED, "server exhausts without ID wrap");

    raya_v2_session_init(&s, 99u);
    CHECK(s.state == RAYA_V2_SESSION_CLOSED, "invalid role starts closed");

    printf("PASS session-v2 state cases: %u\n", checks);
    return 0;
}
