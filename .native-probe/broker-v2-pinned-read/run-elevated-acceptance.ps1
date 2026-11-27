$ErrorActionPreference = 'Stop'
$work = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $work
try {
  & .\build.cmd
  if ($LASTEXITCODE -ne 0) { throw "Broker v2 build failed: $LASTEXITCODE" }
  & .\setup.ps1
  if ($LASTEXITCODE -ne 0) { throw "Broker v2 setup/test failed: $LASTEXITCODE" }
  Write-Output 'BROKER_V2_ACCEPTANCE_TESTS_COMPLETED'
}
finally {
  & .\cleanup.ps1
  if ($LASTEXITCODE -ne 0) { throw "Broker v2 cleanup failed: $LASTEXITCODE" }
  Pop-Location
}
