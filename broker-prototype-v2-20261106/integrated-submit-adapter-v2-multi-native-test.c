#include "integrated-submit-adapter-v2.h"
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>

#define NOPS 70
#define NPRODUCERS 7
#define PER_PRODUCER 10
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

typedef struct {
    HANDLE semaphore;
    volatile LONG calls;
    DWORD error;
    DWORD transferred;
    BYTE value;
} callback_state;

typedef struct {
    raya_integrated_submit_adapter *adapter;
    HANDLE *pipes;
    callback_state *callbacks;
    unsigned first;
    unsigned count;
    volatile LONG *failures;
} producer_args;

static unsigned checks;
static void completion(DWORD error,DWORD transferred,const void *data,void *context) {
    callback_state *s=(callback_state *)context;
    s->error=error; s->transferred=transferred;
    if (data && transferred==1) s->value=*(const BYTE *)data;
    InterlockedIncrement(&s->calls);
    (void)ReleaseSemaphore(s->semaphore,1,NULL);
}
static DWORD WINAPI producer_main(void *context) {
    producer_args *a=(producer_args *)context;
    unsigned i,order[PER_PRODUCER];
    uint32_t random_state=(uint32_t)(GetTickCount64() ^ GetCurrentThreadId() ^ (a->first*2654435761U));
    for (i=0;i<a->count;++i) order[i]=i;
    for (i=a->count;i>1;--i) {
        unsigned j;
        random_state^=random_state<<13; random_state^=random_state>>17; random_state^=random_state<<5;
        j=(unsigned)(random_state%i);
        { unsigned tmp=order[i-1]; order[i-1]=order[j]; order[j]=tmp; }
    }
    for (i=0;i<a->count;++i) {
        unsigned n=a->first+order[i];
        raya_adapter_result r;
        random_state^=random_state<<13; random_state^=random_state>>17; random_state^=random_state<<5;
        Sleep(random_state%4U);
        r=raya_integrated_adapter_submit(a->adapter,a->pipes[n],
            RAYA_MANAGED_PIPE_READ,NULL,1,completion,&a->callbacks[n]);
        if (r!=RAYA_ADAPTER_QUEUED) InterlockedIncrement(a->failures);
    }
    return 0;
}
static int create_pipe(unsigned index,HANDLE *server,HANDLE *client,
    DWORD pid,ULONGLONG nonce) {
    wchar_t name[192]; HANDLE s,c,connect_event; OVERLAPPED connect_ov;
    BOOL connected; DWORD connect_error;
    (void)swprintf_s(name,192,L"\\\\.\\pipe\\raya-integrated-70-%lu-%llu-%u",
        (unsigned long)pid,(unsigned long long)nonce,index);
    s=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
    if (s==INVALID_HANDLE_VALUE) return 0;
    c=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if (c==INVALID_HANDLE_VALUE) { CloseHandle(s); return 0; }
    ZeroMemory(&connect_ov,sizeof(connect_ov));
    connect_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (connect_event==NULL) { CloseHandle(c); CloseHandle(s); return 0; }
    connect_ov.hEvent=connect_event;
    connected=ConnectNamedPipe(s,&connect_ov);
    connect_error=connected?ERROR_SUCCESS:GetLastError();
    CloseHandle(connect_event);
    if (!connected && connect_error!=ERROR_PIPE_CONNECTED) {
        CloseHandle(c); CloseHandle(s); return 0;
    }
    *server=s; *client=c; return 1;
}
int main(void) {
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_integrated_submit_adapter adapter;
    HANDLE servers[NOPS],clients[NOPS],threads[NPRODUCERS];
    callback_state callbacks[NOPS];
    producer_args args[NPRODUCERS];
    HANDLE semaphore;
    volatile LONG failures=0;
    raya_submitter_lifecycle lifecycle;
    size_t q=0,p=0,a=0,leases=0;
    unsigned i,round;
    ULONGLONG nonce=GetTickCount64();
    ZeroMemory(&submitter,sizeof(submitter)); ZeroMemory(&worker,sizeof(worker));
    ZeroMemory(&adapter,sizeof(adapter)); ZeroMemory(servers,sizeof(servers));
    ZeroMemory(clients,sizeof(clients)); ZeroMemory(threads,sizeof(threads));
    ZeroMemory(callbacks,sizeof(callbacks)); ZeroMemory(args,sizeof(args));
    semaphore=CreateSemaphoreW(NULL,0,NOPS,NULL);
    CHECK(semaphore!=NULL);
    for (i=0;i<NOPS;++i) {
        CHECK(create_pipe(i,&servers[i],&clients[i],GetCurrentProcessId(),nonce));
        callbacks[i].semaphore=semaphore;
    }
    CHECK(raya_managed_worker_start(&worker));
    CHECK(raya_integrated_adapter_start(&adapter,&submitter,&worker, NOPS));
    for (i=0;i<NPRODUCERS;++i) {
        args[i].adapter=&adapter; args[i].pipes=servers; args[i].callbacks=callbacks;
        args[i].first=i*PER_PRODUCER; args[i].count=PER_PRODUCER;
        args[i].failures=&failures;
        threads[i]=CreateThread(NULL,0,producer_main,&args[i],0,NULL);
        CHECK(threads[i]!=NULL);
    }
    for (i=0;i<NPRODUCERS;++i) {
        CHECK(WaitForSingleObject(threads[i],10000)==WAIT_OBJECT_0);
        CHECK(CloseHandle(threads[i])); threads[i]=NULL;
    }
    CHECK(InterlockedCompareExchange(&failures,0,0)==0);

    /* Wait on actual ownership accounting, not a timing delay: all 70 requests
       must be issued and resident in the Managed Worker before peers complete. */
    for (round=0;round<10000;++round) {
        BOOL ready;
        EnterCriticalSection(&worker.lock);
        ready=(worker.integrated.registry.count==NOPS &&
            worker.integrated.core.outstanding==NOPS &&
            worker.integrated.core.queued==0 &&
            worker.integrated.reserved_slots==NOPS);
        LeaveCriticalSection(&worker.lock);
        if (ready) break;
        Sleep(1);
    }
    CHECK(round<10000);
    CHECK(raya_submitter_snapshot(&submitter,&lifecycle,&q,&p,&a,&leases));
    CHECK(lifecycle==RAYA_SUBMITTER_RUNNING && q==0 && p==0 && a==0 && leases==NOPS);

    /* All producers have exited; none of the 70 reads has been completed yet. */
    for (i=0;i<NOPS;++i) CHECK(InterlockedCompareExchange(&callbacks[i].calls,0,0)==0);
    {
        unsigned order[NOPS]; uint32_t random_state=(uint32_t)(GetTickCount64() ^ GetCurrentProcessId());
        for (i=0;i<NOPS;++i) order[i]=i;
        for (i=NOPS;i>1;--i) {
            unsigned j; random_state^=random_state<<13; random_state^=random_state>>17; random_state^=random_state<<5;
            j=(unsigned)(random_state%i);
            { unsigned tmp=order[i-1]; order[i-1]=order[j]; order[j]=tmp; }
        }
        for (i=0;i<NOPS;++i) {
            unsigned n=order[i]; BYTE value=(BYTE)(0x40U+n); DWORD written=0;
            random_state^=random_state<<13; random_state^=random_state>>17; random_state^=random_state<<5;
            Sleep(random_state%3U);
            CHECK(WriteFile(clients[n],&value,1,&written,NULL));
            CHECK(written==1);
        }
    }
    for (i=0;i<NOPS;++i) CHECK(WaitForSingleObject(semaphore,15000)==WAIT_OBJECT_0);
    for (i=0;i<NOPS;++i) {
        CHECK(InterlockedCompareExchange(&callbacks[i].calls,0,0)==1);
        CHECK(callbacks[i].error==ERROR_SUCCESS);
        CHECK(callbacks[i].transferred==1);
        CHECK(callbacks[i].value==(BYTE)(0x40U+i));
    }
    CHECK(raya_submitter_snapshot(&submitter,&lifecycle,&q,&p,&a,&leases));
    CHECK(lifecycle==RAYA_SUBMITTER_RUNNING && q==0 && p==0 && a==0 && leases==0);
    CHECK(raya_integrated_adapter_retained_count(&adapter)==0);
    CHECK(raya_submitter_shutdown(&submitter));
    CHECK(raya_managed_worker_shutdown(&worker));
    CHECK(raya_submitter_join(&submitter,10000));
    CHECK(raya_managed_worker_join(&worker,10000));
    CHECK(raya_integrated_adapter_destroy(&adapter));
    CHECK(raya_submitter_destroy(&submitter));
    CHECK(raya_managed_worker_destroy(&worker));
    for (i=0;i<NOPS;++i) {
        CHECK(CloseHandle(clients[i]));
        CHECK(CloseHandle(servers[i]));
    }
    CHECK(CloseHandle(semaphore));
    printf("PASS: %u assertions; 70 integrated live operations, 7 exited producers, completion window >63\n",checks);
    return 0;
}
