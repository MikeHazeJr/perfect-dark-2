/**
 * ini_parse.c -- the descriptor INI parser, on its own.
 *
 * This is the loop behind iniParseBuffer (assetcatalog_scanner.c), moved here
 * unchanged in what it accepts so that it has no catalog, no log and no game
 * state, and the unit tests link the parser the game uses instead of a
 * stand-in.
 *
 * What it reads: lines ending in CR, LF or CRLF; blank lines and lines that
 * start with '#' or ';' are skipped; "[name]" opens a section and the first
 * section's name is kept as the type; every "key = value" after the first
 * section header is stored, in order, whatever section it is in. Text before
 * the first section header is ignored.
 *
 * One thing changed (B-1140). A line inside a section that is not a comment,
 * not a section header and has no '=' used to be "skipped" without moving to
 * the next line, so the loop never ended and the game hung on that
 * descriptor. Such a line is now refused: the parse fails with the line
 * number, like a key or a value that is too long.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "assetcatalog_scanner.h"

static s32 iniFail(char *buf, char *err, size_t err_cap, const char *fmt, ...)
{
	if (err && err_cap) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(err, err_cap, fmt, ap);
		va_end(ap);
		err[err_cap - 1] = '\0';
	}
	free(buf);
	return 0;
}

/* Trim in place; returns the first character that is not white space. */
static char *iniTrim(char *str)
{
	char *end;

	while (*str && isspace((u8)*str)) {
		str++;
	}
	if (*str == '\0') {
		return str;
	}
	end = str + strlen(str) - 1;
	while (end > str && isspace((u8)*end)) {
		*end = '\0';
		end--;
	}
	return str;
}

s32 iniParseText(const char *data, u32 len, ini_section_t *out, char *err, size_t err_cap)
{
	char *buf;
	char *line;
	s32 in_section = 0;
	s32 lineno = 0;

	if (err && err_cap) {
		err[0] = '\0';
	}
	if (!data || !out) {
		return iniFail(NULL, err, err_cap, "no text to parse");
	}

	memset(out, 0, sizeof(*out));

	buf = (char *)malloc((size_t)len + 1);
	if (!buf) {
		return iniFail(NULL, err, err_cap, "out of memory");
	}
	memcpy(buf, data, len);
	buf[len] = '\0';

	line = buf;
	while (line && *line) {
		char *next = strpbrk(line, "\r\n");
		char *p;

		lineno++;
		if (next) {
			char eol = *next;
			*next++ = '\0';
			if (eol == '\r' && *next == '\n') {
				next++;
			}
		}

		p = iniTrim(line);

		/* blank lines and comments */
		if (*p == '\0' || *p == '#' || *p == ';') {
			line = next;
			continue;
		}

		/* section header: [type] */
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) {
				*end = '\0';
				if (out->type[0] == '\0') {
					if (strlen(p + 1) >= sizeof(out->type)) {
						return iniFail(buf, err, err_cap,
							"line %d: section name too long", lineno);
					}
					strncpy(out->type, p + 1, sizeof(out->type) - 1);
				}
				in_section = 1;
			}
			line = next;
			continue;
		}

		/* key = value */
		if (in_section) {
			char *eq = strchr(p, '=');
			char *key;
			char *val;

			if (!eq) {
				return iniFail(buf, err, err_cap,
					"line %d is not a comment, a section or key = value", lineno);
			}

			*eq = '\0';
			key = iniTrim(p);
			val = iniTrim(eq + 1);

			if (out->count >= INI_MAX_PAIRS) {
				return iniFail(buf, err, err_cap,
					"line %d: pair limit %d exceeded", lineno, INI_MAX_PAIRS);
			}
			if (strlen(key) >= sizeof(out->pairs[0].key)
					|| strlen(val) >= sizeof(out->pairs[0].value)) {
				return iniFail(buf, err, err_cap,
					"line %d: key or value too long (key '%.48s')", lineno, key);
			}
			strncpy(out->pairs[out->count].key, key,
				sizeof(out->pairs[0].key) - 1);
			strncpy(out->pairs[out->count].value, val,
				sizeof(out->pairs[0].value) - 1);
			out->count++;
		}

		line = next;
	}

	free(buf);
	/* No section at all is "not a descriptor", with no reason to report. */
	return (out->type[0] != '\0') ? 1 : 0;
}
