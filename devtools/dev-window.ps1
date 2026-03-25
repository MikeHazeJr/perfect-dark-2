# ============================================================================
# Error logging — must be FIRST, before any Add-Type or form code
# ============================================================================

$script:LogPath = Join-Path $PSScriptRoot "error.log"

trap {
    $ts  = [datetime]::Now.ToString("yyyy-MM-dd HH:mm:ss")
    $msg = "[$ts] UNCAUGHT ERROR:`r`n$_`r`nStack: $($_.ScriptStackTrace)`r`n---`r`n"
    try { $msg | Out-File -FilePath $script:LogPath -Append -Encoding UTF8 } catch {}
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction SilentlyContinue
        [System.Windows.Forms.MessageBox]::Show(
            "Fatal Error:`n$_`n`nSee devtools\error.log for details",
            "PD Dev Window Error",
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    } catch {}
    Write-Host "FATAL: $_" -ForegroundColor Red
    Write-Host "Details written to: $script:LogPath"
    Read-Host "Press Enter to exit"
    break
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# ============================================================================
# Console hide
# ============================================================================

if (-not ([System.Management.Automation.PSTypeName]'ConsoleHelper').Type) {
    try {
        Add-Type -Language CSharp @"
using System;
using System.Runtime.InteropServices;
public class ConsoleHelper {
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
    [DllImport("user32.dll")]   public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    public const int SW_HIDE = 0;
    public static void HideConsole() {
        IntPtr hwnd = GetConsoleWindow();
        if (hwnd != IntPtr.Zero) ShowWindow(hwnd, SW_HIDE);
    }
}
"@
    } catch {
        $ts = [datetime]::Now.ToString("yyyy-MM-dd HH:mm:ss")
        "[$ts] ERROR in [Add-Type ConsoleHelper]: $_`r`n---`r`n" |
            Out-File -FilePath $script:LogPath -Append -Encoding UTF8 -ErrorAction SilentlyContinue
    }
}
try { [ConsoleHelper]::HideConsole() } catch {}

# ============================================================================
# Inline C# helpers
# ============================================================================

if (-not ([System.Management.Automation.PSTypeName]'DarkMenuColorTable').Type) {
try {
Add-Type -Language CSharp -ReferencedAssemblies System.Windows.Forms,System.Drawing @"
using System;
using System.IO;
using System.Drawing;
using System.Threading;
using System.Collections.Concurrent;
using System.Windows.Forms;

public class AsyncLineReader {
    public static void StartReading(StreamReader reader, ConcurrentQueue<string> queue, string prefix) {
        var thread = new Thread(() => {
            try {
                string line;
                while ((line = reader.ReadLine()) != null)
                    queue.Enqueue(prefix + line);
            } catch {}
        });
        thread.IsBackground = true;
        thread.Start();
    }
}

public class DarkMenuColorTable : ProfessionalColorTable {
    private Color bg        = Color.FromArgb(40, 40, 40);
    private Color border    = Color.FromArgb(70, 70, 70);
    private Color highlight = Color.FromArgb(60, 60, 60);
    public override Color ToolStripDropDownBackground        { get { return bg; } }
    public override Color MenuBorder                         { get { return border; } }
    public override Color MenuItemBorder                     { get { return highlight; } }
    public override Color MenuItemSelected                   { get { return highlight; } }
    public override Color MenuItemSelectedGradientBegin      { get { return highlight; } }
    public override Color MenuItemSelectedGradientEnd        { get { return highlight; } }
    public override Color MenuItemPressedGradientBegin       { get { return bg; } }
    public override Color MenuItemPressedGradientEnd         { get { return bg; } }
    public override Color MenuStripGradientBegin             { get { return bg; } }
    public override Color MenuStripGradientEnd               { get { return bg; } }
    public override Color ImageMarginGradientBegin           { get { return bg; } }
    public override Color ImageMarginGradientMiddle          { get { return bg; } }
    public override Color ImageMarginGradientEnd             { get { return bg; } }
    public override Color SeparatorDark                      { get { return Color.FromArgb(70,70,70); } }
    public override Color SeparatorLight                     { get { return bg; } }
    public override Color CheckBackground                    { get { return highlight; } }
    public override Color CheckSelectedBackground            { get { return highlight; } }
    public override Color CheckPressedBackground             { get { return highlight; } }
}
"@
} catch {
    $ts = [datetime]::Now.ToString("yyyy-MM-dd HH:mm:ss")
    "[$ts] ERROR in [Add-Type AsyncLineReader/DarkMenuColorTable]: $_`r`n---`r`n" |
        Out-File -FilePath $script:LogPath -Append -Encoding UTF8 -ErrorAction SilentlyContinue
    [System.Windows.Forms.MessageBox]::Show(
        "C# compilation failed:`n$_`n`nSee devtools\error.log",
        "PD Dev Window Error",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
}
} # end if -not DarkMenuColorTable

# ============================================================================
# Configuration
# ============================================================================

$script:ProjectDir      = Split-Path -Parent $PSScriptRoot
$script:ClientBuildDir  = Join-Path $script:ProjectDir "build\client"
$script:ServerBuildDir  = Join-Path $script:ProjectDir "build\server"
$script:BuildDir        = $script:ClientBuildDir
$script:AddinDir        = Join-Path $script:ProjectDir "..\post-batch-addin"
$script:CMake           = "cmake"
$script:Make            = "C:\msys64\usr\bin\make.exe"
$script:CC              = "C:/msys64/mingw64/bin/cc.exe"
$script:ChangesFile     = Join-Path $script:ProjectDir "CHANGES.md"
$script:SettingsFile    = Join-Path $script:ProjectDir ".dev-window-settings.json"
$script:QcFile          = Join-Path $script:ProjectDir "context\qc-tests.md"
$script:SoundsDir       = Join-Path $script:ProjectDir "dist\build-sounds"

$env:MSYSTEM       = "MINGW64"
$env:MINGW_PREFIX  = "/mingw64"
$env:PATH          = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"

# Build state
$script:ErrorLines        = [System.Collections.ArrayList]::new()
$script:AllOutput         = [System.Collections.ArrayList]::new()
$script:ClientErrorLines  = [System.Collections.ArrayList]::new()
$script:ServerErrorLines  = [System.Collections.ArrayList]::new()
$script:ClientBuildFailed = $false
$script:ServerBuildFailed = $false
$script:ClientBuildTime   = 0
$script:ServerBuildTime   = 0
$script:TargetStartTime   = [DateTime]::Now
$script:IsRunning         = $false
$script:BuildSucceeded    = $false
$script:BuildTarget       = ""
$script:GameProcess       = $null
$script:GameRunning       = $false
$script:HasErrors         = $false
$script:OutputQueue       = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
$script:Process           = $null
$script:StepQueue         = [System.Collections.Queue]::new()
$script:CurrentStep       = ""
$script:BuildPercent      = 0
$script:SpinnerChars      = @('|','/','-','\')
$script:SpinnerIndex      = 0
$script:LastOutputTime    = [DateTime]::Now
$script:StepStartTime     = [DateTime]::Now
$script:PendingServerBuild  = $false
$script:PendingPostRelease  = $false

# Git state
$script:GitChangeCount  = 0
$script:LastGitCheck    = [DateTime]::MinValue
$script:GitBusy         = $false

# ============================================================================
# Settings
# ============================================================================

$script:Settings = @{
    GithubRepo    = ""
    SoundsEnabled = $true
}

function Load-Settings {
    if (Test-Path $script:SettingsFile) {
        try {
            $json = Get-Content $script:SettingsFile -Raw | ConvertFrom-Json
            if ($json.GithubRepo)               { $script:Settings.GithubRepo    = $json.GithubRepo }
            if ($null -ne $json.SoundsEnabled)  { $script:Settings.SoundsEnabled = [bool]$json.SoundsEnabled }
        } catch {}
    }
    if ($script:Settings.GithubRepo -eq "") {
        try {
            $remote = (git -C $script:ProjectDir remote get-url origin 2>$null).Trim()
            if ($remote -match 'github\.com[:/](.+?)(?:\.git)?$') {
                $script:Settings.GithubRepo = $Matches[1]
            }
        } catch {}
    }
}

function Save-Settings {
    $script:Settings | ConvertTo-Json | Set-Content $script:SettingsFile
}

Load-Settings

# ============================================================================
# Font
# ============================================================================

$script:FontCollection  = New-Object System.Drawing.Text.PrivateFontCollection
$fontPath = Join-Path $script:ProjectDir "fonts\Menus\Handel Gothic Regular\Handel Gothic Regular.otf"
$script:UseHandelGothic = $false
if (Test-Path $fontPath) {
    try {
        $script:FontCollection.AddFontFile($fontPath)
        $script:HandelFamily    = $script:FontCollection.Families[0]
        $script:UseHandelGothic = $true
    } catch {}
}

function New-UIFont($size, [switch]$Bold) {
    $style = $(if ($Bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular })
    if ($script:UseHandelGothic) {
        return New-Object System.Drawing.Font($script:HandelFamily, $size, $style, [System.Drawing.GraphicsUnit]::Point)
    }
    return New-Object System.Drawing.Font("Segoe UI", $size, $style)
}

# ============================================================================
# Colors
# ============================================================================

$script:ColorBg        = [System.Drawing.Color]::FromArgb(30, 30, 30)
$script:ColorPanelBg   = [System.Drawing.Color]::FromArgb(40, 40, 40)
$script:ColorFieldBg   = [System.Drawing.Color]::FromArgb(45, 45, 45)
$script:ColorConsoleBg = [System.Drawing.Color]::FromArgb(20, 20, 20)
$script:ColorGold      = [System.Drawing.Color]::FromArgb(220, 180, 60)
$script:ColorGreen     = [System.Drawing.Color]::FromArgb(50, 220, 120)
$script:ColorOrange    = [System.Drawing.Color]::FromArgb(255, 180, 50)
$script:ColorPurple    = [System.Drawing.Color]::FromArgb(180, 140, 220)
$script:ColorRed       = [System.Drawing.Color]::FromArgb(255, 100, 100)
$script:ColorBlue      = [System.Drawing.Color]::FromArgb(0, 96, 191)
$script:ColorDim       = [System.Drawing.Color]::FromArgb(160, 160, 160)
$script:ColorDimmer    = [System.Drawing.Color]::FromArgb(80, 80, 80)
$script:ColorText      = [System.Drawing.Color]::FromArgb(200, 200, 200)
$script:ColorWhite     = [System.Drawing.Color]::White
$script:ColorDisabled  = [System.Drawing.Color]::FromArgb(80, 80, 80)

# ============================================================================
# Sound system (one success sound, one failure sound)
# ============================================================================

$script:SoundPlayers = @{}

function Play-Sound($category) {
    if (-not $script:Settings.SoundsEnabled) { return }
    try {
        $catDir = Join-Path $script:SoundsDir $category
        $files  = @()
        if (Test-Path $catDir) {
            $files = @(Get-ChildItem -Path $catDir -Filter "*.wav" -ErrorAction SilentlyContinue |
                       Select-Object -ExpandProperty FullName)
        }
        if ($files.Count -gt 0) {
            $file = $files | Get-Random
            if (-not $script:SoundPlayers.ContainsKey($file)) {
                $p = New-Object System.Media.SoundPlayer($file)
                $p.Load()
                $script:SoundPlayers[$file] = $p
            }
            $script:SoundPlayers[$file].Play()
        }
    } catch {}
}

function Sound-Success { Play-Sound "enemy_argh" }
function Sound-Fail    {
    $catDir = Join-Path $script:SoundsDir "enemy_argh"
    if (Test-Path $catDir) { Play-Sound "enemy_argh" }
    else { try { [System.Media.SystemSounds]::Hand.Play() } catch {} }
}

# ============================================================================
# Main Form
# ============================================================================

$form = New-Object System.Windows.Forms.Form
$form.Text            = "Perfect Dark - Dev Window"
$form.Size            = New-Object System.Drawing.Size(1020, 720)
$form.MinimumSize     = New-Object System.Drawing.Size(820, 580)
$form.StartPosition   = "CenterScreen"
$form.BackColor       = $script:ColorBg
$form.ForeColor       = $script:ColorWhite
$form.Font            = New-UIFont 10
$form.FormBorderStyle = "Sizable"
$form.ShowInTaskbar   = $true

$iconPath = Join-Path $script:ProjectDir "dist\windows\icon.ico"
if (Test-Path $iconPath) {
    $form.Icon = New-Object System.Drawing.Icon($iconPath)
}

# ============================================================================
# Menu Bar
# ============================================================================

$menuStrip = New-Object System.Windows.Forms.MenuStrip
$menuStrip.BackColor = $script:ColorPanelBg
$menuStrip.ForeColor = $script:ColorWhite
$menuStrip.Renderer  = New-Object System.Windows.Forms.ToolStripProfessionalRenderer(
    (New-Object DarkMenuColorTable)
)

$menuFile = New-Object System.Windows.Forms.ToolStripMenuItem("File")
$menuFile.ForeColor = $script:ColorWhite

$menuEditChanges = New-Object System.Windows.Forms.ToolStripMenuItem("Edit CHANGES.md")
$menuEditChanges.ForeColor = $script:ColorText
$menuEditChanges.Add_Click({
    if (!(Test-Path $script:ChangesFile)) {
        $template = @("# Changelog - Perfect Dark 2","","## Improvements","","## Additions","","## Updates","","## Known Issues","","## Missing Content","")
        Set-Content -Path $script:ChangesFile -Value ($template -join "`n")
    }
    Start-Process "notepad.exe" -ArgumentList $script:ChangesFile
})
[void]$menuFile.DropDownItems.Add($menuEditChanges)

$menuEditQc = New-Object System.Windows.Forms.ToolStripMenuItem("Edit qc-tests.md")
$menuEditQc.ForeColor = $script:ColorText
$menuEditQc.Add_Click({
    if (Test-Path $script:QcFile) { Start-Process "notepad.exe" -ArgumentList $script:QcFile }
})
[void]$menuFile.DropDownItems.Add($menuEditQc)

[void]$menuFile.DropDownItems.Add((New-Object System.Windows.Forms.ToolStripSeparator))

$menuOpenGithub = New-Object System.Windows.Forms.ToolStripMenuItem("Open GitHub")
$menuOpenGithub.ForeColor = $script:ColorText
$menuOpenGithub.Add_Click({
    $repo = $script:Settings.GithubRepo
    if ($repo -ne "") { Start-Process ("https://github.com/" + $repo) }
    else { [System.Windows.Forms.MessageBox]::Show("No repository configured. Go to Edit > Settings.", "GitHub", "OK", [System.Windows.Forms.MessageBoxIcon]::Information) }
})
[void]$menuFile.DropDownItems.Add($menuOpenGithub)

$menuOpenFolder = New-Object System.Windows.Forms.ToolStripMenuItem("Open Project Folder")
$menuOpenFolder.ForeColor = $script:ColorText
$menuOpenFolder.Add_Click({ Start-Process "explorer.exe" -ArgumentList $script:ProjectDir })
[void]$menuFile.DropDownItems.Add($menuOpenFolder)

[void]$menuFile.DropDownItems.Add((New-Object System.Windows.Forms.ToolStripSeparator))

$menuExit = New-Object System.Windows.Forms.ToolStripMenuItem("Exit")
$menuExit.ForeColor = $script:ColorText
$menuExit.Add_Click({ $form.Close() })
[void]$menuFile.DropDownItems.Add($menuExit)
[void]$menuStrip.Items.Add($menuFile)

$menuEdit = New-Object System.Windows.Forms.ToolStripMenuItem("Edit")
$menuEdit.ForeColor = $script:ColorWhite
$menuSettings = New-Object System.Windows.Forms.ToolStripMenuItem("Settings...")
$menuSettings.ForeColor = $script:ColorText
$menuSettings.Add_Click({ Show-SettingsDialog })
[void]$menuEdit.DropDownItems.Add($menuSettings)
[void]$menuStrip.Items.Add($menuEdit)

$form.MainMenuStrip = $menuStrip
# MenuStrip added AFTER bottomBar and tabControl so Dock order is correct

# ============================================================================
# Bottom Bar (always visible)
# ============================================================================

$bottomBar = New-Object System.Windows.Forms.Panel
$bottomBar.Height    = 58
$bottomBar.Dock      = "Bottom"
$bottomBar.BackColor = $script:ColorPanelBg
$form.Controls.Add($bottomBar)

function New-BottomBtn($text, $x, $w, $color) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text     = $text
    $btn.Location = New-Object System.Drawing.Point($x, 4)
    $btn.Size     = New-Object System.Drawing.Size($w, 50)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $color
    $btn.FlatAppearance.BorderSize  = 2
    $btn.ForeColor = $color
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor    = "Hand"
    $btn.Font      = New-UIFont 14 -Bold
    $bottomBar.Controls.Add($btn)
    return $btn
}

$btnRunServer = New-BottomBtn "Run Server"   0  400 $script:ColorOrange
$btnRunGame   = New-BottomBtn "Run Game"   404  400 $script:ColorGreen

$lblGameStatus = New-Object System.Windows.Forms.Label
$lblGameStatus.Text      = ""
$lblGameStatus.Font      = New-UIFont 11
$lblGameStatus.ForeColor = $script:ColorDim
$lblGameStatus.AutoSize  = $true
$lblGameStatus.Visible   = $false

# ============================================================================
# TabControl
# ============================================================================

$tabControl = New-Object System.Windows.Forms.TabControl
$tabControl.Dock      = "Fill"
$tabControl.BackColor = $script:ColorBg
$tabControl.Padding   = New-Object System.Drawing.Point(0, 4)
$tabControl.Font      = New-UIFont 10 -Bold

# Style tab headers
$tabControl.DrawMode = "OwnerDrawFixed"
$tabControl.ItemSize = New-Object System.Drawing.Size(140, 30)
$tabControl.Add_DrawItem({
    param($s, $e)
    $tab   = $tabControl.TabPages[$e.Index]
    $rect  = $e.Bounds
    $brush = $(if ($e.Index -eq $tabControl.SelectedIndex) {
        New-Object System.Drawing.SolidBrush($script:ColorPanelBg)
    } else {
        New-Object System.Drawing.SolidBrush($script:ColorBg)
    })
    $e.Graphics.FillRectangle($brush, $rect)
    $brush.Dispose()
    $tc = $(if ($e.Index -eq $tabControl.SelectedIndex) { $script:ColorGold } else { $script:ColorDim })
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment     = [System.Drawing.StringAlignment]::Center
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
    $textBrush = New-Object System.Drawing.SolidBrush($tc)
    $e.Graphics.DrawString($tab.Text, (New-UIFont 10 -Bold), $textBrush, [System.Drawing.RectangleF]$rect, $sf)
    $textBrush.Dispose()
})
$tabControl.Add_Paint({
    param($s, $e)
    $e.Graphics.Clear($script:ColorBg)
})

$tabBuild    = New-Object System.Windows.Forms.TabPage
$tabBuild.Text      = "Build"
$tabBuild.BackColor = $script:ColorBg

$tabPlaytest = New-Object System.Windows.Forms.TabPage
$tabPlaytest.Text      = "Playtest"
$tabPlaytest.BackColor = $script:ColorBg

[void]$tabControl.TabPages.Add($tabBuild)
[void]$tabControl.TabPages.Add($tabPlaytest)
# Force dark background on tab pages (WinForms ignores BackColor unless UseVisualStyleBackColor is off)
$tabBuild.UseVisualStyleBackColor    = $false
$tabPlaytest.UseVisualStyleBackColor = $false
# Add order matters for WinForms docking: last added = first docked.
# TabControl (Fill) must be added BEFORE MenuStrip (Top) so the menu gets top priority.
$form.Controls.Add($tabControl)
$form.Controls.Add($menuStrip)

# ============================================================================
# BUILD TAB
# ============================================================================

$buildPanel = New-Object System.Windows.Forms.Panel
$buildPanel.Dock      = "Fill"
$buildPanel.BackColor = $script:ColorBg
$tabBuild.Controls.Add($buildPanel)

# --- Hero action buttons (top, fill ~40% of tab height, side by side) ---
$btnBuild = New-Object System.Windows.Forms.Button
$btnBuild.Text      = "BUILD"
$btnBuild.Location  = New-Object System.Drawing.Point(8, 8)
$btnBuild.Size      = New-Object System.Drawing.Size(480, 210)
$btnBuild.FlatStyle = "Flat"
$btnBuild.FlatAppearance.BorderColor = $script:ColorGreen
$btnBuild.FlatAppearance.BorderSize  = 2
$btnBuild.ForeColor  = $script:ColorGreen
$btnBuild.BackColor  = $script:ColorFieldBg
$btnBuild.Cursor     = "Hand"
$btnBuild.Font       = New-UIFont 18 -Bold
$btnBuild.Anchor     = "Top,Left"
$buildPanel.Controls.Add($btnBuild)

$btnPush = New-Object System.Windows.Forms.Button
$btnPush.Text      = "PUSH"
$btnPush.Location  = New-Object System.Drawing.Point(496, 8)
$btnPush.Size      = New-Object System.Drawing.Size(480, 210)
$btnPush.FlatStyle = "Flat"
$btnPush.FlatAppearance.BorderColor = $script:ColorGold
$btnPush.FlatAppearance.BorderSize  = 2
$btnPush.ForeColor  = $script:ColorGold
$btnPush.BackColor  = [System.Drawing.Color]::FromArgb(50, 45, 20)
$btnPush.Cursor     = "Hand"
$btnPush.Font       = New-UIFont 18 -Bold
$btnPush.Anchor     = "Top,Right"
$buildPanel.Controls.Add($btnPush)

$chkStable = New-Object System.Windows.Forms.CheckBox
$chkStable.Text      = "Stable"
$chkStable.Font      = New-UIFont 10
$chkStable.ForeColor = $script:ColorGold
$chkStable.BackColor = $script:ColorBg
$chkStable.Location  = New-Object System.Drawing.Point(496, 226)
$chkStable.AutoSize  = $true
$chkStable.Checked   = $false
$buildPanel.Controls.Add($chkStable)

# Stop button - hidden during idle, shown during builds
$btnStop = New-Object System.Windows.Forms.Button
$btnStop.Text      = "Stop"
$btnStop.Location  = New-Object System.Drawing.Point(8, 228)
$btnStop.Size      = New-Object System.Drawing.Size(80, 28)
$btnStop.FlatStyle = "Flat"
$btnStop.FlatAppearance.BorderColor = $script:ColorRed
$btnStop.FlatAppearance.BorderSize  = 1
$btnStop.ForeColor  = $script:ColorDisabled
$btnStop.BackColor  = $script:ColorFieldBg
$btnStop.Cursor     = "Hand"
$btnStop.Font       = New-UIFont 11 -Bold
$btnStop.Enabled    = $false
$btnStop.Visible    = $false
$btnStop.Anchor     = "Top,Left"
$buildPanel.Controls.Add($btnStop)

# --- Status labels (left side, below hero buttons) ---
$lblClientStatus = New-Object System.Windows.Forms.Label
$lblClientStatus.Text      = "client: --"
$lblClientStatus.Font      = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
$lblClientStatus.ForeColor = $script:ColorDim
$lblClientStatus.Location  = New-Object System.Drawing.Point(12, 232)
$lblClientStatus.AutoSize  = $true
$lblClientStatus.Anchor    = "Top,Left"
$buildPanel.Controls.Add($lblClientStatus)

$lblServerStatus = New-Object System.Windows.Forms.Label
$lblServerStatus.Text      = "server: --"
$lblServerStatus.Font      = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
$lblServerStatus.ForeColor = $script:ColorDim
$lblServerStatus.Location  = New-Object System.Drawing.Point(12, 256)
$lblServerStatus.AutoSize  = $true
$lblServerStatus.Anchor    = "Top,Left"
$buildPanel.Controls.Add($lblServerStatus)

# Build status label (step/timing)
$lblBuildStatus = New-Object System.Windows.Forms.Label
$lblBuildStatus.Text      = "Ready"
$lblBuildStatus.Font      = New-UIFont 11
$lblBuildStatus.ForeColor = $script:ColorDim
$lblBuildStatus.Location  = New-Object System.Drawing.Point(12, 280)
$lblBuildStatus.Size      = New-Object System.Drawing.Size(460, 20)
$lblBuildStatus.Anchor    = "Top,Left"
$buildPanel.Controls.Add($lblBuildStatus)

# Game status label (launch feedback)
$buildPanel.Controls.Add($lblGameStatus)
$lblGameStatus.Location = New-Object System.Drawing.Point(12, 304)
$lblGameStatus.Visible  = $true

# --- Progress bar ---
$progressOuter = New-Object System.Windows.Forms.Panel
$progressOuter.Location  = New-Object System.Drawing.Point(12, 390)
$progressOuter.Size      = New-Object System.Drawing.Size(750, 16)
$progressOuter.BackColor = $script:ColorPanelBg
$progressOuter.Anchor    = "Top,Left,Right"
$buildPanel.Controls.Add($progressOuter)

$progressFill = New-Object System.Windows.Forms.Panel
$progressFill.Location  = New-Object System.Drawing.Point(0, 0)
$progressFill.Size      = New-Object System.Drawing.Size(0, 16)
$progressFill.BackColor = $script:ColorBlue
$progressOuter.Controls.Add($progressFill)

$progressLabel = New-Object System.Windows.Forms.Label
$progressLabel.Text      = ""
$progressLabel.Font      = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
$progressLabel.ForeColor = $script:ColorWhite
$progressLabel.BackColor = [System.Drawing.Color]::Transparent
$progressLabel.Location  = New-Object System.Drawing.Point(4, 1)
$progressLabel.AutoSize  = $true
$progressOuter.Controls.Add($progressLabel)
$progressLabel.BringToFront()

# --- Version section (right side, below hero buttons) ---
$verPanel = New-Object System.Windows.Forms.Panel
$verPanel.Location  = New-Object System.Drawing.Point(580, 226)
$verPanel.Size      = New-Object System.Drawing.Size(400, 148)
$verPanel.BackColor = $script:ColorPanelBg
$verPanel.Anchor    = "Top,Right"
$buildPanel.Controls.Add($verPanel)

$lblVerTitle = New-Object System.Windows.Forms.Label
$lblVerTitle.Text      = "VERSION"
$lblVerTitle.Font      = New-UIFont 11 -Bold
$lblVerTitle.ForeColor = $script:ColorDim
$lblVerTitle.Location  = New-Object System.Drawing.Point(8, 6)
$lblVerTitle.AutoSize  = $true
$verPanel.Controls.Add($lblVerTitle)

function New-VerField($x, $y, $parent) {
    $txt = New-Object System.Windows.Forms.TextBox
    $txt.Location    = New-Object System.Drawing.Point($x, $y)
    $txt.Size        = New-Object System.Drawing.Size(50, 24)
    $txt.BackColor   = $script:ColorFieldBg
    $txt.ForeColor   = $script:ColorGold
    $txt.Font        = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.FontStyle]::Bold)
    $txt.TextAlign   = "Center"
    $txt.BorderStyle = "FixedSingle"
    $txt.MaxLength   = 4
    $parent.Controls.Add($txt)
    return $txt
}

