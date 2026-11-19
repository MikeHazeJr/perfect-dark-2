#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "asset_archive_policy.h"
#include "assetcatalog_scanner.h"
#include "grid_match_cache.h"
#include "mod_asset_validate.h"
#include "net/net.h"

typedef struct grid_match_cache_blob {
	size_t refs;
	u64 expanded_bytes;
	u8 bytes[];
} grid_match_cache_blob_t;

typedef struct grid_match_cache_package {
	grid_match_source_package_t expected;
	grid_match_cache_blob_t *blob;
} grid_match_cache_package_t;

struct grid_match_cache {
	grid_match_offer_t offer;
	grid_match_source_entry_t *entries;
	size_t refs;
	size_t entry_count;
	size_t package_count;
	size_t accepted_count;
	u64 expanded_bytes;
	grid_match_cache_package_t packages[];
};

static s32 cacheError(char *err, size_t err_cap, const char *message)
{
	if (err && err_cap) snprintf(err, err_cap, "%s", message);
	return 0;
}

static s32 cacheMatchesOffer(const grid_match_cache_t *cache, const grid_match_offer_t *offer)
{
	return cache && offer && cache->offer.room_id == offer->room_id
		&& cache->offer.generation == offer->generation
		&& memcmp(cache->offer.manifest_digest, offer->manifest_digest, SHA256_HEX_SIZE) == 0;
}

s32 gridMatchCacheBegin(grid_match_cache_t **cache, const grid_match_offer_t *offer,
	const grid_match_sources_t *plan, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!cache || !offer || offer->room_id == 0xff || !offer->generation || !plan) {
		return cacheError(err, err_cap, "invalid Grid match offer or source plan");
	}
	if (*cache) {
		return cacheError(err, err_cap, "reset the current Grid match cache before starting a new offer");
	}

	grid_match_sources_t sealed = *plan;
	if (!gridMatchSourcesSeal(&sealed, err, err_cap)) return 0;
	if (sealed.total_bytes != plan->total_bytes
			|| memcmp(sealed.digest, plan->digest, SHA256_HEX_SIZE) != 0
			|| memcmp(sealed.digest, offer->manifest_digest, SHA256_HEX_SIZE) != 0) {
		return cacheError(err, err_cap, "Grid match rows differ from the offered source plan");
	}

	grid_match_cache_t *candidate = calloc(1, sizeof(*candidate)
		+ plan->package_count * sizeof(candidate->packages[0]));
	if (!candidate) return cacheError(err, err_cap, "out of memory preparing Grid match packages");

	size_t entry_bytes = plan->entry_count * sizeof(plan->entries[0]);
	candidate->entries = malloc(entry_bytes);
	if (!candidate->entries) {
		free(candidate);
		return cacheError(err, err_cap, "out of memory preparing Grid match assets");
	}
	memcpy(candidate->entries, plan->entries, entry_bytes);
	candidate->refs = 1;
	candidate->entry_count = plan->entry_count;
	candidate->offer = *offer;
	candidate->package_count = plan->package_count;
	for (size_t i = 0; i < candidate->package_count; i++) {
		candidate->packages[i].expected = plan->packages[i];
	}
	*cache = candidate;
	return 1;
}

static s32 cacheMatchesCatalogId(const void *bytes, u32 size, const char *archive_name,
		const char *catalog_id, ini_section_t *ini)
{
	u32 descriptor_size = 0;
	char *descriptor = assetArchiveExtractDescriptorMemAlloc(bytes, size, archive_name,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &descriptor_size, NULL);
	s32 matches = descriptor && iniParseText(descriptor, descriptor_size, ini, NULL, 0)
		&& strcmp(iniGet(ini, "catalog_id", ""), catalog_id) == 0;
	free(descriptor);
	return matches;
}

static s32 cacheMatchesMember(const void *bytes, u32 size, const grid_match_source_entry_t *entry,
		ini_section_t *ini)
{
	char name[FS_MAXPATH + 1];
	void *allocated = NULL;
	const char *part = entry->member_chain;
	while (*part) {
		const char *end = strstr(part, "::");
		size_t length = end ? (size_t)(end - part) : strlen(part);
		memcpy(name, part, length);
		name[length] = '\0';
		void *nested = modArchiveExtractMemAlloc(bytes, size, name, &size);
		free(allocated);
		if (!nested) return 0;
		allocated = nested;
		bytes = allocated;
		part = end ? end + 2 : "";
	}
	s32 matches = cacheMatchesCatalogId(bytes, size, name, entry->asset.id, ini);
	free(allocated);
	return matches;
}

