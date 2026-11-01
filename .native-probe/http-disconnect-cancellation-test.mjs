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
function withScope(root,callback,signal){
 const owned=[];
 return (async()=>{
  try{
   if(signal?.aborted) throw Object.assign(new Error("aborted before acquisition"),{name:"AbortError"});
   const h=addon.openWorkspaceRoot(root);owned.push(h);
   const result=await callback({rootHandle:h,read:()=>addon.readChildFromParent(h,"payload.txt",262144).content});
   if(signal?.aborted) throw Object.assign(new Error("aborted at completion"),{name:"AbortError"});
   return result;
  }finally{for(let i=owned.length-1;i>=0;i--)addon.closeParentHandle(owned[i]);}
 })();
}
async function main(){
 const fixture=fs.mkdtempSync(path.join(os.tmpdir(),"raya-http-disconnect-"));
 const root=path.join(fixture,"root");fs.mkdirSync(root);fs.writeFileSync(path.join(root,"payload.txt"),"HTTP-DISCONNECT-PROBE");
 const entered=deferred(),release=deferred();let owner,signal;
 const mcp=new McpServer({name:"http-disconnect-probe",version:"1.0.0"});
 mcp.tool("disconnect_probe","test-only HTTP disconnect probe",async extra=>{
  signal=extra.signal;
  return await withScope(root,async scope=>{
   owner=scope.rootHandle;entered.resolve();await release.promise;
   return {content:[{type:"text",text:Buffer.from(scope.read()).toString("utf8")}]};
  },extra.signal);
 });
 const transport=new StreamableHTTPServerTransport({sessionIdGenerator:randomUUID});
 await mcp.connect(transport);
 const http=createServer(async(req,res)=>{
  try{await transport.handleRequest(req,res);}
  catch(e){if(!res.headersSent)res.writeHead(500);if(!res.writableEnded)res.end("probe error");}
 });
 await new Promise(resolve=>http.listen(0,"127.0.0.1",resolve));
 const address=http.address();const endpoint="http://127.0.0.1:"+address.port+"/mcp";
 const headers={"content-type":"application/json","accept":"application/json, text/event-stream"};
 try{
  const init=await fetch(endpoint,{method:"POST",headers,body:JSON.stringify({jsonrpc:"2.0",id:1,method:"initialize",params:{protocolVersion:"2025-03-26",capabilities:{},clientInfo:{name:"probe",version:"1"}}})});
  assert.equal(init.status,200);
  const session=init.headers.get("mcp-session-id");assert.ok(session);
  await init.text();
  const common={...headers,"mcp-session-id":session,"mcp-protocol-version":"2025-03-26"};
  const initialized=await fetch(endpoint,{method:"POST",headers:common,body:JSON.stringify({jsonrpc:"2.0",method:"notifications/initialized"})});
  assert.equal(initialized.status,202);
  const pending=await fetch(endpoint,{method:"POST",headers:common,body:JSON.stringify({jsonrpc:"2.0",id:2,method:"tools/call",params:{name:"disconnect_probe",arguments:{}}})});
  assert.equal(pending.status,200);
  await entered.promise;
  assert.equal(signal.aborted,false);
  assert.doesNotThrow(()=>addon.inspectParentHandle(owner));
  await pending.body.cancel();
  await new Promise(resolve=>setTimeout(resolve,80));
  const stillNotAborted=signal.aborted===false;
  assert.equal(stillNotAborted,true,"closing the HTTP response stream must not be assumed to abort MCP handler signal");
  release.resolve();
  for(let i=0;i<100;i++){
   try{addon.inspectParentHandle(owner);await new Promise(r=>setImmediate(r));}
   catch(e){if(/closed|invalid/i.test(String(e.message)))break;throw e;}
  }
  assert.throws(()=>addon.inspectParentHandle(owner),/closed|invalid/i);
  process.stdout.write(JSON.stringify({ok:true,checks:6,httpStatus:pending.status,clientResponseStreamCancelled:true,handlerSignalAbortedOnDisconnect:signal.aborted,handleClosedAfterHandlerSettled:true,conclusion:"HTTP response disconnect is not MCP request cancellation"})+"\n");
 }finally{
  release.resolve();
  await new Promise(resolve=>http.close(resolve));
  await mcp.close();
  fs.rmSync(fixture,{recursive:true,force:true});
 }
}
main().catch(e=>{console.error(e);process.exitCode=1;});
