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
. (Join-Path $PSScriptRoot ".." "_build-env-prelude.ps1")

# ============================================================================
# Section 1: Assembly loading + console hide
# ============================================================================

Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName WindowsBase
Add-Type -AssemblyName System.Windows.Forms

if (-not ([System.Management.Automation.PSTypeName]'PD2V2.ConsoleHider').Type) {
    Add-Type -Language CSharp @"
using System;
using System.Runtime.InteropServices;
namespace PD2V2 {
    public class ConsoleHider {
        [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
        [DllImport("user32.dll")]   public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        public const int SW_HIDE = 0;
        public static void Hide() {
            IntPtr hwnd = GetConsoleWindow();
            if (hwnd != IntPtr.Zero) ShowWindow(hwnd, SW_HIDE);
        }
    }
}
"@
}
[PD2V2.ConsoleHider]::Hide()

# ============================================================================
# Section 2: C# helpers (AsyncLineReader) -- guarded
# ============================================================================

if (-not ([System.Management.Automation.PSTypeName]'PD2V2.AsyncLineReader').Type) {
    Add-Type -Language CSharp @"
using System;
using System.IO;
using System.Threading;
using System.Collections.Concurrent;
namespace PD2V2 {
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
}
"@
}

# ============================================================================
# Section 3: Configuration
# ============================================================================

$script:ScriptDir           = $PSScriptRoot
$script:ProjectRoot         = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$script:BuildDir            = Join-Path $script:ProjectRoot "Build"   # unified dir for pd + pd-server
$script:SettingsPath        = Join-Path $script:ScriptDir "settings.json"
$script:ReleaseCachePath    = Join-Path $script:ProjectRoot ".dev-window-release-cache.json"
$script:AddinDir            = Join-Path $script:ProjectRoot "..\post-batch-addin"
$script:CMake               = "cmake"
$script:CC                  = "C:/msys64/mingw64/bin/cc.exe"
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
$script:ForceCleanBuild     = $false
$script:BuildVersion        = $null

$script:GameProcess         = $null
$script:ServerProcess       = $null
$script:GitChangeCount      = 0
$script:GitBusy             = $false
$script:LastGitCheck        = [DateTime]::MinValue

$script:GhAuthOk            = $false
$script:GhCliAvailable      = $false
$script:GhAuthChecked       = $false
$script:LatestRelease       = $null

# ============================================================================
# Section 4: Settings persistence
# ============================================================================

function Load-Settings {
    $defaults = @{
        WindowWidth   = 960
        WindowHeight  = 700
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

function Classify-Line($line) {
    if ($line -match '(?i)\berror\b|^FAILED|undefined reference|multiple definition|fatal error') { return "error" }
    if ($line -match '(?i)\bwarning\b') { return "warning" }
    return "normal"
}

function Format-ElapsedTime($seconds) {
    if ($seconds -lt 60) { return "" + $seconds + "s" }
    $m = [math]::Floor($seconds / 60); $s = $seconds % 60
    return "" + $m + "m " + $s + "s"
}

function Get-ExePath($name) {
    # Both pd and pd-server land in the unified Build/ directory
    $p = Join-Path $script:BuildDir $name
    if (Test-Path $p) { return $p }
    return $null
}

function Test-ExeExists($name) { return ($null -ne (Get-ExePath $name)) }

function Test-NeedsConfigure($buildDir) {
    $cache = Join-Path $buildDir "CMakeCache.txt"
    if (-not (Test-Path $cache)) { return $true }
    $cmake = Join-Path $script:ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path $cmake)) { return $true }
    return ((Get-Item $cmake).LastWriteTime -gt (Get-Item $cache).LastWriteTime)
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
    $cp = Join-Path $script:ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path $cp)) { return }
    try {
        $c = Get-Content $cp -Raw -Encoding UTF8 -ErrorAction Stop
        $c = $c -replace '(VERSION_SEM_MAJOR\s+)\d+', ("`${1}" + $major)
        $c = $c -replace '(VERSION_SEM_MINOR\s+)\d+', ("`${1}" + $minor)
        $c = $c -replace '(VERSION_SEM_PATCH\s+)\d+', ("`${1}" + $patch)
        Set-Content -Path $cp -Value $c -NoNewline -Encoding UTF8 -ErrorAction Stop
    } catch {}
}

function Load-ReleaseCache {
    if (-not (Test-Path $script:ReleaseCachePath)) { return $null }
    try { return (Get-Content $script:ReleaseCachePath -Raw -ErrorAction Stop | ConvertFrom-Json) } catch { return $null }
}

function Save-ReleaseCache($data) {
    try { $data | ConvertTo-Json -Depth 3 | Set-Content $script:ReleaseCachePath -Encoding UTF8 -ErrorAction Stop } catch {}
}

# ============================================================================
# Section 8: Git operations
# ============================================================================

function Get-GitBranch {
    try {
        $b = git -C $script:ProjectRoot branch --show-current 2>$null
        if ($b) { return $b.Trim() }
    } catch {}
    return "unknown"
}

function Get-GitShortHash {
    try {
        $h = git -C $script:ProjectRoot rev-parse --short HEAD 2>$null
        if ($h) { return $h.Trim() }
    } catch {}
    return "------"
}

function Get-GitChangeCount {
    try {
        $st = git -C $script:ProjectRoot status --porcelain 2>$null
        if ($st) { return ($st | Measure-Object).Count }
    } catch {}
    return 0
}

function Auto-Commit-Sync {
    $lock = Join-Path $script:ProjectRoot ".git\index.lock"
    if (Test-Path $lock) { Remove-Item $lock -Force -ErrorAction SilentlyContinue }
    $st = git -C $script:ProjectRoot status --porcelain 2>$null
    if (-not $st) { return $true }
    $ver = $(if ($null -ne $script:BuildVersion) { $script:BuildVersion } else { Get-ProjectVersion })
    $msg = "Build v" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch + " - auto-commit before build"
    git -C $script:ProjectRoot add -A 2>$null | Out-Null
    git -C $script:ProjectRoot commit -m $msg 2>$null | Out-Null
    $commitOk = ($LASTEXITCODE -eq 0)
    try { git -C $script:ProjectRoot push 2>$null | Out-Null } catch {}
    return $commitOk
}

# ============================================================================
# Section 9: WPF XAML Definition
# ============================================================================

