/**
 * updater.c -- Game update system implementation (D13).
 *
 * Uses libcurl for HTTPS requests to the GitHub Releases API.
 * Background operations run on SDL threads with mutex-protected shared state.
 *
 * The mini JSON tokenizer (same pattern as savefile.c and modmgr.c) is used
 * to parse GitHub API responses — no external JSON library needed.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <PR/ultratypes.h>
#include <SDL.h>
#include <curl/curl.h>

#include "system.h"
#include "fs.h"
#include "updater.h"
#include "versioninfo.h"
#include "updateversion.h"
#include "sha256.h"
#include "config.h"
#include "platform.h"

/* ========================================================================
 * Config registration (PD_CONSTRUCTOR runs before main so configInit() can
 * apply the loaded value on startup)
 * ======================================================================== */

/* s32 mirror of update_channel_t, registered with the config system */
static s32 s_UpdateChannelCfg = UPDATE_CHANNEL_STABLE;

/* Comma-separated list of folder/file names that the updater never deletes.
 * Persisted in pd.ini under [Update]. Read directly from pd.ini at apply
 * time (before config is formally initialized). pd.ini itself is always
 * protected regardless of this setting. */
#define UPDATER_DEFAULT_PROTECTED "mods,data,extracted,saves"
static char s_ProtectedFoldersCfg[512] = UPDATER_DEFAULT_PROTECTED;

PD_CONSTRUCTOR static void updaterConfigInit(void)
{
	configRegisterInt("Game.UpdateChannel", &s_UpdateChannelCfg,
		UPDATE_CHANNEL_STABLE, UPDATE_CHANNEL_COUNT - 1);
	configRegisterString("Update.ProtectedFolders", s_ProtectedFoldersCfg,
		sizeof(s_ProtectedFoldersCfg));
}

/* ========================================================================
 * Internal state
 * ======================================================================== */

static struct {
	/* Threading */
	SDL_mutex   *mutex;
	SDL_Thread  *thread;
	s32          cancelFlag;

	/* Status */
	updater_status_t status;
	char             errorMsg[256];

	/* Configuration */
	update_channel_t channel;
	pdversion_t      currentVersion;
	char             versionStr[64];
	s32              isServer;

	/* Results */
	updater_release_t releases[UPDATER_MAX_RELEASES];
	s32               releaseCount;
	s32               latestIndex;  /* index of newest release > current, or -1 */

	/* Download progress */
	updater_progress_t progress;
	const updater_release_t *downloadTarget;

	/* Paths */
	char exePath[512];     /* full path to current executable */
	char installDir[512];  /* directory containing the executable */
	char updatePath[512];  /* installDir/pd.update.zip — downloaded update ZIP */
	char oldPath[512];     /* exePath + ".old" — backup of current exe during apply */
	char versionPath[512]; /* updatePath + ".ver" — version sidecar for staged update */
	char stagingDir[512];  /* installDir/pd_update_staging — extraction working dir */

	/* Staged version — persisted across sessions via versionPath sidecar */
	pdversion_t stagedVersion;
	s32         stagedVersionValid;

	/* curl */
	s32 curlInitialized;
} s_Updater;

/* ========================================================================
 * Mini JSON tokenizer (consistent with savefile.c / modmgr.c)
 * ======================================================================== */

typedef enum {
	JTOK_NONE = 0, JTOK_LBRACE, JTOK_RBRACE, JTOK_LBRACKET, JTOK_RBRACKET,
	JTOK_COLON, JTOK_COMMA, JTOK_STRING, JTOK_NUMBER, JTOK_TRUE, JTOK_FALSE,
	JTOK_NULL, JTOK_EOF, JTOK_ERROR,
} jtok_type_t;

typedef struct {
	const char *start;
	s32 len;
	jtok_type_t type;
} jtok_t;

typedef struct {
	const char *pos;
	jtok_t cur;
} jparse_t;

static void jp_skipws(jparse_t *p)
{
	while (*p->pos && (*p->pos == ' ' || *p->pos == '\t' || *p->pos == '\n' || *p->pos == '\r')) {
		p->pos++;
	}
}

static jtok_t jp_next(jparse_t *p)
{
	jtok_t tok = { NULL, 0, JTOK_NONE };
	jp_skipws(p);

	if (!*p->pos) { tok.type = JTOK_EOF; return tok; }

	tok.start = p->pos;

	switch (*p->pos) {
	case '{': tok.type = JTOK_LBRACE; tok.len = 1; p->pos++; break;
	case '}': tok.type = JTOK_RBRACE; tok.len = 1; p->pos++; break;
	case '[': tok.type = JTOK_LBRACKET; tok.len = 1; p->pos++; break;
	case ']': tok.type = JTOK_RBRACKET; tok.len = 1; p->pos++; break;
	case ':': tok.type = JTOK_COLON; tok.len = 1; p->pos++; break;
	case ',': tok.type = JTOK_COMMA; tok.len = 1; p->pos++; break;
	case '"': {
		p->pos++;
		tok.start = p->pos;
		while (*p->pos && *p->pos != '"') {
			if (*p->pos == '\\') p->pos++;
			p->pos++;
		}
		tok.len = (s32)(p->pos - tok.start);
		tok.type = JTOK_STRING;
		if (*p->pos == '"') p->pos++;
		break;
	}
	case 't':
		if (strncmp(p->pos, "true", 4) == 0) {
			tok.type = JTOK_TRUE; tok.len = 4; p->pos += 4;
		} else {
			tok.type = JTOK_ERROR; tok.len = 1; p->pos++;
		}
		break;
	case 'f':
		if (strncmp(p->pos, "false", 5) == 0) {
			tok.type = JTOK_FALSE; tok.len = 5; p->pos += 5;
		} else {
			tok.type = JTOK_ERROR; tok.len = 1; p->pos++;
		}
		break;
	case 'n':
		if (strncmp(p->pos, "null", 4) == 0) {
			tok.type = JTOK_NULL; tok.len = 4; p->pos += 4;
		} else {
			tok.type = JTOK_ERROR; tok.len = 1; p->pos++;
		}
		break;
	default:
		if (*p->pos == '-' || (*p->pos >= '0' && *p->pos <= '9')) {
			tok.type = JTOK_NUMBER;
			if (*p->pos == '-') p->pos++;
			while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
			if (*p->pos == '.') {
				p->pos++;
				while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
			}
			tok.len = (s32)(p->pos - tok.start);
		} else {
			tok.type = JTOK_ERROR; tok.len = 1; p->pos++;
		}
		break;
	}

	p->cur = tok;
	return tok;
}

/* Check if current token is a string matching `key` */
static s32 jp_iskey(const jtok_t *tok, const char *key)
{
	if (tok->type != JTOK_STRING) return 0;
	s32 klen = (s32)strlen(key);
	return (tok->len == klen && strncmp(tok->start, key, klen) == 0);
}

