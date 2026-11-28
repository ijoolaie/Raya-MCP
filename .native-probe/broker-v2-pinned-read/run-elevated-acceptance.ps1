$ErrorActionPreference = 'Stop'
$work = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $work
try {
  & .\build.cmd
  if ($LASTEXITCODE -ne 0) { throw "Broker v2 build failed: $LASTEXITCODE" }
  & .\broker-v2.exe --peer-auth-test
  if ($LASTEXITCODE -ne 0) { throw "Broker peer identity integration test failed: $LASTEXITCODE" }
  & .\broker-v2.exe --io-cancel-test
  if ($LASTEXITCODE -ne 0) { throw "Broker kernel-backed I/O cancellation test failed: $LASTEXITCODE" }
  & .\setup.ps1
  if ($LASTEXITCODE -ne 0) { throw "Broker v2 setup/test failed: $LASTEXITCODE" }
  Write-Output 'BROKER_V2_ACCEPTANCE_TESTS_COMPLETED'
}
finally {
  & .\cleanup.ps1
  if ($LASTEXITCODE -ne 0) { throw "Broker v2 cleanup failed: $LASTEXITCODE" }
  Pop-Location
}
