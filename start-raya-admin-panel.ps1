param()
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root
if (-not $env:CONTROL_PLANE_API_KEY) {
    throw "CONTROL_PLANE_API_KEY is not set in this PowerShell session. Run this script from the same session used for the Raya tunnel; the key value is never displayed."
}
$node = (Get-Command node.exe -ErrorAction Stop).Source
& $node (Join-Path $root "raya-admin-panel\server.mjs")
exit $LASTEXITCODE