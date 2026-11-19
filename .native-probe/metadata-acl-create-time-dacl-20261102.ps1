$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$addon = Join-Path $here 'build\Release\napi_probe_raya_phase1_rootapi_acl_diag.node'
if (!(Test-Path -LiteralPath $addon)) { throw "Diagnostic addon not found: $addon" }
$fixture = Join-Path $env:TEMP ('raya-acl-create-time-' + [guid]::NewGuid().ToString('N'))
$root = Join-Path $fixture 'root'
$parentPath = Join-Path $root 'parent'
$file = Join-Path $parentPath 'protected-at-create.txt'
New-Item -ItemType Directory -Path $parentPath -Force | Out-Null
$sid = ([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value
$source = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
[StructLayout(LayoutKind.Sequential)] public struct RayaSecAttr { public int nLength; public IntPtr lpSecurityDescriptor; public int bInheritHandle; }
[StructLayout(LayoutKind.Sequential)] public struct RayaUnicodeString { public ushort Length, MaximumLength; public IntPtr Buffer; }
[StructLayout(LayoutKind.Sequential)] public struct RayaObjectAttributes { public int Length; public IntPtr RootDirectory, ObjectName; public uint Attributes; public IntPtr SecurityDescriptor, SecurityQualityOfService; }
[StructLayout(LayoutKind.Sequential)] public struct RayaIoStatusBlock { public IntPtr Status; public UIntPtr Information; }
[StructLayout(LayoutKind.Sequential)] public struct RayaObjectBasicInfo { public uint Attributes, GrantedAccess, HandleCount, PointerCount, PagedPoolUsage, NonPagedPoolUsage; [MarshalAs(UnmanagedType.ByValArray, SizeConst=3)] public uint[] Reserved; public uint NameInfoSize, TypeInfoSize, SecurityDescriptorSize; public long CreateTime; }
public static class RayaCreateTimeAcl {
 [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool ConvertStringSecurityDescriptorToSecurityDescriptorW(string s, uint rev, out IntPtr sd, out uint size);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateFileW(string p, uint a, uint share, ref RayaSecAttr sa, uint creation, uint flags, IntPtr template);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="CreateFileW")] static extern IntPtr CreateFileW(string p, uint a, uint share, IntPtr sa, uint creation, uint flags, IntPtr template);
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr LocalFree(IntPtr p);
 [DllImport("ntdll.dll")] static extern int NtCreateFile(out IntPtr h, uint access, ref RayaObjectAttributes oa, out RayaIoStatusBlock iosb, IntPtr alloc, uint attrs, uint share, uint disp, uint opts, IntPtr ea, uint eaLen);
 [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h, int cls, IntPtr info, uint len, out uint ret);
 static string Hex(uint x) { return "0x" + x.ToString("X8"); }
 static object Query(IntPtr h) {
  int n=Marshal.SizeOf(typeof(RayaObjectBasicInfo)); IntPtr p=Marshal.AllocHGlobal(n);
  try { uint ret; int s=NtQueryObject(h,0,p,(uint)n,out ret); if(s<0)return new{status=Hex((uint)s),grantedAccess=(string)null}; var x=(RayaObjectBasicInfo)Marshal.PtrToStructure(p,typeof(RayaObjectBasicInfo)); return new{status=Hex((uint)s),grantedAccess=Hex(x.GrantedAccess)}; }
  finally { Marshal.FreeHGlobal(p); }
 }
 static object Win32(string path,uint access) {
  IntPtr h=CreateFileW(path,access,7,IntPtr.Zero,3,0x00200080,IntPtr.Zero);
  if(h==new IntPtr(-1)) return new{opened=false,error=Marshal.GetLastWin32Error(),grantedAccess=(string)null};
  try { return new{opened=true,error=0,handle=Query(h)}; } finally { CloseHandle(h); }
 }
 static object Nt(string pathOrLeaf,IntPtr root,uint access) {
  string nt=root==IntPtr.Zero ? @"\??\"+pathOrLeaf : pathOrLeaf;
  IntPtr chars=Marshal.StringToHGlobalUni(nt), usp=IntPtr.Zero, h=IntPtr.Zero;
  try {
   RayaUnicodeString us=new RayaUnicodeString{Length=(ushort)(nt.Length*2),MaximumLength=(ushort)(nt.Length*2),Buffer=chars};
   usp=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(RayaUnicodeString))); Marshal.StructureToPtr(us,usp,false);
   RayaObjectAttributes oa=new RayaObjectAttributes{Length=Marshal.SizeOf(typeof(RayaObjectAttributes)),RootDirectory=root,ObjectName=usp,Attributes=0x1040};
   RayaIoStatusBlock iosb; uint opts=(access & 0x00100000)!=0 ? 0x00200060u : 0u; int st=NtCreateFile(out h,access,ref oa,out iosb,IntPtr.Zero,0x80,7,1,opts,IntPtr.Zero,0);
   if(st<0)return new{opened=false,status=Hex((uint)st),handle=(object)null};
   try{return new{opened=true,status=Hex((uint)st),handle=Query(h)};}finally{CloseHandle(h);h=IntPtr.Zero;}
  } finally {if(h!=IntPtr.Zero)CloseHandle(h);if(usp!=IntPtr.Zero)Marshal.FreeHGlobal(usp);Marshal.FreeHGlobal(chars);}
 }
 public static object CreateAndProbe(string file,string sid) {
  string sddl="D:P(D;;0x00000080;;;"+sid+")(A;;0x00120116;;;"+sid+")(A;;0x00100000;;;"+sid+")(A;;FA;;;SY)";
  IntPtr sd=IntPtr.Zero;
  try {
   uint size; if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,1,out sd,out size))throw new Exception("SDDL conversion failed: "+Marshal.GetLastWin32Error());
   RayaSecAttr sa=new RayaSecAttr{nLength=Marshal.SizeOf(typeof(RayaSecAttr)),lpSecurityDescriptor=sd,bInheritHandle=0};
   IntPtr h=CreateFileW(file,0x40000000,7,ref sa,1,0x80,IntPtr.Zero);
   if(h==new IntPtr(-1))throw new Exception("Create protected file failed: "+Marshal.GetLastWin32Error());
   CloseHandle(h);
   IntPtr parent=CreateFileW(System.IO.Path.GetDirectoryName(file),0x00100001,7,IntPtr.Zero,3,0x02200000,IntPtr.Zero);
   if(parent==new IntPtr(-1))throw new Exception("Open parent failed: "+Marshal.GetLastWin32Error());
   try {
    return new {
     sddl=sddl,
     win32_ra=Win32(file,0x80),
     win32_ra_sync=Win32(file,0x00100080),
     nt_absolute_ra=Nt(file,IntPtr.Zero,0x80),
     nt_absolute_ra_sync=Nt(file,IntPtr.Zero,0x00100080),
     nt_relative_ra=Nt(System.IO.Path.GetFileName(file),parent,0x80),
     nt_relative_ra_sync=Nt(System.IO.Path.GetFileName(file),parent,0x00100080)
    };
   } finally {CloseHandle(parent);}
  } finally {if(sd!=IntPtr.Zero)LocalFree(sd);}
 }
}
'@
try {
  Add-Type -TypeDefinition $source -Language CSharp
  $native = [RayaCreateTimeAcl]::CreateAndProbe($file,$sid)
  $acl = (& icacls.exe $file) -join "`n"
  $env:RAYA_ACL_ROOT = $root
  $env:RAYA_ACL_ADDON = $addon
  $nodeCode = @'
const a=require(process.env.RAYA_ACL_ADDON);
const root=process.env.RAYA_ACL_ROOT;
const p=a.openParentRelative(root,"parent");
try {
 const m=a.inspectEntryMetadata(p,"protected-at-create.txt");
 const c=(()=>{try{return {succeeded:true,value:a.readChildFromParent(p,"protected-at-create.txt",1024).content.toString()}}catch(e){return {succeeded:false,error:String(e.message||e)}}})();
 process.stdout.write(JSON.stringify({metadataSucceeded:true,metadataGrantedAccess:"0x"+m.diagnosticGrantedAccess.toString(16).padStart(8,"0"),ntQueryObjectStatus:"0x"+m.diagnosticNtQueryObjectStatus.toString(16).padStart(8,"0"),numberOfLinks:m.numberOfLinks,size:m.size,contentRead:c}));
} catch(e) { process.stdout.write(JSON.stringify({metadataSucceeded:false,error:String(e.message||e)})); process.exitCode=2; }
finally { a.closeParentHandle(p); }
'@
  $nodeEncoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($nodeCode))
  $nodeResult = & node -e "eval(Buffer.from('$nodeEncoded','base64').toString())"
  $nodeExit = $LASTEXITCODE
  [pscustomobject]@{
    ok = ($nodeExit -eq 0)
    integrity = (whoami /groups | Select-String 'S-1-16-' | Select-Object -First 1).ToString().Trim()
    userSid = $sid
    file = $file
    protectedDaclAppliedAtCreate = $true
    inheritance = 'D:P (protected DACL; no inherited ACEs)'
    acl = $acl
    nativeOpenMatrix = $native
    actualRootApi = ($nodeResult | ConvertFrom-Json)
    nodeExitCode = $nodeExit
    cleanup = 'fixture is removed in finally'
    scope = 'disposable local NTFS file; DACL supplied in SECURITY_ATTRIBUTES during CREATE_NEW; all test handles closed; diagnostic clone only; no workspace/global ACL or production changes'
  } | ConvertTo-Json -Depth 12
} finally {
  Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item Env:RAYA_ACL_ROOT -ErrorAction SilentlyContinue
  Remove-Item Env:RAYA_ACL_ADDON -ErrorAction SilentlyContinue
}
