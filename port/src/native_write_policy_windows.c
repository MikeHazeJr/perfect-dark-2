/* Client and native test targets link both direct symbols and MinGW import
 * pointers through this layer. Ordinary launches delegate unchanged. This
 * covers the compiled application/static libraries, not arbitrary DLL code. */
#include "native_write_policy_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include <share.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>

#define REAL(name) __real___imp_##name
#define IMPORT(name) __typeof__(&name) __wrap___imp_##name = __wrap_##name
#define DECLARE_REAL(name) extern __typeof__(&name) REAL(name)

DECLARE_REAL(fopen); DECLARE_REAL(_wfopen); DECLARE_REAL(_fsopen); DECLARE_REAL(_wfsopen);
DECLARE_REAL(_open); DECLARE_REAL(_wopen); DECLARE_REAL(_sopen); DECLARE_REAL(_wsopen);
DECLARE_REAL(remove); DECLARE_REAL(_wremove); DECLARE_REAL(rename); DECLARE_REAL(_wrename);
DECLARE_REAL(_unlink); DECLARE_REAL(_wunlink); DECLARE_REAL(_mkdir); DECLARE_REAL(_wmkdir);
DECLARE_REAL(_rmdir); DECLARE_REAL(_wrmdir); DECLARE_REAL(_chsize); DECLARE_REAL(_chsize_s);
DECLARE_REAL(freopen); DECLARE_REAL(freopen_s); DECLARE_REAL(_wfreopen); DECLARE_REAL(_wfreopen_s);
DECLARE_REAL(fwrite); DECLARE_REAL(fputs); DECLARE_REAL(fputc); DECLARE_REAL(vfprintf);
DECLARE_REAL(_write);
extern __typeof__(&SetEndOfFile) REAL(SetEndOfFile);
extern __typeof__(&CreateFileA) REAL(CreateFileA);
DECLARE_REAL(CreateFileMappingA); DECLARE_REAL(CreateFileMappingW);
DECLARE_REAL(MapViewOfFile); DECLARE_REAL(MapViewOfFileEx);
DECLARE_REAL(SHFileOperationA); DECLARE_REAL(SHFileOperationW);
DECLARE_REAL(CreateProcessA); DECLARE_REAL(CreateProcessW);
DECLARE_REAL(ShellExecuteA); DECLARE_REAL(ShellExecuteW);
DECLARE_REAL(ShellExecuteExA); DECLARE_REAL(ShellExecuteExW);
DECLARE_REAL(system); DECLARE_REAL(_wsystem);
DECLARE_REAL(_chmod); DECLARE_REAL(_wchmod);
DECLARE_REAL(SetFileAttributesA); DECLARE_REAL(SetFileAttributesW); DECLARE_REAL(SetFileTime);

static int refuse(void) { errno = EACCES; SetLastError(ERROR_ACCESS_DENIED); return -1; }
static bool utf8(const char *path, wchar_t *wide) { return nwpUtf8Path(path, wide, NWP_MAX_PATH); }
static bool streamAllowed(FILE *stream) {
    if (!nativeWritePolicyActive()) return true;
    if (!stream) { refuse(); return false; }
    intptr_t handle = _get_osfhandle(_fileno(stream));
    return handle != -1 && nwpHandleAllowed((HANDLE)handle);
}
static bool fdAllowed(int fd) {
    if (!nativeWritePolicyActive()) return true;
    intptr_t handle = _get_osfhandle(fd);
    return handle != -1 && nwpHandleAllowed((HANDLE)handle);
}

