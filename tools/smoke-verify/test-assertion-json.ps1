#Requires -Version 5.1
<# Verify failed smoke receipts serialize plain log text in Windows PowerShell. #>
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Test-Assertions.ps1')
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proof = Join-Path $repo ('.claude/smoke-storage-validation/assertion-json-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $proof | Out-Null
$checks = 0
function Assert-Check([bool]$Passed, [string]$Description) {
    if (-not $Passed) { throw $Description }
    $script:checks++
}
$line = 'SMOKE: result=wait_condition_timeout name=Joanna path=example\model'
$log = Join-Path $proof 'native.log'
[IO.File]::WriteAllText($log, $line + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
$assertions = [pscustomobject]@{forbidden_patterns=@('SMOKE: result=wait_');required_lines=@('name=Joanna');required_counts=@([pscustomobject]@{pattern='never-present';min=1;max=1})}
$result = Invoke-SmokeAssertions -LogPath $log -Assertions $assertions
Assert-Check (-not $result.Passed) 'Failed fixture must stay failed'
Assert-Check ($result.Total -eq 3 -and $result.Met -eq 1) 'Assertion counts must stay exact'
Assert-Check ($result.Failures.Count -eq 2) 'Both failure kinds must remain'
$failure = $result.Failures | Where-Object Kind -EQ 'forbidden_pattern_matched'
Assert-Check ($failure.Line -ceq $line) 'Full log text must be preserved'
Assert-Check (@($failure.Line.PSObject.Properties | Where-Object Name -NE 'Length').Count -eq 0) 'No filesystem metadata on failure text'
$watch = [Diagnostics.Stopwatch]::StartNew()
$json = ConvertTo-Json -InputObject @{failures=@($result.Failures)} -Depth 50 -Compress
$watch.Stop()
Assert-Check ([Text.Encoding]::UTF8.GetByteCount($json) -lt 1024) 'Tiny failed receipt must remain bounded'
Assert-Check ($json -notmatch 'PSPath|PSParentPath|PSProvider|PSDrive|ReadCount') 'Serialized receipt must contain no provider metadata'
$parsed = $json | ConvertFrom-Json
Assert-Check ($parsed.failures[0].Line -ceq $line) 'JSON roundtrip must preserve log text'
Assert-Check ($parsed.failures[1].Count -eq 0 -and $parsed.failures[1].Min -eq 1) 'Numeric count failure must remain exact'
$formatted = @(Format-AssertionFailures -Result $result)
Assert-Check (($formatted -join [Environment]::NewLine).Contains($line)) 'Human failure formatting must preserve its witness'
$receipt = @{passed=$true;checks=$checks;version=$PSVersionTable.PSVersion.ToString();json_bytes=[Text.Encoding]::UTF8.GetByteCount($json);serialization_seconds=$watch.Elapsed.TotalSeconds;proof=$proof}
$receipt | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $proof 'results.json') -Encoding UTF8
$receipt | ConvertTo-Json -Depth 5
