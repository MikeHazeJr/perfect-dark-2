#Requires -Version 5.1
<#
.SYNOPSIS
    Smoke verify gate runner (Phase 1).

.DESCRIPTION
    Drives PerfectDark.exe through a scripted scenario using --smoke,
    captures the resulting pd-client.log, and applies the test JSON's
    declared assertions.

    Designed to plug into the build-headless flow; can also run
    standalone for local verification.

.PARAMETER Test
    Run a single test by file stem (e.g. -Test boot_smoke). Repeatable.

.PARAMETER Tag
    Run all tests carrying this tag. Repeatable.

.PARAMETER AutoSelect
    Run only tests whose paths_of_interest match a changed path between
    -MergeBase and HEAD. Falls back to all tests if git is unavailable.

.PARAMETER MergeBase
    Git reference used as the base for -AutoSelect. Default: dev.

.PARAMETER Build
    Run the queued build (devtools/build-session.ps1) before running
    tests. Uses the -Session value (default: smoke-<utc>).

.PARAMETER Session
    Session id passed to the queued build. Default: smoke-<utc>.

.PARAMETER Install
    Use an existing install directory instead of a per-test fresh copy.
    Implies -Keep; never used in CI.

.PARAMETER SharedInstall
    Re-seed a single canonical install at .claude/smoke-verify-install/
    for every test, instead of a fresh per-test directory. Closes the
    Windows Defender Firewall prompt class because the same
    PerfectDark.exe path is launched every time. Default ON. To force
    the legacy per-test layout pass -PerTestInstall (e.g. for tests
    that genuinely require pristine isolation).

.PARAMETER PerTestInstall
    Force the legacy per-test install layout
    (.claude/smoke-verify-runs/<utc>-<test>/PerfectDark.exe). Disables
    -SharedInstall. Every fresh path will retrigger the Windows Defender
    Firewall prompt; pair with --no-net or pre-seed the firewall allow
    rule manually.

.PARAMETER Keep
    Do not delete the per-run dir on success.

.PARAMETER Verbose
    Print every assertion check, not just failures.

.PARAMETER Timeout
    Override timeout_seconds in the test definitions. Use for CI where
    the budget needs a higher floor.

.PARAMETER TestsDir
    Where to discover tests. Default: tools/smoke-verify/tests.

.EXAMPLE
    .\tools\smoke-verify\run.ps1 -Test boot_smoke -Verbose

.EXAMPLE
    .\tools\smoke-verify\run.ps1 -AutoSelect -MergeBase dev

.EXAMPLE
    .\tools\smoke-verify\run.ps1 -Tag stability -Tag boot
#>

