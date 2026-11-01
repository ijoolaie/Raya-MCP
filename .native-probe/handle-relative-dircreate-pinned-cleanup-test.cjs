"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const os=require("node:os");
const path=require("node:path");
const addon=require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");
function expectFailure(fn,pattern){let e=null;try{fn();}catch(x){e=x;}assert.ok(e,"expected native operation to fail");assert.match(e.message,pattern,e.message);return e.message;}
function main(){
 const base=fs.mkdtempSync(path.join(os.tmpdir(),"raya-dircreate-cleanup-"));
 const root=path.join(base,"root"),parent=path.join(root,"parent");fs.mkdirSync(parent,{recursive:true});
 const baseline=addon.processHandleCount();const p=addon.openParentRelative(root,"parent");const opened=addon.processHandleCount();
 let report;
 try{
  addon.failNextCloseForTest();
  const finalError=expectFailure(()=>addon.createDirectoryPathRelative(p,"final-fail"),/committed_but_cleanup_failed/);
  assert.ok(fs.statSync(path.join(parent,"final-fail")).isDirectory());
  assert.equal(addon.closeFailureCount(),1);
  addon.failNextCloseForTest();
  const partialError=expectFailure(()=>addon.createDirectoryPathRelative(p,"partial-fail\\child"),/partial_commit/);
  assert.ok(fs.statSync(path.join(parent,"partial-fail","child")).isDirectory());
  assert.equal(addon.closeFailureCount(),2);
  assert.equal(addon.processHandleCount(),opened);
  report={ok:true,finalCloseFailureReportedAfterCommit:true,intermediateCloseFailureReportedAsPartialCommit:true,parentHandleRemainedUsable:true,closeFailureCount:addon.closeFailureCount(),finalError,partialError};
 }finally{
  addon.closeParentHandle(p);
  const afterClose=addon.processHandleCount();
  assert.equal(afterClose,baseline,"parent HANDLE not released after injected failures");
  fs.rmSync(base,{recursive:true,force:true});
 }
 console.log(JSON.stringify(report,null,2));
}
try{main();}catch(e){console.error(e);process.exitCode=1;}
