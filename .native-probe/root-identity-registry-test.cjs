const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require(path.join(__dirname, "build", "Release", "napi_probe_raya_phase1_rootapi.node"));
function within(root, target) { const rel=path.relative(root,target); return rel==="" || (!rel.startsWith("..") && !path.isAbsolute(rel)); }
function identityOf(handle) {
  const x=addon.inspectParentHandle(handle);
  assert.equal(x.isDirectory,true); assert.equal(x.isReparsePoint,false);
  assert.match(x.fileId,/^[0-9A-F]{32}$/);
  return Object.freeze({volumeSerialNumber:x.volumeSerialNumber,fileId:x.fileId});
}
function same(a,b) { return a.volumeSerialNumber===b.volumeSerialNumber && a.fileId===b.fileId; }
function close(h) { if(h) addon.closeParentHandle(h); }
function registry(paths) {
  const normalized=paths.map(p=>path.resolve(p));
  for(let i=0;i<normalized.length;i++) for(let j=i+1;j<normalized.length;j++)
    if(within(normalized[i],normalized[j])||within(normalized[j],normalized[i]))
      throw new Error("Ambiguous configured roots overlap.");
  const entries=normalized.map((rootPath,i)=>{
    const h=addon.openWorkspaceRoot(rootPath);
    try { return Object.freeze({rootId:"root-"+(i+1),rootPath,identity:identityOf(h)}); }
    finally { close(h); }
  });
  for(let i=0;i<entries.length;i++) for(let j=i+1;j<entries.length;j++)
    if(same(entries[i].identity,entries[j].identity))
      throw new Error("Ambiguous configured roots share directory identity.");
  return new Map(entries.map(x=>[x.rootId,x]));
}
function openVerified(reg,id) {
  const e=reg.get(id); if(!e) throw new Error("Unknown or stale workspace rootId.");
  const h=addon.openWorkspaceRoot(e.rootPath);
  try { if(!same(e.identity,identityOf(h))) throw new Error("Workspace root identity changed; rootId is stale."); return {handle:h,entry:e}; }
  catch(err) { close(h); throw err; }
}
function main() {
 const f=fs.mkdtempSync(path.join(os.tmpdir(),"raya-root-identity-"));
 const root=path.join(f,"workspace"), moved=path.join(f,"workspace-moved"), outside=path.join(f,"outside");
 const before=addon.processHandleCount(); let junction=false;
 try {
  fs.mkdirSync(root); fs.mkdirSync(outside);
  fs.writeFileSync(path.join(root,"inside.txt"),"ORIGINAL-ROOT");
  fs.writeFileSync(path.join(outside,"inside.txt"),"OUTSIDE-CANARY");
  const reg=registry([root]), original=reg.get("root-1");
  const h=openVerified(reg,"root-1");
  assert.equal(Buffer.from(addon.readChildFromParent(h.handle,"inside.txt",262144).content).toString("utf8"),"ORIGINAL-ROOT"); close(h.handle);
  assert.throws(()=>openVerified(reg,"unknown"),/Unknown or stale/);
  assert.throws(()=>registry([root,path.join(root,"nested")]),/overlap/i);
  assert.throws(()=>registry([root,root]),/overlap/i);
  fs.renameSync(root,moved); fs.mkdirSync(root); fs.writeFileSync(path.join(root,"inside.txt"),"REPLACEMENT-ROOT");
  assert.throws(()=>openVerified(reg,"root-1"),/identity changed/i);
  fs.unlinkSync(path.join(root,"inside.txt")); fs.rmdirSync(root);
  fs.symlinkSync(outside,root,"junction"); junction=true;
  assert.throws(()=>openVerified(reg,"root-1"),/reparse|plain directory|root component/i);
  fs.rmdirSync(root); junction=false;
  const movedReg=registry([moved]);
  assert.ok(same(original.identity,movedReg.get("root-1").identity));
  const movedHandle=openVerified(movedReg,"root-1");
  assert.equal(Buffer.from(addon.readChildFromParent(movedHandle.handle,"inside.txt",262144).content).toString("utf8"),"ORIGINAL-ROOT"); close(movedHandle.handle);
  assert.equal(addon.processHandleCount(),before);
  process.stdout.write(JSON.stringify({ok:true,checks:12,identity:"volume serial + 128-bit FILE_ID_INFO",replacement:"detected",junction:"rejected",movementPolicy:"explicit re-registration at new path",cleanup:"HANDLE owners closed"})+"\n");
 } finally {
  if(junction) { try{fs.rmdirSync(root);}catch{} }
  // Remove only the exact, uniquely-created test fixture entries.
  try{fs.unlinkSync(path.join(moved,"inside.txt"));}catch{}
  try{fs.rmdirSync(moved);}catch{}
  try{fs.unlinkSync(path.join(outside,"inside.txt"));}catch{}
  try{fs.rmdirSync(outside);}catch{}
  try{fs.rmdirSync(f);}catch{}
 }
}
try{main();}catch(e){console.error(e);process.exitCode=1;}