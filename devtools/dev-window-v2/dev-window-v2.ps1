# ============================================================================
# dev-window-v2.ps1 - Perfect Dark 2 Dev Window v2
#
# Independent fork of the Dev Window. Professional WPF UI with PD accents.
# Tabs: Build, Log, Docs. No Playtest tab.
# Keyboard shortcuts, status bar, log tail with filter.
#
# PowerShell 5.1 compatible. WPF (inline XAML). No external deps.
# ============================================================================

# ============================================================================
# Section 0: PATH fix - BEFORE ANYTHING ELSE
# ============================================================================

# Suppress prelude Write-Host when launched from a visible console (see Dev Window v2.bat).
$env:PD_BUILD_ENV_QUIET = '1'

# Build environment -- self-configures TEMP/TMP, PATH (MinGW64), MSYSTEM, ccache.
. (Join-Path (Join-Path $PSScriptRoot "..") "_build-env-prelude.ps1")

# ----------------------------------------------------------------------------
# GitHub CLI PATH: APPEND Machine+User PATH so gh.exe is reachable for the auth
# runspace (same intent as original Dev Window / _dev-window.ps1 Section 22).
#
# NOTE (audit 2026-04-16): previously this function *prepended* Machine+User PATH
# onto $env:Path AFTER _build-env-prelude.ps1 had already placed
# C:\msys64\mingw64\bin at the front. That shadowing let a Cygwin or devkitPro
# cmake/gcc/ninja earlier in the system PATH win over the MSYS2 MinGW64
# toolchain and broke CMake configure in v2 where v1 was fine.
# We now APPEND instead, so the prelude's prepend keeps precedence.
# ----------------------------------------------------------------------------
function Sync-UserMachinePath {
    try {
        $m = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
        $u = [System.Environment]::GetEnvironmentVariable('Path', 'User')
        # Deduplicate: only append segments not already present (case-insensitive).
        # Previous version appended on every call, growing PATH on each auth re-check.
        $seen = @{}
        foreach ($seg in ($env:Path -split ';')) {
            $key = $seg.TrimEnd('\').ToLowerInvariant()
            if ($key) { $seen[$key] = $true }
        }
        $toAdd = @()
        foreach ($src in @($m, $u)) {
            if (-not $src) { continue }
            foreach ($seg in ($src -split ';')) {
                $key = $seg.TrimEnd('\').ToLowerInvariant()
                if ($key -and -not $seen.ContainsKey($key)) {
                    $seen[$key] = $true
                    $toAdd += $seg
                }
            }
        }
        if ($toAdd.Count -gt 0) { $env:Path = $env:Path + ';' + ($toAdd -join ';') }
    } catch {}
}

Sync-UserMachinePath

# ============================================================================
# Section 1: Assembly loading + console hide
# ============================================================================

Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase
Add-Type -AssemblyName System.Windows.Forms

# S482: force WPF software rendering. Mike was hitting a blank-window state
# where the WPF visual tree rendered correctly to RenderTargetBitmap (verified
# in-process: BtnBuild 1255x104, TabControl 2564x723, all elements visible)
# but PrintWindow + screen capture both returned pure blank white -- the
# HWND composition / GPU pipeline path was silently dropping the visual tree.
# RenderOptions.ProcessRenderMode = SoftwareOnly bypasses the GPU/DWM path
# entirely and uses CPU rendering for the whole process. Slightly slower than
# hardware-accelerated WPF on big windows but well within tolerance for a
# developer tool with no animations. This is the standard WPF workaround for
# composition-pipeline disconnection (driver state, DWM glitch, virtual
# display surface mismatch). MUST be set before the first Window is built.
[System.Windows.Media.RenderOptions]::ProcessRenderMode = [System.Windows.Interop.RenderMode]::SoftwareOnly

# Perf: consolidated to a single Add-Type compile. Three separate Add-Type
# -Language CSharp calls were costing ~2-4s extra on cold start (each runs the
# C# compiler from scratch). Guard on the last type so the single compile only
# runs on first launch.
if (-not ([System.Management.Automation.PSTypeName]'PD2V2.AsyncLineReader').Type) {
    Add-Type -Language CSharp @"
using System;
using System.IO;
using System.Threading;
using System.Collections.Concurrent;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace PD2V2 {
    public static class DpiUtil {
        [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    }
    public class ConsoleHider {
        [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
        [DllImport("user32.dll")]   public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        public const int SW_HIDE = 0;
        public static void Hide() {
            IntPtr hwnd = GetConsoleWindow();
            if (hwnd != IntPtr.Zero) ShowWindow(hwnd, SW_HIDE);
        }
    }
    public class AsyncLineReader {
        public static void StartReading(StreamReader reader, ConcurrentQueue<string> queue, string prefix) {
            var t = new Thread(() => {
                try {
                    string line;
                    while ((line = reader.ReadLine()) != null) queue.Enqueue(prefix + line);
                } catch {}
            });
            t.IsBackground = true;
            t.Start();
        }
    }
    // INotifyPropertyChanged row model for the Clear Worktrees DataGrid. PSCustomObject
    // does not raise PropertyChanged through WPF binding, so Select All / Select None
    // visual refreshes need a real CLR class.
    public class WorktreeEntry : INotifyPropertyChanged {
        public event PropertyChangedEventHandler PropertyChanged;
        void Notify(string n) { var h = PropertyChanged; if (h != null) h(this, new PropertyChangedEventArgs(n)); }
        public string Name { get; set; }
        public string Path { get; set; }
        public string Branch { get; set; }
        public DateTime LastModified { get; set; }
        public string ModifiedDisplay { get; set; }
        public long SizeBytes { get; set; }
        public string SizeDisplay { get; set; }
        public bool IsRegistered { get; set; }
        public bool IsPrunable { get; set; }
        public bool IsCurrent { get; set; }
        public bool IsStale { get; set; }
        public string StaleTag { get; set; }
        bool _sel;
        public bool IsSelected { get { return _sel; } set { if (_sel != value) { _sel = value; Notify("IsSelected"); } } }
    }
}
"@
}
try { [void][PD2V2.DpiUtil]::SetProcessDPIAware() } catch {}
[PD2V2.ConsoleHider]::Hide()

# ============================================================================
# Section 2: (reserved -- C# helpers consolidated above)
# ============================================================================

# ============================================================================
# Section 3: Configuration
# ============================================================================

$script:ScriptDir           = $PSScriptRoot
$script:ProjectRoot         = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
try { $script:ProjectRoot = [System.IO.Path]::GetFullPath($script:ProjectRoot) } catch {}
. (Join-Path (Join-Path $script:ProjectRoot "devtools") "project-state-sync.ps1")
$script:BuildDir            = [System.IO.Path]::GetFullPath((Join-Path $script:ProjectRoot "Build"))   # unified dir for pd + pd-server
$script:SettingsPath        = Join-Path $script:ScriptDir "settings.json"
$script:ReleaseCachePath    = Join-Path $script:ProjectRoot ".dev-window-release-cache.json"
$script:AddinDir            = Join-Path $script:ProjectRoot "..\post-batch-addin"
# Pin native MinGW CMake (not bare "cmake" on PATH): MSYS usr\bin also ships cmake; that Cygwin build
# maps C:\... CWD to /home/... and corrupts -S/-B paths when mixed with Windows paths.
$script:CMake               = "C:/msys64/mingw64/bin/cmake.exe"
$script:CC                  = "C:/msys64/mingw64/bin/cc.exe"
$script:CXX                 = "C:/msys64/mingw64/bin/c++.exe"
$script:Python              = $(if (Test-Path -LiteralPath "C:/Python312/python.exe") { "C:/Python312/python.exe" } else { "C:/msys64/usr/bin/python3.exe" })
$script:ClientExeName       = "PerfectDark.exe"
$script:ServerExeName       = "PerfectDarkServer.exe"
$script:SoundsDir           = Join-Path $script:ProjectRoot "dist\build-sounds"

$script:BuildProcess        = $null
$script:BuildStepQueue      = [System.Collections.ArrayList]::new()
$script:CurrentBuildTarget  = "client"
$script:ClientErrors        = [System.Collections.ArrayList]::new()
$script:ServerErrors        = [System.Collections.ArrayList]::new()
$script:AllOutput           = [System.Collections.ArrayList]::new()
$script:ClientBuildResult   = $null
$script:ServerBuildResult   = $null
$script:ClientBuildTime     = 0
$script:ServerBuildTime     = 0
$script:IsBuilding          = $false
$script:IsPushing           = $false
$script:HasBuildErrors      = $false
$script:BuildPercent        = 0
$script:SpinnerIndex        = 0
$script:SpinnerChars        = @('|', '/', '-', '\')
$script:StepStartTime       = [DateTime]::Now
$script:LastOutputTime      = [DateTime]::Now
$script:OutputQueue         = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
$script:CurrentStepName     = ""
$script:LastReleaseHeartbeat = [DateTime]::MinValue
$script:ForceCleanBuild     = $false
$script:BuildVersion        = $null

$script:GameProcess         = $null
$script:TestsProcess        = $null
$script:TestsRunning        = $false
$script:GitChangeCount      = 0
$script:GitBusy             = $false
$script:KanbanRemoteBusy    = $false
$script:KanbanRemoteStopBusy = $false

$script:GhAuthOk            = $false
$script:GhCliAvailable      = $false
$script:GhAuthChecked       = $false
$script:GhAuthRefreshBusy   = $false  # runspace probe in flight
$script:LastGhAuthProbeUtc  = [DateTime]::UtcNow
$script:GhAuthRunspaceStartedUtc = [DateTime]::UtcNow
$script:LastGhAuthWaitLogUtc     = [DateTime]::MinValue
$script:GhAuthPollTimer     = $null
$script:GhAuthPS            = $null
$script:GhAuthRS            = $null
$script:GhAuthHandle        = $null
$script:GhAuthHadGhOnHost   = $false  # set before each probe; used if probe times out
$script:DevWindowDebugLogPath = Join-Path $script:ScriptDir "dev-window-v2-debug.log"
$script:DevWindowConsoleLogPath = Join-Path $script:ScriptDir "dev-window-v2-console.log"
$script:DevWindowConsoleLogKeep = 3

# Perf: persistent background runspace pool. Used by Update-StatusBar (every
# 2s tick), Populate-DocList, and on-demand git/bash actions. Without this,
# each background task opens a fresh runspace — Runspace.Open() is ~100-500ms
# on the UI thread, which is why the window felt stuttery every 2s.
$script:BgPool = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspacePool(1, 4)
$script:BgPool.ApartmentState = [System.Threading.ApartmentState]::MTA
$script:BgPool.ThreadOptions  = [System.Management.Automation.Runspaces.PSThreadOptions]::ReuseThread
$script:BgPool.Open()
$script:DocListLoadBusy     = $false
$script:GitActionBusy       = $false
$script:GitActionLabel      = ""
$script:GitSyncBusy         = $false

# Pending-state stash for async callbacks.
# GetNewClosure() creates a scriptblock with a SEPARATE $script: scope (initially empty);
# reads return $null and writes do not propagate to the main module. To avoid that trap we
# pass plain (non-closure) scriptblocks as OnComplete callbacks and stash any locals that
# would otherwise have been captured via GetNewClosure into $script: vars here. Safe because
# only one build / one release / one git sync runs at a time (guarded by IsBuilding / IsPushing
# / GitSyncBusy).
$script:PendingGitSyncCallback = $null
$script:CurrentBuildClean      = $false
$script:PendingReleaseVer      = $null
$script:PendingReleaseVs       = ""
$script:PendingReleaseKind     = ""
$script:PendingReleaseIsStable = $false
$script:PendingReleaseScript   = ""

# Live status streaming: drained by DispatcherTimer, written by async line readers.
$script:GameOutputQueue     = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
$script:TestsOutputQueue    = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
$script:BuildStepsTotal     = 0
$script:BuildStepsCompleted = 0
$script:NinjaCurrent        = 0
$script:NinjaTotal          = 0

# ============================================================================
# Section 4: Settings persistence
# ============================================================================

function Load-Settings {
    # Defaults bumped 2026-04-27 (S478) to fit the doubled-bold UI font sweep
    # (Mike's directive: "All the font should be at least double the size,
    # and bold."). First-time launch uses these; subsequent launches restore
    # the user's last manual size from settings.json.
    $defaults = @{
        WindowWidth   = 1700
        WindowHeight  = 1100
        WindowLeft    = -1
        WindowTop     = -1
        GitHubRepo    = ""
        EnableSounds  = $true
    }
    if (-not (Test-Path $script:SettingsPath)) { return $defaults }
    try {
        $json = Get-Content $script:SettingsPath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json
        $result = @{}
        foreach ($key in $defaults.Keys) {
            $result[$key] = $(if ($null -ne $json.$key) { $json.$key } else { $defaults[$key] })
        }
        return $result
    } catch { return $defaults }
}

function Save-Settings($settings) {
    try {
        $settings | ConvertTo-Json -Depth 2 | Set-Content $script:SettingsPath -Encoding UTF8 -ErrorAction Stop
    } catch {}
}

$script:Settings = Load-Settings

# ============================================================================
# Section 5: Utility functions
# ============================================================================

# Debug log (UTF-8 append). Path: devtools/dev-window-v2/dev-window-v2-debug.log
# Disable: $env:PD_DEV_WINDOW_DEBUG = '0'
function Write-DevWindowDebugLog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message,
        [ValidateSet('INFO', 'WARN', 'ERROR', 'AUTH', 'DEBUG')]
        [string]$Level = 'INFO'
    )
    if ($env:PD_DEV_WINDOW_DEBUG -eq '0') { return }
    try {
        $ts = [DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss.fff')
        $safe = $Message -replace "`r`n", ' | ' -replace "`n", ' | '
        $line = "[$ts] [$Level] $safe"
        Add-Content -LiteralPath $script:DevWindowDebugLogPath -Value $line -Encoding UTF8 -ErrorAction Stop
    } catch {}
}

Write-DevWindowDebugLog ("Session start pid=$PID PSVersion=$($PSVersionTable.PSVersion) ProjectRoot=$($script:ProjectRoot) LogFile=$($script:DevWindowDebugLogPath)") "INFO"

function Initialize-DevWindowConsoleLog {
    try {
        $base = $script:DevWindowConsoleLogPath
        $keep = [Math]::Max(1, [int]$script:DevWindowConsoleLogKeep)
        for ($i = $keep - 1; $i -ge 1; $i--) {
            $src = if ($i -eq 1) { $base } else { "$base." + ($i - 1) }
            $dst = "$base.$i"
            if (Test-Path -LiteralPath $dst) {
                Remove-Item -LiteralPath $dst -Force -ErrorAction SilentlyContinue
            }
            if (Test-Path -LiteralPath $src) {
                Move-Item -LiteralPath $src -Destination $dst -Force -ErrorAction SilentlyContinue
            }
        }
        $header = "[{0}] Dev Window v2 console log start pid={1} root={2}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"), $PID, $script:ProjectRoot
        Set-Content -LiteralPath $base -Value $header -Encoding UTF8 -ErrorAction Stop
    } catch {
        Write-DevWindowDebugLog ("Console log init failed: " + $_.Exception.Message) "WARN"
    }
}

function Write-DevWindowConsoleLog {
    param([AllowNull()][string]$Text)
    try {
        if ($null -eq $Text) { $Text = "" }
        $safe = $Text -replace "`r`n", "`n" -replace "`r", "`n"
        foreach ($line in ($safe -split "`n", -1)) {
            $entry = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"), $line
            Add-Content -LiteralPath $script:DevWindowConsoleLogPath -Value $entry -Encoding UTF8 -ErrorAction Stop
        }
    } catch {}
}

Initialize-DevWindowConsoleLog

function Classify-Line($line) {
    if ($line -match '(?i)\berror\b|^FAILED|undefined reference|multiple definition|fatal error|Write-Error|ErrorRecord|Exception:|ERROR\s*:|^\s*At .+:\d+ char:\d+') { return "error" }
    if ($line -match '(?i)\bwarning\b|Write-Warning|WARNING\s*:|^\s*WARN\s') { return "warning" }
    if ($line -match '^\[\d{2}:\d{2}:\d{2}\]\s+Still running:') { return "info" }
    return "normal"
}

function Format-ElapsedTime($seconds) {
    if ($seconds -lt 60) { return "" + $seconds + "s" }
    $m = [math]::Floor($seconds / 60); $s = $seconds % 60
    return "" + $m + "m " + $s + "s"
}

# cmd.exe build steps do not always inherit the same PATH as this PowerShell host; resolve git and
# prepend tool dirs so "git" and CMake/MinGW binaries are found.
function Resolve-GitExecutable {
    # Prefer MinGW64 or Git-for-Windows over MSYS2 usr\bin\git.exe. The latter is Cygwin-style
    # (errors show /c/Users/...), competes badly with IDE git (Cursor/VS Code) for index.lock, and
    # matches the project rule to pin mingw64 for CMake/tooling consistency.
    $preferred = @("C:\msys64\mingw64\bin\git.exe")
    if ($env:ProgramFiles) { $preferred += (Join-Path $env:ProgramFiles "Git\cmd\git.exe") }
    if (${env:ProgramFiles(x86)}) { $preferred += (Join-Path ${env:ProgramFiles(x86)} "Git\cmd\git.exe") }
    foreach ($p in $preferred) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    $cmd = Get-Command git -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        if ($cmd.Path) { return $cmd.Path }
        if ($cmd.Source) { return $cmd.Source }
    }
    foreach ($candidate in @("C:\msys64\usr\bin\git.exe")) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    return $null
}

function Get-MsysToolPath {
    param(
        [string]$ToolName,
        [string]$GitExe
    )
    if (-not $ToolName) { return $null }
    if (-not $GitExe) { $GitExe = Resolve-GitExecutable }
    if ($GitExe) {
        try {
            $gitDir = Split-Path -Parent $GitExe
            $msysRoot = Split-Path -Parent $gitDir
            $msysLeaf = Split-Path -Leaf $msysRoot
            if ($msysLeaf -ieq "usr" -or $msysLeaf -ieq "mingw64") {
                $msysRoot = Split-Path -Parent $msysRoot
            }
            if ($msysRoot) {
                $toolPath = Join-Path (Join-Path $msysRoot "usr\bin") ($ToolName + ".exe")
                if (Test-Path -LiteralPath $toolPath) { return $toolPath }
            }
        } catch {}
    }
    $fallback = Join-Path "C:\msys64\usr\bin" ($ToolName + ".exe")
    if (Test-Path -LiteralPath $fallback) { return $fallback }
    return $null
}

# Remove stale .git/index.lock (crash/interrupt leaves it; sometimes read-only). Safe before git add/commit
# when no other git process is actively using the repo (solo-dev Dev Window assumption).
function Clear-StaleGitIndexLock {
    param([Parameter(Mandatory=$true)][string]$Root)
    if (-not $Root) { return }
    try { $Root = [System.IO.Path]::GetFullPath($Root) } catch { return }

    $winLock = Join-Path $Root ".git\index.lock"
    if (Test-Path -LiteralPath $winLock) {
        try { & cmd.exe /c "attrib -R `"$winLock`"" 2>$null | Out-Null } catch {}
        try { Remove-Item -LiteralPath $winLock -Force -ErrorAction SilentlyContinue } catch {}
        try { & cmd.exe /c "del /f /q `"$winLock`"" 2>$null | Out-Null } catch {}
    }
    # Another git (IDE, terminal) may hold the lock briefly; wait for release without deleting.
    if (Test-Path -LiteralPath $winLock) {
        $until = [DateTime]::UtcNow.AddMilliseconds(2800)
        while ((Test-Path -LiteralPath $winLock) -and [DateTime]::UtcNow -lt $until) {
            Start-Sleep -Milliseconds 200
        }
    }

    $gitExe = Resolve-GitExecutable
    $rmExe = Get-MsysToolPath -ToolName "rm" -GitExe $gitExe
    $cygpathExe = Get-MsysToolPath -ToolName "cygpath" -GitExe $gitExe
    if ($cygpathExe -and $rmExe) {
        try {
            $p = & $cygpathExe -au $Root 2>$null
            if ($p) {
                $p = $p.Trim().TrimEnd('/')
                if ($p) {
                    $msysLock = $p + '/.git/index.lock'
                    try { [void](& $rmExe -f -- $msysLock 2>&1) } catch {}
                }
            }
        } catch {}
    }

    $wslCmd = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if ($wslCmd) {
        $wslExe = $wslCmd.Source
        try {
            $w = & $wslExe wslpath -a $Root 2>$null
            if ($w) {
                $w = $w.Trim().TrimEnd('/')
                if ($w) {
                    $wslLock = $w + '/.git/index.lock'
                    try { [void](& $wslExe -- rm -f -- $wslLock 2>&1) } catch {}
                    if ($rmExe) { try { [void](& $rmExe -f -- $wslLock 2>&1) } catch {} }
                }
            }
        } catch {}
    }
}

function Get-ChildProcessPathEnv {
    $segments = [System.Collections.ArrayList]::new()
    $gitExe = Resolve-GitExecutable
    if ($gitExe) {
        $gd = Split-Path -Parent $gitExe
        if ($gd) { [void]$segments.Add($gd) }
    }
    # mingw64\bin MUST come before usr\bin: both contain cmake.exe; usr\bin wins Cygwin cmake (bad).
    foreach ($d in @("C:\msys64\mingw64\bin", "C:\msys64\usr\bin")) {
        if (Test-Path -LiteralPath $d) { [void]$segments.Add($d) }
    }
    $uniq = $segments | Select-Object -Unique
    if ($uniq.Count -gt 0) {
        return (($uniq -join ";") + ";" + $env:PATH)
    }
    return $env:PATH
}

function Ensure-WindowsLoaderErrorModeType {
    $type = [System.Management.Automation.PSTypeName]'PD2V2.WinErrorMode'
    if ($type.Type) { return }

    $src = @"
using System;
using System.Runtime.InteropServices;

namespace PD2V2
{
    public static class WinErrorMode
    {
        [DllImport("kernel32.dll")]
        public static extern uint SetErrorMode(uint uMode);
    }
}
"@
    Add-Type -TypeDefinition $src
}

function Set-ChildLaunchNoLoaderDialogs {
    Ensure-WindowsLoaderErrorModeType
    $semFailCriticalErrors = 0x0001
    $semNoGpFaultErrorBox = 0x0002
    $semNoOpenFileErrorBox = 0x8000
    $mode = $semFailCriticalErrors -bor $semNoGpFaultErrorBox -bor $semNoOpenFileErrorBox
    return [PD2V2.WinErrorMode]::SetErrorMode($mode)
}

function Restore-ChildLaunchErrorMode([uint32]$previousMode) {
    try {
        Ensure-WindowsLoaderErrorModeType
        [void][PD2V2.WinErrorMode]::SetErrorMode($previousMode)
    } catch {}
}

function Ensure-ExecutableRuntimeDlls([string]$ExePath, [string]$LogPrefix) {
    if (-not $ExePath) { return }
    $targetDir = Split-Path -Parent $ExePath
    if (-not $targetDir -or -not (Test-Path -LiteralPath $targetDir)) { return }

    if (-not $LogPrefix) { $LogPrefix = "launch" }
    $mingwBin = "C:\msys64\mingw64\bin"
    if (-not (Test-Path -LiteralPath $mingwBin)) {
        Add-LogSessionLine ($LogPrefix + ": MinGW runtime folder not found at " + $mingwBin) "#B81818"
        return
    }

    $runtimeDllNames = @(
        "libwinpthread-1.dll",
        "libgcc_s_seh-1.dll",
        "libstdc++-6.dll",
        "zlib1.dll",
        "SDL2.dll",
        "libcurl-4.dll",
        "libcrypto-3-x64.dll",
        "libssl-3-x64.dll",
        "libnghttp2-14.dll",
        "libssh2-1.dll",
        "libidn2-0.dll",
        "libunistring-5.dll",
        "libzstd.dll",
        "libbrotlidec.dll",
        "libbrotlicommon.dll",
        "libpsl-5.dll",
        "libiconv-2.dll"
    )

    $objdump = Join-Path $mingwBin "objdump.exe"
    if (Test-Path -LiteralPath $objdump) {
        try {
            $imports = @{}
            & $objdump -p $ExePath 2>$null |
                Select-String -Pattern "DLL Name:\s*(.+)$" |
                ForEach-Object {
                    $dllName = $_.Matches[0].Groups[1].Value.Trim().ToLowerInvariant()
                    if ($dllName) { $imports[$dllName] = $true }
                }
            if ($imports.Count -gt 0) {
                $runtimeDllNames = @($runtimeDllNames | Where-Object { $imports.ContainsKey($_.ToLowerInvariant()) })
            }
        } catch {
            Add-LogSessionLine ($LogPrefix + ": could not inspect runtime DLL imports; copying known MinGW runtime DLLs defensively") "#B36B00"
        }
    }

    $copied = 0
    foreach ($name in $runtimeDllNames) {
        $sourceDll = Join-Path $mingwBin $name
        if (-not (Test-Path -LiteralPath $sourceDll)) { continue }
        $targetDll = Join-Path $targetDir $name
        try {
            Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
            $copied++
        } catch {
            Add-LogSessionLine ($LogPrefix + ": failed to copy runtime DLL " + $name + ": " + $_.Exception.Message) "#B81818"
        }
    }

    if ($copied -gt 0) {
        Add-LogSessionLine ($LogPrefix + ": ensured " + $copied + " runtime DLL(s) in " + $targetDir) "#44586C"
    }
}

function Get-ExePath($name) {
    # Both pd and pd-server land in the unified Build/ directory
    $p = Join-Path $script:BuildDir $name
    if (Test-Path $p) { return $p }
    return $null
}

function Test-ExeExists($name) { return ($null -ne (Get-ExePath $name)) }

function Test-NeedsConfigure($buildDir, $ver = $null) {
    $cache = Join-Path $buildDir "CMakeCache.txt"
    if (-not (Test-Path $cache)) { return $true }
    $ninja = Join-Path $buildDir "build.ninja"
    if (-not (Test-Path $ninja)) { return $true }
    $cmake = Join-Path $script:ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path $cmake)) { return $true }
    if ((Get-Item $cmake).LastWriteTime -gt (Get-Item $cache).LastWriteTime) { return $true }
    try {
        $snippet = Get-Content -LiteralPath $cache -Raw -ErrorAction Stop
        # Cache produced under WSL/Unix while we build from C:\ -> mixed paths (".../home/.../C:/...")
        if ($script:ProjectRoot -match '^[A-Za-z]:\\' -and $snippet -match '/home/[^\s\r\n]+') {
            return $true
        }
        $expectedPython = $script:Python -replace '\\', '/'
        $pythonMatch = [regex]::Match($snippet, "(?m)^PD_PYTHON_EXECUTABLE(?::[^=]*)?=(.+?)\s*$")
        if (-not $pythonMatch.Success) { return $true }
        $cachedPython = $pythonMatch.Groups[1].Value.Trim() -replace '\\', '/'
        if ($cachedPython -ne $expectedPython) { return $true }
        if ($null -ne $ver) {
            $expected = @{
                VERSION_SEM_MAJOR = [int]$ver.Major
                VERSION_SEM_MINOR = [int]$ver.Minor
                VERSION_SEM_PATCH = [int]$ver.Patch
            }
            foreach ($name in $expected.Keys) {
                $m = [regex]::Match($snippet, "(?m)^$name(?::[^=]*)?=(\d+)\s*$")
                if (-not $m.Success) { return $true }
                if ([int]$m.Groups[1].Value -ne $expected[$name]) { return $true }
            }
        }
    } catch { return $true }
    return $false
}

# ============================================================================
# Section 6: Sound system
# ============================================================================

function Play-SuccessSound {
    if (-not $script:Settings.EnableSounds) { return }
    try {
        $wav = Join-Path $script:SoundsDir "success.wav"
        if (Test-Path $wav) {
            $player = New-Object System.Media.SoundPlayer($wav)
            $player.Play()
        }
    } catch {}
}

function Play-FailureSound {
    if (-not $script:Settings.EnableSounds) { return }
    try {
        $wav = Join-Path $script:SoundsDir "failure.wav"
        if (Test-Path $wav) {
            $player = New-Object System.Media.SoundPlayer($wav)
            $player.Play()
        }
    } catch {}
}

# ============================================================================
# Section 7: Version management (parity with v1)
# ============================================================================

function Get-ProjectVersion {
    $ver = @{Major=0; Minor=0; Patch=0}
    $cp  = Join-Path $script:ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path $cp)) { return $ver }
    try {
        $c = Get-Content $cp -Raw -Encoding UTF8 -ErrorAction Stop
        if ($c -match 'VERSION_SEM_MAJOR\s+(\d+)') { $ver.Major = [int]$Matches[1] }
        if ($c -match 'VERSION_SEM_MINOR\s+(\d+)') { $ver.Minor = [int]$Matches[1] }
        if ($c -match 'VERSION_SEM_PATCH\s+(\d+)') { $ver.Patch = [int]$Matches[1] }
    } catch {}
    return $ver
}

