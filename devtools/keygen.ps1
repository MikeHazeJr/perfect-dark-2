# keygen.ps1 -- Ed25519 keypair generator for the signed updater (SEC-6).
#
# Generates a fresh Ed25519 keypair using OpenSSL, writes the private key to
# dev-keys/ or release-keys/ (BOTH gitignored), and patches the embedded
# public key into port/include/updater_pubkey.h.
#
# Usage:
#   .\devtools\keygen.ps1                 # Development keypair (default)
#   .\devtools\keygen.ps1 -Production     # Production keypair (stored in release-keys/)
#
# IMPORTANT:
#   - The private key MUST NEVER be checked into git or copied to a build
#     server. It is the root of trust for the entire update channel.
#   - The production private key should live only on air-gapped storage.
#   - Re-running this script ROTATES the key. Clients still running older
#     builds with the previous embedded public key will refuse subsequent
#     releases signed with the new key.

param(
    [switch]$Production,
    [switch]$Force   # Overwrite existing key files without prompting
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path $PSScriptRoot -Parent
Set-Location $ProjectRoot

# Pick up MSYS2 mingw64 OpenSSL. build-env.sh already does this for bash;
# the prelude equivalent is _build-env-prelude.ps1.
$preludePath = Join-Path $PSScriptRoot "_build-env-prelude.ps1"
if (Test-Path $preludePath) { . $preludePath }

# --------------------------------------------------------------------------
# Resolve openssl binary
# --------------------------------------------------------------------------
$openssl = (Get-Command openssl -ErrorAction SilentlyContinue).Source
if (-not $openssl) {
    $openssl = "C:\msys64\mingw64\bin\openssl.exe"
    if (-not (Test-Path $openssl)) {
        Write-Host "ERROR: openssl not found. Install via 'pacman -S mingw-w64-x86_64-openssl'." -ForegroundColor Red
        exit 1
    }
}

# --------------------------------------------------------------------------
# Key directory
# --------------------------------------------------------------------------
$keyDir  = if ($Production) { "release-keys" } else { "dev-keys" }
$keyKind = if ($Production) { "PRODUCTION" } else { "DEVELOPMENT" }

if (-not (Test-Path $keyDir)) {
    New-Item -ItemType Directory -Path $keyDir | Out-Null
}

$privatePath = Join-Path $keyDir "ed25519-private.pem"
$publicPath  = Join-Path $keyDir "ed25519-public.pem"

if ((Test-Path $privatePath) -and -not $Force) {
    Write-Host "ERROR: $privatePath already exists. Re-run with -Force to rotate the key." -ForegroundColor Red
    Write-Host "       Rotating will invalidate the embedded public key in existing clients." -ForegroundColor Yellow
    exit 1
}

# --------------------------------------------------------------------------
# Generate
# --------------------------------------------------------------------------
Write-Host "Generating $keyKind Ed25519 keypair at $keyDir/" -ForegroundColor Cyan
& $openssl genpkey -algorithm ED25519 -out $privatePath
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: openssl genpkey failed." -ForegroundColor Red; exit 1 }

& $openssl pkey -in $privatePath -pubout -out $publicPath
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: openssl pkey (public export) failed." -ForegroundColor Red; exit 1 }

# Lock down the private key permissions on Windows (deny inheritance, grant
# only the current user). Best-effort — if icacls is unavailable we warn.
try {
    icacls $privatePath /inheritance:r /grant:r "$env:USERNAME:(R)" 2>&1 | Out-Null
} catch {
    Write-Host "  WARN: could not tighten file permissions on $privatePath" -ForegroundColor Yellow
}

# --------------------------------------------------------------------------
# Extract raw 32-byte public key from DER
#   DER structure: SubjectPublicKeyInfo { SEQUENCE, AlgoId (Ed25519 OID), BIT STRING }
#   The raw Ed25519 public key is always the last 32 bytes.
# --------------------------------------------------------------------------
$tmpDer = Join-Path $keyDir "ed25519-public.der"
& $openssl pkey -in $publicPath -pubin -outform DER -out $tmpDer
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: could not export DER public key." -ForegroundColor Red; exit 1 }

$derBytes = [System.IO.File]::ReadAllBytes($tmpDer)
Remove-Item $tmpDer -Force

if ($derBytes.Length -lt 32) {
    Write-Host "ERROR: DER output was only $($derBytes.Length) bytes — expected 44." -ForegroundColor Red
    exit 1
}
$rawKey = $derBytes[-32..-1]

# --------------------------------------------------------------------------
# Patch port/include/updater_pubkey.h
# --------------------------------------------------------------------------
$headerPath = "port/include/updater_pubkey.h"
if (-not (Test-Path $headerPath)) {
    Write-Host "ERROR: $headerPath not found — cannot patch." -ForegroundColor Red
    exit 1
}

$header = Get-Content $headerPath -Raw

$lines = @()
for ($i = 0; $i -lt 32; $i += 8) {
    $chunk = @()
    for ($j = 0; $j -lt 8 -and ($i + $j) -lt 32; $j++) {
        $chunk += ("0x{0:x2}" -f $rawKey[$i + $j])
    }
    $lines += "`t" + ($chunk -join ", ") + ","
}
$keyBody = $lines -join "`n"

$todayIso = (Get-Date).ToString("yyyy-MM-dd")
$block = @"
/* BEGIN UPDATER_PUBKEY (do not edit by hand -- regenerate via devtools/keygen) */
/* Type: $keyKind$(if (-not $Production) { ' (not for production release)' }) */
/* Generated: $todayIso */
static const u8 UPDATER_PUBKEY[UPDATER_PUBKEY_SIZE] = {
$keyBody
};
/* END UPDATER_PUBKEY */
"@

$pattern = '(?s)/\* BEGIN UPDATER_PUBKEY.*?/\* END UPDATER_PUBKEY \*/'
if ($header -notmatch $pattern) {
    Write-Host "ERROR: BEGIN/END UPDATER_PUBKEY block not found in $headerPath." -ForegroundColor Red
    exit 1
}
# Use MatchEvaluator so the replacement string is treated as literal text —
# avoids regex-substitution corruption if $block ever contained a '$' group.
$evaluator = [System.Text.RegularExpressions.MatchEvaluator] { param($m) $block }
$regex     = [System.Text.RegularExpressions.Regex]::new(
    $pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
$header = $regex.Replace($header, $evaluator, 1)
Set-Content -Path $headerPath -Value $header -NoNewline -Encoding UTF8

Write-Host ""
Write-Host "SUCCESS." -ForegroundColor Green
Write-Host "  Private key: $privatePath  (GITIGNORED -- keep safe)" -ForegroundColor Yellow
Write-Host "  Public key:  $publicPath" -ForegroundColor Gray
Write-Host "  Embedded in: $headerPath" -ForegroundColor Gray
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Rebuild PerfectDark.exe + Updater.exe (the embedded public key is compiled in)." -ForegroundColor Gray
Write-Host "  2. Sign releases with: .\devtools\sign-release.ps1 -ZipPath <zip> -Tag <tag>" -ForegroundColor Gray
if ($Production) {
    Write-Host "  3. Move $privatePath to air-gapped storage and delete the working copy." -ForegroundColor Red
}
