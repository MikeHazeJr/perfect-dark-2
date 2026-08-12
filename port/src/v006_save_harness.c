#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bss.h"
#include "data.h"
#include "fs.h"
#include "mpsetups.h"
#include "net/matchsetup.h"
#include "save_atomic.h"
#include "savefile.h"
#include "system.h"
#include "v006_save_harness.h"

static s32 v006WriteBytes(const char *path, const void *bytes, size_t size)
{
	FILE *stream = fopen(path, "wb");
	if (!stream) return -1;
	s32 ok = fwrite(bytes, 1, size, stream) == size && fflush(stream) == 0;
	if (fclose(stream) != 0) ok = 0;
	return ok ? 0 : -1;
}

static void *v006ReadBytes(const char *path, size_t *out_size)
{
	FILE *stream = fopen(path, "rb");
	long length;
	void *bytes;

	if (out_size) *out_size = 0;
	if (!stream || fseek(stream, 0, SEEK_END) != 0
			|| (length = ftell(stream)) < 0
			|| fseek(stream, 0, SEEK_SET) != 0) {
		if (stream) fclose(stream);
		return NULL;
	}
	bytes = malloc((size_t)length + 1);
	if (!bytes || fread(bytes, 1, (size_t)length, stream) != (size_t)length) {
		free(bytes);
		fclose(stream);
		return NULL;
	}
	fclose(stream);
	if (out_size) *out_size = (size_t)length;
	return bytes;
}

static s32 v006FileEquals(const char *path, const void *expected,
		size_t expected_size)
{
	size_t actual_size = 0;
	void *actual = v006ReadBytes(path, &actual_size);
	s32 equal = actual && actual_size == expected_size
		&& memcmp(actual, expected, expected_size) == 0;
	free(actual);
	return equal;
}

static s32 v006Report(const char *name, s32 passed, s32 *passed_count)
{
	sysLogPrintf(passed ? LOG_NOTE : LOG_ERROR,
		"V006.SAVE: case=%s result=%s", name, passed ? "PASS" : "FAIL");
	if (passed && passed_count) (*passed_count)++;
	return passed;
}

s32 v006SaveFailclosedRun(void)
{
	char json_path[FS_MAXPATH + 1];
	char binary_path[FS_MAXPATH + 1];
	struct mpsetup initial_setup = g_MpSetup;
	struct matchconfig initial_match = g_MatchConfig;
	struct mpsetupfile initial_setup_file = g_MpSetupFile;
	s16 initial_current = g_MpCurrentSetup;
	s32 passed = 0;
	void *baseline = NULL;
	size_t baseline_size = 0;

	snprintf(json_path, sizeof(json_path), "%s/mpsetup_v006_atomic.json",
		saveGetDir());
	fsFullPath("$S/mpsetups.bin", binary_path, sizeof(binary_path));

	matchConfigInit();
	g_MpSetup.timelimit = 41;
	g_MatchConfig.scorelimit = 17;
	if (saveSaveMpSetup("v006_atomic") != 0) goto done;

	{
		const char malformed[] = "{\"version\":2,\"weapon_ids\":[";
		struct mpsetup before_setup = g_MpSetup;
		struct matchconfig before_match = g_MatchConfig;
		struct mpplayerconfig before_player = g_PlayerConfigsArray[0];
		s32 result = v006WriteBytes(json_path, malformed,
			sizeof(malformed) - 1) == 0 ? saveLoadMpSetup("v006_atomic") : 0;
		v006Report("json_malformed_atomic_reject", result != 0
			&& memcmp(&before_setup, &g_MpSetup, sizeof(before_setup)) == 0
			&& memcmp(&before_match, &g_MatchConfig, sizeof(before_match)) == 0
			&& memcmp(&before_player, &g_PlayerConfigsArray[0],
				sizeof(before_player)) == 0,
			&passed);
	}

	{
		const char semantic[] =
			"{\"version\":2,\"timelimit\":7,\"weapon_ids\":["
			"\"v006:missing_weapon\",\"\",\"\",\"\",\"\",\"\"],\"bots\":[]}";
		struct mpsetup before_setup = g_MpSetup;
		struct matchconfig before_match = g_MatchConfig;
		struct mpplayerconfig before_player = g_PlayerConfigsArray[0];
		s32 result = v006WriteBytes(json_path, semantic,
			sizeof(semantic) - 1) == 0 ? saveLoadMpSetup("v006_atomic") : 0;
		v006Report("json_semantic_atomic_reject", result != 0
			&& memcmp(&before_setup, &g_MpSetup, sizeof(before_setup)) == 0
			&& memcmp(&before_match, &g_MatchConfig, sizeof(before_match)) == 0
			&& memcmp(&before_player, &g_PlayerConfigsArray[0],
				sizeof(before_player)) == 0,
			&passed);
	}

	if (saveSaveMpSetup("v006_atomic") != 0) goto done;
	baseline = v006ReadBytes(json_path, &baseline_size);
	if (!baseline) goto done;
	g_MpSetup.scorelimit ^= 1;
	saveAtomicDebugFailNextCommit();
	v006Report("json_failed_commit_preserves_file",
		saveSaveMpSetup("v006_atomic") != 0
			&& v006FileEquals(json_path, baseline, baseline_size), &passed);
	free(baseline);
	baseline = NULL;

	memset(&g_MpSetupFile, 0, sizeof(g_MpSetupFile));
	g_MpSetupFile.version = MPSETUP_VERSION;
	g_MpSetupFile.numsetups = 1;
	g_MpSetupFile.setups[0].bytes[0] = 0x5a;
	if (mpsetupSaveCurrentFile() != 0) goto done;
	{
		const u8 truncated[] = { MPSETUP_VERSION, 0 };
		struct mpsetupfile before = g_MpSetupFile;
		s32 result = v006WriteBytes(binary_path, truncated,
			sizeof(truncated)) == 0 ? mpsetupLoadCurrentFile() : 0;
		v006Report("binary_truncated_atomic_reject", result != 0
			&& memcmp(&before, &g_MpSetupFile, sizeof(before)) == 0, &passed);
	}
	{
		const u8 future[] = { MPSETUP_VERSION + 1, 0, 0 };
		struct mpsetupfile before = g_MpSetupFile;
		s32 result = v006WriteBytes(binary_path, future,
			sizeof(future)) == 0 ? mpsetupLoadCurrentFile() : 0;
		v006Report("binary_future_atomic_reject", result != 0
			&& memcmp(&before, &g_MpSetupFile, sizeof(before)) == 0, &passed);
	}
	if (mpsetupSaveCurrentFile() != 0) goto done;
	baseline = v006ReadBytes(binary_path, &baseline_size);
	if (!baseline) goto done;
	g_MpSetupFile.setups[0].bytes[0] ^= 0xff;
	saveAtomicDebugFailNextCommit();
	v006Report("binary_failed_commit_preserves_file",
		mpsetupSaveCurrentFile() != 0
			&& v006FileEquals(binary_path, baseline, baseline_size), &passed);

done:
	free(baseline);
	remove(json_path);
	remove(binary_path);
	g_MpSetup = initial_setup;
	g_MatchConfig = initial_match;
	g_MpSetupFile = initial_setup_file;
	g_MpCurrentSetup = initial_current;
	sysLogPrintf(passed == 6 ? LOG_NOTE : LOG_ERROR,
		"V006.SAVE: passed=%d cases=6 result=%s", passed,
		passed == 6 ? "PASS" : "FAIL");
	return passed == 6 ? 0 : -1;
}