function Set-ProjectVersion($major, $minor, $patch) {
    # Two safety nets here, both new 2026-04-27 (S477):
    #
    # 1. Skip the write entirely when content is unchanged. The build success
    #    path calls this on every cycle even when version hasn't moved; the
    #    no-op write was rebumping CMakeLists.txt's mtime and (worse) silently
    #    introducing a UTF-8 BOM into the file -- which is what Mike was
    #    seeing as "1 uncommitted change" after every release.
    #
    # 2. When we DO write, use [System.IO.File]::WriteAllText with a no-BOM
    #    UTF-8 encoder. PowerShell 5.1's `Set-Content -Encoding UTF8` always
    #    emits a BOM regardless of `-NoNewline`. The MSYS2 git treats the
    #    BOM-modified CMakeLists.txt as a real change, so the working tree
    #    stays dirty until Mike commits a phantom byte.
    $cp = Join-Path $script:ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path $cp)) { return }
    try {
        $orig = Get-Content $cp -Raw -Encoding UTF8 -ErrorAction Stop
        $c = $orig -replace '(VERSION_SEM_MAJOR\s+)\d+', ("`${1}" + $major)
        $c = $c -replace '(VERSION_SEM_MINOR\s+)\d+', ("`${1}" + $minor)
        $c = $c -replace '(VERSION_SEM_PATCH\s+)\d+', ("`${1}" + $patch)
        if ($c -eq $orig) { return }   # no change, no write -- preserves mtime + bytes
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($cp, $c, $utf8NoBom)
    } catch {}
}

function Load-ReleaseCache {
    if (-not (Test-Path $script:ReleaseCachePath)) { return $null }
    try { return (Get-Content $script:ReleaseCachePath -Raw -ErrorAction Stop | ConvertFrom-Json) } catch { return $null }
}

function Save-ReleaseCache($data) {
    try { $data | ConvertTo-Json -Depth 3 | Set-Content $script:ReleaseCachePath -Encoding UTF8 -ErrorAction Stop } catch {}
}

# Slug used for `gh api repos/<slug>/releases/latest`. Same fork as the
# BtnOpenGitHub URL fallback. If GitHubRepo in settings.json is set as a
# raw "owner/repo" slug, prefer that; if it's a full URL, parse it; else
# default to the canonical fork.
function Get-GitHubRepoSlug {
    $r = $script:Settings.GitHubRepo
    if ($r -and $r -ne "") {
        if ($r -match '^https?://github\.com/([^/]+/[^/]+?)(?:\.git)?/?$') { return $Matches[1] }
        if ($r -match '^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$') { return $r }
    }
    return "MikeHazeJr/perfect-dark-2"
}

# Update the LblLatestRelease text/color from a parsed release object.
# Shape: PSCustomObject with .tag_name and .prerelease (matching the
# `gh api repos/.../releases/latest` JSON shape).
function Update-LatestReleaseLabel($cached) {
    if ($null -eq $cached) {
        $ui["LblLatestRelease"].Text = "latest: --"
        $ui["LblLatestRelease"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#7A8898")))
        return
    }
    try {
        $tag = $cached.tag_name
        $pre = $cached.prerelease
        $kind = $(if ($pre) { "dev" } else { "stable" })
        $ui["LblLatestRelease"].Text = "latest: " + $tag + " (" + $kind + ")"
        $color = $(if ($pre) { "#0078A8" } else { "#10783A" })
        $ui["LblLatestRelease"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($color)))
    } catch {}
}

$script:LatestReleaseRefreshBusy = $false

# Async fetch of the latest GitHub release via `gh api`. Updates
# LblLatestRelease + the on-disk cache on success. Used at startup (when
# no cache exists), on F5, and at the end of every successful release
# (Mike's S480 ask). Skips silently if gh is missing or not authed.
function Refresh-LatestRelease {
    if ($script:LatestReleaseRefreshBusy) { return }
    if (-not $script:GhCliAvailable) { return }
    $script:LatestReleaseRefreshBusy = $true

    # Surface a transient "checking..." state so the user sees the refresh
    # in flight; on completion we either update with the new value or
    # restore the previous cache value.
    $priorText = $ui["LblLatestRelease"].Text
    $priorBrush = $ui["LblLatestRelease"].Foreground
    $ui["LblLatestRelease"].Text = "latest: checking..."
    $ui["LblLatestRelease"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))

    $slug = Get-GitHubRepoSlug
    $envPath = $env:PATH

    Start-AsyncPoolAction `
        -Script {
            param($repoSlug, $envPathArg)
            try {
                $env:PATH = $envPathArg
                $j = & gh api ("repos/" + $repoSlug + "/releases/latest") 2>$null
                if ($LASTEXITCODE -eq 0 -and $j) {
                    return [PSCustomObject]@{ Ok = $true; Data = ($j | ConvertFrom-Json) }
                }
                return [PSCustomObject]@{ Ok = $false; Data = $null; Err = "gh api exit $LASTEXITCODE" }
            } catch {
                return [PSCustomObject]@{ Ok = $false; Data = $null; Err = $_.Exception.Message }
            }
        } `
        -Arguments @($slug, $envPath) `
        -OnComplete {
            param($result)
            try {
                $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
                if ($null -ne $r -and $r.Ok -and $r.Data) {
                    Update-LatestReleaseLabel $r.Data
                    Save-ReleaseCache $r.Data
                } else {
                    # Restore prior text + color so the user is not left with
                    # a stuck "checking..." after a failed probe.
                    $ui["LblLatestRelease"].Text = $priorText
                    $ui["LblLatestRelease"].Foreground = $priorBrush
                }
            } catch {}
            $script:LatestReleaseRefreshBusy = $false
        }
}

# ============================================================================
# Section 9: WPF XAML Definition
# ============================================================================

[xml]$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Perfect Dark 2  |  Dev Window v2"
        MinWidth="720" MinHeight="480"
        Background="#ECEEF2"
        WindowStartupLocation="CenterScreen"
        UseLayoutRounding="True"
        SnapsToDevicePixels="True"
        TextOptions.TextFormattingMode="Display"
        RenderOptions.ClearTypeHint="Enabled"
        TextElement.FontFamily="Segoe UI"
        TextElement.FontSize="30"
        TextElement.FontWeight="Bold"
        TextElement.Foreground="#1A2434">
    <Window.Resources>
        <!-- Light-theme PD palette (S477):
             page bg #ECEEF2, card bg #FFFFFF, card border #C0C8D2,
             primary text #1A2434, secondary #4A5868, dim #7A8898,
             PD cyan #0078A8 / hover #0090C8, BUILD teal-green #008860,
             RELEASE gold #A06A10, gold border #C8A015, magenta #A82070,
             error #B81818, warning #B86810, success #10783A. -->
        <Style x:Key="AccentBtn" TargetType="Button">
            <Setter Property="Background" Value="#0078A8"/>
            <Setter Property="Foreground" Value="#FFFFFF"/>
            <Setter Property="FontWeight" Value="Bold"/>
            <Setter Property="FontSize" Value="28"/>
            <Setter Property="BorderThickness" Value="0"/>
            <Setter Property="Padding" Value="22,14"/>
            <Setter Property="Cursor" Value="Hand"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="2" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#0090C8"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#C8CFD8"/>
                                <Setter Property="Foreground" Value="#7A8898"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="GreenBtn" TargetType="Button" BasedOn="{StaticResource AccentBtn}">
            <Setter Property="Background" Value="#10783A"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="2" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#149048"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#C8CFD8"/>
                                <Setter Property="Foreground" Value="#7A8898"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="GoldBtn" TargetType="Button" BasedOn="{StaticResource AccentBtn}">
            <Setter Property="Background" Value="#A06A10"/>
            <Setter Property="Foreground" Value="#FFFFFF"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="2" Padding="{TemplateBinding Padding}"
                                BorderBrush="#C8A015" BorderThickness="1">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#B87C18"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#C8CFD8"/>
                                <Setter Property="Foreground" Value="#7A8898"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="ToolBtn" TargetType="Button">
            <Setter Property="Background" Value="#FFFFFF"/>
            <Setter Property="Foreground" Value="#1A2434"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="BorderBrush" Value="#B0B8C2"/>
            <Setter Property="Padding" Value="20,12"/>
            <Setter Property="MinHeight" Value="58"/>
            <Setter Property="FontSize" Value="28"/>
            <Setter Property="FontWeight" Value="Bold"/>
            <Setter Property="VerticalAlignment" Value="Center"/>
            <Setter Property="Cursor" Value="Hand"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="2" Padding="{TemplateBinding Padding}"
                                BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#E0E8F0"/>
                                <Setter TargetName="border" Property="BorderBrush" Value="#0078A8"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter Property="Foreground" Value="#A0A8B2"/>
                                <Setter TargetName="border" Property="Background" Value="#F5F7FA"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="RedBtn" TargetType="Button" BasedOn="{StaticResource AccentBtn}">
            <Setter Property="Background" Value="#B81818"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="2" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#D02828"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="OrangeBtn" TargetType="Button" BasedOn="{StaticResource AccentBtn}">
            <Setter Property="Background" Value="#A85420"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="2" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#C46428"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#C8CFD8"/>
                                <Setter Property="Foreground" Value="#7A8898"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
    </Window.Resources>

    <!-- Scaling host (S-scale): the whole UI is laid out at a fixed design
         canvas (1480x900, the size everything was tuned to fit at) and a
         Viewbox scales it UNIFORMLY to whatever the window is. StretchDirection
         DownOnly means big screens render at native 1:1 (Mike's doubled-bold
         fonts untouched) while smaller screens shrink the entire UI to fit
         instead of clipping it. Centered so the page bg frames it evenly. -->
    <Viewbox Stretch="Uniform" StretchDirection="DownOnly"
             HorizontalAlignment="Center" VerticalAlignment="Center">
    <DockPanel Width="1480" Height="900">
        <!-- Header Brand Bar (light theme, narrow PD-styled band; cyan accent
             on the PD2 chip + "v2" label preserves PD identity without
             dominating the page) -->
        <Border DockPanel.Dock="Top" Background="#FFFFFF" BorderBrush="#0078A8" BorderThickness="0,0,0,3" Padding="20,14">
            <DockPanel>
                <TextBlock DockPanel.Dock="Right"
                           Text="Ctrl+B=Build    Ctrl+R=Release    Ctrl+L=Log    Ctrl+G=Game    Ctrl+T=Tests"
                           Foreground="#7A8898" FontSize="26" FontFamily="Consolas"
                           VerticalAlignment="Center"/>
                <StackPanel Orientation="Horizontal" VerticalAlignment="Center">
                    <Border Background="#0078A8" CornerRadius="2" Padding="8,3" Margin="0,0,10,0">
                        <TextBlock Text="PD2" FontSize="28" FontWeight="Black" Foreground="#FFFFFF"
                                   FontFamily="Consolas"/>
                    </Border>
                    <TextBlock Text="Dev Window" FontSize="30" Foreground="#1A2434"
                               FontWeight="Bold" VerticalAlignment="Center"/>
                    <TextBlock Text=" v2" FontSize="30" Foreground="#0078A8"
                               FontWeight="Bold" VerticalAlignment="Center"/>
                </StackPanel>
            </DockPanel>
        </Border>

        <!-- Status Bar (bottom; promoted to primary info row, light theme).
             Mike: this is "primary interactive info"; readable, not crammed.
             FontSize 15 Consolas with vertical separators. -->
        <Border DockPanel.Dock="Bottom" Background="#F5F7FA" BorderBrush="#C0C8D2" BorderThickness="0,1,0,0" Padding="22,16">
            <DockPanel>
                <TextBlock x:Name="StatusVersion" Text="v0.0.0" Foreground="#A06A10"
                           FontFamily="Consolas" FontSize="30" FontWeight="Bold"
                           DockPanel.Dock="Right" VerticalAlignment="Center"/>
                <Rectangle Width="1" Fill="#C0C8D2" Margin="22,0" DockPanel.Dock="Right"/>
                <TextBlock x:Name="StatusAuth" Text="auth: ..." Foreground="#4A5868"
                           FontFamily="Consolas" FontSize="30" FontWeight="Bold"
                           DockPanel.Dock="Right" VerticalAlignment="Center" Margin="0,0,22,0"/>
                <Rectangle Width="1" Fill="#C0C8D2" Margin="0,0,22,0"/>
                <TextBlock x:Name="StatusMode" Text="Idle" Foreground="#1A2434"
                           FontFamily="Consolas" FontSize="30" FontWeight="Bold" Margin="0,0,22,0"/>
                <Rectangle Width="1" Fill="#C0C8D2" Margin="0,0,22,0"/>
                <TextBlock x:Name="StatusBranch" Text="branch: --" Foreground="#0078A8"
                           FontFamily="Consolas" FontSize="30" FontWeight="Bold" Margin="0,0,22,0"/>
                <Rectangle Width="1" Fill="#C0C8D2" Margin="0,0,22,0"/>
                <TextBlock x:Name="StatusHash" Text="HEAD: ------" Foreground="#4A5868"
                           FontFamily="Consolas" FontSize="30" FontWeight="Bold" Margin="0,0,22,0"/>
                <Rectangle Width="1" Fill="#C0C8D2" Margin="0,0,22,0"/>
                <TextBlock x:Name="StatusDirty" Text="clean" Foreground="#10783A"
                           FontFamily="Consolas" FontSize="30" FontWeight="Bold"/>
                <Rectangle Width="1" Fill="#C0C8D2" Margin="22,0"/>
                <TextBlock x:Name="StatusWorktrees" Text="worktrees: --" Foreground="#4A5868"
                           FontFamily="Consolas" FontSize="30" FontWeight="Bold" Margin="0,0,22,0"/>
            </DockPanel>
        </Border>

        <!-- (c127 2026-05-12) Run Tests / Run Game were here as an always-docked
             bottom bar. They now live inside the BUILD tab so they no longer
             obscure the CLI tab's LAUNCH button (or any other tab's content). -->

        <!-- Tab Control (light theme; PD cyan accent on the selected tab) -->
        <TabControl x:Name="TabControl" Background="#ECEEF2" BorderThickness="0" Padding="0">
            <TabControl.Resources>
                <Style TargetType="TabItem">
                    <Setter Property="Background" Value="#ECEEF2"/>
                    <Setter Property="Foreground" Value="#7A8898"/>
                    <Setter Property="Padding" Value="32,18"/>
                    <Setter Property="FontSize" Value="28"/>
                    <Setter Property="FontWeight" Value="Bold"/>
                    <Setter Property="Template">
                        <Setter.Value>
                            <ControlTemplate TargetType="TabItem">
                                <Border x:Name="tabBorder" Background="{TemplateBinding Background}"
                                        Padding="{TemplateBinding Padding}" Margin="0,0,0,0"
                                        BorderBrush="Transparent" BorderThickness="0,0,0,3">
                                    <ContentPresenter ContentSource="Header"/>
                                </Border>
                                <ControlTemplate.Triggers>
                                    <Trigger Property="IsSelected" Value="True">
                                        <Setter TargetName="tabBorder" Property="Background" Value="#FFFFFF"/>
                                        <Setter TargetName="tabBorder" Property="BorderBrush" Value="#0078A8"/>
                                        <Setter Property="Foreground" Value="#1A2434"/>
                                    </Trigger>
                                    <Trigger Property="IsMouseOver" Value="True">
                                        <Setter TargetName="tabBorder" Property="Background" Value="#E0E8F0"/>
                                        <Setter Property="Foreground" Value="#1A2434"/>
                                    </Trigger>
                                </ControlTemplate.Triggers>
                            </ControlTemplate>
                        </Setter.Value>
                    </Setter>
                </Style>
            </TabControl.Resources>

            <!-- BUILD TAB (S477 redesign + S480 ScrollViewer wrap). The
                 ScrollViewer means active-build content (progress bar +
                 STOP / Copy buttons appearing inside the STATUS card) can
                 grow without being clipped at the tab boundary; once the
                 build settles back to idle, the natural-height content fits
                 and the scrollbar disappears. -->
            <TabItem Header="BUILD">
              <ScrollViewer VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled" Padding="0">
                <DockPanel Margin="20,18,20,18" LastChildFill="False">
                    <!-- Hero Buttons Row (S478: doubled-bold text fits at MinHeight=104,
                         font 36 BUILD / 32 RELEASE Bold, padding 20,16). -->
                    <Grid DockPanel.Dock="Top" Margin="0,0,0,16">
                        <Grid.ColumnDefinitions>
                            <ColumnDefinition Width="*"/>
                            <ColumnDefinition Width="14"/>
                            <ColumnDefinition Width="*"/>
                        </Grid.ColumnDefinitions>
                        <Button x:Name="BtnBuild" Style="{StaticResource GreenBtn}"
                                FontSize="36" FontWeight="Black" MinHeight="104" Padding="20,16" Grid.Column="0">
                            <TextBlock Text="BUILD" FontSize="36" FontWeight="Black" FontFamily="Consolas"/>
                        </Button>
                        <Button x:Name="BtnRelease" Style="{StaticResource GoldBtn}"
                                FontSize="32" FontWeight="Bold" MinHeight="104" Padding="20,16" Grid.Column="2">
                            <TextBlock x:Name="TxtRelease" Text="RELEASE" TextAlignment="Center"
                                       FontSize="32" FontWeight="Bold" LineHeight="36"/>
                        </Button>
                    </Grid>

                    <!-- Run Tests + Run Game (c127 2026-05-12: moved from the
                         always-docked bottom bar into the BUILD tab so they no
                         longer obscure the CLI tab's LAUNCH button). Sized as
                         a secondary hero pair beneath BUILD/RELEASE. -->
                    <Grid DockPanel.Dock="Top" Margin="0,0,0,16">
                        <Grid.ColumnDefinitions>
                            <ColumnDefinition Width="*"/>
                            <ColumnDefinition Width="14"/>
                            <ColumnDefinition Width="*"/>
                        </Grid.ColumnDefinitions>
                        <Button x:Name="BtnRunTests" Content="RUN TESTS" Style="{StaticResource GoldBtn}"
                                FontSize="28" FontWeight="Bold" Padding="20,16" MinHeight="78" Grid.Column="0"
                                ToolTip="Build (if needed) and run pd-tests; output streams to the Log tab."/>
                        <Button x:Name="BtnRunGame" Content="RUN GAME" Style="{StaticResource GreenBtn}"
                                FontSize="28" FontWeight="Bold" Padding="20,16" MinHeight="78" Grid.Column="2"/>
                    </Grid>

                    <!-- Utility Buttons Row: one row, sits directly under the
                         hero pair so primary + supporting actions share top of
                         pane. Free-standing rather than crammed into a card. -->
                    <!-- Utility Row card. ScrollViewer keeps the row reachable
                         even at narrow widths now that doubled-bold buttons
                         take more horizontal real estate (S478). -->
                    <Border DockPanel.Dock="Top" Margin="0,0,0,16"
                            Background="#FFFFFF" BorderBrush="#C0C8D2" BorderThickness="1" CornerRadius="3" Padding="12,10">
                        <ScrollViewer HorizontalScrollBarVisibility="Auto" VerticalScrollBarVisibility="Disabled">
                            <StackPanel Orientation="Horizontal" VerticalAlignment="Center">
                                <Button x:Name="BtnOpenGitHub" Content="GitHub" Style="{StaticResource ToolBtn}" Margin="0,0,8,0"/>
                                <Button x:Name="BtnOpenFolder" Content="Project Folder" Style="{StaticResource ToolBtn}" Margin="0,0,8,0"/>
                                <Button x:Name="BtnOpenKanban" Content="Open Kanban" Style="{StaticResource ToolBtn}" Margin="0,0,8,0"
                                        ToolTip="Start the local kanban server if needed, then open the Active / Parked / Bugs board in the default browser (http://localhost:7531/)"/>
                                <Button x:Name="BtnStartKanbanServer" Content="Start Kanban Server" Style="{StaticResource ToolBtn}" Margin="0,0,8,0"
                                        ToolTip="Start remote phone access for Kanban only, then copy and show the join link."/>
                                <Button x:Name="BtnStopKanbanServer" Content="Stop Kanban Server" Style="{StaticResource ToolBtn}" Margin="0,0,8,0"
                                        ToolTip="Stop the tracked remote Kanban server and Cloudflare tunnel."/>
                                <Button x:Name="BtnCleanBuild" Content="Clean Build" Style="{StaticResource ToolBtn}" Margin="0,0,8,0"/>
                                <Button x:Name="BtnPull" Content="Pull" Style="{StaticResource ToolBtn}" Margin="0,0,8,0"
                                        ToolTip="git pull (current branch, upstream)"/>
                                <Button x:Name="BtnPush" Content="Push" Style="{StaticResource ToolBtn}" Margin="0,0,8,0"
                                        ToolTip="Commit pending changes, then push the current branch"/>
                                <Button x:Name="BtnPruneWorktrees" Content="Clear Worktrees..." Style="{StaticResource ToolBtn}" Margin="0,0,8,0"
                                        ToolTip="Show on-disk worktree sizes, select and remove with full git worktree remove + branch cleanup + stale-registry prune"/>
                                <Button x:Name="BtnCheck" Content="Check" Style="{StaticResource ToolBtn}"
                                        ToolTip="Validate clean git state + run git-snapshot.sh"/>
                            </StackPanel>
                        </ScrollViewer>
                    </Border>

                    <!-- Status Area: 2 columns, white cards on light bg.
                         Columns are equal share now (S479) so the right card
                         gets enough horizontal room for the doubled-bold
                         spinner row + auth/latest/local labels without
                         clipping. MinWidth on the right pinned to 740 so the
                         spinner triplet (~600 px at doubled-bold) plus card
                         padding fits comfortably. -->
                    <Grid DockPanel.Dock="Top">
                        <Grid.ColumnDefinitions>
                            <ColumnDefinition Width="*" MinWidth="500"/>
                            <ColumnDefinition Width="16"/>
                            <ColumnDefinition Width="*" MinWidth="740"/>
                        </Grid.ColumnDefinitions>

                        <!-- Left: Build Status (white card) -->
                        <Border Grid.Column="0" Background="#FFFFFF" CornerRadius="3"
                                BorderBrush="#C0C8D2" BorderThickness="1" Padding="20,18">
                            <StackPanel>
                                <TextBlock Text="S T A T U S" Foreground="#7A8898" FontSize="24"
                                           FontFamily="Consolas" FontWeight="Bold" Margin="0,0,0,8"/>
                                <TextBlock x:Name="LblClientStatus" Text="client: --"
                                           Foreground="#1A2434" FontFamily="Consolas" FontSize="32" FontWeight="Bold" Margin="0,0,0,5"/>
                                <TextBlock x:Name="LblServerStatus" Text="tests: --"
                                           Foreground="#1A2434" FontFamily="Consolas" FontSize="32" FontWeight="Bold" Margin="0,0,0,10"/>
                                <TextBlock x:Name="LblBuildActivity" Text="" Foreground="#4A5868"
                                           FontFamily="Consolas" FontSize="28" Margin="0,0,0,6" TextWrapping="Wrap"/>

                                <!-- Progress Bar (S478: height bumped to fit 26pt bold text) -->
                                <Border x:Name="ProgressBack" Background="#E0E8F0" Height="38"
                                        CornerRadius="2" Margin="0,6" Visibility="Collapsed"
                                        BorderBrush="#B0B8C2" BorderThickness="1">
                                    <Grid>
                                        <Border x:Name="ProgressFill" Background="#0078A8"
                                                CornerRadius="1" HorizontalAlignment="Left" Width="0"/>
                                        <TextBlock x:Name="LblProgressText" Text="" Foreground="#FFFFFF"
                                                   FontFamily="Consolas" FontSize="26" FontWeight="Bold"
                                                   HorizontalAlignment="Center" VerticalAlignment="Center"/>
                                    </Grid>
                                </Border>

                                <!-- Action Buttons Row (only visible when relevant) -->
                                <StackPanel Orientation="Horizontal" Margin="0,12,0,0">
                                    <Button x:Name="BtnStop" Content="STOP" Style="{StaticResource RedBtn}"
                                            Padding="22,12" Margin="0,0,8,0" Visibility="Collapsed"/>
                                    <Button x:Name="BtnCopyErrors" Content="Copy Errors" Style="{StaticResource ToolBtn}"
                                            Margin="0,0,8,0" Visibility="Collapsed"/>
                                    <Button x:Name="BtnCopyLog" Content="Copy Log" Style="{StaticResource ToolBtn}"
                                            Margin="0,0,8,0" Visibility="Collapsed"/>
                                </StackPanel>
                            </StackPanel>
                        </Border>

                        <!-- Right: Version + Auth (white card) -->
                        <Border Grid.Column="2" Background="#FFFFFF" CornerRadius="3"
                                BorderBrush="#C0C8D2" BorderThickness="1" Padding="20,18"
                                MinWidth="560" HorizontalAlignment="Stretch">
                            <StackPanel>
                                <TextBlock Text="V E R S I O N" Foreground="#7A8898" FontSize="24"
                                           FontFamily="Consolas" FontWeight="Bold" Margin="0,0,0,6"/>
                                <StackPanel Orientation="Horizontal" Margin="0,0,0,8">
                                    <!-- S479: spinner triplet trimmed to fit the right card without
                                         clipping. Buttons 48x52 (was 56x56), number boxes 64/64/72 (was
                                         80/80/92), inter-stack margin 8 (was 14). Per-stack width:
                                         MAJ/MIN = 48+64+48 = 160; PAT = 48+72+48 = 168. Triplet total
                                         = 160 + 8 + 160 + 8 + 168 = 504 px (was 616), comfortably
                                         under the right card's usable width. -->
                                    <StackPanel Margin="0,0,8,0">
                                        <TextBlock Text="MAJ" Foreground="#7A8898" FontSize="22"
                                                   FontFamily="Consolas" FontWeight="Bold" Margin="0,0,0,4"/>
                                        <StackPanel Orientation="Horizontal">
                                            <Button x:Name="BtnVerMajDown" Content="-" Style="{StaticResource ToolBtn}"
                                                    Padding="0" Width="48" MinHeight="52" FontFamily="Consolas" FontSize="28"/>
                                            <TextBox x:Name="TxtVerMajor" Width="64" TextAlignment="Center"
                                                     Background="#F5F7FA" Foreground="#A06A10" BorderBrush="#C0C8D2"
                                                     FontFamily="Consolas" FontWeight="Bold" FontSize="28" Padding="2"/>
                                            <Button x:Name="BtnVerMajUp" Content="+" Style="{StaticResource ToolBtn}"
                                                    Padding="0" Width="48" MinHeight="52" FontFamily="Consolas" FontSize="28"/>
                                        </StackPanel>
                                    </StackPanel>
                                    <StackPanel Margin="0,0,8,0">
                                        <TextBlock Text="MIN" Foreground="#7A8898" FontSize="22"
                                                   FontFamily="Consolas" FontWeight="Bold" Margin="0,0,0,4"/>
                                        <StackPanel Orientation="Horizontal">
                                            <Button x:Name="BtnVerMinDown" Content="-" Style="{StaticResource ToolBtn}"
                                                    Padding="0" Width="48" MinHeight="52" FontFamily="Consolas" FontSize="28"/>
                                            <TextBox x:Name="TxtVerMinor" Width="64" TextAlignment="Center"
                                                     Background="#F5F7FA" Foreground="#A06A10" BorderBrush="#C0C8D2"
                                                     FontFamily="Consolas" FontWeight="Bold" FontSize="28" Padding="2"/>
                                            <Button x:Name="BtnVerMinUp" Content="+" Style="{StaticResource ToolBtn}"
                                                    Padding="0" Width="48" MinHeight="52" FontFamily="Consolas" FontSize="28"/>
                                        </StackPanel>
                                    </StackPanel>
                                    <StackPanel>
                                        <!-- Display label is "REV" (Mike's S480 rename); the
                                             underlying control names + cmake variable
                                             (VERSION_SEM_PATCH) stay the same so code-behind
                                             and version-stamping are unchanged. -->
                                        <TextBlock Text="REV" Foreground="#7A8898" FontSize="22"
                                                   FontFamily="Consolas" FontWeight="Bold" Margin="0,0,0,4"
                                                   ToolTip="Revision (third semver segment; cmake VERSION_SEM_PATCH)"/>
                                        <StackPanel Orientation="Horizontal">
                                            <Button x:Name="BtnVerPatDown" Content="-" Style="{StaticResource ToolBtn}"
                                                    Padding="0" Width="48" MinHeight="52" FontFamily="Consolas" FontSize="28"/>
                                            <TextBox x:Name="TxtVerPatch" Width="86" TextAlignment="Center"
                                                     Background="#F5F7FA" Foreground="#A06A10" BorderBrush="#C0C8D2"
                                                     FontFamily="Consolas" FontWeight="Bold" FontSize="28" Padding="2"/>
                                            <Button x:Name="BtnVerPatUp" Content="+" Style="{StaticResource ToolBtn}"
                                                    Padding="0" Width="48" MinHeight="52" FontFamily="Consolas" FontSize="28"/>
                                        </StackPanel>
                                    </StackPanel>
                                </StackPanel>
                                <CheckBox x:Name="ChkStable" Content="Stable release" Foreground="#A06A10"
                                          FontSize="28" FontWeight="Bold" Margin="0,2,0,8"/>
                                <TextBlock x:Name="LblAuthStatus" Text="auth: ..." Foreground="#4A5868"
                                           FontFamily="Consolas" FontSize="28" FontWeight="Bold" Margin="0,0,0,4" Cursor="Hand"
                                           TextWrapping="Wrap"/>
                                <TextBlock x:Name="LblLatestRelease" Text="latest: --" Foreground="#4A5868"
                                           FontFamily="Consolas" FontSize="28" FontWeight="Bold" Margin="0,0,0,4"
                                           TextWrapping="Wrap"/>
                                <TextBlock x:Name="LblDevVersion" Text="local: --" Foreground="#0078A8"
                                           FontFamily="Consolas" FontSize="28" FontWeight="Bold" TextWrapping="Wrap"/>
                            </StackPanel>
                        </Border>
                    </Grid>
                </DockPanel>
              </ScrollViewer>
            </TabItem>

            <!-- LOG TAB (light theme: white surface for readability of long
                 streaming output; line color classification still applied via
                 Foreground per-Run in code-behind). -->
            <TabItem Header="LOG">
                <DockPanel Margin="14,12,14,12">
                    <DockPanel DockPanel.Dock="Top" Margin="0,0,0,8">
                        <Button x:Name="BtnLogClear" Content="Clear" Style="{StaticResource ToolBtn}"
                                DockPanel.Dock="Right" Margin="6,0,0,0"/>
                        <Button x:Name="BtnLogExport" Content="Export..." Style="{StaticResource ToolBtn}"
                                DockPanel.Dock="Right" Margin="6,0,0,0"/>
                        <CheckBox x:Name="ChkAutoScroll" Content="Auto-scroll" Foreground="#1A2434"
                                  FontFamily="Segoe UI" FontSize="28"
                                  IsChecked="True" DockPanel.Dock="Right" VerticalAlignment="Center" Margin="10,0"/>
                        <TextBox x:Name="TxtLogFilter" Background="#FFFFFF" Foreground="#4A5868"
                                 BorderBrush="#C0C8D2" Padding="8,5"
                                 FontFamily="Consolas" FontSize="28"
                                 Tag="Filter..." FontStyle="Italic"/>
                    </DockPanel>
                    <RichTextBox x:Name="LogOutput" Background="#FFFFFF" Foreground="#1A2434"
                                 IsReadOnly="True" BorderThickness="1" BorderBrush="#C0C8D2"
                                 FontFamily="Consolas"
                                 FontSize="28" VerticalScrollBarVisibility="Auto"
                                 HorizontalScrollBarVisibility="Auto"
                                 Padding="8,6">
                        <FlowDocument>
                            <Paragraph/>
                        </FlowDocument>
                    </RichTextBox>
                </DockPanel>
            </TabItem>

            <!-- DOCS TAB (light theme; left list + right content reader) -->
            <TabItem Header="DOCS">
                <Grid Margin="14,12,14,12">
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="320"/>
                        <ColumnDefinition Width="6"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <ListBox x:Name="DocList" Grid.Column="0" Background="#FFFFFF" Foreground="#1A2434"
                             BorderBrush="#C0C8D2" BorderThickness="1"
                             FontFamily="Consolas" FontSize="28"/>
                    <GridSplitter Grid.Column="1" Width="6" Background="#C0C8D2" HorizontalAlignment="Stretch"/>
                    <TextBox x:Name="DocContent" Grid.Column="2" Background="#FFFFFF" Foreground="#1A2434"
                             IsReadOnly="True" TextWrapping="Wrap" AcceptsReturn="True"
                             VerticalScrollBarVisibility="Auto" BorderThickness="1" BorderBrush="#C0C8D2"
                             FontFamily="Consolas" FontSize="28" Padding="8,6"/>
                </Grid>
            </TabItem>

            <!-- CLI TAB: launcher only. Strips the old prompt-composition
                 workbench (actions / cards / prompt box / modes) down to the
                 two buttons that open a CLI session in the project root. -->
            <TabItem Header="CLI">
                <DockPanel Margin="14,12,14,12" LastChildFill="True">

                    <!-- Header strip -->
                    <Border DockPanel.Dock="Top" Background="#FFFFFF" CornerRadius="3"
                            BorderBrush="#C0C8D2" BorderThickness="1" Padding="14,8" Margin="0,0,0,10">
                        <StackPanel Orientation="Horizontal">
                            <TextBlock Text="CLI" Foreground="#0078A8" FontFamily="Consolas"
                                       FontSize="24" FontWeight="Bold" Margin="0,0,14,0"/>
                            <Rectangle Width="1" Fill="#C0C8D2" Margin="0,2"/>
                            <TextBlock Text="Open a command-line session in the project root."
                                       Foreground="#7A8898"
                                       FontFamily="Segoe UI" FontSize="20" FontWeight="Bold" Margin="14,0,0,0"
                                       VerticalAlignment="Center"/>
                        </StackPanel>
                    </Border>

                    <!-- Launcher buttons (centered) -->
                    <Border Background="#FFFFFF" CornerRadius="3"
                            BorderBrush="#C0C8D2" BorderThickness="1" Padding="24,24">
                        <StackPanel Orientation="Horizontal" HorizontalAlignment="Center"
                                    VerticalAlignment="Center">
                            <Button x:Name="BtnCliLaunch" Content="Open Claude CLI"
                                    Style="{StaticResource GreenBtn}"
                                    FontSize="26" Padding="30,18" MinHeight="68" MinWidth="320"
                                    Margin="0,0,18,0"
                                    ToolTip="Open the Claude Code CLI in a new console at the project root."/>
                            <Button x:Name="BtnCliLaunchUltra" Content="Open Claude CLI (ultracode)"
                                    Style="{StaticResource AccentBtn}"
                                    FontSize="26" Padding="30,18" MinHeight="68" MinWidth="320"
                                    Margin="0,0,18,0"
                                    ToolTip="Open the Claude Code CLI with ultracode mode on (launches with --settings &quot;ultracode: true&quot;)."/>
                            <Button x:Name="BtnCliLaunchCodex" Content="Open Codex CLI"
                                    Style="{StaticResource ToolBtn}"
                                    FontSize="26" Padding="30,18" MinHeight="68" MinWidth="320"
                                    ToolTip="Open the Codex CLI as administrator in a new console at the project root."/>
                        </StackPanel>
                    </Border>

                </DockPanel>
            </TabItem>
        </TabControl>
    </DockPanel>
    </Viewbox>
</Window>
"@

# ============================================================================
# Section 10: Create WPF Window from XAML
# ============================================================================

$reader = New-Object System.Xml.XmlNodeReader $xaml
$window = [Windows.Markup.XamlReader]::Load($reader)

# Find named elements
$ui = @{}
$namedElements = @(
    "StatusBranch","StatusHash","StatusDirty","StatusWorktrees","StatusAuth","StatusVersion","StatusMode",
    "BtnRunGame","BtnRunTests","TabControl",
    "BtnBuild","BtnRelease","TxtRelease","BtnStop","BtnCopyErrors","BtnCopyLog","BtnCheck",
    "LblClientStatus","LblServerStatus","LblBuildActivity",
    "ProgressBack","ProgressFill","LblProgressText",
    "TxtVerMajor","TxtVerMinor","TxtVerPatch",
    "BtnVerMajDown","BtnVerMajUp","BtnVerMinDown","BtnVerMinUp","BtnVerPatDown","BtnVerPatUp",
    "ChkStable","LblAuthStatus","LblLatestRelease","LblDevVersion",
    "BtnOpenGitHub","BtnOpenFolder","BtnOpenKanban","BtnStartKanbanServer","BtnStopKanbanServer","BtnCleanBuild","BtnPull","BtnPush","BtnPruneWorktrees",
    "BtnLogClear","BtnLogExport","ChkAutoScroll","TxtLogFilter","LogOutput",
    "DocList","DocContent",
    "BtnCliLaunch","BtnCliLaunchUltra","BtnCliLaunchCodex"
)
foreach ($name in $namedElements) {
    $ui[$name] = $window.FindName($name)
}

# ============================================================================
# Section 11: Version UI wiring
# ============================================================================

function Update-ReleaseButtonText {
    $maj = 0; $min = 0; $pat = 0
    try { $maj = [int]$ui["TxtVerMajor"].Text } catch {}
    try { $min = [int]$ui["TxtVerMinor"].Text } catch {}
    try { $pat = [int]$ui["TxtVerPatch"].Text } catch {}
    $vs = "v" + $maj + "." + $min + "." + $pat
    $kind = $(if ($ui["ChkStable"].IsChecked) { "Stable" } else { "Dev" })
    if ($null -ne $ui["TxtRelease"]) { $ui["TxtRelease"].Text = "RELEASE`n" + $kind + " " + $vs }
}

