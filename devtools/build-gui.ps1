#Requires -Version 5.1
<#
.SYNOPSIS
    WinForms GUI for Perfect Dark 2 builds (Cursor Build / Build), version bump, commit/push, and release.

.DESCRIPTION
    No extra dependencies. Double-click or: powershell -File devtools\build-gui.ps1

.PARAMETER NoHideConsole
    Keep the PowerShell console visible (default: hidden when launching the GUI).
#>

param([switch]$NoHideConsole)

# WinForms requires an STA thread; default PowerShell console is often MTA.
if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne [System.Threading.ApartmentState]::STA) {
    $exe = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh.exe' } else { 'powershell.exe' }
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-STA', '-File', $MyInvocation.MyCommand.Path)
    if ($NoHideConsole) { $argList += '-NoHideConsole' }
    & $exe @argList
    exit $LASTEXITCODE
}

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path $ScriptDir -Parent
. (Join-Path $ScriptDir "_build-env-prelude.ps1")
. (Join-Path $ScriptDir "version-util.ps1")

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not $NoHideConsole) {
    if (-not ([System.Management.Automation.PSTypeName]'PDBuildGui.ConsoleHider').Type) {
        Add-Type -Language CSharp @"
using System;
using System.Runtime.InteropServices;
namespace PDBuildGui {
    public static class ConsoleHider {
        [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
        [DllImport("user32.dll")]   public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        public const int SW_HIDE = 0;
        public static void Hide() {
            IntPtr h = GetConsoleWindow();
            if (h != IntPtr.Zero) ShowWindow(h, SW_HIDE);
        }
    }
}
"@
    }
    [PDBuildGui.ConsoleHider]::Hide()
}

# ---------------------------------------------------------------------------
# Form + controls
# ---------------------------------------------------------------------------

$form = New-Object System.Windows.Forms.Form
$form.Text = "Perfect Dark 2 - Build"
$form.Size = New-Object System.Drawing.Size(920, 700)
$form.MinimumSize = New-Object System.Drawing.Size(700, 500)
$form.StartPosition = "CenterScreen"
$form.BackColor = [System.Drawing.Color]::FromArgb(32, 32, 36)
$form.ForeColor = [System.Drawing.Color]::FromArgb(230, 230, 235)
$form.Font = New-Object System.Drawing.Font("Segoe UI", 9.0)

$menu = New-Object System.Windows.Forms.MenuStrip
$menu.BackColor = [System.Drawing.Color]::FromArgb(45, 45, 48)
$menu.ForeColor = $form.ForeColor
$miFile = New-Object System.Windows.Forms.ToolStripMenuItem("File")
$miExit = New-Object System.Windows.Forms.ToolStripMenuItem("Exit")
$null = $miFile.DropDownItems.Add($miExit)
$miHelp = New-Object System.Windows.Forms.ToolStripMenuItem("Help")
$miAbout = New-Object System.Windows.Forms.ToolStripMenuItem("About")
$null = $miHelp.DropDownItems.Add($miAbout)
$null = $menu.Items.Add($miFile)
$null = $menu.Items.Add($miHelp)
$form.MainMenuStrip = $menu
$form.Controls.Add($menu)

$panelTop = New-Object System.Windows.Forms.Panel
$panelTop.Dock = "Top"
$panelTop.Height = 220
$panelTop.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 44)
$panelTop.Padding = New-Object System.Windows.Forms.Padding(12, 8, 12, 8)

$lblOut = New-Object System.Windows.Forms.Label
$lblOut.Text = "Output folder"
$lblOut.AutoSize = $true
$lblOut.Location = New-Object System.Drawing.Point(8, 8)
$lblOut.ForeColor = [System.Drawing.Color]::FromArgb(180, 180, 190)

