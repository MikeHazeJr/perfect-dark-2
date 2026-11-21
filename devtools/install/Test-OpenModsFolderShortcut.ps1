#Requires -Version 5.1
param(
    [string]$ScratchRoot = [IO.Path]::GetTempPath()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$writeShortcut = Join-Path $PSScriptRoot 'Write-OpenModsFolderShortcut.ps1'
$scratchDirectory = Get-Item -LiteralPath $ScratchRoot -Force
if ($scratchDirectory.PSProvider.Name -ne 'FileSystem' -or -not $scratchDirectory.PSIsContainer -or
    ($scratchDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'The scratch root must be an existing filesystem directory without a reparse point.'
}
$testRoot = [IO.Path]::Combine($scratchDirectory.FullName, ('pd2-open-mods-' + [guid]::NewGuid().ToString('N')))
[void][IO.Directory]::CreateDirectory($testRoot)

$checks = 0
function Check([bool]$Pass, [string]$Label) {
    if (-not $Pass) { throw "FAILED: $Label" }
    $script:checks++
}

function Check-Refusal([scriptblock]$Action, [string]$Expected) {
    $message = $null
    try { & $Action } catch { $message = $_.Exception.Message }
    Check ($message -like "*$Expected*") "expected refusal '$Expected', received '$message'"
}

function Read-LinkString([IO.BinaryReader]$Reader) {
    [Text.Encoding]::Unicode.GetString($Reader.ReadBytes(2 * $Reader.ReadUInt16()))
}

$linkName = 'Open Mods Folder.lnk'
$metacharacters = '[A] $literal O''Connor; ' + [char]0x03a9
$installA = [IO.Path]::Combine($testRoot, "install $metacharacters")
$installB = [IO.Path]::Combine($testRoot, 'install B')
[void][IO.Directory]::CreateDirectory($installA)
[void][IO.Directory]::CreateDirectory($installB)
& $writeShortcut -InstallRoot $installA
& $writeShortcut -InstallRoot $installB
$linkA = [IO.Path]::Combine($installA, $linkName)
$linkB = [IO.Path]::Combine($installB, $linkName)
$linkBytes = [IO.File]::ReadAllBytes($linkA)
Check ([Convert]::ToBase64String($linkBytes) -ceq [Convert]::ToBase64String([IO.File]::ReadAllBytes($linkB))) 'link bytes match across different install roots'

$oldTimestamp = [datetime]::new(2020, 1, 1, 0, 0, 0, [DateTimeKind]::Utc)
[IO.File]::SetLastWriteTimeUtc($linkA, $oldTimestamp)
& $writeShortcut -InstallRoot $installA
Check ([IO.File]::GetLastWriteTimeUtc($linkA) -eq $oldTimestamp) 'rerunning does not replace the generated shortcut'

$stream = [IO.MemoryStream]::new($linkBytes, $false)
$reader = [IO.BinaryReader]::new($stream)
try {
    Check ($reader.ReadUInt32() -eq 0x4c) '76-byte shell link header'
    Check ([guid]::new($reader.ReadBytes(16)) -eq '00021401-0000-0000-c000-000000000046') 'shell link class ID'
    Check ($reader.ReadUInt32() -eq 0x020002a4) 'only the portable target, Unicode name, and arguments flags are set'
    $stream.Position = 60
    Check ($reader.ReadUInt32() -eq 1) 'normal shell show command and no elevation flag'
    Check ($reader.ReadUInt16() -eq 0) 'hotkey is unset at offset 64'
    Check ($reader.ReadUInt16() -eq 0) 'two-byte Reserved1 is zero at offset 66'
    Check ($reader.ReadUInt32() -eq 0) 'four-byte Reserved2 is zero at offset 68'
    Check ($reader.ReadUInt32() -eq 0) 'four-byte Reserved3 is zero at offset 72'
    Check ($stream.Position -eq 76) 'string data starts after all 76 header bytes'
    Check ((Read-LinkString $reader) -ceq 'Open your Perfect Dark mods folder') 'shortcut description'
    $arguments = Read-LinkString $reader
    Check ($reader.ReadUInt32() -eq 0x314 -and $reader.ReadUInt32() -eq 2684354561) 'valid EnvironmentVariableDataBlock'
    $expectedTarget = '%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe'
    $targetAnsi = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(260)).TrimEnd([char]0)
    $targetUnicode = [Text.Encoding]::Unicode.GetString($reader.ReadBytes(520)).TrimEnd([char]0)
    Check ($targetAnsi -ceq $expectedTarget -and $targetUnicode -ceq $expectedTarget) 'both target strings use the system Windows PowerShell'
    Check ($reader.ReadUInt32() -eq 0 -and $stream.Position -eq $stream.Length) 'terminal block with no additional shell tracking data'
} finally {
    $reader.Dispose()
    $stream.Dispose()
}

$command = [regex]::Match($arguments, '^-NoProfile -NonInteractive -WindowStyle Hidden -Command "([^"]+)"$')
Check $command.Success 'link arguments contain one self-contained PowerShell command'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseInput($command.Groups[1].Value, [ref]$tokens, [ref]$parseErrors)
Check ($parseErrors.Count -eq 0) 'exact shortcut command parses'
$tryStatement = $ast.Find({ param($node) $node -is [Management.Automation.Language.TryStatementAst] }, $false)
Check ($null -ne $tryStatement) 'folder logic has an error handler'
$tryBody = $tryStatement.Body.Extent.Text
$openBody = [scriptblock]::Create($tryBody.Substring(1, $tryBody.Length - 2))

$explorerPath = [IO.Path]::Combine([Environment]::GetFolderPath('Windows'), 'explorer.exe')
Check ($command.Groups[1].Value -notmatch 'Invoke-Item|Invoke-Expression|cmd(\.exe)?\s+/c') 'the link never opens a path by its type'
Check ($command.Groups[1].Value.Contains('Start-Process -FilePath $explorer')) 'the link starts Windows Explorer by its full path'

function Invoke-SyntheticAccount([string]$LocalAppData) {
    $original = $env:LOCALAPPDATA
    try {
        $env:LOCALAPPDATA = $LocalAppData
        # Exercise the exact linked command body with Start-Process replaced:
        # nothing is started. The stand-in checks the program is Windows
        # Explorer by its full path and returns the folder it would open.
        function Start-Process([string]$FilePath, [string]$ArgumentList) {
            if ($FilePath -cne $explorerPath) { throw "The link would start '$FilePath', not $explorerPath" }
            $quote = [string][char]34
            if (-not ($ArgumentList.StartsWith($quote) -and $ArgumentList.EndsWith($quote))) {
                throw 'The folder argument is not quoted.'
            }
            $ArgumentList.Substring(1, $ArgumentList.Length - 2)
        }
        if ((Get-Command Start-Process).CommandType -ne 'Function') {
            throw 'Refusing to run the link command: Start-Process is not replaced.'
        }
        & $openBody
    } finally {
        $env:LOCALAPPDATA = $original
    }
}

$profileA = [IO.Path]::Combine($testRoot, "profile $metacharacters")
$profileB = [IO.Path]::Combine($testRoot, 'profile B')
$modsA = [IO.Path]::Combine($profileA, 'perfectdark', 'mods')
$modsB = [IO.Path]::Combine($profileB, 'perfectdark', 'mods')
[void][IO.Directory]::CreateDirectory($profileA)
[void][IO.Directory]::CreateDirectory($profileB)
Check (-not [IO.Directory]::Exists($modsA) -and -not [IO.Directory]::Exists($modsB)) 'both synthetic folders begin absent'
Check ((Invoke-SyntheticAccount $profileA) -ceq $modsA -and [IO.Directory]::Exists($modsA)) 'first account creates and opens its literal profile path'
Check ((Invoke-SyntheticAccount $profileB) -ceq $modsB -and [IO.Directory]::Exists($modsB)) 'same shortcut creates and opens the second account path'
Check (-not [IO.Directory]::Exists([IO.Path]::Combine($installA, 'mods'))) 'opening never creates the install sample directory'
$existingMod = [IO.Path]::Combine($modsA, 'user-file.txt')
[IO.File]::WriteAllText($existingMod, 'preserve this mod')
Check ((Invoke-SyntheticAccount $profileA) -ceq $modsA -and [IO.File]::ReadAllText($existingMod) -ceq 'preserve this mod') 'opening again preserves existing files'
Check-Refusal { Invoke-SyntheticAccount 'relative-folder' } 'existing Local AppData directory'

$blockedProfile = [IO.Path]::Combine($testRoot, 'blocked-profile')
[void][IO.Directory]::CreateDirectory($blockedProfile)
$blockedMods = [IO.Path]::Combine($blockedProfile, 'perfectdark')
[IO.File]::WriteAllText($blockedMods, 'preserve this path')
Check-Refusal { Invoke-SyntheticAccount $blockedProfile } 'file or reparse point'
Check ([IO.File]::ReadAllText($blockedMods) -ceq 'preserve this path') 'a file in the profile path is preserved'

$linkedProfile = [IO.Path]::Combine($testRoot, 'linked-profile')
$linkTarget = [IO.Path]::Combine($testRoot, 'reparse-target')
[void][IO.Directory]::CreateDirectory($linkedProfile)
[void][IO.Directory]::CreateDirectory($linkTarget)
$junction = [IO.Path]::Combine($linkedProfile, 'perfectdark')
New-Item -ItemType Junction -Path $junction -Value $linkTarget | Out-Null
Check-Refusal { Invoke-SyntheticAccount $linkedProfile } 'file or reparse point'
Check (-not [IO.Directory]::Exists([IO.Path]::Combine($linkTarget, 'mods'))) 'a profile reparse point does not create a folder in its target'
Check-Refusal { & $writeShortcut -InstallRoot $junction } 'without a reparse point'

$collisionRoot = [IO.Path]::Combine($testRoot, 'collision-install')
[void][IO.Directory]::CreateDirectory($collisionRoot)
$victim = [IO.Path]::Combine($testRoot, 'linked-file.txt')
[IO.File]::WriteAllText($victim, 'preserve the linked file')
New-Item -ItemType HardLink -Path ([IO.Path]::Combine($collisionRoot, $linkName)) -Value $victim | Out-Null
Check-Refusal { & $writeShortcut -InstallRoot $collisionRoot } 'existing item blocks'
Check ([IO.File]::ReadAllText($victim) -ceq 'preserve the linked file') 'a conflicting file and its hardlink are preserved'

Write-Output "PASS: $checks shortcut checks. Synthetic fixtures retained at $testRoot"
