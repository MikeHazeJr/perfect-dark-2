#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_archive_policy.h"
#include "asset_path_contract.h"
#include "catalog_dep_activation_plan.h"
#include "grid_match_sources.h"
#include "mod_asset_validate.h"
#include "net/net.h"
#include "constants.h"
#include "net/netmanifest.h"

_Static_assert(ASSET_TYPE_COUNT <= GRID_MATCH_SOURCE_FIESTA_WIRE_BIT,
	"match source type fits below the Fiesta membership bit");

enum { GRID_MATCH_SOURCE_FIESTA_MAX = NUM_MPWEAPONS - 3 }; /* NONE, DISABLED and SHIELD */

static s32 sourceError(char *err, size_t cap, const char *message, const char *id)
{
	if (err && cap) snprintf(err, cap, "%s%s%s", message, id ? ": " : "", id ? id : "");
	return 0;
}

void gridMatchSourcesFree(grid_match_sources_t *plan)
{
	if (!plan) return;
	free(plan->entries);
	free(plan->packages);
	memset(plan, 0, sizeof(*plan));
}

static int sourceCompare(const void *lhs, const void *rhs)
{
	const grid_match_source_entry_t *a = lhs;
	const grid_match_source_entry_t *b = rhs;
	return strcmp(a->asset.id, b->asset.id);
}

typedef struct grid_match_model_review {
	grid_match_source_root_t asset;
	grid_match_source_archive_t archive;
	char (*textures)[CATALOG_ID_LEN];
	size_t texture_count;
	size_t *total_textures;
} grid_match_model_review_t;

typedef struct grid_match_source_review {
	grid_match_model_review_t *rows;
	size_t count;
	size_t capacity;
	size_t texture_count;
	grid_match_source_model_fn inspect;
	void *userdata;
	catalog_dep_activation_supplement_fn extra_edges;
	void *extra_data;
} grid_match_source_review_t;

static grid_match_model_review_t *sourceFindModel(grid_match_source_review_t *reviews,
	const char *id)
{
	for (size_t i = 0; i < reviews->count; i++) {
		if (strcmp(reviews->rows[i].asset.id, id) == 0) return &reviews->rows[i];
	}
	return NULL;
}

static void sourceFreeModels(grid_match_source_review_t *reviews)
{
	for (size_t i = 0; i < reviews->count; i++) free(reviews->rows[i].textures);
	free(reviews->rows);
}

static s32 sourceCatalogIdIsCanonical(const char id[CATALOG_ID_LEN]);

static s32 sourceRememberTexture(const char *id, void *userdata)
{
	grid_match_model_review_t *model = userdata;
	char canonical[CATALOG_ID_LEN];
	if (!assetPathCopyChecked(canonical, sizeof(canonical), id)
			|| !sourceCatalogIdIsCanonical(canonical)) return 0;
	for (size_t i = 0; i < model->texture_count; i++) {
		if (strcmp(model->textures[i], canonical) == 0) return 1;
	}
	if (*model->total_textures == MANIFEST_MAX_ENTRIES) return 0;
	char (*grown)[CATALOG_ID_LEN] = realloc(model->textures,
		(model->texture_count + 1) * sizeof(*grown));
	if (!grown) return 0;
	model->textures = grown;
	assetPathCopyChecked(model->textures[model->texture_count++], CATALOG_ID_LEN, canonical);
	++*model->total_textures;
	return 1;
}

