$ErrorActionPreference = 'Stop'
$svc = 'RayaFsBrokerV2Probe20261127'
$root = 'C:\ProgramData\RayaFsBrokerV2Probe-20261127'
$rootOld = 'C:\ProgramData\RayaFsBrokerV2Probe-20261127-original'
$configRoot = 'C:\ProgramData\RayaFsBrokerV2ProbeConfig-20261127'
$bin = 'C:\ProgramData\RayaFsBrokerV2ProbeBin-20261127'
$work = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $work 'broker-v2.exe'
$config = Join-Path $work 'configure-virtual-service-account.exe'
$security = Join-Path $work 'broker-v2-security-negative.exe'

if (Get-Service -Name $svc -ErrorAction SilentlyContinue) { throw 'Disposable v2 service already exists; refusing to overwrite.' }
if ((Test-Path -LiteralPath $root) -or (Test-Path -LiteralPath $rootOld) -or (Test-Path -LiteralPath $configRoot) -or (Test-Path -LiteralPath $bin)) {
  throw 'Disposable v2 root/image path already exists; refusing to overwrite.'
}
if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $config) -or !(Test-Path -LiteralPath $security)) {
  throw 'Build outputs missing; run build.cmd first.'
}

try {
  & sc.exe create $svc binPath= ('"' + $exe + '" --service') start= demand
  if ($LASTEXITCODE -ne 0) { throw "sc create failed: $LASTEXITCODE" }

  & $config
  if ($LASTEXITCODE -ne 0) { throw "service virtual-account configuration failed: $LASTEXITCODE" }

  & sc.exe sidtype $svc unrestricted
  if ($LASTEXITCODE -ne 0) { throw "sc sidtype failed: $LASTEXITCODE" }

  & $exe --prepare
  if ($LASTEXITCODE -ne 0) { throw "fixture preparation failed: $LASTEXITCODE" }

  & $exe --install-image
  if ($LASTEXITCODE -ne 0) { throw "protected image installation failed: $LASTEXITCODE" }

  & sc.exe config $svc binPath= ('"' + (Join-Path $bin 'broker-v2.exe') + '" --service')
  if ($LASTEXITCODE -ne 0) { throw "service image path update failed: $LASTEXITCODE" }

  Start-Service -Name $svc
  (Get-Service -Name $svc).WaitForStatus('Running', [TimeSpan]::FromSeconds(20))
  $info = Get-CimInstance Win32_Service -Filter "Name='$svc'"
  if ($info.StartName -ne ('NT SERVICE\' + $svc) -or $info.State -ne 'Running') {
    throw "service identity/state mismatch: StartName=$($info.StartName), State=$($info.State)"
  }
  $info | Select-Object Name,State,StartName,ProcessId,ExitCode | Format-List

  & $exe --client-test
  if ($LASTEXITCODE -ne 0) { throw "initial Broker v2 protocol suite failed: $LASTEXITCODE" }

  # The independent policy directory must reject an over-broad DACL as well.
  Stop-Service -Name $svc
  (Get-Service -Name $svc).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(20))
  & icacls.exe $configRoot /grant '*S-1-1-0:(OI)(CI)(M)'
  if ($LASTEXITCODE -ne 0) { throw "could not install deliberately broad config ACE: $LASTEXITCODE" }
  & sc.exe start $svc | Out-Null
  Start-Sleep -Seconds 2
  if ((Get-Service -Name $svc).Status -ne 'Stopped') {
    throw 'Broker accepted a config DACL containing an Everyone Modify ACE.'
  }
  Write-Output 'PASS over-broad independent config DACL rejected'
  & icacls.exe $configRoot /remove:g '*S-1-1-0'
  if ($LASTEXITCODE -ne 0) { throw "could not remove deliberately broad config ACE: $LASTEXITCODE" }
  Start-Service -Name $svc
  (Get-Service -Name $svc).WaitForStatus('Running', [TimeSpan]::FromSeconds(20))
  & $exe --client-test
  if ($LASTEXITCODE -ne 0) { throw "Broker v2 protocol suite failed after restoring exact config DACL: $LASTEXITCODE" }
  # The broker must reject a protected but over-broad DACL, not merely check
  # that the DACL is present and marked protected.
  Stop-Service -Name $svc
  (Get-Service -Name $svc).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(20))
  & icacls.exe $root /grant '*S-1-1-0:(OI)(CI)(M)'
  if ($LASTEXITCODE -ne 0) { throw "could not install deliberately broad test ACE: $LASTEXITCODE" }
  & sc.exe start $svc | Out-Null
  Start-Sleep -Seconds 2
  if ((Get-Service -Name $svc).Status -ne 'Stopped') {
    throw 'Broker accepted a protected root DACL containing an Everyone Modify ACE.'
  }
  Write-Output 'PASS over-broad protected root DACL rejected'
  & icacls.exe $root /remove:g '*S-1-1-0'
  if ($LASTEXITCODE -ne 0) { throw "could not remove deliberately broad test ACE: $LASTEXITCODE" }
  Start-Service -Name $svc
  (Get-Service -Name $svc).WaitForStatus('Running', [TimeSpan]::FromSeconds(20))
  & $exe --client-test
  if ($LASTEXITCODE -ne 0) { throw "Broker v2 protocol suite failed after restoring exact root DACL: $LASTEXITCODE" }

  & $security
  if ($LASTEXITCODE -ne 0) { throw "restricted-token ACL/hard-link denial suite failed: $LASTEXITCODE" }

  # Adversarial root-path replacement while the service retains its pinned root HANDLE.
  Move-Item -LiteralPath $root -Destination $rootOld
  New-Item -ItemType Directory -Path $root | Out-Null
  Set-Content -LiteralPath (Join-Path $root 'payload.txt') -NoNewline -Value 'REPLACEMENT-PATH-DECOY'
  & $exe --client-test
  if ($LASTEXITCODE -ne 0) { throw "pinned-root protocol suite failed after path replacement: $LASTEXITCODE" }

  Write-Output 'BROKER_V2_PINNED_ROOT_ACCEPTANCE_PASS'
}
catch {
  $failure = $_
  try { & (Join-Path $work 'cleanup.ps1') } catch { Write-Output "CLEANUP_FAILURE: $_" }
  throw $failure
}
