#include "pipe-completion-context-v2.h"

/*
 * ERROR_OPERATION_ABORTED is accepted only in the narrow named-pipe
 * cancellation context: the operation's own event is signaled and the
 * exact issuing handle/OVERLAPPED pair is used for result collection.
 * All other failed results remain uncertain pending a separately reviewed
 * operation-specific contract.
 */
raya_pipe_completion_class raya_classify_pipe_completion(
    BOOL event_signaled,
    BOOL exact_issuing_pair,
    BOOL api_success,
    DWORD error
) {
    if (!event_signaled || !exact_issuing_pair)
        return RAYA_PIPE_COMPLETION_UNCERTAIN;
    if (api_success)
        return RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS;
    if (error == ERROR_IO_INCOMPLETE)
        return RAYA_PIPE_COMPLETION_INCOMPLETE;
    if (error == ERROR_OPERATION_ABORTED)
        return RAYA_PIPE_COMPLETION_TERMINAL_ABORTED;
    return RAYA_PIPE_COMPLETION_UNCERTAIN;
}