static s32 sourceSupplementEdges(const char *id, asset_type_e type, catalog_dep_activation_add_fn add,
	void *add_context, void *userdata, char *err, size_t err_cap)
{
	grid_match_source_review_t *reviews = userdata;
	if (reviews->extra_edges && !reviews->extra_edges(id, type, add, add_context,
			reviews->extra_data, err, err_cap)) {
		if (err && err_cap && !err[0]) sourceError(err, err_cap, "could not include Grid source dependencies", id);
		return 0;
	}
	if (type != ASSET_MODEL || !reviews->inspect) return 1;
	grid_match_model_review_t *model = sourceFindModel(reviews, id);
	if (!model) {
		if (reviews->count == MANIFEST_MAX_ENTRIES) return sourceError(err, err_cap, "too many Grid model dependencies", id);
		if (reviews->count == reviews->capacity) {
			size_t capacity = reviews->capacity ? reviews->capacity * 2 : 16;
			if (capacity > MANIFEST_MAX_ENTRIES) capacity = MANIFEST_MAX_ENTRIES;
			grid_match_model_review_t *rows = realloc(reviews->rows, capacity * sizeof(*rows));
			if (!rows) return sourceError(err, err_cap, "out of memory reviewing Grid models", id);
			reviews->rows = rows;
			reviews->capacity = capacity;
		}
		model = &reviews->rows[reviews->count++];
		memset(model, 0, sizeof(*model));
		model->total_textures = &reviews->texture_count;
		if (!assetPathCopyChecked(model->asset.id, sizeof(model->asset.id), id)) {
			return sourceError(err, err_cap, "invalid Grid model identity", id);
		}
		model->asset.type = type;
		if (!reviews->inspect(&model->asset, &model->archive, sourceRememberTexture, model,
				reviews->userdata, err, err_cap)) return 0;
	}

	for (size_t i = 0; i < model->texture_count; i++) {
		if (!add(model->textures[i], ASSET_TEXTURE, add_context)) {
			return sourceError(err, err_cap, "could not include Grid model texture", model->textures[i]);
		}
	}
	return 1;
}

static s32 sourceAppendRoot(grid_match_sources_t *plan, const grid_match_source_root_t *root,
	grid_match_source_resolve_fn resolve, void *userdata, grid_match_source_review_t *reviews,
	char *err, size_t err_cap)
{
	catalog_dep_activation_plan_t closure = {0};
	if (!memchr(root->id, '\0', sizeof(root->id)) || !root->id[0]
			|| root->type <= ASSET_NONE || root->type >= ASSET_TYPE_COUNT) {
		return sourceError(err, err_cap, "invalid match asset root", NULL);
	}
	if (!catalogDepActivationPlanBuildWithEdges(&closure, root->id, root->type, resolve, userdata,
			(reviews->inspect || reviews->extra_edges) ? sourceSupplementEdges : NULL, reviews, err, err_cap)) {
		return 0;
	}
	s32 ok = 1;
	for (size_t i = 0; i < closure.count && ok; i++) {
		const catalog_dep_activation_node_t *node = &closure.nodes[i];
		size_t index = 0;
		while (index < plan->entry_count && strcmp(plan->entries[index].asset.id, node->id) != 0) index++;
		if (index < plan->entry_count) {
			if (plan->entries[index].asset.type != node->expected_type) {
				ok = sourceError(err, err_cap, "conflicting asset types", node->id);
			}
			continue;
		}
		if (index == MANIFEST_MAX_ENTRIES || node->expected_type <= ASSET_NONE
				|| node->expected_type >= ASSET_TYPE_COUNT) {
			ok = sourceError(err, err_cap, "invalid asset type or match manifest is full", node->id);
			continue;
		}
		grid_match_source_entry_t *entry = &plan->entries[plan->entry_count++];
		snprintf(entry->asset.id, sizeof(entry->asset.id), "%s", node->id);
		entry->asset.type = node->expected_type;
	}
	catalogDepActivationPlanFree(&closure);
	return ok;
}