/* Copy string token to buffer (with null termination, respecting bufsize) */
static void jp_copystr(const jtok_t *tok, char *buf, s32 bufsize)
{
	s32 len = tok->len;
	if (len >= bufsize) len = bufsize - 1;
	if (len > 0) memcpy(buf, tok->start, len);
	buf[len] = '\0';
}

/* Copy string token with JSON escape sequence unescaping (\n \r \t \\ \").
 * GitHub release bodies contain literal \r\n sequences that must become
 * real newlines for correct display. */
static void jp_copystr_unescape(const jtok_t *tok, char *buf, s32 bufsize)
{
	const char *src = tok->start;
	s32 srclen = tok->len;
	s32 di = 0;
	for (s32 si = 0; si < srclen && di < bufsize - 1; si++) {
		if (src[si] == '\\' && si + 1 < srclen) {
			si++;
			switch (src[si]) {
			case 'n':  buf[di++] = '\n'; break;
			case 'r':  buf[di++] = '\r'; break;
			case 't':  buf[di++] = '\t'; break;
			case '"':  buf[di++] = '"';  break;
			case '\\': buf[di++] = '\\'; break;
			case '/':  buf[di++] = '/';  break;
			default:   buf[di++] = src[si]; break;
			}
		} else {
			buf[di++] = src[si];
		}
	}
	buf[di] = '\0';
}

/* Skip an entire JSON value (object, array, or primitive) */
static void jp_skipval(jparse_t *p)
{
	jtok_t tok = jp_next(p);
	if (tok.type == JTOK_LBRACE) {
		s32 depth = 1;
		while (depth > 0) {
			tok = jp_next(p);
			if (tok.type == JTOK_LBRACE) depth++;
			else if (tok.type == JTOK_RBRACE) depth--;
			else if (tok.type == JTOK_EOF) return;
		}
	} else if (tok.type == JTOK_LBRACKET) {
		s32 depth = 1;
		while (depth > 0) {
			tok = jp_next(p);
			if (tok.type == JTOK_LBRACKET) depth++;
			else if (tok.type == JTOK_RBRACKET) depth--;
			else if (tok.type == JTOK_EOF) return;
		}
	}
	/* primitives (string, number, bool, null) already consumed by jp_next */
}

/* ========================================================================
 * curl helpers
 * ======================================================================== */

typedef struct {
	char *data;
	size_t size;
	size_t capacity;
} curl_buffer_t;

static size_t curlWriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	curl_buffer_t *buf = (curl_buffer_t *)userp;
	size_t total = size * nmemb;

	if (buf->size + total >= buf->capacity) {
		size_t newcap = (buf->capacity == 0) ? 4096 : buf->capacity;
		while (newcap <= buf->size + total) newcap *= 2;
		char *newdata = (char *)realloc(buf->data, newcap);
		if (!newdata) return 0;
		buf->data = newdata;
		buf->capacity = newcap;
	}

	memcpy(buf->data + buf->size, contents, total);
	buf->size += total;
	buf->data[buf->size] = '\0';
	return total;
}

/* File download write callback */
typedef struct {
	FILE *fp;
	s64   written;
	s32  *cancelFlag;
} curl_file_ctx_t;

static size_t curlFileWriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	curl_file_ctx_t *ctx = (curl_file_ctx_t *)userp;
	if (ctx->cancelFlag && *ctx->cancelFlag) return 0; /* abort */
	size_t total = size * nmemb;
	size_t written = fwrite(contents, 1, total, ctx->fp);
	ctx->written += (s64)written;

	/* Update progress atomically */
	SDL_LockMutex(s_Updater.mutex);
	s_Updater.progress.bytesDownloaded = ctx->written;
	if (s_Updater.progress.bytesTotal > 0) {
		s_Updater.progress.percent = (f32)ctx->written / (f32)s_Updater.progress.bytesTotal * 100.0f;
	}
	SDL_UnlockMutex(s_Updater.mutex);

	return written;
}

/* Download progress callback for curl (to get total size) */
static int curlProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	(void)ultotal; (void)ulnow; (void)clientp; (void)dlnow;

	SDL_LockMutex(s_Updater.mutex);
	if (dltotal > 0) {
		s_Updater.progress.bytesTotal = (s64)dltotal;
	}
	if (s_Updater.cancelFlag) {
		SDL_UnlockMutex(s_Updater.mutex);
		return 1; /* abort */
	}
	SDL_UnlockMutex(s_Updater.mutex);
	return 0;
}

/* Perform a GET request, return response body in a buffer.
 * Caller must free buf->data. Returns curl result code. */
static CURLcode curlGet(const char *url, curl_buffer_t *buf)
{
	CURL *curl = curl_easy_init();
	if (!curl) return CURLE_FAILED_INIT;

	memset(buf, 0, sizeof(*buf));

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "PerfectDark-Updater/" VERSION_STRING);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
	/* Use native Windows Certificate Store as CA source (curl >= 7.71.0) */
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

	/* GitHub API requires Accept header */
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	CURLcode res = curl_easy_perform(curl);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	return res;
}

/* ========================================================================
 * Version parsing helpers
 * ======================================================================== */

s32 versionParse(const char *str, pdversion_t *out)
{
	if (!str || !out) return -1;

	memset(out, 0, sizeof(*out));

	/* Skip optional 'v' prefix */
	if (*str == 'v') str++;

	if (sscanf(str, "%d.%d.%d", &out->major, &out->minor, &out->patch) < 3) {
		return -1;
	}

	return 0;
}

s32 versionFormat(const pdversion_t *ver, char *buf, s32 bufsize)
{
	return snprintf(buf, bufsize, "%d.%d.%d",
		ver->major, ver->minor, ver->patch);
}

s32 versionParseTag(const char *tag, char *prefixbuf, s32 prefixbufsize, pdversion_t *ver)
{
	if (!tag || !ver) return -1;

	/* Unified tag format: "v0.1.1" (starts with 'v', no prefix)
	 * Legacy format: "client-v0.1.1" or "server-v0.1.1" (prefix before "-v")
	 * Handle both. */

	if (tag[0] == 'v') {
		/* Unified format: no prefix, version starts at position 0 */
		if (prefixbuf && prefixbufsize > 0) {
			prefixbuf[0] = '\0';
		}
		return versionParse(tag, ver); /* versionParse skips the 'v' */
	}

	/* Legacy format: find the "-v" separator */
	const char *vp = strstr(tag, "-v");
	if (!vp) return -1;

	if (prefixbuf) {
		s32 plen = (s32)(vp - tag);
		if (plen >= prefixbufsize) plen = prefixbufsize - 1;
		memcpy(prefixbuf, tag, plen);
		prefixbuf[plen] = '\0';
	}

	return versionParse(vp + 1, ver);
}

/* ========================================================================
 * GitHub API response parsing
 * ======================================================================== */

static const char *getTagPrefix(void)
{
	return s_Updater.isServer ? UPDATER_TAG_SERVER : UPDATER_TAG_CLIENT;
}

