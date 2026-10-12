# Retained wrapper protocol. Existing storage owns archive accounting and locks.
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/smoke-verify/lib/Storage-Harness.ps1')
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/smoke-verify/lib/Runner-Retention.ps1')

# No downstream source has a released, reviewed retained-build capability yet.
# Populate only in the separately owned/reviewed builder integration cohort.
$script:BuildRetentionReviewedChildSha256=''
$script:BuildRetentionContext=$null
$script:BuildRetentionChild=$null
$script:BuildRetentionQueueLock=$null

function Assert-BuildRetentionOptions([bool]$Retained,[hashtable]$Modes,[string]$DevMods,[string]$Session) {
    if(-not $Retained){return}
    foreach($name in @('Clean','NoQueue','Remove','RemoveAll','SelfTest','Force','List')){
        if($Modes[$name]){throw "Retained build refuses $name before setup."}
    }
    if($DevMods){throw 'Retained build refuses DevMods before setup.'}
    if(-not $Session){throw 'Retained build requires an explicit stable Session.'}
}

function Assert-BuildRetainedChild([string]$Path) {
    Assert-SmokePlainTree -Path $Path
    $approved=$script:BuildRetentionReviewedChildSha256
    if(-not $approved -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $approved){
        throw 'Retained build child has no released, reviewed exact capability; no queue, output or native launch admitted.'
    }
}

function Assert-BuildProtocolPath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    $root=[IO.Path]::GetFullPath($SessionBuildRoot).TrimEnd('\')
    if(-not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Build protocol path escaped session root.'}
    Assert-SmokePlainTree -Path $full
    return $full
}

function Get-BuildProtocolBindingDirectory([string]$Path) {
    $full=Assert-BuildProtocolPath $Path
    $hash=[Security.Cryptography.SHA256]::Create()
    try {$id=[BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($full.ToLowerInvariant()))).Replace('-','').ToLowerInvariant()}
    finally {$hash.Dispose()}
    return Join-Path (Join-Path $QueueDir '.retained-bindings') $id
}

function Test-BuildProtocolRetained([string]$Path) {
    # A retained binding survives malformed JSON and later wrapper invocations.
    $binding=Get-BuildProtocolBindingDirectory $Path
    Assert-SmokePlainTree -Path $binding -Descendants
    return Test-Path -LiteralPath $binding
}

function Write-BuildProtocolReceipt([string]$Path,[string]$Phase,[byte[]]$Bytes) {
    if(-not $script:BuildRetentionContext){throw 'Build retention context missing.'}
    if($script:BuildRetentionContext.Owner.pid -ne $PID){throw 'Build receipt owner mismatch.'}
    $full=Assert-BuildProtocolPath $Path
    if($null -eq $Bytes){$Bytes=[byte[]]@()}
    $hash=[Security.Cryptography.SHA256]::Create()
    try {$digest=[BitConverter]::ToString($hash.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant()}
    finally {$hash.Dispose()}
    $value=@{schema='pd2.build-protocol-receipt.v1';path=$full;phase=$Phase;bytes=$Bytes.Length;
        sha256=$digest;raw_base64=[Convert]::ToBase64String($Bytes);
        run_id=$script:BuildRetentionContext.RunId;owner=$script:BuildRetentionContext.Owner}
    return Write-SmokeRunnerRetainedJson -Context $script:BuildRetentionContext -Name ('protocol-'+[guid]::NewGuid().ToString('N')+'.json') -Value $value
}

function New-BuildProtocolBinding([string]$Path) {
    $directory=Get-BuildProtocolBindingDirectory $Path
    Assert-SmokePlainTree -Path $directory -Descendants
    [void][IO.Directory]::CreateDirectory($directory)
    $destination=Join-Path $directory ($script:BuildRetentionContext.RunId+'.json')
    Assert-SmokePlainTree -Path $destination
    $value=@{schema='pd2.build-protocol-binding.v1';path=[IO.Path]::GetFullPath($Path);
        run_id=$script:BuildRetentionContext.RunId;owner=$script:BuildRetentionContext.Owner}
    $bytes=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $value -Depth 8 -Compress))
    if(Test-Path -LiteralPath $destination){
        if([Convert]::ToBase64String([IO.File]::ReadAllBytes($destination)) -cne [Convert]::ToBase64String($bytes)){throw 'Build retained binding collision.'}
        return
    }
    [void](Write-BuildProtocolReceipt -Path $Path -Phase 'binding' -Bytes $bytes)
    $stream=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)} finally {$stream.Dispose()}
}

