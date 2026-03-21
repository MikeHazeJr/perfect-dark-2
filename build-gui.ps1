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
$script:IsRunning  = $false
$script:ExeName    = "pd.x86_64.exe"
$script:BuildSucceeded = $false
$script:BuildTarget    = ""
$script:GameProcess    = $null
$script:GameRunning    = $false
$script:OutputQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()

# ============================================================================
# Settings persistence
# ============================================================================

$script:Settings = @{
    GithubRepo = ""
}

function Load-Settings {
    if (Test-Path $script:SettingsFile) {
        try {
            $json = Get-Content $script:SettingsFile -Raw | ConvertFrom-Json
            if ($json.GithubRepo) { $script:Settings.GithubRepo = $json.GithubRepo }
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
}

function Save-Settings {
    $script:Settings | ConvertTo-Json | Set-Content $script:SettingsFile
}

Load-Settings

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
$form.Size = New-Object System.Drawing.Size(880, 680)
$form.StartPosition = "CenterScreen"
$form.BackColor = $script:ColorBg
$form.ForeColor = $script:ColorWhite
$form.Font = New-Object System.Drawing.Font("Segoe UI", 9)
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

$menuFile = New-Object System.Windows.Forms.ToolStripMenuItem("File")
$menuFile.ForeColor = $script:ColorWhite
$menuFile.BackColor = $script:ColorPanelBg

$menuSettings = New-Object System.Windows.Forms.ToolStripMenuItem("Settings...")
$menuSettings.ForeColor = $script:ColorText
$menuSettings.BackColor = $script:ColorPanelBg
$menuSettings.Add_Click({ Show-SettingsDialog })
[void]$menuFile.DropDownItems.Add($menuSettings)

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
$form.MainMenuStrip = $menuStrip
$form.Controls.Add($menuStrip)

# ============================================================================
# Title + Version + Status
# ============================================================================

$title = New-Object System.Windows.Forms.Label
$title.Text = "Perfect Dark PC Port"
$title.Font = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$title.ForeColor = $script:ColorGold
$title.Location = New-Object System.Drawing.Point(16, 30)
$title.AutoSize = $true
$form.Controls.Add($title)

$lblVersionDisplay = New-Object System.Windows.Forms.Label
$lblVersionDisplay.Text = ""
$lblVersionDisplay.Font = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
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
$statusLabel.Font = New-Object System.Drawing.Font("Consolas", 9)
$statusLabel.ForeColor = $script:ColorGreen
$statusLabel.Location = New-Object System.Drawing.Point(500, 34)
$statusLabel.Size = New-Object System.Drawing.Size(360, 20)
$statusLabel.TextAlign = "MiddleRight"
$form.Controls.Add($statusLabel)

# ============================================================================
# Action Bar (horizontal toolbar)
# ============================================================================

$actionBar = New-Object System.Windows.Forms.Panel
$actionBar.Location = New-Object System.Drawing.Point(10, 60)
$actionBar.Size = New-Object System.Drawing.Size(850, 46)
$actionBar.BackColor = $script:ColorPanelBg
$form.Controls.Add($actionBar)

function New-ActionButton($text, $x, $w, $color, $parent) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $text
    $btn.Location = New-Object System.Drawing.Point($x, 6)
    $btn.Size = New-Object System.Drawing.Size($w, 34)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $color
    $btn.FlatAppearance.BorderSize = 1
    $btn.ForeColor = $color
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor = "Hand"
    $btn.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $parent.Controls.Add($btn)
    return $btn
}

# Build target dropdown
$lblBuildTarget = New-Object System.Windows.Forms.Label
$lblBuildTarget.Text = "Build:"
$lblBuildTarget.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
$lblBuildTarget.ForeColor = $script:ColorDim
$lblBuildTarget.Location = New-Object System.Drawing.Point(8, 14)
$lblBuildTarget.AutoSize = $true
$actionBar.Controls.Add($lblBuildTarget)

$cmbBuildTarget = New-Object System.Windows.Forms.ComboBox
$cmbBuildTarget.Location = New-Object System.Drawing.Point(52, 10)
$cmbBuildTarget.Size = New-Object System.Drawing.Size(140, 26)
$cmbBuildTarget.DropDownStyle = "DropDownList"
$cmbBuildTarget.BackColor = $script:ColorFieldBg
$cmbBuildTarget.ForeColor = $script:ColorWhite
$cmbBuildTarget.FlatStyle = "Flat"
$cmbBuildTarget.Font = New-Object System.Drawing.Font("Consolas", 9)
[void]$cmbBuildTarget.Items.Add("Client")
[void]$cmbBuildTarget.Items.Add("Server")
[void]$cmbBuildTarget.Items.Add("Client + Server")
$cmbBuildTarget.SelectedIndex = 0
$actionBar.Controls.Add($cmbBuildTarget)

$btnBuild = New-ActionButton "Build" 200 80 $script:ColorGreen $actionBar

# Separator
$sep1 = New-Object System.Windows.Forms.Label
$sep1.Text = ""; $sep1.Location = New-Object System.Drawing.Point(290, 6)
$sep1.Size = New-Object System.Drawing.Size(2, 34); $sep1.BackColor = $script:ColorDimmer
$actionBar.Controls.Add($sep1)

# Run buttons
$btnRunClient = New-ActionButton "Run Client" 300 90 $script:ColorGreen $actionBar
$btnRunServer = New-ActionButton "Run Server" 396 90 $script:ColorOrange $actionBar

# Separator
$sep2 = New-Object System.Windows.Forms.Label
$sep2.Text = ""; $sep2.Location = New-Object System.Drawing.Point(496, 6)
$sep2.Size = New-Object System.Drawing.Size(2, 34); $sep2.BackColor = $script:ColorDimmer
$actionBar.Controls.Add($sep2)

# Open GitHub
$btnGithub = New-ActionButton "GitHub" 506 80 $script:ColorBlue $actionBar

# ============================================================================
# Push Bar (version fields + increment + push + stable)
# ============================================================================

$pushBar = New-Object System.Windows.Forms.Panel
$pushBar.Location = New-Object System.Drawing.Point(10, 108)
$pushBar.Size = New-Object System.Drawing.Size(850, 42)
$pushBar.BackColor = $script:ColorPanelBg
$form.Controls.Add($pushBar)

# Version label
$lblVerTitle = New-Object System.Windows.Forms.Label
$lblVerTitle.Text = "Version:"
$lblVerTitle.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
$lblVerTitle.ForeColor = $script:ColorDim
$lblVerTitle.Location = New-Object System.Drawing.Point(8, 12)
$lblVerTitle.AutoSize = $true
$pushBar.Controls.Add($lblVerTitle)

function New-VerField($x, $parent) {
    $txt = New-Object System.Windows.Forms.TextBox
    $txt.Location = New-Object System.Drawing.Point($x, 8)
    $txt.Size = New-Object System.Drawing.Size(42, 24)
    $txt.BackColor = $script:ColorFieldBg
    $txt.ForeColor = $script:ColorGold
    $txt.Font = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
    $txt.TextAlign = "Center"
    $txt.BorderStyle = "FixedSingle"
    $txt.MaxLength = 4
    $parent.Controls.Add($txt)
    return $txt
}

function New-IncrButton($x, $parent) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = "+"
    $btn.Location = New-Object System.Drawing.Point($x, 8)
    $btn.Size = New-Object System.Drawing.Size(24, 24)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $script:ColorDim
    $btn.FlatAppearance.BorderSize = 1
    $btn.ForeColor = $script:ColorGold
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor = "Hand"
    $btn.Font = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
    $parent.Controls.Add($btn)
    return $btn
}