function New-SmallBtn($text, $x, $y, $parent) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text     = $text
    $btn.Location = New-Object System.Drawing.Point($x, $y)
    $btn.Size     = New-Object System.Drawing.Size(22, 18)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $script:ColorDim
    $btn.FlatAppearance.BorderSize  = 1
    $btn.ForeColor = $script:ColorGold
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor    = "Hand"
    $btn.Font      = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
    $btn.Padding   = New-Object System.Windows.Forms.Padding(0)
    $parent.Controls.Add($btn)
    return $btn
}

$vx1 = 8; $vx2 = 72; $vx3 = 136; $vy = 26; $vby = 52

$txtVerMajor    = New-VerField $vx1 $vy $verPanel
$txtVerMinor    = New-VerField $vx2 $vy $verPanel
$txtVerRevision = New-VerField $vx3 $vy $verPanel

foreach ($dx in @(($vx1+52), ($vx2+52))) {
    $dot = New-Object System.Windows.Forms.Label
    $dot.Text      = "."; $dot.Font = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.FontStyle]::Bold)
    $dot.ForeColor = $script:ColorDim; $dot.Location = New-Object System.Drawing.Point($dx, ($vy+2)); $dot.AutoSize = $true
    $verPanel.Controls.Add($dot)
}

$btnDecMajor    = New-SmallBtn "-" $vx1 $vby $verPanel
$btnIncMajor    = New-SmallBtn "+" ($vx1+24) $vby $verPanel
$btnDecMinor    = New-SmallBtn "-" $vx2 $vby $verPanel
$btnIncMinor    = New-SmallBtn "+" ($vx2+24) $vby $verPanel
$btnDecRevision = New-SmallBtn "-" $vx3 $vby $verPanel
$btnIncRevision = New-SmallBtn "+" ($vx3+24) $vby $verPanel

