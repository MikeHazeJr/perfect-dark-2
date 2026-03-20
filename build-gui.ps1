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

# --- Form ---
$form = New-Object System.Windows.Forms.Form
$form.Text = "Perfect Dark - Build Tool"
$form.Size = New-Object System.Drawing.Size(960, 700)
$form.StartPosition = "CenterScreen"
$form.BackColor = [System.Drawing.Color]::FromArgb(30, 30, 30)
$form.ForeColor = [System.Drawing.Color]::White
$form.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$form.FormBorderStyle = "FixedSingle"
$form.MaximizeBox = $false
$form.ShowInTaskbar = $true

# --- Title ---
$title = New-Object System.Windows.Forms.Label
$title.Text = "Perfect Dark PC Port"
$title.Font = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$title.ForeColor = [System.Drawing.Color]::FromArgb(220, 180, 60)
$title.Location = New-Object System.Drawing.Point(16, 10)
$title.AutoSize = $true
$form.Controls.Add($title)

$subtitle = New-Object System.Windows.Forms.Label
$subtitle.Text = "MinGW/MSYS2 Build System"
$subtitle.Font = New-Object System.Drawing.Font("Segoe UI", 9)
$subtitle.ForeColor = [System.Drawing.Color]::FromArgb(160, 160, 160)
$subtitle.Location = New-Object System.Drawing.Point(18, 36)
$subtitle.AutoSize = $true
$form.Controls.Add($subtitle)

# --- Status bar ---
$statusLabel = New-Object System.Windows.Forms.Label
$statusLabel.Text = "Ready"
$statusLabel.Font = New-Object System.Drawing.Font("Consolas", 9)
$statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(100, 200, 100)
$statusLabel.Location = New-Object System.Drawing.Point(500, 14)
$statusLabel.Size = New-Object System.Drawing.Size(430, 20)
$statusLabel.TextAlign = "MiddleRight"
$form.Controls.Add($statusLabel)

# --- Left button panel (vertical) ---
$buttonPanel = New-Object System.Windows.Forms.Panel
$buttonPanel.Location = New-Object System.Drawing.Point(10, 58)
$buttonPanel.Size = New-Object System.Drawing.Size(150, 580)
$buttonPanel.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 40)
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
    $btn.BackColor = [System.Drawing.Color]::FromArgb(45, 45, 45)
    $btn.Cursor = "Hand"
    $btn.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $buttonPanel.Controls.Add($btn)
    return $btn
}

# Build actions
$btnBuildClient = New-BuildButton "Build Client"    8 ([System.Drawing.Color]::FromArgb(50,220,120))
$btnBuildServer = New-BuildButton "Build Server"   50 ([System.Drawing.Color]::FromArgb(255,180,50))

# Separator 1
$btnSep0 = New-Object System.Windows.Forms.Label
$btnSep0.Text = ""; $btnSep0.Location = New-Object System.Drawing.Point(8, 94)
$btnSep0.Size = New-Object System.Drawing.Size(134, 2)
$btnSep0.BackColor = [System.Drawing.Color]::FromArgb(80,80,80)
$buttonPanel.Controls.Add($btnSep0)

# Run actions
$btnRunGame    = New-BuildButton "Run Client"   104 ([System.Drawing.Color]::FromArgb(50,220,120))
$btnRunServer  = New-BuildButton "Run Server"   146 ([System.Drawing.Color]::FromArgb(255,180,50))

# Separator 2
$btnSep1 = New-Object System.Windows.Forms.Label
$btnSep1.Text = ""; $btnSep1.Location = New-Object System.Drawing.Point(8, 190)
$btnSep1.Size = New-Object System.Drawing.Size(134, 2)
$btnSep1.BackColor = [System.Drawing.Color]::FromArgb(80,80,80)
$buttonPanel.Controls.Add($btnSep1)

# Package release (D13)
$btnPackage = New-BuildButton "Package" 200 ([System.Drawing.Color]::FromArgb(180,140,220))

# --- Right side: console + utility buttons + progress bar ---