static s32 cacheValidateAssets(const grid_match_cache_t *cache, size_t package_index,
		const void *bytes, u32 size, char *err, size_t err_cap)
{
	const grid_match_source_package_t *package = &cache->packages[package_index].expected;
	ini_section_t *ini = malloc(sizeof(*ini));
	if (!ini) return cacheError(err, err_cap, "out of memory validating Grid match assets");
	if (!cacheMatchesCatalogId(bytes, size, package->suffix, package->owner.id, ini)) {
		free(ini);
		return cacheError(err, err_cap, "Grid match archive has a different owning catalog ID");
	}
	for (size_t i = 0; i < cache->entry_count; i++) {
		const grid_match_source_entry_t *entry = &cache->entries[i];
		if (entry->package_index == package_index && entry->member_chain[0]
				&& !cacheMatchesMember(bytes, size, entry, ini)) {
			if (err && err_cap) snprintf(err, err_cap, "Grid match archive lacks exact typed asset %s", entry->asset.id);
			free(ini);
			return 0;
		}
	}
	free(ini);
	return 1;
}

typedef struct grid_archive_budget {
	const void *archive;
	u64 *remaining;
	u32 size;
	s32 depth;
	s32 valid;
} grid_archive_budget_t;

static s32 cacheMeasureArchive(const void *archive, u32 size, s32 depth, u64 *remaining);

static s32 cacheMeasureMember(const char *name, u32 size, void *userdata)
{
	grid_archive_budget_t *budget = userdata;
	if (size > *budget->remaining) {
		budget->valid = 0;
		return 1;
	}
	*budget->remaining -= size;
	if (!assetArchivePathIsTyped(name)) return 0;
	if (budget->depth == MOD_ASSET_VALIDATE_MAX_DEPTH) {
		budget->valid = 0;
		return 1;
	}
	u32 nested_size = 0;
	void *nested = modArchiveExtractMemAlloc(budget->archive, budget->size, name, &nested_size);
	budget->valid = nested && nested_size == size
		&& cacheMeasureArchive(nested, nested_size, budget->depth + 1, budget->remaining);
	free(nested);
	return !budget->valid;
}

static s32 cacheMeasureArchive(const void *archive, u32 size, s32 depth, u64 *remaining)
{
	grid_archive_budget_t budget = { .archive = archive, .remaining = remaining,
		.size = size, .depth = depth, .valid = 1 };
	return modArchiveMemForEachEntry(archive, size, cacheMeasureMember, &budget) == MODARCHIVE_OK
		&& budget.valid;
}

s32 gridMatchCacheCheckArchiveBudget(const void *bytes, u32 size, char *err, size_t err_cap)
{
	u64 remaining = NET_DISTRIB_MAX_SESSION;
	if (err && err_cap) err[0] = '\0';
	if (!bytes || !size || size > NET_DISTRIB_MAX_COMP
			|| !cacheMeasureArchive(bytes, size, 0, &remaining)) {
		return cacheError(err, err_cap, "Grid archive is invalid or exceeds the package or expansion budget");
	}
	return 1;
}

s32 gridMatchCacheAccept(grid_match_cache_t *cache, const grid_match_offer_t *offer,
	size_t package_index, const void *bytes, u32 size, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!cacheMatchesOffer(cache, offer) || package_index >= cache->package_count) {
		return cacheError(err, err_cap, "Grid match package does not belong to this offer");
	}

	grid_match_cache_package_t *package = &cache->packages[package_index];
	if (!bytes || size != package->expected.size) {
		return cacheError(err, err_cap, "Grid match package size differs from the source plan");
	}
	if (package->blob) {
		if (memcmp(package->blob->bytes, bytes, size) == 0) return 1;
		return cacheError(err, err_cap, "Grid match package bytes changed after validation");
	}

	grid_match_cache_blob_t *blob = malloc(sizeof(*blob) + size);
	if (!blob) return cacheError(err, err_cap, "out of memory staging a Grid match package");
	memcpy(blob->bytes, bytes, size);

	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(blob->bytes, size, digest);
	if (memcmp(digest, package->expected.digest, sizeof(digest)) != 0) {
		free(blob);
		return cacheError(err, err_cap, "Grid match package digest differs from the source plan");
	}
	u64 remaining = NET_DISTRIB_MAX_SESSION - cache->expanded_bytes;
	if (!cacheMeasureArchive(blob->bytes, size, 0, &remaining)) {
		free(blob);
		return cacheError(err, err_cap, "Grid match archive is invalid or exceeds the expanded session budget");
	}
	if (modAssetValidateArchiveBytes(blob->bytes, size, package->expected.suffix, err, err_cap)
			!= MOD_ASSET_VALIDATE_OK
			|| !cacheValidateAssets(cache, package_index, blob->bytes, size, err, err_cap)) {
		free(blob);
		return 0;
	}

	blob->refs = 1;
	blob->expanded_bytes = NET_DISTRIB_MAX_SESSION - remaining - cache->expanded_bytes;
	package->blob = blob;
	cache->expanded_bytes = NET_DISTRIB_MAX_SESSION - remaining;
	cache->accepted_count++;
	return 1;
}