static s32 sourceAddArchive(grid_match_sources_t *plan, grid_match_source_entry_t *entry,
	const grid_match_source_archive_t *archive, char *err, size_t err_cap)
{
	if (!archive->size || !archive->path[0] || !memchr(archive->path, '\0', sizeof(archive->path))
			|| !memchr(archive->member_chain, '\0', sizeof(archive->member_chain))
			|| !memchr(archive->owner.id, '\0', sizeof(archive->owner.id)) || !archive->owner.id[0]
			|| !memchr(archive->suffix, '\0', sizeof(archive->suffix))
			|| archive->owner.type <= ASSET_NONE || archive->owner.type >= ASSET_TYPE_COUNT
			|| assetArchiveTypeForPath(archive->suffix) != archive->owner.type) {
		return sourceError(err, err_cap, "invalid owning archive", entry->asset.id);
	}
	size_t index = plan->package_count;
	for (size_t i = 0; i < plan->package_count; i++) {
		const grid_match_source_package_t *package = &plan->packages[i];
		s32 same_digest = memcmp(package->digest, archive->digest, sizeof(package->digest)) == 0;
		if (same_digest) {
			if (package->size != archive->size || package->bundled != archive->bundled
					|| package->owner.type != archive->owner.type
					|| strcmp(package->owner.id, archive->owner.id) != 0
					|| strcmp(package->suffix, archive->suffix) != 0) {
				return sourceError(err, err_cap, "conflicting archive attributes", entry->asset.id);
			}
			index = i;
		}
		if (!same_digest && strcmp(package->path, archive->path) == 0) {
			return sourceError(err, err_cap, "an archive changed during match preparation", entry->asset.id);
		}
	}
	if (index == plan->package_count) {
		grid_match_source_package_t *package = &plan->packages[plan->package_count++];
		snprintf(package->path, sizeof(package->path), "%s", archive->path);
		package->owner = archive->owner;
		snprintf(package->suffix, sizeof(package->suffix), "%s", archive->suffix);
		memcpy(package->digest, archive->digest, sizeof(package->digest));
		package->size = archive->size;
		package->bundled = archive->bundled;
		plan->total_bytes += archive->size;
	}
	entry->package_index = index;
	snprintf(entry->member_chain, sizeof(entry->member_chain), "%s", archive->member_chain);
	return 1;
}

static void sourceDigest(grid_match_sources_t *plan)
{
	static const char domain[] = "pd2.grid.match.sources.v2";
	sha256_ctx hash;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Init(&hash);
	sha256Update(&hash, domain, sizeof(domain));
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		const grid_match_source_package_t *archive = &plan->packages[entry->package_index];
		const u8 attributes[] = {
			(u8)(entry->asset.type | (entry->fiesta_pool ? GRID_MATCH_SOURCE_FIESTA_WIRE_BIT : 0)),
			(u8)archive->owner.type, (u8)(archive->bundled != 0),
			(u8)(archive->size >> 24), (u8)(archive->size >> 16),
			(u8)(archive->size >> 8), (u8)archive->size
		};
		sha256Update(&hash, attributes, sizeof(attributes));
		sha256Update(&hash, entry->asset.id, strlen(entry->asset.id) + 1);
		sha256Update(&hash, archive->owner.id, strlen(archive->owner.id) + 1);
		sha256Update(&hash, archive->suffix, strlen(archive->suffix) + 1);
		sha256Update(&hash, archive->digest, sizeof(archive->digest));
		sha256Update(&hash, entry->member_chain, strlen(entry->member_chain) + 1);
	}
	sha256Final(&hash, digest);
	sha256ToHex(digest, plan->digest);
}

static s32 sourceCatalogIdIsCanonical(const char id[CATALOG_ID_LEN])
{
	const char *end = memchr(id, '\0', CATALOG_ID_LEN);
	if (!end || end == id || strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_.-")
			!= (size_t)(end - id)) return 0;
	const char *separator = strchr(id, ':');
	return separator && separator != id && separator[1] && !strchr(separator + 1, ':');
}

static s32 sourceArchiveTypeMatches(asset_type_e declared, asset_type_e type)
{
	return declared == type || (type == ASSET_AUDIO && (declared == ASSET_SFX || declared == ASSET_MUSIC));
}

