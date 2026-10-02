# Persist caller-provided readiness data only. This helper never probes a desktop.
$script:PdReadinessDefaultOutput = Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))) '.claude/pd-initial-integration/readiness/latest.json'
function Get-PdReadinessOutputPath { return $script:PdReadinessDefaultOutput }
function Save-PdReadinessOutput {
    param([System.Collections.IDictionary] $Document, [string] $OutputPath)
    if (-not $OutputPath) {
        $OutputPath = Get-PdReadinessOutputPath
    }
    $OutputPath = [IO.Path]::GetFullPath($OutputPath)
    $directory = [IO.Path]::GetDirectoryName($OutputPath)
    [void][IO.Directory]::CreateDirectory($directory)
    $historyDirectory = Join-Path $directory 'history'
    [void][IO.Directory]::CreateDirectory($historyDirectory)
    $historyPath = Join-Path $historyDirectory ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ') + '-' + [guid]::NewGuid().ToString('N') + '.json')
    $Document['output_file'] = $OutputPath
    $Document['history_file'] = $historyPath
    $json = $Document | ConvertTo-Json -Depth 6
    $utf8 = New-Object Text.UTF8Encoding($false)
    $stream = [IO.File]::Open($historyPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $bytes = $utf8.GetBytes($json + [Environment]::NewLine)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
    $temporary = Join-Path $directory ('.readiness-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::WriteAllText($temporary, $json + [Environment]::NewLine, $utf8)
        if ([IO.File]::Exists($OutputPath)) {
            # PS5.1 converts $null to an empty string for this string argument.
            [IO.File]::Replace($temporary, $OutputPath, [System.Management.Automation.Language.NullString]::Value)
        }
        else { [IO.File]::Move($temporary, $OutputPath) }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
    return [pscustomobject]@{ Json=$json; OutputPath=$OutputPath; HistoryPath=$historyPath }
}
