#include "modasset_json.h"

static s32 modAssetJsonIsWs(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static s32 modAssetJsonParseMagnitude(const char *start, const char *end,
		u64 limit, u64 *out, const char **token_end)
{
	const char *cursor;
	u64 value = 0;

	if (start == NULL || end == NULL || out == NULL || token_end == NULL
			|| start >= end || *start < '0' || *start > '9') {
		return 0;
	}

	/* JSON permits zero, but never a multi-digit integer with a leading zero. */
	if (*start == '0') {
		if (start + 1 < end && start[1] >= '0' && start[1] <= '9') {
			return 0;
		}
		*out = 0;
		*token_end = start + 1;
		return 1;
	}

	cursor = start;
	while (cursor < end && *cursor >= '0' && *cursor <= '9') {
		u32 digit = (u32)(*cursor - '0');

		if (value > (limit - digit) / 10u) {
			return 0;
		}
		value = value * 10u + digit;
		cursor++;
	}

	*out = value;
	*token_end = cursor;
	return 1;
}

s32 modAssetJsonParseS32Token(const char *start, const char *end,
		s32 *out, const char **token_end)
{
	const char *digits;
	const char *parsed_end;
	u64 magnitude;
	s32 negative;
	u64 limit;

	if (start == NULL || end == NULL || out == NULL || token_end == NULL
			|| start >= end) {
		return 0;
	}

	negative = *start == '-';
	digits = negative ? start + 1 : start;
	limit = negative ? 2147483648ull : 2147483647ull;

	if (!modAssetJsonParseMagnitude(digits, end, limit, &magnitude,
			&parsed_end)) {
		return 0;
	}

	if (negative) {
		*out = magnitude == 2147483648ull
			? (-2147483647 - 1) : -(s32)magnitude;
	} else {
		*out = (s32)magnitude;
	}
	*token_end = parsed_end;
	return 1;
}

s32 modAssetJsonParseU32Token(const char *start, const char *end,
		u32 *out, const char **token_end)
{
	u64 value;
	const char *parsed_end;

	if (out == NULL || token_end == NULL
			|| !modAssetJsonParseMagnitude(start, end, 0xffffffffull,
			&value, &parsed_end)) {
		return 0;
	}

	*out = (u32)value;
	*token_end = parsed_end;
	return 1;
}

s32 modAssetJsonParseBoolToken(const char *start, const char *end,
		s32 *out, const char **token_end)
{
	if (start == NULL || end == NULL || out == NULL || token_end == NULL
			|| start > end) {
		return 0;
	}

	if (end - start >= 4 && start[0] == 't' && start[1] == 'r'
			&& start[2] == 'u' && start[3] == 'e') {
		*out = 1;
		*token_end = start + 4;
		return 1;
	}
	if (end - start >= 5 && start[0] == 'f' && start[1] == 'a'
			&& start[2] == 'l' && start[3] == 's'
			&& start[4] == 'e') {
		*out = 0;
		*token_end = start + 5;
		return 1;
	}

	return 0;
}

s32 modAssetJsonTokenHasContainerBoundary(const char *token_end,
		const char *container_end, char closing_delimiter)
{
	const char *cursor = token_end;

	if (cursor == NULL || container_end == NULL || cursor > container_end) {
		return 0;
	}

	while (cursor < container_end && modAssetJsonIsWs(*cursor)) {
		cursor++;
	}

	return cursor == container_end || *cursor == ','
		|| *cursor == closing_delimiter;
}