# Output area (right of button panel)
$outputBox = New-Object System.Windows.Forms.RichTextBox
$outputBox.Location = New-Object System.Drawing.Point(170, 58)
$outputBox.Size = New-Object System.Drawing.Size(770, 502)
$outputBox.BackColor = [System.Drawing.Color]::FromArgb(20, 20, 20)
$outputBox.ForeColor = [System.Drawing.Color]::FromArgb(200, 200, 200)
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
$utilPanel.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 40)
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
    $btn.BackColor = [System.Drawing.Color]::FromArgb(45, 45, 45)
    $btn.Cursor = "Hand"
    $btn.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $utilPanel.Controls.Add($btn)
    return $btn
}

$btnCopyErrors = New-UtilButton "Copy Errors"  4 110 ([System.Drawing.Color]::FromArgb(255,120,80))
$btnCopyAll    = New-UtilButton "Copy All"   120  90 ([System.Drawing.Color]::FromArgb(160,160,160))
$btnClear      = New-UtilButton "Clear"      216  70 ([System.Drawing.Color]::FromArgb(120,120,120))

# Error count label (right-aligned in util panel)
$errorCountLabel = New-Object System.Windows.Forms.Label
$errorCountLabel.Text = ""
$errorCountLabel.Font = New-Object System.Drawing.Font("Consolas", 8, [System.Drawing.FontStyle]::Bold)
$errorCountLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 120, 80)
$errorCountLabel.Location = New-Object System.Drawing.Point(300, 8)
$errorCountLabel.Size = New-Object System.Drawing.Size(460, 20)
$errorCountLabel.TextAlign = "MiddleRight"
$utilPanel.Controls.Add($errorCountLabel)

# --- Progress bar (below util panel, full right-side width) ---
$progressPanel = New-Object System.Windows.Forms.Panel
$progressPanel.Location = New-Object System.Drawing.Point(170, 604)
$progressPanel.Size = New-Object System.Drawing.Size(770, 22)
$progressPanel.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 40)
$form.Controls.Add($progressPanel)

$progressFill = New-Object System.Windows.Forms.Panel
$progressFill.Location = New-Object System.Drawing.Point(0, 0)
$progressFill.Size = New-Object System.Drawing.Size(0, 22)
$progressFill.BackColor = [System.Drawing.Color]::FromArgb(0, 96, 191)   # PD blue during compile
$progressPanel.Controls.Add($progressFill)

$progressLabel = New-Object System.Windows.Forms.Label
$progressLabel.Text = ""
$progressLabel.Font = New-Object System.Drawing.Font("Consolas", 8, [System.Drawing.FontStyle]::Bold)
$progressLabel.ForeColor = [System.Drawing.Color]::White
$progressLabel.BackColor = [System.Drawing.Color]::Transparent
$progressLabel.Location = New-Object System.Drawing.Point(4, 3)
$progressLabel.AutoSize = $true
$progressPanel.Controls.Add($progressLabel)
$progressLabel.BringToFront()

$script:BuildPercent = 0
$script:HasErrors = $false     # Track whether any error has been seen this build

# --- Timer for draining async output queue ---
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 100

