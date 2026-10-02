#Requires -Version 5.1
<#
.SYNOPSIS
    Clean-install harness for the smoke verify gate.

.DESCRIPTION
    Seeds private ordinary files from immutable content-addressed inputs.
    Shared mode exclusively reuses a stable workspace after complete byte
    verification and reset; failed installs move to bounded retained storage.
    Per-test mode always uses an isolated managed install. Historical inputs
    and explicit -Install templates are read-only. Receipts remain reconstructable.

    Three install states (orthogonal to mode):

      clean       fresh dir, no data/<romid>/, no pd.ini
      prefilled   fresh dir with data/<romid>/ from .claude/smoke-verify-cache
      current     reads existing baseline data; -Install copies its template

    The runner module dot-sources this script and calls one of:
      New-SmokeSharedInstall  (shared mode -- default)
      New-SmokeInstall        (per-test mode)
      Add-SmokeFirewallAllowRule  (idempotent firewall allow)

    The source binary defaults to Build/PerfectDark.exe, falling back
    through the user's session-build directories in
    .claude/session-builds/*/PerfectDark.exe if the canonical Build/ is
    missing. The ROM defaults to the file matching pd.*.z64 in the
    project root or the canonical Build/ directory.
#>

Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "Storage-Harness.ps1")

function Get-SmokeProjectRoot {
    [CmdletBinding()] param()
    $scriptPath = $PSCommandPath
    if (-not $scriptPath) { $scriptPath = $MyInvocation.MyCommand.Path }
    $libDir = Split-Path -Parent $scriptPath
    $smokeDir = Split-Path -Parent $libDir
    $toolsDir = Split-Path -Parent $smokeDir
    $projectRoot = Split-Path -Parent $toolsDir
    return $projectRoot
}

function Find-SourceBinary {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [string] $ExplicitPath = "",
        [string] $Target = "pd"
    )

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        return $null
    }

    # c115 server-pillar extension (2026-05-14): pd-server target maps to
    # PerfectDarkServer.exe, otherwise fall back to the client exe. The
    # session-build search path is shared because devtools/build-session.ps1
    # writes both targets into the same session dir.
    $exeName = if ($Target -eq "pd-server") { "PerfectDarkServer.exe" } else { "PerfectDark.exe" }

    $candidates = @()
    $candidates += Join-Path $ProjectRoot (Join-Path "Build" $exeName)
    $sessionRoot = Join-Path $ProjectRoot ".claude\session-builds"
    if (Test-Path -LiteralPath $sessionRoot) {
        $candidates += Get-ChildItem -LiteralPath $sessionRoot -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName $exeName }
    }

    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) {
            return (Resolve-Path -LiteralPath $c).Path
        }
    }
    return $null
}

function Copy-SmokeRuntimeDlls {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $SourceBinary,
        [Parameter(Mandatory)] [string] $InstallDir,
        [string] $ProjectRoot = ""
    )

    $copied = 0
    $seen = @{}
    $sourceDirs = @()
    $sourceDir = Split-Path -Parent $SourceBinary
    if ($sourceDir) {
        $sourceDirs += $sourceDir
    }
    if ($ProjectRoot) {
        $sourceDirs += (Join-Path $ProjectRoot "Build")
    }

    foreach ($dir in $sourceDirs) {
        if (-not $dir -or -not (Test-Path -LiteralPath $dir)) {
            continue
        }
        $dlls = @(Get-ChildItem -LiteralPath $dir -Filter "*.dll" -File -ErrorAction SilentlyContinue)
        foreach ($dll in $dlls) {
            if ($seen.ContainsKey($dll.Name)) {
                continue
            }
            Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $InstallDir $dll.Name) -Force
            $seen[$dll.Name] = $true
            $copied++
        }
    }
    return $copied
}

function Find-SourceRom {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [string] $ExplicitPath = ""
    )

    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $roots = @(
        (Join-Path $ProjectRoot "Build"),
        $ProjectRoot
    )

    foreach ($r in $roots) {
        if (-not (Test-Path -LiteralPath $r)) { continue }
        $roms = Get-ChildItem -LiteralPath $r -Filter "pd.*.z64" -File -ErrorAction SilentlyContinue
        foreach ($rom in $roms) {
            return $rom.FullName
        }
    }
    return $null
}

function Get-RomIdFromName {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $RomPath)
    $name = [System.IO.Path]::GetFileNameWithoutExtension($RomPath)
    # pd.<romid>.z64 -> name is "pd.<romid>"; strip leading "pd."
    if ($name -match '^pd\.(.+)$') { return $matches[1] }
    return $name
}

function New-SmokeInstall {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $TestName,
        [string] $InstallState = "clean", [string] $SourceBinary = "",
        [string] $SourceRom = "", [string] $ProjectRoot = "", [string] $Target = "pd"
    )
    if (-not $ProjectRoot) { $ProjectRoot = Get-SmokeProjectRoot }

    return New-SmokeManagedInstall -ProjectRoot $ProjectRoot -TestName $TestName `
        -InstallState $InstallState -SourceBinary $SourceBinary -SourceRom $SourceRom -Target $Target
}

function New-SmokeSharedInstall {
    [CmdletBinding()] param(
        [string] $SharedDir = "",
        [Parameter(Mandatory)] [string] $TestName,
        [string] $InstallState = "clean", [string] $SourceBinary = "",
        [string] $SourceRom = "", [string] $ProjectRoot = "", [string] $Target = "pd"
    )
    if (-not $ProjectRoot) { $ProjectRoot = Get-SmokeProjectRoot }
    if ($SharedDir) { throw "Custom shared path unsupported; use -Install as a read-only template." }
    return New-SmokeManagedInstall -ProjectRoot $ProjectRoot -TestName $TestName `
        -InstallState $InstallState -SourceBinary $SourceBinary -SourceRom $SourceRom -Target $Target -Shared
}

function Add-SmokeFirewallAllowRule {
    <#
    .SYNOPSIS
        Add (or refresh) the Windows Defender Firewall inbound allow rule
        keyed on the canonical smoke binary path.

    .DESCRIPTION
        Idempotent: checks for an existing rule with DisplayName
        "PD2 Smoke Verify"; creates it if missing; updates the program
        path if the rule exists but points elsewhere.

        Non-fatal on failure -- if PowerShell is running unelevated,
        New-NetFirewallRule may throw "Access denied". Log a warning and
        proceed. Worker alpha's --no-net boot arg closes the firewall
        prompt class for smoke runs anyway; the rule is belt-and-braces
        for the case where a test deliberately needs network.

        Required for shared-install mode (default). The rule survives
        across sessions and reboots, so first-run elevation is the only
        prompt the user ever sees.

    .PARAMETER Program
        Absolute path to the canonical PerfectDark.exe. The rule keys on
        this path.
    #>
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $Program
    )

    if (-not (Test-Path -LiteralPath $Program)) {
        Write-Warning ("Add-SmokeFirewallAllowRule: program path does not exist: {0}; skipping rule." -f $Program)
        return $false
    }

    $absProgram = (Resolve-Path -LiteralPath $Program).Path
    # c115 server-pillar extension (2026-05-14): distinct display names per
    # exe so the client and server rules can coexist without one path
    # overwriting the other on every install seed.
    $exeLeaf = [System.IO.Path]::GetFileName($absProgram)
    $displayName = if ($exeLeaf -ieq "PerfectDarkServer.exe") {
        "PD2 Smoke Verify (Server)"
    } else {
        "PD2 Smoke Verify"
    }

    # NetSecurity cmdlets are only present on Windows; bail gracefully
    # elsewhere (the smoke runner is Windows-only today, but keep this
    # script portable for future cross-platform smoke harnesses).
    if (-not (Get-Command -Name Get-NetFirewallRule -ErrorAction SilentlyContinue)) {
        Write-Warning "Add-SmokeFirewallAllowRule: NetSecurity cmdlets unavailable on this platform; skipping rule."
        return $false
    }

    try {
        $rule = Get-NetFirewallRule -DisplayName $displayName -ErrorAction SilentlyContinue
        if (-not $rule) {
            New-NetFirewallRule -DisplayName $displayName `
                -Direction Inbound -Action Allow `
                -Program $absProgram -Profile Any -Enabled True `
                -ErrorAction Stop | Out-Null
            Write-Host ("  firewall: allow rule added for {0}" -f $absProgram) -ForegroundColor DarkGray
            return $true
        }

        # Rule exists. Verify the program path; update if drifted.
        $appFilter = $rule | Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue
        $currentProgram = ""
        if ($appFilter -and $appFilter.Program) { $currentProgram = $appFilter.Program }
        if ([string]::Compare($currentProgram, $absProgram, $true) -ne 0) {
            Set-NetFirewallRule -DisplayName $displayName -Program $absProgram `
                -ErrorAction Stop | Out-Null
            Write-Host ("  firewall: allow rule updated to {0}" -f $absProgram) -ForegroundColor DarkGray
        }
        return $true
    } catch {
        Write-Warning ("Add-SmokeFirewallAllowRule failed (likely needs elevation): {0}. Proceeding without rule -- --no-net should mitigate." -f $_.Exception.Message)
        return $false
    }
}

function Get-SmokeLogPath {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $InstallDir,
        [string] $Target = "pd",
        [string] $Leaf = ""
    )
    # c115 server-pillar extension (2026-05-14): port/src/system.c routes
    # sysLog output to pd-server.log when g_NetDedicated is set (which
    # server_main.c does before sysInit). The client target keeps the
    # historical pd-client.log destination. The two files coexist in the
    # shared install dir.
    if (-not $Leaf) {
        $Leaf = if ($Target -eq "pd-server") { "pd-server.log" } else { "pd-client.log" }
    }

    $candidates = @(Get-SmokeLogCandidatePaths -InstallDir $InstallDir -Leaf $Leaf)
    foreach ($p in $candidates) {
        if (Test-Path -LiteralPath $p) {
            return $p
        }
    }

    return $candidates[0]
}

function Get-SmokeLogCandidatePaths {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $InstallDir,
        [Parameter(Mandatory)] [string] $Leaf
    )

    return @(
        (Join-Path $InstallDir (Join-Path "logs\game client" $Leaf)),
        (Join-Path $InstallDir $Leaf)
    )
}

function Remove-SmokePaths {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $InstallDir,
        [object] $Paths
    )

    if (-not $Paths) { return 0 }
    if ($Paths -is [string]) {
        $Paths = @($Paths)
    } elseif ($Paths -isnot [System.Collections.IEnumerable]) {
        return 0
    }

    $removed = 0
    foreach ($p in $Paths) {
        if (-not $p) { continue }
        $rel = [string]$p
        if (-not $rel) { continue }

        $absPath = Join-Path $InstallDir $rel
        Assert-SmokePlainTree -Path $absPath -Descendants
        $installRoot = [System.IO.Path]::GetFullPath($InstallDir)
        $targetRoot = [System.IO.Path]::GetFullPath($absPath)
        $installRootWithSep = $installRoot.TrimEnd([char[]]@(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar
        )) + [System.IO.Path]::DirectorySeparatorChar
        if ($targetRoot.Equals($installRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
            -not $targetRoot.StartsWith($installRootWithSep, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Warning ("Remove-SmokePaths: path escapes install directory: {0}; skipping." -f $absPath)
            continue
        }

        if (-not (Test-Path -LiteralPath $absPath)) { continue }
        try {
            $item = Get-Item -LiteralPath $absPath -ErrorAction Stop
            if ($item.PSIsContainer) {
                Remove-Item -LiteralPath $absPath -Recurse -Force -ErrorAction Stop
            } else {
                Remove-Item -LiteralPath $absPath -Force -ErrorAction Stop
            }
            $removed++
            Write-Host ("  removed stale: {0}" -f $rel) -ForegroundColor DarkGray
        } catch {
            Write-Warning ("Remove-SmokePaths: failed to remove {0}: {1}" -f $absPath, $_.Exception.Message)
        }
    }
    return $removed
}

function Copy-SmokeFixtures {
    <#
    .SYNOPSIS
        Stage test-declared fixture files or directories into the install directory.

    .DESCRIPTION
        Phase 1 smoke tests can pre-position files or directories inside the install dir
        before the binary launches via the optional `fixtures` array in
        the test JSON. Each entry has the shape:

            { "src": "<repo-relative path>", "dst": "<install-relative path>" }

        Use cases:
          * mod_load_smoke: pre-stage `tools/smoke-verify/fixtures/<id>.pdmod`
            under `mods/<id>.pdmod` so modmgrScanDirectory picks it up.
          * save_roundtrip_smoke: pre-stage a v1 agent save under
            `data/<romid>/saves/agent_001.sav` so the migrator runs.
          * Any future fixture-dependent test.

        Called after install seeding completes and before the binary
        launches. Source paths resolve against $ProjectRoot; destination
        paths resolve against $InstallDir. Parent directories are created
        as needed. Existing destinations are overwritten. Directory sources
        are copied recursively.

        Non-fatal on missing src: emits a warning and continues. The
        binary launch decides whether the missing fixture is fatal -- the
        runner does not pre-judge.

    .PARAMETER ProjectRoot
        Absolute path to the project root (repo top). Used to resolve
        `src` paths relative to the repo.

    .PARAMETER InstallDir
        Absolute path to the per-test or shared install directory. Used
        to resolve `dst` paths.

    .PARAMETER Fixtures
        Array of PSCustomObject entries with `src` and `dst` string
        properties. Empty / $null is a no-op.

    .OUTPUTS
        Number of fixtures successfully copied (int).
    #>
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $InstallDir,
        [object] $Fixtures
    )

    if (-not $Fixtures) { return 0 }
    if ($Fixtures -isnot [System.Collections.IEnumerable]) { return 0 }

    $copied = 0
    foreach ($f in $Fixtures) {
        if (-not $f) { continue }
        $src = $null
        $dst = $null
        if ($f.PSObject.Properties.Match('src').Count -gt 0) { $src = [string]$f.src }
        if ($f.PSObject.Properties.Match('dst').Count -gt 0) { $dst = [string]$f.dst }
        if (-not $src -or -not $dst) {
            Write-Warning ("Copy-SmokeFixtures: fixture entry missing src/dst; skipping.")
            continue
        }

        $absSrc = Join-Path $ProjectRoot $src
        if (-not (Test-Path -LiteralPath $absSrc)) {
            Write-Warning ("Copy-SmokeFixtures: src does not exist: {0}; skipping." -f $absSrc)
            continue
        }

        $absDst = Get-SmokeFixtureDestination -InstallDir $InstallDir -RelativePath $dst
        Assert-SmokePlainTree -Path $absSrc -Descendants
        Assert-SmokePlainTree -Path $absDst -Descendants
        $installRoot = [System.IO.Path]::GetFullPath($InstallDir)
        $targetRoot = [System.IO.Path]::GetFullPath($absDst)
        $installRootWithSep = $installRoot.TrimEnd([char[]]@(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar
        )) + [System.IO.Path]::DirectorySeparatorChar
        if ($targetRoot.Equals($installRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
            -not $targetRoot.StartsWith($installRootWithSep, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Warning ("Copy-SmokeFixtures: dst escapes install directory: {0}; skipping." -f $absDst)
            continue
        }
        $absDstParent = Split-Path -Parent $absDst
        if ($absDstParent -and -not (Test-Path -LiteralPath $absDstParent)) {
            New-Item -ItemType Directory -Path $absDstParent -Force | Out-Null
        }
        try {
            $srcItem = Get-Item -LiteralPath $absSrc -ErrorAction Stop
            if (Test-Path -LiteralPath $absDst) {
                $dstItem = Get-Item -LiteralPath $absDst -ErrorAction Stop
                if ($dstItem.PSIsContainer) {
                    Remove-Item -LiteralPath $absDst -Recurse -Force -ErrorAction Stop
                } else {
                    Remove-Item -LiteralPath $absDst -Force -ErrorAction Stop
                }
            }
            if ($srcItem.PSIsContainer) {
                Copy-Item -LiteralPath $absSrc -Destination $absDst -Recurse -Force -ErrorAction Stop
            } else {
                Copy-Item -LiteralPath $absSrc -Destination $absDst -Force -ErrorAction Stop
            }
            $copied++
            Write-Host ("  fixture: {0} -> {1}" -f $src, $dst) -ForegroundColor DarkGray
        } catch {
            Write-Warning ("Copy-SmokeFixtures: failed to copy {0} -> {1}: {2}" -f $absSrc, $absDst, $_.Exception.Message)
        }
    }
    return $copied
}

function Pack-SmokePdmodFixtures {
    <#
    .SYNOPSIS
        Pack repo fixture directories into install-local .pdmod archives.

    .DESCRIPTION
        Mod pipeline smoke tests need a real archive mounted by the game, but
        should not commit generated zip bytes. Each entry has the shape:

            { "src": "<repo-relative directory>", "dst": "<install-relative .pdmod>" }

        The source directory contents become archive root entries, so a source
        with `mod.json` at its root produces a valid root-manifest .pdmod.
    #>
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $InstallDir,
        [object] $Fixtures
    )

    if (-not $Fixtures) { return 0 }
    if ($Fixtures -isnot [System.Collections.IEnumerable]) { return 0 }

    try {
        Add-Type -AssemblyName System.IO.Compression | Out-Null
        Add-Type -AssemblyName System.IO.Compression.FileSystem | Out-Null
    } catch {
        Write-Warning ("Pack-SmokePdmodFixtures: compression assemblies unavailable: {0}" -f $_.Exception.Message)
        return 0
    }

    $packed = 0
    foreach ($f in $Fixtures) {
        if (-not $f) { continue }
        $src = $null
        $dst = $null
        if ($f.PSObject.Properties.Match('src').Count -gt 0) { $src = [string]$f.src }
        if ($f.PSObject.Properties.Match('dst').Count -gt 0) { $dst = [string]$f.dst }
        if (-not $src -or -not $dst) {
            Write-Warning "Pack-SmokePdmodFixtures: fixture entry missing src/dst; skipping."
            continue
        }

        $absSrc = Join-Path $ProjectRoot $src
        if (-not (Test-Path -LiteralPath $absSrc -PathType Container)) {
            Write-Warning ("Pack-SmokePdmodFixtures: src directory does not exist: {0}; skipping." -f $absSrc)
            continue
        }

        $absDst = Get-SmokeFixtureDestination -InstallDir $InstallDir -RelativePath $dst
        Assert-SmokePlainTree -Path $absSrc -Descendants
        Assert-SmokePlainTree -Path $absDst -Descendants
        $absDstParent = Split-Path -Parent $absDst
        if ($absDstParent -and -not (Test-Path -LiteralPath $absDstParent)) {
            New-Item -ItemType Directory -Path $absDstParent -Force | Out-Null
        }
        if (Test-Path -LiteralPath $absDst) {
            Remove-Item -LiteralPath $absDst -Force -ErrorAction SilentlyContinue
        }

        try {
            [System.IO.Compression.ZipFile]::CreateFromDirectory(
                $absSrc,
                $absDst,
                [System.IO.Compression.CompressionLevel]::Optimal,
                $false)
            $packed++
            Write-Host ("  packed fixture: {0} -> {1}" -f $src, $dst) -ForegroundColor DarkGray
        } catch {
            Write-Warning ("Pack-SmokePdmodFixtures: failed to pack {0} -> {1}: {2}" -f $absSrc, $absDst, $_.Exception.Message)
        }
    }

    return $packed
}