$lblLatestDev = New-Object System.Windows.Forms.Label
$lblLatestDev.Text      = "dev: ---"
$lblLatestDev.Font      = New-UIFont 10
$lblLatestDev.ForeColor = $script:ColorOrange
$lblLatestDev.Location  = New-Object System.Drawing.Point(8, 74)
$lblLatestDev.AutoSize  = $true
$verPanel.Controls.Add($lblLatestDev)

$lblLatestStable = New-Object System.Windows.Forms.Label
$lblLatestStable.Text      = "stable: ---"
$lblLatestStable.Font      = New-UIFont 10
$lblLatestStable.ForeColor = $script:ColorGreen
$lblLatestStable.Location  = New-Object System.Drawing.Point(120, 74)
$lblLatestStable.AutoSize  = $true
$verPanel.Controls.Add($lblLatestStable)

$lblVerWarning = New-Object System.Windows.Forms.Label
$lblVerWarning.Text      = ""
$lblVerWarning.Font      = New-UIFont 10 -Bold
$lblVerWarning.ForeColor = $script:ColorRed
$lblVerWarning.Location  = New-Object System.Drawing.Point(8, 90)
$lblVerWarning.Size      = New-Object System.Drawing.Size(380, 18)
$verPanel.Controls.Add($lblVerWarning)

# Auth label
$lblAuth = New-Object System.Windows.Forms.Label
$lblAuth.Text      = "checking auth..."
$lblAuth.Font      = New-UIFont 11
$lblAuth.ForeColor = $script:ColorDim
$lblAuth.Location  = New-Object System.Drawing.Point(8, 114)
$lblAuth.AutoSize  = $true
$lblAuth.Cursor    = "Hand"
$verPanel.Controls.Add($lblAuth)

# Error buttons (hidden until build produces errors)
$btnCopyErrors = New-Object System.Windows.Forms.Button
$btnCopyErrors.Text      = "Copy Errors"
$btnCopyErrors.Location  = New-Object System.Drawing.Point(12, 420)
$btnCopyErrors.Size      = New-Object System.Drawing.Size(110, 26)
$btnCopyErrors.FlatStyle = "Flat"
$btnCopyErrors.FlatAppearance.BorderColor = $script:ColorRed
$btnCopyErrors.FlatAppearance.BorderSize  = 1
$btnCopyErrors.ForeColor = $script:ColorRed
$btnCopyErrors.BackColor = $script:ColorFieldBg
$btnCopyErrors.Cursor    = "Hand"
$btnCopyErrors.Font      = New-UIFont 11 -Bold
$btnCopyErrors.Visible   = $false
$btnCopyErrors.Anchor    = "Top,Left"
$buildPanel.Controls.Add($btnCopyErrors)

$btnCopyFullLog = New-Object System.Windows.Forms.Button
$btnCopyFullLog.Text      = "Copy Full Log"
$btnCopyFullLog.Location  = New-Object System.Drawing.Point(128, 420)
$btnCopyFullLog.Size      = New-Object System.Drawing.Size(120, 26)
$btnCopyFullLog.FlatStyle = "Flat"
$btnCopyFullLog.FlatAppearance.BorderColor = $script:ColorDim
$btnCopyFullLog.FlatAppearance.BorderSize  = 1
$btnCopyFullLog.ForeColor = $script:ColorDim
$btnCopyFullLog.BackColor = $script:ColorFieldBg
$btnCopyFullLog.Cursor    = "Hand"
$btnCopyFullLog.Font      = New-UIFont 11 -Bold
$btnCopyFullLog.Visible   = $false
$btnCopyFullLog.Anchor    = "Top,Left"
$buildPanel.Controls.Add($btnCopyFullLog)

$lblErrorCount = New-Object System.Windows.Forms.Label
$lblErrorCount.Text      = ""
$lblErrorCount.Font      = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
$lblErrorCount.ForeColor = $script:ColorRed
$lblErrorCount.Location  = New-Object System.Drawing.Point(256, 425)
$lblErrorCount.AutoSize  = $true
$lblErrorCount.Visible   = $false
$buildPanel.Controls.Add($lblErrorCount)

# ============================================================================
# PLAYTEST TAB
# ============================================================================

$playtestPanel = New-Object System.Windows.Forms.Panel
$playtestPanel.Dock      = "Fill"
$playtestPanel.BackColor = $script:ColorBg
$tabPlaytest.Controls.Add($playtestPanel)

# Header row
$qcHeaderPanel = New-Object System.Windows.Forms.Panel
$qcHeaderPanel.Location  = New-Object System.Drawing.Point(0, 0)
$qcHeaderPanel.Height    = 40
$qcHeaderPanel.Dock      = "Top"
$qcHeaderPanel.BackColor = $script:ColorPanelBg
$playtestPanel.Controls.Add($qcHeaderPanel)

$lblQcTitle = New-Object System.Windows.Forms.Label
$lblQcTitle.Text      = "QC TEST CHECKLIST"
$lblQcTitle.Font      = New-UIFont 11 -Bold
$lblQcTitle.ForeColor = $script:ColorGold
$lblQcTitle.Location  = New-Object System.Drawing.Point(10, 10)
$lblQcTitle.AutoSize  = $true
$qcHeaderPanel.Controls.Add($lblQcTitle)

$cmbQcFilter = New-Object System.Windows.Forms.ComboBox
$cmbQcFilter.Location      = New-Object System.Drawing.Point(200, 8)
$cmbQcFilter.Size          = New-Object System.Drawing.Size(100, 24)
$cmbQcFilter.DropDownStyle = "DropDownList"
$cmbQcFilter.BackColor     = $script:ColorFieldBg
$cmbQcFilter.ForeColor     = $script:ColorWhite
$cmbQcFilter.FlatStyle     = "Flat"
$cmbQcFilter.Font          = New-UIFont 11
[void]$cmbQcFilter.Items.AddRange(@("All","Pending","Pass","Fail","Skip"))
$cmbQcFilter.SelectedIndex = 0
$qcHeaderPanel.Controls.Add($cmbQcFilter)

$lblQcSummary = New-Object System.Windows.Forms.Label
$lblQcSummary.Text      = ""
$lblQcSummary.Font      = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
$lblQcSummary.ForeColor = $script:ColorDim
$lblQcSummary.Location  = New-Object System.Drawing.Point(310, 12)
$lblQcSummary.AutoSize  = $true
$qcHeaderPanel.Controls.Add($lblQcSummary)

function New-QcBtn($text, $color) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text      = $text
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $color
    $btn.FlatAppearance.BorderSize  = 1
    $btn.ForeColor  = $color
    $btn.BackColor  = $script:ColorFieldBg
    $btn.Cursor     = "Hand"
    $btn.Font       = New-UIFont 11 -Bold
    $btn.Height     = 28
    $btn.Anchor     = "Top,Right"
    $qcHeaderPanel.Controls.Add($btn)
    return $btn
}

$btnQcCommit  = New-QcBtn "Commit 0 changes" $script:ColorPurple
$btnResetAll  = New-QcBtn "Reset All"  $script:ColorRed
$btnQcRefresh = New-QcBtn "Refresh"    $script:ColorDim

# Right-anchor positions — will be set after form layout
$btnQcRefresh.Width = 70
$btnResetAll.Width  = 80
$btnQcCommit.Width  = 150

$btnQcRefresh.Location = New-Object System.Drawing.Point(880, 5)
$btnResetAll.Location  = New-Object System.Drawing.Point(796, 5)
$btnQcCommit.Location  = New-Object System.Drawing.Point(640, 5)

$btnQcRefresh.Anchor = "Top,Right"
$btnResetAll.Anchor  = "Top,Right"
$btnQcCommit.Anchor  = "Top,Right"
$btnQcCommit.Enabled = $false

# DataGridView
$qcGrid = New-Object System.Windows.Forms.DataGridView
$qcGrid.Dock                          = "Fill"
$qcGrid.BackgroundColor               = $script:ColorConsoleBg
$qcGrid.GridColor                     = [System.Drawing.Color]::FromArgb(55, 55, 55)
$qcGrid.BorderStyle                   = "None"
$qcGrid.RowHeadersVisible             = $false
$qcGrid.AllowUserToAddRows            = $false
$qcGrid.AllowUserToDeleteRows         = $false
$qcGrid.AllowUserToResizeRows         = $false
$qcGrid.SelectionMode                 = "FullRowSelect"
$qcGrid.MultiSelect                   = $false
$qcGrid.ReadOnly                      = $false
$qcGrid.EnableHeadersVisualStyles     = $false
$qcGrid.AutoSizeRowsMode              = "None"
$qcGrid.ScrollBars                    = "Vertical"
$qcGrid.Font                          = New-UIFont 11

# Header style
$qcGrid.ColumnHeadersDefaultCellStyle.BackColor  = [System.Drawing.Color]::FromArgb(50,50,50)
$qcGrid.ColumnHeadersDefaultCellStyle.ForeColor  = $script:ColorGold
$qcGrid.ColumnHeadersDefaultCellStyle.Font       = New-UIFont 11 -Bold
$qcGrid.ColumnHeadersDefaultCellStyle.Padding    = New-Object System.Windows.Forms.Padding(4,0,0,0)
$qcGrid.ColumnHeadersHeight                      = 26

# Default cell style
$qcGrid.DefaultCellStyle.BackColor    = $script:ColorConsoleBg
$qcGrid.DefaultCellStyle.ForeColor    = $script:ColorText
$qcGrid.DefaultCellStyle.SelectionBackColor = [System.Drawing.Color]::FromArgb(55,55,80)
$qcGrid.DefaultCellStyle.SelectionForeColor = $script:ColorWhite
$qcGrid.DefaultCellStyle.Padding      = New-Object System.Windows.Forms.Padding(4,2,4,2)

$qcGrid.AlternatingRowsDefaultCellStyle.BackColor = [System.Drawing.Color]::FromArgb(26, 26, 26)
$qcGrid.AlternatingRowsDefaultCellStyle.SelectionBackColor = [System.Drawing.Color]::FromArgb(55,55,80)

# Columns
$colNum = New-Object System.Windows.Forms.DataGridViewTextBoxColumn
$colNum.HeaderText = "#"; $colNum.Name = "Num"; $colNum.Width = 40
$colNum.ReadOnly = $true; $colNum.Resizable = "False"
$colNum.DefaultCellStyle.Alignment = "MiddleCenter"
$colNum.DefaultCellStyle.ForeColor = $script:ColorDim
[void]$qcGrid.Columns.Add($colNum)

$colStatus = New-Object System.Windows.Forms.DataGridViewComboBoxColumn
$colStatus.HeaderText = "Status"; $colStatus.Name = "Status"; $colStatus.Width = 90
$colStatus.Resizable = "False"
[void]$colStatus.Items.AddRange(@("Pending","Pass","Fail","Skip"))
$colStatus.DefaultCellStyle.Alignment = "MiddleCenter"
[void]$qcGrid.Columns.Add($colStatus)

$colTest = New-Object System.Windows.Forms.DataGridViewTextBoxColumn
$colTest.HeaderText = "Test"; $colTest.Name = "Test"
$colTest.ReadOnly = $true; $colTest.AutoSizeMode = "Fill"; $colTest.MinimumWidth = 200
$colTest.DefaultCellStyle.WrapMode = "True"
[void]$qcGrid.Columns.Add($colTest)

$colExpected = New-Object System.Windows.Forms.DataGridViewTextBoxColumn
$colExpected.HeaderText = "Expected"; $colExpected.Name = "Expected"; $colExpected.Width = 180
$colExpected.ReadOnly = $true
$colExpected.DefaultCellStyle.WrapMode = "True"
$colExpected.DefaultCellStyle.ForeColor = $script:ColorDim
[void]$qcGrid.Columns.Add($colExpected)

$colNotes = New-Object System.Windows.Forms.DataGridViewTextBoxColumn
$colNotes.HeaderText = "Notes"; $colNotes.Name = "Notes"; $colNotes.Width = 160
$colNotes.DefaultCellStyle.WrapMode = "True"
[void]$qcGrid.Columns.Add($colNotes)

$playtestPanel.Controls.Add($qcGrid)
$qcGrid.BringToFront()

# Enable double-buffering on DataGridView via reflection (protected property)
try {
    $dgvType = $qcGrid.GetType()
    $pi = $dgvType.GetProperty('DoubleBuffered', [System.Reflection.BindingFlags]'Instance,NonPublic')
    $pi.SetValue($qcGrid, $true, $null)
} catch {}

# ============================================================================
# QC data model
# ============================================================================

$script:QcAllRows     = @()
$script:QcDirtyNums   = [System.Collections.Generic.HashSet[string]]::new()
$script:QcSaveTimer   = $null

function Parse-QcStatus($raw) {
    $r = $raw.Trim()
    if ($r -match '^\[x\]$' -or $r -match '^\(x\)$' -or $r -eq '[+]' -or $r -match 'PASS') { return "Pass" }
    if ($r -match '^\[!\]$' -or $r -match 'FAIL' -or $r -match '^fail$') { return "Fail" }
    if ($r -match '^\[-\]$' -or $r -match '^N/?A$') { return "Skip" }
    return "Pending"
}

function StatusToMdMarker($status) {
    switch ($status) {
        "Pass"    { return "[x]" }
        "Fail"    { return "[!]" }
        "Skip"    { return "[-]" }
        default   { return "[ ]" }
    }
}