function Get-UiVersion {
    $maj = 0; $min = 0; $pat = 0
    try { $maj = [int]$ui["TxtVerMajor"].Text } catch {}
    try { $min = [int]$ui["TxtVerMinor"].Text } catch {}
    try { $pat = [int]$ui["TxtVerPatch"].Text } catch {}
    return @{Major=$maj; Minor=$min; Patch=$pat}
}

function Refresh-VersionDisplay {
    $ver = Get-ProjectVersion
    $ui["TxtVerMajor"].Text = "" + $ver.Major
    $ui["TxtVerMinor"].Text = "" + $ver.Minor
    $ui["TxtVerPatch"].Text = "" + $ver.Patch
    Update-ReleaseButtonText
    if ($null -ne $ui["StatusVersion"]) {
        $ui["StatusVersion"].Text = "v" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch
    }
}

# Version spinner events
$ui["BtnVerMajDown"].Add_Click({ try { $v = [int]$ui["TxtVerMajor"].Text; if ($v -gt 0) { $ui["TxtVerMajor"].Text = "" + ($v - 1) }; Update-ReleaseButtonText } catch {} })
$ui["BtnVerMajUp"].Add_Click({   try { $v = [int]$ui["TxtVerMajor"].Text; $ui["TxtVerMajor"].Text = "" + ($v + 1); Update-ReleaseButtonText } catch {} })
$ui["BtnVerMinDown"].Add_Click({ try { $v = [int]$ui["TxtVerMinor"].Text; if ($v -gt 0) { $ui["TxtVerMinor"].Text = "" + ($v - 1) }; Update-ReleaseButtonText } catch {} })
$ui["BtnVerMinUp"].Add_Click({   try { $v = [int]$ui["TxtVerMinor"].Text; $ui["TxtVerMinor"].Text = "" + ($v + 1); Update-ReleaseButtonText } catch {} })
$ui["BtnVerPatDown"].Add_Click({ try { $v = [int]$ui["TxtVerPatch"].Text; if ($v -gt 0) { $ui["TxtVerPatch"].Text = "" + ($v - 1) }; Update-ReleaseButtonText } catch {} })
$ui["BtnVerPatUp"].Add_Click({   try { $v = [int]$ui["TxtVerPatch"].Text; $ui["TxtVerPatch"].Text = "" + ($v + 1); Update-ReleaseButtonText } catch {} })
$ui["ChkStable"].Add_Checked({ Update-ReleaseButtonText })
$ui["ChkStable"].Add_Unchecked({ Update-ReleaseButtonText })

# ============================================================================
# Section 12: Log tab
# ============================================================================

function Get-LogFilterText {
    if ($null -eq $ui["TxtLogFilter"]) { return "" }
    $t = $ui["TxtLogFilter"].Text
    if ($t -eq "Filter..." -or $t -eq "") { return "" }
    return $t
}

function Test-LogLineMatchesFilter([string]$line) {
    $f = Get-LogFilterText
    if ($f -eq "") { return $true }
    return $line -match [regex]::Escape($f)
}

function Get-ClassifiedLogColor($cls) {
    # Light-theme palette (S477): chosen for legibility on white #FFFFFF
    # Log surface. Same semantics as the dark-theme classification, just
    # darker / higher-contrast values.
    switch ($cls) {
        "error"   { return "#B81818" }
        "warning" { return "#B86810" }
        "info"    { return "#0078A8" }
        default   { return "#1A2434" }
    }
}

# Session / step banners: also append to AllOutput so Copy Log, Export, and filter apply.
function Add-LogSessionLine($text, $color) {
    [void]$script:AllOutput.Add($text)
    Add-LogLine $text $color
}

function Add-LogLine($text, $color) {
    Write-DevWindowConsoleLog $text
    if ($null -eq $ui["LogOutput"]) { return }
    if (-not (Test-LogLineMatchesFilter $text)) { return }
    $doc = $ui["LogOutput"].Document
    $para = $doc.Blocks.LastBlock
    if ($null -eq $para) { $para = New-Object System.Windows.Documents.Paragraph; $doc.Blocks.Add($para) }
    $run = New-Object System.Windows.Documents.Run($text + "`n")
    $run.Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($color)))
    $para.Inlines.Add($run)
    if ($ui["ChkAutoScroll"].IsChecked) { $ui["LogOutput"].ScrollToEnd() }
}

function Rebuild-LogView {
    if ($null -eq $ui["LogOutput"]) { return }
    $doc = $ui["LogOutput"].Document
    $doc.Blocks.Clear()
    $para = New-Object System.Windows.Documents.Paragraph
    $doc.Blocks.Add($para)
    foreach ($line in $script:AllOutput) {
        if (-not (Test-LogLineMatchesFilter $line)) { continue }
        $cls = Classify-Line $line
        $color = Get-ClassifiedLogColor $cls
        $run = New-Object System.Windows.Documents.Run($line + "`n")
        $run.Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($color)))
        $para.Inlines.Add($run)
    }
    if ($ui["ChkAutoScroll"].IsChecked) { $ui["LogOutput"].ScrollToEnd() }
}

function Add-LogLines {
    Rebuild-LogView
}

$ui["BtnLogClear"].Add_Click({
    try {
        [void]$script:AllOutput.Clear()
        $ui["LogOutput"].Document.Blocks.Clear()
        $ui["LogOutput"].Document.Blocks.Add((New-Object System.Windows.Documents.Paragraph))
    } catch {}
})

$ui["BtnLogExport"].Add_Click({
    try {
        $dlg = New-Object Microsoft.Win32.SaveFileDialog
        $dlg.Title = "Export build log"
        $dlg.Filter = "Log files (*.log;*.txt)|*.log;*.txt|All files (*.*)|*.*"
        $dlg.FileName = "dev-window-log-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".txt"
        $dlg.DefaultExt = ".txt"
        if ($true -eq $dlg.ShowDialog()) {
            $body = ($script:AllOutput | ForEach-Object { $_ }) -join "`r`n"
            [System.IO.File]::WriteAllText($dlg.FileName, $body, [System.Text.UTF8Encoding]::new($false))
        }
    } catch {
        [System.Windows.MessageBox]::Show("Export failed: " + $_.Exception.Message, "Export", "OK", "Error") | Out-Null
    }
})

$ui["TxtLogFilter"].Add_TextChanged({ try { Rebuild-LogView } catch {} })

# Filter placeholder behavior
$ui["TxtLogFilter"].Add_GotFocus({
    if ($ui["TxtLogFilter"].Text -eq "Filter...") {
        $ui["TxtLogFilter"].Text = ""
        $ui["TxtLogFilter"].FontStyle = [System.Windows.FontStyles]::Normal
    }
})
$ui["TxtLogFilter"].Add_LostFocus({
    if ($ui["TxtLogFilter"].Text -eq "") {
        $ui["TxtLogFilter"].Text = "Filter..."
        $ui["TxtLogFilter"].FontStyle = [System.Windows.FontStyles]::Italic
    }
})

# ============================================================================
# Section 13: Docs tab
# ============================================================================

function Populate-DocList {
    # Perf: scan disk on the background pool, populate ListBox on UI thread in
    # one pass. Old code did Get-ChildItem -Recurse over context/docs (hundreds
    # of files) on the UI thread during Loaded, blocking window paint for
    # 200-1000ms on cold disk.
    if ($null -eq $ui["DocList"]) { return }
    if ($script:DocListLoadBusy) { return }
    $script:DocListLoadBusy = $true

    Start-AsyncPoolAction `
        -Script {
            param($root)
            $results = New-Object System.Collections.ArrayList
            $folders = @("docs", "context")
            foreach ($folder in $folders) {
                $fp = Join-Path $root $folder
                if (Test-Path $fp) {
                    Get-ChildItem -Path $fp -Include "*.md","*.txt" -Recurse -File -ErrorAction SilentlyContinue |
                        Sort-Object FullName | ForEach-Object {
                        $rel = $_.FullName.Substring($root.Length).TrimStart('\', '/')
                        [void]$results.Add([PSCustomObject]@{ Rel = $rel; Full = $_.FullName })
                    }
                }
            }
            Get-ChildItem -Path $root -Filter "*.md" -File -ErrorAction SilentlyContinue |
                Sort-Object Name | ForEach-Object {
                [void]$results.Add([PSCustomObject]@{ Rel = $_.Name; Full = $_.FullName })
            }
            return ,$results
        } `
        -Arguments @($script:ProjectRoot) `
        -OnComplete {
            # Plain scriptblock (NO GetNewClosure) so $script: refs go to the main module.
            # The earlier GetNewClosure'd tick handler had a SEPARATE $script: scope, so
            # writes to $script:DocListLoadBusy never propagated -- the busy guard would
            # latch true after the first call and the doc list would never refresh.
            param($result)
            try {
                # EndInvoke wraps single returns in a collection. Unwrap carefully.
                $items = @()
                if ($null -ne $result) {
                    foreach ($r in @($result)) {
                        if ($null -eq $r) { continue }
                        if ($r -is [System.Collections.IEnumerable] -and -not ($r -is [string]) -and -not ($r -is [psobject])) {
                            foreach ($x in $r) { if ($null -ne $x) { $items += $x } }
                        } else {
                            $items += $r
                        }
                    }
                }
                $script:DocFileMap = @{}
                $ui["DocList"].Items.Clear()
                foreach ($item in $items) {
                    if ($null -eq $item -or $null -eq $item.Rel) { continue }
                    if (-not $script:DocFileMap.ContainsKey($item.Rel)) {
                        [void]$ui["DocList"].Items.Add($item.Rel)
                        $script:DocFileMap[$item.Rel] = $item.Full
                    }
                }
            } catch {}
            $script:DocListLoadBusy = $false
        }
}

$ui["DocList"].Add_SelectionChanged({
    try {
        $key = $ui["DocList"].SelectedItem
        if ($null -ne $key -and $script:DocFileMap.ContainsKey($key)) {
            $ui["DocContent"].Text = (Get-Content -Path $script:DocFileMap[$key] -Raw -Encoding UTF8 -ErrorAction Stop)
        }
    } catch {}
})

# ============================================================================
# Section 14: Build pipeline
# ============================================================================

function Copy-AddinFiles {
    # NON-DESTRUCTIVE addin deploy. Per Mike's BYOR (bring-your-own-ROM)
    # distribution model: post-batch-addin/data holds the dev ROM + any
    # author-bundled runtime files. We need it copied INTO the build dir
    # so the client can find it, but we MUST NOT mirror-delete extras.
    # Build/data/mods/ holds runtime-extracted UI textures populated by
    # pdguiThemeExtractRomTextures() at first launch from the user's
    # ROM. Mirroring would wipe those extracts every build, forcing a
    # fresh extraction every launch. Switch to /E (preserve extras) +
    # non-destructive Copy-Item fallback.
    #
    # B-326 (2026-05-03): ROM file (*.z64) lives at install root, not
    # Build/data/. Post B-321 the binary's fsFileLoad searches at $E
    # (the EXE directory aka install root) with DEFAULT_BASEDIR_NAME=".",
    # so any *.z64 under Build/data/ is dead bytes the client never reads.
    # Sweep *.z64 anywhere in the addin tree and place them at $BuildDir
    # next to PerfectDark.exe; exclude them from the data/ copy below so
    # they do not duplicate.
    $parentDir = Split-Path $script:ProjectRoot -Parent
    $srcData = Join-Path $parentDir "post-batch-addin" | Join-Path -ChildPath "data"
    $dstData = Join-Path $script:BuildDir "data"
    if (-not (Test-Path $srcData)) { return }
    if (-not (Test-Path $dstData)) { New-Item -ItemType Directory -Path $dstData -Force | Out-Null }

    # B-326: ROM file(s) go to install root. Find any *.z64 anywhere in
    # the addin source tree and copy them up to $BuildDir, which is where
    # the binary's fsFileLoad searches. Done before the data copy so even
    # if that step fails the ROM still lands in the right place.
    try {
        Get-ChildItem -Path $srcData -Filter "*.z64" -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
            $destRom = Join-Path $script:BuildDir $_.Name
            try { Copy-Item -Path $_.FullName -Destination $destRom -Force -ErrorAction Stop } catch {}
        }
    } catch {}

    try {
        $robocopy = Get-Command robocopy.exe -ErrorAction SilentlyContinue
        $dataMirrorOk = $false
        if ($null -ne $robocopy) {
            $robocopyPath = if ($robocopy.Path) { $robocopy.Path } elseif ($robocopy.Source) { $robocopy.Source } else { "robocopy.exe" }
            # /E copies subdirs incl. empty (non-destructive); /XO skips
            # files that already exist at dest with same/newer timestamp.
            # /XF "*.z64" excludes ROM files (handled above for install root).
            & $robocopyPath $srcData $dstData /E /XO /XF "*.z64" /NFL /NDL /NJH /NJS /NP | Out-Null
            if ($LASTEXITCODE -le 7) { $dataMirrorOk = $true }
        }
        if (-not $dataMirrorOk) {
            # Fallback (no robocopy): file-by-file copy that overwrites but
            # does NOT delete extras at the destination. Skip *.z64 (handled above).
            Get-ChildItem -Path $srcData -Recurse -File -ErrorAction Stop | Where-Object { $_.Extension -ne ".z64" } | ForEach-Object {
                $rel = $_.FullName.Substring($srcData.Length).TrimStart('\','/')
                $dest = Join-Path $dstData $rel
                $destDir = Split-Path $dest -Parent
                if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }
                Copy-Item -Path $_.FullName -Destination $dest -Force -ErrorAction SilentlyContinue
            }
        }
        $hoistByor = Join-Path $dstData "put_your_rom_here.txt"
        $rootByor = Join-Path $script:BuildDir "put_your_rom_here.txt"
        if (Test-Path -LiteralPath $hoistByor) {
            try { Move-Item -LiteralPath $hoistByor -Destination $rootByor -Force -ErrorAction Stop } catch {}
        }
    } catch {}
}

function Get-GitCurrentBranch {
    param([string]$GitExe)
    if (-not $GitExe) { $GitExe = Resolve-GitExecutable }
    if (-not $GitExe) { $GitExe = "git" }
    try {
        $b = & $GitExe -C $script:ProjectRoot rev-parse --abbrev-ref HEAD 2>$null
        if ($b) { return $b.Trim() }
    } catch {}
    return "HEAD"
}

# Solo-dev workflow: commit + push any dirty tree before Build / Release so subprocesses (e.g.
# release.ps1 git pull --rebase) never hit "index contains uncommitted changes".
#
# Async pattern: all git/WSL/rm subprocess calls run in a BgPool runspace; the UI thread stays
# fully interactive while add/commit/push execute. Completion invokes $OnComplete($ok) on the
# UI thread. Callers must set their own guard flags (IsBuilding, etc.) before calling this
# because the function returns immediately.
function Start-GitSyncBeforeBuild {
    param(
        [string]$CommitMessage = "Tooling - c120: Commit pre-build Dev Window sync",
        [string]$ActionLabel = "sync before build",
        [bool]$RequirePush = $false,
        [Parameter(Mandatory=$true)][scriptblock]$OnComplete
    )
    # Stash the caller's callback in a $script: var instead of a local + GetNewClosure().
    # GetNewClosure() would put the inner OnComplete scriptblock in a SEPARATE $script:
    # scope, so its reads/writes of $script:GitSyncBusy etc. would not reach the main
    # module. Guarded below by the GitSyncBusy flag so only one sync is pending at a time.
    $gitExe = Resolve-GitExecutable
    if (-not $gitExe) {
        [System.Windows.MessageBox]::Show("git was not found on PATH.", "Git", "OK", "Warning") | Out-Null
        & $OnComplete $false
        return
    }
    if ($script:GitSyncBusy) {
        Add-LogSessionLine "git sync already running; ignoring duplicate request." "#A07810"
        & $OnComplete $false
        return
    }
    $script:GitSyncBusy = $true
    $script:PendingGitSyncCallback = $OnComplete
    Clear-StaleGitIndexLock $script:ProjectRoot

    # UI-thread work is fast path-checks only. The slow external-process calls (wslpath
    # cold-start in particular can be 10+ seconds) happen inside the runspace so the
    # button-click-to-UI-update latency stays sub-second.
    $root = $script:ProjectRoot
    $winLock = Join-Path $root ".git\index.lock"
    $rmExe = Get-MsysToolPath -ToolName "rm" -GitExe $gitExe
    $cygpathExe = Get-MsysToolPath -ToolName "cygpath" -GitExe $gitExe
    $wslCmd = Get-Command wsl.exe -ErrorAction SilentlyContinue
    $wslExe = if ($null -ne $wslCmd) { $wslCmd.Source } else { $null }
    $stateSyncScript = Join-Path (Join-Path $script:ProjectRoot "devtools") "project-state-sync.ps1"

    Add-LogSessionLine "" "#C0C8D2"
    Add-LogSessionLine (">>> git: " + $ActionLabel) "#0078A8"

    Start-AsyncPoolAction `
        -Script {
            param($root, $gitExe, $commitMessage, $actionLabel, $winLock, $rmExe, $cygpathExe, $wslExe, $requirePush, $stateSyncScript)

            $logs = New-Object System.Collections.ArrayList
            if (Test-Path -LiteralPath $stateSyncScript) {
                try { . $stateSyncScript } catch {}
            }
            if (Get-Command Sync-ProjectMindState -ErrorAction SilentlyContinue) {
                $syncResult = Sync-ProjectMindState -ProjectRoot $root -Quiet
                if ($syncResult.MemoryCopied) {
                    [void]$logs.Add(@{ Text = "state sync: mirrored Codex memory to tools/kanban/memories.md"; Color = "#0078A8" })
                }
                foreach ($note in $syncResult.Notes) {
                    [void]$logs.Add(@{ Text = "state sync: $note"; Color = "#A07810" })
                }
            }
            $commitBody = "Dev Window staged pending changes during $actionLabel so code, Kanban board state, Codex memory, and release-note state can move together."
            $commitRefs = "Refs: c120"

            $branch = "HEAD"
            try {
                $b = & $gitExe -C $root rev-parse --abbrev-ref HEAD 2>$null
                if ($b) { $branch = $b.Trim() }
            } catch {}

            $msysLock = $null
            if ($cygpathExe) {
                try {
                    $p = & $cygpathExe -au $root 2>$null
                    if ($p) {
                        $p = $p.Trim().TrimEnd('/')
                        if ($p) { $msysLock = $p + '/.git/index.lock' }
                    }
                } catch {}
            }

            $wslLock = $null
            if ($wslExe) {
                try {
                    $w = & $wslExe wslpath -a $root 2>$null
                    if ($w) {
                        $w = $w.Trim().TrimEnd('/')
                        if ($w) { $wslLock = $w + '/.git/index.lock' }
                    }
                } catch {}
            }

            [void]$logs.Add(@{ Text = "branch: $branch  |  git: $gitExe"; Color = "#44586C" })

            $cleanup = {
                if (Test-Path -LiteralPath $winLock) {
                    try { & cmd.exe /c "attrib -R `"$winLock`"" 2>$null | Out-Null } catch {}
                    try { Remove-Item -LiteralPath $winLock -Force -ErrorAction SilentlyContinue } catch {}
                    try { & cmd.exe /c "del /f /q `"$winLock`"" 2>$null | Out-Null } catch {}
                }
                if ($rmExe -and $msysLock) {
                    try { [void](& $rmExe -f -- $msysLock 2>&1) } catch {}
                }
                if ($wslExe -and $wslLock) {
                    try { [void](& $wslExe -- rm -f -- $wslLock 2>&1) } catch {}
                }
                if ($rmExe -and $wslLock) {
                    try { [void](& $rmExe -f -- $wslLock 2>&1) } catch {}
                }
            }

            & $cleanup
            # Brief pause so IDE git (Cursor/VS Code) can finish an in-flight index lock.
            Start-Sleep -Milliseconds 400

            $addBackoffMs = @(300, 700, 1200, 2000, 2800, 3500)
            $addOut = @(); $addCode = 1
            for ($attempt = 1; $attempt -le $addBackoffMs.Count; $attempt++) {
                $addOut = @(& $gitExe -C $root add -A 2>&1)
                $addCode = $LASTEXITCODE
                if ($addCode -eq 0) { break }
                $addText = ($addOut | Out-String)
                if ($addText -match 'index\.lock|Unable to create|Another git process') {
                    [void]$logs.Add(@{ Text = "git add: index.lock / concurrent git (attempt $attempt/$($addBackoffMs.Count)), cleanup + backoff..."; Color = "#A07810" })
                    & $cleanup
                    if (Test-Path -LiteralPath $winLock) {
                        $wUntil = [DateTime]::UtcNow.AddMilliseconds(2200)
                        while ((Test-Path -LiteralPath $winLock) -and [DateTime]::UtcNow -lt $wUntil) {
                            Start-Sleep -Milliseconds 200
                        }
                    }
                    Start-Sleep -Milliseconds $addBackoffMs[$attempt - 1]
                    continue
                }
                break
            }
            if ($addCode -ne 0) {
                foreach ($line in $addOut) { [void]$logs.Add(@{ Text = "$line"; Color = "#B81818" }) }
                $addFailText = if ($requirePush) { "git add failed before push." } else { "git add failed before build." }
                [void]$logs.Add(@{ Text = $addFailText; Color = "#B81818" })
                $hint = "git add failed (index.lock). Close other git users of this repo: Cursor/VS Code Source Control, terminals, then retry. Prefer MSYS2 MinGW git (C:\msys64\mingw64\bin\git.exe) over usr\bin\git.exe."
                return [PSCustomObject]@{ Ok = $false; Logs = $logs; ErrMsg = $hint }
            }

            & $gitExe -C $root diff --cached --quiet 2>$null
            $needCommit = ($LASTEXITCODE -ne 0)
            if ($needCommit) {
                & $cleanup
                if (Get-Command Get-ProjectStateCommitBody -ErrorAction SilentlyContinue) {
                    $commitBody = Get-ProjectStateCommitBody -ProjectRoot $root -ActionLabel $actionLabel
                }
                $co = @(& $gitExe -C $root commit -m $commitMessage -m $commitBody -m $commitRefs 2>&1)
                $commitCode = $LASTEXITCODE
                if ($commitCode -ne 0 -and (($co | ForEach-Object { "$_" }) -join "`n") -match 'index\.lock|Unable to create') {
                    & $cleanup
                    $co = @(& $gitExe -C $root commit -m $commitMessage -m $commitBody -m $commitRefs 2>&1)
                    $commitCode = $LASTEXITCODE
                }
                if ($commitCode -ne 0) {
                    [void]$logs.Add(@{ Text = "git commit failed; hooks were not bypassed. Fix the hook or repo state, then retry."; Color = "#B81818" })
                }
                foreach ($line in $co) {
                    $cl = if ($commitCode -ne 0) { "#B81818" } else { "#4A5868" }
                    [void]$logs.Add(@{ Text = "$line"; Color = $cl })
                }
                if ($commitCode -ne 0) {
                    [void]$logs.Add(@{ Text = "git commit failed (hooks, conflicts, or repo state)."; Color = "#B81818" })
                    $commitHint = if ($requirePush) { "git commit failed. Fix the repo, then retry." } else { "git commit failed before build. Fix the repo, then retry." }
                    return [PSCustomObject]@{ Ok = $false; Logs = $logs; ErrMsg = $commitHint }
                }
                [void]$logs.Add(@{ Text = "Committed: $commitMessage"; Color = "#0078A8" })
            } else {
                [void]$logs.Add(@{ Text = "(working tree already clean - nothing to commit)"; Color = "#44586C" })
            }

            $pushBackoffMs = @(400, 900, 1800, 2800)
            $maxPushAttempts = 5
            $pu = @(); $pushCode = 1
            for ($pAttempt = 1; $pAttempt -le $maxPushAttempts; $pAttempt++) {
                $pu = @(& $gitExe -C $root push origin $branch 2>&1)
                $pushCode = $LASTEXITCODE
                if ($pushCode -eq 0) { break }
                $pushText = ($pu | Out-String)
                if ($pushText -match 'index\.lock|Unable to create|Another git process') {
                    [void]$logs.Add(@{ Text = "git push: index.lock / concurrent git (attempt $pAttempt/$maxPushAttempts), cleanup + backoff..."; Color = "#A07810" })
                    & $cleanup
                    if ($pAttempt -lt $maxPushAttempts) {
                        $si = [math]::Min($pAttempt - 1, $pushBackoffMs.Count - 1)
                        Start-Sleep -Milliseconds $pushBackoffMs[$si]
                    }
                    continue
                }
                break
            }
            foreach ($line in $pu) {
                $cl = if ($pushCode -ne 0) { "#A07810" } else { "#4A5868" }
                [void]$logs.Add(@{ Text = "$line"; Color = $cl })
            }
            if ($pushCode -ne 0) {
                if ($requirePush) {
                    [void]$logs.Add(@{ Text = "git push failed (exit $pushCode)."; Color = "#B81818" })
                    return [PSCustomObject]@{ Ok = $false; Logs = $logs; ErrMsg = "git push failed. See the Log tab for details." }
                } else {
                    # Audit 2026-04-16: push failure must NOT abort the build. v1 treats
                    # push as a best-effort step; log and continue.
                    [void]$logs.Add(@{ Text = "git push failed (exit $pushCode) - continuing build without pushing."; Color = "#A07810" })
                }
            } else {
                [void]$logs.Add(@{ Text = "git push: ok"; Color = "#0078A8" })
            }

            return [PSCustomObject]@{ Ok = $true; Logs = $logs; ErrMsg = $null }
        } `
        -Arguments @($root, $gitExe, $CommitMessage, $ActionLabel, $winLock, $rmExe, $cygpathExe, $wslExe, $RequirePush, $stateSyncScript) `
        -OnComplete {
            # Plain scriptblock (NO GetNewClosure). It retains the main module's
            # $script: scope, so writes to $script:GitSyncBusy here actually clear the
            # flag the next Start-GitSyncBeforeBuild call checks.
            param($result)
            $ok = $false
            try {
                $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
                if ($null -ne $r) {
                    foreach ($entry in $r.Logs) {
                        if ($null -ne $entry) {
                            try { Add-LogSessionLine $entry.Text $entry.Color } catch {}
                        }
                    }
                    $ok = $r.Ok
                    if (-not $ok -and $r.ErrMsg) {
                        [System.Windows.MessageBox]::Show($r.ErrMsg, "Git", "OK", "Error") | Out-Null
                    }
                }
            } catch {}
            Add-LogSessionLine "" "#C0C8D2"
            $script:GitSyncBusy = $false
            $cb = $script:PendingGitSyncCallback
            $script:PendingGitSyncCallback = $null
            if ($null -ne $cb) {
                try { & $cb $ok } catch {
                    try { Add-LogSessionLine ("git sync callback threw: " + $_.Exception.Message) "#B81818" } catch {}
                }
            }
        }
}

# Runs a script block on BgPool; invokes $OnComplete on the UI thread with the
# script's return value. Perf: keeps the UI thread responsive for on-demand
# actions (pull/push/prune/check) that previously ran synchronously and could
# block for seconds (or longer, on network hiccups).
#
# CRITICAL #1: the DispatcherTimer Tick scriptblock MUST use GetNewClosure() to bind
# $handle/$ps/$OnComplete. PowerShell 5.1 scriptblocks registered with .Add_Tick()
# do NOT capture the enclosing function's local variables by default -- when the
# tick fires, those variables are $null, so `-not $handle.IsCompleted` is `-not $null`
# = `$true`, the handler returns early forever, and OnComplete is never invoked.
# This was silently broken before -- async git/doc operations never completed,
# leaving the UI in a permanent "busy" state (buttons stuck disabled).
#
# CRITICAL #2: GetNewClosure() ALSO creates a SEPARATE $script: scope for the closure.
# Inside a GetNewClosure'd scriptblock, `$script:Foo` reads start as $null and writes
# do NOT propagate to the main module. So this tick body is intentionally limited to
# *local* variables ($handle, $ps, $OnComplete) -- it never touches $script: state.
# All state mutation lives inside the caller-supplied $OnComplete, which is invoked
# as a plain (non-closure) scriptblock, so its $script: refs resolve in the main
# module as normal. NEVER add `$script:Foo = ...` writes inside this tick handler.
# Pass plain scriptblocks (no .GetNewClosure()) when calling Start-AsyncPoolAction.
function Start-AsyncPoolAction {
    param(
        [Parameter(Mandatory=$true)][scriptblock]$Script,
        [object[]]$Arguments,
        [Parameter(Mandatory=$true)][scriptblock]$OnComplete
    )
    $ps = [System.Management.Automation.PowerShell]::Create()
    $ps.RunspacePool = $script:BgPool
    [void]$ps.AddScript($Script)
    if ($Arguments) {
        foreach ($a in $Arguments) { [void]$ps.AddArgument($a) }
    }
    $handle = $ps.BeginInvoke()
    $t = New-Object System.Windows.Threading.DispatcherTimer
    $t.Interval = [TimeSpan]::FromMilliseconds(150)
    $t.Add_Tick({
        if (-not $handle.IsCompleted) { return }
        $this.Stop()
        $result = $null
        try { $result = $ps.EndInvoke($handle) } catch { $result = $null }
        try { $ps.Dispose() } catch {}
        try { & $OnComplete $result } catch {}
    }.GetNewClosure())
    $t.Start()
}

function Show-GitPullCommitDialog {
    param(
        [object[]]$Commits,
        [string]$Branch
    )

    if (-not $Commits -or $Commits.Count -eq 0) { return $null }

    $form = New-Object System.Windows.Forms.Form
    $form.Text = "Choose commit to pull"
    $form.Width = 980
    $form.Height = 560
    $form.StartPosition = "CenterScreen"
    $form.MinimizeBox = $false
    $form.MaximizeBox = $false
    $form.ShowIcon = $false

    $label = New-Object System.Windows.Forms.Label
    $label.Text = "Select the newest remote commit you want to fast-forward to on $Branch."
    $label.AutoSize = $true
    $label.Left = 12
    $label.Top = 12
    $form.Controls.Add($label)

    $list = New-Object System.Windows.Forms.ListBox
    $list.Left = 12
    $list.Top = 38
    $list.Width = 940
    $list.Height = 430
    $list.DisplayMember = "Label"
    foreach ($commit in $Commits) { [void]$list.Items.Add($commit) }
    if ($list.Items.Count -gt 0) { $list.SelectedIndex = $list.Items.Count - 1 }
    $form.Controls.Add($list)

    $ok = New-Object System.Windows.Forms.Button
    $ok.Text = "Pull Selected"
    $ok.Width = 120
    $ok.Height = 32
    $ok.Left = 702
    $ok.Top = 480
    $ok.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.AcceptButton = $ok
    $form.Controls.Add($ok)

    $cancel = New-Object System.Windows.Forms.Button
    $cancel.Text = "Cancel"
    $cancel.Width = 100
    $cancel.Height = 32
    $cancel.Left = 836
    $cancel.Top = 480
    $cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.CancelButton = $cancel
    $form.Controls.Add($cancel)

    $result = $form.ShowDialog()
    if ($result -ne [System.Windows.Forms.DialogResult]::OK -or $null -eq $list.SelectedItem) {
        return $null
    }
    return $list.SelectedItem
}

function Finish-GitPullAction {
    $script:GitActionBusy = $false
    $script:GitActionLabel = ""
    $ui["BtnPull"].IsEnabled = $true
    $ui["BtnPush"].IsEnabled = $true
    Refresh-VersionDisplay
    Update-StatusBar
}

function Start-GitPullMerge {
    param(
        [string]$GitExe,
        [object]$Commit
    )

    if ($null -eq $Commit) {
        Add-LogLine "Pull cancelled." "#44586C"
        Finish-GitPullAction
        return
    }

    $sha = [string]$Commit.Sha
    $short = [string]$Commit.Short
    Add-LogLine (">>> git merge --ff-only " + $short) "#0078A8"

    Start-AsyncPoolAction `
        -Script {
            param($root, $gitExe, $sha)
            try {
                $out = & $gitExe -C $root merge --ff-only $sha 2>&1
                [PSCustomObject]@{ Code = $LASTEXITCODE; Out = @($out | ForEach-Object { "$_" }) }
            } catch {
                [PSCustomObject]@{ Code = -1; Out = @($_.Exception.Message) }
            }
        } `
        -Arguments @($script:ProjectRoot, $GitExe, $sha) `
        -OnComplete {
            param($result)
            try {
                $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
                if ($null -ne $r) {
                    $code = [int]$r.Code
                    foreach ($line in $r.Out) {
                        $cl = if ($code -ne 0) { "#B81818" } else { "#4A5868" }
                        Add-LogLine ("$line".TrimEnd("`r")) $cl
                    }
                    if ($code -eq 0) {
                        [System.Windows.MessageBox]::Show("Pulled through selected commit.", "Git Pull", "OK", "Information") | Out-Null
                    } else {
                        [System.Windows.MessageBox]::Show("Pull to selected commit failed with exit code $code.`nSee Log tab for details.", "Git Pull", "OK", "Warning") | Out-Null
                    }
                }
            } catch {}
            Finish-GitPullAction
        }
}

