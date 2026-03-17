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
$script:ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:BuildDir   = Join-Path $script:ProjectDir "build"
$script:AddinDir   = Join-Path $script:ProjectDir "..\post-batch-addin"
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
$script:LaunchBat  = "PD(All in One Mod)[US](64bit).bat"
$script:ExeName    = "pd.x86_64.exe"

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

# --- Button panel ---
$buttonPanel = New-Object System.Windows.Forms.Panel
$buttonPanel.Location = New-Object System.Drawing.Point(10, 58)
$buttonPanel.Size = New-Object System.Drawing.Size(930, 46)
$buttonPanel.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 40)
$form.Controls.Add($buttonPanel)

function New-BuildButton($text, $x, $w, $color) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text = $text
    $btn.Location = New-Object System.Drawing.Point($x, 6)
    $btn.Size = New-Object System.Drawing.Size($w, 34)
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

# Main actions
$btnBuild   = New-BuildButton "Build"    8   120 ([System.Drawing.Color]::FromArgb(220,180,60))
$btnRunGame = New-BuildButton "Run Game" 136 120 ([System.Drawing.Color]::FromArgb(50,220,120))

# Separator
$btnSep1 = New-Object System.Windows.Forms.Label
$btnSep1.Text = ""; $btnSep1.Location = New-Object System.Drawing.Point(268, 6)
$btnSep1.Size = New-Object System.Drawing.Size(2, 34)
$btnSep1.BackColor = [System.Drawing.Color]::FromArgb(80,80,80)
$buttonPanel.Controls.Add($btnSep1)

# Utility buttons
$btnCopyErrors = New-BuildButton "Copy Errors" 352 120 ([System.Drawing.Color]::FromArgb(255,120,80))
$btnCopyAll    = New-BuildButton "Copy All"     480 120 ([System.Drawing.Color]::FromArgb(160,160,160))
$btnClear      = New-BuildButton "Clear"        608 120 ([System.Drawing.Color]::FromArgb(120,120,120))

# --- Output area ---
$outputBox = New-Object System.Windows.Forms.RichTextBox
$outputBox.Location = New-Object System.Drawing.Point(10, 110)
$outputBox.Size = New-Object System.Drawing.Size(930, 474)
$outputBox.BackColor = [System.Drawing.Color]::FromArgb(20, 20, 20)
$outputBox.ForeColor = [System.Drawing.Color]::FromArgb(200, 200, 200)
$outputBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$outputBox.ReadOnly = $true
$outputBox.WordWrap = $false
$outputBox.ScrollBars = "Both"
$outputBox.BorderStyle = "None"
$form.Controls.Add($outputBox)

# --- Progress bar ---
$progressPanel = New-Object System.Windows.Forms.Panel
$progressPanel.Location = New-Object System.Drawing.Point(10, 588)
$progressPanel.Size = New-Object System.Drawing.Size(930, 22)
$progressPanel.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 40)
$form.Controls.Add($progressPanel)

$progressFill = New-Object System.Windows.Forms.Panel
$progressFill.Location = New-Object System.Drawing.Point(0, 0)
$progressFill.Size = New-Object System.Drawing.Size(0, 22)
$progressFill.BackColor = [System.Drawing.Color]::FromArgb(80, 160, 80)
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

# --- Error count label ---
$errorCountLabel = New-Object System.Windows.Forms.Label
$errorCountLabel.Text = ""
$errorCountLabel.Font = New-Object System.Drawing.Font("Consolas", 9, [System.Drawing.FontStyle]::Bold)
$errorCountLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 120, 80)
$errorCountLabel.Location = New-Object System.Drawing.Point(10, 616)
$errorCountLabel.Size = New-Object System.Drawing.Size(600, 20)
$form.Controls.Add($errorCountLabel)

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
    $btnBuild.Enabled = $enabled
    $btnRunGame.Enabled = $enabled
}