function Load-QcFile {
    $script:QcAllRows = @()
    if (-not (Test-Path $script:QcFile)) { return }
    $lines = Get-Content $script:QcFile -Encoding UTF8
    $currentSection = ""
    foreach ($line in $lines) {
        if ($line -match '^##\s+(.+)$') { $currentSection = $Matches[1].Trim(); continue }
        if ($line -match '^\|') {
            if ($line -match '^\|\s*[-:]+') { continue }
            $cells = $line -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
            if ($cells.Count -lt 4) { continue }
            $numCell = $cells[0]; $testCell = $cells[1]; $expCell = $cells[2]; $stCell = $cells[3]
            $notes   = $(if ($cells.Count -ge 5) { $cells[4] } else { "" })
            if ($numCell -eq '#' -or $testCell -eq 'Test') { continue }
            if ($numCell -notmatch '^\d+$') { continue }
            $script:QcAllRows += @{
                Num     = $numCell; Test = $testCell; Expected = $expCell
                Status  = Parse-QcStatus $stCell
                Notes   = $notes; Section = $currentSection
                IsHeader = $false
            }
        }
    }
}

function Save-QcFile {
    if (-not (Test-Path $script:QcFile)) { return }
    $lines = Get-Content $script:QcFile -Encoding UTF8
    $statusMap = @{}; $notesMap = @{}
    foreach ($row in $script:QcAllRows) { $statusMap[$row.Num] = $row.Status; $notesMap[$row.Num] = $row.Notes }
    $outLines = @()
    foreach ($line in $lines) {
        if ($line -match '^\|' -and $line -notmatch '^\|\s*[-:]+') {
            $cells = $line -split '\|'
            if ($cells.Count -ge 5) {
                $numCell = $cells[1].Trim()
                if ($numCell -match '^\d+$' -and $statusMap.ContainsKey($numCell)) {
                    $cells[4] = " $(StatusToMdMarker $statusMap[$numCell]) "
                    if ($cells.Count -ge 7 -and $notesMap.ContainsKey($numCell)) {
                        $cells[5] = " $($notesMap[$numCell]) "
                    }
                    $line = $cells -join '|'
                }
            }
        }
        $outLines += $line
    }
    Set-Content -Path $script:QcFile -Value $outLines -Encoding UTF8
}

function Get-StatusColor($status) {
    switch ($status) {
        "Pass"    { return $script:ColorGreen }
        "Fail"    { return $script:ColorRed }
        "Skip"    { return $script:ColorDim }
        default   { return $script:ColorText }
    }
}

function Update-QcSummary {
    $total  = 0; $passed = 0; $failed = 0; $skip = 0
    foreach ($row in $script:QcAllRows) {
        $total++
        switch ($row.Status) { "Pass" { $passed++ } "Fail" { $failed++ } "Skip" { $skip++ } }
    }
    $pend = $total - $passed - $failed - $skip
    $lblQcSummary.Text = "Pass: $passed  Fail: $failed  Skip: $skip  Pending: $pend  Total: $total"
    if ($failed -gt 0)              { $lblQcSummary.ForeColor = $script:ColorRed }
    elseif ($passed -eq $total -and $total -gt 0) { $lblQcSummary.ForeColor = $script:ColorGreen }
    else                            { $lblQcSummary.ForeColor = $script:ColorDim }
}

function Populate-QcGrid {
    param([string]$filter = "All")
    $qcGrid.SuspendLayout()
    $qcGrid.Rows.Clear()
    $lastSection = ""
    $dataRowIdx  = 0

    foreach ($row in $script:QcAllRows) {
        # Insert section header row if section changed
        if ($row.Section -ne $lastSection -and $row.Section -ne "") {
            # If filtering, check if this section has any matching rows
            $hasMatch = $false
            if ($filter -eq "All") { $hasMatch = $true }
            else {
                foreach ($r in $script:QcAllRows) {
                    if ($r.Section -eq $row.Section -and $r.Status -eq $filter) { $hasMatch = $true; break }
                }
            }
            if ($hasMatch) {
                $hIdx = $qcGrid.Rows.Add()
                $hr   = $qcGrid.Rows[$hIdx]
                $hr.Cells["Num"].Value    = ""
                $hr.Cells["Status"].Value = ""
                $hr.Cells["Test"].Value   = $row.Section
                $hr.Cells["Expected"].Value = ""
                $hr.Cells["Notes"].Value  = ""
                $hr.Tag        = "header"
                $hr.Height     = 24
                $hr.ReadOnly   = $true
                $hr.DefaultCellStyle.BackColor  = [System.Drawing.Color]::FromArgb(50, 48, 25)
                $hr.DefaultCellStyle.ForeColor  = $script:ColorGold
                $hr.DefaultCellStyle.Font       = New-UIFont 11 -Bold
                $hr.DefaultCellStyle.SelectionBackColor = [System.Drawing.Color]::FromArgb(50,48,25)
                $hr.DefaultCellStyle.SelectionForeColor = $script:ColorGold
            }
            $lastSection = $row.Section
        }

        # Skip rows that don't match filter
        if ($filter -ne "All" -and $row.Status -ne $filter) { continue }

        $rIdx = $qcGrid.Rows.Add()
        $gr   = $qcGrid.Rows[$rIdx]
        $gr.Cells["Num"].Value      = $row.Num
        $gr.Cells["Status"].Value   = $row.Status
        $gr.Cells["Test"].Value     = $row.Test
        $gr.Cells["Expected"].Value = $row.Expected
        $gr.Cells["Notes"].Value    = $row.Notes
        $gr.Tag    = $row.Num
        $gr.Height = 30

        $sc = Get-StatusColor $row.Status
        $gr.DefaultCellStyle.ForeColor = $script:ColorText
        $gr.Cells["Status"].Style.ForeColor = $sc
        $gr.Cells["Status"].Style.Font = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
        $dataRowIdx++
    }
    $qcGrid.ResumeLayout()
    Update-QcSummary
}

# Debounced notes save
$script:NotesSaveTimer = New-Object System.Windows.Forms.Timer
$script:NotesSaveTimer.Interval = 500
$script:NotesSaveTimer.Add_Tick({
    try {
        $script:NotesSaveTimer.Stop()
        if ($script:QcDirtyNums.Count -gt 0) {
            Save-QcFile
            $script:QcDirtyNums.Clear()
        }
    } catch {}
})

$qcGrid.Add_CellValueChanged({
    param($s, $e)
    if ($e.RowIndex -lt 0) { return }
    $gr = $qcGrid.Rows[$e.RowIndex]
    if ($gr.Tag -eq "header") { return }
    $num = $gr.Tag
    if ($null -eq $num) { return }

    $colName = $qcGrid.Columns[$e.ColumnIndex].Name

    # Find and update model row
    foreach ($row in $script:QcAllRows) {
        if ($row.Num -eq "$num") {
            if ($colName -eq "Status") {
                $newVal = $gr.Cells["Status"].Value
                if ($null -ne $newVal) {
                    $row.Status = "$newVal"
                    $gr.Cells["Status"].Style.ForeColor = Get-StatusColor "$newVal"
                    Save-QcFile
                    Update-QcSummary
                }
            }
            elseif ($colName -eq "Notes") {
                $row.Notes = "$($gr.Cells['Notes'].Value)"
                [void]$script:QcDirtyNums.Add("$num")
                $script:NotesSaveTimer.Stop()
                $script:NotesSaveTimer.Start()
            }
            break
        }
    }
})

$qcGrid.Add_CurrentCellDirtyStateChanged({
    if ($qcGrid.IsCurrentCellDirty -and $qcGrid.CurrentCell -ne $null -and
        $qcGrid.Columns[$qcGrid.CurrentCell.ColumnIndex].Name -eq "Status") {
        $qcGrid.CommitEdit([System.Windows.Forms.DataGridViewDataErrorContexts]::Commit)
    }
})

$cmbQcFilter.Add_SelectedIndexChanged({
    $filter = $cmbQcFilter.SelectedItem.ToString()
    Populate-QcGrid -filter $filter
})

$btnQcRefresh.Add_Click({
    Load-QcFile
    Populate-QcGrid -filter $cmbQcFilter.SelectedItem.ToString()
})

$btnResetAll.Add_Click({
    $res = [System.Windows.Forms.MessageBox]::Show(
        "Reset all QC items to Pending?`nThis will overwrite qc-tests.md.",
        "Confirm Reset", "YesNo", "Warning")
    if ($res -ne "Yes") { return }
    foreach ($row in $script:QcAllRows) { $row.Status = "Pending" }
    Save-QcFile
    Populate-QcGrid -filter $cmbQcFilter.SelectedItem.ToString()
})

# ============================================================================
# Output helpers (accumulate, no display box)
# ============================================================================

function Accumulate-Line($text, $class) {
    [void]$script:AllOutput.Add($text)
    if ($class -eq "error") {
        [void]$script:ErrorLines.Add($text)
        if ($script:BuildTarget -eq "server") { [void]$script:ServerErrorLines.Add($text) }
        else                                   { [void]$script:ClientErrorLines.Add($text) }
    }
}

# ============================================================================
# Classify build output lines
# ============================================================================

function Classify-Line($line) {
    if ($line -match ':\s*error\s*:|:\s*fatal error\s*:|^make.*\*\*\*.*Error|FAILED|undefined reference|multiple definition|collect2:\s*error|ld returned|cannot find -l|CMake Error|error:\s|Error:') { return "error" }
    if ($line -match ':\s*warning\s*:|Warning:') { return "warning" }
    if ($line -match ':\s*note\s*:')              { return "note" }
    return "normal"
}

# ============================================================================
# Version management
# ============================================================================

function Get-ProjectVersion {
    $cmake = Join-Path $script:ProjectDir "CMakeLists.txt"
    if (!(Test-Path $cmake)) { return @{ Major=0; Minor=0; Revision=0; String="v0.0.0" } }
    $content = Get-Content $cmake -Raw
    $major = 0; $minor = 0; $revision = 0
    if ($content -match 'VERSION_SEM_MAJOR\s+(\d+)') { $major    = [int]$Matches[1] }
    if ($content -match 'VERSION_SEM_MINOR\s+(\d+)') { $minor    = [int]$Matches[1] }
    if ($content -match 'VERSION_SEM_PATCH\s+(\d+)') { $revision = [int]$Matches[1] }
    return @{ Major=$major; Minor=$minor; Revision=$revision; String="v$major.$minor.$revision" }
}

function Set-ProjectVersion($major, $minor, $revision) {
    $cmake = Join-Path $script:ProjectDir "CMakeLists.txt"
    if (!(Test-Path $cmake)) { return }
    $c = Get-Content $cmake -Raw
    $c = $c -replace '(VERSION_SEM_MAJOR\s+)\d+', "`${1}$major"
    $c = $c -replace '(VERSION_SEM_MINOR\s+)\d+', "`${1}$minor"
    $c = $c -replace '(VERSION_SEM_PATCH\s+)\d+', "`${1}$revision"
    Set-Content -Path $cmake -Value $c -NoNewline
}

function Get-PushBarVersion {
    $major = 0; $minor = 0; $revision = 0
    try { $major    = [int]$txtVerMajor.Text    } catch {}
    try { $minor    = [int]$txtVerMinor.Text    } catch {}
    try { $revision = [int]$txtVerRevision.Text } catch {}
    return @{ Major=$major; Minor=$minor; Revision=$revision; String="v$major.$minor.$revision" }
}

function Refresh-VersionDisplay {
    $ver = Get-ProjectVersion
    $txtVerMajor.Text    = "$($ver.Major)"
    $txtVerMinor.Text    = "$($ver.Minor)"
    $txtVerRevision.Text = "$($ver.Revision)"
    Check-VersionWarning
}

# ============================================================================
# GitHub release cache
# ============================================================================

$script:GhReleaseCacheFile = Join-Path $script:ProjectDir ".build-release-cache.json"
$script:GhReleaseCache     = $null
$script:GhReleaseCacheTime = [DateTime]::MinValue
$script:GhOnline           = $false
$script:GhAuthOk           = $false
$script:LastAuthCheck      = [DateTime]::MinValue

function Load-ReleaseCache {
    if (Test-Path $script:GhReleaseCacheFile) {
        try {
            $json = Get-Content $script:GhReleaseCacheFile -Raw | ConvertFrom-Json
            $releases = @()
            foreach ($r in $json.releases) {
                $releases += @{ Tag=$r.Tag; Major=[int]$r.Major; Minor=[int]$r.Minor; Revision=[int]$r.Revision; Prerelease=[bool]$r.Prerelease }
            }
            $script:GhReleaseCache = $releases
            if ($json.fetchedAt) { $script:GhReleaseCacheTime = [DateTime]::Parse($json.fetchedAt) }
        } catch { $script:GhReleaseCache = @() }
    }
}

function Save-ReleaseCache($releases) {
    try {
        $data = @{ fetchedAt=[DateTime]::Now.ToString("o"); releases=@() }
        foreach ($r in $releases) { $data.releases += @{ Tag=$r.Tag; Major=$r.Major; Minor=$r.Minor; Revision=$r.Revision; Prerelease=$r.Prerelease } }
        $data | ConvertTo-Json -Depth 3 | Set-Content $script:GhReleaseCacheFile
    } catch {}
}

function Fetch-GithubReleases {
    if ($script:GhReleaseCache -ne $null -and $script:GhReleaseCache.Count -ge 0 -and
        ([DateTime]::Now - $script:GhReleaseCacheTime).TotalSeconds -lt 30) {
        return $script:GhReleaseCache
    }
    $releases = @(); $fetched = $false
    try {
        $repo = $script:Settings.GithubRepo
        if ($repo -ne "") {
            $json = gh api "repos/$repo/releases" --paginate 2>$null
            if ($LASTEXITCODE -eq 0 -and $json) {
                foreach ($rel in ($json | ConvertFrom-Json)) {
                    $tag = $rel.tag_name
                    if ($tag -match '(?:client-|server-)?v?(\d+)\.(\d+)\.(\d+)') {
                        $releases += @{ Tag=$tag; Major=[int]$Matches[1]; Minor=[int]$Matches[2]; Revision=[int]$Matches[3]; Prerelease=[bool]$rel.prerelease }
                    }
                }
                $fetched = $true
            }
        }
    } catch {}
    if ($fetched) {
        $script:GhReleaseCache = $releases; $script:GhReleaseCacheTime = [DateTime]::Now; $script:GhOnline = $true
        Save-ReleaseCache $releases
    } else {
        $script:GhOnline = $false
        if ($script:GhReleaseCache -eq $null -or $script:GhReleaseCache.Count -eq 0) { Load-ReleaseCache }
        if ($script:GhReleaseCache -eq $null) { $script:GhReleaseCache = @() }
    }
    try { Update-LatestVersionLabels } catch {}
    return $script:GhReleaseCache
}

