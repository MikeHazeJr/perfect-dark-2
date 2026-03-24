Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# --- Hide the PowerShell console window so only the GUI appears in the taskbar ---
Add-Type -Language CSharp @"
using System;
using System.Runtime.InteropServices;

public class ConsoleHelper
{
    [DllImport("kernel32.dll")]
    public static extern IntPtr GetConsoleWindow();

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public const int SW_HIDE = 0;
    public const int SW_SHOW = 5;

    public static void HideConsole()
    {
        IntPtr hwnd = GetConsoleWindow();
        if (hwnd != IntPtr.Zero)
        {
            ShowWindow(hwnd, SW_HIDE);
        }
    }
}
"@
[ConsoleHelper]::HideConsole()

# --- Inline C# helpers ---
# AsyncLineReader: reliable async stream reading without PS delegate issues.
# DarkMenuColorTable: dark theme for WinForms menu strip dropdowns.
Add-Type -Language CSharp -ReferencedAssemblies System.Windows.Forms, System.Drawing @"
using System;
using System.IO;
using System.Drawing;
using System.Threading;
using System.Collections.Concurrent;
using System.Windows.Forms;

public class AsyncLineReader
{
    public static void StartReading(StreamReader reader, ConcurrentQueue<string> queue, string prefix)
    {
        var thread = new Thread(() =>
        {
            try
            {
                string line;
                while ((line = reader.ReadLine()) != null)
                {
                    queue.Enqueue(prefix + line);
                }
            }
            catch {}
        });
        thread.IsBackground = true;
        thread.Start();
    }
}

public class DarkMenuColorTable : ProfessionalColorTable
{
    private Color bg        = Color.FromArgb(40, 40, 40);
    private Color border    = Color.FromArgb(70, 70, 70);
    private Color highlight = Color.FromArgb(60, 60, 60);
    private Color sep       = Color.FromArgb(70, 70, 70);

    public override Color ToolStripDropDownBackground { get { return bg; } }
    public override Color MenuBorder                  { get { return border; } }
    public override Color MenuItemBorder              { get { return highlight; } }
    public override Color MenuItemSelected            { get { return highlight; } }
    public override Color MenuItemSelectedGradientBegin { get { return highlight; } }
    public override Color MenuItemSelectedGradientEnd   { get { return highlight; } }
    public override Color MenuItemPressedGradientBegin  { get { return bg; } }
    public override Color MenuItemPressedGradientEnd    { get { return bg; } }
    public override Color MenuStripGradientBegin        { get { return bg; } }
    public override Color MenuStripGradientEnd          { get { return bg; } }
    public override Color ImageMarginGradientBegin      { get { return bg; } }
    public override Color ImageMarginGradientMiddle     { get { return bg; } }
    public override Color ImageMarginGradientEnd        { get { return bg; } }
    public override Color SeparatorDark                 { get { return sep; } }
    public override Color SeparatorLight                { get { return bg; } }
    public override Color CheckBackground               { get { return highlight; } }
    public override Color CheckSelectedBackground       { get { return highlight; } }
    public override Color CheckPressedBackground        { get { return highlight; } }
}
"@

# ============================================================================
# Configuration
# ============================================================================

$script:ProjectDir      = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:ClientBuildDir  = Join-Path $script:ProjectDir "build\client"
$script:ServerBuildDir  = Join-Path $script:ProjectDir "build\server"
$script:BuildDir        = $script:ClientBuildDir
$script:AddinDir        = Join-Path $script:ProjectDir "..\post-batch-addin"
$script:CMake      = "cmake"
$script:Make       = "C:\msys64\usr\bin\make.exe"
$script:CC         = "C:/msys64/mingw64/bin/cc.exe"
$script:ChangesFile = Join-Path $script:ProjectDir "CHANGES.md"
$script:SettingsFile = Join-Path $script:ProjectDir ".build-settings.json"

# MSYS2 MINGW64 environment
$env:MSYSTEM       = "MINGW64"
$env:MINGW_PREFIX  = "/mingw64"
$env:PATH          = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"

$script:ErrorLines = [System.Collections.ArrayList]::new()
$script:AllOutput  = [System.Collections.ArrayList]::new()
$script:ClientErrorLines = [System.Collections.ArrayList]::new()
$script:ServerErrorLines = [System.Collections.ArrayList]::new()
$script:ClientBuildFailed = $false
$script:ServerBuildFailed = $false
$script:ClientBuildTime = 0
$script:ServerBuildTime = 0
$script:TargetStartTime = [DateTime]::Now
$script:IsRunning  = $false
$script:ExeName    = "PerfectDark.exe"
$script:BuildSucceeded = $false
$script:BuildTarget    = ""
$script:GameProcess    = $null
$script:GameRunning    = $false
$script:OutputQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()

# ============================================================================
# Custom font: Handel Gothic (PD's authentic menu font)
# ============================================================================

Add-Type -AssemblyName System.Drawing

$script:FontCollection = New-Object System.Drawing.Text.PrivateFontCollection
$fontPath = Join-Path $script:ProjectDir "fonts\Menus\Handel Gothic Regular\Handel Gothic Regular.otf"
$script:UseHandelGothic = $false
if (Test-Path $fontPath) {
    try {
        $script:FontCollection.AddFontFile($fontPath)
        $script:HandelFamily = $script:FontCollection.Families[0]
        $script:UseHandelGothic = $true
    } catch {
        # Fall back to Segoe UI if font loading fails
    }
}

# Helper: create a font using Handel Gothic if available, otherwise Segoe UI
function New-UIFont($size, [switch]$Bold) {
    $style = if ($Bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    if ($script:UseHandelGothic) {
        return New-Object System.Drawing.Font($script:HandelFamily, $size, $style, [System.Drawing.GraphicsUnit]::Point)
    }
    return New-Object System.Drawing.Font("Segoe UI", $size, $style)
}

# ============================================================================
# Settings persistence
# ============================================================================

$script:SoundsDir = Join-Path $script:ProjectDir "dist\build-sounds"

$script:Settings = @{
    GithubRepo = ""
    SoundsEnabled = $true
    RomPath = ""
}

function Load-Settings {
    if (Test-Path $script:SettingsFile) {
        try {
            $json = Get-Content $script:SettingsFile -Raw | ConvertFrom-Json
            if ($json.GithubRepo) { $script:Settings.GithubRepo = $json.GithubRepo }
            if ($null -ne $json.SoundsEnabled) { $script:Settings.SoundsEnabled = $json.SoundsEnabled }
            if ($json.RomPath) { $script:Settings.RomPath = $json.RomPath }
        } catch {}
    }

    # Auto-detect repo from git remote if not set
    if ($script:Settings.GithubRepo -eq "") {
        try {
            $remote = (git -C $script:ProjectDir remote get-url origin 2>$null).Trim()
            if ($remote -match 'github\.com[:/](.+?)(?:\.git)?$') {
                $script:Settings.GithubRepo = $Matches[1]
            }
        } catch {}
    }

    # Auto-detect ROM path if not set or if saved path no longer exists
    if ([string]::IsNullOrEmpty($script:Settings.RomPath) -or -not (Test-Path $script:Settings.RomPath)) {
        $defaultRom = Join-Path $script:ProjectDir "data\pd.ntsc-final.z64"
        if (Test-Path $defaultRom) {
            $script:Settings.RomPath = $defaultRom
        } else {
            # Try any .z64 in the data folder
            $dataDir = Join-Path $script:ProjectDir "data"
            if (Test-Path $dataDir) {
                $found = Get-ChildItem -Path $dataDir -Filter "*.z64" -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($found) { $script:Settings.RomPath = $found.FullName }
            }
        }
    }
}

# Helper: resolve ROM path — returns path or $null, offers browse dialog if not found
function Resolve-RomPath {
    if ($script:Settings.RomPath -ne "" -and (Test-Path $script:Settings.RomPath)) {
        return $script:Settings.RomPath
    }

    # ROM not found — open file browser
    $ofd = New-Object System.Windows.Forms.OpenFileDialog
    $ofd.Title = "Locate Perfect Dark ROM (z64)"
    $ofd.Filter = "N64 ROM files (*.z64)|*.z64|All files (*.*)|*.*"
    $ofd.InitialDirectory = Join-Path $script:ProjectDir "data"
    if (-not (Test-Path $ofd.InitialDirectory)) {
        $ofd.InitialDirectory = $script:ProjectDir
    }

    if ($ofd.ShowDialog() -eq "OK") {
        $script:Settings.RomPath = $ofd.FileName
        Save-Settings
        return $ofd.FileName
    }
    return $null
}

function Save-Settings {
    $script:Settings | ConvertTo-Json | Set-Content $script:SettingsFile
}

Load-Settings

# ============================================================================
# Game Sound System
# ============================================================================

# Sound categories — each maps to a folder in dist/build-sounds/ containing .wav files.
# Play-GameSound picks a random file from the category folder.

$script:SoundPlayers = @{}  # Cache of loaded SoundPlayer objects

function Get-SoundFiles($category) {
    $catDir = Join-Path $script:SoundsDir $category
    if (Test-Path $catDir) {
        return @(Get-ChildItem -Path $catDir -Filter "*.wav" | Select-Object -ExpandProperty FullName)
    }
    return @()
}

function Play-GameSound($category) {
    if (-not $script:Settings.SoundsEnabled) { return }

    try {
        $files = Get-SoundFiles $category
        if ($files.Count -eq 0) { return }

        # Pick a random file from the category
        $file = $files | Get-Random

        # Use SoundPlayer for async playback (non-blocking)
        if ($script:SoundPlayers.ContainsKey($file)) {
            $player = $script:SoundPlayers[$file]
        } else {
            $player = New-Object System.Media.SoundPlayer($file)
            $player.Load()
            $script:SoundPlayers[$file] = $player
        }
        $player.Play()
    } catch {
        # Silently ignore sound errors — they should never disrupt the build
    }
}

# Sound trigger functions (named for clarity at call sites)
function Sound-MenuClick   { Play-GameSound "menu_click" }
function Sound-MenuTick    { Play-GameSound "menu_tick" }
function Sound-ItemPickup  { Play-GameSound "item_pickup" }
function Sound-BuildSuccess { Play-GameSound "enemy_argh" }
function Sound-BuildFail   {
    # Fall back to system sound if no game sounds extracted yet
    $files = Get-SoundFiles "enemy_argh"
    if ($files.Count -gt 0) {
        Play-GameSound "enemy_argh"
    } else {
        try { [System.Media.SystemSounds]::Hand.Play() } catch {}
    }
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
# Main Form
# ============================================================================

$form = New-Object System.Windows.Forms.Form
$form.Text = "Perfect Dark - Build Tool"
$form.Size = New-Object System.Drawing.Size(880, 560)
$form.StartPosition = "CenterScreen"
$form.BackColor = $script:ColorBg
$form.ForeColor = $script:ColorWhite
$form.Font = New-UIFont 10
$form.FormBorderStyle = "FixedSingle"
$form.MaximizeBox = $false
$form.ShowInTaskbar = $true
$form.TopLevel = $true

# Set custom icon so it shows as its own app in the taskbar (not PowerShell)
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
$menuStrip.Renderer = New-Object System.Windows.Forms.ToolStripProfessionalRenderer(
    (New-Object DarkMenuColorTable)
)

# --- File menu ---
$menuFile = New-Object System.Windows.Forms.ToolStripMenuItem("File")
$menuFile.ForeColor = $script:ColorWhite
$menuFile.BackColor = $script:ColorPanelBg

$menuEditChanges = New-Object System.Windows.Forms.ToolStripMenuItem("Edit CHANGES.md")
$menuEditChanges.ForeColor = $script:ColorText
$menuEditChanges.BackColor = $script:ColorPanelBg
$menuEditChanges.Add_Click({
    if (!(Test-Path $script:ChangesFile)) {
        $template = @("# Changelog - Perfect Dark 2", "",
            "## Improvements", "", "## Additions", "", "## Updates", "",
            "## Known Issues", "", "## Missing Content", "")
        Set-Content -Path $script:ChangesFile -Value ($template -join "`n")
    }
    Start-Process "notepad.exe" -ArgumentList $script:ChangesFile
})
[void]$menuFile.DropDownItems.Add($menuEditChanges)

$menuSep = New-Object System.Windows.Forms.ToolStripSeparator
[void]$menuFile.DropDownItems.Add($menuSep)

$menuExit = New-Object System.Windows.Forms.ToolStripMenuItem("Exit")
$menuExit.ForeColor = $script:ColorText
$menuExit.BackColor = $script:ColorPanelBg
$menuExit.Add_Click({ $form.Close() })
[void]$menuFile.DropDownItems.Add($menuExit)

[void]$menuStrip.Items.Add($menuFile)

# --- Edit menu ---
$menuEdit = New-Object System.Windows.Forms.ToolStripMenuItem("Edit")
$menuEdit.ForeColor = $script:ColorWhite
$menuEdit.BackColor = $script:ColorPanelBg

$menuSettings = New-Object System.Windows.Forms.ToolStripMenuItem("Settings...")
$menuSettings.ForeColor = $script:ColorText
$menuSettings.BackColor = $script:ColorPanelBg
$menuSettings.Add_Click({ Show-SettingsDialog })
[void]$menuEdit.DropDownItems.Add($menuSettings)

[void]$menuStrip.Items.Add($menuEdit)

$form.MainMenuStrip = $menuStrip
$form.Controls.Add($menuStrip)

# ============================================================================
# Title + Version + Status
# ============================================================================

$title = New-Object System.Windows.Forms.Label
$title.Text = "Perfect Dark PC Port"
$title.Font = New-UIFont 16 -Bold
$title.ForeColor = $script:ColorGold
$title.Location = New-Object System.Drawing.Point(16, 30)
$title.AutoSize = $true
$form.Controls.Add($title)

$lblVersionDisplay = New-Object System.Windows.Forms.Label
$lblVersionDisplay.Text = ""
$lblVersionDisplay.Font = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
$lblVersionDisplay.ForeColor = $script:ColorGold
$lblVersionDisplay.Location = New-Object System.Drawing.Point(240, 34)
$lblVersionDisplay.AutoSize = $true
$form.Controls.Add($lblVersionDisplay)

# Version fields for editing (used in push bar)
$script:VerMajor = 0
$script:VerMinor = 0
$script:VerRevision = 0

$statusLabel = New-Object System.Windows.Forms.Label
$statusLabel.Text = "Ready"
$statusLabel.Font = New-Object System.Drawing.Font("Consolas", 10)
$statusLabel.ForeColor = $script:ColorGreen
$statusLabel.Location = New-Object System.Drawing.Point(500, 34)
$statusLabel.Size = New-Object System.Drawing.Size(360, 20)
$statusLabel.TextAlign = "MiddleRight"
$form.Controls.Add($statusLabel)

# ============================================================================
# Layout: Left sidebar (220px) + Right console area
# ============================================================================

$sideW = 220
$consoleX = $sideW + 16
$consoleW = 850 - $sideW - 6

# --- Left Sidebar Panel ---
$sidePanel = New-Object System.Windows.Forms.Panel
$sidePanel.Location = New-Object System.Drawing.Point(10, 60)
$sidePanel.Size = New-Object System.Drawing.Size($sideW, 464)
$sidePanel.BackColor = $script:ColorPanelBg
$form.Controls.Add($sidePanel)

function New-SideButton($text, $y, $w, $h, $color, $parent) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $text
    $btn.Location = New-Object System.Drawing.Point(8, $y)
    $btn.Size = New-Object System.Drawing.Size($w, $h)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $color
    $btn.FlatAppearance.BorderSize = 1
    $btn.ForeColor = $color
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor = "Hand"
    $btn.Font = New-UIFont 10 -Bold
    $parent.Controls.Add($btn)
    return $btn
}

# --- Version Section ---
$lblVerTitle = New-Object System.Windows.Forms.Label
$lblVerTitle.Text = "VERSION"
$lblVerTitle.Font = New-UIFont 9 -Bold
$lblVerTitle.ForeColor = $script:ColorDim
$lblVerTitle.Location = New-Object System.Drawing.Point(8, 8)
$lblVerTitle.AutoSize = $true
$sidePanel.Controls.Add($lblVerTitle)

# Version fields: Major . Minor . Revision (compact row)
function New-VerField($x, $y, $parent) {
    $txt = New-Object System.Windows.Forms.TextBox
    $txt.Location = New-Object System.Drawing.Point($x, $y)
    $txt.Size = New-Object System.Drawing.Size(42, 22)
    $txt.BackColor = $script:ColorFieldBg
    $txt.ForeColor = $script:ColorGold
    $txt.Font = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
    $txt.TextAlign = "Center"
    $txt.BorderStyle = "FixedSingle"
    $txt.MaxLength = 4
    $parent.Controls.Add($txt)
    return $txt
}

function New-SmallButton($text, $x, $y, $parent) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $text
    $btn.Location = New-Object System.Drawing.Point($x, $y)
    $btn.Size = New-Object System.Drawing.Size(20, 16)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $script:ColorDim
    $btn.FlatAppearance.BorderSize = 1
    $btn.ForeColor = $script:ColorGold
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor = "Hand"
    $btn.Font = New-Object System.Drawing.Font("Consolas", 8, [System.Drawing.FontStyle]::Bold)
    $btn.Padding = New-Object System.Windows.Forms.Padding(0)
    $parent.Controls.Add($btn)
    return $btn
}