FILE *__wrap_fopen(const char *path, const char *mode) {
    if (!nativeWritePolicyActive()) return REAL(fopen)(path, mode);
    wchar_t wide[NWP_MAX_PATH], wideMode[16];
    if (!utf8(path, wide) || !nwpUtf8Path(mode, wideMode, 16)) return NULL;
    if (mode[0] == 'r' && !strchr(mode, '+')) return REAL(_wfopen)(wide, wideMode);
    return nwpFopenW(wide, wideMode);
}
FILE *__wrap__wfopen(const wchar_t *path, const wchar_t *mode) {
    if (!nativeWritePolicyActive() || (mode && mode[0] == L'r' && !wcschr(mode, L'+')))
        return REAL(_wfopen)(path, mode);
    return nwpFopenW(path, mode);
}
FILE *__wrap__fsopen(const char *path, const char *mode, int share) {
    if (!nativeWritePolicyActive()) return REAL(_fsopen)(path, mode, share);
    if (share != _SH_DENYNO) { refuse(); return NULL; }
    return __wrap_fopen(path, mode);
}
FILE *__wrap__wfsopen(const wchar_t *path, const wchar_t *mode, int share) {
    if (!nativeWritePolicyActive()) return REAL(_wfsopen)(path, mode, share);
    if (share != _SH_DENYNO) { refuse(); return NULL; }
    return __wrap__wfopen(path, mode);
}
int __wrap__open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & _O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (!nativeWritePolicyActive()) return REAL(_open)(path, flags, mode);
    wchar_t wide[NWP_MAX_PATH];
    return utf8(path, wide) ? nwpOpenW(wide, flags, mode) : -1;
}
int __wrap__wopen(const wchar_t *path, int flags, ...) {
    int mode = 0;
    if (flags & _O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    if (!nativeWritePolicyActive()) return REAL(_wopen)(path, flags, mode);
    return nwpOpenW(path, flags, mode);
}
int __wrap__sopen(const char *path, int flags, int share, ...) {
    int mode = 0;
    if (flags & _O_CREAT) { va_list ap; va_start(ap, share); mode = va_arg(ap, int); va_end(ap); }
    if (!nativeWritePolicyActive()) return REAL(_sopen)(path, flags, share, mode);
    if (share != _SH_DENYNO) return refuse();
    return __wrap__open(path, flags, mode);
}
int __wrap__wsopen(const wchar_t *path, int flags, int share, ...) {
    int mode = 0;
    if (flags & _O_CREAT) { va_list ap; va_start(ap, share); mode = va_arg(ap, int); va_end(ap); }
    if (!nativeWritePolicyActive()) return REAL(_wsopen)(path, flags, share, mode);
    if (share != _SH_DENYNO) return refuse();
    return nwpOpenW(path, flags, mode);
}

/* All reopened FILE objects are refused in this opt-in mode. None of the
 * supported client path requires reopening, and this avoids a CRT-internal
 * truncate before the new descriptor can be inspected. */
FILE *__wrap_freopen(const char *path, const char *mode, FILE *stream) {
    if (nativeWritePolicyActive()) { refuse(); return NULL; }
    return REAL(freopen)(path, mode, stream);
}
FILE *__wrap__wfreopen(const wchar_t *path, const wchar_t *mode, FILE *stream) {
    if (nativeWritePolicyActive()) { refuse(); return NULL; }
    return REAL(_wfreopen)(path, mode, stream);
}
errno_t __wrap_freopen_s(FILE **result, const char *path, const char *mode, FILE *stream) {
    if (nativeWritePolicyActive()) { if (result) *result = NULL; refuse(); return EACCES; }
    return REAL(freopen_s)(result, path, mode, stream);
}
errno_t __wrap__wfreopen_s(FILE **result, const wchar_t *path, const wchar_t *mode, FILE *stream) {
    if (nativeWritePolicyActive()) { if (result) *result = NULL; refuse(); return EACCES; }
    return REAL(_wfreopen_s)(result, path, mode, stream);
}

#define ONE_PATH_W(name) \
int __wrap_##name(const wchar_t *path) { \
    if (!nativeWritePolicyActive()) return REAL(name)(path); \
    nwp_guard guard; if (!nwpGuardPath(path, &guard)) return -1; \
    int result = REAL(name)(guard.path); nwpCloseGuard(&guard); return result; \
}
ONE_PATH_W(_wremove) ONE_PATH_W(_wunlink) ONE_PATH_W(_wmkdir) ONE_PATH_W(_wrmdir)
#define ONE_PATH_A(name, wideName) \
int __wrap_##name(const char *path) { \
    if (!nativeWritePolicyActive()) return REAL(name)(path); \
    wchar_t wide[NWP_MAX_PATH]; return utf8(path, wide) ? __wrap_##wideName(wide) : -1; \
}
ONE_PATH_A(remove, _wremove) ONE_PATH_A(_unlink, _wunlink)
ONE_PATH_A(_mkdir, _wmkdir) ONE_PATH_A(_rmdir, _wrmdir)
/* MinGW's POSIX spelling is a distinct undefined symbol in map import and
 * distribution objects. Route it directly, rather than relying on a static
 * CRT alias forwarding through an already-linked original import. */