function Update-LatestVersionLabels {
    $releases = $script:GhReleaseCache
    if ($null -eq $releases -or $releases.Count -eq 0) {
        $lblLatestDev.Text = "dev: ---"; $lblLatestStable.Text = "stable: ---"; return
    }
    $src = $(if ($script:GhOnline) { "" } else { "*" })
    $hd = $null; $hdv = -1; $hs = $null; $hsv = -1
    foreach ($rel in $releases) {
        $val = $rel.Major*1000000 + $rel.Minor*1000 + $rel.Revision
        if ($rel.Prerelease) { if ($val -gt $hdv) { $hdv = $val; $hd = $rel } }
        else                 { if ($val -gt $hsv) { $hsv = $val; $hs = $rel } }
    }
    $lblLatestDev.Text    = $(if ($hd) { "dev: $($hd.Major).$($hd.Minor).$($hd.Revision)$src" } else { "dev: ---" })
    $lblLatestStable.Text = $(if ($hs) { "stable: $($hs.Major).$($hs.Minor).$($hs.Revision)$src" } else { "stable: ---" })
}

function Check-VersionWarning {
    $ver = Get-PushBarVersion
    $lblVerWarning.Text = ""
    try {
        $releases = Fetch-GithubReleases
        if ($releases.Count -eq 0) { return }
        foreach ($rel in $releases) {
            if ($rel.Major -eq $ver.Major -and $rel.Minor -eq $ver.Minor -and $rel.Revision -eq $ver.Revision) {
                $src = $(if ($script:GhOnline) { "" } else { " (cached)" })
                $lblVerWarning.Text = "WARNING: $($ver.String) already released$src"
                $lblVerWarning.ForeColor = $script:ColorOrange; return
            }
        }
        $curVal = $ver.Major*1000000 + $ver.Minor*1000 + $ver.Revision
        $highest = 0; $highTag = ""
        foreach ($rel in $releases) {
            $v = $rel.Major*1000000 + $rel.Minor*1000 + $rel.Revision
            if ($v -gt $highest) { $highest = $v; $highTag = $rel.Tag }
        }
        if ($highest -gt 0 -and $curVal -lt $highest) {
            $src = $(if ($script:GhOnline) { "" } else { " (cached)" })
            $lblVerWarning.Text = "WARNING: < latest $highTag$src"
            $lblVerWarning.ForeColor = $script:ColorRed
        }
    } catch {}
}

# ============================================================================
# CHANGES.md
# ============================================================================

function Read-ChangesFile {
    $sections = @{ "Improvements"=@(); "Additions"=@(); "Updates"=@(); "Known Issues"=@(); "Missing Content"=@() }
    if (!(Test-Path $script:ChangesFile)) { return $sections }
    $cur = ""
    foreach ($line in (Get-Content $script:ChangesFile)) {
        if ($line -match '^## (.+)$') { $sn = $Matches[1].Trim(); if ($sections.ContainsKey($sn)) { $cur = $sn }; continue }
        if ($cur -ne "" -and $line -match '^\s*-\s+(.+)$') { $sections[$cur] += $Matches[1].Trim() }
    }
    return $sections
}

function Clear-ChangesForRelease {
    $s = Read-ChangesFile
    $s["Improvements"] = @(); $s["Additions"] = @(); $s["Updates"] = @()
    $lines = @("# Changelog - Perfect Dark 2","")
    foreach ($sec in @("Improvements","Additions","Updates","Known Issues","Missing Content")) {
        $lines += "## $sec"
        foreach ($item in $s[$sec]) { $lines += "- $item" }
        $lines += ""
    }
    Set-Content -Path $script:ChangesFile -Value ($lines -join "`n")
}

function Format-ReleaseNotes($sections, $verStr, $isStable) {
    $lines = @()
    if ($isStable) {
        $lines += "# Perfect Dark 2 - $verStr"; $lines += ""
        if ($sections["Additions"].Count -gt 0)    { $lines += "## New Features";    foreach ($i in $sections["Additions"])    { $lines += "- $i" }; $lines += "" }
        if ($sections["Improvements"].Count -gt 0) { $lines += "## What's Fixed";    foreach ($i in $sections["Improvements"]) { $lines += "- $i" }; $lines += "" }
        if ($sections["Updates"].Count -gt 0)      { $lines += "## What's Changed";  foreach ($i in $sections["Updates"])      { $lines += "- $i" }; $lines += "" }
        if ($sections["Known Issues"].Count -gt 0) { $lines += "## Known Issues";    foreach ($i in $sections["Known Issues"]) { $lines += "- $i" }; $lines += "" }
    } else {
        $lines += "# $verStr - Development Build"; $lines += ""; $lines += "**Pre-release build for testing.**"; $lines += ""
        foreach ($sec in @("Improvements","Additions","Updates","Known Issues","Missing Content")) {
            if ($sections[$sec].Count -gt 0) { $lines += "## $sec"; foreach ($i in $sections[$sec]) { $lines += "- $i" }; $lines += "" }
        }
    }
    return ($lines -join "`n")
}

# ============================================================================
# Git functions
# ============================================================================

function Update-GitChangeCount {
    if (([DateTime]::Now - $script:LastGitCheck).TotalSeconds -lt 5) { return }
    if ($script:GitBusy) { return }
    $script:LastGitCheck = [DateTime]::Now
    try {
        $status = git -C $script:ProjectDir status --porcelain 2>$null
        $count  = $(if ($status) { ($status | Measure-Object).Count } else { 0 })
    } catch { $count = 0 }
    $script:GitChangeCount = $count
    $s = $(if ($count -eq 1) { "" } else { "s" })
    if ($count -gt 0) {
        $btnQcCommit.Text = "Commit $count change$s"; $btnQcCommit.Enabled = $true
        $btnQcCommit.ForeColor = $script:ColorPurple
    } else {
        $btnQcCommit.Text = "No changes"; $btnQcCommit.Enabled = $false
        $btnQcCommit.ForeColor = $script:ColorDisabled
    }
}

function Auto-Commit {
    $script:GitBusy = $true
    $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $lockFile = Join-Path $script:ProjectDir ".git\index.lock"
    if (Test-Path $lockFile) { Remove-Item $lockFile -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 200 }
    $status = git -C $script:ProjectDir status --porcelain 2>$null
    if (!$status) { $ErrorActionPreference = $savedEAP; $script:GitBusy = $false; return $true }
    $ver = Get-ProjectVersion
    $msg = "Build $($ver.String) - auto-commit before build"
    git -C $script:ProjectDir add -A 2>$null | Out-Null
    git -C $script:ProjectDir commit -m $msg 2>$null | Out-Null
    $exit = $LASTEXITCODE
    $ErrorActionPreference = $savedEAP; $script:GitBusy = $false
    return ($exit -eq 0)
}

function Start-ManualCommit {
    if ($script:GitChangeCount -eq 0) { return }
    $statusLines = git -C $script:ProjectDir status --porcelain 2>$null
    $added=@(); $modified=@(); $deleted=@()
    foreach ($line in $statusLines) {
        if ($line.Length -lt 3) { continue }
        $code = $line.Substring(0,2).Trim(); $file = $line.Substring(3).Trim()
        switch -Wildcard ($code) {
            "A"  { $added += $file } "M"  { $modified += $file } "MM" { $modified += $file }
            "D"  { $deleted += $file } "??" { $added += $file } default { $modified += $file }
        }
    }
    $detail = @()
    if ($modified.Count -gt 0) { $detail += "Modified (" + $modified.Count + "): " + ($modified -join ', ') }
    if ($added.Count    -gt 0) { $detail += "Added    (" + $added.Count + "):    " + ($added -join ', ') }
    if ($deleted.Count  -gt 0) { $detail += "Deleted  (" + $deleted.Count + "):  " + ($deleted -join ', ') }

    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = "Commit Changes"; $dlg.Size = New-Object System.Drawing.Size(520, 360)
    $dlg.StartPosition = "CenterParent"; $dlg.FormBorderStyle = "FixedDialog"
    $dlg.MaximizeBox = $false; $dlg.MinimizeBox = $false
    $dlg.BackColor = $script:ColorPanelBg; $dlg.ForeColor = $script:ColorWhite

    $lMsg = New-Object System.Windows.Forms.Label; $lMsg.Text = "Commit message (" + $script:GitChangeCount + " files):"
    $lMsg.Font = New-UIFont 10; $lMsg.ForeColor = $script:ColorWhite
    $lMsg.Location = New-Object System.Drawing.Point(12,12); $lMsg.AutoSize = $true; $dlg.Controls.Add($lMsg)

    $tMsg = New-Object System.Windows.Forms.TextBox; $tMsg.Location = New-Object System.Drawing.Point(12,36)
    $tMsg.Size = New-Object System.Drawing.Size(480,24); $tMsg.BackColor = $script:ColorFieldBg
    $tMsg.ForeColor = $script:ColorWhite; $tMsg.Font = New-Object System.Drawing.Font("Consolas",10)
    $tMsg.BorderStyle = "FixedSingle"; $ver = Get-ProjectVersion; $tMsg.Text = $ver.String + " -"; $dlg.Controls.Add($tMsg)

    $lDet = New-Object System.Windows.Forms.Label; $lDet.Text = "Changes:"; $lDet.Font = New-UIFont 11
    $lDet.ForeColor = $script:ColorDim; $lDet.Location = New-Object System.Drawing.Point(12,68); $lDet.AutoSize = $true; $dlg.Controls.Add($lDet)

    $tDet = New-Object System.Windows.Forms.TextBox; $tDet.Location = New-Object System.Drawing.Point(12,88)
    $tDet.Size = New-Object System.Drawing.Size(480,130); $tDet.BackColor = [System.Drawing.Color]::FromArgb(30,30,35)
    $tDet.ForeColor = $script:ColorDim; $tDet.Font = New-Object System.Drawing.Font("Consolas",11)
    $tDet.Multiline = $true; $tDet.ReadOnly = $true; $tDet.ScrollBars = "Vertical"; $tDet.WordWrap = $false
    $tDet.BorderStyle = "FixedSingle"; $tDet.Text = ($detail -join "`r`n"); $dlg.Controls.Add($tDet)

    $chkPush = New-Object System.Windows.Forms.CheckBox; $chkPush.Text = "Push to GitHub after commit"
    $chkPush.Font = New-UIFont 11; $chkPush.ForeColor = $script:ColorDim
    $chkPush.Location = New-Object System.Drawing.Point(12,230); $chkPush.AutoSize = $true; $chkPush.Checked = $true
    $dlg.Controls.Add($chkPush)

    function DlgBtn($text,$x,$color,$dr) {
        $b = New-Object System.Windows.Forms.Button; $b.Text = $text
        $b.Location = New-Object System.Drawing.Point($x,272); $b.Size = New-Object System.Drawing.Size(86,30)
        $b.FlatStyle="Flat"; $b.FlatAppearance.BorderColor=$color; $b.ForeColor=$color
        $b.BackColor=$script:ColorFieldBg; $b.Font=New-UIFont 10 -Bold; $b.DialogResult=$dr; $dlg.Controls.Add($b); return $b
    }
    $bOk  = DlgBtn "Commit" 310 $script:ColorGreen "OK"
    $bCan = DlgBtn "Cancel" 406 $script:ColorDim   "Cancel"
    $dlg.AcceptButton = $bOk; $dlg.CancelButton = $bCan

    $result = $dlg.ShowDialog($form)
    $commitMsg = $tMsg.Text.Trim(); $shouldPush = $chkPush.Checked; $dlg.Dispose()
    if ($result -ne "OK" -or $commitMsg -eq "") { return }

    $script:GitBusy = $true
    $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $lockFile = Join-Path $script:ProjectDir ".git\index.lock"
    if (Test-Path $lockFile) { Remove-Item $lockFile -Force -ErrorAction SilentlyContinue }

    $lblBuildStatus.Text = "Committing..."; $lblBuildStatus.ForeColor = $script:ColorPurple

    $rs = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
    $rs.Open()
    $rs.SessionStateProxy.SetVariable('BgProjectDir', $script:ProjectDir)
    $rs.SessionStateProxy.SetVariable('BgCommitMsg',  $commitMsg)
    $rs.SessionStateProxy.SetVariable('BgShouldPush', $shouldPush)
    $ps = [System.Management.Automation.PowerShell]::Create()
    $ps.Runspace = $rs
    [void]$ps.AddScript({
        $r = @{ CommitExit=1; PushExit=0; CommitLog=@(); PushLog=@() }
        git -C $BgProjectDir add -A 2>&1 | Out-Null
        $r.CommitLog  = git -C $BgProjectDir commit -m $BgCommitMsg 2>&1
        $r.CommitExit = $LASTEXITCODE
        if ($r.CommitExit -eq 0 -and $BgShouldPush) {
            $up = git -C $BgProjectDir rev-parse --abbrev-ref "@{upstream}" 2>$null
            if ($LASTEXITCODE -ne 0 -or -not $up) {
                $br = git -C $BgProjectDir rev-parse --abbrev-ref HEAD 2>$null
                $r.PushLog = git -C $BgProjectDir push --set-upstream origin $br 2>&1
            } else {
                $r.PushLog = git -C $BgProjectDir push origin 2>&1
            }
            $r.PushExit = $LASTEXITCODE
        }
        return $r
    })
    $asyncHandle = $ps.BeginInvoke()
    $pollTimer = New-Object System.Windows.Forms.Timer; $pollTimer.Interval = 200
    $pollTimer.Add_Tick({
        try {
            if ($null -eq $asyncHandle -or -not $asyncHandle.IsCompleted) { return }
            if ($null -ne $pollTimer) { $pollTimer.Stop(); $pollTimer.Dispose() }
            if ($null -eq $ps) { $script:GitBusy = $false; return }
            $invResult = $ps.EndInvoke($asyncHandle)
            try { $ps.Dispose() } catch {}
            if ($null -ne $rs) { try { $rs.Close() } catch {}; try { $rs.Dispose() } catch {} }
            $gitR = $(if ($null -ne $invResult -and $invResult.Count -gt 0) { $invResult[0] } else { $null })
            if ($null -eq $gitR) {
                if ($null -ne $lblBuildStatus) { $lblBuildStatus.Text = "Commit error (no result)"; $lblBuildStatus.ForeColor = $script:ColorRed }
                $script:GitBusy = $false; return
            }
            if ($gitR.CommitExit -ne 0) {
                if ($null -ne $lblBuildStatus) { $lblBuildStatus.Text = "Commit failed"; $lblBuildStatus.ForeColor = $script:ColorRed }
            } else {
                if ($shouldPush -and $gitR.PushExit -ne 0) {
                    if ($null -ne $lblBuildStatus) { $lblBuildStatus.Text = "Push failed"; $lblBuildStatus.ForeColor = $script:ColorRed }
                } else {
                    if ($null -ne $lblBuildStatus) {
                        $lblBuildStatus.Text = $(if ($shouldPush) { "Committed + pushed" } else { "Committed" })
                        $lblBuildStatus.ForeColor = $script:ColorGreen
                    }
                }
            }
            if ($null -ne $savedEAP) { $ErrorActionPreference = $savedEAP }
            $script:GitBusy = $false; $script:LastGitCheck = [DateTime]::MinValue
            Update-GitChangeCount
        } catch {
            try { $script:GitBusy = $false } catch {}
        }
    })
    $pollTimer.Start()
}

