# Perfect Dark 2 -- Release Script
# Packages client + updater for distribution.
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
#   - Updater.exe (standalone GUI updater; recovery path if client self-update breaks)
#   - pd.ini (default config), put_your_rom_here.txt (BYOR onboarding text)
#   - data/<romid>/... (skeleton for first-launch ROM extraction; ships any
#     pre-generated per-asset .pd<ext> seeds present in Build/data)
#
# B-321 (2026-05-03): the dist root is the install root.  PerfectDark.exe,
# pd.ini, and put_your_rom_here.txt all live at the top level of the zip
# alongside data/ and mods/.  Pre-fix, everything was nested inside an extra
# data/ folder which made fsDataDir() resolve to data/data/<romid>/...
# (one level too deep).  data/ at install root now contains ONLY the
# extracted-asset tree (data/<romid>/...).  pd.ini, ROM, and the onboarding
# text live at install root, NOT inside data/.
#
# NOT included: pd-server (deprecated), pd-tests (dev-only), ROM files (.z64),
# the legacy `base/` (Step 5 retired), `mod source files/`, top-level README.
# Source code is NOT included -- GitHub auto-generates source archives.
#
# Post-release housekeeping:
#   - Dev (prerelease) tags are pruned to the newest 10 after a successful push;
#     stable releases are never touched.
#
# Prerequisites:
#   - gh CLI installed and authenticated (gh auth login)
#   - Successful build of client (+ updater)
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
                $orig = Get-Content "CMakeLists.txt" -Raw -ErrorAction Stop
                $cmake = $orig -replace '(VERSION_SEM_MAJOR\s+)\d+', ("`${1}" + $parts[0])
                $cmake = $cmake -replace '(VERSION_SEM_MINOR\s+)\d+', ("`${1}" + $parts[1])
                $cmake = $cmake -replace '(VERSION_SEM_PATCH\s+)\d+', ("`${1}" + $parts[2])
                # No-op skip + no-BOM UTF-8 encoder. Set-Content -Encoding UTF8
                # on PowerShell 5.1 emits a BOM that leaves the working tree
                # dirty after every release (S477).
                if ($cmake -ne $orig) {
                    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
                    [System.IO.File]::WriteAllText((Resolve-Path "CMakeLists.txt").Path, $cmake, $utf8NoBom)
                    Write-Host "  Synced CMakeLists.txt to v$Version" -ForegroundColor Gray
                } else {
                    Write-Host "  CMakeLists.txt already at v$Version (no rewrite)" -ForegroundColor Gray
                }
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
        [string]$Subject,
        [string]$Body,
        [string]$Refs = "Refs: c120",
        [switch]$AllowNoVerifyFallback
    )

    $args = @("commit", "-m", $Subject)
    if ($Body) { $args += @("-m", $Body) }
    if ($Refs) { $args += @("-m", $Refs) }

    $commitOut = @(git @args 2>&1)
    $commitCode = $LASTEXITCODE
    foreach ($line in $commitOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }

    if ($commitCode -eq 0) {
        return $true
    }

    if (-not $AllowNoVerifyFallback) {
        return $false
    }

    Write-Host "  Commit failed; retrying with --no-verify (--force commit mode)." -ForegroundColor Yellow
    $args2 = @("commit", "--no-verify", "-m", $Subject)
    if ($Body) { $args2 += @("-m", $Body) }
    if ($Refs) { $args2 += @("-m", $Refs) }
    $commitOut2 = @(git @args2 2>&1)
    $commitCode2 = $LASTEXITCODE
    foreach ($line in $commitOut2) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }
    return ($commitCode2 -eq 0)
}

function Get-ReleaseCommitBody {
    param(
        [string]$Version,
        [string]$Stage
    )

    return "The release pipeline found staged or pending changes during $Stage for v$Version and needs a clean commit before tagging, rebasing, or pushing. This automated commit keeps the release flow compatible with the c120 commit-message hook instead of bypassing validation."
}

