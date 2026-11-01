import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";

const require=createRequire(import.meta.url);
const here=path.dirname(fileURLToPath(import.meta.url));
const addon=require(path.join(here, "build", "Release", "napi_probe_raya_phase1_rootapi.node"));

class ProbeTransport {
  onmessage;
  onclose;
  onerror;
  messages=[];
  async start() {}
  async send(message) { this.messages.push(message); }
  async close() { this.onclose?.(); }
  receive(message) { this.onmessage?.(message); }
}
function deferred(){let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};}
function withScope(root, callback, signal) {
  if(signal?.aborted) return Promise.reject(Object.assign(new Error("cancelled before acquire"),{name:"AbortError"}));
  const owned=[];
  return (async()=>{
    try {
      const h=addon.openWorkspaceRoot(root); owned.push(h);
      const scope=Object.freeze({rootHandle:h,read:()=>addon.readChildFromParent(h,"payload.txt",262144).content});
      const result=await callback(scope);
      if(signal?.aborted) throw Object.assign(new Error("cancelled before completion"),{name:"AbortError"});
      return result;
    } finally {
      for(let i=owned.length-1;i>=0;i--) addon.closeParentHandle(owned[i]);
    }
  })();
}
async function waitFor(predicate,label){
  for(let i=0;i<100;i++){if(predicate())return;await new Promise(r=>setImmediate(r));}
  throw new Error("Timed out waiting for "+label);
}
async function runScenario({root,id,cancelMode}) {
  const server=new McpServer({name:"cancellation-probe",version:"1.0.0"});
  const transport=new ProbeTransport();
  const entered=deferred(),release=deferred();
  let owner;
  let observedSignal;
  server.tool("scope_probe","test-only request scope probe",async extra=>{
    observedSignal=extra.signal;
    return await withScope(root,async scope=>{
      owner=scope.rootHandle;
      entered.resolve();
      await release.promise;
      return {content:[{type:"text",text:Buffer.from(scope.read()).toString("utf8")}]};
    },extra.signal);
  });
  await server.connect(transport);
  transport.receive({jsonrpc:"2.0",id,method:"tools/call",params:{name:"scope_probe",arguments:{}}});
  await entered.promise;
  assert.doesNotThrow(()=>addon.inspectParentHandle(owner),"HANDLE must remain open while handler is pending");
  if(cancelMode==="notification") {
    transport.receive({jsonrpc:"2.0",method:"notifications/cancelled",params:{requestId:id,reason:"acceptance test"}});
  } else if(cancelMode==="transport-close") {
    await server.close();
  }
  release.resolve();
  await waitFor(()=>{try{addon.inspectParentHandle(owner);return false;}catch(e){return /closed|invalid/i.test(String(e.message));}},"HANDLE cleanup");
  assert.equal(observedSignal.aborted,true,"MCP cancellation/connection close must abort the tool signal");
  return {mode:cancelMode,signalAborted:observedSignal.aborted,closed:true,responseMessages:transport.messages.length};
}
async function main(){
  const fixture=fs.mkdtempSync(path.join(os.tmpdir(),"raya-mcp-cancel-"));
  const root=path.join(fixture,"root");
  fs.mkdirSync(root);fs.writeFileSync(path.join(root,"payload.txt"),"MCP-CANCEL-PROBE");
  try {
    const protocolCancel=await runScenario({root,id:101,cancelMode:"notification"});
    const connectionClose=await runScenario({root,id:202,cancelMode:"transport-close"});
    process.stdout.write(JSON.stringify({ok:true,checks:6,protocolCancel,connectionClose,scope:"MCP SDK tool extra.signal",disconnectCaveat:"HTTP client socket disconnect is not equated with notifications/cancelled or transport.close"})+"\n");
  } finally { fs.rmSync(fixture,{recursive:true,force:true}); }
}
main().catch(e=>{console.error(e);process.exitCode=1;});