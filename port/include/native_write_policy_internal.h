#ifndef PD_NATIVE_WRITE_POLICY_INTERNAL_H
#define PD_NATIVE_WRITE_POLICY_INTERNAL_H
#include "native_write_policy.h"
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define NWP_MAX_PATH 4096
#define NWP_MAX_PARENTS 128
typedef struct nwp_guard {
    wchar_t path[NWP_MAX_PATH];
    HANDLE parents[NWP_MAX_PARENTS];
    unsigned int count;
    bool exists;
} nwp_guard;

bool nwpUtf8Path(const char *path, wchar_t *out, size_t count);
bool nwpGuardPath(const wchar_t *path, nwp_guard *guard);
void nwpCloseGuard(nwp_guard *guard);
bool nwpHandleAllowed(HANDLE handle);
HANDLE nwpCreateFileW(LPCWSTR path, DWORD access, DWORD share,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templateFile);
FILE *nwpFopenW(const wchar_t *path, const wchar_t *mode);
int nwpOpenW(const wchar_t *path, int flags, int mode);

/* These are the original import pointers, not import stubs which can resolve
 * back through the wrapped pointer. Only this implementation uses them. */
extern __typeof__(&CreateFileW) __real___imp_CreateFileW;
extern __typeof__(&WriteFile) __real___imp_WriteFile;
extern __typeof__(&MoveFileExA) __real___imp_MoveFileExA;
extern __typeof__(&MoveFileExW) __real___imp_MoveFileExW;
extern __typeof__(&CopyFileA) __real___imp_CopyFileA;
extern __typeof__(&CopyFileW) __real___imp_CopyFileW;
extern __typeof__(&DeleteFileA) __real___imp_DeleteFileA;
extern __typeof__(&DeleteFileW) __real___imp_DeleteFileW;
extern __typeof__(&CreateDirectoryA) __real___imp_CreateDirectoryA;
extern __typeof__(&CreateDirectoryW) __real___imp_CreateDirectoryW;
extern __typeof__(&RemoveDirectoryA) __real___imp_RemoveDirectoryA;
extern __typeof__(&RemoveDirectoryW) __real___imp_RemoveDirectoryW;
#endif
