Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# --- Hide the PowerShell console window so only the GUI appears ---
Add-Type -Language CSharp @"
using System;
using System.Runtime.InteropServices;

public class ConsoleSuppressor
{
    [DllImport("kernel32.dll")]
    public static extern IntPtr GetConsoleWindow();

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public const int SW_HIDE = 0;

    public static void HideConsole()
    {
        IntPtr hwnd = GetConsoleWindow();
        if (hwnd != IntPtr.Zero) { ShowWindow(hwnd, SW_HIDE); }
    }
}
"@
[ConsoleSuppressor]::HideConsole()

# --- Dark menu color table ---
Add-Type -Language CSharp -ReferencedAssemblies System.Windows.Forms, System.Drawing @"
using System;
using System.Drawing;
using System.Windows.Forms;

public class DarkMenuColors : ProfessionalColorTable
{
    private Color bg        = Color.FromArgb(40, 40, 40);
    private Color border    = Color.FromArgb(70, 70, 70);
    private Color highlight = Color.FromArgb(60, 60, 60);
    private Color sep       = Color.FromArgb(70, 70, 70);

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
    public override Color SeparatorDark                      { get { return sep; } }
    public override Color SeparatorLight                     { get { return bg; } }
    public override Color CheckBackground                    { get { return highlight; } }
    public override Color CheckSelectedBackground            { get { return highlight; } }
    public override Color CheckPressedBackground             { get { return highlight; } }
}
"@

# ============================================================================
# Configuration
# ============================================================================

$script:ProjectDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:ClientBuildDir = Join-Path $script:ProjectDir "build\client"
$script:ServerBuildDir = Join-Path $script:ProjectDir "build\server"
$script:QcFile         = Join-Path $script:ProjectDir "context\qc-tests.md"
$script:SettingsFile   = Join-Path $script:ProjectDir ".playtest-settings.json"

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
# Custom font: Handel Gothic
# ============================================================================

$script:FontCollection = New-Object System.Drawing.Text.PrivateFontCollection
$fontPath = Join-Path $script:ProjectDir "fonts\Menus\Handel Gothic Regular\Handel Gothic Regular.otf"
$script:UseHandelGothic = $false
if (Test-Path $fontPath) {
    try {
        $script:FontCollection.AddFontFile($fontPath)
        $script:HandelFamily = $script:FontCollection.Families[0]
        $script:UseHandelGothic = $true
    } catch {}
}

function New-UIFont($size, [switch]$Bold) {
    $style = if ($Bold) { [System.Drawing.FontStyle]::Bold } else { [System.Drawing.FontStyle]::Regular }
    if ($script:UseHandelGothic) {
        return New-Object System.Drawing.Font($script:HandelFamily, $size, $style, [System.Drawing.GraphicsUnit]::Point)
    }
    return New-Object System.Drawing.Font("Segoe UI", $size, $style)
}

# ============================================================================
# Colors (exact match with build-gui.ps1)
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

# Status colors for QC rows
$script:ColorPass      = [System.Drawing.Color]::FromArgb(50, 220, 120)   # green
$script:ColorFail      = [System.Drawing.Color]::FromArgb(255, 100, 100)  # red
$script:ColorSkip      = [System.Drawing.Color]::FromArgb(160, 160, 160)  # dim

# ============================================================================
# Main Form
# ============================================================================

$form = New-Object System.Windows.Forms.Form
$form.Text = "Perfect Dark - Playtest Dashboard"
$form.Size = New-Object System.Drawing.Size(1000, 680)
$form.StartPosition = "CenterScreen"
$form.BackColor = $script:ColorBg
$form.ForeColor = $script:ColorWhite
$form.Font = New-UIFont 10
$form.FormBorderStyle = "FixedSingle"
$form.MaximizeBox = $false
$form.ShowInTaskbar = $true
$form.TopLevel = $true

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
    (New-Object DarkMenuColors)
)

$menuFile = New-Object System.Windows.Forms.ToolStripMenuItem("File")
$menuFile.ForeColor = $script:ColorWhite
$menuFile.BackColor = $script:ColorPanelBg

$menuOpenQc = New-Object System.Windows.Forms.ToolStripMenuItem("Edit qc-tests.md")
$menuOpenQc.ForeColor = $script:ColorText
$menuOpenQc.BackColor = $script:ColorPanelBg
$menuOpenQc.Add_Click({
    if (Test-Path $script:QcFile) {
        Start-Process "notepad.exe" -ArgumentList $script:QcFile
    }
})
[void]$menuFile.DropDownItems.Add($menuOpenQc)

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
# Title bar row
# ============================================================================

$lblTitle = New-Object System.Windows.Forms.Label
$lblTitle.Text = "Perfect Dark - Playtest Dashboard"
$lblTitle.Font = New-UIFont 15 -Bold
$lblTitle.ForeColor = $script:ColorGold
$lblTitle.Location = New-Object System.Drawing.Point(16, 30)
$lblTitle.AutoSize = $true
$form.Controls.Add($lblTitle)

$lblStatus = New-Object System.Windows.Forms.Label
$lblStatus.Text = "Ready"
$lblStatus.Font = New-Object System.Drawing.Font("Consolas", 10)
$lblStatus.ForeColor = $script:ColorGreen
$lblStatus.Location = New-Object System.Drawing.Point(600, 34)
$lblStatus.Size = New-Object System.Drawing.Size(380, 20)
$lblStatus.TextAlign = "MiddleRight"
$form.Controls.Add($lblStatus)