static s32 sourceMemberChainMatches(const char *chain, asset_type_e type)
{
	char name[FS_MAXPATH + 1];
	for (s32 depth = 0; depth < MOD_ASSET_VALIDATE_MAX_DEPTH; depth++) {
		const char *end = strstr(chain, "::");
		size_t length = end ? (size_t)(end - chain) : strlen(chain);
		memcpy(name, chain, length);
		name[length] = '\0';
		asset_type_e member_type = assetArchiveTypeForPath(name);
		if (member_type == ASSET_NONE || assetPathIsAbsolute(name)
				|| assetPathHasParentTraversal(name) || strpbrk(name, ":\r\n\\")) {
			return 0;
		}
		if (!end) return sourceArchiveTypeMatches(type, member_type);
		chain = end + 2;
	}
	return 0;
}

s32 gridMatchSourcesSeal(grid_match_sources_t *plan, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!plan || !plan->entries || !plan->packages || !plan->entry_count
			|| plan->entry_count > MANIFEST_MAX_ENTRIES || !plan->package_count
			|| plan->package_count > plan->entry_count) {
		return sourceError(err, err_cap, "invalid Grid manifest rows", NULL);
	}
	u64 total = 0;
	const u8 empty_digest[SHA256_DIGEST_SIZE] = {0};
	for (size_t i = 0; i < plan->package_count; i++) {
		const grid_match_source_package_t *package = &plan->packages[i];
		if (!package->size || package->size > NET_DISTRIB_MAX_COMP) {
			return sourceError(err, err_cap, "Grid archive exceeds the package limit or is empty", NULL);
		}
		if (!sourceCatalogIdIsCanonical(package->owner.id)
				|| !memchr(package->suffix, '\0', sizeof(package->suffix))
				|| package->suffix[0] != '.'
				|| strspn(package->suffix + 1, "abcdefghijklmnopqrstuvwxyz0123456789")
					!= strlen(package->suffix + 1)
				|| package->owner.type <= ASSET_NONE || package->owner.type >= ASSET_TYPE_COUNT
				|| assetArchiveTypeForPath(package->suffix) != package->owner.type
				|| (package->bundled != 0 && package->bundled != 1)
				|| memcmp(package->digest, empty_digest, sizeof(empty_digest)) == 0) {
			return sourceError(err, err_cap, "invalid Grid package identity", NULL);
		}
		for (size_t j = 0; j < i; j++) {
			const grid_match_source_package_t *other = &plan->packages[j];
			if (strcmp(other->owner.id, package->owner.id) == 0
					|| memcmp(other->digest, package->digest, sizeof(package->digest)) == 0) {
				return sourceError(err, err_cap, "duplicate or conflicting archive owner", package->owner.id);
			}
		}
		total += package->size;
		if (total > NET_DISTRIB_MAX_SESSION) {
			return sourceError(err, err_cap, "Grid packages exceed the session budget", NULL);
		}
	}

	size_t next_package = 0;
	size_t fiesta_count = 0;
	for (size_t i = 0; i < plan->entry_count; i++) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		if (!sourceCatalogIdIsCanonical(entry->asset.id)
				|| entry->asset.type <= ASSET_NONE || entry->asset.type >= ASSET_TYPE_COUNT
				|| (i && strcmp(plan->entries[i - 1].asset.id, entry->asset.id) >= 0)
				|| entry->package_index >= plan->package_count || entry->package_index > next_package
				|| !memchr(entry->member_chain, '\0', sizeof(entry->member_chain))) {
			return sourceError(err, err_cap, "invalid or unsorted Grid asset identity", NULL);
		}
		if (entry->fiesta_pool && (entry->fiesta_pool != 1 || entry->asset.type != ASSET_WEAPON)) {
			return sourceError(err, err_cap, "invalid Grid Fiesta weapon membership", entry->asset.id);
		}
		fiesta_count += entry->fiesta_pool;
		if (entry->package_index == next_package) next_package++;
		const grid_match_source_package_t *package = &plan->packages[entry->package_index];
		const char *chain = entry->member_chain;
		if (chain[0]) {
			if (!sourceMemberChainMatches(chain, entry->asset.type)) {
				return sourceError(err, err_cap, "invalid typed member chain", entry->asset.id);
			}
		} else if (strcmp(entry->asset.id, package->owner.id) != 0
				|| !sourceArchiveTypeMatches(entry->asset.type, package->owner.type)) {
			return sourceError(err, err_cap, "asset does not name its owning archive", entry->asset.id);
		}
	}
	if (next_package != plan->package_count) {
		return sourceError(err, err_cap, "Grid manifest contains unused packages", NULL);
	}
	if (fiesta_count > GRID_MATCH_SOURCE_FIESTA_MAX) {
		return sourceError(err, err_cap, "Grid Fiesta pool has too many weapons", NULL);
	}
	plan->total_bytes = total;
	sourceDigest(plan);
	return 1;
}

