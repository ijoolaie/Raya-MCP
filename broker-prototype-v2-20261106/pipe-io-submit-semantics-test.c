#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(x, name) do { if (!(x)) { printf("FAIL %s (error=%lu)\n", name, (unsigned long)GetLastError()); return 1; } } while (0)

typedef struct pipe_pair {
    HANDLE server;
    HANDLE client;
} pipe_pair;

static HANDLE overlapped_event_value(HANDLE event_handle) {
    return (HANDLE)((ULONG_PTR)event_handle | (ULONG_PTR)1);
}

static int create_pair(pipe_pair *pair, HANDLE *connect_event_out) {
    wchar_t name[160];
    OVERLAPPED connect_ov;
    HANDLE connect_event;
    BOOL connected;
    DWORD error;

    pair->server = INVALID_HANDLE_VALUE;
    pair->client = INVALID_HANDLE_VALUE;
    connect_event = NULL;
    memset(&connect_ov, 0, sizeof(connect_ov));

    if (swprintf_s(name, sizeof(name) / sizeof(name[0]),
        L"\\\\.\\pipe\\RayaPipeSubmitSemantics_%lu", (unsigned long)GetCurrentProcessId()) < 0) {
        return 0;
    }

    pair->server = CreateNamedPipeW(
        name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);
    if (pair->server == INVALID_HANDLE_VALUE) return 0;

    pair->client = CreateFileW(
        name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (pair->client == INVALID_HANDLE_VALUE) return 0;

    connect_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!connect_event) return 0;
    connect_ov.hEvent = overlapped_event_value(connect_event);

    connected = ConnectNamedPipe(pair->server, &connect_ov);
    if (!connected) {
        error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            if (WaitForSingleObject(connect_event, 5000) != WAIT_OBJECT_0) {
                CloseHandle(connect_event);
                return 0;
            }
            {
                DWORD ignored = 0;
                if (!GetOverlappedResult(pair->server, &connect_ov, &ignored, FALSE)) {
                    CloseHandle(connect_event);
                    return 0;
                }
            }
        } else if (error != ERROR_PIPE_CONNECTED) {
            CloseHandle(connect_event);
            return 0;
        }
    }

    CloseHandle(connect_event);
    if (connect_event_out) *connect_event_out = NULL;
    return 1;
}

static void close_pair(pipe_pair *pair) {
    if (pair->client && pair->client != INVALID_HANDLE_VALUE) CloseHandle(pair->client);
    if (pair->server && pair->server != INVALID_HANDLE_VALUE) CloseHandle(pair->server);
    pair->client = INVALID_HANDLE_VALUE;
    pair->server = INVALID_HANDLE_VALUE;
}

static int test_immediate_read(void) {
    static const unsigned char sent[] = { 'R', 'E', 'A', 'D' };
    unsigned char received[sizeof(sent)];
    pipe_pair pair;
    OVERLAPPED ov;
    HANDLE event_handle;
    DWORD written = 0, read_count = 0xccccccccu, error;
    BOOL ok;

    CHECK(create_pair(&pair, NULL), "create pair for immediate read");
    CHECK(WriteFile(pair.client, sent, (DWORD)sizeof(sent), &written, NULL), "prime immediate read");
    CHECK(written == sizeof(sent), "prime immediate read byte count");

    memset(&ov, 0, sizeof(ov));
    event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(event_handle != NULL, "create immediate-read event");
    ov.hEvent = overlapped_event_value(event_handle);

    ok = ReadFile(pair.server, received, (DWORD)sizeof(received), &read_count, &ov);
    if (!ok) {
        error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            DWORD pending_count = 0;
            CHECK(WaitForSingleObject(event_handle, 5000) == WAIT_OBJECT_0, "wait pending read fallback");
            CHECK(GetOverlappedResult(pair.server, &ov, &pending_count, FALSE), "collect pending read fallback");
            CloseHandle(event_handle);
            close_pair(&pair);
            printf("INCONCLUSIVE immediate ReadFile branch: operation was pending (bytes=%lu)\n",
                (unsigned long)pending_count);
            return 2;
        }
        CloseHandle(event_handle);
        close_pair(&pair);
        printf("FAIL immediate ReadFile returned error %lu\n", (unsigned long)error);
        return 1;
    }

    CHECK(read_count == sizeof(sent), "immediate ReadFile byte count");
    CHECK(memcmp(received, sent, sizeof(sent)) == 0, "immediate ReadFile payload");
    CloseHandle(event_handle);
    close_pair(&pair);
    printf("PASS immediate ReadFile byte-count branch\n");
    return 0;
}

