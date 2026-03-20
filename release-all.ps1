# Perfect Dark 2 -- Full Release Pipeline
# Builds and pushes releases on both the release and dev branches.
#
# Usage:
#   .\release-all.ps1              # Full pipeline: build + package + push both branches
#   .\release-all.ps1 -SkipPush    # Build and package only (no GitHub release)
#   .\release-all.ps1 -DryRun      # Show what would happen without doing anything
#   .\release-all.ps1 -SkipBuild   # Skip compilation (use existing build/ artifacts)
#
# What it does:
#   1. Preflight checks (tools, clean tree, branches exist)
#   2. Switch to 'release' branch -> build client + server -> package + push as stable
#   3. Switch to 'dev' branch -> build client + server -> package + push as prerelease
#   4. Return to original branch
#
# Prerequisites:
#   - MSYS2/MinGW installed (cmake, make, gcc)
#   - gh CLI installed and authenticated
#   - libcurl installed: pacman -S mingw-w64-x86_64-curl
#   - Git working tree clean on all branches

param(
    [switch]$SkipPush,
    [switch]$SkipBuild,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$OriginalBranch = git branch --show-current

# ============================================================================
# Helpers
# ============================================================================

function Write-Step($step, $total, $msg) {
    Write-Host ""
    Write-Host ("=" * 70) -ForegroundColor DarkGray
    Write-Host "  [$step/$total] $msg" -ForegroundColor Cyan
    Write-Host ("=" * 70) -ForegroundColor DarkGray
    Write-Host ""
}

function Write-OK($msg)   { Write-Host "  OK  $msg" -ForegroundColor Green }
function Write-Warn($msg) { Write-Host "  !!  $msg" -ForegroundColor Yellow }
function Write-Fail($msg) { Write-Host "  XX  $msg" -ForegroundColor Red }
function Write-Info($msg) { Write-Host "  --  $msg" -ForegroundColor Gray }

function Get-VersionFromCMake {
    $cmake = Get-Content "CMakeLists.txt" -Raw
    $major = if ($cmake -match 'VERSION_SEM_MAJOR\s+(\d+)') { $matches[1] } else { "0" }
    $minor = if ($cmake -match 'VERSION_SEM_MINOR\s+(\d+)') { $matches[1] } else { "0" }
    $patch = if ($cmake -match 'VERSION_SEM_PATCH\s+(\d+)') { $matches[1] } else { "0" }
    $dev   = if ($cmake -match 'VERSION_SEM_DEV\s+(\d+)')   { $matches[1] } else { "0" }
    $label = if ($cmake -match 'VERSION_SEM_LABEL\s+"([^"]*)"') { $matches[1] } else { "" }

    if ([int]$dev -gt 0) {
        return "$major.$minor.$patch-dev.$dev"
    } elseif ($label -ne "") {
        return "$major.$minor.$patch$label"
    } else {
        return "$major.$minor.$patch"
    }
}

function Invoke-Build($target) {
    # target: "client" or "server"
    $cmakePath = "C:/msys64/usr/bin/cmake.exe"
    $makePath  = "C:/msys64/usr/bin/make.exe"
    $ccPath    = "C:/msys64/mingw64/bin/cc.exe"
    $cores     = $env:NUMBER_OF_PROCESSORS
    if (!$cores) { $cores = 4 }

    # Use cmake if available in PATH, otherwise try MSYS2 path
    $cmake = if (Get-Command "cmake" -ErrorAction SilentlyContinue) { "cmake" } else { $cmakePath }

    if (-not (Test-Path "build/CMakeCache.txt")) {
        Write-Info "Configuring CMake..."
        & $cmake -G "Unix Makefiles" `
            -DCMAKE_MAKE_PROGRAM="$makePath" `
            -DCMAKE_C_COMPILER="$ccPath" `
            -B build -S .
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    }

    if ($target -eq "server") {
        Write-Info "Building dedicated server..."
        & $cmake --build build --target pd-server -- -j$cores -k
    } else {
        Write-Info "Building client..."
        & $cmake --build build --target pd -- -j$cores -k
    }

    if ($LASTEXITCODE -ne 0) { throw "Build failed for $target" }
}

function Invoke-PostBuildCopy {
    # Copy post-batch-addin files (DLLs, data, mods) into build directory
    $addinDir = "post-batch-addin"
    if (-not (Test-Path $addinDir)) {
        # Check parent -- some setups have it one level up
        $parentAddin = "../post-batch-addin"
        if (Test-Path $parentAddin) { $addinDir = $parentAddin }
    }
    if (Test-Path $addinDir) {
        Write-Info "Copying post-build files from $addinDir..."
        Copy-Item "$addinDir/*" "build/" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# ============================================================================
# Step 1: Preflight
# ============================================================================

Write-Step 1 6 "Preflight checks"

# Git
$dirty = git status --porcelain
if ($dirty) {
    Write-Warn "Working tree is not clean. Uncommitted changes:"
    git status --short
    Write-Host ""
    $confirm = Read-Host "Continue anyway? (y/N)"
    if ($confirm -ne "y") { exit 1 }
}

# Branches exist
$branches = git branch --list | ForEach-Object { $_.Trim().TrimStart("* ") }
$hasRelease = $branches -contains "release"
$hasDev = $branches -contains "dev"

if ($hasRelease) { Write-OK "Branch 'release' exists" } else { Write-Fail "Branch 'release' not found"; exit 1 }
if ($hasDev)     { Write-OK "Branch 'dev' exists" }     else { Write-Fail "Branch 'dev' not found"; exit 1 }

# Tools
if (Get-Command "gh" -ErrorAction SilentlyContinue) { Write-OK "gh CLI found" } else { Write-Warn "gh CLI not found -- will skip GitHub release" }

# Show versions
git checkout release --quiet
$releaseVer = Get-VersionFromCMake
git checkout dev --quiet
$devVer = Get-VersionFromCMake
git checkout $OriginalBranch --quiet

Write-Host ""
Write-Info "Release version: v$releaseVer"
Write-Info "Dev version:     v$devVer"
Write-Info "Current branch:  $OriginalBranch"
Write-Host ""

if ($DryRun) {
    Write-Warn "[DRY RUN] Would build + release v$releaseVer (stable) and v$devVer (prerelease)"
    Write-Warn "[DRY RUN] No changes will be made."
    Write-Host ""
}

# ============================================================================
# Step 2: Build + Package Release Branch
# ============================================================================

Write-Step 2 6 "Building RELEASE branch (v$releaseVer)"

git checkout release --quiet
Write-OK "Switched to 'release'"

if (-not $SkipBuild -and -not $DryRun) {
    # Clear CMake cache to ensure version is picked up fresh
    if (Test-Path "build/CMakeCache.txt") {
        Remove-Item "build/CMakeCache.txt" -Force
        Write-Info "Cleared CMakeCache.txt for clean version detection"
    }

    Invoke-Build "client"
    Write-OK "Client built"

    Invoke-Build "server"
    Write-OK "Server built"

    Invoke-PostBuildCopy
} else {
    Write-Info "$(if ($DryRun) { '[DRY RUN] ' })Skipping build"
}

# Verify artifacts
$hasClient = Test-Path "build/pd.x86_64.exe"
$hasServer = Test-Path "build/pd-server.x86_64.exe"
if ($hasClient) { Write-OK "Client artifact: build/pd.x86_64.exe" } else { Write-Warn "Client artifact missing" }
if ($hasServer) { Write-OK "Server artifact: build/pd-server.x86_64.exe" } else { Write-Warn "Server artifact missing" }

# ============================================================================
# Step 3: Package + Push Release
# ============================================================================

Write-Step 3 6 "Packaging RELEASE v$releaseVer"

if ($DryRun) {
    Write-Info "[DRY RUN] Would run: .\release.ps1 -Version $releaseVer"
} else {
    $releaseArgs = @()
    if ($SkipPush) { $releaseArgs += "-SkipPush" }
    & ".\release.ps1" -Version $releaseVer @releaseArgs
}

# ============================================================================
# Step 4: Build + Package Dev Branch
# ============================================================================

Write-Step 4 6 "Building DEV branch (v$devVer)"

git checkout dev --quiet
Write-OK "Switched to 'dev'"

if (-not $SkipBuild -and -not $DryRun) {
    if (Test-Path "build/CMakeCache.txt") {
        Remove-Item "build/CMakeCache.txt" -Force
        Write-Info "Cleared CMakeCache.txt for clean version detection"
    }

    Invoke-Build "client"
    Write-OK "Client built"

    Invoke-Build "server"
    Write-OK "Server built"

    Invoke-PostBuildCopy
} else {
    Write-Info "$(if ($DryRun) { '[DRY RUN] ' })Skipping build"
}

$hasClient = Test-Path "build/pd.x86_64.exe"
$hasServer = Test-Path "build/pd-server.x86_64.exe"
if ($hasClient) { Write-OK "Client artifact: build/pd.x86_64.exe" } else { Write-Warn "Client artifact missing" }
if ($hasServer) { Write-OK "Server artifact: build/pd-server.x86_64.exe" } else { Write-Warn "Server artifact missing" }

# ============================================================================
# Step 5: Package + Push Dev (as prerelease)
# ============================================================================

Write-Step 5 6 "Packaging DEV v$devVer (prerelease)"

if ($DryRun) {
    Write-Info "[DRY RUN] Would run: .\release.ps1 -Version $devVer -Prerelease"
} else {
    $devArgs = @("-Prerelease")
    if ($SkipPush) { $devArgs += "-SkipPush" }
    & ".\release.ps1" -Version $devVer @devArgs
}

# ============================================================================
# Step 6: Return to original branch + summary
# ============================================================================

Write-Step 6 6 "Cleanup"

git checkout $OriginalBranch --quiet
Write-OK "Returned to '$OriginalBranch'"

Write-Host ""
Write-Host ("=" * 70) -ForegroundColor Green
Write-Host "  RELEASE PIPELINE COMPLETE" -ForegroundColor Green
Write-Host ("=" * 70) -ForegroundColor Green
Write-Host ""
Write-Host "  Release (stable):     v$releaseVer" -ForegroundColor White
Write-Host "  Dev (prerelease):     v$devVer" -ForegroundColor White
Write-Host ""

if (-not $SkipPush -and -not $DryRun) {
    Write-Host "  GitHub releases:" -ForegroundColor Cyan
    Write-Host "    https://github.com/MikeHazeJr/perfect-dark-2/releases/tag/v$releaseVer" -ForegroundColor Gray
    Write-Host "    https://github.com/MikeHazeJr/perfect-dark-2/releases/tag/v$devVer" -ForegroundColor Gray
    Write-Host ""
    Write-Host "  Update system test:" -ForegroundColor Yellow
    Write-Host "    1. Launch the v$releaseVer client" -ForegroundColor Gray
    Write-Host "    2. Set update channel to 'Dev' in Settings" -ForegroundColor Gray
    Write-Host "    3. It should detect v$devVer as available" -ForegroundColor Gray
}

Write-Host ""