# Version fields row: three textboxes with dots between them
# Layout:  [ 0 ] . [ 0 ] . [ 0 ]
#          [-][+]   [-][+]   [-][+]
$verFieldW = 48
$verCol1 = 8
$verCol2 = 72
$verCol3 = 136
$verFieldY = 26
$verBtnY = 50
$verBtnW = 22
$verBtnH = 16

# Major field
$txtVerMajor = New-VerField $verCol1 $verFieldY $sidePanel
$txtVerMajor.Size = New-Object System.Drawing.Size($verFieldW, 22)

# Dot 1
$lblDot1 = New-Object System.Windows.Forms.Label
$lblDot1.Text = "."; $lblDot1.Font = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.FontStyle]::Bold)
$lblDot1.ForeColor = $script:ColorDim; $lblDot1.Location = New-Object System.Drawing.Point(58, 28); $lblDot1.AutoSize = $true
$sidePanel.Controls.Add($lblDot1)

# Minor field
$txtVerMinor = New-VerField $verCol2 $verFieldY $sidePanel
$txtVerMinor.Size = New-Object System.Drawing.Size($verFieldW, 22)

# Dot 2
$lblDot2 = New-Object System.Windows.Forms.Label
$lblDot2.Text = "."; $lblDot2.Font = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.FontStyle]::Bold)
$lblDot2.ForeColor = $script:ColorDim; $lblDot2.Location = New-Object System.Drawing.Point(122, 28); $lblDot2.AutoSize = $true
$sidePanel.Controls.Add($lblDot2)

# Revision field
$txtVerRevision = New-VerField $verCol3 $verFieldY $sidePanel
$txtVerRevision.Size = New-Object System.Drawing.Size($verFieldW, 22)

# Inc/Dec buttons below each field: [-][+] pairs centered under each textbox
# Major: [-] at col1, [+] at col1+26
$btnDecMajor = New-SmallButton "-" $verCol1 $verBtnY $sidePanel
$btnDecMajor.Size = New-Object System.Drawing.Size($verBtnW, $verBtnH)
$btnIncMajor = New-SmallButton "+" ($verCol1 + $verBtnW + 4) $verBtnY $sidePanel
$btnIncMajor.Size = New-Object System.Drawing.Size($verBtnW, $verBtnH)

# Minor: [-] at col2, [+] at col2+26
$btnDecMinor = New-SmallButton "-" $verCol2 $verBtnY $sidePanel
$btnDecMinor.Size = New-Object System.Drawing.Size($verBtnW, $verBtnH)
$btnIncMinor = New-SmallButton "+" ($verCol2 + $verBtnW + 4) $verBtnY $sidePanel
$btnIncMinor.Size = New-Object System.Drawing.Size($verBtnW, $verBtnH)

# Revision: [-] at col3, [+] at col3+26
$btnDecRevision = New-SmallButton "-" $verCol3 $verBtnY $sidePanel
$btnDecRevision.Size = New-Object System.Drawing.Size($verBtnW, $verBtnH)
$btnIncRevision = New-SmallButton "+" ($verCol3 + $verBtnW + 4) $verBtnY $sidePanel
$btnIncRevision.Size = New-Object System.Drawing.Size($verBtnW, $verBtnH)

# Latest released version labels (dev / stable from GitHub)
$lblLatestDev = New-Object System.Windows.Forms.Label
$lblLatestDev.Text = "dev: ---"
$lblLatestDev.Font = New-UIFont 8
$lblLatestDev.ForeColor = $script:ColorOrange
$lblLatestDev.Location = New-Object System.Drawing.Point(8, 70)
$lblLatestDev.AutoSize = $true
$sidePanel.Controls.Add($lblLatestDev)

$lblLatestStable = New-Object System.Windows.Forms.Label
$lblLatestStable.Text = "stable: ---"
$lblLatestStable.Font = New-UIFont 8
$lblLatestStable.ForeColor = $script:ColorGreen
$lblLatestStable.Location = New-Object System.Drawing.Point(108, 70)
$lblLatestStable.AutoSize = $true
$sidePanel.Controls.Add($lblLatestStable)

# Version warning label
$lblVerWarning = New-Object System.Windows.Forms.Label
$lblVerWarning.Text = ""
$lblVerWarning.Font = New-UIFont 8 -Bold
$lblVerWarning.ForeColor = $script:ColorRed
$lblVerWarning.Location = New-Object System.Drawing.Point(8, 84)
$lblVerWarning.Size = New-Object System.Drawing.Size(204, 16)
$sidePanel.Controls.Add($lblVerWarning)

# --- Separator ---
$sideSep1 = New-Object System.Windows.Forms.Label
$sideSep1.Text = ""; $sideSep1.Location = New-Object System.Drawing.Point(8, 104)
$sideSep1.Size = New-Object System.Drawing.Size(204, 1); $sideSep1.BackColor = $script:ColorDimmer
$sidePanel.Controls.Add($sideSep1)

# --- Build Section ---
$lblBuildSection = New-Object System.Windows.Forms.Label
$lblBuildSection.Text = "BUILD"
$lblBuildSection.Font = New-UIFont 9 -Bold
$lblBuildSection.ForeColor = $script:ColorDim
$lblBuildSection.Location = New-Object System.Drawing.Point(8, 110)
$lblBuildSection.AutoSize = $true
$sidePanel.Controls.Add($lblBuildSection)

$cmbBuildTarget = New-Object System.Windows.Forms.ComboBox
$cmbBuildTarget.Location = New-Object System.Drawing.Point(8, 128)
$cmbBuildTarget.Size = New-Object System.Drawing.Size(204, 24)
$cmbBuildTarget.DropDownStyle = "DropDownList"
$cmbBuildTarget.BackColor = $script:ColorFieldBg
$cmbBuildTarget.ForeColor = $script:ColorWhite
$cmbBuildTarget.FlatStyle = "Flat"
$cmbBuildTarget.Font = New-Object System.Drawing.Font("Consolas", 10)
[void]$cmbBuildTarget.Items.Add("Client")
[void]$cmbBuildTarget.Items.Add("Server")
[void]$cmbBuildTarget.Items.Add("Client + Server")
$cmbBuildTarget.SelectedIndex = 0
$cmbBuildTarget.Add_SelectedIndexChanged({ Sound-MenuTick })
$sidePanel.Controls.Add($cmbBuildTarget)

# Build + Clean Build side by side
$btnBuild = New-SideButton "Build" 156 98 28 $script:ColorGreen $sidePanel
$btnCleanBuild = New-Object System.Windows.Forms.Button
$btnCleanBuild.Text = "Clean Build"
$btnCleanBuild.Location = New-Object System.Drawing.Point(114, 156)
$btnCleanBuild.Size = New-Object System.Drawing.Size(98, 28)
$btnCleanBuild.FlatStyle = "Flat"
$btnCleanBuild.FlatAppearance.BorderColor = $script:ColorGreen
$btnCleanBuild.FlatAppearance.BorderSize = 1
$btnCleanBuild.ForeColor = $script:ColorGreen
$btnCleanBuild.BackColor = $script:ColorFieldBg
$btnCleanBuild.Cursor = "Hand"
$btnCleanBuild.Font = New-UIFont 10 -Bold
$sidePanel.Controls.Add($btnCleanBuild)

# Hidden clean checkbox (toggled by Clean Build button)
$chkClean = New-Object System.Windows.Forms.CheckBox
$chkClean.Visible = $false
$sidePanel.Controls.Add($chkClean)

# Stop Building button (full width, active only during builds)
$btnStop = New-Object System.Windows.Forms.Button
$btnStop.Text = "Stop Building"
$btnStop.Location = New-Object System.Drawing.Point(8, 188)
$btnStop.Size = New-Object System.Drawing.Size(204, 26)
$btnStop.FlatStyle = "Flat"
$btnStop.FlatAppearance.BorderColor = $script:ColorRed
$btnStop.FlatAppearance.BorderSize = 1
$btnStop.ForeColor = $script:ColorDisabled
$btnStop.BackColor = $script:ColorFieldBg
$btnStop.Font = New-UIFont 10 -Bold
$btnStop.Enabled = $false
$sidePanel.Controls.Add($btnStop)

# --- Separator ---
$sideSep2 = New-Object System.Windows.Forms.Label
$sideSep2.Text = ""; $sideSep2.Location = New-Object System.Drawing.Point(8, 220)
$sideSep2.Size = New-Object System.Drawing.Size(204, 1); $sideSep2.BackColor = $script:ColorDimmer
$sidePanel.Controls.Add($sideSep2)

# --- Run Section ---
$lblRunSection = New-Object System.Windows.Forms.Label
$lblRunSection.Text = "RUN"
$lblRunSection.Font = New-UIFont 9 -Bold
$lblRunSection.ForeColor = $script:ColorDim
$lblRunSection.Location = New-Object System.Drawing.Point(8, 226)
$lblRunSection.AutoSize = $true
$sidePanel.Controls.Add($lblRunSection)

$btnRunClient = New-SideButton "Client" 244 98 28 $script:ColorGreen $sidePanel
$btnRunServer = New-Object System.Windows.Forms.Button
$btnRunServer.Text = "Server"
$btnRunServer.Location = New-Object System.Drawing.Point(114, 244)
$btnRunServer.Size = New-Object System.Drawing.Size(98, 28)
$btnRunServer.FlatStyle = "Flat"
$btnRunServer.FlatAppearance.BorderColor = $script:ColorOrange
$btnRunServer.FlatAppearance.BorderSize = 1
$btnRunServer.ForeColor = $script:ColorOrange
$btnRunServer.BackColor = $script:ColorFieldBg
$btnRunServer.Cursor = "Hand"
$btnRunServer.Font = New-UIFont 10 -Bold
$sidePanel.Controls.Add($btnRunServer)

# --- Separator ---
$sideSep3 = New-Object System.Windows.Forms.Label
$sideSep3.Text = ""; $sideSep3.Location = New-Object System.Drawing.Point(8, 278)
$sideSep3.Size = New-Object System.Drawing.Size(204, 1); $sideSep3.BackColor = $script:ColorDimmer
$sidePanel.Controls.Add($sideSep3)

# --- Push Section ---
$lblPushSection = New-Object System.Windows.Forms.Label
$lblPushSection.Text = "PUSH"
$lblPushSection.Font = New-UIFont 9 -Bold
$lblPushSection.ForeColor = $script:ColorDim
$lblPushSection.Location = New-Object System.Drawing.Point(8, 284)
$lblPushSection.AutoSize = $true
$sidePanel.Controls.Add($lblPushSection)