static s32 sourcePlanIsSealed(const grid_match_sources_t *plan, char *err, size_t err_cap)
{
	if (!plan) return sourceError(err, err_cap, "missing Grid source plan", NULL);
	grid_match_sources_t sealed = *plan;
	if (!gridMatchSourcesSeal(&sealed, err, err_cap)) return 0;
	if (sealed.total_bytes != plan->total_bytes || memcmp(sealed.digest, plan->digest, sizeof(sealed.digest)) != 0) {
		return sourceError(err, err_cap, "Grid source plan seal does not match its rows", NULL);
	}
	for (size_t i = 0; i < plan->package_count; i++) {
		const grid_match_source_package_t *package = &plan->packages[i];
		if (!memchr(package->path, '\0', sizeof(package->path))) {
			return sourceError(err, err_cap, "invalid Grid archive locator", package->owner.id);
		}
	}
	return 1;
}

size_t gridMatchSourcesFiestaCount(const grid_match_sources_t *plan)
{
	if (!plan || !plan->entries || plan->entry_count > MANIFEST_MAX_ENTRIES) return 0;
	size_t count = 0;
	for (size_t i = 0; i < plan->entry_count; i++) {
		count += plan->entries[i].fiesta_pool == 1;
	}
	return count;
}

s32 gridMatchSourcesSetFiestaPool(grid_match_sources_t *plan, const grid_match_source_root_t *roots,
	size_t count, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if ((count && !roots) || count > GRID_MATCH_SOURCE_FIESTA_MAX) {
		return sourceError(err, err_cap, "invalid Grid Fiesta pool size", NULL);
	}
	if (!sourcePlanIsSealed(plan, err, err_cap)) return 0;

	size_t indices[GRID_MATCH_SOURCE_FIESTA_MAX];
	for (size_t i = 0; i < count; i++) {
		const grid_match_source_root_t *root = &roots[i];
		if (root->type != ASSET_WEAPON || !sourceCatalogIdIsCanonical(root->id)) {
			return sourceError(err, err_cap, "invalid Grid Fiesta weapon identity", NULL);
		}
		const grid_match_source_entry_t key = { .asset = *root };
		const grid_match_source_entry_t *entry = bsearch(&key, plan->entries, plan->entry_count,
			sizeof(plan->entries[0]), sourceCompare);
		if (!entry || entry->asset.type != ASSET_WEAPON) {
			return sourceError(err, err_cap, "Grid Fiesta weapon is absent from the exact source plan", root->id);
		}
		const size_t index = (size_t)(entry - plan->entries);
		for (size_t j = 0; j < i; j++) {
			if (indices[j] == index) {
				return sourceError(err, err_cap, "duplicate Grid Fiesta weapon", root->id);
			}
		}
		indices[i] = index;
	}

	for (size_t i = 0; i < plan->entry_count; i++) plan->entries[i].fiesta_pool = 0;
	for (size_t i = 0; i < count; i++) plan->entries[indices[i]].fiesta_pool = 1;
	sourceDigest(plan);
	return 1;
}

static s32 sourcePackageMatches(const grid_match_source_package_t *a, const grid_match_source_package_t *b)
{
	return a->owner.type == b->owner.type && strcmp(a->owner.id, b->owner.id) == 0
		&& strcmp(a->suffix, b->suffix) == 0 && a->bundled == b->bundled && a->size == b->size
		&& memcmp(a->digest, b->digest, sizeof(a->digest)) == 0;
}