function Invoke-GitPull {
    if ($script:IsBuilding -or $script:IsPushing) {
        [System.Windows.MessageBox]::Show("Wait for the current build or release to finish.", "Git Pull", "OK", "Information") | Out-Null
        return
    }
    if ($script:GitActionBusy) {
        [System.Windows.MessageBox]::Show("Another git action is in progress.", "Git Pull", "OK", "Information") | Out-Null
        return
    }
    $gitExe = Resolve-GitExecutable
    if (-not $gitExe) {
        [System.Windows.MessageBox]::Show("git was not found on PATH.", "Git Pull", "OK", "Warning") | Out-Null
        return
    }
    $script:GitActionBusy = $true
    $script:GitActionLabel = "pulling..."
    $ui["BtnPull"].IsEnabled = $false
    $ui["BtnPush"].IsEnabled = $false
    $br = Get-GitCurrentBranch -GitExe $gitExe
    Add-LogLine ">>> git fetch for selectable pull (branch: $br)" "#0078A8"
    Clear-StaleGitIndexLock $script:ProjectRoot

    Start-AsyncPoolAction `
        -Script {
            param($root, $gitExe)
            try {
                $logs = New-Object System.Collections.ArrayList
                $branch = (& $gitExe -C $root rev-parse --abbrev-ref HEAD 2>$null).Trim()
                $upstream = (& $gitExe -C $root rev-parse --abbrev-ref --symbolic-full-name "@{u}" 2>$null)
                if (-not $upstream) { $upstream = "origin/$branch" }
                $upstream = $upstream.Trim()

                $fetchOut = & $gitExe -C $root fetch origin $branch 2>&1
                $fetchCode = $LASTEXITCODE
                foreach ($line in $fetchOut) { [void]$logs.Add("$line") }
                if ($fetchCode -ne 0) {
                    return [PSCustomObject]@{ Code = $fetchCode; Out = @($logs); Branch = $branch; Upstream = $upstream; Commits = @() }
                }

                $raw = @(& $gitExe -C $root log --date=short --pretty=format:"%H%x09%h%x09%ad%x09%s" "HEAD..$upstream" 2>&1)
                $logCode = $LASTEXITCODE
                if ($logCode -ne 0) {
                    foreach ($line in $raw) { [void]$logs.Add("$line") }
                    return [PSCustomObject]@{ Code = $logCode; Out = @($logs); Branch = $branch; Upstream = $upstream; Commits = @() }
                }

                $commits = New-Object System.Collections.ArrayList
                foreach ($line in $raw) {
                    $parts = "$line" -split "`t", 4
                    if ($parts.Count -ge 4) {
                        $label = "{0}  {1}  {2}" -f $parts[1], $parts[2], $parts[3]
                        [void]$commits.Add([PSCustomObject]@{
                            Sha = $parts[0]
                            Short = $parts[1]
                            Date = $parts[2]
                            Subject = $parts[3]
                            Label = $label
                        })
                    }
                }

                [PSCustomObject]@{ Code = 0; Out = @($logs); Branch = $branch; Upstream = $upstream; Commits = @($commits) }
            } catch {
                [PSCustomObject]@{ Code = -1; Out = @($_.Exception.Message); Branch = ""; Upstream = ""; Commits = @() }
            }
        } `
        -Arguments @($script:ProjectRoot, $gitExe) `
        -OnComplete {
            param($result)
            try {
                $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
                if ($null -ne $r) {
                    $code = [int]$r.Code
                    foreach ($line in $r.Out) {
                        $cl = if ($code -ne 0) { "#B81818" } else { "#4A5868" }
                        Add-LogLine ("$line".TrimEnd("`r")) $cl
                    }
                    if ($code -eq 0 -and $r.Commits -and $r.Commits.Count -gt 0) {
                        Add-LogLine ("remote commits available: " + $r.Commits.Count + " from " + $r.Upstream) "#44586C"
                        $selected = Show-GitPullCommitDialog -Commits $r.Commits -Branch $r.Branch
                        Start-GitPullMerge -GitExe $gitExe -Commit $selected
                        return
                    } elseif ($code -eq 0) {
                        [System.Windows.MessageBox]::Show("Already up to date.", "Git Pull", "OK", "Information") | Out-Null
                    } else {
                        [System.Windows.MessageBox]::Show("Fetch failed with exit code $code.`nSee Log tab for details.", "Git Pull", "OK", "Warning") | Out-Null
                    }
                }
            } catch {}
            Finish-GitPullAction
        }
}

function Invoke-GitPush {
    if ($script:IsBuilding -or $script:IsPushing) {
        [System.Windows.MessageBox]::Show("Wait for the current build or release to finish.", "Git Push", "OK", "Information") | Out-Null
        return
    }
    if ($script:GitActionBusy) {
        [System.Windows.MessageBox]::Show("Another git action is in progress.", "Git Push", "OK", "Information") | Out-Null
        return
    }
    $gitExe = Resolve-GitExecutable
    if (-not $gitExe) {
        [System.Windows.MessageBox]::Show("git was not found on PATH.", "Git Push", "OK", "Warning") | Out-Null
        return
    }
    $script:GitActionBusy = $true
    $script:GitActionLabel = "committing + pushing..."
    $ui["BtnPush"].IsEnabled = $false
    $ui["BtnPull"].IsEnabled = $false
    $br = Get-GitCurrentBranch -GitExe $gitExe
    Add-LogLine ">>> git commit + push (branch: $br)" "#0078A8"

    Start-GitSyncBeforeBuild -CommitMessage "Tooling - c120: Sync live project state before push" -ActionLabel "commit + push" -RequirePush $true -OnComplete {
            param($ok)
            $script:GitActionBusy = $false
            $script:GitActionLabel = ""
            $ui["BtnPush"].IsEnabled = $true
            $ui["BtnPull"].IsEnabled = $true
            Refresh-VersionDisplay
            Update-RunButtons
            Update-StatusBar
            if ($ok) {
                [System.Windows.MessageBox]::Show("Commit + push completed successfully.", "Git Push", "OK", "Information") | Out-Null
            } else {
                Add-LogLine "Commit + push did not complete. See details above." "#B81818"
            }
        }
}

function Format-WorktreeByteSize {
    param([long]$Bytes)
    if ($Bytes -lt 0) { return "-" }
    if ($Bytes -lt 1024) { return ("{0} B" -f $Bytes) }
    if ($Bytes -lt 1048576) { return ("{0:N1} KB" -f ($Bytes / 1024.0)) }
    if ($Bytes -lt 1073741824) { return ("{0:N1} MB" -f ($Bytes / 1048576.0)) }
    return ("{0:N2} GB" -f ($Bytes / 1073741824.0))
}

function Get-WorktreeDirectorySize {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return [long]0 }
    $total = [long]0
    try {
        $stack = New-Object System.Collections.Generic.Stack[string]
        $stack.Push($Path)
        while ($stack.Count -gt 0) {
            $dir = $stack.Pop()
            try {
                $di = [System.IO.DirectoryInfo]::new($dir)
                foreach ($f in $di.EnumerateFiles()) {
                    try { $total += $f.Length } catch {}
                }
                foreach ($d in $di.EnumerateDirectories()) {
                    if (($d.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
                        $stack.Push($d.FullName)
                    }
                }
            } catch {}
        }
    } catch {}
    return $total
}

