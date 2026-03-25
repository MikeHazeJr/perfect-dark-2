# ============================================================================
# Section 1: Assembly loading + console hide
# ============================================================================

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not ([System.Management.Automation.PSTypeName]'PD2Dev.ConsoleHider').Type) {
    Add-Type -Language CSharp @"
using System;
using System.Runtime.InteropServices;
namespace PD2Dev {
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
[PD2Dev.ConsoleHider]::Hide()

# ============================================================================
# Section 2: C# helpers (AsyncLineReader, DarkMenuColorTable) -- guarded
# ============================================================================

if (-not ([System.Management.Automation.PSTypeName]'PD2Dev.AsyncLineReader').Type) {
    Add-Type -Language CSharp -ReferencedAssemblies System.Windows.Forms, System.Drawing @"
using System;
using System.IO;
using System.Drawing;
using System.Threading;
using System.Collections.Concurrent;
using System.Windows.Forms;
namespace PD2Dev {
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
    public class DarkMenuColorTable : ProfessionalColorTable {
        private Color bg  = Color.FromArgb(40, 40, 40);
        private Color brd = Color.FromArgb(70, 70, 70);
        private Color hi  = Color.FromArgb(60, 60, 60);
        private Color sep = Color.FromArgb(70, 70, 70);
        public override Color ToolStripDropDownBackground        { get { return bg; } }
        public override Color MenuBorder                         { get { return brd; } }
        public override Color MenuItemBorder                     { get { return hi; } }
        public override Color MenuItemSelected                   { get { return hi; } }
        public override Color MenuItemSelectedGradientBegin      { get { return hi; } }
        public override Color MenuItemSelectedGradientEnd        { get { return hi; } }
        public override Color MenuItemPressedGradientBegin       { get { return bg; } }
        public override Color MenuItemPressedGradientEnd         { get { return bg; } }
        public override Color MenuStripGradientBegin             { get { return bg; } }
        public override Color MenuStripGradientEnd               { get { return bg; } }
        public override Color ImageMarginGradientBegin           { get { return bg; } }
        public override Color ImageMarginGradientMiddle          { get { return bg; } }
        public override Color ImageMarginGradientEnd             { get { return bg; } }
        public override Color SeparatorDark                      { get { return sep; } }
        public override Color SeparatorLight                     { get { return bg; } }
        public override Color CheckBackground                    { get { return hi; } }
        public override Color CheckSelectedBackground            { get { return hi; } }
        public override Color CheckPressedBackground             { get { return hi; } }
    }
}
"@
}

# ============================================================================
# Section 3: Configuration
# ============================================================================

$script:ProjectRoot        = Split-Path -Parent $PSScriptRoot
$script:BuildDir           = Join-Path $script:ProjectRoot "build"
$script:ClientBuildDir     = Join-Path $script:BuildDir "client"
$script:ServerBuildDir     = Join-Path $script:BuildDir "server"
$script:QcFilePath         = Join-Path $script:ProjectRoot "context\qc-tests.md"
$script:SettingsPath       = Join-Path $script:ProjectRoot ".dev-window-settings.json"
$script:ReleaseCachePath   = Join-Path $script:ProjectRoot ".dev-window-release-cache.json"
$script:AddinDir           = Join-Path $script:ProjectRoot "..\post-batch-addin"
$script:CMake              = "cmake"
$script:Make               = "C:\msys64\usr\bin\make.exe"
$script:CC                 = "C:/msys64/mingw64/bin/cc.exe"
$script:ClientExeName      = "PerfectDark.exe"
$script:ServerExeName      = "PerfectDarkServer.exe"
$script:SoundsDir          = Join-Path $script:ProjectRoot "dist\build-sounds"

$env:MSYSTEM       = "MINGW64"
$env:MINGW_PREFIX  = "/mingw64"
$env:PATH          = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:PATH

$script:BuildProcess       = $null
$script:BuildStepQueue     = [System.Collections.ArrayList]::new()
$script:CurrentBuildTarget = "client"
$script:CurrentBuildDir    = $script:ClientBuildDir
$script:ClientErrors       = [System.Collections.ArrayList]::new()
$script:ServerErrors       = [System.Collections.ArrayList]::new()
$script:AllOutput          = [System.Collections.ArrayList]::new()
$script:ClientBuildResult  = $null
$script:ServerBuildResult  = $null
$script:ClientBuildTime    = 0
$script:ServerBuildTime    = 0
$script:IsBuilding         = $false
$script:IsPushing          = $false
$script:HasBuildErrors     = $false
$script:BuildPercent       = 0
$script:SpinnerIndex       = 0
$script:SpinnerChars       = @('|', '/', '-', '\')
$script:StepStartTime      = [DateTime]::Now
$script:LastOutputTime     = [DateTime]::Now
$script:OutputQueue        = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
$script:CurrentStepName    = ""

$script:GameProcess        = $null
$script:ServerProcess      = $null
$script:GitChangeCount     = 0
$script:GitBusy            = $false
$script:LastGitCheck       = [DateTime]::MinValue

$script:QcData             = @()
$script:QcFilter           = "All"
$script:QcDirty            = $false

$script:GhAuthOk           = $false
$script:LatestRelease      = $null

$script:Settings = @{
    GitHubRepo   = ""
    FontSize     = 10
    EnableSounds = $true
}

# ============================================================================
# Section 4: Font loading
# ============================================================================

$script:FontCollection  = New-Object System.Drawing.Text.PrivateFontCollection
$script:TitleFontFamily = $null
$script:UseCustomFont   = $false

$fontPath = Join-Path $script:ProjectRoot "fonts\Menus\Handel Gothic Regular\Handel Gothic Regular.otf"
if (Test-Path $fontPath) {
    try {
        $script:FontCollection.AddFontFile($fontPath)
        $script:TitleFontFamily = $script:FontCollection.Families[0]
        $script:UseCustomFont   = $true
    } catch {}
}

function New-UIFont($size, [switch]$Bold) {
    $style = $(if ($Bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular })
    if ($script:UseCustomFont -and $null -ne $script:TitleFontFamily) {
        return New-Object System.Drawing.Font($script:TitleFontFamily, $size, $style, [System.Drawing.GraphicsUnit]::Point)
    }
    return New-Object System.Drawing.Font("Segoe UI", $size, $style)
}

# ============================================================================
# Section 5: Settings persistence
# ============================================================================

function Load-Settings {
    if (Test-Path $script:SettingsPath) {
        try {
            $json = Get-Content $script:SettingsPath -Raw | ConvertFrom-Json
            if ($json.GitHubRepo)             { $script:Settings.GitHubRepo   = $json.GitHubRepo }
            if ($json.FontSize)               { $script:Settings.FontSize      = [int]$json.FontSize }
            if ($null -ne $json.EnableSounds) { $script:Settings.EnableSounds  = [bool]$json.EnableSounds }
        } catch {}
    }
    if ($script:Settings.GitHubRepo -eq "") {
        try {
            $remote = (git -C $script:ProjectRoot remote get-url origin 2>$null)
            if ($remote -and $remote -match 'github\.com[:/](.+?)(?:\.git)?$') {
                $script:Settings.GitHubRepo = $Matches[1]
            }
        } catch {}
    }
}

function Save-Settings {
    try { $script:Settings | ConvertTo-Json | Set-Content $script:SettingsPath -Encoding UTF8 } catch {}
}

Load-Settings

# ============================================================================
# Section 6: Color palette
# ============================================================================

$script:ColorBg       = [System.Drawing.Color]::FromArgb(30,  30,  30)
$script:ColorBgAlt    = [System.Drawing.Color]::FromArgb(40,  40,  40)
$script:ColorBgInput  = [System.Drawing.Color]::FromArgb(50,  50,  50)
$script:ColorText     = [System.Drawing.Color]::FromArgb(220, 220, 220)
$script:ColorTextDim  = [System.Drawing.Color]::FromArgb(140, 140, 140)
$script:ColorGold     = [System.Drawing.Color]::FromArgb(218, 165, 32)
$script:ColorGreen    = [System.Drawing.Color]::FromArgb(0,   180, 0)
$script:ColorRed      = [System.Drawing.Color]::FromArgb(220, 50,  50)
$script:ColorOrange   = [System.Drawing.Color]::FromArgb(255, 140, 0)
$script:ColorBlue     = [System.Drawing.Color]::FromArgb(80,  140, 220)
$script:ColorBorder   = [System.Drawing.Color]::FromArgb(60,  60,  60)
$script:ColorProgress = [System.Drawing.Color]::FromArgb(0,   180, 0)
$script:ColorWhite    = [System.Drawing.Color]::White
$script:ColorDisabled = [System.Drawing.Color]::FromArgb(80,  80,  80)

# ============================================================================
# Section 7: Sound system
# ============================================================================

function Play-SuccessSound {
    if (-not $script:Settings.EnableSounds) { return }
    try {
        $f = Join-Path $script:SoundsDir "success.wav"
        if (Test-Path $f) { (New-Object System.Media.SoundPlayer($f)).Play() }
    } catch {}
}

function Play-FailureSound {
    if (-not $script:Settings.EnableSounds) { return }
    try {
        $f = Join-Path $script:SoundsDir "failure.wav"
        if (Test-Path $f) { (New-Object System.Media.SoundPlayer($f)).Play() }
        else { [System.Media.SystemSounds]::Hand.Play() }
    } catch {}
}

# ============================================================================
# Section 8: Utility functions
# ============================================================================

function Classify-Line($line) {
    if ($line -match ':\s*error\s*:|:\s*fatal error\s*:|^make.*\*\*\*.*Error|FAILED|undefined reference|multiple definition|collect2:\s*error|ld returned|cannot find -l|CMake Error|error:\s') {
        return "error"
    }
    if ($line -match ':\s*warning\s*:|Warning:') { return "warning" }
    return "normal"
}

function Format-ElapsedTime($seconds) {
    if ($seconds -lt 60) { return "" + $seconds + "s" }
    $m = [math]::Floor($seconds / 60)
    $s = $seconds % 60
    return "" + $m + "m" + $s + "s"
}

function Get-ExePath($name) {
    if ($name -eq "server") { return Join-Path $script:ServerBuildDir $script:ServerExeName }
    return Join-Path $script:ClientBuildDir $script:ClientExeName
}

function Test-ExeExists($name) { return Test-Path (Get-ExePath $name) }

# ============================================================================
# Section 9: Form creation
# ============================================================================

[System.Windows.Forms.Application]::EnableVisualStyles()

$script:Form = New-Object System.Windows.Forms.Form
$script:Form.Text          = "Perfect Dark 2 - Dev Window"
$script:Form.Size          = New-Object System.Drawing.Size(900, 700)
$script:Form.MinimumSize   = New-Object System.Drawing.Size(700, 500)
$script:Form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
$script:Form.BackColor     = $script:ColorBg
$script:Form.ForeColor     = $script:ColorText
$script:Form.Font          = New-UIFont $script:Settings.FontSize

$iconPath = Join-Path $script:ProjectRoot "dist\windows\icon.ico"
if (Test-Path $iconPath) { try { $script:Form.Icon = New-Object System.Drawing.Icon($iconPath) } catch {} }

# Menu strip
$menuStrip = New-Object System.Windows.Forms.MenuStrip
$menuStrip.BackColor = $script:ColorBgAlt
$menuStrip.ForeColor = $script:ColorText
$menuStrip.Renderer  = New-Object System.Windows.Forms.ToolStripProfessionalRenderer(
    (New-Object PD2Dev.DarkMenuColorTable)
)

$menuFile = New-Object System.Windows.Forms.ToolStripMenuItem("File")
$menuFile.ForeColor = $script:ColorText; $menuFile.BackColor = $script:ColorBgAlt

$miGitHub = New-Object System.Windows.Forms.ToolStripMenuItem("GitHub Repository")
$miGitHub.ForeColor = $script:ColorText; $miGitHub.BackColor = $script:ColorBgAlt
$miGitHub.Add_Click({
    $r = $script:Settings.GitHubRepo
    if ($r -ne "") { Start-Process ("https://github.com/" + $r) }
})
[void]$menuFile.DropDownItems.Add($miGitHub)

$miFolder = New-Object System.Windows.Forms.ToolStripMenuItem("Project Folder")
$miFolder.ForeColor = $script:ColorText; $miFolder.BackColor = $script:ColorBgAlt
$miFolder.Add_Click({ Start-Process "explorer.exe" -ArgumentList $script:ProjectRoot })
[void]$menuFile.DropDownItems.Add($miFolder)

$miSettings = New-Object System.Windows.Forms.ToolStripMenuItem("Settings")
$miSettings.ForeColor = $script:ColorText; $miSettings.BackColor = $script:ColorBgAlt
$miSettings.Add_Click({ Show-SettingsDialog })
[void]$menuFile.DropDownItems.Add($miSettings)

[void]$menuFile.DropDownItems.Add((New-Object System.Windows.Forms.ToolStripSeparator))

$miExit = New-Object System.Windows.Forms.ToolStripMenuItem("Exit")
$miExit.ForeColor = $script:ColorText; $miExit.BackColor = $script:ColorBgAlt
$miExit.Add_Click({ $script:Form.Close() })
[void]$menuFile.DropDownItems.Add($miExit)

[void]$menuStrip.Items.Add($menuFile)
$script:Form.MainMenuStrip = $menuStrip
$script:Form.Controls.Add($menuStrip)

# Tab control
$script:TabControl = New-Object System.Windows.Forms.TabControl
$script:TabControl.Dock     = [System.Windows.Forms.DockStyle]::Fill
$script:TabControl.DrawMode = [System.Windows.Forms.TabDrawMode]::OwnerDrawFixed
$script:TabControl.ItemSize = New-Object System.Drawing.Size(110, 28)

$script:TabControl.Add_DrawItem({
    param($sender, $e)
    try {
        $tab = $sender.TabPages[$e.Index]
        $rect = $e.Bounds
        $isSel = ($e.Index -eq $sender.SelectedIndex)
        $bg = $(if ($isSel) { $script:ColorBgAlt } else { $script:ColorBg })
        $fg = $(if ($isSel) { $script:ColorGold  } else { $script:ColorTextDim })
        $e.Graphics.FillRectangle((New-Object System.Drawing.SolidBrush($bg)), $rect)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment     = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
        $fnt = New-UIFont $script:Settings.FontSize -Bold
        $e.Graphics.DrawString($tab.Text, $fnt, (New-Object System.Drawing.SolidBrush($fg)), [System.Drawing.RectangleF]$rect, $sf)
        $fnt.Dispose(); $sf.Dispose()
        if ($isSel) {
            $pen = New-Object System.Drawing.Pen($script:ColorGold, 2)
            $e.Graphics.DrawLine($pen, $rect.Left, ($rect.Bottom - 1), ($rect.Right - 1), ($rect.Bottom - 1))
            $pen.Dispose()
        }
    } catch {}
})

$script:TabBuild    = New-Object System.Windows.Forms.TabPage
$script:TabBuild.Text      = "Build"
$script:TabBuild.BackColor = $script:ColorBg

$script:TabPlaytest = New-Object System.Windows.Forms.TabPage
$script:TabPlaytest.Text      = "Playtest"
$script:TabPlaytest.BackColor = $script:ColorBg

[void]$script:TabControl.TabPages.Add($script:TabBuild)
[void]$script:TabControl.TabPages.Add($script:TabPlaytest)

# Bottom bar (docked before TabControl so Fill gets remaining space)
$script:BottomBar = New-Object System.Windows.Forms.Panel
$script:BottomBar.Dock      = [System.Windows.Forms.DockStyle]::Bottom
$script:BottomBar.Height    = 58
$script:BottomBar.BackColor = $script:ColorBgAlt

$script:BtnRunServer = New-Object System.Windows.Forms.Button
$script:BtnRunServer.Text      = "RUN SERVER"
$script:BtnRunServer.Dock      = [System.Windows.Forms.DockStyle]::Left
$script:BtnRunServer.Width     = 450
$script:BtnRunServer.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnRunServer.FlatAppearance.BorderColor = $script:ColorOrange
$script:BtnRunServer.FlatAppearance.BorderSize  = 1
$script:BtnRunServer.ForeColor = $script:ColorOrange
$script:BtnRunServer.BackColor = $script:ColorBgAlt
$script:BtnRunServer.Font      = New-UIFont 14 -Bold
$script:BtnRunServer.Cursor    = [System.Windows.Forms.Cursors]::Hand

$script:BtnRunGame = New-Object System.Windows.Forms.Button
$script:BtnRunGame.Text      = "RUN GAME"
$script:BtnRunGame.Dock      = [System.Windows.Forms.DockStyle]::Fill
$script:BtnRunGame.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnRunGame.FlatAppearance.BorderColor = $script:ColorGreen
$script:BtnRunGame.FlatAppearance.BorderSize  = 1
$script:BtnRunGame.ForeColor = $script:ColorGreen
$script:BtnRunGame.BackColor = $script:ColorBgAlt
$script:BtnRunGame.Font      = New-UIFont 14 -Bold
$script:BtnRunGame.Cursor    = [System.Windows.Forms.Cursors]::Hand

$script:BottomBar.Controls.Add($script:BtnRunGame)
$script:BottomBar.Controls.Add($script:BtnRunServer)
$script:Form.Controls.Add($script:BottomBar)
$script:Form.Controls.Add($script:TabControl)

# ============================================================================
# Section 10: Build tab controls
# ============================================================================

$script:HeroPanel = New-Object System.Windows.Forms.Panel
$script:HeroPanel.BackColor = $script:ColorBg
$script:TabBuild.Controls.Add($script:HeroPanel)

$script:BtnBuild = New-Object System.Windows.Forms.Button
$script:BtnBuild.Text      = "BUILD"
$script:BtnBuild.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnBuild.FlatAppearance.BorderColor = $script:ColorGreen
$script:BtnBuild.FlatAppearance.BorderSize  = 2
$script:BtnBuild.ForeColor = $script:ColorGreen
$script:BtnBuild.BackColor = $script:ColorBgAlt
$script:BtnBuild.Font      = New-UIFont 22 -Bold
$script:BtnBuild.Cursor    = [System.Windows.Forms.Cursors]::Hand
$script:HeroPanel.Controls.Add($script:BtnBuild)

$script:BtnPush = New-Object System.Windows.Forms.Button
$script:BtnPush.Text      = "PUSH"
$script:BtnPush.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnPush.FlatAppearance.BorderColor = $script:ColorGold
$script:BtnPush.FlatAppearance.BorderSize  = 2
$script:BtnPush.ForeColor = $script:ColorGold
$script:BtnPush.BackColor = $script:ColorBgAlt
$script:BtnPush.Font      = New-UIFont 22 -Bold
$script:BtnPush.Cursor    = [System.Windows.Forms.Cursors]::Hand
$script:HeroPanel.Controls.Add($script:BtnPush)

$script:StatusPanel = New-Object System.Windows.Forms.Panel
$script:StatusPanel.BackColor = $script:ColorBg
$script:TabBuild.Controls.Add($script:StatusPanel)

$script:LblClientStatus = New-Object System.Windows.Forms.Label
$script:LblClientStatus.Text      = "client: --"
$script:LblClientStatus.Font      = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
$script:LblClientStatus.ForeColor = $script:ColorTextDim
$script:LblClientStatus.AutoSize  = $true
$script:LblClientStatus.Location  = New-Object System.Drawing.Point(0, 4)
$script:StatusPanel.Controls.Add($script:LblClientStatus)

$script:LblServerStatus = New-Object System.Windows.Forms.Label
$script:LblServerStatus.Text      = "server: --"
$script:LblServerStatus.Font      = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
$script:LblServerStatus.ForeColor = $script:ColorTextDim
$script:LblServerStatus.AutoSize  = $true
$script:LblServerStatus.Location  = New-Object System.Drawing.Point(0, 28)
$script:StatusPanel.Controls.Add($script:LblServerStatus)

$script:LblBuildActivity = New-Object System.Windows.Forms.Label
$script:LblBuildActivity.Text      = ""
$script:LblBuildActivity.Font      = New-Object System.Drawing.Font("Consolas", 9)
$script:LblBuildActivity.ForeColor = $script:ColorBlue
$script:LblBuildActivity.AutoSize  = $true
$script:LblBuildActivity.Location  = New-Object System.Drawing.Point(0, 56)
$script:StatusPanel.Controls.Add($script:LblBuildActivity)

$script:ProgressBack = New-Object System.Windows.Forms.Panel
$script:ProgressBack.BackColor = $script:ColorBgAlt
$script:ProgressBack.Location  = New-Object System.Drawing.Point(0, 76)
$script:ProgressBack.Height    = 18
$script:ProgressBack.Visible   = $false
$script:StatusPanel.Controls.Add($script:ProgressBack)

$script:ProgressFill = New-Object System.Windows.Forms.Panel
$script:ProgressFill.BackColor = $script:ColorProgress
$script:ProgressFill.Location  = New-Object System.Drawing.Point(0, 0)
$script:ProgressFill.Size      = New-Object System.Drawing.Size(0, 18)
$script:ProgressBack.Controls.Add($script:ProgressFill)

$script:LblProgressText = New-Object System.Windows.Forms.Label
$script:LblProgressText.Text      = ""
$script:LblProgressText.Font      = New-Object System.Drawing.Font("Consolas", 8, [System.Drawing.FontStyle]::Bold)
$script:LblProgressText.ForeColor = $script:ColorWhite
$script:LblProgressText.BackColor = [System.Drawing.Color]::Transparent
$script:LblProgressText.Location  = New-Object System.Drawing.Point(4, 2)
$script:LblProgressText.AutoSize  = $true
$script:ProgressBack.Controls.Add($script:LblProgressText)
$script:LblProgressText.BringToFront()

$script:BtnStop = New-Object System.Windows.Forms.Button
$script:BtnStop.Text      = "STOP"
$script:BtnStop.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnStop.FlatAppearance.BorderColor = $script:ColorRed
$script:BtnStop.FlatAppearance.BorderSize  = 1
$script:BtnStop.ForeColor = $script:ColorRed
$script:BtnStop.BackColor = $script:ColorBgAlt
$script:BtnStop.Font      = New-UIFont 10 -Bold
$script:BtnStop.Location  = New-Object System.Drawing.Point(0, 100)
$script:BtnStop.Size      = New-Object System.Drawing.Size(80, 28)
$script:BtnStop.Visible   = $false
$script:BtnStop.Cursor    = [System.Windows.Forms.Cursors]::Hand
$script:StatusPanel.Controls.Add($script:BtnStop)

$script:BtnCopyErrors = New-Object System.Windows.Forms.Button
$script:BtnCopyErrors.Text      = "Copy Errors"
$script:BtnCopyErrors.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnCopyErrors.FlatAppearance.BorderColor = $script:ColorRed
$script:BtnCopyErrors.FlatAppearance.BorderSize  = 1
$script:BtnCopyErrors.ForeColor = $script:ColorRed
$script:BtnCopyErrors.BackColor = $script:ColorBgAlt
$script:BtnCopyErrors.Font      = New-UIFont 9
$script:BtnCopyErrors.Location  = New-Object System.Drawing.Point(0, 132)
$script:BtnCopyErrors.Size      = New-Object System.Drawing.Size(100, 26)
$script:BtnCopyErrors.Visible   = $false
$script:BtnCopyErrors.Cursor    = [System.Windows.Forms.Cursors]::Hand
$script:StatusPanel.Controls.Add($script:BtnCopyErrors)

$script:BtnCopyLog = New-Object System.Windows.Forms.Button
$script:BtnCopyLog.Text      = "Copy Log"
$script:BtnCopyLog.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnCopyLog.FlatAppearance.BorderColor = $script:ColorTextDim
$script:BtnCopyLog.FlatAppearance.BorderSize  = 1
$script:BtnCopyLog.ForeColor = $script:ColorTextDim
$script:BtnCopyLog.BackColor = $script:ColorBgAlt
$script:BtnCopyLog.Font      = New-UIFont 9
$script:BtnCopyLog.Location  = New-Object System.Drawing.Point(108, 132)
$script:BtnCopyLog.Size      = New-Object System.Drawing.Size(80, 26)
$script:BtnCopyLog.Visible   = $false
$script:BtnCopyLog.Cursor    = [System.Windows.Forms.Cursors]::Hand
$script:StatusPanel.Controls.Add($script:BtnCopyLog)

# Version panel (right side of status area)
$script:VersionPanel = New-Object System.Windows.Forms.Panel
$script:VersionPanel.BackColor = $script:ColorBg
$script:TabBuild.Controls.Add($script:VersionPanel)

$lblVerLabel = New-Object System.Windows.Forms.Label
$lblVerLabel.Text = "version:"; $lblVerLabel.Font = New-UIFont 9
$lblVerLabel.ForeColor = $script:ColorTextDim; $lblVerLabel.AutoSize = $true
$lblVerLabel.Location = New-Object System.Drawing.Point(0, 4)
$script:VersionPanel.Controls.Add($lblVerLabel)

$script:TxtVerMajor = New-Object System.Windows.Forms.TextBox
$script:TxtVerMajor.Size = New-Object System.Drawing.Size(36, 22)
$script:TxtVerMajor.Location = New-Object System.Drawing.Point(0, 24)
$script:TxtVerMajor.BackColor = $script:ColorBgInput; $script:TxtVerMajor.ForeColor = $script:ColorText
$script:TxtVerMajor.Font = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
$script:TxtVerMajor.TextAlign = [System.Windows.Forms.HorizontalAlignment]::Center
$script:TxtVerMajor.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
$script:VersionPanel.Controls.Add($script:TxtVerMajor)

$lblD1 = New-Object System.Windows.Forms.Label
$lblD1.Text = "."; $lblD1.Font = New-UIFont 12 -Bold
$lblD1.ForeColor = $script:ColorGold; $lblD1.AutoSize = $true
$lblD1.Location = New-Object System.Drawing.Point(38, 26)
$script:VersionPanel.Controls.Add($lblD1)

$script:TxtVerMinor = New-Object System.Windows.Forms.TextBox
$script:TxtVerMinor.Size = New-Object System.Drawing.Size(36, 22)
$script:TxtVerMinor.Location = New-Object System.Drawing.Point(48, 24)
$script:TxtVerMinor.BackColor = $script:ColorBgInput; $script:TxtVerMinor.ForeColor = $script:ColorText
$script:TxtVerMinor.Font = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
$script:TxtVerMinor.TextAlign = [System.Windows.Forms.HorizontalAlignment]::Center
$script:TxtVerMinor.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
$script:VersionPanel.Controls.Add($script:TxtVerMinor)

$lblD2 = New-Object System.Windows.Forms.Label
$lblD2.Text = "."; $lblD2.Font = New-UIFont 12 -Bold
$lblD2.ForeColor = $script:ColorGold; $lblD2.AutoSize = $true
$lblD2.Location = New-Object System.Drawing.Point(86, 26)
$script:VersionPanel.Controls.Add($lblD2)

$script:TxtVerPatch = New-Object System.Windows.Forms.TextBox
$script:TxtVerPatch.Size = New-Object System.Drawing.Size(36, 22)
$script:TxtVerPatch.Location = New-Object System.Drawing.Point(96, 24)
$script:TxtVerPatch.BackColor = $script:ColorBgInput; $script:TxtVerPatch.ForeColor = $script:ColorText
$script:TxtVerPatch.Font = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
$script:TxtVerPatch.TextAlign = [System.Windows.Forms.HorizontalAlignment]::Center
$script:TxtVerPatch.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
$script:VersionPanel.Controls.Add($script:TxtVerPatch)

$script:BtnVerDown = New-Object System.Windows.Forms.Button
$script:BtnVerDown.Text = "-"; $script:BtnVerDown.Size = New-Object System.Drawing.Size(24, 22)
$script:BtnVerDown.Location = New-Object System.Drawing.Point(0, 50)
$script:BtnVerDown.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnVerDown.FlatAppearance.BorderColor = $script:ColorBorder
$script:BtnVerDown.ForeColor = $script:ColorText; $script:BtnVerDown.BackColor = $script:ColorBgAlt
$script:BtnVerDown.Font = New-UIFont 10 -Bold; $script:BtnVerDown.Cursor = [System.Windows.Forms.Cursors]::Hand
$script:VersionPanel.Controls.Add($script:BtnVerDown)

$script:BtnVerUp = New-Object System.Windows.Forms.Button
$script:BtnVerUp.Text = "+"; $script:BtnVerUp.Size = New-Object System.Drawing.Size(24, 22)
$script:BtnVerUp.Location = New-Object System.Drawing.Point(28, 50)
$script:BtnVerUp.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnVerUp.FlatAppearance.BorderColor = $script:ColorBorder
$script:BtnVerUp.ForeColor = $script:ColorText; $script:BtnVerUp.BackColor = $script:ColorBgAlt
$script:BtnVerUp.Font = New-UIFont 10 -Bold; $script:BtnVerUp.Cursor = [System.Windows.Forms.Cursors]::Hand
$script:VersionPanel.Controls.Add($script:BtnVerUp)

$lblPatch = New-Object System.Windows.Forms.Label
$lblPatch.Text = "patch"; $lblPatch.Font = New-UIFont 8
$lblPatch.ForeColor = $script:ColorTextDim; $lblPatch.AutoSize = $true
$lblPatch.Location = New-Object System.Drawing.Point(56, 54)
$script:VersionPanel.Controls.Add($lblPatch)

$script:LblAuthStatus = New-Object System.Windows.Forms.Label
$script:LblAuthStatus.Text = "auth: --"; $script:LblAuthStatus.Font = New-UIFont 9
$script:LblAuthStatus.ForeColor = $script:ColorTextDim; $script:LblAuthStatus.AutoSize = $true
$script:LblAuthStatus.Location = New-Object System.Drawing.Point(0, 80)
$script:VersionPanel.Controls.Add($script:LblAuthStatus)

$script:LblLatestRelease = New-Object System.Windows.Forms.Label
$script:LblLatestRelease.Text = "latest: --"; $script:LblLatestRelease.Font = New-UIFont 9
$script:LblLatestRelease.ForeColor = $script:ColorTextDim; $script:LblLatestRelease.AutoSize = $true
$script:LblLatestRelease.Location = New-Object System.Drawing.Point(0, 100)
$script:VersionPanel.Controls.Add($script:LblLatestRelease)

$script:BuildTimer = New-Object System.Windows.Forms.Timer
$script:BuildTimer.Interval = 100

# ============================================================================
# Section 11: Playtest tab controls
# ============================================================================

$script:QcHeaderPanel = New-Object System.Windows.Forms.Panel
$script:QcHeaderPanel.Dock      = [System.Windows.Forms.DockStyle]::Top
$script:QcHeaderPanel.Height    = 44
$script:QcHeaderPanel.BackColor = $script:ColorBgAlt
$script:TabPlaytest.Controls.Add($script:QcHeaderPanel)

$lblFilt = New-Object System.Windows.Forms.Label
$lblFilt.Text = "Filter:"; $lblFilt.Font = New-UIFont 9
$lblFilt.ForeColor = $script:ColorTextDim; $lblFilt.AutoSize = $true
$lblFilt.Location = New-Object System.Drawing.Point(8, 14)
$script:QcHeaderPanel.Controls.Add($lblFilt)

$script:CmbFilter = New-Object System.Windows.Forms.ComboBox
$script:CmbFilter.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
$script:CmbFilter.BackColor = $script:ColorBgInput; $script:CmbFilter.ForeColor = $script:ColorText
$script:CmbFilter.Font = New-UIFont 9
$script:CmbFilter.Location = New-Object System.Drawing.Point(52, 10)
$script:CmbFilter.Size = New-Object System.Drawing.Size(90, 22)
[void]$script:CmbFilter.Items.Add("All")
[void]$script:CmbFilter.Items.Add("Pending")
[void]$script:CmbFilter.Items.Add("Pass")
[void]$script:CmbFilter.Items.Add("Fail")
[void]$script:CmbFilter.Items.Add("Skip")
$script:CmbFilter.SelectedIndex = 0
$script:QcHeaderPanel.Controls.Add($script:CmbFilter)

$script:LblQcSummary = New-Object System.Windows.Forms.Label
$script:LblQcSummary.Text = "Pass:0  Fail:0  Skip:0  Pending:0"
$script:LblQcSummary.Font = New-Object System.Drawing.Font("Consolas", 9, [System.Drawing.FontStyle]::Bold)
$script:LblQcSummary.ForeColor = $script:ColorTextDim; $script:LblQcSummary.AutoSize = $true
$script:LblQcSummary.Location = New-Object System.Drawing.Point(156, 14)
$script:QcHeaderPanel.Controls.Add($script:LblQcSummary)

$script:BtnQcRefresh = New-Object System.Windows.Forms.Button
$script:BtnQcRefresh.Text = "Refresh"
$script:BtnQcRefresh.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnQcRefresh.FlatAppearance.BorderColor = $script:ColorTextDim
$script:BtnQcRefresh.FlatAppearance.BorderSize  = 1
$script:BtnQcRefresh.ForeColor = $script:ColorTextDim; $script:BtnQcRefresh.BackColor = $script:ColorBgAlt
$script:BtnQcRefresh.Font = New-UIFont 9
$script:BtnQcRefresh.Size = New-Object System.Drawing.Size(68, 26)
$script:BtnQcRefresh.Cursor = [System.Windows.Forms.Cursors]::Hand
$script:QcHeaderPanel.Controls.Add($script:BtnQcRefresh)

$script:BtnQcReset = New-Object System.Windows.Forms.Button
$script:BtnQcReset.Text = "Reset"
$script:BtnQcReset.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
$script:BtnQcReset.FlatAppearance.BorderColor = $script:ColorRed
$script:BtnQcReset.FlatAppearance.BorderSize  = 1
$script:BtnQcReset.ForeColor = $script:ColorRed; $script:BtnQcReset.BackColor = $script:ColorBgAlt
$script:BtnQcReset.Font = New-UIFont 9
$script:BtnQcReset.Size = New-Object System.Drawing.Size(60, 26)
$script:BtnQcReset.Cursor = [System.Windows.Forms.Cursors]::Hand
$script:QcHeaderPanel.Controls.Add($script:BtnQcReset)

# DataGridView
$script:QcGrid = New-Object System.Windows.Forms.DataGridView
$script:QcGrid.Dock                        = [System.Windows.Forms.DockStyle]::Fill
$script:QcGrid.AllowUserToAddRows          = $false
$script:QcGrid.AllowUserToDeleteRows       = $false
$script:QcGrid.AllowUserToResizeRows       = $false
$script:QcGrid.MultiSelect                 = $false
$script:QcGrid.SelectionMode               = [System.Windows.Forms.DataGridViewSelectionMode]::FullRowSelect
$script:QcGrid.BackgroundColor             = $script:ColorBg
$script:QcGrid.GridColor                   = $script:ColorBorder
$script:QcGrid.BorderStyle                 = [System.Windows.Forms.BorderStyle]::None
$script:QcGrid.RowHeadersVisible           = $false
$script:QcGrid.ColumnHeadersHeightSizeMode = [System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode]::DisableResizing
$script:QcGrid.ColumnHeadersHeight         = 28
$script:QcGrid.RowTemplate.Height          = 28
$script:QcGrid.ScrollBars                  = [System.Windows.Forms.ScrollBars]::Vertical
$script:QcGrid.AutoSizeColumnsMode         = [System.Windows.Forms.DataGridViewAutoSizeColumnsMode]::None
$script:QcGrid.ClipboardCopyMode           = [System.Windows.Forms.DataGridViewClipboardCopyMode]::Disable

try {
    $dbProp = [System.Windows.Forms.DataGridView].GetProperty(
        "DoubleBuffered",
        [System.Reflection.BindingFlags]::Instance -bor [System.Reflection.BindingFlags]::NonPublic
    )
    $dbProp.SetValue($script:QcGrid, $true, $null)
} catch {}

$script:QcGrid.DefaultCellStyle.BackColor           = $script:ColorBg
$script:QcGrid.DefaultCellStyle.ForeColor           = $script:ColorText
$script:QcGrid.DefaultCellStyle.SelectionBackColor  = [System.Drawing.Color]::FromArgb(60, 60, 80)
$script:QcGrid.DefaultCellStyle.SelectionForeColor  = $script:ColorText
$script:QcGrid.DefaultCellStyle.Font                = New-Object System.Drawing.Font("Consolas", 9)
$script:QcGrid.ColumnHeadersDefaultCellStyle.BackColor = $script:ColorBgAlt
$script:QcGrid.ColumnHeadersDefaultCellStyle.ForeColor = $script:ColorGold
$script:QcGrid.ColumnHeadersDefaultCellStyle.Font      = New-UIFont 9 -Bold
$script:QcGrid.EnableHeadersVisualStyles = $false
$script:QcGrid.AlternatingRowsDefaultCellStyle.BackColor = [System.Drawing.Color]::FromArgb(35, 35, 35)

$colNum = New-Object System.Windows.Forms.DataGridViewTextBoxColumn
$colNum.Name = "Num"; $colNum.HeaderText = "#"; $colNum.Width = 40
$colNum.ReadOnly = $true; $colNum.SortMode = [System.Windows.Forms.DataGridViewColumnSortMode]::NotSortable
[void]$script:QcGrid.Columns.Add($colNum)

$colStatus = New-Object System.Windows.Forms.DataGridViewComboBoxColumn
$colStatus.Name = "Status"; $colStatus.HeaderText = "Status"; $colStatus.Width = 90
$colStatus.SortMode = [System.Windows.Forms.DataGridViewColumnSortMode]::NotSortable
$colStatus.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
[void]$colStatus.Items.Add("Pending")
[void]$colStatus.Items.Add("Pass")
[void]$colStatus.Items.Add("Fail")
[void]$colStatus.Items.Add("Skip")
[void]$script:QcGrid.Columns.Add($colStatus)

$colTest = New-Object System.Windows.Forms.DataGridViewTextBoxColumn
$colTest.Name = "Test"; $colTest.HeaderText = "Test"; $colTest.Width = 280
$colTest.ReadOnly = $true; $colTest.SortMode = [System.Windows.Forms.DataGridViewColumnSortMode]::NotSortable
$colTest.DefaultCellStyle.WrapMode = [System.Windows.Forms.DataGridViewTriState]::True
[void]$script:QcGrid.Columns.Add($colTest)

$colExpected = New-Object System.Windows.Forms.DataGridViewTextBoxColumn
$colExpected.Name = "Expected"; $colExpected.HeaderText = "Expected"; $colExpected.Width = 180
$colExpected.ReadOnly = $true; $colExpected.SortMode = [System.Windows.Forms.DataGridViewColumnSortMode]::NotSortable
$colExpected.DefaultCellStyle.WrapMode = [System.Windows.Forms.DataGridViewTriState]::True
[void]$script:QcGrid.Columns.Add($colExpected)

$colNotes = New-Object System.Windows.Forms.DataGridViewTextBoxColumn
$colNotes.Name = "Notes"; $colNotes.HeaderText = "Notes"
$colNotes.AutoSizeMode = [System.Windows.Forms.DataGridViewAutoSizeColumnMode]::Fill
$colNotes.SortMode = [System.Windows.Forms.DataGridViewColumnSortMode]::NotSortable
[void]$script:QcGrid.Columns.Add($colNotes)

$script:TabPlaytest.Controls.Add($script:QcGrid)

$script:NotesSaveTimer = New-Object System.Windows.Forms.Timer
$script:NotesSaveTimer.Interval = 500
$script:NotesSaveTimer.Add_Tick({
    try { $this.Stop(); $this.Dispose(); $script:NotesSaveTimer = $null; Save-QcFile } catch {}
})

# ============================================================================
# Section 12: QC file management
# ============================================================================

function Parse-QcStatus($raw) {
    $r = $raw.Trim()
    if ($r -match '^\[x\]$' -or $r -match '^\(x\)$' -or $r -eq '[+]' -or $r -match 'PASS' -or $r -match 'pass') { return "Pass" }
    if ($r -match '^\[!\]$' -or $r -match 'FAIL'  -or $r -match 'fail') { return "Fail" }
    if ($r -match '^\[-\]$' -or $r -match '^N/?A$' -or $r -match '[Ss]kip') { return "Skip" }
    return "Pending"
}

function StatusToMdMarker($status) {
    switch ($status) {
        "Pass"  { return "[x]" }
        "Fail"  { return "[!]" }
        "Skip"  { return "[-]" }
        default { return "[ ]" }
    }
}

function Load-QcFile {
    $script:QcData = @()
    if (-not (Test-Path $script:QcFilePath)) { return }
    try { $lines = Get-Content $script:QcFilePath -Encoding UTF8 -ErrorAction Stop } catch { return }
    $curSec = ""
    foreach ($line in $lines) {
        if ($line -match '^##\s+(.+)$') {
            $curSec = $Matches[1].Trim()
            $script:QcData += @{Type="section"; Title=$curSec; Num=""; Test=$curSec; Expected=""; Status=""; Notes=""}
            continue
        }
        if ($line -match '^\|') {
            if ($line -match '^\|\s*[-:]+') { continue }
            $cells = $line -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
            if ($cells.Count -lt 4) { continue }
            $n=$cells[0]; $t=$cells[1]; $exp=$cells[2]; $st=$cells[3]
            $nt = $(if ($cells.Count -ge 5) { $cells[4] } else { "" })
            if ($n -eq '#' -or $t -eq 'Test') { continue }
            if ($n -notmatch '^\d+$') { continue }
            $script:QcData += @{Type="test"; Title=$curSec; Num=$n; Test=$t; Expected=$exp; Status=(Parse-QcStatus $st); Notes=$nt}
        }
    }
}

function Save-QcFile {
    if (-not (Test-Path $script:QcFilePath)) { return }
    try { $lines = Get-Content $script:QcFilePath -Encoding UTF8 -ErrorAction Stop } catch { return }
    $lookup = @{}
    foreach ($row in $script:QcData) { if ($row.Type -eq "test") { $lookup[$row.Num] = $row } }
    $out = @()
    foreach ($line in $lines) {
        if ($line -match '^\|' -and $line -notmatch '^\|\s*[-:]+') {
            $cells = $line -split '\|'
            if ($cells.Count -ge 5) {
                $nCell = $cells[1].Trim()
                if ($nCell -match '^\d+$' -and $lookup.ContainsKey($nCell)) {
                    $cells[4] = " " + (StatusToMdMarker $lookup[$nCell].Status) + " "
                    if ($cells.Count -ge 7) { $cells[5] = " " + $lookup[$nCell].Notes + " " }
                    $line = $cells -join '|'
                }
            }
        }
        $out += $line
    }
    try { Set-Content -Path $script:QcFilePath -Value $out -Encoding UTF8 -ErrorAction Stop } catch {}
}

function Update-QcSummary {
    if ($null -eq $script:LblQcSummary) { return }
    $pass=0; $fail=0; $skip=0; $pend=0
    foreach ($row in $script:QcData) {
        if ($row.Type -ne "test") { continue }
        switch ($row.Status) { "Pass"{$pass++} "Fail"{$fail++} "Skip"{$skip++} default{$pend++} }
    }
    $script:LblQcSummary.Text = "Pass:" + $pass + "  Fail:" + $fail + "  Skip:" + $skip + "  Pending:" + $pend
    $script:LblQcSummary.ForeColor = $(if ($fail -gt 0) { $script:ColorRed } elseif ($pend -eq 0 -and $pass -gt 0) { $script:ColorGreen } else { $script:ColorTextDim })
}

function Get-StatusColor($status) {
    switch ($status) { "Pass"{return $script:ColorGreen} "Fail"{return $script:ColorRed} "Skip"{return $script:ColorTextDim} default{return $script:ColorText} }
}

function Populate-QcGrid {
    if ($null -eq $script:QcGrid) { return }
    $filter = $(if ($null -ne $script:CmbFilter) { $script:CmbFilter.SelectedItem } else { "All" })
    $filterStr = $(if ($null -eq $filter) { "All" } else { $filter.ToString() })
    $script:QcGrid.SuspendLayout()
    $script:QcGrid.Rows.Clear()
    $secBg  = [System.Drawing.Color]::FromArgb(48, 40, 20)
    $secFnt = New-UIFont 9 -Bold
    foreach ($row in $script:QcData) {
        if ($row.Type -eq "section") {
            $ri = $script:QcGrid.Rows.Add()
            $r  = $script:QcGrid.Rows[$ri]
            $r.Cells["Num"].Value = ""; $r.Cells["Status"].Value = ""
            $r.Cells["Test"].Value = "--- " + $row.Title + " ---"
            $r.Cells["Expected"].Value = ""; $r.Cells["Notes"].Value = ""
            $r.ReadOnly = $true; $r.Tag = "section"
            $r.DefaultCellStyle.BackColor = $secBg
            $r.DefaultCellStyle.ForeColor = $script:ColorGold
            $r.DefaultCellStyle.Font = $secFnt
            $r.DefaultCellStyle.SelectionBackColor = $secBg
            $r.DefaultCellStyle.SelectionForeColor = $script:ColorGold
            continue
        }
        if ($filterStr -ne "All" -and $row.Status -ne $filterStr) { continue }
        $ri = $script:QcGrid.Rows.Add()
        $r  = $script:QcGrid.Rows[$ri]
        $r.Cells["Num"].Value      = $row.Num
        $r.Cells["Test"].Value     = $row.Test
        $r.Cells["Expected"].Value = $row.Expected
        $r.Cells["Notes"].Value    = $row.Notes
        $r.Tag = $row.Num
        try { $r.Cells["Status"].Value = $row.Status } catch {}
        $r.Cells["Status"].Style.ForeColor = (Get-StatusColor $row.Status)
    }
    $script:QcGrid.ResumeLayout()
    Update-QcSummary
}

function Refresh-QcGrid { Load-QcFile; Populate-QcGrid }

function Reset-QcStatuses {
    $confirm = [System.Windows.Forms.MessageBox]::Show(
        "Reset ALL test statuses to Pending?", "Reset QC Tests",
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Warning
    )
    if ($confirm -ne [System.Windows.Forms.DialogResult]::Yes) { return }
    foreach ($row in $script:QcData) { if ($row.Type -eq "test") { $row.Status = "Pending" } }
    Save-QcFile; Populate-QcGrid
}

$script:QcGrid.Add_CurrentCellDirtyStateChanged({
    try {
        if ($null -ne $script:QcGrid -and $script:QcGrid.IsCurrentCellDirty) {
            $script:QcGrid.CommitEdit([System.Windows.Forms.DataGridViewDataErrorContexts]::Commit)
        }
    } catch {}
})

$script:QcGrid.Add_CellValueChanged({
    param($sender, $e)
    try {
        if ($e.RowIndex -lt 0 -or $e.ColumnIndex -lt 0) { return }
        $r = $script:QcGrid.Rows[$e.RowIndex]
        if ($r.Tag -eq "section") { return }
        $rowNum = $r.Tag
        $tgt = $null
        foreach ($qr in $script:QcData) { if ($qr.Type -eq "test" -and $qr.Num -eq $rowNum) { $tgt = $qr; break } }
        if ($null -eq $tgt) { return }
        $colName = $script:QcGrid.Columns[$e.ColumnIndex].Name
        if ($colName -eq "Status") {
            $v = $r.Cells["Status"].Value
            if ($null -ne $v) {
                $tgt.Status = $v.ToString()
                $r.Cells["Status"].Style.ForeColor = (Get-StatusColor $tgt.Status)
            }
            Save-QcFile; Update-QcSummary
        } elseif ($colName -eq "Notes") {
            $v = $r.Cells["Notes"].Value
            $tgt.Notes = $(if ($null -ne $v) { $v.ToString() } else { "" })
            if ($null -eq $script:NotesSaveTimer) {
                $script:NotesSaveTimer = New-Object System.Windows.Forms.Timer
                $script:NotesSaveTimer.Interval = 500
                $script:NotesSaveTimer.Add_Tick({
                    try { $this.Stop(); $this.Dispose(); $script:NotesSaveTimer = $null; Save-QcFile } catch {}
                })
            }
            $script:NotesSaveTimer.Stop(); $script:NotesSaveTimer.Start()
        }
    } catch {}
})

$script:QcGrid.Add_CellPainting({
    param($sender, $e)
    try {
        if ($e.RowIndex -lt 0) { return }
        $r = $script:QcGrid.Rows[$e.RowIndex]
        if ($r.Tag -ne "section") { return }
        $e.PaintBackground($e.CellBounds, $true)
        if ($e.ColumnIndex -eq 2) {
            $br  = New-Object System.Drawing.SolidBrush($script:ColorGold)
            $fnt = New-UIFont 9 -Bold
            $sf  = New-Object System.Drawing.StringFormat
            $sf.Alignment = [System.Drawing.StringAlignment]::Near
            $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
            $rc  = New-Object System.Drawing.RectangleF(($e.CellBounds.X + 4), $e.CellBounds.Y, ($e.CellBounds.Width - 8), $e.CellBounds.Height)
            $e.Graphics.DrawString($e.Value, $fnt, $br, $rc, $sf)
            $br.Dispose(); $fnt.Dispose(); $sf.Dispose()
        }
        $e.Handled = $true
    } catch {}
})

$script:CmbFilter.Add_SelectedIndexChanged({ try { Populate-QcGrid } catch {} })

# ============================================================================
# Section 13: Version management
# ============================================================================

function Get-ProjectVersion {
    $ver = @{Major=0; Minor=0; Patch=0}
    $cp  = Join-Path $script:ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path $cp)) { return $ver }
    try {
        $c = Get-Content $cp -Raw -ErrorAction Stop
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
        $c = Get-Content $cp -Raw -ErrorAction Stop
        $c = $c -replace '(VERSION_SEM_MAJOR\s+)\d+', ("`${1}" + $major)
        $c = $c -replace '(VERSION_SEM_MINOR\s+)\d+', ("`${1}" + $minor)
        $c = $c -replace '(VERSION_SEM_PATCH\s+)\d+', ("`${1}" + $patch)
        Set-Content -Path $cp -Value $c -NoNewline -Encoding UTF8 -ErrorAction Stop
    } catch {}
}

function Get-UiVersion {
    $major=0; $minor=0; $patch=0
    try { $major = [int]$script:TxtVerMajor.Text } catch {}
    try { $minor = [int]$script:TxtVerMinor.Text } catch {}
    try { $patch = [int]$script:TxtVerPatch.Text } catch {}
    return @{Major=$major; Minor=$minor; Patch=$patch}
}

function Refresh-VersionDisplay {
    if ($null -eq $script:TxtVerMajor) { return }
    $ver = Get-ProjectVersion
    $script:TxtVerMajor.Text = "" + $ver.Major
    $script:TxtVerMinor.Text = "" + $ver.Minor
    $script:TxtVerPatch.Text = "" + $ver.Patch
}

function Load-ReleaseCache {
    if (-not (Test-Path $script:ReleaseCachePath)) { return $null }
    try { return (Get-Content $script:ReleaseCachePath -Raw -ErrorAction Stop | ConvertFrom-Json) } catch { return $null }
}

function Save-ReleaseCache($data) {
    try { $data | ConvertTo-Json -Depth 3 | Set-Content $script:ReleaseCachePath -Encoding UTF8 -ErrorAction Stop } catch {}
}

function Update-ReleaseCacheUI($data) {
    if ($null -eq $script:LblLatestRelease) { return }
    if ($null -eq $data) { $script:LblLatestRelease.Text = "latest: --"; return }
    try {
        $tag = $data.tag_name
        if ($null -eq $tag) { $tag = "unknown" }
        $script:LblLatestRelease.Text = "latest: " + $tag
        $script:LatestRelease = $data
    } catch {}
}

# ============================================================================
# Section 14: Git operations
# ============================================================================

function Update-GitChangeCount {
    if ($script:GitBusy) { return }
    if (([DateTime]::Now - $script:LastGitCheck).TotalSeconds -lt 5) { return }
    $script:LastGitCheck = [DateTime]::Now
    try {
        $st = git -C $script:ProjectRoot status --porcelain 2>$null
        $script:GitChangeCount = $(if ($st) { ($st | Measure-Object).Count } else { 0 })
    } catch { $script:GitChangeCount = 0 }
}

function Auto-Commit {
    $script:GitBusy = $true
    $savedEAP = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $lock = Join-Path $script:ProjectRoot ".git\index.lock"
        if (Test-Path $lock) { Remove-Item $lock -Force -ErrorAction SilentlyContinue }
        $st = git -C $script:ProjectRoot status --porcelain 2>$null
        if (-not $st) { return $true }
        $ver = Get-ProjectVersion
        $msg = "Build v" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch + " - auto-commit before build"
        git -C $script:ProjectRoot add -A 2>$null | Out-Null
        git -C $script:ProjectRoot commit -m $msg 2>$null | Out-Null
        $script:LastGitCheck = [DateTime]::MinValue
        return ($LASTEXITCODE -eq 0)
    } finally {
        $ErrorActionPreference = $savedEAP
        $script:GitBusy = $false
    }
}