s32 gridMatchCacheReady(const grid_match_cache_t *cache, const grid_match_offer_t *offer)
{
	return cacheMatchesOffer(cache, offer) && cache->accepted_count == cache->package_count;
}

static s32 cachePackageMatches(const grid_match_source_package_t *entry,
		const grid_match_source_package_t *source)
{
	return entry->size == source->size && entry->owner.type == source->owner.type
		&& entry->bundled == source->bundled && strcmp(entry->owner.id, source->owner.id) == 0
		&& strcmp(entry->suffix, source->suffix) == 0
		&& memcmp(entry->digest, source->digest, sizeof(entry->digest)) == 0;
}

static const grid_match_cache_package_t *cacheFindPackage(const grid_match_cache_t *cache,
		const grid_match_source_package_t *source)
{
	for (size_t i = 0; i < cache->package_count; ++i) {
		if (cachePackageMatches(&cache->packages[i].expected, source)) return &cache->packages[i];
	}
	return NULL;
}

static int cacheCompareAssetId(const void *key, const void *row)
{
	const grid_match_source_entry_t *entry = row;
	return strcmp(key, entry->asset.id);
}

static s32 cacheProvesAsset(const grid_match_cache_t *cache, const grid_match_source_entry_t *source,
		const grid_match_source_package_t *package)
{
	const grid_match_source_entry_t *entry = bsearch(source->asset.id, cache->entries, cache->entry_count,
		sizeof(cache->entries[0]), cacheCompareAssetId);
	return entry && entry->asset.type == source->asset.type
		&& (!source->fiesta_pool || entry->fiesta_pool)
		&& strcmp(entry->member_chain, source->member_chain) == 0
		&& cachePackageMatches(&cache->packages[entry->package_index].expected, package);
}

static s32 cacheBindCombinedPackages(const grid_match_sources_t *plan,
		const grid_match_cache_t *first, const grid_match_cache_t *second,
		grid_match_cache_t *candidate, char *err, size_t err_cap)
{
	u64 remaining = NET_DISTRIB_MAX_SESSION;
	for (size_t i = 0; i < plan->package_count; ++i) {
		const grid_match_source_package_t *source = &plan->packages[i];
		const grid_match_cache_package_t *cached = cacheFindPackage(first, source);
		if (!cached) cached = cacheFindPackage(second, source);
		grid_match_cache_blob_t *blob = cached ? cached->blob : NULL;
		if (!blob) return cacheError(err, err_cap, "a combined Grid package is unavailable");
		if (blob->expanded_bytes > remaining) {
			return cacheError(err, err_cap, "combined Grid packages exceed the expanded session budget");
		}
		remaining -= blob->expanded_bytes;
		if (candidate) {
			if (blob->refs == SIZE_MAX) return cacheError(err, err_cap, "too many references to a Grid package");
			if (!memchr(source->path, '\0', sizeof(source->path))) {
				return cacheError(err, err_cap, "invalid combined Grid archive locator");
			}
			blob->refs++;
			candidate->packages[i].blob = blob;
			candidate->accepted_count++;
		}
	}
	if (candidate) candidate->expanded_bytes = NET_DISTRIB_MAX_SESSION - remaining;
	return 1;
}

s32 gridMatchCacheValidateCombinedBudget(const grid_match_sources_t *plan,
	const grid_match_cache_t *first, const grid_match_offer_t *first_offer,
	const grid_match_cache_t *second, const grid_match_offer_t *second_offer,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!plan || !gridMatchCacheReady(first, first_offer) || !gridMatchCacheReady(second, second_offer)) {
		return cacheError(err, err_cap, "the combined plan needs two ready Grid donor caches");
	}
	grid_match_sources_t sealed = *plan;
	if (!gridMatchSourcesSeal(&sealed, err, err_cap)) return 0;
	if (sealed.total_bytes != plan->total_bytes || memcmp(sealed.digest, plan->digest, sizeof(sealed.digest)) != 0) {
		return cacheError(err, err_cap, "the combined Grid plan does not match its seal");
	}
	return cacheBindCombinedPackages(plan, first, second, NULL, err, err_cap);
}