static s32 sourceEntryMatches(const grid_match_sources_t *a, size_t i, const grid_match_sources_t *b, size_t j)
{
	const grid_match_source_entry_t *left = &a->entries[i];
	const grid_match_source_entry_t *right = &b->entries[j];
	return left->asset.type == right->asset.type && strcmp(left->member_chain, right->member_chain) == 0
		&& sourcePackageMatches(&a->packages[left->package_index], &b->packages[right->package_index]);
}

s32 gridMatchSourcesExactSubset(const grid_match_sources_t *subset, const grid_match_sources_t *whole,
		char *err, size_t err_cap)
{
	if (!sourcePlanIsSealed(subset, err, err_cap) || !sourcePlanIsSealed(whole, err, err_cap)) return 0;
	size_t j = 0;
	for (size_t i = 0; i < subset->entry_count; i++) {
		const char *id = subset->entries[i].asset.id;
		while (j < whole->entry_count && strcmp(whole->entries[j].asset.id, id) < 0) j++;
		if (j == whole->entry_count || strcmp(whole->entries[j].asset.id, id) != 0) {
			return sourceError(err, err_cap, "asset is absent from the Grid source set", id);
		}
		if (!sourceEntryMatches(subset, i, whole, j)) {
			return sourceError(err, err_cap, "asset has a different exact Grid source", id);
		}
		if (subset->entries[i].fiesta_pool && !whole->entries[j].fiesta_pool) {
			return sourceError(err, err_cap, "asset is absent from the exact Grid Fiesta pool", id);
		}
		j++;
	}
	return 1;
}

static s32 sourceUnionPackageIndex(grid_match_sources_t *plan, const grid_match_source_package_t *source,
		char *err, size_t err_cap)
{
	for (size_t i = 0; i < plan->package_count; i++) {
		grid_match_source_package_t *package = &plan->packages[i];
		if (strcmp(package->owner.id, source->owner.id) != 0
				&& memcmp(package->digest, source->digest, sizeof(package->digest)) != 0) continue;
		if (!sourcePackageMatches(package, source)) {
			sourceError(err, err_cap, "conflicting Grid archive ownership", source->owner.id);
			return -1;
		}
		if (!package->path[0]) strcpy(package->path, source->path);
		return (s32)i;
	}
	size_t i = plan->package_count++;
	plan->packages[i] = *source;
	return (s32)i;
}

s32 gridMatchSourcesCombine(grid_match_sources_t *out, const grid_match_sources_t *gameplay,
		const grid_match_sources_t *arena, char *err, size_t err_cap)
{
	if (!out) return sourceError(err, err_cap, "missing Grid source plan output", NULL);
	if (!sourcePlanIsSealed(gameplay, err, err_cap) || !sourcePlanIsSealed(arena, err, err_cap)) return 0;
	size_t capacity = gameplay->entry_count + arena->entry_count;
	if (capacity > MANIFEST_MAX_ENTRIES) capacity = MANIFEST_MAX_ENTRIES;
	grid_match_sources_t candidate = {0};
	candidate.entries = calloc(capacity, sizeof(*candidate.entries));
	candidate.packages = calloc(capacity, sizeof(*candidate.packages));
	if (!candidate.entries || !candidate.packages) {
		sourceError(err, err_cap, "out of memory combining Grid sources", NULL);
		goto refused;
	}

	/* Merge the sealed ID order. Allocate package indices at first use in that
	 * order, independent of either input plan's package numbering. */
	size_t i = 0, j = 0;
	while (i < gameplay->entry_count || j < arena->entry_count) {
		const s32 order = i == gameplay->entry_count ? 1 : j == arena->entry_count ? -1
			: strcmp(gameplay->entries[i].asset.id, arena->entries[j].asset.id);
		const grid_match_sources_t *source = order <= 0 ? gameplay : arena;
		const grid_match_source_entry_t *entry = &source->entries[order <= 0 ? i : j];
		const grid_match_source_package_t *package = &source->packages[entry->package_index];
		if (!order) {
			if (!sourceEntryMatches(gameplay, i, arena, j)) {
				sourceError(err, err_cap, "conflicting exact Grid asset source", entry->asset.id);
				goto refused;
			}
			if (!package->path[0]) package = &arena->packages[arena->entries[j].package_index];
		}
		if (candidate.entry_count == capacity) {
			sourceError(err, err_cap, "combined Grid source set has too many assets", entry->asset.id);
			goto refused;
		}
		const s32 package_index = sourceUnionPackageIndex(&candidate, package, err, err_cap);
		if (package_index < 0) goto refused;
		grid_match_source_entry_t *merged = &candidate.entries[candidate.entry_count++];
		*merged = *entry;
		merged->package_index = (size_t)package_index;
		if (!order) merged->fiesta_pool |= arena->entries[j].fiesta_pool;
		if (order <= 0) i++;
		if (order >= 0) j++;
	}
	if (!gridMatchSourcesSeal(&candidate, err, err_cap)) goto refused;
	gridMatchSourcesFree(out);
	*out = candidate;
	return 1;

refused:
	gridMatchSourcesFree(&candidate);
	return 0;
}