static int test_immediate_write(void) {
    static const unsigned char sent[] = { 'W', 'R', 'I', 'T', 'E' };
    unsigned char received[sizeof(sent)];
    pipe_pair pair;
    OVERLAPPED ov;
    HANDLE event_handle;
    DWORD write_count = 0xccccccccu, read_count = 0, error;
    BOOL ok;

    CHECK(create_pair(&pair, NULL), "create pair for immediate write");
    memset(&ov, 0, sizeof(ov));
    event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(event_handle != NULL, "create immediate-write event");
    ov.hEvent = overlapped_event_value(event_handle);

    ok = WriteFile(pair.server, sent, (DWORD)sizeof(sent), &write_count, &ov);
    if (!ok) {
        error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            DWORD pending_count = 0;
            CHECK(WaitForSingleObject(event_handle, 5000) == WAIT_OBJECT_0, "wait pending write fallback");
            CHECK(GetOverlappedResult(pair.server, &ov, &pending_count, FALSE), "collect pending write fallback");
            CHECK(ReadFile(pair.client, received, (DWORD)sizeof(received), &read_count, NULL), "read pending write payload");
            CloseHandle(event_handle);
            close_pair(&pair);
            printf("INCONCLUSIVE immediate WriteFile branch: operation was pending (bytes=%lu)\n",
                (unsigned long)pending_count);
            return 2;
        }
        CloseHandle(event_handle);
        close_pair(&pair);
        printf("FAIL immediate WriteFile returned error %lu\n", (unsigned long)error);
        return 1;
    }

    CHECK(write_count == sizeof(sent), "immediate WriteFile byte count");
    CHECK(ReadFile(pair.client, received, (DWORD)sizeof(received), &read_count, NULL), "read immediate write payload");
    CHECK(read_count == sizeof(sent), "immediate write peer byte count");
    CHECK(memcmp(received, sent, sizeof(sent)) == 0, "immediate write peer payload");
    CloseHandle(event_handle);
    close_pair(&pair);
    printf("PASS immediate WriteFile byte-count branch\n");
    return 0;
}

static int test_pending_write(void) {
    const DWORD total = 1024u * 1024u;
    unsigned char *sent = NULL;
    unsigned char received[4096];
    pipe_pair pair;
    OVERLAPPED ov;
    HANDLE event_handle = NULL;
    DWORD immediate_count = 0xccccccccu, transferred = 0, error = 0;
    DWORD consumed = 0;
    BOOL ok;
    int result = 1;

    sent = (unsigned char*)malloc(total);
    CHECK(sent != NULL, "allocate pending-write payload");
    for (DWORD i = 0; i < total; ++i) sent[i] = (unsigned char)((i * 37u + 11u) & 0xffu);

    CHECK(create_pair(&pair, NULL), "create pair for pending write");
    memset(&ov, 0, sizeof(ov));
    event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(event_handle != NULL, "create pending-write event");
    ov.hEvent = overlapped_event_value(event_handle);

    /* The 1 MiB write exceeds the 4 KiB outbound pipe buffer; no peer read is
       issued until after submission, so a pending result must be observed. */
    ok = WriteFile(pair.server, sent, total, &immediate_count, &ov);
    if (ok) {
        printf("INCONCLUSIVE pending WriteFile branch: large write completed immediately (bytes=%lu)\n",
            (unsigned long)immediate_count);
        result = 2;
        goto cleanup;
    }
    error = GetLastError();
    if (error != ERROR_IO_PENDING) {
        printf("FAIL pending WriteFile submission error %lu\n", (unsigned long)error);
        goto cleanup;
    }

    while (consumed < total) {
        DWORD got = 0;
        DWORD wanted = total - consumed;
        if (wanted > sizeof(received)) wanted = (DWORD)sizeof(received);
        if (!ReadFile(pair.client, received, wanted, &got, NULL)) {
            error = GetLastError();
            printf("FAIL draining pending WriteFile payload (error=%lu, consumed=%lu)\n",
                (unsigned long)error, (unsigned long)consumed);
            goto cancel_pending;
        }
        if (got == 0 || memcmp(received, sent + consumed, got) != 0) {
            printf("FAIL pending WriteFile peer payload/count (got=%lu, consumed=%lu)\n",
                (unsigned long)got, (unsigned long)consumed);
            goto cancel_pending;
        }
        consumed += got;
    }

    if (WaitForSingleObject(event_handle, 5000) != WAIT_OBJECT_0) {
        printf("FAIL pending WriteFile completion event (error=%lu)\n",
            (unsigned long)GetLastError());
        goto cancel_pending;
    }
    if (!GetOverlappedResult(pair.server, &ov, &transferred, FALSE)) {
        error = GetLastError();
        printf("FAIL collect pending WriteFile result (error=%lu)\n", (unsigned long)error);
        goto cleanup;
    }
    if (transferred != total || consumed != total) {
        printf("FAIL pending WriteFile byte count (transferred=%lu, consumed=%lu)\n",
            (unsigned long)transferred, (unsigned long)consumed);
        goto cleanup;
    }

    printf("PASS ERROR_IO_PENDING WriteFile/GetOverlappedResult branch\n");
    result = 0;
    goto cleanup;

cancel_pending:
    (void)CancelIoEx(pair.server, &ov);
    if (WaitForSingleObject(event_handle, 5000) == WAIT_OBJECT_0) {
        DWORD ignored = 0;
        (void)GetOverlappedResult(pair.server, &ov, &ignored, FALSE);
    } else {
        printf("INCOMPLETE cleanup: pending WriteFile did not signal after cancellation\n");
    }

cleanup:
    if (event_handle) CloseHandle(event_handle);
    close_pair(&pair);
    free(sent);
    return result;
}