[xml]$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Perfect Dark 2  |  Dev Window v2"
        MinWidth="700" MinHeight="500"
        Background="#141820"
        WindowStartupLocation="CenterScreen"
        TextElement.FontFamily="Segoe UI"
        TextElement.FontSize="13"
        TextElement.Foreground="#C8D0DC">
    <Window.Resources>
        <Style x:Key="AccentBtn" TargetType="Button">
            <Setter Property="Background" Value="#0090D0"/>
            <Setter Property="Foreground" Value="White"/>
            <Setter Property="FontWeight" Value="SemiBold"/>
            <Setter Property="BorderThickness" Value="0"/>
            <Setter Property="Padding" Value="16,8"/>
            <Setter Property="Cursor" Value="Hand"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="3" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#00A8E8"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#404040"/>
                                <Setter Property="Foreground" Value="#808080"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="GreenBtn" TargetType="Button" BasedOn="{StaticResource AccentBtn}">
            <Setter Property="Background" Value="#2D5A27"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="3" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#3A7A30"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#404040"/>
                                <Setter Property="Foreground" Value="#808080"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="GoldBtn" TargetType="Button" BasedOn="{StaticResource AccentBtn}">
            <Setter Property="Background" Value="#1A3A5C"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="3" Padding="{TemplateBinding Padding}"
                                BorderBrush="#DAA520" BorderThickness="1">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#254A6C"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#404040"/>
                                <Setter Property="Foreground" Value="#808080"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="ToolBtn" TargetType="Button">
            <Setter Property="Background" Value="#3A3A3A"/>
            <Setter Property="Foreground" Value="#DCDCDC"/>
            <Setter Property="BorderThickness" Value="1"/>
            <Setter Property="BorderBrush" Value="#505050"/>
            <Setter Property="Padding" Value="10,7"/>
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
                                <Setter TargetName="border" Property="Background" Value="#505050"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="RedBtn" TargetType="Button" BasedOn="{StaticResource AccentBtn}">
            <Setter Property="Background" Value="#8B2020"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="3" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#A03030"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
        <Style x:Key="OrangeBtn" TargetType="Button" BasedOn="{StaticResource AccentBtn}">
            <Setter Property="Background" Value="#5A3A10"/>
            <Setter Property="Template">
                <Setter.Value>
                    <ControlTemplate TargetType="Button">
                        <Border x:Name="border" Background="{TemplateBinding Background}"
                                CornerRadius="3" Padding="{TemplateBinding Padding}">
                            <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                        </Border>
                        <ControlTemplate.Triggers>
                            <Trigger Property="IsMouseOver" Value="True">
                                <Setter TargetName="border" Property="Background" Value="#7A5020"/>
                            </Trigger>
                            <Trigger Property="IsEnabled" Value="False">
                                <Setter TargetName="border" Property="Background" Value="#404040"/>
                                <Setter Property="Foreground" Value="#808080"/>
                            </Trigger>
                        </ControlTemplate.Triggers>
                    </ControlTemplate>
                </Setter.Value>
            </Setter>
        </Style>
    </Window.Resources>

    <DockPanel>
        <!-- Header Brand Bar -->
        <Border DockPanel.Dock="Top" Background="#0C1220" BorderBrush="#0A2040" BorderThickness="0,0,0,1" Padding="14,7">
            <DockPanel>
                <TextBlock DockPanel.Dock="Right"
                           Text="Ctrl+B=Build    Ctrl+R=Release    Ctrl+L=Log    Ctrl+G=Game"
                           Foreground="#304870" FontSize="12" FontFamily="Consolas"
                           VerticalAlignment="Center"/>
                <StackPanel Orientation="Horizontal" VerticalAlignment="Center">
                    <Border Background="#0090D0" CornerRadius="2" Padding="7,2" Margin="0,0,10,0">
                        <TextBlock Text="PD2" FontSize="13" FontWeight="Black" Foreground="White"
                                   FontFamily="Consolas"/>
                    </Border>
                    <TextBlock Text="Dev Window" FontSize="13" Foreground="#6878A0"
                               VerticalAlignment="Center"/>
                    <TextBlock Text=" v2" FontSize="13" Foreground="#0090D0"
                               FontWeight="SemiBold" VerticalAlignment="Center"/>
                </StackPanel>
            </DockPanel>
        </Border>

        <!-- Status Bar -->
        <Border DockPanel.Dock="Bottom" Background="#0A0F1A" BorderBrush="#0A2040" BorderThickness="0,1,0,0" Padding="10,7">
            <DockPanel>
                <TextBlock x:Name="StatusVersion" Text="v0.0.0" Foreground="#C8A000"
                           FontFamily="Consolas" FontSize="13" FontWeight="SemiBold"
                           DockPanel.Dock="Right" VerticalAlignment="Center"/>
                <Rectangle Width="1" Fill="#162030" Margin="12,0" DockPanel.Dock="Right"/>
                <TextBlock x:Name="StatusAuth" Text="auth: ..." Foreground="#506070"
                           FontFamily="Consolas" FontSize="13"
                           DockPanel.Dock="Right" VerticalAlignment="Center" Margin="0,0,12,0"/>
                <Rectangle Width="1" Fill="#162030" Margin="0,0,12,0"/>
                <TextBlock x:Name="StatusBranch" Text="branch: --" Foreground="#0090D0"
                           FontFamily="Consolas" FontSize="13" Margin="0,0,12,0"/>
                <Rectangle Width="1" Fill="#162030" Margin="0,0,12,0"/>
                <TextBlock x:Name="StatusHash" Text="HEAD: ------" Foreground="#3A5070"
                           FontFamily="Consolas" FontSize="13" Margin="0,0,12,0"/>
                <Rectangle Width="1" Fill="#162030" Margin="0,0,12,0"/>
                <TextBlock x:Name="StatusDirty" Text="clean" Foreground="#00B400"
                           FontFamily="Consolas" FontSize="13"/>
            </DockPanel>
        </Border>

        <!-- Bottom Bar: Run Server + Run Game -->
        <Border DockPanel.Dock="Bottom" Background="#0C1018" BorderBrush="#0A2040" BorderThickness="0,1,0,0" Padding="6">
            <Grid>
                <Grid.ColumnDefinitions>
                    <ColumnDefinition Width="*"/>
                    <ColumnDefinition Width="4"/>
                    <ColumnDefinition Width="*"/>
                </Grid.ColumnDefinitions>
                <Button x:Name="BtnRunServer" Content="RUN SERVER" Style="{StaticResource OrangeBtn}"
                        FontSize="14" FontWeight="Bold" Padding="16,12" Grid.Column="0"/>
                <Button x:Name="BtnRunGame" Content="RUN GAME" Style="{StaticResource GreenBtn}"
                        FontSize="14" FontWeight="Bold" Padding="16,12" Grid.Column="2"/>
            </Grid>
        </Border>

        <!-- Tab Control -->
        <TabControl x:Name="TabControl" Background="#141820" BorderThickness="0" Padding="0">
            <TabControl.Resources>
                <Style TargetType="TabItem">
                    <Setter Property="Background" Value="#141820"/>
                    <Setter Property="Foreground" Value="#44586C"/>
                    <Setter Property="Padding" Value="20,9"/>
                    <Setter Property="FontSize" Value="12"/>
                    <Setter Property="FontWeight" Value="SemiBold"/>
                    <Setter Property="Template">
                        <Setter.Value>
                            <ControlTemplate TargetType="TabItem">
                                <Border x:Name="tabBorder" Background="{TemplateBinding Background}"
                                        Padding="{TemplateBinding Padding}" Margin="0,0,0,0"
                                        BorderBrush="Transparent" BorderThickness="0,0,0,2">
                                    <ContentPresenter ContentSource="Header"/>
                                </Border>
                                <ControlTemplate.Triggers>
                                    <Trigger Property="IsSelected" Value="True">
                                        <Setter TargetName="tabBorder" Property="Background" Value="#141820"/>
                                        <Setter TargetName="tabBorder" Property="BorderBrush" Value="#0090D0"/>
                                        <Setter Property="Foreground" Value="#D0DCF0"/>
                                    </Trigger>
                                    <Trigger Property="IsMouseOver" Value="True">
                                        <Setter TargetName="tabBorder" Property="Background" Value="#1A2030"/>
                                        <Setter Property="Foreground" Value="#7090B0"/>
                                    </Trigger>
                                </ControlTemplate.Triggers>
                            </ControlTemplate>
                        </Setter.Value>
                    </Setter>
                </Style>
            </TabControl.Resources>

            <!-- BUILD TAB -->
            <TabItem Header="BUILD">
                <DockPanel Margin="10">
                    <!-- Hero Buttons Row -->
                    <Grid DockPanel.Dock="Top" Margin="0,0,0,10">
                        <Grid.ColumnDefinitions>
                            <ColumnDefinition Width="*"/>
                            <ColumnDefinition Width="8"/>
                            <ColumnDefinition Width="*"/>
                        </Grid.ColumnDefinitions>
                        <Button x:Name="BtnBuild" Style="{StaticResource GreenBtn}"
                                FontSize="20" FontWeight="Black" MinHeight="82" Padding="16,0" Grid.Column="0">
                            <TextBlock Text="BUILD" FontSize="20" FontWeight="Black" FontFamily="Consolas"/>
                        </Button>
                        <Button x:Name="BtnRelease" Style="{StaticResource GoldBtn}"
                                FontSize="14" FontWeight="Bold" MinHeight="82" Padding="16,0" Grid.Column="2">
                            <TextBlock x:Name="TxtRelease" Text="RELEASE" TextAlignment="Center"
                                       FontSize="14" FontWeight="Bold" LineHeight="18"/>
                        </Button>
                    </Grid>

                    <!-- Status Area -->
                    <Grid DockPanel.Dock="Top" Margin="0,0,0,8">
                        <Grid.ColumnDefinitions>
                            <ColumnDefinition Width="*"/>
                            <ColumnDefinition Width="8"/>
                            <ColumnDefinition Width="252"/>
                        </Grid.ColumnDefinitions>

                        <!-- Left: Build Status (card panel) -->
                        <Border Grid.Column="0" Background="#0E1420" CornerRadius="4"
                                BorderBrush="#162438" BorderThickness="1" Padding="10,8">
                            <StackPanel>
                                <TextBlock x:Name="LblClientStatus" Text="client: --"
                                           Foreground="#44586C" FontFamily="Consolas" FontSize="13" Margin="0,0,0,3"/>
                                <TextBlock x:Name="LblServerStatus" Text="server: --"
                                           Foreground="#44586C" FontFamily="Consolas" FontSize="13" Margin="0,0,0,6"/>
                                <TextBlock x:Name="LblBuildActivity" Text="" Foreground="#506880"
                                           FontFamily="Consolas" FontSize="13" Margin="0,0,0,4"/>

                                <!-- Progress Bar -->
                                <Border x:Name="ProgressBack" Background="#0A1520" Height="16"
                                        CornerRadius="3" Margin="0,2" Visibility="Collapsed"
                                        BorderBrush="#1A3050" BorderThickness="1">
                                    <Grid>
                                        <Border x:Name="ProgressFill" Background="#0090D0"
                                                CornerRadius="2" HorizontalAlignment="Left" Width="0"/>
                                        <TextBlock x:Name="LblProgressText" Text="" Foreground="White"
                                                   FontFamily="Consolas" FontSize="11"
                                                   HorizontalAlignment="Center" VerticalAlignment="Center"/>
                                    </Grid>
                                </Border>

                                <!-- Action Buttons Row -->
                                <StackPanel Orientation="Horizontal" Margin="0,6,0,0">
                                    <Button x:Name="BtnStop" Content="STOP" Style="{StaticResource RedBtn}"
                                            Padding="10,7" Margin="0,0,4,0" Visibility="Collapsed"/>
                                    <Button x:Name="BtnCopyErrors" Content="Copy Errors" Style="{StaticResource ToolBtn}"
                                            Margin="0,0,4,0" Visibility="Collapsed"/>
                                    <Button x:Name="BtnCopyLog" Content="Copy Log" Style="{StaticResource ToolBtn}"
                                            Margin="0,0,4,0" Visibility="Collapsed"/>
                                    <Button x:Name="BtnCheck" Content="Check" Style="{StaticResource ToolBtn}"
                                            Padding="12,7" ToolTip="Validate clean git state + run git-snapshot.sh"/>
                                </StackPanel>
                            </StackPanel>
                        </Border>

                        <!-- Right: Version + Auth (card panel) -->
                        <Border Grid.Column="2" Background="#0E1420" CornerRadius="4"
                                BorderBrush="#162438" BorderThickness="1" Padding="10,8">
                            <StackPanel>
                                <TextBlock Text="V E R S I O N" Foreground="#2A4060" FontSize="11"
                                           FontFamily="Consolas" FontWeight="Bold" Margin="0,0,0,5"/>
                                <StackPanel Orientation="Horizontal" Margin="0,0,0,6">
                                    <StackPanel Margin="0,0,6,0">
                                        <TextBlock Text="MAJ" Foreground="#2A4060" FontSize="11"
                                                   FontFamily="Consolas" Margin="0,0,0,2"/>
                                        <StackPanel Orientation="Horizontal">
                                            <Button x:Name="BtnVerMajDown" Content="-" Style="{StaticResource ToolBtn}"
                                                    Padding="4,5" Width="28" FontFamily="Consolas"/>
                                            <TextBox x:Name="TxtVerMajor" Width="32" TextAlignment="Center"
                                                     Background="#0A1020" Foreground="#C8A000" BorderBrush="#1A3050"
                                                     FontFamily="Consolas" FontWeight="Bold" FontSize="13" Padding="2"/>
                                            <Button x:Name="BtnVerMajUp" Content="+" Style="{StaticResource ToolBtn}"
                                                    Padding="4,5" Width="28" FontFamily="Consolas"/>
                                        </StackPanel>
                                    </StackPanel>
                                    <StackPanel Margin="0,0,6,0">
                                        <TextBlock Text="MIN" Foreground="#2A4060" FontSize="11"
                                                   FontFamily="Consolas" Margin="0,0,0,2"/>
                                        <StackPanel Orientation="Horizontal">
                                            <Button x:Name="BtnVerMinDown" Content="-" Style="{StaticResource ToolBtn}"
                                                    Padding="4,5" Width="28" FontFamily="Consolas"/>
                                            <TextBox x:Name="TxtVerMinor" Width="32" TextAlignment="Center"
                                                     Background="#0A1020" Foreground="#C8A000" BorderBrush="#1A3050"
                                                     FontFamily="Consolas" FontWeight="Bold" FontSize="13" Padding="2"/>
                                            <Button x:Name="BtnVerMinUp" Content="+" Style="{StaticResource ToolBtn}"
                                                    Padding="4,5" Width="28" FontFamily="Consolas"/>
                                        </StackPanel>
                                    </StackPanel>
                                    <StackPanel>
                                        <TextBlock Text="PAT" Foreground="#2A4060" FontSize="11"
                                                   FontFamily="Consolas" Margin="0,0,0,2"/>
                                        <StackPanel Orientation="Horizontal">
                                            <Button x:Name="BtnVerPatDown" Content="-" Style="{StaticResource ToolBtn}"
                                                    Padding="4,5" Width="28" FontFamily="Consolas"/>
                                            <TextBox x:Name="TxtVerPatch" Width="32" TextAlignment="Center"
                                                     Background="#0A1020" Foreground="#C8A000" BorderBrush="#1A3050"
                                                     FontFamily="Consolas" FontWeight="Bold" FontSize="13" Padding="2"/>
                                            <Button x:Name="BtnVerPatUp" Content="+" Style="{StaticResource ToolBtn}"
                                                    Padding="4,5" Width="28" FontFamily="Consolas"/>
                                        </StackPanel>
                                    </StackPanel>
                                </StackPanel>
                                <CheckBox x:Name="ChkStable" Content="Stable release" Foreground="#C8A000"
                                          FontSize="13" FontWeight="SemiBold" Margin="0,2,0,6"/>
                                <TextBlock x:Name="LblAuthStatus" Text="auth: ..." Foreground="#44586C"
                                           FontFamily="Consolas" FontSize="13" Margin="0,0,0,3" Cursor="Hand"/>
                                <TextBlock x:Name="LblLatestRelease" Text="latest: --" Foreground="#44586C"
                                           FontFamily="Consolas" FontSize="13" Margin="0,0,0,2"/>
                                <TextBlock x:Name="LblDevVersion" Text="local: --" Foreground="#3860A0"
                                           FontFamily="Consolas" FontSize="13"/>
                            </StackPanel>
                        </Border>
                    </Grid>

                    <!-- Utility Buttons Row -->
                    <StackPanel DockPanel.Dock="Top" Orientation="Horizontal" Margin="0,2,0,0">
                        <Button x:Name="BtnOpenGitHub" Content="GitHub" Style="{StaticResource ToolBtn}" Margin="0,0,4,0"/>
                        <Button x:Name="BtnOpenFolder" Content="Project Folder" Style="{StaticResource ToolBtn}" Margin="0,0,4,0"/>
                        <Button x:Name="BtnCleanBuild" Content="Clean Build" Style="{StaticResource ToolBtn}" Margin="0,0,4,0"/>
                    </StackPanel>
                </DockPanel>
            </TabItem>

            <!-- LOG TAB -->
            <TabItem Header="LOG">
                <DockPanel Margin="10">
                    <DockPanel DockPanel.Dock="Top" Margin="0,0,0,6">
                        <Button x:Name="BtnLogClear" Content="Clear" Style="{StaticResource ToolBtn}"
                                DockPanel.Dock="Right" Margin="6,0,0,0"/>
                        <CheckBox x:Name="ChkAutoScroll" Content="Auto-scroll" Foreground="#44586C"
                                  FontFamily="Consolas" FontSize="11"
                                  IsChecked="True" DockPanel.Dock="Right" VerticalAlignment="Center" Margin="10,0"/>
                        <TextBox x:Name="TxtLogFilter" Background="#0A1020" Foreground="#6888A8"
                                 BorderBrush="#1A3050" Padding="6,3"
                                 FontFamily="Consolas" FontSize="11"
                                 Tag="Filter..." FontStyle="Italic"/>
                    </DockPanel>
                    <RichTextBox x:Name="LogOutput" Background="#080D14" Foreground="#6888A8"
                                 IsReadOnly="True" BorderThickness="1" BorderBrush="#0E1E30"
                                 FontFamily="Consolas"
                                 FontSize="11" VerticalScrollBarVisibility="Auto"
                                 HorizontalScrollBarVisibility="Auto">
                        <FlowDocument>
                            <Paragraph/>
                        </FlowDocument>
                    </RichTextBox>
                </DockPanel>
            </TabItem>

            <!-- DOCS TAB -->
            <TabItem Header="DOCS">
                <Grid Margin="10">
                    <Grid.ColumnDefinitions>
                        <ColumnDefinition Width="240"/>
                        <ColumnDefinition Width="6"/>
                        <ColumnDefinition Width="*"/>
                    </Grid.ColumnDefinitions>
                    <ListBox x:Name="DocList" Grid.Column="0" Background="#0E1420" Foreground="#6888A8"
                             BorderBrush="#162438" BorderThickness="1"
                             FontFamily="Consolas" FontSize="11"/>
                    <GridSplitter Grid.Column="1" Width="6" Background="#0A1828" HorizontalAlignment="Stretch"/>
                    <TextBox x:Name="DocContent" Grid.Column="2" Background="#080D14" Foreground="#A0B8D0"
                             IsReadOnly="True" TextWrapping="Wrap" AcceptsReturn="True"
                             VerticalScrollBarVisibility="Auto" BorderThickness="1" BorderBrush="#0E1E30"
                             FontFamily="Consolas" FontSize="11"/>
                </Grid>
            </TabItem>
        </TabControl>
    </DockPanel>
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
    "StatusBranch","StatusHash","StatusDirty","StatusAuth","StatusVersion",
    "BtnRunServer","BtnRunGame","TabControl",
    "BtnBuild","BtnRelease","TxtRelease","BtnStop","BtnCopyErrors","BtnCopyLog","BtnCheck",
    "LblClientStatus","LblServerStatus","LblBuildActivity",
    "ProgressBack","ProgressFill","LblProgressText",
    "TxtVerMajor","TxtVerMinor","TxtVerPatch",
    "BtnVerMajDown","BtnVerMajUp","BtnVerMinDown","BtnVerMinUp","BtnVerPatDown","BtnVerPatUp",
    "ChkStable","LblAuthStatus","LblLatestRelease","LblDevVersion",
    "BtnOpenGitHub","BtnOpenFolder","BtnCleanBuild",
    "BtnLogClear","ChkAutoScroll","TxtLogFilter","LogOutput",
    "DocList","DocContent"
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

