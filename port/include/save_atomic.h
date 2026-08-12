#ifndef PD_SAVE_ATOMIC_H
#define PD_SAVE_ATOMIC_H

#include <stdio.h>
#include <PR/ultratypes.h>
#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A save transaction always writes a sibling candidate first.  Commit flushes
 * the candidate to disk and atomically replaces the destination.  Any error
 * removes only the candidate, leaving an existing destination untouched. */
typedef struct save_atomic_file {
	FILE *stream;
	char destination[FS_MAXPATH + 1];
	char candidate[FS_MAXPATH + 1];
} save_atomic_file_t;

s32 saveAtomicBegin(save_atomic_file_t *transaction, const char *destination);
FILE *saveAtomicStream(save_atomic_file_t *transaction);
s32 saveAtomicCommit(save_atomic_file_t *transaction);
void saveAtomicAbort(save_atomic_file_t *transaction);

/* Debug/smoke seam.  The next otherwise-successful commit fails immediately
 * before replacement, then automatically clears the latch. */
void saveAtomicDebugFailNextCommit(void);

#ifdef __cplusplus
}
#endif

#endif
