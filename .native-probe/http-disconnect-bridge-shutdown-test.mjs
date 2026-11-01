import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
import { createServer } from "node:http";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
const require=createRequire(import.meta.url),here=path.dirname(fileURLToPath(import.meta.url));
const addon=require(path.join(here,"build","Release","napi_probe_raya_phase1_rootapi.node"));
function deferred(){let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};}
function keyOf(session,id){return String(session||"__initialization__")+"::"+JSON.stringify(id);}
function waitFor(fn,timeout=3000){return new Promise((resolve,reject)=>{const start=Date.now();const tick=()=>{if(fn())return resolve();if(Date.now()-start>timeout)return reject(new Error("condition timeout"));setTimeout(tick,5);};tick();});}
async function main(){
 const fixture=fs.mkdtempSync(path.join(os.tmpdir(),"raya-http-shutdown-")),root=path.join(fixture,"root");
 fs.mkdirSync(root);fs.writeFileSync(path.join(root,"payload.txt"),"SHUTDOWN-HANDLE");
 const registry=new Map(),enteredA=deferred(),enteredB=deferred(),enteredC=deferred(),releaseA=deferred(),releaseB=deferred(),releaseC=deferred(),signals=new Map(),owners=new Map();
 const mcp=new McpServer({name:"http-shutdown-bridge-probe",version:"1"});
 async function runPending(extra,label,release,entered){
  const ext=registry.get(keyOf(extra.sessionId,extra.requestId)),signal=ext?AbortSignal.any([extra.signal,ext.signal]):extra.signal;
  signals.set(label,signal);const handle=addon.openWorkspaceRoot(root);owners.set(label,handle);entered.resolve();
  try{await release.promise;return {content:[{type:"text",text:Buffer.from(addon.readChildFromParent(handle,"payload.txt",1000).content).toString()}]};}
  finally{addon.closeParentHandle(handle);}
 }
 mcp.tool("pending_a","pending A",extra=>runPending(extra,"A",releaseA,enteredA));
 mcp.tool("pending_b","pending B",extra=>runPending(extra,"B",releaseB,enteredB));
 mcp.tool("pending_c","pending C",extra=>runPending(extra,"C",releaseC,enteredC));
 mcp.tool("reject_probe","handler rejection probe",async()=>{throw new Error("intentional test rejection");});
 let fastSignal;
 mcp.tool("fast_probe","normal response completion probe",async extra=>{const ext=registry.get(keyOf(extra.sessionId,extra.requestId));fastSignal=ext?AbortSignal.any([extra.signal,ext.signal]):extra.signal;return {content:[{type:"text",text:"fast-ok"}]};});
 const transport=new StreamableHTTPServerTransport({sessionIdGenerator:randomUUID});await mcp.connect(transport);
 let shuttingDown=false;
 const http=createServer(async(req,res)=>{
  const local=new Map();
  try{
   const chunks=[];let bytes=0;for await(const c of req){bytes+=c.length;if(bytes>1024*1024)throw Object.assign(new Error("body too large"),{status:413});chunks.push(c);}
   let raw;try{raw=JSON.parse(Buffer.concat(chunks).toString("utf8"));}catch{throw Object.assign(new Error("malformed JSON"),{status:400});}
   if(Array.isArray(raw))throw Object.assign(new Error("batch unsupported"),{status:400});
   if(!raw||raw.jsonrpc!=="2.0"||typeof raw.method!=="string")throw Object.assign(new Error("invalid JSON-RPC"),{status:400});
   if(raw.id!==undefined){const key=keyOf(req.headers["mcp-session-id"],raw.id),controller=new AbortController();if(registry.has(key))throw Object.assign(new Error("duplicate request id"),{status:409});registry.set(key,controller);local.set(key,controller);}
   res.once("close",()=>{if(!res.writableEnded)for(const controller of local.values())controller.abort(new Error("HTTP response disconnected"));});
   await transport.handleRequest(req,res,raw);
  }catch(e){if(!res.headersSent)res.writeHead(e.status||500,{"content-type":"text/plain"});if(!res.writableEnded)res.end(e.message||"bridge error");}
  finally{for(const [key,controller] of local)if(registry.get(key)===controller)registry.delete(key);}
 });
 await new Promise(resolve=>http.listen(0,"127.0.0.1",resolve));
 const endpoint="http://127.0.0.1:"+http.address().port+"/mcp",headers={"content-type":"application/json","accept":"application/json, text/event-stream"};
 let session;
 const post=(id,method,params={})=>fetch(endpoint,{method:"POST",headers:{...headers,"mcp-session-id":session,"mcp-protocol-version":"2025-03-26"},body:JSON.stringify({jsonrpc:"2.0",id,method,params})});
 try{
  const init=await fetch(endpoint,{method:"POST",headers,body:JSON.stringify({jsonrpc:"2.0",id:1,method:"initialize",params:{protocolVersion:"2025-03-26",capabilities:{},clientInfo:{name:"probe",version:"1"}}})});
  assert.equal(init.status,200);session=init.headers.get("mcp-session-id");assert.ok(session);await init.text();
  const initialized=await fetch(endpoint,{method:"POST",headers:{...headers,"mcp-session-id":session,"mcp-protocol-version":"2025-03-26"},body:JSON.stringify({jsonrpc:"2.0",method:"notifications/initialized"})});assert.equal(initialized.status,202);
  const malformed=await fetch(endpoint,{method:"POST",headers:{...headers,"mcp-session-id":session,"mcp-protocol-version":"2025-03-26"},body:"{bad"});assert.equal(malformed.status,400);assert.equal(registry.size,0);
  const batch=await fetch(endpoint,{method:"POST",headers:{...headers,"mcp-session-id":session,"mcp-protocol-version":"2025-03-26"},body:JSON.stringify([{jsonrpc:"2.0",id:8,method:"tools/call"}])});assert.equal(batch.status,400);assert.equal(registry.size,0);
  const fast=await post(12,"tools/call",{name:"fast_probe",arguments:{}});assert.equal(fast.status,200);await fast.text();await new Promise(r=>setImmediate(r));assert.equal(fastSignal.aborted,false);assert.equal(registry.size,0);
  const rejection=await post(9,"tools/call",{name:"reject_probe",arguments:{}});assert.equal(rejection.status,200);await rejection.text();await waitFor(()=>registry.size===0);
  const reqC=post(13,"tools/call",{name:"pending_c",arguments:{}}).then(response=>({response}),error=>({error}));await enteredC.promise;await waitFor(()=>registry.size===1);
  http.closeAllConnections();await waitFor(()=>signals.get("C").aborted);releaseC.resolve();const cResult=await reqC;assert.ok(cResult.error,"socket closure should terminate the HTTP client request");await waitFor(()=>registry.size===0);assert.throws(()=>addon.inspectParentHandle(owners.get("C")),/closed|invalid/i);
  const reqA=post(10,"tools/call",{name:"pending_a",arguments:{}}),reqB=post(11,"tools/call",{name:"pending_b",arguments:{}});
  await Promise.all([enteredA.promise,enteredB.promise]);await waitFor(()=>registry.size===2);
  assert.equal(signals.get("A").aborted,false);assert.equal(signals.get("B").aborted,false);
  shuttingDown=true;await transport.close();await waitFor(()=>signals.get("A").aborted&&signals.get("B").aborted);
  releaseA.resolve();releaseB.resolve();await Promise.allSettled([reqA,reqB]);await waitFor(()=>registry.size===0);
  for(const label of ["A","B"])assert.throws(()=>addon.inspectParentHandle(owners.get(label)),/closed|invalid/i);
  process.stdout.write(JSON.stringify({ok:true,checks:15,malformedAndBatchRejected:true,handlerRejectionRegistryClean:true,normalResponseCloseSafe:true,httpSocketDisconnectAborts:true,pendingRequestsAbortedOnTransportClose:true,activeRequests:2,handlesClosed:true,registryClean:true})+"\n");
 }finally{releaseA.resolve();releaseB.resolve();releaseC.resolve();if(!shuttingDown)await transport.close().catch(()=>{});await new Promise(resolve=>http.close(resolve));await mcp.close().catch(()=>{});fs.rmSync(fixture,{recursive:true,force:true});}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