# Same layout as build-headless.ps1 / dev-window Copy-AddinFiles (B-326):
# *.z64 from post-batch-addin/data (recursive) -> $BuildDir (install root next
# to PerfectDark.exe). Everything else under addin data/ -> $BuildDir/data/.
# put_your_rom_here.txt is hoisted to install root if present under addin.
# Runs after the client build so local launches work even if a later release
# step fails. Safe to call multiple times; overwrite-on-copy.
function Copy-RomAddinIntoBuild {
    $addinData = Join-Path $ProjectRoot "..\post-batch-addin\data"
    $buildData = Join-Path $BuildDir "data"
    if (-not (Test-Path $addinData)) {
        Write-Host "  [rom-copy] post-batch-addin/data not found -- skipping ROM copy." -ForegroundColor Yellow
        return
    }
    try {
        Get-ChildItem -Path $addinData -Filter "*.z64" -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
            $destRom = Join-Path $BuildDir $_.Name
            try { Copy-Item -Path $_.FullName -Destination $destRom -Force -ErrorAction Stop } catch {}
        }
        if (-not (Test-Path $buildData)) {
            New-Item -ItemType Directory -Path $buildData -Force | Out-Null
        }
        $robocopy = Get-Command robocopy.exe -ErrorAction SilentlyContinue
        $copied = $false
        if ($null -ne $robocopy) {
            & robocopy.exe $addinData $buildData /E /XO /XF "*.z64" /NFL /NDL /NJH /NJS /NP | Out-Null
            if ($LASTEXITCODE -le 7) { $copied = $true }
        }
        if (-not $copied) {
            Get-ChildItem -Path $addinData -Recurse -File -ErrorAction Stop | Where-Object { $_.Extension -ne ".z64" } | ForEach-Object {
                $rel = $_.FullName.Substring($addinData.Length).TrimStart('\', '/')
                $dest = Join-Path $buildData $rel
                $destDir = Split-Path $dest -Parent
                if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }
                Copy-Item -Path $_.FullName -Destination $dest -Force -ErrorAction SilentlyContinue
            }
        }
        $hoistByor = Join-Path $buildData "put_your_rom_here.txt"
        $rootByor = Join-Path $BuildDir "put_your_rom_here.txt"
        if (Test-Path -LiteralPath $hoistByor) {
            try { Move-Item -LiteralPath $hoistByor -Destination $rootByor -Force -ErrorAction Stop } catch {}
        }
        Write-Host "  [rom-copy] post-batch-addin -> $BuildDir (ROM + BYOR text at install root; rest under data/)." -ForegroundColor Green
    } catch {
        Write-Host "  [rom-copy] WARN: copy failed: $_" -ForegroundColor Yellow
    }
}

