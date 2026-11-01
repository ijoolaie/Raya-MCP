"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const {spawn}=require("node:child_process");
const workerPath=path.join(__dirname,"handle-relative-dircreate-pinned-worker.cjs");
async function main(){
 const base=fs.mkdtempSync(path.join(__dirname,"fixture-dircreate-pinned-"));
 const root=path.join(base,"root"),parent=path.join(root,"parent"),outside=path.join(base,"outside");
 fs.mkdirSync(parent,{recursive:true});fs.mkdirSync(outside);
 fs.writeFileSync(path.join(parent,"sentinel.txt"),"INSIDE");
 const child=spawn(process.execPath,[workerPath,root],{cwd:__dirname,stdio:["pipe","pipe","pipe"],windowsHide:true});
 let stdout="",stderr="";child.stdout.on("data",x=>stdout+=x.toString());child.stderr.on("data",x=>stderr+=x.toString());
 const closed=new Promise(resolve=>child.once("close",resolve));
 try{
  await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(new Error("pinned worker readiness timeout: "+stdout+stderr)),8000);child.stdout.on("data",()=>{if(stdout.includes("PINNED_PARENT_READY")){clearTimeout(t);resolve();}});});
  const attempts=[["parent",parent,path.join(outside,"parent-moved")],["configured root",root,path.join(outside,"root-moved")],["ancestor",base,base+"-moved"]];
  const blocked=[];
  for(const item of attempts){for(let attempt=0;attempt<10;attempt++){let err=null;try{fs.renameSync(item[1],item[2]);}catch(e){err=e;}assert.ok(err,item[0]+" rename unexpectedly succeeded while namespace handles were pinned (attempt "+attempt+")");blocked.push({label:item[0],attempt,code:err.code});}}
  child.stdin.write("PATH\n");
  let workerTimeout;const code=await Promise.race([closed,new Promise((_,reject)=>{workerTimeout=setTimeout(()=>reject(new Error("pinned worker timeout: "+stdout+stderr)),10000);})]);clearTimeout(workerTimeout);
  assert.equal(code,0,stderr+stdout);
  const report=JSON.parse(stdout.split(/\r?\n/).find(x=>x.startsWith("{")));
  assert.equal(report.ok,true,JSON.stringify(report));assert.deepEqual(report.result,{createdCount:3,existingCount:0,committed:true});
  assert.equal(report.opened,report.baseline+1,JSON.stringify(report));
  // Writing the readiness line lazily activates Node's stdout pipe HANDLE.
  // The extra HANDLE is already present before the native create call.
  assert.equal(report.afterReady,report.opened+1,JSON.stringify(report));
  assert.equal(report.beforeWait,report.afterReady,JSON.stringify(report));
  assert.equal(report.afterWait,report.afterReady,JSON.stringify(report));
  assert.equal(report.afterCreate,report.afterReady,JSON.stringify(report));
  assert.equal(report.afterClose,report.opened,JSON.stringify(report));
  assert.equal(report.closeFailures,0,JSON.stringify(report));
  assert.ok(fs.statSync(path.join(parent,"recursive-a","recursive-b","recursive-c")).isDirectory());
  assert.equal(fs.readFileSync(path.join(parent,"sentinel.txt"),"utf8"),"INSIDE");
  fs.renameSync(parent,path.join(outside,"parent-after-release"));fs.renameSync(path.join(outside,"parent-after-release"),parent);
  console.log(JSON.stringify({ok:true,blocked,createdInsidePinnedParent:true,handleCountStableAcrossCreate:true,parentHandleReleased:true,renameAllowedAfterRelease:true,handleCounts:{baseline:report.baseline,opened:report.opened,afterReady:report.afterReady,afterCreate:report.afterCreate,afterClose:report.afterClose}},null,2));
 }finally{if(child.exitCode===null){child.kill();await closed;}fs.rmSync(base,{recursive:true,force:true});}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