# ============================================================================
# Layout: Left sidebar (220px) + main content area
# ============================================================================

$sideW   = 220
$mainX   = $sideW + 16
$mainW   = 1000 - $mainX - 10
$mainY   = 60
$mainH   = 580

# --- Left Sidebar ---
$sidePanel = New-Object System.Windows.Forms.Panel
$sidePanel.Location = New-Object System.Drawing.Point(10, 60)
$sidePanel.Size = New-Object System.Drawing.Size($sideW, 580)
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

# --- RUN section ---
$lblRunSection = New-Object System.Windows.Forms.Label
$lblRunSection.Text = "RUN"
$lblRunSection.Font = New-UIFont 9 -Bold
$lblRunSection.ForeColor = $script:ColorDim
$lblRunSection.Location = New-Object System.Drawing.Point(8, 8)
$lblRunSection.AutoSize = $true
$sidePanel.Controls.Add($lblRunSection)

$btnRunClient = New-SideButton "Run Client" 26 204 30 $script:ColorGreen $sidePanel
$btnRunServer = New-SideButton "Run Server" 60 204 30 $script:ColorOrange $sidePanel

# --- Separator ---
$sep1 = New-Object System.Windows.Forms.Label
$sep1.Text = ""
$sep1.Location = New-Object System.Drawing.Point(8, 96)
$sep1.Size = New-Object System.Drawing.Size(204, 1)
$sep1.BackColor = $script:ColorDimmer
$sidePanel.Controls.Add($sep1)

# --- OPEN section ---
$lblOpenSection = New-Object System.Windows.Forms.Label
$lblOpenSection.Text = "OPEN"
$lblOpenSection.Font = New-UIFont 9 -Bold
$lblOpenSection.ForeColor = $script:ColorDim
$lblOpenSection.Location = New-Object System.Drawing.Point(8, 102)
$lblOpenSection.AutoSize = $true
$sidePanel.Controls.Add($lblOpenSection)

$btnOpenBuild   = New-SideButton "Build Folder"   120 204 28 $script:ColorDim $sidePanel
$btnOpenProject = New-SideButton "Project Folder" 152 204 28 $script:ColorDim $sidePanel
$btnGithub      = New-SideButton "Open GitHub"    184 204 28 $script:ColorBlue $sidePanel

# --- Separator ---
$sep2 = New-Object System.Windows.Forms.Label
$sep2.Text = ""
$sep2.Location = New-Object System.Drawing.Point(8, 218)
$sep2.Size = New-Object System.Drawing.Size(204, 1)
$sep2.BackColor = $script:ColorDimmer
$sidePanel.Controls.Add($sep2)

# --- GIT section ---
$lblGitSection = New-Object System.Windows.Forms.Label
$lblGitSection.Text = "GIT"
$lblGitSection.Font = New-UIFont 9 -Bold
$lblGitSection.ForeColor = $script:ColorDim
$lblGitSection.Location = New-Object System.Drawing.Point(8, 224)
$lblGitSection.AutoSize = $true
$sidePanel.Controls.Add($lblGitSection)

$script:GitChangeCount = 0
$btnCommit = New-SideButton "Commit 0 changes" 242 204 30 $script:ColorPurple $sidePanel
$btnCommit.Enabled = $false
$btnCommit.ForeColor = $script:ColorDisabled

# --- Separator ---
$sep3 = New-Object System.Windows.Forms.Label
$sep3.Text = ""
$sep3.Location = New-Object System.Drawing.Point(8, 278)
$sep3.Size = New-Object System.Drawing.Size(204, 1)
$sep3.BackColor = $script:ColorDimmer
$sidePanel.Controls.Add($sep3)

# --- PUSH section ---
$lblPushSection = New-Object System.Windows.Forms.Label
$lblPushSection.Text = "PUSH"
$lblPushSection.Font = New-UIFont 9 -Bold
$lblPushSection.ForeColor = $script:ColorDim
$lblPushSection.Location = New-Object System.Drawing.Point(8, 284)
$lblPushSection.AutoSize = $true
$sidePanel.Controls.Add($lblPushSection)

$btnPushDev    = New-SideButton "Dev"    302 98  28 $script:ColorOrange $sidePanel
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

# ============================================================================
# Main Content Area: QC Tests Panel
# ============================================================================

# Panel header bar
$qcHeaderPanel = New-Object System.Windows.Forms.Panel
$qcHeaderPanel.Location = New-Object System.Drawing.Point($mainX, $mainY)
$qcHeaderPanel.Size = New-Object System.Drawing.Size($mainW, 36)
$qcHeaderPanel.BackColor = $script:ColorPanelBg
$form.Controls.Add($qcHeaderPanel)

$lblQcTitle = New-Object System.Windows.Forms.Label
$lblQcTitle.Text = "QC TEST CHECKLIST"
$lblQcTitle.Font = New-UIFont 11 -Bold
$lblQcTitle.ForeColor = $script:ColorGold
$lblQcTitle.Location = New-Object System.Drawing.Point(10, 8)
$lblQcTitle.AutoSize = $true
$qcHeaderPanel.Controls.Add($lblQcTitle)