int __wrap_rmdir(const char *path) { return __wrap__rmdir(path); }
int __wrap__wrename(const wchar_t *source, const wchar_t *destination) {
    if (!nativeWritePolicyActive()) return REAL(_wrename)(source, destination);
    nwp_guard a, b;
    if (!nwpGuardPath(source, &a)) return -1;
    if (!nwpGuardPath(destination, &b)) { nwpCloseGuard(&a); return -1; }
    int result = REAL(_wrename)(a.path, b.path);
    nwpCloseGuard(&a); nwpCloseGuard(&b); return result;
}
int __wrap_rename(const char *source, const char *destination) {
    if (!nativeWritePolicyActive()) return REAL(rename)(source, destination);
    wchar_t a[NWP_MAX_PATH], b[NWP_MAX_PATH];
    return utf8(source, a) && utf8(destination, b) ? __wrap__wrename(a, b) : -1;
}
int __wrap__chsize(int fd, long size) { return fdAllowed(fd) ? REAL(_chsize)(fd, size) : -1; }
errno_t __wrap__chsize_s(int fd, __int64 size) { return fdAllowed(fd) ? REAL(_chsize_s)(fd, size) : EACCES; }
int __wrap__write(int fd, const void *bytes, unsigned count) {
    return fdAllowed(fd) ? REAL(_write)(fd, bytes, count) : -1;
}
size_t __wrap_fwrite(const void *bytes, size_t size, size_t count, FILE *stream) {
    return streamAllowed(stream) ? REAL(fwrite)(bytes, size, count, stream) : 0;
}
int __wrap_fputs(const char *text, FILE *stream) { return streamAllowed(stream) ? REAL(fputs)(text, stream) : EOF; }
int __wrap_fputc(int c, FILE *stream) { return streamAllowed(stream) ? REAL(fputc)(c, stream) : EOF; }
int __wrap_vfprintf(FILE *stream, const char *format, va_list ap) {
    return streamAllowed(stream) ? REAL(vfprintf)(stream, format, ap) : -1;
}
int __wrap_fprintf(FILE *stream, const char *format, ...) {
    va_list ap; va_start(ap, format);
    int result = __wrap_vfprintf(stream, format, ap); va_end(ap); return result;
}

