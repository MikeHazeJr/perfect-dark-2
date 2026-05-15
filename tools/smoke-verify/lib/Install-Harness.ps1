#Requires -Version 5.1
<#
.SYNOPSIS
    Clean-install harness for the smoke verify gate.

.DESCRIPTION
    Builds an install directory from a known source binary and a known
    ROM, then exposes the run path. Two seeding modes:

      shared (default)  one canonical install at
                        .claude/smoke-verify-install/. Every test re-seeds
                        binary + ROM + data/ in this single location.
                        Windows Defender Firewall only ever sees ONE
                        PerfectDark.exe path; the inbound allow rule
                        added by Add-SmokeFirewallAllowRule is keyed on
                        it. Use this for everything except deliberate
                        per-test isolation.

      per-test          fresh dir at
                        .claude/smoke-verify-runs/<utc>-<test>/.
                        Pre-c115 default; retained for the rare case
                        where two tests must not share state. Each new
                        path re-triggers the firewall prompt; pair with
                        --no-net or run with -SourceBinary pointing at a
                        binary already in the firewall allow list.

    Three install states (orthogonal to mode):

      clean       fresh dir, no data/<romid>/, no pd.ini
      prefilled   fresh dir with data/<romid>/ from .claude/smoke-verify-cache
      current     points at an existing install (only via -Install on
                  the runner; never the default)

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

