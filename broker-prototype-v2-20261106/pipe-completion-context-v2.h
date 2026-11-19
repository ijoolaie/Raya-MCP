#ifndef RAYA_PIPE_COMPLETION_CONTEXT_V2_H
#define RAYA_PIPE_COMPLETION_CONTEXT_V2_H
#include <windows.h>

typedef enum {
    RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS = 0,
    RAYA_PIPE_COMPLETION_TERMINAL_ABORTED = 1,
    RAYA_PIPE_COMPLETION_INCOMPLETE = 2,
    RAYA_PIPE_COMPLETION_UNCERTAIN = 3
} raya_pipe_completion_class;

/*
 * Context must describe the exact operation being collected:
 * - event_signaled: its dedicated OVERLAPPED.hEvent was observed signaled;
 * - exact_issuing_pair: GetOverlappedResult is being called with the exact
 *   pipe handle and OVERLAPPED pointer used for the pending ReadFile/WriteFile.
 *
 * This helper does not inspect handles and cannot establish either fact.
 * The caller must establish both from its ownership record and Win32 calls.
 */
raya_pipe_completion_class raya_classify_pipe_completion(
    BOOL event_signaled,
    BOOL exact_issuing_pair,
    BOOL api_success,
    DWORD error
);
#endif
