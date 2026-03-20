# Perfect Dark 2 — Release Script
# Packages and pushes builds for both client and dedicated server.
#
# Usage:
#   .\release.ps1                    # Uses version from CMakeLists.txt
#   .\release.ps1 -Version "0.0.3a" # Override version string
#   .\release.ps1 -SkipPush          # Build packages but don't push to GitHub
#   .\release.ps1 -DryRun            # Show what would happen without doing it
#
# Prerequisites:
#   - gh CLI installed and authenticated (gh auth login)
#   - Successful build of both client and server in build/
#   - Git working tree clean (all changes committed)

param(
    [string]$Version = "",
    [switch]$SkipPush,
    [switch]$DryRun,
    [switch]$Prerelease
)

$ErrorActionPreference = "Stop"

# --- Resolve version ---

if ($Version -eq "") {
    # Parse from CMakeLists.txt
    $cmake = Get-Content "CMakeLists.txt" -Raw
    $major = if ($cmake -match 'VERSION_SEM_MAJOR\s+(\d+)') { $matches[1] } else { "0" }
    $minor = if ($cmake -match 'VERSION_SEM_MINOR\s+(\d+)') { $matches[1] } else { "0" }
    $patch = if ($cmake -match 'VERSION_SEM_PATCH\s+(\d+)') { $matches[1] } else { "0" }
    $dev   = if ($cmake -match 'VERSION_SEM_DEV\s+(\d+)')   { $matches[1] } else { "0" }
    $label = if ($cmake -match 'VERSION_SEM_LABEL\s+"([^"]*)"') { $matches[1] } else { "" }

    if ([int]$dev -gt 0) {
        $Version = "$major.$minor.$patch-dev.$dev"
    } elseif ($label -ne "") {
        $Version = "$major.$minor.$patch$label"
    } else {
        $Version = "$major.$minor.$patch"
    }
}

$Tag = "v$Version"
$ClientExe = "build/pd.x86_64.exe"
$ServerExe = "build/pd-server.x86_64.exe"
$ReleaseNotes = "RELEASE_v$Version.md"
$DistDir = "dist/$Version"

Write-Host "=== Perfect Dark 2 — Release $Tag ===" -ForegroundColor Cyan
Write-Host ""

# --- Preflight checks ---

Write-Host "[Preflight] Checking prerequisites..." -ForegroundColor Yellow

# Check gh CLI
if (-not (Get-Command "gh" -ErrorAction SilentlyContinue)) {
    Write-Host "  ERROR: gh CLI not found. Install from https://cli.github.com/" -ForegroundColor Red
    exit 1
}

# Check builds exist
$hasClient = Test-Path $ClientExe
$hasServer = Test-Path $ServerExe

if (-not $hasClient -and -not $hasServer) {
    Write-Host "  ERROR: No build artifacts found." -ForegroundColor Red
    Write-Host "  Expected: $ClientExe and/or $ServerExe" -ForegroundColor Red
    Write-Host "  Run 'Build Tool.bat' → Build Client / Build Server first." -ForegroundColor Red
    exit 1
}

Write-Host "  Client: $(if ($hasClient) { 'FOUND' } else { 'MISSING (will skip)' })" -ForegroundColor $(if ($hasClient) { 'Green' } else { 'Yellow' })
Write-Host "  Server: $(if ($hasServer) { 'FOUND' } else { 'MISSING (will skip)' })" -ForegroundColor $(if ($hasServer) { 'Green' } else { 'Yellow' })

# Check release notes
$hasNotes = Test-Path $ReleaseNotes
Write-Host "  Release notes ($ReleaseNotes): $(if ($hasNotes) { 'FOUND' } else { 'MISSING (will use auto-generated)' })" -ForegroundColor $(if ($hasNotes) { 'Green' } else { 'Yellow' })

# --- Create dist directory ---

Write-Host ""
Write-Host "[1/5] Creating distribution packages..." -ForegroundColor Yellow

