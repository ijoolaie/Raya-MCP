#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr, "FAIL line %d: %s (err=%lu)\n", __LINE__, #x, (unsigned long)GetLastError()); return 1; } } while (0)

typedef struct {
    CRITICAL_SECTION lock;
    HANDLE wake;
    HANDLE release_submitter;
    int queue[2];
    unsigned head;
    unsigned tail;
    unsigned count;
    unsigned capacity;
    unsigned accepted;
    unsigned backpressure;
    unsigned consumed;
} queue_state;

static unsigned checks;

static int enqueue(queue_state *s, int value) {
    int accepted = 0;
    EnterCriticalSection(&s->lock);
    if (s->count < s->capacity) {
        s->queue[s->tail] = value;
        s->tail = (s->tail + 1U) % 2U;
        ++s->count;
        ++s->accepted;
        accepted = 1;
        (void)SetEvent(s->wake);
    } else {
        ++s->backpressure;
    }
    LeaveCriticalSection(&s->lock);
    return accepted;
}

static DWORD WINAPI submitter(void *context) {
    queue_state *s = (queue_state *)context;
    if (WaitForSingleObject(s->release_submitter, 5000) != WAIT_OBJECT_0)
        return 10;
    for (;;) {
        int value;
        EnterCriticalSection(&s->lock);
        if (s->count == 0) {
            (void)ResetEvent(s->wake);
            LeaveCriticalSection(&s->lock);
            break;
        }
        value = s->queue[s->head];
        s->head = (s->head + 1U) % 2U;
        --s->count;
        ++s->consumed;
        LeaveCriticalSection(&s->lock);
        (void)value;
    }
    return 0;
}

static int run_capacity(unsigned capacity) {
    queue_state s;
    HANDLE worker;
    DWORD exit_code = STILL_ACTIVE;
    ZeroMemory(&s, sizeof(s));
    s.capacity = capacity;
    InitializeCriticalSection(&s.lock);
    s.wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    s.release_submitter = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(s.wake != NULL && s.release_submitter != NULL);
    worker = CreateThread(NULL, 0, submitter, &s, 0, NULL);
    CHECK(worker != NULL);

    CHECK(enqueue(&s, 100));
    if (capacity == 2) CHECK(enqueue(&s, 101));
    CHECK(!enqueue(&s, 999));
    CHECK(s.accepted == capacity);
    CHECK(s.backpressure == 1);
    CHECK(s.count == capacity);

    CHECK(SetEvent(s.release_submitter));
    CHECK(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(worker, &exit_code));
    CHECK(exit_code == 0);
    CHECK(s.consumed == capacity);
    CHECK(s.count == 0);
    CHECK(CloseHandle(worker));
    CHECK(CloseHandle(s.release_submitter));
    CHECK(CloseHandle(s.wake));
    DeleteCriticalSection(&s.lock);
    return 0;
}

int main(void) {
    unsigned run;
    for (run = 0; run < 25; ++run) {
        if (run_capacity(1) != 0) return 1;
        if (run_capacity(2) != 0) return 1;
    }
    (void)printf("Submitter queue backpressure: %u assertions, 50/50 cases PASS\n", checks);
    return 0;
}