$btnPushDev = New-Object System.Windows.Forms.Button
$btnPushDev.Text = "Dev"
$btnPushDev.Location = New-Object System.Drawing.Point(8, 302)
$btnPushDev.Size = New-Object System.Drawing.Size(98, 28)
$btnPushDev.FlatStyle = "Flat"
$btnPushDev.FlatAppearance.BorderColor = $script:ColorOrange
$btnPushDev.FlatAppearance.BorderSize = 1
$btnPushDev.ForeColor = $script:ColorOrange
$btnPushDev.BackColor = $script:ColorFieldBg
$btnPushDev.Cursor = "Hand"
$btnPushDev.Font = New-UIFont 10 -Bold
$sidePanel.Controls.Add($btnPushDev)

$btnPushStable = New-Object System.Windows.Forms.Button
$btnPushStable.Text = "Stable"
$btnPushStable.Location = New-Object System.Drawing.Point(114, 302)
$btnPushStable.Size = New-Object System.Drawing.Size(98, 28)
$btnPushStable.FlatStyle = "Flat"
$btnPushStable.FlatAppearance.BorderColor = $script:ColorGreen
$btnPushStable.FlatAppearance.BorderSize = 1
$btnPushStable.ForeColor = $script:ColorGreen
$btnPushStable.BackColor = $script:ColorFieldBg
$btnPushStable.Cursor = "Hand"
$btnPushStable.Font = New-UIFont 10 -Bold
$sidePanel.Controls.Add($btnPushStable)

# Hidden stable checkbox (used internally by push functions)
$chkStable = New-Object System.Windows.Forms.CheckBox
$chkStable.Visible = $false
$sidePanel.Controls.Add($chkStable)

# --- Separator ---
$sideSep4 = New-Object System.Windows.Forms.Label
$sideSep4.Text = ""; $sideSep4.Location = New-Object System.Drawing.Point(8, 336)
$sideSep4.Size = New-Object System.Drawing.Size(204, 1); $sideSep4.BackColor = $script:ColorDimmer
$sidePanel.Controls.Add($sideSep4)

# --- Git Section ---
$lblGitSection = New-Object System.Windows.Forms.Label
$lblGitSection.Text = "GIT"
$lblGitSection.Font = New-UIFont 9 -Bold
$lblGitSection.ForeColor = $script:ColorDim
$lblGitSection.Location = New-Object System.Drawing.Point(8, 342)
$lblGitSection.AutoSize = $true
$sidePanel.Controls.Add($lblGitSection)

$script:GitChangeCount = 0

$btnCommit = New-SideButton "Commit 0 changes" 358 204 28 $script:ColorPurple $sidePanel
$btnCommit.Enabled = $false
$btnCommit.ForeColor = $script:ColorDisabled

# --- Separator ---
$sideSep5 = New-Object System.Windows.Forms.Label
$sideSep5.Text = ""; $sideSep5.Location = New-Object System.Drawing.Point(8, 392)
$sideSep5.Size = New-Object System.Drawing.Size(204, 1); $sideSep5.BackColor = $script:ColorDimmer
$sidePanel.Controls.Add($sideSep5)

# Open GitHub button
$btnGithub = New-SideButton "Open GitHub" 398 204 28 $script:ColorBlue $sidePanel

# Open Project button (opens project folder in Explorer)
$btnOpenProject = New-SideButton "Open Project" 430 204 28 $script:ColorDim $sidePanel

# ============================================================================
# Right side: Console output + utility bar + progress
# ============================================================================

$outputBox = New-Object System.Windows.Forms.RichTextBox
$outputBox.Location = New-Object System.Drawing.Point($consoleX, 60)
$outputBox.Size = New-Object System.Drawing.Size($consoleW, 394)
$outputBox.BackColor = $script:ColorConsoleBg
$outputBox.ForeColor = $script:ColorText
$outputBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$outputBox.ReadOnly = $true
$outputBox.WordWrap = $false
$outputBox.ScrollBars = "Both"
$outputBox.BorderStyle = "None"
$form.Controls.Add($outputBox)

# Utility bar (below console)
$utilPanel = New-Object System.Windows.Forms.Panel
$utilPanel.Location = New-Object System.Drawing.Point($consoleX, 458)
$utilPanel.Size = New-Object System.Drawing.Size($consoleW, 28)
$utilPanel.BackColor = $script:ColorPanelBg
$form.Controls.Add($utilPanel)

function New-UtilButton($text, $x, $w, $color) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $text
    $btn.Location = New-Object System.Drawing.Point($x, 2)
    $btn.Size = New-Object System.Drawing.Size($w, 24)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $color
    $btn.FlatAppearance.BorderSize = 1
    $btn.ForeColor = $color
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor = "Hand"
    $btn.Font = New-UIFont 9 -Bold
    $utilPanel.Controls.Add($btn)
    return $btn
}

$btnCopyErrors = New-UtilButton "Copy Errors"  4 90 $script:ColorRed
$btnCopyAll    = New-UtilButton "Copy All"    98 70 $script:ColorDim
$btnClear      = New-UtilButton "Clear"      172 55 ([System.Drawing.Color]::FromArgb(120,120,120))

$errorCountLabel = New-Object System.Windows.Forms.Label
$errorCountLabel.Text = ""
$errorCountLabel.Font = New-Object System.Drawing.Font("Consolas", 8, [System.Drawing.FontStyle]::Bold)
$errorCountLabel.ForeColor = $script:ColorRed
$errorCountLabel.Location = New-Object System.Drawing.Point(234, 6)
$errorCountLabel.Size = New-Object System.Drawing.Size(($consoleW - 240), 16)
$errorCountLabel.TextAlign = "MiddleRight"
$utilPanel.Controls.Add($errorCountLabel)

# Progress bar
$progressPanel = New-Object System.Windows.Forms.Panel
$progressPanel.Location = New-Object System.Drawing.Point($consoleX, 490)
$progressPanel.Size = New-Object System.Drawing.Size($consoleW, 22)
$progressPanel.BackColor = $script:ColorPanelBg
$form.Controls.Add($progressPanel)

$progressFill = New-Object System.Windows.Forms.Panel
$progressFill.Location = New-Object System.Drawing.Point(0, 0)
$progressFill.Size = New-Object System.Drawing.Size(0, 22)
$progressFill.BackColor = $script:ColorBlue
$progressPanel.Controls.Add($progressFill)

$progressLabel = New-Object System.Windows.Forms.Label
$progressLabel.Text = ""
$progressLabel.Font = New-Object System.Drawing.Font("Consolas", 9, [System.Drawing.FontStyle]::Bold)
$progressLabel.ForeColor = $script:ColorWhite
$progressLabel.BackColor = [System.Drawing.Color]::Transparent
$progressLabel.Location = New-Object System.Drawing.Point(4, 3)
$progressLabel.AutoSize = $true
$progressPanel.Controls.Add($progressLabel)
$progressLabel.BringToFront()

$script:BuildPercent = 0
$script:HasErrors = $false

# --- Build tool version label (lower-left, subtle) ---
$script:BuildToolVersion = "3.2"
$lblBuildToolVer = New-Object System.Windows.Forms.Label
$lblBuildToolVer.Text = "Build tool version $($script:BuildToolVersion)"
$lblBuildToolVer.Font = New-UIFont 8
$lblBuildToolVer.ForeColor = [System.Drawing.Color]::FromArgb(70, 70, 70)
$lblBuildToolVer.Location = New-Object System.Drawing.Point(12, 498)
$lblBuildToolVer.AutoSize = $true
$form.Controls.Add($lblBuildToolVer)

# ============================================================================
# Timer + Spinner
# ============================================================================

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 100

