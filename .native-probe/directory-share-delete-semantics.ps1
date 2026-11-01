$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NativeShareTest4 {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern IntPtr CreateFileW(string n,uint a,uint s,IntPtr sec,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)]
 [return:MarshalAs(UnmanagedType.Bool)] public static extern bool CloseHandle(IntPtr h);
}
'@
$LIST_DIRECTORY=0x1;$DELETE_ACCESS=0x10000;$SHARE_RW=3;$SHARE_ALL=7;$OPEN_EXISTING=3;$DIR_FLAGS=0x02000000 -bor 0x00200000
function Open-Dir([string]$p,[uint32]$access,[uint32]$share) {
  $h=[NativeShareTest4]::CreateFileW($p,$access,$share,[IntPtr]::Zero,$OPEN_EXISTING,$DIR_FLAGS,[IntPtr]::Zero)
  if($h -eq [IntPtr](-1)){throw "open failed for $p (access=$access share=$share): $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"}
  return $h
}
function Try-Move([string]$src,[string]$dst) {
  try {[IO.Directory]::Move($src,$dst);return "UNEXPECTED_SUCCESS"} catch {return "blocked"}
}
$base=Join-Path $PSScriptRoot ("share-delete-fixture-"+[guid]::NewGuid().ToString("N"))
$ancestor=Join-Path $base "ancestor";$root=Join-Path $ancestor "root";$outside=Join-Path $base "outside"
New-Item -ItemType Directory -Path $root -Force|Out-Null;New-Item -ItemType Directory -Path $outside|Out-Null
$out=[ordered]@{}
try {
  # Root-only restrictive handle: determine whether its containing ancestor can move.
  $hRoot=Open-Dir $root $LIST_DIRECTORY $SHARE_RW
  try {$out.move_ancestor_with_only_root_locked=Try-Move $ancestor (Join-Path $outside "ancestor-with-root-only")}
  finally {[void][NativeShareTest4]::CloseHandle($hRoot)}
  # Recreate after possible move.
  if(-not (Test-Path -LiteralPath $ancestor)){[IO.Directory]::Move((Join-Path $outside "ancestor-with-root-only"),$ancestor)}
  # Lock each ancestor in the configured path.
  $hAncestor=Open-Dir $ancestor $LIST_DIRECTORY $SHARE_RW
  $hRoot=Open-Dir $root $LIST_DIRECTORY $SHARE_RW
  try {
    $out.move_root_with_chain_locked=Try-Move $root (Join-Path $outside "root-with-chain")
    $out.move_ancestor_with_chain_locked=Try-Move $ancestor (Join-Path $outside "ancestor-with-chain")
  } finally {[void][NativeShareTest4]::CloseHandle($hRoot);[void][NativeShareTest4]::CloseHandle($hAncestor)}
  # Pre-existing DELETE-capable handle must prevent restrictive open.
  $hDelete=Open-Dir $root $DELETE_ACCESS $SHARE_ALL
  try {
    $hRestrictive=[NativeShareTest4]::CreateFileW($root,$LIST_DIRECTORY,$SHARE_RW,[IntPtr]::Zero,$OPEN_EXISTING,$DIR_FLAGS,[IntPtr]::Zero)
    $out.restrictive_open_with_preexisting_delete_handle=($hRestrictive -eq [IntPtr](-1))
    $out.preexisting_delete_conflict_error=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
    if($hRestrictive -ne [IntPtr](-1)){[void][NativeShareTest4]::CloseHandle($hRestrictive)}
  } finally {[void][NativeShareTest4]::CloseHandle($hDelete)}
  $ok=($out.move_root_with_chain_locked -eq "blocked" -and $out.move_ancestor_with_chain_locked -eq "blocked" -and $out.restrictive_open_with_preexisting_delete_handle -eq $true -and $out.preexisting_delete_conflict_error -eq 32)
  [pscustomobject]@{ok=$ok;results=$out}|ConvertTo-Json -Depth 5
  if(-not $ok){exit 2}
} finally {Remove-Item $base -Recurse -Force -ErrorAction SilentlyContinue}