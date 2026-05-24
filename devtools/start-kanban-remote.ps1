param(
    [switch]$Stop,
    [switch]$Status,
    [switch]$InstallStartupTask,
    [switch]$RemoveStartupTask,
    [switch]$ReuseToken,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ScriptPath = $PSCommandPath
$StateDir = Join-Path $ProjectRoot ".claude\scratch\kanban-remote"
$PidPath = Join-Path $StateDir "pids.json"
$TokenPath = Join-Path $StateDir "token.txt"
$UrlPath = Join-Path $StateDir "remote-url.txt"
$ServerOutPath = Join-Path $StateDir "kanban-server.out.log"
$ServerErrPath = Join-Path $StateDir "kanban-server.err.log"
$TunnelOutPath = Join-Path $StateDir "cloudflared.out.log"
$TunnelErrPath = Join-Path $StateDir "cloudflared.err.log"
$PreferredPort = 7531
$Port = $PreferredPort
$TargetUrl = "http://127.0.0.1:$Port"
$TaskName = "PD2 Remote Kanban"

function Show-Usage {
    Write-Host "Starts the PD2 Kanban server and a Cloudflare localhost tunnel for phone access."
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  powershell -File devtools\start-kanban-remote.ps1"
    Write-Host "  powershell -File devtools\start-kanban-remote.ps1 -Stop"
    Write-Host "  powershell -File devtools\start-kanban-remote.ps1 -Status"
    Write-Host "  powershell -File devtools\start-kanban-remote.ps1 -InstallStartupTask"
    Write-Host ""
    Write-Host "Safety:"
    Write-Host "  Only forwards the selected 127.0.0.1 Kanban HTTP target through cloudflared tunnel."
    Write-Host "  Does not set a system proxy, enable WARP, configure an exit node, or change routes."
}

function Ensure-StateDir {
    if (-not (Test-Path -LiteralPath $StateDir)) {
        New-Item -ItemType Directory -Force -Path $StateDir | Out-Null
    }
}

function Resolve-CommandPath {
    param(
        [string]$Name,
        [string[]]$Candidates = @()
    )
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

function Resolve-Python {
    return Resolve-CommandPath "python.exe" @(
        "C:\Python312\python.exe",
        "C:\Python311\python.exe",
        "C:\msys64\mingw64\bin\python.exe",
        "C:\msys64\usr\bin\python3.exe"
    )
}

function Resolve-Cloudflared {
    return Resolve-CommandPath "cloudflared.exe" @(
        (Join-Path $env:ProgramFiles "cloudflared\cloudflared.exe"),
        (Join-Path $env:LOCALAPPDATA "cloudflared\cloudflared.exe"),
        (Join-Path $env:USERPROFILE "cloudflared.exe"),
        (Join-Path $env:USERPROFILE "Downloads\Programs\cloudflared.exe"),
        (Join-Path $env:USERPROFILE "Downloads\Programs\cloudflared-windows-amd64.exe")
    )
}

function New-RemoteToken {
    $bytes = New-Object byte[] 32
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    return (($bytes | ForEach-Object { $_.ToString("x2") }) -join "")
}

function Get-RemoteToken {
    if ($ReuseToken -and (Test-Path -LiteralPath $TokenPath)) {
        $existing = (Get-Content -LiteralPath $TokenPath -Raw).Trim()
        if ($existing) {
            return $existing
        }
    }
    $token = New-RemoteToken
    [System.IO.File]::WriteAllText($TokenPath, $token, [System.Text.UTF8Encoding]::new($false))
    return $token
}

function Test-PortOpen {
    param([int]$PortNumber)
    $client = $null
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $iar = $client.BeginConnect("127.0.0.1", $PortNumber, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(250, $false)) {
            return $false
        }
        $client.EndConnect($iar)
        return $client.Connected
    } catch {
        return $false
    } finally {
        if ($client) {
            try { $client.Close() } catch {}
        }
    }
}

function Test-PortBindable {
    param([int]$PortNumber)
    $listener = $null
    try {
        $address = [System.Net.IPAddress]::Parse("127.0.0.1")
        $listener = [System.Net.Sockets.TcpListener]::new($address, $PortNumber)
        $listener.Start()
        return $true
    } catch {
        return $false
    } finally {
        if ($listener) {
            try { $listener.Stop() } catch {}
        }
    }
}

function Set-RemoteTarget {
    param([int]$PortNumber)
    $script:Port = $PortNumber
    $script:TargetUrl = "http://127.0.0.1:$PortNumber"
}

function Select-RemotePort {
    for ($candidate = $PreferredPort; $candidate -le ($PreferredPort + 8); $candidate++) {
        if ((-not (Test-PortOpen $candidate)) -and (Test-PortBindable $candidate)) {
            Set-RemoteTarget $candidate
            return
        }
    }
    throw "No free localhost port found in $PreferredPort-$($PreferredPort + 8). Stop an existing Kanban server or remote session, then retry."
}

function Read-PidState {
    if (-not (Test-Path -LiteralPath $PidPath)) {
        return $null
    }
    try {
        return Get-Content -LiteralPath $PidPath -Raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Test-PidAlive {
    param([int]$ProcessId)
    if ($ProcessId -le 0) {
        return $false
    }
    try {
        $p = Get-Process -Id $ProcessId -ErrorAction Stop
        return -not $p.HasExited
    } catch {
        return $false
    }
}

function Stop-Pid {
    param([int]$ProcessId, [string]$Name)
    if (-not (Test-PidAlive $ProcessId)) {
        return
    }
    Write-Host "Stopping $Name pid=$ProcessId"
    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

function Stop-RemoteKanban {
    $state = Read-PidState
    if ($null -eq $state) {
        Write-Host "No tracked remote Kanban session found."
        return
    }
    Stop-Pid ([int]$state.tunnel_pid) "cloudflared"
    Stop-Pid ([int]$state.server_pid) "kanban server"
    if (Test-Path -LiteralPath $PidPath) {
        Remove-Item -LiteralPath $PidPath -Force
    }
    Write-Host "Stopped tracked remote Kanban session."
}

function Show-Status {
    $state = Read-PidState
    if ($null -eq $state) {
        Write-Host "Remote Kanban is not tracked as running."
        return
    }
    Write-Host ("Kanban server: " + ($(if (Test-PidAlive ([int]$state.server_pid)) { "running" } else { "stopped" })) + " pid=" + $state.server_pid)
    Write-Host ("Cloudflare tunnel: " + ($(if (Test-PidAlive ([int]$state.tunnel_pid)) { "running" } else { "stopped" })) + " pid=" + $state.tunnel_pid)
    if (Test-Path -LiteralPath $UrlPath) {
        Write-Host ("Remote URL: " + (Get-Content -LiteralPath $UrlPath -Raw).Trim())
    }
}

function Install-StartupTask {
    $psExe = (Get-Command powershell.exe -ErrorAction Stop).Source
    $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$ScriptPath`" -ReuseToken"
    $action = New-ScheduledTaskAction -Execute $psExe -Argument $arg -WorkingDirectory $ProjectRoot
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $userId = if ($env:USERDOMAIN) { "$env:USERDOMAIN\$env:USERNAME" } else { $env:USERNAME }
    $principal = New-ScheduledTaskPrincipal -UserId $userId -LogonType Interactive -RunLevel LeastPrivilege
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Description "Starts PD2 remote Kanban localhost tunnel only." -Force | Out-Null
    Write-Host "Installed startup task '$TaskName'."
}

function Remove-StartupTask {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "Removed startup task '$TaskName' if it existed."
}

function Start-KanbanServer {
    param([string]$PythonExe, [string]$Token)
    if (Test-PortOpen $Port) {
        throw "Port $Port is already in use. Stop the existing Kanban server first so remote mode can enforce token auth."
    }

    $serverScript = Join-Path $ProjectRoot "tools\kanban\server.py"
    $psExe = (Get-Command powershell.exe -ErrorAction Stop).Source
    $startupCommand = @(
        "`$env:KANBAN_REMOTE_TOKEN = '$Token'",
        "`$env:KANBAN_IDLE_TIMEOUT_S = '0'",
        "`$env:KANBAN_HOST = '127.0.0.1'",
        "`$env:KANBAN_PORT = '$Port'",
        "Set-Location -LiteralPath '$ProjectRoot'",
        "& '$PythonExe' '$serverScript'"
    ) -join "; "
    $encodedStartupCommand = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($startupCommand))
    $proc = Start-Process -FilePath $psExe `
        -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", $encodedStartupCommand) `
        -WorkingDirectory $ProjectRoot `
        -WindowStyle Hidden `
        -RedirectStandardOutput $ServerOutPath `
        -RedirectStandardError $ServerErrPath `
        -PassThru

    $deadline = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $deadline) {
        if (Test-PortOpen $Port) {
            return $proc
        }
        if ($proc.HasExited) {
            throw "Kanban server exited early. See $ServerErrPath"
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Kanban server did not start on port $Port. See $ServerErrPath"
}

function Start-CloudflareTunnel {
    param([string]$CloudflaredExe)

    if (Test-Path -LiteralPath $TunnelOutPath) { Remove-Item -LiteralPath $TunnelOutPath -Force }
    if (Test-Path -LiteralPath $TunnelErrPath) { Remove-Item -LiteralPath $TunnelErrPath -Force }

    $proc = Start-Process -FilePath $CloudflaredExe `
        -ArgumentList @("tunnel", "--no-autoupdate", "--url", $TargetUrl) `
        -WorkingDirectory $ProjectRoot `
        -WindowStyle Hidden `
        -RedirectStandardOutput $TunnelOutPath `
        -RedirectStandardError $TunnelErrPath `
        -PassThru

    $deadline = (Get-Date).AddSeconds(45)
    $pattern = "https://[a-zA-Z0-9-]+\.trycloudflare\.com"
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) {
            throw "cloudflared exited early. See $TunnelErrPath"
        }
        $text = ""
        if (Test-Path -LiteralPath $TunnelOutPath) { $text += Get-Content -LiteralPath $TunnelOutPath -Raw -ErrorAction SilentlyContinue }
        if (Test-Path -LiteralPath $TunnelErrPath) { $text += Get-Content -LiteralPath $TunnelErrPath -Raw -ErrorAction SilentlyContinue }
        $m = [regex]::Match($text, $pattern)
        if ($m.Success) {
            return [PSCustomObject]@{ Process = $proc; Url = $m.Value }
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Timed out waiting for cloudflared public URL. See $TunnelErrPath"
}

if ($Help) {
    Show-Usage
    exit 0
}

Ensure-StateDir

if ($Stop) {
    Stop-RemoteKanban
    exit 0
}

if ($Status) {
    Show-Status
    exit 0
}

if ($RemoveStartupTask) {
    Remove-StartupTask
    exit 0
}

if ($InstallStartupTask) {
    Install-StartupTask
    exit 0
}

$existing = Read-PidState
if ($existing -and (Test-PidAlive ([int]$existing.server_pid)) -and (Test-PidAlive ([int]$existing.tunnel_pid))) {
    Write-Host "Remote Kanban is already running."
    Show-Status
    exit 0
}

$python = Resolve-Python
if (-not $python) {
    throw "Python was not found. Install Python or add python.exe to PATH."
}

$cloudflared = Resolve-Cloudflared
if (-not $cloudflared) {
    throw "cloudflared.exe was not found. Install Cloudflare cloudflared, then rerun this script."
}

Select-RemotePort
$token = Get-RemoteToken

Write-Host "Starting PD2 Kanban remote access."
Write-Host "Target: $TargetUrl only"
Write-Host "Routing: no system proxy, no WARP, no exit node, no route changes"

$serverProc = Start-KanbanServer -PythonExe $python -Token $token
try {
    $tunnel = Start-CloudflareTunnel -CloudflaredExe $cloudflared
} catch {
    Stop-Pid $serverProc.Id "kanban server"
    throw
}

$remoteUrl = $tunnel.Url.TrimEnd("/") + "/?token=" + [uri]::EscapeDataString($token)
[System.IO.File]::WriteAllText($UrlPath, $remoteUrl, [System.Text.UTF8Encoding]::new($false))

$pidState = [PSCustomObject]@{
    started_at = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    server_pid = $serverProc.Id
    tunnel_pid = $tunnel.Process.Id
    local_target = $TargetUrl
    tunnel_url = $tunnel.Url
    remote_url_file = $UrlPath
}
$pidState | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $PidPath -Encoding UTF8

Write-Host ""
Write-Host "Remote Kanban is running."
Write-Host "Open this on your phone:"
Write-Host $remoteUrl
Write-Host ""
Write-Host "Logs:"
Write-Host "  $ServerOutPath"
Write-Host "  $TunnelErrPath"