[CmdletBinding()]
param(
    [string[]] $Test,
    [string[]] $Tag,
    [switch]   $AutoSelect,
    [string]   $MergeBase = "dev",

    [switch]   $Build,
    [string]   $Session = "",

    [string]   $Install = "",
    [switch]   $SharedInstall,
    [switch]   $PerTestInstall,
    [switch]   $Keep,
    [int]      $Timeout = 0,

    [string]   $TestsDir = "",
    [string]   $SourceBinary = "",
    [string]   $SourceRom = "",
    [switch]   $VerboseAssertions
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ----------------------------------------------------------------
# Bootstrap paths
# ----------------------------------------------------------------

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$LibDir = Join-Path $ScriptDir "lib"
if (-not $TestsDir) { $TestsDir = Join-Path $ScriptDir "tests" }

. (Join-Path $ProjectRoot "devtools\_build-env-prelude.ps1")

$errorModeSource = @"
using System;
using System.Runtime.InteropServices;

public static class PdSmokeWinErrorMode
{
    [DllImport("kernel32.dll")]
    public static extern uint SetErrorMode(uint uMode);
}
"@

if (-not ([System.Management.Automation.PSTypeName]'PdSmokeWinErrorMode').Type) {
    Add-Type -TypeDefinition $errorModeSource
}

$captureSource = @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public static class PdSmokeWindowCapture
{
    private const int SRCCOPY = 0x00CC0020;
    private const int BI_RGB = 0;
    private const int DIB_RGB_COLORS = 0;
    private const uint PW_RENDERFULLCONTENT = 2;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAPINFOHEADER
    {
        public uint biSize;
        public int biWidth;
        public int biHeight;
        public ushort biPlanes;
        public ushort biBitCount;
        public uint biCompression;
        public uint biSizeImage;
        public int biXPelsPerMeter;
        public int biYPelsPerMeter;
        public uint biClrUsed;
        public uint biClrImportant;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct BITMAPINFO
    {
        public BITMAPINFOHEADER bmiHeader;
    }

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    private static extern IntPtr GetDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateCompatibleDC(IntPtr hdc);

    [DllImport("gdi32.dll")]
    private static extern IntPtr CreateCompatibleBitmap(IntPtr hdc, int cx, int cy);

    [DllImport("gdi32.dll")]
    private static extern IntPtr SelectObject(IntPtr hdc, IntPtr h);

    [DllImport("gdi32.dll")]
    private static extern bool DeleteObject(IntPtr ho);

    [DllImport("gdi32.dll")]
    private static extern bool DeleteDC(IntPtr hdc);

    [DllImport("gdi32.dll")]
    private static extern bool BitBlt(IntPtr hdcDest, int xDest, int yDest, int wDest, int hDest,
                                      IntPtr hdcSrc, int xSrc, int ySrc, int rop);

    [DllImport("user32.dll")]
    private static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);

    [DllImport("gdi32.dll")]
    private static extern int GetDIBits(IntPtr hdc, IntPtr hbm, uint start, uint cLines,
                                        byte[] lpvBits, ref BITMAPINFO lpbmi, uint usage);

    public static bool Capture(IntPtr hWnd, string path)
    {
        if (hWnd == IntPtr.Zero) return false;
        ShowWindow(hWnd, 5);
        SetForegroundWindow(hWnd);
        Thread.Sleep(300);
        RECT rect;
        if (!GetWindowRect(hWnd, out rect)) return false;
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;
        if (width <= 0 || height <= 0) return false;

        string dir = Path.GetDirectoryName(path);
        if (!String.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);

        IntPtr screenDc = GetDC(IntPtr.Zero);
        IntPtr memoryDc = IntPtr.Zero;
        IntPtr bitmap = IntPtr.Zero;
        IntPtr oldObject = IntPtr.Zero;
        try
        {
            memoryDc = CreateCompatibleDC(screenDc);
            bitmap = CreateCompatibleBitmap(screenDc, width, height);
            if (memoryDc == IntPtr.Zero || bitmap == IntPtr.Zero) return false;
            oldObject = SelectObject(memoryDc, bitmap);
            // PrintWindow grabs the window's own DWM-composited surface (incl. the
            // OpenGL content) regardless of z-order, so a foreground Claude / other
            // window no longer masks the game. Fall back to the screen BitBlt only if
            // PrintWindow fails outright.
            if (!PrintWindow(hWnd, memoryDc, PW_RENDERFULLCONTENT))
            {
                if (!BitBlt(memoryDc, 0, 0, width, height, screenDc, rect.Left, rect.Top, SRCCOPY)) return false;
            }

            int stride = width * 4;
            byte[] pixels = new byte[stride * height];
            BITMAPINFO info = new BITMAPINFO();
            info.bmiHeader.biSize = (uint)Marshal.SizeOf(typeof(BITMAPINFOHEADER));
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            info.bmiHeader.biSizeImage = (uint)pixels.Length;
            if (GetDIBits(memoryDc, bitmap, 0, (uint)height, pixels, ref info, DIB_RGB_COLORS) == 0) return false;

            using (FileStream fs = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (BinaryWriter bw = new BinaryWriter(fs))
            {
                int fileHeaderSize = 14;
                int dibHeaderSize = 40;
                int pixelOffset = fileHeaderSize + dibHeaderSize;
                int fileSize = pixelOffset + pixels.Length;
                bw.Write((byte)'B');
                bw.Write((byte)'M');
                bw.Write(fileSize);
                bw.Write((ushort)0);
                bw.Write((ushort)0);
                bw.Write(pixelOffset);
                bw.Write(dibHeaderSize);
                bw.Write(width);
                bw.Write(height);
                bw.Write((ushort)1);
                bw.Write((ushort)32);
                bw.Write(BI_RGB);
                bw.Write(pixels.Length);
                bw.Write(0);
                bw.Write(0);
                bw.Write(0);
                bw.Write(0);
                for (int row = height - 1; row >= 0; row--) {
                    bw.Write(pixels, row * stride, stride);
                }
            }
            return true;
        }
        finally
        {
            if (oldObject != IntPtr.Zero && memoryDc != IntPtr.Zero) SelectObject(memoryDc, oldObject);
            if (bitmap != IntPtr.Zero) DeleteObject(bitmap);
            if (memoryDc != IntPtr.Zero) DeleteDC(memoryDc);
            if (screenDc != IntPtr.Zero) ReleaseDC(IntPtr.Zero, screenDc);
        }
    }
}
"@

$SEM_FAILCRITICALERRORS = 0x0001
$SEM_NOGPFAULTERRORBOX = 0x0002
$SEM_NOOPENFILEERRORBOX = 0x8000
$loaderErrorMode = $SEM_FAILCRITICALERRORS -bor $SEM_NOGPFAULTERRORBOX -bor $SEM_NOOPENFILEERRORBOX
$previousErrorMode = [PdSmokeWinErrorMode]::SetErrorMode($loaderErrorMode)

$RunRoot = Join-Path $ProjectRoot ".claude\smoke-verify-runs"
$ResultsFile = ""

. (Join-Path $LibDir "Test-Assertions.ps1")
. (Join-Path $LibDir "Install-Harness.ps1")
. (Join-Path $LibDir "Catalog-Ingress-Fixtures.ps1")
. (Join-Path $LibDir "Temporary-Recovery-Fixtures.ps1")
. (Join-Path $LibDir "Test-MemorySafety.ps1")

function New-NeedlerEffectReplacementFixtures {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $InstallDir)

    $generator = "devtools/generate-needler-effect-replacement-fixtures.py"
    $relativeInstall = [System.IO.Path]::GetRelativePath(
        $ProjectRoot, $InstallDir).Replace('\', '/')
    Push-Location $ProjectRoot
    try {
        & python $generator --install-dir $relativeInstall
        if ($LASTEXITCODE -ne 0) {
            throw "Needler effect replacement fixture generation failed"
        }
    } finally {
        Pop-Location
    }
}

function Stop-SmokeOwnedFaultProcesses {
    [CmdletBinding()] param([int[]] $KnownPids = @())

    $known = @{}
    foreach ($pidValue in @($KnownPids)) {
        if ($pidValue -gt 0) { $known[[int]$pidValue] = $true }
    }

    $processes = @()
    try {
        $processes = @(Get-CimInstance Win32_Process -Filter "name = 'PerfectDark.exe' OR name = 'PerfectDarkServer.exe' OR name = 'WerFault.exe'" -ErrorAction SilentlyContinue)
    } catch {
        $processes = @()
    }

    foreach ($procInfo in $processes) {
        $pidValue = [int]$procInfo.ProcessId
        $name = [string]$procInfo.Name
        $cmd = [string]$procInfo.CommandLine
        $path = [string]$procInfo.ExecutablePath
        $parent = 0
        if ($null -ne $procInfo.ParentProcessId) {
            $parent = [int]$procInfo.ParentProcessId
        }

        $owned = $false
        if ($known.ContainsKey($pidValue) -or ($parent -gt 0 -and $known.ContainsKey($parent))) {
            $owned = $true
        }
        if (-not $owned -and ($cmd -match '--smoke')) {
            $owned = $true
        }
        if (-not $owned -and ($cmd -like "*.claude\smoke-verify*" -or $path -like "*.claude\smoke-verify*")) {
            $owned = $true
        }
        if (-not $owned -and $name -ieq "WerFault.exe") {
            foreach ($pidKey in $known.Keys) {
                if ($cmd -match "(^|\D)$pidKey(\D|$)") {
                    $owned = $true
                    break
                }
            }
            if (-not $owned -and $cmd -match 'PerfectDark(\.exe|Server\.exe)') {
                $owned = $true
            }
        }

        if ($owned) {
            try {
                Stop-Process -Id $pidValue -Force -ErrorAction Stop
                Write-Host ("  reaped smoke-owned lingering process: {0} pid={1}" -f $name, $pidValue) -ForegroundColor Yellow
            } catch {}
        }
    }
}

Stop-SmokeOwnedFaultProcesses

try {

# ----------------------------------------------------------------
# Resolve install mode (c115, 2026-05-14)
# ----------------------------------------------------------------
# SharedInstall is ON by default. -PerTestInstall forces the legacy
# per-test layout. -Install (existing dir) overrides both. If both
# -SharedInstall and -PerTestInstall are passed explicitly, the latter
# wins for backward-compat with anyone scripting around the old layout.
$useSharedInstall = $true
if ($PerTestInstall) { $useSharedInstall = $false }
if ($Install)        { $useSharedInstall = $false }  # honour explicit -Install
if ($PerTestInstall -and $SharedInstall) {
    Write-Warning "Both -SharedInstall and -PerTestInstall passed; -PerTestInstall wins."
}

function Write-Info([string]$t) { Write-Host $t -ForegroundColor Gray }
function Write-Ok([string]$t)   { Write-Host $t -ForegroundColor Green }
function Write-Warn([string]$t) { Write-Host $t -ForegroundColor Yellow }
function Write-Fail([string]$t) { Write-Host $t -ForegroundColor Red }

function New-SmokeScreenshotSchedule {
    [CmdletBinding()] param(
        [psobject] $Definition,
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $TestName
    )

    $events = @()
    if ($Definition.PSObject.Properties.Match('screenshots').Count -eq 0 -or -not $Definition.screenshots) {
        return @()
    }

    $safeName = ($TestName -replace '[^A-Za-z0-9_.-]', '_')
    $artifactDir = Join-Path $RunRoot ("screenshots\{0:yyyyMMddTHHmmss}-{1}" -f (Get-Date), $safeName)
    $ordinal = 0
    foreach ($shot in @($Definition.screenshots)) {
        $ordinal += 1
        $atMs = 0
        if ($shot.PSObject.Properties.Match('at_ms').Count -gt 0 -and $shot.at_ms) {
            $atMs = [int]$shot.at_ms
        }
        $name = "shot-$ordinal"
        if ($shot.PSObject.Properties.Match('name').Count -gt 0 -and $shot.name) {
            $name = [string]$shot.name
        }
        $safeShot = ($name -replace '[^A-Za-z0-9_.-]', '_')
        $events += [PSCustomObject]@{
            AtMs = $atMs
            Name = $name
            Path = (Join-Path $artifactDir ("{0:000}-{1}.bmp" -f $ordinal, $safeShot))
            Captured = $false
            Success = $false
        }
    }
    return @($events)
}

function Invoke-PendingSmokeScreenshots {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [System.Diagnostics.Process] $Process,
        [Parameter(Mandatory)] [array] $Schedule,
        [Parameter(Mandatory)] [datetime] $Started
    )

    if ($Schedule.Count -eq 0) { return }
    $elapsedMs = [int](((Get-Date) - $Started).TotalMilliseconds)
    foreach ($shot in $Schedule) {
        if ($shot.Captured) { continue }
        if ($elapsedMs -lt [int]$shot.AtMs) { continue }
        # Prefer the in-game glReadPixels capture (focus/size/occlusion
        # independent): if the harness already wrote the file, use it.
        if (Test-Path -LiteralPath ([string]$shot.Path)) {
            $shot.Captured = $true
            $shot.Success  = $true
            Write-Info ("  screenshot (glReadPixels): {0}" -f $shot.Path)
            continue
        }
        # Grace: give the harness up to 2s past at_ms to write before falling
        # back to PrintWindow (which returns black GL on small/occluded windows).
        if ($elapsedMs -lt ([int]$shot.AtMs + 2000)) { continue }
        $shot.Captured = $true
        Write-Warn ("  glReadPixels file absent for '{0}'; PrintWindow fallback" -f $shot.Name)
        try { $Process.Refresh() } catch {}
        if (-not ([System.Management.Automation.PSTypeName]'PdSmokeWindowCapture').Type) {
            try {
                Add-Type -TypeDefinition $captureSource
            } catch {
                Write-Warn ("  screenshot capture unavailable: {0}" -f $_.Exception.Message)
                $shot.Success = $false
                continue
            }
        }
        $ok = $false
        try {
            $ok = [PdSmokeWindowCapture]::Capture($Process.MainWindowHandle, [string]$shot.Path)
        } catch {
            $ok = $false
        }
        $shot.Success = $ok
        if ($ok) {
            Write-Info ("  screenshot: {0}" -f $shot.Path)
        } else {
            Write-Warn ("  screenshot failed: {0}" -f $shot.Name)
        }
    }
}

# ----------------------------------------------------------------
# Helper: enumerate tests
# ----------------------------------------------------------------

function Get-SmokeTests {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $Dir)
    if (-not (Test-Path -LiteralPath $Dir)) {
        throw "Tests directory not found: $Dir"
    }
    # Recurse so bug-regression tests at tests/bugs/B-NNN.json are
    # discovered alongside scenario tests at tests/*.json. Schema is the
    # same; bug-regression tests carry an extra bug_id field that the
    # runner surfaces in results for the bug-tracker auto-flip path.
    $files = Get-ChildItem -LiteralPath $Dir -Filter "*.json" -File -Recurse -ErrorAction SilentlyContinue
    $out = @()
    foreach ($f in $files) {
        try {
            $raw = Get-Content -LiteralPath $f.FullName -Raw
            # Strip // line comments because JSON.NET does not accept them.
            $clean = ($raw -replace '(?m)^\s*//.*$', '')
            $def = $clean | ConvertFrom-Json
        } catch {
            Write-Warn ("Failed to parse {0}: {1}" -f $f.Name, $_.Exception.Message)
            continue
        }

        # Compute a stable category from the path relative to $Dir so the
        # bug tracker can group results (e.g. "scenario" or "bugs").
        $rootFull = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd('\').TrimEnd('/')
        $fileFull = $f.FullName
        $rel = $fileFull
        if ($fileFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            $rel = $fileFull.Substring($rootFull.Length).TrimStart('\').TrimStart('/')
        }
        $relParent = Split-Path -Parent $rel
        if (-not $relParent -or $relParent -eq "" -or $relParent -eq ".") {
            $category = "scenario"
        } else {
            $category = $relParent.Replace("\", "/")
        }

        $bugId = $null
        if ($def.PSObject.Properties.Match('bug_id').Count -gt 0 -and $def.bug_id) {
            $bugId = [string]$def.bug_id
        }
        $regressionFor = $null
        if ($def.PSObject.Properties.Match('regression_for').Count -gt 0 -and $def.regression_for) {
            $regressionFor = [string]$def.regression_for
        }

        $out += [PSCustomObject]@{
            Name = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
            Path = $f.FullName
            Category = $category
            BugId = $bugId
            RegressionFor = $regressionFor
            Definition = $def
        }
    }
    return $out
}

function Get-ChangedPaths {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $Base)
    try {
        $gitOut = & git -C $ProjectRoot diff --name-only "$Base...HEAD" 2>$null
        if ($LASTEXITCODE -ne 0) { return @() }
        return @($gitOut | Where-Object { $_ -ne "" })
    } catch {
        return @()
    }
}

function Test-MatchesPath {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string[]] $Patterns,
        [Parameter(Mandatory)] [string[]] $Paths
    )
    foreach ($p in $Paths) {
        foreach ($pat in $Patterns) {
            if (-not $pat) { continue }
            # Convert a glob-ish pattern to a -like form. The test
            # definitions use simple */** style globs.
            if ($p -like $pat) { return $true }
        }
    }
    return $false
}

function Select-SmokeTests {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [array] $All,
        [string[]] $Names,
        [string[]] $Tags,
        [switch] $UseAutoSelect,
        [string] $AutoSelectBase = "dev"
    )

    $candidates = $All
    if ($Names -and $Names.Count -gt 0) {
        $candidates = @($candidates | Where-Object { $Names -contains $_.Name })
    }
    if ($Tags -and $Tags.Count -gt 0) {
        $candidates = @($candidates | Where-Object {
            $defTags = @()
            if ($_.Definition.PSObject.Properties.Match('tags').Count -gt 0) {
                $defTags = @($_.Definition.tags)
            }
            foreach ($t in $Tags) {
                if ($defTags -contains $t) { return $true }
            }
            return $false
        })
    }
    if ($UseAutoSelect) {
        $changed = Get-ChangedPaths -Base $AutoSelectBase
        if ($changed.Count -eq 0) {
            Write-Info "Auto-select: no changed paths discovered (or git unavailable); running all tests."
        } else {
            Write-Info ("Auto-select: {0} changed paths discovered vs {1}." -f $changed.Count, $AutoSelectBase)
            $candidates = @($candidates | Where-Object {
                $patterns = @()
                if ($_.Definition.PSObject.Properties.Match('paths_of_interest').Count -gt 0) {
                    $patterns = @($_.Definition.paths_of_interest)
                }
                if ($patterns.Count -eq 0) { return $true }  # no filter -> always eligible
                Test-MatchesPath -Patterns $patterns -Paths $changed
            })
        }
    }
    return $candidates
}

