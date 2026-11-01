"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const os=require("node:os");
const path=require("node:path");
const {spawn}=require("node:child_process");
const addon=require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");
const holderExe=path.join(__dirname,"delete-share-holder.exe");
async function waitForReady(child){
 let out="",err="";
 child.stdout.on("data",x=>out+=x.toString());child.stderr.on("data",x=>err+=x.toString());
 await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error("holder readiness timeout: "+out+err)),8000);child.stdout.on("data",()=>{if(out.includes("READY")){clearTimeout(timer);resolve();}});});
 return ()=>({out,err});
}
async function main(){
 const base=fs.mkdtempSync(path.join(os.tmpdir(),"raya-delete-share-"));
 const root=path.join(base,"root"),parent=path.join(root,"parent");
 fs.mkdirSync(parent,{recursive:true});
 const holder=spawn(holderExe,[parent],{cwd:__dirname,stdio:["pipe","pipe","pipe"],windowsHide:true});
 const holderClosed=new Promise(resolve=>holder.once("close",(code)=>resolve(code)));
 try{
  const output=await waitForReady(holder);
  const beforeRejectedOpen=addon.processHandleCount();
  let openError=null;
  try{addon.openParentRelative(root,"parent");}catch(e){openError=e;}
  const afterRejectedOpen=addon.processHandleCount();
  assert.equal(afterRejectedOpen,beforeRejectedOpen,"failed openParentRelative leaked a root-chain HANDLE");
  assert.ok(openError,"openParentRelative unexpectedly succeeded while a pre-existing DELETE-capable handle was open");
  assert.match(openError.message,/root component open blocked|parent component open blocked|NTSTATUS/i,openError.message);
  holder.stdin.write("\n");
  const holderCode=await holderClosed;
  assert.equal(holderCode,0,output().out+output().err);
  const parentHandle=addon.openParentRelative(root,"parent");
  assert.equal(addon.inspectParentHandle(parentHandle).isDirectory,true);
  assert.equal(addon.closeParentHandle(parentHandle),true);
  console.log(JSON.stringify({ok:true,preexistingDeleteHandleRejected:true,openSucceededAfterRelease:true,holderExitCode:holderCode,error:openError.message},null,2));
 }finally{
  if(holder.exitCode===null){holder.kill();await holderClosed;}
  fs.rmSync(base,{recursive:true,force:true});
 }
}
main().catch(e=>{console.error(e);process.exitCode=1;});
