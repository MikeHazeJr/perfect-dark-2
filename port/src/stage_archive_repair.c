#include <windows.h>
#include <SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "assetcatalog.h"
#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "fs.h"
#include "loader_walker.h"
#include "modarchive.h"
#include "modvfs.h"
#include "native_write_policy.h"
#include "romdata.h"
#include "romextract_pd.h"
#include "sha256.h"
#include "stage_archive_repair.h"
#include "system.h"

typedef enum stage_repair_phase {
	STAGE_REPAIR_QUEUED,
	STAGE_REPAIR_PREPARING,
	STAGE_REPAIR_RUNNING,
	STAGE_REPAIR_ADOPTING,
	STAGE_REPAIR_PUBLISHING,
	STAGE_REPAIR_READY,
	STAGE_REPAIR_FAILED,
} stage_repair_phase_t;

typedef struct stage_repair_job {
	s32 stagenum;
	char id[CATALOG_ID_LEN];
	char path[FS_MAXPATH + 1];
	char mission_path[FS_MAXPATH + 1];
	wchar_t original[FS_MAXPATH + 1];
	wchar_t scratch[FS_MAXPATH + 1];
	wchar_t worker_archive[FS_MAXPATH + 1];
	u8 original_digest[SHA256_DIGEST_SIZE];
	u8 candidate_digest[SHA256_DIGEST_SIZE];
	s32 original_present;
	s32 changed;
	DWORD child_exit;
	ULONGLONG child_started;
	HANDLE child;
	HANDLE process_job;
	SDL_atomic_t phase;
} stage_repair_job_t;

static s32 s_Enabled;
static s32 s_Started;
static stage_repair_job_t *s_Jobs;
static size_t s_JobCount;
static size_t s_JobCapacity;
static size_t s_NextJob;
static wchar_t s_Executable[FS_MAXPATH + 1];
static wchar_t s_BaseDirectory[FS_MAXPATH + 1];
static wchar_t s_RomPath[FS_MAXPATH + 1];
static wchar_t s_TempDirectory[FS_MAXPATH + 1];

static s32 repairWidePath(const char *path, wchar_t out[FS_MAXPATH + 1])
{
	wchar_t wide[FS_MAXPATH + 1];
	/* The Windows client's narrow argv and file resolver use the CRT code page. */
	if (!path || !path[0] || !MultiByteToWideChar(CP_ACP, 0, path, -1,
			wide, SDL_arraysize(wide))) return 0;
	const DWORD length = GetFullPathNameW(wide, FS_MAXPATH + 1, out, NULL);
	return length > 0 && length <= FS_MAXPATH;
}

