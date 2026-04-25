# `.pdmod` migration audit -- 2026-04-25

**Tracker:** B-238 (Priority M, M-4.1).
**Author:** AI session 2026-04-25.
**Status:** verification complete on Mike's worktree fixture; production rollout pending Mike's first launch after merge.

---

## What landed

The one-shot folder->`.pdmod` auto-migration introduced by M-4.1 fires inside `modmgrScanDirectory`, immediately after the modsdir is resolved and before the iteration loop. It is sentinel-guarded by `mods/.pdmod-migration-done`; subsequent launches skip the scan unless the sentinel is removed.

Source files:
  - `port/include/modmigrate.h`
  - `port/src/modmigrate.c`
  - `port/src/modmgr.c` (call site + `.legacy_backup` skip)
  - `port/src/modarchive_bench.c` (`--migrate-pdmod` standalone CLI)

Per-mod transition (atomic):
  1. Read `<modsdir>/<name>/mod.json` to compute the comment mirror.
  2. `modpackPdmodFromFolder(folder, <name>.pdmod.tmp)` writes the archive.
  3. `modArchiveFinish` renames `.tmp` -> `<name>.pdmod`.
  4. Re-open `<name>.pdmod` and re-extract `mod.json` to verify integrity.
  5. Rename source folder to `<name>.legacy_backup`.

Failure rollback at any step leaves the source folder untouched. The sentinel is written ONLY after a complete pass.

## Skip rules

The walker skips:
  - dotfiles
  - reserved trust-gate names (`shared`, `inbox`, `untrusted`)
  - `.legacy_backup` suffixed folders (already-migrated artifacts)
  - regular files at the root (only directories migrate)
  - directories without a root `mod.json`
  - directories that already have a sibling `<name>.pdmod`
  - directories where `<name>.legacy_backup` already exists (avoid clobber)

Skipped folders are NOT failures; the summary distinguishes packaged / skipped / failed.

## Verification recipe (operator)

```bash
# Build
source devtools/build-env.sh && ninja -C Build pd

# Optional: drop a fixture to test against
mkdir -p mods/_migrate_test_fixture/textures
cat > mods/_migrate_test_fixture/mod.json << 'EOF'
{"id":"migrate.test.fixture","name":"Migrate Fixture","version":"0.1","author":"verification"}
EOF
echo "fake texture" > mods/_migrate_test_fixture/textures/dummy.txt

# Force a fresh migration even if the sentinel exists
rm -f mods/.pdmod-migration-done

# Standalone migration run (logs to stdout, exits when done)
./Build/PerfectDark.exe --migrate-pdmod 2>&1 | grep MIGRATE:
```

Expected log shape (one line per folder, plus a summary):
```
MIGRATE: targeting '<modsdir>'
MIGRATE: scanning '<modsdir>' for legacy folder mods...
MIGRATE: packaging 'mod1' -> 'mod1.pdmod'
MIGRATE: packaged 'mod1' -> 'mod1.pdmod' + renamed source to 'mod1.legacy_backup'
...
MIGRATE: pass complete -- packaged=N skipped=M failed=K
```

## End-to-end fixture verification

Run on the worktree at HEAD (commit `5cd130f0`) against the actual folder mods present in the repo plus a synthetic fixture. All four folders packaged successfully:

| Mod folder           | Archive size | Entries | Comment mirror |
|---|---|---|---|
| `base-ui/`           | 4445 B  | 8 | `{"name":"base-ui","creator":"Rare / PD2 Team","version":"1.1.0"}` |
| `bot-names/`         |  665 B  | 1 | `{"name":"Sim Type Renames","creator":"PD2 Team","version":"1.0.0"}` |
| `pd-modern-ui/`      |  690 B  | 1 | `{"name":"pd-modern-ui","creator":"PD2 Team","version":"1.0.0"}` |
| `_migrate_test_fixture/` | 435 B | 2 | `{"name":"Migrate Fixture","creator":"M-4.1 verification","version":"0.1"}` |

Each archive opens cleanly via Python `zipfile`, contains its `mod.json` at the root, preserves nested asset paths (`themes/theme_blackgold.json` etc.), and carries the M-2.3 zip-comment mirror with the headline manifest fields. Worktree was restored to its pre-test state before commit (`git checkout -- mods/` + manual cleanup of untracked `.pdmod` and `.legacy_backup` artifacts).

Failure path was NOT exercised against a real corruption case in this audit. Static review of `modmigrate.c` confirms each of the five transition steps surfaces a `LOG_WARNING`, removes the partial archive, and leaves the source folder intact. The next launch retries because the sentinel is only written after a complete pass.

## Trust invariant verification

The migration runs ONLY against entries directly under `g_ModsDirPath`. Reserved subdirectories (`shared`, `inbox`, `untrusted`) are skipped at the walker level (`isReservedName` in `modmigrate.c`), matching the loader's `modmgrIsReservedTopLevel`. Defense-in-depth: even if an archive somehow appears under a reserved subdirectory, `modmgrTryRegisterArchive` (M-1.6) refuses to register it.

A `mods/shared/<friend>/foo/mod.json` would NOT be auto-packaged. Only the user explicitly moving it into `mods/installed/` (or, in the current layout, `mods/`) makes it a candidate.

## Migration recipe for future tools (M-3.3 hand-off)

When adding a new mod-authoring tool that should output `.pdmod` from the start, the recipe is:

1. Build a `mod.json` payload as a NUL-terminated buffer (or pass an existing folder's mod.json).
2. Call `modpackPdmodWriteSingle(out_path, manifest, len, entries, n_entries)` for in-memory builds, OR `modpackPdmodFromFolder(src_folder, out_path)` to wrap a folder.
3. Write to `<modsdir>/<id>.pdmod`. The loader picks it up on next scan.
4. The Property Handler (M-2) then surfaces the headline fields in Explorer Properties without opening the archive (Windows only; non-Windows reads the comment mirror via 7-Zip / file managers).

Both helpers are atomic (`<out>.tmp` -> rename) and compute the comment mirror automatically -- no separate bookkeeping required.

## Open follow-ups

- **`legacy_backup/` retention:** Per design Section 6 Phase M-4 (optional), the legacy folder loader can be retired entirely after a sunset window confirms no widespread migration misses. Current loader still handles folder mods, so users can manually unpack and edit a `.legacy_backup/` if needed.
- **Component-based content inside archives:** the catalog scanner (`assetCatalogScanComponents`) walks subdirectories looking for components (skins, etc.). Archive-backed components are not yet enumerated -- they would need a corresponding archive walk. M-1's `mod.json`-content path covers bodies/heads/arenas/audio/themes; component-based content (skin packs in a `components/<id>/` layout) is a future enhancement.
- **Cross-platform shell metadata:** Windows Property Handler is M-2.1. macOS Spotlight `mdimporter` and Linux file-manager hooks are deferred (the comment mirror is the universal fallback).
