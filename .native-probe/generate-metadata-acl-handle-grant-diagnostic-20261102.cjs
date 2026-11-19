"use strict";
const fs = require("node:fs");
const path = require("node:path");
const dir = __dirname;
const source = fs.readFileSync(path.join(dir, "handle-relative-probe-phase1-rootapi.c"), "utf8");
let clone = source;
const typedefAnchor = "typedef NTSTATUS (NTAPI *NtCreateFileFn)(";
const typedefPos = clone.indexOf(typedefAnchor);
if (typedefPos < 0) throw new Error("NtCreateFileFn anchor missing");
const typedefEnd = clone.indexOf(";\n", typedefPos) + 2;
const diagTypedef = `
typedef struct _RAYA_OBJECT_BASIC_INFORMATION {
  ULONG Attributes;
  ACCESS_MASK GrantedAccess;
  ULONG HandleCount;
  ULONG PointerCount;
  ULONG PagedPoolUsage;
  ULONG NonPagedPoolUsage;
  ULONG Reserved[3];
  ULONG NameInfoSize;
  ULONG TypeInfoSize;
  ULONG SecurityDescriptorSize;
  LARGE_INTEGER CreationTime;
} RAYA_OBJECT_BASIC_INFORMATION;
typedef NTSTATUS (NTAPI *RayaNtQueryObjectFn)(
    HANDLE, ULONG, PVOID, ULONG, PULONG);
`;
clone = clone.slice(0, typedefEnd) + diagTypedef + clone.slice(typedefEnd);

const fnStart = clone.indexOf("static napi_value inspect_entry_metadata(");
if (fnStart < 0) throw new Error("inspect_entry_metadata missing");
const fnEnd = clone.indexOf("\nstatic napi_value init(", fnStart);
if (fnEnd < 0) throw new Error("inspect function end missing");
let fn = clone.slice(fnStart, fnEnd);
const varAnchor = "DWORD reparseLogicalSize = 0;";
if (!fn.includes(varAnchor)) throw new Error("metadata variable anchor missing");
fn = fn.replace(varAnchor, varAnchor + `
  ACCESS_MASK diagnosticGrantedAccess = 0;
  ULONG diagnosticNtQueryObjectStatus = 0xFFFFFFFFUL;
`);
const openAnchor = `  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "%s: relative metadata open blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
`;
if (!fn.includes(openAnchor)) throw new Error("post-open anchor missing");
const queryBlock = openAnchor + `
  {
    HMODULE ntdllForQuery = GetModuleHandleW(L"ntdll.dll");
    RayaNtQueryObjectFn queryObject = ntdllForQuery ?
      (RayaNtQueryObjectFn)GetProcAddress(ntdllForQuery, "NtQueryObject") : NULL;
    RAYA_OBJECT_BASIC_INFORMATION basicInfo;
    ULONG returned = 0;
    if (queryObject) {
      NTSTATUS queryStatus = queryObject(child, 0 /* ObjectBasicInformation */,
          &basicInfo, (ULONG)sizeof(basicInfo), &returned);
      diagnosticNtQueryObjectStatus = (ULONG)queryStatus;
      if (queryStatus >= 0) diagnosticGrantedAccess = basicInfo.GrantedAccess;
    }
  }
`;
fn = fn.replace(openAnchor, queryBlock);
const resultAnchor = `  if (napi_create_object(env, &result) != napi_ok ||
`;
if (!fn.includes(resultAnchor)) throw new Error("result creation anchor missing");
fn = fn.replace(resultAnchor, `  if (napi_create_object(env, &result) != napi_ok ||
      napi_create_uint32(env, diagnosticGrantedAccess, &value) != napi_ok ||
      napi_set_named_property(env, result, "diagnosticGrantedAccess", value) != napi_ok ||
      napi_create_uint32(env, diagnosticNtQueryObjectStatus, &value) != napi_ok ||
      napi_set_named_property(env, result, "diagnosticNtQueryObjectStatus", value) != napi_ok ||
`);
clone = clone.slice(0, fnStart) + fn + clone.slice(fnEnd);

const sourcePath = path.join(dir, "handle-relative-probe-phase1-rootapi-acl-diag.c");
const buildPath = path.join(dir, "build-raya-phase1-rootapi-acl-diag.cmd");
const outputPath = path.join(dir, "build", "Release", "napi_probe_raya_phase1_rootapi_acl_diag.node");
if (fs.existsSync(sourcePath) || fs.existsSync(buildPath) || fs.existsSync(outputPath)) {
  throw new Error("Refusing to overwrite an existing diagnostic artifact");
}
fs.writeFileSync(sourcePath, clone, "utf8");
const cmd = `@echo off
call "C:\\Program Files (x86)\\Microsoft Visual Studio\\18\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
if not exist "build\\Release" mkdir "build\\Release"
cl.exe /nologo /W4 /LD /I "C:\\Users\\Ali\\local-mcp-bridge\\.native-probe\\node-gyp-cache\\22.23.2\\include\\node" /DNODE_GYP_MODULE_NAME=napi_probe handle-relative-probe-phase1-rootapi-acl-diag.c /link /LIBPATH:"C:\\Users\\Ali\\local-mcp-bridge\\.native-probe\\node-gyp-cache\\22.23.2\\x64" node.lib /OUT:"build\\Release\\napi_probe_raya_phase1_rootapi_acl_diag.node"
exit /b %errorlevel%
`;
fs.writeFileSync(buildPath, cmd, "utf8");
process.stdout.write(JSON.stringify({sourcePath,buildPath,outputPath,sourceBytes:Buffer.byteLength(clone)},null,2));