static s32 str_ends_with(const char *str, const char *suffix)
{
	size_t sl = strlen(str), su = strlen(suffix);
	return (sl >= su) && (strcmp(str + sl - su, suffix) == 0);
}

/**
 * Parse one release object from the JSON array.
 * Parser should be positioned just after the opening '{'.
 */
static s32 parseRelease(jparse_t *p, updater_release_t *rel)
{
	memset(rel, 0, sizeof(*rel));

	char tagStr[UPDATER_MAX_TAG_LEN] = {0};
	const char *prefix = getTagPrefix();
	s32 depth = 1;

	while (depth > 0) {
		jtok_t key = jp_next(p);
		if (key.type == JTOK_RBRACE) { depth--; continue; }
		if (key.type == JTOK_LBRACE) { depth++; continue; }
		if (key.type == JTOK_EOF) return -1;
		if (key.type == JTOK_COMMA) continue;

		if (key.type != JTOK_STRING) {
			jp_skipval(p);
			continue;
		}

		/* Expect colon */
		jtok_t colon = jp_next(p);
		if (colon.type != JTOK_COLON) continue;

		if (jp_iskey(&key, "tag_name")) {
			jtok_t val = jp_next(p);
			if (val.type == JTOK_STRING) {
				jp_copystr(&val, tagStr, sizeof(tagStr));
				jp_copystr(&val, rel->tag, sizeof(rel->tag));
			}
		} else if (jp_iskey(&key, "name")) {
			jtok_t val = jp_next(p);
			if (val.type == JTOK_STRING) {
				jp_copystr(&val, rel->name, sizeof(rel->name));
			}
		} else if (jp_iskey(&key, "body")) {
			jtok_t val = jp_next(p);
			if (val.type == JTOK_STRING) {
				jp_copystr_unescape(&val, rel->body, sizeof(rel->body));
			}
		} else if (jp_iskey(&key, "prerelease")) {
			jtok_t val = jp_next(p);
			rel->isPrerelease = (val.type == JTOK_TRUE) ? 1 : 0;
		} else if (jp_iskey(&key, "draft")) {
			jtok_t val = jp_next(p);
			rel->isDraft = (val.type == JTOK_TRUE) ? 1 : 0;
		} else if (jp_iskey(&key, "assets")) {
			/* Parse assets array looking for our binary + hash */
			jtok_t arr = jp_next(p);
			if (arr.type == JTOK_LBRACKET) {
				while (1) {
					jtok_t t = jp_next(p);
					if (t.type == JTOK_RBRACKET) break;
					if (t.type == JTOK_EOF) return -1;
					if (t.type == JTOK_COMMA) continue;

					if (t.type == JTOK_LBRACE) {
						/* Parse asset object */
						char aname[128] = {0};
						char aurl[UPDATER_MAX_URL_LEN] = {0};
						s64 asize = 0;

						while (1) {
							jtok_t ak = jp_next(p);
							if (ak.type == JTOK_RBRACE) break;
							if (ak.type == JTOK_EOF) return -1;
							if (ak.type == JTOK_COMMA) continue;

							if (ak.type != JTOK_STRING) continue;
							jtok_t ac = jp_next(p);
							if (ac.type != JTOK_COLON) continue;

							if (jp_iskey(&ak, "name")) {
								jtok_t av = jp_next(p);
								if (av.type == JTOK_STRING) {
									jp_copystr(&av, aname, sizeof(aname));
								}
							} else if (jp_iskey(&ak, "browser_download_url")) {
								jtok_t av = jp_next(p);
								if (av.type == JTOK_STRING) {
									jp_copystr(&av, aurl, sizeof(aurl));
								}
							} else if (jp_iskey(&ak, "size")) {
								jtok_t av = jp_next(p);
								if (av.type == JTOK_NUMBER) {
									asize = strtoll(av.start, NULL, 10);
								}
							} else {
								jp_skipval(p);
							}
						}

						/* Match the win64 ZIP asset (e.g. PerfectDark-v0.0.19-win64.zip) */
						if (str_ends_with(aname, UPDATER_ASSET_ZIP_SUFFIX)) {
							strncpy(rel->assetUrl, aurl, UPDATER_MAX_URL_LEN - 1);
							rel->assetUrl[UPDATER_MAX_URL_LEN - 1] = '\0';
							rel->assetSize = asize;
						} else {
							/* Check for .sha256 sidecar of the ZIP */
							char zipSha[16];
							snprintf(zipSha, sizeof(zipSha), "%s.sha256", UPDATER_ASSET_ZIP_SUFFIX);
							if (str_ends_with(aname, zipSha)) {
								strncpy(rel->hashUrl, aurl, UPDATER_MAX_URL_LEN - 1);
								rel->hashUrl[UPDATER_MAX_URL_LEN - 1] = '\0';
							}
						}
					}
				}
			}
		} else {
			jp_skipval(p);
		}
	}

	/* Parse version from tag, filtering by our prefix */
	if (strncmp(tagStr, prefix, strlen(prefix)) != 0) {
		sysLogPrintf(LOG_NOTE, "UPDATER: skipping release tag '%s' (expected prefix '%s')", tagStr, prefix);
		return -1;  /* wrong prefix — skip this release */
	}

	char prefixbuf[32];
	if (versionParseTag(tagStr, prefixbuf, sizeof(prefixbuf), &rel->version) != 0) {
		sysLogPrintf(LOG_WARNING, "UPDATER: failed to parse version from tag '%s'", tagStr);
		return -1;
	}

	/* Fallback: if no ZIP URL was found in the assets array, construct the
	 * conventional GitHub download URL from the tag.
	 * e.g. tag "v0.0.19" → "PerfectDark-v0.0.19-win64.zip" */
	if (!rel->assetUrl[0] && rel->tag[0]) {
		snprintf(rel->assetUrl, UPDATER_MAX_URL_LEN - 1,
			"https://github.com/%s/%s/releases/download/%s/PerfectDark-%s%s",
			UPDATER_GITHUB_OWNER, UPDATER_GITHUB_REPO, rel->tag,
			rel->tag, UPDATER_ASSET_ZIP_SUFFIX);
		rel->assetUrl[UPDATER_MAX_URL_LEN - 1] = '\0';
	}

	return 0;
}

/**
 * Parse the full GitHub releases API JSON response.
 * Populates s_Updater.releases[] and s_Updater.releaseCount.
 */