$btnRefresh = New-Object System.Windows.Forms.Button
$btnRefresh.Text = "Refresh"
$btnRefresh.Location = New-Object System.Drawing.Point(($mainW - 130), 4)
$btnRefresh.Size = New-Object System.Drawing.Size(70, 26)
$btnRefresh.FlatStyle = "Flat"
$btnRefresh.FlatAppearance.BorderColor = $script:ColorDim
$btnRefresh.FlatAppearance.BorderSize = 1
$btnRefresh.ForeColor = $script:ColorDim
$btnRefresh.BackColor = $script:ColorFieldBg
$btnRefresh.Cursor = "Hand"
$btnRefresh.Font = New-UIFont 9 -Bold
$qcHeaderPanel.Controls.Add($btnRefresh)

$btnResetAll = New-Object System.Windows.Forms.Button
$btnResetAll.Text = "Reset All"
$btnResetAll.Location = New-Object System.Drawing.Point(($mainW - 56), 4)
$btnResetAll.Size = New-Object System.Drawing.Size(50, 26)
$btnResetAll.FlatStyle = "Flat"
$btnResetAll.FlatAppearance.BorderColor = $script:ColorRed
$btnResetAll.FlatAppearance.BorderSize = 1
$btnResetAll.ForeColor = $script:ColorRed
$btnResetAll.BackColor = $script:ColorFieldBg
$btnResetAll.Cursor = "Hand"
$btnResetAll.Font = New-UIFont 9 -Bold
$qcHeaderPanel.Controls.Add($btnResetAll)

# Summary label (pass/total count)
$lblQcSummary = New-Object System.Windows.Forms.Label
$lblQcSummary.Text = ""
$lblQcSummary.Font = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
$lblQcSummary.ForeColor = $script:ColorDim
$lblQcSummary.Location = New-Object System.Drawing.Point(200, 9)
$lblQcSummary.AutoSize = $true
$qcHeaderPanel.Controls.Add($lblQcSummary)

# Scrollable panel for QC rows
$qcScrollPanel = New-Object System.Windows.Forms.Panel
$qcScrollPanel.Location = New-Object System.Drawing.Point($mainX, ($mainY + 36))
$qcScrollPanel.Size = New-Object System.Drawing.Size($mainW, ($mainH - 36))
$qcScrollPanel.BackColor = $script:ColorConsoleBg
$qcScrollPanel.AutoScroll = $true
$form.Controls.Add($qcScrollPanel)

# ============================================================================
# QC file parsing and rendering
# ============================================================================

# Status emoji constants (ASCII-safe representation used in .md file)
# The .md file uses Unicode emoji: checked box = [x] style markers
# We parse: [x] or (x) = pass, [ ] or ( ) = unchecked, [-] = skip/na
# For display we use text labels.

# Recognized status tokens in the Status column:
#   pass/checked  :=  [x]  or  (x)  or  checkmark emoji
#   fail          :=  [!]  or  FAIL
#   skip/na       :=  [-]  or  N/A
#   unchecked     :=  [ ]  or  ( )  or  empty

$script:QcSections  = @()   # list of section objects
$script:QcAllRows   = @()   # flat list of all row objects for summary

# Each section: @{ Title = "..."; Rows = @(...) }
# Each row:     @{ Num = "1"; Test = "..."; Expected = "..."; StatusRaw = "..."; Status = "pass|fail|skip|pending"; SectionTitle = "..." }

function Parse-QcStatus($raw) {
    # Unicode emoji in the file: square box empty, checked, x mark
    # We match on the common patterns Mike uses
    $r = $raw.Trim()
    if ($r -match '^\[x\]$' -or $r -match '^\(x\)$' -or $r -eq '[+]' -or $r -match 'PASS') { return "pass" }
    if ($r -match '^\[!\]$' -or $r -match 'FAIL' -or $r -match 'fail') { return "fail" }
    if ($r -match '^\[-\]$' -or $r -match '^N/?A$') { return "skip" }
    # Unicode empty checkbox (U+2B1C, U+25A1, etc) - just match non-empty non-pass non-fail
    # The file uses the Unicode box character for pending: treat anything else as pending
    return "pending"
}

function StatusToEmoji($status) {
    switch ($status) {
        "pass"    { return "[PASS]" }
        "fail"    { return "[FAIL]" }
        "skip"    { return "[N/A ]" }
        default   { return "[    ]" }
    }
}

function StatusToMdMarker($status) {
    # Write back to .md file using the same emoji style that was there originally
    # We preserve whatever was there for non-pending rows; for changed rows we use standard markers
    switch ($status) {
        "pass"    { return "[x]" }
        "fail"    { return "[!]" }
        "skip"    { return "[-]" }
        default   { return "[ ]" }
    }
}

function Load-QcFile {
    $script:QcSections = @()
    $script:QcAllRows  = @()

    if (-not (Test-Path $script:QcFile)) {
        return
    }

    $lines = Get-Content $script:QcFile -Encoding UTF8

    $currentSection = $null

    foreach ($line in $lines) {
        # Section header: ## Some Title
        if ($line -match '^##\s+(.+)$') {
            $secTitle = $Matches[1].Trim()
            $currentSection = @{ Title = $secTitle; Rows = @() }
            $script:QcSections += $currentSection
            continue
        }

        # Table data row: | num | test | expected | status |
        # Skip separator rows (|---|...) and header rows (| # | Test |...)
        if ($line -match '^\|') {
            # Skip separator lines
            if ($line -match '^\|\s*[-:]+') { continue }

            # Split on | - trim whitespace from each cell
            $cells = $line -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }

            if ($cells.Count -lt 4) { continue }

            $numCell      = $cells[0]
            $testCell     = $cells[1]
            $expectedCell = $cells[2]
            $statusCell   = $cells[3]

            # Skip the header row (# | Test | Expected | Status)
            if ($numCell -eq '#' -or $testCell -eq 'Test') { continue }

            # Must look like a numbered row
            if ($numCell -notmatch '^\d+$') { continue }

            $statusParsed = Parse-QcStatus $statusCell

            $row = @{
                Num           = $numCell
                Test          = $testCell
                Expected      = $expectedCell
                StatusRaw     = $statusCell
                Status        = $statusParsed
                SectionTitle  = if ($currentSection) { $currentSection.Title } else { "" }
            }

            $script:QcAllRows += $row
            if ($currentSection) {
                $currentSection.Rows += $row
            }
        }
    }
}

