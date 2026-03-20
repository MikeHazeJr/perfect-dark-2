# Session Log

Reverse-chronological session summaries.

---

## 2026-03-20 — Release Infrastructure & Branching

### What Was Done
- Committed all outstanding work (68 files, 10.5K insertions) to main
- Added `VERSION_LABEL` support to version system (CMake + versioninfo.h.in + updateversion.h)
  - Allows cosmetic suffixes like "a" (alpha), "b" (beta) on version strings
  - Display-only — does not affect numeric version comparison in update system
- Created `release` branch from main:
  - Version: `0.0.3a` (MAJOR=0, MINOR=0, PATCH=3, LABEL="a")
  - Tagged as `v0.0.3a`
  - Full release notes in `RELEASE_v0.0.3a.md`
  - Release script `release.ps1` for automated packaging + GitHub release
- Created `dev` branch from main:
  - Version: `0.0.4-dev.1` (MAJOR=0, MINOR=0, PATCH=4, DEV=1)
  - Tagged as `v0.0.4-dev.1`
  - Purpose: test update system's ability to detect newer versions

### Decisions Made
- Version numbering starts at `0.0.3a` (alpha) — previous GitHub releases to be removed
- Full binary replacement for updates (not patches) — build is small enough
- Three-branch model: `main` (development), `release` (stable), `dev` (testing)
- Release assets include individual exe + sha256 files (for update system) plus full zip

### Next Steps
- Mike: Build client + server on both branches, push to GitHub
- Mike: Install libcurl (`pacman -S mingw-w64-x86_64-curl`), delete CMakeCache.txt, reconfigure
- Mike: Run `release.ps1` on release branch, then on dev branch with `--Prerelease`
- Test update system: launch release build → should detect 0.0.4-dev.1 as available on dev channel
- Remove old v0.0.2 and nightly releases from GitHub
