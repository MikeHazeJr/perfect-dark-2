"""Native dependency import audit for the bgfx, bx and bimg static libraries.

tools/native_write_policy_guard.py reviews the disk-write and library-load
routes in the client's own sources and notes that "new dependency versions
still require the native dependency import audit". This is that audit for the
2.0 renderer's bgfx dependency (context/designs/rendering/bgfx-classic-gl.md).
The pd link applies the write-policy --wrap list to every object it links,
bgfx's included, so wrapped routes (fopen, fwrite, _mkdir, CreateProcessA...)
are enforced at link time; this audit covers what the wrap list cannot:

1. Source review. Every translation unit the configured build compiles into
   pd_bgfx, pd_bimg and pd_bx, plus the headers they include from
   third_party/bgfx and port/fast3d, is scanned with the guard's own scanner.
   Each file with an unwrapped write, process or dynamic-loading route must
   match a reviewed entry below: the same route set and the same call-context
   fingerprint (the guard's review_fingerprint over LF-normalised text). A
   changed call site or a new route fails until it is reviewed again.
2. Binary review (--exe). The linked PerfectDark.exe must contain none of the
   names D6 forbids (AMD AGS, NVAPI, RenderDoc, plus Nsight Aftermath and the
   Superluminal profiler bgfx can also load) and no symbol-server URL; its
   import table must name only Windows system DLLs (and, with --baseline-exe,
   exactly the DLLs a classic build imports); and every DLL name inside the
   bgfx, bx and bimg archives must be one reviewed in ARCHIVE_DLLS.

Usage:
    python tools/bgfx_dependency_audit.py --compile-commands <build>/compile_commands.json
        [--exe <build>/PerfectDark.exe] [--baseline-exe <classic build>/PerfectDark.exe] [--inventory]
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import native_write_policy_guard as guard  # noqa: E402

TARGET_DIRS = ("pd_bgfx.dir/", "pd_bimg.dir/", "pd_bx.dir/")
ARCHIVES = ("libpd_bgfx.a", "libpd_bimg.a", "libpd_bx.a")
INCLUDE_ROOTS = [
    "third_party/bgfx/bgfx/include", "third_party/bgfx/bgfx/src", "third_party/bgfx/bgfx/3rdparty",
    "third_party/bgfx/bgfx/3rdparty/khronos", "third_party/bgfx/bx/include", "third_party/bgfx/bx/3rdparty",
    "third_party/bgfx/bx/include/compat/mingw", "third_party/bgfx/bimg/include",
    "third_party/bgfx/bimg/3rdparty/astc-encoder/include", "port/fast3d",
]
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
PATCHED = "<build>/third_party/bgfx-pd/patched/"

# Why the stream-API hits are not disk writes: bx names its own stream API
# write/open (WriterI, ReaderI, the ReaderOpenI/WriterOpenI/ProcessOpenI
# helpers), and bgfx/bimg use it for memory, ring, command and shader-blob
# buffers. bx's only file implementation is bx::FileWriter/FileReader in
# bx/src/file.cpp over fopen/fwrite, which the write-policy wrap list covers.
STREAM = "bx stream API (WriterI/ReaderI) named write/open, not the CRT routes"

# Reviewed routes, bound to the exact call contexts of the pinned sources
# (bx 36ad613, bimg b43fea9, bgfx 651c203). "why" records the review.
REVIEWED = {
    "<build>/third_party/bgfx-pd/patched/bx_debug.cpp": {"routes": ["dlopen", "open", "write"], "sha256": "8acea86a6df0aa354b9dd026350ef717f6ad494664a5a6f14e55846de35d911a", "why":
        "dlopen(dbghelp.dll) is the call-stack symbol resolver and kernel32.dll its stack capture; both are reached "
        "only from bx's assert handler, and BX_CONFIG_DEBUG=0 compiles every BX_ASSERT out (bx's exception handler "
        "is never installed). The bgfx-pd patch makes the resolver lazy (no SymInitialize before main) and "
        "local-only (no symbol-server download, no %TEMP% symbol cache). open() starts addr2line or atos through "
        "ProcessReader in the Linux and macOS branches only. write() is the " + STREAM + "."},
    "third_party/bgfx/bgfx/src/bgfx.cpp": {"routes": ["dlopen", "write"], "sha256": "d40ed87d31cfb935f1fc64e909cf9d7bbeb222dedd16f8bb2e0e61dcb465b0be", "why":
        "dlopen() is the Linux dl_iterate_phdr branch of findModule (RTLD_NOLOAD); on Windows findModule only "
        "enumerates loaded modules (EnumProcessModules), to notice Nsight. Superluminal's "
        "bx::dlopen(PerformanceAPI.dll) sits behind BX_ENABLED(BGFX_CONFIG_PROFILER), which is 0, so the call and "
        "the name compile out. RtlGetVersion is looked up in the already loaded ntdll. write() is the command and "
        "uniform buffer writer (memory)."},
    "third_party/bgfx/bgfx/src/bgfx_p.h": {"routes": ["write"], "sha256": "db95139d91a772f8913c2be0ec164011f80fb32b0883277d80e2ec622f3d5849", "why":
        "CommandBuffer and UniformBuffer write() members (memory)."},
    "third_party/bgfx/bgfx/src/renderer_d3d11.cpp": {"routes": ["write"], "sha256": "cbe4bd26dd0b0e4aa645c351b7ba3f9ec8fb81782bbca869708a11ae9af6d599", "why":
        "Direct3D 11 renderer, compiled as a stub (BGFX_CONFIG_RENDERER_DIRECT3D11=0); write() is the DXBC blob "
        "writer (" + STREAM + ")."},
    "third_party/bgfx/bgfx/src/renderer_d3d12.cpp": {"routes": ["write"], "sha256": "561d609af6e3b9cbf682a65b38c2f667b2599c75765f7648294e0db60fd453d9", "why":
        "Direct3D 12 renderer, compiled as a stub (BGFX_CONFIG_RENDERER_DIRECT3D12=0); write() is the DXBC blob "
        "writer (" + STREAM + ")."},
    "third_party/bgfx/bgfx/src/renderer_vk.h": {"routes": ["write"], "sha256": "a9030bdf4235a9f80b7cefc13ab6885205a383136d24e8077c7fdf17f9ee8961", "why":
        "Vulkan renderer header, included only under BGFX_CONFIG_RENDERER_VULKAN, which is 0; write() is a "
        "scratch-buffer member."},
    "third_party/bgfx/bgfx/src/renderer_webgpu.h": {"routes": ["GetProcAddress", "write"], "sha256": "dd42724a6ff5800d71a1b50a08293bc494361a816b9ccc454e8745d11889031a", "why":
        "WebGPU renderer header, included only under BGFX_CONFIG_RENDERER_WEBGPU, which is 0; GetProcAddress is "
        "an entry in its import-table macro and write() a scratch-buffer member."},
    "third_party/bgfx/bgfx/src/shader_dxbc.cpp": {"routes": ["write"], "sha256": "30228c0e1e5f2099d71356d536520a6e9d601d1f3fd361dd7a6da006afa8388b", "why":
        "DXBC shader-blob serializer over bx::WriterI (" + STREAM + ")."},
    "third_party/bgfx/bgfx/src/shader_dxbc.h": {"routes": ["write"], "sha256": "da06f66dadd93289b47499eaa23fc00a0eaa966d754bfea07c9a32d7db803c05", "why":
        "DXBC shader-blob serializer declarations (" + STREAM + ")."},
    "third_party/bgfx/bgfx/src/shader_spirv.cpp": {"routes": ["write"], "sha256": "5d91144732d4588b0acccf78cf60da573ba1ee311f24377e8c93c3511b604880", "why":
        "SPIR-V shader-blob serializer over bx::WriterI (" + STREAM + ")."},
    "third_party/bgfx/bgfx/src/shader_spirv.h": {"routes": ["write"], "sha256": "2717b77e840526dd9b15abba9d025033e83c33451db1a1c67dfc94f075f3fb62", "why":
        "SPIR-V shader-blob serializer declarations (" + STREAM + ")."},
    "third_party/bgfx/bgfx/src/vertexlayout.cpp": {"routes": ["write"], "sha256": "802e9aaed71ac7a82034ff992e149bca055a0792a0944512897369e247012843", "why":
        "Vertex layout serializer over bx::WriterI (" + STREAM + ")."},
    "third_party/bgfx/bgfx/src/vertexlayout.h": {"routes": ["write"], "sha256": "120102e4598d2609cd229b859b3ea109ab94c3a48718d93b1fc6c112919bda89", "why":
        "Vertex layout serializer declaration (" + STREAM + ")."},
    "third_party/bgfx/bimg/src/image.cpp": {"routes": ["write"], "sha256": "53df5c7d9b7d755eb58aa45dbaf0ec3ce1f1f15749546e79f7fb80def471ffe9", "why":
        "HashWriter, a bx::WriterI that hashes image data in memory."},
    "third_party/bgfx/bx/include/bx/file.h": {"routes": ["open", "write"], "sha256": "be97028d3d9c238977bd4100520b94f9736fce8cb80f080f6b3971144f78a344", "why":
        "FileReader, FileWriter and DirectoryReader interfaces; the implementations in bx/src/file.cpp use the "
        "wrapped fopen and fwrite."},
    "third_party/bgfx/bx/include/bx/inline/os.inl": {"routes": ["dlsym"], "sha256": "7cd4ced03536c906d58465280519a2554282e34ca3644e68403153aae098645d", "why":
        "Typed template over bx::dlsym (bx/src/os.cpp)."},
    "third_party/bgfx/bx/include/bx/inline/readerwriter.inl": {"routes": ["open", "write"], "sha256": "1a1ee21a535457652b8a9fada59c208d761834425b160a7d0d8bed058823c624", "why":
        "Stream helpers: open() forwards to the ReaderOpenI, WriterOpenI or ProcessOpenI object, write() to a "
        "WriterI (" + STREAM + ")."},
    "third_party/bgfx/bx/include/bx/inline/ringbuffer.inl": {"routes": ["write"], "sha256": "3d6fa8de92809187e6883147270170adc7876bff815de7c58a58e9d3eee28b83", "why":
        "Ring buffer cursor arithmetic and copies (memory)."},
    "third_party/bgfx/bx/include/bx/os.h": {"routes": ["dlopen", "dlsym"], "sha256": "24bb39cc1591ffdcadd5548b46990c58e0065a1a507d6f2d6896d589ef483fe2", "why":
        "Declarations of bx::dlopen and bx::dlsym (bx/src/os.cpp)."},
    "third_party/bgfx/bx/include/bx/process.h": {"routes": ["open", "write"], "sha256": "e230ae2ce5bbce46ae2e4738c1db9410cd66bb258d2ff34780ff4f26cc4ff402", "why":
        "ProcessReader and ProcessWriter declarations; no ProcessReader or ProcessWriter is linked into the client."},
    "third_party/bgfx/bx/include/bx/readerwriter.h": {"routes": ["open", "write"], "sha256": "6a93ec6472c06c15f74f4ea8aa899884e41fad24e8a40cd657de8ed182cc93eb", "why":
        "Stream interfaces and helper declarations (" + STREAM + ")."},
    "third_party/bgfx/bx/include/bx/ringbuffer.h": {"routes": ["write"], "sha256": "57b7d174e0d3dcbb395b84769160a932d26936f0650703f774a58c925513a0c1", "why":
        "Ring buffer (memory)."},
    "third_party/bgfx/bx/include/bx/settings.h": {"routes": ["write"], "sha256": "ef723c2d3979b767233f36806e3f0303319b662a0d35e757bae93af68351906d", "why":
        "Settings serializer declarations over bx::WriterI (" + STREAM + ")."},
    "third_party/bgfx/bx/src/bx.cpp": {"routes": ["write"], "sha256": "a008cbcd199e3e32214f8fa0deeb225998c2523ecac6730470dc0d3da425d615", "why":
        "The assert handler formats into a static memory block and sends it to debug output "
        "(OutputDebugStringA); with BX_CONFIG_DEBUG=0 no BX_ASSERT calls it."},
    "third_party/bgfx/bx/src/crtnone.cpp": {"routes": ["popen", "syscall"], "sha256": "3955bd2dbbb4ad65f64b6a2814010fbca57da475802d820adb474b930de982b8", "why":
        "Freestanding C runtime stubs, compiled only under BX_CRT_NONE, which is off."},
    "third_party/bgfx/bx/src/file.cpp": {"routes": ["open", "write"], "sha256": "680da79f486d67b036e40b024f2637463c2717ba311bc1c95e0307309de5b3c7", "why":
        "FileReader and FileWriter over fopen, fread and fwrite, DirectoryReader over opendir (reads only), and "
        "make/remove over _mkdir, rmdir, _rmdir and remove; fopen, fwrite, _mkdir, rmdir, _rmdir and remove are on "
        "the write-policy wrap list, and bgfx calls none of them at run time (its CallbackI file hooks go to "
        "PdBgfxCallback, which writes nothing)."},
    "third_party/bgfx/bx/src/filepath.cpp": {"routes": ["write"], "sha256": "dcc51da7cfeff8e4e0ebdd2ea79a3a565ff0a190c6921199e39b572bb4e25538", "why":
        "Path normalisation into a memory writer (" + STREAM + ")."},
    "third_party/bgfx/bx/src/os.cpp": {"routes": ["dlopen", "dlsym", "mmap"], "sha256": "760574e7ecbc65c2d5fe9aed315713e1b392f59ed373fa147e62ea567d99cec9", "why":
        "bx::dlopen and bx::dlsym are LoadLibraryA and GetProcAddress on Windows. Their Windows callers in this "
        "build load opengl32.dll (glcontext_wgl.cpp, the system DLL SDL has already loaded), look up "
        "RtlGetVersion in the loaded ntdll (bgfx.cpp) and serve the unreachable assert-path resolver (bx "
        "debug.cpp). mmap is the Linux and macOS branch of virtual memory reservation. bx::exec "
        "(CreateProcessA, on the wrap list) has no caller."},
    "third_party/bgfx/bx/src/process.cpp": {"routes": ["_popen", "popen"], "sha256": "c3ddb0f553e1e8cc7793a732b9f10163da0e80c690e234dc3eeec4f2d25196f1", "why":
        "ProcessReader and ProcessWriter over popen. Nothing in the Windows client references them, so the "
        "linker leaves this object out (PerfectDark.exe has no ProcessReader symbol and imports no _popen)."},
    "third_party/bgfx/bx/src/settings.cpp": {"routes": ["write"], "sha256": "71763471f4a4e6302a83787a997739144b922e234b9df62566a368c8a4e671cf", "why":
        "Settings serializer over bx::WriterI (" + STREAM + ")."},
    "third_party/bgfx/bx/src/string.cpp": {"routes": ["write"], "sha256": "9d5c5bb8d362f69682a44022068ae088362457f2b907c36d985e65fa5d63df4d", "why":
        "printf-style formatter into a bx::WriterI (" + STREAM + ")."},
}

# DLL names that may appear inside the bgfx, bx and bimg archives, with the
# review. A UTF-16 scan can pick up one adjacent byte as a prefix, and the
# ASCII scan stops at a dot, so a found name passes when it ends with a
# reviewed one.
ARCHIVE_DLLS = {
    "opengl32.dll": "glcontext_wgl.cpp: bx::dlopen of the system OpenGL DLL that SDL has already loaded.",
    "interception.dll": "renderer_gl.cpp: findModule(\"Nvda.Graphics.Interception.dll\") only lists loaded "
                        "modules (EnumProcessModules) to notice Nsight Graphics; nothing is loaded.",
    "ntdll.dll": "bgfx.cpp: GetModuleHandleW(L\"ntdll.dll\"), already loaded, for RtlGetVersion.",
    "dbghelp.dll": "bx debug.cpp: assert-path symbol resolver, unreachable with BX_CONFIG_DEBUG=0.",
    "kernel32.dll": "bx debug.cpp: assert-path stack capture and the never-installed exception handler.",
}

# Names that must not appear anywhere in the client binary (ASCII or UTF-16).
# D6: release builds never load AMD AGS, NVAPI or RenderDoc. Nsight Aftermath
# (loaded by bgfx's nvapi.cpp) and the Superluminal profiler (bgfx.cpp) are
# excluded with them, and no dbghelp symbol-server path may reach the client.
FORBIDDEN_STRINGS = [
    "renderdoc.dll", "nvapi64.dll", "nvapi.dll", "amd_ags_x64.dll", "amd_ags_x86.dll",
    "gfsdk_aftermath_lib", "performanceapi.dll", "msdl.microsoft.com",
]

# Load-time imports the client may have: Windows system DLLs (D6).
ALLOWED_IMPORTS = {
    "advapi32.dll", "bcrypt.dll", "comdlg32.dll", "crypt32.dll", "dbghelp.dll", "gdi32.dll", "imm32.dll",
    "iphlpapi.dll", "kernel32.dll", "msvcrt.dll", "ole32.dll", "oleaut32.dll", "opengl32.dll", "psapi.dll",
    "secur32.dll", "setupapi.dll", "shell32.dll", "user32.dll", "version.dll", "winmm.dll", "wldap32.dll",
    "ws2_32.dll", "winspool.drv", "ucrtbase.dll", "ntdll.dll", "dwmapi.dll", "uxtheme.dll", "hid.dll",
    "normaliz.dll", "shlwapi.dll", "cfgmgr32.dll", "xinput1_4.dll", "dinput8.dll",
}

ASCII_DLL = re.compile(rb"[A-Za-z0-9_\-]{2,60}\.[dD][lL][lL]")
WIDE_DLL = re.compile(rb"(?:[A-Za-z0-9_\-]\x00){2,60}\.\x00[dD]\x00[lL]\x00[lL]\x00")


def read_text(path):
    # LF-normalised, byte-preserving decode: a CRLF checkout must fingerprint the same.
    return path.read_bytes().replace(b"\r\n", b"\n").decode("latin-1")


def library_sources(database):
    rows = json.loads(Path(database).read_text(encoding="utf-8-sig"))
    sources = set()
    for row in rows:
        output = str(row.get("output", row.get("command", ""))).replace("\\", "/")
        if any(tag in output for tag in TARGET_DIRS):
            sources.add(Path(row["file"]).resolve())
    if not sources:
        raise ValueError("compile database has no pd_bgfx, pd_bimg or pd_bx sources")
    return sources


def closure(sources, root=ROOT):
    roots = [root / name for name in INCLUDE_ROOTS]
    pending = list(sources)
    found = set()
    while pending:
        path = pending.pop()
        if path in found:
            continue
        found.add(path)
        for name in INCLUDE.findall(read_text(path)):
            for directory in [path.parent, *roots]:
                candidate = (directory / name).resolve()
                if candidate.is_file():
                    pending.append(candidate)
                    break
    return found


def display_path(path, root=ROOT):
    # The patched copies (third_party/bgfx-pd/CMakeLists.txt) live in the build tree.
    if path.parent.name == "patched" and path.parent.parent.name == "bgfx-pd":
        return PATCHED + path.name
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def source_review(database, root=ROOT):
    wrapped = guard.canonical_symbols(root / "port/include/native_write_policy_symbols.inc")
    files = closure(library_sources(database), root)
    failures, routes, seen = [], [], set()
    for path in sorted(files):
        rel = display_path(path, root)
        text = read_text(path)
        unsupported, calls, streams = guard.scan_source(text, wrapped)
        if not (unsupported or calls or streams):
            continue
        digest = guard.review_fingerprint(text, unsupported) if unsupported else None
        # As in the guard, a C++ file stream is reported, not failed: libstdc++
        # opens files through the CRT routes the wrap list covers. (The only
        # hits, bimg's astcenc_diagnostic_trace.*, compile out: nothing defines
        # ASTCENC_DIAGNOSTICS.)
        routes.append({"path": rel, "unwrapped": unsupported, "wrapped": calls,
                       "cpp_stream_via_guarded_crt": streams, "call_context_sha256": digest})
        if unsupported:
            seen.add(rel)
            review = REVIEWED.get(rel)
            if not review or review["sha256"] != digest or set(review["routes"]) != set(unsupported):
                failures.append({"path": rel, "routes": unsupported, "call_context_sha256": digest})
    stale = sorted(set(REVIEWED) - seen)
    return {"files": len(files), "reviewed": len(seen), "routes": routes, "failures": failures,
            "stale_reviews": stale}


def dll_names(data):
    names = {m.group(0).decode("ascii").lower() for m in ASCII_DLL.finditer(data)}
    names |= {m.group(0).decode("utf-16-le").lower() for m in WIDE_DLL.finditer(data)}
    return names


def contains(data, text):
    lowered = data.lower()
    return text.encode("ascii") in lowered or text.encode("utf-16-le") in lowered


def imports_of(exe):
    dump = subprocess.run(["objdump", "-p", str(exe)], capture_output=True, text=True, check=True).stdout
    return sorted({m.lower() for m in re.findall(r"DLL Name:\s*(\S+)", dump)})


def binary_review(exe, archive_dir, baseline=None):
    data = Path(exe).read_bytes()
    result = {"forbidden_strings": [name for name in FORBIDDEN_STRINGS if contains(data, name)]}
    try:
        result["imports"] = imports_of(exe)
        if baseline:
            result["baseline_imports"] = imports_of(baseline)
    except (OSError, subprocess.CalledProcessError) as error:
        return {"error": f"objdump failed: {error}"}
    result["unexpected_imports"] = [name for name in result["imports"] if name not in ALLOWED_IMPORTS]
    if baseline:
        result["imports_added_over_baseline"] = sorted(set(result["imports"]) - set(result["baseline_imports"]))
    archives, unreviewed = {}, []
    for name in ARCHIVES:
        path = Path(archive_dir) / name
        if not path.is_file():
            unreviewed.append(f"{name}: archive not found in {archive_dir}")
            continue
        found = sorted(dll_names(path.read_bytes()))
        archives[name] = found
        unreviewed += [f"{name}: {dll}" for dll in found if not any(dll.endswith(ok) for ok in ARCHIVE_DLLS)]
    result["archive_dll_names"] = archives
    result["unreviewed_archive_dll_names"] = unreviewed
    return result


def binary_ok(b):
    return ("error" not in b and not b["forbidden_strings"] and not b["unexpected_imports"]
            and not b.get("imports_added_over_baseline") and not b["unreviewed_archive_dll_names"])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--exe")
    parser.add_argument("--baseline-exe", help="a classic (pre-bgfx) client build to compare the import table with")
    parser.add_argument("--inventory", action="store_true", help="print every route with its fingerprint")
    args = parser.parse_args()

    source = source_review(args.compile_commands)
    result = {"schema": "pd2.bgfx-dependency-audit.v1", "source": source}
    ok = not source["failures"] and not source["stale_reviews"]
    if args.exe:
        archive_dir = Path(args.compile_commands).resolve().parent / "third_party" / "bgfx-pd"
        result["binary"] = binary_review(args.exe, archive_dir, args.baseline_exe)
        ok = ok and binary_ok(result["binary"])
    result["passed"] = ok
    if not args.inventory:
        result["source"] = {k: v for k, v in source.items() if k != "routes"}
    print(json.dumps(result, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