function ConvertFrom-BuildRetainedDurationBytes([byte[]]$Bytes) {
    try {
        $text=[Text.Encoding]::UTF8.GetString($Bytes).TrimStart([char]0xfeff).Trim()
        if(-not $text -or $text[0] -notin @('[','{')){throw 'Unsupported history envelope.'}
        $parsed=$text|ConvertFrom-Json -ErrorAction Stop
        $rows=@($parsed)
        if($null -eq $parsed){
            if($text -match '^\[\s*\]$'){return}
            throw 'Null duration history.'
        }
        foreach($row in $rows){
            foreach($field in @('Session','Target','ExitCode','DurationSeconds','CompletedUtc')){
                if(-not $row.PSObject.Properties.Match($field).Count){throw ('Missing '+$field)}
            }
            if($row.Session -isnot [string] -or -not $row.Session -or
               $row.Target -notin @('client','updater','tests','settings-tests','probe','all') -or
               $row.ExitCode -isnot [ValueType] -or $row.ExitCode -is [bool] -or
               $row.DurationSeconds -isnot [ValueType] -or $row.DurationSeconds -is [bool] -or
               [double]::IsNaN([double]$row.DurationSeconds) -or [double]::IsInfinity([double]$row.DurationSeconds) -or
               [double]$row.DurationSeconds -lt 0){throw 'Unsupported duration record.'}
            [void][DateTime]::Parse([string]$row.CompletedUtc,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind)
        }
        return $rows
    } catch {throw ('Retained build refuses malformed or unsupported duration history: '+$_.Exception.Message)}
}

function Read-BuildRetainedDurationHistory {
    Assert-SmokePlainTree -Path $QueueDurationsPath
    if(-not (Test-Path -LiteralPath $QueueDurationsPath)){return}
    return ConvertFrom-BuildRetainedDurationBytes ([IO.File]::ReadAllBytes($QueueDurationsPath))
}

function Assert-BuildOwnedProtocolBytes([string]$Path,[byte[]]$Bytes,[string]$RequestId='') {
    if($Path -eq $QueueDurationsPath){[void](ConvertFrom-BuildRetainedDurationBytes $Bytes);return}
    $record=[Text.Encoding]::UTF8.GetString($Bytes).TrimStart([char]0xfeff)|ConvertFrom-Json
    if($record.WrapperPid -ne $PID -or $record.RetentionRunId -cne $script:BuildRetentionContext.RunId -or
       ($RequestId -and $record.RequestId -cne $RequestId)){throw 'Build protocol is not this invocation owned record.'}
}

function Write-BuildRetainedProtocol([string]$Path,$Value) {
    if($null -eq $script:BuildRetentionQueueLock -or -not $script:BuildRetentionQueueLock.CanWrite -or
        -not [string]::Equals($script:BuildRetentionQueueLock.Name,$QueueLockPath,[StringComparison]::OrdinalIgnoreCase)){
        throw 'Retained queue write requires the canonical queue state lock.'
    }
    $full=Assert-BuildProtocolPath $Path
    $request=([IO.Path]::GetDirectoryName($full) -eq $QueueDir -and $full.EndsWith('.request.json'))
    if($full -ne $QueueDurationsPath -and $full -ne $QueueActivePath -and -not $request){throw 'Unknown retained queue output.'}
    $bytes=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $Value -Depth 8))
    Assert-BuildOwnedProtocolBytes $full $bytes
    $exists=Test-Path -LiteralPath $full
    if($exists -and $full.EndsWith('.request.json')){throw 'Retained request output collision.'}
    [void](Write-BuildProtocolReceipt -Path $full -Phase 'write-intent' -Bytes $bytes)
    New-BuildProtocolBinding $full
    $mode=if($exists){[IO.FileMode]::Open}else{[IO.FileMode]::CreateNew}
    $stream=[IO.File]::Open($full,$mode,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {
        if($exists){
            $old=New-Object byte[] ([int]$stream.Length)
            if($stream.Read($old,0,$old.Length) -ne $old.Length){throw 'Incomplete old queue record read.'}
            $id=if($full -eq $QueueDurationsPath){''}else{[string]$Value.RequestId}
            Assert-BuildOwnedProtocolBytes $full $old $id
            [void](Write-BuildProtocolReceipt -Path $full -Phase 'before-replace' -Bytes $old)
        }
        $stream.Position=0;$stream.SetLength(0);$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)
    } finally {$stream.Dispose()}
}