static s32 sourceCanonicalizeExpandedPlan(grid_match_sources_t *plan, char *err, size_t err_cap)
{
	grid_match_source_package_t *packages = calloc(plan->package_count, sizeof(*packages));
	if (!packages) return sourceError(err, err_cap, "out of memory ordering Grid match archives", NULL);

	qsort(plan->entries, plan->entry_count, sizeof(*plan->entries), sourceCompare);
	u16 by_first_use[MANIFEST_MAX_ENTRIES];
	memset(by_first_use, 0xff, sizeof(by_first_use));
	u16 count = 0;
	for (size_t i = 0; i < plan->entry_count; ++i) {
		grid_match_source_entry_t *entry = &plan->entries[i];
		u16 *index = &by_first_use[entry->package_index];
		if (*index == 0xffff) {
			*index = count++;
			packages[*index] = plan->packages[entry->package_index];
		}
		entry->package_index = *index;
	}
	free(plan->packages);
	plan->packages = packages;
	return 1;
}

static s32 sourceVerifyCompletedOwners(const grid_match_sources_t *plan,
	u8 *required, char *err, size_t err_cap)
{
	for (size_t i = 0; i < plan->entry_count; ++i) {
		const grid_match_source_entry_t *entry = &plan->entries[i];
		const grid_match_source_package_t *package = &plan->packages[entry->package_index];
		if (!entry->member_chain[0] && entry->asset.type == package->owner.type
				&& strcmp(entry->asset.id, package->owner.id) == 0) {
			required[entry->package_index] = 0;
		}
	}
	for (size_t i = 0; i < plan->package_count; ++i) {
		if (required[i]) {
			return sourceError(err, err_cap, "Grid archive owner does not resolve to the same package",
				plan->packages[i].owner.id);
		}
	}
	return 1;
}

