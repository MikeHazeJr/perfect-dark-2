# Perfect Dark 2 -- Release Script
# Packages and pushes builds for both client and dedicated server.
#
# Usage:
#   .\release.ps1                    # Uses version from CMakeLists.txt
#   .\release.ps1 -Version "0.0.3a" # Override version string
#   .\release.ps1 -SkipPush          # Build packages but don't push to GitHub
#   .\release.ps1 -DryRun            # Show what would happen without doing it
#
# Package contents:
#   - pd.x86_64.exe (client) + pd-server.x86_64.exe (server)
#   - port/ and src/ folders (source code)
#   - mods/ folder (mod data)
#   - data/ folder (game data, EXCLUDING *.z64 ROM files)
#   - Runtime DLLs (SDL2, zlib, libwinpthread)
#   - SHA-256 hashes for update system verification
#
# Prerequisites:
#   - gh CLI installed and authenticated (gh auth login)
#   - Successful build of both client and server
#   - Git working tree clean (all changes committed)

param(
    [string]$Version = "",
    [switch]$SkipPush,
    [switch]$DryRun,
    [switch]$Prerelease
)

$ErrorActionPreference = "Stop"

# ============================================================================
# Resolve version from CMakeLists.txt
# ============================================================================

if ($Version -eq "") {
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
$ReleaseNotes = "RELEASE_$Tag.md"
$DistDir = "dist/$Tag"

# Build artifact paths -- supports both flat and subdirectory layouts
# Prefer build/client/ and build/server/ (current CMake), fall back to build/
$ClientExe = if (Test-Path "build/client/pd.x86_64.exe") { "build/client/pd.x86_64.exe" }
             elseif (Test-Path "build/pd.x86_64.exe")    { "build/pd.x86_64.exe" }
             else { "" }
$ServerExe = if (Test-Path "build/server/pd-server.x86_64.exe") { "build/server/pd-server.x86_64.exe" }
             elseif (Test-Path "build/pd-server.x86_64.exe")    { "build/pd-server.x86_64.exe" }
             else { "" }

# Data and mods -- prefer build/client/ copies, fall back to post-batch-addin
$DataSource = if (Test-Path "build/client/data") { "build/client/data" }
              elseif (Test-Path "post-batch-addin/data") { "post-batch-addin/data" }
              else { "" }
$ModsSource = if (Test-Path "build/client/mods") { "build/client/mods" }
              elseif (Test-Path "post-batch-addin/mods") { "post-batch-addin/mods" }
              else { "" }

# DLLs -- check build/client first, then post-batch-addin
$DllSearchPaths = @("build/client", "post-batch-addin")

Write-Host ""
Write-Host ("=" * 70) -ForegroundColor Cyan
Write-Host "  Perfect Dark 2 -- Release $Tag" -ForegroundColor Cyan
Write-Host ("=" * 70) -ForegroundColor Cyan
Write-Host ""

# ============================================================================
# Preflight
# ============================================================================

Write-Host "[Preflight] Checking prerequisites..." -ForegroundColor Yellow

$hasGh = [bool](Get-Command "gh" -ErrorAction SilentlyContinue)
$hasClient = $ClientExe -ne ""
$hasServer = $ServerExe -ne ""
$hasData = $DataSource -ne ""
$hasMods = $ModsSource -ne ""
$hasNotes = Test-Path $ReleaseNotes

if ($hasGh) {
    Write-Host "  gh CLI:      FOUND" -ForegroundColor Green
    # Configure git to use gh's auth token for HTTPS push (prevents hang on credential prompt)
    Write-Host "  Setting up gh credential helper for git..." -ForegroundColor Gray
    gh auth setup-git 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor Gray }
} else {
    Write-Host "  gh CLI:      MISSING (will skip GitHub release)" -ForegroundColor Yellow
}

# Prevent git from hanging on credential prompts in subprocess mode
$env:GIT_TERMINAL_PROMPT = "0"