# c115 follow-up (2026-05-14): shared-install inter-test settle counter.
# Tracks how many tests have already re-seeded the shared install during
# this run.ps1 invocation. The second-and-later New-SmokeSharedInstall
# call inserts a brief Start-Sleep before wiping pd-client.log so the
# prior process's atexit flush has time to land on disk -- otherwise
# trailing harness sentinel writes can race with the wipe and the next
# test sees a polluted log header. Single-test runs increment to 1 but
# never trigger the delay (counter is checked BEFORE increment).
$script:SmokeSharedInstallCount = 0

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

    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
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
        [string] $InstallState = "clean",
        [string] $SourceBinary = "",
        [string] $SourceRom = "",
        [string] $ProjectRoot = "",
        [string] $Target = "pd"
    )

    if (-not $ProjectRoot) { $ProjectRoot = Get-SmokeProjectRoot }

    $stampedName = "{0}-{1}" -f ((Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")), $TestName
    $installDir = Join-Path $RunRoot $stampedName
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null

    $exeName = if ($Target -eq "pd-server") { "PerfectDarkServer.exe" } else { "PerfectDark.exe" }
    $bin = Find-SourceBinary -ProjectRoot $ProjectRoot -ExplicitPath $SourceBinary -Target $Target
    if (-not $bin) {
        throw "Cannot find $exeName to seed the smoke install. Build the corresponding target first or pass -SourceBinary."
    }
    Copy-Item -LiteralPath $bin -Destination (Join-Path $installDir $exeName) -Force

    # c115 server-pillar extension (2026-05-14): dedicated server target has
    # no ROM-load path (CLC_AUTH skips the ROM hash check when g_NetDedicated
    # is set). Skip the ROM seed for pd-server so tests do not gate on a
    # ROM being present; for the client target the ROM remains mandatory.
    $rom = $null
    $romId = ""
    if ($Target -ne "pd-server") {
        $rom = Find-SourceRom -ProjectRoot $ProjectRoot -ExplicitPath $SourceRom
        if (-not $rom) {
            throw "Cannot find pd.<romid>.z64 to seed the smoke install. Place a ROM in the project root or pass -SourceRom."
        }
        Copy-Item -LiteralPath $rom -Destination (Join-Path $installDir ([System.IO.Path]::GetFileName($rom))) -Force
        $romId = Get-RomIdFromName -RomPath $rom

        if ($InstallState -eq "prefilled") {
            $cache = Join-Path $ProjectRoot (".claude\smoke-verify-cache\$romId")
            if (Test-Path -LiteralPath $cache) {
                $dataDir = Join-Path $installDir "data"
                New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
                Copy-Item -LiteralPath $cache -Destination (Join-Path $dataDir $romId) -Recurse -Force
            } else {
                Write-Warning ("Prefilled cache not found at {0}; falling back to clean state for this run." -f $cache)
            }
        }
    }

    return [PSCustomObject]@{
        InstallDir = $installDir
        SourceBinary = $bin
        SourceRom = $rom
        RomId = $romId
        InstallState = $InstallState
        Target = $Target
        ExeName = $exeName
    }
}

function New-SmokeSharedInstall {
    <#
    .SYNOPSIS
        Re-seed a single canonical install directory shared across smoke tests.

    .DESCRIPTION
        Used by the runner's -SharedInstall mode (default). The path
        .claude/smoke-verify-install/ is created once per project; on
        every test the binary, ROM, and (optional) prefilled data/ are
        refreshed from the source. Per-test artefacts (logs, crash dumps)
        still land in .claude/smoke-verify-runs/<utc>-<test>/, but the
        executable lives at a stable path so Windows Defender Firewall
        only ever sees ONE PerfectDark.exe.

        Idempotent: if the shared dir already has the right binary +
        ROM with newer or equal mtime, the copy is skipped (mtime + size
        comparison). data/ is re-seeded every time because tests may
        mutate it.

    .OUTPUTS
        Same shape as New-SmokeInstall (InstallDir, SourceBinary,
        SourceRom, RomId, InstallState).
    #>
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $TestName,
        [string] $InstallState = "clean",
        [string] $SourceBinary = "",
        [string] $SourceRom = "",
        [string] $Target = "pd"
    )

    $installDir = Join-Path $ProjectRoot ".claude\smoke-verify-install"
    if (-not (Test-Path -LiteralPath $installDir)) {
        New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    }

    # c115 follow-up (2026-05-14): inter-test settle delay. The prior
    # PerfectDark.exe writes its harness atexit sentinel ("SMOKE: result=...")
    # to pd-client.log via a buffered stream; the buffer flush is racing
    # with the next test's wipe-log step further down. A 1-second pause
    # at the START of every second-or-later shared-install seed gives the
    # OS time to settle the prior write before we delete the file. First
    # call (counter == 0) skips the sleep so single-test runs are not
    # penalised.
    if ($script:SmokeSharedInstallCount -gt 0) {
        Start-Sleep -Milliseconds 1000
    }
    $script:SmokeSharedInstallCount++

    $exeName = if ($Target -eq "pd-server") { "PerfectDarkServer.exe" } else { "PerfectDark.exe" }
    $bin = Find-SourceBinary -ProjectRoot $ProjectRoot -ExplicitPath $SourceBinary -Target $Target
    if (-not $bin) {
        throw "Cannot find $exeName to seed the shared smoke install. Build the corresponding target first or pass -SourceBinary."
    }

    $destBin = Join-Path $installDir $exeName

    # Refresh binary only if source is newer or sizes differ.
    $copyBin = $true
    if (Test-Path -LiteralPath $destBin) {
        $srcInfo = Get-Item -LiteralPath $bin
        $dstInfo = Get-Item -LiteralPath $destBin
        if ($srcInfo.Length -eq $dstInfo.Length -and $srcInfo.LastWriteTimeUtc -le $dstInfo.LastWriteTimeUtc) {
            $copyBin = $false
        }
    }
    if ($copyBin) {
        Copy-Item -LiteralPath $bin -Destination $destBin -Force
    }

    # c115 server-pillar extension (2026-05-14): dedicated server has no
    # ROM-load path -- pd-server's CLC_AUTH skips the ROM hash check
    # because no ROM is loaded. Skip the ROM seed for pd-server so the
    # test does not gate on a ROM being present. The client target keeps
    # the ROM seed mandatory.
    $rom = $null
    $romId = ""
    $logFileName = if ($Target -eq "pd-server") { "pd-server.log" } else { "pd-client.log" }
    if ($Target -ne "pd-server") {
        $rom = Find-SourceRom -ProjectRoot $ProjectRoot -ExplicitPath $SourceRom
        if (-not $rom) {
            throw "Cannot find pd.<romid>.z64 to seed the shared smoke install. Place a ROM in the project root or pass -SourceRom."
        }
        $destRom = Join-Path $installDir ([System.IO.Path]::GetFileName($rom))
        # Always refresh ROM file (cheap; ensures tests start consistent).
        Copy-Item -LiteralPath $rom -Destination $destRom -Force
        $romId = Get-RomIdFromName -RomPath $rom

        $existingDataDir = Join-Path $installDir "data\$romId"
        if (Test-Path -LiteralPath $existingDataDir) {
            Remove-Item -LiteralPath $existingDataDir -Recurse -Force -ErrorAction SilentlyContinue
        }

        if ($InstallState -eq "prefilled") {
            $cache = Join-Path $ProjectRoot (".claude\smoke-verify-cache\$romId")
            if (Test-Path -LiteralPath $cache) {
                $dataDir = Join-Path $installDir "data"
                New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
                Copy-Item -LiteralPath $cache -Destination (Join-Path $dataDir $romId) -Recurse -Force
            } else {
                Write-Warning ("Prefilled cache not found at {0}; falling back to clean state for this run." -f $cache)
            }
        }
    }

    # Stale prior test artefacts in the shared dir: nuke the relevant log
    # so the test sees a fresh log file. Client tests target pd-client.log;
    # server tests target pd-server.log. Both can coexist in the shared
    # install dir without interference.
    $logPath = Join-Path $installDir $logFileName
    if (Test-Path -LiteralPath $logPath) {
        Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue
    }

    # Seed the firewall allow rule against the canonical path (idempotent).
    # Add a separate rule entry for the server binary so the OS doesn't
    # prompt on the first pd-server smoke run. The client and server rules
    # are independent (different DisplayName + program path).
    Add-SmokeFirewallAllowRule -Program $destBin | Out-Null

    return [PSCustomObject]@{
        InstallDir = $installDir
        SourceBinary = $bin
        SourceRom = $rom
        RomId = $romId
        InstallState = $InstallState
        Target = $Target
        ExeName = $exeName
    }
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
                -Program $absProgram -Profile Any -Enabled True | Out-Null
            Write-Host ("  firewall: allow rule added for {0}" -f $absProgram) -ForegroundColor DarkGray
            return $true
        }

        # Rule exists. Verify the program path; update if drifted.
        $appFilter = $rule | Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue
        $currentProgram = ""
        if ($appFilter -and $appFilter.Program) { $currentProgram = $appFilter.Program }
        if ([string]::Compare($currentProgram, $absProgram, $true) -ne 0) {
            Set-NetFirewallRule -DisplayName $displayName -Program $absProgram | Out-Null
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
        [string] $Target = "pd"
    )
    # c115 server-pillar extension (2026-05-14): port/src/system.c routes
    # sysLog output to pd-server.log when g_NetDedicated is set (which
    # server_main.c does before sysInit). The client target keeps the
    # historical pd-client.log destination. The two files coexist in the
    # shared install dir.
    $leaf = if ($Target -eq "pd-server") { "pd-server.log" } else { "pd-client.log" }
    return (Join-Path $InstallDir $leaf)
}

function Copy-SmokeFixtures {
    <#
    .SYNOPSIS
        Stage test-declared fixture files into the install directory.

    .DESCRIPTION
        Phase 1 smoke tests can pre-position files inside the install dir
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
        as needed. Existing destinations are overwritten (Copy-Item
        -Force).

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

        $absDst = Join-Path $InstallDir $dst
        $absDstParent = Split-Path -Parent $absDst
        if ($absDstParent -and -not (Test-Path -LiteralPath $absDstParent)) {
            New-Item -ItemType Directory -Path $absDstParent -Force | Out-Null
        }
        try {
            Copy-Item -LiteralPath $absSrc -Destination $absDst -Force -ErrorAction Stop
            $copied++
            Write-Host ("  fixture: {0} -> {1}" -f $src, $dst) -ForegroundColor DarkGray
        } catch {
            Write-Warning ("Copy-SmokeFixtures: failed to copy {0} -> {1}: {2}" -f $absSrc, $absDst, $_.Exception.Message)
        }
    }
    return $copied
}