function Save-QcFile {
    if (-not (Test-Path $script:QcFile)) { return }

    # Read raw lines, then rewrite status cells in-place
    $lines = Get-Content $script:QcFile -Encoding UTF8

    # Build a lookup: num -> status
    $statusMap = @{}
    foreach ($row in $script:QcAllRows) {
        $statusMap[$row.Num] = $row.Status
    }

    $outLines = @()
    foreach ($line in $lines) {
        if ($line -match '^\|' -and $line -notmatch '^\|\s*[-:]+') {
            $cells = $line -split '\|'
            # cells[0] is empty (before first |), cells[1..n] are data, cells[-1] empty after last |
            if ($cells.Count -ge 5) {
                $numCell = $cells[1].Trim()
                if ($numCell -match '^\d+$' -and $statusMap.ContainsKey($numCell)) {
                    $newMarker = StatusToMdMarker $statusMap[$numCell]
                    # Replace last data cell (status) - cells[-2] when trailing | creates empty last
                    $lastDataIdx = $cells.Count - 2
                    # Keep leading/trailing space consistent
                    $cells[$lastDataIdx] = " $newMarker "
                    $line = $cells -join '|'
                }
            }
        }
        $outLines += $line
    }

    Set-Content -Path $script:QcFile -Value $outLines -Encoding UTF8
}

# ============================================================================
# QC UI rendering
# ============================================================================

$script:QcCheckboxes = @{}   # num -> checkbox control
$script:QcStatusDrops = @{}  # num -> combobox

function Get-StatusColor($status) {
    switch ($status) {
        "pass"    { return $script:ColorPass }
        "fail"    { return $script:ColorFail }
        "skip"    { return $script:ColorSkip }
        default   { return $script:ColorText }
    }
}

function Update-QcSummary {
    $total  = $script:QcAllRows.Count
    $passed = ($script:QcAllRows | Where-Object { $_.Status -eq "pass" }).Count
    $failed = ($script:QcAllRows | Where-Object { $_.Status -eq "fail" }).Count
    $skip   = ($script:QcAllRows | Where-Object { $_.Status -eq "skip" }).Count
    $pend   = $total - $passed - $failed - $skip

    $lblQcSummary.Text = "Pass: $passed  Fail: $failed  Skip: $skip  Pending: $pend  Total: $total"

    if ($failed -gt 0) {
        $lblQcSummary.ForeColor = $script:ColorFail
    } elseif ($passed -eq $total -and $total -gt 0) {
        $lblQcSummary.ForeColor = $script:ColorPass
    } else {
        $lblQcSummary.ForeColor = $script:ColorDim
    }
}

