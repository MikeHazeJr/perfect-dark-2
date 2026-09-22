# Requires a coordinated encoding lane before execution.
# Synthetic tones only; no third-party recordings or game audio.
param(
    [string]$Ffmpeg = 'ffmpeg',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'fixtures')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$toneCases = @(
    @{ Name = 'tone_mono_22050.ogg'; Signal = '0.35*sin(2*PI*440*t)'; Rate = 22050; Channels = 1 },
    @{ Name = 'tone_stereo_44100.ogg'; Signal = '0.35*sin(2*PI*440*t)|0.25*sin(2*PI*880*t)'; Rate = 44100; Channels = 2 },
    @{ Name = 'tone_stereo_22050.ogg'; Signal = '0.35*sin(2*PI*440*t)|0.25*sin(2*PI*880*t)'; Rate = 22050; Channels = 2 }
)
$encoderVersion = & $Ffmpeg -version
if ($LASTEXITCODE -ne 0) { throw 'ffmpeg version query failed' }
$encoderVersion | Set-Content -LiteralPath (Join-Path $OutputDirectory 'encoder-version.txt')
foreach ($tone in $toneCases) {
    $destination = Join-Path $OutputDirectory $tone.Name
    # -n refuses overwriting an existing fixture.
    & $Ffmpeg -hide_banner -nostdin -n -f lavfi -i "aevalsrc=$($tone.Signal):s=$($tone.Rate):d=0.2" -ac $tone.Channels -ar $tone.Rate -map_metadata -1 -fflags +bitexact -flags:a +bitexact -c:a libvorbis -q:a 4 $destination
    if ($LASTEXITCODE -ne 0) { throw "Tone encoding failed: $($tone.Name)" }
}
Get-FileHash -Algorithm SHA256 -LiteralPath ($toneCases | ForEach-Object { Join-Path $OutputDirectory $_.Name }) |
    Select-Object Path,Hash | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'fixture-hashes.json')
