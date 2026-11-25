$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Node = (Get-Command node.exe -ErrorAction Stop).Source
if (-not $env:CONTROL_PLANE_API_KEY) {
    throw "CONTROL_PLANE_API_KEY is not set in this PowerShell session. The key value is never displayed."
}
& $Node (Join-Path $Root "raya-admin-panel\server.mjs")
exit $LASTEXITCODE