# Major
$txtVerMajor = New-VerField 72 $pushBar
$btnIncMajor = New-IncrButton 116 $pushBar

# Dot separator
$lblDot1 = New-Object System.Windows.Forms.Label
$lblDot1.Text = "."
$lblDot1.Font = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.FontStyle]::Bold)
$lblDot1.ForeColor = $script:ColorDim
$lblDot1.Location = New-Object System.Drawing.Point(142, 8)
$lblDot1.AutoSize = $true
$pushBar.Controls.Add($lblDot1)

# Minor
$txtVerMinor = New-VerField 154 $pushBar
$btnIncMinor = New-IncrButton 198 $pushBar

# Dot separator
$lblDot2 = New-Object System.Windows.Forms.Label
$lblDot2.Text = "."
$lblDot2.Font = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.FontStyle]::Bold)
$lblDot2.ForeColor = $script:ColorDim
$lblDot2.Location = New-Object System.Drawing.Point(224, 8)
$lblDot2.AutoSize = $true
$pushBar.Controls.Add($lblDot2)

# Revision
$txtVerRevision = New-VerField 236 $pushBar
$btnIncRevision = New-IncrButton 280 $pushBar

# Version warning label (shows overwrite/regression warnings)
$lblVerWarning = New-Object System.Windows.Forms.Label
$lblVerWarning.Text = ""
$lblVerWarning.Font = New-Object System.Drawing.Font("Segoe UI", 8, [System.Drawing.FontStyle]::Bold)
$lblVerWarning.ForeColor = $script:ColorRed
$lblVerWarning.Location = New-Object System.Drawing.Point(312, 2)
$lblVerWarning.Size = New-Object System.Drawing.Size(220, 38)
$pushBar.Controls.Add($lblVerWarning)

