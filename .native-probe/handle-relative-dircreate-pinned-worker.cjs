"use strict";
const fs=require("node:fs");
const addon=require("./build/Release/napi_probe_raya_phase1_dircreate_pinned.node");
const root=process.argv[2];
let parent=null,failure=null,result=null,baseline=null,opened=null,afterReady=null,beforeWait=null,afterWait=null,afterCreate=null,closeFailures=null;
function wait(){const b=Buffer.alloc(128);let s="";while(!s.includes("\n")){const n=fs.readSync(0,b,0,b.length,null);if(!n)throw new Error("sync pipe closed");s+=b.subarray(0,n).toString("utf8");}return s.split(/\r?\n/,1)[0];}
try{baseline=addon.processHandleCount();parent=addon.openParentRelative(root,"parent");opened=addon.processHandleCount();process.stdout.write("PINNED_PARENT_READY\n");afterReady=addon.processHandleCount();beforeWait=addon.processHandleCount();const cmd=wait();afterWait=addon.processHandleCount();if(cmd==="PATH"){result=addon.createDirectoryPathRelative(parent,"recursive-a\\recursive-b\\recursive-c");afterCreate=addon.processHandleCount();}else throw new Error("unexpected command "+cmd);}
catch(e){failure=e.message;}
finally{if(parent){try{addon.closeParentHandle(parent);}catch(e){failure=(failure?failure+"; ":"")+e.message;}}}
const afterClose=addon.processHandleCount();closeFailures=addon.closeFailureCount();console.log(JSON.stringify({ok:!failure,message:failure,result,baseline,opened,afterReady,beforeWait,afterWait,afterCreate,afterClose,closeFailures}));if(failure)process.exitCode=1;