$script:SpinnerChars = @('|', '/', '-', '\')
$script:SpinnerIndex = 0
$script:LastOutputTime = [DateTime]::Now
$script:StepStartTime = [DateTime]::Now

# ============================================================================
# Output helpers
# ============================================================================

function Write-Output-Line($text, $color) {
    $outputBox.SelectionStart = $outputBox.TextLength
    $outputBox.SelectionColor = $color
    $outputBox.AppendText("$text`r`n")
    $outputBox.ScrollToCaret()
    [void]$script:AllOutput.Add($text)
}

function Write-Header($text) {
    Write-Output-Line "" $script:ColorDimmer
    Write-Output-Line ("=" * 70) $script:ColorDimmer
    Write-Output-Line "  $text" $script:ColorGold
    Write-Output-Line ("=" * 70) $script:ColorDimmer
}

function Play-BuildSound($success) {
    if ($success) {
        Sound-BuildSuccess
    } else {
        Sound-BuildFail
    }
}

function Set-Buttons-Enabled($enabled) {
    $script:IsRunning = !$enabled
    $btnBuild.Enabled = $enabled
    $btnCleanBuild.Enabled = $enabled
    $btnPushDev.Enabled = $enabled
    $btnPushStable.Enabled = $enabled
    $cmbBuildTarget.Enabled = $enabled
    # Commit button: only enabled if idle AND there are changes
    $btnCommit.Enabled = $enabled -and ($script:GitChangeCount -gt 0)
    $btnCommit.ForeColor = if ($enabled -and ($script:GitChangeCount -gt 0)) { $script:ColorPurple } else { $script:ColorDisabled }
    # Stop button is inverse — active when running, disabled when idle
    $btnStop.Enabled = (-not $enabled)
    $btnStop.ForeColor = if (-not $enabled) { $script:ColorRed } else { $script:ColorDisabled }
    Update-RunButtons
}

function Update-RunButtons {
    $clientExe = Join-Path $script:ClientBuildDir $script:ExeName
    $serverExe = Join-Path $script:ServerBuildDir "PerfectDarkServer.exe"

    $canRunClient = (-not $script:IsRunning) -and (Test-Path $clientExe)
    $canRunServer = (-not $script:IsRunning) -and (Test-Path $serverExe)

    $btnRunClient.Enabled = $canRunClient
    $btnRunServer.Enabled = $canRunServer

    $btnRunClient.ForeColor = if ($canRunClient) { $script:ColorGreen } else { $script:ColorDisabled }
    $btnRunServer.ForeColor = if ($canRunServer) { $script:ColorOrange } else { $script:ColorDisabled }
    $btnBuild.ForeColor = if (-not $script:IsRunning) { $script:ColorGreen } else { $script:ColorDisabled }
    $btnCleanBuild.ForeColor = if (-not $script:IsRunning) { $script:ColorGreen } else { $script:ColorDisabled }
    $btnPushDev.ForeColor = if (-not $script:IsRunning) { $script:ColorOrange } else { $script:ColorDisabled }
    $btnPushStable.ForeColor = if (-not $script:IsRunning) { $script:ColorGreen } else { $script:ColorDisabled }
}

function Classify-Line($line) {
    if ($line -match ':\s*error\s*:|:\s*fatal error\s*:|^make.*\*\*\*.*Error|FAILED|undefined reference|multiple definition|collect2:\s*error|ld returned|cannot find -l|CMake Error|error:\s|Error:') {
        return "error"
    }
    if ($line -match ':\s*warning\s*:|Warning:') {
        return "warning"
    }
    if ($line -match ':\s*note\s*:') {
        return "note"
    }
    return "normal"
}

function Get-Line-Color($classification) {
    switch ($classification) {
        "error"   { return $script:ColorRed }
        "warning" { return [System.Drawing.Color]::FromArgb(230, 200, 80) }
        "note"    { return [System.Drawing.Color]::FromArgb(130, 170, 210) }
        default   { return [System.Drawing.Color]::FromArgb(190, 190, 190) }
    }
}

# ============================================================================
# Version management
# ============================================================================

function Get-ProjectVersion {
    $cmakePath = Join-Path $script:ProjectDir "CMakeLists.txt"
    if (!(Test-Path $cmakePath)) { return @{ String = "?"; Major = 0; Minor = 0; Revision = 0 } }

    $content = Get-Content $cmakePath -Raw
    $major = 0; $minor = 0; $revision = 0

    if ($content -match 'VERSION_SEM_MAJOR\s+(\d+)') { $major = [int]$Matches[1] }
    if ($content -match 'VERSION_SEM_MINOR\s+(\d+)') { $minor = [int]$Matches[1] }
    if ($content -match 'VERSION_SEM_PATCH\s+(\d+)') { $revision = [int]$Matches[1] }

    return @{
        Major = $major; Minor = $minor; Revision = $revision
        String = "v$major.$minor.$revision"
    }
}

function Set-ProjectVersion($major, $minor, $revision) {
    $cmakePath = Join-Path $script:ProjectDir "CMakeLists.txt"
    if (!(Test-Path $cmakePath)) { return }

    $content = Get-Content $cmakePath -Raw
    $content = $content -replace '(VERSION_SEM_MAJOR\s+)\d+', "`${1}$major"
    $content = $content -replace '(VERSION_SEM_MINOR\s+)\d+', "`${1}$minor"
    $content = $content -replace '(VERSION_SEM_PATCH\s+)\d+', "`${1}$revision"
    Set-Content -Path $cmakePath -Value $content -NoNewline
}

function Refresh-VersionDisplay {
    $ver = Get-ProjectVersion
    $lblVersionDisplay.Text = $ver.String
    $lblVersionDisplay.ForeColor = $script:ColorGold

    # Sync the push bar fields
    $script:VerMajor = $ver.Major
    $script:VerMinor = $ver.Minor
    $script:VerRevision = $ver.Revision
    $txtVerMajor.Text = "$($ver.Major)"
    $txtVerMinor.Text = "$($ver.Minor)"
    $txtVerRevision.Text = "$($ver.Revision)"

    Check-VersionWarning
}

function Get-PushBarVersion {
    $major = 0; $minor = 0; $revision = 0
    try { $major = [int]$txtVerMajor.Text } catch {}
    try { $minor = [int]$txtVerMinor.Text } catch {}
    try { $revision = [int]$txtVerRevision.Text } catch {}
    return @{ Major = $major; Minor = $minor; Revision = $revision; String = "v$major.$minor.$revision" }
}

# ============================================================================
# GitHub release cache (persisted to disk for offline use)
# ============================================================================

$script:GhReleaseCacheFile = Join-Path $script:ProjectDir ".build-release-cache.json"
$script:GhReleaseCache = $null
$script:GhReleaseCacheTime = [DateTime]::MinValue
$script:GhOnline = $false   # tracks whether last fetch succeeded

function Load-ReleaseCache {
    if (Test-Path $script:GhReleaseCacheFile) {
        try {
            $json = Get-Content $script:GhReleaseCacheFile -Raw | ConvertFrom-Json
            $releases = @()
            foreach ($r in $json.releases) {
                $releases += @{ Tag = $r.Tag; Major = [int]$r.Major; Minor = [int]$r.Minor; Revision = [int]$r.Revision; Prerelease = [bool]$r.Prerelease }
            }
            $script:GhReleaseCache = $releases
            if ($json.fetchedAt) {
                $script:GhReleaseCacheTime = [DateTime]::Parse($json.fetchedAt)
            }
        } catch {
            $script:GhReleaseCache = @()
        }
    }
}

function Save-ReleaseCache($releases) {
    try {
        $data = @{
            fetchedAt = [DateTime]::Now.ToString("o")
            releases = @()
        }
        foreach ($r in $releases) {
            $data.releases += @{ Tag = $r.Tag; Major = $r.Major; Minor = $r.Minor; Revision = $r.Revision; Prerelease = $r.Prerelease }
        }
        $data | ConvertTo-Json -Depth 3 | Set-Content $script:GhReleaseCacheFile
    } catch {}
}

function Fetch-GithubReleases {
    # Return in-memory cache if fresh (30s)
    if ($script:GhReleaseCache -ne $null -and $script:GhReleaseCache.Count -ge 0 -and
        ([DateTime]::Now - $script:GhReleaseCacheTime).TotalSeconds -lt 30) {
        return $script:GhReleaseCache
    }

    # Try fetching from GitHub
    $releases = @()
    $fetched = $false
    try {
        $repo = $script:Settings.GithubRepo
        if ($repo -ne "") {
            $json = gh api "repos/$repo/releases" --paginate 2>$null
            if ($LASTEXITCODE -eq 0 -and $json) {
                $parsed = $json | ConvertFrom-Json
                foreach ($rel in $parsed) {
                    $tag = $rel.tag_name
                    if ($tag -match '(?:client-|server-)?v?(\d+)\.(\d+)\.(\d+)') {
                        $releases += @{
                            Tag = $tag
                            Major = [int]$Matches[1]; Minor = [int]$Matches[2]; Revision = [int]$Matches[3]
                            Prerelease = [bool]$rel.prerelease
                        }
                    }
                }
                $fetched = $true
            }
        }
    } catch {}

    if ($fetched) {
        $script:GhReleaseCache = $releases
        $script:GhReleaseCacheTime = [DateTime]::Now
        $script:GhOnline = $true
        Save-ReleaseCache $releases
    } else {
        # Offline or failed — use disk cache if memory cache is empty
        $script:GhOnline = $false
        if ($script:GhReleaseCache -eq $null -or $script:GhReleaseCache.Count -eq 0) {
            Load-ReleaseCache
        }
        if ($script:GhReleaseCache -eq $null) { $script:GhReleaseCache = @() }
        # Don't update cache time so we retry on next call
    }

    # Refresh the latest version display labels
    try { Update-LatestVersionLabels } catch {}

    return $script:GhReleaseCache
}

function Check-VersionWarning {
    $ver = Get-PushBarVersion
    $tag = "client-v$($ver.Major).$($ver.Minor).$($ver.Revision)"
    $lblVerWarning.Text = ""

    try {
        $releases = Fetch-GithubReleases
        if ($releases.Count -eq 0) { return }

        # Check if this exact version already has a GitHub release
        foreach ($rel in $releases) {
            if ($rel.Major -eq $ver.Major -and $rel.Minor -eq $ver.Minor -and $rel.Revision -eq $ver.Revision) {
                $src = if ($script:GhOnline) { "" } else { " (cached)" }
                $lblVerWarning.Text = "WARNING: Release $tag exists$src"
                $lblVerWarning.ForeColor = $script:ColorOrange
                return
            }
        }

        # Check if version is lower than the highest existing release
        $curVal = $ver.Major * 1000000 + $ver.Minor * 1000 + $ver.Revision
        $highestVal = 0; $highestTag = ""
        foreach ($rel in $releases) {
            $relVal = $rel.Major * 1000000 + $rel.Minor * 1000 + $rel.Revision
            if ($relVal -gt $highestVal) {
                $highestVal = $relVal
                $highestTag = $rel.Tag
            }
        }
        if ($highestVal -gt 0 -and $curVal -lt $highestVal) {
            $src = if ($script:GhOnline) { "" } else { " (cached)" }
            $lblVerWarning.Text = "WARNING: < latest $highestTag$src"
            $lblVerWarning.ForeColor = $script:ColorRed
        }
    } catch {}
}

function Update-LatestVersionLabels {
    try {
        $releases = $script:GhReleaseCache
        if ($releases -eq $null -or $releases.Count -eq 0) {
            $lblLatestDev.Text = "dev: ---"
            $lblLatestStable.Text = "stable: ---"
            return
        }

        $src = if ($script:GhOnline) { "" } else { "*" }

        # Find highest dev (prerelease) and highest stable
        $highDev = $null; $highDevVal = -1
        $highStable = $null; $highStableVal = -1
        foreach ($rel in $releases) {
            $val = $rel.Major * 1000000 + $rel.Minor * 1000 + $rel.Revision
            if ($rel.Prerelease) {
                if ($val -gt $highDevVal) { $highDevVal = $val; $highDev = $rel }
            } else {
                if ($val -gt $highStableVal) { $highStableVal = $val; $highStable = $rel }
            }
        }

        if ($highDev) {
            $lblLatestDev.Text = "dev: $($highDev.Major).$($highDev.Minor).$($highDev.Revision)$src"
        } else {
            $lblLatestDev.Text = "dev: ---"
        }
        if ($highStable) {
            $lblLatestStable.Text = "stable: $($highStable.Major).$($highStable.Minor).$($highStable.Revision)$src"
        } else {
            $lblLatestStable.Text = "stable: ---"
        }
    } catch {
        $lblLatestDev.Text = "dev: ---"
        $lblLatestStable.Text = "stable: ---"
    }
}

# Load disk cache on startup so version warnings work immediately (even offline)
Load-ReleaseCache
Update-LatestVersionLabels

# ============================================================================
# CHANGES.md management
# ============================================================================

function Read-ChangesFile {
    $sections = @{
        "Improvements" = @(); "Additions" = @(); "Updates" = @()
        "Known Issues" = @(); "Missing Content" = @()
    }
    if (!(Test-Path $script:ChangesFile)) { return $sections }

    $content = Get-Content $script:ChangesFile
    $currentSection = ""

    foreach ($line in $content) {
        if ($line -match '^## (.+)$') {
            $sectionName = $Matches[1].Trim()
            if ($sections.ContainsKey($sectionName)) { $currentSection = $sectionName }
            continue
        }
        if ($currentSection -ne "" -and $line -match '^\s*-\s+(.+)$') {
            $sections[$currentSection] += $Matches[1].Trim()
        }
    }
    return $sections
}

function Write-ChangesFile($sections) {
    $lines = @()
    $lines += "# Changelog - Perfect Dark 2"
    $lines += ""
    $lines += "<!-- This file tracks changes for the NEXT release."
    $lines += "     After a release is pushed, Improvements/Additions/Updates are cleared."
    $lines += "     Known Issues and Missing Content persist across releases."
    $lines += "     Format: Use - bullet points under each section. -->"
    $lines += ""

    foreach ($section in @("Improvements", "Additions", "Updates", "Known Issues", "Missing Content")) {
        $lines += "## $section"
        if ($sections.ContainsKey($section) -and $sections[$section].Count -gt 0) {
            foreach ($item in $sections[$section]) { $lines += "- $item" }
        }
        $lines += ""
    }
    Set-Content -Path $script:ChangesFile -Value ($lines -join "`n")
}

function Clear-ChangesForRelease {
    $sections = Read-ChangesFile
    $sections["Improvements"] = @()
    $sections["Additions"] = @()
    $sections["Updates"] = @()
    Write-ChangesFile $sections
}

function Format-ReleaseNotes($sections, $version, $isStable) {
    $lines = @()
    if ($isStable) {
        $lines += "# Perfect Dark 2 - $version"
        $lines += ""
        if ($sections["Additions"].Count -gt 0) {
            $lines += "## New Features"
            foreach ($item in $sections["Additions"]) { $lines += "- $item" }
            $lines += ""
        }
        if ($sections["Improvements"].Count -gt 0) {
            $lines += "## What's Fixed"
            foreach ($item in $sections["Improvements"]) { $lines += "- $item" }
            $lines += ""
        }
        if ($sections["Updates"].Count -gt 0) {
            $lines += "## What's Changed"
            foreach ($item in $sections["Updates"]) { $lines += "- $item" }
            $lines += ""
        }
        if ($sections["Known Issues"].Count -gt 0) {
            $lines += "## Known Issues"
            foreach ($item in $sections["Known Issues"]) { $lines += "- $item" }
            $lines += ""
        }
    } else {
        $lines += "# $version - Development Build"
        $lines += ""
        $lines += "**This is a pre-release development build for testing.**"
        $lines += ""
        foreach ($section in @("Improvements", "Additions", "Updates", "Known Issues", "Missing Content")) {
            if ($sections[$section].Count -gt 0) {
                $lines += "## $section"
                foreach ($item in $sections[$section]) { $lines += "- $item" }
                $lines += ""
            }
        }
    }
    return ($lines -join "`n")
}

# ============================================================================
# Git change tracking + manual commit
# ============================================================================

$script:LastGitCheck = [DateTime]::MinValue
$script:GitBusy = $false

function Update-GitChangeCount {
    # Throttle: only check every 5 seconds
    if (([DateTime]::Now - $script:LastGitCheck).TotalSeconds -lt 5) { return }
    # Skip if another git operation (commit, push) is in progress
    if ($script:GitBusy) { return }
    $script:LastGitCheck = [DateTime]::Now

    try {
        $status = git -C $script:ProjectDir status --porcelain 2>$null
        if ($status) {
            $count = ($status | Measure-Object).Count
        } else {
            $count = 0
        }
    } catch {
        $count = 0
    }

    $script:GitChangeCount = $count

    if ($count -gt 0) {
        $s = if ($count -eq 1) { "" } else { "s" }
        $btnCommit.Text = "Commit $count change$s"
        $btnCommit.Enabled = (-not $script:IsRunning)
        $btnCommit.ForeColor = if (-not $script:IsRunning) { $script:ColorPurple } else { $script:ColorDisabled }
    } else {
        $btnCommit.Text = "No changes"
        $btnCommit.Enabled = $false
        $btnCommit.ForeColor = $script:ColorDisabled
    }
}

function Start-ManualCommit {
    if ($script:GitChangeCount -eq 0) { return }

    Sound-MenuClick

    # Gather change details for the dialog
    $statusLines = git -C $script:ProjectDir status --porcelain 2>$null
    $added = @(); $modified = @(); $deleted = @(); $renamed = @()
    foreach ($line in $statusLines) {
        $code = $line.Substring(0, 2).Trim()
        $file = $line.Substring(3).Trim()
        switch -Wildcard ($code) {
            "A"  { $added += $file }
            "M"  { $modified += $file }
            "MM" { $modified += $file }
            "D"  { $deleted += $file }
            "R*" { $renamed += $file }
            "??" { $added += $file }
            default { $modified += $file }
        }
    }

    # Build summary grouped by area
    $detailLines = @()
    function Group-ByArea($files) {
        $grouped = @{}
        foreach ($f in $files) {
            $area = switch -Wildcard ($f) {
                "src/game/*"    { "Game" }
                "src/include/*" { "Headers" }
                "src/lib/*"     { "Lib" }
                "port/*"        { "Port/Renderer" }
                "context/*"     { "Context" }
                "build-gui*"    { "Build Tool" }
                "CMake*"        { "Build System" }
                default         { "Other" }
            }
            if (-not $grouped[$area]) { $grouped[$area] = @() }
            $grouped[$area] += ($f -replace '^.+/', '')
        }
        return $grouped
    }

    if ($modified.Count -gt 0) {
        $detailLines += "Modified ($($modified.Count)):"
        $groups = Group-ByArea $modified
        foreach ($area in ($groups.Keys | Sort-Object)) {
            $detailLines += "  [$area] $($groups[$area] -join ', ')"
        }
    }
    if ($added.Count -gt 0) {
        $detailLines += "Added ($($added.Count)):"
        $groups = Group-ByArea $added
        foreach ($area in ($groups.Keys | Sort-Object)) {
            $detailLines += "  [$area] $($groups[$area] -join ', ')"
        }
    }
    if ($deleted.Count -gt 0) {
        $detailLines += "Deleted ($($deleted.Count)):"
        $groups = Group-ByArea $deleted
        foreach ($area in ($groups.Keys | Sort-Object)) {
            $detailLines += "  [$area] $($groups[$area] -join ', ')"
        }
    }
    if ($renamed.Count -gt 0) {
        $detailLines += "Renamed ($($renamed.Count)):"
        foreach ($r in $renamed) { $detailLines += "  $r" }
    }
    $detailText = $detailLines -join "`r`n"

    # Show commit message dialog
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = "Commit Changes"
    $dlg.Size = New-Object System.Drawing.Size(520, 380)
    $dlg.StartPosition = "CenterParent"
    $dlg.FormBorderStyle = "FixedDialog"
    $dlg.MaximizeBox = $false
    $dlg.MinimizeBox = $false
    $dlg.BackColor = $script:ColorPanelBg
    $dlg.ForeColor = $script:ColorWhite

    $lblMsg = New-Object System.Windows.Forms.Label
    $lblMsg.Text = "Commit message ($($script:GitChangeCount) file(s)):"
    $lblMsg.Font = New-UIFont 10
    $lblMsg.ForeColor = $script:ColorWhite
    $lblMsg.Location = New-Object System.Drawing.Point(12, 12)
    $lblMsg.AutoSize = $true
    $dlg.Controls.Add($lblMsg)

    $txtMsg = New-Object System.Windows.Forms.TextBox
    $txtMsg.Location = New-Object System.Drawing.Point(12, 38)
    $txtMsg.Size = New-Object System.Drawing.Size(480, 24)
    $txtMsg.BackColor = $script:ColorFieldBg
    $txtMsg.ForeColor = $script:ColorWhite
    $txtMsg.Font = New-Object System.Drawing.Font("Consolas", 10)
    $txtMsg.BorderStyle = "FixedSingle"
    $ver = Get-ProjectVersion
    $txtMsg.Text = "$($ver.String) -"
    $dlg.Controls.Add($txtMsg)

    # Details area — read-only summary of changes
    $lblDetails = New-Object System.Windows.Forms.Label
    $lblDetails.Text = "Changes:"
    $lblDetails.Font = New-UIFont 9
    $lblDetails.ForeColor = $script:ColorDim
    $lblDetails.Location = New-Object System.Drawing.Point(12, 70)
    $lblDetails.AutoSize = $true
    $dlg.Controls.Add($lblDetails)

    $txtDetails = New-Object System.Windows.Forms.TextBox
    $txtDetails.Location = New-Object System.Drawing.Point(12, 90)
    $txtDetails.Size = New-Object System.Drawing.Size(480, 160)
    $txtDetails.BackColor = [System.Drawing.Color]::FromArgb(30, 30, 35)
    $txtDetails.ForeColor = $script:ColorDim
    $txtDetails.Font = New-Object System.Drawing.Font("Consolas", 9)
    $txtDetails.BorderStyle = "FixedSingle"
    $txtDetails.Multiline = $true
    $txtDetails.ReadOnly = $true
    $txtDetails.ScrollBars = "Vertical"
    $txtDetails.WordWrap = $false
    $txtDetails.Text = $detailText
    $dlg.Controls.Add($txtDetails)

    $chkPush = New-Object System.Windows.Forms.CheckBox
    $chkPush.Text = "Push to GitHub after commit"
    $chkPush.Font = New-UIFont 9
    $chkPush.ForeColor = $script:ColorDim
    $chkPush.Location = New-Object System.Drawing.Point(12, 260)
    $chkPush.AutoSize = $true
    $chkPush.Checked = $true
    $dlg.Controls.Add($chkPush)

    $btnOk = New-Object System.Windows.Forms.Button
    $btnOk.Text = "Commit"
    $btnOk.Location = New-Object System.Drawing.Point(310, 300)
    $btnOk.Size = New-Object System.Drawing.Size(86, 30)
    $btnOk.FlatStyle = "Flat"
    $btnOk.FlatAppearance.BorderColor = $script:ColorGreen
    $btnOk.ForeColor = $script:ColorGreen
    $btnOk.BackColor = $script:ColorFieldBg
    $btnOk.Font = New-UIFont 10 -Bold
    $btnOk.DialogResult = "OK"
    $dlg.Controls.Add($btnOk)
    $dlg.AcceptButton = $btnOk

    $btnCancel = New-Object System.Windows.Forms.Button
    $btnCancel.Text = "Cancel"
    $btnCancel.Location = New-Object System.Drawing.Point(406, 300)
    $btnCancel.Size = New-Object System.Drawing.Size(86, 30)
    $btnCancel.FlatStyle = "Flat"
    $btnCancel.FlatAppearance.BorderColor = $script:ColorDim
    $btnCancel.ForeColor = $script:ColorDim
    $btnCancel.BackColor = $script:ColorFieldBg
    $btnCancel.Font = New-UIFont 10 -Bold
    $btnCancel.DialogResult = "Cancel"
    $dlg.Controls.Add($btnCancel)
    $dlg.CancelButton = $btnCancel

    $result = $dlg.ShowDialog($form)
    $commitMsg = $txtMsg.Text.Trim()
    $shouldPush = $chkPush.Checked
    $dlg.Dispose()

    if ($result -ne "OK" -or $commitMsg -eq "") { return }

    Write-Header "Git Commit"

    # Lock out timer-based git status checks while we're committing
    $script:GitBusy = $true

    $savedEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    # Safety: remove stale index.lock if present (previous crash)
    $lockFile = Join-Path $script:ProjectDir ".git\index.lock"
    if (Test-Path $lockFile) {
        Write-Output-Line "  Removing stale index.lock..." $script:ColorYellow
        Remove-Item $lockFile -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 200
    }

    # Stage all
    Write-Output-Line "  Staging all changes..." $script:ColorPurple
    $addOut = git -C $script:ProjectDir add -A 2>&1
    foreach ($line in $addOut) { Write-Output-Line "    $($line.ToString())" $script:ColorDim }

    # Commit
    $commitOut = git -C $script:ProjectDir commit -m $commitMsg 2>&1
    $commitExit = $LASTEXITCODE
    foreach ($line in $commitOut) { Write-Output-Line "    $($line.ToString())" $script:ColorDim }

    if ($commitExit -ne 0) {
        Write-Output-Line "  Commit failed (exit $commitExit)" $script:ColorRed
        $ErrorActionPreference = $savedEAP
        $script:GitBusy = $false
        return
    }
    Write-Output-Line "  Committed: $commitMsg" $script:ColorGreen
    Sound-ItemPickup

    # Push
    if ($shouldPush) {
        Write-Output-Line "  Pushing to origin..." $script:ColorPurple
        # Check if current branch has an upstream; if not, use --set-upstream
        $upstream = git -C $script:ProjectDir rev-parse --abbrev-ref "@{upstream}" 2>$null
        if ($LASTEXITCODE -ne 0 -or !$upstream) {
            $branch = git -C $script:ProjectDir rev-parse --abbrev-ref HEAD 2>$null
            Write-Output-Line "  Setting upstream for '$branch'..." $script:ColorDim
            $pushOut = git -C $script:ProjectDir push --set-upstream origin $branch 2>&1
        } else {
            $pushOut = git -C $script:ProjectDir push origin 2>&1
        }
        $pushExit = $LASTEXITCODE
        foreach ($line in $pushOut) { Write-Output-Line "    $($line.ToString())" $script:ColorDim }

        if ($pushExit -ne 0) {
            Write-Output-Line "  Push failed (exit $pushExit)" $script:ColorRed
        } else {
            Write-Output-Line "  Pushed to GitHub" $script:ColorGreen
        }
    }

    $ErrorActionPreference = $savedEAP

    # Unlock and reset change count immediately
    $script:GitBusy = $false
    $script:LastGitCheck = [DateTime]::MinValue
    Update-GitChangeCount
}

$btnCommit.Add_Click({ Start-ManualCommit })

# ============================================================================
# Auto-commit (runs before every build and push)
# ============================================================================

function Auto-Commit {
    # Lock out timer-based git status checks
    $script:GitBusy = $true

    $savedEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    # Safety: remove stale index.lock if present
    $lockFile = Join-Path $script:ProjectDir ".git\index.lock"
    if (Test-Path $lockFile) {
        Write-Output-Line "  Removing stale index.lock..." $script:ColorYellow
        Remove-Item $lockFile -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 200
    }

    $status = git -C $script:ProjectDir status --porcelain 2>$null
    if (!$status) {
        Write-Output-Line "  No uncommitted changes." $script:ColorDim
        $ErrorActionPreference = $savedEAP
        $script:GitBusy = $false
        return $true
    }

    Write-Header "Auto-Commit"
    $changeCount = ($status | Measure-Object).Count
    Write-Output-Line "  $changeCount file(s) changed -- committing..." $script:ColorPurple

    # Stage all
    $addOut = git -C $script:ProjectDir add -A 2>&1
    foreach ($line in $addOut) { Write-Output-Line "    $($line.ToString())" $script:ColorDim }

    # Generate commit message from version
    $ver = Get-ProjectVersion
    $msg = "Build $($ver.String) - auto-commit before build"

    $commitOut = git -C $script:ProjectDir commit -m $msg 2>&1
    $commitExit = $LASTEXITCODE
    foreach ($line in $commitOut) { Write-Output-Line "    $($line.ToString())" $script:ColorDim }

    $ErrorActionPreference = $savedEAP
    $script:GitBusy = $false

    if ($commitExit -ne 0) {
        Write-Output-Line "  Commit failed (exit $commitExit)" $script:ColorRed
        return $false
    }

    Write-Output-Line "  Committed: $msg" $script:ColorGreen
    return $true
}

# ============================================================================
# Process runner
# ============================================================================

$script:Process = $null
$script:StepQueue = [System.Collections.Queue]::new()
$script:CurrentStep = ""

function Start-Build-Step($stepName, $exe, $argList) {
    Sound-ItemPickup
    $script:CurrentStep = $stepName
    $script:LastOutputTime = [DateTime]::Now
    $script:StepStartTime = [DateTime]::Now
    $script:BuildPercent = 0
    $progressFill.Size = New-Object System.Drawing.Size(0, 22)
    if (-not $script:HasErrors) { $progressFill.BackColor = $script:ColorBlue }
    $progressLabel.Text = ""
    $statusLabel.Text = $stepName
    $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(100, 180, 255)
    Write-Header $stepName

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = $argList
    $psi.WorkingDirectory = $script:ProjectDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
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
        $timer.Start()
    } catch {
        Write-Output-Line "Failed to start: $exe $argList" $script:ColorRed
        Write-Output-Line $_.Exception.Message $script:ColorRed
        $statusLabel.Text = "FAILED"
        $statusLabel.ForeColor = $script:ColorRed
        Set-Buttons-Enabled $true
    }
}