# --- Spinner ---
$script:SpinnerChars = @('|', '/', '-', '\')
$script:SpinnerIndex = 0
$script:LastOutputTime = [DateTime]::Now
$script:StepStartTime = [DateTime]::Now

# --- Helpers ---
function Write-Output-Line($text, $color) {
    $outputBox.SelectionStart = $outputBox.TextLength
    $outputBox.SelectionColor = $color
    $outputBox.AppendText("$text`r`n")
    $outputBox.ScrollToCaret()
    [void]$script:AllOutput.Add($text)
}

function Write-Header($text) {
    Write-Output-Line "" ([System.Drawing.Color]::FromArgb(80,80,80))
    Write-Output-Line ("=" * 70) ([System.Drawing.Color]::FromArgb(80,80,80))
    Write-Output-Line "  $text" ([System.Drawing.Color]::FromArgb(220,180,60))
    Write-Output-Line ("=" * 70) ([System.Drawing.Color]::FromArgb(80,80,80))
}

function Set-Buttons-Enabled($enabled) {
    $script:IsRunning = !$enabled
    $btnBuildClient.Enabled = $enabled
    $btnBuildServer.Enabled = $enabled
    Update-RunButtons
}

function Update-RunButtons {
    # Each target lives in its own build directory
    $clientExe = Join-Path $script:ClientBuildDir $script:ExeName
    $serverExe = Join-Path $script:ServerBuildDir "pd-server.x86_64.exe"

    $canRunClient = (-not $script:IsRunning) -and (Test-Path $clientExe)
    $canRunServer = (-not $script:IsRunning) -and (Test-Path $serverExe)

    # Package button: enabled when not building and at least one exe exists
    $canPackage = (-not $script:IsRunning) -and ((Test-Path $clientExe) -or (Test-Path $serverExe))

    $btnRunGame.Enabled = $canRunClient
    $btnRunServer.Enabled = $canRunServer
    $btnPackage.Enabled = $canPackage

    $btnRunGame.ForeColor = if ($canRunClient) { [System.Drawing.Color]::FromArgb(50, 220, 120) } else { [System.Drawing.Color]::FromArgb(80, 80, 80) }
    $btnRunServer.ForeColor = if ($canRunServer) { [System.Drawing.Color]::FromArgb(255, 180, 50) } else { [System.Drawing.Color]::FromArgb(80, 80, 80) }
    $btnPackage.ForeColor = if ($canPackage) { [System.Drawing.Color]::FromArgb(180, 140, 220) } else { [System.Drawing.Color]::FromArgb(80, 80, 80) }
}

function Classify-Line($line) {
    # Comprehensive error detection for GCC/G++/ld/CMake toolchains
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
        "error"   { return [System.Drawing.Color]::FromArgb(255, 100, 100) }
        "warning" { return [System.Drawing.Color]::FromArgb(230, 200, 80)  }
        "note"    { return [System.Drawing.Color]::FromArgb(130, 170, 210) }
        default   { return [System.Drawing.Color]::FromArgb(190, 190, 190) }
    }
}

# --- Process runner using .NET Tasks for reliable async I/O ---
$script:Process = $null
$script:StepQueue = [System.Collections.Queue]::new()
$script:CurrentStep = ""

function Start-Build-Step($stepName, $exe, $argList) {
    $script:CurrentStep = $stepName
    $script:LastOutputTime = [DateTime]::Now
    $script:StepStartTime = [DateTime]::Now
    $script:BuildPercent = 0
    $progressFill.Size = New-Object System.Drawing.Size(0, 22)
    # Keep red if errors already seen in a prior step, otherwise reset to blue
    if (-not $script:HasErrors) {
        $progressFill.BackColor = [System.Drawing.Color]::FromArgb(0, 96, 191)
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
    # Ensure MSYS2 environment is inherited
    $psi.EnvironmentVariables["PATH"]         = $env:PATH
    $psi.EnvironmentVariables["MSYSTEM"]      = "MINGW64"
    $psi.EnvironmentVariables["MINGW_PREFIX"] = "/mingw64"

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    try {
        [void]$proc.Start()
        $script:Process = $proc

        # Launch C# background threads to read stdout and stderr into the queue.
        # Using pure C# avoids PowerShell's variable scoping issues with .NET delegates.
        [AsyncLineReader]::StartReading($proc.StandardOutput, $script:OutputQueue, "OUT:")
        [AsyncLineReader]::StartReading($proc.StandardError,  $script:OutputQueue, "ERR:")

        $timer.Start()
    } catch {
        Write-Output-Line "Failed to start: $exe $argList" ([System.Drawing.Color]::FromArgb(255,100,100))
        Write-Output-Line $_.Exception.Message ([System.Drawing.Color]::FromArgb(255,100,100))
        $statusLabel.Text = "FAILED"
        $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 100, 100)
        Set-Buttons-Enabled $true
    }
}