$btnQcCommit.Add_Click({ Start-ManualCommit })

# ============================================================================
# Copy addin files (post-build)
# ============================================================================

function Copy-AddinFiles {
    if ($script:BuildTarget -eq "server") { return }
    if (!(Test-Path $script:AddinDir)) { return }
    if (!(Test-Path $script:BuildDir)) { return }
    $dataDir = Join-Path $script:AddinDir "data"
    if (Test-Path $dataDir) { Copy-Item $dataDir -Destination $script:BuildDir -Recurse -Force }
}

# ============================================================================
# Build pipeline
# ============================================================================

function Get-ConfigureStep {
    return @{
        Name = "Configure (CMake)"
        Exe  = $script:CMake
        Args = "-G `"Unix Makefiles`" -DCMAKE_MAKE_PROGRAM=`"$($script:Make)`" -DCMAKE_C_COMPILER=`"$($script:CC)`" -B `"$($script:BuildDir)`" -S `"$($script:ProjectDir)`""
    }
}

function Get-BuildStep($target) {
    $cores = $env:NUMBER_OF_PROCESSORS; if (!$cores) { $cores = 4 }
    $targetArg = ""; $label = "Build All"
    if ($target -eq "client") { $targetArg = "--target pd";        $label = "Build Client" }
    if ($target -eq "server") { $targetArg = "--target pd-server"; $label = "Build Server" }
    return @{ Name="$label (Compile)"; Exe=$script:CMake; Args="--build `"$($script:BuildDir)`" $targetArg -- -j$cores -k" }
}

function Set-BuildUI-Running($running) {
    $script:IsRunning = $running
    $btnBuild.Enabled = (-not $running)
    $btnPush.Enabled  = (-not $running)
    $btnBuild.ForeColor = $(if (-not $running) { $script:ColorGreen }   else { $script:ColorDisabled })
    $btnPush.ForeColor  = $(if (-not $running) { $script:ColorGold }    else { $script:ColorDisabled })
    $btnStop.Enabled    = $running
    $btnStop.Visible    = $running
    $btnStop.ForeColor  = $(if ($running) { $script:ColorRed } else { $script:ColorDisabled })
    Update-RunButtons
}

function Update-RunButtons {
    $ce = Test-Path (Join-Path $script:ClientBuildDir "PerfectDark.exe")
    $se = Test-Path (Join-Path $script:ServerBuildDir "PerfectDarkServer.exe")
    $btnRunGame.Enabled   = (-not $script:IsRunning) -and $ce
    $btnRunServer.Enabled = (-not $script:IsRunning) -and $se
    $btnRunGame.ForeColor   = $(if ((-not $script:IsRunning) -and $ce) { $script:ColorGreen  } else { $script:ColorDisabled })
    $btnRunServer.ForeColor = $(if ((-not $script:IsRunning) -and $se) { $script:ColorOrange } else { $script:ColorDisabled })
}

$script:BuildTimer = New-Object System.Windows.Forms.Timer
$script:BuildTimer.Interval = 100

$script:BuildTimer.Add_Tick({
  try {
    $linesThisTick = 0
    $line = $null
    while ($linesThisTick -lt 80 -and $script:OutputQueue.TryDequeue([ref]$line)) {
        if ($null -eq $line -or $line.Length -lt 4) { $linesThisTick++; continue }
        $text  = $line.Substring(4)
        $class = Classify-Line $text
        Accumulate-Line $text $class
        if ($class -eq "error" -and -not $script:HasErrors) {
            $script:HasErrors = $true
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191,0,0)
        }
        if ($text -match '^\[\s*(\d+)%\]') {
            $pct = [int]$Matches[1]
            if ($pct -ge $script:BuildPercent) {
                $script:BuildPercent = $pct
                $pw = $progressOuter.Width
                $fillW = [math]::Floor(($pct / 100.0) * $pw)
                $progressFill.Size = New-Object System.Drawing.Size($fillW, 16)
                $progressLabel.Text = "${pct}%"
                if (-not $script:HasErrors) { $progressFill.BackColor = $script:ColorBlue }
            }
        }
        $script:LastOutputTime = [DateTime]::Now
        $linesThisTick++
    }

    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        $totalElapsed = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
        $silentSec    = [math]::Floor(([DateTime]::Now - $script:LastOutputTime).TotalSeconds)
        $spin = $script:SpinnerChars[$script:SpinnerIndex % 4]; $script:SpinnerIndex++
        if ($silentSec -gt 2) {
            $lblBuildStatus.Text = "$($script:CurrentStep) $spin (${totalElapsed}s)"
        } elseif ($script:BuildPercent -gt 0) {
            $lblBuildStatus.Text = "$($script:CurrentStep): $($script:BuildPercent)% (${totalElapsed}s)"
        } else {
            $lblBuildStatus.Text = "$($script:CurrentStep) (${totalElapsed}s)"
        }
        return
    }

    if ($null -ne $script:Process -and $script:Process.HasExited -and $script:OutputQueue.IsEmpty) {
        $script:BuildTimer.Stop()
        $exitCode = $script:Process.ExitCode
        $elapsed  = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
        try { $script:Process.Dispose() } catch {}
        $script:Process = $null

        if ($exitCode -ne 0) {
            $progressFill.Size      = New-Object System.Drawing.Size($progressOuter.Width, 16)
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191,0,0)
            $progressLabel.Text     = "FAILED"
            $failedTime = [math]::Floor(([DateTime]::Now - $script:TargetStartTime).TotalSeconds)
            if ($script:BuildTarget -eq "server") { $script:ServerBuildFailed=$true; $script:ServerBuildTime=$failedTime }
            else                                   { $script:ClientBuildFailed=$true; $script:ClientBuildTime=$failedTime }
            $script:StepQueue.Clear()

            if ($script:PendingServerBuild) {
                $script:HasErrors=$false; $script:IsRunning=$false; return
            }

            Update-BuildStatusLabels
            Show-ErrorButtons
            Sound-Fail
            Set-BuildUI-Running $false
            return
        }

        $progressFill.Size      = New-Object System.Drawing.Size($progressOuter.Width, 16)
        $progressFill.BackColor = $(if ($script:HasErrors) { [System.Drawing.Color]::FromArgb(191,0,0) } else { $script:ColorBlue })
        $progressLabel.Text     = "100%"
        $lblBuildStatus.Text    = "$($script:CurrentStep) OK (${elapsed}s)"

        if ($script:StepQueue.Count -gt 0) {
            $next = $script:StepQueue.Dequeue()
            Start-Build-Step $next.Name $next.Exe $next.Args
        } else {
            $finalTime = [math]::Floor(([DateTime]::Now - $script:TargetStartTime).TotalSeconds)
            if ($script:BuildTarget -eq "server") { $script:ServerBuildTime = $finalTime }
            else                                   { $script:ClientBuildTime = $finalTime }

            $anyErrors = $script:HasErrors -or $script:ClientBuildFailed -or $script:ServerBuildFailed
            $progressFill.BackColor = $(if ($anyErrors) { [System.Drawing.Color]::FromArgb(191,0,0) } else { [System.Drawing.Color]::FromArgb(0,191,96) })
            $progressLabel.Text     = $(if ($anyErrors) { "DONE (errors)" } else { "BUILD OK" })

            Copy-AddinFiles
            Update-BuildStatusLabels
            if ($anyErrors) { Show-ErrorButtons }
            if ($anyErrors) { Sound-Fail } else { Sound-Success }
            $lblBuildStatus.Text    = $(if ($anyErrors) { "Build complete - see errors" } else { "Build complete" })
            $lblBuildStatus.ForeColor = $(if ($anyErrors) { $script:ColorOrange } else { $script:ColorGreen })
            $script:BuildSucceeded  = -not $anyErrors
            Set-BuildUI-Running $false
            Refresh-VersionDisplay
        }
    }
  } catch {}
})

function Start-Build-Step($stepName, $exe, $argList) {
    $script:CurrentStep   = $stepName
    $script:LastOutputTime = [DateTime]::Now
    $script:StepStartTime = [DateTime]::Now
    $script:BuildPercent  = 0
    $progressFill.Size    = New-Object System.Drawing.Size(0, 16)
    if (-not $script:HasErrors) { $progressFill.BackColor = $script:ColorBlue }
    $progressLabel.Text   = ""
    $lblBuildStatus.Text  = $stepName
    $lblBuildStatus.ForeColor = [System.Drawing.Color]::FromArgb(100,180,255)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName        = $exe
    $psi.Arguments       = $argList
    $psi.WorkingDirectory = $script:ProjectDir
    $psi.UseShellExecute  = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow   = $true
    $psi.EnvironmentVariables["PATH"]         = $env:PATH
    $psi.EnvironmentVariables["MSYSTEM"]      = "MINGW64"
    $psi.EnvironmentVariables["MINGW_PREFIX"] = "/mingw64"

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        [void]$proc.Start()
        $script:Process = $proc
        [AsyncLineReader]::StartReading($proc.StandardOutput, $script:OutputQueue, "OUT:")
        [AsyncLineReader]::StartReading($proc.StandardError,  $script:OutputQueue, "ERR:")
        $script:BuildTimer.Start()
    } catch {
        $lblBuildStatus.Text = "Failed to start: $exe"
        $lblBuildStatus.ForeColor = $script:ColorRed
        Set-BuildUI-Running $false
    }
}

function Update-BuildStatusLabels {
    if ($script:ClientBuildTime -gt 0 -or $script:ClientBuildFailed) {
        if ($script:ClientBuildFailed) {
            $lblClientStatus.Text = "client: FAILED (" + $script:ClientBuildTime + "s)"
            $lblClientStatus.ForeColor = $script:ColorRed
        } else {
            $lblClientStatus.Text = "client: SUCCESS (" + $script:ClientBuildTime + "s)"
            $lblClientStatus.ForeColor = $script:ColorGreen
        }
    }
    if ($script:ServerBuildTime -gt 0 -or $script:ServerBuildFailed) {
        if ($script:ServerBuildFailed) {
            $lblServerStatus.Text = "server: FAILED (" + $script:ServerBuildTime + "s)"
            $lblServerStatus.ForeColor = $script:ColorRed
        } else {
            $lblServerStatus.Text = "server: SUCCESS (" + $script:ServerBuildTime + "s)"
            $lblServerStatus.ForeColor = $script:ColorGreen
        }
    }
}

function Show-ErrorButtons {
    $btnCopyErrors.Visible = $true
    $btnCopyFullLog.Visible = $true
    $lblErrorCount.Visible = $true
    $total = $script:ErrorLines.Count
    $lblErrorCount.Text = "$total error line(s)"
}

function Hide-ErrorButtons {
    $btnCopyErrors.Visible  = $false
    $btnCopyFullLog.Visible = $false
    $lblErrorCount.Visible  = $false
}

# ============================================================================
# Start-Build
# ============================================================================

function Start-Build {
    if ($script:IsRunning) { return }
    $script:IsRunning = $true

    # Reset state
    $script:ErrorLines.Clear(); $script:AllOutput.Clear()
    $script:ClientErrorLines.Clear(); $script:ServerErrorLines.Clear()
    $script:ClientBuildFailed=$false; $script:ServerBuildFailed=$false
    $script:ClientBuildTime=0; $script:ServerBuildTime=0
    $script:TargetStartTime=[DateTime]::Now; $script:BuildSucceeded=$false
    $script:HasErrors=$false; $script:BuildTarget="client"; $script:PendingServerBuild=$true

    $lblClientStatus.Text="client: building..."; $lblClientStatus.ForeColor=$script:ColorDim
    $lblServerStatus.Text="server: waiting...";  $lblServerStatus.ForeColor=$script:ColorDim
    Hide-ErrorButtons

    # Disable buttons during commit phase; stop button stays hidden until build starts
    $btnBuild.Enabled   = $false
    $btnPush.Enabled    = $false
    $btnBuild.ForeColor = $script:ColorDisabled
    $btnPush.ForeColor  = $script:ColorDisabled
    $btnStop.Visible    = $false
    $btnStop.Enabled    = $false
    $lblBuildStatus.Text      = "Committing..."
    $lblBuildStatus.ForeColor = $script:ColorPurple
    Update-RunButtons

    # Remove stale git lock if present
    $bgLockFile = Join-Path $script:ProjectDir ".git\index.lock"
    if (Test-Path $bgLockFile) { Remove-Item $bgLockFile -Force -ErrorAction SilentlyContinue }

    $bgVer = Get-ProjectVersion
    $bgMsg = "Build " + $bgVer.String + " - auto-commit before build"

    $bgRs = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
    $bgRs.Open()
    $bgRs.SessionStateProxy.SetVariable('BgProjectDir', $script:ProjectDir)
    $bgRs.SessionStateProxy.SetVariable('BgCommitMsg',  $bgMsg)
    $bgPs = [System.Management.Automation.PowerShell]::Create()
    $bgPs.Runspace = $bgRs
    [void]$bgPs.AddScript({
        $st = git -C $BgProjectDir status --porcelain 2>$null
        if ($st) {
            git -C $BgProjectDir add -A 2>&1 | Out-Null
            git -C $BgProjectDir commit -m $BgCommitMsg 2>&1 | Out-Null
        }
    })
    $bgHandle = $bgPs.BeginInvoke()

    $bgPollTimer = New-Object System.Windows.Forms.Timer
    $bgPollTimer.Interval = 200
    $bgPollTimer.Add_Tick({
        try {
            if (-not $bgHandle.IsCompleted) { return }
            $bgPollTimer.Stop()
            $bgPollTimer.Dispose()
            try { $bgPs.EndInvoke($bgHandle) } catch {}
            try { $bgPs.Dispose() } catch {}
            try { $bgRs.Close(); $bgRs.Dispose() } catch {}

            # Clean both build dirs
            foreach ($dir in @($script:ClientBuildDir, $script:ServerBuildDir)) {
                if (Test-Path $dir) { Remove-Item -Path $dir -Recurse -Force -ErrorAction SilentlyContinue }
            }

            $script:BuildDir        = $script:ClientBuildDir
            $progressFill.Size      = New-Object System.Drawing.Size(0, 16)
            $progressFill.BackColor = $script:ColorBlue
            $progressLabel.Text     = ""

            $script:IsRunning = $false
            Set-BuildUI-Running $true
            $script:StepQueue.Clear()
            $script:StepQueue.Enqueue((Get-BuildStep "client"))
            $step = Get-ConfigureStep
            Start-Build-Step $step.Name $step.Exe $step.Args
        } catch {
            try {
                $script:IsRunning   = $false
                $btnBuild.Enabled   = $true
                $btnBuild.ForeColor = $script:ColorGreen
                $btnPush.Enabled    = $true
                $btnPush.ForeColor  = $script:ColorGold
                $btnStop.Visible    = $false
                if ($null -ne $lblBuildStatus) {
                    $lblBuildStatus.Text      = "Commit error"
                    $lblBuildStatus.ForeColor = $script:ColorRed
                }
            } catch {}
        }
    })
    $bgPollTimer.Start()
}

function Start-Build-Server-After-Client {
    if ($script:IsRunning) { return }
    $script:ClientBuildTime = [math]::Floor(([DateTime]::Now - $script:TargetStartTime).TotalSeconds)
    $script:TargetStartTime = [DateTime]::Now
    $script:BuildTarget = "server"; $script:HasErrors=$false
    $script:BuildDir = $script:ServerBuildDir

    $lblServerStatus.Text="server: building..."; $lblServerStatus.ForeColor=$script:ColorDim

    $progressFill.Size = New-Object System.Drawing.Size(0, 16)
    $progressFill.BackColor = $script:ColorBlue; $progressLabel.Text = ""

    Set-BuildUI-Running $true
    $script:StepQueue.Clear()
    $script:StepQueue.Enqueue((Get-BuildStep "server"))
    $step = Get-ConfigureStep
    Start-Build-Step $step.Name $step.Exe $step.Args
}

# ============================================================================
# Push Release
# ============================================================================

function Start-PushRelease {
    if ($script:IsRunning) { return }

    $releaseScript = Join-Path $script:ProjectDir "release.ps1"
    if (!(Test-Path $releaseScript)) {
        [System.Windows.Forms.MessageBox]::Show("release.ps1 not found in project root.", "Push Release",
            "OK", [System.Windows.Forms.MessageBoxIcon]::Error)
        return
    }

    $pushVer = Get-PushBarVersion
    Set-ProjectVersion $pushVer.Major $pushVer.Minor $pushVer.Revision

    Auto-Commit

    $ver        = Get-PushBarVersion
    $verStr     = "$($ver.Major).$($ver.Minor).$($ver.Revision)"
    $sections   = Read-ChangesFile
    $totalChanges = ($sections["Improvements"].Count + $sections["Additions"].Count + $sections["Updates"].Count)

    # Ask stable vs prerelease
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = "Push Release v$verStr"; $dlg.Size = New-Object System.Drawing.Size(420, 280)
    $dlg.StartPosition = "CenterParent"; $dlg.FormBorderStyle = "FixedDialog"
    $dlg.MaximizeBox = $false; $dlg.MinimizeBox = $false
    $dlg.BackColor = $script:ColorPanelBg; $dlg.ForeColor = $script:ColorWhite

    $lInfo = New-Object System.Windows.Forms.Label
    $lInfo.Text = "Push release v$verStr to GitHub?`n`nChanges logged: $totalChanges$(if($totalChanges-eq 0){"`nWARNING: No CHANGES.md entries."})"
    $lInfo.Font = New-UIFont 10; $lInfo.ForeColor = $script:ColorText
    $lInfo.Location = New-Object System.Drawing.Point(16,16); $lInfo.Size = New-Object System.Drawing.Size(380,80)
    $dlg.Controls.Add($lInfo)

    $lTags = New-Object System.Windows.Forms.Label
    $lTags.Text = "Will create tags: client-v$verStr + server-v$verStr and push to GitHub."
    $lTags.Font = New-UIFont 11; $lTags.ForeColor = $script:ColorDim
    $lTags.Location = New-Object System.Drawing.Point(16, 136); $lTags.Size = New-Object System.Drawing.Size(380, 40)
    $dlg.Controls.Add($lTags)

    function PushBtn($text,$x,$color,$dr) {
        $b=New-Object System.Windows.Forms.Button; $b.Text=$text
        $b.Location=New-Object System.Drawing.Point($x,192); $b.Size=New-Object System.Drawing.Size(90,30)
        $b.FlatStyle="Flat"; $b.FlatAppearance.BorderColor=$color; $b.ForeColor=$color
        $b.BackColor=$script:ColorFieldBg; $b.Font=New-UIFont 10 -Bold; $b.DialogResult=$dr; $dlg.Controls.Add($b); return $b
    }
    $bOk  = PushBtn "Push"   216 $script:ColorGold "OK"
    $bCan = PushBtn "Cancel" 312 $script:ColorDim  "Cancel"
    $dlg.AcceptButton=$bOk; $dlg.CancelButton=$bCan

    $result   = $dlg.ShowDialog($form)
    $isStable = $chkStable.Checked; $dlg.Dispose()
    if ($result -ne "OK") { return }

    # Generate release notes
    $notesFile = Join-Path $script:ProjectDir "RELEASE_v$verStr.md"
    if ($totalChanges -gt 0) {
        $notes = Format-ReleaseNotes $sections "v$verStr" $isStable
        Set-Content -Path $notesFile -Value $notes
    }

    $script:ErrorLines.Clear(); $script:AllOutput.Clear()
    $script:HasErrors = $false; $script:BuildTarget = ""
    Hide-ErrorButtons
    Set-BuildUI-Running $true
    $script:StepQueue.Clear()
    $script:PendingPostRelease = ($totalChanges -gt 0)

    $scriptArgs = "-Version `"v$verStr`""
    if (-not $isStable) { $scriptArgs += " -Prerelease" }

    Start-Build-Step "Push Release v$verStr" "powershell.exe" "-ExecutionPolicy Bypass -Command `"& { . '$releaseScript' $scriptArgs } *>&1`""
}

# ============================================================================
# Auth check
# ============================================================================

function Check-Auth {
    try {
        $savedEAP = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        $out = gh auth status 2>&1
        $ErrorActionPreference = $savedEAP
        $str = ($out | ForEach-Object { $_.ToString() }) -join "`n"
        if ($str -match 'Logged in to') {
            $lblAuth.Text = "auth ok"; $lblAuth.ForeColor = $script:ColorGreen
        } else {
            $lblAuth.Text = "authenticate"; $lblAuth.ForeColor = $script:ColorRed
        }
    } catch {
        $lblAuth.Text = "gh not found"; $lblAuth.ForeColor = $script:ColorRed
    }
}

$lblAuth.Add_Click({
    if (-not $script:GhAuthOk) {
        Start-Process "cmd.exe" -ArgumentList "/k gh auth login"
        # Force re-check sooner after user clicks
        $script:LastAuthCheck = [DateTime]::MinValue
    }
})

# ============================================================================
# Game launch
# ============================================================================

function Launch-Game($mode) {
    if ($script:IsRunning) { return }
    if ($mode -eq "server") {
        $dir = $script:ServerBuildDir; $exe = Join-Path $dir "PerfectDarkServer.exe"; $label = "Server"
    } else {
        $dir = $script:ClientBuildDir; $exe = Join-Path $dir "PerfectDark.exe"; $label = "Client"
    }
    if (!(Test-Path $exe)) {
        $lblGameStatus.Text = "$label not found"; $lblGameStatus.ForeColor = $script:ColorRed; return
    }
    try {
        $script:GameProcess = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
        $lblGameStatus.Text = "$label launched"; $lblGameStatus.ForeColor = $script:ColorGreen
    } catch {
        $lblGameStatus.Text = "Launch failed"; $lblGameStatus.ForeColor = $script:ColorRed
    }
}

function Update-GameStatus {
    $wasRunning = $script:GameRunning
    if ($null -ne $script:GameProcess) {
        try {
            $script:GameRunning = (-not $script:GameProcess.HasExited)
            if ($script:GameProcess.HasExited) { $script:GameProcess = $null }
        } catch { $script:GameProcess=$null; $script:GameRunning=$false }
    } else {
        try {
            $p = Get-Process -Name "PerfectDark" -ErrorAction SilentlyContinue
            $script:GameRunning = ($null -ne $p -and $p.Count -gt 0)
        } catch { $script:GameRunning=$false }
    }
    if ($script:GameRunning -and -not $script:IsRunning) {
        $lblGameStatus.Text="Game running"; $lblGameStatus.ForeColor=$script:ColorGreen
    } elseif ($wasRunning -and -not $script:GameRunning -and -not $script:IsRunning) {
        $lblGameStatus.Text="Game exited"; $lblGameStatus.ForeColor=$script:ColorDim
    }
}

# ============================================================================
# Settings dialog
# ============================================================================

function Show-SettingsDialog {
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = "Settings"; $dlg.Size = New-Object System.Drawing.Size(460, 240)
    $dlg.StartPosition = "CenterParent"; $dlg.FormBorderStyle = "FixedDialog"
    $dlg.MaximizeBox = $false; $dlg.MinimizeBox = $false
    $dlg.BackColor = $script:ColorBg; $dlg.ForeColor = $script:ColorWhite

    function SLbl($text,$x,$y) {
        $l=New-Object System.Windows.Forms.Label; $l.Text=$text; $l.Font=New-UIFont 10 -Bold
        $l.ForeColor=$script:ColorDim; $l.Location=New-Object System.Drawing.Point($x,$y); $l.AutoSize=$true
        $dlg.Controls.Add($l)
    }
    function STxt($val,$x,$y,$w) {
        $t=New-Object System.Windows.Forms.TextBox; $t.Text=$val
        $t.Location=New-Object System.Drawing.Point($x,$y); $t.Size=New-Object System.Drawing.Size($w,24)
        $t.BackColor=$script:ColorFieldBg; $t.ForeColor=$script:ColorWhite
        $t.Font=New-Object System.Drawing.Font("Consolas",11); $t.BorderStyle="FixedSingle"
        $dlg.Controls.Add($t); return $t
    }

    SLbl "GitHub Repository (owner/repo):" 16 16
    $tRepo = STxt $script:Settings.GithubRepo 16 40 350

    $chkSnd = New-Object System.Windows.Forms.CheckBox; $chkSnd.Text = "Enable sounds"
    $chkSnd.Font = New-UIFont 10; $chkSnd.ForeColor = $script:ColorWhite
    $chkSnd.Location = New-Object System.Drawing.Point(16, 80); $chkSnd.AutoSize=$true; $chkSnd.Checked=$script:Settings.SoundsEnabled
    $dlg.Controls.Add($chkSnd)

    $bSave = New-Object System.Windows.Forms.Button; $bSave.Text = "Save"
    $bSave.Location = New-Object System.Drawing.Point(340,160); $bSave.Size = New-Object System.Drawing.Size(80,30)
    $bSave.FlatStyle="Flat"; $bSave.FlatAppearance.BorderColor=$script:ColorGold; $bSave.ForeColor=$script:ColorGold
    $bSave.BackColor=$script:ColorFieldBg; $bSave.Font=New-UIFont 10 -Bold
    $bSave.Add_Click({
        $script:Settings.GithubRepo    = $tRepo.Text.Trim()
        $script:Settings.SoundsEnabled = $chkSnd.Checked
        Save-Settings; $dlg.Close()
    })
    $dlg.Controls.Add($bSave)

    $bCan = New-Object System.Windows.Forms.Button; $bCan.Text = "Cancel"
    $bCan.Location = New-Object System.Drawing.Point(250,160); $bCan.Size = New-Object System.Drawing.Size(80,30)
    $bCan.FlatStyle="Flat"; $bCan.FlatAppearance.BorderColor=$script:ColorDim; $bCan.ForeColor=$script:ColorDim
    $bCan.BackColor=$script:ColorFieldBg; $bCan.Font=New-UIFont 10 -Bold; $bCan.DialogResult="Cancel"
    $dlg.Controls.Add($bCan); $dlg.CancelButton=$bCan

    [void]$dlg.ShowDialog($form)
}

# ============================================================================
# Button handlers
# ============================================================================

$btnBuild.Add_Click({ Start-Build })

$btnPush.Add_Click({ Start-PushRelease })

$btnStop.Add_Click({
    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        try { $script:Process.Kill() } catch {}
        try { $script:Process.Dispose() } catch {}
        $script:Process = $null
    }
    $script:BuildTimer.Stop(); $script:StepQueue.Clear()
    $script:PendingServerBuild = $false
    $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191,0,0)
    $progressLabel.Text = "STOPPED"
    $lblBuildStatus.Text = "Stopped by user"; $lblBuildStatus.ForeColor = $script:ColorRed
    Set-BuildUI-Running $false
})