HANDLE WINAPI __wrap_CreateFileW(LPCWSTR path, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile) {
    return nwpCreateFileW(path, access, share, security, disposition, flags, templateFile);
}
HANDLE WINAPI __wrap_CreateFileA(LPCSTR path, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile) {
    if (!nativeWritePolicyActive()) return REAL(CreateFileA)(path, access, share, security, disposition, flags, templateFile);
    wchar_t wide[NWP_MAX_PATH];
    return utf8(path, wide) ? nwpCreateFileW(wide, access, share, security, disposition, flags, templateFile) : INVALID_HANDLE_VALUE;
}
BOOL WINAPI __wrap_WriteFile(HANDLE file, LPCVOID bytes, DWORD count, LPDWORD written, LPOVERLAPPED overlapped) {
    if (!nwpHandleAllowed(file)) { if (written) *written = 0; return FALSE; }
    return REAL(WriteFile)(file, bytes, count, written, overlapped);
}
BOOL WINAPI __wrap_SetEndOfFile(HANDLE file) {
    return nwpHandleAllowed(file) ? REAL(SetEndOfFile)(file) : FALSE;
}
#define WIN_ONE_W(name) \
BOOL WINAPI __wrap_##name(LPCWSTR path) { \
    if (!nativeWritePolicyActive()) return REAL(name)(path); \
    nwp_guard g; if (!nwpGuardPath(path, &g)) return FALSE; \
    BOOL result = REAL(name)(g.path); nwpCloseGuard(&g); return result; \
}
WIN_ONE_W(DeleteFileW) WIN_ONE_W(RemoveDirectoryW)
#define WIN_ONE_A(name, wideName) \
BOOL WINAPI __wrap_##name(LPCSTR path) { \
    if (!nativeWritePolicyActive()) return REAL(name)(path); \
    wchar_t wide[NWP_MAX_PATH]; return utf8(path, wide) ? __wrap_##wideName(wide) : FALSE; \
}
WIN_ONE_A(DeleteFileA, DeleteFileW) WIN_ONE_A(RemoveDirectoryA, RemoveDirectoryW)
BOOL WINAPI __wrap_CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES security) {
    if (!nativeWritePolicyActive()) return REAL(CreateDirectoryW)(path, security);
    nwp_guard g; if (!nwpGuardPath(path, &g)) return FALSE;
    BOOL result = REAL(CreateDirectoryW)(g.path, security); nwpCloseGuard(&g); return result;
}
BOOL WINAPI __wrap_CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES security) {
    if (!nativeWritePolicyActive()) return REAL(CreateDirectoryA)(path, security);
    wchar_t wide[NWP_MAX_PATH]; return utf8(path, wide) ? __wrap_CreateDirectoryW(wide, security) : FALSE;
}
BOOL WINAPI __wrap_MoveFileExW(LPCWSTR source, LPCWSTR destination, DWORD flags) {
    if (!nativeWritePolicyActive()) return REAL(MoveFileExW)(source, destination, flags);
    if (!destination || (flags & MOVEFILE_DELAY_UNTIL_REBOOT)) { refuse(); return FALSE; }
    nwp_guard a, b;
    if (!nwpGuardPath(source, &a)) return FALSE;
    if (!nwpGuardPath(destination, &b)) { nwpCloseGuard(&a); return FALSE; }
    BOOL result = REAL(MoveFileExW)(a.path, b.path, flags);
    nwpCloseGuard(&a); nwpCloseGuard(&b); return result;
}
BOOL WINAPI __wrap_MoveFileExA(LPCSTR source, LPCSTR destination, DWORD flags) {
    if (!nativeWritePolicyActive()) return REAL(MoveFileExA)(source, destination, flags);
    wchar_t a[NWP_MAX_PATH], b[NWP_MAX_PATH];
    return utf8(source, a) && utf8(destination, b) ? __wrap_MoveFileExW(a, b, flags) : FALSE;
}
BOOL WINAPI __wrap_CopyFileW(LPCWSTR source, LPCWSTR destination, BOOL failIfExists) {
    if (!nativeWritePolicyActive()) return REAL(CopyFileW)(source, destination, failIfExists);
    /* CopyFile may overwrite before an output handle can be inspected. Existing
     * destinations are refused; new ordinary outputs remain profile-scoped. */
    nwp_guard g;
    if (!nwpGuardPath(destination, &g)) return FALSE;
    if (g.exists) { nwpCloseGuard(&g); refuse(); return FALSE; }
    BOOL result = REAL(CopyFileW)(source, g.path, TRUE); nwpCloseGuard(&g); return result;
}
BOOL WINAPI __wrap_CopyFileA(LPCSTR source, LPCSTR destination, BOOL failIfExists) {
    if (!nativeWritePolicyActive()) return REAL(CopyFileA)(source, destination, failIfExists);
    wchar_t a[NWP_MAX_PATH], b[NWP_MAX_PATH];
    return utf8(source, a) && utf8(destination, b) ? __wrap_CopyFileW(a, b, failIfExists) : FALSE;
}

/* Supported smoke paths do not require writable mappings or external writer
 * processes. Refuse those escape routes, including mappings inherited before
 * activation and shell operations whose writes happen inside a system DLL. */