$rbCursor = New-Object System.Windows.Forms.RadioButton
$rbCursor.Text = "Cursor Build (out-of-tree)"
$rbCursor.Location = New-Object System.Drawing.Point(12, 28)
$rbCursor.AutoSize = $true
$rbCursor.Checked = $true
$rbCursor.ForeColor = $form.ForeColor

$rbBuild = New-Object System.Windows.Forms.RadioButton
$rbBuild.Text = "Build (default project dir)"
$rbBuild.Location = New-Object System.Drawing.Point(12, 48)
$rbBuild.AutoSize = $true
$rbBuild.ForeColor = $form.ForeColor

$lblTarget = New-Object System.Windows.Forms.Label
$lblTarget.Text = "Target"
$lblTarget.AutoSize = $true
$lblTarget.Location = New-Object System.Drawing.Point(320, 8)
$lblTarget.ForeColor = $lblOut.ForeColor

$cbTarget = New-Object System.Windows.Forms.ComboBox
$cbTarget.Location = New-Object System.Drawing.Point(320, 28)
$cbTarget.Width = 120
$cbTarget.DropDownStyle = "DropDownList"
[void]$cbTarget.Items.AddRange(@("all", "client", "server"))
$cbTarget.SelectedIndex = 0

$chkClean = New-Object System.Windows.Forms.CheckBox
$chkClean.Text = "Clean (delete build dir first)"
$chkClean.Location = New-Object System.Drawing.Point(320, 58)
$chkClean.AutoSize = $true
$chkClean.ForeColor = $form.ForeColor

$chkVerbose = New-Object System.Windows.Forms.CheckBox
$chkVerbose.Text = "Verbose compiler output"
$chkVerbose.Location = New-Object System.Drawing.Point(320, 80)
$chkVerbose.AutoSize = $true
$chkVerbose.ForeColor = $form.ForeColor

$chkNextVer = New-Object System.Windows.Forms.CheckBox
$chkNextVer.Text = "Use next version (max of CMake + git tags, then +1)"
$chkNextVer.Location = New-Object System.Drawing.Point(12, 78)
$chkNextVer.AutoSize = $true
$chkNextVer.ForeColor = [System.Drawing.Color]::FromArgb(255, 200, 120)

$chkCommit = New-Object System.Windows.Forms.CheckBox
$chkCommit.Text = "Commit + push before build"
$chkCommit.Location = New-Object System.Drawing.Point(12, 100)
$chkCommit.AutoSize = $true
$chkCommit.ForeColor = $form.ForeColor

$lblVer = New-Object System.Windows.Forms.Label
$lblVer.Text = 'Version override (optional X.Y.Z; empty = use rules above)'
$lblVer.AutoSize = $true
$lblVer.Location = New-Object System.Drawing.Point(12, 128)
$lblVer.ForeColor = $lblOut.ForeColor

$tbVersion = New-Object System.Windows.Forms.TextBox
$tbVersion.Location = New-Object System.Drawing.Point(12, 148)
$tbVersion.Width = 280

$lblPreview = New-Object System.Windows.Forms.Label
$lblPreview.Text = ""
$lblPreview.AutoSize = $false
$lblPreview.Width = 850
$lblPreview.Height = 36
$lblPreview.Location = New-Object System.Drawing.Point(12, 168)
$lblPreview.ForeColor = [System.Drawing.Color]::FromArgb(140, 200, 255)

$panelTop.Controls.AddRange(@(
    $lblOut, $rbCursor, $rbBuild, $lblTarget, $cbTarget, $chkClean, $chkVerbose,
    $chkNextVer, $chkCommit, $lblVer, $tbVersion, $lblPreview
))

$panelButtons = New-Object System.Windows.Forms.FlowLayoutPanel
$panelButtons.Dock = "Bottom"
$panelButtons.Height = 48
$panelButtons.Padding = New-Object System.Windows.Forms.Padding(12, 8, 12, 8)
$panelButtons.BackColor = [System.Drawing.Color]::FromArgb(36, 36, 40)

