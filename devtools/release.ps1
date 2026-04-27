# Perfect Dark 2 -- Release Script
# Packages and pushes builds for both client and dedicated server.
#
# Usage:
#   .\release.ps1                    # Version = max(CMakeLists, v* git tags) + 1 patch; updates CMakeLists.txt
#   .\release.ps1 -Version "1.2.3" # Explicit X.Y.Z (numeric); syncs CMakeLists.txt when possible
#   .\release.ps1 -Nightly           # Nightly dev build (date-based tag, prerelease)
#   .\release.ps1 -SkipPush          # Build packages but don't push to GitHub
#   .\release.ps1 -DryRun            # Show what would happen without doing it
#
# Package contents (game zip -- "PerfectDark-v{X.Y.Z}-win64.zip"):
#   - PerfectDark.exe (game client, fully static -- no runtime DLLs required)
#   - PerfectDarkServer.exe (dedicated server, fully static)
#   - Updater.exe (standalone GUI updater; recovery path if client self-update breaks)
#   - data/ folder (game data, EXCLUDING *.z64 ROM files)
#   - mods/ folder (mod content)
#
# Source code is NOT included -- GitHub auto-generates source archives.
#
# Post-release housekeeping:
#   - Dev (prerelease) tags are pruned to the newest 10 after a successful push;
#     stable releases are never touched.
#
# Prerequisites:
#   - gh CLI installed and authenticated (gh auth login)
#   - Successful build of both client and server
#   - Git working tree clean (all changes committed)

param(
    [string]$Version = "",
    [switch]$Nightly,
    [switch]$SkipPush,
    [switch]$DryRun,
    [switch]$Prerelease,
    [switch]$SkipBuild,   # Skip cmake reconfigure+build (caller already built; artifacts must exist in Build/)
    [switch]$ForceCommitNoVerify  # If commit hooks fail, retry commit with --no-verify
)

$ErrorActionPreference = "Stop"

# Project root is one level up from devtools/
$ProjectRoot = Split-Path $PSScriptRoot -Parent

# Tracks whether Step 5 successfully published the release. Used by Step 6
# (Dev-release prune) so a failed publish never triggers deletion of prior
# prereleases. Initialized here so the Step 5 skip path leaves it falsy.
$script:ReleasePublishOk = $false

# Ensure CWD is the project root -- all relative paths (build/, dist/, CMakeLists.txt)
# assume this. The dev window sets it explicitly, but this covers direct invocation too.
Set-Location $ProjectRoot

# ============================================================================
# Resolve version / nightly date code
# ============================================================================

if ($Nightly) {
    $DateCode = (Get-Date).ToString("yyyy-MM-dd")
    $ReleaseTag = "nightly-$DateCode"
    $ReleaseTitle = "Nightly Build - $DateCode"
    $DistDir = "dist/nightly-$DateCode"
    $Prerelease = $true
} else {
    $ExplicitVersion = ($Version -ne "")
    if ($Version -eq "") {
        . (Join-Path $PSScriptRoot "version-util.ps1")
        $nextInfo = Get-NextReleaseSemVer $ProjectRoot
        Write-Host "  Release version (max of CMake + git tags, then +1): $($nextInfo.NextString)" -ForegroundColor Cyan
        Write-Host "    Previous max: $($nextInfo.Previous.Major).$($nextInfo.Previous.Minor).$($nextInfo.Previous.Patch)" -ForegroundColor Gray
        Set-CMakeListsSemVer $ProjectRoot $nextInfo.Next.Major $nextInfo.Next.Minor $nextInfo.Next.Patch
        $Version = $nextInfo.NextString
    }
    $ReleaseTag = "v$Version"
    $ReleaseTitle = "Perfect Dark 2 v$Version ($(if ($Prerelease) { 'Dev' } else { 'Stable' }))"
    $DistDir = "dist/v$Version"

    # When -Version is explicitly provided, sync CMakeLists.txt so it matches the release tag.
    # This keeps the file in sync for subsequent builds. Only applies when all parts are numeric.
    if ($ExplicitVersion) {
        $parts = $Version -split '\.'
        if ($parts.Count -ge 3 -and $parts[0] -match '^\d+$' -and $parts[1] -match '^\d+$' -and $parts[2] -match '^\d+$') {
            try {
                $cmake = Get-Content "CMakeLists.txt" -Raw -ErrorAction Stop
                $cmake = $cmake -replace '(VERSION_SEM_MAJOR\s+)\d+', ("`${1}" + $parts[0])
                $cmake = $cmake -replace '(VERSION_SEM_MINOR\s+)\d+', ("`${1}" + $parts[1])
                $cmake = $cmake -replace '(VERSION_SEM_PATCH\s+)\d+', ("`${1}" + $parts[2])
                Set-Content "CMakeLists.txt" -Value $cmake -NoNewline -Encoding UTF8 -ErrorAction Stop
                Write-Host "  Synced CMakeLists.txt to v$Version" -ForegroundColor Gray
            } catch {
                Write-Host "  Warning: Could not sync CMakeLists.txt: $_" -ForegroundColor Yellow
            }
        }
    }
}

$ReleaseNotes = "UNRELEASED.md"