function Render-QcPanel {
    $qcScrollPanel.SuspendLayout()

    # Remove old controls
    $toDispose = @($qcScrollPanel.Controls)
    $qcScrollPanel.Controls.Clear()
    foreach ($ctrl in $toDispose) { try { $ctrl.Dispose() } catch {} }

    $script:QcCheckboxes  = @{}
    $script:QcStatusDrops = @{}

    $yPos     = 6
    $panelW   = $mainW - 22   # account for scrollbar

    $colNumW  = 36
    $colStW   = 90
    $colExpW  = 200
    $colTestW = $panelW - $colNumW - $colStW - $colExpW - 20
    $minRowH  = 30
    $rowPad   = 10   # top + bottom padding for row content

    # Pre-create fonts for measuring and display (shared across all rows)
    $fmtFlags    = [System.Windows.Forms.TextFormatFlags]::WordBreak -bor [System.Windows.Forms.TextFormatFlags]::Left
    $testFont    = New-UIFont 9
    $expFont     = New-UIFont 8
    $secBoldFont = New-UIFont 9 -Bold

    foreach ($section in $script:QcSections) {
        if ($section.Rows.Count -eq 0) { continue }

        # Section header - measure text height so long titles don't get clipped
        $secMeasureSize = New-Object System.Drawing.Size(($panelW - 16), 65535)
        $secSz = [System.Windows.Forms.TextRenderer]::MeasureText(
            $section.Title, $secBoldFont, $secMeasureSize, $fmtFlags)
        $secH = [Math]::Max(22, [int]$secSz.Height + 8)

        $secLbl = New-Object System.Windows.Forms.Label
        $secLbl.Text = $section.Title
        $secLbl.Font = $secBoldFont
        $secLbl.ForeColor = $script:ColorGold
        $secLbl.BackColor = [System.Drawing.Color]::FromArgb(50, 50, 50)
        $secLbl.Location = New-Object System.Drawing.Point(0, $yPos)
        $secLbl.Size = New-Object System.Drawing.Size($panelW, $secH)
        $secLbl.Padding = New-Object System.Windows.Forms.Padding(8, 4, 0, 0)
        $qcScrollPanel.Controls.Add($secLbl)
        $yPos += $secH + 2

        foreach ($row in $section.Rows) {
            $rowNum = $row.Num

            # Measure both text columns to determine the required row height
            $testMeasureSize = New-Object System.Drawing.Size($colTestW, 65535)
            $expMeasureSize  = New-Object System.Drawing.Size($colExpW,  65535)
            $testSz = [System.Windows.Forms.TextRenderer]::MeasureText(
                $row.Test, $testFont, $testMeasureSize, $fmtFlags)
            $expSz  = [System.Windows.Forms.TextRenderer]::MeasureText(
                $row.Expected, $expFont, $expMeasureSize, $fmtFlags)
            $textH = [Math]::Max([int]$testSz.Height, [int]$expSz.Height)
            $rowH  = [Math]::Max($minRowH, $textH + $rowPad)

            # Alternate row background
            $rowBg = if (([int]$rowNum % 2) -eq 0) {
                [System.Drawing.Color]::FromArgb(28, 28, 28)
            } else {
                [System.Drawing.Color]::FromArgb(35, 35, 35)
            }

            # Row container - height is now dynamic
            $rowPanel = New-Object System.Windows.Forms.Panel
            $rowPanel.Location = New-Object System.Drawing.Point(0, $yPos)
            $rowPanel.Size = New-Object System.Drawing.Size($panelW, $rowH)
            $rowPanel.BackColor = $rowBg
            $qcScrollPanel.Controls.Add($rowPanel)

            $xPos = 4

            # Number label
            $lblNum = New-Object System.Windows.Forms.Label
            $lblNum.Text = $rowNum
            $lblNum.Font = New-Object System.Drawing.Font("Consolas", 9, [System.Drawing.FontStyle]::Bold)
            $lblNum.ForeColor = $script:ColorDim
            $lblNum.Location = New-Object System.Drawing.Point($xPos, 7)
            $lblNum.Size = New-Object System.Drawing.Size($colNumW, 18)
            $lblNum.TextAlign = "MiddleCenter"
            $rowPanel.Controls.Add($lblNum)
            $xPos += $colNumW + 4

            # Status dropdown (Pass / Fail / Skip / Pending)
            $cmb = New-Object System.Windows.Forms.ComboBox
            $cmb.Location = New-Object System.Drawing.Point($xPos, 4)
            $cmb.Size = New-Object System.Drawing.Size($colStW, 22)
            $cmb.DropDownStyle = "DropDownList"
            $cmb.BackColor = $script:ColorFieldBg
            $cmb.ForeColor = Get-StatusColor $row.Status
            $cmb.FlatStyle = "Flat"
            $cmb.Font = New-Object System.Drawing.Font("Consolas", 9, [System.Drawing.FontStyle]::Bold)
            [void]$cmb.Items.Add("Pending")
            [void]$cmb.Items.Add("Pass")
            [void]$cmb.Items.Add("Fail")
            [void]$cmb.Items.Add("Skip/NA")

            $cmbIdx = switch ($row.Status) {
                "pass"  { 1 }
                "fail"  { 2 }
                "skip"  { 3 }
                default { 0 }
            }
            $cmb.SelectedIndex = $cmbIdx

            # Capture row ref for closure
            $capturedNum = $rowNum
            $capturedCmb = $cmb

            $cmb.Add_SelectedIndexChanged({
                $selIdx = $capturedCmb.SelectedIndex
                $newStatus = switch ($selIdx) {
                    1 { "pass" }
                    2 { "fail" }
                    3 { "skip" }
                    default { "pending" }
                }
                $capturedCmb.ForeColor = Get-StatusColor $newStatus

                # Update data model
                foreach ($r in $script:QcAllRows) {
                    if ($r.Num -eq $capturedNum) {
                        $r.Status = $newStatus
                        break
                    }
                }
                Save-QcFile
                Update-QcSummary
            })

            $rowPanel.Controls.Add($cmb)
            $script:QcStatusDrops[$rowNum] = $cmb
            $xPos += $colStW + 8

            # Test description - fills computed row height, wraps long text
            $lblTest = New-Object System.Windows.Forms.Label
            $lblTest.Text = $row.Test
            $lblTest.Font = $testFont
            $lblTest.ForeColor = $script:ColorText
            $lblTest.Location = New-Object System.Drawing.Point($xPos, 5)
            $lblTest.Size = New-Object System.Drawing.Size($colTestW, ($rowH - 6))
            $lblTest.TextAlign = "TopLeft"
            $rowPanel.Controls.Add($lblTest)
            $xPos += $colTestW + 8

            # Expected result - fills computed row height, wraps long text
            $lblExp = New-Object System.Windows.Forms.Label
            $lblExp.Text = $row.Expected
            $lblExp.Font = $expFont
            $lblExp.ForeColor = $script:ColorDim
            $lblExp.Location = New-Object System.Drawing.Point($xPos, 5)
            $lblExp.Size = New-Object System.Drawing.Size($colExpW, ($rowH - 6))
            $lblExp.TextAlign = "TopLeft"
            $rowPanel.Controls.Add($lblExp)

            $yPos += $rowH
        }

        $yPos += 6  # gap between sections
    }

    $qcScrollPanel.ResumeLayout()
    Update-QcSummary
}

# ============================================================================
# Output log panel (below QC - for git/run output)
# ============================================================================

$logY = $mainY + $mainH + 4
$logH = 680 - $logY - 40