# Stable checkbox
$chkStable = New-Object System.Windows.Forms.CheckBox
$chkStable.Text = "Stable"
$chkStable.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
$chkStable.ForeColor = $script:ColorGreen
$chkStable.Location = New-Object System.Drawing.Point(546, 10)
$chkStable.AutoSize = $true
$chkStable.BackColor = $script:ColorPanelBg
$pushBar.Controls.Add($chkStable)

# Push button
$btnPush = New-Object System.Windows.Forms.Button
$btnPush.Text = "Push to GitHub"
$btnPush.Location = New-Object System.Drawing.Point(626, 4)
$btnPush.Size = New-Object System.Drawing.Size(130, 34)
$btnPush.FlatStyle = "Flat"
$btnPush.FlatAppearance.BorderColor = $script:ColorPurple
$btnPush.FlatAppearance.BorderSize = 1
$btnPush.ForeColor = $script:ColorPurple
$btnPush.BackColor = $script:ColorFieldBg
$btnPush.Cursor = "Hand"
$btnPush.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
$pushBar.Controls.Add($btnPush)

# Separator between push bar and action bar
$sep3 = New-Object System.Windows.Forms.Label
$sep3.Text = ""; $sep3.Location = New-Object System.Drawing.Point(536, 4)
$sep3.Size = New-Object System.Drawing.Size(2, 34); $sep3.BackColor = $script:ColorDimmer
$pushBar.Controls.Add($sep3)

# ============================================================================
# Console output
# ============================================================================

$outputBox = New-Object System.Windows.Forms.RichTextBox
$outputBox.Location = New-Object System.Drawing.Point(10, 156)
$outputBox.Size = New-Object System.Drawing.Size(850, 386)
$outputBox.BackColor = $script:ColorConsoleBg
$outputBox.ForeColor = $script:ColorText
$outputBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$outputBox.ReadOnly = $true
$outputBox.WordWrap = $false
$outputBox.ScrollBars = "Both"
$outputBox.BorderStyle = "None"
$form.Controls.Add($outputBox)

# ============================================================================
# Utility bar (below console)
# ============================================================================

