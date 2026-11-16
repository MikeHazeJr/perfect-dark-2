/*
 * romextract_pdai.c -- the global AI lists as the base asset base:ai_global.
 *
 * Global AI lists slice 2, step 2 (context/designs/modding/global-ai-lists.md):
 * the binary's global AI lists (g_GlobalAilists, src/game/gailists.c) are
 * written as one typed archive, ai/base_ai_global.pdai in the data folder:
 *
 *   ai.ini                  [ai_lists] catalog_id = base:ai_global,
 *                           ai_lists_file = ai/global_ailists.json,
 *                           ai_lists_graph_file = ai/global_ailists.graph.json
 *   ai/global_ailists.json  the pd2.scenario.ai.lists.v1 rows a scenario's
 *                           ai/ailists.json holds (romExtractGlobalAiListsJson,
 *                           the writer the slice 1 round trip proved exact)
 *   ai/global_ailists.graph.json
 *                           the lists' control flow, pd2.scenario.ai.graph.v1:
 *                           one node per row (its graph_node id) and the next,
 *                           branch, goto and restart edges between them
 *                           (romExtractGlobalAiListsGraphJson,
 *                           context/designs/modding/ai-list-graphs.md); the
 *                           conformance tool recomputes them from the rows,
 *                           the runtime does not read them yet
 *   _meta/...               the archive metadata (pd_kind ai_lists)
 *
 * The rows come from the binary, not from the ROM, so the archive changes only
 * when the writer or the lists change: PDAI_FAST_CACHE_KIND names the writer's
 * output and must move with it (SP-86). Nothing reads the archive yet; the
 * runtime switch is step 3.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "asset_archive_writer.h"
#include "fs.h"
#include "modarchive.h"
#include "romextract_pd.h"
#include "system.h"

#define PDAI_OUT_DIR "ai"
#define PDAI_ARCHIVE_NAME "base_ai_global.pdai"
#define PDAI_CATALOG_ID "base:ai_global"
#define PDAI_ROWS_MEMBER "ai/global_ailists.json"
#define PDAI_GRAPH_MEMBER "ai/global_ailists.graph.json"
#define PDAI_EXTRACT_VERSION "global_ailists_rows_v3_typedargs"
#define PDAI_FAST_CACHE_KIND "pdai_global_ailists_rows_v3_typedargs"

#if !defined(PD_SERVER)
static s32 s_writePdaiArchive(const char *dst_full)
{
	u32 lists = 0;
	u32 commands = 0;
	u32 graph_nodes = 0;
	u32 graph_edges = 0;
	char *json = romExtractGlobalAiListsJson(&lists, &commands);
	char *graph = NULL;
	char ini[640];
	char manifest[768];
	int ini_len;
	int manifest_len;
	u32 json_size;
	u32 graph_size;
	mod_archive_writer_t *aw;
	asset_archive_writer_t writer;

	if (!json) {
		sysLoudFailf("EXTRACT.PDAI", "the global AI lists could not be written as rows");
		return -1;
	}
	json_size = (u32)strlen(json);
	/* ai-list-graphs.md: the lists' control-flow edges, from the same bytes. */
	graph = romExtractGlobalAiListsGraphJson(&graph_nodes, &graph_edges);
	if (!graph || graph_nodes != commands) {
		sysLoudFailf("EXTRACT.PDAI",
			"the global AI lists' graph could not be written (%u nodes for %u rows)",
			(unsigned)graph_nodes, (unsigned)commands);
		free(graph);
		free(json);
		return -1;
	}
	graph_size = (u32)strlen(graph);

	ini_len = snprintf(ini, sizeof(ini),
		"[ai_lists]\n"
		"catalog_id = %s\n"
		"ai_lists_file = %s\n"
		"ai_lists_graph_file = %s\n"
		"list_count = %u\n"
		"command_count = %u\n"
		"edge_count = %u\n"
		"extract_version = %s\n"
		"source = g_GlobalAilists\n",
		PDAI_CATALOG_ID, PDAI_ROWS_MEMBER, PDAI_GRAPH_MEMBER, (unsigned)lists,
		(unsigned)commands, (unsigned)graph_edges, PDAI_EXTRACT_VERSION);
	manifest_len = snprintf(manifest, sizeof(manifest),
		"{\n"
		"  \"pd_kind\": \"ai_lists\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"data\": \"%s\",\n"
		"  \"data_size\": %u,\n"
		"  \"list_count\": %u,\n"
		"  \"command_count\": %u,\n"
		"  \"extract_version\": \"%s\",\n"
		"  \"source_symbol\": \"g_GlobalAilists\"\n"
		"}\n",
		PDAI_CATALOG_ID, PDAI_ROWS_MEMBER, (unsigned)json_size,
		(unsigned)lists, (unsigned)commands, PDAI_EXTRACT_VERSION);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini)
			|| manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest)) {
		sysLoudFailf("EXTRACT.PDAI", "ai.ini or the manifest would be truncated");
		free(graph);
		free(json);
		return -1;
	}

	aw = modArchiveBeginDerived(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDAI", "modArchiveBegin failed for \"%s\"", dst_full);
		free(graph);
		free(json);
		return -1;
	}
	if (assetArchiveWriterInit(&writer, aw, "ai_lists", PDAI_CATALOG_ID) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDAI", "assetArchiveWriterInit failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		free(graph);
		free(json);
		return -1;
	}
	assetArchiveWriterSetProvenance(&writer, "romextract_pdai", "src/game/gailists.c", -1,
		"g_GlobalAilists");
	if (assetArchiveWriterAddDescriptor(&writer, "ai.ini", ini, (u32)ini_len) != MODARCHIVE_OK
			|| assetArchiveWriterAddManifestJson(&writer, manifest, (u32)manifest_len)
				!= MODARCHIVE_OK
			|| assetArchiveWriterAddPublicMem(&writer, PDAI_ROWS_MEMBER, json, json_size,
				"rows") != MODARCHIVE_OK
			|| assetArchiveWriterAddPublicMem(&writer, PDAI_GRAPH_MEMBER, graph,
				graph_size, "graph") != MODARCHIVE_OK
			|| assetArchiveWriterFinishMetadata(&writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDAI", "could not write the members of \"%s\"", dst_full);
		modArchiveAbort(aw);
		free(graph);
		free(json);
		return -1;
	}
	free(graph);
	free(json);
	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDAI", "modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}
	sysLogPrintf(LOG_NOTE,
		"romextract pdai: base:ai_global %u lists, %u commands, %u edges, %u bytes of rows",
		(unsigned)lists, (unsigned)commands, (unsigned)graph_edges,
		(unsigned)json_size);
	return 1;
}
#endif /* !PD_SERVER */