function Get-WorktreeEntries {
    param(
        [Parameter(Mandatory=$true)][string]$GitExe,
        [Parameter(Mandatory=$true)][string]$Root,
        [string]$CurrentPath
    )

    $worktreesDir = Join-Path $Root ".claude\worktrees"
    $regMap = @{}
    $stalePrunable = New-Object System.Collections.ArrayList
    $regOutRaw = ""
    try {
        $regOutRaw = (& $GitExe -C $Root worktree list --porcelain 2>$null | Out-String)
    } catch { $regOutRaw = "" }

    if ($regOutRaw) {
        $blocks = $regOutRaw -split "(`r?`n){2,}"
        foreach ($block in $blocks) {
            $path = ""
            $branch = ""
            $prunable = $false
            foreach ($line in ($block -split "`r?`n")) {
                if ($line -match '^worktree\s+(.+)$') { $path = $matches[1].Trim() }
                elseif ($line -match '^branch\s+refs/heads/(.+)$') { $branch = $matches[1].Trim() }
                elseif ($line -match '^detached') { $branch = "(detached)" }
                elseif ($line -match '^prunable') { $prunable = $true }
            }
            if ($path) {
                $norm = $path -replace '/', '\'
                try {
                    $rp = Resolve-Path -LiteralPath $norm -ErrorAction SilentlyContinue
                    if ($rp) { $norm = $rp.Path }
                } catch {}
                $key = $norm.ToLower().TrimEnd('\')
                $regMap[$key] = [PSCustomObject]@{ Branch = $branch; Prunable = $prunable; Path = $norm }
                if ($prunable) {
                    [void]$stalePrunable.Add($norm)
                }
            }
        }
    }

    $currentNorm = ""
    if ($CurrentPath) {
        try {
            $rp = Resolve-Path -LiteralPath $CurrentPath -ErrorAction SilentlyContinue
            if ($rp) { $currentNorm = $rp.Path.ToLower().TrimEnd('\') }
        } catch {}
    }

    $entries = New-Object System.Collections.Generic.List[object]
    if (Test-Path -LiteralPath $worktreesDir) {
        Get-ChildItem -LiteralPath $worktreesDir -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $dir = $_.FullName
            $key = $dir.ToLower().TrimEnd('\')
            $reg = $regMap[$key]
            $branch = if ($reg -and $reg.Branch) { $reg.Branch } else { "(unregistered)" }
            $sz = Get-WorktreeDirectorySize -Path $dir
            $isCurrent = ($currentNorm -and $key -eq $currentNorm)
            $obj = New-Object PD2V2.WorktreeEntry
            $obj.Name = $_.Name
            $obj.Path = $dir
            $obj.Branch = $branch
            $obj.ModifiedDisplay = $_.LastWriteTime.ToString("yyyy-MM-dd HH:mm")
            $obj.SizeBytes = $sz
            $obj.SizeDisplay = Format-WorktreeByteSize $sz
            $obj.IsRegistered = ($null -ne $reg)
            $obj.IsPrunable = ($reg -and $reg.Prunable)
            $obj.IsCurrent = $isCurrent
            $obj.IsSelected = $false
            [void]$entries.Add($obj)
        }
    }

    $total = [long]0
    foreach ($e in $entries) { $total += $e.SizeBytes }

    return [PSCustomObject]@{
        OnDisk           = $entries
        StalePrunable    = $stalePrunable
        TotalSizeBytes   = $total
        WorktreesDir     = $worktreesDir
    }
}

function Show-WorktreeClearDialog {
    param(
        [Parameter(Mandatory=$true)]$Entries
    )

    $xamlStr = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Clear Worktrees"
        Width="900" Height="560"
        WindowStartupLocation="CenterOwner"
        Background="#F4F6F8">
    <DockPanel Margin="14">
        <StackPanel DockPanel.Dock="Top" Margin="0,0,0,10">
            <TextBlock x:Name="LblHeader"     FontFamily="Consolas" FontSize="14" FontWeight="Bold" Foreground="#1A2733"/>
            <TextBlock x:Name="LblTotal"      FontFamily="Consolas" FontSize="12" Foreground="#4A5868" Margin="0,4,0,0"/>
            <TextBlock x:Name="LblAutoSelect" FontFamily="Consolas" FontSize="12" Foreground="#A04020" Margin="0,2,0,0" Visibility="Collapsed"/>
            <TextBlock x:Name="LblSelected"   FontFamily="Consolas" FontSize="12" Foreground="#0078A8" Margin="0,4,0,0"/>
        </StackPanel>
        <Border DockPanel.Dock="Bottom" BorderBrush="#C0C8D2" BorderThickness="0,1,0,0" Margin="0,10,0,0" Padding="0,10,0,0">
            <DockPanel>
                <CheckBox x:Name="ChkAlsoPrune" DockPanel.Dock="Left" VerticalAlignment="Center"
                          FontFamily="Consolas" FontSize="12"/>
                <StackPanel Orientation="Horizontal" HorizontalAlignment="Right">
                    <Button x:Name="BtnSelectAll"  Content="Select All"  Width="110" Margin="0,0,6,0" Padding="8,5" FontFamily="Consolas" FontSize="12"/>
                    <Button x:Name="BtnSelectNone" Content="Select None" Width="110" Margin="0,0,16,0" Padding="8,5" FontFamily="Consolas" FontSize="12"/>
                    <Button x:Name="BtnCancel"     Content="Cancel"      Width="90"  Margin="0,0,6,0" Padding="8,5" FontFamily="Consolas" FontSize="12"/>
                    <Button x:Name="BtnClear"      Content="Clear Selected" Width="160" Padding="8,5" FontFamily="Consolas" FontSize="12" FontWeight="Bold" Background="#B85020" Foreground="White"/>
                </StackPanel>
            </DockPanel>
        </Border>
        <DataGrid x:Name="DgWorktrees"
                  AutoGenerateColumns="False"
                  HeadersVisibility="Column"
                  GridLinesVisibility="Horizontal"
                  CanUserAddRows="False"
                  CanUserDeleteRows="False"
                  CanUserReorderColumns="False"
                  CanUserResizeColumns="True"
                  CanUserSortColumns="True"
                  IsReadOnly="False"
                  RowHeight="24"
                  AlternatingRowBackground="#F0F2F5"
                  Background="#FFFFFF"
                  FontFamily="Consolas" FontSize="12">
            <DataGrid.Columns>
                <DataGridCheckBoxColumn Header="" Binding="{Binding IsSelected, Mode=TwoWay, UpdateSourceTrigger=PropertyChanged}" Width="34"/>
                <DataGridTextColumn Header="Name"          Binding="{Binding Name}"            Width="240" IsReadOnly="True"/>
                <DataGridTextColumn Header="Branch"        Binding="{Binding Branch}"          Width="260" IsReadOnly="True"/>
                <DataGridTextColumn Header="Last Modified" Binding="{Binding ModifiedDisplay}" Width="130" IsReadOnly="True"/>
                <DataGridTextColumn Header=""              Binding="{Binding StaleTag}"        Width="60"  IsReadOnly="True">
                    <DataGridTextColumn.ElementStyle>
                        <Style TargetType="TextBlock">
                            <Setter Property="Foreground" Value="#A04020"/>
                            <Setter Property="FontWeight" Value="Bold"/>
                            <Setter Property="HorizontalAlignment" Value="Center"/>
                        </Style>
                    </DataGridTextColumn.ElementStyle>
                </DataGridTextColumn>
                <DataGridTextColumn Header="Size"          Binding="{Binding SizeDisplay}"     Width="100" IsReadOnly="True"/>
            </DataGrid.Columns>
        </DataGrid>
    </DockPanel>
</Window>
"@

    $reader = New-Object System.Xml.XmlNodeReader ([xml]$xamlStr)
    $dlg = [Windows.Markup.XamlReader]::Load($reader)

    $dgWorktrees    = $dlg.FindName("DgWorktrees")
    $lblHeader      = $dlg.FindName("LblHeader")
    $lblTotal       = $dlg.FindName("LblTotal")
    $lblAutoSelect  = $dlg.FindName("LblAutoSelect")
    $lblSelected    = $dlg.FindName("LblSelected")
    $chkAlsoPrune   = $dlg.FindName("ChkAlsoPrune")
    $btnSelectAll   = $dlg.FindName("BtnSelectAll")
    $btnSelectNone  = $dlg.FindName("BtnSelectNone")
    $btnCancel      = $dlg.FindName("BtnCancel")
    $btnClear       = $dlg.FindName("BtnClear")

    $obs = New-Object System.Collections.ObjectModel.ObservableCollection[object]
    foreach ($e in $Entries.OnDisk) { [void]$obs.Add($e) }
    $dgWorktrees.ItemsSource = $obs

    $lblHeader.Text  = ("On-disk worktrees: {0}    Stale registry entries: {1}" -f $Entries.OnDisk.Count, $Entries.StalePrunable.Count)
    $lblTotal.Text   = ("Total on-disk size: {0}" -f (Format-WorktreeByteSize $Entries.TotalSizeBytes))
    $chkAlsoPrune.Content   = ("Also prune {0} stale registry entries" -f $Entries.StalePrunable.Count)
    $chkAlsoPrune.IsChecked = ($Entries.StalePrunable.Count -gt 0)
    $chkAlsoPrune.IsEnabled = ($Entries.StalePrunable.Count -gt 0)

    # c124: Surface the auto-select rule so pre-checked rows are explained, not
    # confusing. Counts only non-current stale rows because the active worktree
    # is never auto-selected (matches Select All behaviour).
    $autoStaleCount = 0
    foreach ($e in $Entries.OnDisk) { if ($e.IsStale -and -not $e.IsCurrent) { $autoStaleCount++ } }
    if ($autoStaleCount -gt 0) {
        $todayStr = [DateTime]::Today.ToString("yyyy-MM-dd")
        $lblAutoSelect.Text = ("Auto-selected {0} worktrees last modified before {1} (uncheck any you want to keep)" -f $autoStaleCount, $todayStr)
        $lblAutoSelect.Visibility = [System.Windows.Visibility]::Visible
    }

    $updateSelectedLine = {
        $count = 0; $sum = [long]0
        foreach ($e in $obs) { if ($e.IsSelected) { $count++; $sum += $e.SizeBytes } }
        $lblSelected.Text = ("Selected: {0} worktrees, {1}" -f $count, (Format-WorktreeByteSize $sum))
    }
    & $updateSelectedLine

    # Refresh selection summary on every edit. CellEditEnding fires after the
    # bound source property is set, so we can read the running totals directly.
    $dgWorktrees.Add_CellEditEnding({ try { & $updateSelectedLine } catch {} })

    $btnSelectAll.Add_Click({
        foreach ($e in $obs) {
            if (-not $e.IsCurrent) { $e.IsSelected = $true }
        }
        & $updateSelectedLine
    })
    $btnSelectNone.Add_Click({
        foreach ($e in $obs) { $e.IsSelected = $false }
        & $updateSelectedLine
    })

    $script:WorktreeClearResult = $null
    $btnCancel.Add_Click({ $dlg.DialogResult = $false; $dlg.Close() })
    $btnClear.Add_Click({
        $selected = New-Object System.Collections.Generic.List[object]
        $blockedCurrent = $false
        foreach ($e in $obs) {
            if ($e.IsSelected) {
                if ($e.IsCurrent) { $blockedCurrent = $true; continue }
                [void]$selected.Add($e)
            }
        }
        if ($blockedCurrent) {
            [System.Windows.MessageBox]::Show($dlg, "Skipping the active worktree (this dev-window or build is using it). Uncheck it or remove it manually.", "Clear Worktrees", "OK", "Warning") | Out-Null
        }
        if ($selected.Count -eq 0 -and -not $chkAlsoPrune.IsChecked) {
            [System.Windows.MessageBox]::Show($dlg, "Nothing to do. Select at least one worktree, or enable prune.", "Clear Worktrees", "OK", "Information") | Out-Null
            return
        }
        $sumBytes = [long]0
        foreach ($e in $selected) { $sumBytes += $e.SizeBytes }
        $confirmMsg = ("Remove {0} on-disk worktrees ({1})?" -f $selected.Count, (Format-WorktreeByteSize $sumBytes))
        if ($chkAlsoPrune.IsChecked) { $confirmMsg += ("`n`nAlso prune {0} stale registry entries." -f $Entries.StalePrunable.Count) }
        $confirmMsg += "`n`nThis runs git worktree remove --force, falls back to rm -rf for stragglers, and deletes the claude/* branch if it exists."
        $ok = [System.Windows.MessageBox]::Show($dlg, $confirmMsg, "Confirm Clear Worktrees", "OKCancel", "Warning")
        if ($ok -ne [System.Windows.MessageBoxResult]::OK) { return }

        $script:WorktreeClearResult = [PSCustomObject]@{
            Selected  = @($selected)
            AlsoPrune = [bool]$chkAlsoPrune.IsChecked
        }
        $dlg.DialogResult = $true
        $dlg.Close()
    })

    try { $dlg.Owner = [System.Windows.Application]::Current.MainWindow } catch {}
    [void]$dlg.ShowDialog()
    return $script:WorktreeClearResult
}

# Worker scriptblock for the runspace pool. Removes a list of worktrees and
# (optionally) runs git worktree prune. Returns a structured report so the
# UI thread can render per-worktree outcomes + a freed-byte total.
$script:WorktreeClearWorker = {
    param($gitExe, $root, $selectedPaths, $alsoPrune)

    $results = New-Object System.Collections.Generic.List[object]
    $freedTotal = [long]0

    function _measure_dir([string]$p) {
        if (-not (Test-Path -LiteralPath $p)) { return [long]0 }
        $sum = [long]0
        try {
            $stack = New-Object System.Collections.Generic.Stack[string]
            $stack.Push($p)
            while ($stack.Count -gt 0) {
                $d = $stack.Pop()
                try {
                    $di = [System.IO.DirectoryInfo]::new($d)
                    foreach ($f in $di.EnumerateFiles()) { try { $sum += $f.Length } catch {} }
                    foreach ($s in $di.EnumerateDirectories()) {
                        if (($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) { $stack.Push($s.FullName) }
                    }
                } catch {}
            }
        } catch {}
        return $sum
    }

    function _run_git($args) {
        $outF = Join-Path $env:TEMP ("pd2-wt-clr-" + [guid]::NewGuid().ToString("N") + ".out")
        $errF = $outF + ".err"
        try {
            $proc = Start-Process -FilePath $gitExe -ArgumentList $args -Wait -PassThru -NoNewWindow `
                -RedirectStandardOutput $outF -RedirectStandardError $errF
            $o = Get-Content $outF -Raw -ErrorAction SilentlyContinue
            $e = Get-Content $errF -Raw -ErrorAction SilentlyContinue
            return [PSCustomObject]@{ Code = $proc.ExitCode; Out = $o; Err = $e }
        } catch {
            return [PSCustomObject]@{ Code = -1; Out = ""; Err = $_.Exception.Message }
        } finally {
            try { Remove-Item $outF -Force -ErrorAction SilentlyContinue } catch {}
            try { Remove-Item $errF -Force -ErrorAction SilentlyContinue } catch {}
        }
    }

    foreach ($entry in $selectedPaths) {
        $name = $entry.Name
        $path = $entry.Path
        $branch = $entry.Branch
        $isRegistered = $entry.IsRegistered

        $sizeBefore = _measure_dir $path

        $steps = New-Object System.Collections.Generic.List[string]
        $ok = $false
        $usedForce = $false
        $fellBackToFs = $false

        if ($isRegistered) {
            $r1 = _run_git @("-C", $root, "worktree", "remove", $path)
            if ($r1.Code -eq 0) {
                $steps.Add("git worktree remove -> OK")
                $ok = $true
            } else {
                $msg = if ($r1.Err) { ($r1.Err.Trim() -replace "`r?`n", " | ") } else { "exit " + $r1.Code }
                $steps.Add("git worktree remove -> FAIL (" + $msg + ")")
                $r2 = _run_git @("-C", $root, "worktree", "remove", "--force", $path)
                if ($r2.Code -eq 0) {
                    $steps.Add("git worktree remove --force -> OK")
                    $ok = $true
                    $usedForce = $true
                } else {
                    $msg2 = if ($r2.Err) { ($r2.Err.Trim() -replace "`r?`n", " | ") } else { "exit " + $r2.Code }
                    $steps.Add("git worktree remove --force -> FAIL (" + $msg2 + ")")
                }
            }
        } else {
            $steps.Add("(unregistered: skipping git worktree remove)")
        }

        if (Test-Path -LiteralPath $path) {
            try {
                Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop
                $steps.Add("rm -rf -> OK")
                $ok = $true
                $fellBackToFs = $true
            } catch {
                $steps.Add("rm -rf -> FAIL (" + $_.Exception.Message + ")")
            }
        }

        if ($branch -and $branch -ne "(unregistered)" -and $branch -ne "(detached)") {
            # Note: array elements that are string concatenations MUST be parenthesised.
            # Inside @(...) the parser otherwise reads `"x" + $y` as two elements ("x"
            # then a unary + on $y) because the comma binds tighter than the +.
            $verifyRef = _run_git @("-C", $root, "show-ref", "--verify", "--quiet", ("refs/heads/" + $branch))
            if ($verifyRef.Code -eq 0) {
                $rb = _run_git @("-C", $root, "branch", "-D", $branch)
                if ($rb.Code -eq 0) {
                    $steps.Add("git branch -D " + $branch + " -> OK")
                } else {
                    $msg = if ($rb.Err) { ($rb.Err.Trim() -replace "`r?`n", " | ") } else { "exit " + $rb.Code }
                    $steps.Add("git branch -D " + $branch + " -> FAIL (" + $msg + ")")
                }
            }
        }

        $stillThere = Test-Path -LiteralPath $path
        $freed = if ($stillThere) { [long]0 } else { $sizeBefore }
        $freedTotal += $freed

        $results.Add([PSCustomObject]@{
            Name      = $name
            Path      = $path
            Branch    = $branch
            SizeBytes = $sizeBefore
            FreedBytes = $freed
            Ok        = (-not $stillThere)
            UsedForce = $usedForce
            FellBackToFs = $fellBackToFs
            Steps     = $steps
        })
    }

    $pruneOut = ""
    $pruneCode = 0
    if ($alsoPrune) {
        $pp = _run_git @("-C", $root, "worktree", "prune", "-v")
        $pruneCode = $pp.Code
        if ($pp.Out) { $pruneOut += $pp.Out }
        if ($pp.Err) { $pruneOut += $pp.Err }
    }

    return [PSCustomObject]@{
        Results    = @($results)
        FreedTotal = $freedTotal
        Pruned     = $alsoPrune
        PruneCode  = $pruneCode
        PruneOut   = $pruneOut
    }
}

function Test-KanbanServerUp {
    param([int]$Port = 7531, [int]$TimeoutMs = 250)
    $client = $null
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $iar = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne($TimeoutMs, $false)
        if (-not $ok) { return $false }
        $client.EndConnect($iar)
        return $client.Connected
    } catch {
        return $false
    } finally {
        if ($client) { try { $client.Close() } catch {} }
    }
}

function Test-KanbanPortBindable {
    param([int]$Port)
    $listener = $null
    try {
        $address = [System.Net.IPAddress]::Parse("127.0.0.1")
        $listener = [System.Net.Sockets.TcpListener]::new($address, $Port)
        $listener.Start()
        return $true
    } catch {
        return $false
    } finally {
        if ($listener) { try { $listener.Stop() } catch {} }
    }
}

function Test-KanbanTokenlessHttp {
    param([int]$Port)
    try {
        $resp = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$Port/api/state" -TimeoutSec 2 -ErrorAction Stop
        return ($resp.StatusCode -eq 200)
    } catch {
        return $false
    }
}

function Select-LocalKanbanTarget {
    for ($candidate = 7531; $candidate -le 7541; $candidate++) {
        if (Test-KanbanServerUp -Port $candidate) {
            if (Test-KanbanTokenlessHttp -Port $candidate) {
                return [PSCustomObject]@{ Port = $candidate; Running = $true }
            }
            continue
        }
        if (Test-KanbanPortBindable -Port $candidate) {
            return [PSCustomObject]@{ Port = $candidate; Running = $false }
        }
    }
    return $null
}

function Invoke-OpenKanban {
    $target = Select-LocalKanbanTarget
    if ($null -eq $target) {
        Add-LogLine "Open Kanban: no free local port found in 7531-7541." "#B81818"
        [System.Windows.MessageBox]::Show("No free local Kanban port was found in 7531-7541.", "Open Kanban", "OK", "Warning") | Out-Null
        return
    }
    $port = [int]$target.Port
    $url = "http://localhost:$port/"
    $serverScript = Join-Path $script:ProjectRoot "tools\kanban\server.py"

    if ($target.Running) {
        Add-LogSessionLine ">>> Open Kanban: tokenless local server already running on $port; opening browser." "#0078A8"
        try { Start-Process $url } catch {
            Add-LogLine ("Open Kanban: Start-Process failed: " + $_.Exception.Message) "#B81818"
            [System.Windows.MessageBox]::Show("Could not open browser at $url. See the Log tab for details.", "Open Kanban", "OK", "Warning") | Out-Null
        }
        return
    }

    if (-not (Test-Path -LiteralPath $serverScript)) {
        Add-LogLine ("Open Kanban: server script not found at " + $serverScript) "#B81818"
        [System.Windows.MessageBox]::Show("Kanban server script not found:`n$serverScript", "Open Kanban", "OK", "Warning") | Out-Null
        return
    }
    if (-not $script:Python -or -not (Test-Path -LiteralPath $script:Python)) {
        Add-LogLine ("Open Kanban: python interpreter not found at " + $script:Python) "#B81818"
        [System.Windows.MessageBox]::Show("Python interpreter not found at:`n$($script:Python)", "Open Kanban", "OK", "Warning") | Out-Null
        return
    }

    Add-LogSessionLine "" "#C0C8D2"
    Add-LogSessionLine ">>> Open Kanban: starting tokenless local server on port $port..." "#0078A8"

    try {
        $localStateDir = Join-Path $script:ProjectRoot ".claude\scratch\kanban-local"
        if (-not (Test-Path -LiteralPath $localStateDir)) { New-Item -ItemType Directory -Force -Path $localStateDir | Out-Null }
        $stdoutPath = Join-Path $localStateDir "kanban-local.out.log"
        $stderrPath = Join-Path $localStateDir "kanban-local.err.log"
        $psExe = (Get-Command powershell.exe -ErrorAction Stop).Source
        $startupCommand = @(
            "Remove-Item Env:KANBAN_REMOTE_TOKEN -ErrorAction SilentlyContinue",
            "`$env:KANBAN_HOST = 'localhost'",
            "`$env:KANBAN_PORT = '$port'",
            "Set-Location -LiteralPath '$($script:ProjectRoot)'",
            "& '$($script:Python)' '$serverScript'"
        ) -join "; "
        $encodedStartupCommand = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($startupCommand))
        $proc = Start-Process -FilePath $psExe `
                              -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", $encodedStartupCommand) `
                              -WorkingDirectory $script:ProjectRoot `
                              -WindowStyle Hidden `
                              -RedirectStandardOutput $stdoutPath `
                              -RedirectStandardError $stderrPath `
                              -PassThru
        Add-LogLine ("Open Kanban: spawned local launcher pid=" + $proc.Id) "#44586C"
    } catch {
        Add-LogLine ("Open Kanban: failed to launch server: " + $_.Exception.Message) "#B81818"
        [System.Windows.MessageBox]::Show("Failed to start kanban server:`n" + $_.Exception.Message, "Open Kanban", "OK", "Warning") | Out-Null
        return
    }

    # Poll for port readiness. Server startup is normally well under a second
    # (single Python HTTPServer bind), but allow up to ~5s to cover slow disk
    # / Defender first-run scan.
    $deadline = [DateTime]::Now.AddSeconds(5)
    $ready = $false
    while ([DateTime]::Now -lt $deadline) {
        if (Test-KanbanServerUp -Port $port -TimeoutMs 200) { $ready = $true; break }
        Start-Sleep -Milliseconds 150
    }

    if (-not $ready) {
        Add-LogLine "Open Kanban: server did not come up within 5s. Opening browser anyway; refresh once it is up." "#A07810"
    } else {
        Add-LogLine ("Open Kanban: server listening on port " + $port + ".") "#1A8A1A"
    }

    try { Start-Process $url } catch {
        Add-LogLine ("Open Kanban: Start-Process failed: " + $_.Exception.Message) "#B81818"
        [System.Windows.MessageBox]::Show("Could not open browser at $url. See the Log tab for details.", "Open Kanban", "OK", "Warning") | Out-Null
    }
}

function Test-KanbanRemotePidAlive {
    param([int]$ProcessId)
    if ($ProcessId -le 0) { return $false }
    try {
        $p = Get-Process -Id $ProcessId -ErrorAction Stop
        return -not $p.HasExited
    } catch {
        return $false
    }
}

function Test-KanbanRemoteStateAlive {
    param([string]$StatePath)
    if (-not (Test-Path -LiteralPath $StatePath)) { return $false }
    try {
        $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
        return (Test-KanbanRemotePidAlive ([int]$state.server_pid)) -and (Test-KanbanRemotePidAlive ([int]$state.tunnel_pid))
    } catch {
        return $false
    }
}

function Reset-KanbanRemoteButton {
    $script:KanbanRemoteBusy = $false
    $ui["BtnStartKanbanServer"].IsEnabled = $true
    $ui["BtnStartKanbanServer"].Content = "Start Kanban Server"
}

function Complete-KanbanRemoteStart {
    param([string]$Url)
    if (-not $Url) { return }
    Reset-KanbanRemoteButton
    try { [System.Windows.Clipboard]::SetText($Url) } catch {}
    Add-LogLine ("Start Kanban Server: phone link copied to clipboard: " + $Url) "#1A8A1A"
    [System.Windows.MessageBox]::Show(
        "Kanban is running for remote phone access.`n`nThe join link has been copied to the clipboard:`n`n$Url",
        "Start Kanban Server",
        "OK",
        "Information"
    ) | Out-Null
}

function Invoke-StartKanbanRemote {
    $starterScript = Join-Path $script:ProjectRoot "devtools\start-kanban-remote.ps1"
    $stateDir = Join-Path $script:ProjectRoot ".claude\scratch\kanban-remote"
    $urlFile = Join-Path $stateDir "remote-url.txt"
    $pidFile = Join-Path $stateDir "pids.json"
    if ($script:KanbanRemoteBusy) {
        [System.Windows.MessageBox]::Show("Kanban remote startup is already running.", "Start Kanban Server", "OK", "Information") | Out-Null
        return
    }
    if (-not (Test-Path -LiteralPath $starterScript)) {
        Add-LogLine ("Start Kanban Server: starter script not found at " + $starterScript) "#B81818"
        [System.Windows.MessageBox]::Show("Remote Kanban starter not found:`n$starterScript", "Start Kanban Server", "OK", "Warning") | Out-Null
        return
    }
    if ((Test-KanbanRemoteStateAlive $pidFile) -and (Test-Path -LiteralPath $urlFile)) {
        $existingUrl = (Get-Content -LiteralPath $urlFile -Raw -ErrorAction SilentlyContinue).Trim()
        if ($existingUrl) {
            Complete-KanbanRemoteStart $existingUrl
            return
        }
    }
    if (Test-Path -LiteralPath $urlFile) {
        try { Remove-Item -LiteralPath $urlFile -Force } catch {}
    }

    $script:KanbanRemoteBusy = $true
    $ui["BtnStartKanbanServer"].IsEnabled = $false
    $ui["BtnStartKanbanServer"].Content = "Starting..."
    Add-LogSessionLine "" "#C0C8D2"
    Add-LogSessionLine ">>> Start Kanban Server: starting remote Kanban access..." "#0078A8"
    Add-LogLine "Start Kanban Server: target is a dedicated 127.0.0.1 Kanban port; no proxy, WARP, exit node, or route changes." "#44586C"

    Start-AsyncPoolAction `
        -Script {
            param($root, $starter)
            $stateDir = Join-Path $root ".claude\scratch\kanban-remote"
            $urlFile = Join-Path $root ".claude\scratch\kanban-remote\remote-url.txt"
            $outFile = Join-Path $stateDir "devwindow-start.out.log"
            $errFile = Join-Path $stateDir "devwindow-start.err.log"
            $result = [PSCustomObject]@{
                Ok = $false
                ExitCode = -1
                Output = ""
                Url = ""
                UrlFile = $urlFile
            }
            try {
                if (-not (Test-Path -LiteralPath $stateDir)) {
                    New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
                }
                if (Test-Path -LiteralPath $outFile) { Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue }
                if (Test-Path -LiteralPath $errFile) { Remove-Item -LiteralPath $errFile -Force -ErrorAction SilentlyContinue }
                $psExe = (Get-Command powershell.exe -ErrorAction Stop).Source
                $helper = Start-Process -FilePath $psExe `
                    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $starter, "-ReuseToken") `
                    -WorkingDirectory $root `
                    -WindowStyle Hidden `
                    -RedirectStandardOutput $outFile `
                    -RedirectStandardError $errFile `
                    -PassThru
                $url = ""
                $deadline = [DateTime]::UtcNow.AddSeconds(90)
                while ([DateTime]::UtcNow -lt $deadline) {
                    if (Test-Path -LiteralPath $urlFile) {
                        $url = (Get-Content -LiteralPath $urlFile -Raw -ErrorAction SilentlyContinue).Trim()
                        if ($url -match '^https://') {
                            $result.Ok = $true
                            $result.ExitCode = 0
                            $result.Url = $url
                            return $result
                        }
                    }
                    if ($helper.HasExited) {
                        break
                    }
                    Start-Sleep -Milliseconds 500
                }

                $code = if ($helper.HasExited) { [int]$helper.ExitCode } else { -1 }
                $output = ""
                if (Test-Path -LiteralPath $outFile) { $output += Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue }
                if (Test-Path -LiteralPath $errFile) { $output += Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue }
                if (-not $url -and $output) {
                    $m = [regex]::Match($output, "https://\S+?trycloudflare\.com/\?token=[0-9A-Fa-f]+")
                    if ($m.Success) { $url = $m.Value }
                }
                $result.Ok = (-not [string]::IsNullOrWhiteSpace($url))
                $result.ExitCode = $code
                $result.Output = $output
                $result.Url = $url
            } catch {
                $result.Output = $_.Exception.Message
            }
            return $result
        } `
        -Arguments @($script:ProjectRoot, $starterScript) `
        -OnComplete {
            param($result)
            if (-not $script:KanbanRemoteBusy) {
                return
            }

            $r = @($result)[0]
            if ($null -eq $r) {
                Reset-KanbanRemoteButton
                Add-LogLine "Start Kanban Server: startup failed before returning a result." "#B81818"
                [System.Windows.MessageBox]::Show("Remote Kanban startup failed. See the Log tab for details.", "Start Kanban Server", "OK", "Warning") | Out-Null
                return
            }

            if ($r.Output) {
                foreach ($line in (($r.Output -split "`r?`n") | Where-Object { $_.Trim() })) {
                    Add-LogLine ("Start Kanban Server: " + $line) "#44586C"
                }
            }

            if ($r.Ok -and $r.Url) {
                Complete-KanbanRemoteStart $r.Url
                return
            }

            Reset-KanbanRemoteButton
            $message = "Remote Kanban startup failed."
            if ($r.Output -match "cloudflared\.exe was not found") {
                $message = "cloudflared.exe was not found. Install Cloudflare cloudflared, then click Start Kanban Server again."
            }
            Add-LogLine ("Start Kanban Server: failed with exit code " + $r.ExitCode) "#B81818"
            [System.Windows.MessageBox]::Show($message + "`n`nSee the Log tab for details.", "Start Kanban Server", "OK", "Warning") | Out-Null
        }
}

function Invoke-StopKanbanRemote {
    $starterScript = Join-Path $script:ProjectRoot "devtools\start-kanban-remote.ps1"
    if ($script:KanbanRemoteStopBusy) {
        [System.Windows.MessageBox]::Show("Kanban remote stop is already running.", "Stop Kanban Server", "OK", "Information") | Out-Null
        return
    }
    if (-not (Test-Path -LiteralPath $starterScript)) {
        Add-LogLine ("Stop Kanban Server: starter script not found at " + $starterScript) "#B81818"
        [System.Windows.MessageBox]::Show("Remote Kanban starter not found:`n$starterScript", "Stop Kanban Server", "OK", "Warning") | Out-Null
        return
    }

    Reset-KanbanRemoteButton
    $script:KanbanRemoteStopBusy = $true
    $ui["BtnStopKanbanServer"].IsEnabled = $false
    $ui["BtnStopKanbanServer"].Content = "Stopping..."
    Add-LogSessionLine "" "#C0C8D2"
    Add-LogSessionLine ">>> Stop Kanban Server: stopping tracked remote Kanban session..." "#A07810"

    Start-AsyncPoolAction `
        -Script {
            param($starter)
            $result = [PSCustomObject]@{
                Ok = $false
                ExitCode = -1
                Output = ""
            }
            try {
                $psExe = (Get-Command powershell.exe -ErrorAction Stop).Source
                $output = (& $psExe -NoProfile -ExecutionPolicy Bypass -File $starter -Stop 2>&1 | Out-String)
                $code = if ($null -ne $LASTEXITCODE) { [int]$LASTEXITCODE } else { 0 }
                $result.Ok = ($code -eq 0)
                $result.ExitCode = $code
                $result.Output = $output
            } catch {
                $result.Output = $_.Exception.Message
            }
            return $result
        } `
        -Arguments @($starterScript) `
        -OnComplete {
            param($result)
            $script:KanbanRemoteStopBusy = $false
            $ui["BtnStopKanbanServer"].IsEnabled = $true
            $ui["BtnStopKanbanServer"].Content = "Stop Kanban Server"

            $r = @($result)[0]
            if ($null -eq $r) {
                Add-LogLine "Stop Kanban Server: stop failed before returning a result." "#B81818"
                [System.Windows.MessageBox]::Show("Remote Kanban stop failed. See the Log tab for details.", "Stop Kanban Server", "OK", "Warning") | Out-Null
                return
            }

            if ($r.Output) {
                foreach ($line in (($r.Output -split "`r?`n") | Where-Object { $_.Trim() })) {
                    Add-LogLine ("Stop Kanban Server: " + $line) "#44586C"
                }
            }

            if ($r.Ok) {
                Add-LogLine "Stop Kanban Server: tracked remote Kanban session stopped." "#1A8A1A"
                [System.Windows.MessageBox]::Show("Remote Kanban server and tunnel stopped.", "Stop Kanban Server", "OK", "Information") | Out-Null
                return
            }

            Add-LogLine ("Stop Kanban Server: failed with exit code " + $r.ExitCode) "#B81818"
            [System.Windows.MessageBox]::Show("Remote Kanban stop failed. See the Log tab for details.", "Stop Kanban Server", "OK", "Warning") | Out-Null
        }
}

function Invoke-GitPruneWorktrees {
    if ($script:IsBuilding -or $script:IsPushing) {
        [System.Windows.MessageBox]::Show("Wait for the current build or release to finish.", "Clear Worktrees", "OK", "Information") | Out-Null
        return
    }
    if ($script:GitActionBusy) {
        [System.Windows.MessageBox]::Show("Another git action is in progress.", "Clear Worktrees", "OK", "Information") | Out-Null
        return
    }
    $gitExe = Resolve-GitExecutable
    if (-not $gitExe) {
        [System.Windows.MessageBox]::Show("git was not found on PATH.", "Clear Worktrees", "OK", "Warning") | Out-Null
        return
    }

    $script:GitActionBusy = $true
    $script:GitActionLabel = "enumerating worktrees..."
    $ui["BtnPruneWorktrees"].IsEnabled = $false
    Add-LogSessionLine "" "#C0C8D2"
    Add-LogSessionLine ">>> Clear Worktrees: enumerating .claude/worktrees..." "#0078A8"

    $cwd = ""
    try { $cwd = (Get-Location).Path } catch { $cwd = "" }

    # Stash for the OnComplete callbacks: function-local vars do not survive the
    # async boundary (Start-AsyncPoolAction passes plain scriptblocks per the
    # comment near line 207); script-scope persists.
    $script:WtClearGitExe = $gitExe

    Start-AsyncPoolAction `
        -Script {
            param($gitExe, $root, $currentPath)

            # Inline helper: child runspace has no access to module-level functions.
            function Get-WorktreeDirectorySize {
                param([string]$Path)
                if (-not (Test-Path -LiteralPath $Path)) { return [long]0 }
                $total = [long]0
                try {
                    $stack = New-Object System.Collections.Generic.Stack[string]
                    $stack.Push($Path)
                    while ($stack.Count -gt 0) {
                        $d = $stack.Pop()
                        try {
                            $di = [System.IO.DirectoryInfo]::new($d)
                            foreach ($f in $di.EnumerateFiles()) { try { $total += $f.Length } catch {} }
                            foreach ($s in $di.EnumerateDirectories()) {
                                if (($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) { $stack.Push($s.FullName) }
                            }
                        } catch {}
                    }
                } catch {}
                return $total
            }

            $worktreesDir = Join-Path $root ".claude\worktrees"
            $regMap = @{}
            $stalePrunable = New-Object System.Collections.ArrayList
            $regOutRaw = ""
            try { $regOutRaw = (& $gitExe -C $root worktree list --porcelain 2>$null | Out-String) } catch {}
            if ($regOutRaw) {
                $blocks = $regOutRaw -split "(`r?`n){2,}"
                foreach ($block in $blocks) {
                    $p = ""; $b = ""; $pr = $false
                    foreach ($line in ($block -split "`r?`n")) {
                        if ($line -match '^worktree\s+(.+)$') { $p = $matches[1].Trim() }
                        elseif ($line -match '^branch\s+refs/heads/(.+)$') { $b = $matches[1].Trim() }
                        elseif ($line -match '^detached') { $b = "(detached)" }
                        elseif ($line -match '^prunable') { $pr = $true }
                    }
                    if ($p) {
                        $norm = $p -replace '/', '\'
                        try { $rp = Resolve-Path -LiteralPath $norm -ErrorAction SilentlyContinue; if ($rp) { $norm = $rp.Path } } catch {}
                        $key = $norm.ToLower().TrimEnd('\')
                        $regMap[$key] = [PSCustomObject]@{ Branch = $b; Prunable = $pr; Path = $norm }
                        if ($pr) { [void]$stalePrunable.Add($norm) }
                    }
                }
            }

            $currentNorm = ""
            if ($currentPath) {
                try { $rp = Resolve-Path -LiteralPath $currentPath -ErrorAction SilentlyContinue; if ($rp) { $currentNorm = $rp.Path.ToLower().TrimEnd('\') } } catch {}
            }

            # Local-day boundary (00:00 today). Worktrees touched before this are
            # auto-selected for clearing (c124). [DateTime]::Today returns local
            # midnight, FileInfo.LastWriteTime is also local, so the comparison is
            # zone-consistent.
            $todayStart = [DateTime]::Today

            $entries = New-Object System.Collections.Generic.List[object]
            if (Test-Path -LiteralPath $worktreesDir) {
                Get-ChildItem -LiteralPath $worktreesDir -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                    $dir = $_.FullName
                    $key = $dir.ToLower().TrimEnd('\')
                    $reg = $regMap[$key]
                    $branch = if ($reg -and $reg.Branch) { $reg.Branch } else { "(unregistered)" }
                    $sz = Get-WorktreeDirectorySize -Path $dir
                    $isCurrent = ($currentNorm -and $key -eq $currentNorm)
                    $lwt = $_.LastWriteTime
                    $isStale = ($lwt -lt $todayStart)
                    $obj = New-Object PD2V2.WorktreeEntry
                    $obj.Name = $_.Name
                    $obj.Path = $dir
                    $obj.Branch = $branch
                    $obj.LastModified = $lwt
                    $obj.ModifiedDisplay = $lwt.ToString("yyyy-MM-dd HH:mm")
                    $obj.SizeBytes = $sz
                    $obj.SizeDisplay = if ($sz -lt 1048576) { "{0:N1} KB" -f ($sz / 1024.0) } elseif ($sz -lt 1073741824) { "{0:N1} MB" -f ($sz / 1048576.0) } else { "{0:N2} GB" -f ($sz / 1073741824.0) }
                    $obj.IsRegistered = ($null -ne $reg)
                    $obj.IsPrunable = ($null -ne $reg -and $reg.Prunable)
                    $obj.IsCurrent = $isCurrent
                    $obj.IsStale = $isStale
                    $obj.StaleTag = if ($isStale) { "stale" } else { "" }
                    # Auto-select stale worktrees (c124). Current worktree stays
                    # excluded -- matches Select All's "if (-not IsCurrent)" rule.
                    $obj.IsSelected = ($isStale -and -not $isCurrent)
                    [void]$entries.Add($obj)
                }
            }

            # Sort oldest-first so stale auto-selected rows cluster at the top of
            # the dialog and today's surviving worktrees sit at the bottom.
            $sortedEntries = @($entries | Sort-Object -Property LastModified)
            $total = [long]0; foreach ($e in $sortedEntries) { $total += $e.SizeBytes }

            return [PSCustomObject]@{
                OnDisk = $sortedEntries
                StalePrunable = $stalePrunable
                TotalSizeBytes = $total
                WorktreesDir = $worktreesDir
            }
        } `
        -Arguments @($gitExe, $script:ProjectRoot, $cwd) `
        -OnComplete {
            param($result)
            try {
                $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
                if ($null -eq $r) {
                    Add-LogLine "Enumeration returned no result." "#B81818"
                    $script:GitActionBusy = $false
                    $script:GitActionLabel = ""
                    $ui["BtnPruneWorktrees"].IsEnabled = $true
                    return
                }
                if ($r.OnDisk.Count -eq 0 -and $r.StalePrunable.Count -eq 0) {
                    Add-LogLine ("Clear Worktrees: nothing to clear (0 on-disk, 0 stale).") "#44586C"
                    [System.Windows.MessageBox]::Show("No worktrees on disk and no stale registry entries.", "Clear Worktrees", "OK", "Information") | Out-Null
                    $script:GitActionBusy = $false
                    $script:GitActionLabel = ""
                    $ui["BtnPruneWorktrees"].IsEnabled = $true
                    return
                }

                Add-LogLine ("Found {0} on-disk worktrees, total {1}; {2} stale registry entries." -f `
                    $r.OnDisk.Count, (Format-WorktreeByteSize $r.TotalSizeBytes), $r.StalePrunable.Count) "#6888A8"

                $sel = Show-WorktreeClearDialog -Entries $r
                if (-not $sel) {
                    Add-LogLine "Clear Worktrees: cancelled." "#44586C"
                    $script:GitActionBusy = $false
                    $script:GitActionLabel = ""
                    $ui["BtnPruneWorktrees"].IsEnabled = $true
                    return
                }

                $payload = @()
                foreach ($e in $sel.Selected) {
                    $payload += [PSCustomObject]@{
                        Name = $e.Name
                        Path = $e.Path
                        Branch = $e.Branch
                        IsRegistered = $e.IsRegistered
                    }
                }
                $alsoPrune = $sel.AlsoPrune
                $script:GitActionLabel = "clearing worktrees..."
                Add-LogSessionLine ("") "#C0C8D2"
                Add-LogSessionLine (">>> Clear Worktrees: removing {0} entries (alsoPrune={1})" -f $payload.Count, $alsoPrune) "#0078A8"

                Start-AsyncPoolAction `
                    -Script $script:WorktreeClearWorker `
                    -Arguments @($script:WtClearGitExe, $script:ProjectRoot, $payload, [bool]$alsoPrune) `
                    -OnComplete {
                        param($actionResult)
                        try {
                            $a = if ($actionResult -and $actionResult.Count -gt 0) { $actionResult[0] } else { $actionResult }
                            if ($null -eq $a) {
                                Add-LogLine "Clear Worktrees: action returned no result." "#B81818"
                            } else {
                                $okCount = 0; $failCount = 0
                                foreach ($row in $a.Results) {
                                    $colour = if ($row.Ok) { "#10783A" } else { "#B81818" }
                                    $tag = if ($row.Ok) { "OK" } else { "FAIL" }
                                    Add-LogLine ("[{0}] {1}  freed={2}  branch={3}" -f $tag, $row.Name, (Format-WorktreeByteSize $row.FreedBytes), $row.Branch) $colour
                                    foreach ($s in $row.Steps) { Add-LogLine ("    " + $s) "#4A5868" }
                                    if ($row.Ok) { $okCount++ } else { $failCount++ }
                                }
                                if ($a.Pruned) {
                                    if ($a.PruneOut) { foreach ($l in ($a.PruneOut -split "`r?`n")) { if ($l.Trim()) { Add-LogLine ("[prune] " + $l) "#6888A8" } } }
                                    Add-LogLine ("[prune] git worktree prune -v exited " + $a.PruneCode) (if ($a.PruneCode -eq 0) { "#10783A" } else { "#B81818" })
                                }
                                Add-LogSessionLine ("") "#C0C8D2"
                                Add-LogSessionLine (">>> Clear Worktrees: done. Removed {0}, failed {1}, freed {2}." -f $okCount, $failCount, (Format-WorktreeByteSize $a.FreedTotal)) "#0078A8"

                                $summary = ("Removed: {0}`nFailed: {1}`nFreed:  {2}" -f $okCount, $failCount, (Format-WorktreeByteSize $a.FreedTotal))
                                if ($a.Pruned) { $summary += ("`nPrune:  exit " + $a.PruneCode) }
                                $icon = if ($failCount -eq 0) { "Information" } else { "Warning" }
                                [System.Windows.MessageBox]::Show($summary, "Clear Worktrees - Done", "OK", $icon) | Out-Null
                            }
                        } catch {
                            Add-LogLine ("Clear Worktrees onComplete threw: " + $_.Exception.Message) "#B81818"
                        }
                        $script:GitActionBusy = $false
                        $script:GitActionLabel = ""
                        $ui["BtnPruneWorktrees"].IsEnabled = $true
                        Update-StatusBar
                    }
            } catch {
                Add-LogLine ("Clear Worktrees enum onComplete threw: " + $_.Exception.Message) "#B81818"
                $script:GitActionBusy = $false
                $script:GitActionLabel = ""
                $ui["BtnPruneWorktrees"].IsEnabled = $true
            }
        }
}

function Stop-Build {
    if ($null -ne $script:BuildProcess) { try { $script:BuildProcess.Kill() } catch {}; $script:BuildProcess = $null }
    $script:BuildStepQueue.Clear()
    $script:BuildTimer.Stop()
    $script:IsBuilding = $false; $script:IsPushing = $false
    $ui["BtnBuild"].IsEnabled = $true
    $ui["BtnRelease"].IsEnabled = $true
    $ui["BtnCleanBuild"].IsEnabled = $true
    $ui["BtnPull"].IsEnabled = $true
    $ui["BtnPush"].IsEnabled = $true
    $ui["BtnPruneWorktrees"].IsEnabled = $true
    $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["ProgressBack"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["LblBuildActivity"].Text = "Stopped."
    $ui["LblProgressText"].Text = ""
}

function Start-Build-Step($step) {
    if ($step.Name -eq "Auto-commit + push") {
        Clear-StaleGitIndexLock $script:ProjectRoot
    }
    $script:CurrentStepName   = $step.Name
    $script:CurrentBuildTarget = $step.Target
    $script:OutputQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
    $script:StepStartTime  = [DateTime]::Now
    $script:LastOutputTime = [DateTime]::Now
    $script:LastReleaseHeartbeat = [DateTime]::Now
    $script:BuildPercent   = 0
    $script:NinjaCurrent   = 0
    $script:NinjaTotal     = 0
    $ui["LblBuildActivity"].Text = $step.Name + "..."
    $ui["LblProgressText"].Text = "0% - " + $step.Name
    $ui["ProgressFill"].Background = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))
    $ui["ProgressFill"].Width = 0

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $step.Exe; $psi.Arguments = $step.Args
    $psi.WorkingDirectory = $script:ProjectRoot
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables["PATH"]                 = Get-ChildProcessPathEnv
    $psi.EnvironmentVariables["MSYSTEM"]              = "MINGW64"
    $psi.EnvironmentVariables["MINGW_PREFIX"]         = "/mingw64"
    $psi.EnvironmentVariables["GIT_TERMINAL_PROMPT"]  = "0"
    $psi.EnvironmentVariables["TEMP"]                 = $env:TEMP
    $psi.EnvironmentVariables["TMP"]                  = $env:TMP
    $psi.EnvironmentVariables["CCACHE_SLOPPINESS"]    = "pch_defines,time_macros,include_file_mtime,include_file_ctime"
    if ($env:CCACHE_BASEDIR) { $psi.EnvironmentVariables["CCACHE_BASEDIR"] = $env:CCACHE_BASEDIR }
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        [void]$proc.Start()
        $script:BuildProcess = $proc
        [PD2V2.AsyncLineReader]::StartReading($proc.StandardOutput, $script:OutputQueue, "OUT:")
        [PD2V2.AsyncLineReader]::StartReading($proc.StandardError,  $script:OutputQueue, "ERR:")
        $script:BuildTimer.Start()
    } catch {
        $script:IsBuilding = $false; $script:IsPushing = $false
        $ui["BtnBuild"].IsEnabled = $true; $ui["BtnRelease"].IsEnabled = $true
        $ui["BtnCleanBuild"].IsEnabled = $true
        $ui["BtnPull"].IsEnabled = $true
        $ui["BtnPush"].IsEnabled = $true
        $ui["BtnPruneWorktrees"].IsEnabled = $true
        $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Collapsed
        $ui["LblBuildActivity"].Text = "ERROR starting: " + $step.Exe
    }
}

function Get-BuildSteps($ver, [bool]$forceClean = $false) {
    # SYNC RULE: when configure runs, args MUST stay aligned with build-headless.ps1.
    $cores = $(if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { "4" })
    # SYNC: optional -DPD_STABLE_RELEASE=ON matches CMakeLists.txt / build-headless.ps1
    $vFlags = " -DVERSION_SEM_MAJOR=" + $ver.Major + " -DVERSION_SEM_MINOR=" + $ver.Minor + " -DVERSION_SEM_PATCH=" + $ver.Patch
    $steps = [System.Collections.ArrayList]::new()

    # NOTE (2026-04-27): the old "Auto-commit + push" cmd.exe step was a no-op
    # second commit -- Start-GitSyncBeforeBuild already commits + pushes async
    # before this function runs. Removing it cuts ~2-3 s of redundant git work
    # per build.

    if ($forceClean) {
        $cleanArgs = "/c (if exist `"" + $script:BuildDir + "`" rmdir /s /q `"" + $script:BuildDir + "`") & exit 0"
        [void]$steps.Add(@{Name="Cleaning build dir"; Exe="cmd.exe"; Target="client"; Args=$cleanArgs})
    }

    $ensureBuildDirArgs = "/c if not exist `"" + $script:BuildDir + "`" mkdir `"" + $script:BuildDir + "`""
    [void]$steps.Add(@{Name="Ensure build dir"; Exe="cmd.exe"; Target="client"; Args=$ensureBuildDirArgs})

    $needsConfigure = $forceClean -or (Test-NeedsConfigure $script:BuildDir $ver)
    if ($needsConfigure) {
        $cfgArgs = "-G Ninja -DCMAKE_C_COMPILER=`"" + $script:CC + "`" -DCMAKE_CXX_COMPILER=`"" + $script:CXX + "`" -DCMAKE_C_COMPILER_FORCED=TRUE -DCMAKE_CXX_COMPILER_FORCED=TRUE -DPD_PYTHON_EXECUTABLE=`"" + $script:Python + "`" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -B `"" + $script:BuildDir + "`" -S `"" + $script:ProjectRoot + "`"" + $vFlags
        [void]$steps.Add(@{Name="Configure (Ninja + ccache)"; Exe=$script:CMake; Target="client"; Args=$cfgArgs})
    } else {
        Add-LogSessionLine "CMake configure skipped (cache/version current)." "#44586C"
    }
    # Client only. Dedicated server connectivity is now in-client (listen mode);
    # pd-server is no longer shipped and the cmake target has been removed.
    [void]$steps.Add(@{Name="Build (client: pd)"; Exe=$script:CMake; Target="client"; Args="--build `"" + $script:BuildDir + "`" --target pd --parallel " + $cores})

    return $steps
}

function Reset-BuildUiState {
    # Re-enable buttons and hide progress UI after a failed/cancelled build or release.
    $ui["BtnBuild"].IsEnabled = $true; $ui["BtnRelease"].IsEnabled = $true; $ui["BtnCleanBuild"].IsEnabled = $true
    $ui["BtnPull"].IsEnabled = $true
    $ui["BtnPush"].IsEnabled = $true
    $ui["BtnPruneWorktrees"].IsEnabled = $true
    $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["ProgressBack"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["LblBuildActivity"].Text = "Idle"
    $ui["LblProgressText"].Text = ""
}

function Start-Build {
    if ($script:IsBuilding -or $script:IsPushing) { return }
    # Set guard flag up front so async callbacks and double-click protection see consistent state.
    $script:IsBuilding = $true

    $script:ClientErrors.Clear(); $script:ServerErrors.Clear(); $script:AllOutput.Clear()
    $script:ClientBuildResult = $null; $script:ServerBuildResult = $null
    $script:ClientBuildTime = 0; $script:ServerBuildTime = 0
    $script:HasBuildErrors = $false; $script:CurrentBuildTarget = "client"
    $script:NinjaCurrent = 0; $script:NinjaTotal = 0
    $script:BuildStepsTotal = 0; $script:BuildStepsCompleted = 0

    $ui["LblClientStatus"].Text = "client: building..."; $ui["LblClientStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))
    # LblServerStatus is repurposed as the tests status row (pd-server is no
    # longer built or shipped). Leave whatever the tests pipeline last wrote.
    if ($null -eq $ui["LblServerStatus"].Text -or $ui["LblServerStatus"].Text -match '^server:') {
        $ui["LblServerStatus"].Text = "tests: --"
        $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#4A5868")))
    }
    $ui["BtnBuild"].IsEnabled = $false; $ui["BtnRelease"].IsEnabled = $false; $ui["BtnCleanBuild"].IsEnabled = $false
    $ui["BtnPull"].IsEnabled = $false
    $ui["BtnPush"].IsEnabled = $false
    $ui["BtnPruneWorktrees"].IsEnabled = $false
    $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Visible
    $ui["BtnCopyErrors"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["BtnCopyLog"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["ProgressBack"].Visibility = [System.Windows.Visibility]::Visible
    $ui["LblBuildActivity"].Text = "Git: syncing..."
    $ui["LblProgressText"].Text = "Git: syncing..."
    $ui["ProgressFill"].Background = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))
    $ui["ProgressFill"].Width = 0
    $ui["ProgressBack"].UpdateLayout()
    $pw0 = $ui["ProgressBack"].ActualWidth
    if ($pw0 -gt 0) { $ui["ProgressFill"].Width = [math]::Floor($pw0 * 0.12) }

    # Stash clean-flag in a $script: var instead of capturing via GetNewClosure().
    # GetNewClosure() would put the callback in a SEPARATE $script: scope, so reads of
    # $script:IsBuilding inside would return $null -- the -not $script:IsBuilding guard
    # below would always fire and the build would never start.
    $script:CurrentBuildClean = $script:ForceCleanBuild
    $script:ForceCleanBuild = $false

    Start-GitSyncBeforeBuild -CommitMessage "Tooling - c120: Sync live project state before build" -OnComplete {
        # Plain scriptblock (NO GetNewClosure) so $script: refs go to the main module.
        param($ok)
        # If the user hit Stop or closed the window during git sync, bail.
        if (-not $script:IsBuilding) {
            Reset-BuildUiState
            return
        }
        if (-not $ok) {
            $script:IsBuilding = $false
            Reset-BuildUiState
            return
        }

        $buildMode = $(if ($script:CurrentBuildClean) { "clean" } else { "incremental" })
        $ui["LblBuildActivity"].Text = "Starting " + $buildMode + " build..."
        $ui["LblProgressText"].Text = "0% - starting " + $buildMode + " build..."
        Add-LogSessionLine "" "#C0C8D2"
        Add-LogSessionLine (">>> BUILD (" + $buildMode + ")") "#0078A8"
        Add-LogSessionLine "" "#C0C8D2"

        $script:BuildVersion = Get-UiVersion
        $script:BuildProcess = $null
        $script:BuildStepQueue.Clear()
        foreach ($s in (Get-BuildSteps $script:BuildVersion $script:CurrentBuildClean)) { [void]$script:BuildStepQueue.Add($s) }
        $script:BuildStepsTotal = $script:BuildStepQueue.Count
        $script:BuildStepsCompleted = 0
        $script:BuildTimer.Start()
    }
}

# ============================================================================
# Section 14a: CLI launcher panel (pillar=Tooling)
#
# Opens a CLI session (Claude Code or Codex) in the project root. The earlier
# prompt-composition workbench (action wraps, kanban cards, prompt box, headless
# mode) was stripped -- the CLI tab is now just the two launch buttons.
# ============================================================================

$script:CliClaudeExe         = $null         # resolved on first use
$script:CliCodexExe          = $null         # resolved on first use
$script:CliPowerShellExe     = $null         # resolved on first use

function Get-CliClaudeExe {
    if ($script:CliClaudeExe) { return $script:CliClaudeExe }
    # Prefer the .cmd shim on Windows (npm install creates both claude and claude.cmd).
    $candidates = @(
        (Join-Path $env:APPDATA "npm\claude.cmd"),
        (Join-Path $env:APPDATA "npm\claude")
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { $script:CliClaudeExe = $c; return $c }
    }
    # Fall back to PATH lookup.
    try {
        $cmd = Get-Command "claude.cmd" -ErrorAction SilentlyContinue
        if (-not $cmd) { $cmd = Get-Command "claude" -ErrorAction SilentlyContinue }
        if ($cmd) { $script:CliClaudeExe = $cmd.Source; return $cmd.Source }
    } catch {}
    return $null
}

function Get-CliCodexExe {
    if ($script:CliCodexExe) { return $script:CliCodexExe }
    # Prefer the .cmd shim on Windows (npm installs create both codex and codex.cmd).
    $candidates = @(
        (Join-Path $env:APPDATA "npm\codex.cmd"),
        (Join-Path $env:APPDATA "npm\codex"),
        (Join-Path $env:LOCALAPPDATA "Programs\Codex\codex.exe"),
        (Join-Path $env:ProgramFiles "Codex\codex.exe")
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { $script:CliCodexExe = $c; return $c }
    }
    # Fall back to PATH lookup.
    try {
        $cmd = Get-Command "codex.cmd" -ErrorAction SilentlyContinue
        if (-not $cmd) { $cmd = Get-Command "codex.exe" -ErrorAction SilentlyContinue }
        if (-not $cmd) { $cmd = Get-Command "codex" -ErrorAction SilentlyContinue }
        if ($cmd) { $script:CliCodexExe = $cmd.Source; return $cmd.Source }
    } catch {}
    return $null
}

function Get-CliPowerShellExe {
    if ($script:CliPowerShellExe) { return $script:CliPowerShellExe }
    $candidates = @(
        "C:\Program Files\PowerShell\7\pwsh.exe",
        (Join-Path $env:ProgramFiles "PowerShell\7\pwsh.exe"),
        (Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe")
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { $script:CliPowerShellExe = $c; return $c }
    }
    try {
        $cmd = Get-Command "pwsh.exe" -ErrorAction SilentlyContinue
        if (-not $cmd) { $cmd = Get-Command "powershell.exe" -ErrorAction SilentlyContinue }
        if ($cmd) { $script:CliPowerShellExe = $cmd.Source; return $cmd.Source }
    } catch {}
    return "powershell.exe"
}

function Invoke-CliLaunch {
    # Open the Claude Code CLI interactively in a new console at the project
    # root. The prompt-composition workbench was removed; this just launches
    # the CLI so Mike can drive it directly.
    #
    # -Ultracode starts the session with ultracode mode ON. Ultracode is a
    # Claude Code session SETTING -- not an --effort level (--effort only takes
    # low/medium/high/xhigh/max) and not an env var -- so we enable it at launch
    # via "--settings". We write {"ultracode": true} to a temp JSON file and
    # pass its PATH (--settings accepts a file or a JSON string), which avoids
    # the fragile inline-JSON quoting that breaks going through cmd.exe.
    param([switch]$Ultracode)

    $claudeExe = Get-CliClaudeExe
    if (-not $claudeExe) {
        Add-LogLine "CLI: claude executable not found. Install with: npm install -g @anthropic-ai/claude-code" "#B81818"
        [System.Windows.MessageBox]::Show("Claude CLI executable not found. Install with:`n`nnpm install -g @anthropic-ai/claude-code", "Claude CLI", "OK", "Warning") | Out-Null
        return
    }

    $extraArgs = ""
    $logLabel  = "claude started in new console"
    if ($Ultracode) {
        $settingsPath = Join-Path $env:TEMP "pd2-devwindow-ultracode-settings.json"
        try {
            # UTF-8 without BOM -- node's JSON.parse chokes on a leading BOM.
            [System.IO.File]::WriteAllText($settingsPath, '{"ultracode": true}', (New-Object System.Text.UTF8Encoding($false)))
        } catch {
            Add-LogLine ("CLI: failed to write ultracode settings file: " + $_.Exception.Message) "#B81818"
            [System.Windows.MessageBox]::Show("Failed to write ultracode settings file: " + $_.Exception.Message, "Claude CLI", "OK", "Warning") | Out-Null
            return
        }
        $extraArgs = " --settings `"" + $settingsPath + "`""
        $logLabel  = "claude started in new console (ultracode on)"
    }

    # cmd /K keeps the console open after claude exits. When passing extra args
    # we wrap the whole command in an outer quote pair so cmd strips only that
    # pair, leaving "claude.cmd" and the quoted settings path intact.
    if ($extraArgs -ne "") {
        $cmdLine = "/K `"`"" + $claudeExe + "`"" + $extraArgs + "`""
    } else {
        $cmdLine = "/K `"" + $claudeExe + "`""
    }
    try {
        Start-Process -FilePath "cmd.exe" `
                      -ArgumentList $cmdLine `
                      -WorkingDirectory $script:ProjectRoot
        Add-LogSessionLine (">>> CLI launch: " + $logLabel + ".") "#0078A8"
    } catch {
        Add-LogLine ("CLI: failed to launch cmd.exe: " + $_.Exception.Message) "#B81818"
        [System.Windows.MessageBox]::Show("Failed to launch console: " + $_.Exception.Message, "Claude CLI", "OK", "Warning") | Out-Null
    }
}

function Invoke-CliLaunchCodexAdmin {
    $codexExe = Get-CliCodexExe
    if (-not $codexExe) {
        Add-LogLine "CLI: codex executable not found. Install Codex CLI and make sure codex is on PATH." "#B81818"
        [System.Windows.MessageBox]::Show(
            "Codex CLI executable not found. Install Codex CLI and make sure codex is on PATH.",
            "Codex CLI",
            "OK",
            "Warning") | Out-Null
        return
    }

    $psExe = Get-CliPowerShellExe
    $safeHistory = Join-Path $env:TEMP "pd2-codex-admin-psreadline-history.txt"
    $startupCommand = @(
        "`$env:TERM = 'xterm-256color'",
        "`$env:COLORTERM = 'truecolor'",
        "try { Import-Module PSReadLine -ErrorAction SilentlyContinue; Set-PSReadLineOption -HistorySavePath `"$safeHistory`" -ErrorAction SilentlyContinue } catch {}",
        "Set-Location -LiteralPath `"$script:ProjectRoot`"",
        "& `"$codexExe`""
    ) -join "; "
    $encodedStartupCommand = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($startupCommand))

    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $psExe
        $psi.Arguments = "-NoLogo -NoProfile -NoExit -ExecutionPolicy Bypass -EncodedCommand " + $encodedStartupCommand
        $psi.WorkingDirectory = $script:ProjectRoot
        $psi.UseShellExecute = $true
        $psi.Verb = "runas"
        [System.Diagnostics.Process]::Start($psi) | Out-Null
        Add-LogSessionLine ">>> CLI launch: Codex admin PowerShell console requested." "#0078A8"
    } catch {
        Add-LogLine ("CLI: failed to launch Codex admin console: " + $_.Exception.Message) "#B81818"
        [System.Windows.MessageBox]::Show(
            "Failed to launch Codex CLI as administrator: " + $_.Exception.Message,
            "Codex CLI",
            "OK",
            "Warning") | Out-Null
    }
}

# ============================================================================
# Section 15: Release pipeline
# ============================================================================

function Start-PushRelease {
    if ($script:IsPushing -or $script:IsBuilding) { return }
    $releaseScript = Join-Path (Join-Path $script:ProjectRoot "devtools") "release.ps1"
    if (-not (Test-Path $releaseScript)) {
        [System.Windows.MessageBox]::Show("release.ps1 not found in devtools/.", "Release Error", "OK", "Warning") | Out-Null
        return
    }
    if (-not $script:GhCliAvailable -or -not $script:GhAuthOk) {
        $go = [System.Windows.MessageBox]::Show(
            "GitHub CLI (gh) is missing or you are not logged in.`n`n" +
            "The release pipeline will fail when pushing to GitHub.`n`n" +
            "Continue anyway (local build + package still run), or Cancel to install gh / run gh auth login first?",
            "GitHub authentication",
            "YesNo",
            "Warning")
        if ($go -ne [System.Windows.MessageBoxResult]::Yes) { return }
    }
    $ver = Get-UiVersion
    $vs = "" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch
    $isStable = $ui["ChkStable"].IsChecked
    $kind = $(if ($isStable) { "Stable" } else { "Dev" })
    $msg = "Release v" + $vs + " (" + $kind + ")?`n`nThis will:`n1. Set version to " + $vs + " in CMakeLists.txt`n2. Build client + updater`n3. Package and push to GitHub"
    $ok = [System.Windows.MessageBox]::Show($msg, ($kind + " Release v" + $vs), "YesNo", "Warning")
    if ($ok -ne [System.Windows.MessageBoxResult]::Yes) { return }

    # Set guard flag up front so double-click/keyboard guards see consistent state.
    $script:IsPushing = $true
    $script:NinjaCurrent = 0; $script:NinjaTotal = 0
    $script:BuildStepsTotal = 0; $script:BuildStepsCompleted = 0

    $ui["BtnBuild"].IsEnabled = $false; $ui["BtnRelease"].IsEnabled = $false; $ui["BtnCleanBuild"].IsEnabled = $false
    $ui["BtnPull"].IsEnabled = $false
    $ui["BtnPush"].IsEnabled = $false
    $ui["BtnPruneWorktrees"].IsEnabled = $false
    $ui["ProgressBack"].Visibility = [System.Windows.Visibility]::Visible
    $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Visible
    $ui["LblBuildActivity"].Text = "Git: syncing..."
    $ui["LblProgressText"].Text = "Git: syncing..."
    $ui["ProgressFill"].Background = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))
    $ui["ProgressFill"].Width = 0
    $ui["ProgressBack"].UpdateLayout()
    $pw0r = $ui["ProgressBack"].ActualWidth
    if ($pw0r -gt 0) { $ui["ProgressFill"].Width = [math]::Floor($pw0r * 0.12) }

    $script:HasBuildErrors = $false; $script:AllOutput.Clear()
    $script:ClientErrors.Clear(); $script:ServerErrors.Clear()

    # Stash release params in $script: vars instead of capturing via GetNewClosure().
    # GetNewClosure() would put the callback in a SEPARATE $script: scope -- reads of
    # $script:IsPushing would return $null and the release would never start.
    $script:PendingReleaseVer      = $ver
    $script:PendingReleaseVs       = $vs
    $script:PendingReleaseKind     = $kind
    $script:PendingReleaseIsStable = $isStable
    $script:PendingReleaseScript   = $releaseScript

    Start-GitSyncBeforeBuild -CommitMessage "Tooling - c120: Sync live project state before release" -OnComplete {
        # Plain scriptblock (NO GetNewClosure) so $script: refs go to the main module.
        param($ok)
        # If the user hit Stop or closed the window during git sync, bail.
        if (-not $script:IsPushing) {
            Reset-BuildUiState
            return
        }
        if (-not $ok) {
            $script:IsPushing = $false
            Reset-BuildUiState
            return
        }

        $relVer      = $script:PendingReleaseVer
        $relVs       = $script:PendingReleaseVs
        $relKind     = $script:PendingReleaseKind
        $relIsStable = $script:PendingReleaseIsStable
        $relScript   = $script:PendingReleaseScript

        Set-ProjectVersion $relVer.Major $relVer.Minor $relVer.Patch
        $ui["LblBuildActivity"].Text = "Release v" + $relVs + ": building..."
        $ui["LblProgressText"].Text = "0% - release v" + $relVs + " (starting steps...)"

        $script:ClientBuildResult = $null; $script:ServerBuildResult = $null
        $script:BuildStepQueue.Clear()
        $ui["LblClientStatus"].Text = "client: building..."
        $ui["BtnCopyLog"].Visibility = [System.Windows.Visibility]::Visible

        $script:BuildVersion = $relVer
        Add-LogSessionLine "" "#C0C8D2"
        Add-LogSessionLine (">>> RELEASE PIPELINE v" + $relVs + " (" + $relKind + ")") "#A06A10"
        Add-LogSessionLine "    Version written to CMakeLists.txt; steps below run in order (build, then package/push)." "#44586C"
        Add-LogSessionLine "" "#C0C8D2"

        # Audit 2026-04-16: release builds now do a clean build to match v1 behavior.
        # Stale object files from a previous broken build can otherwise link into
        # the release binary.
        foreach ($s in (Get-BuildSteps $relVer $true)) { [void]$script:BuildStepQueue.Add($s) }

        $prerelArg = $(if ($relIsStable) { "" } else { " -Prerelease" })
        $psExe = $(if (Get-Command pwsh -ErrorAction SilentlyContinue) { "pwsh.exe" } else { "powershell.exe" })
        # Audit 2026-04-16: switched from -File to -Command to match v1. With -File,
        # switch parameters like `-SkipPush:$false` are parsed as a literal string
        # and can be misinterpreted; -Command evaluates the expression. Also adding
        # -NonInteractive so a credential/auth prompt doesn't hang the subprocess.
        # -SkipBuild: dev-window-v2 already built both targets above; release.ps1 skips cmake step 0.
        # -SkipPush:$false: always push to GitHub (not skipped).
        $relCmd = "& `"$relScript`" -Version `"$relVs`"$prerelArg -SkipBuild -SkipPush:`$false"
        [void]$script:BuildStepQueue.Add(@{
            Name   = "Release: packaging + GitHub push"
            Exe    = $psExe
            Target = "client"
            Args   = "-NonInteractive -ExecutionPolicy Bypass -Command `"$relCmd`""
        })
        $script:BuildStepsTotal = $script:BuildStepQueue.Count
        $script:BuildStepsCompleted = 0
        $script:BuildTimer.Start()
    }
}

# ============================================================================
# Section 16: Game / Server launch
# ============================================================================

# Toggle-Server removed (2026-04-27): dedicated server is no longer shipped;
# connectivity moved into the client via in-process listen-host mode.

function Toggle-Game {
    if ($null -ne $script:GameProcess -and -not $script:GameProcess.HasExited) {
        $ui["BtnRunGame"].IsEnabled = $false
        $ui["BtnRunGame"].Content = "STOPPING..."
        try { $script:GameProcess.Kill() } catch {}
        $script:GameProcess = $null
        $ui["BtnRunGame"].Content = "RUN GAME"
        $ui["BtnRunGame"].IsEnabled = $true
        return
    }
    $exe = Get-ExePath $script:ClientExeName
    if ($null -eq $exe) {
        [System.Windows.MessageBox]::Show("Game executable not found. Build first.", "Run Error", "OK", "Warning") | Out-Null
        return
    }
    $ui["BtnRunGame"].IsEnabled = $false
    $ui["BtnRunGame"].Content = "STARTING..."
    Add-LogLine (">>> Starting game: " + $exe) "#0078A8"
    Ensure-ExecutableRuntimeDlls $exe "game"
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $exe
        $psi.WorkingDirectory = (Split-Path $exe -Parent)
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.CreateNoWindow = $true
        $psi.EnvironmentVariables["PATH"]         = Get-ChildProcessPathEnv
        $psi.EnvironmentVariables["MSYSTEM"]      = "MINGW64"
        $psi.EnvironmentVariables["MINGW_PREFIX"] = "/mingw64"
        $psi.EnvironmentVariables["TEMP"]         = $env:TEMP
        $psi.EnvironmentVariables["TMP"]          = $env:TMP
        $proc = New-Object System.Diagnostics.Process
        $proc.StartInfo = $psi
        $previousErrorMode = Set-ChildLaunchNoLoaderDialogs
        try {
            [void]$proc.Start()
        } finally {
            Restore-ChildLaunchErrorMode $previousErrorMode
        }
        $script:GameProcess = $proc
        [PD2V2.AsyncLineReader]::StartReading($proc.StandardOutput, $script:GameOutputQueue, "OUT:")
        [PD2V2.AsyncLineReader]::StartReading($proc.StandardError,  $script:GameOutputQueue, "ERR:")
        $ui["BtnRunGame"].Content = "STOP GAME"
    } catch {
        Add-LogLine ("Game launch failed: " + $_.Exception.Message) "#B81818"
        $ui["BtnRunGame"].Content = "RUN GAME"
        [System.Windows.MessageBox]::Show("Game launch failed: " + $_.Exception.Message, "Run Error", "OK", "Error") | Out-Null
    } finally {
        $ui["BtnRunGame"].IsEnabled = $true
    }
}

# ============================================================================
# Run Tests pipeline (pd-tests target -- self-contained Catch2 runner)
# ============================================================================
#
# Async pattern: build (if needed) + execute happen on the BgPool. The UI
# stays interactive throughout. Output streams into the Log tab via
# TestsOutputQueue (drained by StatusModeTimer at 500 ms). When the runner
# exits, a status MessageBox surfaces pass/fail counts.

$script:TestsBuildBusy = $false
$script:TestsLastSummary = ""
$script:TestsPassed = 0
$script:TestsFailed = 0
$script:TestsAssertions = 0
# Per-run scan buffer for the tests pipeline. Drain-ProcessOutputQueues
# appends every "[tests] ..." line here in addition to writing it to the
# Log tab. Build's $script:AllOutput is used by Copy Log / Export and
# starts/ends per BUILD, so we don't want test lines to pollute it. The
# watchdog parser scans this buffer for the Catch2 summary lines.
$script:TestsScanBuffer = [System.Collections.ArrayList]::new()

function Stop-RunTests {
    if ($null -ne $script:TestsProcess -and -not $script:TestsProcess.HasExited) {
        try { $script:TestsProcess.Kill() } catch {}
    }
    $script:TestsProcess = $null
    $script:TestsRunning = $false
    $ui["BtnRunTests"].Content = "RUN TESTS"
    $ui["BtnRunTests"].IsEnabled = $true
}

function Toggle-Tests {
    # Click while running = stop. Otherwise = run.
    if ($script:TestsRunning) {
        Add-LogSessionLine ">>> tests: STOP requested" "#A07810"
        Stop-RunTests
        return
    }
    if ($script:IsBuilding -or $script:IsPushing) {
        [System.Windows.MessageBox]::Show("Wait for the current build or release to finish.", "Run Tests", "OK", "Information") | Out-Null
        return
    }
    if ($script:TestsBuildBusy) {
        [System.Windows.MessageBox]::Show("Tests are already being prepared.", "Run Tests", "OK", "Information") | Out-Null
        return
    }
    Start-RunTests
}

function Start-RunTests {
    $script:TestsRunning = $true
    $script:TestsLastSummary = ""
    $script:TestsPassed = 0
    $script:TestsFailed = 0
    $script:TestsAssertions = 0
    [void]$script:TestsScanBuffer.Clear()
    $ui["BtnRunTests"].Content = "PREPARING..."
    $ui["BtnRunTests"].IsEnabled = $false
    $ui["LblServerStatus"].Text = "tests: preparing..."
    $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))
    Add-LogSessionLine "" "#C0C8D2"
    Add-LogSessionLine ">>> RUN TESTS (pd-tests)" "#A06A10"
    Add-LogSessionLine "" "#C0C8D2"

    # Make sure the Log tab is visible so the user sees streaming output
    # without needing to switch manually.
    try { $ui["TabControl"].SelectedIndex = 1 } catch {}

    $testsExe = Join-Path $script:BuildDir "pd-tests.exe"
    $needsBuild = -not (Test-Path -LiteralPath $testsExe)

    if ($needsBuild) {
        $ui["LblServerStatus"].Text = "tests: building..."
        Build-Tests-Then-Run
    } else {
        $ui["LblServerStatus"].Text = "tests: starting..."
        Run-Tests-Process
    }
}