function Add-LogLine($text, $color) {
    if ($null -eq $ui["LogOutput"]) { return }
    $doc = $ui["LogOutput"].Document
    $para = $doc.Blocks.LastBlock
    if ($null -eq $para) { $para = New-Object System.Windows.Documents.Paragraph; $doc.Blocks.Add($para) }
    $run = New-Object System.Windows.Documents.Run($text + "`n")
    $run.Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($color)))
    $para.Inlines.Add($run)
    if ($ui["ChkAutoScroll"].IsChecked) { $ui["LogOutput"].ScrollToEnd() }
}

function Add-LogLines {
    $filter = ""
    if ($null -ne $ui["TxtLogFilter"] -and $ui["TxtLogFilter"].Text -ne "Filter...") {
        $filter = $ui["TxtLogFilter"].Text
    }
    foreach ($line in $script:AllOutput) {
        if ($filter -ne "" -and $line -notmatch [regex]::Escape($filter)) { continue }
        $cls = Classify-Line $line
        $color = "#8C8C8C"
        if ($cls -eq "error") { $color = "#DC3232" }
        elseif ($cls -eq "warning") { $color = "#FF8C00" }
        Add-LogLine $line $color
    }
}

$ui["BtnLogClear"].Add_Click({
    try {
        $ui["LogOutput"].Document.Blocks.Clear()
        $ui["LogOutput"].Document.Blocks.Add((New-Object System.Windows.Documents.Paragraph))
    } catch {}
})

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
    if ($null -eq $ui["DocList"]) { return }
    $ui["DocList"].Items.Clear()
    $script:DocFileMap = @{}
    $folders = @("docs", "context")
    foreach ($folder in $folders) {
        $fp = Join-Path $script:ProjectRoot $folder
        if (Test-Path $fp) {
            Get-ChildItem -Path $fp -Include "*.md","*.txt" -Recurse | Sort-Object FullName | ForEach-Object {
                $rel = $_.FullName.Substring($script:ProjectRoot.Length).TrimStart('\', '/')
                [void]$ui["DocList"].Items.Add($rel)
                $script:DocFileMap[$rel] = $_.FullName
            }
        }
    }
    Get-ChildItem -Path $script:ProjectRoot -Filter "*.md" -File | Sort-Object Name | ForEach-Object {
        $rel = $_.Name
        if (-not $script:DocFileMap.ContainsKey($rel)) {
            [void]$ui["DocList"].Items.Add($rel)
            $script:DocFileMap[$rel] = $_.FullName
        }
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
    $parentDir = Split-Path $script:ProjectRoot -Parent
    $srcData = Join-Path $parentDir "post-batch-addin" | Join-Path -ChildPath "data"
    $dstData = Join-Path $script:BuildDir "data"
    if (-not (Test-Path $srcData)) { return }
    try {
        if (Test-Path $dstData) { Remove-Item $dstData -Recurse -Force -ErrorAction SilentlyContinue }
        Copy-Item -Path $srcData -Destination $dstData -Recurse -Force -ErrorAction Stop
    } catch {}
}

function Stop-Build {
    if ($null -ne $script:BuildProcess) { try { $script:BuildProcess.Kill() } catch {}; $script:BuildProcess = $null }
    $script:BuildStepQueue.Clear()
    $script:BuildTimer.Stop()
    $script:IsBuilding = $false; $script:IsPushing = $false
    $ui["BtnBuild"].IsEnabled = $true
    $ui["BtnRelease"].IsEnabled = $true
    $ui["BtnCleanBuild"].IsEnabled = $true
    $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["ProgressBack"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["LblBuildActivity"].Text = "Stopped."
}

function Start-Build-Step($step) {
    $script:CurrentStepName   = $step.Name
    $script:CurrentBuildTarget = $step.Target
    $script:OutputQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
    $script:StepStartTime  = [DateTime]::Now
    $script:LastOutputTime = [DateTime]::Now
    $script:BuildPercent   = 0
    $ui["LblBuildActivity"].Text = $step.Name + "..."

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $step.Exe; $psi.Arguments = $step.Args
    $psi.WorkingDirectory = $script:ProjectRoot
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables["PATH"]                 = $env:PATH
    $psi.EnvironmentVariables["MSYSTEM"]              = "MINGW64"
    $psi.EnvironmentVariables["MINGW_PREFIX"]         = "/mingw64"
    $psi.EnvironmentVariables["GIT_TERMINAL_PROMPT"]  = "0"
    $psi.EnvironmentVariables["TEMP"]                 = $env:TEMP
    $psi.EnvironmentVariables["TMP"]                  = $env:TMP
    $psi.EnvironmentVariables["CCACHE_SLOPPINESS"]    = "pch_defines,time_macros"
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
        $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Collapsed
        $ui["LblBuildActivity"].Text = "ERROR starting: " + $step.Exe
    }
}

function Get-BuildSteps($ver, [bool]$forceClean = $false) {
    # SYNC RULE: cmake configure args MUST match build-headless.ps1 exactly.
    $cores = $(if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { "4" })
    $vFlags = " -DVERSION_SEM_MAJOR=" + $ver.Major + " -DVERSION_SEM_MINOR=" + $ver.Minor + " -DVERSION_SEM_PATCH=" + $ver.Patch
    $steps = [System.Collections.ArrayList]::new()

    $commitMsg = "Build v" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch + " - auto-commit before build"
    $commitArgs = "/c cd /d `"" + $script:ProjectRoot + "`" && git add -A && (git diff --cached --quiet || git commit -m `"" + $commitMsg + "`") && (git push >nul 2>&1 & exit 0)"
    [void]$steps.Add(@{Name="Auto-commit + push"; Exe="cmd.exe"; Target="client"; Args=$commitArgs})

    if ($forceClean) {
        $cleanArgs = "/c (if exist `"" + $script:BuildDir + "`" rmdir /s /q `"" + $script:BuildDir + "`") & exit 0"
        [void]$steps.Add(@{Name="Cleaning build dir"; Exe="cmd.exe"; Target="client"; Args=$cleanArgs})
    }

    # Single configure for unified Build/ dir (pd + pd-server share one CMake dir)
    $needsConfigure = $forceClean -or (Test-NeedsConfigure $script:BuildDir)

    if ($needsConfigure) {
        $cfgArgs = "-G Ninja -DCMAKE_C_COMPILER=`"" + $script:CC + "`" -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -B `"" + $script:BuildDir + "`" -S `"" + $script:ProjectRoot + "`"" + $vFlags
        [void]$steps.Add(@{Name="Configure (Ninja + ccache)"; Exe=$script:CMake; Target="client"; Args=$cfgArgs})
    }
    [void]$steps.Add(@{Name="Build (client: pd)"; Exe=$script:CMake; Target="client"; Args="--build `"" + $script:BuildDir + "`" --target pd"})
    [void]$steps.Add(@{Name="Build (server: pd-server)"; Exe=$script:CMake; Target="server"; Args="--build `"" + $script:BuildDir + "`" --target pd-server"})

    return $steps
}

function Start-Build {
    if ($script:IsBuilding) { return }
    $script:IsBuilding = $true
    $script:ClientErrors.Clear(); $script:ServerErrors.Clear(); $script:AllOutput.Clear()
    $script:ClientBuildResult = $null; $script:ServerBuildResult = $null
    $script:ClientBuildTime = 0; $script:ServerBuildTime = 0
    $script:HasBuildErrors = $false; $script:CurrentBuildTarget = "client"

    $ui["LblClientStatus"].Text = "client: building..."; $ui["LblClientStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#508CDC")))
    $ui["LblServerStatus"].Text = "server: --"; $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#8C8C8C")))
    $ui["BtnBuild"].IsEnabled = $false; $ui["BtnRelease"].IsEnabled = $false; $ui["BtnCleanBuild"].IsEnabled = $false
    $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Visible
    $ui["BtnCopyErrors"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["BtnCopyLog"].Visibility = [System.Windows.Visibility]::Collapsed
    $ui["ProgressBack"].Visibility = [System.Windows.Visibility]::Visible

    $clean = $script:ForceCleanBuild; $script:ForceCleanBuild = $false
    $buildMode = $(if ($clean) { "clean" } else { "incremental" })
    $ui["LblBuildActivity"].Text = "Starting " + $buildMode + " build..."

    $script:BuildVersion = Get-UiVersion
    $script:BuildProcess = $null
    $script:BuildStepQueue.Clear()
    foreach ($s in (Get-BuildSteps $script:BuildVersion $clean)) { [void]$script:BuildStepQueue.Add($s) }
    $script:BuildTimer.Start()
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
    $msg = "Release v" + $vs + " (" + $kind + ")?`n`nThis will:`n1. Set version to " + $vs + " in CMakeLists.txt`n2. Build client + server`n3. Package and push to GitHub"
    $ok = [System.Windows.MessageBox]::Show($msg, ($kind + " Release v" + $vs), "YesNo", "Warning")
    if ($ok -ne [System.Windows.MessageBoxResult]::Yes) { return }
    $script:IsPushing = $true
    $ui["BtnBuild"].IsEnabled = $false; $ui["BtnRelease"].IsEnabled = $false; $ui["BtnCleanBuild"].IsEnabled = $false
    Set-ProjectVersion $ver.Major $ver.Minor $ver.Patch
    $ui["LblBuildActivity"].Text = "Release v" + $vs + ": building..."

    $script:HasBuildErrors = $false; $script:AllOutput.Clear()
    $script:ClientErrors.Clear(); $script:ServerErrors.Clear()
    $script:ClientBuildResult = $null; $script:ServerBuildResult = $null
    $script:BuildStepQueue.Clear()
    $ui["ProgressBack"].Visibility = [System.Windows.Visibility]::Visible
    $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Visible
    $ui["LblClientStatus"].Text = "client: building..."

    $script:BuildVersion = $ver
    foreach ($s in (Get-BuildSteps $ver $false)) { [void]$script:BuildStepQueue.Add($s) }

    $prerelArg = $(if ($isStable) { "" } else { " -Prerelease" })
    $psExe = $(if (Get-Command pwsh -ErrorAction SilentlyContinue) { "pwsh.exe" } else { "powershell.exe" })
    # -SkipBuild: dev-window-v2 already built both targets above; release.ps1 skips cmake step 0.
    # -SkipPush:$false: always push to GitHub (not skipped).
    [void]$script:BuildStepQueue.Add(@{
        Name   = "Release: packaging + GitHub push"
        Exe    = $psExe
        Target = "client"
        Args   = "-ExecutionPolicy Bypass -File `"" + $releaseScript + "`" -Version `"" + $vs + "`"" + $prerelArg + " -SkipBuild -SkipPush:`$false"
    })
    # Auto-switch to Log tab so release output is visible (gh upload can take 30-60s silently)
    $ui["TabControl"].SelectedIndex = 1
    $script:BuildTimer.Start()
}

# ============================================================================
# Section 16: Game / Server launch
# ============================================================================

function Toggle-Server {
    if ($null -ne $script:ServerProcess -and -not $script:ServerProcess.HasExited) {
        try { $script:ServerProcess.Kill() } catch {}
        $script:ServerProcess = $null
        $ui["BtnRunServer"].Content = "RUN SERVER"
        return
    }
    $exe = Get-ExePath $script:ServerExeName
    if ($null -eq $exe) {
        [System.Windows.MessageBox]::Show("Server executable not found. Build first.", "Run Error", "OK", "Warning") | Out-Null
        return
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe; $psi.UseShellExecute = $true
    $script:ServerProcess = [System.Diagnostics.Process]::Start($psi)
    $ui["BtnRunServer"].Content = "STOP SERVER"
}

function Toggle-Game {
    if ($null -ne $script:GameProcess -and -not $script:GameProcess.HasExited) {
        try { $script:GameProcess.Kill() } catch {}
        $script:GameProcess = $null
        $ui["BtnRunGame"].Content = "RUN GAME"
        return
    }
    $exe = Get-ExePath $script:ClientExeName
    if ($null -eq $exe) {
        [System.Windows.MessageBox]::Show("Game executable not found. Build first.", "Run Error", "OK", "Warning") | Out-Null
        return
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe; $psi.UseShellExecute = $true
    $script:GameProcess = [System.Diagnostics.Process]::Start($psi)
    $ui["BtnRunGame"].Content = "STOP GAME"
}

function Update-RunButtons {
    if ($null -ne $script:ServerProcess -and $script:ServerProcess.HasExited) {
        $script:ServerProcess = $null
        $ui["BtnRunServer"].Content = "RUN SERVER"
    }
    if ($null -ne $script:GameProcess -and $script:GameProcess.HasExited) {
        $script:GameProcess = $null
        $ui["BtnRunGame"].Content = "RUN GAME"
    }
}

# ============================================================================
# Section 17: Event wiring
# ============================================================================

$ui["BtnBuild"].Add_Click({ Start-Build })
$ui["BtnRelease"].Add_Click({ Start-PushRelease })
$ui["BtnStop"].Add_Click({ Stop-Build })
$ui["BtnRunServer"].Add_Click({ Toggle-Server })
$ui["BtnRunGame"].Add_Click({ Toggle-Game })

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
    try {
        $st = git -C $script:ProjectRoot status --porcelain 2>$null
        $cnt = $(if ($st) { ($st | Measure-Object).Count } else { 0 })
        $msg = ""
        if ($cnt -eq 0) {
            $msg = "Git state: CLEAN (no uncommitted changes)`n"
        } else {
            $msg = "Git state: DIRTY (" + $cnt + " uncommitted files)`n"
        }
        $snapshotScript = Join-Path $script:ProjectRoot "devtools\git-snapshot.sh"
        if (Test-Path $snapshotScript) {
            $msg = $msg + "`nRunning git-snapshot.sh...`n"
            $snapOut = bash $snapshotScript 2>&1
            $msg = $msg + ($snapOut -join "`n")
        } else {
            $msg = $msg + "git-snapshot.sh not found (optional)."
        }
        [System.Windows.MessageBox]::Show($msg, "Pre-Build Check", "OK", "Information") | Out-Null
    } catch {
        [System.Windows.MessageBox]::Show("Check failed: " + $_.Exception.Message, "Error", "OK", "Error") | Out-Null
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

$ui["LblAuthStatus"].Cursor = [System.Windows.Input.Cursors]::Hand
$ui["LblAuthStatus"].Add_MouseLeftButtonDown({ Invoke-GhAuthHelp })
$ui["StatusAuth"].Cursor = [System.Windows.Input.Cursors]::Hand
$ui["StatusAuth"].Add_MouseLeftButtonDown({ Invoke-GhAuthHelp })

# ============================================================================
# Section 18: Timers (WPF DispatcherTimer)
# ============================================================================

# Build timer (100ms) -- drains async output, tracks progress
$script:BuildTimer = New-Object System.Windows.Threading.DispatcherTimer
$script:BuildTimer.Interval = [TimeSpan]::FromMilliseconds(100)
$script:BuildTimer.Add_Tick({
    try {
        if ($null -eq $script:BuildProcess) {
            if ($script:BuildStepQueue.Count -gt 0 -and $script:IsBuilding) {
                $next = $script:BuildStepQueue[0]; $script:BuildStepQueue.RemoveAt(0)
                if ($next.Target -eq "server") {
                    $ui["LblServerStatus"].Text = "server: building..."
                    $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#508CDC")))
                }
                # When the release step starts, inject a banner in the log so output is traceable
                if ($next.Name -match "Release") {
                    Add-LogLine "" "#1A3050"
                    Add-LogLine ">>> Release: packaging + GitHub push" "#0090D0"
                    Add-LogLine "    gh upload may be quiet for 30-90s -- output arrives when GitHub responds" "#44586C"
                    Add-LogLine "" "#1A3050"
                }
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
                $ui["ProgressFill"].Background = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#DC3232")))
            }
            # Add to live log
            $logColor = "#8C8C8C"
            if ($cls -eq "error") { $logColor = "#DC3232" }
            elseif ($cls -eq "warning") { $logColor = "#FF8C00" }
            Add-LogLine $text $logColor

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
            } else {
                $ui["LblBuildActivity"].Text = $script:CurrentStepName + " (" + $el + "s)"
            }
            return
        }

        if ($null -ne $script:BuildProcess -and $script:BuildProcess.HasExited -and $script:OutputQueue.IsEmpty) {
            $script:BuildTimer.Stop()
            $exitCode = $script:BuildProcess.ExitCode
            $elapsed  = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
            try { $script:BuildProcess.Dispose() } catch {}
            $script:BuildProcess = $null

            $greenBrush = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#00B400"))
            $redBrush   = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#DC3232"))
            $dimBrush   = New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#8C8C8C"))

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
                # Skip remaining steps for same target
                $keep = [System.Collections.ArrayList]::new()
                foreach ($s in $script:BuildStepQueue) { if ($s.Target -ne $script:CurrentBuildTarget) { [void]$keep.Add($s) } }
                $script:BuildStepQueue.Clear()
                foreach ($s in $keep) { [void]$script:BuildStepQueue.Add($s) }
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
                    $ui["LblServerStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#508CDC")))
                }
                $ui["ProgressFill"].Background = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#00B400")))
                Start-Build-Step $next; $script:BuildTimer.Start()
            } else {
                $anyErr = $script:HasBuildErrors -or ($script:ClientBuildResult -eq "FAILED") -or ($script:ServerBuildResult -eq "FAILED")
                if (-not $anyErr) {
                    Play-SuccessSound; Copy-AddinFiles
                    if ($null -ne $script:BuildVersion) {
                        Set-ProjectVersion $script:BuildVersion.Major $script:BuildVersion.Minor $script:BuildVersion.Patch
                    }
                    try {
                        $headHash = (git -C $script:ProjectRoot rev-parse HEAD 2>$null)
                        if ($headHash) {
                            $hf = Join-Path $script:ProjectRoot "build\.last-built-hash"
                            Set-Content -Path $hf -Value $headHash.Trim() -NoNewline -Encoding UTF8
                        }
                    } catch {}
                } else { Play-FailureSound }
                $fillColor = $(if ($anyErr) { "#DC3232" } else { "#00B400" })
                $ui["ProgressFill"].Background = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($fillColor)))
                $pw = $ui["ProgressBack"].ActualWidth
                if ($pw -gt 0) { $ui["ProgressFill"].Width = $pw }
                if ($anyErr) { $ui["LblBuildActivity"].Text = "Build complete (with errors)" }
                else { $ui["LblBuildActivity"].Text = "Build complete." }
                $errCnt = $script:ClientErrors.Count + $script:ServerErrors.Count
                $ui["BtnCopyErrors"].Visibility = $(if ($errCnt -gt 0) { [System.Windows.Visibility]::Visible } else { [System.Windows.Visibility]::Collapsed })
                $ui["BtnCopyLog"].Visibility = [System.Windows.Visibility]::Visible
                $script:IsBuilding = $false; $script:IsPushing = $false
                $ui["BtnBuild"].IsEnabled = $true; $ui["BtnRelease"].IsEnabled = $true; $ui["BtnCleanBuild"].IsEnabled = $true
                $ui["BtnStop"].Visibility = [System.Windows.Visibility]::Collapsed
                Refresh-VersionDisplay; Update-RunButtons; Update-StatusBar
            }
        }
    } catch {}
})

