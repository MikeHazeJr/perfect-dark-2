#include "native_write_policy_internal.h"
#include "sha256.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <direct.h>
#include <share.h>
#include <shellapi.h>

/* Raw assembler aliases ensure this witness tests both undefined direct
 * references and IAT pointers, including functions whose MinGW headers expose
 * inline front ends. Missing linker options cause refusal before activation. */
#define NWP_SYMBOL(name) \
    extern __typeof__(name) nwpDirect_##name __asm__(#name); \
    extern __typeof__(name) __wrap_##name; \
    extern __typeof__(&name) nwpImport_##name __asm__("__imp_" #name);
#include "native_write_policy_symbols.inc"
#undef NWP_SYMBOL

bool nativeWritePolicyLinkContract(void)
{
#define NWP_SYMBOL(name) \
    do { volatile __typeof__(&name) direct = nwpDirect_##name; \
        if (direct != __wrap_##name || nwpImport_##name != __wrap_##name) return false; \
    } while (0);
#include "native_write_policy_symbols.inc"
#undef NWP_SYMBOL
    return true;
}

static bool active;
static volatile LONG deviceRefused;
bool nativeWritePolicyRefused(void) { return InterlockedCompareExchange(&deviceRefused, 0, 0) != 0; }
static wchar_t profilePath[NWP_MAX_PATH];
static char profileUtf8[NWP_MAX_PATH * 3];
static HANDLE rootLocks[NWP_MAX_PARENTS * 2];
static unsigned int rootLockCount;

static bool denied(void)
{
    errno = EACCES;
    SetLastError(ERROR_ACCESS_DENIED);
    return false;
}

bool nativeWritePolicyActive(void) { return active; }
const char *nativeWritePolicyProfile(void) { return active ? profileUtf8 : ""; }

bool nwpUtf8Path(const char *path, wchar_t *out, size_t count)
{
    if (!path || !path[0] || !out || !count || count > INT_MAX) return denied();
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, (int)count))
        return denied();
    return true;
}

static bool ordinaryPath(const wchar_t *path, wchar_t *out)
{
    if (!path || !path[0] || wcslen(path) >= NWP_MAX_PATH - 1) return denied();
    /* No device/UNC/extended namespaces, drive-relative names, streams,
     * placeholders, or Win32 trailing-dot/space aliases. Relative paths are
     * resolved only against the caller's current directory. */
    if (path[0] == L'\\' || path[0] == L'/' || wcschr(path, L'$')) return denied();
    if (path[1] == L':' && (!iswalpha(path[0]) ||
        (path[2] != L'/' && path[2] != L'\\'))) return denied();
    const wchar_t *segment = path;
    for (const wchar_t *p = path;; ++p) {
        if (*p == L':' && p != path + 1) return denied();
        if (*p == L'/' || *p == L'\\' || !*p) {
            size_t len = (size_t)(p - segment);
            if ((len == 2 && segment[0] == L'.' && segment[1] == L'.') ||
                (len && (segment[len - 1] == L' ' ||
                    (segment[len - 1] == L'.' && len != 1)))) return denied();
            size_t stem = 0;
            while (stem < len && segment[stem] != L'.') ++stem;
            if ((stem == 3 && (!_wcsnicmp(segment, L"con", 3) ||
                !_wcsnicmp(segment, L"prn", 3) || !_wcsnicmp(segment, L"aux", 3) ||
                !_wcsnicmp(segment, L"nul", 3))) ||
                (stem == 6 && !_wcsnicmp(segment, L"conin$", 6)) ||
                (stem == 7 && !_wcsnicmp(segment, L"conout$", 7)) ||
                (stem == 4 && (!_wcsnicmp(segment, L"com", 3) ||
                    !_wcsnicmp(segment, L"lpt", 3)) &&
                    ((segment[3] >= L'0' && segment[3] <= L'9') ||
                        segment[3] == 0x00b9 || segment[3] == 0x00b2 || segment[3] == 0x00b3)))
                return denied();
            segment = p + 1;
        }
        if (!*p) break;
    }
    DWORD size = GetFullPathNameW(path, NWP_MAX_PATH, out, NULL);
    if (!size || size >= NWP_MAX_PATH || out[1] != L':' || out[2] != L'\\') return denied();
    size_t len = wcslen(out);
    while (len > 3 && out[len - 1] == L'\\') out[--len] = 0;
    return true;
}

