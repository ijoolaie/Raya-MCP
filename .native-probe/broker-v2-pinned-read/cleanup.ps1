$ErrorActionPreference = 'Stop'
$svc = 'RayaFsBrokerV2Probe20261127'
$work = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $work 'broker-v2.exe'

$service = Get-Service -Name $svc -ErrorAction SilentlyContinue
if ($service) {
  if ($service.Status -ne 'Stopped') {
    Stop-Service -Name $svc -Force
    (Get-Service -Name $svc).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(20))
  }
  & sc.exe delete $svc
  if ($LASTEXITCODE -ne 0) { throw "sc delete failed: $LASTEXITCODE" }
}

$fixturePaths = @(
  'C:\ProgramData\RayaFsBrokerV2Probe-20261127',
  'C:\ProgramData\RayaFsBrokerV2Probe-20261127-original',
  'C:\ProgramData\RayaFsBrokerV2ProbeBin-20261127'
)
$hasFixture = $false
foreach ($path in $fixturePaths) { if (Test-Path -LiteralPath $path) { $hasFixture = $true } }
if ($hasFixture) {
  if (!(Test-Path -LiteralPath $exe)) { throw 'Broker v2 source executable missing; cannot safely clean fixture.' }
  & $exe --cleanup
  if ($LASTEXITCODE -ne 0) { throw "fixture/image cleanup failed: $LASTEXITCODE" }
}

Start-Sleep -Milliseconds 500
$service = Get-Service -Name $svc -ErrorAction SilentlyContinue
$expectedImage = 'C:\ProgramData\RayaFsBrokerV2ProbeBin-20261127\broker-v2.exe'
$procs = Get-CimInstance Win32_Process -Filter "Name='broker-v2.exe'" | Where-Object { $_.ExecutablePath -eq $expectedImage }
$paths = @(
  'C:\ProgramData\RayaFsBrokerV2Probe-20261127',
  'C:\ProgramData\RayaFsBrokerV2Probe-20261127-original',
  'C:\ProgramData\RayaFsBrokerV2ProbeBin-20261127'
)
if ($service) { throw 'Disposable v2 service still registered.' }
if ($procs) { throw ('Broker v2 process remains: ' + (($procs.ProcessId) -join ',')) }
foreach ($path in $paths) {
  if (Test-Path -LiteralPath $path) { throw "Disposable artifact remains: $path" }
}

$artifacts = @(
  'broker-v2.exe','broker-v2.obj',
  'configure-virtual-service-account.exe','configure-virtual-service-account.obj',
  'broker-v2-security-negative.exe','broker-v2-security-negative.obj'
)
foreach ($name in $artifacts) {
  $path = Join-Path $work $name
  if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}
Write-Output 'CLEANUP_VERIFIED service=absent brokerProcesses=0 fixture=absent renamedFixture=absent image=absent buildOutputs=absent'