# Main timer (2s) -- git status, process monitoring, status bar
$script:MainTimer = New-Object System.Windows.Threading.DispatcherTimer
$script:MainTimer.Interval = [TimeSpan]::FromSeconds(2)
$script:MainTimer.Add_Tick({
    try {
        Update-RunButtons
        if (-not $script:IsBuilding) { Update-StatusBar }
    } catch {}
})

# ============================================================================
# Section 19: Status bar updates
# ============================================================================

function Update-Auth-Labels {
    $authText  = "auth: ..."
    $authColor = "#8C8C8C"
    if (-not $script:GhAuthChecked) {
        # still checking — avoids flashing auth: no gh before background run finishes
    } elseif (-not $script:GhCliAvailable) {
        $authText  = "auth: no gh"
        $authColor = "#C9A020"
    } elseif (-not $script:GhAuthOk) {
        $authText  = "auth: sign in"
        $authColor = "#FF8C00"
    } else {
        $authText  = "auth: ok"
        $authColor = "#00B400"
    }
    try {
        $ui["StatusAuth"].Text = $authText
        $ui["StatusAuth"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($authColor)))
        $ui["LblAuthStatus"].Text = $authText
        $ui["LblAuthStatus"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($authColor)))
    } catch {}
}

function Invoke-GhAuthHelp {
    if (-not $script:GhCliAvailable) {
        [System.Windows.MessageBox]::Show(
            "GitHub CLI (gh) is not installed or not on your PATH.`n`n" +
            "Install (example):`n" +
            "  winget install GitHub.cli`n`n" +
            "Or: https://cli.github.com/`n`n" +
            "Restart Dev Window after installing, then click here again to run: gh auth login",
            "GitHub CLI",
            "OK",
            "Information") | Out-Null
        return
    }
    if ($script:GhAuthOk) { return }
    try {
        $gh = Get-Command gh -ErrorAction Stop
        Start-Process -FilePath $gh.Source -ArgumentList @('auth','login') -WorkingDirectory $script:ProjectRoot
    } catch {
        [System.Windows.MessageBox]::Show(
            "Could not start GitHub CLI: " + $_.Exception.Message,
            "Error",
            "OK",
            "Error") | Out-Null
    }
}

