#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    OWNER_CALLER_PENDING = 1,
    OWNER_CALLER_COLLECTING = 2,
    OWNER_REAPER_QUEUED = 3,
    OWNER_REAPER_PENDING = 4,
    OWNER_TERMINAL = 5
} owner_state;

typedef struct {
    CRITICAL_SECTION lock;
    owner_state state;
    HANDLE completion;
    HANDLE probe_done;
    HANDLE allow_enqueue;
    HANDLE worker_done;
    LONG caller_collectors;
    LONG worker_collectors;
    LONG cleanup_count;
    BOOL caller_touched_after_handoff;
    BOOL scenario_worker_wins;
} operation_model;

static int check(BOOL ok, const char *what) {
    if (!ok) {
        printf("FAIL: %s (error=%lu)\n", what, (unsigned long)GetLastError());
        return 0;
    }
    return 1;
}

static DWORD WINAPI signal_completion_after_probe(void *arg) {
    operation_model *m = (operation_model*)arg;
    if (WaitForSingleObject(m->probe_done, 5000) != WAIT_OBJECT_0) return 1;
    return SetEvent(m->completion) ? 0 : 2;
}

static DWORD WINAPI model_worker(void *arg) {
    operation_model *m = (operation_model*)arg;
    if (!m->scenario_worker_wins) return 0;
    if (WaitForSingleObject(m->probe_done, 5000) != WAIT_OBJECT_0) return 1;
    EnterCriticalSection(&m->lock);
    if (m->state == OWNER_REAPER_QUEUED) {
        m->state = OWNER_REAPER_PENDING;
        LeaveCriticalSection(&m->lock);
        if (WaitForSingleObject(m->completion, 5000) != WAIT_OBJECT_0) return 2;
        InterlockedIncrement(&m->worker_collectors);
        EnterCriticalSection(&m->lock);
        if (m->state != OWNER_REAPER_PENDING) {
            LeaveCriticalSection(&m->lock);
            return 3;
        }
        m->state = OWNER_TERMINAL;
        InterlockedIncrement(&m->cleanup_count);
        LeaveCriticalSection(&m->lock);
    } else {
        LeaveCriticalSection(&m->lock);
        return 4;
    }
    SetEvent(m->worker_done);
    return 0;
}

/* Mirrors the specified linearization point: lock + zero-time completion probe. */
static int caller_handoff(operation_model *m) {
    DWORD probe;
    EnterCriticalSection(&m->lock);
    if (m->state != OWNER_CALLER_PENDING) {
        LeaveCriticalSection(&m->lock);
        return 0;
    }
    probe = WaitForSingleObject(m->completion, 0);
    if (probe == WAIT_OBJECT_0) {
        m->state = OWNER_CALLER_COLLECTING;
        LeaveCriticalSection(&m->lock);
        InterlockedIncrement(&m->caller_collectors);
        EnterCriticalSection(&m->lock);
        if (m->state != OWNER_CALLER_COLLECTING) {
            LeaveCriticalSection(&m->lock);
            return 0;
        }
        m->state = OWNER_TERMINAL;
        InterlockedIncrement(&m->cleanup_count);
        LeaveCriticalSection(&m->lock);
        return 1;
    }
    if (probe != WAIT_TIMEOUT) {
        LeaveCriticalSection(&m->lock);
        return 0;
    }
    m->state = OWNER_REAPER_QUEUED;
    /* Caller relinquishes operation access at this exact state transition. */
    LeaveCriticalSection(&m->lock);
    return 1;
}

static int run_case(BOOL worker_wins) {
    operation_model m;
    HANDLE worker = NULL, signaler = NULL;
    DWORD exit_code = 99;
    int ok = 0;

    ZeroMemory(&m, sizeof(m));
    InitializeCriticalSection(&m.lock);
    m.state = OWNER_CALLER_PENDING;
    m.scenario_worker_wins = worker_wins;
    m.completion = CreateEventW(NULL, TRUE, FALSE, NULL);
    m.probe_done = CreateEventW(NULL, TRUE, FALSE, NULL);
    m.allow_enqueue = CreateEventW(NULL, TRUE, FALSE, NULL);
    m.worker_done = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!check(m.completion && m.probe_done && m.allow_enqueue && m.worker_done,
        "create deterministic model events")) goto cleanup;

    if (worker_wins) {
        worker = CreateThread(NULL, 0, model_worker, &m, 0, NULL);
        if (!check(worker != NULL, "start model worker")) goto cleanup;
        signaler = CreateThread(NULL, 0, signal_completion_after_probe, &m, 0, NULL);
        if (!check(signaler != NULL, "start deterministic completion-race signaler")) goto cleanup;
        /* Signal completion after the zero-time probe but before the caller commits handoff,
           while the caller still holds the ownership lock. The worker must wait for that lock. */
        EnterCriticalSection(&m.lock);
        if (!check(WaitForSingleObject(m.completion, 0) == WAIT_TIMEOUT,
            "pre-handoff probe observes pending")) {
            LeaveCriticalSection(&m.lock);
            goto cleanup;
        }
        SetEvent(m.probe_done);
        if (!check(WaitForSingleObject(m.completion, 5000) == WAIT_OBJECT_0,
            "completion races inside ownership critical section")) {
            LeaveCriticalSection(&m.lock);
            goto cleanup;
        }
        m.state = OWNER_REAPER_QUEUED;
        LeaveCriticalSection(&m.lock);
        if (!check(WaitForSingleObject(m.worker_done, 5000) == WAIT_OBJECT_0,
            "worker collects completion after handoff")) goto cleanup;
        if (!check(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0,
            "worker thread terminates")) goto cleanup;
        if (!check(GetExitCodeThread(worker, &exit_code) && exit_code == 0,
            "worker exits with terminal ownership")) goto cleanup;
        m.caller_touched_after_handoff = FALSE;
    } else {
        SetEvent(m.completion);
        if (!check(caller_handoff(&m), "caller wins when event is already signaled")) goto cleanup;
    }

    if (!check(m.state == OWNER_TERMINAL, "one terminal owner")) goto cleanup;
    if (!check(m.cleanup_count == 1, "exactly one cleanup")) goto cleanup;
    if (!check(m.caller_collectors + m.worker_collectors == 1,
        "exactly one completion collector")) goto cleanup;
    if (!check(worker_wins ? (m.worker_collectors == 1 && m.caller_collectors == 0)
                           : (m.caller_collectors == 1 && m.worker_collectors == 0),
        "expected winner owns collection")) goto cleanup;
    if (!check(!m.caller_touched_after_handoff, "caller does not touch operation after handoff")) goto cleanup;

    printf("PASS: %s wins completion ownership race\n",
        worker_wins ? "reaper worker" : "caller");
    ok = 1;

cleanup:
    if (signaler) {
        WaitForSingleObject(signaler, 5000);
        CloseHandle(signaler);
    }
    if (worker) {
        WaitForSingleObject(worker, 5000);
        CloseHandle(worker);
    }
    if (m.worker_done) CloseHandle(m.worker_done);
    if (m.allow_enqueue) CloseHandle(m.allow_enqueue);
    if (m.probe_done) CloseHandle(m.probe_done);
    if (m.completion) CloseHandle(m.completion);
    DeleteCriticalSection(&m.lock);
    return ok;
}

int main(void) {
    if (!run_case(FALSE)) return 1;
    if (!run_case(TRUE)) return 1;
    puts("PASS: both deterministic ownership-handoff model cases");
    return 0;
}
