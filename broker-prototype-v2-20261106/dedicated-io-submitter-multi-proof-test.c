#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define OP_COUNT 70
#define PRODUCER_COUNT 7
#define CHECK(x) do { InterlockedIncrement(&g_checks); if (!(x)) { (void)fprintf(stderr, "FAIL line %d: %s (err=%lu)\n", __LINE__, #x, (unsigned long)GetLastError()); return 1; } } while (0)

typedef struct {
    HANDLE server;
    HANDLE client;
    OVERLAPPED ov;
    BYTE buffer;
    DWORD issue_error;
    DWORD completion_error;
    DWORD transferred;
} operation;

typedef struct {
    CRITICAL_SECTION lock;
    HANDLE queue_event;
    HANDLE all_issued_event;
    HANDLE all_acked_event;
    operation ops[OP_COUNT];
    int queue[OP_COUNT];
    unsigned head;
    unsigned tail;
    unsigned count;
    unsigned capacity;
    volatile LONG next_index;
    volatile LONG producers_done;
    volatile LONG issued_count;
    volatile LONG ack_count;
    volatile LONG submitter_tid;
    volatile LONG submitter_exited;
    volatile LONG producer_exited;
    DWORD submitter_error;
    DWORD completion_error;
} shared_state;

static volatile LONG g_checks;

static int make_pipe(operation *op, unsigned index) {
    wchar_t name[160];
    OVERLAPPED connect_ov;
    BOOL ok;
    DWORD error;
    (void)swprintf_s(name, 160, L"\\\\.\\pipe\\raya-submit-multi-%lu-%u",
        (unsigned long)GetCurrentProcessId(), index);
    op->server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    if (op->server == INVALID_HANDLE_VALUE) return 0;
    op->client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL);
    if (op->client == INVALID_HANDLE_VALUE) { CloseHandle(op->server); return 0; }
    ZeroMemory(&connect_ov, sizeof(connect_ov));
    connect_ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!connect_ov.hEvent) { CloseHandle(op->client); CloseHandle(op->server); return 0; }
    ok = ConnectNamedPipe(op->server, &connect_ov);
    error = ok ? ERROR_SUCCESS : GetLastError();
    (void)CloseHandle(connect_ov.hEvent);
    if (!ok && error != ERROR_PIPE_CONNECTED) {
        CloseHandle(op->client); CloseHandle(op->server); return 0;
    }
    op->ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!op->ov.hEvent) { CloseHandle(op->client); CloseHandle(op->server); return 0; }
    return 1;
}

static int enqueue(shared_state *s, int index) {
    int ok = 0;
    EnterCriticalSection(&s->lock);
    if (s->count < s->capacity) {
        s->queue[s->tail] = index;
        s->tail = (s->tail + 1U) % OP_COUNT;
        ++s->count;
        ok = 1;
        if (!SetEvent(s->queue_event)) {
            /* Queue ownership is retained; Submitter also polls the predicate. */
            (void)fprintf(stderr, "SetEvent(queue) failed: %lu\n", (unsigned long)GetLastError());
        }
    }
    LeaveCriticalSection(&s->lock);
    return ok;
}

static DWORD WINAPI producer_thread(void *context) {
    shared_state *s = (shared_state *)context;
    for (;;) {
        LONG index = InterlockedIncrement(&s->next_index) - 1;
        if (index >= OP_COUNT) break;
        if (!enqueue(s, (int)index)) {
            (void)fprintf(stderr, "unexpected bounded queue backpressure at %ld\n", index);
            return 31;
        }
    }
    (void)InterlockedIncrement(&s->producers_done);
    (void)InterlockedIncrement(&s->producer_exited);
    (void)SetEvent(s->queue_event);
    return 0;
}

static DWORD WINAPI submitter_thread(void *context) {
    shared_state *s = (shared_state *)context;
    s->submitter_tid = (LONG)GetCurrentThreadId();
    for (;;) {
        int index = -1;
        EnterCriticalSection(&s->lock);
        if (s->count != 0) {
            index = s->queue[s->head];
            s->head = (s->head + 1U) % OP_COUNT;
            --s->count;
        } else {
            (void)ResetEvent(s->queue_event);
            if (s->count != 0 || s->producers_done == PRODUCER_COUNT) {
                if (s->count != 0) (void)SetEvent(s->queue_event);
            }
        }
        LeaveCriticalSection(&s->lock);

        if (index >= 0) {
            operation *op = &s->ops[index];
            DWORD bytes = 0;
            BOOL ok = ReadFile(op->server, &op->buffer, 1, &bytes, &op->ov);
            op->issue_error = ok ? ERROR_SUCCESS : GetLastError();
            if (!ok && op->issue_error != ERROR_IO_PENDING) {
                s->submitter_error = op->issue_error;
                return 32;
            }
            (void)InterlockedIncrement(&s->issued_count);
            if (s->issued_count == OP_COUNT) (void)SetEvent(s->all_issued_event);
            continue;
        }

        if (s->producers_done == PRODUCER_COUNT) break;
        if (WaitForSingleObject(s->queue_event, 1000) != WAIT_OBJECT_0) {
            if (s->count == 0 && s->producers_done == PRODUCER_COUNT) break;
        }
    }

    /* Issuer lifetime is pinned until every completion is positively collected. */
    if (WaitForSingleObject(s->all_acked_event, 15000) != WAIT_OBJECT_0) {
        s->submitter_error = GetLastError();
        return 33;
    }
    (void)InterlockedExchange(&s->submitter_exited, 1);
    return 0;
}

