# sign-release.ps1 -- Sign a release ZIP with the developer Ed25519 key (SEC-6).
#
# Produces two sidecars next to the ZIP:
#   <zip>.sha256   -- SEC-5: "<64 hex>  <zip filename>" (GNU sha256sum format)
#   <zip>.sig      -- SEC-6: raw 64-byte Ed25519 signature over sha256(zip) || tag
#
# Usage:
#   .\devtools\sign-release.ps1 -ZipPath dist\PerfectDark-v0.0.21-win64.zip -Tag v0.0.21
#   .\devtools\sign-release.ps1 -ZipPath <zip> -Tag <tag> -KeyPath release-keys/ed25519-private.pem
#
# The default private key is dev-keys/ed25519-private.pem (development flow).
# For production releases, pass -KeyPath release-keys/ed25519-private.pem AFTER
# restoring the key from air-gapped storage.

param(
    [Parameter(Mandatory = $true)] [string]$ZipPath,
    [Parameter(Mandatory = $true)] [string]$Tag,
    [string]$KeyPath = "dev-keys/ed25519-private.pem"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path $PSScriptRoot -Parent
Set-Location $ProjectRoot

$preludePath = Join-Path $PSScriptRoot "_build-env-prelude.ps1"
if (Test-Path $preludePath) { . $preludePath }

# --------------------------------------------------------------------------
# Preconditions
# --------------------------------------------------------------------------
if (-not (Test-Path $ZipPath)) {
    Write-Host "ERROR: ZIP not found: $ZipPath" -ForegroundColor Red; exit 1
}
if (-not (Test-Path $KeyPath)) {
    Write-Host "ERROR: Private key not found: $KeyPath" -ForegroundColor Red
    Write-Host "       Run .\devtools\keygen.ps1 to generate a development key." -ForegroundColor Yellow
    exit 1
}

$openssl = (Get-Command openssl -ErrorAction SilentlyContinue).Source
if (-not $openssl) {
    $openssl = "C:\msys64\mingw64\bin\openssl.exe"
    if (-not (Test-Path $openssl)) {
        Write-Host "ERROR: openssl not found." -ForegroundColor Red; exit 1
    }
}

$zipFullPath = (Resolve-Path $ZipPath).Path
$zipName     = Split-Path $zipFullPath -Leaf
$zipDir      = Split-Path $zipFullPath -Parent

# --------------------------------------------------------------------------
# 1. SHA-256 sidecar (SEC-5)
#    Format: "<64 hex lowercase>  <zip filename>\n"  (GNU sha256sum compatible)
# --------------------------------------------------------------------------
Write-Host "Computing SHA-256 over $zipName ..." -ForegroundColor Cyan
$sha = (Get-FileHash -Algorithm SHA256 -Path $zipFullPath).Hash.ToLowerInvariant()
Write-Host "  $sha" -ForegroundColor Gray

$shaFile = "$zipFullPath.sha256"
# Write as ASCII with LF line endings so the updater's whitespace parser
# behaves identically on Windows and other OSes.
[System.IO.File]::WriteAllText($shaFile, "$sha  $zipName`n", [System.Text.Encoding]::ASCII)
Write-Host "  Wrote $($shaFile)" -ForegroundColor Green

# --------------------------------------------------------------------------
# 2. Ed25519 signature (SEC-6)
#    Sign: sha256(zip) || tag
#    The 32 raw hash bytes followed by the tag string (no NUL, no length
#    prefix) — the updater reconstructs the same message before verify.
# --------------------------------------------------------------------------
Write-Host "Building signature message (sha256 || tag) ..." -ForegroundColor Cyan

$hashBytes = [byte[]]::new(32)
for ($i = 0; $i -lt 32; $i++) {
    $hashBytes[$i] = [Convert]::ToByte($sha.Substring($i * 2, 2), 16)
}
$tagBytes = [System.Text.Encoding]::UTF8.GetBytes($Tag)

$msgBytes = New-Object byte[] ($hashBytes.Length + $tagBytes.Length)
[System.Array]::Copy($hashBytes, 0, $msgBytes, 0, $hashBytes.Length)
[System.Array]::Copy($tagBytes,  0, $msgBytes, $hashBytes.Length, $tagBytes.Length)

$msgFile = [System.IO.Path]::GetTempFileName()
[System.IO.File]::WriteAllBytes($msgFile, $msgBytes)

$sigFile = "$zipFullPath.sig"
Write-Host "Signing with $KeyPath ..." -ForegroundColor Cyan
& $openssl pkeyutl -sign -inkey $KeyPath -rawin -in $msgFile -out $sigFile
$sigExit = $LASTEXITCODE
Remove-Item $msgFile -Force -ErrorAction SilentlyContinue

if ($sigExit -ne 0) {
    Write-Host "ERROR: openssl pkeyutl -sign failed (exit $sigExit)." -ForegroundColor Red
    exit 1
}

$sigBytes = [System.IO.File]::ReadAllBytes($sigFile)
if ($sigBytes.Length -ne 64) {
    Write-Host "ERROR: signature is $($sigBytes.Length) bytes, expected 64." -ForegroundColor Red
    exit 1
}
Write-Host "  Wrote $sigFile ($($sigBytes.Length) bytes)" -ForegroundColor Green

# --------------------------------------------------------------------------
# 3. Self-verify — catches the "signed with the wrong key" class of bugs
#    BEFORE the release gets pushed to GitHub.
# --------------------------------------------------------------------------
$pubFile = [System.IO.Path]::ChangeExtension($KeyPath, ".pub.tmp.pem")
& $openssl pkey -in $KeyPath -pubout -out $pubFile | Out-Null

$verifyMsgFile = [System.IO.Path]::GetTempFileName()
[System.IO.File]::WriteAllBytes($verifyMsgFile, $msgBytes)

& $openssl pkeyutl -verify -pubin -inkey $pubFile -rawin -in $verifyMsgFile -sigfile $sigFile | Out-Null
$verifyExit = $LASTEXITCODE

Remove-Item $verifyMsgFile -Force -ErrorAction SilentlyContinue
Remove-Item $pubFile -Force -ErrorAction SilentlyContinue

if ($verifyExit -ne 0) {
    Write-Host "ERROR: local signature verify failed -- refusing to advertise this release." -ForegroundColor Red
    Remove-Item $sigFile -Force -ErrorAction SilentlyContinue
    exit 1
}

Write-Host ""
Write-Host "Release signed OK:" -ForegroundColor Green
Write-Host "  ZIP:     $zipFullPath"
Write-Host "  SHA-256: $shaFile"
Write-Host "  SIG:     $sigFile"
Write-Host ""
Write-Host "Upload all three as release assets. The updater requires .sha256 AND .sig" -ForegroundColor Cyan
Write-Host "to be present; releases missing either sidecar are rejected at download time." -ForegroundColor Cyan