static s32 parseReleasesJson(const char *json)
{
	jparse_t p;
	p.pos = json;
	memset(&p.cur, 0, sizeof(p.cur));

	s32 count = 0;

	/* Expect top-level array */
	jtok_t arr = jp_next(&p);
	if (arr.type != JTOK_LBRACKET) {
		return -1;
	}

	while (count < UPDATER_MAX_RELEASES) {
		jtok_t t = jp_next(&p);
		if (t.type == JTOK_RBRACKET) break;
		if (t.type == JTOK_EOF) break;
		if (t.type == JTOK_COMMA) continue;

		if (t.type == JTOK_LBRACE) {
			updater_release_t rel;
			if (parseRelease(&p, &rel) == 0 && !rel.isDraft) {
				/* Channel filter: if on stable, skip prereleases */
				if (s_Updater.channel == UPDATE_CHANNEL_STABLE && rel.isPrerelease) {
					continue;
				}
				s_Updater.releases[count] = rel;
				count++;
			}
		}
	}

	sysLogPrintf(LOG_NOTE, "UPDATER: parsed %d valid release(s) from API response", count);

	/* Sort by version descending (newest first) — simple insertion sort */
	for (s32 i = 1; i < count; i++) {
		updater_release_t tmp = s_Updater.releases[i];
		s32 j = i - 1;
		while (j >= 0 && versionCompare(&s_Updater.releases[j].version, &tmp.version) < 0) {
			s_Updater.releases[j + 1] = s_Updater.releases[j];
			j--;
		}
		s_Updater.releases[j + 1] = tmp;
	}

	return count;
}

/* ========================================================================
 * Background thread: update check
 * ======================================================================== */

static int SDLCALL checkThread(void *data)
{
	(void)data;

	char url[256];
	snprintf(url, sizeof(url),
		"https://api.github.com/repos/%s/%s/releases?per_page=30",
		UPDATER_GITHUB_OWNER, UPDATER_GITHUB_REPO);

	curl_buffer_t buf;
	CURLcode res = curlGet(url, &buf);

	SDL_LockMutex(s_Updater.mutex);

	if (res != CURLE_OK) {
		snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg),
			"Update check failed: %s", curl_easy_strerror(res));
		s_Updater.status = UPDATER_CHECK_FAILED;
		sysLogPrintf(LOG_WARNING, "UPDATER: %s", s_Updater.errorMsg);
	} else if (!buf.data || buf.size == 0) {
		snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg),
			"Update check failed: empty response");
		s_Updater.status = UPDATER_CHECK_FAILED;
	} else {
		s32 count = parseReleasesJson(buf.data);
		if (count < 0) {
			snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg),
				"Update check failed: could not parse response");
			s_Updater.status = UPDATER_CHECK_FAILED;
		} else {
			s_Updater.releaseCount = count;
			s_Updater.latestIndex = -1;

			/* Find newest release that is newer than current */
			for (s32 i = 0; i < count; i++) {
				if (versionCompare(&s_Updater.releases[i].version, &s_Updater.currentVersion) > 0) {
					s_Updater.latestIndex = i;
					break;  /* already sorted newest first */
				}
			}

			s_Updater.status = UPDATER_CHECK_DONE;

			if (s_Updater.latestIndex >= 0) {
				char verstr[32];
				versionFormat(&s_Updater.releases[s_Updater.latestIndex].version, verstr, sizeof(verstr));
				sysLogPrintf(LOG_NOTE, "UPDATER: Update available: v%s", verstr);
			} else {
				sysLogPrintf(LOG_NOTE, "UPDATER: Up to date (v%s)", s_Updater.versionStr);
			}
		}
	}

	SDL_UnlockMutex(s_Updater.mutex);

	if (buf.data) free(buf.data);
	return 0;
}

/* ========================================================================
 * Background thread: download
 * ======================================================================== */

static void writeStagedVersionFile(const pdversion_t *ver);

static int SDLCALL downloadThread(void *data)
{
	const updater_release_t *rel = (const updater_release_t *)data;

	sysLogPrintf(LOG_NOTE, "UPDATER: Downloading ZIP for %s (%lld bytes)",
		rel->tag, (long long)rel->assetSize);

	/* Download the .sha256 sidecar first (if available) */
	char expectedHash[SHA256_HEX_SIZE] = {0};
	s32 hasHash = 0;

	if (rel->hashUrl[0]) {
		curl_buffer_t hashBuf;
		CURLcode hres = curlGet(rel->hashUrl, &hashBuf);
		if (hres == CURLE_OK && hashBuf.data) {
			/* SHA256 file format: "hexhash  filename\n" or just "hexhash\n" */
			s32 i;
			for (i = 0; i < 64 && hashBuf.data[i] && !isspace((u8)hashBuf.data[i]); i++) {
				expectedHash[i] = hashBuf.data[i];
			}
			expectedHash[i] = '\0';
			if (i == 64) hasHash = 1;
			free(hashBuf.data);
		}
	}

	/* Download the binary */
	FILE *fp = fopen(s_Updater.updatePath, "wb");
	if (!fp) {
		SDL_LockMutex(s_Updater.mutex);
		snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg),
			"Cannot write update file: %s", s_Updater.updatePath);
		s_Updater.status = UPDATER_DOWNLOAD_FAILED;
		SDL_UnlockMutex(s_Updater.mutex);
		return 1;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		fclose(fp);
		remove(s_Updater.updatePath);
		SDL_LockMutex(s_Updater.mutex);
		snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg), "curl init failed");
		s_Updater.status = UPDATER_DOWNLOAD_FAILED;
		SDL_UnlockMutex(s_Updater.mutex);
		return 1;
	}

	curl_file_ctx_t fctx;
	fctx.fp = fp;
	fctx.written = 0;
	fctx.cancelFlag = &s_Updater.cancelFlag;

	curl_easy_setopt(curl, CURLOPT_URL, rel->assetUrl);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlFileWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fctx);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "PerfectDark-Updater/" VERSION_STRING);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);  /* 10 min max for large files */
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif

	/* GitHub releases redirect to S3 — need Accept: application/octet-stream */
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Accept: application/octet-stream");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	/* Progress callback to get total size */
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, NULL);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

	CURLcode res = curl_easy_perform(curl);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	fclose(fp);

	SDL_LockMutex(s_Updater.mutex);

	if (s_Updater.cancelFlag) {
		remove(s_Updater.updatePath);
		snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg), "Download cancelled");
		s_Updater.status = UPDATER_IDLE;
		SDL_UnlockMutex(s_Updater.mutex);
		return 0;
	}

	if (res != CURLE_OK) {
		remove(s_Updater.updatePath);
		snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg),
			"Download failed: %s", curl_easy_strerror(res));
		s_Updater.status = UPDATER_DOWNLOAD_FAILED;
		SDL_UnlockMutex(s_Updater.mutex);
		return 1;
	}

	/* Sanity check: verify the downloaded file starts with the ZIP magic bytes
	 * ("PK\x03\x04" = 0x50 0x4B 0x03 0x04). If the download was redirected to
	 * a GitHub 404 page or any other non-binary response, this catches it. */
	{
		FILE *zipcheck = fopen(s_Updater.updatePath, "rb");
		unsigned char magic[4] = {0, 0, 0, 0};
		if (zipcheck) {
			(void)fread(magic, 1, 4, zipcheck);
			fclose(zipcheck);
		}
		if (magic[0] != 0x50 || magic[1] != 0x4B || magic[2] != 0x03 || magic[3] != 0x04) {
			remove(s_Updater.updatePath);
			/* Mutex already held from the top of this function — do NOT re-lock. */
			snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg),
				"Downloaded file is not a valid ZIP archive (bad PK signature) — "
				"release asset may be missing from GitHub. Update the game manually.");
			s_Updater.status = UPDATER_DOWNLOAD_FAILED;
			sysLogPrintf(LOG_ERROR, "UPDATER: ZIP signature check failed for %s "
				"(first bytes: 0x%02X 0x%02X 0x%02X 0x%02X) — likely a 404 page or wrong asset",
				rel->tag, magic[0], magic[1], magic[2], magic[3]);
			SDL_UnlockMutex(s_Updater.mutex);
			return 1;
		}
	}

	/* SHA-256 verification */
	if (hasHash) {
		SDL_UnlockMutex(s_Updater.mutex);  /* hash check can take a moment */

		s32 verified = sha256VerifyFile(s_Updater.updatePath, expectedHash);

		SDL_LockMutex(s_Updater.mutex);

		if (verified != 1) {
			remove(s_Updater.updatePath);
			snprintf(s_Updater.errorMsg, sizeof(s_Updater.errorMsg),
				"SHA-256 verification failed — download may be corrupted");
			s_Updater.status = UPDATER_DOWNLOAD_FAILED;
			sysLogPrintf(LOG_ERROR, "UPDATER: SHA-256 mismatch for %s", rel->tag);
			SDL_UnlockMutex(s_Updater.mutex);
			return 1;
		}

		sysLogPrintf(LOG_NOTE, "UPDATER: SHA-256 verified OK");
	} else {
		sysLogPrintf(LOG_WARNING, "UPDATER: No SHA-256 sidecar — download not verified");
	}

	/* Write version sidecar so the staged version survives across sessions.
	 * Do the file I/O outside the mutex to avoid blocking the main thread. */
	SDL_UnlockMutex(s_Updater.mutex);
	writeStagedVersionFile(&rel->version);
	SDL_LockMutex(s_Updater.mutex);

	s_Updater.stagedVersion = rel->version;
	s_Updater.stagedVersionValid = 1;
	s_Updater.progress.percent = 100.0f;
	s_Updater.status = UPDATER_DOWNLOAD_DONE;

	char verstr[32];
	versionFormat(&rel->version, verstr, sizeof(verstr));
	sysLogPrintf(LOG_NOTE, "UPDATER: Downloaded ZIP v%s to: %s — restart to apply",
		verstr, s_Updater.updatePath);

	SDL_UnlockMutex(s_Updater.mutex);
	return 0;
}