static DWORD WINAPI completion_thread(void *context) {
    shared_state *s = (shared_state *)context;
    unsigned i;
    if (WaitForSingleObject(s->all_issued_event, 10000) != WAIT_OBJECT_0) {
        s->completion_error = GetLastError();
        return 41;
    }
    for (i = 0; i < OP_COUNT; ++i) {
        operation *op = &s->ops[i];
        DWORD bytes = 0;
        BOOL ok;
        if (WaitForSingleObject(op->ov.hEvent, 10000) != WAIT_OBJECT_0) {
            s->completion_error = GetLastError();
            return 42;
        }
        ok = GetOverlappedResult(op->server, &op->ov, &bytes, FALSE);
        op->completion_error = ok ? ERROR_SUCCESS : GetLastError();
        op->transferred = bytes;
        if (!ok) return 43;
        (void)InterlockedIncrement(&s->ack_count);
    }
    (void)SetEvent(s->all_acked_event);
    return 0;
}

static int run_once(void) {
    shared_state s;
    HANDLE producers[PRODUCER_COUNT] = {0};
    HANDLE submitter = NULL, completion = NULL;
    unsigned i;
    DWORD written;
    DWORD thread_exit = STILL_ACTIVE;
    BYTE payload = 'M';
    ZeroMemory(&s, sizeof(s));
    s.capacity = OP_COUNT;
    s.next_index = 0;
    InitializeCriticalSection(&s.lock);
    s.queue_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    s.all_issued_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    s.all_acked_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s.queue_event || !s.all_issued_event || !s.all_acked_event) return 1;
    for (i = 0; i < OP_COUNT; ++i) {
        s.ops[i].server = INVALID_HANDLE_VALUE;
        s.ops[i].client = INVALID_HANDLE_VALUE;
        CHECK(make_pipe(&s.ops[i], i));
    }

    submitter = CreateThread(NULL, 0, submitter_thread, &s, 0, NULL);
    CHECK(submitter != NULL);
    completion = CreateThread(NULL, 0, completion_thread, &s, 0, NULL);
    CHECK(completion != NULL);
    for (i = 0; i < PRODUCER_COUNT; ++i) {
        producers[i] = CreateThread(NULL, 0, producer_thread, &s, 0, NULL);
        CHECK(producers[i] != NULL);
    }
    for (i = 0; i < PRODUCER_COUNT; ++i) {
        CHECK(WaitForSingleObject(producers[i], 10000) == WAIT_OBJECT_0);
        CHECK(CloseHandle(producers[i]));
    }
    CHECK(s.producer_exited == PRODUCER_COUNT);
    CHECK(WaitForSingleObject(s.all_issued_event, 10000) == WAIT_OBJECT_0);
    CHECK(s.issued_count == OP_COUNT);
    CHECK(WaitForSingleObject(submitter, 0) == WAIT_TIMEOUT);
    CHECK((DWORD)s.submitter_tid == GetThreadId(submitter));

    for (i = 0; i < OP_COUNT; ++i) {
        written = 0;
        CHECK(WriteFile(s.ops[i].client, &payload, 1, &written, NULL));
        CHECK(written == 1);
    }
    CHECK(WaitForSingleObject(completion, 15000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(completion, &thread_exit));
    CHECK(thread_exit == 0);
    CHECK(s.completion_error == 0);
    CHECK(s.ack_count == OP_COUNT);
    CHECK(WaitForSingleObject(submitter, 5000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(submitter, &thread_exit));
    CHECK(thread_exit == 0);
    CHECK(s.submitter_exited == 1);
    CHECK(CloseHandle(completion));
    CHECK(CloseHandle(submitter));

    for (i = 0; i < OP_COUNT; ++i) {
        CHECK(s.ops[i].issue_error == ERROR_IO_PENDING);
        CHECK(s.ops[i].completion_error == ERROR_SUCCESS);
        CHECK(s.ops[i].transferred == 1 && s.ops[i].buffer == payload);
        CHECK(CloseHandle(s.ops[i].ov.hEvent));
        CHECK(CloseHandle(s.ops[i].client));
        CHECK(CloseHandle(s.ops[i].server));
    }
    CHECK(CloseHandle(s.all_acked_event));
    CHECK(CloseHandle(s.all_issued_event));
    CHECK(CloseHandle(s.queue_event));
    DeleteCriticalSection(&s.lock);
    return 0;
}

int main(void) {
    unsigned run;
    for (run = 0; run < 10; ++run) {
        if (run_once() != 0) return 1;
    }
    (void)printf("Dedicated Submitter multi-producer proof: %ld assertions, 10/10 runs PASS\n",
        (long)g_checks);
    return 0;
}
