const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const addon = require(path.join(__dirname, "build", "Release", "napi_probe_raya_phase1_rootapi.node"));
const TAG_MOUNT_POINT = 0xA0000003;
const TAG_SYMLINK = 0xA000000C;
function main() {
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(), "raya-reparse-parity-"));
  const root = path.join(fixture, "root"), outside = path.join(fixture, "outside");
  let handle = null;
  const installed = [], outcomes = [], before = addon.processHandleCount();
  try {
    fs.mkdirSync(root); fs.mkdirSync(outside);
    const unicodeOutside=path.join(fixture,"outside-سلام"); fs.mkdirSync(unicodeOutside);
    fs.writeFileSync(path.join(outside, "target.txt"), "target");
    fs.mkdirSync(path.join(outside, "target-dir"));
    const fixtures = [
      {name:"directory-junction",target:outside,type:"junction",tag:TAG_MOUNT_POINT,required:true},
      {name:"directory-symlink",target:outside,type:"dir",tag:TAG_SYMLINK,required:false},
      {name:"file-symlink",target:path.join(outside,"target.txt"),type:"file",tag:TAG_SYMLINK,required:false},
      {name:"relative-file-symlink",target:path.relative(root,path.join(outside,"target.txt")),type:"file",tag:TAG_SYMLINK,required:false},
      {name:"unicode-directory-symlink",target:unicodeOutside,type:"dir",tag:TAG_SYMLINK,required:false}
    ];
    for (const item of fixtures) {
      try { fs.symlinkSync(item.target,path.join(root,item.name),item.type); installed.push(item.name); }
      catch(e) { if(item.required) throw e; outcomes.push(item.name+": creation unavailable ("+(e.code||e.message)+")"); }
    }
    handle=addon.openWorkspaceRoot(root);
    for(const item of fixtures) {
      if(!installed.includes(item.name)) continue;
      const linkPath=path.join(root,item.name), nodeStat=fs.lstatSync(linkPath);
      const native=addon.inspectEntryMetadata(handle,item.name);
      const filetimeMs=value=>Number((BigInt(value)-116444736000000000n)/10000n);
      assert.ok(Math.abs(filetimeMs(native.creationTime100ns)-nodeStat.birthtimeMs)<=1,item.name+" creation time");
      assert.ok(Math.abs(filetimeMs(native.lastWriteTime100ns)-nodeStat.mtimeMs)<=1,item.name+" modified time");
      assert.ok(Math.abs(filetimeMs(native.lastAccessTime100ns)-nodeStat.atimeMs)<=1,item.name+" accessed time");
      assert.equal(Boolean(native.attributes&1),(nodeStat.mode&0o200)===0,item.name+" readonly");
      assert.equal(nodeStat.isSymbolicLink(),true,item.name);
      assert.equal(native.isReparsePoint,true,item.name);
      assert.equal(native.reparseTag,item.tag,item.name);
      assert.equal(native.size,"0",item.name);
      assert.equal(native.legacySize,"0",item.name);
      assert.equal(Number(native.reparseLogicalSize),nodeStat.size,item.name);
      const printName=native.reparsePrintName;
      const hasExtendedPrefix=printName.length>=4&&printName.charCodeAt(0)===92&&printName.charCodeAt(1)===92&&printName.charCodeAt(2)===63&&printName.charCodeAt(3)===92;
      const normalizedPrintName=hasExtendedPrefix?printName.slice(4):printName;
      assert.equal(normalizedPrintName,item.target,item.name+" decoded print name");
      assert.ok(nodeStat.size>0,item.name);
      assert.equal(native.isDirectory,item.type!=="file",item.name);
      assert.equal(nodeStat.isDirectory(),false,item.name);
      outcomes.push(item.name+": Node symlink=true; tag=0x"+native.reparseTag.toString(16).toUpperCase()+"; native directory="+native.isDirectory+"; standard size="+native.size+"; legacy size="+native.legacySize+"; Node-compatible size="+native.reparseLogicalSize+"; Node lstat size="+nodeStat.size);
    }
    assert.equal(addon.processHandleCount(),before+1);
    addon.closeParentHandle(handle); handle=null;
    assert.equal(addon.processHandleCount(),before);
    process.stdout.write(JSON.stringify({ok:true,outcomes,checks:[
      "directory junction maps to IO_REPARSE_TAG_MOUNT_POINT",
      "absolute, relative, and Unicode file/directory symlinks map to IO_REPARSE_TAG_SYMLINK when creation is available",
      "Node lstat reports reparse entries as symbolic links and not directories",
      "native metadata exposes reparse tag and native directory attribute",
      "reparse print-name decoding and UTF-8 byte size match Node lstat for junctions and absolute, relative, and Unicode symlinks",
      "all handles close and process HANDLE count returns to baseline"
    ]})+"\n");
  } finally {
    if(handle){try{addon.closeParentHandle(handle);}catch{}}
    for(const name of installed){const p=path.join(root,name);try{fs.rmdirSync(p);}catch{try{fs.unlinkSync(p);}catch{}}}
    fs.rmSync(fixture,{recursive:true,force:true});
  }
}
try{main();}catch(e){console.error(e);process.exitCode=1;}