function Save-BuildOwnedProtocolRelease([string]$Path,[string]$RequestId) {
    if($null -eq $script:BuildRetentionQueueLock -or -not $script:BuildRetentionQueueLock.CanWrite -or
        -not [string]::Equals($script:BuildRetentionQueueLock.Name,$QueueLockPath,[StringComparison]::OrdinalIgnoreCase)){
        throw 'Retained queue release requires the canonical queue state lock.'
    }
    $full=Assert-BuildProtocolPath $Path
    $stream=[IO.File]::Open($full,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try {
        $bytes=New-Object byte[] ([int]$stream.Length)
        if($stream.Read($bytes,0,$bytes.Length) -ne $bytes.Length){throw 'Incomplete release record read.'}
        Assert-BuildOwnedProtocolBytes $full $bytes $RequestId
        [void](Write-BuildProtocolReceipt -Path $full -Phase 'before-own-release' -Bytes $bytes)
    } finally {$stream.Dispose()}
}

function Save-BuildOwnedSessionLock($Lock) {
    if($Lock.RetentionRunId -cne $script:BuildRetentionContext.RunId -or $Lock.OwnerPid -ne $PID){throw 'Build lock owner mismatch.'}
    $stream=$Lock.Stream
    $stream.Flush($true);$position=$stream.Position;$stream.Position=0
    try {
        $bytes=New-Object byte[] ([int]$stream.Length)
        if($stream.Read($bytes,0,$bytes.Length) -ne $bytes.Length){throw 'Incomplete owned lock read.'}
        [void](Write-BuildProtocolReceipt -Path $Lock.Path -Phase 'before-own-lock-release' -Bytes $bytes)
    } finally {$stream.Position=$position}
}

function Assert-BuildRetainedCloseout($QueueToken) {
    if(-not $script:BuildRetentionContext -or $null -eq $QueueToken){return}
    $active=Read-QueueJson $QueueActivePath
    if($null -eq $active -or $active.RequestId -cne $QueueToken.RequestId -or $active.WrapperPid -ne $PID){
        throw 'Retained build queue closeout unverified; retain own protocol locks.'
    }
    if([int]$active.ChildPid -gt 0){
        $child=$script:BuildRetentionChild
        if($null -eq $child -or $child.Id -ne [int]$active.ChildPid -or -not $child.HasExited){
            throw 'Retained build child closeout unverified; retain own protocol locks.'
        }
    }
}

function New-BuildRetentionInvocation([string]$BuildDir,[string]$SessionName) {
    $context=New-SmokeRunnerRetentionContext -ProjectRoot $ProjectDir
    $value=@{schema='pd2.build-retention-context.v1';run_id=$context.RunId;owner=$context.Owner;
        project_root=$ProjectDir;build_dir=$BuildDir;session=$SessionName;target=$Target;
        created_utc=[DateTime]::UtcNow.ToString('o');stdout=(Join-Path $context.RunRoot '_build-session.out.log');
        stderr=(Join-Path $context.RunRoot '_build-session.err.log');reviewed_child_sha256=$script:BuildRetentionReviewedChildSha256}
    $receipt=Write-SmokeRunnerRetainedJson -Context $context -Name 'build-context.json' -Value $value
    $context|Add-Member -NotePropertyName BuildContextPath -NotePropertyValue $receipt.Path
    return $context
}

function Show-BuildRetainedTail([string]$SessionName,[int]$Lines,[bool]$Follow) {
    $receipts=Join-Path $ProjectDir '.claude/smoke-storage/receipts'
    Assert-SmokePlainTree -Path $receipts -Descendants
    $matches=@(foreach($directory in @(Get-ChildItem -LiteralPath $receipts -Directory -Filter 'runner-*' -ErrorAction SilentlyContinue)){
        $path=Join-Path $directory.FullName 'build-context.json'
        if(Test-Path -LiteralPath $path){
            $record=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
            if($record.schema -cne 'pd2.build-retention-context.v1'){throw 'Invalid retained build context.'}
            if($record.session -ceq $SessionName -and $record.project_root -ceq $ProjectDir){
                foreach($leaf in @('stdout','stderr')){
                    $expected=Join-Path $directory.FullName ('_build-session.'+$(if($leaf -eq 'stdout'){'out'}else{'err'})+'.log')
                    if($record.$leaf -cne $expected){throw 'Retained build log path escaped owned receipt.'}
                }
                $record
            }
        }
    })
    $record=$matches|Sort-Object created_utc -Descending|Select-Object -First 1
    if($null -eq $record){throw 'No retained build log context for this session.'}
    foreach($path in @($record.stdout,$record.stderr)){
        Assert-SmokePlainTree -Path $path
        if(Test-Path -LiteralPath $path){
            if($Follow){Get-Content -LiteralPath $path -Tail $Lines -Wait}else{Get-Content -LiteralPath $path -Tail $Lines}
        }
    }
}