function Build-Tests-Then-Run {
    $script:TestsBuildBusy = $true
    $ui["BtnRunTests"].Content = "BUILDING TESTS..."
    Add-LogSessionLine "tests: building pd-tests target..." "#0078A8"

    $cmakeExe = $script:CMake
    $buildDir = $script:BuildDir
    $projectRoot = $script:ProjectRoot
    $cc = $script:CC
    $cxx = $script:CXX
    $py = $script:Python
    $ver = Get-ProjectVersion
    $needsConfigure = Test-NeedsConfigure $buildDir $ver
    $cores = $(if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { "4" })
    $versionMajor = [int]$ver.Major
    $versionMinor = [int]$ver.Minor
    $versionPatch = [int]$ver.Patch

    Start-AsyncPoolAction `
        -Script {
            param($cmakeExe, $buildDir, $projectRoot, $cc, $cxx, $py, $needsConfigure, $cores, $versionMajor, $versionMinor, $versionPatch)
            $output = New-Object System.Collections.ArrayList
            try {
                if ($needsConfigure) {
                    $cfg = & $cmakeExe -G Ninja `
                        "-DCMAKE_C_COMPILER=$cc" `
                        "-DCMAKE_CXX_COMPILER=$cxx" `
                        "-DCMAKE_C_COMPILER_FORCED=TRUE" `
                        "-DCMAKE_CXX_COMPILER_FORCED=TRUE" `
                        "-DPD_PYTHON_EXECUTABLE=$py" `
                        "-DVERSION_SEM_MAJOR=$versionMajor" `
                        "-DVERSION_SEM_MINOR=$versionMinor" `
                        "-DVERSION_SEM_PATCH=$versionPatch" `
                        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY" `
                        "-DCMAKE_C_COMPILER_LAUNCHER=ccache" `
                        "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache" `
                        -B $buildDir -S $projectRoot 2>&1
                    $cfgCode = $LASTEXITCODE
                    foreach ($l in $cfg) { [void]$output.Add(@{ Text = "$l"; Color = "#4A5868" }) }
                    if ($cfgCode -ne 0) {
                        return [PSCustomObject]@{ Ok = $false; Output = $output; Err = "configure failed" }
                    }
                } else {
                    [void]$output.Add(@{ Text = "tests: configure skipped (cache current)."; Color = "#44586C" })
                }
                # Build pd-tests target.
                $bld = & $cmakeExe --build $buildDir --target pd-tests --parallel $cores 2>&1
                $bldCode = $LASTEXITCODE
                foreach ($l in $bld) {
                    $color = "#4A5868"
                    if ("$l" -match '(?i)\berror\b|FAILED|undefined reference') { $color = "#B81818" }
                    elseif ("$l" -match '(?i)\bwarning\b') { $color = "#B86810" }
                    [void]$output.Add(@{ Text = "$l"; Color = $color })
                }
                if ($bldCode -ne 0) {
                    return [PSCustomObject]@{ Ok = $false; Output = $output; Err = "pd-tests build failed (exit $bldCode)" }
                }
                return [PSCustomObject]@{ Ok = $true; Output = $output; Err = $null }
            } catch {
                [void]$output.Add(@{ Text = $_.Exception.Message; Color = "#B81818" })
                return [PSCustomObject]@{ Ok = $false; Output = $output; Err = $_.Exception.Message }
            }
        } `
        -Arguments @($cmakeExe, $buildDir, $projectRoot, $cc, $cxx, $py, $needsConfigure, $cores, $versionMajor, $versionMinor, $versionPatch) `
        -OnComplete {
            param($result)
            $script:TestsBuildBusy = $false
            $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
            if ($null -eq $r) {
                Add-LogSessionLine "tests: build returned no result" "#B81818"
                Stop-RunTests
                return
            }
            foreach ($entry in $r.Output) {
                if ($null -ne $entry) {
                    try { Add-LogLine $entry.Text $entry.Color } catch {}
                }
            }
            if (-not $r.Ok) {
                Add-LogSessionLine ("tests: " + $r.Err) "#B81818"
                $ui["LblServerStatus"].Text = "tests: build FAILED"
                $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#B81818")))
                [System.Windows.MessageBox]::Show("pd-tests build failed.`n`n" + $r.Err + "`n`nSee Log tab for details.", "Run Tests", "OK", "Error") | Out-Null
                Stop-RunTests
                return
            }
            Add-LogSessionLine "tests: build OK; running..." "#0078A8"
            $ui["LblServerStatus"].Text = "tests: starting..."
            Run-Tests-Process
        }
}