# ----------------------------------------------------------------
# Run one test
# ----------------------------------------------------------------

# c118 (2026-05-15): multi-process orchestration helper.
#
# Test JSON schema for two-or-more-process smokes (currently used by the
# connectivity pillar's listen_host_peer_smoke):
#
#   {
#     "scenario_name": "...",
#     "timeout_seconds": 25,
#     "processes": [
#       {
#         "name": "host",
#         "log_file": "pd-host.log",   // optional; defaults per --host routing
#         "boot_args": ["--host", "--listen-bind", "27200", ...],
#         "wait_for": "NET: created server on port 27200"  // emit barrier
#       },
#       {
#         "name": "client",
#         "log_file": "pd-client.log",
#         "boot_args": ["--connect-host", "127.0.0.1:27200", ...],
#         "wait_after_launch_ms": 0
#       }
#     ],
#     "assertions": { ... }            // run against concatenation of all logs
#   }
#
# Behaviour:
#   - Processes share one install directory by default. Set
#     `separate_process_installs: true` to give each process isolated base,
#     save, and mod directories while still launching the one canonical executable.
#     The runner passes those directories through the game's authoritative
#     --basedir/--savedir arguments; WorkingDirectory alone is not sufficient
#     because --portable otherwise resolves both paths from the executable.
#     Top-level fixtures are applied to every isolated install; process-local
#     remove_paths, fixtures, and packed_fixtures are applied afterward.
#   - Processes launch sequentially. After each launch, the runner polls
#     the process's log file for `wait_for` (if specified) before
#     proceeding to the next process. Poll cadence: 200ms. If the marker
#     does not appear within process[i].wait_timeout_seconds (default 15s)
#     the orchestration aborts -- the late-launching processes are not
#     started, the already-running ones are killed, and the test fails.
#   - Once all processes are launched, the runner waits up to
#     timeout_seconds for ALL processes to exit. Any still running after
#     the timeout are killed.
#   - Assertions run against the concatenation of every process's log
#     (separator: a blank line + a `--- <log_file> ---` header). This
#     keeps the single-pattern Test-Assertions engine intact.
function Invoke-SmokeTestMultiProcess {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [psobject] $Test,
        [string] $ExistingInstall = "",
        [switch] $KeepOnSuccess,
        [int] $TimeoutOverride = 0,
        [switch] $VerboseEval,
        [string] $BinaryOverride = "",
        [string] $RomOverride = "",
        [switch] $Shared
    )

    $name = $Test.Name
    $def = $Test.Definition

    $separateProcessInstalls = $false
    if ($def.PSObject.Properties.Match('separate_process_installs').Count -gt 0) {
        $separateProcessInstalls = [bool]$def.separate_process_installs
    }

    $installState = "clean"
    if ($def.PSObject.Properties.Match('install_state').Count -gt 0 -and $def.install_state) {
        $installState = [string]$def.install_state
    }

    if (-not (Test-Path -LiteralPath $RunRoot)) {
        New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
    }

    # Multi-process tests are pd-only today (pd-server has its own log
    # routing rules and we'd need a richer per-process target field to
    # mix targets). Default target stays "pd" and is shared by every
    # process; install dir is seeded once.
    $target = "pd"
    if ($def.PSObject.Properties.Match('target').Count -gt 0 -and $def.target) {
        $target = [string]$def.target
    }

    $installInfo = $null
    if ($ExistingInstall) {
        $installInfo = [PSCustomObject]@{
            InstallDir = (Resolve-Path -LiteralPath $ExistingInstall).Path
            SourceBinary = ""
            SourceRom = ""
            RomId = ""
            InstallState = "current"
            ExeName = "PerfectDark.exe"
        }
    } elseif ($Shared) {
        $installInfo = New-SmokeSharedInstall `
            -ProjectRoot $ProjectRoot `
            -TestName $name `
            -InstallState $installState `
            -SourceBinary $BinaryOverride `
            -SourceRom $RomOverride `
            -Target $target
    } else {
        $installInfo = New-SmokeInstall `
            -RunRoot $RunRoot `
            -TestName $name `
            -InstallState $installState `
            -SourceBinary $BinaryOverride `
            -SourceRom $RomOverride `
            -ProjectRoot $ProjectRoot `
            -Target $target
    }

    Write-Info ("  install dir: {0}" -f $installInfo.InstallDir)
    Write-Info ("  state: {0}" -f $installInfo.InstallState)

    $timeoutSeconds = 30
    if ($def.PSObject.Properties.Match('timeout_seconds').Count -gt 0 -and $def.timeout_seconds) {
        $timeoutSeconds = [int]$def.timeout_seconds
    }
    if ($TimeoutOverride -gt 0) { $timeoutSeconds = $TimeoutOverride }
    $watchdogSeconds = $timeoutSeconds + 30

    $exeLeaf = "PerfectDark.exe"
    if ($installInfo.PSObject.Properties.Match('ExeName').Count -gt 0 -and $installInfo.ExeName) {
        $exeLeaf = [string]$installInfo.ExeName
    }
    $exe = Join-Path $installInfo.InstallDir $exeLeaf
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "$exeLeaf missing inside install dir after seeding: $exe"
    }

    $processPlans = @()
    $processIndex = 0
    foreach ($pdef in $def.processes) {
        $pname = "process-{0}" -f $processIndex
        if ($pdef.PSObject.Properties.Match('name').Count -gt 0 -and $pdef.name) {
            $pname = [string]$pdef.name
        }

        $processInstallInfo = $installInfo
        if ($separateProcessInstalls) {
            $processInstallState = $installState
            if ($pdef.PSObject.Properties.Match('install_state').Count -gt 0 -and $pdef.install_state) {
                $processInstallState = [string]$pdef.install_state
            }
            $processRom = $RomOverride
            if (-not $processRom -and $installInfo.SourceRom) {
                $processRom = [string]$installInfo.SourceRom
            }
            $processInstallInfo = New-SmokeInstall `
                -RunRoot $RunRoot `
                -TestName ("{0}-{1}" -f $name, $pname) `
                -InstallState $processInstallState `
                -SourceBinary $exe `
                -SourceRom $processRom `
                -ProjectRoot $ProjectRoot `
                -Target $target
            $processModsDir = Join-Path $processInstallInfo.InstallDir "mods"
            if (-not (Test-Path -LiteralPath $processModsDir)) {
                New-Item -ItemType Directory -Path $processModsDir -Force | Out-Null
            }
            Write-Info ("  process install[{0}]: {1}" -f $pname, $processInstallInfo.InstallDir)
            Write-Info ("  process state[{0}]: {1}" -f $pname, $processInstallInfo.InstallState)
        }

        $processPlans += [PSCustomObject]@{
            Name = $pname
            Definition = $pdef
            InstallInfo = $processInstallInfo
        }
        $processIndex++
    }

    function Initialize-MultiProcessSmokeInstall {
        param(
            [Parameter(Mandatory)] [psobject] $Definition,
            [Parameter(Mandatory)] [string] $InstallDir,
            [Parameter(Mandatory)] [string] $Label
        )

        if ($Definition.PSObject.Properties.Match('remove_paths').Count -gt 0 -and $Definition.remove_paths) {
            $removeCount = Remove-SmokePaths -InstallDir $InstallDir -Paths $Definition.remove_paths
            if ($removeCount -gt 0) {
                Write-Info ("  removed {0} stale path(s) [{1}]" -f $removeCount, $Label)
            }
        }
        if ($Definition.PSObject.Properties.Match('fixtures').Count -gt 0 -and $Definition.fixtures) {
            $fixCount = Copy-SmokeFixtures -ProjectRoot $ProjectRoot -InstallDir $InstallDir -Fixtures $Definition.fixtures
            if ($fixCount -gt 0) {
                Write-Info ("  staged {0} fixture(s) [{1}]" -f $fixCount, $Label)
            }
        }
        if ($Definition.PSObject.Properties.Match('packed_fixtures').Count -gt 0 -and $Definition.packed_fixtures) {
            $packCount = Pack-SmokePdmodFixtures -ProjectRoot $ProjectRoot -InstallDir $InstallDir -Fixtures $Definition.packed_fixtures
            if ($packCount -gt 0) {
                Write-Info ("  packed {0} fixture archive(s) [{1}]" -f $packCount, $Label)
            }
        }
        if ($Definition.PSObject.Properties.Match('catalog_ingress_boundary_fixtures').Count -gt 0 `
                -and $Definition.catalog_ingress_boundary_fixtures) {
            New-CatalogIngressBoundaryFixtures -InstallDir $InstallDir
        }
        if ($Definition.PSObject.Properties.Match('needler_effect_replacement_fixtures').Count -gt 0 `
                -and $Definition.needler_effect_replacement_fixtures) {
            New-NeedlerEffectReplacementFixtures -InstallDir $InstallDir
        }
        if ($Definition.PSObject.Properties.Match('temporary_recovery_fixtures').Count -gt 0 `
                -and $Definition.temporary_recovery_fixtures) {
            New-TemporaryRecoveryFixtures -ProjectRoot $ProjectRoot -InstallDir $InstallDir
        }
    }

    if ($separateProcessInstalls) {
        foreach ($plan in $processPlans) {
            Initialize-MultiProcessSmokeInstall -Definition $def -InstallDir $plan.InstallInfo.InstallDir -Label $plan.Name
            Initialize-MultiProcessSmokeInstall -Definition $plan.Definition -InstallDir $plan.InstallInfo.InstallDir -Label $plan.Name
        }
    } else {
        Initialize-MultiProcessSmokeInstall -Definition $def -InstallDir $installInfo.InstallDir -Label "shared"
    }

    # B-801 memory-risk control: refuse to launch a live game process when host
    # commit/pagefile headroom is dangerously low. A live Scenario smoke once
    # exhausted commit (2 GB pagefile) and crashed the machine + corrupted the git
    # index. This is a read-only probe (no system changes, no game launch); it is
    # env-overridable. See lib/Test-MemorySafety.ps1 and B-801 in context/bugs.md.
    $memSafety = Test-SmokeMemorySafety
    foreach ($m in $memSafety.Info) { Write-Info ("  mem: {0}" -f $m) }
    if (-not $memSafety.Safe) {
        foreach ($r in $memSafety.Reasons) { Write-Fail ("  MEMORY GUARD: {0}" -f $r) }
        if ($memSafety.Remediation) { Write-Fail ("  {0}" -f $memSafety.Remediation) }
        throw "B-801 memory guard refused live launch (unsafe host memory); set PD_SMOKE_SKIP_MEMORY_GUARD=1 to override."
    }

    # Pre-clear known log files in the install dir so wait-for polling
    # is deterministic. The shared-install harness already wipes
    # pd-client.log; ensure pd-host.log is wiped too if it exists from a
    # prior run.
    foreach ($plan in $processPlans) {
        foreach ($leaf in @("pd-client.log", "pd-host.log", "pd-server.log")) {
            foreach ($p in (Get-SmokeLogCandidatePaths -InstallDir $plan.InstallInfo.InstallDir -Leaf $leaf)) {
                if (Test-Path -LiteralPath $p) {
                    Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
                }
            }
        }
    }

    $started = Get-Date
    $procs = @()  # array of @{ Name; Process; LogPath; }
    $launchFailed = $false

    foreach ($plan in $processPlans) {
        $pdef = $plan.Definition
        $pname = $plan.Name
        $processInstallInfo = $plan.InstallInfo

        $pBootArgs = @()
        if ($pdef.PSObject.Properties.Match('boot_args').Count -gt 0 -and $pdef.boot_args) {
            $pBootArgs = @($pdef.boot_args)
        }

        # Default log routing: --host or --listen-bind without explicit
        # log_file => pd-host.log; otherwise pd-client.log. Caller can
        # override via process.log_file.
        $logFile = "pd-client.log"
        $usesHostLog = $false
        foreach ($a in $pBootArgs) {
            if ($a -eq "--host" -or $a -eq "--listen-bind") {
                $usesHostLog = $true
                break
            }
        }
        if ($usesHostLog) { $logFile = "pd-host.log" }
        if ($pdef.PSObject.Properties.Match('log_file').Count -gt 0 -and $pdef.log_file) {
            $logFile = [string]$pdef.log_file
        }
        $logPath = Get-SmokeLogPath -InstallDir $processInstallInfo.InstallDir -Leaf $logFile

        # All processes share the same --smoke <test-path> so each binary
        # loads the same scripted schedule (typically just a single exit
        # event at timeout_seconds * 1000 ms). The boot_args of the
        # process override binary-level behaviour like --host /
        # --connect-host. Keep the in-game crash handler enabled by
        # default so smoke failures write logs instead of modal Windows
        # fault dialogs.
        $crashArgs = @()
        if ($env:PD_SMOKE_DISABLE_CRASH_HANDLER -eq "1") {
            $crashArgs = @("--no-crash-handler")
        }
        $isolationArgs = @()
        if ($separateProcessInstalls) {
            $processModsDir = Join-Path $processInstallInfo.InstallDir "mods"
            $isolationArgs = @(
                "--basedir", [string]$processInstallInfo.InstallDir,
                "--savedir", [string]$processInstallInfo.InstallDir,
                "--moddir", [string]$processModsDir
            )
        }
        $processSmokePath = $Test.Path
        if ($pdef.PSObject.Properties.Match('smoke_path').Count -gt 0 -and $pdef.smoke_path) {
            $processSmokePath = [string]$pdef.smoke_path
            if (-not [System.IO.Path]::IsPathRooted($processSmokePath)) {
                $processSmokePath = Join-Path $ProjectRoot $processSmokePath
            }
            $processSmokePath = [System.IO.Path]::GetFullPath($processSmokePath)
        }
        $allArgs = @("--smoke", $processSmokePath) + $crashArgs + $isolationArgs + $pBootArgs

        # Sequential restart scenarios snapshot the previous process before
        # reaching this launch. Remove that process's ordinary log now so the
        # next wait_for poll cannot accept a stale marker during logger startup
        # and then apply the post-marker exit timeout to the wrong process.
        if ($pdef.PSObject.Properties.Match('snapshot_log_file').Count -gt 0 -and
                $pdef.snapshot_log_file -and (Test-Path -LiteralPath $logPath)) {
            Remove-Item -LiteralPath $logPath -Force -ErrorAction Stop
            Write-Info ("    reset sequential log before launch: {0}" -f $logPath)
        }

        Write-Info ""
        Write-Info ("  launch[{0}]: {1}" -f $pname, ($allArgs -join ' '))
        Write-Info ("    log: {0}" -f $logPath)

        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName         = $exe
        $quotedArgs = foreach ($a in $allArgs) {
            if ($null -eq $a) { continue }
            $s = [string]$a
            if ($s -match '[\s"]') {
                '"' + ($s -replace '"', '\"') + '"'
            } else {
                $s
            }
        }
        $psi.Arguments        = ($quotedArgs -join ' ')
        $psi.WorkingDirectory = $processInstallInfo.InstallDir
        $psi.UseShellExecute  = $false
        $psi.CreateNoWindow      = $false
        $psi.RedirectStandardError  = $false
        $psi.RedirectStandardOutput = $false

        $p = $null
        try {
            $p = [System.Diagnostics.Process]::Start($psi)
        } catch {
            Write-Fail ("    launch failed: {0}" -f $_.Exception.Message)
            $launchFailed = $true
            break
        }

        $procs += [PSCustomObject]@{
            Name = $pname
            Process = $p
            LogPath = $logPath
            InstallDir = $processInstallInfo.InstallDir
            Assertions = $(if ($pdef.PSObject.Properties.Match('assertions').Count -gt 0) { $pdef.assertions } else { $null })
        }

        # Optional barrier: poll the just-launched process's log for the
        # `wait_for` marker before launching the next process.
        $waitFor = $null
        if ($pdef.PSObject.Properties.Match('wait_for').Count -gt 0 -and $pdef.wait_for) {
            $waitFor = [string]$pdef.wait_for
        }
        $waitTimeoutSec = 15
        if ($pdef.PSObject.Properties.Match('wait_timeout_seconds').Count -gt 0 -and $pdef.wait_timeout_seconds) {
            $waitTimeoutSec = [int]$pdef.wait_timeout_seconds
        }

        if ($waitFor) {
            Write-Info ("    waiting for marker: {0} (timeout {1}s)" -f $waitFor, $waitTimeoutSec)
            $deadline = (Get-Date).AddSeconds($waitTimeoutSec)
            $matched = $false
            while ((Get-Date) -lt $deadline) {
                if (Test-Path -LiteralPath $logPath) {
                    try {
                        $hit = Select-String -LiteralPath $logPath -Pattern $waitFor -SimpleMatch:$false -List -ErrorAction SilentlyContinue
                        if ($hit) {
                            $matched = $true
                            break
                        }
                    } catch {}
                }
                if ($p.HasExited) {
                    Write-Fail ("    process exited (code {0}) before emitting wait_for marker" -f $p.ExitCode)
                    $launchFailed = $true
                    break
                }
                Start-Sleep -Milliseconds 200
            }
            if (-not $matched -and -not $launchFailed) {
                Write-Fail ("    wait_for marker not seen within {0}s: {1}" -f $waitTimeoutSec, $waitFor)
                $launchFailed = $true
            }
            if ($matched) {
                Write-Info "    marker reached"
            }
        }

        if (-not $launchFailed -and
                $pdef.PSObject.Properties.Match('snapshot_log_file').Count -gt 0 -and
                $pdef.snapshot_log_file) {
            $snapshotExitTimeoutSec = 30
            if ($pdef.PSObject.Properties.Match('snapshot_exit_timeout_seconds').Count -gt 0 -and
                    $pdef.snapshot_exit_timeout_seconds) {
                $snapshotExitTimeoutSec = [Math]::Max(1,
                    [int]$pdef.snapshot_exit_timeout_seconds)
            }
            if (-not $p.WaitForExit($snapshotExitTimeoutSec * 1000)) {
                Write-Fail ("    process did not exit after snapshot barrier: {0}" -f $pname)
                $launchFailed = $true
            } else {
                $snapshotPath = Get-SmokeLogPath `
                    -InstallDir $processInstallInfo.InstallDir `
                    -Leaf ([string]$pdef.snapshot_log_file)
                try {
                    Copy-Item -LiteralPath $logPath -Destination $snapshotPath -Force
                    $procs[-1].LogPath = $snapshotPath
                    Write-Info ("    snapshotted log: {0}" -f $snapshotPath)
                } catch {
                    Write-Fail ("    log snapshot failed: {0}" -f $_.Exception.Message)
                    $launchFailed = $true
                }
            }
        }

        if ($launchFailed) { break }

        # Optional inter-process settle (rare; used when the marker
        # appears mid-init and the next process needs the host's
        # post-marker state to be stable).
        if ($pdef.PSObject.Properties.Match('wait_after_launch_ms').Count -gt 0 -and $pdef.wait_after_launch_ms) {
            $extraMs = [int]$pdef.wait_after_launch_ms
            if ($extraMs -gt 0) { Start-Sleep -Milliseconds $extraMs }
        }
    }

    # If a launch failed, mass-kill any survivors and let the assertion
    # pass below report on whatever log content exists.
    if ($launchFailed) {
        foreach ($entry in $procs) {
            try { if (-not $entry.Process.HasExited) { $entry.Process.Kill() } } catch {}
        }
    }

    # Wait for everything to exit (or hit watchdog).
    $allExited = $true
    $deadline = (Get-Date).AddSeconds($watchdogSeconds)
    foreach ($entry in $procs) {
        $remainingMs = [int][math]::Max(0, ($deadline - (Get-Date)).TotalMilliseconds)
        if (-not $entry.Process.WaitForExit($remainingMs)) {
            Write-Warn ("Watchdog firing on [{0}] pid={1} after {2}s; terminating." -f $entry.Name, $entry.Process.Id, $watchdogSeconds)
            try { $entry.Process.Kill() } catch {}
            try { $entry.Process.WaitForExit(5000) | Out-Null } catch {}
            $allExited = $false
        }
    }
    $launchedPids = @($procs | ForEach-Object { try { [int]$_.Process.Id } catch { 0 } } | Where-Object { $_ -gt 0 })
    Stop-SmokeOwnedFaultProcesses -KnownPids $launchedPids

    $elapsed = ((Get-Date) - $started).TotalSeconds

    # Build the aggregated log: concatenate every process's log file with
    # a header so the assertion engine can grep across both.
    $aggLogPath = Join-Path $installInfo.InstallDir ("pd-aggregated-{0}.log" -f $name)
    $aggBody = New-Object System.Text.StringBuilder
    foreach ($entry in $procs) {
        [void]$aggBody.AppendLine(("--- {0} ({1}) ---" -f $entry.Name, $entry.LogPath))
        if (Test-Path -LiteralPath $entry.LogPath) {
            try {
                $content = Get-Content -LiteralPath $entry.LogPath -Raw -ErrorAction SilentlyContinue
                if ($content) { [void]$aggBody.Append($content) }
            } catch {}
        } else {
            [void]$aggBody.AppendLine("(log file missing)")
        }
        [void]$aggBody.AppendLine("")
    }
    Set-Content -LiteralPath $aggLogPath -Value $aggBody.ToString() -Encoding UTF8 -NoNewline
    Write-Info ("  aggregated log: {0}" -f $aggLogPath)
    Write-Info ("  elapsed: {0:N1}s" -f $elapsed)

    # Run assertions against the aggregated log.
    $assertions = $null
    if ($def.PSObject.Properties.Match('assertions').Count -gt 0) {
        $assertions = $def.assertions
    } else {
        $assertions = [PSCustomObject]@{}
    }
    $aggregateResult = Invoke-SmokeAssertions -LogPath $aggLogPath -Assertions $assertions -VerboseAssertions:$VerboseEval
    $assertResult = [PSCustomObject]@{
        Passed = $aggregateResult.Passed
        Total = $aggregateResult.Total
        Met = $aggregateResult.Met
        Failures = New-Object System.Collections.Generic.List[psobject]
    }
    foreach ($failure in $aggregateResult.Failures) {
        $assertResult.Failures.Add($failure)
    }

    foreach ($entry in $procs) {
        if ($null -eq $entry.Assertions) { continue }
        $processResult = Invoke-SmokeAssertions -LogPath $entry.LogPath -Assertions $entry.Assertions -VerboseAssertions:$VerboseEval
        $assertResult.Total += $processResult.Total
        $assertResult.Met += $processResult.Met
        if (-not $processResult.Passed) { $assertResult.Passed = $false }
        foreach ($failure in $processResult.Failures) {
            $scopedFailure = $failure | Select-Object *
            $scopedFailure | Add-Member -NotePropertyName Process -NotePropertyValue $entry.Name
            if ($scopedFailure.PSObject.Properties.Match('Pattern').Count -gt 0) {
                $scopedFailure.Pattern = "[{0}] {1}" -f $entry.Name, $scopedFailure.Pattern
            } elseif ($scopedFailure.PSObject.Properties.Match('Message').Count -gt 0) {
                $scopedFailure.Message = "[{0}] {1}" -f $entry.Name, $scopedFailure.Message
            }
            $assertResult.Failures.Add($scopedFailure)
        }
    }

    $testOk = $assertResult.Passed -and -not $launchFailed

    foreach ($line in (Format-AssertionFailures -Result $assertResult)) {
        if ($testOk) { Write-Info $line } else { Write-Fail $line }
    }
    if ($launchFailed) {
        Write-Fail "  launch barrier failed (see logs for context)"
    }

    if ($testOk -and -not $KeepOnSuccess -and -not $ExistingInstall -and -not $Shared) {
        try {
            Remove-Item -LiteralPath $installInfo.InstallDir -Recurse -Force -ErrorAction Stop
            Write-Info "  cleaned install dir"
        } catch {
            Write-Warn ("  cleanup skipped: {0}" -f $_.Exception.Message)
        }
    } elseif (-not $testOk) {
        Write-Info ("  retained for debugging: {0}" -f $installInfo.InstallDir)
        try {
            $tail = Get-Content -LiteralPath $aggLogPath -Tail 60 -ErrorAction SilentlyContinue
            if ($tail) {
                Write-Host "  --- aggregated log tail (last 60 lines) ---" -ForegroundColor DarkGray
                foreach ($t in $tail) { Write-Host ("    {0}" -f $t) -ForegroundColor DarkGray }
            }
        } catch {}
    }

    if ($separateProcessInstalls) {
        $uniqueProcessInstallDirs = @($processPlans | ForEach-Object { $_.InstallInfo.InstallDir } | Select-Object -Unique)
        if ($testOk -and -not $KeepOnSuccess) {
            foreach ($processInstallDir in $uniqueProcessInstallDirs) {
                try {
                    Remove-Item -LiteralPath $processInstallDir -Recurse -Force -ErrorAction Stop
                    Write-Info ("  cleaned process install: {0}" -f $processInstallDir)
                } catch {
                    Write-Warn ("  process cleanup skipped: {0}" -f $_.Exception.Message)
                }
            }
        } elseif (-not $testOk) {
            foreach ($processInstallDir in $uniqueProcessInstallDirs) {
                Write-Info ("  retained process install for debugging: {0}" -f $processInstallDir)
            }
        }
    }

    $bugId = $null
    if ($Test.PSObject.Properties.Match('BugId').Count -gt 0) { $bugId = $Test.BugId }
    $regressionFor = $null
    if ($Test.PSObject.Properties.Match('RegressionFor').Count -gt 0) { $regressionFor = $Test.RegressionFor }
    $category = "scenario"
    if ($Test.PSObject.Properties.Match('Category').Count -gt 0 -and $Test.Category) { $category = $Test.Category }

    return [PSCustomObject]@{
        Name = $name
        Category = $category
        BugId = $bugId
        RegressionFor = $regressionFor
        Passed = $testOk
        ExitCode = $(if ($testOk) { 0 } else { 1 })
        ElapsedSeconds = $elapsed
        InstallDir = $installInfo.InstallDir
        ProcessInstallDirs = @($processPlans | ForEach-Object { $_.InstallInfo.InstallDir })
        AssertionsTotal = $assertResult.Total
        AssertionsMet = $assertResult.Met
        Failures = @($assertResult.Failures)
    }
}

function Invoke-SmokeTest {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [psobject] $Test,
        [string] $ExistingInstall = "",
        [switch] $KeepOnSuccess,
        [int] $TimeoutOverride = 0,
        [switch] $VerboseEval,
        [string] $BinaryOverride = "",
        [string] $RomOverride = "",
        [switch] $Shared
    )

    $name = $Test.Name
    $def = $Test.Definition
    Write-Host ""
    Write-Host ("=== {0} ===" -f $name) -ForegroundColor Cyan
    if ($def.PSObject.Properties.Match('description').Count -gt 0) {
        Write-Info ("  {0}" -f $def.description)
    }

    # c118 (2026-05-15): multi-process branch. A test JSON may declare a
    # `processes: [...]` array (each entry describes one binary launch with
    # its own boot_args + optional wait-for marker barrier). The dispatch
    # to Invoke-SmokeTestMultiProcess handles sequential launch with
    # log-tail polling for the wait-for marker, then collects each
    # process's log and runs the top-level assertions against the
    # concatenation. Used for the listen_host_peer_smoke (host + client).
    if ($def.PSObject.Properties.Match('processes').Count -gt 0 -and $def.processes) {
        return Invoke-SmokeTestMultiProcess `
            -Test $Test `
            -ExistingInstall $ExistingInstall `
            -KeepOnSuccess:$KeepOnSuccess `
            -TimeoutOverride $TimeoutOverride `
            -VerboseEval:$VerboseEval `
            -BinaryOverride $BinaryOverride `
            -RomOverride $RomOverride `
            -Shared:$Shared
    }

    $installState = "clean"
    if ($def.PSObject.Properties.Match('install_state').Count -gt 0 -and $def.install_state) {
        $installState = [string]$def.install_state
    }

    # c115 server-pillar extension (2026-05-14). Test JSON gains two
    # optional fields:
    #
    #   target            "pd" (default) -> PerfectDark.exe + pd-client.log
    #                     "pd-server"   -> PerfectDarkServer.exe + pd-server.log
    #
    #   runtime_strategy  "harness" (default) -> launch with `--smoke <path>`
    #                                            and trust the harness sentinel
    #                                            (only valid when smoke_harness.c
    #                                            is compiled into the target;
    #                                            today that is pd only).
    #                     "timeout-kill"      -> launch with boot_args only,
    #                                            wait timeout_seconds, then Kill
    #                                            the process and rely on
    #                                            log-only assertions. Non-zero
    #                                            exit code is acceptable.
    #
    # pd-server defaults to "timeout-kill" because the server target does NOT
    # link smoke_harness.c (see CMakeLists.txt SRC_SERVER). If a future
    # commit adds smoke_harness.c to SRC_SERVER, set runtime_strategy
    # explicitly in the test JSON to opt back in.
    $target = "pd"
    if ($def.PSObject.Properties.Match('target').Count -gt 0 -and $def.target) {
        $target = [string]$def.target
    }
    $runtimeStrategy = "harness"
    if ($def.PSObject.Properties.Match('runtime_strategy').Count -gt 0 -and $def.runtime_strategy) {
        $runtimeStrategy = [string]$def.runtime_strategy
    } elseif ($target -eq "pd-server") {
        $runtimeStrategy = "timeout-kill"
    }

    if (-not (Test-Path -LiteralPath $RunRoot)) {
        New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
    }

    $installInfo = $null
    if ($ExistingInstall) {
        $installInfo = [PSCustomObject]@{
            InstallDir = (Resolve-Path -LiteralPath $ExistingInstall).Path
            SourceBinary = ""
            SourceRom = ""
            RomId = ""
            InstallState = "current"
        }
    } elseif ($Shared) {
        # Single canonical install path; firewall rule seeded inside.
        $installInfo = New-SmokeSharedInstall `
            -ProjectRoot $ProjectRoot `
            -TestName $name `
            -InstallState $installState `
            -SourceBinary $BinaryOverride `
            -SourceRom $RomOverride `
            -Target $target
    } else {
        $installInfo = New-SmokeInstall `
            -RunRoot $RunRoot `
            -TestName $name `
            -InstallState $installState `
            -SourceBinary $BinaryOverride `
            -SourceRom $RomOverride `
            -ProjectRoot $ProjectRoot `
            -Target $target
    }

    Write-Info ("  install dir: {0}" -f $installInfo.InstallDir)
    Write-Info ("  state: {0}" -f $installInfo.InstallState)

    # Stage test-declared fixtures (mods, save files, etc.) into the
    # install dir before launching the binary. Optional `fixtures` array
    # in test JSON; entries shaped { src: <repo-relative>, dst: <install-relative> }.
    # Used by future mod_load_smoke / save_roundtrip_smoke / wall_jump_capsule_smoke.
    if ($def.PSObject.Properties.Match('remove_paths').Count -gt 0 -and $def.remove_paths) {
        $removeCount = Remove-SmokePaths -InstallDir $installInfo.InstallDir -Paths $def.remove_paths
        if ($removeCount -gt 0) {
            Write-Info ("  removed {0} stale path(s)" -f $removeCount)
        }
    }
    if ($def.PSObject.Properties.Match('fixtures').Count -gt 0 -and $def.fixtures) {
        $fixCount = Copy-SmokeFixtures -ProjectRoot $ProjectRoot -InstallDir $installInfo.InstallDir -Fixtures $def.fixtures
        if ($fixCount -gt 0) {
            Write-Info ("  staged {0} fixture(s)" -f $fixCount)
        }
    }
    if ($def.PSObject.Properties.Match('packed_fixtures').Count -gt 0 -and $def.packed_fixtures) {
        $packCount = Pack-SmokePdmodFixtures -ProjectRoot $ProjectRoot -InstallDir $installInfo.InstallDir -Fixtures $def.packed_fixtures
        if ($packCount -gt 0) {
            Write-Info ("  packed {0} fixture archive(s)" -f $packCount)
        }
    }

    if ($def.PSObject.Properties.Match('catalog_ingress_boundary_fixtures').Count -gt 0 `
            -and $def.catalog_ingress_boundary_fixtures) {
        New-CatalogIngressBoundaryFixtures -InstallDir $installInfo.InstallDir
    }
    if ($def.PSObject.Properties.Match('needler_effect_replacement_fixtures').Count -gt 0 `
            -and $def.needler_effect_replacement_fixtures) {
        New-NeedlerEffectReplacementFixtures -InstallDir $installInfo.InstallDir
    }
    if ($def.PSObject.Properties.Match('temporary_recovery_fixtures').Count -gt 0 `
            -and $def.temporary_recovery_fixtures) {
        New-TemporaryRecoveryFixtures -ProjectRoot $ProjectRoot -InstallDir $installInfo.InstallDir
    }

    # Resolve timeout
    $timeoutSeconds = 90
    if ($def.PSObject.Properties.Match('timeout_seconds').Count -gt 0 -and $def.timeout_seconds) {
        $timeoutSeconds = [int]$def.timeout_seconds
    }
    if ($TimeoutOverride -gt 0) { $timeoutSeconds = $TimeoutOverride }
    # 60 second cushion beyond the harness's own timeout so the harness
    # is the side that fires "result=timeout", not the runner.
    $watchdogSeconds = $timeoutSeconds + 60

    # Assemble process args. ExeName comes from the install harness so
    # the runner stays target-agnostic (pd vs pd-server).
    $exeLeaf = "PerfectDark.exe"
    if ($installInfo.PSObject.Properties.Match('ExeName').Count -gt 0 -and $installInfo.ExeName) {
        $exeLeaf = [string]$installInfo.ExeName
    }
    $exe = Join-Path $installInfo.InstallDir $exeLeaf
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "$exeLeaf missing inside install dir after seeding: $exe"
    }

    # B-801 memory-risk control: refuse to launch a live game process when host
    # commit/pagefile headroom is dangerously low. A live Scenario smoke once
    # exhausted commit (2 GB pagefile) and crashed the machine + corrupted the git
    # index. This is a read-only probe (no system changes, no game launch); it is
    # env-overridable. See lib/Test-MemorySafety.ps1 and B-801 in context/bugs.md.
    $memSafety = Test-SmokeMemorySafety
    foreach ($m in $memSafety.Info) { Write-Info ("  mem: {0}" -f $m) }
    if (-not $memSafety.Safe) {
        foreach ($r in $memSafety.Reasons) { Write-Fail ("  MEMORY GUARD: {0}" -f $r) }
        if ($memSafety.Remediation) { Write-Fail ("  {0}" -f $memSafety.Remediation) }
        throw "B-801 memory guard refused live launch (unsafe host memory); set PD_SMOKE_SKIP_MEMORY_GUARD=1 to override."
    }

    $bootArgs = @()
    if ($def.PSObject.Properties.Match('boot_args').Count -gt 0) {
        $bootArgs = @($def.boot_args)
    }

    # c115 server-pillar extension (2026-05-14). The "harness" strategy
    # injects `--smoke <path>` so the client smoke harness reads the JSON,
    # schedules input/exit events, and emits the SMOKE: result=... sentinel
    # on atexit. Keep the game crash handler enabled by default because it
    # suppresses modal Windows fault dialogs and returns through logs. The
    # "timeout-kill" strategy is for binaries that do not link
    # smoke_harness.c (today: pd-server) -- the runner launches with
    # boot_args only and tears down the process after timeout_seconds.
    # Screenshot schedule created early so the in-game glReadPixels capture
    # (focus/size/occlusion-independent) can OWN the standard `screenshots`
    # array via --smoke-screenshot-dir. The PrintWindow path stays as a
    # fallback for any shot the harness didn't write.
    $screenshotSchedule = @(New-SmokeScreenshotSchedule -Definition $def -RunRoot $RunRoot -TestName $name)
    $screenshotDir = $null
    if ($screenshotSchedule.Count -gt 0) {
        $screenshotDir = Split-Path -Parent ([string]$screenshotSchedule[0].Path)
        try { New-Item -ItemType Directory -Force -Path $screenshotDir | Out-Null } catch {}
    }
    if ($runtimeStrategy -eq "harness") {
        $crashArgs = @()
        if ($env:PD_SMOKE_DISABLE_CRASH_HANDLER -eq "1") {
            $crashArgs = @("--no-crash-handler")
        }
        $allArgs = @("--smoke", $Test.Path) + $crashArgs + $bootArgs
        if ($screenshotDir) { $allArgs += @("--smoke-screenshot-dir", $screenshotDir) }
    } else {
        $allArgs = @() + $bootArgs
    }
    Write-Info ("  target: {0} ({1})" -f $target, $exeLeaf)
    Write-Info ("  strategy: {0}" -f $runtimeStrategy)

    $started = Get-Date
    $proc = $null
    $exitCode = -1

    # c115 (2026-05-14): Start-Process -PassThru returns a Process object
    # whose .ExitCode property is unreliable for non-console GUI apps --
    # under some PS host configurations it stays -1 even after the
    # process exits cleanly with code 0. The harness writes
    # "SMOKE: result=scripted_exit code=0" to the log correctly but the
    # runner reads .ExitCode = -1 and reports FAIL even when 10-12/12
    # assertions pass. System.Diagnostics.Process.Start with explicit
    # ProcessStartInfo + WaitForExit gives a reliable .ExitCode for GUI
    # processes (the OS-level wait handle resolves the exit code
    # synchronously). UseShellExecute=$false keeps the call out of
    # ShellExecuteEx so the parent owns the process handle directly.
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName         = $exe
        # ProcessStartInfo.ArgumentList exists on .NET Core but not on
        # the .NET Framework PS 5.1 ships with; build the legacy
        # Arguments string with proper quoting instead so test paths
        # containing spaces survive intact.
        $quotedArgs = foreach ($a in $allArgs) {
            if ($null -eq $a) { continue }
            $s = [string]$a
            if ($s -match '[\s"]') {
                '"' + ($s -replace '"', '\"') + '"'
            } else {
                $s
            }
        }
        $psi.Arguments        = ($quotedArgs -join ' ')
        $psi.WorkingDirectory = $installInfo.InstallDir
        $psi.UseShellExecute  = $false
        # Keep the window visible so SDL initialises with a real
        # foreground window. Hiding it forces SDL into a background
        # mode that confuses focus tracking and breaks ImGui nav.
        $psi.CreateNoWindow      = $false
        $psi.RedirectStandardError  = $false
        $psi.RedirectStandardOutput = $false

        $proc = [System.Diagnostics.Process]::Start($psi)
        if (-not $proc) {
            throw "ProcessStartInfo returned null Process"
        }
        if ($runtimeStrategy -eq "timeout-kill") {
            # c115 server-pillar extension (2026-05-14): the binary is
            # expected to run forever (pd-server has no auto-exit path);
            # let it boot, then kill it after timeout_seconds. The
            # assertions are log-only -- non-zero exit code is treated as
            # acceptable for this strategy.
            if (-not $proc.WaitForExit($timeoutSeconds * 1000)) {
                Write-Info ("  timeout-kill: shutting down {0} (pid {1}) after {2}s." -f $exeLeaf, $proc.Id, $timeoutSeconds)
                try { $proc.Kill() } catch {}
                try { $proc.WaitForExit(5000) | Out-Null } catch {}
                # Exit code from Kill() is 1 / -1 / 0xC000013A on Windows
                # depending on the binary; we record it but don't gate on it.
                try { $exitCode = $proc.ExitCode } catch { $exitCode = -2 }
            } else {
                # Process exited on its own before the timeout. That's
                # unusual for pd-server (it's a long-running daemon) --
                # treat as a hint that something failed; surface the
                # actual exit code and let the log assertions catch the
                # real failure (e.g. "SERVER: Failed to start").
                $exitCode = $proc.ExitCode
                Write-Info ("  timeout-kill: process exited early with code {0}." -f $exitCode)
            }
        } else {
            if ($screenshotSchedule.Count -gt 0) {
                $deadline = $started.AddSeconds($watchdogSeconds)
                while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
                    Invoke-PendingSmokeScreenshots -Process $proc -Schedule $screenshotSchedule -Started $started
                    Start-Sleep -Milliseconds 250
                }
                if (-not $proc.HasExited) {
                    Write-Warn ("Watchdog firing after {0}s; terminating {1} (pid {2})." -f $watchdogSeconds, $exeLeaf, $proc.Id)
                    try { $proc.Kill() } catch {}
                    try { $proc.WaitForExit(5000) | Out-Null } catch {}
                    $exitCode = -2
                } else {
                    $exitCode = $proc.ExitCode
                }
            } elseif (-not $proc.WaitForExit($watchdogSeconds * 1000)) {
                Write-Warn ("Watchdog firing after {0}s; terminating {1} (pid {2})." -f $watchdogSeconds, $exeLeaf, $proc.Id)
                try { $proc.Kill() } catch {}
                try { $proc.WaitForExit(5000) | Out-Null } catch {}
                $exitCode = -2
            } else {
                $exitCode = $proc.ExitCode
            }
        }
    } catch {
        Write-Fail ("Failed to launch {0}: {1}" -f $exeLeaf, $_.Exception.Message)
        $exitCode = -3
    }
    if ($proc) {
        Stop-SmokeOwnedFaultProcesses -KnownPids @([int]$proc.Id)
    } else {
        Stop-SmokeOwnedFaultProcesses
    }
    $elapsed = ((Get-Date) - $started).TotalSeconds

    # c115 (2026-05-14) belt-and-braces: parse the harness's own
    # "SMOKE: result=<reason> ... code=N" sentinel out of the log
    # and override the OS-reported exit code with it when it's
    # cleaner. This protects against the residual class where
    # Process.ExitCode returns 0 even though the harness's atexit
    # path was skipped (forced terminate, ucrt assert popup) -- the
    # sentinel only exists when smokeHarnessExit actually ran.
    #
    # Sentinel override is harness-only. timeout-kill never emits a
    # sentinel (the harness isn't linked), so we skip the parse and
    # leave the OS exit code untouched.
    $logPathPre = Get-SmokeLogPath -InstallDir $installInfo.InstallDir -Target $target
    if ($runtimeStrategy -eq "harness" -and $logPathPre -and (Test-Path -LiteralPath $logPathPre)) {
        try {
            $sentinel = Select-String -LiteralPath $logPathPre `
                -Pattern 'SMOKE: result=\S+\s+scenario=.+?\s+elapsed_ms=\d+\s+events_fired=\d+/\d+\s+code=(-?\d+)' `
                -AllMatches | Select-Object -Last 1
            if ($sentinel -and $sentinel.Matches.Count -gt 0) {
                $sentinelCode = [int]$sentinel.Matches[-1].Groups[1].Value
                if ($exitCode -ne $sentinelCode) {
                    Write-Info ("  exit-code override: OS reported {0}, harness sentinel reported {1}; trusting sentinel." -f $exitCode, $sentinelCode)
                    $exitCode = $sentinelCode
                }
            }
        } catch {
            # Sentinel parse is best-effort; the OS exit code remains canonical on failure.
        }
    }

    $logPath = Get-SmokeLogPath -InstallDir $installInfo.InstallDir -Target $target
    Write-Info ("  log: {0}" -f $logPath)
    Write-Info ("  exit code: {0}" -f $exitCode)
    Write-Info ("  elapsed: {0:N1}s" -f $elapsed)

    # Run assertions
    $assertions = $null
    if ($def.PSObject.Properties.Match('assertions').Count -gt 0) {
        $assertions = $def.assertions
    } else {
        $assertions = [PSCustomObject]@{}
    }

    # c115 S-2 fix: rename to -VerboseAssertions to avoid PowerShell's
    # auto-binding collision with the common -Verbose parameter. Under
    # PS 5.1 the collision surfaced as a misleading "Count not found"
    # strict-mode crash.
    $assertResult = Invoke-SmokeAssertions -LogPath $logPath -Assertions $assertions -VerboseAssertions:$VerboseEval

    # c115 server-pillar extension (2026-05-14): the timeout-kill strategy
    # tears down the process forcibly after timeout_seconds, so a non-zero
    # exit code is the expected steady state. Gate solely on assertions
    # for that strategy. The harness strategy keeps the historical
    # exit-code-must-be-zero requirement.
    if ($runtimeStrategy -eq "timeout-kill") {
        $testOk = $assertResult.Passed
        $reasonExitNonZero = $false
    } else {
        $testOk = $assertResult.Passed -and ($exitCode -eq 0)
        $reasonExitNonZero = $false
        if ($exitCode -ne 0 -and $assertResult.Passed) {
            # The runner expects the harness to exit 0 on scripted-exit. A non-zero
            # exit when assertions pass suggests timeout-fired or the binary
            # crashed; treat as a failure but log it explicitly.
            $reasonExitNonZero = $true
            $testOk = $false
        }
    }

    foreach ($line in (Format-AssertionFailures -Result $assertResult)) {
        if ($testOk) { Write-Info $line } else { Write-Fail $line }
    }
    if ($reasonExitNonZero) {
        Write-Fail "  exit code non-zero: harness force-exited (timeout or fatal)"
    }

    # Cleanup: only nuke per-test dirs. Shared install survives across
    # tests because deleting it would defeat the firewall-rule pinning
    # and force re-elevation; -ExistingInstall is user-managed.
    if ($testOk -and -not $KeepOnSuccess -and -not $ExistingInstall -and -not $Shared) {
        try {
            Remove-Item -LiteralPath $installInfo.InstallDir -Recurse -Force -ErrorAction Stop
            Write-Info "  cleaned install dir"
        } catch {
            Write-Warn ("  cleanup skipped: {0}" -f $_.Exception.Message)
        }
    } elseif (-not $testOk) {
        Write-Info ("  retained for debugging: {0}" -f $installInfo.InstallDir)
        try {
            $tail = Get-Content -LiteralPath $logPath -Tail 30 -ErrorAction SilentlyContinue
            if ($tail) {
                Write-Host "  --- log tail (last 30 lines) ---" -ForegroundColor DarkGray
                foreach ($t in $tail) { Write-Host ("    {0}" -f $t) -ForegroundColor DarkGray }
            }
        } catch {}
    }

    $bugId = $null
    if ($Test.PSObject.Properties.Match('BugId').Count -gt 0) { $bugId = $Test.BugId }
    $regressionFor = $null
    if ($Test.PSObject.Properties.Match('RegressionFor').Count -gt 0) { $regressionFor = $Test.RegressionFor }
    $category = "scenario"
    if ($Test.PSObject.Properties.Match('Category').Count -gt 0 -and $Test.Category) { $category = $Test.Category }

    return [PSCustomObject]@{
        Name = $name
        Category = $category
        BugId = $bugId
        RegressionFor = $regressionFor
        Passed = $testOk
        ExitCode = $exitCode
        ElapsedSeconds = $elapsed
        InstallDir = $installInfo.InstallDir
        Screenshots = @($screenshotSchedule | Where-Object { $_.Captured -and $_.Success } | ForEach-Object { $_.Path })
        AssertionsTotal = $assertResult.Total
        AssertionsMet = $assertResult.Met
        Failures = @($assertResult.Failures)
    }
}

# ----------------------------------------------------------------
# Optional: queued build first
# ----------------------------------------------------------------

if ($Build) {
    if (-not $Session) {
        $Session = "smoke-{0:yyyyMMddHHmmss}" -f (Get-Date)
    }
    Write-Info ("Running queued build (session={0}, target=client)" -f $Session)
    $buildScript = Join-Path $ProjectRoot "devtools\build-session.ps1"
    if (-not (Test-Path -LiteralPath $buildScript)) {
        throw "Cannot find queued build wrapper: $buildScript"
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript -Session $Session -Target client
    $rc = $LASTEXITCODE
    if ($rc -ne 0) {
        Write-Fail ("Build failed with exit code {0}. Aborting smoke run." -f $rc)
        exit $rc
    }
    if (-not $SourceBinary) {
        $builtBinary = Join-Path $ProjectRoot (Join-Path ".claude\session-builds\$Session" "PerfectDark.exe")
        if (Test-Path -LiteralPath $builtBinary) {
            $SourceBinary = (Resolve-Path -LiteralPath $builtBinary).Path
            Write-Info ("Using freshly built smoke binary: {0}" -f $SourceBinary)
        } else {
            Write-Warn ("Queued build completed but session binary was not found at {0}; smoke install will use normal binary discovery." -f $builtBinary)
        }
    }
}

# ----------------------------------------------------------------
# Discover + select
# ----------------------------------------------------------------

$all = @(Get-SmokeTests -Dir $TestsDir)
if ($all.Count -eq 0) {
    throw "No tests found in $TestsDir"
}

# c115 S-1 fix: wrap in @(...) so a single-test return doesn't get
# auto-unwrapped to a PSCustomObject -- otherwise $selected.Count
# crashes under Set-StrictMode -Version Latest with PropertyNotFound.
$selected = @(Select-SmokeTests `
    -All $all `
    -Names $Test `
    -Tags $Tag `
    -UseAutoSelect:$AutoSelect `
    -AutoSelectBase $MergeBase)

if ($selected.Count -eq 0) {
    Write-Warn "No tests matched the selection criteria."
    exit 0
}

Write-Info ("Selected {0} of {1} tests." -f $selected.Count, $all.Count)
foreach ($t in $selected) {
    Write-Info ("  - {0}" -f $t.Name)
}

# ----------------------------------------------------------------
# Run
# ----------------------------------------------------------------

if ($useSharedInstall) {
    Write-Info "Install mode: shared (.claude/smoke-verify-install/). Firewall rule pinned to canonical path."
} elseif ($Install) {
    Write-Info ("Install mode: existing dir at {0}" -f $Install)
} else {
    Write-Info "Install mode: per-test (.claude/smoke-verify-runs/<utc>-<test>/). Firewall prompt may appear on each new path."
}

$results = @()
foreach ($t in $selected) {
    # Some native/helper calls inside Invoke-SmokeTest can write auxiliary
    # objects to the success pipeline. Capture the complete stream, then keep
    # exactly the typed scenario result instead of assuming every emitted
    # object exposes Passed/Name/Assertions fields (B-1029).
    $invokeOutput = @(Invoke-SmokeTest `
        -Test $t `
        -ExistingInstall $Install `
        -KeepOnSuccess:$Keep `
        -TimeoutOverride $Timeout `
        -VerboseEval:$VerboseAssertions `
        -BinaryOverride $SourceBinary `
        -RomOverride $SourceRom `
        -Shared:$useSharedInstall)
    $resultCandidates = @($invokeOutput | Where-Object {
        $_ -and $_.PSObject -and
        $_.PSObject.Properties.Match('Passed').Count -gt 0 -and
        $_.PSObject.Properties.Match('AssertionsTotal').Count -gt 0
    })
    if ($resultCandidates.Count -ne 1) {
        throw "Smoke test '$($t.Name)' emitted $($resultCandidates.Count) typed result objects; expected exactly one."
    }
    $results += $resultCandidates[0]
}

# ----------------------------------------------------------------
# Summary
# ----------------------------------------------------------------

Write-Host ""
Write-Host "  Summary" -ForegroundColor Cyan
Write-Host "  -------" -ForegroundColor Cyan
$passCount = 0
$failCount = 0
foreach ($r in $results) {
    $resultLabel = if ($r.Passed) { "PASS" } else { "FAIL" }
    $color = if ($r.Passed) { "Green" } else { "Red" }
    $bugSuffix = ""
    if ($r.BugId) { $bugSuffix = " [{0}]" -f $r.BugId }
    Write-Host ("  [{0}] {1}{2} ({3:N1}s) assertions={4}/{5}" -f `
        $resultLabel, $r.Name, $bugSuffix, $r.ElapsedSeconds, $r.AssertionsMet, $r.AssertionsTotal) -ForegroundColor $color
    if ($r.Passed) { $passCount++ } else { $failCount++ }
}

Write-Host ""
Write-Host ("  Total: {0} pass, {1} fail" -f $passCount, $failCount) -ForegroundColor $(if ($failCount -eq 0) { "Green" } else { "Red" })

# Write a machine-readable result file alongside the latest run
if (-not (Test-Path -LiteralPath $RunRoot)) {
    New-Item -ItemType Directory -Path $RunRoot -Force | Out-Null
}
$ResultsFile = Join-Path $RunRoot ("results-{0:yyyyMMddTHHmmssZ}.json" -f ((Get-Date).ToUniversalTime()))
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ResultsFile -Encoding UTF8
Write-Info ("Results written to: {0}" -f $ResultsFile)

exit $(if ($failCount -eq 0) { 0 } else { 1 })
} finally {
    Stop-SmokeOwnedFaultProcesses
    [void][PdSmokeWinErrorMode]::SetErrorMode($previousErrorMode)
}