static int test_cancel_pending_read(void) {
    unsigned char received[8];
    pipe_pair pair;
    OVERLAPPED ov;
    HANDLE event_handle = NULL;
    DWORD count = 0, error = 0, transferred = 0;
    BOOL ok;
    int result = 1;

    if (!create_pair(&pair, NULL)) return 1;
    memset(&ov, 0, sizeof(ov));
    event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event_handle) goto cleanup;
    ov.hEvent = overlapped_event_value(event_handle);

    ok = ReadFile(pair.server, received, sizeof(received), &count, &ov);
    if (ok || GetLastError() != ERROR_IO_PENDING) {
        printf("FAIL cancellation setup did not produce pending read (error=%lu)\n",
            (unsigned long)GetLastError());
        goto cleanup;
    }

    if (!CancelIoEx(pair.server, &ov)) {
        error = GetLastError();
        printf("FAIL CancelIoEx pending-read request (error=%lu)\n", (unsigned long)error);
        goto cleanup;
    }
    if (WaitForSingleObject(event_handle, 5000) != WAIT_OBJECT_0) {
        printf("FAIL cancellation completion event (error=%lu)\n", (unsigned long)GetLastError());
        goto cleanup;
    }
    if (GetOverlappedResult(pair.server, &ov, &transferred, FALSE)) {
        printf("FAIL cancelled read unexpectedly reported success (bytes=%lu)\n",
            (unsigned long)transferred);
        goto cleanup;
    }
    error = GetLastError();
    if (error != ERROR_OPERATION_ABORTED) {
        printf("FAIL cancelled read terminal error=%lu, expected ERROR_OPERATION_ABORTED\n",
            (unsigned long)error);
        goto cleanup;
    }
    printf("PASS CancelIoEx accepted; terminal event/result collected as OPERATION_ABORTED\n");
    result = 0;

cleanup:
    if (event_handle) CloseHandle(event_handle);
    close_pair(&pair);
    return result;
}

static int test_cancel_completion_race(void) {
    static const unsigned char sent[] = { 'R', 'A', 'C', 'E' };
    unsigned char received[sizeof(sent)];
    pipe_pair pair;
    OVERLAPPED ov;
    HANDLE event_handle = NULL;
    DWORD count = 0, written = 0, transferred = 0, error = 0;
    BOOL ok;
    int result = 1;

    if (!create_pair(&pair, NULL)) return 1;
    memset(&ov, 0, sizeof(ov));
    event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event_handle) goto cleanup;
    ov.hEvent = overlapped_event_value(event_handle);

    ok = ReadFile(pair.server, received, sizeof(received), &count, &ov);
    if (ok || GetLastError() != ERROR_IO_PENDING) {
        printf("FAIL race setup did not produce pending read (error=%lu)\n",
            (unsigned long)GetLastError());
        goto cleanup;
    }
    if (!WriteFile(pair.client, sent, sizeof(sent), &written, NULL) || written != sizeof(sent)) {
        printf("FAIL release race read (error=%lu)\n", (unsigned long)GetLastError());
        goto cleanup;
    }
    if (WaitForSingleObject(event_handle, 5000) != WAIT_OBJECT_0) {
        printf("FAIL race completion event (error=%lu)\n", (unsigned long)GetLastError());
        goto cleanup;
    }

    if (CancelIoEx(pair.server, &ov)) {
        printf("FAIL CancelIoEx unexpectedly matched already-completed operation\n");
        goto cleanup;
    }
    error = GetLastError();
    if (error != ERROR_NOT_FOUND) {
        printf("FAIL completion-race CancelIoEx error=%lu, expected ERROR_NOT_FOUND\n",
            (unsigned long)error);
        goto cleanup;
    }
    if (!GetOverlappedResult(pair.server, &ov, &transferred, FALSE) ||
        transferred != sizeof(sent) || memcmp(received, sent, sizeof(sent)) != 0) {
        printf("FAIL collect completed read after ERROR_NOT_FOUND (error=%lu)\n",
            (unsigned long)GetLastError());
        goto cleanup;
    }
    printf("PASS completion wins; CancelIoEx ERROR_NOT_FOUND; result collected normally\n");
    result = 0;

