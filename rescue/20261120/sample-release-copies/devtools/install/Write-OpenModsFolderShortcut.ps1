#Requires -Version 5.1
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$installDirectory = Get-Item -LiteralPath $InstallRoot -Force
if ($installDirectory.PSProvider.Name -ne 'FileSystem' -or -not $installDirectory.PSIsContainer -or
    ($installDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'The shortcut destination must be an existing install directory without a reparse point.'
}
$shortcutPath = [IO.Path]::Combine($installDirectory.FullName, 'Open Mods Folder.lnk')

# Keep the same Windows profile root and relative path as fsModRoots.
# Resolve the profile at launch, so the same shortcut works for each account.
$openFolder = @'
try {
    $ErrorActionPreference = 'Stop';
    $folder = $env:LOCALAPPDATA;
    if ($folder -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^?.\\/][^\\/]*[\\/][^\\/]+)' -or -not [IO.Directory]::Exists($folder)) {
        throw 'Windows did not supply an existing Local AppData directory.';
    };
    foreach ($leaf in 'perfectdark', 'mods') {
        $folder = [IO.Path]::Combine($folder, $leaf);
        $entry = Get-Item -LiteralPath $folder -Force -ErrorAction SilentlyContinue;
        if ($entry -and (-not $entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint))) {
            throw 'A file or reparse point occupies the Perfect Dark mods path.';
        };
        [void][IO.Directory]::CreateDirectory($folder);
    };
    Invoke-Item -LiteralPath $folder;
} catch {
    Add-Type -AssemblyName System.Windows.Forms;
    [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Open Mods Folder');
    exit 1;
}
'@
$arguments = '-NoProfile -NonInteractive -WindowStyle Hidden -Command "' +
    (($openFolder -split '\r?\n' | ForEach-Object { $_.Trim() }) -join ' ') + '"'
$description = 'Open your Perfect Dark mods folder'
$target = '%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe'

# MS-SHLLINK carries the portable target in an EnvironmentVariableDataBlock.
# Omit account- and machine-specific paths, ID lists and tracking metadata.
$stream = [IO.MemoryStream]::new()
$writer = [IO.BinaryWriter]::new($stream)
try {
    $writer.Write([uint32]0x4c)
    $writer.Write([Guid]::Parse('00021401-0000-0000-c000-000000000046').ToByteArray())
    # PreferEnvironmentPath | HasExpString | IsUnicode | HasArguments | HasName
    $writer.Write([uint32]0x020002a4)
    $writer.Write([uint32]0) # File attributes
    $writer.Write([byte[]]::new(24)) # Creation, access and write times
    $writer.Write([uint32]0) # File size
    $writer.Write([int32]0) # Icon index
    $writer.Write([uint32]1) # SW_SHOWNORMAL
    $writer.Write([uint16]0) # HotKey
    $writer.Write([uint16]0) # Reserved1
    $writer.Write([uint32]0) # Reserved2
    $writer.Write([uint32]0) # Reserved3

    foreach ($stringData in $description, $arguments) {
        $writer.Write([uint16]$stringData.Length)
        $writer.Write([Text.Encoding]::Unicode.GetBytes($stringData))
    }

    $writer.Write([uint32]0x314) # EnvironmentVariableDataBlock size
    $writer.Write([uint32]2684354561) # EnvironmentVariableDataBlock signature, 0xa0000001
    $targetAnsi = [byte[]]::new(260)
    $targetUnicode = [byte[]]::new(520)
    [Text.Encoding]::ASCII.GetBytes($target).CopyTo($targetAnsi, 0)
    [Text.Encoding]::Unicode.GetBytes($target).CopyTo($targetUnicode, 0)
    $writer.Write($targetAnsi)
    $writer.Write($targetUnicode)
    $writer.Write([uint32]0) # TerminalBlock
    $writer.Flush()
    $shortcutBytes = $stream.ToArray()
} finally {
    $writer.Dispose()
    $stream.Dispose()
}

$existing = Get-Item -LiteralPath $shortcutPath -Force -ErrorAction SilentlyContinue
if ($existing) {
    if ($existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $existing.Length -ne $shortcutBytes.Length -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($shortcutPath)) -cne [Convert]::ToBase64String($shortcutBytes)) {
        throw "An existing item blocks the Open Mods Folder shortcut: $shortcutPath"
    }
    return
}

# CreateNew also refuses an item created at this path after the lookup.
$output = [IO.File]::Open($shortcutPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    $output.Write($shortcutBytes, 0, $shortcutBytes.Length)
} finally {
    $output.Dispose()
}
