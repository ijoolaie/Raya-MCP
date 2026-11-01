"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const os=require("node:os");
const path=require("node:path");
const {spawnSync}=require("node:child_process");
const addon=require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");
function run(exe,args){const r=spawnSync(exe,args,{encoding:"utf8",windowsHide:true});if(r.error)throw r.error;if(r.status!==0)throw new Error(exe+" "+args.join(" ")+" failed ("+r.status+"): "+r.stdout+" "+r.stderr);return r.stdout.trim();}
function currentSid(){const row=run("whoami.exe",["/user","/fo","csv","/nh"]);const m=row.match(/"([^"]+)"\s*$/);if(!m)throw new Error("could not parse current user SID from whoami: "+row);return m[1];}
function acl(pathname,verb,sid){return run("icacls.exe",verb==="/deny"?[pathname,verb,"*"+sid+":(AD)"]:[pathname,verb,"*"+sid]);}
function main(){
 const base=fs.mkdtempSync(path.join(__dirname,"fixture-dircreate-acl-"));
 const root=path.join(base,"root"),parentPath=path.join(root,"parent");
 fs.mkdirSync(parentPath,{recursive:true});
 const sid=currentSid();let parent=null,denyApplied=false,restored=false;
 const baseline=addon.processHandleCount();
 try{
  parent=addon.openParentRelative(root,"parent");
  assert.ok(parent);
  const beforeDeny=addon.processHandleCount();
  acl(parentPath,"/deny",sid);denyApplied=true;
  let failure=null;
  try{addon.createDirectoryPathRelative(parent,"denied-child\\nested");}catch(e){failure=e;}
  assert.ok(failure,"creation unexpectedly succeeded despite explicit current-user AD deny ACE");
  assert.match(String(failure.message||failure),/0xC0000022|0xC0000061|0xC000000D|0xC000003A/i,"expected access-related NTSTATUS, got "+failure);
  assert.equal(fs.existsSync(path.join(parentPath,"denied-child")),false,"denied create left a directory behind");
  const afterDenied=addon.processHandleCount();
  assert.equal(afterDenied,beforeDeny,"failed ACL operation leaked a native HANDLE");
  acl(parentPath,"/remove:d",sid);denyApplied=false;restored=true;
  const success=addon.createDirectoryPathRelative(parent,"allowed-child\\nested");
  assert.equal(success.committed,true);
  assert.equal(success.createdCount,2);
  assert.ok(fs.statSync(path.join(parentPath,"allowed-child","nested")).isDirectory());
  addon.closeParentHandle(parent);parent=null;
  assert.equal(addon.processHandleCount(),baseline,"HANDLE count did not return to baseline");
  console.log(JSON.stringify({ok:true,userSid:sid,explicitDenyRight:"AD (add subdirectory)",deniedCreateFailedClosed:true,noPartialDirectoryCreated:true,noHandleLeakOnDeniedCreate:true,aclRestored:true,createSucceededAfterAclRestoration:true,handleCountReturnedToBaseline:true},null,2));
 }finally{
  if(denyApplied){try{acl(parentPath,"/remove:d",sid);restored=true;}catch(e){console.error("CRITICAL: failed to remove temporary deny ACE:",e.message);}}
  if(parent){try{addon.closeParentHandle(parent);}catch(e){console.error("close parent failed:",e.message);}}
  if(!denyApplied&&fs.existsSync(base))fs.rmSync(base,{recursive:true,force:true});
  if(denyApplied)console.error("Fixture retained for manual ACL recovery:",base);
 }
}
try{main();}catch(e){console.error(e.stack||e);process.exitCode=1;}