cleanup:
    if (event_handle) CloseHandle(event_handle);
    close_pair(&pair);
    return result;
}

static int test_cancel_invalid_handle_error(void) {
    OVERLAPPED ov;
    BOOL ok;
    DWORD error;
    memset(&ov, 0, sizeof(ov));
    ok = CancelIoEx(INVALID_HANDLE_VALUE, &ov);
    if (ok) {
        printf("FAIL CancelIoEx invalid handle unexpectedly succeeded\n");
        return 1;
    }
    error = GetLastError();
    if (error != ERROR_INVALID_HANDLE) {
        printf("FAIL CancelIoEx invalid handle error=%lu, expected ERROR_INVALID_HANDLE\n",
            (unsigned long)error);
        return 1;
    }
    printf("PASS CancelIoEx non-ERROR_NOT_FOUND API failure classification (invalid handle)\n");
    return 0;
}

static int test_pending_read(void) {
    static const unsigned char sent[] = { 'P', 'E', 'N', 'D' };
    unsigned char received[sizeof(sent)];
    pipe_pair pair;
    OVERLAPPED ov;
    HANDLE event_handle;
    DWORD immediate_count = 0xccccccccu, written = 0, transferred = 0, error;
    BOOL ok;

    CHECK(create_pair(&pair, NULL), "create pair for pending read");
    memset(&ov, 0, sizeof(ov));
    event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(event_handle != NULL, "create pending-read event");
    ov.hEvent = overlapped_event_value(event_handle);

    ok = ReadFile(pair.server, received, (DWORD)sizeof(received), &immediate_count, &ov);
    CHECK(!ok, "pending ReadFile returns FALSE");
    error = GetLastError();
    CHECK(error == ERROR_IO_PENDING, "pending ReadFile returns ERROR_IO_PENDING");
    CHECK(WriteFile(pair.client, sent, (DWORD)sizeof(sent), &written, NULL), "release pending read");
    CHECK(written == sizeof(sent), "pending read peer write count");
    CHECK(WaitForSingleObject(event_handle, 5000) == WAIT_OBJECT_0, "wait pending read event");
    CHECK(GetOverlappedResult(pair.server, &ov, &transferred, FALSE), "collect pending read result");
    CHECK(transferred == sizeof(sent), "pending read transferred count");
    CHECK(memcmp(received, sent, sizeof(sent)) == 0, "pending read payload");

    CloseHandle(event_handle);
    close_pair(&pair);
    printf("PASS ERROR_IO_PENDING ReadFile/GetOverlappedResult branch\n");
    return 0;
}

static int test_immediate_submission_failure(void) {
    unsigned char byte = 0;
    OVERLAPPED ov;
    HANDLE event_handle;
    DWORD count = 0, error;
    BOOL ok;

    memset(&ov, 0, sizeof(ov));
    event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(event_handle != NULL, "create failure-test event");
    ov.hEvent = overlapped_event_value(event_handle);

    ok = ReadFile(INVALID_HANDLE_VALUE, &byte, 1, &count, &ov);
    CHECK(!ok, "invalid-handle ReadFile returns FALSE");
    error = GetLastError();
    CHECK(error == ERROR_INVALID_HANDLE, "invalid-handle ReadFile error");
    CHECK(WaitForSingleObject(event_handle, 0) == WAIT_TIMEOUT, "submission failure has no completion event");

    CloseHandle(event_handle);
    printf("PASS immediate non-pending submission failure branch\n");
    return 0;
}

int main(void) {
    int result;
    result = test_immediate_read();
    if (result != 0) return result;

    result = test_immediate_write();
    if (result != 0) return result;

    result = test_pending_write();
    if (result != 0) return result;

    if (test_cancel_pending_read() != 0) return 1;
    if (test_cancel_completion_race() != 0) return 1;
    if (test_cancel_invalid_handle_error() != 0) return 1;

    if (test_pending_read() != 0) return 1;
    if (test_immediate_submission_failure() != 0) return 1;

    printf("PASS pipe submission semantics harness\n");
    return 0;
}
