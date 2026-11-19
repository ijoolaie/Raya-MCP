#ifndef RAYA_PIPE_IO_FINALIZER_V2_H
#define RAYA_PIPE_IO_FINALIZER_V2_H
#include <windows.h>
typedef enum {
 RAYA_FINALIZER_NOT_STARTED=0,
 RAYA_FINALIZER_CLOSING_PIPE=1,
 RAYA_FINALIZER_PIPE_CLOSED=2,
 RAYA_FINALIZER_CLOSING_EVENT=3,
 RAYA_FINALIZER_HANDLES_CLOSED=4,
 RAYA_FINALIZER_CLOSE_UNCERTAIN=5,
 RAYA_FINALIZER_RELEASED=6
} raya_finalizer_state;
typedef enum {
 RAYA_FINALIZER_OK=1,
 RAYA_FINALIZER_INVALID=0,
 RAYA_FINALIZER_CLOSE_FAILED=-1,
 RAYA_FINALIZER_NOT_TERMINAL=-2
} raya_finalizer_result;
typedef BOOL (*raya_close_handle_fn)(HANDLE handle, void *context);
typedef struct {
 HANDLE issuing_pipe;
 HANDLE completion_event;
 raya_finalizer_state state;
 unsigned close_attempts;
 unsigned successful_closes;
 unsigned release_commits;
} raya_pipe_finalizer;
/* Initialize only after terminal I/O collection; handles must be distinct and owned. */
int raya_pipe_finalizer_init(raya_pipe_finalizer *f, HANDLE pipe, HANDLE event);
/* Caller must serialize this record and call only after exact terminal collection.
   The callback runs outside the Reaper lock. A failed close is sticky/uncertain. */
raya_finalizer_result raya_pipe_finalizer_close_handles(
 raya_pipe_finalizer *f, raya_close_handle_fn close_fn, void *context);
/* Memory may be freed only after both handles were positively closed. */
int raya_pipe_finalizer_commit_release(raya_pipe_finalizer *f);
#endif
