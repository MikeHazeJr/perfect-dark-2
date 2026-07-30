# ============================================================================
# dev-window-v3.ps1 -- Perfect Dark 2 Dev Window v3
#
# A clean, quick, efficient rebuild of the Dev Window. Lean WPF UI. Every
# build action shells out to devtools/build-headless.ps1 (the ONE canonical
# build engine) -- v3 never defines its own CMake flags, so it cannot drift
# from headless the way v2's duplicated Get-BuildSteps could.
#
# Features: Build (all targets), Run Game, Run Tests, Release, a live build
# queue panel, and a streaming log with copy/copy-errors. All long work runs
# in a background runspace; the UI thread never blocks.
#
# PowerShell 5.1 + WPF (inline XAML). No external modules. ASCII only, no
# em-dashes (Windows-1252 truncation hazard).
# ============================================================================

$env:PD_BUILD_ENV_QUIET = '1'

# Build environment -- self-configures TEMP/TMP, PATH (MinGW64), MSYSTEM, ccache.
. (Join-Path (Join-Path $PSScriptRoot "..") "_build-env-prelude.ps1")

# Append Machine+User PATH (deduped) so gh.exe is reachable, WITHOUT shadowing
# the prelude's MinGW64 prepend (that shadowing broke CMake in v2).
try {
    $seen = @{}
    foreach ($seg in ($env:Path -split ';')) {
        $k = $seg.TrimEnd('\').ToLowerInvariant()
        if ($k) { $seen[$k] = $true }
    }
    $add = @()
    foreach ($src in @([System.Environment]::GetEnvironmentVariable('Path','Machine'),
                       [System.Environment]::GetEnvironmentVariable('Path','User'))) {
        if (-not $src) { continue }
        foreach ($seg in ($src -split ';')) {
            $k = $seg.TrimEnd('\').ToLowerInvariant()
            if ($k -and -not $seen.ContainsKey($k)) { $seen[$k] = $true; $add += $seg }
        }
    }
    if ($add.Count -gt 0) { $env:Path = $env:Path + ';' + ($add -join ';') }
} catch {}

Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase

# S482: force WPF software rendering -- the GPU/DWM composition path silently
# dropped the visual tree on Mike's box. Must be set before the first Window.
[System.Windows.Media.RenderOptions]::ProcessRenderMode = [System.Windows.Interop.RenderMode]::SoftwareOnly

# Single consolidated Add-Type compile (three separate compiles cost 2-4s of
# cold start). Guard on the last type so it only runs on first launch.
if (-not ([System.Management.Automation.PSTypeName]'PD2V3.AsyncLineReader').Type) {
    Add-Type -Language CSharp @"
using System;
using System.IO;
using System.Threading;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
namespace PD2V3 {
    public class ConsoleHider {
        [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
        [DllImport("user32.dll")]   public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        public static void Hide() {
            IntPtr h = GetConsoleWindow();
            if (h != IntPtr.Zero) ShowWindow(h, 0);
        }
    }
    public class AsyncLineReader {
        public static void StartReading(StreamReader reader, ConcurrentQueue<string> queue, string prefix) {
            var t = new Thread(() => {
                try { string line; while ((line = reader.ReadLine()) != null) queue.Enqueue(prefix + line); }
                catch {}
            });
            t.IsBackground = true;
            t.Start();
        }
    }
}
"@
}
[PD2V3.ConsoleHider]::Hide()

# ============================================================================
# Paths + settings
# ============================================================================

$script:ScriptDir    = $PSScriptRoot
$script:ProjectRoot  = (Resolve-Path (Join-Path $script:ScriptDir "..\..")).Path
$script:DevToolsDir  = Join-Path $script:ProjectRoot "devtools"
$script:Headless     = Join-Path $script:DevToolsDir "build-headless.ps1"
$script:RunTests     = Join-Path $script:DevToolsDir "run-pd-tests.ps1"
$script:ReleasePs    = Join-Path $script:DevToolsDir "release.ps1"
$script:BuildDir     = Join-Path $script:ProjectRoot "Build"
$script:GameExe      = Join-Path $script:BuildDir "PerfectDark.exe"
$script:CMakeLists   = Join-Path $script:ProjectRoot "CMakeLists.txt"
$script:SettingsPath = Join-Path $script:ScriptDir "settings.json"
$script:QueueDir     = Join-Path $script:ProjectRoot ".claude\session-builds\.queue"
$script:QueueActive  = Join-Path $script:QueueDir "active.json"

function Load-Settings {
    $defaults = @{ WindowWidth = 1200; WindowHeight = 820; WindowLeft = -1; WindowTop = -1; FontScale = 1.0 }
    if (-not (Test-Path -LiteralPath $script:SettingsPath)) { return $defaults }
    try {
        $json = Get-Content -LiteralPath $script:SettingsPath -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json
        $r = @{}
        foreach ($k in $defaults.Keys) { $r[$k] = $(if ($null -ne $json.$k) { $json.$k } else { $defaults[$k] }) }
        return $r
    } catch { return $defaults }
}

function Save-Settings($settings) {
    try { $settings | ConvertTo-Json -Depth 2 | Set-Content -LiteralPath $script:SettingsPath -Encoding UTF8 -ErrorAction Stop } catch {}
}

$script:Settings = Load-Settings

# ============================================================================
# Log pipe: background threads enqueue, a DispatcherTimer drains to the UI.
# ============================================================================

$script:LogQueue    = New-Object System.Collections.Concurrent.ConcurrentQueue[string]
$script:LogLines    = New-Object System.Collections.Generic.List[string]
$script:MaxLogLines = 5000
$script:Busy        = $false
$script:ActiveProc  = $null
$script:BuildStart  = $null

function Enqueue-Log([string]$text) {
    if ($null -eq $text) { return }
    $script:LogQueue.Enqueue($text)
}

# ============================================================================
# Runspace pool for non-blocking background work.
# ============================================================================

$script:BgPool = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspacePool(1, 3)
$script:BgPool.ApartmentState = "MTA"
$script:BgPool.Open()
$script:BgJobs = New-Object System.Collections.ArrayList

function Start-Bg([scriptblock]$Script, [object[]]$Arguments, [scriptblock]$OnComplete) {
    $ps = [System.Management.Automation.PowerShell]::Create()
    $ps.RunspacePool = $script:BgPool
    [void]$ps.AddScript($Script)
    foreach ($a in $Arguments) { [void]$ps.AddArgument($a) }
    $handle = $ps.BeginInvoke()
    [void]$script:BgJobs.Add([PSCustomObject]@{ PS = $ps; Handle = $handle; OnComplete = $OnComplete })
}

# Drained by the DispatcherTimer (defined after the window is built).
function Pump-BgJobs {
    for ($i = $script:BgJobs.Count - 1; $i -ge 0; $i--) {
        $job = $script:BgJobs[$i]
        if ($job.Handle.IsCompleted) {
            $result = $null
            try { $result = $job.PS.EndInvoke($job.Handle) } catch {}
            try { $job.PS.Dispose() } catch {}
            $script:BgJobs.RemoveAt($i)
            if ($job.OnComplete) { & $job.OnComplete $result }
        }
    }
}

# ============================================================================
# Version + git helpers (fast, called on a background runspace at startup).
# ============================================================================

function Get-ProjectVersionString {
    try {
        $text = Get-Content -LiteralPath $script:CMakeLists -Raw -ErrorAction Stop
        $maj = if ($text -match 'VERSION_SEM_MAJOR\s+(\d+)') { $Matches[1] } else { '0' }
        $min = if ($text -match 'VERSION_SEM_MINOR\s+(\d+)') { $Matches[1] } else { '0' }
        $pat = if ($text -match 'VERSION_SEM_PATCH\s+(\d+)') { $Matches[1] } else { '0' }
        return "$maj.$min.$pat"
    } catch { return "0.0.0" }
}

function Resolve-GitExe {
    $mingw = "C:\msys64\mingw64\bin\git.exe"
    if (Test-Path -LiteralPath $mingw) { return $mingw }
    $cmd = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# ============================================================================
# XAML: header, action buttons, queue panel, log pane.
# Dark theme, gold accents. Segoe UI for chrome, Consolas for the log.
# ============================================================================

[xml]$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Perfect Dark 2 -- Dev Window v3" Height="820" Width="1200"
        WindowStartupLocation="CenterScreen" Background="#1E1E1E"
        TextElement.FontFamily="Segoe UI" TextElement.Foreground="#D8DEE6">
  <Window.Resources>
    <Style TargetType="Button">
      <Setter Property="Background" Value="#2A2D33"/>
      <Setter Property="Foreground" Value="#E4C983"/>
      <Setter Property="BorderBrush" Value="#3C4048"/>
      <Setter Property="BorderThickness" Value="1"/>
      <Setter Property="Padding" Value="14,8"/>
      <Setter Property="Margin" Value="0,0,8,0"/>
      <Setter Property="FontWeight" Value="SemiBold"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}"
                    BorderThickness="{TemplateBinding BorderThickness}" CornerRadius="4">
              <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
            </Border>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
  </Window.Resources>
  <Grid Margin="14">
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="*"/>
      <RowDefinition Height="Auto"/>
    </Grid.RowDefinitions>

    <!-- Header -->
    <Border Grid.Row="0" Background="#26292E" CornerRadius="6" Padding="14,10" Margin="0,0,0,10">
      <Grid>
        <Grid.ColumnDefinitions>
          <ColumnDefinition Width="*"/>
          <ColumnDefinition Width="Auto"/>
        </Grid.ColumnDefinitions>
        <StackPanel Grid.Column="0" Orientation="Horizontal">
          <TextBlock Text="PERFECT DARK 2" FontSize="18" FontWeight="Bold" Foreground="#E4C983" VerticalAlignment="Center"/>
          <TextBlock Name="LblBranch" Text="  branch --" FontSize="14" Foreground="#8A94A2" VerticalAlignment="Center" Margin="12,0,0,0"/>
          <TextBlock Name="LblVersion" Text="  v--" FontSize="14" Foreground="#8A94A2" VerticalAlignment="Center" Margin="12,0,0,0"/>
          <TextBlock Name="LblDirty" Text="" FontSize="14" Foreground="#C98A3A" VerticalAlignment="Center" Margin="12,0,0,0"/>
          <TextBlock Name="LblWorktrees" Text="" FontSize="14" Foreground="#9A8AD0" VerticalAlignment="Center" Margin="12,0,0,0"/>
        </StackPanel>
        <Button Grid.Column="1" Name="BtnRefresh" Content="Refresh" Margin="0"/>
      </Grid>
    </Border>

    <!-- Action buttons -->
    <StackPanel Grid.Row="1" Orientation="Horizontal" Margin="0,0,0,10">
      <Button Name="BtnBuild" Content="BUILD (all)" FontSize="15" Padding="22,12" Foreground="#F0E4B8"/>
      <Button Name="BtnStop" Content="Stop" Padding="16,12" Foreground="#E08A8A" IsEnabled="False"/>
      <Button Name="BtnRun" Content="Run Game" Padding="16,12"/>
      <Button Name="BtnTests" Content="Run Tests" Padding="16,12"/>
      <Button Name="BtnWorkbench" Content="Workbench" Padding="16,12"/>
      <Button Name="BtnRelease" Content="Release..." Padding="16,12" Foreground="#C9A86A"/>
    </StackPanel>

    <!-- Status -->
    <Border Grid.Row="2" Background="#22252A" CornerRadius="4" Padding="12,8" Margin="0,0,0,10">
      <StackPanel Orientation="Horizontal">
        <TextBlock Name="LblStatus" Text="Idle." FontSize="14" Foreground="#B8C0CC" VerticalAlignment="Center"/>
        <TextBlock Name="LblElapsed" Text="" FontSize="13" Foreground="#6C7684" VerticalAlignment="Center" Margin="14,0,0,0"/>
      </StackPanel>
    </Border>

    <!-- Log + queue -->
    <Grid Grid.Row="3">
      <Grid.ColumnDefinitions>
        <ColumnDefinition Width="*"/>
        <ColumnDefinition Width="300"/>
      </Grid.ColumnDefinitions>
      <Border Grid.Column="0" Background="#141619" CornerRadius="6" Margin="0,0,10,0">
        <Grid>
          <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
          </Grid.RowDefinitions>
          <StackPanel Grid.Row="0" Orientation="Horizontal" Margin="8,6">
            <TextBlock Text="Log" FontWeight="Bold" Foreground="#8A94A2" VerticalAlignment="Center"/>
            <CheckBox Name="ChkAutoscroll" Content="Autoscroll" IsChecked="True" Foreground="#8A94A2" Margin="16,0,0,0" VerticalAlignment="Center"/>
            <Button Name="BtnCopyLog" Content="Copy All" Padding="10,3" Margin="16,0,0,0"/>
            <Button Name="BtnCopyErrors" Content="Copy Errors" Padding="10,3"/>
            <Button Name="BtnClearLog" Content="Clear" Padding="10,3"/>
          </StackPanel>
          <ScrollViewer Grid.Row="1" Name="LogScroll" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Auto" Margin="4">
            <TextBox Name="LogBox" IsReadOnly="True" Background="Transparent" Foreground="#C4CCD6" BorderThickness="0"
                     FontFamily="Consolas" FontSize="12.5" TextWrapping="NoWrap"
                     VerticalScrollBarVisibility="Disabled" HorizontalScrollBarVisibility="Disabled"/>
          </ScrollViewer>
        </Grid>
      </Border>
      <Border Grid.Column="1" Background="#141619" CornerRadius="6">
        <Grid>
          <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
          </Grid.RowDefinitions>
          <TextBlock Grid.Row="0" Text="Build Queue" FontWeight="Bold" Foreground="#8A94A2" Margin="10,8"/>
          <TextBox Grid.Row="1" Name="QueueBox" IsReadOnly="True" Background="Transparent" Foreground="#9AA4B2"
                   BorderThickness="0" FontFamily="Consolas" FontSize="12" Margin="8,0" TextWrapping="Wrap"/>
          <Button Grid.Row="2" Name="BtnCleanQueue" Content="Clean stale builds" Margin="8" Padding="8,6"/>
        </Grid>
      </Border>
    </Grid>

    <!-- Footer -->
    <TextBlock Grid.Row="4" Name="LblFooter" Text="v3 shells out to build-headless.ps1 -- one build tool, two interfaces."
               FontSize="11" Foreground="#5A6472" Margin="2,8,0,0"/>
  </Grid>
</Window>
"@

$reader = New-Object System.Xml.XmlNodeReader $xaml
$win = [System.Windows.Markup.XamlReader]::Load($reader)

$ui = @{}
foreach ($name in @('LblBranch','LblVersion','LblDirty','LblWorktrees','BtnRefresh','BtnBuild','BtnStop','BtnRun','BtnTests',
    'BtnWorkbench','BtnRelease','LblStatus','LblElapsed','ChkAutoscroll','BtnCopyLog','BtnCopyErrors','BtnClearLog',
    'LogScroll','LogBox','QueueBox','BtnCleanQueue','LblFooter')) {
    $ui[$name] = $win.FindName($name)
}

# ============================================================================
# Streaming child-process runner. Launches a PowerShell script, streams its
# stdout/stderr into the log queue via AsyncLineReader, and calls $OnExit(code)
# from the dispatcher timer once the process ends. The UI thread never waits.
# ============================================================================

$script:ProcOnExit = $null

function Start-StreamingProcess([string]$scriptPath, [string[]]$argList, [string]$workDir, [scriptblock]$OnExit) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "powershell.exe"
    $allArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $scriptPath) + $argList
    $psi.Arguments = ($allArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    if ($workDir -and (Test-Path -LiteralPath $workDir)) { $psi.WorkingDirectory = $workDir }
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()
    [PD2V3.AsyncLineReader]::StartReading($proc.StandardOutput, $script:LogQueue, "")
    [PD2V3.AsyncLineReader]::StartReading($proc.StandardError, $script:LogQueue, "! ")
    $script:ActiveProc = $proc
    $script:ProcOnExit = $OnExit
}

function Stop-ActiveProcess {
    if ($null -ne $script:ActiveProc -and -not $script:ActiveProc.HasExited) {
        try { & taskkill.exe /PID $script:ActiveProc.Id /T /F 2>&1 | Out-Null } catch {}
        Enqueue-Log ">>> stopped by user."
    }
}

# ============================================================================
# UI state helpers.
# ============================================================================

function Set-Busy([bool]$busy, [string]$status) {
    $script:Busy = $busy
    $ui['BtnBuild'].IsEnabled   = -not $busy
    $ui['BtnTests'].IsEnabled   = -not $busy
    $ui['BtnRelease'].IsEnabled = -not $busy
    $ui['BtnStop'].IsEnabled    = $busy
    if ($status) { $ui['LblStatus'].Text = $status }
    if ($busy) { $script:BuildStart = [DateTime]::UtcNow } else { $script:BuildStart = $null; $ui['LblElapsed'].Text = "" }
}

function Refresh-Header {
    $git = Resolve-GitExe
    $root = $script:ProjectRoot
    Start-Bg -Script {
        param($git, $root)
        $branch = "?"; $dirty = 0; $worktrees = 0
        if ($git) {
            try { $b = & $git -C $root rev-parse --abbrev-ref HEAD 2>$null; if ($b) { $branch = "$b".Trim() } } catch {}
            try { $st = @(& $git -C $root status --porcelain 2>$null); $dirty = @($st | Where-Object { $_ -ne "" }).Count } catch {}
            # c040: count LINKED worktrees. `git worktree list` includes the main
            # working tree as its first entry, so subtract it. Surfaces active
            # .claude/worktrees builds in the header so they are visible at a glance.
            try {
                $wt = @(& $git -C $root worktree list --porcelain 2>$null | Where-Object { $_ -like 'worktree *' })
                $worktrees = [Math]::Max(0, $wt.Count - 1)
            } catch {}
        }
        [PSCustomObject]@{ Branch = $branch; Dirty = $dirty; Worktrees = $worktrees }
    } -Arguments @($git, $root) -OnComplete {
        param($result)
        $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
        if ($r) {
            $ui['LblBranch'].Text = "  branch " + $r.Branch
            $ui['LblDirty'].Text = if ($r.Dirty -gt 0) { "* " + $r.Dirty + " changed" } else { "clean" }
            $ui['LblWorktrees'].Text = if ($r.Worktrees -gt 0) { "* " + $r.Worktrees + " worktree(s)" } else { "" }
        }
    }
    $ui['LblVersion'].Text = "  v" + (Get-ProjectVersionString)
}

# ============================================================================
# Git sync before build/release: add -A, commit only if staged, best-effort
# push. Lighter than v2 (no WSL/cygwin lock dance) but same core contract.
# ============================================================================

function Start-GitSync([string]$commitMessage, [bool]$requirePush, [scriptblock]$OnComplete) {
    $git = Resolve-GitExe
    if (-not $git) { Enqueue-Log "! git not found on PATH; skipping sync."; & $OnComplete $false; return }
    $root = $script:ProjectRoot
    Enqueue-Log ""
    Enqueue-Log ">>> git: sync before build"
    Start-Bg -Script {
        param($git, $root, $commitMessage, $requirePush)
        $logs = New-Object System.Collections.ArrayList
        $winLock = Join-Path $root ".git\index.lock"
        if (Test-Path -LiteralPath $winLock) { try { Remove-Item -LiteralPath $winLock -Force -ErrorAction SilentlyContinue } catch {} }
        $branch = "HEAD"
        try { $b = & $git -C $root rev-parse --abbrev-ref HEAD 2>$null; if ($b) { $branch = "$b".Trim() } } catch {}
        [void]$logs.Add("branch: $branch")
        # Reap stale transient .claude/ debris (smoke installs, multi-GB caches,
        # 200MB render logs) BEFORE `git add -A` so it never gets swept into a
        # commit -- and never bloats disk. Best-effort; a failure never blocks
        # the build. See devtools/clean-claude-workspace.ps1.
        $cleaner = Join-Path $root "devtools\clean-claude-workspace.ps1"
        if (Test-Path -LiteralPath $cleaner) {
            try {
                & powershell -NoProfile -ExecutionPolicy Bypass -File $cleaner -Root $root -Quiet 2>&1 |
                    ForEach-Object { [void]$logs.Add("$_") }
            } catch { [void]$logs.Add("cleanup skipped: $($_.Exception.Message)") }
        }
        & $git -C $root add -A 2>&1 | ForEach-Object { [void]$logs.Add("$_") }
        if ($LASTEXITCODE -ne 0) { return [PSCustomObject]@{ Ok = $false; Logs = $logs } }
        & $git -C $root diff --cached --quiet 2>$null
        if ($LASTEXITCODE -ne 0) {
            $co = @(& $git -C $root commit -m $commitMessage -m "Dev Window synchronized the live project state before this operation." -m "Refs: T-TOOLING-001" 2>&1)
            foreach ($l in $co) { [void]$logs.Add("$l") }
            if ($LASTEXITCODE -ne 0) { return [PSCustomObject]@{ Ok = $false; Logs = $logs } }
        } else {
            [void]$logs.Add("(nothing to commit)")
        }
        $pu = @(& $git -C $root push origin $branch 2>&1)
        foreach ($l in $pu) { [void]$logs.Add("$l") }
        $pushed = ($LASTEXITCODE -eq 0)
        if (-not $pushed -and $requirePush) { return [PSCustomObject]@{ Ok = $false; Logs = $logs } }
        return [PSCustomObject]@{ Ok = $true; Logs = $logs }
    } -Arguments @($git, $root, $commitMessage, $requirePush) -OnComplete {
        param($result)
        $r = if ($result -and $result.Count -gt 0) { $result[0] } else { $result }
        $ok = $false
        if ($r) { foreach ($l in $r.Logs) { Enqueue-Log $l }; $ok = $r.Ok }
        Enqueue-Log ""
        & $OnComplete $ok
    }
}

# ============================================================================
# Button handlers.
# ============================================================================

function Do-Build {
    if ($script:Busy) { return }
    Set-Busy $true "Git: syncing before build..."
    Start-GitSync "Tooling - T-TOOLING-001: Sync project state before build" $false {
        param($ok)
        $ver = Get-ProjectVersionString
        Set-Busy $true ("Building v" + $ver + " (all targets)...")
        Enqueue-Log (">>> build-headless.ps1 -Target all -Version " + $ver)
        Start-StreamingProcess $script:Headless @("-Target","all","-Version",$ver) $script:ProjectRoot {
            param($code)
            $ui['LblStatus'].Text = if ($code -eq 0) { "Build SUCCESS." } else { "Build FAILED (exit $code)." }
            Set-Busy $false $ui['LblStatus'].Text
            Refresh-Header
        }
    }
}

function Do-RunGame {
    if (-not (Test-Path -LiteralPath $script:GameExe)) {
        [System.Windows.MessageBox]::Show("PerfectDark.exe not found in Build/. Build first.", "Run Game", "OK", "Warning") | Out-Null
        return
    }
    try {
        Start-Process -FilePath $script:GameExe -WorkingDirectory $script:BuildDir | Out-Null
        Enqueue-Log ">>> launched PerfectDark.exe"
    } catch {
        [System.Windows.MessageBox]::Show("Failed to launch: $_", "Run Game", "OK", "Error") | Out-Null
    }
}

function Do-RunTests {
    if ($script:Busy) { return }
    Set-Busy $true "Running tests (session devwin)..."
    Enqueue-Log ">>> run-pd-tests.ps1 -Session devwin -BuildTimeoutSeconds 300"
    Start-StreamingProcess $script:RunTests @("-Session","devwin","-BuildTimeoutSeconds","300") $script:ProjectRoot {
        param($code)
        $ui['LblStatus'].Text = if ($code -eq 0) { "Tests PASSED." } else { "Tests FAILED (exit $code)." }
        Set-Busy $false $ui['LblStatus'].Text
    }
}

function Do-OpenWorkbench {
    $url = "http://127.0.0.1:8378/"
    $ready = $false
    try {
        $response = Invoke-WebRequest -UseBasicParsing -Uri ($url + "api/meta") -TimeoutSec 1
        $ready = ($response.StatusCode -eq 200 -and $response.Content -match '"items"')
    } catch {}

    if (-not $ready) {
        $node = Get-Command node.exe -ErrorAction SilentlyContinue
        $server = Join-Path $script:ProjectRoot "Tools\Workbench\server.js"
        if (-not $node -or -not (Test-Path -LiteralPath $server)) {
            [System.Windows.MessageBox]::Show(
                "Node.js or Tools\Workbench\server.js was not found.",
                "Open Workbench", "OK", "Error"
            ) | Out-Null
            return
        }
        $logDir = Join-Path $script:ProjectRoot ".claude\scratch\workbench"
        New-Item -ItemType Directory -Force -Path $logDir | Out-Null
        try {
            Start-Process -FilePath $node.Source -ArgumentList @($server) `
                -WorkingDirectory (Split-Path -Parent $server) -WindowStyle Hidden `
                -RedirectStandardOutput (Join-Path $logDir "server.out.log") `
                -RedirectStandardError (Join-Path $logDir "server.err.log") | Out-Null
        } catch {
            [System.Windows.MessageBox]::Show(
                "Failed to start Workbench: $_",
                "Open Workbench", "OK", "Error"
            ) | Out-Null
            return
        }
        for ($attempt = 0; $attempt -lt 25 -and -not $ready; $attempt++) {
            Start-Sleep -Milliseconds 100
            try {
                $response = Invoke-WebRequest -UseBasicParsing -Uri ($url + "api/meta") -TimeoutSec 1
                $ready = ($response.StatusCode -eq 200)
            } catch {}
        }
    }

    if (-not $ready) {
        [System.Windows.MessageBox]::Show(
            "Workbench did not start. Check .claude\scratch\workbench\server.err.log.",
            "Open Workbench", "OK", "Warning"
        ) | Out-Null
        return
    }
    Start-Process $url | Out-Null
    Enqueue-Log ">>> opened Workbench at $url"
}

function Do-Release {
    if ($script:Busy) { return }
    $ver = Get-ProjectVersionString
    $msg = "Release v" + $ver + "?`n`nThis runs devtools/release.ps1 (sets version, builds client + updater, packages, and pushes to GitHub)."
    $ok = [System.Windows.MessageBox]::Show($msg, "Release v" + $ver, "YesNo", "Warning")
    if ($ok -ne [System.Windows.MessageBoxResult]::Yes) { return }
    Set-Busy $true "Git: syncing before release..."
    Start-GitSync "Tooling - T-TOOLING-001: Sync project state before release" $true {
        param($ok)
        if (-not $ok) { Set-Busy $false "Release aborted (git sync failed)."; return }
        Set-Busy $true ("Release v" + $ver + ": running release.ps1...")
        Enqueue-Log (">>> release.ps1 -Version " + $ver)
        Start-StreamingProcess $script:ReleasePs @("-Version",$ver) $script:ProjectRoot {
            param($code)
            $ui['LblStatus'].Text = if ($code -eq 0) { "Release v$ver complete." } else { "Release FAILED (exit $code)." }
            Set-Busy $false $ui['LblStatus'].Text
            Refresh-Header
        }
    }
}

function Do-CleanQueue {
    if (Test-Path -LiteralPath $script:QueueActive) {
        [System.Windows.MessageBox]::Show("A build is currently active in the queue. Wait for it to finish before cleaning.", "Clean Queue", "OK", "Warning") | Out-Null
        return
    }
    $ok = [System.Windows.MessageBox]::Show("Remove ALL session build directories under .claude/session-builds/? (No active build detected.)", "Clean stale builds", "YesNo", "Warning")
    if ($ok -ne [System.Windows.MessageBoxResult]::Yes) { return }
    $bs = Join-Path $script:DevToolsDir "build-session.ps1"
    Enqueue-Log ">>> build-session.ps1 -RemoveAll"
    Start-StreamingProcess $bs @("-RemoveAll") $script:ProjectRoot {
        param($code)
        Enqueue-Log ">>> clean queue exit $code"
    }
}

# ============================================================================
# Queue panel refresh (reads the queue dir, no locks).
# ============================================================================

function Refresh-QueuePanel {
    $lines = New-Object System.Collections.Generic.List[string]
    try {
        if (Test-Path -LiteralPath $script:QueueActive) {
            $a = Get-Content -LiteralPath $script:QueueActive -Raw -ErrorAction Stop | ConvertFrom-Json
            $started = [DateTime]::Parse($a.StartedUtc, $null, [System.Globalization.DateTimeStyles]::RoundtripKind)
            $age = [int]([DateTime]::UtcNow - $started).TotalSeconds
            $lines.Add("ACTIVE: " + $a.Session + " (" + $a.Target + ")")
            $lines.Add("  running " + $age + "s")
        } else {
            $lines.Add("(no active build)")
        }
        $reqs = @(Get-ChildItem -LiteralPath $script:QueueDir -Filter "*.request.json" -File -ErrorAction SilentlyContinue | Sort-Object Name)
        if ($reqs.Count -gt 0) {
            $lines.Add("")
            $lines.Add("Waiting (" + $reqs.Count + "):")
            foreach ($f in $reqs) {
                try {
                    $r = Get-Content -LiteralPath $f.FullName -Raw | ConvertFrom-Json
                    $lines.Add("  " + $r.Session + " (" + $r.Target + ")")
                } catch {}
            }
        }
    } catch {
        $lines.Clear(); $lines.Add("(queue idle)")
    }
    $ui['QueueBox'].Text = ($lines -join "`r`n")
}

# ============================================================================
# Timers: drain log queue + elapsed clock (100ms), refresh queue (2s).
# ============================================================================

$script:FastTimer = New-Object System.Windows.Threading.DispatcherTimer
$script:FastTimer.Interval = [TimeSpan]::FromMilliseconds(100)
$script:FastTimer.Add_Tick({
    $drained = $false
    $line = ""
    while ($script:LogQueue.TryDequeue([ref]$line)) { $script:LogLines.Add($line); $drained = $true }
    if ($drained) {
        if ($script:LogLines.Count -gt $script:MaxLogLines) {
            $script:LogLines.RemoveRange(0, $script:LogLines.Count - $script:MaxLogLines)
        }
        $ui['LogBox'].Text = ($script:LogLines -join "`r`n")
        if ($ui['ChkAutoscroll'].IsChecked) { $ui['LogScroll'].ScrollToEnd() }
    }
    if ($null -ne $script:BuildStart) {
        $el = [int]([DateTime]::UtcNow - $script:BuildStart).TotalSeconds
        $ui['LblElapsed'].Text = "elapsed " + $el + "s"
    }
    if ($null -ne $script:ActiveProc -and $script:ActiveProc.HasExited) {
        $code = $script:ActiveProc.ExitCode
        $cb = $script:ProcOnExit
        $script:ActiveProc = $null
        $script:ProcOnExit = $null
        if ($cb) { & $cb $code }
    }
    Pump-BgJobs
})

$script:SlowTimer = New-Object System.Windows.Threading.DispatcherTimer
$script:SlowTimer.Interval = [TimeSpan]::FromSeconds(2)
$script:SlowTimer.Add_Tick({ Refresh-QueuePanel })

# ============================================================================
# Wiring + window lifecycle.
# ============================================================================

$ui['BtnRefresh'].Add_Click({ Refresh-Header })
$ui['BtnBuild'].Add_Click({ Do-Build })
$ui['BtnStop'].Add_Click({ Stop-ActiveProcess })
$ui['BtnRun'].Add_Click({ Do-RunGame })
$ui['BtnTests'].Add_Click({ Do-RunTests })
$ui['BtnWorkbench'].Add_Click({ Do-OpenWorkbench })
$ui['BtnRelease'].Add_Click({ Do-Release })
$ui['BtnCleanQueue'].Add_Click({ Do-CleanQueue })
$ui['BtnCopyLog'].Add_Click({ try { [System.Windows.Clipboard]::SetText(($script:LogLines -join "`r`n")) } catch {} })
$ui['BtnCopyErrors'].Add_Click({
    $errs = @($script:LogLines | Where-Object { $_ -match '(?i)error|failed|fatal' })
    try { [System.Windows.Clipboard]::SetText(($errs -join "`r`n")) } catch {}
})
$ui['BtnClearLog'].Add_Click({ $script:LogLines.Clear(); $ui['LogBox'].Text = "" })

if ($script:Settings.WindowWidth -gt 400)  { $win.Width  = [double]$script:Settings.WindowWidth }
if ($script:Settings.WindowHeight -gt 300) { $win.Height = [double]$script:Settings.WindowHeight }
if ([double]$script:Settings.WindowLeft -ge 0 -and [double]$script:Settings.WindowTop -ge 0) {
    $win.WindowStartupLocation = "Manual"
    $win.Left = [double]$script:Settings.WindowLeft
    $win.Top  = [double]$script:Settings.WindowTop
}

$win.Add_Loaded({
    $script:FastTimer.Start()
    $script:SlowTimer.Start()
    Refresh-Header
    Refresh-QueuePanel
    Enqueue-Log "Dev Window v3 ready. BUILD shells out to build-headless.ps1 (one build tool, two interfaces)."
})

$win.Add_Closing({
    try {
        $s = @{
            WindowWidth  = [int]$win.Width
            WindowHeight = [int]$win.Height
            WindowLeft   = [int]$win.Left
            WindowTop    = [int]$win.Top
            FontScale    = [double]$script:Settings.FontScale
        }
        Save-Settings $s
    } catch {}
    try { $script:FastTimer.Stop(); $script:SlowTimer.Stop() } catch {}
    try { if ($null -ne $script:ActiveProc -and -not $script:ActiveProc.HasExited) { Stop-ActiveProcess } } catch {}
    try { $script:BgPool.Close() } catch {}
})

[void]$win.ShowDialog()
