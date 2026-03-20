Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# --- Inline C# helper for reliable async stream reading ---
# PowerShell [Action] delegates cannot capture PS-scope variables inside .NET Tasks.
# This pure C# class avoids that problem entirely.
Add-Type -Language CSharp @"
using System;
using System.IO;
using System.Threading;
using System.Collections.Concurrent;

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
"@

# --- Config ---
$script:ProjectDir      = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:ClientBuildDir  = Join-Path $script:ProjectDir "build\client"
$script:ServerBuildDir  = Join-Path $script:ProjectDir "build\server"
$script:BuildDir        = $script:ClientBuildDir   # Active build dir (set per-build)
$script:AddinDir        = Join-Path $script:ProjectDir "..\post-batch-addin"
$script:CMake      = "cmake"
$script:Make       = "C:\msys64\usr\bin\make.exe"
$script:CC         = "C:/msys64/mingw64/bin/cc.exe"
$script:ChangesFile = Join-Path $script:ProjectDir "CHANGES.md"

# Set up MSYS2 MINGW64 environment (critical for compiler toolchain)
$env:MSYSTEM       = "MINGW64"
$env:MINGW_PREFIX  = "/mingw64"
$env:PATH          = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"

$script:ErrorLines = [System.Collections.ArrayList]::new()
$script:AllOutput  = [System.Collections.ArrayList]::new()
$script:IsRunning  = $false
$script:ExeName    = "pd.x86_64.exe"
$script:BuildSucceeded = $false        # True after a successful build
$script:BuildTarget    = ""            # "client", "server", or "" (tracks current build)
$script:GameProcess    = $null         # Tracked game process object
$script:GameRunning    = $false        # Actual running state (polled)

# Thread-safe queue for async output from background reader threads
$script:OutputQueue = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()

# --- Colors ---
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

# --- Form ---
$form = New-Object System.Windows.Forms.Form
$form.Text = "Perfect Dark - Build Tool"
$form.Size = New-Object System.Drawing.Size(960, 780)
$form.StartPosition = "CenterScreen"
$form.BackColor = $script:ColorBg
$form.ForeColor = $script:ColorWhite
$form.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$form.FormBorderStyle = "FixedSingle"
$form.MaximizeBox = $false
$form.ShowInTaskbar = $true

# --- Title ---
$title = New-Object System.Windows.Forms.Label
$title.Text = "Perfect Dark PC Port"
$title.Font = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$title.ForeColor = $script:ColorGold
$title.Location = New-Object System.Drawing.Point(16, 10)
$title.AutoSize = $true
$form.Controls.Add($title)

$subtitle = New-Object System.Windows.Forms.Label
$subtitle.Text = "MinGW/MSYS2 Build System"
$subtitle.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$subtitle.ForeColor = $script:ColorDim
$subtitle.Location = New-Object System.Drawing.Point(18, 36)
$subtitle.AutoSize = $true
$form.Controls.Add($subtitle)

# --- Status bar ---
$statusLabel = New-Object System.Windows.Forms.Label
$statusLabel.Text = "Ready"
$statusLabel.Font = New-Object System.Drawing.Font("Consolas", 9)
$statusLabel.ForeColor = $script:ColorGreen
$statusLabel.Location = New-Object System.Drawing.Point(500, 14)
$statusLabel.Size = New-Object System.Drawing.Size(430, 20)
$statusLabel.TextAlign = "MiddleRight"
$form.Controls.Add($statusLabel)

# ============================================================================
# Top Info Bar: Branch selector + Version editor
# ============================================================================
$infoPanel = New-Object System.Windows.Forms.Panel
$infoPanel.Location = New-Object System.Drawing.Point(10, 58)
$infoPanel.Size = New-Object System.Drawing.Size(930, 54)
$infoPanel.BackColor = $script:ColorPanelBg
$form.Controls.Add($infoPanel)

# Branch label
$lblBranch = New-Object System.Windows.Forms.Label
$lblBranch.Text = "Branch:"
$lblBranch.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
$lblBranch.ForeColor = $script:ColorDim
$lblBranch.Location = New-Object System.Drawing.Point(8, 6)
$lblBranch.AutoSize = $true
$infoPanel.Controls.Add($lblBranch)

# Branch dropdown
$cmbBranch = New-Object System.Windows.Forms.ComboBox
$cmbBranch.Location = New-Object System.Drawing.Point(70, 3)
$cmbBranch.Size = New-Object System.Drawing.Size(130, 24)
$cmbBranch.DropDownStyle = "DropDownList"
$cmbBranch.BackColor = $script:ColorFieldBg
$cmbBranch.ForeColor = $script:ColorWhite
$cmbBranch.FlatStyle = "Flat"
$cmbBranch.Font = New-Object System.Drawing.Font("Consolas", 9)
$infoPanel.Controls.Add($cmbBranch)

# Version fields
$lblVersion = New-Object System.Windows.Forms.Label
$lblVersion.Text = "Version:"
$lblVersion.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
$lblVersion.ForeColor = $script:ColorDim
$lblVersion.Location = New-Object System.Drawing.Point(220, 6)
$lblVersion.AutoSize = $true
$infoPanel.Controls.Add($lblVersion)