/* ========================================================================
 * Executable path detection
 * ======================================================================== */

/* ========================================================================
 * Version sidecar helpers — track which version is staged on disk
 * ======================================================================== */

/**
 * Write the version of the staged update to a small sidecar file.
 * Called after a successful download so the staged version survives restarts.
 */
static void writeStagedVersionFile(const pdversion_t *ver)
{
	char verstr[32];
	versionFormat(ver, verstr, sizeof(verstr));
	FILE *fp = fopen(s_Updater.versionPath, "w");
	if (fp) {
		fprintf(fp, "%s\n", verstr);
		fclose(fp);
	}
}

/**
 * Read the staged version sidecar. Returns 1 on success, 0 if absent or invalid.
 */
static s32 readStagedVersionFile(pdversion_t *out)
{
	FILE *fp = fopen(s_Updater.versionPath, "r");
	if (!fp) return 0;
	char buf[32];
	if (!fgets(buf, sizeof(buf), fp)) { fclose(fp); return 0; }
	fclose(fp);
	/* Strip newline */
	for (s32 i = 0; buf[i]; i++) {
		if (buf[i] == '\n' || buf[i] == '\r') { buf[i] = '\0'; break; }
	}
	return (versionParse(buf, out) == 0) ? 1 : 0;
}

#ifdef _WIN32
#include <windows.h>
static void detectExePath(void)
{
	GetModuleFileNameA(NULL, s_Updater.exePath, sizeof(s_Updater.exePath));

	/* installDir = directory containing the exe */
	strncpy(s_Updater.installDir, s_Updater.exePath, sizeof(s_Updater.installDir) - 1);
	s_Updater.installDir[sizeof(s_Updater.installDir) - 1] = '\0';
	char *lastSep = strrchr(s_Updater.installDir, '\\');
	if (!lastSep) lastSep = strrchr(s_Updater.installDir, '/');
	if (lastSep) *lastSep = '\0';

	snprintf(s_Updater.updatePath,  sizeof(s_Updater.updatePath),  "%s\\pd.update.zip",        s_Updater.installDir);
	snprintf(s_Updater.oldPath,     sizeof(s_Updater.oldPath),     "%s%s",                     s_Updater.exePath, UPDATER_SUFFIX_OLD);
	snprintf(s_Updater.versionPath, sizeof(s_Updater.versionPath), "%s.ver",                   s_Updater.updatePath);
	snprintf(s_Updater.stagingDir,  sizeof(s_Updater.stagingDir),  "%s\\pd_update_staging",    s_Updater.installDir);
}
#else
#include <unistd.h>
#include <limits.h>
static void detectExePath(void)
{
	ssize_t len = readlink("/proc/self/exe", s_Updater.exePath, sizeof(s_Updater.exePath) - 1);
	if (len > 0) {
		s_Updater.exePath[len] = '\0';
	} else {
		strncpy(s_Updater.exePath, "PerfectDark", sizeof(s_Updater.exePath));
	}
	strncpy(s_Updater.installDir, s_Updater.exePath, sizeof(s_Updater.installDir) - 1);
	s_Updater.installDir[sizeof(s_Updater.installDir) - 1] = '\0';
	char *lastSep = strrchr(s_Updater.installDir, '/');
	if (lastSep) *lastSep = '\0';
	snprintf(s_Updater.updatePath,  sizeof(s_Updater.updatePath),  "%s/pd.update.zip",      s_Updater.installDir);
	snprintf(s_Updater.oldPath,     sizeof(s_Updater.oldPath),     "%s%s",                  s_Updater.exePath, UPDATER_SUFFIX_OLD);
	snprintf(s_Updater.versionPath, sizeof(s_Updater.versionPath), "%s.ver",                s_Updater.updatePath);
	snprintf(s_Updater.stagingDir,  sizeof(s_Updater.stagingDir),  "%s/pd_update_staging",  s_Updater.installDir);
}
#endif

/* ========================================================================
 * Public API — Lifecycle
 * ======================================================================== */