if ($logH -gt 50) {
    $logPanel = New-Object System.Windows.Forms.Panel
    $logPanel.Location = New-Object System.Drawing.Point($mainX, $logY)
    $logPanel.Size = New-Object System.Drawing.Size($mainW, $logH)
    $logPanel.BackColor = $script:ColorConsoleBg
    $form.Controls.Add($logPanel)

    $logBox = New-Object System.Windows.Forms.RichTextBox
    $logBox.Location = New-Object System.Drawing.Point(0, 0)
    $logBox.Size = New-Object System.Drawing.Size($mainW, $logH)
    $logBox.BackColor = $script:ColorConsoleBg
    $logBox.ForeColor = $script:ColorText
    $logBox.Font = New-Object System.Drawing.Font("Consolas", 9)
    $logBox.ReadOnly = $true
    $logBox.WordWrap = $false
    $logBox.ScrollBars = "Both"
    $logBox.BorderStyle = "None"
    $logPanel.Controls.Add($logBox)
} else {
    $logBox = $null
}

function Write-Log($text, $color) {
    if ($null -eq $logBox) { return }
    $logBox.SelectionStart = $logBox.TextLength
    $logBox.SelectionColor = $color
    $logBox.AppendText("$text`r`n")
    $logBox.ScrollToCaret()
}

# ============================================================================
# Periodic git change count refresh
# ============================================================================

$script:LastGitCheck = [DateTime]::MinValue
$script:GitBusy = $false

function Update-GitChangeCount {
    if (([DateTime]::Now - $script:LastGitCheck).TotalSeconds -lt 5) { return }
    if ($script:GitBusy) { return }
    $script:LastGitCheck = [DateTime]::Now

    try {
        $statusOut = git -C $script:ProjectDir status --porcelain 2>$null
        $count = if ($statusOut) { ($statusOut | Measure-Object).Count } else { 0 }
    } catch {
        $count = 0
    }

    $script:GitChangeCount = $count

    if ($count -gt 0) {
        $s = if ($count -eq 1) { "" } else { "s" }
        $btnCommit.Text = "Commit $count change$s"
        $btnCommit.Enabled = $true
        $btnCommit.ForeColor = $script:ColorPurple
    } else {
        $btnCommit.Text = "No changes"
        $btnCommit.Enabled = $false
        $btnCommit.ForeColor = $script:ColorDisabled
    }
}

# ============================================================================
# Run client / server
# ============================================================================

function Update-RunButtons {
    $clientExe = Join-Path $script:ClientBuildDir "PerfectDark.exe"
    $serverExe = Join-Path $script:ServerBuildDir "PerfectDarkServer.exe"

    $canRunClient = Test-Path $clientExe
    $canRunServer = Test-Path $serverExe

    $btnRunClient.Enabled = $canRunClient
    $btnRunServer.Enabled = $canRunServer
    $btnRunClient.ForeColor = if ($canRunClient) { $script:ColorGreen } else { $script:ColorDisabled }
    $btnRunServer.ForeColor = if ($canRunServer) { $script:ColorOrange } else { $script:ColorDisabled }
}

$btnRunClient.Add_Click({
    $exe = Join-Path $script:ClientBuildDir "PerfectDark.exe"
    if (Test-Path $exe) {
        Write-Log "Launching client: $exe" $script:ColorGreen
        Start-Process $exe -WorkingDirectory $script:ClientBuildDir
        $lblStatus.Text = "Client launched"
        $lblStatus.ForeColor = $script:ColorGreen
    } else {
        Write-Log "PerfectDark.exe not found at: $exe" $script:ColorRed
        $lblStatus.Text = "Client not found"
        $lblStatus.ForeColor = $script:ColorRed
    }
})

$btnRunServer.Add_Click({
    $exe = Join-Path $script:ServerBuildDir "PerfectDarkServer.exe"
    if (Test-Path $exe) {
        Write-Log "Launching server: $exe" $script:ColorOrange
        Start-Process $exe -WorkingDirectory $script:ServerBuildDir
        $lblStatus.Text = "Server launched"
        $lblStatus.ForeColor = $script:ColorOrange
    } else {
        Write-Log "PerfectDarkServer.exe not found at: $exe" $script:ColorRed
        $lblStatus.Text = "Server not found"
        $lblStatus.ForeColor = $script:ColorRed
    }
})

# ============================================================================
# Open folder / GitHub buttons
# ============================================================================

$btnOpenBuild.Add_Click({
    $dir = $script:ClientBuildDir
    if (-not (Test-Path $dir)) { $dir = Join-Path $script:ProjectDir "build" }
    if (-not (Test-Path $dir)) { $dir = $script:ProjectDir }
    Start-Process "explorer.exe" -ArgumentList $dir
})

$btnOpenProject.Add_Click({
    Start-Process "explorer.exe" -ArgumentList $script:ProjectDir
})

$btnGithub.Add_Click({
    $repo = $script:Settings.GithubRepo
    if ($repo -ne "") {
        Start-Process "https://github.com/$repo"
    } else {
        Write-Log "GitHub repo not configured (check git remote origin)." $script:ColorRed
    }
})

# ============================================================================
# Git commit
# ============================================================================

