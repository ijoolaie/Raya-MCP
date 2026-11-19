#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "pipe-completion-context-v2.h"

static HANDLE ov_event(HANDLE h) { return (HANDLE)((ULONG_PTR)h | (ULONG_PTR)1); }
typedef struct { HANDLE server, client; } pipe_pair;

static int make_pair(pipe_pair *p) {
    wchar_t name[160];
    OVERLAPPED ov;
    HANDLE ev;
    DWORD ignored=0, err;
    BOOL ok;
    p->server=INVALID_HANDLE_VALUE; p->client=INVALID_HANDLE_VALUE;
    if (swprintf_s(name, 160, L"\\\\.\\pipe\\RayaGORProbe_%lu",
                   (unsigned long)GetCurrentProcessId()) < 0) return 0;
    p->server=CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT, 1,4096,4096,0,NULL);
    if(p->server==INVALID_HANDLE_VALUE) return 0;
    p->client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(p->client==INVALID_HANDLE_VALUE) return 0;
    memset(&ov,0,sizeof(ov)); ev=CreateEventW(NULL,TRUE,FALSE,NULL); if(!ev) return 0;
    ov.hEvent=ov_event(ev); ok=ConnectNamedPipe(p->server,&ov);
    if(!ok) {
        err=GetLastError();
        if(err==ERROR_IO_PENDING) {
            if(WaitForSingleObject(ev,5000)!=WAIT_OBJECT_0 ||
               !GetOverlappedResult(p->server,&ov,&ignored,FALSE)) { CloseHandle(ev); return 0; }
        } else if(err!=ERROR_PIPE_CONNECTED) { CloseHandle(ev); return 0; }
    }
    CloseHandle(ev); return 1;
}
static void close_pair(pipe_pair *p) {
    if(p->client!=INVALID_HANDLE_VALUE) CloseHandle(p->client);
    if(p->server!=INVALID_HANDLE_VALUE) CloseHandle(p->server);
}
#define REQUIRE(x,msg) do { if(!(x)) { printf("FAIL: %s (err=%lu)\n",msg,(unsigned long)GetLastError()); goto fail; } } while(0)