function Update-StatusBar {
    # Guard: skip if a git poll is already in flight
    if ($script:GitBusy) { return }
    $script:GitBusy = $true

    # Run the three git queries on a background runspace so the UI thread never
    # blocks.  Same pattern as the gh-auth check in Section 21.
    $root = $script:ProjectRoot
    $rs = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
    $rs.Open()
    $ps = [System.Management.Automation.PowerShell]::Create()
    $ps.Runspace = $rs
    [void]$ps.AddScript({
        param($root)
        $b = try { $x = git -C $root branch --show-current 2>$null; if ($x) { $x.Trim() } else { 'unknown' } } catch { 'unknown' }
        $h = try { $x = git -C $root rev-parse --short HEAD 2>$null; if ($x) { $x.Trim() } else { '------' } } catch { '------' }
        $c = try { $st = git -C $root status --porcelain 2>$null; if ($st) { @($st).Count } else { 0 } } catch { 0 }
        [PSCustomObject]@{ Branch = $b; Hash = $h; Count = $c }
    })
    [void]$ps.AddArgument($root)
    $handle = $ps.BeginInvoke()

    # Poll on the dispatcher (250 ms) until the background job finishes, then
    # apply results on the UI thread.  GitBusy is cleared only after completion.
    $gitPollTimer = New-Object System.Windows.Threading.DispatcherTimer
    $gitPollTimer.Interval = [TimeSpan]::FromMilliseconds(250)
    $gitPollTimer.Add_Tick({
        if (-not $handle.IsCompleted) { return }
        $this.Stop()
        try {
            $results = $ps.EndInvoke($handle)
            $r = if ($results -and $results.Count -gt 0) { $results[0] } else { $null }
            if ($r) {
                $script:GitChangeCount = $r.Count
                $ui["StatusBranch"].Text = "branch: " + $r.Branch
                $ui["StatusHash"].Text   = "HEAD: " + $r.Hash
                if ($r.Count -eq 0) {
                    $ui["StatusDirty"].Text = "clean"
                    $ui["StatusDirty"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#00B400")))
                } else {
                    $ui["StatusDirty"].Text = [string]$r.Count + " uncommitted"
                    $ui["StatusDirty"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString("#FF8C00")))
                }
                Update-Auth-Labels
            }
            try { $ps.Dispose() } catch {}
            try { $rs.Close(); $rs.Dispose() } catch {}
        } catch {}
        $script:GitBusy = $false
    })
    $gitPollTimer.Start()
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
        Toggle-Server; $e.Handled = $true
    }
    elseif ($ctrl -and $e.Key -eq [System.Windows.Input.Key]::L) {
        $ui["TabControl"].SelectedIndex = 1; $e.Handled = $true
    }
    elseif ($e.Key -eq [System.Windows.Input.Key]::F5) {
        Update-StatusBar; Refresh-VersionDisplay; $e.Handled = $true
    }
})

