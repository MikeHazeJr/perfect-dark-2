#include "mod_sequence_path.h"

#include <string.h>

s32 modSequenceSiblingPath(const char *path, const char *member,
		char *out, size_t out_n)
{
	const char *sep;
	const char *next_sep;
	const char *slash;
	size_t prefix_len;

	if (!path || !member || !out || out_n == 0) {
		return -1;
	}

	sep = strstr(path, "::");
	while (sep && (next_sep = strstr(sep + 2, "::")) != NULL) {
		sep = next_sep;
	}
	if (sep) {
		prefix_len = (size_t)(sep - path) + 2;
		if (prefix_len + strlen(member) >= out_n) {
			return -1;
		}
		memcpy(out, path, prefix_len);
		strcpy(out + prefix_len, member);
		return 0;
	}

	slash = strrchr(path, '/');
	if (!slash) {
		slash = strrchr(path, '\\');
	}
	if (slash) {
		prefix_len = (size_t)(slash - path) + 1;
		if (prefix_len + strlen(member) >= out_n) {
			return -1;
		}
		memcpy(out, path, prefix_len);
		strcpy(out + prefix_len, member);
		return 0;
	}

	if (strlen(member) >= out_n) {
		return -1;
	}
	strcpy(out, member);
	return 0;
}