# ============================================================================
# Step 0: Rebuild from source (cmake reconfigure + compile)
# Version is baked in at cmake configure time via versioninfo.h.in.
# Pre-existing binaries may embed a stale version — always reconfigure + build.
# Pass -SkipBuild when the caller (e.g. dev-window-v2.ps1) has already built
# both targets; artifacts must already exist in Build/.
# ============================================================================

# Build tool paths — same as build-headless.ps1 and dev-window-v2.ps1
$CMakeExe  = "cmake"
$CCExe     = "C:/msys64/mingw64/bin/cc.exe"
$Cores     = if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { 4 }
$BuildDir  = Join-Path $ProjectRoot "Build"

# Build environment -- self-configures TEMP/TMP, PATH (MinGW64), MSYSTEM, ccache.
. (Join-Path $PSScriptRoot "_build-env-prelude.ps1")
$env:GIT_TERMINAL_PROMPT = "0"                          # prevent git from hanging on credential prompts
$gitIndexLock = Join-Path $ProjectRoot ".git\index.lock"
if (Test-Path -LiteralPath $gitIndexLock) {
    try { & cmd.exe /c "attrib -R `"$gitIndexLock`"" 2>$null | Out-Null } catch {}
    Remove-Item -LiteralPath $gitIndexLock -Force -ErrorAction SilentlyContinue
}

# Version parts for cmake -D flags (resolved above from CMakeLists.txt or -Version param)
$vParts = $Version -split '\.'
$vMaj = if ($vParts.Count -ge 1 -and $vParts[0] -match '^\d+$') { $vParts[0] } else { "0" }
$vMin = if ($vParts.Count -ge 2 -and $vParts[1] -match '^\d+$') { $vParts[1] } else { "0" }
$vPat = if ($vParts.Count -ge 3 -and $vParts[2] -match '^\d+$') { $vParts[2] } else { "0" }

function Invoke-ReleaseCommit {
    param(
        [string]$Message,
        [switch]$AllowNoVerifyFallback
    )

    $commitOut = @(git commit -m $Message 2>&1)
    $commitCode = $LASTEXITCODE
    foreach ($line in $commitOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }

    if ($commitCode -eq 0) {
        return $true
    }

    if (-not $AllowNoVerifyFallback) {
        return $false
    }

    Write-Host "  Commit failed; retrying with --no-verify (--force commit mode)." -ForegroundColor Yellow
    $commitOut2 = @(git commit --no-verify -m $Message 2>&1)
    $commitCode2 = $LASTEXITCODE
    foreach ($line in $commitOut2) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }
    return ($commitCode2 -eq 0)
}

# Mirror dev-window-v2's Copy-AddinFiles: copies post-batch-addin/data into
# Build/data so the game can find pd.{ROMID}.z64 at $E/data/pd.*.z64. Runs
# right after the client build succeeds, so Mike can test the built exe
# locally even if a subsequent release step (server build, push, gh release)
# fails. Safe to call multiple times; overwrite-on-copy.
function Copy-RomAddinIntoBuild {
    $addinData = Join-Path $ProjectRoot "..\post-batch-addin\data"
    $buildData = Join-Path $BuildDir "data"
    if (-not (Test-Path $addinData)) {
        Write-Host "  [rom-copy] post-batch-addin/data not found -- skipping ROM copy." -ForegroundColor Yellow
        return
    }
    try {
        if (-not (Test-Path $buildData)) {
            New-Item -ItemType Directory -Path $buildData -Force | Out-Null
        }
        Copy-Item -Path (Join-Path $addinData "*") -Destination $buildData -Recurse -Force -ErrorAction Stop
        Write-Host "  [rom-copy] Copied post-batch-addin/data -> Build/data/ (ROM ready for local testing)." -ForegroundColor Green
    } catch {
        Write-Host "  [rom-copy] WARN: copy failed: $_" -ForegroundColor Yellow
    }
}

Write-Host ""
if ($SkipBuild) {
    Write-Host "[0/8] Skipping rebuild (-SkipBuild set; using existing artifacts in Build/)." -ForegroundColor Gray

    # Caller (e.g. dev-window-v2) already built the client. Drop the ROM into
    # Build/data/ right away so Mike can launch Build/PerfectDark.exe even if a
    # later step in this script fails.
    Copy-RomAddinIntoBuild

    # Commit any pending changes so the release tag lands on a clean commit
    Write-Host "  [pre-release] Committing any pending changes..." -ForegroundColor Gray
    $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $statusOut = git -C $ProjectRoot status --porcelain 2>&1
    if ($statusOut) {
        git -C $ProjectRoot add -A 2>&1 | Out-Null
        if (Invoke-ReleaseCommit -Message "chore: pre-release commit v$Version" -AllowNoVerifyFallback:$ForceCommitNoVerify) {
            Write-Host "  [pre-release] Committed pending changes." -ForegroundColor Green
        } else {
            Write-Host "  [pre-release] Commit failed." -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "  [pre-release] Nothing to commit." -ForegroundColor Gray
    }
    $currentBranchForPush = git -C $ProjectRoot rev-parse --abbrev-ref HEAD 2>&1
    $pushOut = git -C $ProjectRoot push origin $currentBranchForPush 2>&1
    $pushExit = $LASTEXITCODE
    $ErrorActionPreference = $savedEAP
    if ($pushExit -ne 0) {
        Write-Host "  [pre-release] Push failed (will retry in Step 4)." -ForegroundColor Yellow
    } else {
        Write-Host "  [pre-release] Pushed to remote." -ForegroundColor Green
    }

    # Also build the standalone Updater if it's missing (dev-window-v2 builds
    # only client+server). A missing Updater is a warning, not a release
    # blocker — the zip will simply omit it.
    $updaterExePath = Join-Path $BuildDir "Updater.exe"
    if (-not (Test-Path $updaterExePath)) {
        Write-Host "  [updater] Updater.exe missing -- building pd-updater incrementally..." -ForegroundColor Gray
        $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        $bldOut  = & $CMakeExe --build $BuildDir --target pd-updater 2>&1
        $bldExit = $LASTEXITCODE
        $ErrorActionPreference = $savedEAP
        if ($bldExit -ne 0) {
            $bldOut | Select-Object -Last 20 | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
            Write-Host "  [updater] WARN: build failed (exit $bldExit) -- release will proceed without Updater.exe." -ForegroundColor Yellow
        } else {
            Write-Host "  [updater] build OK." -ForegroundColor Green
        }
    }
} else {
    Write-Host "[0/8] Rebuilding from source (cmake reconfigure + compile)..." -ForegroundColor Yellow

    # ---- Pre-build: commit + push so the release tag lands on a clean commit ----
    Write-Host "  [pre-build] Committing any pending changes before release build..." -ForegroundColor Gray
    $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $statusOut = git -C $ProjectRoot status --porcelain 2>&1
    if ($statusOut) {
        git -C $ProjectRoot add -A 2>&1 | Out-Null
        if (Invoke-ReleaseCommit -Message "chore: pre-release commit v$Version" -AllowNoVerifyFallback:$ForceCommitNoVerify) {
            Write-Host "  [pre-build] Committed pending changes." -ForegroundColor Green
        } else {
            Write-Host "  [pre-build] Commit failed." -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "  [pre-build] Nothing to commit." -ForegroundColor Gray
    }
    $currentBranchForPush = git -C $ProjectRoot rev-parse --abbrev-ref HEAD 2>&1
    $pushOut = git -C $ProjectRoot push origin $currentBranchForPush 2>&1
    $pushExit = $LASTEXITCODE
    $ErrorActionPreference = $savedEAP
    if ($pushExit -ne 0) {
        Write-Host "  [pre-build] Push failed (continuing -- will retry in Step 4)." -ForegroundColor Yellow
    } else {
        Write-Host "  [pre-build] Pushed to remote." -ForegroundColor Green
    }

    # ---- Single configure (unified Build/ dir) then build both targets ----
    $buildOk = $true
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
        Write-Host "  [cmake] created missing build directory: $BuildDir" -ForegroundColor Gray
    }

    Write-Host "  [cmake] configure (Ninja + ccache)..." -ForegroundColor Gray
    $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $stableArg = @()
    if (-not $Prerelease) { $stableArg = @("-DPD_STABLE_RELEASE=ON") }
    $cfgOut  = & $CMakeExe -G Ninja "-DCMAKE_C_COMPILER=$CCExe" `
        "-DCMAKE_C_COMPILER_LAUNCHER=ccache" "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache" `
        "-B" $BuildDir "-S" $ProjectRoot "-DVERSION_SEM_MAJOR=$vMaj" "-DVERSION_SEM_MINOR=$vMin" "-DVERSION_SEM_PATCH=$vPat" @stableArg 2>&1
    $cfgExit = $LASTEXITCODE
    $ErrorActionPreference = $savedEAP

    if ($cfgExit -ne 0) {
        $cfgOut | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
        Write-Host "  ERROR: cmake configure failed (exit $cfgExit)" -ForegroundColor Red
        $buildOk = $false
    }

    if ($buildOk) {
        # Client must be first so Copy-RomAddinIntoBuild (below) only runs once
        # the client exe is known-good. Updater is optional — its failure only
        # drops it from the release, not the whole pipeline.
        # Server target retired from the release pipeline 2026-04-27.
        # Connectivity now lives in-client via listen-host mode; pd-server is
        # no longer shipped. The cmake target itself remains defined for
        # pd-tests linkage and ad-hoc dev runs, but releases skip building it
        # (saves ~6-10 s per release).
        $targets = @(
            @{ Name="client";  Target="pd";         Optional=$false },
            @{ Name="updater"; Target="pd-updater"; Optional=$true  }
        )
        foreach ($t in $targets) {
            Write-Host "  [$($t.Name)] cmake build ($($t.Target))..." -ForegroundColor Gray
            $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            $bldOut  = & $CMakeExe --build $BuildDir --target $t.Target 2>&1
            $bldExit = $LASTEXITCODE
            $ErrorActionPreference = $savedEAP

            if ($bldExit -ne 0) {
                $bldOut | Select-Object -Last 20 | ForEach-Object { Write-Host "    $_" -ForegroundColor $(if ($t.Optional) { 'Yellow' } else { 'Red' }) }
                if ($t.Optional) {
                    Write-Host "  WARN: build failed for $($t.Name) (exit $bldExit) -- release will continue without it." -ForegroundColor Yellow
                    continue
                }
                Write-Host "  ERROR: build failed for $($t.Name) (exit $bldExit)" -ForegroundColor Red
                $buildOk = $false; break
            }
            Write-Host "  [$($t.Name)] build OK." -ForegroundColor Green

            # Client is now on disk and known-good. Drop the ROM into Build/data/
            # immediately so Mike can launch Build/PerfectDark.exe locally even
            # if the server/updater build or any later release step fails.
            if ($t.Name -eq "client") {
                Copy-RomAddinIntoBuild
            }
        }
    }

    if (-not $buildOk) {
        Write-Host ""
        Write-Host "  ERROR: Build failed. Fix errors before releasing." -ForegroundColor Red
        exit 1
    }
    Write-Host "  All required targets built successfully (v$Version)." -ForegroundColor Green
}

# Build artifact paths -- unified Build/ directory
$ClientExe  = $(if (Test-Path (Join-Path $BuildDir "PerfectDark.exe"))       { Join-Path $BuildDir "PerfectDark.exe" }       else { "" })
# pd-server is no longer built/shipped (see Step 0 -- connectivity is in-client
# via listen-host mode). $ServerExe stays "" so the assembly + asset-upload
# branches below skip cleanly.
$ServerExe  = ""
$UpdaterExe = $(if (Test-Path (Join-Path $BuildDir "Updater.exe"))           { Join-Path $BuildDir "Updater.exe" }           else { "" })

# Data and mods -- prefer Build/ copies, fall back to post-batch-addin
$DataSource = $(if (Test-Path (Join-Path $BuildDir "data"))  { Join-Path $BuildDir "data" }
                elseif (Test-Path "../post-batch-addin/data") { "../post-batch-addin/data" }
                else { "" })
$ModsSource = $(if (Test-Path (Join-Path $BuildDir "mods"))  { Join-Path $BuildDir "mods" }
                elseif (Test-Path "../post-batch-addin/mods") { "../post-batch-addin/mods" }
                else { "" })

Write-Host ""
Write-Host ("=" * 70) -ForegroundColor Cyan
Write-Host "  Perfect Dark 2 -- $ReleaseTitle" -ForegroundColor Cyan
Write-Host ("=" * 70) -ForegroundColor Cyan
Write-Host ""

# ============================================================================
# Preflight
# ============================================================================

Write-Host "[Preflight] Checking prerequisites..." -ForegroundColor Yellow

# Find gh CLI -- check PATH first, then common install locations
$ghCmd = Get-Command "gh" -ErrorAction SilentlyContinue
if (-not $ghCmd) {
    $ghSearchPaths = @(
        "$env:ProgramFiles\GitHub CLI\gh.exe",
        "${env:ProgramFiles(x86)}\GitHub CLI\gh.exe",
        "$env:LOCALAPPDATA\Programs\GitHub CLI\gh.exe",
        "$env:USERPROFILE\scoop\shims\gh.exe",
        "C:\Program Files\GitHub CLI\gh.exe",
        "C:\Program Files (x86)\GitHub CLI\gh.exe"
    )
    foreach ($p in $ghSearchPaths) {
        if (Test-Path $p) {
            $ghCmd = $p
            # Add its directory to PATH for this session so git can find it too
            $ghDir = Split-Path $p -Parent
            $env:PATH = "$ghDir;$env:PATH"
            break
        }
    }
}
$hasGh = [bool]$ghCmd
$hasClient  = $ClientExe  -ne ""
$hasServer  = $ServerExe  -ne ""
$hasUpdater = $UpdaterExe -ne ""
$hasData    = $DataSource -ne ""
$hasMods    = $ModsSource -ne ""
$hasNotes   = Test-Path $ReleaseNotes

if ($hasGh) {
    $ghPath = $(if ($ghCmd -is [string]) { $ghCmd } else { $ghCmd.Source })
    Write-Host "  gh CLI:      FOUND ($ghPath)" -ForegroundColor Green
    # Configure git to use gh's auth token for HTTPS push (prevents hang on credential prompt)
    Write-Host "  Setting up gh credential helper for git..." -ForegroundColor Gray
    $savedEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $ghSetup = & gh auth setup-git 2>&1
    $ErrorActionPreference = $savedEAP
    foreach ($line in $ghSetup) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }
} else {
    Write-Host "  gh CLI:      MISSING (will skip GitHub release)" -ForegroundColor Yellow
    Write-Host "               Install: winget install GitHub.cli" -ForegroundColor Gray
}

# Prevent git from hanging on credential prompts in subprocess mode
$env:GIT_TERMINAL_PROMPT = "0"

if ($hasClient) { Write-Host "  Client:      FOUND ($ClientExe)" -ForegroundColor Green }
else            { Write-Host "  Client:      MISSING" -ForegroundColor Yellow }

# pd-server retired from the release pipeline (in-client listen host).
# Skip the FOUND/MISSING line entirely so the preflight isn't noisy.

if ($hasUpdater) { Write-Host "  Updater:     FOUND ($UpdaterExe)" -ForegroundColor Green }
else             { Write-Host "  Updater:     MISSING (release will omit Updater.exe)" -ForegroundColor Yellow }

if ($hasData)   { Write-Host "  Data:        FOUND ($DataSource)" -ForegroundColor Green }
else            { Write-Host "  Data:        MISSING" -ForegroundColor Yellow }

if ($hasMods)   { Write-Host "  Mods:        FOUND ($ModsSource)" -ForegroundColor Green }
else            { Write-Host "  Mods:        MISSING" -ForegroundColor Yellow }

Write-Host "  Notes:       $(if ($hasNotes) { 'FOUND' } else { 'MISSING (will auto-generate)' })" -ForegroundColor $(if ($hasNotes) { 'Green' } else { 'Yellow' })
Write-Host "  Source:      GitHub auto-generates source archives" -ForegroundColor Gray

if (-not $hasClient) {
    Write-Host ""
    Write-Host "  ERROR: No client build artifact found." -ForegroundColor Red
    Write-Host "  Build the client first via Dev Window v2 or build-headless.ps1." -ForegroundColor Red
    exit 1
}

# ============================================================================
# Step 1: Create distribution directory
# ============================================================================

Write-Host ""
Write-Host "[1/8] Assembling distribution in $DistDir ..." -ForegroundColor Yellow

if (Test-Path $DistDir) {
    Remove-Item $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

# --- Executables ---

if ($hasClient) {
    Copy-Item $ClientExe "$DistDir/PerfectDark.exe"
    Write-Host "  PerfectDark.exe" -ForegroundColor Gray
}

if ($hasServer) {
    Copy-Item $ServerExe "$DistDir/PerfectDarkServer.exe"
    Write-Host "  PerfectDarkServer.exe" -ForegroundColor Gray
}

if ($hasUpdater) {
    Copy-Item $UpdaterExe "$DistDir/Updater.exe"
    Write-Host "  Updater.exe" -ForegroundColor Gray
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
Write-Host "[2/8] Creating zip archive..." -ForegroundColor Yellow

$zipName = $(if ($Nightly) { "PerfectDark-nightly-$DateCode-win64.zip" } else { "PerfectDark-v$Version-win64.zip" })
$zipPath = "dist/$zipName"

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
# Stale sidecars from a previous run would otherwise be re-uploaded against
# a freshly-built ZIP they no longer match.
if (Test-Path "$zipPath.sha256") { Remove-Item "$zipPath.sha256" -Force }
if (Test-Path "$zipPath.sig")    { Remove-Item "$zipPath.sig" -Force }

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
$zipSizeStr = $(if ($zipSize -gt 1MB) { "{0:N1} MB" -f ($zipSize / 1MB) } else { "{0:N0} KB" -f ($zipSize / 1KB) })
Write-Host "  [100%] $zipName ($zipSizeStr)" -ForegroundColor Green

# ============================================================================
# Step 2b: Sign the ZIP (SEC-5 + SEC-6)
# Writes <zip>.sha256 and <zip>.sig next to the ZIP. The updater refuses any
# release that is missing either sidecar, so this step is now required for
# auto-updates to work at all.
# ============================================================================

Write-Host ""
Write-Host "[2b/8] Signing release ..." -ForegroundColor Yellow

$signScript = Join-Path $PSScriptRoot "sign-release.ps1"
if ($DryRun) {
    Write-Host "  [DRY RUN] Would sign $zipPath with tag $ReleaseTag" -ForegroundColor Magenta
} elseif (-not (Test-Path $signScript)) {
    Write-Host "  ERROR: $signScript not found." -ForegroundColor Red
    exit 1
} else {
    # Always use dev-keys/ -- Mike is sole developer, one key is sufficient.
    $devKey = Join-Path $ProjectRoot "dev-keys\ed25519-private.pem"
    if (-not (Test-Path $devKey)) {
        Write-Host "  No signing key found -- auto-generating..." -ForegroundColor Yellow
        & (Join-Path $PSScriptRoot "keygen.ps1")
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  ERROR: Keypair generation failed." -ForegroundColor Red
            exit 1
        }
    }
    $signKey = $devKey
    Write-Host "  Signing key: $signKey" -ForegroundColor Cyan

    & $signScript -ZipPath $zipPath -Tag $ReleaseTag -KeyPath $signKey
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: sign-release.ps1 failed (exit $LASTEXITCODE)." -ForegroundColor Red
        exit 1
    }
}

# ============================================================================
# Step 3: Git tag
# ============================================================================

Write-Host ""
Write-Host "[3/8] Git tagging..." -ForegroundColor Yellow

# Create unified release tag
$existingTag = git tag -l $ReleaseTag 2>$null
if ($existingTag) {
    Write-Host "  Tag $ReleaseTag already exists -- will be replaced by gh release create." -ForegroundColor Yellow
} elseif ($DryRun) {
    Write-Host "  [DRY RUN] Would create tag: $ReleaseTag" -ForegroundColor Magenta
} else {
    git tag -a $ReleaseTag -m "Release $ReleaseTag"
    Write-Host "  Created tag: $ReleaseTag" -ForegroundColor Green
}

# ============================================================================
# Step 4: Push branch + tags
# ============================================================================

Write-Host ""
Write-Host "[4/8] Pushing to remote..." -ForegroundColor Yellow

if ($SkipPush -or $DryRun) {
    Write-Host "  $(if ($DryRun) { '[DRY RUN] ' })Skipping push." -ForegroundColor $(if ($DryRun) { 'Magenta' } else { 'Yellow' })
} else {
    $currentBranch = git branch --show-current
    Write-Host "  Pushing branch '$currentBranch' ..." -ForegroundColor Gray

    # Index must be clean for `git pull --rebase` (staged-but-uncommitted breaks rebase).
    git add -A 2>&1 | Out-Null
    git diff --cached --quiet 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  Committing staged changes before pull --rebase..." -ForegroundColor Gray
        $okCommit = Invoke-ReleaseCommit -Message "chore: auto-commit before release v$Version" -AllowNoVerifyFallback:$ForceCommitNoVerify
        if (-not $okCommit) {
            Write-Host "  ERROR: git commit failed before pull --rebase. Fix hooks or repo state, or use -ForceCommitNoVerify." -ForegroundColor Red
            exit 1
        }
    }

    # Sync with remote before pushing — code sessions may have pushed commits
    # that the local working copy doesn't have yet. Rebase keeps our release
    # commit on top. If a conflict occurs, rebase aborts and the push below
    # will fail cleanly with a meaningful error.
    Write-Host "  Syncing with remote (pull --rebase) ..." -ForegroundColor Gray
    $rebaseOut = git pull --rebase origin $currentBranch 2>&1
    $rebaseExit = $LASTEXITCODE
    foreach ($line in $rebaseOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }
    if ($rebaseExit -ne 0) {
        Write-Host "  WARNING: Rebase failed — aborting rebase and continuing with push." -ForegroundColor Yellow
        git rebase --abort 2>$null
    }

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

    # Push the release tag -- force-replace if it already exists on remote
    $tagsToPush = @($ReleaseTag)
    foreach ($t in $tagsToPush) {
        Write-Host "  Pushing tag '$t' ..." -ForegroundColor Gray
        # Ensure the tag exists locally before pushing; create it if not.
        $localTag = git tag -l $t 2>$null
        if (-not $localTag) {
            Write-Host "  Tag '$t' not found locally -- creating it on HEAD..." -ForegroundColor Yellow
            git tag $t
            Write-Host "  Created local tag: $t" -ForegroundColor Green
        }
        $ErrorActionPreference = "Continue"
        $tagOut = git push origin $t --force --progress 2>&1
        $tagExit = $LASTEXITCODE
        $ErrorActionPreference = $savedEAP
        foreach ($line in $tagOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }

        if ($tagExit -ne 0) {
            Write-Host "  Tag $t may already exist, deleting and retrying..." -ForegroundColor Yellow
            $ErrorActionPreference = "Continue"
            git push origin ":refs/tags/$t" 2>&1 | Out-Null
            $tagOut = git push origin $t --progress 2>&1
            $tagExit = $LASTEXITCODE
            $ErrorActionPreference = $savedEAP
            foreach ($line in $tagOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }
        }

        if ($tagExit -ne 0) {
            Write-Host "  ERROR: Tag push failed for $t (exit $tagExit)" -ForegroundColor Red
            exit 1
        }
        Write-Host "  Tag $t pushed." -ForegroundColor Green
    }
}

# ============================================================================
# Step 5: GitHub release
# ============================================================================

Write-Host ""
Write-Host "[5/8] Creating GitHub releases..." -ForegroundColor Yellow

if ($SkipPush -or $DryRun -or -not $hasGh) {
    $reason = $(if ($DryRun) { "[DRY RUN]" } elseif (-not $hasGh) { "gh CLI not found" } else { "push skipped" })
    Write-Host "  Skipping GitHub releases ($reason)." -ForegroundColor $(if ($DryRun) { 'Magenta' } else { 'Yellow' })
} else {
    $savedEAP = $ErrorActionPreference
    $channel = $(if ($Prerelease) { "Dev" } else { "Stable" })

    # --- Helper: create or overwrite a GitHub release ---
    function Push-GhRelease($tag, $title, $assets, $useNotes) {
        # Check if release already exists
        $ErrorActionPreference = "Continue"
        $existCheck = gh release view $tag 2>&1
        $exists = ($LASTEXITCODE -eq 0)
        $ErrorActionPreference = $savedEAP

        if ($exists) {
            Write-Host "  Release $tag already exists -- deleting and recreating..." -ForegroundColor Yellow
            [System.Media.SystemSounds]::Exclamation.Play()
            $ErrorActionPreference = "Continue"
            gh release delete $tag --yes 2>&1 | Out-Null
            # Also delete the git tag so we can recreate it at the current commit
            gh api -X DELETE "repos/MikeHazeJr/perfect-dark-2/git/refs/tags/$tag" 2>&1 | Out-Null
            git tag -d $tag 2>&1 | Out-Null
            $ErrorActionPreference = $savedEAP
            Write-Host "  Old release deleted." -ForegroundColor Gray
        }

        $ghArgs = @("release", "create", $tag, "--title", $title)
        if ($useNotes -and $hasNotes) {
            $ghArgs += "--notes-file"
            $ghArgs += $ReleaseNotes
        } else {
            $ghArgs += "--generate-notes"
        }
        if ($Prerelease) { $ghArgs += "--prerelease" }
        foreach ($a in $assets) {
            if (Test-Path $a) { $ghArgs += $a }
        }

        Write-Host "  Running: gh $($ghArgs -join ' ')" -ForegroundColor Gray

        $ErrorActionPreference = "Continue"
        $ghOut = gh @ghArgs 2>&1
        $ghExit = $LASTEXITCODE
        $ErrorActionPreference = $savedEAP
        foreach ($line in $ghOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }

        return $ghExit
    }

    # --- Unified release (tag: v{M}.{m}.{p}) ---
    # The zip is the full distribution for new users (client + updater + data + mods).
    # Bare exe files are ALSO uploaded as individual release assets so the in-game updater
    # (updater.c) can find them by exact filename.
    # Updater.exe ships alongside so users can fall back to the standalone recovery tool
    # if a bad release breaks PerfectDark.exe's self-update path.
    # pd-server / PerfectDarkServer.exe retired 2026-04-27 -- see Step 0.
    # GitHub auto-generates source archives.
    Write-Host "  Creating release ($ReleaseTag) ..." -ForegroundColor Cyan
    $assets = @()
    if (Test-Path $zipPath) { $assets += $zipPath }
    # SEC-5 / SEC-6: sidecars MUST ship with the ZIP. The in-game updater
    # rejects any release that's missing either one.
    if (Test-Path "$zipPath.sha256") { $assets += "$zipPath.sha256" }
    if (Test-Path "$zipPath.sig")    { $assets += "$zipPath.sig" }
    # Bare executables for in-game updater
    if (Test-Path "$DistDir/PerfectDark.exe")               { $assets += "$DistDir/PerfectDark.exe" }
    # Standalone updater (recovery tool, zero-DLL)
    if (Test-Path "$DistDir/Updater.exe")                   { $assets += "$DistDir/Updater.exe" }

    $ghExit = Push-GhRelease $ReleaseTag $ReleaseTitle $assets $true
    $script:ReleasePublishOk = ($ghExit -eq 0)

    if ($ghExit -eq 0) {
        Write-Host "  Release created:" -ForegroundColor Green
        Write-Host "  https://github.com/MikeHazeJr/perfect-dark-2/releases/tag/$ReleaseTag" -ForegroundColor Cyan
        [System.Media.SystemSounds]::Asterisk.Play()
    } else {
        Write-Host "  ERROR: Release creation failed (exit $ghExit)." -ForegroundColor Red
        Write-Host "  Run 'gh auth status' to check authentication." -ForegroundColor Red
        [System.Media.SystemSounds]::Hand.Play()
    }
}

# ============================================================================
# Step 6: Prune old Dev (prerelease) releases
# Rolling window: keep the 10 newest prereleases, drop everything older.
# Stable releases are NEVER touched (isPrerelease=false is skipped). Only
# runs after a successful prerelease publish -- on stable releases or when
# the push was skipped/failed we leave GitHub state alone.
# ============================================================================

Write-Host ""
Write-Host "[6/8] Pruning old Dev releases..." -ForegroundColor Yellow

$DevReleaseKeep = 10
$shouldPrune = $Prerelease -and -not $SkipPush -and -not $DryRun -and $hasGh -and ($script:ReleasePublishOk -eq $true)

if (-not $shouldPrune) {
    $reason = "skipped"
    if     (-not $Prerelease)                              { $reason = "stable release -- pruning skipped" }
    elseif ($DryRun)                                       { $reason = "[DRY RUN]" }
    elseif ($SkipPush)                                     { $reason = "push skipped" }
    elseif (-not $hasGh)                                   { $reason = "gh CLI not found" }
    elseif (-not ($script:ReleasePublishOk -eq $true))     { $reason = "release publish failed -- leaving prior Dev releases alone" }
    Write-Host "  Skipping prune ($reason)." -ForegroundColor Gray
} else {
    $savedEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    # `gh release list --json` gives us isPrerelease/isDraft/publishedAt so we
    # can sort and filter safely. --limit 200 covers far more than we'd ever
    # keep around (rolling window caps at 10).
    $listJson = gh release list --limit 200 --json tagName,isPrerelease,isDraft,publishedAt 2>&1
    $listExit = $LASTEXITCODE
    $ErrorActionPreference = $savedEAP

    if ($listExit -ne 0) {
        Write-Host "  WARN: 'gh release list' failed (exit $listExit) -- cannot prune this run." -ForegroundColor Yellow
        foreach ($line in $listJson) { Write-Host "    $($line.ToString())" -ForegroundColor DarkGray }
    } else {
        $releases = @()
        try {
            $releases = @(($listJson | Out-String) | ConvertFrom-Json)
        } catch {
            Write-Host "  WARN: Could not parse gh release list output -- pruning skipped." -ForegroundColor Yellow
        }

        $prereleases = @($releases |
            Where-Object { $_.isPrerelease -eq $true -and $_.isDraft -ne $true } |
            Sort-Object -Property { [DateTime]$_.publishedAt } -Descending)

        Write-Host ("  Dev (prerelease) releases on GitHub: {0} (keeping newest {1})" -f $prereleases.Count, $DevReleaseKeep) -ForegroundColor Gray

        if ($prereleases.Count -le $DevReleaseKeep) {
            Write-Host "  No pruning needed." -ForegroundColor Gray
        } else {
            $victims = @($prereleases | Select-Object -Skip $DevReleaseKeep)
            Write-Host ("  Deleting {0} old Dev release(s) + tag(s) ..." -f $victims.Count) -ForegroundColor Gray
            foreach ($v in $victims) {
                $tag = $v.tagName
                Write-Host ("    - {0} (published {1})" -f $tag, $v.publishedAt) -ForegroundColor DarkGray

                $ErrorActionPreference = "Continue"
                # --cleanup-tag removes both the release and its git tag on the remote.
                $delOut = gh release delete $tag --yes --cleanup-tag 2>&1
                $delExit = $LASTEXITCODE
                $ErrorActionPreference = $savedEAP
                foreach ($line in $delOut) { Write-Host "      $($line.ToString())" -ForegroundColor DarkGray }

                if ($delExit -ne 0) {
                    # Fallback: delete release first, then tag via the git refs API.
                    # Older gh versions lack --cleanup-tag.
                    $ErrorActionPreference = "Continue"
                    gh release delete $tag --yes 2>&1 | Out-Null
                    gh api -X DELETE "repos/MikeHazeJr/perfect-dark-2/git/refs/tags/$tag" 2>&1 | Out-Null
                    $ErrorActionPreference = $savedEAP
                }

                # Drop the local tag too so this worktree doesn't keep resurrecting it.
                $ErrorActionPreference = "Continue"
                git tag -d $tag 2>&1 | Out-Null
                $ErrorActionPreference = $savedEAP
            }
            Write-Host "  Prune complete." -ForegroundColor Green
        }
    }
}

# ============================================================================
# Step 7: Local backup + cleanup
# ============================================================================

Write-Host ""
Write-Host "[7/8] Cleanup and backup..." -ForegroundColor Yellow

# For STABLE releases, keep a local backup of the zip
if (-not $Prerelease -and (Test-Path $zipPath)) {
    $backupDir = Join-Path $ProjectRoot "backups"
    if (-not (Test-Path $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    }
    $backupDest = Join-Path $backupDir $zipName
    Copy-Item $zipPath $backupDest -Force
    Write-Host "  Stable backup: $backupDest" -ForegroundColor Green
}

# Remove the staging directory (executables, data, mods, DLLs) -- GitHub has them
if (Test-Path $DistDir) {
    Remove-Item $DistDir -Recurse -Force
    Write-Host "  Cleaned staging: $DistDir" -ForegroundColor Gray
}

# Remove the zip too -- GitHub is the source of truth, stable backup is saved above
if (-not $DryRun -and -not $SkipPush -and (Test-Path $zipPath)) {
    Remove-Item $zipPath -Force
    Write-Host "  Cleaned zip: $zipPath" -ForegroundColor Gray
}
# Clean sidecars along with the zip — keeping them on disk with no matching
# zip just litters dist/ and confuses the next release run's staleness check.
if (-not $DryRun -and -not $SkipPush) {
    if (Test-Path "$zipPath.sha256") { Remove-Item "$zipPath.sha256" -Force }
    if (Test-Path "$zipPath.sig")    { Remove-Item "$zipPath.sig" -Force }
}

# Clean up any old dist/{tag} staging folders left from previous releases
Get-ChildItem "dist" -Directory -ErrorAction SilentlyContinue | Where-Object {
    $_.Name -match '^(client-)?v\d+\.\d+\.\d+'
} | ForEach-Object {
    Write-Host "  Cleaning old staging: $($_.FullName)" -ForegroundColor Gray
    Remove-Item $_.FullName -Recurse -Force
}

# Prune old release zips. dist/ accumulates from prior runs (e.g. -SkipPush or
# early failures leave zips behind). Keep only the N most recent by mtime.
$ReleaseZipKeep = 3
$BackupZipKeep  = 5

function Invoke-PruneOldZips {
    param(
        [string]$Directory,
        [int]$Keep,
        [string]$Label
    )
    if (-not (Test-Path $Directory)) { return }
    $zips = @(Get-ChildItem -Path $Directory -Filter "PerfectDark-*.zip" -File -ErrorAction SilentlyContinue `
        | Sort-Object LastWriteTime -Descending)
    if ($zips.Count -le $Keep) { return }
    $victims = $zips | Select-Object -Skip $Keep
    $bytes = ($victims | Measure-Object -Property Length -Sum).Sum
    $mb = [math]::Round($bytes / 1MB, 1)
    Write-Host "  Pruning $Label zips: removing $($victims.Count) old file(s), $mb MB" -ForegroundColor Gray
    foreach ($v in $victims) {
        Write-Host "    - $($v.Name) ($([math]::Round($v.Length / 1MB, 1)) MB)" -ForegroundColor DarkGray
        Remove-Item $v.FullName -Force -ErrorAction Continue
    }
}

Invoke-PruneOldZips -Directory (Join-Path $ProjectRoot "dist")    -Keep $ReleaseZipKeep -Label "dist/"
Invoke-PruneOldZips -Directory (Join-Path $ProjectRoot "backups") -Keep $BackupZipKeep  -Label "backups/"

# ============================================================================
# Summary
# ============================================================================

Write-Host ""
Write-Host ("=" * 70) -ForegroundColor Green
Write-Host "  $ReleaseTitle -- COMPLETE" -ForegroundColor Green
Write-Host ("=" * 70) -ForegroundColor Green
Write-Host ""

$zipSizeDisplay = $(if (Test-Path $zipPath) { $zipSizeStr } else { "(uploaded + cleaned)" })
Write-Host "  Zip:    $zipName $zipSizeDisplay" -ForegroundColor White
if (-not $Prerelease) {
    Write-Host "  Backup: backups/$zipName" -ForegroundColor White
}
Write-Host "  Release: https://github.com/MikeHazeJr/perfect-dark-2/releases/tag/$ReleaseTag" -ForegroundColor Cyan
Write-Host ""
