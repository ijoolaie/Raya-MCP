#ifndef RAYA_MANAGED_PIPE_IO_V2_H
#define RAYA_MANAGED_PIPE_IO_V2_H
#include "reaper-managed-worker-v2.h"
#define RAYA_MANAGED_PIPE_MAX_IO (1024U * 1024U)
typedef enum {
 RAYA_MANAGED_PIPE_READ=1,
 RAYA_MANAGED_PIPE_WRITE=2
} raya_managed_pipe_direction;
typedef enum {
 RAYA_MANAGED_PIPE_REJECTED=0,
 RAYA_MANAGED_PIPE_CALLBACK_INLINE=1,
 RAYA_MANAGED_PIPE_HANDED_OFF=2,
 RAYA_MANAGED_PIPE_RETAINED=3
} raya_managed_pipe_submit_result;
/* data is valid only for the duration of callback; copy it to retain read data. */
typedef void (*raya_managed_pipe_completion_fn)(
 DWORD error, DWORD transferred, const void *data, void *context);
typedef BOOL (WINAPI *raya_managed_cancel_fn)(HANDLE pipe,
 LPOVERLAPPED overlapped, void *context);
/*
 * Duplicates pipe, allocates private OVERLAPPED/event/buffer state, and issues
 * one overlapped byte-pipe operation. timeout_ms bounds the caller wait;
 * cancel_drain_ms bounds the post-CancelIoEx drain. If still nonterminal,
 * ownership transfers to the managed Worker and callback runs later.
 *
 * The callback may run inline before return or later on the Worker thread.
 * IMPORTANT: current Windows overlapped-I/O behavior is issuer-thread-bound in
 * this prototype: the thread that issued ReadFile/WriteFile MUST remain alive
 * until that operation reaches terminal completion. A handoff does not make
 * the kernel request independent of the issuing thread. Do not let a producer
 * thread exit immediately after HANDED_OFF; wait for its callback or use a
 * future dedicated long-lived submission-thread design. This constraint is
 * confirmed by the native thread-exit characterization test.
 * callback context must remain valid until callback. REJECTED means no I/O
 * was accepted and no callback will run. RETAINED means safety won over
 * liveness: resources are deliberately retained and callback is suppressed.
 */
raya_managed_pipe_submit_result raya_managed_pipe_submit_with_cancel(
 raya_managed_worker *worker, HANDLE pipe,
 raya_managed_pipe_direction direction, const void *write_data,
 DWORD length, DWORD timeout_ms, DWORD cancel_drain_ms,
 raya_managed_pipe_completion_fn callback, void *callback_context,
 raya_managed_cancel_fn cancel_fn, void *cancel_context);
raya_managed_pipe_submit_result raya_managed_pipe_submit(
 raya_managed_worker *worker, HANDLE pipe,
 raya_managed_pipe_direction direction, const void *write_data,
 DWORD length, DWORD timeout_ms, DWORD cancel_drain_ms,
 raya_managed_pipe_completion_fn callback, void *callback_context);
#endif