function Start-ManualCommit {
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = "Commit Changes"
    $dlg.Size = New-Object System.Drawing.Size(480, 190)
    $dlg.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterParent
    $dlg.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $dlg.MaximizeBox = $false; $dlg.MinimizeBox = $false
    $dlg.BackColor = $script:ColorBgAlt; $dlg.ForeColor = $script:ColorText
    $lbl = New-Object System.Windows.Forms.Label
    $lbl.Text = "Commit message (" + $script:GitChangeCount + " file(s)):"
    $lbl.Font = New-UIFont 10; $lbl.ForeColor = $script:ColorText
    $lbl.Location = New-Object System.Drawing.Point(12, 12); $lbl.AutoSize = $true
    $dlg.Controls.Add($lbl)
    $ver = Get-ProjectVersion
    $txt = New-Object System.Windows.Forms.TextBox
    $txt.Location = New-Object System.Drawing.Point(12, 36); $txt.Size = New-Object System.Drawing.Size(440, 24)
    $txt.BackColor = $script:ColorBgInput; $txt.ForeColor = $script:ColorText
    $txt.Font = New-Object System.Drawing.Font("Consolas", 10)
    $txt.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
    $txt.Text = "v" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch + " -"
    $dlg.Controls.Add($txt)
    $btnOk = New-Object System.Windows.Forms.Button
    $btnOk.Text = "Commit"; $btnOk.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $btnOk.Location = New-Object System.Drawing.Point(260, 116); $btnOk.Size = New-Object System.Drawing.Size(90, 30)
    $btnOk.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
    $btnOk.FlatAppearance.BorderColor = $script:ColorGreen
    $btnOk.ForeColor = $script:ColorGreen; $btnOk.BackColor = $script:ColorBgAlt
    $btnOk.Font = New-UIFont 10 -Bold
    $dlg.Controls.Add($btnOk); $dlg.AcceptButton = $btnOk
    $btnCancel = New-Object System.Windows.Forms.Button
    $btnCancel.Text = "Cancel"; $btnCancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $btnCancel.Location = New-Object System.Drawing.Point(360, 116); $btnCancel.Size = New-Object System.Drawing.Size(90, 30)
    $btnCancel.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
    $btnCancel.FlatAppearance.BorderColor = $script:ColorTextDim
    $btnCancel.ForeColor = $script:ColorTextDim; $btnCancel.BackColor = $script:ColorBgAlt
    $btnCancel.Font = New-UIFont 10 -Bold
    $dlg.Controls.Add($btnCancel); $dlg.CancelButton = $btnCancel
    $res = $dlg.ShowDialog($script:Form)
    $msg = $txt.Text.Trim(); $dlg.Dispose()
    if ($res -ne [System.Windows.Forms.DialogResult]::OK -or $msg -eq "") { return }
    $script:GitBusy = $true
    $savedEAP = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        git -C $script:ProjectRoot add -A 2>$null | Out-Null
        git -C $script:ProjectRoot commit -m $msg 2>$null | Out-Null
        $script:LastGitCheck = [DateTime]::MinValue
        Update-GitChangeCount
    } finally {
        $ErrorActionPreference = $savedEAP
        $script:GitBusy = $false
    }
}

