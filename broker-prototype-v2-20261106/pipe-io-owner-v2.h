#ifndef RAYA_PIPE_IO_OWNER_V2_H
#define RAYA_PIPE_IO_OWNER_V2_H
#include "pipe-completion-context-v2.h"

typedef enum {
    RAYA_PIPE_OWNER_CALLER_PENDING = 1,
    RAYA_PIPE_OWNER_CALLER_UNCERTAIN = 2,
    RAYA_PIPE_OWNER_REAPER_QUEUED = 3,
    RAYA_PIPE_OWNER_REAPER_PENDING = 4,
    RAYA_PIPE_OWNER_REAPER_UNCERTAIN = 5,
    RAYA_PIPE_OWNER_CALLER_TERMINAL = 6,
    RAYA_PIPE_OWNER_REAPER_TERMINAL = 7,
    RAYA_PIPE_OWNER_RELEASED = 8
} raya_pipe_owner_state;

typedef struct {
    raya_pipe_owner_state state;
    unsigned collection_attempts;
    unsigned terminal_collections;
    unsigned release_commits;
} raya_pipe_owner;

void raya_pipe_owner_init(raya_pipe_owner *owner);
/* Terminal submission paths that never leave caller ownership. */
BOOL raya_pipe_owner_caller_complete_immediate(raya_pipe_owner *owner);
BOOL raya_pipe_owner_caller_submission_failed(raya_pipe_owner *owner);
BOOL raya_pipe_owner_caller_retry(raya_pipe_owner *owner);
BOOL raya_pipe_owner_caller_collect(raya_pipe_owner *owner,
    BOOL event_signaled, BOOL exact_issuing_pair, BOOL api_success, DWORD error);
BOOL raya_pipe_owner_handoff(raya_pipe_owner *owner);
BOOL raya_pipe_owner_reaper_take(raya_pipe_owner *owner);
BOOL raya_pipe_owner_reaper_collect(raya_pipe_owner *owner,
    BOOL event_signaled, BOOL exact_issuing_pair, BOOL api_success, DWORD error);
BOOL raya_pipe_owner_reaper_retry(raya_pipe_owner *owner);
BOOL raya_pipe_owner_release(raya_pipe_owner *owner);
BOOL raya_pipe_owner_caller_may_access(const raya_pipe_owner *owner);
BOOL raya_pipe_owner_resources_may_be_freed(const raya_pipe_owner *owner);
#endif