if ($hasClient) { Write-Host "  Client:      FOUND ($ClientExe)" -ForegroundColor Green }
else            { Write-Host "  Client:      MISSING" -ForegroundColor Yellow }

if ($hasServer) { Write-Host "  Server:      FOUND ($ServerExe)" -ForegroundColor Green }
else            { Write-Host "  Server:      MISSING" -ForegroundColor Yellow }

if ($hasData)   { Write-Host "  Data:        FOUND ($DataSource)" -ForegroundColor Green }
else            { Write-Host "  Data:        MISSING" -ForegroundColor Yellow }

if ($hasMods)   { Write-Host "  Mods:        FOUND ($ModsSource)" -ForegroundColor Green }
else            { Write-Host "  Mods:        MISSING" -ForegroundColor Yellow }

Write-Host "  port/:       $(if (Test-Path 'port') { 'FOUND' } else { 'MISSING' })" -ForegroundColor $(if (Test-Path 'port') { 'Green' } else { 'Yellow' })
Write-Host "  src/:        $(if (Test-Path 'src') { 'FOUND' } else { 'MISSING' })" -ForegroundColor $(if (Test-Path 'src') { 'Green' } else { 'Yellow' })
Write-Host "  Notes:       $(if ($hasNotes) { 'FOUND' } else { 'MISSING (will auto-generate)' })" -ForegroundColor $(if ($hasNotes) { 'Green' } else { 'Yellow' })

if (-not $hasClient -and -not $hasServer) {
    Write-Host ""
    Write-Host "  ERROR: No build artifacts found." -ForegroundColor Red
    Write-Host "  Build client and/or server first via Build Tool or build.bat." -ForegroundColor Red
    exit 1
}

# ============================================================================
# Step 1: Create distribution directory
# ============================================================================

Write-Host ""
Write-Host "[1/5] Assembling distribution in $DistDir ..." -ForegroundColor Yellow