void updaterInit(void)
{
	memset(&s_Updater, 0, sizeof(s_Updater));

	s_Updater.mutex = SDL_CreateMutex();
	s_Updater.currentVersion = (pdversion_t)BUILD_VERSION_INIT;
	versionFormat(&s_Updater.currentVersion, s_Updater.versionStr, sizeof(s_Updater.versionStr));
	/* Apply channel from config (loaded before updaterInit via PD_CONSTRUCTOR) */
	if (s_UpdateChannelCfg >= 0 && s_UpdateChannelCfg < UPDATE_CHANNEL_COUNT) {
		s_Updater.channel = (update_channel_t)s_UpdateChannelCfg;
	} else {
		s_Updater.channel = UPDATE_CHANNEL_STABLE;
	}
	s_Updater.latestIndex = -1;

#ifdef PD_SERVER
	s_Updater.isServer = 1;
#else
	s_Updater.isServer = 0;
#endif

	detectExePath();

	/* Restore staged version if a .update file already exists on disk.
	 * This covers the case where the user downloaded an update in a
	 * previous session but hasn't restarted yet. */
	s_Updater.stagedVersionValid = 0;
	{
		FILE *updateTest = fopen(s_Updater.updatePath, "rb");
		if (updateTest) {
			fclose(updateTest);
			if (readStagedVersionFile(&s_Updater.stagedVersion)) {
				s_Updater.stagedVersionValid = 1;
				char verstr[32];
				versionFormat(&s_Updater.stagedVersion, verstr, sizeof(verstr));
				sysLogPrintf(LOG_NOTE, "UPDATER: Found staged update on disk: v%s", verstr);
			}
		}
	}

	/* Initialize curl globally (once) */
	if (!s_Updater.curlInitialized) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		s_Updater.curlInitialized = 1;
	}

	sysLogPrintf(LOG_NOTE, "UPDATER: Initialized — v%s (%s, %s channel)",
		s_Updater.versionStr,
		s_Updater.isServer ? "server" : "client",
		s_Updater.channel == UPDATE_CHANNEL_DEV ? "dev" : "stable");
}

void updaterShutdown(void)
{
	/* Signal cancel and wait for thread */
	if (s_Updater.thread) {
		SDL_LockMutex(s_Updater.mutex);
		s_Updater.cancelFlag = 1;
		SDL_UnlockMutex(s_Updater.mutex);
		SDL_WaitThread(s_Updater.thread, NULL);
		s_Updater.thread = NULL;
	}

	if (s_Updater.mutex) {
		SDL_DestroyMutex(s_Updater.mutex);
		s_Updater.mutex = NULL;
	}

	if (s_Updater.curlInitialized) {
		curl_global_cleanup();
		s_Updater.curlInitialized = 0;
	}
}

void updaterTick(void)
{
	/* Currently a no-op — status is read directly via getters.
	 * Reserved for future use (e.g., firing callbacks, auto-check scheduling). */
}

/* ========================================================================
 * Public API — Checking
 * ======================================================================== */

void updaterCheckAsync(void)
{
	SDL_LockMutex(s_Updater.mutex);

	if (s_Updater.status == UPDATER_CHECKING || s_Updater.status == UPDATER_DOWNLOADING) {
		SDL_UnlockMutex(s_Updater.mutex);
		return;
	}

	s_Updater.status = UPDATER_CHECKING;
	s_Updater.cancelFlag = 0;
	s_Updater.releaseCount = 0;
	s_Updater.latestIndex = -1;
	s_Updater.errorMsg[0] = '\0';

	SDL_UnlockMutex(s_Updater.mutex);

	/* Wait for any previous thread */
	if (s_Updater.thread) {
		SDL_WaitThread(s_Updater.thread, NULL);
	}

	s_Updater.thread = SDL_CreateThread(checkThread, "updater_check", NULL);
}

updater_status_t updaterGetStatus(void)
{
	SDL_LockMutex(s_Updater.mutex);
	updater_status_t st = s_Updater.status;
	SDL_UnlockMutex(s_Updater.mutex);
	return st;
}

s32 updaterGetReleaseCount(void)
{
	SDL_LockMutex(s_Updater.mutex);
	s32 n = s_Updater.releaseCount;
	SDL_UnlockMutex(s_Updater.mutex);
	return n;
}

const updater_release_t *updaterGetRelease(s32 index)
{
	SDL_LockMutex(s_Updater.mutex);
	const updater_release_t *r = NULL;
	if (index >= 0 && index < s_Updater.releaseCount) {
		r = &s_Updater.releases[index];
	}
	SDL_UnlockMutex(s_Updater.mutex);
	return r;
}

const updater_release_t *updaterGetLatest(void)
{
	SDL_LockMutex(s_Updater.mutex);
	const updater_release_t *r = NULL;
	if (s_Updater.latestIndex >= 0) {
		r = &s_Updater.releases[s_Updater.latestIndex];
	}
	SDL_UnlockMutex(s_Updater.mutex);
	return r;
}

s32 updaterIsUpdateAvailable(void)
{
	SDL_LockMutex(s_Updater.mutex);
	s32 avail = (s_Updater.latestIndex >= 0) ? 1 : 0;
	SDL_UnlockMutex(s_Updater.mutex);
	return avail;
}

/* ========================================================================
 * Public API — Downloading
 * ======================================================================== */

void updaterDownloadAsync(const updater_release_t *release)
{
	if (!release || !release->assetUrl[0]) return;

	SDL_LockMutex(s_Updater.mutex);

	if (s_Updater.status == UPDATER_CHECKING || s_Updater.status == UPDATER_DOWNLOADING) {
		SDL_UnlockMutex(s_Updater.mutex);
		return;
	}

	s_Updater.status = UPDATER_DOWNLOADING;
	s_Updater.cancelFlag = 0;
	s_Updater.errorMsg[0] = '\0';
	s_Updater.downloadTarget = release;
	memset(&s_Updater.progress, 0, sizeof(s_Updater.progress));
	s_Updater.progress.bytesTotal = release->assetSize;

	SDL_UnlockMutex(s_Updater.mutex);

	/* Wait for any previous thread */
	if (s_Updater.thread) {
		SDL_WaitThread(s_Updater.thread, NULL);
	}

	s_Updater.thread = SDL_CreateThread(downloadThread, "updater_dl", (void *)release);
}

void updaterDownloadCancel(void)
{
	SDL_LockMutex(s_Updater.mutex);
	s_Updater.cancelFlag = 1;
	SDL_UnlockMutex(s_Updater.mutex);
}

updater_progress_t updaterGetProgress(void)
{
	SDL_LockMutex(s_Updater.mutex);
	updater_progress_t p = s_Updater.progress;
	SDL_UnlockMutex(s_Updater.mutex);
	return p;
}

const char *updaterGetError(void)
{
	return s_Updater.errorMsg;
}

const pdversion_t *updaterGetStagedVersion(void)
{
	SDL_LockMutex(s_Updater.mutex);
	const pdversion_t *r = s_Updater.stagedVersionValid ? &s_Updater.stagedVersion : NULL;
	SDL_UnlockMutex(s_Updater.mutex);
	return r;
}

/* ========================================================================
 * ZIP extraction and install helpers (Windows)
 * ======================================================================== */

#ifdef _WIN32

/* Dynamic protected path list — built from pd.ini at apply time.
 * pd.ini itself is always protected and added last. */
static char  s_ProtectedParseBuf[512 + 8]; /* +8 for "pd.ini\0" */
static char *s_ProtectedList[64];
static s32   s_ProtectedCount = 0;