# Timer tick: drain queue on UI thread
$timer.Add_Tick({
    $linesThisTick = 0
    $maxLinesPerTick = 80
    $line = $null

    while ($linesThisTick -lt $maxLinesPerTick -and $script:OutputQueue.TryDequeue([ref]$line)) {
        $text = $line.Substring(4)
        $class = Classify-Line $text
        Write-Output-Line $text (Get-Line-Color $class)
        if ($class -eq "error") {
            [void]$script:ErrorLines.Add($text)
            # Route to target-specific error list
            if ($script:BuildTarget -eq "server") {
                [void]$script:ServerErrorLines.Add($text)
            } else {
                [void]$script:ClientErrorLines.Add($text)
            }
            if (-not $script:HasErrors) {
                $script:HasErrors = $true
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
            }
        }

        if ($text -match '^\[\s*(\d+)%\]') {
            $pct = [int]$Matches[1]
            if ($pct -ge $script:BuildPercent) {
                $script:BuildPercent = $pct
                $fillWidth = [math]::Floor(($pct / 100.0) * $consoleW)
                $progressFill.Size = New-Object System.Drawing.Size($fillWidth, 22)
                $progressLabel.Text = "${pct}% - $($script:CurrentStep)"
                if (-not $script:HasErrors) { $progressFill.BackColor = $script:ColorBlue }
            }
        }

        $script:LastOutputTime = [DateTime]::Now
        $linesThisTick++
    }

    # Spinner when no output
    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        $totalElapsed = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
        $silentSec = [math]::Floor(([DateTime]::Now - $script:LastOutputTime).TotalSeconds)
        if ($silentSec -gt 2) {
            $spin = $script:SpinnerChars[$script:SpinnerIndex % 4]
            $script:SpinnerIndex++
            $statusLabel.Text = "$($script:CurrentStep) $spin ${totalElapsed}s"
        } else {
            $statusLabel.Text = "$($script:CurrentStep) (${totalElapsed}s)"
        }
        return
    }

    # Process exited
    if ($null -ne $script:Process -and $script:Process.HasExited -and $script:OutputQueue.IsEmpty) {
        $timer.Stop()
        $exitCode = $script:Process.ExitCode
        $totalElapsed = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
        try { $script:Process.Dispose() } catch {}
        $script:Process = $null

        $errCount = $script:ErrorLines.Count
        if ($errCount -gt 0) {
            $errorCountLabel.Text = "$errCount error line(s) - click 'Copy Errors'"
        }

        if ($exitCode -ne 0) {
            $progressFill.Size = New-Object System.Drawing.Size($consoleW, 22)
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
            $progressLabel.Text = "FAILED - $($script:CurrentStep)"
            Write-Output-Line "" $script:ColorRed
            Write-Output-Line ">>> $($script:CurrentStep) FAILED (exit code $exitCode) after ${totalElapsed}s <<<" $script:ColorRed
            $statusLabel.Text = "FAILED: $($script:CurrentStep)"
            $statusLabel.ForeColor = $script:ColorRed
            $script:BuildSucceeded = $false
            $script:StepQueue.Clear()

            # Surface CMake diagnostic logs on configure/generate failures
            if ($script:CurrentStep -match "Configure") {
                $cmakeErrLog = Join-Path $script:BuildDir "CMakeFiles\CMakeError.log"
                $cmakeOutLog = Join-Path $script:BuildDir "CMakeFiles\CMakeConfigureLog.yaml"
                # Also check the newer CMake 3.26+ configure log
                if (-not (Test-Path $cmakeOutLog)) {
                    $cmakeOutLog = Join-Path $script:BuildDir "CMakeFiles\CMakeOutput.log"
                }

                foreach ($logFile in @($cmakeErrLog, $cmakeOutLog)) {
                    if (Test-Path $logFile) {
                        $logName = Split-Path $logFile -Leaf
                        Write-Output-Line "" $script:ColorDim
                        Write-Output-Line "--- $logName ---" $script:ColorOrange
                        try {
                            $lines = Get-Content $logFile -Tail 40
                            foreach ($l in $lines) {
                                $cls = Classify-Line $l
                                Write-Output-Line "  $l" (Get-Line-Color $cls)
                            }
                        } catch {
                            Write-Output-Line "  (could not read $logFile)" $script:ColorDim
                        }
                    }
                }
            }

            # Record build time for the failed target
            $failedTime = [math]::Floor(([DateTime]::Now - $script:TargetStartTime).TotalSeconds)

            # Track which target failed
            if ($script:BuildTarget -eq "server") {
                $script:ServerBuildFailed = $true
                $script:ServerBuildTime = $failedTime
            } else {
                $script:ClientBuildFailed = $true
                $script:ClientBuildTime = $failedTime
            }

            # In Client+Server mode, continue to server build even if client failed
            if ($script:PendingServerBuild) {
                Write-Output-Line "" $script:ColorDim
                Write-Output-Line "--- Client build failed ($($failedTime)s), continuing to Server build ---" $script:ColorOrange
                $script:HasErrors = $false
                $script:IsRunning = $false
                # PendingServerBuild will be picked up by the game timer
                return
            }

            # Build timing summary for progress bar
            $timingParts = @()
            if ($script:ClientBuildTime -gt 0 -or $script:ClientBuildFailed) {
                $cs = if ($script:ClientBuildFailed) { "FAILED" } else { "OK" }
                $timingParts += "Client $cs - $($script:ClientBuildTime)s"
            }
            if ($script:ServerBuildTime -gt 0 -or $script:ServerBuildFailed) {
                $ss = if ($script:ServerBuildFailed) { "FAILED" } else { "OK" }
                $timingParts += "Server $ss - $($script:ServerBuildTime)s"
            }
            if ($timingParts.Count -gt 0) {
                $progressLabel.Text = ($timingParts -join "  |  ")
                # Print timing to output
                Write-Output-Line "" $script:ColorDim
                foreach ($part in $timingParts) {
                    $partColor = if ($part -match "FAILED") { $script:ColorRed } else { $script:ColorGreen }
                    Write-Output-Line "  $part" $partColor
                }
            }

            Play-BuildSound $false
            Set-Buttons-Enabled $true
            return
        }

        $progressFill.Size = New-Object System.Drawing.Size($consoleW, 22)
        if ($script:HasErrors) {
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
        } else {
            $progressFill.BackColor = $script:ColorBlue
        }
        $progressLabel.Text = "100% - $($script:CurrentStep) Complete"
        Write-Output-Line ">>> $($script:CurrentStep) OK (${totalElapsed}s) <<<" $script:ColorGreen

        if ($script:StepQueue.Count -gt 0) {
            $next = $script:StepQueue.Dequeue()
            Start-Build-Step $next.Name $next.Exe $next.Args
        } else {
            # Record final target build time
            $finalTime = [math]::Floor(([DateTime]::Now - $script:TargetStartTime).TotalSeconds)
            if ($script:BuildTarget -eq "server") {
                $script:ServerBuildTime = $finalTime
            } else {
                $script:ClientBuildTime = $finalTime
            }

            # Check for errors from this target AND any previous target (Client+Server mode)
            $anyErrors = $script:HasErrors -or $script:ClientBuildFailed -or $script:ServerBuildFailed
            $totalErrCount = $script:ErrorLines.Count

            # Build per-target timing summary
            $timingParts = @()
            if ($script:ClientBuildTime -gt 0 -or $script:ClientBuildFailed) {
                $clientStatus = if ($script:ClientBuildFailed) { "FAILED" } else { "OK" }
                $timingParts += "Client $clientStatus - $($script:ClientBuildTime)s"
            }
            if ($script:ServerBuildTime -gt 0 -or $script:ServerBuildFailed) {
                $serverStatus = if ($script:ServerBuildFailed) { "FAILED" } else { "OK" }
                $timingParts += "Server $serverStatus - $($script:ServerBuildTime)s"
            }
            $timingSummary = $timingParts -join "  |  "

            if ($anyErrors) {
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
                if ($timingParts.Count -gt 0) {
                    $progressLabel.Text = $timingSummary
                } else {
                    $progressLabel.Text = "COMPLETE (with errors)"
                }
            } else {
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(0, 191, 96)
                if ($timingParts.Count -gt 0) {
                    $progressLabel.Text = $timingSummary
                } else {
                    $progressLabel.Text = "BUILD COMPLETE"
                }
            }

            # Print timing summary to output
            if ($timingParts.Count -gt 0) {
                Write-Output-Line "" $script:ColorDim
                foreach ($part in $timingParts) {
                    $partColor = if ($part -match "FAILED") { $script:ColorRed } else { $script:ColorGreen }
                    Write-Output-Line "  $part" $partColor
                }
            }

            if ($totalErrCount -gt 0) {
                $errorCountLabel.Text = "$totalErrCount error line(s) - click 'Copy Errors'"
            }

            Copy-AddinFiles
            Play-BuildSound (-not $anyErrors)
            $statusLabel.Text = "Build Complete"
            $statusLabel.ForeColor = if ($anyErrors) { $script:ColorOrange } else { $script:ColorGreen }
            $script:BuildSucceeded = -not $anyErrors
            Set-Buttons-Enabled $true
            Refresh-VersionDisplay
        }
    }
})