static bool finalPath(HANDLE handle, wchar_t *out)
{
    wchar_t value[NWP_MAX_PATH];
    DWORD size = GetFinalPathNameByHandleW(handle, value, NWP_MAX_PATH, FILE_NAME_NORMALIZED);
    if (size < 7 || size >= NWP_MAX_PATH || wcsncmp(value, L"\\\\?\\", 4)) return denied();
    if (value[5] != L':') return denied();
    wcscpy(out, value + 4);
    size_t len = wcslen(out);
    while (len > 3 && out[len - 1] == L'\\') out[--len] = 0;
    return true;
}

static bool isWithin(const wchar_t *path, const wchar_t *root)
{
    size_t len = wcslen(root);
    return !_wcsnicmp(path, root, len) && path[len] == L'\\';
}

static bool checkHandle(HANDLE handle, bool directory, const wchar_t *expected)
{
    BY_HANDLE_FILE_INFORMATION info;
    wchar_t resolved[NWP_MAX_PATH];
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (directory && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ||
        (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && info.nNumberOfLinks != 1) ||
        !finalPath(handle, resolved) || (expected && _wcsicmp(resolved, expected))) return denied();
    return true;
}

void nwpCloseGuard(nwp_guard *guard)
{
    DWORD error = GetLastError();
    int savedErrno = errno;
    while (guard->count) CloseHandle(guard->parents[--guard->count]);
    errno = savedErrno;
    SetLastError(error);
}

static bool lockParents(nwp_guard *guard, bool includeTarget)
{
    wchar_t part[NWP_MAX_PATH];
    wcscpy(part, guard->path);
    size_t len = wcslen(part);
    for (size_t i = 3; i <= len; ++i) {
        if (i != len && part[i] != L'\\') continue;
        if (i == len && !includeTarget) break;
        wchar_t saved = part[i];
        part[i] = 0;
        if (guard->count >= NWP_MAX_PARENTS) { nwpCloseGuard(guard); return denied(); }
        HANDLE h = __real___imp_CreateFileW(part, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        bool ok = h != INVALID_HANDLE_VALUE && checkHandle(h, true, part);
        part[i] = saved;
        if (!ok) {
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            nwpCloseGuard(guard);
            return denied();
        }
        guard->parents[guard->count++] = h;
    }
    return true;
}

bool nwpGuardPath(const wchar_t *path, nwp_guard *guard)
{
    memset(guard, 0, sizeof(*guard));
    if (!active) return true;
    if (!ordinaryPath(path, guard->path) || !isWithin(guard->path, profilePath)) return denied();
    const wchar_t *rel = guard->path + wcslen(profilePath) + 1;
    if (!_wcsicmp(rel, L".pd-reuse-owner.json") || !_wcsicmp(rel, L".pd-storage-owner.json"))
        return denied();
    if (!lockParents(guard, false)) return false;
    DWORD attr = GetFileAttributesW(guard->path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) { nwpCloseGuard(guard); return denied(); }
        guard->exists = false;
        return true;
    }
    guard->exists = true;
    HANDLE h = __real___imp_CreateFileW(guard->path, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    bool ok = h != INVALID_HANDLE_VALUE && checkHandle(h, false, guard->path);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (!ok) { nwpCloseGuard(guard); return denied(); }
    return true;
}

bool nativeWritePolicyAllowsPath(const char *path)
{
    if (!active) return true;
    wchar_t wide[NWP_MAX_PATH];
    nwp_guard guard;
    if (!nwpUtf8Path(path, wide, NWP_MAX_PATH) || !nwpGuardPath(wide, &guard)) return false;
    nwpCloseGuard(&guard);
    return true;
}

bool nwpHandleAllowed(HANDLE handle)
{
    if (!active) return true;
    DWORD type = GetFileType(handle);
    if (type == FILE_TYPE_UNKNOWN) InterlockedExchange(&deviceRefused, 1);
    /* Console and IPC are not asset files. Disk handles require exact profile
     * containment and ordinary, single-link identity before mutation. */
    if (type == FILE_TYPE_CHAR || type == FILE_TYPE_PIPE) return true;
    wchar_t path[NWP_MAX_PATH];
    if (type != FILE_TYPE_DISK || !checkHandle(handle, false, NULL) ||
        !finalPath(handle, path) || !isWithin(path, profilePath)) return denied();
    const wchar_t *rel = path + wcslen(profilePath) + 1;
    if (!_wcsicmp(rel, L".pd-reuse-owner.json") || !_wcsicmp(rel, L".pd-storage-owner.json"))
        return denied();
    return true;
}

bool nativeWritePolicyInit(const char *base, const char *profileRoot)
{
    if (active || rootLockCount || !nativeWritePolicyLinkContract()) return denied();
    wchar_t baseWide[NWP_MAX_PATH], profileWide[NWP_MAX_PATH];
    nwp_guard baseGuard = {0}, profileGuard = {0};
    if (!nwpUtf8Path(base, baseWide, NWP_MAX_PATH) ||
        !nwpUtf8Path(profileRoot, profileWide, NWP_MAX_PATH) ||
        baseWide[1] != L':' || profileWide[1] != L':' ||
        !ordinaryPath(baseWide, baseGuard.path) ||
        !ordinaryPath(profileWide, profileGuard.path) ||
        wcslen(baseGuard.path) <= 3 || wcslen(profileGuard.path) <= 3 ||
        !_wcsicmp(baseGuard.path, profileGuard.path) ||
        isWithin(baseGuard.path, profileGuard.path) || isWithin(profileGuard.path, baseGuard.path))
        return denied();
    if (!lockParents(&baseGuard, true) || !lockParents(&profileGuard, true)) {
        nwpCloseGuard(&baseGuard); nwpCloseGuard(&profileGuard); return false;
    }
    wcscpy(profilePath, profileGuard.path);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, profilePath, -1,
        profileUtf8, sizeof(profileUtf8), NULL, NULL)) {
        nwpCloseGuard(&baseGuard); nwpCloseGuard(&profileGuard); return denied();
    }
    while (baseGuard.count) rootLocks[rootLockCount++] = baseGuard.parents[--baseGuard.count];
    while (profileGuard.count) rootLocks[rootLockCount++] = profileGuard.parents[--profileGuard.count];
    active = true;
    /* Reject inherited disk outputs before any subsystem can print. Keep
     * interception and root locks active on refusal until process exit so a
     * caller cannot report the failure through an unguarded output handle. */
    HANDLE outputs[] = { GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE),
        (HANDLE)_get_osfhandle(_fileno(stdout)), (HANDLE)_get_osfhandle(_fileno(stderr)) };
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i)
        if (!nwpHandleAllowed(outputs[i])) return false;
    return true;
}

