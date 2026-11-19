#ifndef PD_GRID_MATCH_PACKAGE_IO_H
#define PD_GRID_MATCH_PACKAGE_IO_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "fs.h"
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Split a canonical typed descriptor locator at its first typed owner.
 * The owner may be a loose typed archive or an entry in one physical .pdmod.
 * root preserves the locator spelling. member_chain is relative to that
 * root and empty for its own descriptor. The optional leaf borrows the
 * input string. A refusal leaves all outputs unchanged. */
s32 gridMatchPackageLocation(const char *descriptor, char *root, size_t root_cap,
	char *member_chain, size_t chain_cap, const char **descriptor_leaf);

/* Resolve the immediate typed owner of a nonempty relative archive chain.
 * The returned member borrows the final segment. A refusal preserves outputs. */
s32 gridMatchPackageMemberOwner(asset_type_e outer_type, const char *chain,
	asset_type_e *owner_type, const char **member);

/* Read the exact bytes named by an owner locator from gridMatchPackageLocation.
 * The caller frees the result with free(). On failure out_size is zero. */
void *gridMatchPackageLoad(const char *locator, u32 *out_size);

/* Check a canonical suffix (for example .pdmesh) against the typed owners
 * supported by temporary Grid admission. The caller still checks package
 * identity, archive contents and exact redistribution permission. */
s32 gridMatchPackageTypeShareable(asset_type_e type, const char *suffix);

/* Resolve the game-owned, digest-named path used for a temporary native
 * package with a canonical shareable suffix. This does not create its
 * directory or write an archive. A refusal leaves the outputs unchanged. */
s32 gridMatchPackagePrivatePath(const u8 digest[SHA256_DIGEST_SIZE], const char *suffix,
	char folder[FS_MAXPATH + 1], char path[FS_MAXPATH + 1]);

/* Read one unique, canonical member with a size check before expansion.
 * A true absence sets absent to 1. The caller frees successful results. */
void *gridMatchPackageReadMember(const void *archive, u32 size, const char *member,
	u32 max_size, u32 *out_size, s32 *absent);

#ifdef __cplusplus
}
#endif

#endif