# ============================================================================
# Section 21: Initialization
# ============================================================================

$window.Add_Loaded({
    try {
        # Restore window size/position
        $s = $script:Settings
        if ($s.WindowWidth -gt 0 -and $s.WindowHeight -gt 0) {
            $window.Width  = $s.WindowWidth
            $window.Height = $s.WindowHeight
        }
        if ($s.WindowLeft -ge 0 -and $s.WindowTop -ge 0) {
            $window.Left = $s.WindowLeft
            $window.Top  = $s.WindowTop
            $window.WindowStartupLocation = [System.Windows.WindowStartupLocation]::Manual
        }

        # Version
        Refresh-VersionDisplay

        # Release cache
        $cached = Load-ReleaseCache
        if ($null -ne $cached) {
            try {
                $tag = $cached.tag_name
                $pre = $cached.prerelease
                $kind = $(if ($pre) { "dev" } else { "stable" })
                $ui["LblLatestRelease"].Text = "latest: " + $tag + " (" + $kind + ")"
                $color = $(if ($pre) { "#508CDC" } else { "#00B400" })
                $ui["LblLatestRelease"].Foreground = (New-Object System.Windows.Media.SolidColorBrush([System.Windows.Media.ColorConverter]::ConvertFromString($color)))
            } catch {}
        }

        # Dev version
        $ver = Get-ProjectVersion
        $ui["LblDevVersion"].Text = "Dev Latest: v" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch

        # Run buttons
        Update-RunButtons

        # Docs
        Populate-DocList

        # Status bar (initial)
        Update-StatusBar

        # Background: detect gh CLI + auth (no stderr spam if gh missing)
        $rs = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
        $rs.Open()
        $ps = [System.Management.Automation.PowerShell]::Create()
        $ps.Runspace = $rs
        [void]$ps.AddScript({
            $ghCmd = Get-Command gh -ErrorAction SilentlyContinue
            if (-not $ghCmd) {
                return [PSCustomObject]@{ Present = $false; Ok = $false }
            }
            $null = & gh auth status 2>&1
            return [PSCustomObject]@{ Present = $true; Ok = ($LASTEXITCODE -eq 0) }
        })
        $handle = $ps.BeginInvoke()
        $authPollTimer = New-Object System.Windows.Threading.DispatcherTimer
        $authPollTimer.Interval = [TimeSpan]::FromMilliseconds(500)
        $authPollTimer.Add_Tick({
            try {
                if (-not $handle.IsCompleted) { return }
                $this.Stop()
                $result = $ps.EndInvoke($handle)
                $script:GhCliAvailable = $false
                $script:GhAuthOk = $false
                $script:GhAuthChecked = $true
                if ($null -ne $result -and $result.Count -gt 0) {
                    $o = $result[0]
                    if ($null -ne $o -and $o.Present) {
                        $script:GhCliAvailable = $true
                        $script:GhAuthOk = [bool]$o.Ok
                    }
                }
                try { $ps.Dispose() } catch {}
                try { $rs.Close(); $rs.Dispose() } catch {}
                Update-Auth-Labels
                Update-StatusBar
            } catch {}
        })
        $authPollTimer.Start()

        $authTip = "GitHub CLI: click for install help or gh auth login"
        try { $ui["LblAuthStatus"].ToolTip = $authTip; $ui["StatusAuth"].ToolTip = $authTip } catch {}

        # Start main timer
        $script:MainTimer.Start()
    } catch {}
})

# ============================================================================
# Section 22: Cleanup + save window state
# ============================================================================

$window.Add_Closing({
    try {
        $script:MainTimer.Stop()
        $script:BuildTimer.Stop()
        if ($null -ne $script:BuildProcess) { try { $script:BuildProcess.Kill() } catch {} }

        # Save window state
        $script:Settings.WindowWidth  = [int]$window.ActualWidth
        $script:Settings.WindowHeight = [int]$window.ActualHeight
        $script:Settings.WindowLeft   = [int]$window.Left
        $script:Settings.WindowTop    = [int]$window.Top
        Save-Settings $script:Settings
    } catch {}
})

# ============================================================================
# Section 23: Run
# ============================================================================

try {
    [void]$window.ShowDialog()
} catch {
    [System.Windows.MessageBox]::Show(
        "Fatal error: " + $_.Exception.Message,
        "Dev Window v2 Error",
        "OK",
        "Error"
    ) | Out-Null
}
