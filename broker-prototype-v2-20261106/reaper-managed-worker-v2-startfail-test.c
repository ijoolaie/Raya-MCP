#include "reaper-managed-worker-v2.h"
#include <stdio.h>
#define CHECK(x) do { ++checks; if (!(x)) { \
    (void)fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)
static volatile LONG factory_calls = 0;
static HANDLE WINAPI fail_factory(LPTHREAD_START_ROUTINE entry,
    LPVOID parameter, void *context) {
    (void)entry; (void)parameter; (void)context;
    (void)InterlockedIncrement(&factory_calls);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
}
int main(void) {
    unsigned checks = 0;
    raya_managed_worker worker;
    ZeroMemory(&worker, sizeof(worker));
    CHECK(!raya_managed_worker_start_with_factory(&worker, fail_factory, NULL));
    CHECK(InterlockedCompareExchange(&factory_calls, 0, 0) == 1);
    CHECK(!worker.initialized && !worker.started && worker.thread == NULL);
    CHECK(!raya_managed_worker_admit(&worker));
    CHECK(!raya_managed_worker_shutdown(&worker));
    CHECK(!raya_managed_worker_destroy(&worker));
    (void)printf("PASS: %u assertions; thread-start failure cleans unstarted worker state\n", checks);
    return 0;
}