# Timer tick: drain queue on UI thread (never blocks)
$timer.Add_Tick({
    $linesThisTick = 0
    $maxLinesPerTick = 80
    $line = $null

    while ($linesThisTick -lt $maxLinesPerTick -and $script:OutputQueue.TryDequeue([ref]$line)) {
        # Strip the OUT:/ERR: prefix
        $text = $line.Substring(4)

        $class = Classify-Line $text
        Write-Output-Line $text (Get-Line-Color $class)
        if ($class -eq "error") {
            [void]$script:ErrorLines.Add($text)
            # Turn progress bar red on FIRST error (immediate feedback)
            if (-not $script:HasErrors) {
                $script:HasErrors = $true
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
            }
        }

        # Parse build progress: lines like "[  5%] Building C object ..."
        if ($text -match '^\[\s*(\d+)%\]') {
            $pct = [int]$Matches[1]
            if ($pct -ge $script:BuildPercent) {
                $script:BuildPercent = $pct
                $fillWidth = [math]::Floor(($pct / 100.0) * 770)
                $progressFill.Size = New-Object System.Drawing.Size($fillWidth, 22)
                $progressLabel.Text = "${pct}% - $($script:CurrentStep)"
                # Keep red if errors have been seen, otherwise stay blue
                if (-not $script:HasErrors) {
                    $progressFill.BackColor = [System.Drawing.Color]::FromArgb(0, 96, 191)
                }
            }
        }

        $script:LastOutputTime = [DateTime]::Now
        $linesThisTick++
    }

    # Show elapsed time + spinner when no output for a while
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

    # Process has exited - wait for queue to drain then handle completion
    if ($null -ne $script:Process -and $script:Process.HasExited -and $script:OutputQueue.IsEmpty) {
        $timer.Stop()
        $exitCode = $script:Process.ExitCode
        $totalElapsed = [math]::Floor(([DateTime]::Now - $script:StepStartTime).TotalSeconds)
        try { $script:Process.Dispose() } catch {}
        $script:Process = $null

        # Update error count
        $errCount = $script:ErrorLines.Count
        if ($errCount -gt 0) {
            $errorCountLabel.Text = "$errCount error line(s) captured - click 'Copy Errors' to copy"
        }

        if ($exitCode -ne 0) {
            # --- RED: build failed ---
            $progressFill.Size = New-Object System.Drawing.Size(770, 22)
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
            $progressLabel.Text = "FAILED - $($script:CurrentStep)"
            Write-Output-Line "" ([System.Drawing.Color]::FromArgb(255,100,100))
            Write-Output-Line ">>> $($script:CurrentStep) FAILED (exit code $exitCode) after ${totalElapsed}s <<<" ([System.Drawing.Color]::FromArgb(255,100,100))
            $statusLabel.Text = "FAILED: $($script:CurrentStep)"
            $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 100, 100)
            $script:BuildSucceeded = $false
            $script:StepQueue.Clear()
            Set-Buttons-Enabled $true
            return
        }

        # --- Step succeeded: keep red if errors were seen, else blue ---
        $progressFill.Size = New-Object System.Drawing.Size(770, 22)
        if ($script:HasErrors) {
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
        } else {
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(0, 96, 191)
        }
        $progressLabel.Text = "100% - $($script:CurrentStep) Complete"
        Write-Output-Line ">>> $($script:CurrentStep) OK (${totalElapsed}s) <<<" ([System.Drawing.Color]::FromArgb(100,200,100))

        # Run next queued step
        if ($script:StepQueue.Count -gt 0) {
            $next = $script:StepQueue.Dequeue()
            Start-Build-Step $next.Name $next.Exe $next.Args
        } else {
            # --- All steps done: green if clean, red if errors were seen ---
            if ($script:HasErrors) {
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(191, 0, 0)
                $progressLabel.Text = "COMPLETE (with errors)"
            } else {
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb(0, 191, 96)
                $progressLabel.Text = "BUILD COMPLETE"
            }
            Copy-AddinFiles
            $statusLabel.Text = "Build Complete"
            $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(100, 200, 100)
            $script:BuildSucceeded = $true
            Set-Buttons-Enabled $true
        }
    }
})

# --- Build step definitions ---
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

# --- Copy addin files helper (called after build completes) ---
# Only client builds need data/mods. Server is fully self-contained.
function Copy-AddinFiles {
    if ($script:BuildTarget -eq "server") {
        Write-Header "Post-Build"
        Write-Output-Line "  Server build -- no addin files needed." ([System.Drawing.Color]::FromArgb(160,160,160))
        return
    }

    Write-Header "Copy Addin Files"

    if (!(Test-Path $script:AddinDir)) {
        Write-Output-Line "Addin directory not found: $($script:AddinDir)" ([System.Drawing.Color]::FromArgb(255,100,100))
        return
    }
    if (!(Test-Path $script:BuildDir)) {
        Write-Output-Line "Build directory not found." ([System.Drawing.Color]::FromArgb(255,100,100))
        return
    }

    $copied = 0
    # Data folder
    $dataDir = Join-Path $script:AddinDir "data"
    if (Test-Path $dataDir) {
        Copy-Item $dataDir -Destination $script:BuildDir -Recurse -Force
        Write-Output-Line "  data\" ([System.Drawing.Color]::FromArgb(180,140,220))
        $copied++
    }
    # Mods folder
    $modsDir = Join-Path $script:AddinDir "mods"
    if (Test-Path $modsDir) {
        Copy-Item $modsDir -Destination $script:BuildDir -Recurse -Force
        Write-Output-Line "  mods\" ([System.Drawing.Color]::FromArgb(180,140,220))
        $copied++
    }
    Write-Output-Line "Copied $copied items." ([System.Drawing.Color]::FromArgb(100,200,100))
}

