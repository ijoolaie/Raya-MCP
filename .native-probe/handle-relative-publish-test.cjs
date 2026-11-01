const assert=require("node:assert/strict");
const fs=require("node:fs");
const os=require("node:os");
const path=require("node:path");
const roots=require(path.join(__dirname,"build","Release","napi_probe_raya_phase1_rootapi.node"));
const publish=require(path.join(__dirname,"build","Release","napi_probe_raya_phase1_publish.node"));

function main(){
 const fixture=fs.mkdtempSync(path.join(os.tmpdir(),"raya-publish-exclusive-"));
 const root=path.join(fixture,"root"),outside=path.join(fixture,"outside"),moved=path.join(fixture,"root-held");
 let parent=null,swapped=false;
 const baseline=roots.processHandleCount(),checks=[];
 const close=()=>{if(parent){roots.closeParentHandle(parent);parent=null;}};
 try{
  fs.mkdirSync(root);fs.mkdirSync(outside);
  parent=roots.openWorkspaceRoot(root);
  const payload=Buffer.from("NEW-FILE-UTF8-سلام-✓");
  let r=publish.publishChildExclusiveRelative(parent,".tmp-one","created.txt",payload);
  assert.equal(r.committed,true);assert.equal(r.bytesWritten,payload.length);assert.equal(r.replacedExisting,false);
  assert.deepEqual(fs.readFileSync(path.join(root,"created.txt")),payload);
  assert.equal(fs.existsSync(path.join(root,".tmp-one")),false);
  checks.push("bounded bytes are staged, flushed and atomically published under held parent HANDLE");

  fs.writeFileSync(path.join(root,"existing.txt"),"ORIGINAL");
  assert.throws(()=>publish.publishChildExclusiveRelative(parent,".tmp-two","existing.txt",Buffer.from("MUST-NOT-REPLACE")),/publish_failed_without_overwrite/);
  assert.equal(fs.readFileSync(path.join(root,"existing.txt"),"utf8"),"ORIGINAL");
  assert.equal(fs.existsSync(path.join(root,".tmp-two")),false);
  checks.push("existing destination collision preserves original and removes staged sibling");

  fs.writeFileSync(path.join(root,".tmp-collision"),"TEMP-CANARY");
  assert.throws(()=>publish.publishChildExclusiveRelative(parent,".tmp-collision","never.txt",Buffer.from("x")),/temporary_create_failed/);
  assert.equal(fs.readFileSync(path.join(root,".tmp-collision"),"utf8"),"TEMP-CANARY");
  checks.push("temporary sibling collision is exclusive and preserves pre-existing temp entry");

  publish.failNextWriteForTest();
  assert.throws(()=>publish.publishChildExclusiveRelative(parent,".tmp-write-fail","write-fail.txt",Buffer.from("x")),/injected_write_failure/);
  assert.equal(fs.existsSync(path.join(root,".tmp-write-fail")),false);
  assert.equal(fs.existsSync(path.join(root,"write-fail.txt")),false);
  checks.push("injected write failure cleans temporary sibling and exposes no destination");

  publish.failNextFlushForTest();
  assert.throws(()=>publish.publishChildExclusiveRelative(parent,".tmp-flush-fail","flush-fail.txt",Buffer.from("x")),/injected_flush_failure/);
  assert.equal(fs.existsSync(path.join(root,".tmp-flush-fail")),false);
  assert.equal(fs.existsSync(path.join(root,"flush-fail.txt")),false);
  checks.push("injected flush failure cleans temporary sibling and exposes no destination");

  assert.throws(()=>publish.publishChildExclusiveRelative(parent,"bad\\leaf","bad.txt",Buffer.from("x")),/invalid_name/);
  assert.throws(()=>publish.publishChildExclusiveRelative(parent,".tmp-bad","bad:stream",Buffer.from("x")),/invalid_name/);
  assert.throws(()=>publish.publishChildExclusiveRelative(parent,"same.txt","same.txt",Buffer.from("x")),/invalid_name/);
  assert.throws(()=>publish.publishChildExclusiveRelative(parent,".tmp-too-large","large.txt",Buffer.alloc(1024*1024+1)),/1 MiB/);
  checks.push("unsafe leaf names, identical temp/destination and oversized buffers are rejected");

  publish.failNextCloseAfterCommitForTest();
  assert.throws(()=>publish.publishChildExclusiveRelative(parent,".tmp-post","post-commit.txt",Buffer.from("COMMITTED")),/committed_but_cleanup_failed/);
  assert.equal(fs.readFileSync(path.join(root,"post-commit.txt"),"utf8"),"COMMITTED");
  checks.push("post-commit close-status failure is reported while published bytes remain committed");

  close();
  fs.renameSync(root,moved);
  fs.symlinkSync(outside,root,"junction");swapped=true;
  parent=roots.openWorkspaceRoot(moved);
  publish.publishChildExclusiveRelative(parent,".tmp-held","held-parent.txt",Buffer.from("HELD-PARENT"));
  assert.equal(fs.readFileSync(path.join(moved,"held-parent.txt"),"utf8"),"HELD-PARENT");
  assert.equal(fs.existsSync(path.join(outside,"held-parent.txt")),false);
  checks.push("root rename and outside junction replacement cannot redirect held parent HANDLE");

  close();
  assert.equal(roots.processHandleCount(),baseline);
  checks.push("parent HANDLE ownership returns process count to baseline");
  process.stdout.write(JSON.stringify({ok:true,checks,checkCount:checks.length,maxBytes:1024*1024,semantics:"atomic create-if-absent only; existing-file replacement intentionally unsupported"})+"\n");
 }finally{
  close();
  if(swapped){try{fs.rmdirSync(root);}catch{}}
  fs.rmSync(fixture,{recursive:true,force:true});
 }
}
try{main();}catch(e){console.error(e);process.exitCode=1;}
