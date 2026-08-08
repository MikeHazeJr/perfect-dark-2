#ifndef PD2_THEME_ARCHIVE_AUTHORING_H
#define PD2_THEME_ARCHIVE_AUTHORING_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum theme_archive_dependency_role {
	THEME_ARCHIVE_DEP_UI = 0,
	THEME_ARCHIVE_DEP_FONT,
	THEME_ARCHIVE_DEP_AUDIO,
	THEME_ARCHIVE_DEP_MUSIC,
	THEME_ARCHIVE_DEP_COUNT
} theme_archive_dependency_role_e;

typedef struct theme_archive_dependency {
	theme_archive_dependency_role_e role;
	const char *catalog_id;
	const char *archive_path;
} theme_archive_dependency_t;

typedef struct theme_archive_author_request {
	const char *archive_path;
	const char *catalog_id;
	const char *display_name;
	const char *theme_json;
	size_t theme_json_size;
	const theme_archive_dependency_t *dependencies;
	size_t dependency_count;
} theme_archive_author_request_t;

/* Writes a complete staged .pdtheme, validates the public archive in release
 * mode, then atomically replaces archive_path. A failed candidate never
 * replaces the last good file. */
s32 themeArchiveAuthor(const theme_archive_author_request_t *request,
	char *error, size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif
