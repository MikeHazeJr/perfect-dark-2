/**
 * assetcatalog_slug.h -- c3849 Wave 4 (Slice B): the ONE catalog-id to
 * filename slug shared by typed-archive emit and registration-time binding.
 *
 * The emitter side (romextract_pdtexture.c s_archiveRelPath) names the
 * archive on disk: data/<romid>/textures/<slug>.pdtexture. The catalog side
 * (assetcatalog_base_extended.c s_buildArchiveMemberPath) builds the bound
 * FileProvider primary path: data/<romid>/textures/<slug>.pdtexture::texture.png.
 * If the two slugs ever diverge, boot completes fine and the FIRST texLoad
 * of the divergent texture sysFatalErrors (mod_texture_source.c fatal-on-
 * missing-source contract) with zero boot-time detection. One shared body
 * makes that drift impossible for the callers below; the remaining private
 * copy in romextract_pdmeta.c (s_idToFilenameSlug, used by the non-texture
 * meta families that assetcatalog_base_extended.c also binds) is pinned
 * byte-identical to this body by tests/test_asset_native_source_contract.cpp.
 *
 * Mapping: ':' '/' '\\' become '_'; every other byte is copied verbatim
 * (case is preserved); output is truncated to out_n - 1 plus NUL. Do not
 * "improve" the mapping in place -- existing installs already have archives
 * on disk named with these slugs, so any change is a re-emit migration.
 *
 * Header-only static inline so C and C++ translation units share one
 * source text with no link-order or include-cycle cost.
 */
#ifndef ASSETCATALOG_SLUG_H
#define ASSETCATALOG_SLUG_H

#include <stddef.h>

static inline void catalogIdToFilenameSlug(const char *id, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	if (!id) return;
	size_t j = 0;
	for (size_t i = 0; id[i] && j + 1 < out_n; i++) {
		char c = id[i];
		out[j++] = (c == ':' || c == '/' || c == '\\') ? '_' : c;
	}
	out[j] = '\0';
}

#endif /* ASSETCATALOG_SLUG_H */