# ============================================================================
# Section 15: Build pipeline
# ============================================================================

function Copy-AddinFiles {
    if ($script:CurrentBuildTarget -eq "server") { return }
    $dataDir = Join-Path $script:AddinDir "data"
    if (Test-Path $dataDir) {
        try { Copy-Item $dataDir -Destination $script:ClientBuildDir -Recurse -Force -ErrorAction Stop } catch {}
    }
}

function Stop-Build {
    if ($null -ne $script:BuildProcess) { try { $script:BuildProcess.Kill() } catch {}; $script:BuildProcess = $null }
    $script:BuildStepQueue.Clear()
    $script:BuildTimer.Stop()
    $script:IsBuilding = $false; $script:IsPushing = $false
    if ($null -ne $script:BtnBuild) { $script:BtnBuild.Enabled = $true }
    if ($null -ne $script:BtnPush)  { $script:BtnPush.Enabled  = $true }
    if ($null -ne $script:BtnStop)  { $script:BtnStop.Visible  = $false }
    if ($null -ne $script:ProgressBack) { $script:ProgressBack.Visible = $false }
    if ($null -ne $script:LblBuildActivity) { $script:LblBuildActivity.Text = "Stopped." }
}

function Start-Build-Step($step) {
    $script:CurrentStepName = $step.Name
    $script:CurrentBuildTarget = $step.Target
    $script:OutputQueue     = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
    $script:StepStartTime   = [DateTime]::Now
    $script:LastOutputTime  = [DateTime]::Now
    $script:BuildPercent    = 0
    if ($null -ne $script:ProgressFill) { $script:ProgressFill.Size = New-Object System.Drawing.Size(0, 18) }
    if ($null -ne $script:LblBuildActivity) { $script:LblBuildActivity.Text = $step.Name + "..." }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $step.Exe; $psi.Arguments = $step.Args
    $psi.WorkingDirectory = $script:ProjectRoot
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables["PATH"]         = $env:PATH
    $psi.EnvironmentVariables["MSYSTEM"]      = "MINGW64"
    $psi.EnvironmentVariables["MINGW_PREFIX"] = "/mingw64"
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        [void]$proc.Start()
        $script:BuildProcess = $proc
        [PD2Dev.AsyncLineReader]::StartReading($proc.StandardOutput, $script:OutputQueue, "OUT:")
        [PD2Dev.AsyncLineReader]::StartReading($proc.StandardError,  $script:OutputQueue, "ERR:")
        $script:BuildTimer.Start()
    } catch {
        $script:IsBuilding = $false; $script:IsPushing = $false
        if ($null -ne $script:BtnBuild) { $script:BtnBuild.Enabled = $true }
        if ($null -ne $script:BtnPush)  { $script:BtnPush.Enabled  = $true }
        if ($null -ne $script:BtnStop)  { $script:BtnStop.Visible  = $false }
        if ($null -ne $script:LblBuildActivity) { $script:LblBuildActivity.Text = "ERROR starting: " + $step.Exe }
    }
}