function Run-Tests-Process {
    $exe = Join-Path $script:BuildDir "pd-tests.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        Add-LogSessionLine ("tests: executable not found at " + $exe) "#B81818"
        [System.Windows.MessageBox]::Show("pd-tests.exe not found after build at:`n" + $exe, "Run Tests", "OK", "Error") | Out-Null
        Stop-RunTests
        return
    }
    Ensure-ExecutableRuntimeDlls $exe "tests"
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $exe
        # Verbose per-case run (S477, Mike's ask): -s prints every assertion
        # (the result that made each case pass/fail), -d yes prints per-case
        # durations. Final Catch2 summary still appears at end and is parsed
        # by the watchdog. -r console makes the reporter explicit; default
        # would also pick console but be explicit so future Catch2 default
        # changes don't silently shift the format the parser expects.
        $psi.Arguments = "-r console -s -d yes"
        $psi.WorkingDirectory = (Split-Path $exe -Parent)
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.CreateNoWindow = $true
        $psi.EnvironmentVariables["PATH"]         = Get-ChildProcessPathEnv
        $psi.EnvironmentVariables["MSYSTEM"]      = "MINGW64"
        $psi.EnvironmentVariables["MINGW_PREFIX"] = "/mingw64"
        $psi.EnvironmentVariables["TEMP"]         = $env:TEMP
        $psi.EnvironmentVariables["TMP"]          = $env:TMP
        $proc = New-Object System.Diagnostics.Process
        $proc.StartInfo = $psi
        $previousErrorMode = Set-ChildLaunchNoLoaderDialogs
        try {
            [void]$proc.Start()
        } finally {
            Restore-ChildLaunchErrorMode $previousErrorMode
        }
        $script:TestsProcess = $proc
        [PD2V2.AsyncLineReader]::StartReading($proc.StandardOutput, $script:TestsOutputQueue, "OUT:")
        [PD2V2.AsyncLineReader]::StartReading($proc.StandardError,  $script:TestsOutputQueue, "ERR:")
        $ui["BtnRunTests"].Content = "STOP TESTS"
        $ui["BtnRunTests"].IsEnabled = $true
        $ui["LblServerStatus"].Text = "tests: running..."
        $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))
        Add-LogSessionLine ("tests: started " + $exe) "#0078A8"
        # Poll for completion on the dispatcher; final summary surfaces in the
        # MessageBox after the queues drain.
        $watchdog = New-Object System.Windows.Threading.DispatcherTimer
        $watchdog.Interval = [TimeSpan]::FromMilliseconds(500)
        $watchdog.Add_Tick({
            try {
                if ($null -eq $script:TestsProcess) { $this.Stop(); return }
                if (-not $script:TestsProcess.HasExited) { return }
                # Drain remaining output before computing summary.
                if (-not $script:TestsOutputQueue.IsEmpty) { return }
                $this.Stop()
                $code = $script:TestsProcess.ExitCode
                $script:TestsProcess = $null
                # Parse Catch2 summary. Two shapes are emitted by the
                # default "console" reporter:
                #   (a) all-passed:   "All tests passed (1881 assertions in 155 test cases)"
                #   (b) with-failures:
                #         "test cases: 12 | 10 passed | 2 failed"
                #         "assertions: 4080 | 4078 passed | 2 failed"
                # Scan the per-run TestsScanBuffer (NOT $script:AllOutput,
                # which is the build pipeline's buffer and is not populated
                # by the tests drain path).
                $cases = 0; $passed = 0; $failed = 0; $assertions = 0
                foreach ($line in $script:TestsScanBuffer) {
                    if ($line -match 'All tests passed.*?\((\d+)\s+assertion(?:s)?\s+in\s+(\d+)\s+test case(?:s)?\)') {
                        $assertions = [int]$Matches[1]
                        $cases      = [int]$Matches[2]
                        $passed     = $cases
                        $failed     = 0
                    } elseif ($line -match '^\s*test cases:\s*(\d+)\s*\|\s*(\d+)\s+passed(?:\s*\|\s*(\d+)\s+failed)?') {
                        $cases  = [int]$Matches[1]
                        $passed = [int]$Matches[2]
                        if ($Matches.Count -ge 4 -and $Matches[3]) { $failed = [int]$Matches[3] }
                    } elseif ($line -match '^\s*assertions:\s*(\d+)\s*\|') {
                        $assertions = [int]$Matches[1]
                    }
                }
                $script:TestsPassed     = $passed
                $script:TestsFailed     = $failed
                $script:TestsAssertions = $assertions
                $script:TestsRunning    = $false
                $ui["BtnRunTests"].Content = "RUN TESTS"
                $ui["BtnRunTests"].IsEnabled = $true

                # Compose status + message-box text. Show whichever counts we
                # were able to extract; fall back to "exit 0" if Catch2's
                # summary was missing entirely.
                $countsBlurb = if ($cases -gt 0 -or $assertions -gt 0) {
                    "$cases case$(if ($cases -ne 1) {'s'}), $assertions assertion$(if ($assertions -ne 1) {'s'})"
                } else {
                    "no Catch2 summary found"
                }
                $okMsg = "pd-tests exit $code`n`n"
                $okMsg += "Cases: $cases   Passed: $passed   Failed: $failed`n"
                $okMsg += "Assertions: $assertions"

                $greenBrush = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#10783A"))
                $redBrush   = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#B81818"))
                if ($code -eq 0 -and $failed -eq 0) {
                    Add-LogSessionLine ("tests: PASSED (" + $countsBlurb + ")") "#10783A"
                    $ui["LblServerStatus"].Text = "tests: PASSED ($countsBlurb)"
                    $ui["LblServerStatus"].Foreground = $greenBrush
                    [System.Windows.MessageBox]::Show($okMsg, "Run Tests -- PASSED", "OK", "Information") | Out-Null
                } else {
                    Add-LogSessionLine ("tests: FAILED (exit " + $code + ", " + $failed + " failure[s] of " + $cases + " case[s])") "#B81818"
                    $ui["LblServerStatus"].Text = "tests: FAILED ($failed of $cases failed)"
                    $ui["LblServerStatus"].Foreground = $redBrush
                    [System.Windows.MessageBox]::Show($okMsg + "`n`nSee Log tab for failure details.", "Run Tests -- FAILED", "OK", "Warning") | Out-Null
                }
            } catch {}
        })
        $watchdog.Start()
    } catch {
        Add-LogSessionLine ("tests: launch failed " + $_.Exception.Message) "#B81818"
        [System.Windows.MessageBox]::Show("pd-tests launch failed: " + $_.Exception.Message, "Run Tests", "OK", "Error") | Out-Null
        Stop-RunTests
    }
}

function Drain-ProcessOutputQueues {
    # Pull up to N lines per tick from game/tests so streaming feels responsive
    # without blocking the UI thread when a noisy process floods stdout. Called
    # from StatusModeTimer (500 ms).
    $maxPer = 60; $line = $null
    $count = 0
    while ($count -lt $maxPer -and $script:GameOutputQueue.TryDequeue([ref]$line)) {
        $text = if ($line.Length -ge 4) { $line.Substring(4) } else { $line }
        $cls = if ($line.StartsWith("ERR:")) { "error" } else { Classify-Line $text }
        Add-LogLine ("[game] " + $text) (Get-ClassifiedLogColor $cls)
        $count++
    }
    $count = 0
    while ($count -lt $maxPer -and $script:TestsOutputQueue.TryDequeue([ref]$line)) {
        $text = if ($line.Length -ge 4) { $line.Substring(4) } else { $line }
        $cls = if ($line.StartsWith("ERR:")) { "error" } else { Classify-Line $text }
        Add-LogLine ("[tests] " + $text) (Get-ClassifiedLogColor $cls)
        # Feed the raw test line (without "[tests] " prefix) into the scan
        # buffer so the watchdog parser can find Catch2's summary.
        if ($null -ne $script:TestsScanBuffer) {
            [void]$script:TestsScanBuffer.Add($text)
        }
        $count++
    }
}

function Update-RunButtons {
    if ($null -ne $script:GameProcess -and $script:GameProcess.HasExited) {
        $script:GameProcess = $null
        $ui["BtnRunGame"].Content = "RUN GAME"
    }
    # Tests-process completion is handled by Finish-RunTests when its drain
    # cycle catches HasExited; nothing to do here.
}

# ============================================================================
# Section 17: Event wiring
# ============================================================================

$ui["BtnBuild"].Add_Click({ Start-Build })
$ui["BtnRelease"].Add_Click({ Start-PushRelease })
$ui["BtnStop"].Add_Click({ Stop-Build })
$ui["BtnRunGame"].Add_Click({ Toggle-Game })
$ui["BtnRunTests"].Add_Click({ Toggle-Tests })

$ui["BtnCopyErrors"].Add_Click({
    try {
        $errs = ($script:ClientErrors + $script:ServerErrors) -join "`n"
        [System.Windows.Clipboard]::SetText($errs)
    } catch {}
})
$ui["BtnCopyLog"].Add_Click({
    try {
        $log = $script:AllOutput -join "`n"
        [System.Windows.Clipboard]::SetText($log)
    } catch {}
})

$ui["BtnCheck"].Add_Click({
    # Perf: git + bash run on BgPool so the UI thread stays responsive while
    # git-snapshot.sh runs (which can take seconds on a cold Git-for-Windows
    # process).
    if ($script:GitActionBusy) {
        [System.Windows.MessageBox]::Show("Another git action is in progress.", "Pre-Build Check", "OK", "Information") | Out-Null
        return
    }
    $script:GitActionBusy = $true
    $ui["BtnCheck"].IsEnabled = $false
    Add-LogLine ">>> check: git status + git-snapshot.sh" "#0078A8"

    Start-AsyncPoolAction `
        -Script {
            param($root)
            try {
                $st = git -C $root status --porcelain 2>$null
                $cnt = if ($st) { @($st).Count } else { 0 }
                $snap = Join-Path $root "devtools\git-snapshot.sh"
                $snapOut = $null
                if (Test-Path $snap) {
                    try {
                        $snapOut = (bash $snap 2>&1 | Out-String)
                    } catch {
                        $snapOut = "git-snapshot.sh failed: " + $_.Exception.Message
                    }
                }
                [PSCustomObject]@{ Dirty = $cnt; SnapOut = $snapOut; HasSnap = (Test-Path $snap) }
            } catch {
                [PSCustomObject]@{ Dirty = -1; SnapOut = $_.Exception.Message; HasSnap = $false }
            }
        } `
        -Arguments @($script:ProjectRoot) `
        -OnComplete {
            param($result)
            try {
                $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
                $msg = ""
                if ($null -ne $r) {
                    if ($r.Dirty -lt 0) {
                        $msg = "Check failed: " + $r.SnapOut
                    } elseif ($r.Dirty -eq 0) {
                        $msg = "Git state: CLEAN (no uncommitted changes)`n"
                    } else {
                        $msg = "Git state: DIRTY (" + $r.Dirty + " uncommitted files)`n"
                    }
                    if ($r.HasSnap) {
                        $msg = $msg + "`ngit-snapshot.sh output:`n" + ($r.SnapOut -replace "`r", "")
                    } else {
                        $msg = $msg + "git-snapshot.sh not found (optional)."
                    }
                }
                [System.Windows.MessageBox]::Show($msg, "Pre-Build Check", "OK", "Information") | Out-Null
            } catch {}
            $script:GitActionBusy = $false
            $ui["BtnCheck"].IsEnabled = $true
        }
})

$ui["BtnCleanBuild"].Add_Click({
    $script:ForceCleanBuild = $true
    Start-Build
})

$ui["BtnOpenGitHub"].Add_Click({
    try {
        $repo = $script:Settings.GitHubRepo
        if (-not $repo -or $repo -eq "") { $repo = "https://github.com/MikeHazeJr/perfect-dark-2" }
        Start-Process $repo
    } catch {}
})

$ui["BtnOpenFolder"].Add_Click({
    try { Start-Process "explorer.exe" $script:ProjectRoot } catch {}
})

$ui["BtnOpenKanban"].Add_Click({ Invoke-OpenKanban })
$ui["BtnStartKanbanServer"].Add_Click({ Invoke-StartKanbanRemote })
$ui["BtnStopKanbanServer"].Add_Click({ Invoke-StopKanbanRemote })

$ui["BtnPull"].Add_Click({ Invoke-GitPull })
$ui["BtnPush"].Add_Click({ Invoke-GitPush })
$ui["BtnPruneWorktrees"].Add_Click({ Invoke-GitPruneWorktrees })

$ui["LblAuthStatus"].Cursor = [System.Windows.Input.Cursors]::Hand
$ui["LblAuthStatus"].Add_MouseLeftButtonDown({ Invoke-GhAuthHelp })
$ui["StatusAuth"].Cursor = [System.Windows.Input.Cursors]::Hand
$ui["StatusAuth"].Add_MouseLeftButtonDown({ Invoke-GhAuthHelp })

# --- CLI launcher panel event wiring -----------------------------------------
$ui["BtnCliLaunch"].Add_Click({ Invoke-CliLaunch })
$ui["BtnCliLaunchUltra"].Add_Click({ Invoke-CliLaunch -Ultracode })
$ui["BtnCliLaunchCodex"].Add_Click({ Invoke-CliLaunchCodexAdmin })

# ============================================================================
# Section 18: Timers (WPF DispatcherTimer)
# ============================================================================

# Build timer (100ms) -- drains async output, tracks progress
$script:BuildTimer = New-Object System.Windows.Threading.DispatcherTimer
$script:BuildTimer.Interval = [TimeSpan]::FromMilliseconds(100)
$script:BuildTimer.Add_Tick({
    try {
        if ($null -eq $script:BuildProcess) {
            if ($script:BuildStepQueue.Count -gt 0 -and ($script:IsBuilding -or $script:IsPushing)) {
                $next = $script:BuildStepQueue[0]; $script:BuildStepQueue.RemoveAt(0)
                if ($next.Target -eq "server") {
                    $ui["LblServerStatus"].Text = "server: building..."
                    $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))
                }
                Add-LogSessionLine "" "#C0C8D2"
                Add-LogSessionLine (">>> " + $next.Name) "#0078A8"
                if ($next.Name -match "Release") {
                    Add-LogSessionLine "    Packaging / gh release upload may print little until GitHub responds (30-90s is normal)." "#44586C"
                }
                Add-LogSessionLine "" "#C0C8D2"
                Start-Build-Step $next
            } else {
                $script:BuildTimer.Stop()
            }
            return
        }

        $maxPer = 80; $count = 0; $line = $null
        while ($count -lt $maxPer -and $script:OutputQueue.TryDequeue([ref]$line)) {
            $text = $line.Substring(4)
            [void]$script:AllOutput.Add($text)
            $cls = Classify-Line $text
            if ($cls -eq "error") {
                $script:HasBuildErrors = $true
                if ($script:CurrentBuildTarget -eq "server") { [void]$script:ServerErrors.Add($text) }
                else { [void]$script:ClientErrors.Add($text) }
                $ui["ProgressFill"].Background = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#B81818")))
            }
            Add-LogLine $text (Get-ClassifiedLogColor $cls)

            # CMake --build prefix form: "[50%] Building X..."
            if ($text -match '^\[\s*(\d+)%\]') {
                $pct = [int]$Matches[1]
                if ($pct -ge $script:BuildPercent) {
                    $script:BuildPercent = $pct
                    $pw = $ui["ProgressBack"].ActualWidth
                    if ($pw -gt 0) {
                        $fw = [math]::Floor(($pct / 100.0) * $pw)
                        $ui["ProgressFill"].Width = $fw
                    }
                    $ui["LblProgressText"].Text = "" + $pct + "% - " + $script:CurrentStepName
                }
            }
            # Ninja --build emits "[N/total] Compiling X.cpp". Track and convert to percent so the
            # progress bar moves live during the long build step (previously stuck at 0% the whole time).
            elseif ($text -match '^\s*\[(\d+)/(\d+)\]') {
                $n = [int]$Matches[1]; $tot = [int]$Matches[2]
                if ($tot -gt 0) {
                    $script:NinjaCurrent = $n; $script:NinjaTotal = $tot
                    $pct = [int]([math]::Floor(100.0 * $n / $tot))
                    if ($pct -ge $script:BuildPercent) {
                        $script:BuildPercent = $pct
                        $pw = $ui["ProgressBack"].ActualWidth
                        if ($pw -gt 0) {
                            $fw = [math]::Floor(($pct / 100.0) * $pw)
                            $ui["ProgressFill"].Width = $fw
                        }
                        $ui["LblProgressText"].Text = "[$n/$tot] " + $pct + "% - " + $script:CurrentStepName
                    }
                }
            }
            $script:LastOutputTime = [DateTime]::Now; $count++
        }

        if ($null -ne $script:BuildProcess -and -not $script:BuildProcess.HasExited) {
            $el  = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
            $sil = [math]::Floor(([DateTime]::Now - $script:LastOutputTime).TotalSeconds)
            if ($sil -gt 2) {
                $spin = $script:SpinnerChars[$script:SpinnerIndex % 4]; $script:SpinnerIndex++
                $hint = ""
                if ($script:CurrentStepName -match "Release" -and $sil -gt 30) {
                    $hint = "  [gh upload -- see Log tab]"
                }
                $ui["LblBuildActivity"].Text = $script:CurrentStepName + " " + $spin + " " + $el + "s" + $hint
                if ($script:BuildPercent -eq 0) {
                    $ui["LblProgressText"].Text = "0% - " + $script:CurrentStepName + " " + $spin + " " + $el + "s"
                }
                if ($script:CurrentStepName -match "Release" -and $sil -ge 12) {
                    $sinceHb = ([DateTime]::Now - $script:LastReleaseHeartbeat).TotalSeconds
                    if ($sinceHb -ge 12) {
                        $script:LastReleaseHeartbeat = [DateTime]::Now
                        $hb = "[" + (Get-Date -Format "HH:mm:ss") + "] Still running: " + $script:CurrentStepName + " (" + $el + "s elapsed, last output " + $sil + "s ago)"
                        [void]$script:AllOutput.Add($hb)
                        Add-LogLine $hb (Get-ClassifiedLogColor (Classify-Line $hb))
                    }
                }
            } else {
                $ui["LblBuildActivity"].Text = $script:CurrentStepName + " (" + $el + "s)"
                if ($script:BuildPercent -eq 0) {
                    $ui["LblProgressText"].Text = "0% - " + $script:CurrentStepName + " (" + $el + "s)"
                }
            }
            return
        }

        if ($null -ne $script:BuildProcess -and $script:BuildProcess.HasExited -and $script:OutputQueue.IsEmpty) {
            $script:BuildTimer.Stop()
            $exitCode = $script:BuildProcess.ExitCode
            $elapsed  = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
            try { $script:BuildProcess.Dispose() } catch {}
            $script:BuildProcess = $null
            $script:BuildStepsCompleted = $script:BuildStepsCompleted + 1

            $greenBrush = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#10783A"))
            $redBrush   = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#B81818"))
            $dimBrush   = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#4A5868"))

            if ($exitCode -ne 0) {
                if ($script:CurrentBuildTarget -eq "client") {
                    $script:ClientBuildResult = "FAILED"; $script:ClientBuildTime = $elapsed
                    $ui["LblClientStatus"].Text = "client: FAILED (" + (Format-ElapsedTime $elapsed) + ")"
                    $ui["LblClientStatus"].Foreground = $redBrush
                } else {
                    $script:ServerBuildResult = "FAILED"; $script:ServerBuildTime = $elapsed
                    $ui["LblServerStatus"].Text = "server: FAILED (" + (Format-ElapsedTime $elapsed) + ")"
                    $ui["LblServerStatus"].Foreground = $redBrush
                }
                # Abort the rest of the pipeline. Client configure/build and server build share one
                # Ninja build dir; keeping only "server" Target steps after a "client" failure ran
                # cmake --build on an unconfigured Build/ (missing CMakeCache.txt).
                $script:BuildStepQueue.Clear()
            } else {
                if ($script:CurrentStepName -match 'Build') {
                    if ($script:CurrentBuildTarget -eq "client") {
                        $script:ClientBuildResult = "SUCCESS"; $script:ClientBuildTime = $elapsed
                        $ui["LblClientStatus"].Text = "client: SUCCESS (" + (Format-ElapsedTime $elapsed) + ")"
                        $ui["LblClientStatus"].Foreground = $greenBrush
                    } else {
                        $script:ServerBuildResult = "SUCCESS"; $script:ServerBuildTime = $elapsed
                        $ui["LblServerStatus"].Text = "server: SUCCESS (" + (Format-ElapsedTime $elapsed) + ")"
                        $ui["LblServerStatus"].Foreground = $greenBrush
                    }
                }
            }

            if ($script:BuildStepQueue.Count -gt 0) {
                $next = $script:BuildStepQueue[0]; $script:BuildStepQueue.RemoveAt(0)
                if ($next.Target -eq "server") {
                    $ui["LblServerStatus"].Text = "server: building..."
                    $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#0078A8")))
                }
                Start-Build-Step $next; $script:BuildTimer.Start()
            } else {
                $anyErr = $script:HasBuildErrors -or ($script:ClientBuildResult -eq "FAILED") -or ($script:ServerBuildResult -eq "FAILED")
                if (-not $anyErr) {
                    Play-SuccessSound; Copy-AddinFiles
                    if ($null -ne $script:BuildVersion) {
                        Set-ProjectVersion $script:BuildVersion.Major $script:BuildVersion.Minor $script:BuildVersion.Patch
                    }
                    try {
                        $gitExe = Resolve-GitExecutable
                        $headHash = if ($gitExe) { (& $gitExe -C $script:ProjectRoot rev-parse HEAD 2>$null) } else { $null }
                        if ($headHash) {
                            $hf = Join-Path $script:ProjectRoot "build\.last-built-hash"
                            Set-Content -Path $hf -Value $headHash.Trim() -NoNewline -Encoding UTF8
                        }
                    } catch {}
                } else { Play-FailureSound }
                $fillColor = $(if ($anyErr) { "#B81818" } else { "#10783A" })
                $ui["ProgressFill"].Background = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($fillColor)))
                $pw = $ui["ProgressBack"].ActualWidth
                if ($pw -gt 0) { $ui["ProgressFill"].Width = $pw }
                if ($anyErr) {
                    $ui["LblBuildActivity"].Text = $(if ($script:IsPushing) { "Release finished (with errors)." } else { "Build complete (with errors)." })
                    $ui["LblProgressText"].Text = $(if ($script:IsPushing) { "Release finished (errors)" } else { "Build finished (errors)" })
                } else {
                    $ui["LblBuildActivity"].Text = $(if ($script:IsPushing) { "Release complete." } else { "Build complete." })
                    $ui["LblProgressText"].Text = $(if ($script:IsPushing) { "100% - release complete" } else { "100% - build complete" })
                }
                $errCnt = $script:ClientErrors.Count + $script:ServerErrors.Count
                $ui["BtnCopyErrors"].Visibility = $(if ($errCnt -gt 0) { [System.Windows.Visibility]::Visible } else { [System.Windows.Visibility]::Collapsed })
                $ui["BtnCopyLog"].Visibility = [System.Windows.Visibility]::Visible
                $wasReleaseSuccess = ($script:IsPushing -and -not $anyErr)
                $script:IsBuilding = $false; $script:IsPushing = $false
                $ui["BtnBuild"].IsEnabled = $true; $ui["BtnRelease"].IsEnabled = $true; $ui["BtnCleanBuild"].IsEnabled = $true
                $ui["BtnPull"].IsEnabled = $true
                $ui["BtnPush"].IsEnabled = $true
                $ui["BtnPruneWorktrees"].IsEnabled = $true
                $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Collapsed
                Refresh-VersionDisplay; Update-RunButtons; Update-StatusBar
                # S480: post-release latest-version label refresh. Only fires
                # on a fully successful release (anyErr false + IsPushing
                # was true at completion). gh push lag is usually 1-3 s, so
                # the gh-api call has ~likely-fresh data; if not, the label
                # falls back to the prior value.
                if ($wasReleaseSuccess) {
                    Refresh-LatestRelease
                }
            }
        }
    } catch {}
})