function New-VersionField($x, $w) {
    $txt = New-Object System.Windows.Forms.TextBox
    $txt.Location = New-Object System.Drawing.Point($x, 3)
    $txt.Size = New-Object System.Drawing.Size($w, 24)
    $txt.BackColor = $script:ColorFieldBg
    $txt.ForeColor = $script:ColorWhite
    $txt.Font = New-Object System.Drawing.Font("Consolas", 9)
    $txt.BorderStyle = "FixedSingle"
    $txt.TextAlign = "Center"
    $infoPanel.Controls.Add($txt)
    return $txt
}

$txtMajor = New-VersionField 286 36
$lblDot1 = New-Object System.Windows.Forms.Label
$lblDot1.Text = "."; $lblDot1.ForeColor = $script:ColorDim
$lblDot1.Location = New-Object System.Drawing.Point(323, 6); $lblDot1.AutoSize = $true
$lblDot1.Font = New-Object System.Drawing.Font("Consolas", 9, [System.Drawing.FontStyle]::Bold)
$infoPanel.Controls.Add($lblDot1)

$txtMinor = New-VersionField 334 36
$lblDot2 = New-Object System.Windows.Forms.Label
$lblDot2.Text = "."; $lblDot2.ForeColor = $script:ColorDim
$lblDot2.Location = New-Object System.Drawing.Point(371, 6); $lblDot2.AutoSize = $true
$lblDot2.Font = New-Object System.Drawing.Font("Consolas", 9, [System.Drawing.FontStyle]::Bold)
$infoPanel.Controls.Add($lblDot2)

$txtPatch = New-VersionField 382 36

$lblDash = New-Object System.Windows.Forms.Label
$lblDash.Text = "dev"; $lblDash.ForeColor = $script:ColorDim
$lblDash.Location = New-Object System.Drawing.Point(424, 6); $lblDash.AutoSize = $true
$lblDash.Font = New-Object System.Drawing.Font("Consolas", 9)
$infoPanel.Controls.Add($lblDash)

$txtDev = New-VersionField 450 36

$lblLabel = New-Object System.Windows.Forms.Label
$lblLabel.Text = "label"; $lblLabel.ForeColor = $script:ColorDim
$lblLabel.Location = New-Object System.Drawing.Point(492, 6); $lblLabel.AutoSize = $true
$lblLabel.Font = New-Object System.Drawing.Font("Consolas", 9)
$infoPanel.Controls.Add($lblLabel)

$txtLabel = New-VersionField 530 40

# Save Version button
$btnSaveVersion = New-Object System.Windows.Forms.Button
$btnSaveVersion.Text = "Save"
$btnSaveVersion.Location = New-Object System.Drawing.Point(578, 2)
$btnSaveVersion.Size = New-Object System.Drawing.Size(52, 26)
$btnSaveVersion.FlatStyle = "Flat"
$btnSaveVersion.FlatAppearance.BorderColor = $script:ColorGold
$btnSaveVersion.FlatAppearance.BorderSize = 1
$btnSaveVersion.ForeColor = $script:ColorGold
$btnSaveVersion.BackColor = $script:ColorFieldBg
$btnSaveVersion.Cursor = "Hand"
$btnSaveVersion.Font = New-Object System.Drawing.Font("Segoe UI", 8, [System.Drawing.FontStyle]::Bold)
$infoPanel.Controls.Add($btnSaveVersion)

# Computed version string display
$lblVersionStr = New-Object System.Windows.Forms.Label
$lblVersionStr.Text = ""
$lblVersionStr.Font = New-Object System.Drawing.Font("Consolas", 10, [System.Drawing.FontStyle]::Bold)
$lblVersionStr.ForeColor = $script:ColorGold
$lblVersionStr.Location = New-Object System.Drawing.Point(8, 30)
$lblVersionStr.AutoSize = $true
$infoPanel.Controls.Add($lblVersionStr)

# Edit CHANGES.md button
$btnEditChanges = New-Object System.Windows.Forms.Button
$btnEditChanges.Text = "Edit CHANGES.md"
$btnEditChanges.Location = New-Object System.Drawing.Point(770, 2)
$btnEditChanges.Size = New-Object System.Drawing.Size(148, 26)
$btnEditChanges.FlatStyle = "Flat"
$btnEditChanges.FlatAppearance.BorderColor = $script:ColorPurple
$btnEditChanges.FlatAppearance.BorderSize = 1
$btnEditChanges.ForeColor = $script:ColorPurple
$btnEditChanges.BackColor = $script:ColorFieldBg
$btnEditChanges.Cursor = "Hand"
$btnEditChanges.Font = New-Object System.Drawing.Font("Segoe UI", 8, [System.Drawing.FontStyle]::Bold)
$infoPanel.Controls.Add($btnEditChanges)

# Changes status line
$lblChangesStatus = New-Object System.Windows.Forms.Label
$lblChangesStatus.Text = ""
$lblChangesStatus.Font = New-Object System.Drawing.Font("Segoe UI", 8)
$lblChangesStatus.ForeColor = $script:ColorDim
$lblChangesStatus.Location = New-Object System.Drawing.Point(640, 32)
$lblChangesStatus.Size = New-Object System.Drawing.Size(280, 16)
$lblChangesStatus.TextAlign = "MiddleRight"
$infoPanel.Controls.Add($lblChangesStatus)