# --- Game launch helper ---
# mode: "client" or "server"
function Launch-Game($mode) {
    if ($script:IsRunning) { return }

    if ($mode -eq "server") {
        $launchDir = $script:ServerBuildDir
        $launchExe = Join-Path $launchDir "pd-server.x86_64.exe"
        $gameArgs = "--log"
        $label = "Dedicated Server"
        $labelColor = [System.Drawing.Color]::FromArgb(255, 180, 50)

        if (!(Test-Path $launchExe)) {
            Write-Output-Line "Server not found. Build server first." ([System.Drawing.Color]::FromArgb(255,100,100))
            $statusLabel.Text = "Server not found"
            $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 100, 100)
            return
        }
    } else {
        $launchDir = $script:ClientBuildDir
        $launchExe = Join-Path $launchDir $script:ExeName
        $gameArgs = "--moddir mods/mod_allinone --gexmoddir mods/mod_gex --kakarikomoddir mods/mod_kakariko --darknoonmoddir mods/mod_dark_noon --goldfinger64moddir mods/mod_goldfinger_64 --log"
        $label = "Client"
        $labelColor = [System.Drawing.Color]::FromArgb(50, 220, 120)

        if (!(Test-Path $launchExe)) {
            Write-Output-Line "Game not found. Build client first." ([System.Drawing.Color]::FromArgb(255,100,100))
            $statusLabel.Text = "Game not found"
            $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 100, 100)
            return
        }
    }

    Write-Output-Line "" ([System.Drawing.Color]::FromArgb(80,80,80))
    Write-Output-Line "Launching $label..." $labelColor
    Write-Output-Line "  Exe: $launchExe" ([System.Drawing.Color]::FromArgb(50,180,220))
    Write-Output-Line "  Logging enabled" ([System.Drawing.Color]::FromArgb(50,180,220))

    $script:GameProcess = Start-Process -FilePath $launchExe -ArgumentList $gameArgs -WorkingDirectory $launchDir -PassThru
    Update-GameStatus
}

# --- Game process polling ---
function Update-GameStatus {
    $wasRunning = $script:GameRunning

    # Check our tracked process first
    if ($null -ne $script:GameProcess) {
        try {
            if ($script:GameProcess.HasExited) {
                $script:GameProcess = $null
                $script:GameRunning = $false
            } else {
                $script:GameRunning = $true
            }
        } catch {
            # Process handle invalidated
            $script:GameProcess = $null
            $script:GameRunning = $false
        }
    } else {
        $script:GameRunning = $false
    }

    # Fallback: also check system-wide for the exe name (catches external launches)
    if (-not $script:GameRunning) {
        try {
            $procs = Get-Process -Name ($script:ExeName -replace '\.exe$','') -ErrorAction SilentlyContinue
            $script:GameRunning = ($null -ne $procs -and $procs.Count -gt 0)
        } catch {
            $script:GameRunning = $false
        }
    }

    # Update status bar
    if ($script:GameRunning) {
        if (-not $script:IsRunning) {
            $statusLabel.Text = "Game running"
            $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(50, 220, 120)
        }
    } else {
        if ($wasRunning -and -not $script:IsRunning) {
            # Game just exited
            $statusLabel.Text = "Game exited"
            $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(160, 160, 160)
        }
    }
}