Write-Host ""
if ($SkipBuild) {
    Write-Host "[0/8] Skipping rebuild (-SkipBuild set; using existing artifacts in Build/)." -ForegroundColor Gray

    # Caller (e.g. dev-window-v2) already built the client. Stage addin
    # (ROM at Build root, rest under Build/data/) so Mike can launch locally
    # even if a later step in this script fails.
    Copy-RomAddinIntoBuild

    # Commit any pending changes so the release tag lands on a clean commit
    Write-Host "  [pre-release] Committing any pending changes..." -ForegroundColor Gray
    $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $statusOut = git -C $ProjectRoot status --porcelain 2>&1
    if ($statusOut) {
        git -C $ProjectRoot add -A 2>&1 | Out-Null
        if (Invoke-ReleaseCommit `
                -Subject "Tooling - c120: Commit pre-release changes for v$Version" `
                -Body (Get-ReleaseCommitBody -Version $Version -Stage "pre-release") `
                -AllowNoVerifyFallback:$ForceCommitNoVerify) {
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
    # only client). A missing Updater is a warning, not a release
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
        if (Invoke-ReleaseCommit `
                -Subject "Tooling - c120: Commit pre-build changes for v$Version" `
                -Body (Get-ReleaseCommitBody -Version $Version -Stage "pre-build") `
                -AllowNoVerifyFallback:$ForceCommitNoVerify) {
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

            # Client is now on disk and known-good. Stage addin (ROM at Build
            # root) so Mike can launch locally even if a later release step fails.
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
$UpdaterExe = $(if (Test-Path (Join-Path $BuildDir "Updater.exe"))           { Join-Path $BuildDir "Updater.exe" }           else { "" })

# Data -- prefer Build/ copy, fall back to post-batch-addin
$DataSource = $(if (Test-Path (Join-Path $BuildDir "data"))  { Join-Path $BuildDir "data" }
                elseif (Test-Path "../post-batch-addin/data") { "../post-batch-addin/data" }
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
$script:GhExe = $null
$hasClient  = $ClientExe  -ne ""
$hasUpdater = $UpdaterExe -ne ""
$hasData    = $DataSource -ne ""
$hasNotes   = Test-Path $ReleaseNotes

if ($hasGh) {
    $ghPath = $(if ($ghCmd -is [string]) { $ghCmd } else { $ghCmd.Source })
    $script:GhExe = $ghPath
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

function Quote-NativeArg([string]$arg) {
    if ($null -eq $arg) { return '""' }
    if ($arg -notmatch '[\s"]') { return $arg }
    return '"' + ($arg -replace '\\(?=\\*")', '$0$0' -replace '"', '\"') + '"'
}

function Invoke-GhStreaming([string[]]$Arguments, [string]$Label) {
    if (-not $script:GhExe) { return 1 }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $script:GhExe
    $psi.Arguments = (($Arguments | ForEach-Object { Quote-NativeArg $_ }) -join " ")
    $psi.WorkingDirectory = $ProjectRoot
    $psi.UseShellExecute = $false
    # Do not use BeginOutputReadLine/DataReceivedEventHandler here. Those
    # callbacks can run on a .NET thread without a PowerShell runspace, and a
    # Write-Host from that callback can crash the release process. Let gh
    # inherit this script's redirected stdout/stderr instead; Dev Window v2
    # already captures this process output and persists it to the rolling log.
    $psi.RedirectStandardOutput = $false
    $psi.RedirectStandardError = $false
    $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables["GH_PROMPT_DISABLED"] = "1"
    $psi.EnvironmentVariables["GIT_TERMINAL_PROMPT"] = "0"

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $lastHeartbeat = [DateTime]::Now

    [void]$proc.Start()

    while (-not $proc.WaitForExit(1000)) {
        if (([DateTime]::Now - $lastHeartbeat).TotalSeconds -ge 15) {
            $elapsedSeconds = [int](([DateTime]::Now - $proc.StartTime).TotalSeconds)
            Write-Host ("    {0}: still waiting for gh {1} ({2}s elapsed)" -f (Get-Date -Format "HH:mm:ss"), $Label, $elapsedSeconds) -ForegroundColor Gray
            $lastHeartbeat = [DateTime]::Now
        }
    }

    $proc.WaitForExit()
    return $proc.ExitCode
}

if ($hasClient) { Write-Host "  Client:      FOUND ($ClientExe)" -ForegroundColor Green }
else            { Write-Host "  Client:      MISSING" -ForegroundColor Yellow }

if ($hasUpdater) { Write-Host "  Updater:     FOUND ($UpdaterExe)" -ForegroundColor Green }
else             { Write-Host "  Updater:     MISSING (release will omit Updater.exe)" -ForegroundColor Yellow }

if ($hasData)   { Write-Host "  Data:        FOUND ($DataSource)" -ForegroundColor Green }
else            { Write-Host "  Data:        MISSING" -ForegroundColor Yellow }

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

if ($hasUpdater) {
    Copy-Item $UpdaterExe "$DistDir/Updater.exe"
    Write-Host "  Updater.exe" -ForegroundColor Gray
}

# --- Data folder + install-root files ---
#
# B-321 (2026-05-03): the dist root IS the install root.  Files at the top
# level of $DataSource (pd.ini, the ROM, etc.) land at install root in the
# zip; nested data/<romid>/... stays nested.  The legacy `base/` tree (Step 5
# retired the .pdbase aggregate) and `mod source files/` directories are
# explicitly excluded.

if ($hasData) {
    $dataSourceFull = (Resolve-Path $DataSource).Path
    $skippedDirs = @{}
    $copyPlan = @()

    foreach ($file in (Get-ChildItem $DataSource -Recurse -File)) {
        # Skip ROM files unconditionally (BYOR; user supplies their own).
        if ($file.Extension -eq ".z64") {
            continue
        }

        $relativePath = $file.FullName.Substring($dataSourceFull.Length + 1)
        $relForward   = $relativePath -replace '\\', '/'

        # Drop legacy / non-shipping trees:
        #   base/              -- Step 5 retired the .pdbase aggregate.
        #   mod source files/  -- dev scratch, never shipped.
        #   README.txt         -- B-322 retired in favour of put_your_rom_here.txt.
        if ($relForward -match '^(base/|mod source files/|README\.txt$)') {
            $top = $relForward -split '/' | Select-Object -First 1
            $skippedDirs[$top] = $true
            continue
        }

        # Layout: $DataSource is the install root.  Relative paths under it
        # stay relative under $DistDir.  Files at the top of Build/data
        # (e.g. pd.ini) land at install root; files at Build/data/data/<romid>
        # land at dist/data/<romid>.  No extra wrapping.
        $copyPlan += [pscustomobject]@{
            Source      = $file.FullName
            Destination = $relForward
        }
    }

    Write-Host "  Copying $($copyPlan.Count) files (excluding *.z64 ROM files) ..." -ForegroundColor Gray
    foreach ($entry in $copyPlan) {
        $destPath = Join-Path $DistDir $entry.Destination
        $destDir  = Split-Path $destPath -Parent
        if (-not (Test-Path $destDir)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        }
        Copy-Item $entry.Source $destPath
    }

    foreach ($k in $skippedDirs.Keys) {
        Write-Host "    EXCLUDED: $k (legacy retired or non-shipping)" -ForegroundColor DarkYellow
    }
    $romFiles = Get-ChildItem $DataSource -Filter "*.z64" -Recurse
    if ($romFiles) {
        foreach ($rom in $romFiles) {
            Write-Host "    EXCLUDED: $($rom.Name) (ROM file)" -ForegroundColor DarkYellow
        }
    }
} else {
    Write-Host "  data/ -- NOT FOUND (skipped)" -ForegroundColor Yellow
}

# --- put_your_rom_here.txt (B-322: replaces README.txt) ---
#
# Lives at the install root next to PerfectDark.exe so the user sees it
# the moment they extract the zip -- the filename itself is the call-to-
# action.  Contents subsume the prior data/README.txt so there is one
# place to look for ROM-placement instructions.

$readmePath = "$DistDir/put_your_rom_here.txt"
$readmeContent = @"
Perfect Dark 2 -- Place your ROM here
======================================

This file is the placeholder for your ROM.  When you legally obtain a
copy of Perfect Dark (Nintendo 64, NTSC-Final), put it next to this file
(install root, beside PerfectDark.exe) and rename it to:

    pd.ntsc-final.z64

You can delete this text file after the ROM is in place.  The game will
not run without the ROM.

REQUIRED ROM FILE
-----------------
- Region: NTSC-Final (USA).  PAL and JPN ROMs are NOT supported.
- Format: .z64 (big-endian).  If you have a .n64 or .v64 ROM, convert it
  to .z64 with a tool like Tool64 first; the game will attempt auto-
  conversion on launch but a known-good .z64 is the safe path.
- Exact filename: pd.ntsc-final.z64 (lowercase).

FIRST LAUNCH
------------
PerfectDark.exe will detect pd.ntsc-final.z64 at the install root and
extract the assets it needs into data/ntsc-final/.  This is a one-time
process that takes a few seconds.  After extraction the ROM is no longer
required for gameplay; keep it around if you ever need to reset assets.

BRING YOUR OWN ROM (BYOR)
--------------------------
Perfect Dark 2 ships with no copyrighted ROM data.  You must supply your
own legally obtained copy of Perfect Dark (N64, NTSC-Final).

TROUBLESHOOTING
---------------
- Game will not start: confirm the ROM is named exactly pd.ntsc-final.z64
  and placed at the install root next to PerfectDark.exe.
- Extraction fails: verify the region (NTSC-Final) and format (.z64
  big-endian).  PAL and JPN ROMs are not supported.
- Missing assets after update: re-run extraction by placing the ROM back
  at the install root and relaunching.
- Updater issues: run Updater.exe (in this release) to recover.

INSTALL LAYOUT
--------------
After first launch with a valid ROM, the install will look like:

    PerfectDark.exe
    Updater.exe
    pd.ini                     (config)
    pd.ntsc-final.z64          (your ROM)
    data/ntsc-final/           (extracted assets)
    mods/                      (your installed mods)

The data/ and mods/ folders live at install root next to the executable;
do not move them to a sub-folder or the game will not find them.
"@
Set-Content -LiteralPath $readmePath -Value $readmeContent -Encoding UTF8
Write-Host "  put_your_rom_here.txt (ROM placement instructions)" -ForegroundColor Gray

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
        $okCommit = Invoke-ReleaseCommit `
            -Subject "Tooling - c120: Commit release rebase changes for v$Version" `
            -Body (Get-ReleaseCommitBody -Version $Version -Stage "release rebase") `
            -AllowNoVerifyFallback:$ForceCommitNoVerify
        if (-not $okCommit) {
            Write-Host "  ERROR: git commit failed before pull --rebase. Fix hooks or repo state, or use -ForceCommitNoVerify." -ForegroundColor Red
            exit 1
        }
    }

    # Pre-rebase working-tree refresh + diagnostics (S481).
    #
    # Mike hit "error: cannot rebase: You have unstaged changes" on a release.
    # Root cause: with core.autocrlf=true (Mike's local config) AND the
    # 2026-04-25 .gitattributes change (`* text=auto eol=lf`), text files
    # often have CRLF on disk while the index has LF. `git add -A` normalises
    # CRLF -> LF for the index so `--cached --quiet` returns 0 (no commit
    # needed), but the working-tree file still has CRLF. `git pull --rebase`
    # then runs its own working-tree-vs-HEAD check on raw bytes and refuses
    # to rebase because the file looks "modified."
    #
    # `git update-index --refresh` clears the stale modified flag for any
    # file whose content matches HEAD after .gitattributes-driven
    # normalisation. Idempotent and safe; it does not touch files that are
    # genuinely modified.
    git update-index --refresh -q --unmerged 2>&1 | Out-Null

    # Capture working-tree state for the log so the next rebase failure has
    # a clear paper trail showing exactly which file blocked the rebase.
    $preRebaseStatus = git status --porcelain 2>&1
    if ($preRebaseStatus -and $preRebaseStatus.Count -gt 0) {
        Write-Host "  [pre-rebase status] working tree shows changes:" -ForegroundColor Yellow
        foreach ($line in $preRebaseStatus) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }
    }

    # Sync with remote before pushing — code sessions may have pushed commits
    # that the local working copy doesn't have yet. Rebase keeps our release
    # commit on top. If a conflict occurs, rebase aborts and the push below
    # will fail cleanly with a meaningful error.
    Write-Host "  Syncing with remote (pull --rebase) ..." -ForegroundColor Gray
    $rebaseOut = git pull --rebase origin $currentBranch 2>&1
    $rebaseExit = $LASTEXITCODE

    # Recovery: if the rebase failed specifically with "unstaged changes"
    # (the autocrlf / .gitattributes drift class above) and the only
    # difference is line-ending, force a clean checkout of those files
    # from the index to bring the working tree byte-for-byte in sync.
    # `git checkout-index -a -f` is safe here because we just ran
    # `git add -A` and committed-or-confirmed-clean above, so the index
    # holds the canonical content.
    if ($rebaseExit -ne 0) {
        $rebaseText = ($rebaseOut | ForEach-Object { $_.ToString() }) -join "`n"
        if ($rebaseText -match 'unstaged changes|cannot rebase|would be overwritten') {
            Write-Host "  Rebase blocked by working-tree drift; forcing checkout-index from staged state and retrying..." -ForegroundColor Yellow
            git rebase --abort 2>&1 | Out-Null
            git checkout-index -a -f 2>&1 | Out-Null
            git update-index --refresh -q --unmerged 2>&1 | Out-Null
            $postFix = git status --porcelain 2>&1
            if ($postFix -and $postFix.Count -gt 0) {
                Write-Host "  [post-fix status] still dirty after checkout-index:" -ForegroundColor Yellow
                foreach ($line in $postFix) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }
            } else {
                Write-Host "  Working tree clean after recovery; retrying rebase..." -ForegroundColor Gray
            }
            $rebaseOut = git pull --rebase origin $currentBranch 2>&1
            $rebaseExit = $LASTEXITCODE
        }
    }

    foreach ($line in $rebaseOut) { Write-Host "    $($line.ToString())" -ForegroundColor Gray }
    if ($rebaseExit -ne 0) {
        Write-Host "  ERROR: Rebase failed -- aborting release before push/GitHub publish." -ForegroundColor Red
        Write-Host "  Commit or discard the reported working-tree changes, then run Release again." -ForegroundColor Red
        git rebase --abort 2>$null
        exit 1
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
        $ghExit = Invoke-GhStreaming $ghArgs "release create"
        $ErrorActionPreference = $savedEAP

        return $ghExit
    }

    # --- Unified release (tag: v{M}.{m}.{p}) ---
    # The zip is the full distribution for new users (client + updater + data + mods).
    # Bare exe files are ALSO uploaded as individual release assets so the in-game updater
    # (updater.c) can find them by exact filename.
    # Updater.exe ships alongside so users can fall back to the standalone recovery tool
    # if a bad release breaks PerfectDark.exe's self-update path.
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