# ============================================================================
# Left button panel (vertical)
# ============================================================================
$buttonPanel = New-Object System.Windows.Forms.Panel
$buttonPanel.Location = New-Object System.Drawing.Point(10, 118)
$buttonPanel.Size = New-Object System.Drawing.Size(150, 520)
$buttonPanel.BackColor = $script:ColorPanelBg
$form.Controls.Add($buttonPanel)

function New-BuildButton($text, $y, $color) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $text
    $btn.Location = New-Object System.Drawing.Point(8, $y)
    $btn.Size = New-Object System.Drawing.Size(134, 36)
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderColor = $color
    $btn.FlatAppearance.BorderSize = 1
    $btn.ForeColor = $color
    $btn.BackColor = $script:ColorFieldBg
    $btn.Cursor = "Hand"
    $btn.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $buttonPanel.Controls.Add($btn)
    return $btn
}

# Build actions
$btnBuildClient = New-BuildButton "Build Client"    8 $script:ColorGreen
$btnBuildServer = New-BuildButton "Build Server"   50 $script:ColorOrange

# Separator 1
$btnSep0 = New-Object System.Windows.Forms.Label
$btnSep0.Text = ""; $btnSep0.Location = New-Object System.Drawing.Point(8, 94)
$btnSep0.Size = New-Object System.Drawing.Size(134, 2)
$btnSep0.BackColor = $script:ColorDimmer
$buttonPanel.Controls.Add($btnSep0)

# Run actions
$btnRunGame    = New-BuildButton "Run Client"   104 $script:ColorGreen
$btnRunServer  = New-BuildButton "Run Server"   146 $script:ColorOrange

# Separator 2
$btnSep1 = New-Object System.Windows.Forms.Label
$btnSep1.Text = ""; $btnSep1.Location = New-Object System.Drawing.Point(8, 190)
$btnSep1.Size = New-Object System.Drawing.Size(134, 2)
$btnSep1.BackColor = $script:ColorDimmer
$buttonPanel.Controls.Add($btnSep1)

# Release actions
$btnPackage     = New-BuildButton "Package"       200 $script:ColorPurple
$btnPushRelease = New-BuildButton "Push Release"  242 $script:ColorRed

# ============================================================================
# Right side: console + utility buttons + progress bar
# ============================================================================

$outputBox = New-Object System.Windows.Forms.RichTextBox
$outputBox.Location = New-Object System.Drawing.Point(170, 118)
$outputBox.Size = New-Object System.Drawing.Size(770, 442)
$outputBox.BackColor = $script:ColorConsoleBg
$outputBox.ForeColor = $script:ColorText
$outputBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$outputBox.ReadOnly = $true
$outputBox.WordWrap = $false
$outputBox.ScrollBars = "Both"
$outputBox.BorderStyle = "None"
$form.Controls.Add($outputBox)

# Utility buttons (below console)
$utilPanel = New-Object System.Windows.Forms.Panel
$utilPanel.Location = New-Object System.Drawing.Point(170, 564)
$utilPanel.Size = New-Object System.Drawing.Size(770, 36)
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

# Error count label (right-aligned in util panel)
$errorCountLabel = New-Object System.Windows.Forms.Label
$errorCountLabel.Text = ""
$errorCountLabel.Font = New-Object System.Drawing.Font("Consolas", 8, [System.Drawing.FontStyle]::Bold)
$errorCountLabel.ForeColor = $script:ColorRed
$errorCountLabel.Location = New-Object System.Drawing.Point(300, 8)
$errorCountLabel.Size = New-Object System.Drawing.Size(460, 20)
$errorCountLabel.TextAlign = "MiddleRight"
$utilPanel.Controls.Add($errorCountLabel)

# --- Progress bar ---
$progressPanel = New-Object System.Windows.Forms.Panel
$progressPanel.Location = New-Object System.Drawing.Point(170, 604)
$progressPanel.Size = New-Object System.Drawing.Size(770, 22)
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

# --- Timer for draining async output queue ---
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 100

# --- Spinner ---
$script:SpinnerChars = @('|', '/', '-', '\')
$script:SpinnerIndex = 0
$script:LastOutputTime = [DateTime]::Now
$script:StepStartTime = [DateTime]::Now

# ============================================================================
# Helpers
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
    $btnBuildClient.Enabled = $enabled
    $btnBuildServer.Enabled = $enabled
    $cmbBranch.Enabled = $enabled
    $btnSaveVersion.Enabled = $enabled
    Update-RunButtons
}

