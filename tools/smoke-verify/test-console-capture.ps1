#Requires -Version 5.1
# Tiny synthetic pipes only; no game, asset scan, desktop or profile access.
param([string]$EvidenceDirectory='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
. (Join-Path $PSScriptRoot 'lib/Console-Capture.ps1')
if (-not $EvidenceDirectory) {
    $EvidenceDirectory=Join-Path ([IO.Path]::GetTempPath()) ('pd-console-'+[guid]::NewGuid().ToString('N'))
}
Assert-SmokePlainTree -Path $EvidenceDirectory -Descendants
[void][IO.Directory]::CreateDirectory($EvidenceDirectory)
$script:cases=0
function Assert-Case([bool]$Value,[string]$Name) {
    if(-not $Value){throw "FAIL: $Name"};$script:cases++
}
$encoding=New-Object Text.UTF8Encoding($false)
$marker=$encoding.GetBytes("`n[console capture truncated; final bytes follow]`n")
foreach($size in @(0,11,256,257,131072)) {
    $data=New-Object byte[] $size
    for($i=0;$i -lt $size;$i++){$data[$i]=[byte](33+$i%80)}
    $source=New-Object IO.MemoryStream(,$data)
    $path=Join-Path $EvidenceDirectory ("bytes-$size.log")
    $drain=New-Object PdSmokeConsoleDrain($source,$path,256)
    Assert-Case ($drain.Wait(5000)) "drain $size completes"
    $actual=[IO.File]::ReadAllBytes($path)
    Assert-Case (-not $drain.Error -and $drain.BytesRead -eq $size) "drain $size reads all bytes"
    Assert-Case ($actual.Length -le 256 -and $drain.BytesWritten -eq $actual.Length) "drain $size stays bounded"
    if($size -le 256) {
        Assert-Case (-not $drain.Truncated -and [Convert]::ToBase64String($actual) -eq [Convert]::ToBase64String($data)) "exact $size bytes"
    } else {
        $suffixLength=256-128-$marker.Length
        Assert-Case ($drain.Truncated -and $actual.Length -eq 256) "truncation $size explicit"
        Assert-Case ([Convert]::ToBase64String($actual[0..127]) -eq [Convert]::ToBase64String($data[0..127])) "prefix $size preserved"
        Assert-Case ($encoding.GetString($actual,128,$marker.Length) -eq $encoding.GetString($marker)) "marker $size preserved"
        Assert-Case ([Convert]::ToBase64String($actual[(256-$suffixLength)..255]) -eq [Convert]::ToBase64String($data[($size-$suffixLength)..($size-1)])) "suffix $size preserved"
    }
    $source.Dispose()
}
$closed=New-Object IO.MemoryStream
$closed.Dispose()
$failure=New-Object PdSmokeConsoleDrain($closed,(Join-Path $EvidenceDirectory 'failed.log'),256)
Assert-Case ($failure.Wait(5000) -and [bool]$failure.Error) 'read failure recorded'
$fake=[pscustomobject]@{Stdout=$failure;Stderr=$failure;MetadataPath=(Join-Path $EvidenceDirectory 'failed.json')}
Assert-Case (-not (Complete-SmokeConsoleCapture -Capture $fake).Passed) 'read error fails capture verdict'
$helper=Join-Path $EvidenceDirectory 'pipe-child.ps1'
[IO.File]::WriteAllText($helper,@'
$out=[Console]::OpenStandardOutput()
$err=[Console]::OpenStandardError()
$buffer=New-Object byte[] 8192
for($i=0;$i -lt $buffer.Length;$i++){$buffer[$i]=65}
$out.Write([Text.Encoding]::ASCII.GetBytes('STDOUT-BEGIN'),0,12)
$err.Write([Text.Encoding]::ASCII.GetBytes('STDERR-BEGIN'),0,12)
for($i=0;$i -lt 128;$i++){$out.Write($buffer,0,$buffer.Length);$err.Write($buffer,0,$buffer.Length)}
$out.Write([Text.Encoding]::ASCII.GetBytes('STDOUT-END'),0,10)
$err.Write([Text.Encoding]::ASCII.GetBytes('STDERR-END'),0,10)
$out.Flush();$err.Flush()
'@,$encoding)
$psi=New-Object Diagnostics.ProcessStartInfo
$psi.FileName=(Get-Process -Id $PID).Path
$psi.Arguments='-NoProfile -NonInteractive -File "'+$helper+'"'
$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
$process=$null
try {
    $process=[Diagnostics.Process]::Start($psi)
    $capture=Start-SmokeConsoleCapture -Process $process -Directory $EvidenceDirectory -LimitBytes 1024
    Assert-Case ($process.WaitForExit(15000)) 'dual pipes exceeding OS buffers exit without deadlock'
    Assert-Case ($process.ExitCode -eq 0) 'synthetic child exits successfully'
    $summary=Complete-SmokeConsoleCapture -Capture $capture
    Assert-Case $summary.Passed 'both native pipe drains complete'
    foreach($row in $summary.Streams) {
        $body=[IO.File]::ReadAllText($row.Path)
        Assert-Case ($row.BytesRead -eq 1048598 -and $row.BytesWritten -eq 1024 -and $row.Truncated) ($row.Stream+' drains every byte and caps retention')
        Assert-Case ($body.StartsWith($row.Stream.ToUpperInvariant()+'-BEGIN') -and $body.EndsWith($row.Stream.ToUpperInvariant()+'-END')) ($row.Stream+' preserves beginning and final bytes')
    }
} finally {
    if($process){try{if(-not $process.HasExited){$process.Kill();[void]$process.WaitForExit(5000)}}catch{};$process.Dispose()}
}
$result=[pscustomobject]@{Passed=$true;Cases=$cases;PowerShell=$PSVersionTable.PSVersion.ToString();EvidenceDirectory=$EvidenceDirectory;GameLaunched=$false}
[IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'summary.json'),($result|ConvertTo-Json),$encoding)
$result|ConvertTo-Json -Compress
