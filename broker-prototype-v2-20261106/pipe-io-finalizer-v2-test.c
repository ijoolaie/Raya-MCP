#include "pipe-io-finalizer-v2.h"
#include <stdio.h>
#include <stdint.h>
static int checks=0, failures=0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL line %d: %s\n",__LINE__,#x); } } while (0)
typedef struct { int calls; int fail_on; HANDLE seen[4]; } fake_close;
static BOOL fake_close_handle(HANDLE h, void *ctx) {
 fake_close *f=(fake_close *)ctx;
 ++f->calls;
 if (f->calls<=4) f->seen[f->calls-1]=h;
 return f->calls!=f->fail_on;
}
static HANDLE hval(uintptr_t n) { return (HANDLE)n; }
static void test_success(void) {
 raya_pipe_finalizer f; fake_close c={0};
 CHECK(raya_pipe_finalizer_init(&f,hval(0x101),hval(0x202)));
 CHECK(raya_pipe_finalizer_close_handles(&f,fake_close_handle,&c)==RAYA_FINALIZER_OK);
 CHECK(c.calls==2 && c.seen[0]==hval(0x101) && c.seen[1]==hval(0x202));
 CHECK(f.state==RAYA_FINALIZER_HANDLES_CLOSED && f.successful_closes==2);
 CHECK(f.issuing_pipe==NULL && f.completion_event==NULL);
 CHECK(raya_pipe_finalizer_commit_release(&f));
 CHECK(f.state==RAYA_FINALIZER_RELEASED && f.release_commits==1);
 CHECK(!raya_pipe_finalizer_commit_release(&f));
 CHECK(raya_pipe_finalizer_close_handles(&f,fake_close_handle,&c)==RAYA_FINALIZER_OK);
}
static void test_first_close_failure(void) {
 raya_pipe_finalizer f; fake_close c={0}; c.fail_on=1;
 CHECK(raya_pipe_finalizer_init(&f,hval(0x303),hval(0x404)));
 CHECK(raya_pipe_finalizer_close_handles(&f,fake_close_handle,&c)==RAYA_FINALIZER_CLOSE_FAILED);
 CHECK(c.calls==1 && f.issuing_pipe==hval(0x303) && f.completion_event==hval(0x404));
 CHECK(f.state==RAYA_FINALIZER_CLOSE_UNCERTAIN && f.successful_closes==0);
 CHECK(!raya_pipe_finalizer_commit_release(&f));
 CHECK(raya_pipe_finalizer_close_handles(&f,fake_close_handle,&c)==RAYA_FINALIZER_CLOSE_FAILED);
 CHECK(c.calls==1);
}
static void test_second_close_failure(void) {
 raya_pipe_finalizer f; fake_close c={0}; c.fail_on=2;
 CHECK(raya_pipe_finalizer_init(&f,hval(0x505),hval(0x606)));
 CHECK(raya_pipe_finalizer_close_handles(&f,fake_close_handle,&c)==RAYA_FINALIZER_CLOSE_FAILED);
 CHECK(c.calls==2 && c.seen[0]==hval(0x505) && c.seen[1]==hval(0x606));
 CHECK(f.issuing_pipe==NULL && f.completion_event==hval(0x606));
 CHECK(f.successful_closes==1 && f.state==RAYA_FINALIZER_CLOSE_UNCERTAIN);
 CHECK(!raya_pipe_finalizer_commit_release(&f));
 CHECK(raya_pipe_finalizer_close_handles(&f,fake_close_handle,&c)==RAYA_FINALIZER_CLOSE_FAILED);
 CHECK(c.calls==2);
}
static void test_invalid(void) {
 raya_pipe_finalizer f;
 CHECK(!raya_pipe_finalizer_init(&f,NULL,hval(2)));
 CHECK(!raya_pipe_finalizer_init(&f,hval(1),hval(1)));
 CHECK(!raya_pipe_finalizer_init(&f,INVALID_HANDLE_VALUE,hval(2)));
 CHECK(raya_pipe_finalizer_init(&f,hval(3),hval(4)));
 CHECK(raya_pipe_finalizer_close_handles(&f,NULL,NULL)==RAYA_FINALIZER_INVALID);
 CHECK(!raya_pipe_finalizer_commit_release(&f));
}
int main(void) {
 test_success(); test_first_close_failure(); test_second_close_failure(); test_invalid();
 printf("pipe-io-finalizer-v2: %d assertions, %d failures\n",checks,failures);
 return failures?1:0;
}