s32 romExtractAllPdai(s32 force_rewrite)
{
#if defined(PD_SERVER)
	(void)force_rewrite;
	return 0;
#else
	char data_dir_buf[FS_MAXPATH + 1];
	char ai_dir[FS_MAXPATH];
	char dst_full[FS_MAXPATH + 32];
	s32 force;
	s32 written = 0;
	s32 skipped = 0;

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDAI", "fsDataDirEnsure failed");
		return -1;
	}
	snprintf(ai_dir, sizeof(ai_dir), "%s/%s",
		fsDataDir(data_dir_buf, sizeof(data_dir_buf)), PDAI_OUT_DIR);
	if (!fsCreateDir(ai_dir)) {
		sysLoudFailf("EXTRACT.PDAI", "fsCreateDir(\"%s\") failed", ai_dir);
		return -1;
	}
	snprintf(dst_full, sizeof(dst_full), "%s/%s", ai_dir, PDAI_ARCHIVE_NAME);

	/* B-943: a changed kind forces one rewrite; an existing archive of the
	 * current kind is kept. */
	force = force_rewrite | romExtractPdFastCacheKindMismatch(PDAI_FAST_CACHE_KIND, ai_dir);
	if (!force && fsFileSize(dst_full) > 0) {
		skipped = 1;
	} else if (s_writePdaiArchive(dst_full) == 1) {
		written = 1;
	} else {
		sysLogPrintf(LOG_NOTE, "romextract pdai: written=0 skipped=0 failed=1 (out=%s)", ai_dir);
		return -1;
	}
	sysLogPrintf(LOG_NOTE, "romextract pdai: written=%d skipped=%d failed=0 (out=%s)",
		written, skipped, ai_dir);
	romExtractPdFastCacheWrite(PDAI_FAST_CACHE_KIND, ai_dir, ".pdai");
	return written;
#endif
}