static s32 repairInitializePaths(void)
{
	const DWORD executable_length = GetModuleFileNameW(NULL, s_Executable, SDL_arraysize(s_Executable));
	const DWORD temp_length = GetTempPathW(SDL_arraysize(s_TempDirectory), s_TempDirectory);
	char rom_path[FS_MAXPATH + 1];
	if (!executable_length || executable_length > FS_MAXPATH || temp_length < 3
			|| temp_length > FS_MAXPATH || s_TempDirectory[1] != L':'
			|| !repairWidePath(fsGetBaseDir(), s_BaseDirectory)
			|| !repairWidePath(fsFullPath(g_RomName, rom_path, sizeof(rom_path)), s_RomPath)) return 0;
	const DWORD rom_attributes = GetFileAttributesW(s_RomPath);
	return rom_attributes != INVALID_FILE_ATTRIBUTES
		&& !(rom_attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

static s32 repairResolveLoosePath(const char *path, char out[FS_MAXPATH + 1])
{
	char resolved[FS_MAXPATH + 1];
	return !modVfsCanResolve(path)
		&& assetPathJoinChecked(out, FS_MAXPATH + 1, fsGetBaseDir(), "/", path)
		&& strcmp(fsFullPath(path, resolved, sizeof(resolved)), out) == 0;
}

void stageArchiveRepairSetEnabled(s32 enabled)
{
	s_Enabled = enabled;
}

s32 stageArchiveRepairQueue(s32 stagenum, const char *expected_id, const char *relative_path)
{
	static const char prefix[] = "base:scenario_";
	if (!s_Enabled || s_Started || nativeWritePolicyActive() || !expected_id || !relative_path
			|| strncmp(expected_id, prefix, sizeof(prefix) - 1) != 0) return 0;
	const char *slug = expected_id + sizeof(prefix) - 1;
	if (!slug[0] || slug[strspn(slug, "abcdefghijklmnopqrstuvwxyz0123456789_-")]) return 0;
	char canonical[FS_MAXPATH + 1];
	char data_root[FS_MAXPATH + 1];
	fsDataDir(data_root, sizeof(data_root));
	const int length = snprintf(canonical, sizeof(canonical), "%s/scenarios/base_scenario_%s.pdscenario",
		data_root, slug);
	if (length < 0 || (size_t)length >= sizeof(canonical) || strcmp(canonical, relative_path)) return 0;
	for (size_t i = 0; i < s_JobCount; i++) {
		const stage_repair_job_t *prior = &s_Jobs[i];
		if (prior->stagenum == stagenum || strcmp(prior->path, relative_path) == 0) {
			return prior->stagenum == stagenum && strcmp(prior->path, relative_path) == 0;
		}
	}
	stage_repair_job_t job = {0};
	char loose_path[FS_MAXPATH + 1];
	if (!assetPathCopyChecked(job.id, sizeof(job.id), expected_id)
			|| !assetPathCopyChecked(job.path, sizeof(job.path), relative_path)
			|| romExtractStandaloneMissionPath(stagenum, job.mission_path, sizeof(job.mission_path)) < 0
			|| (job.mission_path[0] && !repairResolveLoosePath(job.mission_path, loose_path))
			|| !repairResolveLoosePath(relative_path, loose_path)
			|| !repairWidePath(loose_path, job.original)
			|| (s_JobCount == 0 && !repairInitializePaths())) return 0;
	if (s_JobCount == s_JobCapacity) {
		const size_t max_jobs = SIZE_MAX / sizeof(*s_Jobs);
		if (s_JobCapacity > max_jobs / 2) return 0;
		const size_t capacity = s_JobCapacity ? s_JobCapacity * 2 : 8;
		stage_repair_job_t *jobs = realloc(s_Jobs, capacity * sizeof(*s_Jobs));
		if (!jobs) return 0;
		s_Jobs = jobs;
		s_JobCapacity = capacity;
	}
	job.stagenum = stagenum;
	s_Jobs[s_JobCount++] = job;
	sysLogPrintf(LOG_NOTE, "STAGE.REPAIR.QUEUED: stagenum=%d id=%s path=%s",
		stagenum, expected_id, relative_path);
	return 1;
}

static s32 repairJobStatus(stage_repair_job_t *job)
{
	const s32 phase = SDL_AtomicGet(&job->phase);
	if (phase == STAGE_REPAIR_READY) return 0;
	if (phase == STAGE_REPAIR_FAILED) return -1;
	return 1;
}

s32 stageArchiveRepairIsPendingPath(const char *relative_path)
{
	if (!relative_path) return 0;
	for (size_t i = 0; i < s_JobCount; i++) {
		if (strcmp(s_Jobs[i].path, relative_path) == 0) return repairJobStatus(&s_Jobs[i]) != 0;
	}
	return 0;
}

s32 stageArchiveRepairIsPendingMissionPath(const char *relative_path)
{
	if (!relative_path || !relative_path[0]) return 0;
	for (size_t i = 0; i < s_JobCount; i++) {
		if (strcmp(s_Jobs[i].mission_path, relative_path) == 0) return repairJobStatus(&s_Jobs[i]) != 0;
	}
	return 0;
}

s32 stageArchiveRepairStatus(s32 stagenum)
{
	for (size_t i = 0; i < s_JobCount; i++) {
		if (s_Jobs[i].stagenum == stagenum) return repairJobStatus(&s_Jobs[i]);
	}
	return 0;
}

s32 stageArchiveRepairHasJobs(void)
{
	return s_NextJob < s_JobCount;
}

static HANDLE repairOpenArchive(const wchar_t *path, s32 *missing)
{
	*missing = 0;
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		const DWORD error = GetLastError();
		*missing = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
		return file;
	}
	BY_HANDLE_FILE_INFORMATION info;
	if (!GetFileInformationByHandle(file, &info)
			|| (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
		CloseHandle(file);
		return INVALID_HANDLE_VALUE;
	}
	return file;
}

static s32 repairReadArchive(HANDLE input, HANDLE output, u8 *digest)
{
	sha256_ctx hash;
	if (digest) sha256Init(&hash);
	u8 buffer[32768];
	for (;;) {
		DWORD size;
		if (!ReadFile(input, buffer, sizeof(buffer), &size, NULL)) return 0;
		if (!size) break;
		if (digest) sha256Update(&hash, buffer, size);
		if (output != INVALID_HANDLE_VALUE) {
			DWORD written;
			if (!WriteFile(output, buffer, size, &written, NULL) || written != size) return 0;
		}
	}
	if (digest) sha256Final(&hash, digest);
	return 1;
}

static s32 repairSeedWorker(stage_repair_job_t *job)
{
	s32 missing;
	HANDLE source = repairOpenArchive(job->original, &missing);
	if (source == INVALID_HANDLE_VALUE) return missing;
	job->original_present = 1;
	HANDLE copy = CreateFileW(job->worker_archive, GENERIC_WRITE, 0, NULL, CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL, NULL);
	const s32 ok = copy != INVALID_HANDLE_VALUE && repairReadArchive(source, copy, job->original_digest);
	if (copy != INVALID_HANDLE_VALUE) CloseHandle(copy);
	CloseHandle(source);
	return ok;
}

static s32 repairOrdinaryDirectory(const wchar_t *path)
{
	const DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)
		&& !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

static void repairCleanup(stage_repair_job_t *job)
{
	if (job->scratch[0] && repairOrdinaryDirectory(job->scratch)) {
		if (job->worker_archive[0]) DeleteFileW(job->worker_archive);
		/* sysInit writes a log under the worker's private working directory. */
		wchar_t logs[FS_MAXPATH + 1], directory[FS_MAXPATH + 1], path[FS_MAXPATH + 1];
		const int logs_length = swprintf(logs, SDL_arraysize(logs), L"%ls\\logs", job->scratch);
		if (logs_length >= 0 && logs_length <= FS_MAXPATH && repairOrdinaryDirectory(logs)) {
			const int dir_length = swprintf(directory, SDL_arraysize(directory), L"%ls\\game client", logs);
			if (dir_length >= 0 && dir_length <= FS_MAXPATH && repairOrdinaryDirectory(directory)) {
				const int file_length = swprintf(path, SDL_arraysize(path), L"%ls\\pd-client.log", directory);
				if (file_length >= 0 && file_length <= FS_MAXPATH) DeleteFileW(path);
				RemoveDirectoryW(directory);
			}
			RemoveDirectoryW(logs);
		}
		RemoveDirectoryW(job->scratch);
	}
	job->scratch[0] = L'\0';
	job->worker_archive[0] = L'\0';
}

static void repairFail(stage_repair_job_t *job, const char *reason)
{
	repairCleanup(job);
	sysLogPrintf(LOG_WARNING, "STAGE.REPAIR.FAILED: stagenum=%d id=%s reason=%s",
		job->stagenum, job->id, reason);
	SDL_AtomicSet(&job->phase, STAGE_REPAIR_FAILED);
}

static s32 repairAppendArg(wchar_t *command, size_t capacity, size_t *used, const wchar_t *arg)
{
	if (wcschr(arg, L'"')) return 0;
	const size_t length = wcslen(arg);
	size_t trailing = 0;
	while (trailing < length && arg[length - trailing - 1] == L'\\') trailing++;
	if (capacity - *used <= length + trailing + 3) return 0;
	if (*used) command[(*used)++] = L' ';
	command[(*used)++] = L'"';
	memcpy(command + *used, arg, length * sizeof(*arg));
	*used += length;
	while (trailing--) command[(*used)++] = L'\\';
	command[(*used)++] = L'"';
	command[*used] = L'\0';
	return 1;
}

static s32 repairStartChild(stage_repair_job_t *job)
{
	wchar_t stage_number[16];
	swprintf(stage_number, SDL_arraysize(stage_number), L"%d", job->stagenum);
	const wchar_t *args[] = {s_Executable, L"--extract-assets-only", L"--stage-repair", stage_number,
		L"--stage-repair-output", job->scratch, L"--basedir", s_BaseDirectory,
		L"--savedir", job->scratch, L"--moddir", job->scratch, L"--rom-file", s_RomPath,
		L"--no-update-check", L"--no-sound", L"--portable"};
	wchar_t command[7 * (FS_MAXPATH + 5) + 256];
	size_t used = 0;
	for (size_t i = 0; i < SDL_arraysize(args); i++) {
		if (!repairAppendArg(command, SDL_arraysize(command), &used, args[i])) return 0;
	}
	HANDLE process_job = CreateJobObjectW(NULL, NULL);
	if (!process_job) return 0;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	STARTUPINFOW start = {0};
	start.cb = sizeof(start);
	start.dwFlags = STARTF_USESHOWWINDOW;
	start.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION process = {0};
	if (!SetInformationJobObject(process_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
			|| !CreateProcessW(s_Executable, command, NULL, NULL, FALSE,
				CREATE_NO_WINDOW | CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS,
				NULL, job->scratch, &start, &process)) {
		CloseHandle(process_job);
		return 0;
	}
	const s32 started = AssignProcessToJobObject(process_job, process.hProcess)
		&& ResumeThread(process.hThread) != (DWORD)-1;
	CloseHandle(process.hThread);
	if (!started) {
		TerminateProcess(process.hProcess, 2);
		CloseHandle(process.hProcess);
		CloseHandle(process_job);
		return 0;
	}
	job->child = process.hProcess;
	job->process_job = process_job;
	job->child_started = GetTickCount64();
	return 1;
}

static int SDLCALL repairPrepareWorker(void *data)
{
	stage_repair_job_t *job = data;
	const int length = swprintf(job->scratch, SDL_arraysize(job->scratch), L"%lspd2-stage-%lu-%llu-%d",
		s_TempDirectory, (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64(), job->stagenum);
	if (length < 0 || length > FS_MAXPATH || !CreateDirectoryW(job->scratch, NULL)) {
		job->scratch[0] = L'\0'; /* No ownership was acquired. */
		repairFail(job, "could not create private directory");
		return 0;
	}
	const wchar_t *leaf = wcsrchr(job->original, L'\\');
	const wchar_t *slash = wcsrchr(job->original, L'/');
	if (slash && (!leaf || slash > leaf)) leaf = slash;
	const int output_length = leaf ? swprintf(job->worker_archive, SDL_arraysize(job->worker_archive),
		L"%ls\\%ls", job->scratch, leaf + 1) : -1;
	if (output_length < 0 || output_length > FS_MAXPATH) {
		job->worker_archive[0] = L'\0';
		repairFail(job, "private archive path exceeds capacity");
	} else if (!repairSeedWorker(job)) {
		repairFail(job, "could not snapshot loose scenario");
	} else if (!repairStartChild(job)) {
		repairFail(job, "could not start isolated worker");
	} else {
		sysLogPrintf(LOG_NOTE, "STAGE.REPAIR.START: stagenum=%d id=%s", job->stagenum, job->id);
		SDL_AtomicSet(&job->phase, STAGE_REPAIR_RUNNING);
	}
	return 0;
}

static s32 repairOriginalMatches(const stage_repair_job_t *job)
{
	s32 missing;
	HANDLE original = repairOpenArchive(job->original, &missing);
	if (original == INVALID_HANDLE_VALUE) return missing && !job->original_present;
	u8 digest[SHA256_DIGEST_SIZE];
	const s32 same = job->original_present && repairReadArchive(original, INVALID_HANDLE_VALUE, digest)
		&& memcmp(job->original_digest, digest, sizeof(digest)) == 0;
	CloseHandle(original);
	return same;
}

static s32 repairCandidateMatches(const stage_repair_job_t *job)
{
	char path[FS_MAXPATH + 1];
	wchar_t roundtrip[FS_MAXPATH + 1];
	if (!WideCharToMultiByte(CP_ACP, 0, job->worker_archive, -1, path, sizeof(path), NULL, NULL)
			|| !MultiByteToWideChar(CP_ACP, 0, path, -1, roundtrip, SDL_arraysize(roundtrip))
			|| wcscmp(job->worker_archive, roundtrip) != 0) return 0;
	mod_archive_t *source = modArchiveOpen(path);
	if (!source) return 0;
	const s32 entry = modArchiveFindEntry(source, ASSET_ARCHIVE_META_MANIFEST_PATH);
	u32 manifest_size = 0;
	char *manifest = entry >= 0 ? modArchiveExtractAlloc(source, entry, &manifest_size) : NULL;
	modArchiveClose(source);
	if (!manifest) return 0;
	char kind[32], id[CATALOG_ID_LEN];
	s64 stagenum = 0;
	const s32 matches = loaderWalkerEnvelopeStrCopy(manifest, manifest_size, "pd_kind", kind, sizeof(kind))
		&& strcmp(kind, "scenario") == 0
		&& loaderWalkerEnvelopeStrCopy(manifest, manifest_size, "id", id, sizeof(id))
		&& strcmp(id, job->id) == 0
		&& loaderWalkerEnvelopeInt(manifest, manifest_size, "stagenum", &stagenum)
		&& stagenum == job->stagenum;
	free(manifest);
	return matches;
}

static s32 repairAdoptArchive(stage_repair_job_t *job, HANDLE archive)
{
	if (!repairReadArchive(archive, INVALID_HANDLE_VALUE, job->candidate_digest)) return 0;
	if (job->original_present && memcmp(job->original_digest, job->candidate_digest, SHA256_DIGEST_SIZE) == 0) {
		return repairOriginalMatches(job);
	}
	wchar_t candidate[FS_MAXPATH + 1];
	int length = swprintf(candidate, SDL_arraysize(candidate), L"%ls.pd2repair-%lu-%d.tmp",
		job->original, (unsigned long)GetCurrentProcessId(), job->stagenum);
	if (length < 0 || length > FS_MAXPATH) return 0;
	LARGE_INTEGER zero = {0};
	if (!SetFilePointerEx(archive, zero, NULL, FILE_BEGIN)) return 0;
	/* Own a unique sibling independently of the main thread's save serial. */
	HANDLE copy = CreateFileW(candidate, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (copy == INVALID_HANDLE_VALUE) return 0;
	const s32 copied = repairReadArchive(archive, copy, NULL) && FlushFileBuffers(copy);
	CloseHandle(copy);
	const DWORD flags = MOVEFILE_WRITE_THROUGH | (job->original_present ? MOVEFILE_REPLACE_EXISTING : 0);
	if (!copied || !repairOriginalMatches(job) || !MoveFileExW(candidate, job->original, flags)) {
		DeleteFileW(candidate);
		return 0;
	}
	job->changed = 1;
	return 1;
}

static int SDLCALL repairCollectWorker(void *data)
{
	stage_repair_job_t *job = data;
	if (job->child_exit != 0) {
		repairFail(job, "isolated worker exited unsuccessfully");
		return 0;
	}
	s32 missing;
	HANDLE archive = repairOpenArchive(job->worker_archive, &missing);
	if (archive == INVALID_HANDLE_VALUE) {
		repairFail(job, "worker did not produce an ordinary scenario archive");
		return 0;
	}
	/* Hold the candidate open against writes while qualifying and adopting it. */
	if (!repairCandidateMatches(job)) {
		CloseHandle(archive);
		repairFail(job, "worker output does not match the queued scenario identity");
		return 0;
	}
	const s32 adopted = repairAdoptArchive(job, archive);
	CloseHandle(archive);
	if (!adopted) {
		repairFail(job, "loose scenario changed or the atomic replacement failed");
	} else {
		repairCleanup(job);
		SDL_AtomicSet(&job->phase, STAGE_REPAIR_PUBLISHING);
	}
	return 0;
}

static void repairStartThread(stage_repair_job_t *job, s32 phase, SDL_ThreadFunction worker)
{
	SDL_AtomicSet(&job->phase, phase);
	SDL_Thread *thread = SDL_CreateThread(worker, "stage archive repair", job);
	if (!thread) repairFail(job, "could not start repair thread");
	else SDL_DetachThread(thread);
}

void stageArchiveRepairPump(s32 menu_ready)
{
	if (!stageArchiveRepairHasJobs() || (!s_Started && !menu_ready)) return;
	s_Started = 1;
	stage_repair_job_t *job = &s_Jobs[s_NextJob];
	switch (SDL_AtomicGet(&job->phase)) {
	case STAGE_REPAIR_QUEUED:
		repairStartThread(job, STAGE_REPAIR_PREPARING, repairPrepareWorker);
		break;
	case STAGE_REPAIR_RUNNING: {
		const DWORD wait_result = WaitForSingleObject(job->child, 0);
		if (wait_result == WAIT_TIMEOUT) {
			if (GetTickCount64() - job->child_started >= 60000) {
				TerminateJobObject(job->process_job, ERROR_TIMEOUT);
			}
			break;
		}
		DWORD exit_code = 1;
		if (wait_result == WAIT_OBJECT_0) GetExitCodeProcess(job->child, &exit_code);
		CloseHandle(job->process_job);
		CloseHandle(job->child);
		char loose_path[FS_MAXPATH + 1];
		job->child_exit = repairResolveLoosePath(job->path, loose_path) ? exit_code : 1;
		repairStartThread(job, STAGE_REPAIR_ADOPTING, repairCollectWorker);
		break;
	}
	case STAGE_REPAIR_PUBLISHING: {
		char loose_path[FS_MAXPATH + 1];
		if (job->mission_path[0] && (!repairResolveLoosePath(job->mission_path, loose_path)
				|| romExtractRefreshStandaloneMission(job->stagenum) < 0
				|| !loaderWalkerPublishMissionArchive(job->mission_path, job->id))) {
			repairFail(job, "standalone mission metadata could not be refreshed");
		} else if (!loaderWalkerPublishScenarioArchive(job->path, job->id)) {
			repairFail(job, "verified archive could not be published in the catalog");
		} else {
			romExtractRecordStandaloneScenarioVerified(job->stagenum, job->path, job->candidate_digest);
			sysLogPrintf(LOG_NOTE, "STAGE.REPAIR.READY: stagenum=%d id=%s changed=%d",
				job->stagenum, job->id, job->changed);
			SDL_AtomicSet(&job->phase, STAGE_REPAIR_READY);
		}
		break;
	}
	case STAGE_REPAIR_READY:
	case STAGE_REPAIR_FAILED:
		s_NextJob++;
		break;
	default:
		break;
	}
}
