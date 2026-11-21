# Native smoke for imported Grid atmosphere

This single-client fixture uses an archive produced by the committed model importer. It checks stage-wide rules through the native stage loader without exercising the Grid browser, editor Save As, or online dependency transfer.

Prepare a fresh, owned template from the frozen local-dev client and an approved ROM. A compatible extracted `data` tree from an approved immutable test input can shorten the run. Do not use a personal or protected install. The Model Room source GLB is already available in the canonical checkout with SHA-256 `A6B8BBD0442174282DF7E60B450203E356426AB4DD83551EECFAD3085DD46B8C`. There is no need to run the generator that replaces files in `dev-mods/model_room`.

From the canonical checkout, after build admission and with an approved template destination and input ROM, set `$work` to the `grid-exact-candidate` task folder and `$rom` to the exact approved `pd.*.z64` path. The managed runner's `-Install` option reads the template and runs a private copy.

```powershell
$checkout = (Get-Location).Path
$exe = Join-Path $checkout '.claude/session-builds/grid-78a80000/PerfectDark.exe'
$template = Join-Path $work 'grid-stage-template'
if (Test-Path -LiteralPath $template) { throw 'Choose a new disposable template path.' }
if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or !(Test-Path -LiteralPath $rom -PathType Leaf)) {
    throw 'The frozen candidate and approved ROM are required.'
}
New-Item -ItemType Directory -Path $template | Out-Null
Copy-Item -LiteralPath @($exe, $rom) -Destination $template
Get-ChildItem -LiteralPath (Split-Path -Parent $exe) -Filter '*.dll' -File |
    Copy-Item -Destination $template

$profile = Join-Path $template 'grid-stage-verify'
$mod = Join-Path $profile 'mods/grid_stage_import'
$stage = Join-Path $work 'grid-import-stage.json'
$source = '{"schema":"pd2.scenario.stage.v1","atmosphere":{"sky_rgb":[15,35,75],"fog":{"start":120,"end":480},"ambient_light":104,"primary_music":"base:song_sequence_b"}}'
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($stage, $source, $utf8)
python -B tools/build_level_from_model.py dev-mods/model_room/model_room.glb `
    --id grid_stage_import --name 'Grid Stage Import' --stage-rules $stage --out $mod
if ($LASTEXITCODE -ne 0) { throw 'The importer refused this input.' }
[IO.File]::WriteAllText((Join-Path $profile 'mods-enabled.json'), '["grid_stage_import"]', $utf8)
Copy-Item -LiteralPath 'Tools/smoke-verify/fixtures/pdmod_migration_done.txt' `
    -Destination (Join-Path $profile 'mods/.pdmod-migration-done')
Get-FileHash -Algorithm SHA256 -LiteralPath @(
    $exe, $rom, $stage, (Join-Path $mod 'grid_stage_import.pdarena'), (Join-Path $mod 'mod.json')
)
```

Freeze the template, definition, exact source and dirty-input identity, and any extracted-base identity. Run only in the existing game-runtime turn and with the normal managed storage admission.

```powershell
.\Tools\smoke-verify\run.ps1 -TestsDir (Join-Path $work 'fixtures') `
    -Test grid_import_stage_atmosphere_smoke -Install $template -SourceBinary $exe -PerTestInstall
```

The definition uses the install-relative `--savedir` and `--moddir` with `--portable`, so the source template and personal profile stay outside the running client's writes. `-SourceSeed` excludes `-Install`; exact-base reuse currently admits only the unchanged Agent Cancel case. This draft has no staged template, executable, generated archive, or managed run receipt. Runtime still needs an exact built candidate, approved test inputs, the game-runtime FIFO turn, and separate native run storage admission. Record the fixture verdict and inspect its frame. The audio assertions check device initialization, track selection and source ingestion. Audible playback requires listening evidence.
