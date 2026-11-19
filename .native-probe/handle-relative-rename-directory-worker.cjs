const fs=require("node:fs");
const path=require("node:path");
const roots=require(path.join(__dirname,"build","Release","napi_probe_raya_phase1_rootapi.node"));
const rename=require(path.join(__dirname,"build","Release","napi_probe_raya_phase1_rename_directory.node"));
const [rootPath,srcRel,dstRel,sourceLeaf,destinationLeaf,readyPath,goPath,resultPath]=process.argv.slice(2);
let src=null,dst=null;
function waitForGo() {
  const cell=new Int32Array(new SharedArrayBuffer(4));
  const end=Date.now()+15000;
  while(!fs.existsSync(goPath)) {
    if(Date.now()>end) throw new Error("worker timed out waiting for go marker");
    Atomics.wait(cell,0,0,10);
  }
}
try {
  src=roots.openParentRelative(rootPath,srcRel);
  dst=roots.openParentRelative(rootPath,dstRel);
  fs.writeFileSync(readyPath,JSON.stringify({pid:process.pid,ready:true}));
  waitForGo();
  const result=rename.renameChildRelative(src,sourceLeaf,dst,destinationLeaf);
  roots.closeParentHandle(src);src=null;
  roots.closeParentHandle(dst);dst=null;
  fs.writeFileSync(resultPath,JSON.stringify({ok:true,pid:process.pid,committed:result.committed,sourceIsDirectory:result.sourceIsDirectory,parentsClosed:true}));
} catch(error) {
  try { if(src) roots.closeParentHandle(src); } catch {}
  try { if(dst) roots.closeParentHandle(dst); } catch {}
  fs.writeFileSync(resultPath,JSON.stringify({ok:false,error:String(error&&error.message||error)}));
  process.exitCode=1;
}