s32 gridMatchSourcesBuildWithModelTexturesAndEdges(grid_match_sources_t *out,
	const grid_match_source_root_t *roots, size_t root_count,
	grid_match_source_resolve_fn resolve, grid_match_source_archive_fn inspect,
	grid_match_source_model_fn inspect_model, void *userdata,
	catalog_dep_activation_supplement_fn extra_edges, void *extra_data,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out || !roots || !root_count || root_count > MANIFEST_MAX_ENTRIES || !resolve || !inspect) {
		return sourceError(err, err_cap, "invalid match source request", NULL);
	}

	grid_match_sources_t candidate = {0};
	grid_match_source_review_t reviews = {
		.inspect = inspect_model, .userdata = userdata, .extra_edges = extra_edges, .extra_data = extra_data,
	};
	candidate.entries = calloc(MANIFEST_MAX_ENTRIES, sizeof(*candidate.entries));
	if (!candidate.entries) {
		sourceError(err, err_cap, "out of memory building match entries", NULL);
		goto refused;
	}
	for (size_t i = 0; i < root_count; i++) {
		if (!sourceAppendRoot(&candidate, &roots[i], resolve, userdata, &reviews, err, err_cap)) goto refused;
	}
	qsort(candidate.entries, candidate.entry_count, sizeof(*candidate.entries), sourceCompare);
	const size_t initial_entries = candidate.entry_count;
	size_t package_capacity = initial_entries;
	candidate.packages = calloc(package_capacity, sizeof(*candidate.packages));
	if (!candidate.packages) {
		sourceError(err, err_cap, "out of memory building match archives", NULL);
		goto refused;
	}
	u8 required_owners[MANIFEST_MAX_ENTRIES] = {0};
	for (size_t i = 0; i < candidate.entry_count; i++) {
		grid_match_source_archive_t archive = {0};
		grid_match_source_entry_t *entry = &candidate.entries[i];
		const grid_match_model_review_t *model = sourceFindModel(&reviews, entry->asset.id);
		if (model) {
			archive = model->archive;
		} else if (!inspect(&entry->asset, &archive, userdata, err, err_cap)) {
			goto refused;
		}
		if (!sourceAddArchive(&candidate, entry, &archive, err, err_cap)) goto refused;
		u8 *needs_owner = &required_owners[entry->package_index];
		if (!archive.complete_owner || archive.bundled || !entry->member_chain[0] || *needs_owner) continue;

		/* The loader registers the owning archive as a whole. Include its
		 * regular dependency closure without adding a child-to-owner edge. */
		*needs_owner = 1;
		if (!sourceAppendRoot(&candidate, &archive.owner, resolve, userdata, &reviews, err, err_cap)) goto refused;
		if (candidate.entry_count > package_capacity) {
			package_capacity *= 2;
			if (package_capacity > MANIFEST_MAX_ENTRIES) package_capacity = MANIFEST_MAX_ENTRIES;
			if (package_capacity < candidate.entry_count) package_capacity = candidate.entry_count;
			grid_match_source_package_t *packages = realloc(candidate.packages,
				package_capacity * sizeof(*packages));
			if (!packages) {
				sourceError(err, err_cap, "out of memory growing Grid match archives", NULL);
				goto refused;
			}
			candidate.packages = packages;
		}
	}
	if (!sourceVerifyCompletedOwners(&candidate, required_owners, err, err_cap)
			|| (candidate.entry_count != initial_entries && !sourceCanonicalizeExpandedPlan(&candidate, err, err_cap))
			|| !gridMatchSourcesSeal(&candidate, err, err_cap)) goto refused;

	sourceFreeModels(&reviews);
	gridMatchSourcesFree(out);
	*out = candidate;
	return 1;

refused:
	sourceFreeModels(&reviews);
	gridMatchSourcesFree(&candidate);
	return 0;
}

s32 gridMatchSourcesBuildWithModelTextures(grid_match_sources_t *out,
	const grid_match_source_root_t *roots, size_t root_count,
	grid_match_source_resolve_fn resolve, grid_match_source_archive_fn inspect,
	grid_match_source_model_fn inspect_model, void *userdata, char *err, size_t err_cap)
{
	return gridMatchSourcesBuildWithModelTexturesAndEdges(out, roots, root_count, resolve, inspect,
		inspect_model, userdata, NULL, NULL, err, err_cap);
}

s32 gridMatchSourcesBuild(grid_match_sources_t *out,
	const grid_match_source_root_t *roots, size_t root_count,
	grid_match_source_resolve_fn resolve, grid_match_source_archive_fn inspect,
	void *userdata, char *err, size_t err_cap)
{
	return gridMatchSourcesBuildWithModelTextures(out, roots, root_count, resolve, inspect,
		NULL, userdata, err, err_cap);
}
