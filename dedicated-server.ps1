# Perfect Dark 2 - Dedicated Server Launcher
# Run from the project root directory

param(
    [int]$Port = 27100,
    [int]$MaxClients = 8,
    [int]$Profile = 0
)

$Host.UI.RawUI.WindowTitle = "Perfect Dark 2 - Dedicated Server"

Write-Host ""
Write-Host "  ============================================" -ForegroundColor Cyan
Write-Host "    Perfect Dark 2 - Dedicated Server v0.0.2" -ForegroundColor Cyan
Write-Host "  ============================================" -ForegroundColor Cyan
Write-Host ""

# Detect public IP
Write-Host "  Detecting public IP..." -ForegroundColor Gray
try {
    $publicIP = (Invoke-WebRequest -Uri "https://api.ipify.org" -UseBasicParsing -TimeoutSec 5).Content.Trim()
} catch {
    $publicIP = $null
}

# Detect local IP
try {
    $localIP = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.InterfaceAlias -notlike "*Loopback*" -and $_.PrefixOrigin -ne "WellKnown" } | Select-Object -First 1).IPAddress
} catch {
    $localIP = "unknown"
}

Write-Host ""
Write-Host "  ============================================" -ForegroundColor Green
if ($publicIP) {
    Write-Host "    PUBLIC IP:  $publicIP" -ForegroundColor White
    Write-Host "    LOCAL IP:   $localIP" -ForegroundColor Gray
    Write-Host "    PORT:       $Port" -ForegroundColor White
    Write-Host ""
    Write-Host "    Connect:    $publicIP`:$Port" -ForegroundColor Yellow
} else {
    Write-Host "    LOCAL IP:   $localIP" -ForegroundColor White
    Write-Host "    PORT:       $Port" -ForegroundColor White
    Write-Host "    PUBLIC IP:  (could not detect)" -ForegroundColor Red
    Write-Host ""
    Write-Host "    LAN Connect: $localIP`:$Port" -ForegroundColor Yellow
    Write-Host "    Check https://whatismyip.com for public IP" -ForegroundColor Gray
}
Write-Host "    Max Players: $MaxClients" -ForegroundColor Gray
Write-Host "  ============================================" -ForegroundColor Green
Write-Host ""

# Port forward reminder
Write-Host "  NAT punch-through is used for connectivity." -ForegroundColor DarkYellow
Write-Host ""

# Check if build exists
$exePath = Join-Path $PSScriptRoot "build\pd.x86_64.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "  ERROR: build\pd.x86_64.exe not found!" -ForegroundColor Red
    Write-Host "  Build the project first, then re-run this script." -ForegroundColor Red
    Read-Host "  Press Enter to exit"
    exit 1
}

# Copy connection info to clipboard
if ($publicIP) {
    $connectStr = "$publicIP`:$Port"
    Set-Clipboard -Value $connectStr
    Write-Host "  Connection address copied to clipboard!" -ForegroundColor Green
    Write-Host ""
}

Write-Host "  Starting server..." -ForegroundColor Cyan
Write-Host "  (Close this window or press Ctrl+C to stop)" -ForegroundColor Gray
Write-Host ""

# Launch
Push-Location (Join-Path $PSScriptRoot "build")
& ".\pd.x86_64.exe" --host --port $Port --maxclients $MaxClients --file $Profile
Pop-Location

Write-Host ""
Write-Host "  Server stopped." -ForegroundColor Yellow
Read-Host "  Press Enter to exit"
