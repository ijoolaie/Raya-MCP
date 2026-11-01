const fs=require("node:fs");
const path=require("node:path");
const roots=require(path.join(__dirname,"build","Release","napi_probe_raya_phase1_rootapi.node"));
const publish=require(path.join(__dirname,"build","Release","napi_probe_raya_phase1_publish.node"));
const [rootPath,tempLeaf,destinationLeaf,readyPath,goPath,resultPath,payloadText]=process.argv.slice(2);
let parent=null;
const baseline=roots.processHandleCount();
function waitForGo(){
  const cell=new Int32Array(new SharedArrayBuffer(4));
  const end=Date.now()+15000;
  while(!fs.existsSync(goPath)){
    if(Date.now()>end)throw new Error("worker timed out waiting for go marker");
    Atomics.wait(cell,0,0,10);
  }
}
try{
  parent=roots.openWorkspaceRoot(rootPath);
  fs.writeFileSync(readyPath,JSON.stringify({pid:process.pid,ready:true}));
  waitForGo();
  const result=publish.publishChildExclusiveRelative(parent,tempLeaf,destinationLeaf,Buffer.from(payloadText,"utf8"));
  roots.closeParentHandle(parent);parent=null;
  const after=roots.processHandleCount();
  fs.writeFileSync(resultPath,JSON.stringify({ok:true,pid:process.pid,committed:result.committed,bytesWritten:result.bytesWritten,handlesReturnedToBaseline:after===baseline,handleBaseline:baseline,handleAfter:after}));
}catch(error){
  try{if(parent){roots.closeParentHandle(parent);parent=null;}}catch{}
  const after=roots.processHandleCount();
  fs.writeFileSync(resultPath,JSON.stringify({ok:false,pid:process.pid,error:String(error&&error.message||error),handlesReturnedToBaseline:after===baseline,handleBaseline:baseline,handleAfter:after}));
  if(after!==baseline)process.exitCode=2;
}