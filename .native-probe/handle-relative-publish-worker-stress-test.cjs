const assert=require("node:assert/strict");
const fs=require("node:fs");
const os=require("node:os");
const path=require("node:path");
const {spawn}=require("node:child_process");

const worker=path.join(__dirname,"handle-relative-publish-worker.cjs");
const rounds=5,workersPerRound=4;
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function waitFor(predicate,label,timeoutMs=15000){
 const end=Date.now()+timeoutMs;
 while(Date.now()<end){if(predicate())return;await sleep(20);}
 throw new Error("timeout waiting for "+label);
}
async function runRound(round){
 const fixture=fs.mkdtempSync(path.join(os.tmpdir(),"raya-publish-race-"));
 const root=path.join(fixture,"root"),goPath=path.join(fixture,"GO");
 fs.mkdirSync(root);
 const children=[],ready=[],reports=[],checks=[];
 try{
  for(let i=0;i<workersPerRound;i++){
   const readyPath=path.join(fixture,"ready-"+i+".json");
   const resultPath=path.join(fixture,"result-"+i+".json");
   const tempLeaf=".publish-temp-"+round+"-"+i;
   const payload="ROUND-"+round+"-WORKER-"+i+"-WINNER";
   ready.push(readyPath);reports.push(resultPath);
   const child=spawn(process.execPath,[worker,root,tempLeaf,"shared-destination.bin",readyPath,goPath,resultPath,payload],{stdio:"ignore",windowsHide:true});
   children.push({child,tempLeaf,payload,resultPath});
  }
  await waitFor(()=>ready.every(p=>fs.existsSync(p)),"all workers to acquire parent HANDLE");
  checks.push("all four workers acquired independent parent HANDLEs before release");
  fs.writeFileSync(goPath,"go");
  const exits=await Promise.all(children.map(({child})=>new Promise((resolve,reject)=>{
   const timer=setTimeout(()=>{child.kill();reject(new Error("worker process timeout"));},20000);
   child.once("error",error=>{clearTimeout(timer);reject(error);});
   child.once("exit",(code,signal)=>{clearTimeout(timer);resolve({code,signal});});
  })));
  assert.ok(exits.every(x=>x.code===0),"all worker processes must report clean HANDLE lifecycle");
  await waitFor(()=>reports.every(p=>fs.existsSync(p)),"all worker result reports");
  const results=reports.map(p=>JSON.parse(fs.readFileSync(p,"utf8")));
  const winners=results.filter(r=>r.ok&&r.committed);
  const losers=results.filter(r=>!r.ok&&/publish_failed_without_overwrite/.test(r.error));
  assert.equal(winners.length,1,"exactly one worker must atomically publish the shared destination");
  assert.equal(losers.length,workersPerRound-1,"all other workers must receive no-overwrite collision");
  assert.ok(results.every(r=>r.handlesReturnedToBaseline),"every worker must release its parent HANDLE");
  const winnerIndex=results.findIndex(r=>r.ok);
  assert.equal(fs.readFileSync(path.join(root,"shared-destination.bin"),"utf8"),children[winnerIndex].payload);
  assert.ok(children.every(c=>!fs.existsSync(path.join(root,c.tempLeaf))),"all staged temp leaves must be gone");
  checks.push("exactly one publisher wins and three collisions preserve the winning bytes");
  checks.push("all worker HANDLE counts return to per-process baseline");
  checks.push("all losing temporary siblings are cleaned and no temp names remain");
  return {round,workers:workersPerRound,assertions:checks.length,checks};
 }finally{
  for(const entry of children){if(entry.child.exitCode===null)entry.child.kill();}
  fs.rmSync(fixture,{recursive:true,force:true});
 }
}
async function main(){
 const results=[];
 for(let round=1;round<=rounds;round++)results.push(await runRound(round));
 const assertions=results.reduce((n,r)=>n+r.assertions,0);
 process.stdout.write(JSON.stringify({ok:true,rounds,workers:rounds*workersPerRound,assertions,results})+"\n");
}
main().catch(error=>{console.error(error);process.exitCode=1;});