$btnCopyErrors.Add_Click({
    if ($script:ErrorLines.Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show("No errors captured.", "Copy Errors", "OK", [System.Windows.Forms.MessageBoxIcon]::Information)
        return
    }
    $text = ""
    $hc = $script:ClientErrorLines.Count -gt 0
    $hs = $script:ServerErrorLines.Count -gt 0
    $cc = $script:ClientErrorLines.Count; $sc = $script:ServerErrorLines.Count; $ec = $script:ErrorLines.Count
    if ($hc) { $text += "Client Build Errors (" + $cc + "):`r`n``````r`n"; foreach ($l in $script:ClientErrorLines) { $text += "$l`r`n" }; $text += "```````r`n`r`n" }
    if ($hs) { $text += "Server Build Errors (" + $sc + "):`r`n``````r`n"; foreach ($l in $script:ServerErrorLines) { $text += "$l`r`n" }; $text += "```````r`n" }
    if (-not $hc -and -not $hs) { $text += "Build errors:`r`n``````r`n"; foreach ($l in $script:ErrorLines) { $text += "$l`r`n" }; $text += "```````r`n" }
    [System.Windows.Forms.Clipboard]::SetText($text)
    $lblBuildStatus.Text = "" + $ec + " errors copied"; $lblBuildStatus.ForeColor = $script:ColorOrange
})

