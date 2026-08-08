/**
 * voice_archive_authoring.h -- atomic creator-facing .pdvoice emission.
 *
 * The request contains public authored source only. The writer emits a
 * self-contained typed archive, validates the staged archive, and replaces
 * the destination only after validation succeeds.
 */
#ifndef _IN_VOICE_ARCHIVE_AUTHORING_H
#define _IN_VOICE_ARCHIVE_AUTHORING_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct voice_archive_author_request {
	const char *archive_path;
	const char *catalog_id;
	const char *display_name;
	const char *source_audio_path;
	const char *actor;
	const char *context;
	const char *subtitle;
	u32 duration_ms;
	const char *locale;
	const char *fallback_locale;
} voice_archive_author_request_t;

/** Returns 1 on success, 0 on validation or I/O failure. */
s32 voiceArchiveAuthor(const voice_archive_author_request_t *request,
	char *error, size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_VOICE_ARCHIVE_AUTHORING_H */
