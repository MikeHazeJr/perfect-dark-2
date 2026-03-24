# Perfect Dark 2 — Documentation Reader
# Simple WinForms markdown/text viewer for project docs.
# Scans the docs/ and context/ folders and lets you browse them in a GUI.

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent

# --- Collect docs ---
$docFolders = @("docs", "context")
$files = @()
$missingFolders = @()
foreach ($folder in $docFolders) {
    $path = Join-Path $root $folder
    if (Test-Path $path) {
        Get-ChildItem -Path $path -Include "*.md","*.txt" -Recurse | ForEach-Object {
            $files += $_
        }
    } else {
        $missingFolders += $path
    }
}
# Also include root-level .md files
Get-ChildItem -Path $root -Filter "*.md" -File | ForEach-Object {
    $files += $_
}

# --- Build form ---
$form = New-Object System.Windows.Forms.Form
$form.Text = "Perfect Dark 2 — Documentation"
$form.Size = New-Object System.Drawing.Size(1100, 750)
$form.StartPosition = "CenterScreen"
$form.BackColor = [System.Drawing.Color]::FromArgb(30, 30, 30)
$form.ForeColor = [System.Drawing.Color]::White

$split = New-Object System.Windows.Forms.SplitContainer
$split.Dock = "Fill"
$split.SplitterDistance = 260
$split.BackColor = [System.Drawing.Color]::FromArgb(45, 45, 45)

# Left: file list
$listBox = New-Object System.Windows.Forms.ListBox
$listBox.Dock = "Fill"
$listBox.BackColor = [System.Drawing.Color]::FromArgb(30, 30, 30)
$listBox.ForeColor = [System.Drawing.Color]::FromArgb(200, 200, 200)
$listBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$listBox.BorderStyle = "None"

# Right: text view
$textBox = New-Object System.Windows.Forms.RichTextBox
$textBox.Dock = "Fill"
$textBox.BackColor = [System.Drawing.Color]::FromArgb(20, 20, 20)
$textBox.ForeColor = [System.Drawing.Color]::FromArgb(220, 220, 220)
$textBox.Font = New-Object System.Drawing.Font("Consolas", 10)
$textBox.ReadOnly = $true
$textBox.WordWrap = $true
$textBox.BorderStyle = "None"
$textBox.ScrollBars = "Vertical"

$split.Panel1.Controls.Add($listBox)
$split.Panel2.Controls.Add($textBox)
$form.Controls.Add($split)

# Populate list
$fileMap = @{}
foreach ($f in ($files | Sort-Object FullName)) {
    $rel = $f.FullName.Substring($root.Length).TrimStart('\','/')
    $listBox.Items.Add($rel) | Out-Null
    $fileMap[$rel] = $f.FullName
}

# On selection change, load file
$listBox.add_SelectedIndexChanged({
    $key = $listBox.SelectedItem
    if ($key -and $fileMap.ContainsKey($key)) {
        try {
            $content = Get-Content -Path $fileMap[$key] -Raw -Encoding UTF8
            $textBox.Text = $content
            $textBox.SelectionStart = 0
            $textBox.ScrollToCaret()
        } catch {
            $textBox.Text = "Error reading file: $_"
        }
    }
})

# Show startup errors / select first item
if ($missingFolders.Count -gt 0 -and $files.Count -eq 0) {
    $textBox.Text = "ERROR: Could not find documentation folders.`r`n`r`nSearched in:`r`n" +
                    ($missingFolders -join "`r`n") +
                    "`r`n`r`nProject root resolved to:`r`n$root`r`n`r`nCheck that this script is in the devtools/ folder."
} elseif ($listBox.Items.Count -gt 0) {
    if ($missingFolders.Count -gt 0) {
        $textBox.Text = "Note: Some folders were not found: $($missingFolders -join ', ')`r`n`r`nSelect a file from the list."
    }
    $listBox.SelectedIndex = 0
}

try {
    [System.Windows.Forms.Application]::Run($form)
} catch {
    $msg = "[$([datetime]::Now)] doc-reader.ps1 fatal error:`r`n$_`r`n$($_.ScriptStackTrace)"
    $msg | Out-File -FilePath (Join-Path $PSScriptRoot "error.log") -Append
    [System.Windows.Forms.MessageBox]::Show(
        "Fatal error:`n$_`n`nFull details written to devtools\error.log",
        "PD Doc Reader — Error",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
}
