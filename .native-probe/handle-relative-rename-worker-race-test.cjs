const assert=require("node:assert/strict");
const fs=require("node:fs");
const os=require("node:os");
const path=require("node:path");
const {spawn}=require("node:child_process");

function waitForFiles(paths,children,timeoutMs=15000) {
  const cell=new Int32Array(new SharedArrayBuffer(4));
  const end=Date.now()+timeoutMs;
  while(!paths.every(p=>fs.existsSync(p))) {
    if(Date.now()>end) {
      for(const child of children) if(child.exitCode===null) child.kill();
      throw new Error("workers did not all acquire parent HANDLEs before timeout");
    }
    Atomics.wait(cell,0,0,10);
  }
}
function waitChildren(children) {
  return Promise.all(children.map(child=>new Promise((resolve,reject)=>{
    let stderr="";
    child.stderr.on("data",d=>stderr+=d.toString());
    child.once("error",reject);
    child.once("close",(code)=>code===0?resolve():reject(new Error("worker exited "+code+": "+stderr)));
  })));
}
async function main() {
  const fixture=fs.mkdtempSync(path.join(os.tmpdir(),"raya-rename-workers-"));
  const root=path.join(fixture,"root");
  const children=[],records=[];
  const count=4;
  try {
    fs.mkdirSync(root);
    for(let i=0;i<count;i++) {
      const src="src-"+i,dst="dst-"+i;
      const srcPath=path.join(root,src),dstPath=path.join(root,dst);
      const outsideSrc=path.join(fixture,"outside-src-"+i),outsideDst=path.join(fixture,"outside-dst-"+i);
      fs.mkdirSync(srcPath);fs.mkdirSync(dstPath);fs.mkdirSync(outsideSrc);fs.mkdirSync(outsideDst);
      fs.writeFileSync(path.join(srcPath,"payload.txt"),"WORKER-"+i);
      fs.writeFileSync(path.join(outsideSrc,"payload.txt"),"OUTSIDE-SOURCE-"+i);
      fs.writeFileSync(path.join(outsideDst,"sentinel.txt"),"OUTSIDE-DEST-"+i);
      const ready=path.join(fixture,"ready-"+i),go=path.join(fixture,"go-"+i),result=path.join(fixture,"result-"+i);
      const srcHeld=path.join(root,src+"-held"),dstHeld=path.join(root,dst+"-held");
      records.push({i,src,dst,outsideSrc,outsideDst,ready,go,result,srcHeld,dstHeld});
      const child=spawn(process.execPath,[path.join(__dirname,"handle-relative-rename-worker.cjs"),root,src,dst,"payload.txt","renamed.txt",ready,go,result],{stdio:["ignore","ignore","pipe"]});
      children.push(child);
    }
    waitForFiles(records.map(x=>x.ready),children);
    for(const x of records) {
      fs.renameSync(path.join(root,x.src),x.srcHeld);
      fs.symlinkSync(x.outsideSrc,path.join(root,x.src),"junction");
      fs.renameSync(path.join(root,x.dst),x.dstHeld);
      fs.symlinkSync(x.outsideDst,path.join(root,x.dst),"junction");
    }
    for(const x of records) fs.writeFileSync(x.go,"go");
    await waitChildren(children);
    for(const x of records) {
      const result=JSON.parse(fs.readFileSync(x.result,"utf8"));
      assert.equal(result.ok,true,JSON.stringify(result));
      assert.equal(result.committed,true);
      assert.equal(result.parentsClosed,true);
      assert.equal(fs.readFileSync(path.join(x.dstHeld,"renamed.txt"),"utf8"),"WORKER-"+x.i);
      assert.equal(fs.existsSync(path.join(x.srcHeld,"payload.txt")),false);
      assert.equal(fs.readFileSync(path.join(x.outsideSrc,"payload.txt"),"utf8"),"OUTSIDE-SOURCE-"+x.i);
      assert.equal(fs.readFileSync(path.join(x.outsideDst,"sentinel.txt"),"utf8"),"OUTSIDE-DEST-"+x.i);
      assert.equal(fs.existsSync(path.join(x.outsideDst,"renamed.txt")),false);
    }
    process.stdout.write(JSON.stringify({ok:true,workers:count,checks:count*6,assertions:["all workers acquired source/destination HANDLEs before swaps","both source and destination paths replaced by outside junctions","each rename committed into the original held destination directory","no operation escaped into outside source or destination","all workers closed both parent HANDLE owners"]})+"\n");
  } finally {
    for(const child of children) if(child.exitCode===null) child.kill();
    fs.rmSync(fixture,{recursive:true,force:true});
  }
}
main().catch(e=>{console.error(e);process.exitCode=1;});