/* Scan pd.ini (without the config system) for Update.ProtectedFolders.
 * Returns 1 and fills out[] on success; returns 0 and leaves out unchanged
 * if the key is absent or the file can't be opened. */
static s32 readProtectedFoldersFromIni(const char *iniPath, char *out, size_t outsize)
{
	FILE *fp = fopen(iniPath, "r");
	if (!fp) return 0;
	char line[512];
	s32 inUpdate = 0;
	while (fgets(line, sizeof(line), fp)) {
		size_t l = strlen(line);
		while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
		if (line[0] == '[') { inUpdate = (strcmp(line, "[Update]") == 0); continue; }
		if (inUpdate && strncmp(line, "ProtectedFolders=", 17) == 0) {
			strncpy(out, line + 17, outsize - 1);
			out[outsize - 1] = '\0';
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	return 0;
}

/* Parse a comma-separated list of folder/file names into s_ProtectedList[].
 * pd.ini is always appended to the list regardless of the csv contents. */
static void buildProtectedList(const char *csv)
{
	strncpy(s_ProtectedParseBuf, csv, sizeof(s_ProtectedParseBuf) - 8);
	s_ProtectedParseBuf[sizeof(s_ProtectedParseBuf) - 8] = '\0';
	s_ProtectedCount = 0;
	char *p = s_ProtectedParseBuf;
	while (*p && s_ProtectedCount < 62) {
		while (*p == ' ' || *p == '\t') p++;
		if (!*p) break;
		s_ProtectedList[s_ProtectedCount++] = p;
		char *comma = strchr(p, ',');
		if (!comma) { p += strlen(p); break; }
		*comma = '\0';
		p = comma + 1;
		/* Trim trailing whitespace from the entry we just ended */
		char *end = s_ProtectedList[s_ProtectedCount - 1];
		size_t elen = strlen(end);
		while (elen > 0 && (end[elen-1] == ' ' || end[elen-1] == '\t')) end[--elen] = '\0';
	}
	/* pd.ini is always protected regardless of the setting */
	s_ProtectedList[s_ProtectedCount++] = "pd.ini";
}

/* Return 1 if relPath is under a protected directory/file. */
static s32 isProtectedRelPath(const char *relPath)
{
	char norm[512];
	s32 i;
	for (i = 0; relPath[i] && i < (s32)(sizeof(norm) - 1); i++) {
		norm[i] = (relPath[i] == '\\') ? '/' : (char)tolower((unsigned char)relPath[i]);
	}
	norm[i] = '\0';
	for (s32 j = 0; j < s_ProtectedCount; j++) {
		const char *prot = s_ProtectedList[j];
		size_t plen = strlen(prot);
		if (strncmp(norm, prot, plen) == 0 && (norm[plen] == '\0' || norm[plen] == '/')) {
			return 1;
		}
	}
	return 0;
}

/* Extract ZIP to stagingDir using PowerShell Expand-Archive. Returns 0 on success. */
static s32 extractZipToStaging(const char *zipPath, const char *stagingDir)
{
	CreateDirectoryA(stagingDir, NULL);

	char cmd[2048];
	snprintf(cmd, sizeof(cmd),
		"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass "
		"-Command \"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
		zipPath, stagingDir);

	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);

	if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
			CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
		fprintf(stderr, "UPDATER: Failed to launch PowerShell for extraction (error %lu)\n",
			GetLastError());
		return -1;
	}

	WaitForSingleObject(pi.hProcess, 300000); /* 5-minute timeout */
	DWORD exitCode = 1;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return (exitCode == 0) ? 0 : -1;
}

