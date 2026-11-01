"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const {spawn}=require("node:child_process");
const workerPath=path.join(__dirname,"handle-relative-dircreate-pinned-concurrent-worker.cjs");
async function main(){
 const base=fs.mkdtempSync(path.join(__dirname,"fixture-dircreate-concurrent-"));
 const root=path.join(base,"root"),parent=path.join(root,"parent"),outside=path.join(base,"outside");
 fs.mkdirSync(parent,{recursive:true});fs.mkdirSync(outside);
 const child=spawn(process.execPath,[workerPath,root],{cwd:__dirname,stdio:["pipe","pipe","pipe"],windowsHide:true});
 let stdout="",stderr="";child.stdout.on("data",x=>stdout+=x.toString());child.stderr.on("data",x=>stderr+=x.toString());
 const closed=new Promise(resolve=>child.once("close",resolve));
 const waitFor=(needle)=>new Promise((resolve,reject)=>{if(stdout.includes(needle))return resolve();const t=setTimeout(()=>reject(new Error("timeout waiting for "+needle+": "+stdout+stderr)),30000);child.stdout.on("data",()=>{if(stdout.includes(needle)){clearTimeout(t);resolve();}});});
 try{
  await waitFor("PINNED_CONCURRENT_READY");
  child.stdin.write("STRESS\n");
  const targets=[["parent",parent,path.join(outside,"parent-moved")],["root",root,path.join(outside,"root-moved")],["ancestor",base,base+"-moved"]];
  const blocked={parent:0,root:0,ancestor:0};
  // Repeated controller-process rename attempts overlap the worker's create loop.
  for(let round=0;round<3000;round++)for(const [label,source,dest] of targets){
   let err=null;try{fs.renameSync(source,dest);fs.renameSync(dest,source);}catch(e){err=e;}
   if(err){assert.ok(["EBUSY","EPERM","EACCES"].includes(err.code),label+" unexpected error "+err.code);blocked[label]++;}
   else throw new Error(label+" rename unexpectedly succeeded during pinned operation");
  }
  await waitFor("PINNED_STRESS_DONE");
  // The worker deliberately retains its parent HANDLE here, so these checks
  // cannot pass merely because the create loop already closed the owner.
  for(let round=0;round<10;round++)for(const [label,source,dest] of targets){
   let err=null;try{fs.renameSync(source,dest);fs.renameSync(dest,source);}catch(e){err=e;}
   assert.ok(err,label+" rename unexpectedly succeeded while worker HANDLE is definitely still open");
   assert.ok(["EBUSY","EPERM","EACCES"].includes(err.code),label+" unexpected error "+err.code);
   blocked[label]++;
  }
  child.stdin.write("RELEASE\n");
  let timeout;const code=await Promise.race([closed,new Promise((_,reject)=>{timeout=setTimeout(()=>reject(new Error("worker timeout: "+stdout+stderr)),30000);})]);clearTimeout(timeout);
  assert.equal(code,0,stderr+stdout);
  const report=JSON.parse(stdout.split(/\r?\n/).find(x=>x.startsWith("{")));
  assert.equal(report.ok,true,JSON.stringify(report));assert.equal(report.completed,200,JSON.stringify(report));
  assert.equal(report.afterClose,report.afterReady-1,JSON.stringify(report));assert.equal(report.closeFailures,0,JSON.stringify(report));
  for(let i=0;i<200;i++){const n=String(i).padStart(3,"0");assert.ok(fs.statSync(path.join(parent,"race-"+n,"nested","leaf")).isDirectory());}
  console.log(JSON.stringify({ok:true,concurrentAttempts:9000,postCreatePinnedAttempts:90,blocked,workerCreates:report.completed,allCreatedInsidePinnedParent:true,handleCountReturnedToBaseline:true},null,2));
 }finally{if(child.exitCode===null){child.kill();await closed;}fs.rmSync(base,{recursive:true,force:true});}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
