#include "pipe-io-owner-v2.h"
#include <string.h>

void raya_pipe_owner_init(raya_pipe_owner *owner) {
    if (!owner) return;
    memset(owner, 0, sizeof(*owner));
    owner->state = RAYA_PIPE_OWNER_CALLER_PENDING;
}

BOOL raya_pipe_owner_caller_complete_immediate(raya_pipe_owner *owner) {
    if (!owner || owner->state != RAYA_PIPE_OWNER_CALLER_PENDING ||
        owner->terminal_collections != 0 || owner->release_commits != 0)
        return FALSE;
    owner->state = RAYA_PIPE_OWNER_CALLER_TERMINAL;
    ++owner->terminal_collections;
    return TRUE;
}

BOOL raya_pipe_owner_caller_submission_failed(raya_pipe_owner *owner) {
    /* No overlapped operation was accepted by the OS; ownership is terminal. */
    return raya_pipe_owner_caller_complete_immediate(owner);
}

BOOL raya_pipe_owner_caller_retry(raya_pipe_owner *owner) {
    if (!owner || owner->state != RAYA_PIPE_OWNER_CALLER_UNCERTAIN)
        return FALSE;
    owner->state = RAYA_PIPE_OWNER_CALLER_PENDING;
    return TRUE;
}

static raya_pipe_completion_class collect_class(BOOL event_signaled,
    BOOL exact_issuing_pair, BOOL api_success, DWORD error) {
    return raya_classify_pipe_completion(event_signaled, exact_issuing_pair,
        api_success, error);
}

BOOL raya_pipe_owner_caller_collect(raya_pipe_owner *owner,
    BOOL event_signaled, BOOL exact_issuing_pair, BOOL api_success, DWORD error) {
    raya_pipe_completion_class result;
    if (!owner || owner->state != RAYA_PIPE_OWNER_CALLER_PENDING) return FALSE;
    ++owner->collection_attempts;
    result = collect_class(event_signaled, exact_issuing_pair, api_success, error);
    if (result == RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS ||
        result == RAYA_PIPE_COMPLETION_TERMINAL_ABORTED) {
        owner->state = RAYA_PIPE_OWNER_CALLER_TERMINAL;
        ++owner->terminal_collections;
    } else if (result == RAYA_PIPE_COMPLETION_UNCERTAIN) {
        owner->state = RAYA_PIPE_OWNER_CALLER_UNCERTAIN;
    }
    /* INCOMPLETE deliberately retains caller ownership and pending state. */
    return TRUE;
}

BOOL raya_pipe_owner_handoff(raya_pipe_owner *owner) {
    if (!owner || (owner->state != RAYA_PIPE_OWNER_CALLER_PENDING &&
                   owner->state != RAYA_PIPE_OWNER_CALLER_UNCERTAIN)) return FALSE;
    owner->state = RAYA_PIPE_OWNER_REAPER_QUEUED;
    return TRUE;
}

BOOL raya_pipe_owner_reaper_take(raya_pipe_owner *owner) {
    if (!owner || owner->state != RAYA_PIPE_OWNER_REAPER_QUEUED) return FALSE;
    owner->state = RAYA_PIPE_OWNER_REAPER_PENDING;
    return TRUE;
}

BOOL raya_pipe_owner_reaper_collect(raya_pipe_owner *owner,
    BOOL event_signaled, BOOL exact_issuing_pair, BOOL api_success, DWORD error) {
    raya_pipe_completion_class result;
    if (!owner || owner->state != RAYA_PIPE_OWNER_REAPER_PENDING) return FALSE;
    ++owner->collection_attempts;
    result = collect_class(event_signaled, exact_issuing_pair, api_success, error);
    if (result == RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS ||
        result == RAYA_PIPE_COMPLETION_TERMINAL_ABORTED) {
        owner->state = RAYA_PIPE_OWNER_REAPER_TERMINAL;
        ++owner->terminal_collections;
    } else if (result == RAYA_PIPE_COMPLETION_UNCERTAIN) {
        owner->state = RAYA_PIPE_OWNER_REAPER_UNCERTAIN;
    }
    /* INCOMPLETE remains REAPER_PENDING; uncertain remains explicitly owned. */
    return TRUE;
}

BOOL raya_pipe_owner_reaper_retry(raya_pipe_owner *owner) {
    if (!owner || owner->state != RAYA_PIPE_OWNER_REAPER_UNCERTAIN) return FALSE;
    owner->state = RAYA_PIPE_OWNER_REAPER_PENDING;
    return TRUE;
}

BOOL raya_pipe_owner_release(raya_pipe_owner *owner) {
    if (!owner || (owner->state != RAYA_PIPE_OWNER_CALLER_TERMINAL &&
                   owner->state != RAYA_PIPE_OWNER_REAPER_TERMINAL) ||
        owner->terminal_collections != 1 || owner->release_commits != 0) return FALSE;
    ++owner->release_commits;
    owner->state = RAYA_PIPE_OWNER_RELEASED;
    return TRUE;
}

BOOL raya_pipe_owner_caller_may_access(const raya_pipe_owner *owner) {
    if (!owner) return FALSE;
    return owner->state == RAYA_PIPE_OWNER_CALLER_PENDING ||
           owner->state == RAYA_PIPE_OWNER_CALLER_UNCERTAIN ||
           owner->state == RAYA_PIPE_OWNER_CALLER_TERMINAL;
}

BOOL raya_pipe_owner_resources_may_be_freed(const raya_pipe_owner *owner) {
    if (!owner) return FALSE;
    return owner->state == RAYA_PIPE_OWNER_CALLER_TERMINAL ||
           owner->state == RAYA_PIPE_OWNER_REAPER_TERMINAL;
}
