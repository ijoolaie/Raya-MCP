"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const os=require("node:os");
const path=require("node:path");
const addon=require("./build/Release/napi_probe_raya_phase1_rootapi.node");
const fixture=fs.mkdtempSync(path.join(os.tmpdir(),"raya-rootapi-hardlink-policy-"));
const root=path.join(fixture,"root"),outside=path.join(fixture,"outside");
let parent=null;
try{
 fs.mkdirSync(root);fs.mkdirSync(outside);
 fs.writeFileSync(path.join(outside,"secret.txt"),"OUTSIDE-SECRET-HARDLINK-CANARY");
 fs.linkSync(path.join(outside,"secret.txt"),path.join(root,"alias.txt"));
 assert.equal(fs.statSync(path.join(root,"alias.txt")).nlink,2);
 parent=addon.openWorkspaceRoot(root);
 const read=addon.readChildFromParent(parent,"alias.txt",1024);
 const tail=addon.readTailChildFromParent(parent,"alias.txt",1024);
 const metadata=addon.inspectEntryMetadata(parent,"alias.txt");
 const page=addon.enumerateDirectoryPage(parent,100,true);
 const entry=page.entries.find(x=>x.name==="alias.txt");
 assert.equal(read.content.toString(),"OUTSIDE-SECRET-HARDLINK-CANARY");
 assert.equal(tail.content.toString(),"OUTSIDE-SECRET-HARDLINK-CANARY");
 assert.equal(metadata.numberOfLinks,2);
 assert.ok(entry,"directory enumeration must include the alias");
 assert.equal(entry.isDirectory,false);
 assert.equal(entry.isReparsePoint,false);
 assert.equal(Object.prototype.hasOwnProperty.call(entry,"numberOfLinks"),false);
 console.log(JSON.stringify({
  ok:true,
  localProvider:"NTFS",
  outsideBackedHardLinkCount:metadata.numberOfLinks,
  readChildReturnedOutsideBackedBytes:true,
  readTailReturnedOutsideBackedBytes:true,
  metadataReturnedWithoutRejectingMultiLink:true,
  enumerationReturnedRegularFileWithoutLinkCount:true,
  classification:"isolated rootapi candidate does not enforce the approved strict single-link policy"
 },null,2));
}finally{
 if(parent){try{addon.closeParentHandle(parent);}catch{}}
 fs.rmSync(fixture,{recursive:true,force:true});
}