$utilPanel = New-Object System.Windows.Forms.Panel
$utilPanel.Location = New-Object System.Drawing.Point(10, 546)
$utilPanel.Size = New-Object System.Drawing.Size(850, 34)
$utilPanel.BackColor = $script:ColorPanelBg
$form.Controls.Add($utilPanel)

function New-UtilButton($text, $x, $w, $color) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $text
    $btn.Location = New-Object System.Drawing.Point($x, 2)
    $btn.Size = New-Object System.Drawing.Size($w, 32)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $color
    $btn.FlatAppearance.BorderSize = 1
    $btn.ForeColor = $color
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor = "Hand"
    $btn.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $utilPanel.Controls.Add($btn)
    return $btn
}

$btnCopyErrors = New-UtilButton "Copy Errors"  4 110 $script:ColorRed
$btnCopyAll    = New-UtilButton "Copy All"   120  90 $script:ColorDim
$btnClear      = New-UtilButton "Clear"      216  70 ([System.Drawing.Color]::FromArgb(120,120,120))

$errorCountLabel = New-Object System.Windows.Forms.Label
$errorCountLabel.Text = ""
$errorCountLabel.Font = New-Object System.Drawing.Font("Consolas", 8, [System.Drawing.FontStyle]::Bold)
$errorCountLabel.ForeColor = $script:ColorRed
$errorCountLabel.Location = New-Object System.Drawing.Point(300, 8)
$errorCountLabel.Size = New-Object System.Drawing.Size(540, 20)
$errorCountLabel.TextAlign = "MiddleRight"
$utilPanel.Controls.Add($errorCountLabel)

# ============================================================================
# Progress bar
# ============================================================================

$progressPanel = New-Object System.Windows.Forms.Panel
$progressPanel.Location = New-Object System.Drawing.Point(10, 586)
$progressPanel.Size = New-Object System.Drawing.Size(850, 22)
$progressPanel.BackColor = $script:ColorPanelBg
$form.Controls.Add($progressPanel)

$progressFill = New-Object System.Windows.Forms.Panel
$progressFill.Location = New-Object System.Drawing.Point(0, 0)
$progressFill.Size = New-Object System.Drawing.Size(0, 22)
$progressFill.BackColor = $script:ColorBlue
$progressPanel.Controls.Add($progressFill)

$progressLabel = New-Object System.Windows.Forms.Label
$progressLabel.Text = ""
$progressLabel.Font = New-Object System.Drawing.Font("Consolas", 8, [System.Drawing.FontStyle]::Bold)
$progressLabel.ForeColor = $script:ColorWhite
$progressLabel.BackColor = [System.Drawing.Color]::Transparent
$progressLabel.Location = New-Object System.Drawing.Point(4, 3)
$progressLabel.AutoSize = $true
$progressPanel.Controls.Add($progressLabel)
$progressLabel.BringToFront()

$script:BuildPercent = 0
$script:HasErrors = $false

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

function Set-Buttons-Enabled($enabled) {
    $script:IsRunning = !$enabled
    $btnBuild.Enabled = $enabled
    $btnPush.Enabled = $enabled
    $cmbBuildTarget.Enabled = $enabled
    Update-RunButtons
}