# --- Build helper (shared by both build buttons) ---
function Start-Build($target) {
    if ($script:IsRunning) { return }

    $outputBox.Clear()
    $script:ErrorLines.Clear()
    $script:AllOutput.Clear()
    $errorCountLabel.Text = ""
    $script:BuildSucceeded = $false
    $script:BuildTarget = $target
    $script:HasErrors = $false

    # Set active build directory based on target
    if ($target -eq "server") {
        $script:BuildDir = $script:ServerBuildDir
    } else {
        $script:BuildDir = $script:ClientBuildDir
    }

    $progressFill.Size = New-Object System.Drawing.Size(0, 22)
    $progressFill.BackColor = [System.Drawing.Color]::FromArgb(0, 96, 191)
    $progressLabel.Text = ""

    # Always clean build: wipe the target's build directory
    if (Test-Path $script:BuildDir) {
        Write-Header "Clean: $($script:BuildDir | Split-Path -Leaf)"
        Remove-Item -Path $script:BuildDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Output-Line "  Cleared build directory." ([System.Drawing.Color]::FromArgb(160,160,160))
    } else {
        Write-Header "Clean Build"
    }

    Set-Buttons-Enabled $false

    # Pipeline: Configure -> Build target -> Copy Files
    $script:StepQueue.Clear()
    $script:StepQueue.Enqueue((Get-BuildStep $target))

    $step = Get-ConfigureStep
    Start-Build-Step $step.Name $step.Exe $step.Args
}

# --- Package Release (D13) ---
# Creates a release/ folder with exe + SHA-256 sidecar, ready for GitHub upload.

function Get-BuildVersion {
    # Read version from the generated versioninfo.h (try client dir first, then server)
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
    if ($content -match '#define VERSION_HASH\s+"([^"]*)"') { $hash = $Matches[1] }

    if ($dev -gt 0) {
        $verStr = "$major.$minor.$patch-dev.$dev"
    } else {
        $verStr = "$major.$minor.$patch"
    }

    return @{
        Major   = $major
        Minor   = $minor
        Patch   = $patch
        Dev     = $dev
        String  = $verStr
        Hash    = $hash
        IsDev   = ($dev -gt 0)
    }
}

function Package-Release {
    if ($script:IsRunning) { return }

    $version = Get-BuildVersion
    if ($null -eq $version) {
        Write-Output-Line "Cannot read version -- build first." ([System.Drawing.Color]::FromArgb(255,100,100))
        $statusLabel.Text = "Package failed: build first"
        $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 100, 100)
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

    # --- Client ---
    if (Test-Path $clientExe) {
        $destExe = Join-Path $releaseDir "pd.x86_64.exe"
        Copy-Item $clientExe -Destination $destExe -Force
        Write-Output-Line "  Client: pd.x86_64.exe" ([System.Drawing.Color]::FromArgb(50, 220, 120))

        # SHA-256 sidecar
        $hash = (Get-FileHash -Path $destExe -Algorithm SHA256).Hash.ToLower()
        $hashFile = Join-Path $releaseDir "pd.x86_64.exe.sha256"
        "$hash  pd.x86_64.exe" | Set-Content -Path $hashFile -NoNewline
        Write-Output-Line "  SHA-256: $hash" ([System.Drawing.Color]::FromArgb(130, 170, 210))

        $fileSize = (Get-Item $destExe).Length
        $sizeMB = [math]::Round($fileSize / 1MB, 1)
        Write-Output-Line "  Size: $sizeMB MB" ([System.Drawing.Color]::FromArgb(160, 160, 160))

        $packaged++
    } else {
        Write-Output-Line "  Client: NOT FOUND (skipped)" ([System.Drawing.Color]::FromArgb(230, 200, 80))
    }

    # --- Server ---
    if (Test-Path $serverExe) {
        $destExe = Join-Path $releaseDir "pd-server.x86_64.exe"
        Copy-Item $serverExe -Destination $destExe -Force
        Write-Output-Line "  Server: pd-server.x86_64.exe" ([System.Drawing.Color]::FromArgb(255, 180, 50))

        # SHA-256 sidecar
        $hash = (Get-FileHash -Path $destExe -Algorithm SHA256).Hash.ToLower()
        $hashFile = Join-Path $releaseDir "pd-server.x86_64.exe.sha256"
        "$hash  pd-server.x86_64.exe" | Set-Content -Path $hashFile -NoNewline
        Write-Output-Line "  SHA-256: $hash" ([System.Drawing.Color]::FromArgb(130, 170, 210))

        $fileSize = (Get-Item $destExe).Length
        $sizeMB = [math]::Round($fileSize / 1MB, 1)
        Write-Output-Line "  Size: $sizeMB MB" ([System.Drawing.Color]::FromArgb(160, 160, 160))

        $packaged++
    } else {
        Write-Output-Line "  Server: NOT FOUND (skipped)" ([System.Drawing.Color]::FromArgb(230, 200, 80))
    }

    if ($packaged -eq 0) {
        Write-Output-Line "Nothing to package -- build client and/or server first." ([System.Drawing.Color]::FromArgb(255,100,100))
        $statusLabel.Text = "Package failed: no executables"
        $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 100, 100)
        return
    }

    # --- GitHub release tags ---
    Write-Output-Line "" ([System.Drawing.Color]::FromArgb(80,80,80))
    Write-Output-Line "GitHub Release Tags:" ([System.Drawing.Color]::FromArgb(220, 180, 60))

    if (Test-Path $clientExe) {
        $clientTag = "client-v$($version.String)"
        Write-Output-Line "  Client: $clientTag" ([System.Drawing.Color]::FromArgb(50, 220, 120))
    }
    if (Test-Path $serverExe) {
        $serverTag = "server-v$($version.String)"
        Write-Output-Line "  Server: $serverTag" ([System.Drawing.Color]::FromArgb(255, 180, 50))
    }

    if ($version.IsDev) {
        Write-Output-Line "  Channel: DEV (mark as prerelease on GitHub)" ([System.Drawing.Color]::FromArgb(230, 200, 80))
    } else {
        Write-Output-Line "  Channel: STABLE" ([System.Drawing.Color]::FromArgb(50, 220, 120))
    }

    Write-Output-Line "  Commit: $($version.Hash)" ([System.Drawing.Color]::FromArgb(160, 160, 160))
    Write-Output-Line "" ([System.Drawing.Color]::FromArgb(80,80,80))
    Write-Output-Line "Output: $releaseDir" ([System.Drawing.Color]::FromArgb(180, 140, 220))
    Write-Output-Line "Upload these files as GitHub Release assets." ([System.Drawing.Color]::FromArgb(160, 160, 160))

    $statusLabel.Text = "Packaged v$($version.String) ($packaged artifact(s))"
    $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(180, 140, 220)

    # Open the release folder in Explorer
    Start-Process explorer.exe -ArgumentList $releaseDir
}