/* Recursively copy all files from srcDir into dstDir (overwrite existing). */
static void copyDirRecursive(const char *srcDir, const char *dstDir)
{
	char pattern[MAX_PATH];
	snprintf(pattern, sizeof(pattern), "%s\\*", srcDir);

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return;

	do {
		if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
		char src[MAX_PATH], dst[MAX_PATH];
		snprintf(src, sizeof(src), "%s\\%s", srcDir, fd.cFileName);
		snprintf(dst, sizeof(dst), "%s\\%s", dstDir, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			CreateDirectoryA(dst, NULL);
			copyDirRecursive(src, dst);
		} else {
			if (!CopyFileA(src, dst, FALSE)) {
				fprintf(stderr, "UPDATER: Warning: could not copy %s (error %lu)\n",
					fd.cFileName, GetLastError());
			}
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

/* Recursively delete a directory and its contents. */
static void removeDirRecursive(const char *dir)
{
	char pattern[MAX_PATH];
	snprintf(pattern, sizeof(pattern), "%s\\*", dir);

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
			char path[MAX_PATH];
			snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				removeDirRecursive(path);
			} else {
				DeleteFileA(path);
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryA(dir);
}

/* Walk installDir, deleting any file/dir that is absent from stagingDir
 * and not under a protected path. relBase is "" for the root pass. */
static void cleanupStaleFiles(const char *installDir, const char *stagingDir, const char *relBase)
{
	char searchPath[MAX_PATH];
	if (relBase[0]) {
		snprintf(searchPath, sizeof(searchPath), "%s\\%s\\*", installDir, relBase);
	} else {
		snprintf(searchPath, sizeof(searchPath), "%s\\*", installDir);
	}

	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(searchPath, &fd);
	if (h == INVALID_HANDLE_VALUE) return;

	do {
		if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
		/* Skip the staging dir and any update artifacts */
		if (strcmp(fd.cFileName, "pd_update_staging") == 0) continue;
		{
			const char *fn = fd.cFileName;
			size_t fnl = strlen(fn);
			if (fnl > 4  && strcmp(fn + fnl - 4,  ".old") == 0) continue;
			if (fnl > 4  && strcmp(fn + fnl - 4,  ".zip") == 0) continue;
			if (fnl > 4  && strcmp(fn + fnl - 4,  ".ver") == 0) continue;
		}

		char relPath[MAX_PATH];
		if (relBase[0]) {
			snprintf(relPath, sizeof(relPath), "%s\\%s", relBase, fd.cFileName);
		} else {
			snprintf(relPath, sizeof(relPath), "%s", fd.cFileName);
		}

		if (isProtectedRelPath(relPath)) continue;

		char stagingPath[MAX_PATH];
		snprintf(stagingPath, sizeof(stagingPath), "%s\\%s", stagingDir, relPath);
		s32 inStaging = (GetFileAttributesA(stagingPath) != INVALID_FILE_ATTRIBUTES);

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (inStaging) {
				cleanupStaleFiles(installDir, stagingDir, relPath);
			} else {
				char fullPath[MAX_PATH];
				snprintf(fullPath, sizeof(fullPath), "%s\\%s", installDir, relPath);
				removeDirRecursive(fullPath);
				fprintf(stderr, "UPDATER: Removed stale dir: %s\n", relPath);
			}
		} else {
			if (!inStaging) {
				char fullPath[MAX_PATH];
				snprintf(fullPath, sizeof(fullPath), "%s\\%s", installDir, relPath);
				DeleteFileA(fullPath);
				fprintf(stderr, "UPDATER: Removed stale file: %s\n", relPath);
			}
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

#endif /* _WIN32 */

/* ========================================================================
 * Public API — Self-replacement
 * ======================================================================== */

s32 updaterApplyPending(void)
{
	detectExePath();

	/* Diagnostics: always log what we're looking for, even before sysInit.
	 * Use fprintf(stderr) as a reliable fallback since sysLogPrintf may
	 * not be fully initialized this early in startup. */
	fprintf(stderr, "UPDATER: Checking for pending update at: %s\n", s_Updater.updatePath);
	fprintf(stderr, "UPDATER: Current exe path: %s\n", s_Updater.exePath);
	sysLogPrintf(LOG_NOTE, "UPDATER: Checking for pending update at: %s", s_Updater.updatePath);

	/* Clean up .old from previous update */
	updaterCleanupOld();

	/* Check for .update file */
	FILE *test = fopen(s_Updater.updatePath, "rb");
	if (!test) {
		fprintf(stderr, "UPDATER: No pending update found (file does not exist)\n");
		sysLogPrintf(LOG_NOTE, "UPDATER: No pending update found at %s", s_Updater.updatePath);
		return 0;  /* no pending update */
	}
	fclose(test);

	sysLogPrintf(LOG_NOTE, "UPDATER: Found pending update ZIP: %s", s_Updater.updatePath);

#ifdef _WIN32
	/* Step 1: Extract ZIP to staging directory */
	fprintf(stderr, "UPDATER: Extracting ZIP to staging: %s\n", s_Updater.stagingDir);
	if (extractZipToStaging(s_Updater.updatePath, s_Updater.stagingDir) != 0) {
		fprintf(stderr, "UPDATER: ZIP extraction failed\n");
		sysLogPrintf(LOG_ERROR, "UPDATER: ZIP extraction failed");
		return -1;
	}

	/* Step 2: Verify extraction produced the expected client binary */
	{
		char testExe[MAX_PATH];
		snprintf(testExe, sizeof(testExe), "%s\\PerfectDark.exe", s_Updater.stagingDir);
		if (GetFileAttributesA(testExe) == INVALID_FILE_ATTRIBUTES) {
			fprintf(stderr, "UPDATER: Staging appears empty or invalid — extraction failed\n");
			sysLogPrintf(LOG_ERROR, "UPDATER: Staging dir missing PerfectDark.exe after extraction");
			removeDirRecursive(s_Updater.stagingDir);
			return -1;
		}
	}

	/* Step 3: Rename current exe → .old (frees the exe name so we can overwrite it) */
	if (!MoveFileExA(s_Updater.exePath, s_Updater.oldPath,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		fprintf(stderr, "UPDATER: Failed to rename exe to .old (error %lu)\n", GetLastError());
		sysLogPrintf(LOG_ERROR, "UPDATER: Failed to rename exe to .old (error %lu)", GetLastError());
		removeDirRecursive(s_Updater.stagingDir);
		return -1;
	}

	/* Step 4: Copy all staging files → install dir (overwrite mode) */
	fprintf(stderr, "UPDATER: Copying staging → install dir: %s\n", s_Updater.installDir);
	copyDirRecursive(s_Updater.stagingDir, s_Updater.installDir);

	/* Step 5: Cleanup stale files (delete install files absent from ZIP, skip protected) */
	{
		/* Read protected folders from pd.ini directly (config not yet initialized).
		 * Fall back to the compiled default if the setting is absent. */
		char iniPath[MAX_PATH];
		snprintf(iniPath, sizeof(iniPath), "%s\\pd.ini", s_Updater.installDir);
		char protectedCsv[512];
		strncpy(protectedCsv, UPDATER_DEFAULT_PROTECTED, sizeof(protectedCsv) - 1);
		protectedCsv[sizeof(protectedCsv) - 1] = '\0';
		readProtectedFoldersFromIni(iniPath, protectedCsv, sizeof(protectedCsv));
		buildProtectedList(protectedCsv);
		fprintf(stderr, "UPDATER: Protected folders: %s (+ pd.ini always)\n", protectedCsv);
	}
	fprintf(stderr, "UPDATER: Cleaning up stale files...\n");
	cleanupStaleFiles(s_Updater.installDir, s_Updater.stagingDir, "");

	/* Step 6: Remove staging dir, update ZIP, and version sidecar */
	removeDirRecursive(s_Updater.stagingDir);
	remove(s_Updater.updatePath);
	remove(s_Updater.versionPath);

	sysLogPrintf(LOG_NOTE, "UPDATER: Update applied successfully — restarting...");
	fprintf(stderr, "UPDATER: Update applied — restarting...\n");

	/* Step 7: Re-launch the new binary */
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);

	if (CreateProcessA(s_Updater.exePath, GetCommandLineA(),
			NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		ExitProcess(0);
	}

	sysLogPrintf(LOG_WARNING, "UPDATER: Re-launch failed (error %lu), continuing with new binary",
		GetLastError());
	return 1;
#else
	/* Unix: rename and re-exec */
	if (rename(s_Updater.exePath, s_Updater.oldPath) != 0) {
		sysLogPrintf(LOG_ERROR, "UPDATER: Failed to rename current exe to .old");
		return -1;
	}

	if (rename(s_Updater.updatePath, s_Updater.exePath) != 0) {
		rename(s_Updater.oldPath, s_Updater.exePath);
		sysLogPrintf(LOG_ERROR, "UPDATER: Failed to rename .update to exe");
		return -1;
	}

	/* Clean up version sidecar — update has been applied */
	remove(s_Updater.versionPath);

	sysLogPrintf(LOG_NOTE, "UPDATER: Update applied — re-execing...");

	/* Re-exec with same args */
	extern int g_Argc;
	extern const char **g_Argv;
	execv(s_Updater.exePath, (char *const *)g_Argv);

	/* If execv returns, something went wrong */
	sysLogPrintf(LOG_WARNING, "UPDATER: execv failed, continuing");
	return 1;
#endif
}

void updaterCleanupOld(void)
{
	/* Try to remove .old from previous update */
	remove(s_Updater.oldPath);
}

/* ========================================================================
 * Public API — Channel management
 * ======================================================================== */

update_channel_t updaterGetChannel(void)
{
	return s_Updater.channel;
}

void updaterSetChannel(update_channel_t channel)
{
	if (channel >= UPDATE_CHANNEL_COUNT) channel = UPDATE_CHANNEL_STABLE;
	s_Updater.channel = channel;
	s_UpdateChannelCfg = (s32)channel;
	configSave("pd.ini");
}

/* ========================================================================
 * Public API — Version info
 * ======================================================================== */

const pdversion_t *updaterGetCurrentVersion(void)
{
	return &s_Updater.currentVersion;
}

const char *updaterGetVersionString(void)
{
	return s_Updater.versionStr;
}

s32 updaterIsServer(void)
{
	return s_Updater.isServer;
}
