#ifndef RAYA_REAPER_WAITSET_V2_H
#define RAYA_REAPER_WAITSET_V2_H
#include <windows.h>
#include <stddef.h>
#include "reaper-window-v2.h"
typedef struct {
    HANDLE handles[64];
    size_t operation_indices[63];
    DWORD handle_count;
    DWORD operation_count;
    size_t next_cursor;
} raya_reaper_waitset;
typedef enum { RAYA_WAITSET_INVALID=0, RAYA_WAITSET_READY=1 } raya_waitset_status;
/* Caller holds the lock that stabilizes events and operation ownership. */
raya_waitset_status raya_reaper_waitset_build(
    HANDLE wake_event, const HANDLE *operation_events, size_t total,
    size_t cursor, raya_reaper_waitset *out);
#endif
