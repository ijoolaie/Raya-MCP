#include "raya-orchestrator-registry-v1.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x,n) do { if (x) ++passed; else { ++failures; printf("FAIL: %s\n",n); } } while(0)
typedef struct { raya_orch_registry *r; raya_orch_handle h; HANDLE entered; HANDLE release; } lease_thread_args;
static DWORD WINAPI hold_lease(LPVOID p) {
    lease_thread_args *a=(lease_thread_args *)p; raya_orch_lease lease;
    if (raya_orch_acquire(a->r,a->h,7,&lease)!=RAYA_ORCH_OK) return 10;
    SetEvent(a->entered);
    WaitForSingleObject(a->release,5000);
    if (raya_orch_release(&lease)!=RAYA_ORCH_OK) return 11;
    return 0;
}
int main(void) {
    raya_orch_registry r; int object=42, other=7; raya_orch_handle h=0,h2=0;
    raya_orch_owner_token owner=0, second=0; raya_orch_lease lease, lease_copy, lease_copy_alias;
    raya_orch_cleanup_step step; DWORD code=99; HANDLE thread,entered,release;
    lease_thread_args args; int passed=0;
    CHECK(raya_orch_registry_init(&r),"registry initializes");
    CHECK(raya_orch_register(&r,&object,7,&h)==RAYA_ORCH_OK && h!=0,"register returns nonzero generation token");
    CHECK(raya_orch_acquire(&r,h,7,&lease)==RAYA_ORCH_OK && lease.object==&object,"valid typed handle acquires lease");
    CHECK(raya_orch_acquire(&r,h,7,&lease_copy)==RAYA_ORCH_OK,"second independent lease acquired");
    lease_copy_alias=lease_copy;
    CHECK(raya_orch_release(&lease_copy)==RAYA_ORCH_OK,"first release of copied lease identity succeeds");
    CHECK(raya_orch_release(&lease_copy_alias)==RAYA_ORCH_INVALID,"duplicate copied lease release is rejected");
    CHECK(raya_orch_acquire(&r,h,8,&(raya_orch_lease){0})==RAYA_ORCH_INVALID,"wrong object kind rejected");
    CHECK(raya_orch_begin_close(&r,h,&owner)==RAYA_ORCH_OK && owner!=0,"BeginClose elects exclusive owner");
    CHECK(raya_orch_begin_close(&r,h,&second)==RAYA_ORCH_BUSY,"second cleanup owner rejected");
    CHECK(raya_orch_acquire(&r,h,7,&(raya_orch_lease){0})==RAYA_ORCH_INVALID,"new leases denied after BeginClose");
    CHECK(raya_orch_wait_drained(&r,h,owner,0)==RAYA_ORCH_TIMEOUT,"drain wait observes outstanding lease");
    CHECK(raya_orch_complete_close(&r,h,owner)==RAYA_ORCH_BUSY,"cannot reclaim while lease outstanding");
    CHECK(raya_orch_cursor_update(&r,h,owner,2,RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK,"owner records cleanup attempt start");
    CHECK(raya_orch_cursor_update(&r,h,owner,2,RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_ABNORMAL_EXIT,5,17)==RAYA_ORCH_OK,"owner records committed effect and abnormal outcome");
    CHECK(raya_orch_cursor_read(&r,h,2,&step)==RAYA_ORCH_OK && step.attempts==1 && step.last_error==5 && step.exit_code==17 && step.effect==RAYA_ORCH_EFFECT_COMMITTED,"cursor retains attempt and native result");
    CHECK(raya_orch_cursor_update(&r,h,owner+99u,3,RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_INVALID,"stale or foreign cleanup owner cannot mutate cursor");
    CHECK(raya_orch_cursor_update(&r,h,owner,3,RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK,"first retryable step attempt begins");
    CHECK(raya_orch_cursor_update(&r,h,owner,3,RAYA_ORCH_EFFECT_NOT_ATTEMPTED,RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE,ERROR_TIMEOUT,0)==RAYA_ORCH_OK,"no-effect retryable failure is persisted");
    CHECK(raya_orch_cursor_update(&r,h,owner,3,RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK,"retry begins only after explicit retryable result");
    CHECK(raya_orch_cursor_read(&r,h,3,&step)==RAYA_ORCH_OK && step.attempts==2 && step.effect==RAYA_ORCH_EFFECT_IN_PROGRESS,"retry count survives cursor transition");
    CHECK(raya_orch_cursor_update(&r,h,owner,3,RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,"retry completion commits once");
    CHECK(raya_orch_cursor_update(&r,h,owner,3,RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_INVALID,"committed step cannot be re-executed");
    CHECK(raya_orch_cursor_update(&r,h,owner,3,RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,"identical committed result is idempotent");
    CHECK(raya_orch_cursor_update(&r,h,owner,4,RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK,"uncertain step begins before owner interruption");
    CHECK(raya_orch_cursor_update(&r,h,owner,4,RAYA_ORCH_EFFECT_UNKNOWN,RAYA_ORCH_OUTCOME_UNCERTAIN,ERROR_GEN_FAILURE,0)==RAYA_ORCH_OK,"uncertain side effect is persisted");
    CHECK(raya_orch_release_cleanup_owner(&r,h,owner)==RAYA_ORCH_BUSY,"owner cannot yield while a step needs reconciliation");
    CHECK(raya_orch_cursor_reconcile(&r,h,owner,4,RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,"evidence-based reconciliation resolves committed effect");
    CHECK(raya_orch_release_cleanup_owner(&r,h,owner)==RAYA_ORCH_BUSY,"owner cannot yield while pre-close API leases remain");
    CHECK(raya_orch_release(&lease)==RAYA_ORCH_OK,"pre-close API lease releases before owner handoff");
    CHECK(raya_orch_release_cleanup_owner(&r,h,owner)==RAYA_ORCH_OK,"owner yields only after drain and reconciliation");
    CHECK(raya_orch_cursor_update(&r,h,owner,5,RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_INVALID,"yielded owner can no longer mutate cursor");
    CHECK(raya_orch_begin_close(&r,h,&second)==RAYA_ORCH_OK && second!=owner,"new cleanup owner can resume CLOSING handle");
    CHECK(raya_orch_cursor_read(&r,h,2,&step)==RAYA_ORCH_OK && step.attempts==1 && step.effect==RAYA_ORCH_EFFECT_COMMITTED,"cursor history survives cleanup owner transfer");
    CHECK(raya_orch_cursor_read(&r,h,3,&step)==RAYA_ORCH_OK && step.attempts==2 && step.effect==RAYA_ORCH_EFFECT_COMMITTED,"retry count survives cleanup owner transfer");
    CHECK(raya_orch_cursor_update(&r,h,owner,5,RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_INVALID,"previous owner is rejected after transfer");
    CHECK(raya_orch_release_cleanup_owner(&r,h,second)==RAYA_ORCH_OK,"replacement owner can explicitly yield");
    CHECK(raya_orch_begin_close(&r,h,&owner)==RAYA_ORCH_OK && owner!=second,"cleanup ownership can be reacquired after explicit yield");
    CHECK(raya_orch_release(&lease)==RAYA_ORCH_INVALID,"released lease identity cannot be released twice");
    CHECK(raya_orch_wait_drained(&r,h,owner,100)==RAYA_ORCH_OK,"drain completes after final release");
    CHECK(raya_orch_complete_close(&r,h,owner)==RAYA_ORCH_OK,"owner reclaims drained slot");
    CHECK(raya_orch_acquire(&r,h,7,&lease)==RAYA_ORCH_INVALID,"stale generation token rejected after reclaim");
    CHECK(raya_orch_register(&r,&other,7,&h2)==RAYA_ORCH_OK && h2!=h,"reused slot receives new generation token");
    entered=CreateEventW(NULL,TRUE,FALSE,NULL); release=CreateEventW(NULL,TRUE,FALSE,NULL);
    args.r=&r; args.h=h2; args.entered=entered; args.release=release;
    thread=CreateThread(NULL,0,hold_lease,&args,0,NULL);
    CHECK(thread!=NULL,"lease-holder thread created");
    if (thread!=NULL) {
        CHECK(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0,"concurrent thread acquired lease");
        CHECK(raya_orch_begin_close(&r,h2,&owner)==RAYA_ORCH_OK,"concurrent object enters closing");
        CHECK(raya_orch_wait_drained(&r,h2,owner,10)==RAYA_ORCH_TIMEOUT,"timed wait does not pass live lease");
        SetEvent(release); CHECK(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0,"lease-holder exits after release");
        GetExitCodeThread(thread,&code); CHECK(code==0,"concurrent lease released successfully");
        CHECK(raya_orch_wait_drained(&r,h2,owner,1000)==RAYA_ORCH_OK,"concurrent drain wakes owner");
        CHECK(raya_orch_complete_close(&r,h2,owner)==RAYA_ORCH_OK,"concurrent slot safely reclaimed");
        CloseHandle(thread);
    }
    if (entered) CloseHandle(entered); if (release) CloseHandle(release);
    {
        raya_orch_handle generic_handle=0;
        raya_orch_owner_token generic_owner=0;
        CHECK(raya_orch_register(&r,&other,10,&generic_handle)==RAYA_ORCH_OK &&
            raya_orch_begin_close(&r,generic_handle,&generic_owner)==RAYA_ORCH_OK,
            "generic policy slot registers and enters CLOSING");
        CHECK(raya_orch_complete_close_required(&r,generic_handle,generic_owner,10)==RAYA_ORCH_BUSY,
            "strict completion rejects a GENERIC-policy slot");
        CHECK(raya_orch_complete_close(&r,generic_handle,generic_owner)==RAYA_ORCH_OK,
            "legacy generic completion remains compatible for GENERIC slots");
    }    {
        raya_orch_handle strict_handle=0, invalid_handle=99;
        raya_orch_owner_token strict_owner=0;
        CHECK(raya_orch_register_with_policy(&r,&other,9,
            RAYA_ORCH_CLOSE_POLICY_REQUIRED_STEPS,3u,&strict_handle)==RAYA_ORCH_OK,
            "strict policy and immutable required mask register atomically");
        CHECK(raya_orch_register_with_policy(&r,&other,9,
            RAYA_ORCH_CLOSE_POLICY_REQUIRED_STEPS,1u<<RAYA_ORCH_CLEANUP_STEPS,
            &invalid_handle)==RAYA_ORCH_INVALID && invalid_handle==0,
            "strict registration rejects out-of-range required mask");
        CHECK(raya_orch_begin_close(&r,strict_handle,&strict_owner)==RAYA_ORCH_OK &&
            raya_orch_wait_drained(&r,strict_handle,strict_owner,0)==RAYA_ORCH_OK,
            "strict policy slot enters drained CLOSING state");
        CHECK(raya_orch_complete_close(&r,strict_handle,strict_owner)==RAYA_ORCH_BUSY,
            "generic completion cannot bypass strict registered policy");
        CHECK(raya_orch_complete_close_required(&r,strict_handle,strict_owner,8)==RAYA_ORCH_BUSY,
            "strict completion rejects wrong object kind");
        CHECK(raya_orch_complete_close_required(&r,strict_handle,strict_owner,9)==RAYA_ORCH_BUSY,
            "strict completion rejects missing required cursor evidence");
        CHECK(raya_orch_release_cleanup_owner(&r,strict_handle,strict_owner)==RAYA_ORCH_OK &&
            raya_orch_begin_close(&r,strict_handle,&strict_owner)==RAYA_ORCH_OK,
            "strict close policy and immutable mask survive cleanup-owner transfer");
        CHECK(raya_orch_cursor_update(&r,strict_handle,strict_owner,0,
            RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK &&
            raya_orch_cursor_update(&r,strict_handle,strict_owner,0,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,
            "first required strict step commits successfully");
        CHECK(raya_orch_cursor_update(&r,strict_handle,strict_owner,1,
            RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK &&
            raya_orch_cursor_update(&r,strict_handle,strict_owner,1,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,
            "second required strict step commits successfully");
        CHECK(raya_orch_complete_close_required(&r,strict_handle,strict_owner,9)==RAYA_ORCH_OK,
            "strict completion atomically reclaims after exact required success");
        CHECK(raya_orch_complete_close_required(&r,strict_handle,strict_owner,9)==RAYA_ORCH_BUSY,
            "strict completion rejects stale generation after reclaim");
    }    printf("SUMMARY: %d passed; %d failed\n",passed,failures);
    return failures ? 1 : 0;
}
