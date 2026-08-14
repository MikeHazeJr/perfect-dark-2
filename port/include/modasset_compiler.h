/**
 * modasset_compiler.h -- private runtime cache adapter for external mod assets.
 *
 * External-format .pdmod archives expose standard authoring files to modders
 * (GLTF/OBJ/INI/JSON/etc.). When the engine needs a native runtime payload, the
 * conversion result belongs in this private cache layer, not back inside the
 * authored archive.
 */

#ifndef _IN_MODASSET_COMPILER_H
#define _IN_MODASSET_COMPILER_H

#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODASSET_COMPILER_VERSION 7
#define MODASSET_COMPILER_MODELDEF_VERSION 9
#define MODASSET_SHA256_HEX_LEN 65

struct colmesh;
struct modeldef;
struct modelnode;
struct animtableentry;
struct skeleton;

typedef struct modasset_compiled_result {
	s32 ready;
	char descriptor_path[FS_MAXPATH];
	char normalized_path[FS_MAXPATH];
	char source_sha256[MODASSET_SHA256_HEX_LEN];
	u32 source_size;
	s32 vertex_count;
	s32 triangle_count;
	char validation[128];
} modasset_compiled_result_t;

/**
 * Returns true for external 3-D/animation source formats that must be compiled
 * before the legacy engine can consume them.
 */
s32 modAssetCompilerIsExternalSource(const char *path);

/**
 * Returns true for animation authoring sources that can be rebuilt into the
 * engine animation byte stream.
 */
s32 modAssetCompilerIsAnimationSource(const char *path);

/**
 * Verify an external source file and write/update a private cache descriptor.
 *
 * The cache descriptor is not an authored asset and deliberately does not use a
 * .bin suffix. Future format-specific backends can add native payload paths to
 * the descriptor while preserving this stable cache key contract.
 *
 * Returns 1 on cache-ready, 0 when the source is not an external format, and -1
 * on validation/write failure.
 */
s32 modAssetCompilerEnsureCache(const asset_entry_t *entry,
                                const char *asset_kind,
                                const char *source_path,
                                char *out_cache_path,
                                s32 out_cache_path_len);

/**
 * Verify/compile an external source into readable generated cache.
 *
 * Mesh sources also produce a normalized JSON mesh cache that can be translated
 * into engine-native collision mesh memory. GLTF/GLB scenario sources are not
 * descriptor-only: they are the authoritative runtime source for generated
 * collision when no collision override is present.
 */
s32 modAssetCompilerCompileReadable(const asset_entry_t *entry,
                                    const char *asset_kind,
                                    const char *source_path,
                                    modasset_compiled_result_t *out);

/**
 * Build an engine collision mesh from a supported authored mesh source. Returns
 * 1 when a mesh was built, 0 when the source is not supported, and -1 on
 * parse/allocation failure.
 */
s32 modAssetCompilerBuildColmesh(const char *source_path,
                                 struct colmesh *out_mesh);

/**
 * Compatibility wrapper for existing OBJ call sites.
 */
s32 modAssetCompilerBuildObjColmesh(const char *source_path,
                                    struct colmesh *out_mesh);

/**
 * Build an engine model definition from a standard authored static mesh source.
 * The returned modeldef is an in-memory, modeldef-compatible runtime payload and
 * must be released with modAssetCompilerFreeModeldef.
 *
 * Returns 1 when a modeldef was built, 0 when the source is not a supported
 * external model source, and -1 on parse/allocation/conversion failure.
 */
s32 modAssetCompilerBuildModeldef(const asset_entry_t *entry,
                                  const char *source_path,
                                  struct modeldef **out_modeldef);

void modAssetCompilerFreeModeldef(struct modeldef *modeldef);

void modAssetCompilerSetGeneratedModeldefRenderAudit(s32 enabled);
s32 modAssetCompilerGeneratedModeldefRenderAuditEnabled(void);
s32 modAssetCompilerNeedlerRenderAuditWitnessActive(void);
s32 modAssetCompilerModeldefIsGenerated(const struct modeldef *modeldef);

/**
 * Resolve a command-aligned pointer against every live generated model GDL,
 * including hierarchy payloads, their relocated GUNDL copies, and payloads
 * owned by a separately generated head attached to a character clone.
 * Returns 1 with exact bytes remaining, or 0 with out_bytes_remaining cleared.
 */
s32 modAssetCompilerGeneratedGdlBytesRemaining(
	const void *gdl, u32 *out_bytes_remaining);

s32 modAssetCompilerShouldHideGeneratedModeldef(const struct modeldef *modeldef);
void modAssetCompilerTraceGeneratedModeldefRender(
	const struct modeldef *modeldef,
	const struct modelnode *node);
void modAssetCompilerTraceGeneratedModeldefRenderStep(
	const struct modeldef *modeldef,
	const struct modelnode *node,
	const char *stage,
	const void *rwdata,
	const void *gdl,
	const void *vertices,
	const void *colours,
	s32 numvertices,
	s32 mcount);

const char *modAssetCompilerSkeletonSymbolForPointer(
	const struct skeleton *skeleton);
struct skeleton *modAssetCompilerSkeletonForSymbol(const char *symbol);

/**
 * Build an engine animation clip from a GLTF/GLB animation source. Static clips
 * use an identity header; skeletal translation/rotation/scale channels are
 * packed into the engine animtableentry header/frame byte stream. Unsupported
 * channel paths fail validation with a readable error rather than falling back
 * to raw source data.
 *
 * Returns 1 when a clip was built, 0 when the source is not a supported
 * external animation source, and -1 on parse/allocation/conversion failure.
 */
s32 modAssetCompilerBuildAnimationClip(const asset_entry_t *entry,
                                       const char *source_path,
                                       struct animtableentry *out_entry,
                                       u8 **out_data,
                                       u32 *out_data_size);

void modAssetCompilerFreeAnimationClip(void *data);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODASSET_COMPILER_H */