# Main timer (2s) -- git status, process monitoring, gh auth polling.
# Separate from the fast-tick StatusModeTimer (500ms) so we don't spin up a git
# runspace 4x/second; git poll is expensive, live status updates are cheap.
$script:MainTimer = New-Object System.Windows.Threading.DispatcherTimer
$script:MainTimer.Interval = [TimeSpan]::FromSeconds(2)
$script:MainTimer.Add_Tick({
    try {
        Update-RunButtons
        if (-not $script:IsBuilding -and -not $script:IsPushing) { Update-StatusBar }
        # Re-check gh auth if not signed in (do not require GhAuthChecked — if the first probe never
        # completes, GhAuthChecked stays false and we would never retry; activation/F5 cancel instead).
        if (-not $script:GhAuthOk -and -not $script:GhAuthRefreshBusy) {
            $elapsed = ([DateTime]::UtcNow - $script:LastGhAuthProbeUtc).TotalSeconds
            if ($elapsed -ge 10) { Invoke-GhAuthBackgroundCheck }
        }
        # Deferred one-shot initial fetch of the latest GitHub release.
        # Set in Window.Loaded; fires here once gh CLI is available and we
        # are not in a build / release / refresh window (avoids competing
        # with the post-release refresh that auto-fires from the BuildTimer
        # success path).
        if ($script:LatestReleaseNeedsInitialFetch -and $script:GhCliAvailable -and `
            -not $script:LatestReleaseRefreshBusy -and -not $script:IsBuilding -and -not $script:IsPushing) {
            $script:LatestReleaseNeedsInitialFetch = $false
            Refresh-LatestRelease
        }
    } catch {}
})

# Live status mode timer (500ms) -- pure in-memory reads, never does I/O.
# Purpose: the status bar's "mode" label and server/game log streaming feel
# realtime without forcing the expensive MainTimer (which spawns runspaces) to
# run every half second. This handler touches only local variables and the
# output-queue ConcurrentQueues, so it cannot block.
$script:StatusModeTimer = New-Object System.Windows.Threading.DispatcherTimer
$script:StatusModeTimer.Interval = [TimeSpan]::FromMilliseconds(500)
$script:StatusModeTimer.Add_Tick({
    try {
        Drain-ProcessOutputQueues
        Update-StatusMode
        Update-RunButtons
    } catch {}
})

# ============================================================================
# Section 19: Status bar updates
# ============================================================================

# Matches devtools/_dev-window.ps1: pass $env:PATH into runspace, Get-Command gh,
# gh auth status 2>&1, success = output matches 'Logged in' (not exit code).
function Stop-GhAuthProbeInFlight {
    if (-not $script:GhAuthRefreshBusy -and $null -eq $script:GhAuthPollTimer -and $null -eq $script:GhAuthPS) { return }
    Write-DevWindowDebugLog "GhAuth: stopping in-flight probe (user refresh or window activation)" "AUTH"
    if ($null -ne $script:GhAuthPollTimer) {
        try { $script:GhAuthPollTimer.Stop() } catch {}
        $script:GhAuthPollTimer = $null
    }
    if ($null -ne $script:GhAuthHandle -and $null -ne $script:GhAuthPS) {
        try { $script:GhAuthPS.Stop() } catch {}
    }
    $script:GhAuthHandle = $null
    if ($null -ne $script:GhAuthPS) {
        try { $script:GhAuthPS.Dispose() } catch {}
        $script:GhAuthPS = $null
    }
    if ($null -ne $script:GhAuthRS) {
        try { $script:GhAuthRS.Close(); $script:GhAuthRS.Dispose() } catch {}
        $script:GhAuthRS = $null
    }
    $script:GhAuthRefreshBusy = $false
}

function Invoke-GhAuthBackgroundCheck {
    if ($script:GhAuthRefreshBusy) {
        Write-DevWindowDebugLog "GhAuthBackgroundCheck: skipped (already busy)" "AUTH"
        return
    }
    Sync-UserMachinePath
    $script:GhAuthHadGhOnHost = $null -ne (Get-Command gh -ErrorAction SilentlyContinue)
    Write-DevWindowDebugLog "GhAuthBackgroundCheck: starting runspace" "AUTH"
    $script:GhAuthRefreshBusy = $true
    $script:LastGhAuthProbeUtc = [DateTime]::UtcNow
    $pathToPass = $env:PATH

    $script:GhAuthRS = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
    $script:GhAuthRS.Open()
    $script:GhAuthPS = [System.Management.Automation.PowerShell]::Create()
    $script:GhAuthPS.Runspace = $script:GhAuthRS
    [void]$script:GhAuthPS.AddScript({
        param([string]$EnvPath)
        try {
            $env:PATH = $EnvPath
            $ghCmd = Get-Command gh -ErrorAction SilentlyContinue
            if ($null -eq $ghCmd) { return "NOT_INSTALLED" }
            $o = gh auth status 2>&1
            return (($o | ForEach-Object { $_.ToString() }) -join " ")
        } catch {
            return "ERROR"
        }
    })
    [void]$script:GhAuthPS.AddArgument($pathToPass)
    $script:GhAuthRunspaceStartedUtc = [DateTime]::UtcNow
    $script:GhAuthHandle = $script:GhAuthPS.BeginInvoke()
    Write-DevWindowDebugLog "GhAuth BeginInvoke returned; poll timer 400ms (log WARN every 5s while waiting)" "AUTH"
    $script:GhAuthPollTimer = New-Object System.Windows.Threading.DispatcherTimer
    $script:GhAuthPollTimer.Interval = [TimeSpan]::FromMilliseconds(400)
    $script:GhAuthPollTimer.Add_Tick({
        try {
            if ($null -eq $script:GhAuthHandle) { $this.Stop(); $script:GhAuthPollTimer = $null; return }
            if (-not $script:GhAuthHandle.IsCompleted) {
                $waitSec = ([DateTime]::UtcNow - $script:GhAuthRunspaceStartedUtc).TotalSeconds
                if ($waitSec -ge 45) {
                    $this.Stop()
                    $script:GhAuthPollTimer = $null
                    Write-DevWindowDebugLog "GhAuth timeout 45s: runspace still not complete; aborting wait (dispose may fail)" "WARN"
                    try { $script:GhAuthPS.Stop() } catch {}
                    try { $script:GhAuthPS.Dispose() } catch {}
                    try { $script:GhAuthRS.Close(); $script:GhAuthRS.Dispose() } catch {}
                    $script:GhAuthPS = $null
                    $script:GhAuthRS = $null
                    $script:GhAuthHandle = $null
                    $script:GhAuthRefreshBusy = $false
                    $script:GhAuthChecked = $true
                    # Do not claim gh is missing on timeout; only NOT_INSTALLED means that.
                    $script:GhCliAvailable = $script:GhAuthHadGhOnHost
                    $script:GhAuthOk = $false
                    try { Update-Auth-Labels } catch {}
                    return
                }
                if (([DateTime]::UtcNow - $script:LastGhAuthWaitLogUtc).TotalSeconds -ge 5) {
                    $script:LastGhAuthWaitLogUtc = [DateTime]::UtcNow
                    Write-DevWindowDebugLog ("GhAuth still waiting for runspace: " + [math]::Round($waitSec, 1) + "s elapsed, IsCompleted=false") "WARN"
                }
                return
            }
            $this.Stop()
            $script:GhAuthPollTimer = $null
            $ms = [math]::Round(([DateTime]::UtcNow - $script:GhAuthRunspaceStartedUtc).TotalMilliseconds, 0)
            Write-DevWindowDebugLog "GhAuth runspace completed in ${ms}ms, calling EndInvoke" "AUTH"
            # EndInvoke may return a collection or a single object; @() preserves one string (avoid
            # -join splitting a string into characters). Do not gate on .Count — breaks in PS 5.1.
            $res = $script:GhAuthPS.EndInvoke($script:GhAuthHandle)
            $script:GhAuthRefreshBusy = $false
            $txt = ""
            if ($null -ne $res) {
                $txt = (@($res) | ForEach-Object { $_.ToString() }) -join " "
            }
            # Same test as devtools/_dev-window.ps1 Section 22 (PowerShell -match is case-insensitive).
            $ok = $txt -match 'Logged in'
            $notInstalled = $txt -match 'NOT_INSTALLED'
            $script:GhAuthChecked = $true
            $script:GhCliAvailable = -not $notInstalled
            $script:GhAuthOk = $ok
            try {
                $errRecords = $script:GhAuthPS.Streams.Error.ReadAll()
                if ($null -ne $errRecords -and $errRecords.Count -gt 0) {
                    Write-DevWindowDebugLog ("GhAuth PowerShell Streams.Error: " + (($errRecords | ForEach-Object { $_.ToString() }) -join " | ")) "DEBUG"
                }
            } catch {}
            $maxRaw = 4000
            $rawPreview = if ($txt.Length -le $maxRaw) { $txt } else { $txt.Substring(0, $maxRaw) + "...(truncated)" }
            Write-DevWindowDebugLog ("GhAuth: notInstalled=$notInstalled loggedInMatch=$ok GhCliAvailable=$($script:GhCliAvailable) GhAuthOk=$($script:GhAuthOk) rawLen=$($txt.Length)") "AUTH"
            Write-DevWindowDebugLog ("GhAuth raw: " + $rawPreview) "DEBUG"
            try { $script:GhAuthPS.Dispose() } catch {}
            try { $script:GhAuthRS.Close(); $script:GhAuthRS.Dispose() } catch {}
            $script:GhAuthPS = $null
            $script:GhAuthRS = $null
            $script:GhAuthHandle = $null
            try { Update-Auth-Labels } catch { Write-DevWindowDebugLog ("Update-Auth-Labels after GhAuth: " + ($_ | Out-String)) "WARN" }
            try { Update-StatusBar } catch { Write-DevWindowDebugLog ("Update-StatusBar after GhAuth: " + ($_ | Out-String)) "WARN" }
        } catch {
            Write-DevWindowDebugLog ("GhAuthBackgroundCheck handler exception: " + ($_ | Out-String)) "ERROR"
            $script:GhAuthRefreshBusy = $false
            $script:GhAuthChecked = $true
            try { $script:GhAuthPS.Dispose() } catch {}
            try { $script:GhAuthRS.Close(); $script:GhAuthRS.Dispose() } catch {}
            $script:GhAuthPS = $null
            $script:GhAuthRS = $null
            $script:GhAuthHandle = $null
            try { Update-Auth-Labels } catch {}
        }
    })
    $script:GhAuthPollTimer.Start()
}

function Update-Auth-Labels {
    $authText  = "auth: ..."
    $authColor = "#4A5868"
    if (-not $script:GhAuthChecked) {
        # still checking - avoids flashing auth: no gh before background run finishes
    } elseif (-not $script:GhCliAvailable) {
        $authText  = "auth: no gh"
        $authColor = "#C9A020"
    } elseif (-not $script:GhAuthOk) {
        $authText  = "auth: sign in"
        $authColor = "#B86810"
    } else {
        $authText  = "auth: ok"
        $authColor = "#10783A"
    }
    try {
        $ui["StatusAuth"].Text = $authText
        $ui["StatusAuth"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($authColor)))
        $ui["LblAuthStatus"].Text = $authText
        $ui["LblAuthStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($authColor)))
        $tip = $(if (-not $script:GhAuthChecked) { "GitHub CLI: checking..." }
            elseif (-not $script:GhCliAvailable) { "GitHub CLI: not found - click for install help" }
            elseif ($script:GhAuthOk) { "GitHub CLI: signed in" }
            else { "GitHub CLI: not signed in - click to run gh auth login" })
        $ui["LblAuthStatus"].ToolTip = $tip
        $ui["StatusAuth"].ToolTip = $tip
    } catch {}
}

function Invoke-GhAuthHelp {
    # Same as devtools/_dev-window.ps1: visible PowerShell with gh on normal PATH
    if ($script:GhAuthOk) { return }
    Write-DevWindowDebugLog "Invoke-GhAuthHelp: launching powershell -NoExit gh auth login" "AUTH"
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = "powershell.exe"
        $psi.Arguments = "-NoExit -Command `"gh auth login`""
        $psi.UseShellExecute = $true
        [System.Diagnostics.Process]::Start($psi) | Out-Null
    } catch {
        Write-DevWindowDebugLog ("Invoke-GhAuthHelp failed: " + ($_ | Out-String)) "ERROR"
        [System.Windows.MessageBox]::Show(
            "Could not launch gh auth login. Make sure GitHub CLI (gh) is installed.",
            "Auth Error",
            "OK",
            "Warning") | Out-Null
    }
}

function Update-StatusMode {
    # Live status indicator. Reads in-memory state only -- no I/O, no runspaces.
    # Safe to call on the dispatcher at high frequency. Priority order:
    #   Build/Release > GitSync > Git action > Server/Game running > Idle
    if ($null -eq $ui["StatusMode"]) { return }
    $text = "Idle"
    $color = "#506070"

    $gameRunning  = ($null -ne $script:GameProcess  -and -not $script:GameProcess.HasExited)
    $testsRunning = $script:TestsRunning -or $script:TestsBuildBusy

    if ($script:IsBuilding -or $script:IsPushing) {
        $label = if ($script:IsPushing) { "Release" } else { "Build" }
        if ($script:NinjaTotal -gt 0) {
            $pct = [int]([math]::Floor(100.0 * $script:NinjaCurrent / $script:NinjaTotal))
            $text = "${label} [$($script:NinjaCurrent)/$($script:NinjaTotal)] ${pct}%"
        } elseif ($script:BuildStepsTotal -gt 0) {
            $cur = [math]::Min($script:BuildStepsCompleted + 1, $script:BuildStepsTotal)
            $text = "${label} step $cur/$($script:BuildStepsTotal): $($script:CurrentStepName)"
        } else {
            $text = "${label}: $($script:CurrentStepName)"
        }
        $color = "#0078A8"
    }
    elseif ($script:GitSyncBusy) {
        $text = if ($script:GitActionBusy -and $script:GitActionLabel) { "Git: " + $script:GitActionLabel } else { "Git: syncing..." }
        $color = "#0078A8"
    }
    elseif ($script:GitActionBusy) {
        $text = if ($script:GitActionLabel) { "Git: " + $script:GitActionLabel } else { "Git: busy" }
        $color = "#0078A8"
    }
    elseif ($testsRunning) {
        if ($script:TestsBuildBusy) { $text = "Tests: building pd-tests..." }
        elseif ($null -ne $script:TestsProcess) { $text = "Tests: running (pid " + $script:TestsProcess.Id + ")" }
        else { $text = "Tests: starting..." }
        $color = "#A06A10"
    }
    elseif ($gameRunning) {
        $text = "Game (pid " + $script:GameProcess.Id + ")"
        $color = "#10783A"
    }
    else {
        $text = "Idle"
        $color = "#506070"
    }

    try {
        $ui["StatusMode"].Text = $text
        $ui["StatusMode"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($color)))
    } catch {}
}

function Update-StatusBar {
    # Guard: skip if a git poll is already in flight
    if ($script:GitBusy) { return }
    $script:GitBusy = $true
    $gitExe = Resolve-GitExecutable
    if (-not $gitExe) { $gitExe = "git" }

    # Perf: reuse the persistent BgPool instead of creating a fresh runspace per
    # call. Old code opened a runspace here every 2s tick (~100-500ms on UI
    # thread). Pool-backed PowerShell instances skip the Open() cost entirely.
    Start-AsyncPoolAction `
        -Script {
            param($root, $gitExe)
            $b = try { $x = & $gitExe -C $root branch --show-current 2>$null; if ($x) { $x.Trim() } else { 'unknown' } } catch { 'unknown' }
            $h = try { $x = & $gitExe -C $root rev-parse --short HEAD 2>$null; if ($x) { $x.Trim() } else { '------' } } catch { '------' }
            $c = try { $st = & $gitExe -C $root status --porcelain 2>$null; if ($st) { @($st).Count } else { 0 } } catch { 0 }
            $w = try { $wt = & $gitExe -C $root worktree list --porcelain 2>$null; if ($wt) { ([regex]::Matches($wt, '^worktree ', [System.Text.RegularExpressions.RegexOptions]::Multiline)).Count } else { 1 } } catch { 0 }
            [PSCustomObject]@{ Branch = $b; Hash = $h; Count = $c; Worktrees = $w }
        } `
        -Arguments @($script:ProjectRoot, $gitExe) `
        -OnComplete {
            # Plain scriptblock (NO GetNewClosure) so $script: writes propagate.
            # Earlier inlined GetNewClosure'd tick handler had a SEPARATE $script: scope --
            # writes to $script:GitBusy never reached the main module, so the busy guard
            # latched true after the first poll and the status bar froze on its first values.
            param($result)
            try {
                $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
                if ($r) {
                    $script:GitChangeCount = $r.Count
                    $ui["StatusBranch"].Text = "branch: " + $r.Branch
                    $ui["StatusHash"].Text   = "HEAD: " + $r.Hash
                    if ($r.Count -eq 0) {
                        $ui["StatusDirty"].Text = "clean"
                        $ui["StatusDirty"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#10783A")))
                    } else {
                        $ui["StatusDirty"].Text = [string]$r.Count + " uncommitted"
                        $ui["StatusDirty"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#B86810")))
                    }
                    if ($r.Worktrees -gt 0) {
                        $ui["StatusWorktrees"].Text = "worktrees: " + $r.Worktrees
                        $wtColor = if ($r.Worktrees -gt 20) { "#B86810" } else { "#506070" }
                        $ui["StatusWorktrees"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($wtColor)))
                    }
                    Update-Auth-Labels
                }
            } catch {}
            $script:GitBusy = $false
        }
}

# ============================================================================
# Section 20: Keyboard shortcuts
# ============================================================================

$window.Add_KeyDown({
    param($sender, $e)
    $ctrl = $e.KeyboardDevice.IsKeyDown([System.Windows.Input.Key]::LeftCtrl) -or $e.KeyboardDevice.IsKeyDown([System.Windows.Input.Key]::RightCtrl)
    $shift = $e.KeyboardDevice.IsKeyDown([System.Windows.Input.Key]::LeftShift) -or $e.KeyboardDevice.IsKeyDown([System.Windows.Input.Key]::RightShift)

    if ($ctrl -and $shift -and $e.Key -eq [System.Windows.Input.Key]::B) {
        $script:ForceCleanBuild = $true; Start-Build; $e.Handled = $true
    }
    elseif ($ctrl -and $e.Key -eq [System.Windows.Input.Key]::B) {
        Start-Build; $e.Handled = $true
    }
    elseif ($ctrl -and $e.Key -eq [System.Windows.Input.Key]::R) {
        Start-PushRelease; $e.Handled = $true
    }
    elseif ($ctrl -and $e.Key -eq [System.Windows.Input.Key]::G) {
        Toggle-Game; $e.Handled = $true
    }
    elseif ($ctrl -and $e.Key -eq [System.Windows.Input.Key]::T) {
        Toggle-Tests; $e.Handled = $true
    }
    elseif ($ctrl -and $e.Key -eq [System.Windows.Input.Key]::L) {
        $ui["TabControl"].SelectedIndex = 1; $e.Handled = $true
    }
    elseif ($e.Key -eq [System.Windows.Input.Key]::F5) {
        Update-StatusBar
        Stop-GhAuthProbeInFlight
        Invoke-GhAuthBackgroundCheck
        Refresh-VersionDisplay
        Refresh-LatestRelease
        $e.Handled = $true
    }
})

# ============================================================================
# Section 21: Initialization
# ============================================================================

$window.Add_Loaded({
    try {
        Write-DevWindowDebugLog "Window Loaded event" "INFO"
        # Restore window size/position, clamped to the current screen work area.
        # A size saved on a larger monitor must not open bigger than (or off the
        # edge of) a smaller screen -- the Viewbox scales the UI down to fit, but
        # the window itself still has to land on-screen. WorkArea excludes the
        # taskbar; values are in WPF device-independent units, same as Width/Top.
        $s = $script:Settings
        $wa = $null
        try { $wa = [System.Windows.SystemParameters]::WorkArea } catch {}
        if ($s.WindowWidth -gt 0 -and $s.WindowHeight -gt 0) {
            $w = [double]$s.WindowWidth
            $h = [double]$s.WindowHeight
            if ($wa) {
                if ($w -gt $wa.Width)  { $w = $wa.Width }
                if ($h -gt $wa.Height) { $h = $wa.Height }
            }
            $window.Width  = $w
            $window.Height = $h
        }
        if ($s.WindowLeft -ge 0 -and $s.WindowTop -ge 0) {
            $left = [double]$s.WindowLeft
            $top  = [double]$s.WindowTop
            if ($wa) {
                $effW = $window.Width;  if ([double]::IsNaN($effW)) { $effW = $wa.Width }
                $effH = $window.Height; if ([double]::IsNaN($effH)) { $effH = $wa.Height }
                if ($left + $effW -gt $wa.Right)  { $left = [math]::Max($wa.Left, $wa.Right  - $effW) }
                if ($top  + $effH -gt $wa.Bottom) { $top  = [math]::Max($wa.Top,  $wa.Bottom - $effH) }
                if ($left -lt $wa.Left) { $left = $wa.Left }
                if ($top  -lt $wa.Top)  { $top  = $wa.Top }
            }
            $window.Left = $left
            $window.Top  = $top
            $window.WindowStartupLocation = [System.Windows.WindowStartupLocation]::Manual
        }

        # Version
        Refresh-VersionDisplay

        # Release cache: prefer the on-disk cache for instant display, then
        # kick a background refresh so the label tracks the live GitHub
        # state. If no cache exists, the refresh is the first source of
        # truth; the label shows "latest: --" until it lands.
        $cached = Load-ReleaseCache
        if ($null -ne $cached) { Update-LatestReleaseLabel $cached }
        # Defer the gh-api refresh until after the gh-auth probe finishes
        # (it needs gh on PATH and a valid token). MainTimer's existing
        # post-auth state will trigger a one-shot refresh below.
        $script:LatestReleaseNeedsInitialFetch = $true

        # Dev version
        $ver = Get-ProjectVersion
        $ui["LblDevVersion"].Text = "Dev Latest: v" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch

        # Run buttons
        Update-RunButtons

        # Docs
        Populate-DocList

        # Status bar (initial)
        Update-StatusBar
        Update-StatusMode

        Invoke-GhAuthBackgroundCheck

        # Start timers: MainTimer (2s git poll) + StatusModeTimer (500ms live state).
        $script:MainTimer.Start()
        $script:StatusModeTimer.Start()
    } catch {}
})

$window.Add_Activated({
    try {
        if ($script:GhAuthOk) { return }
        # If the first gh auth status is still stuck, GhAuthChecked stays false — still re-probe when
        # the user returns from the browser (cancel stale runspace first).
        if ($script:GhAuthRefreshBusy) {
            $hangSec = ([DateTime]::UtcNow - $script:GhAuthRunspaceStartedUtc).TotalSeconds
            if ($hangSec -ge 2) { Stop-GhAuthProbeInFlight }
            else { return }
        }
        $elapsed = ([DateTime]::UtcNow - $script:LastGhAuthProbeUtc).TotalSeconds
        if ($elapsed -lt 3) { return }
        Invoke-GhAuthBackgroundCheck
    } catch {}
})

# ============================================================================
# Section 22: Cleanup + save window state
# ============================================================================

$window.Add_Closing({
    try {
        Write-DevWindowDebugLog "Window Closing" "INFO"
        $script:MainTimer.Stop()
        $script:BuildTimer.Stop()
        if ($null -ne $script:StatusModeTimer) { try { $script:StatusModeTimer.Stop() } catch {} }
        if ($null -ne $script:BuildProcess) { try { $script:BuildProcess.Kill() } catch {} }
        # Also tear down any game/tests process we started so their stdin/stdout
        # pipes and background reader threads don't outlive the window.
        if ($null -ne $script:GameProcess -and -not $script:GameProcess.HasExited) {
            try { $script:GameProcess.Kill() } catch {}
        }
        if ($null -ne $script:TestsProcess -and -not $script:TestsProcess.HasExited) {
            try { $script:TestsProcess.Kill() } catch {}
        }

        # Save window state
        $script:Settings.WindowWidth  = [int]$window.ActualWidth
        $script:Settings.WindowHeight = [int]$window.ActualHeight
        $script:Settings.WindowLeft   = [int]$window.Left
        $script:Settings.WindowTop    = [int]$window.Top
        Save-Settings $script:Settings

        # Dispose BgPool to release background threads.
        if ($null -ne $script:BgPool) {
            try { $script:BgPool.Close() } catch {}
            try { $script:BgPool.Dispose() } catch {}
            $script:BgPool = $null
        }
    } catch {}
})

# ============================================================================
# Section 23: Run
# ============================================================================

try {
    [void]$window.ShowDialog()
    Write-DevWindowDebugLog "ShowDialog returned (window closed)" "INFO"
} catch {
    Write-DevWindowDebugLog ("ShowDialog fatal: " + ($_ | Out-String)) "ERROR"
    [System.Windows.MessageBox]::Show(
        "Fatal error: " + $_.Exception.Message,
        "Dev Window v2 Error",
        "OK",
        "Error"
    ) | Out-Null
}
