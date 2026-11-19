$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$server = Join-Path $root "raya-admin-panel\server.mjs"
$store = Join-Path $env:LOCALAPPDATA "RayaMCP"
$keyFile = Join-Path $store "control-plane-key.dpapi"
$tokenFile = Join-Path $store "panel-token.dpapi"
$node = (Get-Command node.exe -ErrorAction Stop).Source

function Test-PanelPort {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $task = $client.BeginConnect("127.0.0.1", 8766, $null, $null)
        if (-not $task.AsyncWaitHandle.WaitOne(500)) { return $false }
        $client.EndConnect($task)
        return $true
    } catch { return $false }
    finally { $client.Close() }
}

if (-not (Test-Path $node) -or -not (Test-Path $server)) {
    throw "Raya MCP prerequisites are missing."
}
New-Item -ItemType Directory -Force -Path $store | Out-Null

# Provision the API key only once; DPAPI binds it to this Windows account.
if (-not (Test-Path $keyFile)) {
    $credential = Get-Credential -UserName "Raya MCP" -Message "First-time setup: enter CONTROL_PLANE_API_KEY in Password. It will be encrypted for this Windows account."
    if ($null -eq $credential) { exit 2 }
    if ($credential.Password.Length -lt 16) { throw "The supplied API key is empty or unexpectedly short." }
    $encryptedKey = ConvertFrom-SecureString -SecureString $credential.Password
    [IO.File]::WriteAllText($keyFile, $encryptedKey, [Text.Encoding]::Unicode)
}

# Create one stable panel token; subsequent shortcut clicks reuse it.
if (-not (Test-Path $tokenFile)) {
    $bytes = New-Object byte[] 32
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
    $plainToken = ([BitConverter]::ToString($bytes) -replace "-", "").ToLowerInvariant()
    $secureToken = ConvertTo-SecureString -String $plainToken -AsPlainText -Force
    $encryptedToken = ConvertFrom-SecureString -SecureString $secureToken
    [IO.File]::WriteAllText($tokenFile, $encryptedToken, [Text.Encoding]::Unicode)
    $plainToken = $null
    $secureToken = $null
    [Array]::Clear($bytes, 0, $bytes.Length)
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
& icacls.exe $store /inheritance:r /grant:r "$($identity):(OI)(CI)F" | Out-Null
& icacls.exe $keyFile /inheritance:r /grant:r "$($identity):F" | Out-Null
& icacls.exe $tokenFile /inheritance:r /grant:r "$($identity):F" | Out-Null

$keySecure = ConvertTo-SecureString ([IO.File]::ReadAllText($keyFile))
$tokenSecure = ConvertTo-SecureString ([IO.File]::ReadAllText($tokenFile))
$keyPtr = [IntPtr]::Zero
$tokenPtr = [IntPtr]::Zero
try {
    $keyPtr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($keySecure)
    $tokenPtr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($tokenSecure)
    $env:CONTROL_PLANE_API_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($keyPtr)
    $env:RAYA_PANEL_TOKEN = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPtr)
    $plainToken = $env:RAYA_PANEL_TOKEN
    $plainTokenFile = Join-Path $store "panel-token.txt"
    [IO.File]::WriteAllText($plainTokenFile, $plainToken, [Text.Encoding]::ASCII)
    & icacls.exe $plainTokenFile /inheritance:r /grant:r "$($identity):F" | Out-Null
    $env:PATH = "C:\Program Files\nodejs;" + $env:PATH
    $panelUrl = "http://127.0.0.1:8766/?token=" + $plainToken

    if (Test-PanelPort) {
        Start-Process $panelUrl
        exit 0
    }

    $proc = Start-Process -FilePath $node -ArgumentList @($server) -WorkingDirectory $root -WindowStyle Hidden -PassThru
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        if (Test-PanelPort) { $ready = $true; break }
        if ($proc.HasExited) { break }
    }
    if (-not $ready) {
        throw "Raya Admin Panel failed to listen on port 8766. Check the panel process and its startup logs."
    }
    # server.mjs opens the browser after its listener is ready.
}
finally {
    if ($keyPtr -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($keyPtr) }
    if ($tokenPtr -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPtr) }
    Remove-Variable keySecure,tokenSecure,credential -ErrorAction SilentlyContinue
    $env:CONTROL_PLANE_API_KEY = $null
    $env:RAYA_PANEL_TOKEN = $null
}