if (-not (Test-Path $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
}

# --- Generate SHA-256 hashes ---

if ($hasClient) {
    Write-Host "  Hashing client..." -ForegroundColor Gray
    $clientHash = (Get-FileHash $ClientExe -Algorithm SHA256).Hash.ToLower()
    $clientFileName = [System.IO.Path]::GetFileName($ClientExe)
    "$clientHash  $clientFileName" | Out-File -FilePath "$DistDir/$clientFileName.sha256" -Encoding ascii -NoNewline
    Copy-Item $ClientExe "$DistDir/$clientFileName"
    Write-Host "  Client SHA-256: $clientHash" -ForegroundColor Gray
}

if ($hasServer) {
    Write-Host "  Hashing server..." -ForegroundColor Gray
    $serverHash = (Get-FileHash $ServerExe -Algorithm SHA256).Hash.ToLower()
    $serverFileName = [System.IO.Path]::GetFileName($ServerExe)
    "$serverHash  $serverFileName" | Out-File -FilePath "$DistDir/$serverFileName.sha256" -Encoding ascii -NoNewline
    Copy-Item $ServerExe "$DistDir/$serverFileName"
    Write-Host "  Server SHA-256: $serverHash" -ForegroundColor Gray
}

# --- Copy runtime dependencies ---

Write-Host "  Copying runtime dependencies..." -ForegroundColor Gray
$deps = @("SDL2.dll", "zlib1.dll", "libwinpthread-1.dll")
foreach ($dll in $deps) {
    $dllPath = "build/$dll"
    if (Test-Path $dllPath) {
        Copy-Item $dllPath "$DistDir/$dll"
        Write-Host "    $dll" -ForegroundColor Gray
    }
}

# --- Copy data and mods ---

if (Test-Path "build/data") {
    Write-Host "  Copying data directory..." -ForegroundColor Gray
    Copy-Item "build/data" "$DistDir/data" -Recurse -Force
}

if (Test-Path "build/mods") {
    Write-Host "  Copying mods directory..." -ForegroundColor Gray
    Copy-Item "build/mods" "$DistDir/mods" -Recurse -Force
}

# --- Create zip packages ---

Write-Host ""
Write-Host "[2/5] Creating zip archives..." -ForegroundColor Yellow

$clientZip = "dist/perfect-dark-2-$Tag-client-win64.zip"
$serverZip = "dist/perfect-dark-2-$Tag-server-win64.zip"
$fullZip   = "dist/perfect-dark-2-$Tag-full-win64.zip"

# Full package (client + server + data + mods + DLLs)
$fullItems = @()
if ($hasClient) { $fullItems += "$DistDir/pd.x86_64.exe" }
if ($hasServer) { $fullItems += "$DistDir/pd-server.x86_64.exe" }
foreach ($dll in $deps) { if (Test-Path "$DistDir/$dll") { $fullItems += "$DistDir/$dll" } }
if (Test-Path "$DistDir/data") { $fullItems += "$DistDir/data" }
if (Test-Path "$DistDir/mods") { $fullItems += "$DistDir/mods" }

if ($fullItems.Count -gt 0) {
    Write-Host "  Creating full package: $fullZip" -ForegroundColor Gray
    Compress-Archive -Path $fullItems -DestinationPath $fullZip -Force
}

Write-Host "  Done." -ForegroundColor Green

# --- Git tag ---

Write-Host ""
Write-Host "[3/5] Git tagging..." -ForegroundColor Yellow

$existingTag = git tag -l $Tag 2>$null
if ($existingTag) {
    Write-Host "  Tag $Tag already exists, skipping." -ForegroundColor Yellow
} else {
    if ($DryRun) {
        Write-Host "  [DRY RUN] Would create tag: $Tag" -ForegroundColor Magenta
    } else {
        git tag -a $Tag -m "$Tag release"
        Write-Host "  Created tag: $Tag" -ForegroundColor Green
    }
}

# --- Push ---

Write-Host ""
Write-Host "[4/5] Pushing to remote..." -ForegroundColor Yellow

if ($SkipPush -or $DryRun) {
    Write-Host "  $(if ($DryRun) { '[DRY RUN] ' } else { '' })Skipping push." -ForegroundColor $(if ($DryRun) { 'Magenta' } else { 'Yellow' })
} else {
    $currentBranch = git branch --show-current
    Write-Host "  Pushing branch '$currentBranch' and tags..." -ForegroundColor Gray
    git push origin $currentBranch --tags
    Write-Host "  Pushed." -ForegroundColor Green
}

# --- GitHub Release ---

Write-Host ""
Write-Host "[5/5] Creating GitHub release..." -ForegroundColor Yellow

if ($SkipPush -or $DryRun) {
    Write-Host "  $(if ($DryRun) { '[DRY RUN] ' } else { '' })Skipping GitHub release." -ForegroundColor $(if ($DryRun) { 'Magenta' } else { 'Yellow' })
} else {
    $ghArgs = @("release", "create", $Tag, "--title", "$Tag — Perfect Dark 2")

    if ($hasNotes) {
        $ghArgs += "--notes-file"
        $ghArgs += $ReleaseNotes
    } else {
        $ghArgs += "--generate-notes"
    }

    if ($Prerelease) {
        $ghArgs += "--prerelease"
    }

    # Attach assets: individual exe + sha256 files for update system, plus full zip
    if ($hasClient) {
        $ghArgs += "$DistDir/pd.x86_64.exe"
        $ghArgs += "$DistDir/pd.x86_64.exe.sha256"
    }
    if ($hasServer) {
        $ghArgs += "$DistDir/pd-server.x86_64.exe"
        $ghArgs += "$DistDir/pd-server.x86_64.exe.sha256"
    }
    if (Test-Path $fullZip) {
        $ghArgs += $fullZip
    }

    gh @ghArgs

    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "=== Release $Tag created successfully! ===" -ForegroundColor Green
        Write-Host "  https://github.com/MikeHazeJr/perfect-dark-2/releases/tag/$Tag" -ForegroundColor Cyan
    } else {
        Write-Host ""
        Write-Host "=== Release creation failed. Check 'gh auth status'. ===" -ForegroundColor Red
    }
}

# --- Summary ---

Write-Host ""
Write-Host "=== Distribution files ===" -ForegroundColor Cyan
Get-ChildItem $DistDir -File | ForEach-Object {
    $size = if ($_.Length -gt 1MB) { "{0:N1} MB" -f ($_.Length / 1MB) } else { "{0:N0} KB" -f ($_.Length / 1KB) }
    Write-Host "  $($_.Name)  ($size)" -ForegroundColor Gray
}
if (Test-Path $fullZip) {
    $zipSize = (Get-Item $fullZip).Length
    $zipSizeStr = if ($zipSize -gt 1MB) { "{0:N1} MB" -f ($zipSize / 1MB) } else { "{0:N0} KB" -f ($zipSize / 1KB) }
    Write-Host "  $(Split-Path $fullZip -Leaf)  ($zipSizeStr)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Done. Artifacts are in: $DistDir" -ForegroundColor Green