$btnCopyFullLog.Add_Click({
    if ($script:AllOutput.Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show("No output captured.", "Copy Full Log", "OK", [System.Windows.Forms.MessageBoxIcon]::Information)
        return
    }
    [System.Windows.Forms.Clipboard]::SetText(($script:AllOutput -join "`r`n"))
    $lblBuildStatus.Text = "Full log copied"; $lblBuildStatus.ForeColor = $script:ColorDim
})

# Version inc/dec
$btnIncMajor.Add_Click({    try { $v=[int]$txtVerMajor.Text } catch { $v=0 }; $txtVerMajor.Text="$($v+1)"; $txtVerMinor.Text="0"; $txtVerRevision.Text="0"; Check-VersionWarning })
$btnDecMajor.Add_Click({    try { $v=[int]$txtVerMajor.Text } catch { $v=0 }; if($v-gt 0){$txtVerMajor.Text="$($v-1)"}; Check-VersionWarning })
$btnIncMinor.Add_Click({    try { $v=[int]$txtVerMinor.Text } catch { $v=0 }; $txtVerMinor.Text="$($v+1)"; $txtVerRevision.Text="0"; Check-VersionWarning })
$btnDecMinor.Add_Click({    try { $v=[int]$txtVerMinor.Text } catch { $v=0 }; if($v-gt 0){$txtVerMinor.Text="$($v-1)"}; Check-VersionWarning })
$btnIncRevision.Add_Click({ try { $v=[int]$txtVerRevision.Text } catch { $v=0 }; $txtVerRevision.Text="$($v+1)"; Check-VersionWarning })
$btnDecRevision.Add_Click({ try { $v=[int]$txtVerRevision.Text } catch { $v=0 }; if($v-gt 0){$txtVerRevision.Text="$($v-1)"}; Check-VersionWarning })
$txtVerMajor.Add_TextChanged({ Check-VersionWarning })
$txtVerMinor.Add_TextChanged({ Check-VersionWarning })
$txtVerRevision.Add_TextChanged({ Check-VersionWarning })

# Bottom bar
$btnRunGame.Add_Click({   Launch-Game "client" })
$btnRunServer.Add_Click({ Launch-Game "server" })

# ============================================================================
# Main timer (game status + git count + pending builds)
# ============================================================================

$mainTimer = New-Object System.Windows.Forms.Timer
$mainTimer.Interval = 2000
$mainTimer.Add_Tick({
    try {
        Update-GameStatus
        Update-GitChangeCount
        Update-RunButtons

        if ($script:PendingPostRelease -and -not $script:IsRunning) {
            $script:PendingPostRelease = $false
            if ($script:BuildSucceeded) { Clear-ChangesForRelease }
        }

        if ($script:PendingServerBuild -and -not $script:IsRunning) {
            $script:PendingServerBuild = $false
            Start-Build-Server-After-Client
        }

        # Re-check auth every 30s if not yet authenticated
        if (-not $script:GhAuthOk -and ([DateTime]::Now - $script:LastAuthCheck).TotalSeconds -gt 30) {
            $script:LastAuthCheck = [DateTime]::Now
            try {
                $authOut = gh auth status 2>&1
                $authStr = ($authOut | ForEach-Object { $_.ToString() }) -join "`n"
                if ($null -ne $lblAuth) {
                    if ($authStr -match 'Logged in to') {
                        $lblAuth.Text = "auth ok"; $lblAuth.ForeColor = $script:ColorGreen
                        $lblAuth.Cursor = [System.Windows.Forms.Cursors]::Default
                        $script:GhAuthOk = $true
                    }
                }
            } catch {}
        }
    } catch {}
})
$mainTimer.Start()

# ============================================================================
# Form resize: reposition version panel
# ============================================================================

function Invoke-FormResize {
  try {
    if ($null -eq $buildPanel) { return }
    $tabW = $buildPanel.Width

    # Hero buttons - fill top area side by side (~45% each)
    $heroH = 210
    $heroGap = 8
    $half = [int](($tabW - 24 - $heroGap) / 2)
    if ($null -ne $btnBuild) {
        $btnBuild.Size     = New-Object System.Drawing.Size($half, $heroH)
        $btnBuild.Location = New-Object System.Drawing.Point(8, 8)
    }
    if ($null -ne $btnPush) {
        $btnPush.Location  = New-Object System.Drawing.Point((8 + $half + $heroGap), 8)
        $btnPush.Size      = New-Object System.Drawing.Size(($tabW - 8 - $half - $heroGap - 8), $heroH)
    }
    if ($null -ne $chkStable) {
        $chkStable.Location = New-Object System.Drawing.Point((8 + $half + $heroGap), ($heroH + 16))
    }

    # Version panel - right side below hero buttons
    $vpW = 400
    $vpX = $tabW - $vpW - 8
    if ($vpX -lt 420) { $vpX = 420 }
    if ($null -ne $verPanel)      { $verPanel.Location   = New-Object System.Drawing.Point($vpX, 226) }
    if ($null -ne $progressOuter) { $progressOuter.Width = $tabW - 24 }

    # Bottom bar - split Run Server / Run Game 50/50
    if ($null -ne $bottomBar) {
        $bbW = $bottomBar.Width
        $halfBar = [int]($bbW / 2)
        if ($null -ne $btnRunServer) {
            $btnRunServer.Size     = New-Object System.Drawing.Size(($halfBar - 2), 50)
            $btnRunServer.Location = New-Object System.Drawing.Point(0, 4)
        }
        if ($null -ne $btnRunGame) {
            $btnRunGame.Location   = New-Object System.Drawing.Point(($halfBar + 2), 4)
            $btnRunGame.Size       = New-Object System.Drawing.Size(($bbW - $halfBar - 2), 50)
        }
    }

    # Reposition QC header buttons
    if ($null -ne $qcHeaderPanel) {
        $qhW = $qcHeaderPanel.Width
        if ($null -ne $btnQcRefresh) { $btnQcRefresh.Location = New-Object System.Drawing.Point(($qhW - 80), 5) }
        if ($null -ne $btnResetAll)  { $btnResetAll.Location  = New-Object System.Drawing.Point(($qhW - 164), 5) }
        if ($null -ne $btnQcCommit)  { $btnQcCommit.Location  = New-Object System.Drawing.Point(($qhW - 318), 5) }
    }
  } catch {}
}

$form.Add_Resize({ Invoke-FormResize })

# ============================================================================
# Initialization
# ============================================================================

$form.Add_Shown({
    $form.Activate()
    Invoke-FormResize

    # Version from CMakeLists.txt (local, instant)
    Refresh-VersionDisplay
    Load-ReleaseCache
    Update-LatestVersionLabels

    # Run buttons (local file check, instant)
    Update-RunButtons

    # === Everything below is deferred so the window appears immediately ===

    # Deferred: Load QC file + populate grid (can be slow with large files)
    $qcTimer = New-Object System.Windows.Forms.Timer; $qcTimer.Interval = 50
    $qcTimer.Add_Tick({
        try {
            if ($null -ne $qcTimer) { $qcTimer.Stop(); $qcTimer.Dispose() }
            Load-QcFile
            Populate-QcGrid -filter "All"
        } catch {}
    })
    $qcTimer.Start()

    # Deferred: Git change count (runs git status)
    $gitInitTimer = New-Object System.Windows.Forms.Timer; $gitInitTimer.Interval = 100
    $gitInitTimer.Add_Tick({
        try {
            if ($null -ne $gitInitTimer) { $gitInitTimer.Stop(); $gitInitTimer.Dispose() }
            Update-GitChangeCount
        } catch {}
    })
    $gitInitTimer.Start()

    # Background: Auth check (network call)
    $rs2 = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
    $rs2.Open()
    $ps2 = [System.Management.Automation.PowerShell]::Create(); $ps2.Runspace = $rs2
    [void]$ps2.AddScript({ $out = gh auth status 2>&1; return ($out | ForEach-Object { $_.ToString() }) -join "`n" })
    $ah2 = $ps2.BeginInvoke()
    $authPoll = New-Object System.Windows.Forms.Timer; $authPoll.Interval = 300
    $authPoll.Add_Tick({
        try {
            if ($null -eq $ah2 -or -not $ah2.IsCompleted) { return }
            if ($null -ne $authPoll) { $authPoll.Stop(); $authPoll.Dispose() }
            $invStr = $(if ($null -ne $ps2) { $ps2.EndInvoke($ah2) } else { $null })
            if ($null -ne $ps2) { try { $ps2.Dispose() } catch {} }
            if ($null -ne $rs2) { try { $rs2.Close() } catch {}; try { $rs2.Dispose() } catch {} }
            $str = $(if ($null -ne $invStr -and $invStr.Count -gt 0) { $invStr[0] } else { $null })
            if ($null -ne $lblAuth) {
                if ($null -ne $str -and $str -match 'Logged in to') {
                    $lblAuth.Text = "auth ok"; $lblAuth.ForeColor = $script:ColorGreen
                    $lblAuth.Cursor = [System.Windows.Forms.Cursors]::Default
                    $script:GhAuthOk = $true
                } else {
                    $lblAuth.Text = "authenticate"; $lblAuth.ForeColor = $script:ColorRed
                    $lblAuth.Cursor = [System.Windows.Forms.Cursors]::Hand
                    $script:GhAuthOk = $false
                }
            }
        } catch {
            try { if ($null -ne $lblAuth) { $lblAuth.Text = "gh error"; $lblAuth.ForeColor = $script:ColorRed } } catch {}
        }
    })
    $authPoll.Start()

    # Background: GitHub release fetch (network call — move off UI thread)
    $rs3 = [System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
    $rs3.Open()
    $rs3.SessionStateProxy.SetVariable('BgRepo', $script:Settings.GithubRepo)
    $ps3 = [System.Management.Automation.PowerShell]::Create(); $ps3.Runspace = $rs3
    [void]$ps3.AddScript({
        if ($BgRepo -eq "") { return "none" }
        try {
            $json = gh api "repos/$BgRepo/releases" --paginate 2>$null
            if ($LASTEXITCODE -eq 0 -and $json) { return $json }
        } catch {}
        return "none"
    })
    $ah3 = $ps3.BeginInvoke()
    $relPoll = New-Object System.Windows.Forms.Timer; $relPoll.Interval = 500
    $relPoll.Add_Tick({
        try {
            if ($null -eq $ah3 -or -not $ah3.IsCompleted) { return }
            $relPoll.Stop(); $relPoll.Dispose()
            $result = $null
            try { $result = ($ps3.EndInvoke($ah3))[0] } catch {}
            if ($null -ne $ps3) { try { $ps3.Dispose() } catch {} }
            if ($null -ne $rs3) { try { $rs3.Close(); $rs3.Dispose() } catch {} }
            if ($null -ne $result -and $result -ne "none") {
                try {
                    $parsed = $result | ConvertFrom-Json
                    $releases = @()
                    foreach ($rel in $parsed) {
                        $tag = $rel.tag_name
                        if ($tag -match '(?:client-|server-)?v?(\d+)\.(\d+)\.(\d+)') {
                            $releases += @{ Tag = $tag; Major = [int]$Matches[1]; Minor = [int]$Matches[2]; Revision = [int]$Matches[3]; Prerelease = [bool]$rel.prerelease }
                        }
                    }
                    $script:GhReleaseCache = $releases
                    $script:GhReleaseCacheTime = [DateTime]::Now
                    $script:GhOnline = $true
                    Save-ReleaseCache $releases
                    Update-LatestVersionLabels
                    Check-VersionWarning
                } catch {}
            }
        } catch {}
    })
    $relPoll.Start()
})

$form.Add_FormClosing({
    try { if ($null -ne $script:BuildTimer) { $script:BuildTimer.Stop() } } catch {}
    try { if ($null -ne $mainTimer) { $mainTimer.Stop() } } catch {}
    try { if ($null -ne $script:NotesSaveTimer) { $script:NotesSaveTimer.Stop() } } catch {}
    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        try { $script:Process.Kill() } catch {}
    }
    if ($null -ne $script:QcDirtyNums -and $script:QcDirtyNums.Count -gt 0) {
        try { Save-QcFile } catch {}
    }
})

# ============================================================================
# Run
# ============================================================================

try {
    [System.Windows.Forms.Application]::Run($form)
} catch {
    $msg = "[" + [datetime]::Now + "] dev-window.ps1 fatal error:`r`n" + $_ + "`r`n" + $_.ScriptStackTrace
    $msg | Out-File -FilePath (Join-Path $PSScriptRoot "error.log") -Append
    [System.Windows.Forms.MessageBox]::Show(
        "Fatal error - see devtools/error.log",
        "PD Dev Window", [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error)
}                                                                                                                                                                                                                                                                                                                               