function Update-RunButtons {
    $clientExe = Join-Path $script:ClientBuildDir $script:ExeName
    $serverExe = Join-Path $script:ServerBuildDir "pd-server.x86_64.exe"

    $canRunClient = (-not $script:IsRunning) -and (Test-Path $clientExe)
    $canRunServer = (-not $script:IsRunning) -and (Test-Path $serverExe)
    $canPackage = (-not $script:IsRunning) -and ((Test-Path $clientExe) -or (Test-Path $serverExe))
    $canRelease = -not $script:IsRunning

    $btnRunGame.Enabled = $canRunClient
    $btnRunServer.Enabled = $canRunServer
    $btnPackage.Enabled = $canPackage
    $btnPushRelease.Enabled = $canRelease

    $btnRunGame.ForeColor = if ($canRunClient) { $script:ColorGreen } else { $script:ColorDisabled }
    $btnRunServer.ForeColor = if ($canRunServer) { $script:ColorOrange } else { $script:ColorDisabled }
    $btnPackage.ForeColor = if ($canPackage) { $script:ColorPurple } else { $script:ColorDisabled }
    $btnPushRelease.ForeColor = if ($canRelease) { $script:ColorRed } else { $script:ColorDisabled }
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
# Branch management
# ============================================================================

function Refresh-BranchList {
    $cmbBranch.Items.Clear()
    try {
        $branches = git -C $script:ProjectDir branch --format="%(refname:short)" 2>$null
        if ($branches) {
            foreach ($b in $branches) {
                $b = $b.Trim()
                if ($b -ne "") { [void]$cmbBranch.Items.Add($b) }
            }
        }
    } catch {}

    # Select current branch
    try {
        $current = (git -C $script:ProjectDir branch --show-current 2>$null).Trim()
        if ($current -and $cmbBranch.Items.Contains($current)) {
            $cmbBranch.SelectedItem = $current
        }
    } catch {}
}

function Switch-Branch($targetBranch) {
    if ($script:IsRunning) { return }

    # Must check git directly -- the dropdown already shows the new selection
    $currentBranch = ""
    try { $currentBranch = (git -C $script:ProjectDir branch --show-current 2>$null).Trim() } catch {}
    if ($targetBranch -eq $currentBranch) { return }

    # Release branch requires confirmation
    if ($targetBranch -eq "release") {
        $confirm = [System.Windows.Forms.MessageBox]::Show(
            "You are switching to the RELEASE branch.`n`n" +
            "This branch is for stable builds only.`n" +
            "Make sure your changes are committed first.`n`n" +
            "Continue?",
            "Switch to Release Branch",
            [System.Windows.Forms.MessageBoxButtons]::YesNo,
            [System.Windows.Forms.MessageBoxIcon]::Warning
        )
        if ($confirm -ne [System.Windows.Forms.DialogResult]::Yes) {
            # Revert dropdown to current branch
            Refresh-BranchList
            return
        }
    }

    # Check for uncommitted changes
    $status = git -C $script:ProjectDir status --porcelain 2>$null
    if ($status) {
        $confirm = [System.Windows.Forms.MessageBox]::Show(
            "You have uncommitted changes.`n`n" +
            "Switching branches may cause conflicts.`n" +
            "Commit or stash your changes first.`n`n" +
            "Switch anyway?",
            "Uncommitted Changes",
            [System.Windows.Forms.MessageBoxButtons]::YesNo,
            [System.Windows.Forms.MessageBoxIcon]::Warning
        )
        if ($confirm -ne [System.Windows.Forms.DialogResult]::Yes) {
            Refresh-BranchList
            return
        }
    }

    Write-Header "Switching to branch: $targetBranch"
    try {
        $result = git -C $script:ProjectDir checkout $targetBranch 2>&1
        Write-Output-Line "  $result" $script:ColorGreen
        $statusLabel.Text = "Branch: $targetBranch"
        $statusLabel.ForeColor = $script:ColorGreen
        Refresh-VersionFields
        Refresh-ChangesStatus
    } catch {
        Write-Output-Line "  Failed to switch branch: $_" $script:ColorRed
        $statusLabel.Text = "Branch switch failed"
        $statusLabel.ForeColor = $script:ColorRed
    }
    Refresh-BranchList
}

# ============================================================================
# Version management
# ============================================================================

function Refresh-VersionFields {
    $cmakePath = Join-Path $script:ProjectDir "CMakeLists.txt"
    if (!(Test-Path $cmakePath)) { return }

    $content = Get-Content $cmakePath -Raw
    $major = "0"; $minor = "0"; $patch = "0"; $dev = "0"; $label = ""

    if ($content -match 'VERSION_SEM_MAJOR\s+(\d+)') { $major = $Matches[1] }
    if ($content -match 'VERSION_SEM_MINOR\s+(\d+)') { $minor = $Matches[1] }
    if ($content -match 'VERSION_SEM_PATCH\s+(\d+)') { $patch = $Matches[1] }
    if ($content -match 'VERSION_SEM_DEV\s+(\d+)')   { $dev   = $Matches[1] }
    if ($content -match 'VERSION_SEM_LABEL\s+"([^"]*)"') { $label = $Matches[1] }

    $txtMajor.Text = $major
    $txtMinor.Text = $minor
    $txtPatch.Text = $patch
    $txtDev.Text = $dev
    $txtLabel.Text = $label

    Update-VersionString
}

function Update-VersionString {
    $major = $txtMajor.Text; $minor = $txtMinor.Text; $patch = $txtPatch.Text
    $dev = $txtDev.Text; $label = $txtLabel.Text

    if ([int]$dev -gt 0) {
        $verStr = "v$major.$minor.$patch-dev.$dev"
    } elseif ($label -ne "") {
        $verStr = "v$major.$minor.$patch$label"
    } else {
        $verStr = "v$major.$minor.$patch"
    }
    $lblVersionStr.Text = $verStr
}

function Save-VersionToFile {
    $cmakePath = Join-Path $script:ProjectDir "CMakeLists.txt"
    if (!(Test-Path $cmakePath)) {
        Write-Output-Line "CMakeLists.txt not found." $script:ColorRed
        return
    }

    $content = Get-Content $cmakePath -Raw

    $content = $content -replace '(set\(VERSION_SEM_MAJOR\s+)\d+', "`${1}$($txtMajor.Text)"
    $content = $content -replace '(set\(VERSION_SEM_MINOR\s+)\d+', "`${1}$($txtMinor.Text)"
    $content = $content -replace '(set\(VERSION_SEM_PATCH\s+)\d+', "`${1}$($txtPatch.Text)"
    $content = $content -replace '(set\(VERSION_SEM_DEV\s+)\d+',   "`${1}$($txtDev.Text)"
    $content = $content -replace '(set\(VERSION_SEM_LABEL\s+)"[^"]*"', "`${1}`"$($txtLabel.Text)`""

    Set-Content -Path $cmakePath -Value $content -NoNewline
    Write-Output-Line "  Version saved to CMakeLists.txt" $script:ColorGreen
    Update-VersionString
    $statusLabel.Text = "Version updated: $($lblVersionStr.Text)"
    $statusLabel.ForeColor = $script:ColorGold
}

# ============================================================================
# CHANGES.md management
# ============================================================================

function Read-ChangesFile {
    # Returns a hashtable of section -> array of bullet lines
    $sections = @{
        "Improvements"    = @()
        "Additions"       = @()
        "Updates"         = @()
        "Known Issues"    = @()
        "Missing Content" = @()
    }

    if (!(Test-Path $script:ChangesFile)) { return $sections }

    $content = Get-Content $script:ChangesFile
    $currentSection = ""

    foreach ($line in $content) {
        if ($line -match '^## (.+)$') {
            $sectionName = $Matches[1].Trim()
            if ($sections.ContainsKey($sectionName)) {
                $currentSection = $sectionName
            }
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
    $lines += ""
    $lines += "     Format: Use `"- `" bullet points under each section."
    $lines += "     The build tool reads this file to generate release notes. -->"
    $lines += ""

    foreach ($section in @("Improvements", "Additions", "Updates", "Known Issues", "Missing Content")) {
        $lines += "## $section"
        if ($sections.ContainsKey($section) -and $sections[$section].Count -gt 0) {
            foreach ($item in $sections[$section]) {
                $lines += "- $item"
            }
        }
        $lines += ""
    }

    Set-Content -Path $script:ChangesFile -Value ($lines -join "`n")
}

function Clear-ChangesForRelease {
    # Clear release-specific sections, keep persistent sections
    $sections = Read-ChangesFile
    $sections["Improvements"] = @()
    $sections["Additions"] = @()
    $sections["Updates"] = @()
    # Known Issues and Missing Content persist
    Write-ChangesFile $sections
}

function Refresh-ChangesStatus {
    $sections = Read-ChangesFile
    $totalEntries = 0
    foreach ($key in @("Improvements", "Additions", "Updates")) {
        $totalEntries += $sections[$key].Count
    }
    $issueCount = $sections["Known Issues"].Count + $sections["Missing Content"].Count

    if ($totalEntries -eq 0 -and $issueCount -eq 0) {
        $lblChangesStatus.Text = "CHANGES.md: empty"
        $lblChangesStatus.ForeColor = $script:ColorDim
    } elseif ($totalEntries -eq 0) {
        $lblChangesStatus.Text = "CHANGES.md: $issueCount tracked issue(s)"
        $lblChangesStatus.ForeColor = $script:ColorDim
    } else {
        $lblChangesStatus.Text = "CHANGES.md: $totalEntries change(s), $issueCount issue(s)"
        $lblChangesStatus.ForeColor = $script:ColorPurple
    }
}

function Format-ReleaseNotes($sections, $version, $isRelease) {
    # Generate markdown release notes from CHANGES.md sections.
    # Release builds get player-centric language; dev builds stay technical.
    $lines = @()

    if ($isRelease) {
        # Player-centric format for release builds
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
        if ($sections["Missing Content"].Count -gt 0) {
            $lines += "## Not Yet Available"
            foreach ($item in $sections["Missing Content"]) { $lines += "- $item" }
            $lines += ""
        }
    } else {
        # Technical format for dev builds
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
# Process runner using .NET Tasks for reliable async I/O
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
    if (-not $script:HasErrors) {
        $progressFill.BackColor = $script:ColorBlue
    }
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
                $fillWidth = [math]::Floor(($pct / 100.0) * 770)
                $progressFill.Size = New-Object System.Drawing.Size($fillWidth, 22)
                $progressLabel.Text = "${pct}% - $($script:CurrentStep)"
                if (-not $script:HasErrors) {
                    $progressFill.BackColor = $script:ColorBlue
                }
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

    # Process exited - drain and handle completion
    if ($null -ne $script:Process -and $script:Process.HasExited -and $script:OutputQueue.IsEmpty) {
        $timer.Stop()
        $exitCode = $script:Process.ExitCode
        $totalElapsed = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
        try { $script:Process.Dispose() } catch {}
        $script:Process = $null

        $errCount = $script:ErrorLines.Count
        if ($errCount -gt 0) {
            $errorCountLabel.Text = "$errCount error line(s) captured - click 'Copy Errors' to copy"
        }

        if ($exitCode -ne 0) {
            $progressFill.Size = New-Object System.Drawing.Size(770, 22)
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

        $progressFill.Size = New-Object System.Drawing.Size(770, 22)
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

        if (!(Test-Path $launchExe)) {
            Write-Output-Line "Server not found. Build server first." $script:ColorRed
            $statusLabel.Text = "Server not found"
            $statusLabel.ForeColor = $script:ColorRed
            return
        }
    } else {
        $launchDir = $script:ClientBuildDir
        $launchExe = Join-Path $launchDir $script:ExeName
        $gameArgs = "--moddir mods/mod_allinone --gexmoddir mods/mod_gex --kakarikomoddir mods/mod_kakariko --darknoonmoddir mods/mod_dark_noon --goldfinger64moddir mods/mod_goldfinger_64 --log"
        $label = "Client"
        $labelColor = $script:ColorGreen

        if (!(Test-Path $launchExe)) {
            Write-Output-Line "Game not found. Build client first." $script:ColorRed
            $statusLabel.Text = "Game not found"
            $statusLabel.ForeColor = $script:ColorRed
            return
        }
    }

    Write-Output-Line "" $script:ColorDimmer
    Write-Output-Line "Launching $label..." $labelColor
    Write-Output-Line "  Exe: $launchExe" ([System.Drawing.Color]::FromArgb(50,180,220))
    Write-Output-Line "  Logging enabled" ([System.Drawing.Color]::FromArgb(50,180,220))

    $script:GameProcess = Start-Process -FilePath $launchExe -ArgumentList $gameArgs -WorkingDirectory $launchDir -PassThru
    Update-GameStatus
}

# ============================================================================
# Game process polling
# ============================================================================

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
        } catch {
            $script:GameRunning = $false
        }
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
# Build helper
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

# ============================================================================
# Package Release (local only - creates dist/ folder)
# ============================================================================

function Get-BuildVersion {
    $versionFile = Join-Path $script:ClientBuildDir "port\include\versioninfo.h"
    if (!(Test-Path $versionFile)) {
        $versionFile = Join-Path $script:ServerBuildDir "port\include\versioninfo.h"
    }
    if (!(Test-Path $versionFile)) { return $null }

    $content = Get-Content $versionFile -Raw
    $major = 0; $minor = 0; $patch = 0; $dev = 0

    if ($content -match '#define VERSION_MAJOR\s+(\d+)') { $major = [int]$Matches[1] }
    if ($content -match '#define VERSION_MINOR\s+(\d+)') { $minor = [int]$Matches[1] }
    if ($content -match '#define VERSION_PATCH\s+(\d+)') { $patch = [int]$Matches[1] }
    if ($content -match '#define VERSION_DEV\s+(\d+)')   { $dev   = [int]$Matches[1] }

    $hash = ""
    $label = ""
    if ($content -match '#define VERSION_HASH\s+"([^"]*)"') { $hash = $Matches[1] }
    if ($content -match '#define VERSION_LABEL\s+"([^"]*)"') { $label = $Matches[1] }

    if ($dev -gt 0) {
        $verStr = "$major.$minor.$patch-dev.$dev"
    } elseif ($label -ne "") {
        $verStr = "$major.$minor.$patch$label"
    } else {
        $verStr = "$major.$minor.$patch"
    }

    return @{
        Major   = $major
        Minor   = $minor
        Patch   = $patch
        Dev     = $dev
        Label   = $label
        String  = $verStr
        Hash    = $hash
        IsDev   = ($dev -gt 0)
    }
}

function Package-Release {
    if ($script:IsRunning) { return }

    $version = Get-BuildVersion
    if ($null -eq $version) {
        Write-Output-Line "Cannot read version -- build first." $script:ColorRed
        $statusLabel.Text = "Package failed: build first"
        $statusLabel.ForeColor = $script:ColorRed
        return
    }

    Write-Header "Package Release v$($version.String)"

    $releaseDir = Join-Path $script:ProjectDir "build\release"
    if (!(Test-Path $releaseDir)) {
        New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
    }

    $clientExe = Join-Path $script:ClientBuildDir "pd.x86_64.exe"
    $serverExe = Join-Path $script:ServerBuildDir "pd-server.x86_64.exe"
    $packaged = 0

    if (Test-Path $clientExe) {
        $destExe = Join-Path $releaseDir "pd.x86_64.exe"
        Copy-Item $clientExe -Destination $destExe -Force
        Write-Output-Line "  Client: pd.x86_64.exe" $script:ColorGreen

        $hash = (Get-FileHash -Path $destExe -Algorithm SHA256).Hash.ToLower()
        $hashFile = Join-Path $releaseDir "pd.x86_64.exe.sha256"
        "$hash  pd.x86_64.exe" | Set-Content -Path $hashFile -NoNewline
        Write-Output-Line "  SHA-256: $hash" ([System.Drawing.Color]::FromArgb(130, 170, 210))

        $fileSize = (Get-Item $destExe).Length
        $sizeMB = [math]::Round($fileSize / 1MB, 1)
        Write-Output-Line "  Size: $sizeMB MB" $script:ColorDim
        $packaged++
    } else {
        Write-Output-Line "  Client: NOT FOUND (skipped)" ([System.Drawing.Color]::FromArgb(230, 200, 80))
    }

    if (Test-Path $serverExe) {
        $destExe = Join-Path $releaseDir "pd-server.x86_64.exe"
        Copy-Item $serverExe -Destination $destExe -Force
        Write-Output-Line "  Server: pd-server.x86_64.exe" $script:ColorOrange

        $hash = (Get-FileHash -Path $destExe -Algorithm SHA256).Hash.ToLower()
        $hashFile = Join-Path $releaseDir "pd-server.x86_64.exe.sha256"
        "$hash  pd-server.x86_64.exe" | Set-Content -Path $hashFile -NoNewline
        Write-Output-Line "  SHA-256: $hash" ([System.Drawing.Color]::FromArgb(130, 170, 210))

        $fileSize = (Get-Item $destExe).Length
        $sizeMB = [math]::Round($fileSize / 1MB, 1)
        Write-Output-Line "  Size: $sizeMB MB" $script:ColorDim
        $packaged++
    } else {
        Write-Output-Line "  Server: NOT FOUND (skipped)" ([System.Drawing.Color]::FromArgb(230, 200, 80))
    }

    if ($packaged -eq 0) {
        Write-Output-Line "Nothing to package -- build client and/or server first." $script:ColorRed
        $statusLabel.Text = "Package failed: no executables"
        $statusLabel.ForeColor = $script:ColorRed
        return
    }

    Write-Output-Line "" $script:ColorDimmer
    Write-Output-Line "GitHub Release Tags:" $script:ColorGold

    if (Test-Path $clientExe) {
        $clientTag = "client-v$($version.String)"
        Write-Output-Line "  Client: $clientTag" $script:ColorGreen
    }
    if (Test-Path $serverExe) {
        $serverTag = "server-v$($version.String)"
        Write-Output-Line "  Server: $serverTag" $script:ColorOrange
    }

    if ($version.IsDev) {
        Write-Output-Line "  Channel: DEV (prerelease)" ([System.Drawing.Color]::FromArgb(230, 200, 80))
    } else {
        Write-Output-Line "  Channel: STABLE" $script:ColorGreen
    }

    Write-Output-Line "  Commit: $($version.Hash)" $script:ColorDim
    Write-Output-Line "" $script:ColorDimmer
    Write-Output-Line "Output: $releaseDir" $script:ColorPurple
    Write-Output-Line "Use 'Push Release' to upload to GitHub." $script:ColorDim

    $statusLabel.Text = "Packaged v$($version.String) ($packaged artifact(s))"
    $statusLabel.ForeColor = $script:ColorPurple

    Start-Process explorer.exe -ArgumentList $releaseDir
}

# ============================================================================
# Push Release - runs release.ps1 to push to GitHub
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

    # Read current version from fields
    $major = $txtMajor.Text; $minor = $txtMinor.Text; $patch = $txtPatch.Text
    $dev = $txtDev.Text; $label = $txtLabel.Text

    if ([int]$dev -gt 0) {
        $verStr = "$major.$minor.$patch-dev.$dev"
        $isDevBuild = $true
    } elseif ($label -ne "") {
        $verStr = "$major.$minor.$patch$label"
        $isDevBuild = $false
    } else {
        $verStr = "$major.$minor.$patch"
        $isDevBuild = $false
    }

    $currentBranch = $cmbBranch.SelectedItem
    if (!$currentBranch) {
        try { $currentBranch = (git -C $script:ProjectDir branch --show-current 2>$null).Trim() } catch {}
    }

    # Read changelog
    $sections = Read-ChangesFile
    $totalChanges = 0
    foreach ($key in @("Improvements", "Additions", "Updates")) {
        $totalChanges += $sections[$key].Count
    }

    # Build confirmation message
    $confirmMsg = "Push Release v$verStr to GitHub?`n`n"
    $confirmMsg += "Branch: $currentBranch`n"
    if ($isDevBuild) {
        $confirmMsg += "Channel: DEV (prerelease)`n"
    } else {
        $confirmMsg += "Channel: STABLE`n"
    }
    $confirmMsg += "Changes logged: $totalChanges`n`n"

    if ($totalChanges -eq 0) {
        $confirmMsg += "WARNING: No changes in CHANGES.md.`n"
        $confirmMsg += "Release notes will be auto-generated by GitHub.`n`n"
    }

    $confirmMsg += "This will:`n"
    $confirmMsg += "  - Package build artifacts`n"
    $confirmMsg += "  - Create git tag v$verStr`n"
    $confirmMsg += "  - Push to GitHub`n"
    $confirmMsg += "  - Create GitHub Release`n"
    if ($totalChanges -gt 0) {
        $confirmMsg += "  - Clear CHANGES.md for next version`n"
    }

    $confirm = [System.Windows.Forms.MessageBox]::Show(
        $confirmMsg,
        "Push Release v$verStr",
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Warning
    )
    if ($confirm -ne [System.Windows.Forms.DialogResult]::Yes) { return }

    # Generate release notes file from CHANGES.md
    $notesFile = Join-Path $script:ProjectDir "RELEASE_v$verStr.md"
    if ($totalChanges -gt 0) {
        $notes = Format-ReleaseNotes $sections $verStr (-not $isDevBuild)
        Set-Content -Path $notesFile -Value $notes
        Write-Output-Line "  Generated release notes: RELEASE_v$verStr.md" $script:ColorPurple
    }

    $outputBox.Clear()
    $script:ErrorLines.Clear()
    $script:AllOutput.Clear()
    $errorCountLabel.Text = ""
    $script:HasErrors = $false

    Set-Buttons-Enabled $false

    # Build release.ps1 arguments
    $scriptArgs = "-Version `"$verStr`""
    if ($isDevBuild) {
        $scriptArgs += " -Prerelease"
    }

    $script:StepQueue.Clear()

    # After release.ps1 finishes, we clear CHANGES.md via the completion handler
    $script:PendingPostRelease = $totalChanges -gt 0

    # Use -Command with *>&1 to capture Write-Host output (stream 6).
    # -File mode loses Write-Host in subprocess because it bypasses stdout/stderr.
    Start-Build-Step "Push Release v$verStr" "powershell.exe" "-ExecutionPolicy Bypass -Command `"& { . '$releaseScript' $scriptArgs } *>&1`""
}

# Flag for post-release cleanup
$script:PendingPostRelease = $false

# ============================================================================
# Button handlers
# ============================================================================

$btnBuildClient.Add_Click({ Start-Build "client" })
$btnBuildServer.Add_Click({ Start-Build "server" })

$btnRunGame.Add_Click({ Launch-Game "client" })
$btnRunServer.Add_Click({ Launch-Game "server" })
$btnPackage.Add_Click({ Package-Release })
$btnPushRelease.Add_Click({ Start-PushRelease })

# Branch selector
$cmbBranch.Add_SelectedIndexChanged({
    $selected = $cmbBranch.SelectedItem
    if ($null -eq $selected) { return }
    $current = ""
    try { $current = (git -C $script:ProjectDir branch --show-current 2>$null).Trim() } catch {}
    if ($selected -ne $current) {
        Switch-Branch $selected
    }
})

# Version field change handlers - update display string
$txtMajor.Add_TextChanged({ Update-VersionString })
$txtMinor.Add_TextChanged({ Update-VersionString })
$txtPatch.Add_TextChanged({ Update-VersionString })
$txtDev.Add_TextChanged({ Update-VersionString })
$txtLabel.Add_TextChanged({ Update-VersionString })

$btnSaveVersion.Add_Click({ Save-VersionToFile })

$btnEditChanges.Add_Click({
    if (!(Test-Path $script:ChangesFile)) {
        # Create default template
        $template = @(
            "# Changelog - Perfect Dark 2", "",
            "## Improvements", "",
            "## Additions", "",
            "## Updates", "",
            "## Known Issues", "",
            "## Missing Content", ""
        )
        Set-Content -Path $script:ChangesFile -Value ($template -join "`n")
    }
    Start-Process "notepad.exe" -ArgumentList $script:ChangesFile
    # Refresh status after a short delay (give notepad time to open)
    $refreshTimer = New-Object System.Windows.Forms.Timer
    $refreshTimer.Interval = 3000
    $refreshTimer.Add_Tick({
        $refreshTimer.Stop()
        $refreshTimer.Dispose()
        Refresh-ChangesStatus
    })
    $refreshTimer.Start()
})

$btnCopyErrors.Add_Click({
    if ($script:ErrorLines.Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show(
            "No errors captured.",
            "Copy Errors",
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Information
        )
        return
    }

    $text = "Build errors ($($script:ErrorLines.Count) lines):`r`n"
    $text += "``````r`n"
    foreach ($line in $script:ErrorLines) {
        $text += "$line`r`n"
    }
    $text += "``````r`n"

    [System.Windows.Forms.Clipboard]::SetText($text)
    $statusLabel.Text = "$($script:ErrorLines.Count) error(s) copied to clipboard"
    $statusLabel.ForeColor = $script:ColorOrange
})

$btnCopyAll.Add_Click({
    if ($script:AllOutput.Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show("No output to copy.", "Copy All", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information)
        return
    }
    $text = ($script:AllOutput -join "`r`n")
    [System.Windows.Forms.Clipboard]::SetText($text)
    $statusLabel.Text = "Full output copied to clipboard"
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
# Game status polling timer (every 2 seconds)
# ============================================================================

$gameTimer = New-Object System.Windows.Forms.Timer
$gameTimer.Interval = 2000
$gameTimer.Add_Tick({
    Update-GameStatus
    # Also check for post-release cleanup after release.ps1 finishes
    if ($script:PendingPostRelease -and -not $script:IsRunning) {
        $script:PendingPostRelease = $false
        if ($script:BuildSucceeded) {
            Clear-ChangesForRelease
            Refresh-ChangesStatus
            Write-Output-Line "" $script:ColorDimmer
            Write-Output-Line "CHANGES.md cleared for next version (issues preserved)." $script:ColorPurple
        }
    }
})
$gameTimer.Start()

# ============================================================================
# Initialization
# ============================================================================

# Populate branch list and version fields
Refresh-BranchList
Refresh-VersionFields
Refresh-ChangesStatus

# Enable Run if exe already exists from prior build
$clientCheck = Join-Path $script:ClientBuildDir $script:ExeName
$serverCheck = Join-Path $script:ServerBuildDir "pd-server.x86_64.exe"
if ((Test-Path $clientCheck) -or (Test-Path $serverCheck)) { $script:BuildSucceeded = $true }
Update-RunButtons

# ============================================================================
# Cleanup on close
# ============================================================================

$form.Add_FormClosing({
    $timer.Stop()
    $gameTimer.Stop()
    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        try { $script:Process.Kill() } catch {}
    }
})

# --- Launch ---
[void]$form.ShowDialog()