function Classify-Line($line) {
    if ($line -match ':\s*error\s*:|:\s*fatal error\s*:|^make.*\*\*\*.*Error|FAILED') {
        return "error"
    }
    if ($line -match ':\s*warning\s*:') {
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
        }

        # Parse build progress: lines like "[  5%] Building C object ..."
        if ($text -match '^\[\s*(\d+)%\]') {
            $pct = [int]$Matches[1]
            if ($pct -ge $script:BuildPercent) {
                $script:BuildPercent = $pct
                $fillWidth = [math]::Floor(($pct / 100.0) * 930)
                $progressFill.Size = New-Object System.Drawing.Size($fillWidth, 22)
                $progressLabel.Text = "${pct}% - $($script:CurrentStep)"
                $r = [math]::Max(80, [int](220 - ($pct * 1.4)))
                $g = [math]::Min(200, [int](120 + ($pct * 0.8)))
                $progressFill.BackColor = [System.Drawing.Color]::FromArgb($r, $g, 60)
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
            $progressFill.BackColor = [System.Drawing.Color]::FromArgb(200, 60, 60)
            $progressLabel.Text = "FAILED - $($script:CurrentStep)"
            Write-Output-Line "" ([System.Drawing.Color]::FromArgb(255,100,100))
            Write-Output-Line ">>> $($script:CurrentStep) FAILED (exit code $exitCode) after ${totalElapsed}s <<<" ([System.Drawing.Color]::FromArgb(255,100,100))
            $statusLabel.Text = "FAILED: $($script:CurrentStep)"
            $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 100, 100)
            $script:StepQueue.Clear()
            Set-Buttons-Enabled $true
            return
        }

        $progressFill.Size = New-Object System.Drawing.Size(930, 22)
        $progressFill.BackColor = [System.Drawing.Color]::FromArgb(80, 200, 80)
        $progressLabel.Text = "100% - $($script:CurrentStep) Complete"
        Write-Output-Line ">>> $($script:CurrentStep) OK (${totalElapsed}s) <<<" ([System.Drawing.Color]::FromArgb(100,200,100))

        # Run next queued step
        if ($script:StepQueue.Count -gt 0) {
            $next = $script:StepQueue.Dequeue()
            Start-Build-Step $next.Name $next.Exe $next.Args
        } else {
            # All build steps done — copy addin files
            Copy-AddinFiles
            $statusLabel.Text = "Build Complete"
            $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(100, 200, 100)
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

function Get-BuildStep {
    $cores = $env:NUMBER_OF_PROCESSORS
    if (!$cores) { $cores = 4 }
    return @{
        Name = "Build (Compile)"
        Exe  = $script:CMake
        Args = "--build `"$($script:BuildDir)`" -- -j$cores"
    }
}

# --- Copy addin files helper (called after build completes) ---
function Copy-AddinFiles {
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
    # DLLs
    Get-ChildItem "$($script:AddinDir)\*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName -Destination $script:BuildDir -Force
        Write-Output-Line "  $($_.Name)" ([System.Drawing.Color]::FromArgb(180,140,220))
        $copied++
    }
    # BAT launchers (use -LiteralPath to handle parentheses in filename)
    Get-ChildItem -LiteralPath $script:AddinDir -Filter "*.bat" -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $script:BuildDir -Force
        Write-Output-Line "  $($_.Name)" ([System.Drawing.Color]::FromArgb(180,140,220))
        $copied++
    }
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

# --- Button handlers ---
$btnBuild.Add_Click({
    if ($script:IsRunning) { return }

    # Clear console for a clean slate
    $outputBox.Clear()
    $script:ErrorLines.Clear()
    $script:AllOutput.Clear()
    $errorCountLabel.Text = ""
    $progressFill.Size = New-Object System.Drawing.Size(0, 22)
    $progressLabel.Text = ""

    # Always clean before building
    Write-Header "Clean"
    if (Test-Path $script:BuildDir) {
        try {
            Remove-Item -Recurse -Force $script:BuildDir
            Write-Output-Line "Build directory removed." ([System.Drawing.Color]::FromArgb(100,200,100))
        } catch {
            Write-Output-Line "Failed to remove build dir: $_" ([System.Drawing.Color]::FromArgb(255,100,100))
        }
    } else {
        Write-Output-Line "Already clean." ([System.Drawing.Color]::FromArgb(180,180,180))
    }

    Set-Buttons-Enabled $false

    # Pipeline: Configure → Build → Copy Files
    $script:StepQueue.Clear()
    $script:StepQueue.Enqueue((Get-BuildStep))

    $step = Get-ConfigureStep
    Start-Build-Step $step.Name $step.Exe $step.Args
})

$btnRunGame.Add_Click({
    if ($script:IsRunning) { return }

    # Try the launch BAT first, then fall back to exe directly
    $launchBat = Join-Path $script:BuildDir $script:LaunchBat
    $launchExe = Join-Path $script:BuildDir $script:ExeName

    if (Test-Path -LiteralPath $launchBat) {
        Write-Output-Line "" ([System.Drawing.Color]::FromArgb(80,80,80))
        Write-Output-Line "Launching game via $($script:LaunchBat)..." ([System.Drawing.Color]::FromArgb(50,220,120))
        $statusLabel.Text = "Game running..."
        $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(50, 220, 120)
        Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$launchBat`"" -WorkingDirectory $script:BuildDir
    } elseif (Test-Path $launchExe) {
        Write-Output-Line "" ([System.Drawing.Color]::FromArgb(80,80,80))
        Write-Output-Line "Launching game via $($script:ExeName) (no launch BAT found)..." ([System.Drawing.Color]::FromArgb(50,220,120))
        $statusLabel.Text = "Game running..."
        $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(50, 220, 120)
        Start-Process -FilePath $launchExe -WorkingDirectory $script:BuildDir
    } else {
        Write-Output-Line "Game not found. Build first, then Copy Files." ([System.Drawing.Color]::FromArgb(255,100,100))
        $statusLabel.Text = "Game not found"
        $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 100, 100)
    }
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

# --- Cleanup on close ---
$form.Add_FormClosing({
    $timer.Stop()
    if ($null -ne $script:Process -and !$script:Process.HasExited) {
        try { $script:Process.Kill() } catch {}
    }
})

# --- Launch ---
[void]$form.ShowDialog()