#ifdef PD_NATIVE_WRITE_POLICY_TESTING
void nativeWritePolicyResetForTests(void)
{
    InterlockedExchange(&deviceRefused, 0);
    active = false;
    while (rootLockCount) CloseHandle(rootLocks[--rootLockCount]);
    profilePath[0] = 0; profileUtf8[0] = 0;
}
#endif

HANDLE nwpCreateFileW(LPCWSTR path, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile)
{
    bool hid = path && !_wcsnicmp(path, L"\\\\?\\HID#", 8);
    bool pipe = path && (!_wcsnicmp(path, L"\\\\.\\pipe\\", 9) || !_wcsnicmp(path, L"\\\\?\\pipe\\", 9));
    if (active && (hid || pipe)) {
        DWORD allowedAccess = GENERIC_READ | GENERIC_WRITE;
        bool safe = disposition == OPEN_EXISTING && !security && !templateFile &&
            !(access & ~allowedAccess) && !(share & ~(FILE_SHARE_READ | FILE_SHARE_WRITE)) &&
            !(flags & ~FILE_FLAG_OVERLAPPED) && !wcsstr(path, L"..") &&
            !wcschr(path + (hid ? 8 : 9), L'/') && !wcschr(path + (hid ? 8 : 9), L':');
        HANDLE device = safe ? __real___imp_CreateFileW(path, access, share, security,
            disposition, flags, templateFile) : INVALID_HANDLE_VALUE;
        /* Preserve ordinary OS failures (for example Discord probing absent
         * IPC endpoints). Only a policy rejection changes device visibility. */
        if (safe && device == INVALID_HANDLE_VALUE) return device;
        DWORD type = device == INVALID_HANDLE_VALUE ? FILE_TYPE_UNKNOWN : GetFileType(device);
        if (device != INVALID_HANDLE_VALUE && type == (hid ? FILE_TYPE_CHAR : FILE_TYPE_PIPE)) return device;
        if (device != INVALID_HANDLE_VALUE) CloseHandle(device);
        InterlockedExchange(&deviceRefused, 1);
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    DWORD mutations = GENERIC_WRITE | GENERIC_ALL | DELETE | WRITE_DAC | WRITE_OWNER |
        FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES;
    if (!active || (!(access & mutations) && disposition == OPEN_EXISTING &&
        !(flags & FILE_FLAG_DELETE_ON_CLOSE)))
        return __real___imp_CreateFileW(path, access, share, security, disposition, flags, templateFile);
    nwp_guard guard;
    if (!nwpGuardPath(path, &guard)) return INVALID_HANDLE_VALUE;
    if ((guard.exists && disposition == CREATE_NEW) || (!guard.exists &&
        (disposition == OPEN_EXISTING || disposition == TRUNCATE_EXISTING))) {
        nwpCloseGuard(&guard);
        SetLastError(guard.exists ? ERROR_FILE_EXISTS : ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    DWORD safeDisposition = guard.exists ? OPEN_EXISTING : CREATE_NEW;
    HANDLE h = __real___imp_CreateFileW(guard.path, access, share, security, safeDisposition,
        flags | FILE_FLAG_OPEN_REPARSE_POINT, templateFile);
    bool ok = h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_DISK && nwpHandleAllowed(h);
    if (ok && guard.exists && (disposition == CREATE_ALWAYS || disposition == TRUNCATE_EXISTING)) {
        LARGE_INTEGER zero = {0};
        ok = SetFilePointerEx(h, zero, NULL, FILE_BEGIN) && SetEndOfFile(h);
    }
    nwpCloseGuard(&guard);
    if (!ok && h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; denied(); }
    return h;
}

int nwpOpenW(const wchar_t *path, int flags, int mode)
{
    (void)mode;
    DWORD access = (flags & _O_RDWR) ? GENERIC_READ | GENERIC_WRITE :
        ((flags & _O_WRONLY) ? GENERIC_WRITE : GENERIC_READ);
    DWORD disposition = OPEN_EXISTING;
    if (flags & _O_CREAT) disposition = (flags & _O_EXCL) ? CREATE_NEW :
        ((flags & _O_TRUNC) ? CREATE_ALWAYS : OPEN_ALWAYS);
    else if (flags & _O_TRUNC) disposition = TRUNCATE_EXISTING;
    HANDLE h = nwpCreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        disposition, (flags & _O_TEMPORARY) ? FILE_FLAG_DELETE_ON_CLOSE : FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int fd = _open_osfhandle((intptr_t)h, flags & ~(_O_CREAT | _O_EXCL | _O_TRUNC));
    if (fd < 0) CloseHandle(h);
    return fd;
}

FILE *nwpFopenW(const wchar_t *path, const wchar_t *mode)
{
    if (!mode || !mode[0] || wcslen(mode) > 5) { denied(); return NULL; }
    int flags = wcschr(mode, L'+') ? _O_RDWR : (mode[0] == L'r' ? _O_RDONLY : _O_WRONLY);
    flags |= wcschr(mode, L'b') ? _O_BINARY : _O_TEXT;
    if (mode[0] == L'w') flags |= _O_CREAT | _O_TRUNC;
    else if (mode[0] == L'a') flags |= _O_CREAT | _O_APPEND;
    else if (mode[0] != L'r') { denied(); return NULL; }
    for (const wchar_t *p = mode + 1; *p; ++p)
        if (*p != L'+' && *p != L'b' && *p != L't' && *p != L'x') { denied(); return NULL; }
    if (wcschr(mode, L'x')) flags |= _O_EXCL;
    int fd = nwpOpenW(path, flags, 0666);
    if (fd < 0) return NULL;
    FILE *file = _wfdopen(fd, mode);
    if (!file) _close(fd);
    return file;
}

bool nativeWritePolicyCachePath(const char *kind, const char *identity, char *out, size_t size)
{
    if (!active || !kind || !kind[0] || !identity || !out || !size) return false;
    for (const char *p = kind; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return denied();
    char hash[65];
    sha256_ctx ctx;
    unsigned char digest[32];
    sha256Init(&ctx); sha256Update(&ctx, identity, strlen(identity)); sha256Final(&ctx, digest);
    for (unsigned i = 0; i < 32; ++i) snprintf(hash + i * 2, 3, "%02x", digest[i]);
    char parent[NWP_MAX_PATH * 3];
    wchar_t wide[NWP_MAX_PATH];
    int n = snprintf(parent, sizeof(parent), "%s/cache", profileUtf8);
    if (n < 0 || (size_t)n >= sizeof(parent) || !nwpUtf8Path(parent, wide, NWP_MAX_PATH)) return false;
    if (!CreateDirectoryW(wide, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    n = snprintf(parent, sizeof(parent), "%s/cache/%s", profileUtf8, kind);
    if (n < 0 || (size_t)n >= sizeof(parent) || !nwpUtf8Path(parent, wide, NWP_MAX_PATH)) return false;
    if (!CreateDirectoryW(wide, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    n = snprintf(out, size, "%s/%s", parent, hash);
    if (n < 0 || (size_t)n >= size) { out[0] = 0; return false; }
    return nativeWritePolicyAllowsPath(out);
}
