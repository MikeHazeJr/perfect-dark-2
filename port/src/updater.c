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
	char updatePath[512];  /* exePath + ".update" */
	char oldPath[512];     /* exePath + ".old" */

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

	/* Check for -dev.N suffix */
	const char *dash = strchr(str, '-');
	if (dash) {
		if (strncmp(dash, "-dev.", 5) == 0) {
			out->dev = atoi(dash + 5);
		}
	}

	return 0;
}

s32 versionFormat(const pdversion_t *ver, char *buf, s32 bufsize)
{
	if (ver->dev > 0) {
		return snprintf(buf, bufsize, "%d.%d.%d-dev.%d",
			ver->major, ver->minor, ver->patch, ver->dev);
	} else {
		return snprintf(buf, bufsize, "%d.%d.%d",
			ver->major, ver->minor, ver->patch);
	}
}

s32 versionParseTag(const char *tag, char *prefixbuf, s32 prefixbufsize, pdversion_t *ver)
{
	if (!tag || !ver) return -1;

	/* Find the "-v" separator */
	const char *vp = strstr(tag, "-v");
	if (!vp) return -1;

	/* Extract prefix */
	if (prefixbuf) {
		s32 plen = (s32)(vp - tag);
		if (plen >= prefixbufsize) plen = prefixbufsize - 1;
		memcpy(prefixbuf, tag, plen);
		prefixbuf[plen] = '\0';
	}

	/* Parse version after "-v" */
	return versionParse(vp + 1, ver);  /* +1 to skip the '-', versionParse skips 'v' */
}

/* ========================================================================
 * GitHub API response parsing
 * ======================================================================== */

static const char *getTagPrefix(void)
{
	return s_Updater.isServer ? UPDATER_TAG_SERVER : UPDATER_TAG_CLIENT;
}

static const char *getAssetName(void)
{
	return s_Updater.isServer ? UPDATER_ASSET_SERVER : UPDATER_ASSET_CLIENT;
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
	const char *assetName = getAssetName();
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
				jp_copystr(&val, rel->body, sizeof(rel->body));
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

						/* Match asset to our binary or hash */
						if (strcmp(aname, assetName) == 0) {
							strncpy(rel->assetUrl, aurl, UPDATER_MAX_URL_LEN - 1);
							rel->assetSize = asize;
						} else {
							/* Check for .sha256 sidecar */
							char hashname[140];
							snprintf(hashname, sizeof(hashname), "%s.sha256", assetName);
							if (strcmp(aname, hashname) == 0) {
								strncpy(rel->hashUrl, aurl, UPDATER_MAX_URL_LEN - 1);
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
		return -1;  /* wrong prefix — skip this release */
	}

	char prefixbuf[32];
	if (versionParseTag(tagStr, prefixbuf, sizeof(prefixbuf), &rel->version) != 0) {
		return -1;
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

static int SDLCALL downloadThread(void *data)
{
	const updater_release_t *rel = (const updater_release_t *)data;

	sysLogPrintf(LOG_NOTE, "UPDATER: Downloading %s (%lld bytes)",
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

	s_Updater.progress.percent = 100.0f;
	s_Updater.status = UPDATER_DOWNLOAD_DONE;

	char verstr[32];
	versionFormat(&rel->version, verstr, sizeof(verstr));
	sysLogPrintf(LOG_NOTE, "UPDATER: Downloaded v%s — restart to apply", verstr);

	SDL_UnlockMutex(s_Updater.mutex);
	return 0;
}

/* ========================================================================
 * Executable path detection
 * ======================================================================== */

#ifdef _WIN32
#include <windows.h>
static void detectExePath(void)
{
	GetModuleFileNameA(NULL, s_Updater.exePath, sizeof(s_Updater.exePath));
	snprintf(s_Updater.updatePath, sizeof(s_Updater.updatePath),
		"%s%s", s_Updater.exePath, UPDATER_SUFFIX_UPDATE);
	snprintf(s_Updater.oldPath, sizeof(s_Updater.oldPath),
		"%s%s", s_Updater.exePath, UPDATER_SUFFIX_OLD);
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
		strncpy(s_Updater.exePath, "pd.x86_64", sizeof(s_Updater.exePath));
	}
	snprintf(s_Updater.updatePath, sizeof(s_Updater.updatePath),
		"%s%s", s_Updater.exePath, UPDATER_SUFFIX_UPDATE);
	snprintf(s_Updater.oldPath, sizeof(s_Updater.oldPath),
		"%s%s", s_Updater.exePath, UPDATER_SUFFIX_OLD);
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
	s_Updater.channel = UPDATE_CHANNEL_STABLE;
	s_Updater.latestIndex = -1;

#ifdef PD_SERVER
	s_Updater.isServer = 1;
#else
	s_Updater.isServer = 0;
#endif

	detectExePath();

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

/* ========================================================================
 * Public API — Self-replacement
 * ======================================================================== */

s32 updaterApplyPending(void)
{
	detectExePath();

	/* Clean up .old from previous update */
	updaterCleanupOld();

	/* Check for .update file */
	FILE *test = fopen(s_Updater.updatePath, "rb");
	if (!test) {
		return 0;  /* no pending update */
	}
	fclose(test);

	sysLogPrintf(LOG_NOTE, "UPDATER: Found pending update: %s", s_Updater.updatePath);

#ifdef _WIN32
	/* Step 1: Rename current exe to .old */
	if (!MoveFileExA(s_Updater.exePath, s_Updater.oldPath,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		sysLogPrintf(LOG_ERROR, "UPDATER: Failed to rename current exe to .old (error %lu)",
			GetLastError());
		return -1;
	}

	/* Step 2: Rename .update to current exe */
	if (!MoveFileExA(s_Updater.updatePath, s_Updater.exePath,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		/* Rollback: put old exe back */
		MoveFileExA(s_Updater.oldPath, s_Updater.exePath,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		sysLogPrintf(LOG_ERROR, "UPDATER: Failed to rename .update to exe (error %lu)",
			GetLastError());
		return -1;
	}

	sysLogPrintf(LOG_NOTE, "UPDATER: Update applied successfully — restarting...");

	/* Step 3: Re-launch the new binary */
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

	/* If CreateProcess fails, we still applied the update — just continue */
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
