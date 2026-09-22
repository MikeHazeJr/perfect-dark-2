# Generate original synthetic MP3 regression fixtures in the coordinated encoding lane.
param(
    [string]$Ffmpeg = 'ffmpeg',
    [string]$OutputDirectory = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$cases = @(
    @{ Name = 'tone_mpeg1_stereo_44100.mp3'; Channels = 2; Rate = 44100; Bitrate = '128k'; Signal = '0.35*sin(2*PI*440*t)|0.25*sin(2*PI*880*t)' },
    @{ Name = 'tone_mpeg2_stereo_22050.mp3'; Channels = 2; Rate = 22050; Bitrate = '96k'; Signal = '0.35*sin(2*PI*440*t)|0.25*sin(2*PI*880*t)' },
    @{ Name = 'tone_mpeg2_mono_22050.mp3'; Channels = 1; Rate = 22050; Bitrate = '64k'; Signal = '0.35*sin(2*PI*440*t)' },
    @{ Name = 'tone_mpeg25_mono_11025.mp3'; Channels = 1; Rate = 11025; Bitrate = '32k'; Signal = '0.35*sin(2*PI*440*t)' }
)
$version = & $Ffmpeg -version
if ($LASTEXITCODE -ne 0) { throw 'ffmpeg version query failed' }
$version | Set-Content -LiteralPath (Join-Path $OutputDirectory 'encoder-version.txt')
foreach ($item in $cases) {
    $destination = Join-Path $OutputDirectory $item.Name
    & $Ffmpeg -hide_banner -nostdin -n -f lavfi -i "aevalsrc=$($item.Signal):s=$($item.Rate):d=0.2" -ac $item.Channels -ar $item.Rate -map_metadata -1 -fflags +bitexact -flags:a +bitexact -c:a libmp3lame -b:a $item.Bitrate -write_xing 0 $destination
    if ($LASTEXITCODE -ne 0) { throw "MP3 tone encoding failed: $($item.Name)" }
}
Get-FileHash -Algorithm SHA256 -LiteralPath ($cases | ForEach-Object { Join-Path $OutputDirectory $_.Name }) |
    Select-Object Path,Hash | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'fixture-hashes.json')
