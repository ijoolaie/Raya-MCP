import {Client} from "@modelcontextprotocol/sdk/client/index.js";
import {StdioClientTransport} from "@modelcontextprotocol/sdk/client/stdio.js";
import fs from "node:fs/promises";import path from "node:path";import {fileURLToPath} from "node:url";
const here=path.dirname(fileURLToPath(import.meta.url)),dir=path.join(here,".format-smoke-"+process.pid);
const transport=new StdioClientTransport({command:process.execPath,args:[path.join(here,"server.mjs")],env:{...process.env,MCP_ALLOWED_ROOTS:here}});
const client=new Client({name:"raya-format-smoke",version:"0.1.0"}),checks=[];
function parse(r){if(r.isError)throw new Error(JSON.stringify(r));const t=r.content?.find(x=>x.type==="text")?.text;try{return JSON.parse(t)}catch{return t}}
async function call(name,args={}){return parse(await client.callTool({name,arguments:args}));}
function check(name,ok,detail){if(!ok)throw new Error("FAILED "+name+" "+JSON.stringify(detail));checks.push(name);}
try{
 await client.connect(transport);const {tools}=await client.listTools(),names=new Set(tools.map(t=>t.name));
 for(const name of ["set_config_value","read_url","excel_read_workbook","excel_write_cells","excel_read_range","excel_search_workbook","docx_search","pdf_merge","docx_replace_text","excel_set_formula","pdf_add_text","docx_read","docx_create","pdf_extract_text","pdf_info","pdf_create"])check("tool:"+name,names.has(name),[...names]);
 await fs.mkdir(dir,{recursive:true});
 const xlsx=path.join(dir,"sample.xlsx");
 const ew=await call("excel_write_cells",{path:xlsx,sheetName:"Sheet1",cells:[{address:"A1",value:"Name"},{address:"B1",value:42}],createSheet:true});
 check("xlsx write",ew.cellsWritten===2,ew);
 const er=await call("excel_read_workbook",{path:xlsx,sheetName:"Sheet1",maxRows:10,maxCols:5});
 check("xlsx read",er.rows[0][0]==="Name"&&er.rows[0][1]===42,er);
 let protectedWrite=false;try{await call("excel_write_cells",{path:xlsx,sheetName:"Sheet1",cells:[{address:"A1",value:"No"}]})}catch{protectedWrite=true}
 check("xlsx overwrite guard",protectedWrite);
 const docx=path.join(dir,"sample.docx");
 const dc=await call("docx_create",{path:docx,title:"Raya smoke",paragraphs:["Document format test."]});
 check("docx create",dc.created===docx,dc);
 const dr=await call("docx_read",{path:docx});
 check("docx extract",dr.text.includes("Document format test."),dr);
 const ds=await call("docx_search",{path:docx,query:"Document format"});
 check("docx search",ds.count===1,ds);
 const drp=await call("docx_replace_text",{path:docx,search:"Document format test.",replace:"Document replacement test.",expectedCount:1,overwrite:true});
 check("docx exact replacement",drp.replacements===1,drp);
 const dr2=await call("docx_read",{path:docx});
 check("docx replacement readback",dr2.text.includes("Document replacement test."),dr2);
 const range=await call("excel_read_range",{path:xlsx,sheetName:"Sheet1",range:"A1:B1"});
 check("xlsx range",range.values[0][1]===42,range);
 const searchX=await call("excel_search_workbook",{path:xlsx,query:"Name"});
 check("xlsx search",searchX.count===1&&searchX.matches[0].address==="A1",searchX);
 const formulaWrite=await call("excel_set_formula",{path:xlsx,sheetName:"Sheet1",address:"B2",formula:"SUM(B1:B1)",overwrite:true});
 check("xlsx formula",formulaWrite.formula==="SUM(B1:B1)",formulaWrite); const pdf=path.join(dir,"sample.pdf");
 const pc=await call("pdf_create",{path:pdf,title:"Raya smoke",paragraphs:["PDF format test."]});
 check("pdf create",pc.pages>=1,pc);
 const pi=await call("pdf_info",{path:pdf});
 check("pdf info",pi.pageCount>=1,pi);
 const pt=await call("pdf_extract_text",{path:pdf,maxChars:1000});
 check("pdf extract",pt.text.includes("PDF format test."),pt);
 const overlay=await call("pdf_add_text",{path:pdf,pageNumber:1,text:"Overlay test",x:48,y:48,fontSize:12,confirmOverwrite:true});
 check("pdf text overlay",overlay.pageNumber===1,overlay);
 const pdf2=path.join(dir,"sample2.pdf");
 await call("pdf_create",{path:pdf2,title:"Second PDF",paragraphs:["Second page source."]});
 const merged=path.join(dir,"merged.pdf");
 const mergeResult=await call("pdf_merge",{paths:[pdf,pdf2],outputPath:merged});
 check("pdf merge",mergeResult.pages===2,mergeResult); let blocked=false;try{await call("read_url",{url:"http://127.0.0.1/"})}catch{blocked=true}
 check("URL SSRF/HTTP guard",blocked);
 const cfg=await call("set_config_value",{key:"search.maxDepth",value:8});
 check("config persistence",cfg.persisted&&cfg.config.searchMaxDepth===8,cfg);
 console.log(JSON.stringify({ok:true,toolCount:tools.length,checked:checks.length,checks},null,2));
}finally{await client.close().catch(()=>{});await fs.rm(dir,{recursive:true,force:true}).catch(()=>{});}
