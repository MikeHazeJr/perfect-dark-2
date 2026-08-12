#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "save_atomic.h"
#include "system.h"

static u32 s_SaveAtomicSerial;
static s32 s_SaveAtomicFailNextCommit;

static unsigned long saveAtomicProcessId(void)
{
#ifdef _WIN32
	return (unsigned long)_getpid();
#else
	return (unsigned long)getpid();
#endif
}

static s32 saveAtomicSync(FILE *stream)
{
	if (!stream || ferror(stream) || fflush(stream) != 0) {
		return -1;
	}
#ifdef _WIN32
	return _commit(_fileno(stream)) == 0 ? 0 : -1;
#else
	return fsync(fileno(stream)) == 0 ? 0 : -1;
#endif
}

static s32 saveAtomicReplacePath(const char *candidate,
		const char *destination)
{
#ifdef _WIN32
	return MoveFileExA(candidate, destination,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
	return rename(candidate, destination) == 0 ? 0 : -1;
#endif
}

s32 saveAtomicBegin(save_atomic_file_t *transaction, const char *destination)
{
	int written;

	if (!transaction || !destination || !destination[0]) {
		return -1;
	}
	memset(transaction, 0, sizeof(*transaction));
	if (strlen(destination) >= sizeof(transaction->destination)) {
		sysLogPrintf(LOG_ERROR,
			"SAVE.ATOMIC: destination path exceeds the filesystem contract");
		return -1;
	}
	strcpy(transaction->destination, destination);

	written = snprintf(transaction->candidate,
		sizeof(transaction->candidate), "%s.pd2tmp-%lu-%u", destination,
		saveAtomicProcessId(), (unsigned)++s_SaveAtomicSerial);
	if (written < 0 || (size_t)written >= sizeof(transaction->candidate)) {
		sysLogPrintf(LOG_ERROR,
			"SAVE.ATOMIC: candidate path exceeds the filesystem contract");
		memset(transaction, 0, sizeof(*transaction));
		return -1;
	}

	transaction->stream = fopen(transaction->candidate, "wb");
	if (!transaction->stream) {
		sysLogPrintf(LOG_ERROR,
			"SAVE.ATOMIC: could not open candidate '%s': errno=%d",
			transaction->candidate, errno);
		memset(transaction, 0, sizeof(*transaction));
		return -1;
	}
	return 0;
}

FILE *saveAtomicStream(save_atomic_file_t *transaction)
{
	return transaction ? transaction->stream : NULL;
}

void saveAtomicAbort(save_atomic_file_t *transaction)
{
	if (!transaction) {
		return;
	}
	if (transaction->stream) {
		fclose(transaction->stream);
		transaction->stream = NULL;
	}
	if (transaction->candidate[0]) {
		remove(transaction->candidate);
	}
}

s32 saveAtomicCommit(save_atomic_file_t *transaction)
{
	s32 failed = 0;

	if (!transaction || !transaction->stream) {
		return -1;
	}
	if (saveAtomicSync(transaction->stream) != 0) {
		failed = 1;
	}
	if (fclose(transaction->stream) != 0) {
		failed = 1;
	}
	transaction->stream = NULL;

	if (!failed && s_SaveAtomicFailNextCommit) {
		s_SaveAtomicFailNextCommit = 0;
		failed = 1;
		sysLogPrintf(LOG_NOTE,
			"SAVE.ATOMIC: debug failure injected before replace for '%s'",
			transaction->destination);
	}
	if (!failed && saveAtomicReplacePath(transaction->candidate,
			transaction->destination) != 0) {
		failed = 1;
		sysLogPrintf(LOG_ERROR,
			"SAVE.ATOMIC: could not replace '%s': errno=%d",
			transaction->destination, errno);
	}
	if (failed) {
		remove(transaction->candidate);
		return -1;
	}
	transaction->candidate[0] = '\0';
	return 0;
}

void saveAtomicDebugFailNextCommit(void)
{
	s_SaveAtomicFailNextCommit = 1;
}
