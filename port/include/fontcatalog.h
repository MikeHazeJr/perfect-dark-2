/**
 * fontcatalog.h -- c3849 Wave 3: runtime compiler for public .pdfont
 * bitmap font source.
 *
 * textLoadFont (src/game/game_1531a0.c) consumes the catalog-bound
 * public source FIRST: fontCatalogBuildFace() resolves
 * "base:font_<face>" (ASSET_FONT), loads the archive's glyphs.pgm +
 * font.metrics.json members through fsFileLoad's archive::member
 * chains, and recompiles them into a buffer laid out EXACTLY like the
 * post-preprocess font segment (see port/src/preprocess/segfonts.c):
 *
 *   - little-endian s32 kerning[13*13] at offset 0
 *   - struct fontchar[num_chars] at offsetof(struct font, chars),
 *     filled field-by-field (the struct is build-variant and
 *     pointer-size dependent; never memcpy fixed offsets)
 *   - fontchar.pixeldata = buffer-relative offset to CI4 glyph pixels
 *     repacked from the 8-bit PGM atlas via round-to-nearest
 *     (v + 8) / 17 clamped to 15, fixed 8-byte row stride
 *
 * The caller then runs the exact post-load mutations the segment path
 * runs (pixeldata base patch, JPN baseline, monospace clamp, cache
 * registration, integrity checksum, PAL pipe baseline).
 *
 * Every miss path logs LOG_WARNING and returns 0 so the caller falls
 * back to the ROM segment bytes loudly (asset-chain telemetry is the
 * caller's responsibility). Model: langManifestLoadBankFromCatalog.
 */
#ifndef PD_FONTCATALOG_H
#define PD_FONTCATALOG_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compile the public .pdfont source for `face` (e.g. "handelgothicsm")
 * into a malloc'd segment-shaped payload. On success returns 1 and the
 * caller owns *out_payload (free() it after copying). On any miss
 * (no catalog row, member load failure, PGM/JSON parse failure, glyph
 * bounds, real pixel data past the 16-column CI4 stride ceiling) logs
 * LOG_WARNING and returns 0 with *out_payload = NULL. Declared widths
 * above 16 with zero pixels past column 16 are advance-only metrics
 * (base handelgothiclg has one) and pack the 16 storable columns. */
s32 fontCatalogBuildFace(const char *face, u8 **out_payload, u32 *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PD_FONTCATALOG_H */