function Start-ManualCommit {
    if ($script:GitChangeCount -eq 0) { return }

    $statusLines = git -C $script:ProjectDir status --porcelain 2>$null
    $added = @(); $modified = @(); $deleted = @(); $renamed = @()
    foreach ($line in $statusLines) {
        if ($line.Length -lt 3) { continue }
        $code = $line.Substring(0, 2).Trim()
        $file = $line.Substring(3).Trim()
        switch -Wildcard ($code) {
            "A"  { $added    += $file }
            "M"  { $modified += $file }
            "MM" { $modified += $file }
            "D"  { $deleted  += $file }
            "R*" { $renamed  += $file }
            "??" { $added    += $file }
            default { $modified += $file }
        }
    }

    $detailLines = @()
    if ($modified.Count -gt 0) { $detailLines += "Modified ($($modified.Count)): $($modified -join ', ')" }
    if ($added.Count    -gt 0) { $detailLines += "Added    ($($added.Count)):    $($added -join ', ')" }
    if ($deleted.Count  -gt 0) { $detailLines += "Deleted  ($($deleted.Count)):  $($deleted -join ', ')" }
    if ($renamed.Count  -gt 0) { $detailLines += "Renamed  ($($renamed.Count)):  $($renamed -join ', ')" }
    $detailText = $detailLines -join "`r`n"

    # Show commit dialog
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = "Commit Changes"
    $dlg.Size = New-Object System.Drawing.Size(520, 360)
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
    $txtMsg.Location = New-Object System.Drawing.Point(12, 36)
    $txtMsg.Size = New-Object System.Drawing.Size(480, 24)
    $txtMsg.BackColor = $script:ColorFieldBg
    $txtMsg.ForeColor = $script:ColorWhite
    $txtMsg.Font = New-Object System.Drawing.Font("Consolas", 10)
    $txtMsg.BorderStyle = "FixedSingle"
    $txtMsg.Text = "Playtest -"
    $dlg.Controls.Add($txtMsg)

    $lblDetails = New-Object System.Windows.Forms.Label
    $lblDetails.Text = "Changes:"
    $lblDetails.Font = New-UIFont 9
    $lblDetails.ForeColor = $script:ColorDim
    $lblDetails.Location = New-Object System.Drawing.Point(12, 68)
    $lblDetails.AutoSize = $true
    $dlg.Controls.Add($lblDetails)

    $txtDetails = New-Object System.Windows.Forms.TextBox
    $txtDetails.Location = New-Object System.Drawing.Point(12, 88)
    $txtDetails.Size = New-Object System.Drawing.Size(480, 140)
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
    $chkPush.Location = New-Object System.Drawing.Point(12, 240)
    $chkPush.AutoSize = $true
    $chkPush.Checked = $true
    $dlg.Controls.Add($chkPush)

    $btnOk = New-Object System.Windows.Forms.Button
    $btnOk.Text = "Commit"
    $btnOk.Location = New-Object System.Drawing.Point(310, 278)
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
    $btnCancel.Location = New-Object System.Drawing.Point(406, 278)
    $btnCancel.Size = New-Object System.Drawing.Size(86, 30)
    $btnCancel.FlatStyle = "Flat"
    $btnCancel.FlatAppearance.BorderColor = $script:ColorDim
    $btnCancel.ForeColor = $script:ColorDim
    $btnCancel.BackColor = $script:ColorFieldBg
    $btnCancel.Font = New-UIFont 10 -Bold
    $btnCancel.DialogResult = "Cancel"
    $dlg.Controls.Add($btnCancel)
    $dlg.CancelButton = $btnCancel

    $result    = $dlg.ShowDialog($form)
    $commitMsg = $txtMsg.Text.Trim()
    $shouldPush = $chkPush.Checked
    $dlg.Dispose()

    if ($result -ne "OK" -or $commitMsg -eq "") { return }

    $script:GitBusy = $true
    $savedEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    Write-Log "" $script:ColorDimmer
    Write-Log ("=" * 60) $script:ColorDimmer
    Write-Log "  Git Commit" $script:ColorGold
    Write-Log ("=" * 60) $script:ColorDimmer

    $lockFile = Join-Path $script:ProjectDir ".git\index.lock"
    if (Test-Path $lockFile) {
        Write-Log "  Removing stale index.lock..." $script:ColorOrange
        Remove-Item $lockFile -Force -ErrorAction SilentlyContinue
    }

    Write-Log "  Staging all changes..." $script:ColorPurple
    $addOut = git -C $script:ProjectDir add -A 2>&1
    foreach ($l in $addOut) { Write-Log "    $($l.ToString())" $script:ColorDim }

    $commitOut = git -C $script:ProjectDir commit -m $commitMsg 2>&1
    $commitExit = $LASTEXITCODE
    foreach ($l in $commitOut) { Write-Log "    $($l.ToString())" $script:ColorDim }

    if ($commitExit -ne 0) {
        Write-Log "  Commit failed (exit $commitExit)" $script:ColorRed
        $lblStatus.Text = "Commit failed"
        $lblStatus.ForeColor = $script:ColorRed
        $ErrorActionPreference = $savedEAP
        $script:GitBusy = $false
        return
    }
    Write-Log "  Committed: $commitMsg" $script:ColorGreen
    $lblStatus.Text = "Committed"
    $lblStatus.ForeColor = $script:ColorGreen

    if ($shouldPush) {
        Write-Log "  Pushing to origin..." $script:ColorPurple
        $upstream = git -C $script:ProjectDir rev-parse --abbrev-ref "@{upstream}" 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $upstream) {
            $branch = git -C $script:ProjectDir rev-parse --abbrev-ref HEAD 2>$null
            Write-Log "  Setting upstream for '$branch'..." $script:ColorDim
            $pushOut = git -C $script:ProjectDir push --set-upstream origin $branch 2>&1
        } else {
            $pushOut = git -C $script:ProjectDir push origin 2>&1
        }
        $pushExit = $LASTEXITCODE
        foreach ($l in $pushOut) { Write-Log "    $($l.ToString())" $script:ColorDim }
        if ($pushExit -ne 0) {
            Write-Log "  Push failed (exit $pushExit)" $script:ColorRed
            $lblStatus.Text = "Push failed"
            $lblStatus.ForeColor = $script:ColorRed
        } else {
            Write-Log "  Pushed to GitHub" $script:ColorGreen
            $lblStatus.Text = "Pushed"
            $lblStatus.ForeColor = $script:ColorGreen
        }
    }

    $ErrorActionPreference = $savedEAP
    $script:GitBusy = $false
    $script:LastGitCheck = [DateTime]::MinValue
    Update-GitChangeCount
}