static int test_incomplete_then_success(void) {
    pipe_pair p; OVERLAPPED ov; HANDLE ev=NULL; unsigned char buf[4];
    DWORD immediate=0, bytes=0, wr=0, err; BOOL ok; int result=1;
    REQUIRE(make_pair(&p),"create pipe pair");
    memset(&ov,0,sizeof(ov)); ev=CreateEventW(NULL,TRUE,FALSE,NULL); REQUIRE(ev!=NULL,"create event");
    ov.hEvent=ov_event(ev);
    ok=ReadFile(p.server,buf,sizeof(buf),&immediate,&ov);
    REQUIRE(!ok,"ReadFile should pend"); err=GetLastError(); REQUIRE(err==ERROR_IO_PENDING,"capture pending");
    ok=GetOverlappedResult(p.server,&ov,&bytes,FALSE);
    REQUIRE(!ok,"early GetOverlappedResult should fail");
    err=GetLastError(); REQUIRE(err==ERROR_IO_INCOMPLETE,"early result is IO_INCOMPLETE");
    REQUIRE(WaitForSingleObject(ev,0)==WAIT_TIMEOUT,"event remains unsignaled");
    REQUIRE(WriteFile(p.client,"LIVE",4,&wr,NULL) && wr==4,"peer releases read");
    REQUIRE(WaitForSingleObject(ev,5000)==WAIT_OBJECT_0,"completion event signals");
    REQUIRE(GetOverlappedResult(p.server,&ov,&bytes,FALSE),"collect completed read");
    REQUIRE(bytes==4 && memcmp(buf,"LIVE",4)==0,"validate completed bytes");
    printf("PASS pending -> ERROR_IO_INCOMPLETE -> signaled -> successful collection\n"); result=0;
fail:
    if(ev) CloseHandle(ev); close_pair(&p); return result;
}
static int test_cancelled_terminal_result(void) {
    pipe_pair p; OVERLAPPED ov; HANDLE ev=NULL; unsigned char buf[8];
    DWORD immediate=0, bytes=0, err; BOOL ok; int result=1;
    REQUIRE(make_pair(&p),"create pipe pair for cancel");
    memset(&ov,0,sizeof(ov)); ev=CreateEventW(NULL,TRUE,FALSE,NULL); REQUIRE(ev!=NULL,"create cancel event");
    ov.hEvent=ov_event(ev);
    ok=ReadFile(p.server,buf,sizeof(buf),&immediate,&ov);
    REQUIRE(!ok,"cancel read should pend"); err=GetLastError(); REQUIRE(err==ERROR_IO_PENDING,"cancel read pending");
    REQUIRE(CancelIoEx(p.server,&ov),"CancelIoEx accepted");
    REQUIRE(WaitForSingleObject(ev,5000)==WAIT_OBJECT_0,"cancel event signals");
    ok=GetOverlappedResult(p.server,&ov,&bytes,FALSE);
    REQUIRE(!ok,"cancelled read result is failure");
    err=GetLastError(); REQUIRE(err==ERROR_OPERATION_ABORTED,"collect ERROR_OPERATION_ABORTED");
    REQUIRE(raya_classify_pipe_completion(TRUE,TRUE,FALSE,err)==RAYA_PIPE_COMPLETION_TERMINAL_ABORTED,"exact signaled aborted operation is terminal");
    REQUIRE(raya_classify_pipe_completion(FALSE,TRUE,FALSE,err)==RAYA_PIPE_COMPLETION_UNCERTAIN,"aborted result without event evidence is uncertain");
    REQUIRE(raya_classify_pipe_completion(TRUE,FALSE,FALSE,err)==RAYA_PIPE_COMPLETION_UNCERTAIN,"aborted result without exact issuing pair is uncertain");
    printf("PASS real cancelled operation collected as ERROR_OPERATION_ABORTED\n"); result=0;
fail:
    if(ev) CloseHandle(ev); close_pair(&p); return result;
}
static int test_wrong_handle_does_not_complete_operation(void) {
    pipe_pair p; OVERLAPPED ov; HANDLE ev=NULL; unsigned char buf[4];
    DWORD immediate=0, bytes=0, wr=0, err; BOOL ok; int result=1;
    REQUIRE(make_pair(&p),"create pipe pair for invalid-handle probe");
    memset(&ov,0,sizeof(ov)); ev=CreateEventW(NULL,TRUE,FALSE,NULL); REQUIRE(ev!=NULL,"create invalid-handle event");
    ov.hEvent=ov_event(ev);
    ok=ReadFile(p.server,buf,sizeof(buf),&immediate,&ov);
    REQUIRE(!ok,"probe read should pend"); err=GetLastError(); REQUIRE(err==ERROR_IO_PENDING,"probe read pending");
    ok=GetOverlappedResult(INVALID_HANDLE_VALUE,&ov,&bytes,FALSE);
    REQUIRE(!ok,"wrong handle GetOverlappedResult fails");
    err=GetLastError(); REQUIRE(err==ERROR_IO_INCOMPLETE,"pending state reported before wrong-handle validation");
    REQUIRE(WaitForSingleObject(ev,0)==WAIT_TIMEOUT,"wrong-handle probe did not complete operation");
    REQUIRE(WriteFile(p.client,"SAFE",4,&wr,NULL) && wr==4,"complete operation with correct peer");
    REQUIRE(WaitForSingleObject(ev,5000)==WAIT_OBJECT_0,"correct operation event signals");
    REQUIRE(GetOverlappedResult(p.server,&ov,&bytes,FALSE),"correct handle collects result");
    REQUIRE(bytes==4 && memcmp(buf,"SAFE",4)==0,"correct result payload");
    printf("PASS invalid-handle API error did not establish operation completion\n"); result=0;
fail:
    if(ev) CloseHandle(ev); close_pair(&p); return result;
}
int main(void) {
    if(test_incomplete_then_success()) return 1;
    if(test_cancelled_terminal_result()) return 1;
    if(test_wrong_handle_does_not_complete_operation()) return 1;
    printf("SUMMARY: all live GetOverlappedResult probes passed\n"); return 0;
}