if (Test-Path $DistDir) {
    Remove-Item $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

# --- Executables + SHA-256 hashes ---

if ($hasClient) {
    Copy-Item $ClientExe "$DistDir/pd.x86_64.exe"
    $hash = (Get-FileHash $ClientExe -Algorithm SHA256).Hash.ToLower()
    "$hash  pd.x86_64.exe" | Out-File "$DistDir/pd.x86_64.exe.sha256" -Encoding ascii -NoNewline
    Write-Host "  pd.x86_64.exe         SHA-256: $($hash.Substring(0,16))..." -ForegroundColor Gray
}

if ($hasServer) {
    Copy-Item $ServerExe "$DistDir/pd-server.x86_64.exe"
    $hash = (Get-FileHash $ServerExe -Algorithm SHA256).Hash.ToLower()
    "$hash  pd-server.x86_64.exe" | Out-File "$DistDir/pd-server.x86_64.exe.sha256" -Encoding ascii -NoNewline
    Write-Host "  pd-server.x86_64.exe  SHA-256: $($hash.Substring(0,16))..." -ForegroundColor Gray
}

# --- Runtime DLLs ---

$dllNames = @("SDL2.dll", "zlib1.dll", "libwinpthread-1.dll")
foreach ($dll in $dllNames) {
    $found = $false
    foreach ($searchPath in $DllSearchPaths) {
        $dllPath = "$searchPath/$dll"
        if (Test-Path $dllPath) {
            Copy-Item $dllPath "$DistDir/$dll"
            Write-Host "  $dll (from $searchPath)" -ForegroundColor Gray
            $found = $true
            break
        }
    }
    if (-not $found) {
        Write-Host "  $dll -- NOT FOUND (skipped)" -ForegroundColor Yellow
    }
}

# --- Source folders (port/ and src/) ---

if (Test-Path "port") {
    $portCount = (Get-ChildItem "port" -Recurse -File).Count
    Write-Host "  Copying port/ ($portCount files) ..." -ForegroundColor Gray
    Copy-Item "port" "$DistDir/port" -Recurse -Force
    Write-Host "  port/ copied." -ForegroundColor Green
}

if (Test-Path "src") {
    $srcCount = (Get-ChildItem "src" -Recurse -File).Count
    Write-Host "  Copying src/ ($srcCount files) ..." -ForegroundColor Gray
    Copy-Item "src" "$DistDir/src" -Recurse -Force
    Write-Host "  src/ copied." -ForegroundColor Green
}

# --- Data folder (EXCLUDING *.z64 ROM files) ---

if ($hasData) {
    $dataCount = (Get-ChildItem $DataSource -Recurse -File | Where-Object { $_.Extension -ne ".z64" }).Count
    Write-Host "  Copying data/ ($dataCount files, excluding *.z64 ROM files) ..." -ForegroundColor Gray
    New-Item -ItemType Directory -Path "$DistDir/data" -Force | Out-Null

    # Copy everything except .z64 files
    Get-ChildItem $DataSource -Recurse | Where-Object {
        -not $_.PSIsContainer -and $_.Extension -ne ".z64"
    } | ForEach-Object {
        $relativePath = $_.FullName.Substring((Resolve-Path $DataSource).Path.Length + 1)
        $destPath = Join-Path "$DistDir/data" $relativePath
        $destDir = Split-Path $destPath -Parent
        if (-not (Test-Path $destDir)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        }
        Copy-Item $_.FullName $destPath
    }

    # Report excluded ROMs
    $romFiles = Get-ChildItem $DataSource -Filter "*.z64" -Recurse
    if ($romFiles) {
        foreach ($rom in $romFiles) {
            Write-Host "    EXCLUDED: $($rom.Name) (ROM file)" -ForegroundColor DarkYellow
        }
    }
} else {
    Write-Host "  data/ -- NOT FOUND (skipped)" -ForegroundColor Yellow
}

# --- Mods folder ---

if ($hasMods) {
    Write-Host "  Copying mods/ ..." -ForegroundColor Gray
    Copy-Item $ModsSource "$DistDir/mods" -Recurse -Force
} else {
    Write-Host "  mods/ -- NOT FOUND (skipped)" -ForegroundColor Yellow
}

# ============================================================================
# Step 2: Create zip archive
# ============================================================================

Write-Host ""
Write-Host "[2/5] Creating zip archive..." -ForegroundColor Yellow

$zipName = "perfect-dark-2-$Tag-win64.zip"
$zipPath = "dist/$zipName"

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

# Use .NET ZipFile for progress reporting (Compress-Archive gives no feedback)
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$distFullPath = (Resolve-Path $DistDir).Path
$zipFullPath  = Join-Path (Resolve-Path "dist").Path $zipName

$allFiles = Get-ChildItem $distFullPath -Recurse -File
$totalFiles = $allFiles.Count
$totalBytes = ($allFiles | Measure-Object -Property Length -Sum).Sum
$totalMB = [math]::Round($totalBytes / 1MB, 1)
Write-Host "  Compressing $totalFiles files ($totalMB MB) to $zipName ..." -ForegroundColor Gray

$zipStream = [System.IO.Compression.ZipFile]::Open($zipFullPath, [System.IO.Compression.ZipArchiveMode]::Create)
$processed = 0
$lastPct = -1

foreach ($file in $allFiles) {
    $relativePath = $file.FullName.Substring($distFullPath.Length + 1).Replace("\", "/")
    $entry = $zipStream.CreateEntry($relativePath, [System.IO.Compression.CompressionLevel]::Optimal)
    $entryStream = $entry.Open()
    $fileStream = [System.IO.File]::OpenRead($file.FullName)
    $fileStream.CopyTo($entryStream)
    $fileStream.Close()
    $entryStream.Close()

    $processed++
    $pct = [math]::Floor(($processed / $totalFiles) * 100)
    # Report every 5%
    if ($pct -ge ($lastPct + 5)) {
        $lastPct = $pct
        Write-Host "  [$pct%] $processed / $totalFiles files compressed" -ForegroundColor Gray
    }
}

$zipStream.Dispose()

$zipSize = (Get-Item $zipFullPath).Length
$zipSizeStr = if ($zipSize -gt 1MB) { "{0:N1} MB" -f ($zipSize / 1MB) } else { "{0:N0} KB" -f ($zipSize / 1KB) }
Write-Host "  [100%] $zipName ($zipSizeStr)" -ForegroundColor Green

# ============================================================================
# Step 3: Git tag
# ============================================================================

Write-Host ""
Write-Host "[3/5] Git tagging..." -ForegroundColor Yellow

$existingTag = git tag -l $Tag 2>$null
if ($existingTag) {
    Write-Host "  Tag $Tag already exists, skipping." -ForegroundColor Yellow
} elseif ($DryRun) {
    Write-Host "  [DRY RUN] Would create tag: $Tag" -ForegroundColor Magenta
} else {
    git tag -a $Tag -m "$Tag release"
    Write-Host "  Created tag: $Tag" -ForegroundColor Green
}

# ============================================================================
# Step 4: Push branch + tags
# ============================================================================

Write-Host ""
Write-Host "[4/5] Pushing to remote..." -ForegroundColor Yellow

if ($SkipPush -or $DryRun) {
    Write-Host "  $(if ($DryRun) { '[DRY RUN] ' })Skipping push." -ForegroundColor $(if ($DryRun) { 'Magenta' } else { 'Yellow' })
} else {
    $currentBranch = git branch --show-current
    Write-Host "  Pushing branch '$currentBranch' ..." -ForegroundColor Gray

    # Temporarily allow errors so git's stderr progress lines don't kill us.
    # Git writes ALL progress (Enumerating objects, Counting, etc.) to stderr.
    # With $ErrorActionPreference = "Stop", PowerShell's 2>&1 wraps those as
    # terminating ErrorRecords. We lower to Continue, run git, save exit code,
    # then restore Stop.
    $savedEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $pushOut = git push origin $currentBranch --progress 2>&1
    $pushExit = $LASTEXITCODE
    $ErrorActionPreference = $savedEAP
    foreach ($line in $pushOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }

    if ($pushExit -ne 0) {
        Write-Host "  ERROR: Branch push failed (exit $pushExit)" -ForegroundColor Red
        Write-Host "  Check credentials: git push may need auth." -ForegroundColor Red
        exit 1
    }
    Write-Host "  Branch pushed." -ForegroundColor Green

    Write-Host "  Pushing tags ..." -ForegroundColor Gray
    $ErrorActionPreference = "Continue"
    $tagOut = git push origin --tags --progress 2>&1
    $tagExit = $LASTEXITCODE
    $ErrorActionPreference = $savedEAP
    foreach ($line in $tagOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }

    if ($tagExit -ne 0) {
        Write-Host "  ERROR: Tag push failed (exit $tagExit)" -ForegroundColor Red
        exit 1
    }
    Write-Host "  Tags pushed." -ForegroundColor Green
}

# ============================================================================
# Step 5: GitHub release
# ============================================================================

Write-Host ""
Write-Host "[5/5] Creating GitHub release..." -ForegroundColor Yellow

if ($SkipPush -or $DryRun -or -not $hasGh) {
    $reason = if ($DryRun) { "[DRY RUN]" } elseif (-not $hasGh) { "gh CLI not found" } else { "push skipped" }
    Write-Host "  Skipping GitHub release ($reason)." -ForegroundColor $(if ($DryRun) { 'Magenta' } else { 'Yellow' })
} else {
    $ghArgs = @("release", "create", $Tag, "--title", "$Tag -- Perfect Dark 2")

    if ($hasNotes) {
        $ghArgs += "--notes-file"
        $ghArgs += $ReleaseNotes
    } else {
        $ghArgs += "--generate-notes"
    }

    if ($Prerelease) {
        $ghArgs += "--prerelease"
    }

    # Attach individual executables + hashes (for update system) and the full zip
    if ($hasClient) {
        $ghArgs += "$DistDir/pd.x86_64.exe"
        $ghArgs += "$DistDir/pd.x86_64.exe.sha256"
    }
    if ($hasServer) {
        $ghArgs += "$DistDir/pd-server.x86_64.exe"
        $ghArgs += "$DistDir/pd-server.x86_64.exe.sha256"
    }
    if (Test-Path $zipPath) {
        $ghArgs += $zipPath
    }

    Write-Host "  Running: gh $($ghArgs -join ' ')" -ForegroundColor Gray

    # Count assets being uploaded
    $assetCount = 0
    if ($hasClient) { $assetCount += 2 }   # exe + sha256
    if ($hasServer) { $assetCount += 2 }   # exe + sha256
    if (Test-Path $zipPath) { $assetCount += 1 }
    Write-Host "  Uploading $assetCount asset(s) to GitHub ..." -ForegroundColor Gray

    $savedEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $ghOut = gh @ghArgs 2>&1
    $ghExit = $LASTEXITCODE
    $ErrorActionPreference = $savedEAP
    foreach ($line in $ghOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }

    if ($ghExit -eq 0) {
        Write-Host ""
        Write-Host "  Release created:" -ForegroundColor Green
        Write-Host "  https://github.com/MikeHazeJr/perfect-dark-2/releases/tag/$Tag" -ForegroundColor Cyan
    } else {
        Write-Host ""
        Write-Host "  ERROR: Release creation failed (exit $ghExit)." -ForegroundColor Red
        Write-Host "  Run 'gh auth status' to check authentication." -ForegroundColor Red
    }
}

# ============================================================================
# Summary
# ============================================================================

Write-Host ""
Write-Host ("=" * 70) -ForegroundColor Green
Write-Host "  RELEASE $Tag -- PACKAGING COMPLETE" -ForegroundColor Green
Write-Host ("=" * 70) -ForegroundColor Green
Write-Host ""

# List all files with sizes
Write-Host "  Distribution contents ($DistDir):" -ForegroundColor White
$totalSize = 0

# Top-level files
Get-ChildItem $DistDir -File | ForEach-Object {
    $size = $_.Length
    $totalSize += $size
    $sizeStr = if ($size -gt 1MB) { "{0:N1} MB" -f ($size / 1MB) } else { "{0:N0} KB" -f ($size / 1KB) }
    Write-Host ("    {0,-35} {1,10}" -f $_.Name, $sizeStr) -ForegroundColor Gray
}

# Directories with file counts
foreach ($subdir in @("port", "src", "data", "mods")) {
    $path = "$DistDir/$subdir"
    if (Test-Path $path) {
        $fileCount = (Get-ChildItem $path -Recurse -File).Count
        $dirSize = (Get-ChildItem $path -Recurse -File | Measure-Object -Property Length -Sum).Sum
        $totalSize += $dirSize
        $sizeStr = if ($dirSize -gt 1MB) { "{0:N1} MB" -f ($dirSize / 1MB) } else { "{0:N0} KB" -f ($dirSize / 1KB) }
        Write-Host ("    {0,-35} {1,10}  ({2} files)" -f "$subdir/", $sizeStr, $fileCount) -ForegroundColor Gray
    }
}

$totalStr = if ($totalSize -gt 1MB) { "{0:N1} MB" -f ($totalSize / 1MB) } else { "{0:N0} KB" -f ($totalSize / 1KB) }
Write-Host ""
Write-Host "  Total (uncompressed): $totalStr" -ForegroundColor White
Write-Host "  Zip:                  $zipSizeStr ($zipName)" -ForegroundColor White
Write-Host ""