$btnCommit.Add_Click({ Start-ManualCommit })

# ============================================================================
# Push dev / stable
# ============================================================================

function Do-Push($isStable) {
    $tagType = if ($isStable) { "stable" } else { "dev" }

    # Ask for confirmation
    $msg = "Push $tagType release to GitHub?`n`nThis will tag the current commit and push."
    $res = [System.Windows.Forms.MessageBox]::Show(
        $msg, "Confirm Push", "YesNo", "Question"
    )
    if ($res -ne "Yes") { return }

    Write-Log "" $script:ColorDimmer
    Write-Log ("=" * 60) $script:ColorDimmer
    Write-Log "  Push $tagType release" $script:ColorGold
    Write-Log ("=" * 60) $script:ColorDimmer

    # Derive version from CMakeLists.txt
    $major = 0; $minor = 0; $patch = 0
    $cmakePath = Join-Path $script:ProjectDir "CMakeLists.txt"
    if (Test-Path $cmakePath) {
        $cmakeContent = Get-Content $cmakePath -Raw
        if ($cmakeContent -match 'VERSION_SEM_MAJOR\s+(\d+)') { $major = [int]$Matches[1] }
        if ($cmakeContent -match 'VERSION_SEM_MINOR\s+(\d+)')  { $minor = [int]$Matches[1] }
        if ($cmakeContent -match 'VERSION_SEM_PATCH\s+(\d+)')  { $patch = [int]$Matches[1] }
    }
    $verStr = "v$major.$minor.$patch"
    $tag    = if ($isStable) { "client-$verStr" } else { "client-$verStr-dev" }

    Write-Log "  Tagging as $tag..." $script:ColorPurple
    $tagOut = git -C $script:ProjectDir tag $tag 2>&1
    foreach ($l in $tagOut) { Write-Log "    $($l.ToString())" $script:ColorDim }

    $pushOut = git -C $script:ProjectDir push origin $tag 2>&1
    $pushExit = $LASTEXITCODE
    foreach ($l in $pushOut) { Write-Log "    $($l.ToString())" $script:ColorDim }

    if ($pushExit -ne 0) {
        Write-Log "  Push tag failed (exit $pushExit)" $script:ColorRed
        $lblStatus.Text = "Push failed"
        $lblStatus.ForeColor = $script:ColorRed
    } else {
        Write-Log "  Tag pushed: $tag" $script:ColorGreen
        $lblStatus.Text = "Pushed $tag"
        $lblStatus.ForeColor = $script:ColorGreen
    }
}

$btnPushDev.Add_Click({ Do-Push $false })
$btnPushStable.Add_Click({ Do-Push $true })

# ============================================================================
# Refresh / Reset All
# ============================================================================

$btnRefresh.Add_Click({
    Load-QcFile
    Render-QcPanel
    $lblStatus.Text = "QC file refreshed"
    $lblStatus.ForeColor = $script:ColorGreen
})

$btnResetAll.Add_Click({
    $res = [System.Windows.Forms.MessageBox]::Show(
        "Reset all QC items to Pending?`nThis will overwrite qc-tests.md.",
        "Confirm Reset",
        "YesNo",
        "Warning"
    )
    if ($res -ne "Yes") { return }

    foreach ($row in $script:QcAllRows) {
        $row.Status = "pending"
    }
    Save-QcFile
    Render-QcPanel
    $lblStatus.Text = "All QC items reset to Pending"
    $lblStatus.ForeColor = $script:ColorOrange
})

# ============================================================================
# Timer: periodic git refresh + run button state
# ============================================================================

$gitTimer = New-Object System.Windows.Forms.Timer
$gitTimer.Interval = 2000
$gitTimer.Add_Tick({
    Update-GitChangeCount
    Update-RunButtons
})
$gitTimer.Start()

# ============================================================================
# Tool version footer
# ============================================================================

$lblFooter = New-Object System.Windows.Forms.Label
$lblFooter.Text = "Playtest Dashboard v1.0"
$lblFooter.Font = New-UIFont 8
$lblFooter.ForeColor = [System.Drawing.Color]::FromArgb(70, 70, 70)
$lblFooter.Location = New-Object System.Drawing.Point(12, 650)
$lblFooter.AutoSize = $true
$form.Controls.Add($lblFooter)

# ============================================================================
# Initial load
# ============================================================================

Load-QcFile
Render-QcPanel
Update-RunButtons
Update-GitChangeCount

$form.Add_Shown({
    $form.Activate()
})

$form.Add_FormClosed({
    $gitTimer.Stop()
    $gitTimer.Dispose()
})

[System.Windows.Forms.Application]::Run($form)
