"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs"),path=require("node:path");
const addon=require("./build/Release/napi_probe_raya_phase1_dirwalk.node");
function openTree(root){const p=addon.openParentRelative(root,"parent");let s,i;try{s=addon.openDirectoryChild(p,"sub");i=addon.openDirectoryChild(s,"inner");return {p,s,i};}catch(e){if(i)addon.closeParentHandle(i);if(s)addon.closeParentHandle(s);addon.closeParentHandle(p);throw e;}}
function closeTree(t){if(t.i)addon.closeParentHandle(t.i);if(t.s)addon.closeParentHandle(t.s);if(t.p)addon.closeParentHandle(t.p);}
function walkTree(dir, prefix, depth, output, hits, budget){
 if(depth>8)throw Error("depth cap exceeded");
 let page=addon.enumerateDirectoryPage(dir,1024);
 while(true){
  for(const entry of page.entries){
   if(++budget.count>5000)throw Error("entry cap exceeded");
   const meta=addon.inspectEntryMetadata(dir,entry.name);
   const rel=prefix?prefix+"/"+entry.name:entry.name;
   output.push({path:rel,isDirectory:meta.isDirectory,isReparsePoint:meta.isReparsePoint,size:meta.size});
   if(meta.isDirectory&&!meta.isReparsePoint){
    const child=addon.openDirectoryChild(dir,entry.name);
    try{walkTree(child,rel,depth+1,output,hits,budget);}finally{addon.closeParentHandle(child);}
   }else if(!meta.isDirectory&&!meta.isReparsePoint&&Number(meta.size)<=1048576){
    const read=addon.readChildFromParent(dir,entry.name,1048576);
    if(read.content.toString("utf8").includes("NEEDLE"))hits.push(rel);
   }
  }
  if(page.done)break;
  page=addon.enumerateDirectoryPage(dir,1024);
 }
}
function createSearchSessions({ttlMs,maxSessions,now=()=>Date.now()}){
 const sessions=new Map();let sequence=0;
 function prune(){for(const [id,s] of sessions)if(now()-s.createdAt>=ttlMs)sessions.delete(id);}
 function create(results){prune();while(sessions.size>=maxSessions){let oldest=null;for(const [id,s] of sessions)if(!oldest||s.createdAt<oldest.createdAt)oldest={id,...s};if(!oldest)break;sessions.delete(oldest.id);}
  const id=String(++sequence)+"-"+String(now());const s={id,createdAt:now(),results:results.slice(),next:0};sessions.set(id,s);return id;}
 function page(id,size){prune();const s=sessions.get(id);if(!s)throw Error("Unknown or expired search session.");const results=s.results.slice(s.next,s.next+size);s.next+=results.length;return {results,nextOffset:s.next,nextPageAvailable:s.next<s.results.length,totalFound:s.results.length};}
 function list(){prune();return [...sessions.values()].map(s=>({id:s.id,createdAt:s.createdAt,nextOffset:s.next,totalFound:s.results.length}));}
 function stop(id){return sessions.delete(id);}
 return {create,page,list,stop,prune};
}
function walkTreeResilient(dir,prefix,depth,state,limits){
 if(depth>limits.maxDepth){state.errors.push({path:prefix,error:"depth limit"});state.truncated=true;return;}
 let page;
 try{page=addon.enumerateDirectoryPage(dir,1024);}
 catch(e){state.errors.push({path:prefix,error:String(e.message||e)});return;}
 while(true){
  for(const entry of page.entries){
   if(limits.shouldCancel&&limits.shouldCancel()){state.cancelled=true;return;}
   if(state.count>=limits.maxEntries){state.truncated=true;return;}
   state.count++;
   const rel=prefix?prefix+"/"+entry.name:entry.name;
   if(limits.beforeEntry)limits.beforeEntry(rel);
   let meta;
   try{meta=addon.inspectEntryMetadata(dir,entry.name);}
   catch(e){state.errors.push({path:rel,error:String(e.message||e)});continue;}
   state.entries.push({path:rel,isDirectory:meta.isDirectory,isReparsePoint:meta.isReparsePoint,size:meta.size});
   if(meta.isDirectory&&!meta.isReparsePoint){
    let child;
    try{child=addon.openDirectoryChild(dir,entry.name);}
    catch(e){state.errors.push({path:rel,error:String(e.message||e)});continue;}
    try{if(limits.afterChildOpen)limits.afterChildOpen(rel);walkTreeResilient(child,rel,depth+1,state,limits);}finally{addon.closeParentHandle(child);}
    if(state.truncated||state.cancelled)return;
   }else if(!meta.isDirectory&&!meta.isReparsePoint){
    if(Number(meta.size)>limits.maxFileBytes){state.errors.push({path:rel,error:"file size limit"});continue;}
    try{
     const read=addon.readChildFromParent(dir,entry.name,limits.maxFileBytes);
     if(read.content.toString("utf8").includes(limits.pattern))state.matches.push(rel);
    }catch(e){state.errors.push({path:rel,error:String(e.message||e)});}
   }
  }
  if(state.truncated||state.cancelled||page.done)break;
  if(limits.afterPage)limits.afterPage(prefix,dir);
  try{page=addon.enumerateDirectoryPage(dir,1024);}
  catch(e){state.errors.push({path:prefix,error:String(e.message||e)});break;}
 }
}
function main(){
 process.stdout.write("READY\n");
 const baseHandles=addon.processHandleCount();
 const fixture=fs.mkdtempSync(path.join(__dirname,"fixture-dirwalk-"));
 const root=path.join(fixture,"root"),parent=path.join(root,"parent"),sub=path.join(parent,"sub"),inner=path.join(sub,"inner"),outside=path.join(fixture,"outside");
 fs.mkdirSync(path.join(inner,"nested"),{recursive:true});fs.mkdirSync(path.join(parent,"empty"));fs.mkdirSync(outside);
 fs.writeFileSync(path.join(inner,"nested","needle.txt"),"NEEDLE in root");
 fs.writeFileSync(path.join(outside,"needle-secret.txt"),"NEEDLE outside");
 fs.writeFileSync(path.join(inner,"alpha.txt"),"alpha");fs.writeFileSync(path.join(inner,"beta.txt"),"beta");
 for(let n=0;n<1000;n++)fs.writeFileSync(path.join(inner,"item-"+String(n).padStart(4,"0")+".txt"),"x");
 fs.writeFileSync(path.join(outside,"secret.txt"),"outside");
 let t=null,link=null,moved=false;
 try{
  t=openTree(root);
  assert.throws(()=>addon.enumerateDirectoryPage(t.i,0),/maxEntries must be 1\.\.1024/);
  assert.throws(()=>addon.enumerateDirectoryPage(t.i,1025),/maxEntries must be 1\.\.1024/);
  assert.throws(()=>addon.enumerateDirectoryPage(t.i,1,"yes"),/restart must be a boolean/);
  assert.throws(()=>addon.openDirectoryChild(t.p,".."),/safe filename component/);
  let page=addon.enumerateDirectoryPage(t.i,1024), names=page.entries.map(x=>x.name);
  while(!page.done){page=addon.enumerateDirectoryPage(t.i,1024);names.push(...page.entries.map(x=>x.name));}
  assert.equal(names.length,1003);
  assert.equal(new Set(names).size,1003);
  assert.ok(names.includes("alpha.txt")&&names.includes("beta.txt"));
  const fileMeta=addon.inspectEntryMetadata(t.i,"alpha.txt");
  assert.equal(fileMeta.isDirectory,false);assert.equal(fileMeta.isReparsePoint,false);assert.equal(fileMeta.size,"5");
  assert.ok(fileMeta.fileId&&fileMeta.lastWriteTime100ns);
  const dirMeta=addon.inspectEntryMetadata(t.p,"sub");
  assert.equal(dirMeta.isDirectory,true);assert.equal(dirMeta.isReparsePoint,false);
  closeTree(t);t=null;
  const linkPath=path.join(inner,"outside-link");
  fs.symlinkSync(outside,linkPath,"junction");link=true;
  t=openTree(root);
  let linkPage=addon.enumerateDirectoryPage(t.i,1024), linkEntry=null;
  while(true){linkEntry=linkPage.entries.find(x=>x.name==="outside-link")||linkEntry;if(linkPage.done)break;linkPage=addon.enumerateDirectoryPage(t.i,1024);}
  assert.ok(linkEntry&&linkEntry.isReparsePoint);
  const linkMeta=addon.inspectEntryMetadata(t.i,"outside-link");
  assert.equal(linkMeta.isReparsePoint,true);
  assert.equal(linkMeta.isDirectory,true);
  const walkParent=addon.openParentRelative(root,"parent");
  const tree=[],hits=[];
  try{walkTree(walkParent,"",0,tree,hits,{count:0});}finally{addon.closeParentHandle(walkParent);}
  assert.deepEqual(hits,["sub/inner/nested/needle.txt"]);
  assert.ok(tree.some(x=>x.path==="sub/inner/outside-link"&&x.isReparsePoint));
  assert.ok(!tree.some(x=>x.path.includes("needle-secret.txt")));
  assert.ok(tree.some(x=>x.path==="empty"&&x.isDirectory));
  closeTree(t);t=null;fs.rmdirSync(linkPath);link=false;
  const resilientParent=addon.openParentRelative(root,"parent");
  const alphaPath=path.join(inner,"alpha.txt"),alphaHeld=path.join(inner,"alpha-held.txt");
  let alphaMoved=false;
  const resilient={count:0,entries:[],matches:[],errors:[],truncated:false};
  try{
   walkTreeResilient(resilientParent,"",0,resilient,{
    maxDepth:8,maxEntries:5000,maxFileBytes:1048576,pattern:"NEEDLE",
    beforeEntry(rel){if(rel==="sub/inner/alpha.txt"&&!alphaMoved){fs.renameSync(alphaPath,alphaHeld);alphaMoved=true;}}
   });
  }finally{
   if(alphaMoved)fs.renameSync(alphaHeld,alphaPath);
   addon.closeParentHandle(resilientParent);
  }
  assert.ok(resilient.errors.some(x=>x.path==="sub/inner/alpha.txt"));
  assert.deepEqual(resilient.matches,["sub/inner/nested/needle.txt"]);
  assert.ok(resilient.entries.some(x=>x.path==="empty"));
  const boundedParent=addon.openParentRelative(root,"parent");
  const bounded={count:0,entries:[],matches:[],errors:[],truncated:false};
  try{walkTreeResilient(boundedParent,"",0,bounded,{maxDepth:0,maxEntries:2,maxFileBytes:1048576,pattern:"NEEDLE"});}
  finally{addon.closeParentHandle(boundedParent);}
  assert.equal(bounded.truncated,true);
  assert.ok(bounded.errors.some(x=>x.error==="depth limit"));
  assert.ok(bounded.count<=2);

  const cancelParent=addon.openParentRelative(root,"parent");
  let cancelChecks=0;
  const cancelled={count:0,entries:[],matches:[],errors:[],truncated:false,cancelled:false};
  try{walkTreeResilient(cancelParent,"",0,cancelled,{maxDepth:8,maxEntries:5000,maxFileBytes:1048576,pattern:"NEEDLE",shouldCancel(){return ++cancelChecks>12;}});}
  finally{addon.closeParentHandle(cancelParent);}
  assert.equal(cancelled.cancelled,true);
  assert.ok(cancelled.count<=12);
  assert.equal(addon.processHandleCount(),baseHandles);

  let fakeNow=1000;
  const manager=createSearchSessions({ttlMs:100,maxSessions:2,now:()=>fakeNow});
  const sessionA=manager.create(["a","b","c"]),sessionB=manager.create(["d","e"]);
  assert.deepEqual(manager.page(sessionA,2),{results:["a","b"],nextOffset:2,nextPageAvailable:true,totalFound:3});
  assert.deepEqual(manager.page(sessionA,2),{results:["c"],nextOffset:3,nextPageAvailable:false,totalFound:3});
  assert.equal(manager.list().find(x=>x.id===sessionA).nextOffset,3);
  const sessionC=manager.create(["f"]);
  assert.throws(()=>manager.page(sessionA,1),/Unknown or expired/);
  assert.deepEqual(manager.page(sessionB,10).results,["d","e"]);
  assert.deepEqual(manager.page(sessionC,10).results,["f"]);
  assert.equal(manager.stop(sessionB),true);
  assert.equal(manager.stop(sessionB),false);
  const sessionD=manager.create(["g"]);
  fakeNow+=100;
  assert.throws(()=>manager.page(sessionD,1),/Unknown or expired/);
  assert.equal(manager.list().length,0);

  const pageFaultParent=addon.openParentRelative(root,"parent");
  let pageFaultInjected=false;
  const pageFault={count:0,entries:[],matches:[],errors:[],truncated:false,cancelled:false};
  try{
   walkTreeResilient(pageFaultParent,"",0,pageFault,{
    maxDepth:8,maxEntries:5000,maxFileBytes:1048576,pattern:"NEEDLE",
    afterPage(prefix,dir){if(prefix==="sub/inner"&&!pageFaultInjected){pageFaultInjected=true;addon.closeParentHandle(dir);}}
   });
  }finally{addon.closeParentHandle(pageFaultParent);}
  assert.equal(pageFaultInjected,true);
  assert.ok(pageFault.errors.some(x=>x.path==="sub/inner"&&/closed|invalid|handle/i.test(x.error)));
  assert.equal(addon.processHandleCount(),baseHandles);

  const injectedPageParent=addon.openParentRelative(root,"parent");
  let nativePageFaultInjected=false;
  const nativePageFault={count:0,entries:[],matches:[],errors:[],truncated:false,cancelled:false};
  try{
   walkTreeResilient(injectedPageParent,"",0,nativePageFault,{
    maxDepth:8,maxEntries:5000,maxFileBytes:1048576,pattern:"NEEDLE",
    afterPage(prefix){if(prefix==="sub/inner"&&!nativePageFaultInjected){nativePageFaultInjected=true;addon.failNextDirectoryPageForTest();}}
   });
  }finally{addon.closeParentHandle(injectedPageParent);}
  assert.equal(nativePageFaultInjected,true);
  assert.ok(nativePageFault.errors.some(x=>x.path==="sub/inner"&&/injected directory enumeration page failure/.test(x.error)));
  assert.equal(addon.processHandleCount(),baseHandles);

  const closeFailureBefore=addon.closeFailureCount();
  const closeFaultParent=addon.openParentRelative(root,"parent");
  let closeFaultInjected=false;
  assert.throws(()=>{try{walkTreeResilient(closeFaultParent,"",0,{count:0,entries:[],matches:[],errors:[],truncated:false,cancelled:false},{
   maxDepth:8,maxEntries:5000,maxFileBytes:1048576,pattern:"NEEDLE",
   afterChildOpen(){if(!closeFaultInjected){closeFaultInjected=true;addon.failNextCloseForTest();}}
  });}finally{addon.closeParentHandle(closeFaultParent);}},/CloseHandle failed for parent handle/);
  assert.equal(closeFaultInjected,true);
  assert.equal(addon.closeFailureCount(),closeFailureBefore+1);
  assert.equal(addon.processHandleCount(),baseHandles);

  t={p:addon.openParentRelative(root,"parent")};
  const held=path.join(parent,"sub-held");
  fs.renameSync(sub,held);moved=true;
  fs.symlinkSync(outside,sub,"junction");link=true;
  assert.throws(()=>addon.openDirectoryChild(t.p,"sub"),/reparse point|relative child directory open blocked/i);
  assert.equal(fs.readFileSync(path.join(outside,"secret.txt"),"utf8"),"outside");
  fs.rmdirSync(sub);link=false;fs.renameSync(held,sub);moved=false;
  closeTree(t);t=null;
  t=openTree(root);
  assert.throws(()=>addon.enumerateDirectoryPage(t.i,1),/exceeds maxEntries/);
  let restarted=addon.enumerateDirectoryPage(t.i,1024,true), restartedNames=restarted.entries.map(x=>x.name);
  while(!restarted.done){restarted=addon.enumerateDirectoryPage(t.i,1024);restartedNames.push(...restarted.entries.map(x=>x.name));}
  assert.equal(restartedNames.length,1003);
  assert.equal(new Set(restartedNames).size,1003);
  assert.ok(restartedNames.includes("alpha.txt")&&restartedNames.includes("beta.txt"));
  const endPage=addon.enumerateDirectoryPage(t.i,1024);
  assert.equal(endPage.done,true);
  assert.deepEqual(endPage.entries,[]);
  const closedDirectory=t.i;
  closeTree(t);t=null;
  assert.throws(()=>addon.enumerateDirectoryPage(closedDirectory,16),/invalid|closed|handle/i);
  const closedState={count:0,entries:[],matches:[],errors:[],truncated:false};
  walkTreeResilient(closedDirectory,"closed",0,closedState,{maxDepth:8,maxEntries:10,maxFileBytes:1024,pattern:"NEEDLE"});
  assert.equal(closedState.errors.length,1);
  assert.equal(closedState.errors[0].path,"closed");
  assert.equal(addon.processHandleCount(),baseHandles);
  process.stdout.write(JSON.stringify({ok:true,checks:["nested handle-relative open","native argument validation rejects invalid page bounds and unsafe child names","handle-relative file and directory metadata","leaf junction metadata returned without following","recursive handle-relative list/search skips junction targets","multi-page directory enumeration with no omissions or duplicates","junction entry reported but not traversed","intermediate junction replacement blocked","bounded overflow fails explicitly","restart flag resets consumed enumeration cursor across all pages","end-of-directory is explicit","resilient per-entry metadata failure isolation after synchronized rename","empty-directory traversal","depth and total-entry limits","closed HANDLE enumeration rejection and directory-scoped page error","mid-pagination HANDLE closure is isolated as a directory-scoped error","native page API failure injection is isolated as a directory-scoped error","cooperative cancellation stops traversal and closes directory HANDLEs","search pagination cursor and stable next-page semantics","search session capacity eviction, stop, and TTL expiry","recursive close-status failure aborts traversal and releases remaining HANDLEs","HANDLE count returns to baseline"]})+"\n");
 }finally{
  if(t)closeTree(t);
  if(link){try{fs.rmdirSync(sub);}catch{try{fs.rmdirSync(path.join(inner,"outside-link"));}catch{}}}
  if(moved){try{fs.renameSync(path.join(parent,"sub-held"),sub);}catch{}}
  fs.rmSync(fixture,{recursive:true,force:true});
 }
}
try{main();}catch(e){console.error(e);process.exitCode=1;}