HANDLE WINAPI __wrap_CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES security,
    DWORD protection, DWORD high, DWORD low, LPCSTR name) {
    if (nativeWritePolicyActive()) { refuse(); return NULL; }
    return REAL(CreateFileMappingA)(file, security, protection, high, low, name);
}
HANDLE WINAPI __wrap_CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES security,
    DWORD protection, DWORD high, DWORD low, LPCWSTR name) {
    if (nativeWritePolicyActive()) { refuse(); return NULL; }
    return REAL(CreateFileMappingW)(file, security, protection, high, low, name);
}
LPVOID WINAPI __wrap_MapViewOfFile(HANDLE mapping, DWORD access, DWORD high, DWORD low, SIZE_T count) {
    if (nativeWritePolicyActive() && (access & FILE_MAP_WRITE)) { refuse(); return NULL; }
    return REAL(MapViewOfFile)(mapping, access, high, low, count);
}
LPVOID WINAPI __wrap_MapViewOfFileEx(HANDLE mapping, DWORD access, DWORD high, DWORD low, SIZE_T count, LPVOID address) {
    if (nativeWritePolicyActive() && (access & FILE_MAP_WRITE)) { refuse(); return NULL; }
    return REAL(MapViewOfFileEx)(mapping, access, high, low, count, address);
}
int WINAPI __wrap_SHFileOperationA(LPSHFILEOPSTRUCTA operation) {
    if (nativeWritePolicyActive()) { refuse(); return ERROR_ACCESS_DENIED; }
    return REAL(SHFileOperationA)(operation);
}
int WINAPI __wrap_SHFileOperationW(LPSHFILEOPSTRUCTW operation) {
    if (nativeWritePolicyActive()) { refuse(); return ERROR_ACCESS_DENIED; }
    return REAL(SHFileOperationW)(operation);
}
BOOL WINAPI __wrap_CreateProcessA(LPCSTR application, LPSTR command,
    LPSECURITY_ATTRIBUTES processSecurity, LPSECURITY_ATTRIBUTES threadSecurity,
    BOOL inherit, DWORD flags, LPVOID environment, LPCSTR directory,
    LPSTARTUPINFOA startup, LPPROCESS_INFORMATION process) {
    if (nativeWritePolicyActive()) { refuse(); return FALSE; }
    return REAL(CreateProcessA)(application, command, processSecurity, threadSecurity,
        inherit, flags, environment, directory, startup, process);
}
BOOL WINAPI __wrap_CreateProcessW(LPCWSTR application, LPWSTR command,
    LPSECURITY_ATTRIBUTES processSecurity, LPSECURITY_ATTRIBUTES threadSecurity,
    BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
    if (nativeWritePolicyActive()) { refuse(); return FALSE; }
    return REAL(CreateProcessW)(application, command, processSecurity, threadSecurity,
        inherit, flags, environment, directory, startup, process);
}
HINSTANCE WINAPI __wrap_ShellExecuteA(HWND window, LPCSTR verb, LPCSTR file,
    LPCSTR params, LPCSTR directory, INT show) {
    if (nativeWritePolicyActive()) { refuse(); return (HINSTANCE)(INT_PTR)SE_ERR_ACCESSDENIED; }
    return REAL(ShellExecuteA)(window, verb, file, params, directory, show);
}
HINSTANCE WINAPI __wrap_ShellExecuteW(HWND window, LPCWSTR verb, LPCWSTR file,
    LPCWSTR params, LPCWSTR directory, INT show) {
    if (nativeWritePolicyActive()) { refuse(); return (HINSTANCE)(INT_PTR)SE_ERR_ACCESSDENIED; }
    return REAL(ShellExecuteW)(window, verb, file, params, directory, show);
}
BOOL WINAPI __wrap_ShellExecuteExA(SHELLEXECUTEINFOA *info) {
    if (nativeWritePolicyActive()) { refuse(); return FALSE; }
    return REAL(ShellExecuteExA)(info);
}
BOOL WINAPI __wrap_ShellExecuteExW(SHELLEXECUTEINFOW *info) {
    if (nativeWritePolicyActive()) { refuse(); return FALSE; }
    return REAL(ShellExecuteExW)(info);
}
int __wrap_system(const char *command) {
    if (nativeWritePolicyActive()) return refuse();
    return REAL(system)(command);
}
int __wrap__wsystem(const wchar_t *command) {
    if (nativeWritePolicyActive()) return refuse();
    return REAL(_wsystem)(command);
}
int __wrap__wchmod(const wchar_t *path, int mode) {
    if (!nativeWritePolicyActive()) return REAL(_wchmod)(path, mode);
    nwp_guard g; if (!nwpGuardPath(path, &g)) return -1;
    int result = REAL(_wchmod)(g.path, mode); nwpCloseGuard(&g); return result;
}
int __wrap__chmod(const char *path, int mode) {
    if (!nativeWritePolicyActive()) return REAL(_chmod)(path, mode);
    wchar_t wide[NWP_MAX_PATH]; return utf8(path, wide) ? __wrap__wchmod(wide, mode) : -1;
}
BOOL WINAPI __wrap_SetFileAttributesW(LPCWSTR path, DWORD attributes) {
    if (!nativeWritePolicyActive()) return REAL(SetFileAttributesW)(path, attributes);
    nwp_guard g; if (!nwpGuardPath(path, &g)) return FALSE;
    BOOL result = REAL(SetFileAttributesW)(g.path, attributes); nwpCloseGuard(&g); return result;
}
BOOL WINAPI __wrap_SetFileAttributesA(LPCSTR path, DWORD attributes) {
    if (!nativeWritePolicyActive()) return REAL(SetFileAttributesA)(path, attributes);
    wchar_t wide[NWP_MAX_PATH]; return utf8(path, wide) ? __wrap_SetFileAttributesW(wide, attributes) : FALSE;
}
BOOL WINAPI __wrap_SetFileTime(HANDLE file, const FILETIME *created, const FILETIME *accessed, const FILETIME *written) {
    return nwpHandleAllowed(file) ? REAL(SetFileTime)(file, created, accessed, written) : FALSE;
}

