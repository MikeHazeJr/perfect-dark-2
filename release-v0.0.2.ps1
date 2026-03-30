# Perfect Dark 2 — v0.0.2 Release Script
# Run this from the project root (perfect_dark-mike/)

Write-Host "=== Perfect Dark 2 v0.0.2 Release ===" -ForegroundColor Cyan

# 1. Stage all changes
Write-Host "`n[1/5] Staging all changes..." -ForegroundColor Yellow
git add -A

# 2. Commit
Write-Host "[2/5] Committing..." -ForegroundColor Yellow
git commit -m @"
v0.0.2: ImGui menu system, character fixes, network lobby

New Features:
- ImGui menu system with F8 hot-swap (Agent Select, Create, Main Menu)
- Type-based fallback renderers for Warning (red) and Success (green) dialogs
- Network lobby player list sidebar overlay
- 3D character preview via FBO render-to-texture
- PD-authentic visual style (shimmer, palettes, gradients, text glow)
- Agent Create with name input, body/head carousel, opaque backdrop
- Delete/Copy confirmation prompts (Press A to confirm, B to cancel)
- LB/RB bumper tab switching in Settings

Bug Fixes:
- Skedar & Dr. Carroll mesh loading (duplicate g_MpBodies indices)
- Delete confirmation crash (nested ImGui::Begin)
- Jump height setting disconnected from bondwalk.c
- Dropdown/combo focus stealing
- Movement not inhibited during menus
- Network menu item vertical overlap (\\n escaping)
- langGet signature mismatch (u16 vs s32)
- pdguiNewFrame guard mismatch causing lobby render crash
- Overlay menus now opaque (not see-through)

Infrastructure:
- 14 new source files, 7 modified core files
- 52/233 menu dialogs covered (~22%)
- C/C++ bridge system for safe types.h-free struct access
- Enhanced network session logging (stage start/end detail)
"@

# 3. Tag
Write-Host "[3/5] Creating tag v0.0.2..." -ForegroundColor Yellow
git tag -a v0.0.2 -m "v0.0.2: ImGui menu system, character fixes, network lobby"

# 4. Push
Write-Host "[4/5] Pushing to origin..." -ForegroundColor Yellow
git push origin main --tags

# 5. Create GitHub release with build artifact
Write-Host "[5/5] Creating GitHub release..." -ForegroundColor Yellow

# Package the build folder
$buildZip = "perfect-dark-2-v0.0.2-win64.zip"
if (Test-Path "build/pd.x86_64.exe") {
    Write-Host "  Packaging build..." -ForegroundColor Gray
    Compress-Archive -Path "build/pd.x86_64.exe","build/SDL2.dll","build/libwinpthread-1.dll","build/zlib1.dll","build/data","build/mods" -DestinationPath $buildZip -Force
    Write-Host "  Created $buildZip" -ForegroundColor Green
} else {
    Write-Host "  WARNING: build/pd.x86_64.exe not found, skipping build package" -ForegroundColor Red
    $buildZip = $null
}

# Create the release
$releaseArgs = @(
    "release", "create", "v0.0.2",
    "--title", "v0.0.2 — ImGui Menu System, Character Fixes, Network Lobby",
    "--notes-file", "RELEASE_v0.0.2.md"
)

if ($buildZip -and (Test-Path $buildZip)) {
    $releaseArgs += $buildZip
}

gh @releaseArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== Release v0.0.2 created successfully! ===" -ForegroundColor Green
    if ($buildZip) { Remove-Item $buildZip -ErrorAction SilentlyContinue }
} else {
    Write-Host "`n=== Release creation failed. Check gh auth status. ===" -ForegroundColor Red
}