function Get-BuildSteps {
    $cores = $(if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { "4" })
    $steps = [System.Collections.ArrayList]::new()
    [void]$steps.Add(@{Name="Configure (client)"; Exe=$script:CMake; Target="client"; Args="-G `"Unix Makefiles`" -DCMAKE_MAKE_PROGRAM=`"" + $script:Make + "`" -DCMAKE_C_COMPILER=`"" + $script:CC + "`" -B `"" + $script:ClientBuildDir + "`" -S `"" + $script:ProjectRoot + "`""})
    [void]$steps.Add(@{Name="Build (client)";     Exe=$script:CMake; Target="client"; Args="--build `"" + $script:ClientBuildDir + "`" --target pd -- -j" + $cores + " -k"})
    [void]$steps.Add(@{Name="Configure (server)"; Exe=$script:CMake; Target="server"; Args="-G `"Unix Makefiles`" -DCMAKE_MAKE_PROGRAM=`"" + $script:Make + "`" -DCMAKE_C_COMPILER=`"" + $script:CC + "`" -B `"" + $script:ServerBuildDir + "`" -S `"" + $script:ProjectRoot + "`""})
    [void]$steps.Add(@{Name="Build (server)";     Exe=$script:CMake; Target="server"; Args="--build `"" + $script:ServerBuildDir + "`" --target pd-server -- -j" + $cores + " -k"})
    return $steps
}

function Start-Build {
    if ($script:IsBuilding) { return }
    $script:IsBuilding = $true
    $script:ClientErrors.Clear(); $script:ServerErrors.Clear(); $script:AllOutput.Clear()
    $script:ClientBuildResult = $null; $script:ServerBuildResult = $null
    $script:ClientBuildTime = 0; $script:ServerBuildTime = 0
    $script:HasBuildErrors = $false; $script:CurrentBuildTarget = "client"
    if ($null -ne $script:LblClientStatus) { $script:LblClientStatus.Text = "client: building..."; $script:LblClientStatus.ForeColor = $script:ColorBlue }
    if ($null -ne $script:LblServerStatus) { $script:LblServerStatus.Text = "server: --"; $script:LblServerStatus.ForeColor = $script:ColorTextDim }
    if ($null -ne $script:BtnBuild) { $script:BtnBuild.Enabled = $false }
    if ($null -ne $script:BtnPush)  { $script:BtnPush.Enabled  = $false }
    if ($null -ne $script:BtnStop)  { $script:BtnStop.Visible  = $true }
    if ($null -ne $script:BtnCopyErrors) { $script:BtnCopyErrors.Visible = $false }
    if ($null -ne $script:BtnCopyLog)    { $script:BtnCopyLog.Visible    = $false }
    if ($null -ne $script:ProgressBack)  { $script:ProgressBack.Visible  = $true }
    if ($null -ne $script:ProgressFill)  { $script:ProgressFill.BackColor = $script:ColorProgress }
    Auto-Commit | Out-Null
    $script:BuildStepQueue.Clear()
    foreach ($s in (Get-BuildSteps)) { [void]$script:BuildStepQueue.Add($s) }
    $first = $script:BuildStepQueue[0]; $script:BuildStepQueue.RemoveAt(0)
    Start-Build-Step $first
}

$script:BuildTimer.Add_Tick({
    try {
        $maxPer = 80; $count = 0; $line = $null
        while ($count -lt $maxPer -and $script:OutputQueue.TryDequeue([ref]$line)) {
            $text = $line.Substring(4)
            [void]$script:AllOutput.Add($text)
            $cls  = Classify-Line $text
            if ($cls -eq "error") {
                $script:HasBuildErrors = $true
                if ($script:CurrentBuildTarget -eq "server") { [void]$script:ServerErrors.Add($text) }
                else { [void]$script:ClientErrors.Add($text) }
                if ($null -ne $script:ProgressFill) { $script:ProgressFill.BackColor = $script:ColorRed }
            }
            if ($text -match '^\[\s*(\d+)%\]') {
                $pct = [int]$Matches[1]
                if ($pct -ge $script:BuildPercent) {
                    $script:BuildPercent = $pct
                    if ($null -ne $script:ProgressBack -and $null -ne $script:ProgressFill) {
                        $fw = [math]::Floor(($pct / 100.0) * $script:ProgressBack.Width)
                        $script:ProgressFill.Size = New-Object System.Drawing.Size($fw, 18)
                    }
                    if ($null -ne $script:LblProgressText) { $script:LblProgressText.Text = "" + $pct + "% - " + $script:CurrentStepName }
                }
            }
            $script:LastOutputTime = [DateTime]::Now; $count++
        }

        if ($null -ne $script:BuildProcess -and -not $script:BuildProcess.HasExited) {
            $el  = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
            $sil = [math]::Floor(([DateTime]::Now - $script:LastOutputTime).TotalSeconds)
            if ($sil -gt 2) {
                $spin = $script:SpinnerChars[$script:SpinnerIndex % 4]; $script:SpinnerIndex++
                if ($null -ne $script:LblBuildActivity) { $script:LblBuildActivity.Text = $script:CurrentStepName + " " + $spin + " " + $el + "s" }
            } else {
                if ($null -ne $script:LblBuildActivity) { $script:LblBuildActivity.Text = $script:CurrentStepName + " (" + $el + "s)" }
            }
            return
        }

        if ($null -ne $script:BuildProcess -and $script:BuildProcess.HasExited -and $script:OutputQueue.IsEmpty) {
            $script:BuildTimer.Stop()
            $exitCode = $script:BuildProcess.ExitCode
            $elapsed  = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
            try { $script:BuildProcess.Dispose() } catch {}
            $script:BuildProcess = $null

            if ($exitCode -ne 0) {
                if ($script:CurrentBuildTarget -eq "client") {
                    $script:ClientBuildResult = "FAILED"; $script:ClientBuildTime = $elapsed
                    if ($null -ne $script:LblClientStatus) { $script:LblClientStatus.Text = "client: FAILED (" + (Format-ElapsedTime $elapsed) + ")"; $script:LblClientStatus.ForeColor = $script:ColorRed }
                } else {
                    $script:ServerBuildResult = "FAILED"; $script:ServerBuildTime = $elapsed
                    if ($null -ne $script:LblServerStatus) { $script:LblServerStatus.Text = "server: FAILED (" + (Format-ElapsedTime $elapsed) + ")"; $script:LblServerStatus.ForeColor = $script:ColorRed }
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
                        if ($null -ne $script:LblClientStatus) { $script:LblClientStatus.Text = "client: SUCCESS (" + (Format-ElapsedTime $elapsed) + ")"; $script:LblClientStatus.ForeColor = $script:ColorGreen }
                    } else {
                        $script:ServerBuildResult = "SUCCESS"; $script:ServerBuildTime = $elapsed
                        if ($null -ne $script:LblServerStatus) { $script:LblServerStatus.Text = "server: SUCCESS (" + (Format-ElapsedTime $elapsed) + ")"; $script:LblServerStatus.ForeColor = $script:ColorGreen }
                    }
                }
            }

            if ($script:BuildStepQueue.Count -gt 0) {
                $next = $script:BuildStepQueue[0]; $script:BuildStepQueue.RemoveAt(0)
                if ($next.Target -eq "server" -and $null -ne $script:LblServerStatus) {
                    $script:LblServerStatus.Text = "server: building..."; $script:LblServerStatus.ForeColor = $script:ColorBlue
                }
                if ($null -ne $script:ProgressFill) { $script:ProgressFill.BackColor = $script:ColorProgress }
                Start-Build-Step $next; $script:BuildTimer.Start()
            } else {
                $anyErr = $script:HasBuildErrors -or ($script:ClientBuildResult -eq "FAILED") -or ($script:ServerBuildResult -eq "FAILED")
                if (-not $anyErr) { Copy-AddinFiles; Play-SuccessSound } else { Play-FailureSound }
                if ($null -ne $script:ProgressFill) {
                    $script:ProgressFill.BackColor = $(if ($anyErr) { $script:ColorRed } else { $script:ColorGreen })
                    if ($null -ne $script:ProgressBack) { $script:ProgressFill.Size = New-Object System.Drawing.Size($script:ProgressBack.Width, 18) }
                }
                if ($null -ne $script:LblBuildActivity) { $script:LblBuildActivity.Text = $(if ($anyErr) { "Build complete (with errors)" } else { "Build complete" }) }
                $errCnt = $script:ClientErrors.Count + $script:ServerErrors.Count
                if ($null -ne $script:BtnCopyErrors) { $script:BtnCopyErrors.Visible = ($errCnt -gt 0) }
                if ($null -ne $script:BtnCopyLog)    { $script:BtnCopyLog.Visible    = $true }
                $script:IsBuilding = $false; $script:IsPushing = $false
                if ($null -ne $script:BtnBuild) { $script:BtnBuild.Enabled = $true }
                if ($null -ne $script:BtnPush)  { $script:BtnPush.Enabled  = $true }
                if ($null -ne $script:BtnStop)  { $script:BtnStop.Visible  = $false }
                Refresh-VersionDisplay; Update-RunButtons
            }
        }
    } catch {}
})

# ============================================================================
# Section 16: Push pipeline
# ============================================================================

function Start-PushRelease {
    if ($script:IsPushing -or $script:IsBuilding) { return }
    $releaseScript = Join-Path $script:ProjectRoot "release.ps1"
    if (-not (Test-Path $releaseScript)) {
        [System.Windows.Forms.MessageBox]::Show("release.ps1 not found in project root.", "Push Error", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Warning) | Out-Null
        return
    }
    $ver = Get-UiVersion
    $vs  = "" + $ver.Major + "." + $ver.Minor + "." + $ver.Patch
    $ok  = [System.Windows.Forms.MessageBox]::Show(
        "Push release v" + $vs + " to GitHub?`n`nThis will write version to CMakeLists.txt, auto-commit, tag and push via release.ps1.",
        "Push Release v" + $vs,
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Warning
    )
    if ($ok -ne [System.Windows.Forms.DialogResult]::Yes) { return }
    $script:IsPushing = $true
    if ($null -ne $script:BtnBuild) { $script:BtnBuild.Enabled = $false }
    if ($null -ne $script:BtnPush)  { $script:BtnPush.Enabled  = $false }
    Set-ProjectVersion $ver.Major $ver.Minor $ver.Patch
    Auto-Commit | Out-Null
    $script:HasBuildErrors = $false; $script:AllOutput.Clear()
    $script:OutputQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
    $script:BuildStepQueue.Clear()
    if ($null -ne $script:ProgressBack)     { $script:ProgressBack.Visible  = $true }
    if ($null -ne $script:BtnStop)          { $script:BtnStop.Visible       = $true }
    if ($null -ne $script:LblBuildActivity) { $script:LblBuildActivity.Text = "Pushing v" + $vs + "..." }
    $step = @{
        Name   = "Push Release v" + $vs
        Exe    = "powershell.exe"
        Args   = "-ExecutionPolicy Bypass -Command `"& { . '" + $releaseScript + "' -Version '" + $vs + "' -Prerelease } *>&1`""
        Target = "client"
    }
    $script:IsBuilding = $true
    Start-Build-Step $step
}

# ============================================================================
# Section 17: Game launch + status monitoring
# ============================================================================

function Toggle-Server {
    if ($null -ne $script:ServerProcess) {
        try { if (-not $script:ServerProcess.HasExited) { $script:ServerProcess.Kill(); $script:ServerProcess = $null; if ($null -ne $script:BtnRunServer) { $script:BtnRunServer.Text = "RUN SERVER" }; return } } catch {}
        $script:ServerProcess = $null
    }
    $exe = Get-ExePath "server"
    if (-not (Test-Path $exe)) {
        [System.Windows.Forms.MessageBox]::Show("Server exe not found. Build first.", "Run Server", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
        return
    }
    try {
        $script:ServerProcess = Start-Process -FilePath $exe -WorkingDirectory $script:ServerBuildDir -PassThru
        if ($null -ne $script:BtnRunServer) { $script:BtnRunServer.Text = "STOP SERVER" }
    } catch {}
}

function Toggle-Game {
    if ($null -ne $script:GameProcess) {
        try { if (-not $script:GameProcess.HasExited) { $script:GameProcess.Kill(); $script:GameProcess = $null; if ($null -ne $script:BtnRunGame) { $script:BtnRunGame.Text = "RUN GAME" }; return } } catch {}
        $script:GameProcess = $null
    }
    $exe = Get-ExePath "client"
    if (-not (Test-Path $exe)) {
        [System.Windows.Forms.MessageBox]::Show("Game exe not found. Build first.", "Run Game", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
        return
    }
    try {
        $script:GameProcess = Start-Process -FilePath $exe -WorkingDirectory $script:ClientBuildDir -PassThru
        if ($null -ne $script:BtnRunGame) { $script:BtnRunGame.Text = "STOP GAME" }
    } catch {}
}

function Update-RunButtons {
    if ($null -eq $script:BtnRunServer -or $null -eq $script:BtnRunGame) { return }
    $svrAlive = $false
    if ($null -ne $script:ServerProcess) { try { $svrAlive = -not $script:ServerProcess.HasExited } catch {} }
    $svrExists = Test-ExeExists "server"
    $script:BtnRunServer.Text    = $(if ($svrAlive) { "STOP SERVER" } else { "RUN SERVER" })
    $script:BtnRunServer.Enabled = ($svrAlive -or $svrExists)
    $svrColor = $(if ($svrAlive) { $script:ColorRed } else { $(if ($svrExists) { $script:ColorOrange } else { $script:ColorDisabled }) })
    $script:BtnRunServer.ForeColor = $svrColor
    $script:BtnRunServer.FlatAppearance.BorderColor = $svrColor

    $gmAlive = $false
    if ($null -ne $script:GameProcess) { try { $gmAlive = -not $script:GameProcess.HasExited } catch {} }
    $gmExists = Test-ExeExists "client"
    $script:BtnRunGame.Text    = $(if ($gmAlive) { "STOP GAME" } else { "RUN GAME" })
    $script:BtnRunGame.Enabled = ($gmAlive -or $gmExists)
    $gmColor = $(if ($gmAlive) { $script:ColorRed } else { $(if ($gmExists) { $script:ColorGreen } else { $script:ColorDisabled }) })
    $script:BtnRunGame.ForeColor = $gmColor
    $script:BtnRunGame.FlatAppearance.BorderColor = $gmColor
}

# ============================================================================
# Section 18: Settings dialog
# ============================================================================

function Show-SettingsDialog {
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = "Settings"; $dlg.Size = New-Object System.Drawing.Size(400, 250)
    $dlg.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterParent
    $dlg.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedDialog
    $dlg.MaximizeBox = $false; $dlg.MinimizeBox = $false
    $dlg.BackColor = $script:ColorBgAlt; $dlg.ForeColor = $script:ColorText

    $l1 = New-Object System.Windows.Forms.Label; $l1.Text = "GitHub Repository (owner/repo):"
    $l1.Font = New-UIFont 9; $l1.ForeColor = $script:ColorTextDim
    $l1.Location = New-Object System.Drawing.Point(12, 16); $l1.AutoSize = $true
    $dlg.Controls.Add($l1)
    $tRepo = New-Object System.Windows.Forms.TextBox; $tRepo.Text = $script:Settings.GitHubRepo
    $tRepo.Location = New-Object System.Drawing.Point(12, 36); $tRepo.Size = New-Object System.Drawing.Size(360, 24)
    $tRepo.BackColor = $script:ColorBgInput; $tRepo.ForeColor = $script:ColorText
    $tRepo.Font = New-Object System.Drawing.Font("Consolas", 9)
    $tRepo.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
    $dlg.Controls.Add($tRepo)

    $l2 = New-Object System.Windows.Forms.Label; $l2.Text = "Font Size (8-16):"
    $l2.Font = New-UIFont 9; $l2.ForeColor = $script:ColorTextDim
    $l2.Location = New-Object System.Drawing.Point(12, 74); $l2.AutoSize = $true
    $dlg.Controls.Add($l2)
    $tFont = New-Object System.Windows.Forms.TextBox; $tFont.Text = "" + $script:Settings.FontSize
    $tFont.Location = New-Object System.Drawing.Point(12, 94); $tFont.Size = New-Object System.Drawing.Size(60, 24)
    $tFont.BackColor = $script:ColorBgInput; $tFont.ForeColor = $script:ColorText
    $tFont.Font = New-Object System.Drawing.Font("Consolas", 9)
    $tFont.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
    $dlg.Controls.Add($tFont)

    $chkSnd = New-Object System.Windows.Forms.CheckBox; $chkSnd.Text = "Enable sounds"
    $chkSnd.Checked = $script:Settings.EnableSounds; $chkSnd.Font = New-UIFont 9
    $chkSnd.ForeColor = $script:ColorText
    $chkSnd.Location = New-Object System.Drawing.Point(12, 130); $chkSnd.AutoSize = $true
    $dlg.Controls.Add($chkSnd)

    $bSave = New-Object System.Windows.Forms.Button; $bSave.Text = "Save"
    $bSave.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $bSave.Location = New-Object System.Drawing.Point(196, 176); $bSave.Size = New-Object System.Drawing.Size(80, 30)
    $bSave.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
    $bSave.FlatAppearance.BorderColor = $script:ColorGreen
    $bSave.ForeColor = $script:ColorGreen; $bSave.BackColor = $script:ColorBgAlt
    $bSave.Font = New-UIFont 10 -Bold
    $dlg.Controls.Add($bSave); $dlg.AcceptButton = $bSave

    $bCan = New-Object System.Windows.Forms.Button; $bCan.Text = "Cancel"
    $bCan.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $bCan.Location = New-Object System.Drawing.Point(290, 176); $bCan.Size = New-Object System.Drawing.Size(80, 30)
    $bCan.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
    $bCan.FlatAppearance.BorderColor = $script:ColorTextDim
    $bCan.ForeColor = $script:ColorTextDim; $bCan.BackColor = $script:ColorBgAlt
    $bCan.Font = New-UIFont 10 -Bold
    $dlg.Controls.Add($bCan); $dlg.CancelButton = $bCan

    if ($dlg.ShowDialog($script:Form) -eq [System.Windows.Forms.DialogResult]::OK) {
        $script:Settings.GitHubRepo = $tRepo.Text.Trim()
        $fs = 10; try { $fs = [int]$tFont.Text } catch {}
        if ($fs -lt 8) { $fs = 8 } ; if ($fs -gt 16) { $fs = 16 }
        $script:Settings.FontSize    = $fs
        $script:Settings.EnableSounds = $chkSnd.Checked
        Save-Settings
    }
    $dlg.Dispose()
}

# ============================================================================
# Section 19: Button event handlers
# ============================================================================

$script:BtnBuild.Add_Click({ Start-Build })
$script:BtnPush.Add_Click({  Start-PushRelease })
$script:BtnStop.Add_Click({  Stop-Build })

$script:BtnCopyErrors.Add_Click({
    try {
        $all = [System.Collections.ArrayList]::new()
        foreach ($e in $script:ClientErrors) { [void]$all.Add("[client] " + $e) }
        foreach ($e in $script:ServerErrors) { [void]$all.Add("[server] " + $e) }
        if ($all.Count -gt 0) { [System.Windows.Forms.Clipboard]::SetText(($all -join "`r`n")) }
    } catch {}
})

$script:BtnCopyLog.Add_Click({
    try {
        if ($script:AllOutput.Count -gt 0) { [System.Windows.Forms.Clipboard]::SetText(($script:AllOutput -join "`r`n")) }
    } catch {}
})

$script:BtnVerDown.Add_Click({
    try { $v = 0; try { $v = [int]$script:TxtVerPatch.Text } catch {}; if ($v -gt 0) { $script:TxtVerPatch.Text = "" + ($v - 1) } } catch {}
})

$script:BtnVerUp.Add_Click({
    try { $v = 0; try { $v = [int]$script:TxtVerPatch.Text } catch {}; $script:TxtVerPatch.Text = "" + ($v + 1) } catch {}
})

$script:BtnRunServer.Add_Click({ Toggle-Server })
$script:BtnRunGame.Add_Click({   Toggle-Game })
$script:BtnQcRefresh.Add_Click({ Refresh-QcGrid })
$script:BtnQcReset.Add_Click({   Reset-QcStatuses })

# ============================================================================
# Section 20: Resize handler
# ============================================================================

function Invoke-FormResize {
    if ($null -eq $script:TabBuild) { return }
    try {
        $tw   = $script:TabBuild.ClientSize.Width
        $th   = $script:TabBuild.ClientSize.Height
        $pad  = 8
        $heroH = [math]::Max(120, [math]::Floor($th * 0.40))
        $heroW = $tw - ($pad * 2)
        if ($null -ne $script:HeroPanel) {
            $script:HeroPanel.Location = New-Object System.Drawing.Point($pad, $pad)
            $script:HeroPanel.Size     = New-Object System.Drawing.Size($heroW, $heroH)
        }
        $btnW = [math]::Floor($heroW * 0.48)
        $gap  = $heroW - ($btnW * 2)
        if ($null -ne $script:BtnBuild) { $script:BtnBuild.Location = New-Object System.Drawing.Point(0, 0); $script:BtnBuild.Size = New-Object System.Drawing.Size($btnW, $heroH) }
        if ($null -ne $script:BtnPush)  { $script:BtnPush.Location  = New-Object System.Drawing.Point(($btnW + $gap), 0); $script:BtnPush.Size = New-Object System.Drawing.Size($btnW, $heroH) }
        $statusY = $heroH + ($pad * 2)
        $statusH = $th - $statusY - $pad
        $statusW = [math]::Floor($heroW * 0.60)
        $versionW = $heroW - $statusW - ($pad * 2)
        if ($null -ne $script:StatusPanel)  { $script:StatusPanel.Location  = New-Object System.Drawing.Point($pad, $statusY); $script:StatusPanel.Size = New-Object System.Drawing.Size($statusW, $statusH) }
        if ($null -ne $script:ProgressBack) { $script:ProgressBack.Size = New-Object System.Drawing.Size(($statusW - 4), 18) }
        if ($null -ne $script:VersionPanel) { $script:VersionPanel.Location = New-Object System.Drawing.Point(($pad + $statusW + $pad), $statusY); $script:VersionPanel.Size = New-Object System.Drawing.Size($versionW, $statusH) }
        if ($null -ne $script:QcHeaderPanel) {
            $hw = $script:TabPlaytest.ClientSize.Width
            if ($null -ne $script:BtnQcReset)   { $script:BtnQcReset.Location   = New-Object System.Drawing.Point(($hw - 68),  8) }
            if ($null -ne $script:BtnQcRefresh) { $script:BtnQcRefresh.Location = New-Object System.Drawing.Point(($hw - 140), 8) }
        }
    } catch {}
}

$script:Form.Add_Resize({ Invoke-FormResize })
$script:TabBuild.Add_Resize({ Invoke-FormResize })
$script:TabPlaytest.Add_Resize({ Invoke-FormResize })

# ============================================================================
# Section 21: Main timer (2s)
# ============================================================================

$mainTimer = New-Object System.Windows.Forms.Timer
$mainTimer.Interval = 2000
$mainTimer.Add_Tick({
    try {
        Update-RunButtons
        if (-not $script:IsBuilding -and -not $script:GitBusy) { Update-GitChangeCount }
    } catch {}
})

# ============================================================================
# Section 22: Initialization (Add_Shown)
# ============================================================================

$script:Form.Add_Shown({
    try {
        Invoke-FormResize
        Refresh-VersionDisplay
        Update-ReleaseCacheUI (Load-ReleaseCache)
        Update-RunButtons
        $mainTimer.Start()

        $t1 = New-Object System.Windows.Forms.Timer; $t1.Interval = 50
        $t1.Add_Tick({ try { $this.Stop(); $this.Dispose(); Load-QcFile; Populate-QcGrid } catch {} })
        $t1.Start()

        $t2 = New-Object System.Windows.Forms.Timer; $t2.Interval = 100
        $t2.Add_Tick({ try { $this.Stop(); $this.Dispose(); $script:LastGitCheck = [DateTime]::MinValue; Update-GitChangeCount } catch {} })
        $t2.Start()

        # Background: gh auth check
        $rs = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
        $rs.Open()
        $ps = [System.Management.Automation.PowerShell]::Create()
        $ps.Runspace = $rs
        [void]$ps.AddScript({ try { $o = gh auth status 2>&1; ($o | ForEach-Object { $_.ToString() }) -join " " } catch { "" } })
        $h = $ps.BeginInvoke()
        $ap = New-Object System.Windows.Forms.Timer; $ap.Interval = 400
        $ap.Add_Tick({
            try {
                if (-not $h.IsCompleted) { return }
                $this.Stop(); $this.Dispose()
                $res = $ps.EndInvoke($h)
                $ok  = ($res -join " ") -match 'Logged in'
                $script:GhAuthOk = $ok
                if ($null -ne $script:LblAuthStatus) {
                    $script:LblAuthStatus.Text = $(if ($ok) { "auth: ok" } else { "auth: --" })
                    $script:LblAuthStatus.ForeColor = $(if ($ok) { $script:ColorGreen } else { $script:ColorRed })
                }
                try { $ps.Dispose() } catch {}; try { $rs.Close(); $rs.Dispose() } catch {}
            } catch {}
        })
        $ap.Start()

        # Background: fetch latest release
        $repo = $script:Settings.GitHubRepo
        if ($repo -ne "") {
            $rs2 = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
            $rs2.Open()
            $ps2 = [System.Management.Automation.PowerShell]::Create()
            $ps2.Runspace = $rs2
            [void]$ps2.AddScript({ param($rp); try { $j = gh api ("repos/" + $rp + "/releases/latest") 2>$null; if ($LASTEXITCODE -eq 0 -and $j) { return ($j | ConvertFrom-Json) } } catch {}; return $null }).AddArgument($repo)
            $h2 = $ps2.BeginInvoke()
            $rp = New-Object System.Windows.Forms.Timer; $rp.Interval = 600
            $rp.Add_Tick({
                try {
                    if (-not $h2.IsCompleted) { return }
                    $this.Stop(); $this.Dispose()
                    $data = $ps2.EndInvoke($h2)
                    if ($null -ne $data -and $data.Count -gt 0) {
                        $rel = $data[0]; Save-ReleaseCache $rel
                        $script:Form.Invoke([Action]{ Update-ReleaseCacheUI $rel })
                    }
                    try { $ps2.Dispose() } catch {}; try { $rs2.Close(); $rs2.Dispose() } catch {}
                } catch {}
            })
            $rp.Start()
        }
    } catch {}
})

# ============================================================================
# Section 23: Cleanup (Add_FormClosing)
# ============================================================================

$script:Form.Add_FormClosing({
    try {
        $mainTimer.Stop()
        $script:BuildTimer.Stop()
        if ($null -ne $script:NotesSaveTimer) { try { $script:NotesSaveTimer.Stop(); $script:NotesSaveTimer.Dispose() } catch {} }
        if ($null -ne $script:BuildProcess)   { try { $script:BuildProcess.Kill() } catch {} }
        if ($script:QcData.Count -gt 0)       { try { Save-QcFile } catch {} }
        if ($null -ne $script:GameProcess)    { try { if (-not $script:GameProcess.HasExited) { $script:GameProcess.Kill() } } catch {} }
        if ($null -ne $script:ServerProcess)  { try { if (-not $script:ServerProcess.HasExited) { $script:ServerProcess.Kill() } } catch {} }
        try { $mainTimer.Dispose() } catch {}
        try { $script:BuildTimer.Dispose() } catch {}
        if ($null -ne $script:FontCollection) { try { $script:FontCollection.Dispose() } catch {} }
    } catch {}
})

# ============================================================================
# Section 24: Application::Run entry point
# ============================================================================

try {
    [System.Windows.Forms.Application]::Run($script:Form)
} catch {
    [System.Windows.Forms.MessageBox]::Show(
        "Fatal error: " + $_.Exception.Message,
        "Dev Window Error",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
} finally {
    try { $script:Form.Dispose() } catch {}
}
