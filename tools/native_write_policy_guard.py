"""Review known write/API escape routes in the actual configured Windows client.

This is a source inventory guard, not an OS sandbox or a C/C++ verifier. The
native link witness and behavioral tests separately prove the wrapped routes.
New dependency versions still require the native dependency import audit.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = ROOT / "port/include/native_write_policy_symbols.inc"
TOKEN = re.compile(r"//[^\n]*|/\*[\s\S]*?\*/|(?:L|u8|u|U)?\"(?:\\.|[^\"\\])*\"|(?:L|u|U)?'(?:\\.|[^'\\])*'")
CALL = re.compile(r"(?<![\w.:>])\b([A-Za-z_]\w*)\s*\(")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]', re.M)

# Known disk mutation and process/dynamic loading routes. A newly encountered
# route fails unless linked through the canonical wrapper list or explicitly
# reviewed below. Read-only Win32/CRT calls are deliberately outside this set.
UNWRAPPED = set("""
creat _creat _wcreat open sopen wopen wsopen fopen_s _wfopen_s
write _write_nolock fwrite_unlocked _fwrite_nolock fputc_unlocked
_fputc_nolock _fputwc_nolock fputwc fputws vfwprintf fwprintf
ftruncate truncate _truncate chmod fchmod mkdir rmdir unlink renameat
renameat2 unlinkat mkdirat link linkat symlink symlinkat
CreateFile CreateFile2 CreateFileTransactedA CreateFileTransactedW
OpenFile WriteFileEx SetFileInformationByHandle SetFileValidData
CreateHardLinkA CreateHardLinkW CreateSymbolicLinkA CreateSymbolicLinkW
ReplaceFileA ReplaceFileW MoveFileA MoveFileW MoveFileWithProgressA
MoveFileWithProgressW CopyFile2 CopyFileExA CopyFileExW
DeleteFileTransactedA DeleteFileTransactedW RemoveDirectoryTransactedA
RemoveDirectoryTransactedW CreateDirectoryExA CreateDirectoryExW
CreateDirectoryTransactedA CreateDirectoryTransactedW
SetFileSecurityA SetFileSecurityW SetNamedSecurityInfoA SetNamedSecurityInfoW
SetSecurityInfo SetFileShortNameA SetFileShortNameW
SetFileAttributesTransactedA SetFileAttributesTransactedW
NtCreateFile NtOpenFile NtWriteFile NtSetInformationFile
ZwCreateFile ZwOpenFile ZwWriteFile ZwSetInformationFile
DeviceIoControl CreateProcess CreateProcessAsUserA CreateProcessAsUserW
CreateProcessWithTokenW CreateProcessWithLogonW WinExec _spawnl _wspawnl
_spawnv _wspawnv _spawnlp _wspawnlp _spawnvp _wspawnvp _execv _wexecv
_execl _wexecl popen _popen _wpopen
LoadLibraryA LoadLibraryW LoadLibraryExA LoadLibraryExW GetProcAddress
dlopen dlsym mmap mremap pwrite pwritev writev syscall
""".split())

# These exceptions bind the exact source around every reviewed call. Adding a
# new call or changing its surrounding branch/arguments requires re-review.
REVIEWED = {
    # Inactive MSVC/secure-lib alternatives; MinGW uses guarded fopen/wfopen.
    "port/external/imgui-node-editor/crude_json.cpp": {"routes": ["fopen_s"], "sha256": "c783a9e068da070a25cbab54527f5c108abe4d17cd1fb1e07bcdc3b34d51e3fb"},
    "port/external/stb_image.h": {"routes": ["_wfopen_s", "fopen_s"], "sha256": "95b23d7fa56ee822abf955a6a70d904907f61f7851a28803afbec761394dde1b"},
    "port/external/stb_image_write.h": {"routes": ["_wfopen_s", "fopen_s"], "sha256": "3e6af542cb43006c296169055ae4b3a998f75064034769c17e7b00875ec4e3ca"},
    "port/external/stb_vorbis.c": {"routes": ["fopen_s"], "sha256": "6efbf8ac58a001b2c35133857c632ea12db94f5760410850aeea4df36e1bf07c"},
    # The miniSSDPD Unix socket implementation is excluded on Windows.
    "port/external/miniupnpc/src/minissdpc.c": {"routes": ["write"], "sha256": "21d685f508b4b00cb5c46ed8b62ec78f4fbe1948288f0fce68f8fb996ab9aab9"},
    # Project C++ profile serializer, not the CRT write descriptor function.
    "port/include/actionmap_profile.h": {"routes": ["write"], "sha256": "c6520a3edc4b0be8b585c4924e0523308c7b94676e28fbac70d916a8baec7943"},
    "port/src/actionmap_profile.cpp": {"routes": ["write"], "sha256": "23b777f8a94781e2199dfd6b2fc1042083aaeb5fd46e44a28d1d68914d9335d7"},
    # Inactive POSIX branches; the Windows counterparts are wrapped.
    "port/src/crash.c": {"routes": ["open"], "sha256": "2151fa0d536a11028a0fc5a4ee2280d9135586f28e824d1465ffc2bb3d70b97c"},
    "port/src/fs.c": {"routes": ["mkdir"], "sha256": "636d96c2f35c6c54e1153fa5e18935ed914b8cdc75a9a3f1caed142f919c34cd"},
    "port/src/mapimport.c": {"routes": ["mkdir"], "sha256": "e002d1adc43bdb5f15155259da8c14babba4be0cfa738e07aef429f0e0d012f1"},
    "port/src/modpack_pdmod.c": {"routes": ["mkdir"], "sha256": "69445cd22d0765ad30b95590edcf2e8310ecd44da8739236972eb745c9236afc"},
    "port/src/net/net.c": {"routes": ["open"], "sha256": "e885aee65f0d44338facda86cc842a595aeea0ed45741a89eea0774cba735f8b"},
    "port/src/pdca_extract_transaction.c": {"routes": ["mkdir"], "sha256": "ea7c9891ad5a20663da577577cc846a1fdd1f6e550e057821be09e45d956ffe8"},
    "port/src/romdata.c": {"routes": ["mkdir"], "sha256": "9b9cccf9aa62b258d4cd0d14e2f36f0d3e78b28b2bbf219eccae222ed6d34122"},
    "port/src/theater_format.c": {"routes": ["ftruncate"], "sha256": "0311fba4e2b6a676cc908160641ffe893602edb36ec59bc84c5d11d630e76ee1"},
    # Dynamic resolution is restricted to random bytes and a waitable timer.
    "port/src/net/net_candidate.c": {"routes": ["GetProcAddress", "LoadLibraryA"], "sha256": "0c92bcd5be5027a3e0c60e459151d9c567d817a5aa06474377cbea64a1a4cb2c"},
    "port/src/system.c": {"routes": ["GetProcAddress"], "sha256": "8403f95407161f3cac508e356e87297c4c0d72f74d0034f74fe9e53e3afc64d8"},
}


def code_tokens(source):
    return TOKEN.sub(lambda match: " " * len(match.group()), source)


def canonical_symbols(path=SYMBOLS):
    names = re.findall(r"^NWP_SYMBOL\((\w+)\)$", path.read_text(encoding="utf-8"), re.M)
    if not names or len(names) != len(set(names)):
        raise ValueError("missing or duplicate native write policy symbols")
    return set(names)


def scan_source(source, wrapped):
    code = code_tokens(source)
    calls = set(CALL.findall(code))
    references = set(re.findall(r"(?<![\w.:>])\b(?:[A-Z]\w*|_\w+)\b", code))
    unsupported = sorted((calls | references) & UNWRAPPED - wrapped)
    if re.search(r"\b(?:std|boost)\s*::\s*filesystem\b", code):
        unsupported.append("C++ filesystem")
    if re.search(r"\b(?:__real_(?:__imp_)?|__imp_)(?:" + "|".join(map(re.escape, sorted(wrapped))) + r")\b", code):
        unsupported.append("raw wrapped API import")
    streams = bool(re.search(r"\b(?:std\s*::\s*)?(?:ofstream|fstream|basic_ofstream|basic_fstream)\b", code))
    return sorted(set(unsupported)), sorted(calls & wrapped), streams


def review_fingerprint(source, routes):
    source = source.replace("\r\n", "\n")
    lines = source.splitlines(keepends=True)
    code = code_tokens(source)
    excerpts = []
    references = re.compile(r"(?<![\w.:>])\b(" + "|".join(map(re.escape, sorted(routes))) + r")\b")
    for match in references.finditer(code):
        if match.group(1) in routes:
            line = code.count("\n", 0, match.start())
            excerpts.append("".join(lines[max(0, line - 4):line + 5]))
    return hashlib.sha256("\n".join(excerpts).encode("latin-1")).hexdigest()


def client_sources(database, root=ROOT):
    rows = json.loads(database.read_text(encoding="utf-8-sig"))
    sources = set()
    for row in rows:
        output = str(row.get("output", row.get("command", ""))).replace("\\", "/")
        if "CMakeFiles/pd.dir/" not in output:
            continue
        path = Path(row["file"]).resolve()
        if path.is_relative_to(root) and not path.is_relative_to(root / ".claude"):
            if path.suffix in {".c", ".cpp", ".cc", ".h", ".hpp"}:
                sources.add(path)
    if not sources:
        raise ValueError("compile database contains no canonical pd client sources")
    return sources


def source_closure(sources, root=ROOT):
    # Follow repository headers only. System/static-library implementations are
    # inspected by the separate linked dependency audit and runtime link witness.
    include_roots = [root / name for name in (
        "include", "include/PR", "src/include", "port/include", "port/fast3d",
        "port/fast3d/imgui", "port/external/imgui-node-editor",
        "port/external/miniupnpc/include", "port/external/miniupnpc/src")]
    pending = list(sources)
    found = set()
    while pending:
        path = pending.pop()
        if path in found:
            continue
        found.add(path)
        # Legacy game translation units include non-UTF8 text. Identifiers and
        # directives are ASCII; lossless byte decoding preserves their offsets.
        source = path.read_bytes().decode("latin-1")
        for name in INCLUDE.findall(source):
            for directory in [path.parent, *include_roots]:
                candidate = (directory / name).resolve()
                if candidate.is_relative_to(root) and candidate.is_file():
                    if not candidate.is_relative_to(root / ".claude"):
                        pending.append(candidate)
                    break
    return found


def inventory(database, root=ROOT):
    wrapped = canonical_symbols(root / "port/include/native_write_policy_symbols.inc")
    sources = client_sources(database, root)
    failures = []
    routes = []
    policy_impl = {"port/src/native_write_policy.c", "port/src/native_write_policy_windows.c",
                   "port/include/native_write_policy_internal.h"}
    paths = source_closure(sources, root)
    for path in sorted(paths):
        relative = path.relative_to(root).as_posix()
        source = path.read_bytes().decode("latin-1")
        unsupported, guarded, streams = scan_source(source, wrapped)
        if relative in policy_impl:
            # The implementation uses original imports to enforce the policy.
            unsupported = [route for route in unsupported if route != "raw wrapped API import"]
        review = REVIEWED.get(relative)
        if unsupported:
            digest = review_fingerprint(source, unsupported)
            if not review or review["sha256"] != digest or set(unsupported) != set(review["routes"]):
                failures.append({"path": relative, "routes": unsupported, "call_context_sha256": digest})
        if guarded or unsupported or streams:
            routes.append({"path": relative, "wrapped": guarded, "reviewed": unsupported,
                           "cpp_stream_via_guarded_crt": streams})
    return {"schema": "pd2.native-write-inventory.v1", "passed": not failures,
            "client_translation_units": len(sources), "repository_files": len(paths),
            "wrapped_symbols": len(wrapped), "failures": failures, "routes": routes,
            "limits": "Known source API guard; native link/behavior/dependency audit required separately."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = inventory(args.compile_commands.resolve())
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "routes"}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