# ============================================================================
# Build step definitions
# ============================================================================

function Get-ConfigureStep {
    return @{
        Name = "Configure (CMake)"
        Exe  = $script:CMake
        Args = "-G `"Unix Makefiles`" -DCMAKE_MAKE_PROGRAM=`"$($script:Make)`" -DCMAKE_C_COMPILER=`"$($script:CC)`" -B `"$($script:BuildDir)`" -S `"$($script:ProjectDir)`""
    }
}

function Get-BuildStep($target) {
    $cores = $env:NUMBER_OF_PROCESSORS
    if (!$cores) { $cores = 4 }
    $targetArg = ""
    $label = "Build All"
    if ($target -eq "client") {
        $targetArg = "--target pd"
        $label = "Build Client"
    } elseif ($target -eq "server") {
        $targetArg = "--target pd-server"
        $label = "Build Server"
    }
    return @{
        Name = "$label (Compile)"
        Exe  = $script:CMake
        Args = "--build `"$($script:BuildDir)`" $targetArg -- -j$cores -k"
    }
}

# ============================================================================
# Copy addin files (post-build)
# ============================================================================

function Copy-AddinFiles {
    if ($script:BuildTarget -eq "server") {
        Write-Header "Post-Build"
        Write-Output-Line "  Server build -- no addin files needed." $script:ColorDim
        return
    }

    Write-Header "Copy Addin Files"

    if (!(Test-Path $script:AddinDir)) {
        Write-Output-Line "Addin directory not found: $($script:AddinDir)" $script:ColorRed
        return
    }
    if (!(Test-Path $script:BuildDir)) {
        Write-Output-Line "Build directory not found." $script:ColorRed
        return
    }

    $copied = 0
    $dataDir = Join-Path $script:AddinDir "data"
    if (Test-Path $dataDir) {
        Copy-Item $dataDir -Destination $script:BuildDir -Recurse -Force
        Write-Output-Line "  data\" $script:ColorPurple
        $copied++
    }
    # NOTE: Legacy flat-file mods disabled (S37). They use filename-shadowing
    # that corrupts base game stages via modconfig.txt patching and modmgr
    # file resolution. Mods will be reintroduced in D3R-6+ component format.
    # $modsDir = Join-Path $script:AddinDir "mods"
    # if (Test-Path $modsDir) {
    #     Copy-Item $modsDir -Destination $script:BuildDir -Recurse -Force
    #     Write-Output-Line "  mods\" $script:ColorPurple
    #     $copied++
    # }
    Write-Output-Line "Copied $copied items." $script:ColorGreen
}

# ============================================================================
# Game launch
# ============================================================================

function Launch-Game($mode) {
    if ($script:IsRunning) { return }

    if ($mode -eq "server") {
        $launchDir = $script:ServerBuildDir
        $launchExe = Join-Path $launchDir "PerfectDarkServer.exe"
        $gameArgs = ""
        $label = "Dedicated Server"
        $labelColor = $script:ColorOrange
    } else {
        $launchDir = $script:ClientBuildDir
        $launchExe = Join-Path $launchDir $script:ExeName
        # Legacy mod args disabled (S37) — mods will use D3R component format
        $gameArgs = ""
        $label = "Client"
        $labelColor = $script:ColorGreen
    }

    if (!(Test-Path $launchExe)) {
        Write-Output-Line "$label not found. Build first." $script:ColorRed
        $statusLabel.Text = "$label not found"
        $statusLabel.ForeColor = $script:ColorRed
        return
    }

    Write-Output-Line "" $script:ColorDimmer
    Write-Output-Line "Launching $label..." $labelColor
    Write-Output-Line "  Exe: $launchExe" ([System.Drawing.Color]::FromArgb(50,180,220))

    $startArgs = @{
        FilePath         = $launchExe
        WorkingDirectory = $launchDir
        PassThru         = $true
    }
    if ($gameArgs -ne "") {
        $startArgs.ArgumentList = $gameArgs
    }
    try {
        $script:GameProcess = Start-Process @startArgs
    } catch {
        Write-Output-Line "  Launch failed: $($_.Exception.Message)" $script:ColorRed
        $statusLabel.Text = "Launch failed"
        $statusLabel.ForeColor = $script:ColorRed
        return
    }
    Update-GameStatus
}

function Update-GameStatus {
    $wasRunning = $script:GameRunning

    if ($null -ne $script:GameProcess) {
        try {
            if ($script:GameProcess.HasExited) {
                $script:GameProcess = $null
                $script:GameRunning = $false
            } else {
                $script:GameRunning = $true
            }
        } catch {
            $script:GameProcess = $null
            $script:GameRunning = $false
        }
    } else {
        $script:GameRunning = $false
    }

    if (-not $script:GameRunning) {
        try {
            $procs = Get-Process -Name ($script:ExeName -replace '\.exe$','') -ErrorAction SilentlyContinue
            $script:GameRunning = ($null -ne $procs -and $procs.Count -gt 0)
        } catch { $script:GameRunning = $false }
    }

    if ($script:GameRunning) {
        if (-not $script:IsRunning) {
            $statusLabel.Text = "Game running"
            $statusLabel.ForeColor = $script:ColorGreen
        }
    } else {
        if ($wasRunning -and -not $script:IsRunning) {
            $statusLabel.Text = "Game exited"
            $statusLabel.ForeColor = $script:ColorDim
        }
    }
}

# ============================================================================
# Build action
# ============================================================================

function Start-Build($target) {
    if ($script:IsRunning) { return }

    $outputBox.Clear()
    $script:ErrorLines.Clear()
    $script:AllOutput.Clear()
    $script:ClientErrorLines.Clear()
    $script:ServerErrorLines.Clear()
    $script:ClientBuildFailed = $false
    $script:ServerBuildFailed = $false
    $script:ClientBuildTime = 0
    $script:ServerBuildTime = 0
    $script:TargetStartTime = [DateTime]::Now
    $errorCountLabel.Text = ""
    $script:BuildSucceeded = $false
    $script:BuildTarget = $target
    $script:HasErrors = $false

    # Auto-commit before build
    Auto-Commit

    if ($target -eq "server") {
        $script:BuildDir = $script:ServerBuildDir
    } else {
        $script:BuildDir = $script:ClientBuildDir
    }

    $progressFill.Size = New-Object System.Drawing.Size(0, 22)
    $progressFill.BackColor = $script:ColorBlue
    $progressLabel.Text = ""

    if ($chkClean.Checked) {
        if (Test-Path $script:BuildDir) {
            Write-Header "Clean: $($script:BuildDir | Split-Path -Leaf)"
            Remove-Item -Path $script:BuildDir -Recurse -Force -ErrorAction SilentlyContinue
            Write-Output-Line "  Cleared build directory." $script:ColorDim
        } else {
            Write-Header "Clean Build"
        }
    } else {
        if (Test-Path $script:BuildDir) {
            Write-Header "Incremental Build"
            Write-Output-Line "  Reusing existing build directory (only changed files recompile)." $script:ColorDim
        } else {
            Write-Header "First Build"
        }
    }

    Set-Buttons-Enabled $false

    $script:StepQueue.Clear()
    $script:StepQueue.Enqueue((Get-BuildStep $target))

    $step = Get-ConfigureStep
    Start-Build-Step $step.Name $step.Exe $step.Args
}

function Start-BuildFromDropdown {
    $sel = $cmbBuildTarget.SelectedItem
    if ($sel -eq "Client") {
        Start-Build "client"
    } elseif ($sel -eq "Server") {
        Start-Build "server"
    } elseif ($sel -eq "Client + Server") {
        # Build client first, then server
        Start-Build "client"
        # After client finishes, the step queue will be empty. We need a different
        # approach: queue the server build as a post-completion action.
        $script:PendingServerBuild = $true
    }
}

$script:PendingServerBuild = $false

# Start server build after client, preserving client output and errors
function Start-Build-Server-After-Client {
    if ($script:IsRunning) { return }

    # Record client build time before switching targets
    $script:ClientBuildTime = [math]::Floor(([DateTime]::Now - $script:TargetStartTime).TotalSeconds)
    $script:TargetStartTime = [DateTime]::Now

    # Don't clear output or error lists — keep client output visible
    $script:BuildSucceeded = $false
    $script:BuildTarget = "server"
    $script:HasErrors = $false
    $script:BuildDir = $script:ServerBuildDir

    $progressFill.Size = New-Object System.Drawing.Size(0, 22)
    $progressFill.BackColor = $script:ColorBlue
    $progressLabel.Text = ""

    Write-Output-Line "" $script:ColorDim
    Write-Header "Server Build"

    if ($chkClean.Checked -and (Test-Path $script:BuildDir)) {
        Write-Output-Line "  Clearing server build directory..." $script:ColorDim
        Remove-Item -Path $script:BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    }

    Set-Buttons-Enabled $false

    $script:StepQueue.Clear()
    $script:StepQueue.Enqueue((Get-BuildStep "server"))

    $step = Get-ConfigureStep
    Start-Build-Step $step.Name $step.Exe $step.Args
}

# ============================================================================
# Push to GitHub
# ============================================================================

function Start-PushRelease {
    if ($script:IsRunning) { return }

    $releaseScript = Join-Path $script:ProjectDir "release.ps1"
    if (!(Test-Path $releaseScript)) {
        Write-Output-Line "release.ps1 not found in project root." $script:ColorRed
        $statusLabel.Text = "Release script not found"
        $statusLabel.ForeColor = $script:ColorRed
        return
    }

    # Write push-bar version fields back to CMakeLists.txt
    $pushVer = Get-PushBarVersion
    Set-ProjectVersion $pushVer.Major $pushVer.Minor $pushVer.Revision

    # Auto-commit before push (includes version change)
    Auto-Commit

    $ver = Get-PushBarVersion
    $verStr = "$($ver.Major).$($ver.Minor).$($ver.Revision)"
    $isStable = $chkStable.Checked

    # Read changelog
    $sections = Read-ChangesFile
    $totalChanges = 0
    foreach ($key in @("Improvements", "Additions", "Updates")) {
        $totalChanges += $sections[$key].Count
    }

    # Confirmation
    $channel = if ($isStable) { "STABLE" } else { "DEV (prerelease)" }
    $confirmMsg = "Push Release v$verStr to GitHub?`n`n"
    $confirmMsg += "Channel: $channel`n"
    $confirmMsg += "Changes logged: $totalChanges`n`n"
    if ($totalChanges -eq 0) {
        $confirmMsg += "WARNING: No changes in CHANGES.md.`n"
        $confirmMsg += "Release notes will be auto-generated by GitHub.`n`n"
    }
    $confirmMsg += "This will:`n"
    $confirmMsg += "  - Create git tags: client-v$verStr + server-v$verStr`n"
    $confirmMsg += "  - Push to GitHub`n"
    $confirmMsg += "  - Create GitHub Releases ($channel)`n"

    $confirm = [System.Windows.Forms.MessageBox]::Show(
        $confirmMsg, "Push Release v$verStr",
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Warning
    )
    if ($confirm -ne [System.Windows.Forms.DialogResult]::Yes) { return }

    # Generate release notes
    $notesFile = Join-Path $script:ProjectDir "RELEASE_v$verStr.md"
    if ($totalChanges -gt 0) {
        $notes = Format-ReleaseNotes $sections $verStr $isStable
        Set-Content -Path $notesFile -Value $notes
        Write-Output-Line "  Generated: RELEASE_v$verStr.md" $script:ColorPurple
    }

    $outputBox.Clear()
    $script:ErrorLines.Clear()
    $script:AllOutput.Clear()
    $errorCountLabel.Text = ""
    $script:HasErrors = $false

    Set-Buttons-Enabled $false

    # Build release.ps1 arguments
    $scriptArgs = "-Version `"$verStr`""
    if (-not $isStable) {
        $scriptArgs += " -Prerelease"
    }

    $script:StepQueue.Clear()
    $script:PendingPostRelease = $totalChanges -gt 0

    Start-Build-Step "Push Release v$verStr" "powershell.exe" "-ExecutionPolicy Bypass -Command `"& { . '$releaseScript' $scriptArgs } *>&1`""
}

$script:PendingPostRelease = $false

# ============================================================================
# Settings dialog
# ============================================================================

function Show-SettingsDialog {
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = "Settings"
    $dlg.Size = New-Object System.Drawing.Size(540, 510)
    $dlg.StartPosition = "CenterParent"
    $dlg.BackColor = $script:ColorBg
    $dlg.ForeColor = $script:ColorWhite
    $dlg.FormBorderStyle = "FixedDialog"
    $dlg.MaximizeBox = $false
    $dlg.MinimizeBox = $false

    # --- Tab Control ---
    $tabs = New-Object System.Windows.Forms.TabControl
    $tabs.Location = New-Object System.Drawing.Point(8, 8)
    $tabs.Size = New-Object System.Drawing.Size(510, 400)
    $tabs.Font = New-UIFont 10
    $dlg.Controls.Add($tabs)

    # ======================================================================
    # TAB 1: General
    # ======================================================================
    $tabGeneral = New-Object System.Windows.Forms.TabPage
    $tabGeneral.Text = "General"
    $tabGeneral.BackColor = $script:ColorBg
    $tabGeneral.ForeColor = $script:ColorWhite
    $tabs.TabPages.Add($tabGeneral)

    # GitHub CLI Authentication
    $lblTokenTitle = New-Object System.Windows.Forms.Label
    $lblTokenTitle.Text = "GitHub CLI Authentication:"
    $lblTokenTitle.Font = New-UIFont 10 -Bold
    $lblTokenTitle.ForeColor = $script:ColorDim
    $lblTokenTitle.Location = New-Object System.Drawing.Point(12, 12)
    $lblTokenTitle.AutoSize = $true
    $tabGeneral.Controls.Add($lblTokenTitle)

    $lblTokenStatus = New-Object System.Windows.Forms.Label
    $lblTokenStatus.Font = New-Object System.Drawing.Font("Consolas", 9)
    $lblTokenStatus.Location = New-Object System.Drawing.Point(12, 34)
    $lblTokenStatus.Size = New-Object System.Drawing.Size(470, 20)
    $tabGeneral.Controls.Add($lblTokenStatus)

    try {
        $savedEAP = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $authOut = gh auth status 2>&1
        $ErrorActionPreference = $savedEAP
        $authStr = ($authOut | ForEach-Object { $_.ToString() }) -join "`n"
        if ($authStr -match 'Logged in to') {
            $lblTokenStatus.Text = "Authenticated"
            $lblTokenStatus.ForeColor = $script:ColorGreen
        } else {
            $lblTokenStatus.Text = "Not authenticated. Run: gh auth login"
            $lblTokenStatus.ForeColor = $script:ColorRed
        }
    } catch {
        $lblTokenStatus.Text = "gh CLI not found. Install: winget install GitHub.cli"
        $lblTokenStatus.ForeColor = $script:ColorRed
    }

    $btnAuthLogin = New-Object System.Windows.Forms.Button
    $btnAuthLogin.Text = "Run gh auth login"
    $btnAuthLogin.Location = New-Object System.Drawing.Point(12, 58)
    $btnAuthLogin.Size = New-Object System.Drawing.Size(150, 28)
    $btnAuthLogin.FlatStyle = "Flat"
    $btnAuthLogin.FlatAppearance.BorderColor = $script:ColorPurple
    $btnAuthLogin.ForeColor = $script:ColorPurple
    $btnAuthLogin.BackColor = $script:ColorFieldBg
    $btnAuthLogin.Cursor = "Hand"
    $btnAuthLogin.Add_Click({ Start-Process "cmd.exe" -ArgumentList "/k gh auth login" })
    $tabGeneral.Controls.Add($btnAuthLogin)

    # Repository
    $lblRepoTitle = New-Object System.Windows.Forms.Label
    $lblRepoTitle.Text = "GitHub Repository (owner/repo):"
    $lblRepoTitle.Font = New-UIFont 10 -Bold
    $lblRepoTitle.ForeColor = $script:ColorDim
    $lblRepoTitle.Location = New-Object System.Drawing.Point(12, 100)
    $lblRepoTitle.AutoSize = $true
    $tabGeneral.Controls.Add($lblRepoTitle)

    $txtRepo = New-Object System.Windows.Forms.TextBox
    $txtRepo.Text = $script:Settings.GithubRepo
    $txtRepo.Location = New-Object System.Drawing.Point(12, 122)
    $txtRepo.Size = New-Object System.Drawing.Size(350, 24)
    $txtRepo.BackColor = $script:ColorFieldBg
    $txtRepo.ForeColor = $script:ColorWhite
    $txtRepo.Font = New-Object System.Drawing.Font("Consolas", 9)
    $txtRepo.BorderStyle = "FixedSingle"
    $tabGeneral.Controls.Add($txtRepo)

    # Sounds toggle
    $chkSoundsEnabled = New-Object System.Windows.Forms.CheckBox
    $chkSoundsEnabled.Text = "Enable game sounds"
    $chkSoundsEnabled.Font = New-UIFont 10
    $chkSoundsEnabled.ForeColor = $script:ColorWhite
    $chkSoundsEnabled.Location = New-Object System.Drawing.Point(12, 162)
    $chkSoundsEnabled.AutoSize = $true
    $chkSoundsEnabled.BackColor = $script:ColorBg
    $chkSoundsEnabled.Checked = $script:Settings.SoundsEnabled
    $tabGeneral.Controls.Add($chkSoundsEnabled)

    # ======================================================================
    # TAB 2: Asset Extraction
    # ======================================================================
    $tabExtract = New-Object System.Windows.Forms.TabPage
    $tabExtract.Text = "Asset Extraction"
    $tabExtract.BackColor = $script:ColorBg
    $tabExtract.ForeColor = $script:ColorWhite
    $tabs.TabPages.Add($tabExtract)

    # --- ROM Path ---
    $lblRomTitle = New-Object System.Windows.Forms.Label
    $lblRomTitle.Text = "ROM File:"
    $lblRomTitle.Font = New-UIFont 10 -Bold
    $lblRomTitle.ForeColor = $script:ColorDim
    $lblRomTitle.Location = New-Object System.Drawing.Point(12, 12)
    $lblRomTitle.AutoSize = $true
    $tabExtract.Controls.Add($lblRomTitle)

    $txtRomPath = New-Object System.Windows.Forms.TextBox
    $txtRomPath.Location = New-Object System.Drawing.Point(12, 34)
    $txtRomPath.Size = New-Object System.Drawing.Size(390, 24)
    $txtRomPath.BackColor = $script:ColorFieldBg
    $txtRomPath.ForeColor = $script:ColorWhite
    $txtRomPath.Font = New-Object System.Drawing.Font("Consolas", 9)
    $txtRomPath.BorderStyle = "FixedSingle"
    $txtRomPath.ReadOnly = $true
    if ($script:Settings.RomPath -ne "" -and (Test-Path $script:Settings.RomPath)) {
        $txtRomPath.Text = $script:Settings.RomPath
        $txtRomPath.ForeColor = $script:ColorGreen
    } else {
        $txtRomPath.Text = "(not found)"
        $txtRomPath.ForeColor = $script:ColorRed
    }
    $tabExtract.Controls.Add($txtRomPath)

    $btnBrowseRom = New-Object System.Windows.Forms.Button
    $btnBrowseRom.Text = "Browse..."
    $btnBrowseRom.Location = New-Object System.Drawing.Point(408, 33)
    $btnBrowseRom.Size = New-Object System.Drawing.Size(80, 26)
    $btnBrowseRom.FlatStyle = "Flat"
    $btnBrowseRom.FlatAppearance.BorderColor = $script:ColorDim
    $btnBrowseRom.ForeColor = $script:ColorWhite
    $btnBrowseRom.BackColor = $script:ColorFieldBg
    $btnBrowseRom.Cursor = "Hand"
    $btnBrowseRom.Font = New-UIFont 9
    $btnBrowseRom.Add_Click({
        $ofd = New-Object System.Windows.Forms.OpenFileDialog
        $ofd.Title = "Locate Perfect Dark ROM (z64)"
        $ofd.Filter = "N64 ROM files (*.z64)|*.z64|All files (*.*)|*.*"
        $initDir = Join-Path $script:ProjectDir "data"
        if (Test-Path $initDir) { $ofd.InitialDirectory = $initDir } else { $ofd.InitialDirectory = $script:ProjectDir }
        if ($ofd.ShowDialog() -eq "OK") {
            $script:Settings.RomPath = $ofd.FileName
            $txtRomPath.Text = $ofd.FileName
            $txtRomPath.ForeColor = $script:ColorGreen
        }
    })
    $tabExtract.Controls.Add($btnBrowseRom)

    $lblRomHint = New-Object System.Windows.Forms.Label
    $lblRomHint.Text = "All extraction tools require the original N64 ROM (pd.ntsc-final.z64)."
    $lblRomHint.Font = New-UIFont 9
    $lblRomHint.ForeColor = $script:ColorDim
    $lblRomHint.Location = New-Object System.Drawing.Point(12, 62)
    $lblRomHint.AutoSize = $true
    $tabExtract.Controls.Add($lblRomHint)

    # --- Separator ---
    $sepExtract1 = New-Object System.Windows.Forms.Label
    $sepExtract1.Text = ""
    $sepExtract1.Location = New-Object System.Drawing.Point(12, 82)
    $sepExtract1.Size = New-Object System.Drawing.Size(476, 1)
    $sepExtract1.BackColor = $script:ColorDimmer
    $tabExtract.Controls.Add($sepExtract1)

    # --- Extraction tools section header ---
    $lblToolsTitle = New-Object System.Windows.Forms.Label
    $lblToolsTitle.Text = "EXTRACTION TOOLS"
    $lblToolsTitle.Font = New-UIFont 9 -Bold
    $lblToolsTitle.ForeColor = $script:ColorDim
    $lblToolsTitle.Location = New-Object System.Drawing.Point(12, 90)
    $lblToolsTitle.AutoSize = $true
    $tabExtract.Controls.Add($lblToolsTitle)

    # Helper: create an extraction tool row (button + status label)
    # Returns @{ Button, Status } for wiring click handlers
    $toolRowY = 114
    function New-ExtractToolRow($label, $toolFile, $outDir, $y, $parent) {
        $btn = New-Object System.Windows.Forms.Button
        $btn.Text = $label
        $btn.Location = New-Object System.Drawing.Point(12, $y)
        $btn.Size = New-Object System.Drawing.Size(200, 26)
        $btn.FlatStyle = "Flat"
        $btn.FlatAppearance.BorderColor = $script:ColorOrange
        $btn.ForeColor = $script:ColorOrange
        $btn.BackColor = $script:ColorFieldBg
        $btn.Cursor = "Hand"
        $btn.Font = New-UIFont 9 -Bold
        $parent.Controls.Add($btn)

        $lbl = New-Object System.Windows.Forms.Label
        $lbl.Font = New-UIFont 9
        $lbl.Location = New-Object System.Drawing.Point(220, ($y + 3))
        $lbl.Size = New-Object System.Drawing.Size(268, 40)
        $lbl.TextAlign = "MiddleLeft"
        $parent.Controls.Add($lbl)

        # Check existing output
        $toolPath = Join-Path $script:ProjectDir $toolFile
        if (-not (Test-Path $toolPath)) {
            $btn.Enabled = $false
            $btn.ForeColor = $script:ColorDisabled
            $btn.FlatAppearance.BorderColor = $script:ColorDisabled
            $lbl.Text = "Tool not found"
            $lbl.ForeColor = $script:ColorDim
        } elseif ($outDir -ne "" -and (Test-Path (Join-Path $script:ProjectDir $outDir))) {
            $files = @(Get-ChildItem -Path (Join-Path $script:ProjectDir $outDir) -Recurse -File -ErrorAction SilentlyContinue)
            if ($files.Count -gt 0) {
                $lbl.Text = "$($files.Count) file(s) extracted"
                $lbl.ForeColor = $script:ColorGreen
            } else {
                $lbl.Text = "Not yet extracted"
                $lbl.ForeColor = $script:ColorDim
            }
        } else {
            $lbl.Text = "Not yet extracted"
            $lbl.ForeColor = $script:ColorDim
        }

        return @{ Button = $btn; Status = $lbl; ToolPath = $toolPath }
    }

    # --- Tool 1: Sound Effects ---
    $soundTool = New-ExtractToolRow "Sound Effects" "tools\extract-build-sounds.py" "dist\build-sounds" $toolRowY $tabExtract

    $soundTool.Button.Add_Click({
        $romPath = $script:Settings.RomPath
        if ([string]::IsNullOrEmpty($romPath) -or -not (Test-Path $romPath)) {
            $romPath = Resolve-RomPath
            if (-not $romPath) { return }
            $txtRomPath.Text = $romPath
            $txtRomPath.ForeColor = $script:ColorGreen
        }

        $toolPath = Join-Path $script:ProjectDir "tools\extract-build-sounds.py"
        $soundTool.Button.Enabled = $false
        $soundTool.Button.Text = "Extracting..."
        $soundTool.Button.ForeColor = $script:ColorDim
        $dlg.Refresh()

        try {
            $outDir = $script:SoundsDir
            $result = & python $toolPath $romPath --outdir $outDir 2>&1
            $extractedFiles = @()
            if (Test-Path $outDir) {
                $extractedFiles = @(Get-ChildItem -Path $outDir -Filter "*.wav" -Recurse)
            }
            if ($extractedFiles.Count -gt 0) {
                $soundTool.Status.Text = "$($extractedFiles.Count) file(s) extracted"
                $soundTool.Status.ForeColor = $script:ColorGreen
                Write-Output-Line "  Sound extraction: $($extractedFiles.Count) WAV files extracted" $script:ColorGreen
            } else {
                $soundTool.Status.Text = "No files produced. Check console."
                $soundTool.Status.ForeColor = $script:ColorRed
                $resultStr = ($result | ForEach-Object { $_.ToString() }) -join "`n"
                Write-Output-Line "  Sound extraction output:`n$resultStr" $script:ColorOrange
            }
        } catch {
            $soundTool.Status.Text = "Error: $($_.Exception.Message)"
            $soundTool.Status.ForeColor = $script:ColorRed
            Write-Output-Line "  Sound extraction failed: $($_.Exception.Message)" $script:ColorRed
        }

        $soundTool.Button.Enabled = $true
        $soundTool.Button.Text = "Sound Effects"
        $soundTool.Button.ForeColor = $script:ColorOrange
    }.GetNewClosure())

    # --- Tool 2: Models & Textures (placeholder) ---
    $modelTool = New-ExtractToolRow "Models & Textures" "tools\extract-models.py" "dist\models" ($toolRowY + 34) $tabExtract

    # --- Tool 3: Animations (placeholder) ---
    $animTool = New-ExtractToolRow "Animations" "tools\extract-animations.py" "dist\animations" ($toolRowY + 68) $tabExtract

    # --- Tool 4: Levels (placeholder) ---
    $levelTool = New-ExtractToolRow "Levels" "tools\extract-levels.py" "dist\levels" ($toolRowY + 102) $tabExtract

    # ======================================================================
    # Save button (bottom of dialog, outside tabs)
    # ======================================================================
    $btnSaveSettings = New-Object System.Windows.Forms.Button
    $btnSaveSettings.Text = "Save"
    $btnSaveSettings.Location = New-Object System.Drawing.Point(428, 418)
    $btnSaveSettings.Size = New-Object System.Drawing.Size(80, 28)
    $btnSaveSettings.FlatStyle = "Flat"
    $btnSaveSettings.FlatAppearance.BorderColor = $script:ColorGold
    $btnSaveSettings.ForeColor = $script:ColorGold
    $btnSaveSettings.BackColor = $script:ColorFieldBg
    $btnSaveSettings.Cursor = "Hand"
    $btnSaveSettings.Font = New-UIFont 10 -Bold
    $btnSaveSettings.Add_Click({
        $script:Settings.GithubRepo = $txtRepo.Text.Trim()
        $script:Settings.SoundsEnabled = $chkSoundsEnabled.Checked
        # RomPath is saved immediately on browse, but capture any manual changes too
        Save-Settings
        Write-Output-Line "  Settings saved" $script:ColorGreen
        $dlg.Close()
    })
    $dlg.Controls.Add($btnSaveSettings)

    [void]$dlg.ShowDialog($form)
}

# ============================================================================
# Button handlers
# ============================================================================

# Version increment buttons
$btnIncMajor.Add_Click({
    Sound-MenuTick
    try { $val = [int]$txtVerMajor.Text } catch { $val = 0 }
    $txtVerMajor.Text = "$($val + 1)"
    $txtVerMinor.Text = "0"
    $txtVerRevision.Text = "0"
    Check-VersionWarning
})
$btnIncMinor.Add_Click({
    Sound-MenuTick
    try { $val = [int]$txtVerMinor.Text } catch { $val = 0 }
    $txtVerMinor.Text = "$($val + 1)"
    $txtVerRevision.Text = "0"
    Check-VersionWarning
})
$btnIncRevision.Add_Click({
    Sound-MenuTick
    try { $val = [int]$txtVerRevision.Text } catch { $val = 0 }
    $txtVerRevision.Text = "$($val + 1)"
    Check-VersionWarning
})

# Version decrement buttons
$btnDecMajor.Add_Click({
    Sound-MenuTick
    try { $val = [int]$txtVerMajor.Text } catch { $val = 0 }
    if ($val -gt 0) { $txtVerMajor.Text = "$($val - 1)" }
    Check-VersionWarning
})
$btnDecMinor.Add_Click({
    Sound-MenuTick
    try { $val = [int]$txtVerMinor.Text } catch { $val = 0 }
    if ($val -gt 0) { $txtVerMinor.Text = "$($val - 1)" }
    Check-VersionWarning
})
$btnDecRevision.Add_Click({
    Sound-MenuTick
    try { $val = [int]$txtVerRevision.Text } catch { $val = 0 }
    if ($val -gt 0) { $txtVerRevision.Text = "$($val - 1)" }
    Check-VersionWarning
})

# Re-check warning when version fields are edited manually
$txtVerMajor.Add_TextChanged({ Check-VersionWarning })
$txtVerMinor.Add_TextChanged({ Check-VersionWarning })
$txtVerRevision.Add_TextChanged({ Check-VersionWarning })

$btnBuild.Add_Click({
    Sound-MenuClick
    $chkClean.Checked = $false
    Start-BuildFromDropdown
})
$btnCleanBuild.Add_Click({
    Sound-MenuClick
    $chkClean.Checked = $true
    Start-BuildFromDropdown
})
$btnRunClient.Add_Click({ Sound-MenuClick; Launch-Game "client" })
$btnRunServer.Add_Click({ Sound-MenuClick; Launch-Game "server" })
$btnPushDev.Add_Click({
    Sound-MenuClick
    $chkStable.Checked = $false
    Start-PushRelease
})
$btnPushStable.Add_Click({
    Sound-MenuClick
    $chkStable.Checked = $true
    Start-PushRelease
})

$btnGithub.Add_Click({
    Sound-MenuClick
    $repo = $script:Settings.GithubRepo
    if ($repo -ne "") {
        Start-Process "https://github.com/$repo"
    } else {
        [System.Windows.Forms.MessageBox]::Show(
            "No repository configured.`nGo to File > Settings to set one.",
            "GitHub", [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Information
        )
    }
})

# Open Project button — opens project folder in Explorer
$btnOpenProject.Add_Click({
    Sound-MenuClick
    Start-Process "explorer.exe" -ArgumentList $script:ProjectDir
})

# Stop button — kills the running process and re-enables UI
$btnStop.Add_Click({
    Sound-MenuClick
    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        try { $script:Process.Kill() } catch {}
        try { $script:Process.Dispose() } catch {}
        $script:Process = $null
    }
    $timer.Stop()
    $script:StepQueue.Clear()
    $script:PendingServerBuild = $false

    Write-Output-Line "" $script:ColorRed
    Write-Output-Line ">>> BUILD STOPPED BY USER <<<" $script:ColorRed
    $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
    $progressLabel.Text = "STOPPED"
    $statusLabel.Text = "Stopped"
    $statusLabel.ForeColor = $script:ColorRed

    Set-Buttons-Enabled $true
})

$btnCopyErrors.Add_Click({
    if ($script:ErrorLines.Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show("No errors captured.", "Copy Errors",
            [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information)
        return
    }

    $text = ""
    $hasClient = $script:ClientErrorLines.Count -gt 0
    $hasServer = $script:ServerErrorLines.Count -gt 0

    if ($hasClient -and $hasServer) {
        # Segmented output for Client+Server builds
        $text += "Client Build Errors:`r`nBuild errors ($($script:ClientErrorLines.Count) lines):`r`n```````r`n"
        foreach ($line in $script:ClientErrorLines) { $text += "$line`r`n" }
        $text += "```````r`n`r`n"
        $text += "Server Build Errors:`r`nBuild errors ($($script:ServerErrorLines.Count) lines):`r`n```````r`n"
        foreach ($line in $script:ServerErrorLines) { $text += "$line`r`n" }
        $text += "```````r`n"
    } elseif ($hasClient) {
        $text += "Client Build Errors:`r`nBuild errors ($($script:ClientErrorLines.Count) lines):`r`n```````r`n"
        foreach ($line in $script:ClientErrorLines) { $text += "$line`r`n" }
        $text += "```````r`n"
    } elseif ($hasServer) {
        $text += "Server Build Errors:`r`nBuild errors ($($script:ServerErrorLines.Count) lines):`r`n```````r`n"
        foreach ($line in $script:ServerErrorLines) { $text += "$line`r`n" }
        $text += "```````r`n"
    } else {
        # Fallback: unsegmented (shouldn't happen, but safety)
        $text += "Build errors ($($script:ErrorLines.Count) lines):`r`n```````r`n"
        foreach ($line in $script:ErrorLines) { $text += "$line`r`n" }
        $text += "```````r`n"
    }

    [System.Windows.Forms.Clipboard]::SetText($text)
    $statusLabel.Text = "$($script:ErrorLines.Count) error(s) copied"
    $statusLabel.ForeColor = $script:ColorOrange
})

$btnCopyAll.Add_Click({
    if ($script:AllOutput.Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show("No output to copy.", "Copy All",
            [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information)
        return
    }
    $text = ($script:AllOutput -join "`r`n")
    [System.Windows.Forms.Clipboard]::SetText($text)
    $statusLabel.Text = "Full output copied"
    $statusLabel.ForeColor = $script:ColorDim
})

$btnClear.Add_Click({
    $outputBox.Clear()
    $script:ErrorLines.Clear()
    $script:AllOutput.Clear()
    $errorCountLabel.Text = ""
    $statusLabel.Text = "Ready"
    $statusLabel.ForeColor = $script:ColorGreen
})

# ============================================================================
# Game status + post-release polling (every 2s)
# ============================================================================

$gameTimer = New-Object System.Windows.Forms.Timer
$gameTimer.Interval = 2000
$gameTimer.Add_Tick({
    Update-GameStatus
    Update-GitChangeCount

    # Post-release cleanup
    if ($script:PendingPostRelease -and -not $script:IsRunning) {
        $script:PendingPostRelease = $false
        if ($script:BuildSucceeded) {
            Clear-ChangesForRelease
            Write-Output-Line "" $script:ColorDimmer
            Write-Output-Line "CHANGES.md cleared for next version (issues preserved)." $script:ColorPurple
        }
    }

    # Client+Server sequential build — proceed to server regardless of client result
    if ($script:PendingServerBuild -and -not $script:IsRunning) {
        $script:PendingServerBuild = $false
        Start-Build-Server-After-Client
    }
})
$gameTimer.Start()

# ============================================================================
# Initialization
# ============================================================================

Refresh-VersionDisplay
Update-GitChangeCount

$clientCheck = Join-Path $script:ClientBuildDir $script:ExeName
$serverCheck = Join-Path $script:ServerBuildDir "PerfectDarkServer.exe"
if ((Test-Path $clientCheck) -or (Test-Path $serverCheck)) { $script:BuildSucceeded = $true }
Update-RunButtons

# ============================================================================
# Cleanup
# ============================================================================

$form.Add_FormClosing({
    $timer.Stop()
    $gameTimer.Stop()
    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        try { $script:Process.Kill() } catch {}
    }
})

[void]$form.ShowDialog()
