/**
 * loader_walker_mesh_source_bind.c -- stable catalog-row typed mesh binder.
 */

#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetprovider.h"
#include "fs.h"
#include "loader_walker_mesh_source.h"
#include "weapon_graph_archive.h"

static void meshSourceBindSetErr(char *err, size_t err_cap,
		const char *message, const char *detail)
{
	if (!err || err_cap == 0) return;
	snprintf(err, err_cap, "%s%s%s", message ? message : "",
		detail && detail[0] ? ": " : "", detail ? detail : "");
	err[err_cap - 1] = '\0';
}

static s32 meshSourceArchivePath(const char *source_path,
		char *out, size_t out_cap)
{
	const char *last = NULL;
	const char *cursor = source_path;
	if (!source_path || !source_path[0] || !out || out_cap == 0) return 0;
	while ((cursor = strstr(cursor, "::")) != NULL) {
		last = cursor;
		cursor += 2;
	}
	if (!last || last == source_path || (size_t)(last - source_path) >= out_cap) {
		return 0;
	}
	memcpy(out, source_path, (size_t)(last - source_path));
	out[last - source_path] = '\0';
	return 1;
}

s32 loaderWalkerMeshSourceMatchesEntry(
		const asset_entry_t *entry,
		const char *candidate_archive_path,
		const void *candidate_archive_bytes,
		u32 candidate_archive_size,
		char *err, size_t err_cap)
{
	const char *current_path;
	char current_archive[FS_MAXPATH + 1];
	char current_digest[SHA256_HEX_SIZE];
	char candidate_digest[SHA256_HEX_SIZE];

	if (err && err_cap) err[0] = '\0';
	if (!entry || entry->source.primary.provider != fileProvider()
			|| !candidate_archive_path || !candidate_archive_path[0]) {
		meshSourceBindSetErr(err, err_cap,
			"typed mesh source comparison is missing an archive", NULL);
		return 0;
	}
	current_path = fileProviderPath(entry->source.primary);
	if (!current_path || !meshSourceArchivePath(current_path,
			current_archive, sizeof(current_archive))) {
		meshSourceBindSetErr(err, err_cap,
			"existing typed mesh source has no containing archive", entry->id);
		return 0;
	}
	if (weaponGraphArchiveCanonicalSha256File(current_archive,
			current_digest) != 0) {
		meshSourceBindSetErr(err, err_cap,
			"could not hash existing typed mesh archive", current_archive);
		return 0;
	}
	if (candidate_archive_bytes && candidate_archive_size > 0) {
		if (weaponGraphArchiveCanonicalSha256Bytes(candidate_archive_bytes,
				candidate_archive_size, candidate_digest) != 0) {
			meshSourceBindSetErr(err, err_cap,
				"could not hash candidate typed mesh archive",
				candidate_archive_path);
			return 0;
		}
	} else if (weaponGraphArchiveCanonicalSha256File(candidate_archive_path,
			candidate_digest) != 0) {
		meshSourceBindSetErr(err, err_cap,
			"could not hash candidate typed mesh archive", candidate_archive_path);
		return 0;
	}
	if (strcmp(current_digest, candidate_digest) != 0) {
		meshSourceBindSetErr(err, err_cap,
			"divergent typed mesh archives claim one catalog ID", entry->id);
		return 0;
	}
	return 1;
}

s32 loaderWalkerBindMeshSource(
		asset_entry_t *entry,
		const loader_walker_mesh_source_plan_t *plan,
		s32 reject_live_source_change,
		char *err, size_t err_cap)
{
	const char *current_path = NULL;
	asset_data_handle_t handle;

	if (err && err_cap) err[0] = '\0';
	if (!entry || entry->type != ASSET_MODEL || !plan
			|| !plan->source_path[0]) {
		meshSourceBindSetErr(err, err_cap, "invalid mesh source bind", NULL);
		return 0;
	}
	if (entry->source_filenum > 0 && plan->source_filenum > 0
			&& entry->source_filenum != plan->source_filenum) {
		meshSourceBindSetErr(err, err_cap,
			"mesh source filenum conflicts with stable catalog row", entry->id);
		return 0;
	}
	if (entry->source.primary.provider == fileProvider()) {
		current_path = fileProviderPath(entry->source.primary);
		if (!current_path) {
			meshSourceBindSetErr(err, err_cap,
				"existing typed mesh FileProvider handle is invalid", entry->id);
			return 0;
		}
	}
	if (current_path && strcmp(current_path, plan->source_path) == 0) {
		if (plan->source_filenum > 0) {
			entry->source_filenum = plan->source_filenum;
		}
		return 1;
	}
	if (current_path) {
		if (!loaderWalkerMeshSourceMatchesEntry(entry, plan->archive_path,
				NULL, 0, err, err_cap)) return 0;
		/* Equal public archives share one stable first owner. Only provenance
		 * is merged; registration order never churns the provider path. */
		if (plan->source_filenum > 0) {
			entry->source_filenum = plan->source_filenum;
		}
		return 1;
	}
	if (entry->source.primary.provider
			&& entry->source.primary.provider != romProvider()) {
		meshSourceBindSetErr(err, err_cap,
			"unsupported provider already owns mesh source", entry->id);
		return 0;
	}
	if (reject_live_source_change
			&& !loaderWalkerMeshSourceChangeAllowed(entry->load_state,
				entry->bundled, entry->ref_count, entry->loaded_data != NULL,
				entry->payload_kind)) {
		meshSourceBindSetErr(err, err_cap,
			"cannot change a resident or active mesh source", entry->id);
		return 0;
	}
	handle = catalogHandleForSourceFile(plan->source_path);
	if (assetHandleIsNull(handle)) {
		meshSourceBindSetErr(err, err_cap,
			"could not bind typed mesh FileProvider source", plan->source_path);
		return 0;
	}
	catalogSetPrimary(entry, handle);
	if (plan->source_filenum > 0) {
		entry->source_filenum = plan->source_filenum;
	}
	return 1;
}
