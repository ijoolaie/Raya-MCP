"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");
const addon = require("./build/Release/napi_probe_raya_phase1_tail.node");
const workerPath = path.join(__dirname, "handle-relative-read-family-worker.cjs");
async function runWorker(root, command, beforeRelease) {
  const child = spawn(process.execPath, [workerPath, root], { cwd: __dirname, stdio: ["pipe", "pipe", "pipe"], windowsHide: true });
  let stdout="", stderr="", signaled=false, closed=false;
  child.stdout.on("data", x => stdout += x.toString());
  child.stderr.on("data", x => stderr += x.toString());
  const close = new Promise(resolve => child.once("close", code => {closed=true;resolve(code);}));
  async function reap(){ if(closed)return; child.kill(); const ok=await Promise.race([close.then(()=>true),new Promise(r=>setTimeout(()=>r(false),5000))]); if(!ok)throw new Error("worker not reaped"); }
  try {
    await new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{child.kill();close.then(()=>reject(new Error("worker signal timeout")));},8000);
      const check=()=>{if(stdout.includes("PARENT_HANDLE_READY")){signaled=true;clearTimeout(timer);resolve();}};
      child.stdout.on("data",check); child.once("error",e=>{clearTimeout(timer);reject(e);});
      close.then(code=>{if(!signaled){clearTimeout(timer);reject(new Error("worker exited early "+code+" "+stdout+stderr));}});check();
    });
    if(beforeRelease)beforeRelease();
    child.stdin.write(command+"\n");
    const code=await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(new Error("worker timeout "+stdout+stderr)),10000);close.then(c=>{clearTimeout(t);resolve(c);});});
    return {code,stdout,stderr};
  } catch(e){await reap();throw e;}
}
function report(run,label){
  assert.equal(run.code,0,label+" exit: "+run.stderr+run.stdout);
  const line=run.stdout.split(/\r?\n/).find(x=>x.startsWith("{"));assert.ok(line,label+" no JSON");
  const x=JSON.parse(line);assert.equal(x.ok,true,label+" failed "+JSON.stringify(x));
  assert.equal(x.opened,x.baseline+1,label+" parent handle count");
  assert.equal(x.afterOperation,x.preOperation,label+" leaked operation HANDLE: "+JSON.stringify(x));
  assert.equal(x.afterClose,x.preOperation-1,label+" leaked parent HANDLE: "+JSON.stringify(x));return x;
}
async function main(){
 const base=fs.mkdtempSync(path.join(__dirname,"fixture-read-family-"));
 const root=path.join(base,"root"), parent=path.join(root,"parent"), outside=path.join(base,"outside");
 fs.mkdirSync(parent,{recursive:true});fs.mkdirSync(outside);
 fs.writeFileSync(path.join(parent,"sentinel.txt"),"ROOT-CANARY");
 fs.writeFileSync(path.join(outside,"tail.txt"),"OUTSIDE-SECRET");
 const outcomes=[];
 try {
   const tail=report(await runWorker(root,"TAIL",()=>fs.writeFileSync(path.join(parent,"tail.txt"),"0123456789")),"tail inserted after parent open");
   assert.equal(tail.result.content,"56789");assert.equal(tail.result.size,10);assert.equal(tail.result.offset,5);assert.equal(tail.result.bytesRead,5);
   outcomes.push("tail read after synchronized in-root leaf insertion returned exact final five bytes, total size, and offset");
   fs.rmSync(path.join(parent,"tail.txt"));
   const blocked=report(await runWorker(root,"TAIL-BLOCK",()=>fs.symlinkSync(outside,path.join(parent,"tail.txt"),"junction")),"tail leaf junction");
   assert.equal(blocked.result.blocked,true);assert.equal(fs.readFileSync(path.join(outside,"tail.txt"),"utf8"),"OUTSIDE-SECRET");
   outcomes.push("tail read rejected synchronized outside-root leaf junction without exposing outside bytes");
   fs.rmSync(path.join(parent,"tail.txt"));
   const multi=report(await runWorker(root,"MULTI",()=>{
     fs.writeFileSync(path.join(parent,"one.txt"),"ONE");
     fs.writeFileSync(path.join(parent,"two.txt"),"TWO-CONTENT");
   }),"multi-read");
   assert.deepEqual(multi.result.map(x=>x.ok),[true,false,true]);
   assert.equal(multi.result[0].content,"ONE");assert.equal(multi.result[1].name,"missing.txt");assert.equal(multi.result[2].content,"TWO-CONTENT");
   outcomes.push("multi-read wrapper preserved per-file success/error semantics across two in-root files and one missing leaf");
   const multiJunction=report(await runWorker(root,"MULTI-JUNCTION",()=>fs.symlinkSync(outside,path.join(parent,"escape.txt"),"junction")),"multi-read junction");
   assert.deepEqual(multiJunction.result.map(x=>x.ok),[true,false,true]);
   assert.match(multiJunction.result[1].error,/child open relative|reparse|disallowed/i);
   assert.equal(fs.readFileSync(path.join(outside,"tail.txt"),"utf8"),"OUTSIDE-SECRET");
   outcomes.push("multi-read wrapper isolated a junction error to that item and returned no outside data");
   console.log(JSON.stringify({ok:true,outcomes},null,2));
 } finally {
   try {const x=fs.lstatSync(path.join(parent,"tail.txt"));if(x.isSymbolicLink())fs.rmSync(path.join(parent,"tail.txt"));}catch(e){if(e.code!=="ENOENT")throw e;}
   try {const x=fs.lstatSync(path.join(parent,"escape.txt"));if(x.isSymbolicLink())fs.rmSync(path.join(parent,"escape.txt"));}catch(e){if(e.code!=="ENOENT")throw e;}
   fs.rmSync(base,{recursive:true,force:true});
 }
}
main().catch(e=>{console.error(e);process.exitCode=1;});