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

const require=createRequire(import.meta.url);
const here=path.dirname(fileURLToPath(import.meta.url));
const addon=require(path.join(here,"build","Release","napi_probe_raya_phase1_rootapi.node"));
function deferred(){let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};}
async function readBody(req){
 const chunks=[];let total=0;
 for await(const chunk of req){total+=chunk.length;if(total>1024*1024)throw new Error("test body too large");chunks.push(chunk);}
 return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}
function withScope(root,callback,signal){
 const owned=[];
 return (async()=>{
  try{
   if(signal?.aborted)throw Object.assign(new Error("aborted before acquisition"),{name:"AbortError"});
   const h=addon.openWorkspaceRoot(root);owned.push(h);
   const result=await callback({rootHandle:h,read:()=>addon.readChildFromParent(h,"payload.txt",262144).content});
   if(signal?.aborted)throw Object.assign(new Error("aborted at completion"),{name:"AbortError"});
   return result;
  }finally{for(let i=owned.length-1;i>=0;i--)addon.closeParentHandle(owned[i]);}
 })();
}
async function main(){
 const fixture=fs.mkdtempSync(path.join(os.tmpdir(),"raya-http-abort-bridge-"));
 const root=path.join(fixture,"root");fs.mkdirSync(root);fs.writeFileSync(path.join(root,"payload.txt"),"HTTP-ABORT-BRIDGE");
 const entered=deferred(),release=deferred();
 const disconnectControllers=new Map();let owner,signal;
 const mcp=new McpServer({name:"http-abort-bridge-probe",version:"1.0.0"});
 mcp.tool("bridge_probe","test-only HTTP disconnect bridge probe",async extra=>{
  const externalKey=String(extra.sessionId)+"::"+JSON.stringify(extra.requestId);
  const external=disconnectControllers.get(externalKey)?.signal;
  signal=external?AbortSignal.any([extra.signal,external]):extra.signal;
  return await withScope(root,async scope=>{
   owner=scope.rootHandle;entered.resolve();await release.promise;
   return {content:[{type:"text",text:Buffer.from(scope.read()).toString("utf8")}]};
  },signal);
 });
 const transport=new StreamableHTTPServerTransport({sessionIdGenerator:randomUUID});
 await mcp.connect(transport);
 const http=createServer(async(req,res)=>{
  let raw;const keys=[];const local=new Map();
  try{
   raw=await readBody(req);
   const messages=Array.isArray(raw)?raw:[raw];
   for(const msg of messages){
    if(msg&&msg.jsonrpc==="2.0"&&msg.id!==undefined&&msg.method){
     const key=String(req.headers["mcp-session-id"]||"__initialization__")+"::"+JSON.stringify(msg.id);
     const controller=new AbortController();keys.push(key);local.set(key,controller);disconnectControllers.set(key,controller);
    }
   }
   res.once("close",()=>{
    if(!res.writableEnded){
     for(const controller of local.values())controller.abort(new Error("HTTP response disconnected"));
    }
   });
   await transport.handleRequest(req,res,raw);
  }catch(e){if(!res.headersSent)res.writeHead(500);if(!res.writableEnded)res.end("probe error");}
  finally{
   for(const key of keys)if(disconnectControllers.get(key)===local.get(key))disconnectControllers.delete(key);
  }
 });
 await new Promise(resolve=>http.listen(0,"127.0.0.1",resolve));
 const endpoint="http://127.0.0.1:"+http.address().port+"/mcp";
 const headers={"content-type":"application/json","accept":"application/json, text/event-stream"};
 try{
  const init=await fetch(endpoint,{method:"POST",headers,body:JSON.stringify({jsonrpc:"2.0",id:1,method:"initialize",params:{protocolVersion:"2025-03-26",capabilities:{},clientInfo:{name:"probe",version:"1"}}})});
  assert.equal(init.status,200);const session=init.headers.get("mcp-session-id");assert.ok(session);await init.text();
  const common={...headers,"mcp-session-id":session,"mcp-protocol-version":"2025-03-26"};
  const initialized=await fetch(endpoint,{method:"POST",headers:common,body:JSON.stringify({jsonrpc:"2.0",method:"notifications/initialized"})});
  assert.equal(initialized.status,202);
  const pending=await fetch(endpoint,{method:"POST",headers:common,body:JSON.stringify({jsonrpc:"2.0",id:2,method:"tools/call",params:{name:"bridge_probe",arguments:{}}})});
  assert.equal(pending.status,200);await entered.promise;
  assert.equal(signal.aborted,false);assert.doesNotThrow(()=>addon.inspectParentHandle(owner));
  await pending.body.cancel();
  for(let i=0;i<100&&!signal.aborted;i++)await new Promise(r=>setTimeout(r,5));
  assert.equal(signal.aborted,true,"request-scoped combined signal must abort on HTTP response disconnect");
  release.resolve();
  for(let i=0;i<100;i++){try{addon.inspectParentHandle(owner);await new Promise(r=>setImmediate(r));}catch(e){if(/closed|invalid/i.test(String(e.message)))break;throw e;}}
  assert.throws(()=>addon.inspectParentHandle(owner),/closed|invalid/i);
  for(let i=0;i<100&&disconnectControllers.size;i++)await new Promise(r=>setImmediate(r));
  assert.equal(disconnectControllers.size,0,"request ID controller registry must be cleaned");
  process.stdout.write(JSON.stringify({ok:true,checks:8,clientResponseStreamCancelled:true,combinedSignalAborted:signal.aborted,handleClosed:true,requestRegistryCleaned:disconnectControllers.size===0,scope:"test-only requestId-to-disconnect-controller bridge"})+"\n");
 }finally{
  release.resolve();await new Promise(resolve=>http.close(resolve));await mcp.close();fs.rmSync(fixture,{recursive:true,force:true});
 }
}
main().catch(e=>{console.error(e);process.exitCode=1;});