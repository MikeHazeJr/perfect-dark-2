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
    "port/external/stb_image.h": {"routes": ["_wfopen_s", "fopen_s"], "sha256": "df2c9d77c52bf288c433339d1853d8b7e199d9308c4cf6b6c1e0aab7d150dec0"},
    "port/external/stb_image_write.h": {"routes": ["_wfopen_s", "fopen_s"], "sha256": "b21735cda266d706eed122f3a0ddaff2cce7c064e523bf1ad95e46edfff2eed9"},
    "port/external/stb_vorbis.c": {"routes": ["fopen_s"], "sha256": "6efbf8ac58a001b2c35133857c632ea12db94f5760410850aeea4df36e1bf07c"},
    # The miniSSDPD Unix socket implementation is excluded on Windows.
    "port/external/miniupnpc/src/minissdpc.c": {"routes": ["write"], "sha256": "9f43879fb045e3b86d9f6ac14776f8ee54ffb100f5b21e195fbb5e733517d05a"},
    # Project C++ profile serializer, not the CRT write descriptor function.
    # Re-reviewed for B-1184 (T-MODDING-008): Store::initialize also rewrites a
    # bindings file cut off at an unnamed action through the same serializer.
    "port/include/actionmap_profile.h": {"routes": ["write"], "sha256": "6ea382d50e72f8fb3c7cc0be270d0f9251547f69ce152cda8b2f61017dd60a55"},
    "port/src/actionmap_profile.cpp": {"routes": ["write"], "sha256": "931d3c9353490aaba4efe430025169c1454c6cb3af3844f1d936183031cf9795"},
    # Inactive POSIX branches; the Windows counterparts are wrapped.
    "port/src/crash.c": {"routes": ["open"], "sha256": "766fc2f5ffb484f092be672bded832cd300d97d90805ffe5cc038d89d13c33f7"},
    "port/src/fs.c": {"routes": ["mkdir"], "sha256": "95a210fddb3711643c1ec36e0b219dc1cde9bcac74594f504dc353f1c1e7e53d"},
    "port/src/mapimport.c": {"routes": ["mkdir"], "sha256": "9ba4be3819040245f7fce7417a5b17af3676c025dd75f56b94c409f8a772c5bc"},
    "port/src/modpack_pdmod.c": {"routes": ["mkdir"], "sha256": "53b54543b5e40faf5e505ecf192e2061e103d8e58b176cd94b3143f9719b2220"},
    # Reviewed for the user mods folder (T-MODDING-008): rootsMkdir's POSIX
    # branch; the Windows branch calls the wrapped _mkdir.
    "port/src/mods_roots.c": {"routes": ["mkdir"], "sha256": "79f51849db325f67de3e67f9cd3de9bc8b40cb177814dddba1ab611090c0aaed"},
    "port/src/net/net.c": {"routes": ["open"], "sha256": "606912ef8e745409b97153cd86303300b82fc4ec4847b09d93a80eea9e0a5fc4"},
    "port/src/pdca_extract_transaction.c": {"routes": ["mkdir"], "sha256": "ea7c9891ad5a20663da577577cc846a1fdd1f6e550e057821be09e45d956ffe8"},
    "port/src/romdata.c": {"routes": ["mkdir"], "sha256": "14ae26828e8f5b75c32415b870c2a0489ce57aef47f2f55d0bc4dffa86bffe37"},
    "port/src/theater_format.c": {"routes": ["ftruncate"], "sha256": "b684bb220e080efa8b7780260ff10002c527127f26b3778b6b1060fb9645dceb"},
    # Dynamic resolution is restricted to random bytes and a waitable timer.
    "port/src/net/net_candidate.c": {"routes": ["GetProcAddress", "LoadLibraryA"], "sha256": "8aabafc20491a90e664f81b0ef8df5ed9b12b558a764696741c3eb25183184b8"},
    "port/src/system.c": {"routes": ["GetProcAddress"], "sha256": "bf6b770d4cb62f430834beb52cb74c4438f93f113ca4e764310da4091c0e3cd3"},
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
    # Lines end at "\n" only, the same rule the line number below uses.
    # str.splitlines would also break at form feeds and at 0x85, which legacy
    # sources carry inside comments.
    lines = re.split(r"(?<=\n)", source)
    code = code_tokens(source)
    excerpts = []
    references = re.compile(r"(?<![\w.:>])\b(" + "|".join(map(re.escape, sorted(routes))) + r")\b")
    for match in references.finditer(code):
        if match.group(1) in routes:
            # code_tokens keeps every offset but blanks the newlines inside
            # block comments, so the line is counted in the original text.
            # Counting it in the blanked text bound each pin to a window some
            # lines above the call, and comment-only edits broke the pin while
            # edits next to the call could go unseen.
            line = source.count("\n", 0, match.start())
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