$btnBuild = New-Object System.Windows.Forms.Button
$btnBuild.Text = "Build"
$btnBuild.Width = 120
$btnBuild.Height = 30

$btnOpen = New-Object System.Windows.Forms.Button
$btnOpen.Text = "Open output folder"
$btnOpen.Width = 140
$btnOpen.Height = 30

$btnRelease = New-Object System.Windows.Forms.Button
$btnRelease.Text = "Run release.ps1"
$btnRelease.Width = 140
$btnRelease.Height = 30

$btnClear = New-Object System.Windows.Forms.Button
$btnClear.Text = "Clear log"
$btnClear.Width = 90
$btnClear.Height = 30

$panelButtons.Controls.AddRange(@($btnBuild, $btnOpen, $btnRelease, $btnClear))

$txtLog = New-Object System.Windows.Forms.TextBox
$txtLog.Multiline = $true
$txtLog.ReadOnly = $true
$txtLog.ScrollBars = "Both"
$txtLog.Dock = "Fill"
$txtLog.Font = New-Object System.Drawing.Font("Consolas", 9.0)
$txtLog.BackColor = [System.Drawing.Color]::FromArgb(24, 24, 28)
$txtLog.ForeColor = [System.Drawing.Color]::FromArgb(200, 205, 210)

$status = New-Object System.Windows.Forms.StatusStrip
$status.BackColor = [System.Drawing.Color]::FromArgb(45, 45, 48)
$lblStatus = New-Object System.Windows.Forms.ToolStripStatusLabel
$lblStatus.Text = "Ready"
$lblStatus.Spring = $true
$lblStatus.TextAlign = "MiddleLeft"
$null = $status.Items.Add($lblStatus)

# Dock order: status + toolbars first, fill area last
$form.Controls.Add($status)
$form.Controls.Add($panelButtons)
$form.Controls.Add($panelTop)
$form.Controls.Add($txtLog)

$form.Add_Shown({
    try {
        $nv = Get-NextReleaseSemVer $ProjectRoot
        $lblPreview.Text = "Next version if checked: $($nv.NextString)  (current max: $($nv.Previous.Major).$($nv.Previous.Minor).$($nv.Previous.Patch))"
    } catch {
        $lblPreview.Text = "(Could not compute preview: $($_.Exception.Message))"
    }
})

function Append-Log([string]$line) {
    if ($txtLog.InvokeRequired) {
        [void]$txtLog.Invoke([action]{ param($l) Append-Log $l }, $line)
        return
    }
    if ($null -eq $line) { return }
    $txtLog.AppendText($line + "`r`n")
    $txtLog.SelectionStart = $txtLog.Text.Length
    $txtLog.ScrollToCaret()
}

$script:Running = $false

function Start-BuildProcess {
    param(
        [string]$FilePath,
        [string]$Arguments
    )
    if ($script:Running) {
        [System.Windows.Forms.MessageBox]::Show("A task is already running.", "Busy", "OK", "Information") | Out-Null
        return
    }
    $script:Running = $true
    $lblStatus.Text = "Running..."
    $btnBuild.Enabled = $false
    $btnRelease.Enabled = $false
    Append-Log "======== $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ========"
    Append-Log "$FilePath $Arguments"

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = $Arguments
    $psi.WorkingDirectory = $ProjectRoot
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.Environment["PATH"] = $env:PATH
    $psi.Environment["MSYSTEM"] = "MINGW64"
    $psi.Environment["MINGW_PREFIX"] = "/mingw64"
    $psi.Environment["TEMP"] = $env:TEMP
    $psi.Environment["TMP"] = $env:TMP

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi
    $null = $p.add_OutputDataReceived({ param($sender, $e)
        $ln = $e.Data
        if ($null -ne $ln) {
            $form.BeginInvoke([action]{ Append-Log $ln })
        }
    })
    $null = $p.add_ErrorDataReceived({ param($sender, $e)
        $ln = $e.Data
        if ($null -ne $ln) {
            $form.BeginInvoke([action]{ Append-Log $ln })
        }
    })
    $null = $p.add_Exited({
        param($sender, $eventArgs)
        $ec = [int]$sender.ExitCode
        $form.BeginInvoke([action]{
            $script:Running = $false
            $btnBuild.Enabled = $true
            $btnRelease.Enabled = $true
            $lblStatus.Text = if ($ec -eq 0) { "Finished OK" } else { "Failed (exit $ec)" }
            Append-Log "--- exit code: $ec ---"
        })
    })
    $p.EnableRaisingEvents = $true

    try {
        [void]$p.Start()
        $p.BeginOutputReadLine()
        $p.BeginErrorReadLine()
    } catch {
        $script:Running = $false
        $btnBuild.Enabled = $true
        $btnRelease.Enabled = $true
        $lblStatus.Text = "Error"
        Append-Log $_.Exception.Message
    }
}

