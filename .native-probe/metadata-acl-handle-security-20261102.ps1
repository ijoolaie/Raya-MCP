param()
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $env:TEMP ('raya-handle-sd-' + [guid]::NewGuid().ToString('N'))
$root = Join-Path $fixture 'root'
$parent = Join-Path $root 'parent'
$file = Join-Path $parent 'canary.txt'
New-Item -ItemType Directory -Path $parent -Force | Out-Null
Set-Content -LiteralPath $file -Value 'HANDLE-SECURITY-CANARY' -NoNewline
$sid = ([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value
$denyApplied = $false
try {
  & icacls.exe $file /deny ("*" + $sid + ":(RA)") | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "icacls deny failed: $LASTEXITCODE" }
  $denyApplied = $true
  $aclText = (& icacls.exe $file) -join "\n"
  $source = @'
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)]
public struct RayaMapping { public uint GenericRead, GenericWrite, GenericExecute, GenericAll; }
[StructLayout(LayoutKind.Sequential)]
public struct RayaByHandleInfo {
 public uint FileAttributes; public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime, LastAccessTime, LastWriteTime;
 public uint VolumeSerialNumber, FileSizeHigh, FileSizeLow, NumberOfLinks, FileIndexHigh, FileIndexLow;
}
public static class RayaHandleSecurity {
 [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 static extern uint GetNamedSecurityInfoW(string name,int type,uint info,out IntPtr owner,out IntPtr group,out IntPtr dacl,out IntPtr sacl,out IntPtr sd);
 [DllImport("advapi32.dll", SetLastError=true)]
 static extern uint GetSecurityInfo(IntPtr handle,int type,uint info,out IntPtr owner,out IntPtr group,out IntPtr dacl,out IntPtr sacl,out IntPtr sd);
 [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process,uint access,out IntPtr token);
 [DllImport("advapi32.dll", SetLastError=true)] static extern bool DuplicateToken(IntPtr existing,int level,out IntPtr duplicate);
 [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenThreadToken(IntPtr thread,uint access,bool openAsSelf,out IntPtr token);
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentThread();
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 static extern IntPtr CreateFileW(string path,uint desired,uint share,IntPtr sa,uint disposition,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(IntPtr h,out RayaByHandleInfo info);
 [DllImport("advapi32.dll",SetLastError=true)]
 static extern bool AccessCheck(IntPtr sd,IntPtr token,uint desired,ref RayaMapping map,IntPtr priv,ref uint privLen,out uint granted,out bool allowed);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr LocalFree(IntPtr p);
 static object Check(IntPtr sd,IntPtr token,uint mask,string label) {
  IntPtr p=Marshal.AllocHGlobal(4096); Marshal.Copy(new byte[4096],0,p,4096);
  try { uint len=4096,g=0; bool a=false; RayaMapping m=new RayaMapping { GenericRead=0x00120089,GenericWrite=0x00120116,GenericExecute=0x001200A0,GenericAll=0x001F01FF };
   bool ok=AccessCheck(sd,token,mask,ref m,p,ref len,out g,out a);
   return new {right=label,ok,allowed=ok&&a,granted=String.Format("0x{0:X8}",g),error=ok?0:Marshal.GetLastWin32Error()};
  } finally { Marshal.FreeHGlobal(p); }
 }
 public static object Run(string path) {
  IntPtr owner,group,dacl,sacl,sdPath;
  uint e=GetNamedSecurityInfoW(path,1,7,out owner,out group,out dacl,out sacl,out sdPath);
  if(e!=0) throw new Exception("GetNamedSecurityInfo failed "+e);
  IntPtr primary=IntPtr.Zero,token=IntPtr.Zero,threadToken=IntPtr.Zero,handle=IntPtr.Zero,sdHandle=IntPtr.Zero;
  try {
   OpenProcessToken(GetCurrentProcess(),0x0002,out primary);
   DuplicateToken(primary,2,out token);
   bool hasThreadToken=OpenThreadToken(GetCurrentThread(),0x0008,true,out threadToken);
   int threadTokenError=hasThreadToken?0:Marshal.GetLastWin32Error();
   object[] opens=new object[2]; uint[] masks={0x80,0x00020080};
   for(int i=0;i<2;i++) {
    IntPtr h=CreateFileW(path,masks[i],7,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
    if(h==new IntPtr(-1)) { opens[i]=new {mask=String.Format("0x{0:X8}",masks[i]),opened=false,error=Marshal.GetLastWin32Error()}; continue; }
    RayaByHandleInfo fi; bool fiOk=GetFileInformationByHandle(h,out fi);
    if(i==1) {
     handle=h;
     IntPtr o,g,d,s,sd;
     uint ge=GetSecurityInfo(h,1,7,out o,out g,out d,out s,out sd);
     if(ge==0) sdHandle=sd;
     opens[i]=new {mask=String.Format("0x{0:X8}",masks[i]),opened=true,fileInfoOk=fiOk,volume=fi.VolumeSerialNumber,fileIndex=String.Format("{0:X8}{1:X8}",fi.FileIndexHigh,fi.FileIndexLow),getSecurityInfoError=ge,handleSecurityDescriptorAvailable=ge==0};
    } else {
     opens[i]=new {mask=String.Format("0x{0:X8}",masks[i]),opened=true,fileInfoOk=fiOk,volume=fi.VolumeSerialNumber,fileIndex=String.Format("{0:X8}{1:X8}",fi.FileIndexHigh,fi.FileIndexLow),closeSucceeded=CloseHandle(h)};
    }
   }
   object[] checksPath={Check(sdPath,token,0x80,"path FILE_READ_ATTRIBUTES"),Check(sdPath,token,0x00020000,"path READ_CONTROL")};
   object[] checksHandle=sdHandle==IntPtr.Zero?new object[0]:new object[]{Check(sdHandle,token,0x80,"handle-SD FILE_READ_ATTRIBUTES"),Check(sdHandle,token,0x00020000,"handle-SD READ_CONTROL")};
   return new {userSid=System.Security.Principal.WindowsIdentity.GetCurrent().User.Value,threadHasImpersonationToken=hasThreadToken,threadTokenOpenError=threadTokenError,pathAccessChecks=checksPath,openResults=opens,handleSecurityDescriptorAccessChecks=checksHandle};
  } finally {
   if(handle!=IntPtr.Zero) CloseHandle(handle);
   if(sdHandle!=IntPtr.Zero) LocalFree(sdHandle);
   if(token!=IntPtr.Zero) CloseHandle(token);
   if(primary!=IntPtr.Zero) CloseHandle(primary);
   if(threadToken!=IntPtr.Zero) CloseHandle(threadToken);
   if(sdPath!=IntPtr.Zero) LocalFree(sdPath);
  }
 }
}
'@
  Add-Type -TypeDefinition $source -Language CSharp
  $probe = [RayaHandleSecurity]::Run($file)
  [pscustomobject]@{aclListing=$aclText;probe=$probe} | ConvertTo-Json -Depth 10
}
finally {
  if ($denyApplied) { & icacls.exe $file /remove:d ("*" + $sid) | Out-Null }
  Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}