IMPORT(fopen); IMPORT(_wfopen); IMPORT(_fsopen); IMPORT(_wfsopen);
IMPORT(_open); IMPORT(_wopen); IMPORT(_sopen); IMPORT(_wsopen);
IMPORT(remove); IMPORT(_wremove); IMPORT(rename); IMPORT(_wrename);
IMPORT(_unlink); IMPORT(_wunlink); IMPORT(_mkdir); IMPORT(_wmkdir); IMPORT(_rmdir); IMPORT(_wrmdir);
IMPORT(rmdir);
IMPORT(_chsize); IMPORT(_chsize_s); IMPORT(_write);
IMPORT(freopen); IMPORT(freopen_s); IMPORT(_wfreopen); IMPORT(_wfreopen_s);
IMPORT(fwrite); IMPORT(fputs); IMPORT(fputc); IMPORT(vfprintf); IMPORT(fprintf);
IMPORT(CreateFileA); IMPORT(CreateFileW); IMPORT(WriteFile); IMPORT(SetEndOfFile);
IMPORT(CreateDirectoryA); IMPORT(CreateDirectoryW); IMPORT(RemoveDirectoryA); IMPORT(RemoveDirectoryW);
IMPORT(DeleteFileA); IMPORT(DeleteFileW); IMPORT(MoveFileExA); IMPORT(MoveFileExW);
IMPORT(CopyFileA); IMPORT(CopyFileW);
IMPORT(CreateFileMappingA); IMPORT(CreateFileMappingW); IMPORT(MapViewOfFile); IMPORT(MapViewOfFileEx);
IMPORT(SHFileOperationA); IMPORT(SHFileOperationW);
IMPORT(CreateProcessA); IMPORT(CreateProcessW); IMPORT(ShellExecuteA); IMPORT(ShellExecuteW);
IMPORT(ShellExecuteExA); IMPORT(ShellExecuteExW); IMPORT(system); IMPORT(_wsystem);
IMPORT(_chmod); IMPORT(_wchmod); IMPORT(SetFileAttributesA); IMPORT(SetFileAttributesW); IMPORT(SetFileTime);