function Update-RunButtons {
    $clientExe = Join-Path $script:ClientBuildDir $script:ExeName
    $serverExe = Join-Path $script:ServerBuildDir "pd-server.x86_64.exe"

    $canRunClient = (-not $script:IsRunning) -and (Test-Path $clientExe)
    $canRunServer = (-not $script:IsRunning) -and (Test-Path $serverExe)

    $btnRunClient.Enabled = $canRunClient
    $btnRunServer.Enabled = $canRunServer

    $btnRunClient.ForeColor = if ($canRunClient) { $script:ColorGreen } else { $script:ColorDisabled }
    $btnRunServer.ForeColor = if ($canRunServer) { $script:ColorOrange } else { $script:ColorDisabled }
    $btnBuild.ForeColor = if (-not $script:IsRunning) { $script:ColorGreen } else { $script:ColorDisabled }
    $btnPush.ForeColor = if (-not $script:IsRunning) { $script:ColorPurple } else { $script:ColorDisabled }
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
                $releases += @{ Tag = $r.Tag; Major = [int]$r.Major; Minor = [int]$r.Minor; Revision = [int]$r.Revision }
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
            $data.releases += @{ Tag = $r.Tag; Major = $r.Major; Minor = $r.Minor; Revision = $r.Revision }
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
                    if ($tag -match '^v?(\d+)\.(\d+)\.(\d+)') {
                        $releases += @{
                            Tag = $tag
                            Major = [int]$Matches[1]; Minor = [int]$Matches[2]; Revision = [int]$Matches[3]
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

    return $script:GhReleaseCache
}

function Check-VersionWarning {
    $ver = Get-PushBarVersion
    $tag = "v$($ver.Major).$($ver.Minor).$($ver.Revision)"
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

# Load disk cache on startup so version warnings work immediately (even offline)
Load-ReleaseCache

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
# Auto-commit (runs before every build and push)
# ============================================================================

function Auto-Commit {
    $savedEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    $status = git -C $script:ProjectDir status --porcelain 2>$null
    if (!$status) {
        Write-Output-Line "  No uncommitted changes." $script:ColorDim
        $ErrorActionPreference = $savedEAP
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
            if (-not $script:HasErrors) {
                $script:HasErrors = $true
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
            }
        }

        if ($text -match '^\[\s*(\d+)%\]') {
            $pct = [int]$Matches[1]
            if ($pct -ge $script:BuildPercent) {
                $script:BuildPercent = $pct
                $fillWidth = [math]::Floor(($pct / 100.0) * 850)
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
            $progressFill.Size = New-Object System.Drawing.Size(850, 22)
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
            $progressLabel.Text = "FAILED - $($script:CurrentStep)"
            Write-Output-Line "" $script:ColorRed
            Write-Output-Line ">>> $($script:CurrentStep) FAILED (exit code $exitCode) after ${totalElapsed}s <<<" $script:ColorRed
            $statusLabel.Text = "FAILED: $($script:CurrentStep)"
            $statusLabel.ForeColor = $script:ColorRed
            $script:BuildSucceeded = $false
            $script:StepQueue.Clear()
            Set-Buttons-Enabled $true
            return
        }

        $progressFill.Size = New-Object System.Drawing.Size(850, 22)
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
            if ($script:HasErrors) {
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
                $progressLabel.Text = "COMPLETE (with errors)"
            } else {
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(0, 191, 96)
                $progressLabel.Text = "BUILD COMPLETE"
            }
            Copy-AddinFiles
            $statusLabel.Text = "Build Complete"
            $statusLabel.ForeColor = $script:ColorGreen
            $script:BuildSucceeded = $true
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
    $modsDir = Join-Path $script:AddinDir "mods"
    if (Test-Path $modsDir) {
        Copy-Item $modsDir -Destination $script:BuildDir -Recurse -Force
        Write-Output-Line "  mods\" $script:ColorPurple
        $copied++
    }
    Write-Output-Line "Copied $copied items." $script:ColorGreen
}

# ============================================================================
# Game launch
# ============================================================================

function Launch-Game($mode) {
    if ($script:IsRunning) { return }

    if ($mode -eq "server") {
        $launchDir = $script:ServerBuildDir
        $launchExe = Join-Path $launchDir "pd-server.x86_64.exe"
        $gameArgs = "--log"
        $label = "Dedicated Server"
        $labelColor = $script:ColorOrange
    } else {
        $launchDir = $script:ClientBuildDir
        $launchExe = Join-Path $launchDir $script:ExeName
        $gameArgs = "--moddir mods/mod_allinone --gexmoddir mods/mod_gex --kakarikomoddir mods/mod_kakariko --darknoonmoddir mods/mod_dark_noon --goldfinger64moddir mods/mod_goldfinger_64 --log"
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

    $script:GameProcess = Start-Process -FilePath $launchExe -ArgumentList $gameArgs -WorkingDirectory $launchDir -PassThru
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

    if (Test-Path $script:BuildDir) {
        Write-Header "Clean: $($script:BuildDir | Split-Path -Leaf)"
        Remove-Item -Path $script:BuildDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Output-Line "  Cleared build directory." $script:ColorDim
    } else {
        Write-Header "Clean Build"
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
    $confirmMsg += "  - Create git tag v$verStr`n"
    $confirmMsg += "  - Push to GitHub`n"
    $confirmMsg += "  - Create GitHub Release ($channel)`n"

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
    $dlg.Text = "Build Tool Settings"
    $dlg.Size = New-Object System.Drawing.Size(500, 260)
    $dlg.StartPosition = "CenterParent"
    $dlg.BackColor = $script:ColorBg
    $dlg.ForeColor = $script:ColorWhite
    $dlg.FormBorderStyle = "FixedDialog"
    $dlg.MaximizeBox = $false
    $dlg.MinimizeBox = $false

    # GitHub Token Status
    $lblTokenTitle = New-Object System.Windows.Forms.Label
    $lblTokenTitle.Text = "GitHub CLI Authentication:"
    $lblTokenTitle.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $lblTokenTitle.ForeColor = $script:ColorDim
    $lblTokenTitle.Location = New-Object System.Drawing.Point(16, 16)
    $lblTokenTitle.AutoSize = $true
    $dlg.Controls.Add($lblTokenTitle)

    $lblTokenStatus = New-Object System.Windows.Forms.Label
    $lblTokenStatus.Font = New-Object System.Drawing.Font("Consolas", 9)
    $lblTokenStatus.Location = New-Object System.Drawing.Point(16, 38)
    $lblTokenStatus.Size = New-Object System.Drawing.Size(450, 40)
    $dlg.Controls.Add($lblTokenStatus)

    # Check gh auth status
    try {
        $savedEAP = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $authOut = gh auth status 2>&1
        $ErrorActionPreference = $savedEAP
        $authStr = ($authOut | ForEach-Object { $_.ToString() }) -join "`n"

        if ($authStr -match 'Logged in to') {
            $lblTokenStatus.Text = "Active - authenticated with GitHub"
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
    $btnAuthLogin.Location = New-Object System.Drawing.Point(16, 82)
    $btnAuthLogin.Size = New-Object System.Drawing.Size(150, 30)
    $btnAuthLogin.FlatStyle = "Flat"
    $btnAuthLogin.FlatAppearance.BorderColor = $script:ColorPurple
    $btnAuthLogin.ForeColor = $script:ColorPurple
    $btnAuthLogin.BackColor = $script:ColorFieldBg
    $btnAuthLogin.Cursor = "Hand"
    $btnAuthLogin.Add_Click({
        Start-Process "cmd.exe" -ArgumentList "/k gh auth login"
    })
    $dlg.Controls.Add($btnAuthLogin)

    # Repository
    $lblRepoTitle = New-Object System.Windows.Forms.Label
    $lblRepoTitle.Text = "GitHub Repository (owner/repo):"
    $lblRepoTitle.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $lblRepoTitle.ForeColor = $script:ColorDim
    $lblRepoTitle.Location = New-Object System.Drawing.Point(16, 126)
    $lblRepoTitle.AutoSize = $true
    $dlg.Controls.Add($lblRepoTitle)

    $txtRepo = New-Object System.Windows.Forms.TextBox
    $txtRepo.Text = $script:Settings.GithubRepo
    $txtRepo.Location = New-Object System.Drawing.Point(16, 148)
    $txtRepo.Size = New-Object System.Drawing.Size(350, 24)
    $txtRepo.BackColor = $script:ColorFieldBg
    $txtRepo.ForeColor = $script:ColorWhite
    $txtRepo.Font = New-Object System.Drawing.Font("Consolas", 9)
    $txtRepo.BorderStyle = "FixedSingle"
    $dlg.Controls.Add($txtRepo)

    $btnSaveSettings = New-Object System.Windows.Forms.Button
    $btnSaveSettings.Text = "Save"
    $btnSaveSettings.Location = New-Object System.Drawing.Point(376, 146)
    $btnSaveSettings.Size = New-Object System.Drawing.Size(80, 28)
    $btnSaveSettings.FlatStyle = "Flat"
    $btnSaveSettings.FlatAppearance.BorderColor = $script:ColorGold
    $btnSaveSettings.ForeColor = $script:ColorGold
    $btnSaveSettings.BackColor = $script:ColorFieldBg
    $btnSaveSettings.Cursor = "Hand"
    $btnSaveSettings.Add_Click({
        $script:Settings.GithubRepo = $txtRepo.Text.Trim()
        Save-Settings
        $lblRepoSaved = New-Object System.Windows.Forms.Label
        Write-Output-Line "  Settings saved: repo=$($script:Settings.GithubRepo)" $script:ColorGreen
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
    try { $val = [int]$txtVerMajor.Text } catch { $val = 0 }
    $txtVerMajor.Text = "$($val + 1)"
    $txtVerMinor.Text = "0"
    $txtVerRevision.Text = "0"
    Check-VersionWarning
})
$btnIncMinor.Add_Click({
    try { $val = [int]$txtVerMinor.Text } catch { $val = 0 }
    $txtVerMinor.Text = "$($val + 1)"
    $txtVerRevision.Text = "0"
    Check-VersionWarning
})
$btnIncRevision.Add_Click({
    try { $val = [int]$txtVerRevision.Text } catch { $val = 0 }
    $txtVerRevision.Text = "$($val + 1)"
    Check-VersionWarning
})

# Re-check warning when version fields are edited manually
$txtVerMajor.Add_TextChanged({ Check-VersionWarning })
$txtVerMinor.Add_TextChanged({ Check-VersionWarning })
$txtVerRevision.Add_TextChanged({ Check-VersionWarning })

$btnBuild.Add_Click({ Start-BuildFromDropdown })
$btnRunClient.Add_Click({ Launch-Game "client" })
$btnRunServer.Add_Click({ Launch-Game "server" })
$btnPush.Add_Click({ Start-PushRelease })

$btnGithub.Add_Click({
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

$btnCopyErrors.Add_Click({
    if ($script:ErrorLines.Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show("No errors captured.", "Copy Errors",
            [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information)
        return
    }
    $text = "Build errors ($($script:ErrorLines.Count) lines):`r`n```````r`n"
    foreach ($line in $script:ErrorLines) { $text += "$line`r`n" }
    $text += "```````r`n"
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

    # Post-release cleanup
    if ($script:PendingPostRelease -and -not $script:IsRunning) {
        $script:PendingPostRelease = $false
        if ($script:BuildSucceeded) {
            Clear-ChangesForRelease
            Write-Output-Line "" $script:ColorDimmer
            Write-Output-Line "CHANGES.md cleared for next version (issues preserved)." $script:ColorPurple
        }
    }

    # Client+Server sequential build
    if ($script:PendingServerBuild -and -not $script:IsRunning -and $script:BuildSucceeded) {
        $script:PendingServerBuild = $false
        Start-Build "server"
    }
})
$gameTimer.Start()

# ============================================================================
# Initialization
# ============================================================================

Refresh-VersionDisplay

$clientCheck = Join-Path $script:ClientBuildDir $script:ExeName
$serverCheck = Join-Path $script:ServerBuildDir "pd-server.x86_64.exe"
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
