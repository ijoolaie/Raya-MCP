"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const os=require("node:os");
const path=require("node:path");
const addon=require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");
const base=fs.mkdtempSync(path.join(__dirname,"fixture-dircreate-nul-"));
const root=path.join(base,"root");
fs.mkdirSync(path.join(root,"parent"),{recursive:true});
let parent=null;
try{
 const baseline=addon.processHandleCount();
 parent=addon.openParentRelative(root,"parent");
 const afterOpen=addon.processHandleCount();
 const relativeInput="nul-prefix\0\\nested";
 assert.throws(()=>addon.createDirectoryPathRelative(parent,relativeInput),/valid UTF-8|invalid UTF-8/i);
 assert.equal(fs.existsSync(path.join(root,"parent","nul-prefix")),false,"NUL path created a truncated prefix");
 assert.throws(()=>addon.openParentRelative(root+"\0\\..\\outside","parent"),/invalid UTF-8/i);
 assert.equal(addon.processHandleCount(),afterOpen,"rejected NUL root path leaked a HANDLE");
 addon.closeParentHandle(parent);parent=null;
 assert.equal(addon.processHandleCount(),baseline,"HANDLE count did not return to baseline");
 console.log(JSON.stringify({ok:true,relativeEmbeddedNulRejected:true,noTruncatedDirectoryCreated:true,rootEmbeddedNulRejected:true,noHandleLeakOnRejectedRoot:true,handleCountReturnedToBaseline:true},null,2));
}finally{
 if(parent){try{addon.closeParentHandle(parent);}catch{}}
 fs.rmSync(base,{recursive:true,force:true});
}
