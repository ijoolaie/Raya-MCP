const {spawnSync}=require("node:child_process");
const path=require("node:path");
const rounds=5;
const results=[];
for(let round=1;round<=rounds;round++) {
  const r=spawnSync(process.execPath,[path.join(__dirname,"handle-relative-rename-worker-race-test.cjs")],{encoding:"utf8",timeout:60000});
  if(r.error) throw r.error;
  if(r.status!==0) {
    process.stderr.write(r.stderr||"");
    throw new Error("worker race round "+round+" failed with status "+r.status);
  }
  const line=(r.stdout||"").trim().split(/\r?\n/).filter(Boolean).at(-1);
  const data=JSON.parse(line);
  if(!data.ok || data.workers!==4 || data.checks!==24) throw new Error("unexpected worker result in round "+round);
  results.push({round,workers:data.workers,assertions:data.checks});
}
process.stdout.write(JSON.stringify({ok:true,rounds,workers:results.reduce((n,x)=>n+x.workers,0),assertions:results.reduce((n,x)=>n+x.assertions,0),results})+"\n");
