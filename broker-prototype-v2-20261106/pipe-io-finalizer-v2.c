#include "pipe-io-finalizer-v2.h"
#include <string.h>
static int valid_handle(HANDLE h) { return h!=NULL && h!=INVALID_HANDLE_VALUE; }
int raya_pipe_finalizer_init(raya_pipe_finalizer *f, HANDLE pipe, HANDLE event) {
 if (!f || !valid_handle(pipe) || !valid_handle(event) || pipe==event) return 0;
 memset(f,0,sizeof(*f));
 f->issuing_pipe=pipe;
 f->completion_event=event;
 f->state=RAYA_FINALIZER_NOT_STARTED;
 return 1;
}
raya_finalizer_result raya_pipe_finalizer_close_handles(
 raya_pipe_finalizer *f, raya_close_handle_fn close_fn, void *context) {
 if (!f || !close_fn) return RAYA_FINALIZER_INVALID;
 if (f->state==RAYA_FINALIZER_CLOSE_UNCERTAIN) return RAYA_FINALIZER_CLOSE_FAILED;
 if (f->state==RAYA_FINALIZER_HANDLES_CLOSED ||
     f->state==RAYA_FINALIZER_RELEASED) return RAYA_FINALIZER_OK;
 if (f->state==RAYA_FINALIZER_NOT_STARTED) f->state=RAYA_FINALIZER_CLOSING_PIPE;
 if (f->state==RAYA_FINALIZER_CLOSING_PIPE) {
  if (!valid_handle(f->issuing_pipe)) return RAYA_FINALIZER_INVALID;
  ++f->close_attempts;
  if (!close_fn(f->issuing_pipe,context)) {
   f->state=RAYA_FINALIZER_CLOSE_UNCERTAIN;
   return RAYA_FINALIZER_CLOSE_FAILED;
  }
  f->issuing_pipe=NULL;
  ++f->successful_closes;
  f->state=RAYA_FINALIZER_PIPE_CLOSED;
 }
 if (f->state==RAYA_FINALIZER_PIPE_CLOSED) f->state=RAYA_FINALIZER_CLOSING_EVENT;
 if (f->state==RAYA_FINALIZER_CLOSING_EVENT) {
  if (!valid_handle(f->completion_event)) return RAYA_FINALIZER_INVALID;
  ++f->close_attempts;
  if (!close_fn(f->completion_event,context)) {
   f->state=RAYA_FINALIZER_CLOSE_UNCERTAIN;
   return RAYA_FINALIZER_CLOSE_FAILED;
  }
  f->completion_event=NULL;
  ++f->successful_closes;
  f->state=RAYA_FINALIZER_HANDLES_CLOSED;
 }
 return f->state==RAYA_FINALIZER_HANDLES_CLOSED ?
        RAYA_FINALIZER_OK : RAYA_FINALIZER_INVALID;
}
int raya_pipe_finalizer_commit_release(raya_pipe_finalizer *f) {
 if (!f || f->state!=RAYA_FINALIZER_HANDLES_CLOSED ||
     f->issuing_pipe!=NULL || f->completion_event!=NULL ||
     f->successful_closes!=2 || f->release_commits!=0) return 0;
 ++f->release_commits;
 f->state=RAYA_FINALIZER_RELEASED;
 return 1;
}