# --- Button handlers ---
$btnBuildClient.Add_Click({ Start-Build "client" })
$btnBuildServer.Add_Click({ Start-Build "server" })

$btnRunGame.Add_Click({ Launch-Game "client" })
$btnRunServer.Add_Click({ Launch-Game "server" })
$btnPackage.Add_Click({ Package-Release })

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
    $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 180, 80)
})

$btnCopyAll.Add_Click({
    if ($script:AllOutput.Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show("No output to copy.", "Copy All", [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Information)
        return
    }
    $text = ($script:AllOutput -join "`r`n")
    [System.Windows.Forms.Clipboard]::SetText($text)
    $statusLabel.Text = "Full output copied to clipboard"
    $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(180, 180, 180)
})

$btnClear.Add_Click({
    $outputBox.Clear()
    $script:ErrorLines.Clear()
    $script:AllOutput.Clear()
    $errorCountLabel.Text = ""
    $statusLabel.Text = "Ready"
    $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(100, 200, 100)
})

# --- Game status polling timer (every 2 seconds) ---
$gameTimer = New-Object System.Windows.Forms.Timer
$gameTimer.Interval = 2000
$gameTimer.Add_Tick({ Update-GameStatus })
$gameTimer.Start()

# --- Initial button state (enable Run if exe already exists from prior build) ---
$clientCheck = Join-Path $script:ClientBuildDir $script:ExeName
$serverCheck = Join-Path $script:ServerBuildDir "pd-server.x86_64.exe"
if ((Test-Path $clientCheck) -or (Test-Path $serverCheck)) { $script:BuildSucceeded = $true }
Update-RunButtons

# --- Cleanup on close ---
$form.Add_FormClosing({
    $timer.Stop()
    $gameTimer.Stop()
    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        try { $script:Process.Kill() } catch {}
    }
    # Don't kill the game process -- let it keep running
})

# --- Launch ---
[void]$form.ShowDialog()