$btnBuild.Add_Click({
    $headless = Join-Path $ScriptDir "build-headless.ps1"
    if (-not (Test-Path $headless)) {
        [System.Windows.Forms.MessageBox]::Show("Missing: $headless", "Error", "OK", "Error") | Out-Null
        return
    }
    $outDir = if ($rbCursor.Checked) { "Cursor Build" } else { "" }
    $args = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $headless
        "-Target", $cbTarget.SelectedItem.ToString()
    )
    if ($outDir) { $args += "-OutputDir"; $args += $outDir }
    if ($chkClean.Checked) { $args += "-Clean" }
    if ($chkVerbose.Checked) { $args += "-Verbose" }
    if ($chkCommit.Checked) { $args += "-AutoCommit" }
    if ($chkNextVer.Checked) { $args += "-UseNextVersion" }
    $v = $tbVersion.Text.Trim()
    if ($v -ne "") { $args += "-Version"; $args += $v }

    $argParts = foreach ($a in $args) {
        $s = [string]$a
        if ($s -match '[\s"]') {
            '"' + ($s.Replace('"', '""')) + '"'
        } else {
            $s
        }
    }
    $argString = $argParts -join ' '
    Start-BuildProcess -FilePath "powershell.exe" -Arguments $argString
})

$btnOpen.Add_Click({
    $sub = if ($rbCursor.Checked) { "Cursor Build" } else { "Build" }
    $path = Join-Path $ProjectRoot $sub
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }
    Start-Process "explorer.exe" -ArgumentList $path
})

$btnRelease.Add_Click({
    $rel = Join-Path $ScriptDir "release.ps1"
    if (-not (Test-Path $rel)) {
        [System.Windows.Forms.MessageBox]::Show("Missing: $rel", "Error", "OK", "Error") | Out-Null
        return
    }
    $r = [System.Windows.Forms.MessageBox]::Show(
        "Run devtools\release.ps1 ?`n`nThis bumps version to max(CMake,tags)+1 by default, builds, packages, and may use gh to publish.",
        "Confirm release",
        "YesNo",
        "Question"
    )
    if ($r -ne "Yes") { return }
    $argString = "-NoProfile -ExecutionPolicy Bypass -File `"$rel`""
    Start-BuildProcess -FilePath "powershell.exe" -Arguments $argString
})

$btnClear.Add_Click({ $txtLog.Clear(); $lblStatus.Text = "Ready" })

$miExit.Add_Click({ $form.Close() })
$miAbout.Add_Click({
    [System.Windows.Forms.MessageBox]::Show(
        "Perfect Dark 2 Build GUI`n`nProject: $ProjectRoot`n`nBuilds: build-headless.ps1`nRelease: release.ps1`n`nF5 = Build",
        "About",
        "OK",
        "Information"
    ) | Out-Null
})

$form.Add_KeyDown({
    param($sender, $e)
    if ($e.KeyCode -eq "F5") { $btnBuild.PerformClick() }
})

[System.Windows.Forms.Application]::Run($form)
