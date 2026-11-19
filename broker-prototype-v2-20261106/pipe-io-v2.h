#ifndef RAYA_PIPE_IO_V2_H
#define RAYA_PIPE_IO_V2_H
#include <windows.h>
#include <stddef.h>
typedef enum {
    RAYA_PIPE_IO_OK=0,
    RAYA_PIPE_IO_TIMEOUT=1,
    RAYA_PIPE_IO_DISCONNECTED=2,
    RAYA_PIPE_IO_ERROR=3,
    RAYA_PIPE_IO_INVALID=4
} raya_pipe_io_status;
/* Handle must be a byte-type named pipe opened for overlapped I/O. Deadline is a total
   duration in ms and includes setup/copy time. The caller must keep the original handle
   and the supplied data buffer valid until this function returns. Calls operating on the same
   byte stream must be externally serialized: do not issue concurrent reads, concurrent writes,
   or overlapping frame-level transfers on one pipe. The API does not provide stream-level
   locking. After this function returns, the caller may close/free them, including after timeout,
   because each pending operation owns a duplicated handle and private buffer. A timeout or
   error may occur after a partial transfer; callers must treat the byte stream as unusable and
   close their pipe handle rather than retrying a framed request on the same connection. A write
   that returns TIMEOUT may nevertheless have delivered bytes to the peer; do not retry it on
   this connection or a replacement connection without a higher-level idempotency protocol. */
raya_pipe_io_status raya_pipe_write_all(HANDLE pipe, const unsigned char *data, size_t length, DWORD deadline_ms);
raya_pipe_io_status raya_pipe_read_exact(HANDLE pipe, unsigned char *data, size_t length, DWORD deadline_ms);
#endif
