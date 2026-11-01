import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import fs from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import { fileURLToPath } from "node:url";
const here=path.dirname(fileURLToPath(import.meta.url));
const transport=new StdioClientTransport({command:process.execPath,args:[path.join(here,"server.mjs")]});
const client=new Client({name:"local-mcp-bridge-smoke-test",version:"0.2.0"});
const dir=path.join(here,".mcp-smoke-"+process.pid);
const outsideDir=await fs.mkdtemp(path.join(os.tmpdir(),"raya-outside-"));
function parse(r){if(r.isError)throw new Error("Tool returned error: "+JSON.stringify(r));const t=r.content?.find(x=>x.type==="text")?.text;if(!t)throw new Error("Tool returned no text");try{return JSON.parse(t)}catch{return t}}
async function call(name,args={}){return parse(await client.callTool({name,arguments:args}));}
const checks=[];
function check(name,condition,detail){if(!condition)throw new Error("FAILED "+name+": "+JSON.stringify(detail));checks.push({name,ok:true});}
try{
 await client.connect(transport);
 const {tools}=await client.listTools();
 const names=new Set(tools.map(t=>t.name));
 const expected=["workspace_roots","workspace_list","file_read","file_write","run_check","powershell_execute","get_config","get_usage_stats","get_recent_tool_calls","create_directory","get_file_info","read_multiple_files","list_directory","move_file","edit_block","start_process","read_process_output","interact_with_process","list_sessions","force_terminate","list_processes","kill_process","start_search","get_more_search_results","list_searches","stop_search","get_prompts","read_file_tail"];
 check("tool inventory",expected.every(n=>names.has(n)),[...names]);
 const roots=await call("workspace_roots");
 check("workspace roots",Array.isArray(roots)&&roots.length>0,roots);
 const config=await call("get_config");
 check("config limits",config.settings?.maxSessions>=1&&config.security?.shellIsSandboxed===false,config);
 let blocked=false;try{await call("file_read",{path:"C:\\Windows\\win.ini"})}catch{blocked=true}
 check("outside-root blocked",blocked);
 await call("create_directory",{path:dir,recursive:true});
 const nestedDir=path.join(dir,"nested","deep");
 await call("create_directory",{path:nestedDir,recursive:true});
 check("recursive directory creation",(await call("get_file_info",{path:nestedDir})).type==="directory");
 const outsideFile=path.join(outsideDir,"outside.txt");
 await fs.writeFile(outsideFile,"outside-root-secret-fixture","utf8");
 const junction=path.join(dir,"outside-junction");
 let junctionSupported=false;
 try {
  await fs.symlink(outsideDir,junction,"junction");
  junctionSupported=true;
 } catch (e) {
  if (!["EPERM","EACCES","ENOTSUP","EINVAL"].includes(e.code)) throw e;
 }
 if (junctionSupported) {
  let junctionReadBlocked=false;
  try { await call("file_read",{path:path.join(junction,"outside.txt")}); } catch { junctionReadBlocked=true; }
  check("junction escape read blocked",junctionReadBlocked);
  let junctionEditBlocked=false;
  try { await call("edit_block",{path:path.join(junction,"outside.txt"),search:"outside-root-secret-fixture",replace:"modified"}); } catch { junctionEditBlocked=true; }
  check("junction escape edit blocked",junctionEditBlocked);
  let junctionMoveBlocked=false;
  try { await call("move_file",{source:path.join(junction,"outside.txt"),destination:path.join(dir,"stolen.txt")}); } catch { junctionMoveBlocked=true; }
  check("junction escape move blocked",junctionMoveBlocked);
  let junctionInfoBlocked=false;
  try { await call("get_file_info",{path:junction}); } catch { junctionInfoBlocked=true; }
  check("junction escape metadata blocked",junctionInfoBlocked);
  let junctionListBlocked=false;
  try { await call("list_directory",{path:junction}); } catch { junctionListBlocked=true; }
  check("junction escape directory listing blocked",junctionListBlocked);
  const multiRead=await call("read_multiple_files",{paths:[path.join(junction,"outside.txt")]});
  check("junction escape multi-read blocked",Boolean(multiRead.files?.[0]?.error));
  let junctionWriteBlocked=false;
  try { await call("file_write",{path:path.join(junction,"new.txt"),content:"must-not-escape"}); } catch { junctionWriteBlocked=true; }
  check("junction escape write blocked",junctionWriteBlocked);
  let junctionMkdirBlocked=false;
  try { await call("create_directory",{path:path.join(junction,"new-directory")}); } catch { junctionMkdirBlocked=true; }
  check("junction escape directory creation blocked",junctionMkdirBlocked);
  check("junction target remains unchanged",await fs.readFile(outsideFile,"utf8")==="outside-root-secret-fixture");
 } else {
  checks.push({name:"junction escape regression",ok:true,skipped:"directory junction creation unavailable on this host"});
 }
 const file=path.join(dir,"sample.txt");
 await call("file_write",{path:file,content:"alpha\nbeta\nalpha\n"});
 let overwriteBlocked=false;try{await call("file_write",{path:file,content:"bad"})}catch{overwriteBlocked=true}
 check("overwrite protected",overwriteBlocked);
 const raceFile=path.join(dir,"concurrent-create.txt");
 const raceResults=await Promise.allSettled([
  call("file_write",{path:raceFile,content:"writer-A"}),
  call("file_write",{path:raceFile,content:"writer-B"})
 ]);
 const raceSuccesses=raceResults.filter(r=>r.status==="fulfilled").length;
 const raceFailures=raceResults.filter(r=>r.status==="rejected").length;
 const raceContent=await fs.readFile(raceFile,"utf8");
 check("concurrent create is atomic",raceSuccesses===1&&raceFailures===1&&["writer-A","writer-B"].includes(raceContent),{raceSuccesses,raceFailures,raceContent});
 const preview=await call("edit_block",{path:file,search:"beta",replace:"gamma",previewOnly:true});
 check("edit preview",preview.previewOnly&&preview.replacements===1,preview);
 const edited=await call("edit_block",{path:file,search:"beta",replace:"gamma"});
 check("exact edit",edited.replacements===1,edited);
 const multiple=await call("read_multiple_files",{paths:[file,path.join(dir,"missing.txt")]});
 check("multi-read per-file errors",multiple.files.length===2&&multiple.files[0].content.includes("gamma")&&Boolean(multiple.files[1].error),multiple);
 const info=await call("get_file_info",{path:file});
 check("file metadata",info.type==="file"&&info.sizeBytes>0,info);
 const listing=await call("list_directory",{path:dir});
 check("directory listing",listing.entries.some(e=>e.name==="sample.txt"),listing);
 const moved=path.join(dir,"renamed.txt");
 await call("move_file",{source:file,destination:moved});
 check("move/rename", (await call("get_file_info",{path:moved})).type==="file");
 const search=await call("start_search",{roots:[dir],pattern:"gamma",mode:"content",maxResults:50});
 check("content search",search.totalFound>=1&&Boolean(search.searchId),search);
 const more=await call("get_more_search_results",{searchId:search.searchId,pageSize:10});
 check("search pagination",Array.isArray(more.results),more);
 const listed=await call("list_searches");
 check("search session listing",listed.some(s=>s.searchId===search.searchId),listed);
 check("search session expiry metadata",Boolean(listed.find(s=>s.searchId===search.searchId)?.expiresAt),listed);
 const stopped=await call("stop_search",{searchId:search.searchId});
 check("search cleanup",stopped.removed===true,stopped);
 const capacitySearches=[];
 for(let i=0;i<9;i++)capacitySearches.push(await call("start_search",{roots:[dir],pattern:"gamma",mode:"content",maxResults:10}));
 const retained=await call("list_searches");
 check("search capacity remains bounded",retained.length<=8&&retained.some(s=>s.searchId===capacitySearches[8].searchId),{count:retained.length});
 let oldestEvicted=false;try{await call("get_more_search_results",{searchId:capacitySearches[0].searchId,pageSize:1});}catch{oldestEvicted=true;}
 check("search capacity evicts oldest session",oldestEvicted,{oldest:capacitySearches[0].searchId,newest:capacitySearches[8].searchId});
 const proc=await call("start_process",{executable:process.execPath,args:["-e","process.stdout.write('raya-process-ok')"],cwd:here,initialWaitMs:500});
 check("process starts",Boolean(proc.sessionId)&&Boolean(proc.pid),proc);
 const output=await call("read_process_output",{sessionId:proc.sessionId,maxChars:2000});
 check("process output",output.stdout.includes("raya-process-ok")&&output.status!=="running",output);
 const sessions=await call("list_sessions",{includeFinished:true});
 check("session listing",sessions.some(s=>s.sessionId===proc.sessionId),sessions);
 const echoProc=await call("start_process",{executable:process.execPath,args:["-e","process.stdin.on('data',d=>{process.stdout.write('echo:'+d.toString());process.exit(0)})"],cwd:here,initialWaitMs:100});
 const sent=await call("interact_with_process",{sessionId:echoProc.sessionId,input:"hello",appendNewline:true});
 await new Promise(resolve=>setTimeout(resolve,250));
 const echoOut=await call("read_process_output",{sessionId:echoProc.sessionId,maxChars:2000});
 check("interactive stdin",sent.sent&&echoOut.stdout.includes("echo:hello"),echoOut);
 const longProc=await call("start_process",{executable:process.execPath,args:["-e","setInterval(()=>{},1000)"],cwd:here,initialWaitMs:100});
 const term=await call("force_terminate",{sessionId:longProc.sessionId,force:true});
 await new Promise(resolve=>setTimeout(resolve,300));
 const terminated=await call("read_process_output",{sessionId:longProc.sessionId,maxChars:1000});
 check("managed termination",term.terminationRequested&&terminated.status==="terminated",terminated);
 const processList=await call("list_processes");
 check("OS process listing",Number.isInteger(processList.count)&&Array.isArray(processList.processes),processList);
 let pidMismatchBlocked=false;try{await call("kill_process",{pid:process.pid,expectedName:"definitely-not-the-current-process",confirm:true})}catch{pidMismatchBlocked=true}
 check("PID identity mismatch blocked",pidMismatchBlocked);
 const feedback=await call("give_feedback_to_desktop_commander",{topic:"automated smoke test",feedback:"test-only local note"});
 check("feedback remains local",feedback.recordedLocally===true&&feedback.externalTransmission===false,feedback);
 let shellGateVerified=false;
 try{
  const shell=await call("powershell_execute",{command:"Write-Output 'raya-shell-ok'",cwd:here,timeoutMs:10000});
  shellGateVerified=shell.exitCode===0&&shell.stdout.includes("raya-shell-ok");
 }catch(e){
  shellGateVerified=String(e).includes("PowerShell execution is disabled");
 } check("PowerShell execution gate",shellGateVerified,{enabled:process.env.MCP_ENABLE_POWERSHELL==="true"});
 const stats=await call("get_usage_stats");
 check("usage stats",stats.calls>=1,stats);
 const recent=await call("get_recent_tool_calls",{limit:10});
 check("recent calls",Array.isArray(recent)&&recent.length>0,recent);
 const prompts=await call("get_prompts");
 check("workflow prompts",Array.isArray(prompts)&&prompts.length>=1,prompts);
 const tail=await call("read_file_tail",{path:moved,lines:2});
 check("file tail",tail.tail.includes("gamma"),tail);
 console.log(JSON.stringify({ok:true,toolCount:tools.length,checked:checks.length,checks,rootsReturned:true,outsideRootBlocked:true},null,2));
}finally{
 try{await call("force_terminate",{sessionId:"00000000-0000-4000-8000-000000000000"})}catch{}
 await client.close().catch(()=>{});
 await fs.rm(dir,{recursive:true,force:true}).catch(()=>{});
 await fs.rm(outsideDir,{recursive:true,force:true}).catch(()=>{});
}