s32 gridMatchCacheCompose(grid_match_cache_t **out, const grid_match_offer_t *offer,
		const grid_match_sources_t *plan, const grid_match_cache_t *first, const grid_match_offer_t *first_offer,
		const grid_match_cache_t *second, const grid_match_offer_t *second_offer, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out || *out) return cacheError(err, err_cap, "an empty output is required for a combined Grid cache");
	if (!offer || !gridMatchCacheReady(first, first_offer) || !gridMatchCacheReady(second, second_offer)
			|| first_offer->room_id != offer->room_id || second_offer->room_id != offer->room_id) {
		return cacheError(err, err_cap, "the combined cache needs two ready Grid donors from the same room");
	}

	grid_match_cache_t *candidate = NULL;
	if (!gridMatchCacheBegin(&candidate, offer, plan, err, err_cap)) return 0;
	for (size_t i = 0; i < candidate->entry_count; ++i) {
		const grid_match_source_entry_t *entry = &candidate->entries[i];
		const grid_match_source_package_t *package = &candidate->packages[entry->package_index].expected;
		if (!cacheProvesAsset(first, entry, package) && !cacheProvesAsset(second, entry, package)) {
			if (err && err_cap) snprintf(err, err_cap, "no Grid donor verified the exact typed asset %s", entry->asset.id);
			gridMatchCacheReset(&candidate);
			return 0;
		}
	}
	if (!cacheBindCombinedPackages(plan, first, second, candidate, err, err_cap)) {
		gridMatchCacheReset(&candidate);
		return 0;
	}
	*out = candidate;
	return 1;
}

s32 gridMatchCacheRetainReady(grid_match_cache_t **out, grid_match_cache_t *ready,
		const grid_match_offer_t *offer, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out || *out) return cacheError(err, err_cap, "an empty output is required to retain a Grid cache");
	if (!gridMatchCacheReady(ready, offer)) return cacheError(err, err_cap, "no ready Grid cache matches this offer");
	if (ready->refs == SIZE_MAX) return cacheError(err, err_cap, "too many references to a Grid cache");
	ready->refs++;
	*out = ready;
	return 1;
}

s32 gridMatchCacheCopySources(const grid_match_cache_t *cache, const grid_match_offer_t *offer,
		grid_match_sources_t *out, char *err, size_t err_cap)
{
	if (!out || !gridMatchCacheReady(cache, offer)) {
		return cacheError(err, err_cap, "no sealed source plan for the active Grid offer");
	}

	grid_match_sources_t copy = {
		.entry_count = cache->entry_count,
		.package_count = cache->package_count,
	};
	const size_t entry_bytes = copy.entry_count * sizeof(copy.entries[0]);
	copy.entries = malloc(entry_bytes);
	copy.packages = malloc(copy.package_count * sizeof(copy.packages[0]));
	if (!copy.entries || !copy.packages) {
		gridMatchSourcesFree(&copy);
		return cacheError(err, err_cap, "out of memory copying sealed Grid source rows");
	}
	memcpy(copy.entries, cache->entries, entry_bytes);
	for (size_t i = 0; i < copy.package_count; i++) {
		copy.packages[i] = cache->packages[i].expected;
	}
	if (!gridMatchSourcesSeal(&copy, err, err_cap)) {
		gridMatchSourcesFree(&copy);
		return 0;
	}
	if (memcmp(copy.digest, offer->manifest_digest, SHA256_HEX_SIZE) != 0) {
		gridMatchSourcesFree(&copy);
		return cacheError(err, err_cap, "cached source rows differ from the active Grid offer");
	}
	gridMatchSourcesFree(out);
	*out = copy;
	return 1;
}

const grid_match_source_package_t *gridMatchCachePackage(const grid_match_cache_t *cache,
		const grid_match_offer_t *offer, size_t package_index)
{
	if (!gridMatchCacheReady(cache, offer) || package_index >= cache->package_count) return NULL;
	return &cache->packages[package_index].expected;
}

const void *gridMatchCacheRead(const grid_match_cache_t *cache,
	const grid_match_offer_t *offer, size_t package_index, u32 *out_size)
{
	if (out_size) *out_size = 0;
	if (!gridMatchCacheReady(cache, offer) || package_index >= cache->package_count) return NULL;
	const grid_match_cache_package_t *package = &cache->packages[package_index];
	if (out_size) *out_size = package->expected.size;
	return package->blob->bytes;
}

void gridMatchCacheReset(grid_match_cache_t **cache)
{
	if (!cache || !*cache) return;
	grid_match_cache_t *released = *cache;
	*cache = NULL;
	if (--released->refs) return;
	for (size_t i = 0; i < released->package_count; i++) {
		grid_match_cache_blob_t *blob = released->packages[i].blob;
		if (blob && --blob->refs == 0) free(blob);
	}
	free(released->entries);
	free(released);
}
