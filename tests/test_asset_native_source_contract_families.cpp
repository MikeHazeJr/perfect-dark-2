/*
 * Static pins for c3842 and c3844, split out of
 * tests/test_asset_native_source_contract.cpp for build time (T-TOOLING-018):
 * source-generated model, text and family contracts, c3841/c3843 archives, c3849 slices and the later family pins.
 */

#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

/* c3849 Wave 4 Slice B: the ONE shared catalog-id -> filename slug
 * (header-only static inline), unit-tested behaviorally below. */
#include "assetcatalog_slug.h"

namespace {

std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	std::string text = ss.str();
	/* Checkout line endings are not a source contract. Preserve every other
	 * byte, including lone carriage returns, for the existing content pins. */
	std::size_t write = 0;
	for (std::size_t read = 0; read < text.size(); ++read) {
		if (text[read] == '\r' && read + 1 < text.size() && text[read + 1] == '\n') continue;
		text[write++] = text[read];
	}
	text.resize(write);
	return text;
}

std::string functionBlock(const std::string &text, const std::string &name)
{
	const size_t start = text.find(name);
	if (start == std::string::npos) {
		return "";
	}
	const size_t brace = text.find('{', start);
	if (brace == std::string::npos) {
		return "";
	}
	int depth = 0;
	for (size_t i = brace; i < text.size(); i++) {
		if (text[i] == '{') {
			depth++;
		} else if (text[i] == '}') {
			depth--;
			if (depth == 0) {
				return text.substr(start, i - start + 1);
			}
		}
	}
	return "";
}

void requireTokenOrder(const std::string &text, const char *first,
	const char *second)
{
	const size_t first_pos = text.find(first);
	const size_t second_pos = text.find(second);
	INFO("Expected '" << first << "' before '" << second << "'");
	REQUIRE(first_pos != std::string::npos);
	REQUIRE(second_pos != std::string::npos);
	REQUIRE(first_pos < second_pos);
}

size_t countOccurrences(const std::string &text, const char *needle)
{
	size_t count = 0;
	size_t pos = 0;

	while ((pos = text.find(needle, pos)) != std::string::npos) {
		count++;
		pos += strlen(needle);
	}

	return count;
}

}

TEST_CASE("source-generated windowed doors guard missing legacy toggle nodes",
          "[modding][pdxxx][c3844][source][static][b389]") {
	const std::string propobj = readTextFile("src/game/propobj.c");

	REQUIRE(propobj.find("#include \"model_rodata_guard.h\"") !=
	        std::string::npos);
	REQUIRE(propobj.find("doorGetWindowedDoorToggleRwData") !=
	        std::string::npos);
	REQUIRE(propobj.find("modelGetPart(model->definition, MODELPART_WINDOWEDDOOR_0001)") !=
	        std::string::npos);
	REQUIRE(propobj.find("modelRodataIsReadable(node->rodata") !=
	        std::string::npos);
	REQUIRE(propobj.find("WindowedDoor.portal-toggle") !=
	        std::string::npos);
	REQUIRE(propobj.find("WindowedDoor.destroy-toggle") !=
	        std::string::npos);
	REQUIRE(propobj.find("if (!rwdata || !rwdata->toggle.visible)") !=
	        std::string::npos);
}

TEST_CASE("custom body/head assembly chain stays source-backed through the private slot bridge",
          "[modding][pdxxx][c3844][source][static][bodyhead]") {
	const std::string body_walker = readTextFile("port/src/loader_walker_body.c");
	const std::string head_walker = readTextFile("port/src/loader_walker_head.c");
	const std::string api = readTextFile("port/src/assetcatalog_api.c");
	const std::string catalog = readTextFile("port/src/assetcatalog.c");
	const std::string body_mgr = readTextFile("port/src/catalog_mgr_bodies.c");
	const std::string head_mgr = readTextFile("port/src/catalog_mgr_heads.c");
	const std::string body_runtime = readTextFile("src/game/body.c");
	const std::string modmgr = readTextFile("port/src/modmgr.c");
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string netdistrib = readTextFile("port/src/net/netdistrib.c");
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string types = readTextFile("src/include/types.h");

	/* Both ordinary walkers use the same public parser, checked private slot,
	 * typed mesh binding and native-pool publication as mod scanner ingress. */
	REQUIRE(body_walker.find("assetCatalogRegisterBodyHeadArchive") != std::string::npos);
	REQUIRE(head_walker.find("assetCatalogRegisterBodyHeadArchive") != std::string::npos);
	const std::string shared = functionBlock(scanner, "static s32 registerBodyHeadPublicIni");
	requireTokenOrder(shared, "bodyHeadSourceParseIni", "externalScanTransactionBegin");
	requireTokenOrder(shared, "assetCatalogResolveBodyPrivateSlot(id)", "bodyHeadSourceBindMesh");
	requireTokenOrder(shared, "assetCatalogResolveHeadPrivateSlot(id)", "bodyHeadSourceBindMesh");
	requireTokenOrder(shared, "bodyHeadSourceBindMesh", "assetCatalogRegister(id, type)");
	requireTokenOrder(shared, "entry->runtime_index = slot", "loaderPoolInstallPublicBody(&body)");
	REQUIRE(shared.find("loaderPoolInstallPublicHead(&head)") != std::string::npos);
	REQUIRE(shared.find("externalScanTransactionRollback") != std::string::npos);
	REQUIRE(body_walker.find("loaderPoolParseBodyJson") == std::string::npos);
	REQUIRE(head_walker.find("loaderPoolParseHeadJson") == std::string::npos);

	/* Direct typed registration is also a custom-body path: a later
	 * assetCatalogRegisterBody/Head call must not wipe runtime_index back to
	 * -1 or keep an authored -1 in the selector row. */
	REQUIRE(catalog.find("s_resolveBodyRegistrationSlot") !=
	        std::string::npos);
	REQUIRE(catalog.find("s_resolveHeadRegistrationSlot") !=
	        std::string::npos);
	REQUIRE(catalog.find("assetCatalogResolveBodyPrivateSlot(id)") !=
	        std::string::npos);
	REQUIRE(catalog.find("assetCatalogResolveHeadPrivateSlot(id)") !=
	        std::string::npos);
	REQUIRE(catalog.find("entry->runtime_index = resolved_bodynum") !=
	        std::string::npos);
	REQUIRE(catalog.find("entry->runtime_index = resolved_headnum") !=
	        std::string::npos);

	/* Staged mods and network-distributed mods do not pass through the base
	 * boot walker. They must receive the same private runtime-slot bridge at
	 * catalog registration time, bind the inner .pdmesh model source, and
	 * activate the loader-pool rows immediately. */
	REQUIRE(scanner.find("#include \"assetcatalog_body_head_slots.h\"") !=
	        std::string::npos);
	const std::string binding = readTextFile("port/src/body_head_source_bind.c");
	const std::string pool = readTextFile("port/src/loader_pool.c");
	REQUIRE(scanner.find("registerBodyHeadPublicDescriptor") != std::string::npos);
	REQUIRE(scanner.find("bodyHeadSourceReadIniBytes") != std::string::npos);
	REQUIRE(binding.find("loaderWalkerMeshSourcePlanManifest") != std::string::npos);
	REQUIRE(binding.find("loaderWalkerBindMeshSource") != std::string::npos);
	REQUIRE(binding.find("fsFileSize(candidate.plan.source_path) <= 0") != std::string::npos);
	REQUIRE(binding.find("catalogIdBySourceFilenum(ASSET_MODEL") != std::string::npos);
	REQUIRE(pool.find("loaderPoolInstallPublicBody") != std::string::npos);
	REQUIRE(pool.find("loaderPoolInstallPublicHead") != std::string::npos);
	REQUIRE(scanner.find("catalogManagerRegisterBody(id, body)") == std::string::npos);
	REQUIRE(scanner.find("catalogManagerRegisterHead(id, head)") == std::string::npos);

	requireTokenOrder(main_c,
		"catalogManagerHeadInit();",
		"assetCatalogScanComponents(modsdir);");
	requireTokenOrder(main_c,
		"catalogManagerBodyInit();",
		"assetCatalogScanComponents(modsdir);");
	REQUIRE(main_c.find("assetCatalogScanComponents(modsdir);\n\t\t\tassetCatalogScanBotVariants(modsdir);") !=
	        std::string::npos);

	REQUIRE(netdistrib.find("#include \"assetcatalog_body_head_slots.h\"") !=
	        std::string::npos);
	REQUIRE(netdistrib.find("distribPreparePublicBodyHead(destdir, slot->id") != std::string::npos);
	REQUIRE(netdistrib.find("distribPreparePublicBodyHead(candidates[i].dirpath") != std::string::npos);
	REQUIRE(netdistrib.find("assetCatalogPrepareBodyHeadDescriptor") != std::string::npos);
	REQUIRE(netdistrib.find("assetCatalogFinishBodyHeadAdmission(body_head_admission, 0)") != std::string::npos);
	REQUIRE(netdistrib.find("assetCatalogFinishBodyHeadAdmission(body_head_admission, commit_result > 0)") != std::string::npos);
	REQUIRE(netdistrib.find("loaderPoolParseBodyJsonForSlot") == std::string::npos);
	REQUIRE(netdistrib.find("loaderPoolParseHeadJsonForSlot") == std::string::npos);
	REQUIRE(netdistrib.find("catalogManagerRegisterBody(id, body)") == std::string::npos);
	REQUIRE(netdistrib.find("catalogManagerRegisterHead(id, head)") == std::string::npos);

	/* Private custom head slots begin above the signed 8-bit range. chrdata is
	 * the render-time handoff field, so it must not narrow slot 152 to -104. */
	REQUIRE(types.find("/*0x006*/ s16 headnum;") != std::string::npos);
	REQUIRE(types.find("/*0x006*/ s8 headnum;") == std::string::npos);

	/* Legacy mod.json content rows are not allowed to require public numeric
	 * body/head slots or overwrite the private slot with a cache position. */
	REQUIRE(modmgr.find("if (is_bodies)") != std::string::npos);
	REQUIRE(modmgr.find("if (is_bodies && bodynum_field >= 0)") ==
	        std::string::npos);
	REQUIRE(modmgr.find("} else if (is_heads)") != std::string::npos);
	REQUIRE(modmgr.find("} else if (is_heads && headnum_field >= 0)") ==
	        std::string::npos);
	REQUIRE(modmgr.find("e->runtime_index = body_start + body_reg++") ==
	        std::string::npos);
	REQUIRE(modmgr.find("e->runtime_index = head_start + head_reg++") ==
	        std::string::npos);

	/* The render path still receives an integer slot today, so reverse lookup
	 * must scan catalog entries by runtime_index when boot-time caches have not
	 * yet been built or when a custom slot lives outside the base cache range. */
	REQUIRE(api.find("const char *catalogIdByRuntime(asset_type_e type, s32 runtime_index)") !=
	        std::string::npos);
	REQUIRE(api.find("for (i = 0; i < assetCatalogGetPoolSize(); i++)") !=
	        std::string::npos);
	REQUIRE(api.find("if (e->runtime_index != runtime_index) continue;") !=
	        std::string::npos);
	REQUIRE(api.find("match = e->id;") != std::string::npos);

	/* The managers must load the typed body/head archive from public source
	 * before any legacy handle/modeldef fallback. If public source conversion
	 * fails, FileProvider rows fail closed instead of falling through to a ROM
	 * handle or native bytes. */
	requireTokenOrder(body_mgr,
		"catalogLoadStageAsset(ASSET_BODY, id)",
		"modeldefLoadToNewFromHandle(handle");
	requireTokenOrder(head_mgr,
		"catalogLoadStageAsset(ASSET_HEAD, id)",
		"modeldefLoadToNewFromHandle(handle");
	REQUIRE(body_mgr.find("e->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("e->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("public source modeldef conversion failed") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("public source modeldef conversion failed") !=
	        std::string::npos);

	/* Character assembly must resolve the public catalog body identity before
	 * reading body fields or loading a modeldef, and generated source-backed
	 * modeldefs must instantiate without the legacy animation/skeleton path. */
	requireTokenOrder(body_runtime,
		"body_source_id = catalogBodyIdByBodynum(bodynum);",
		"catalogGetBodyScaleChecked(bodynum, &scaleRaw)");
	requireTokenOrder(body_runtime,
		"body_source_id = catalogBodyIdByBodynum(bodynum);",
		"bodymodeldef = catalogGetBodyModeldef(bodynum);");
	REQUIRE(body_runtime.find("BODY.IDENTITY: bodynum=%d has no catalog body; refusing slot-0 visual fallback") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("catalogGetLoadedModeldef(body_source_id) == bodymodeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("bodymodeldef->numparts == 0") ==
	        std::string::npos);
	REQUIRE(body_runtime.find("if (public_source_generated_modeldef) {\n\t\theadmodeldef = NULL;") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithoutAnim(bodymodeldef)") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithAnim(bodymodeldef)") !=
	        std::string::npos);
}

TEST_CASE("source-generated hit tests carry exact owned display-list spans",
		  "[modding][pdxxx][c3844][source][static][b769][B-1080]") {
	const std::string propobj = readTextFile("src/game/propobj.c");
	const std::string chr_hit_caller = functionBlock(propobj, "func0f06bea0");
	const std::string obj_hit_caller =
		functionBlock(propobj, "bool func0f0849dc");
	const std::string bg = readTextFile("src/game/bg.c");
	const std::string bg_header = readTextFile("src/include/game/bg.h");
	const std::string compiler = readTextFile("port/src/modasset_compiler.c");
	const std::string compiler_header =
		readTextFile("port/include/modasset_compiler.h");

	REQUIRE(!chr_hit_caller.empty());
	REQUIRE(chr_hit_caller.find("modAssetCompilerModeldefIsGenerated(model->definition)") !=
	        std::string::npos);
	REQUIRE(chr_hit_caller.find("modAssetCompilerGeneratedGdlBytesRemaining(") !=
	        std::string::npos);
	REQUIRE(chr_hit_caller.find("s4 = rwdata->gdl;") != std::string::npos);
	REQUIRE(chr_hit_caller.find("s6 = rodata->dl.xlugdl;") !=
	        std::string::npos);
	requireTokenOrder(chr_hit_caller,
		"modAssetCompilerGeneratedGdlBytesRemaining(",
		"s4 = rwdata->gdl;");
	requireTokenOrder(chr_hit_caller,
		"s4 = rwdata->gdl;",
		"UNSEGADDR(rodata->dl.opagdl)");
	requireTokenOrder(chr_hit_caller,
		"modAssetCompilerGeneratedGdlBytesRemaining(",
		"s6 = rodata->dl.xlugdl;");
	requireTokenOrder(chr_hit_caller,
		"s6 = rodata->dl.xlugdl;",
		"UNSEGADDR(rodata->dl.xlugdl)");
	REQUIRE(chr_hit_caller.find("uintptr_t base =") != std::string::npos);
	REQUIRE(chr_hit_caller.find("s32 base = (intptr_t)") == std::string::npos);
	REQUIRE(chr_hit_caller.find("s4, s4bytes,") != std::string::npos);
	REQUIRE(chr_hit_caller.find("s6, s6bytes, vertices") != std::string::npos);

	REQUIRE(!obj_hit_caller.empty());
	REQUIRE(obj_hit_caller.find(
		"generated_model = modAssetCompilerModeldefIsGenerated") !=
		std::string::npos);
	REQUIRE(obj_hit_caller.find("modAssetCompilerGeneratedGdlBytesRemaining(") !=
		std::string::npos);
	REQUIRE(obj_hit_caller.find("uintptr_t base =") != std::string::npos);
	REQUIRE(obj_hit_caller.find("s3, s3bytes,") != std::string::npos);
	REQUIRE(obj_hit_caller.find("s5, s5bytes, vertices") != std::string::npos);
	REQUIRE(obj_hit_caller.find("unowned_generated_gundl_opaque") !=
		std::string::npos);

	REQUIRE(compiler_header.find(
		"modAssetCompilerGeneratedGdlBytesRemaining") != std::string::npos);
	REQUIRE(compiler.find("size_t gdl_bytes;") != std::string::npos);
	REQUIRE(compiler.find("size_t base_bytes;") != std::string::npos);
	REQUIRE(compiler.find("size_t gdl_offset;") != std::string::npos);
	REQUIRE(compiler.find(
		"modeldef->rootnode->rodata == owner->def.rootnode->rodata") !=
		std::string::npos);
	REQUIRE(compiler.find(
		"gdlSpanBytesRemaining(owner->gdl") != std::string::npos);
	REQUIRE(compiler.find(
		"gdlSpanBytesRemaining(payload->gdl") != std::string::npos);
	REQUIRE(compiler.find(
		"gdlSpanBytesRemaining(relocated_gdl") != std::string::npos);
	REQUIRE(compiler.find(
		"gdlSpanFitsAllocation(payload->base_bytes") != std::string::npos);
	REQUIRE(compiler.find("payload->base_bytes = base_bytes;") !=
		std::string::npos);
	REQUIRE(compiler.find("payload->gdl_offset = gdl_offset;") !=
		std::string::npos);
	REQUIRE(compiler.find("gdlSpanEndsWithEnddl(payload->gdl") !=
	        std::string::npos);
	REQUIRE(compiler.find("gdlSpanEndsWithEnddl(owner->gdl") !=
	        std::string::npos);

	REQUIRE(bg_header.find("Gfx *gdl, u32 gdlbytes, Gfx *gdl2,") !=
	        std::string::npos);
	const std::string bounded_walker = functionBlock(bg, "bgTestHitOnChr");
	REQUIRE(!bounded_walker.empty());
	REQUIRE(bounded_walker.find("generated_span_exhausted_before_enddl") !=
	        std::string::npos);
	// The native walk still checks every command before it reads it. The
	// check shares one span per walk so that it asks the operating system
	// once per run of readable memory and not once per command (B-1234).
	REQUIRE(bounded_walker.find(
		"modelRodataIsReadableInSpan(&readable, gdl, sizeof(Gfx))") !=
	        std::string::npos);
	REQUIRE(bounded_walker.find("model_rodata_span_t readable = { 0, 0 };") !=
	        std::string::npos);
	REQUIRE(bounded_walker.find("BG_HIT_GDL_NATIVE_COMMAND_CAP") !=
	        std::string::npos);
	REQUIRE(bounded_walker.find("bytesremaining = gdl2bytes") !=
	        std::string::npos);
	REQUIRE(bounded_walker.find("triangle_index_out_of_range") !=
		std::string::npos);

	const std::string bounded_obj_walker = functionBlock(bg, "bgTestHitOnObj");
	REQUIRE(!bounded_obj_walker.empty());
	REQUIRE(bounded_obj_walker.find(
		"generated_span_exhausted_before_enddl") != std::string::npos);
	REQUIRE(bounded_obj_walker.find(
		"modelRodataIsReadableInSpan(&readable, gdl, sizeof(Gfx))") !=
		std::string::npos);
	REQUIRE(bounded_obj_walker.find(
		"model_rodata_span_t readable = { 0, 0 };") != std::string::npos);
	REQUIRE(bounded_obj_walker.find("BG_HIT_GDL_NATIVE_COMMAND_CAP") !=
		std::string::npos);
	REQUIRE(bounded_obj_walker.find("bytesremaining = gdl2bytes") !=
		std::string::npos);
	REQUIRE(bounded_obj_walker.find("triangle_index_out_of_range") !=
		std::string::npos);
}

TEST_CASE("language runtime loads public pdlang strings source",
          "[modding][pdxxx][c3844][lang][static]") {
	const std::string lang_manifest = readTextFile("port/src/langmanifest.c");
	const std::string lang_manifest_h = readTextFile("port/include/langmanifest.h");
	const std::string lang = readTextFile("src/game/lang.c");
	const std::string langreset = readTextFile("src/game/langreset.c");
	const std::string base = readTextFile("port/src/assetcatalog_base_extended.c");

	REQUIRE(lang_manifest_h.find("langManifestLoadBankFromCatalog") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("langManifestFindBestEntryForBank") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("entry->ext.lang.bank_id != bank") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("entry->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("langManifestLoadExternalJson(entry)") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("score = langSourceLocaleRank(entry->ext.lang.locale,") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("if (!score) continue;") != std::string::npos);
	REQUIRE(lang_manifest.find("score = score * 2 + (entry->bundled ? 0 : 1)") !=
	        std::string::npos);
	REQUIRE(lang_manifest.find("strcmp(entry->id, best->id) < 0") != std::string::npos);
	REQUIRE(lang_manifest.find("JSON bank") !=
	        std::string::npos);

	const std::string lang_extract = readTextFile("port/src/romextract_pdlang.c");
	REQUIRE(lang_extract.find("rzipIs1173") != std::string::npos);
	REQUIRE(lang_extract.find("rzipInflate") != std::string::npos);
	REQUIRE(lang_extract.find("PDLANG_EXTRACT_VERSION") != std::string::npos);
	REQUIRE(lang_extract.find("pdlang_strings_json_rzip_v3_regional_source_hash") !=
	        std::string::npos);
	REQUIRE(lang_extract.find("s_resolveLocaleFile(bank, locale_index") != std::string::npos);
	REQUIRE(lang_extract.find("s_existingArchiveEntryContains(dst_rel, \"lang.ini\", hash_field)") !=
	        std::string::npos);
	REQUIRE(lang_extract.find("extract_version = %s\\n") != std::string::npos);
	REQUIRE(lang_extract.find("s_existingArchiveEntryContains(dst_rel, \"lang.ini\"") !=
	        std::string::npos);

	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	REQUIRE(conformance.find("LANG_NATIVE_FIELDS") != std::string::npos);
	REQUIRE(conformance.find("validate_lang_source_contract") != std::string::npos);
	REQUIRE(conformance.find("source_bank must be a positive integer") !=
	        std::string::npos);
	REQUIRE(conformance.find("strings.json must contain a strings array of at most 512 rows") !=
	        std::string::npos);

	REQUIRE(lang.find("langManifestLoadBankFromCatalog(bank)") !=
	        std::string::npos);
	REQUIRE(lang.find("ASSET.CHAIN: language bank") != std::string::npos);
	REQUIRE(lang.find("refusing ROM fallback") != std::string::npos);
	REQUIRE(lang.find("assetLoadRomToNew") == std::string::npos);
	REQUIRE(lang.find("assetLoadRomToAddr") == std::string::npos);

	REQUIRE(langreset.find("langLoad(LANGBANK_GUN)") != std::string::npos);
	REQUIRE(langreset.find("langLoad(LANGBANK_MPMENU)") != std::string::npos);
	REQUIRE(langreset.find("assetLoadRomToNew") == std::string::npos);

	REQUIRE(base.find("base:lang_options_en") != std::string::npos);
	REQUIRE(base.find("langManifestLoadBankFromCatalog") !=
	        std::string::npos);
}

TEST_CASE("typed archive guard rejects numeric asset references",
          "[modding][pdxxx][c3842][catalog-id][static]") {
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(guard.find("\"model_id\"") != std::string::npos);
	REQUIRE(guard.find("\"modelnum\"") != std::string::npos);
	REQUIRE(guard.find("\"filenum\"") != std::string::npos);
	REQUIRE(guard.find("\"weapon_id\"") != std::string::npos);
	REQUIRE(guard.find("\"mode_id\"") != std::string::npos);
	REQUIRE(guard.find("\"hud_id\"") != std::string::npos);
	REQUIRE(guard.find("\"prop_type\"") != std::string::npos);
	REQUIRE(guard.find("\"stagenum\"") != std::string::npos);
	REQUIRE(guard.find("NUMERIC_LITERAL_RE") != std::string::npos);
	REQUIRE(guard.find("LEGACY_ASSET_SYMBOL_PREFIXES") !=
	        std::string::npos);
	REQUIRE(guard.find("MODEL_") != std::string::npos);
	REQUIRE(guard.find("use a catalog ID") != std::string::npos);
	REQUIRE(guard.find("\"runtime.graph.json\"") != std::string::npos);

	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"setup.tsv\"") ==
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"mpsetup.tsv\"") ==
	        std::string::npos);
	REQUIRE(arena.find("s_buildSetupTsv") == std::string::npos);
	REQUIRE(arena.find("s_buildMpSetupTsv") == std::string::npos);
	REQUIRE(arena.find("visual_segments.tsv") == std::string::npos);
	REQUIRE(arena.find("s_buildWordsTsv") == std::string::npos);
}

TEST_CASE("c3844 live smokes use scoped asset proof logging",
          "[modding][pdxxx][c3844][source][static][b801]") {
	const std::string smoke_harness = readTextFile("port/src/smoke_harness.c");
	const std::string system = readTextFile("port/src/system.c");
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string smoke_runner = readTextFile("tools/smoke-verify/run.ps1");
	const std::string smoke_readme = readTextFile("tools/smoke-verify/README.md");
	const std::string fs_c = readTextFile("port/src/fs.c");
	const std::string modasset_compiler =
		readTextFile("port/src/modasset_compiler.c");
	const std::string scenario_smoke =
		readTextFile("tools/smoke-verify/tests/scenario_pads_source_gate_smoke.json");
	const std::string weapon_smoke =
		readTextFile("tools/smoke-verify/tests/weapon_match_source_gate_smoke.json");
	const std::string all_family_smoke =
		readTextFile("tools/smoke-verify/tests/all_family_source_gate_smoke.json");

	REQUIRE(smoke_harness.find("sysLogChannelNames[i]") != std::string::npos);
	REQUIRE(smoke_harness.find("sysLogChannelBits[i]") != std::string::npos);
	REQUIRE(smoke_harness.find("strtok_r(") == std::string::npos);
	REQUIRE(smoke_harness.find("strtok(tmp, \",|+; \")") != std::string::npos);
	REQUIRE(smoke_harness.find("unknown log_channel_mask token") != std::string::npos);
	REQUIRE(smoke_readme.find("named channels such as") != std::string::npos);
	REQUIRE(smoke_runner.find("Using freshly built smoke binary") != std::string::npos);
	REQUIRE(smoke_runner.find(".claude\\session-builds\\$Session") != std::string::npos);
	REQUIRE(smoke_runner.find("-SourceBinary $BinaryOverride") != std::string::npos);

	REQUIRE(system.find("\"CATALOG.\"") != std::string::npos);
	REQUIRE(system.find("\"ASSET.\"") != std::string::npos);
	REQUIRE(system.find("\"LOADER.\"") != std::string::npos);
	REQUIRE(system.find("\"romextract \"") != std::string::npos);
	REQUIRE(system.find("\"SCENARIO.SOURCE:\"") != std::string::npos);
	REQUIRE(system.find("\"SCENARIO.GRAPH:\"") != std::string::npos);
	REQUIRE(system.find("\"MISSION.GRAPH:\"") != std::string::npos);
	REQUIRE(system.find("\"MODASSET.COMPILER:\"") != std::string::npos);
	REQUIRE(system.find("\"SCENARIO.RENDER:\"") != std::string::npos);
	REQUIRE(system.find("\"MODASSET.RENDER:\"") != std::string::npos);
	REQUIRE(system.find("\"MODELDEF.SOURCE:\"") != std::string::npos);
	REQUIRE(system.find("\"BONDGUN.SOURCE:\"") != std::string::npos);
	REQUIRE(system.find("\"BGUN.ANIM.SOURCE:\"") != std::string::npos);
	REQUIRE(system.find("\"LOG.WPN.DIAG:\"") != std::string::npos);
	REQUIRE(system.find("\"MESHCOL:\"") != std::string::npos);
	REQUIRE(fs_c.find("sysLogPrintf(LOG_VERBOSE, \"FSPATH:") !=
	        std::string::npos);
	REQUIRE(fs_c.find("sysLogPrintf(LOG_NOTE, \"FSPATH:") ==
	        std::string::npos);
	REQUIRE(modasset_compiler.find("ensureCacheFileParentDirs") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("could not open %s cache") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("errno=%d") != std::string::npos);
	REQUIRE(countOccurrences(modasset_compiler,
		        "ensureCacheFileParentDirs(path);\n\tf = fsFileOpenWrite(path);") >=
	        4);
	REQUIRE(modasset_compiler.find("char digest_key[33]") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("memcpy(digest_key, digest_hex") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("source_sha256") != std::string::npos);
	REQUIRE(modasset_compiler.find("MODASSET_COMPILER_VERSION, digest_key") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODASSET_WINDOWS_CRT_PATH_LIMIT 259") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("cachePathFitsPlatform") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("_fullpath(absolute, resolved") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"$H/mod-cache\"") !=
	        std::string::npos);
	REQUIRE(main_c.find("bootDebugCatalogProbesCanRunBeforeBaseEmit") !=
	        std::string::npos);
	REQUIRE(main_c.find("g_BootDebugLoadCatalogAssetsSourceOnly") !=
	        std::string::npos);
	REQUIRE(main_c.find("strstr(arg, \"base:\")") != std::string::npos);
	REQUIRE(main_c.find("g_BootDebugLoadCatalogAssetsFileArg") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (bootDebugCatalogProbesCanRunBeforeBaseEmit())") !=
	        std::string::npos);

	REQUIRE(scenario_smoke.find("\"log_channel_mask\": \"game,catalog,render,system\"") !=
	        std::string::npos);
	REQUIRE(scenario_smoke.find("\"log_channel_mask\": \"all\"") == std::string::npos);
	REQUIRE(weapon_smoke.find("\"log_channel_mask\": \"game,catalog,render,match,combat,network,system\"") !=
	        std::string::npos);
	REQUIRE(weapon_smoke.find("\"log_channel_mask\": \"all\"") == std::string::npos);
	REQUIRE(all_family_smoke.find("\"log_channel_mask\": \"audio,render,game,system\"") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("--debug-exit-after-catalog-probes") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("\"log_channel_mask\": \"all\"") ==
	        std::string::npos);
	REQUIRE(all_family_smoke.find("\"log_channel_mask\": \"catalog") ==
	        std::string::npos);
}

TEST_CASE("extract-assets-only regenerates archives without gameplay boot",
          "[modding][pdxxx][c3844][source][static][extract]") {
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string config_c = readTextFile("port/src/config.c");
	const std::string boot_progress = readTextFile("port/src/boot_progress.c");
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	const std::string boot_catalog = functionBlock(main_c, "bootRunCatalogWork");
	const std::string main_fn = functionBlock(main_c, "int main");

	REQUIRE(config_c.find("#define CONFIG_MAX_SETTINGS 2048") !=
	        std::string::npos);
	REQUIRE(config_c.find("#define CONFIG_MAX_SETTINGS 512") ==
	        std::string::npos);
	REQUIRE(boot_progress.find("current 512-entry cap") ==
	        std::string::npos);
	REQUIRE(conformance.find("collect_source_catalog_ids") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"port/src/bodydata_authored.c\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"port/src/headdata_authored.c\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"port/src/weapondata_authored.c\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("iter_catalog_context_archives") !=
	        std::string::npos);
	REQUIRE(conformance.find("without validating siblings") !=
	        std::string::npos);

	REQUIRE(main_c.find("--extract-assets-only") != std::string::npos);
	REQUIRE(main_c.find("g_BootExtractAssetsOnly") != std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only shutdown; gameplay/window teardown skipped") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only set; window/UI/input startup skipped") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only set; audioInit() skipped") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: --extract-assets-only set; netInit() skipped") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (!g_BootExtractAssetsOnly && !sysArgCheck(\"--no-update-check\"))") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (!g_BootExtractAssetsOnly) {\n\t\tvideoInit();") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (!g_BootExtractAssetsOnly) {\n\t\tpdguiBootOverlayInit();") !=
	        std::string::npos);
	REQUIRE(main_c.find("if (!g_BootExtractAssetsOnly) {\n\t\t\tpdguiBootOverlayPump();") !=
	        std::string::npos);
	REQUIRE(main_c.find("SDL_Delay(1);") != std::string::npos);

	REQUIRE(!boot_catalog.empty());
	REQUIRE(boot_catalog.find("!g_BootNoNet && !g_BootExtractAssetsOnly") !=
	        std::string::npos);
	REQUIRE(boot_catalog.find("romExtractAllPdarena(0)") !=
	        std::string::npos);
	requireTokenOrder(boot_catalog,
		"romExtractAllPdarena(0)",
		"loaderWalkerLoadAll(&walker_result)");
	requireTokenOrder(boot_catalog,
		"loaderWalkerLoadAll(&walker_result)",
		"catalogBuildRuntimeCaches()");
	requireTokenOrder(boot_catalog,
		"catalogBuildRuntimeCaches()",
		"bootProgressMarkComplete()");

	REQUIRE(!main_fn.empty());
	REQUIRE(main_fn.find("g_BootExtractAssetsOnly = sysArgCheck(\"--extract-assets-only\") ? true : false;") !=
	        std::string::npos);
	REQUIRE(main_fn.find("audioInit();\n\t} else {\n\t\tsysLogPrintf(LOG_NOTE,") !=
	        std::string::npos);
	requireTokenOrder(main_fn,
		"bootPoolWaitIdle();",
		"BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init");
	requireTokenOrder(main_fn,
		"BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init",
		"bootCreateSched();");
	requireTokenOrder(main_fn,
		"BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init",
		"mainProc();");
}

TEST_CASE("c3841 scenario archives are source-first and runtime-native",
          "[modding][pdxxx][c3841][scenario][static]") {
	const std::string guard = readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(guard.find("PDSCENARIO_REQUIRED_PUBLIC_ENTRY_NAMES") !=
	        std::string::npos);
	REQUIRE(guard.find("navigation/waypoints.json") !=
	        std::string::npos);
	REQUIRE(guard.find("navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(guard.find("setup.fields.json") !=
	        std::string::npos);
	REQUIRE(guard.find("\"scene.glb\"") != std::string::npos);
	REQUIRE(guard.find("\"level.graph.json\"") != std::string::npos);
	const std::string conformance = readTextFile("tools/asset_archive_conformance.py");
	const std::string scene_texture_checker =
		readTextFile("tools/verify_scene_glb_texture_contract.py");
	REQUIRE(conformance.find("runtime_source_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(conformance.find("setup_fields_file = setup.fields.json") !=
	        std::string::npos);
	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	REQUIRE(arena.find("\"vehicle.ai_list\"") == std::string::npos);
	REQUIRE(arena.find("\"vehicle.ai_offset\"") != std::string::npos);
	REQUIRE(arena.find("const struct heliobj *heli") != std::string::npos);
	REQUIRE(arena.find("\"vehicle.speed\", heli->speed") !=
	        std::string::npos);

	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("scene_file = scene.glb") != std::string::npos);
	REQUIRE(scanner.find("portals_file = portals.json") != std::string::npos);
	REQUIRE(scanner.find("setup_fields_file = setup.fields.json") !=
	        std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"portals_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"waypoints_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"waygroups_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"covers_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"paths_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, sf)") != std::string::npos);

	const std::string distrib = readTextFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("iniGet(ini, \"waypoints_file\"") !=
	        std::string::npos);
	REQUIRE(distrib.find("iniGet(ini, \"paths_file\"") !=
	        std::string::npos);

	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("modAssetCompilerBuildColmesh(colmesh_source_path, mesh)") !=
	        std::string::npos);
	REQUIRE(load.find("entry->ext.scenario.collision_file") !=
	        std::string::npos);

	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("entry->ext.scenario.scene_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.portals_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("scenario_portals_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.navigation_waypoints_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("scenario_navigation_waypoints_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.navigation_paths_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("scenario_navigation_paths_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.scenario.level_graph_file") !=
	        std::string::npos);
	{
		const size_t scenario_anchor =
			runtime.find("entry->ext.scenario.scene_file");
		const size_t scenario_start =
			runtime.rfind("case ASSET_SCENARIO:", scenario_anchor);
		const size_t theme_start = runtime.find("case ASSET_THEME:",
			scenario_anchor);
		REQUIRE(scenario_anchor != std::string::npos);
		REQUIRE(scenario_start != std::string::npos);
		REQUIRE(theme_start != std::string::npos);
		const std::string scenario_case =
			runtime.substr(scenario_start, theme_start - scenario_start);
		REQUIRE(scenario_case.find("entry->ext.scenario.scene_file[0]") ==
		        std::string::npos);
		REQUIRE(scenario_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->authored_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->dependency_b) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_portals_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_pads_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_spawns_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_volumes_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_objects_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_setup_fields_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_ai_lists_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_objectives_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_waypoints_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_waygroups_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_covers_file) &&") !=
		        std::string::npos);
		REQUIRE(scenario_case.find("s_hasText(binding->scenario_navigation_paths_file)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.skin.texture_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.swatches_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.material_archive") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.texture_archive") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.hud.layout_file") !=
	        std::string::npos);
	{
		const size_t hud_anchor =
			runtime.find("entry->ext.hud.layout_file");
		const size_t hud_start =
			runtime.rfind("case ASSET_HUD:", hud_anchor);
		const size_t ui_start = runtime.find("case ASSET_UI:", hud_anchor);
		const size_t material_start = runtime.find("case ASSET_MATERIAL:", ui_start);
		REQUIRE(hud_anchor != std::string::npos);
		REQUIRE(hud_start != std::string::npos);
		REQUIRE(ui_start != std::string::npos);
		REQUIRE(material_start != std::string::npos);
		const std::string hud_case =
			runtime.substr(hud_start, ui_start - hud_start);
		REQUIRE(hud_case.find("entry->ext.hud.layout_file") !=
		        std::string::npos);
		REQUIRE(hud_case.find("entry->ext.hud.texture_file[0]") ==
		        std::string::npos);
		REQUIRE(hud_case.find("binding->primary_path") ==
		        std::string::npos);
		const std::string ui_case =
			runtime.substr(ui_start, material_start - ui_start);
		REQUIRE(ui_case.find("entry->ext.ui.texture_file") !=
		        std::string::npos);
		REQUIRE(ui_case.find("entry->ext.ui.layout_file") !=
		        std::string::npos);
		REQUIRE(ui_case.find("entry->ext.ui.nineslice_file") !=
		        std::string::npos);
		REQUIRE(ui_case.find(": entry->ext.ui.layout_file") ==
		        std::string::npos);
		REQUIRE(ui_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.mission.objectives_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.mission.briefing_file") ==
	        std::string::npos);
	{
		const size_t mission_anchor =
			runtime.find("entry->ext.mission.mission_graph_file");
		const size_t mission_start =
			runtime.rfind("case ASSET_MISSION:", mission_anchor);
		const size_t gamemode_start = runtime.find("case ASSET_GAMEMODE:",
			mission_anchor);
		REQUIRE(mission_anchor != std::string::npos);
		REQUIRE(mission_start != std::string::npos);
		REQUIRE(gamemode_start != std::string::npos);
		const std::string mission_case =
			runtime.substr(mission_start, gamemode_start - mission_start);
		REQUIRE(mission_case.find("entry->ext.mission.mission_graph_file") !=
		        std::string::npos);
		REQUIRE(mission_case.find("entry->ext.mission.scenario_archive") !=
		        std::string::npos);
		REQUIRE(mission_case.find("entry->ext.mission.objectives_file") !=
		        std::string::npos);
		REQUIRE(mission_case.find("entry->ext.mission.briefing_file") ==
		        std::string::npos);
		REQUIRE(mission_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(mission_case.find("s_hasText(binding->authored_file) &&") !=
		        std::string::npos);
		REQUIRE(mission_case.find("s_hasText(binding->dependency_a) &&") !=
		        std::string::npos);
		REQUIRE(mission_case.find("s_hasText(binding->dependency_b));") !=
		        std::string::npos);
		REQUIRE(mission_case.find("s_hasText(binding->dependency_c)") ==
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.effect.timeline_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.character.portrait_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("dependency_c") != std::string::npos);
	REQUIRE(runtime.find("dependency_d") != std::string::npos);
	REQUIRE(runtime.find("dependency_e") != std::string::npos);
}

TEST_CASE("c3843 remaining base asset families emit clean native archives",
          "[modding][pdxxx][c3843][static]") {
	const std::string base =
		readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string meta = readTextFile("port/src/romextract_pdmeta.c");
	const std::string texture_extractor =
		readTextFile("port/src/romextract_pdtexture.c");
	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");
	const std::string guard =
		readTextFile("tools/asset_native_source_guard.py");
	const std::string scene_texture_checker =
		readTextFile("tools/verify_scene_glb_texture_contract.py");
	const std::string header = readTextFile("port/include/romextract_pd.h");
	const std::string theme =
		readTextFile("port/fast3d/pdgui_theme_loader.cpp");
	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	const std::string file_provider =
		readTextFile("port/src/assetprovider_file.c");
	const std::string scanner =
		readTextFile("port/src/assetcatalog_scanner.c");
	const std::string distrib =
		readTextFile("port/src/net/netdistrib.c");
	const std::string examples =
		readTextFile("tools/build_typed_pdxxx_examples.py");

	const char *families[] = {
		".pdtexture",
		".pdmaterial",
		".pdskin",
		".pdeffect",
		".pdprop",
		".pdvehicle",
		".pdgamemode",
		".pdbotprofile",
		".pdhud",
		".pdmission",
		".pdtheme",
	};
	for (const char *family : families) {
		INFO(family);
		REQUIRE((base.find(family) != std::string::npos ||
		         meta.find(family) != std::string::npos ||
		         texture_extractor.find(family) != std::string::npos ||
		         header.find(family) != std::string::npos));
	}

	REQUIRE(base.find("ASSET_MATERIAL") != std::string::npos);
	REQUIRE(base.find("ASSET_SKIN") != std::string::npos);
	REQUIRE(base.find("ASSET_EFFECT") != std::string::npos);
	REQUIRE(base.find("ASSET_VEHICLE") != std::string::npos);
	REQUIRE(base.find("textures\", idbuf, \".pdtexture\"") !=
	        std::string::npos);
	REQUIRE(base.find("materials\", idbuf, \".pdmaterial\"") !=
	        std::string::npos);
	REQUIRE(base.find("skins\", skin_id, \".pdskin\"") !=
	        std::string::npos);
	REQUIRE(base.find("effects\", idbuf, \".pdeffect\"") !=
	        std::string::npos);
	REQUIRE(base.find("props\", idbuf, \".pdprop\"") !=
	        std::string::npos);
	REQUIRE(base.find("vehicles\", idbuf, \".pdvehicle\"") !=
	        std::string::npos);
	REQUIRE(base.find("dependencies/assets/models") !=
	        std::string::npos);
	REQUIRE(base.find("fsDataPathFor(rel, out, out_n)") !=
	        std::string::npos);
	{
		const size_t vehicle_start = base.find("/* ---- vehicles ---- */");
		const size_t hand_start = base.find("/* ---- first-person hand models", vehicle_start);
		REQUIRE(vehicle_start != std::string::npos);
		REQUIRE(hand_start != std::string::npos);
		const std::string vehicle_block =
			base.substr(vehicle_start, hand_start - vehicle_start);
		REQUIRE(vehicle_block.find("const char *primary_file = e->ext.vehicle.model_file[0]") !=
		        std::string::npos);
		REQUIRE(vehicle_block.find("catalogSetPrimaryFile(e, primary_file)") !=
		        std::string::npos);
		REQUIRE(vehicle_block.find("catalogSetPrimaryFile(e, e->ext.vehicle.physics_file)") ==
		        std::string::npos);
	}

	/* c3849 Wave 4: the texture emitter lives in romextract_pdtexture.c and
	 * gates on the family fast-cache stamp pdmeta wrote but never read. */
	REQUIRE(texture_extractor.find("s_emitTexture") != std::string::npos);
	REQUIRE(meta.find("s_emitTexture") == std::string::npos);
	/* Public properties extend the schema-v2/palette cache stamp so older
	 * archives cannot hide newly editable texture controls. */
	REQUIRE(texture_extractor.find(
	                "ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND \\\n"
	                "\t\"pdtexture_png_v1_decoded_rom_rgba_manifest_texture_file_cipalfix_b943_iafix_b945_fmtmeta_palv3_json_properties_v1\"") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find(
	                "romExtractPdFastCacheCanSkip(ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find(
	                "romExtractPdFastCacheWrite(ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND") !=
	        std::string::npos);
	/* c3849 Slice B: boot bind verification (loud TEXTURE.BIND pass) is
	 * part of the extractor; details pinned in the [c3849][bind] case. */
	REQUIRE(texture_extractor.find("TEXTURE.BIND") != std::string::npos);
	REQUIRE(meta.find("s_emitMaterial") != std::string::npos);
	REQUIRE(meta.find("s_emitSkin") != std::string::npos);
	REQUIRE(meta.find("s_emitEffect") != std::string::npos);
	REQUIRE(meta.find("s_emitProp") != std::string::npos);
	REQUIRE(meta.find("s_emitVehicle") != std::string::npos);
	REQUIRE(meta.find("PDMETA_FAST_CACHE_KIND \"pdmeta_table_backed_v16_botprofile_manifest_objective_status_event_mission_endings_vehiclegraph\"") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find("texture.png") != std::string::npos);
	REQUIRE(meta.find("material.json") != std::string::npos);
	REQUIRE(meta.find("skin.json") != std::string::npos);
	REQUIRE(meta.find("swatches.json") != std::string::npos);
	REQUIRE(meta.find("\\\"swatches_file\\\": \\\"swatches.json\\\"") !=
	        std::string::npos);
	REQUIRE(meta.find("\\\"min_players\\\": %d") !=
	        std::string::npos);
	REQUIRE(meta.find("\\\"type_key\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(meta.find("\\\"objectives_file\\\": \\\"objectives.json\\\"") !=
	        std::string::npos);
	REQUIRE(meta.find("assetArchiveWriterAddPublicMem(&writer, \"briefing.json\"") ==
	        std::string::npos);
	REQUIRE(meta.find("prop.json") != std::string::npos);
	REQUIRE(meta.find("physics.json") != std::string::npos);
	REQUIRE(meta.find("behavior.graph.json") != std::string::npos);
	REQUIRE(meta.find("assetArchiveWriterAddPublicDisk(&writer, model_member") !=
	        std::string::npos);

	REQUIRE(arena.find("romExtractDecodeTextureImages") !=
	        std::string::npos);
	REQUIRE(arena.find("romExtractTextureSlotIsEmpty") !=
	        std::string::npos);
	REQUIRE(header.find("romExtractDecodeTextureImages") !=
	        std::string::npos);
	REQUIRE(header.find("romExtractTextureSlotIsEmpty") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find("k_Transparent1x1Png") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find("empty_rom_slot") != std::string::npos);
	REQUIRE(meta.find("\"mode_id = %d") == std::string::npos);
	REQUIRE(meta.find("\"type = %d") == std::string::npos);
	REQUIRE(meta.find("\"difficulty = %d") == std::string::npos);
	REQUIRE(meta.find("\"hud_id = %d") == std::string::npos);
	REQUIRE(meta.find("\"element_type = %d") == std::string::npos);
	REQUIRE(meta.find("\"effect_type = %d") == std::string::npos);
	REQUIRE(meta.find("\"prop_type = %d") == std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDARENA_FAST_CACHE_KIND \"pdarena_clean_public_v10_pdscenario_v99_iafix_b945_allrooms_b1266_texgen_b1268_textbank\"") !=
	        std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND \"pdscenario_scene_glb_clean_public_v99_standalone_backfill_collision_obj_collision_flags_json_room_lights_json_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_alphablend_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_ai_command_graph_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json_cipalfix_b943_iafix_b945_objoperands_b1111_roomtable_b1113_b1114_b1115_lightrules_sky_stagejson_player_ai_intro_mpsetup_ending_hub_stars_dyntex_ammo_objrules_sparks_gas_mplifts_aiedges_simtwins_typedargs_catalogargs_assetargs_ruleevents_liftglass_textbank\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_aiOpcodeSemanticKind") != std::string::npos);
	REQUIRE(arena.find("ai_command_nodes_json") != std::string::npos);
	REQUIRE(arena.find("ai_command_links_json") != std::string::npos);
	REQUIRE(arena.find("\\\"kind\\\": \\\"scenario.ai.command\\\"") != std::string::npos);
	REQUIRE(arena.find("\\\"semantic_kind\\\"") != std::string::npos);
	REQUIRE(arena.find("\\\"ai_commands\\\": %u") != std::string::npos);
	REQUIRE(conformance.find("counts.ai_commands must match ai/ailists.json rows") !=
	        std::string::npos);
	REQUIRE(conformance.find("must include one scenario.ai.command node per ai/ailists.json row") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be linked from scenario.ai.lists") !=
	        std::string::npos);
	REQUIRE(arena.find("supports_drop = true") != std::string::npos);
	REQUIRE(arena.find("\\\"source_counts\\\": { \\\"pads\\\": %u, \\\"volumes\\\": %u, \\\"waypoints\\\": %u, \\\"waygroups\\\": %u, \\\"covers\\\": %u, \\\"paths\\\": %u }") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"source_hashes\\\": { \\\"scene.glb\\\": \\\"%s\\\", \\\"collision.obj\\\": \\\"%s\\\", \\\"navigation.ini\\\": \\\"%s\\\", \\\"portals.json\\\": \\\"%s\\\", \\\"pads.json\\\": \\\"%s\\\", \\\"spawns.json\\\": \\\"%s\\\", \\\"volumes.json\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"portals.json\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(check, \"portals.tsv\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingPdscenarioGeneratedNavmeshIsCurrent(check.archive)") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingPdscenarioNavmeshMetadataMatches") !=
	        std::string::npos);
	REQUIRE(arena.find("\"_meta/generated-navmesh.json\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"source_counts\\\": {") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"source_hashes\\\": {") !=
	        std::string::npos);
	REQUIRE(arena.find("\"navigation/paths.json\", \"paths\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\"scenario.portals.source\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"portals\\\": \\\"portals.json\\\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_portals_json_schema") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare portals_file = portals.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must bind portals table to portals.json") !=
	        std::string::npos);
	REQUIRE(arena.find("PDSCENARIO_BG_VISUAL_EXPORT_VERSION \"bg_visual_scene_glb_v12_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_materialextras_dualtex_alphablend_cipalfix_b943_iafix_b945_dyntex_allrooms_b1266_cull_b1267_texgen_b1268\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_rgbaHasNonOpaqueAlpha") !=
	        std::string::npos);
	REQUIRE(arena.find("tex->has_alpha") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"alphaMode\\\":\\\"MASK\\\",\\\"alphaCutoff\\\":0.01") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveEntryContains(check, \"scene.glb\",") !=
	        std::string::npos);
	REQUIRE(arena.find("PDSCENARIO_BG_VISUAL_EXPORT_VERSION)") !=
	        std::string::npos);
	REQUIRE(arena.find("scenario_graph_cache = %s") != std::string::npos);
	REQUIRE(arena.find("scenario_graph_cache = \" ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(conformance.find("SCENARIO_GRAPH_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(conformance.find("_meta/generated-navmesh.json must declare source_counts") !=
	        std::string::npos);
	REQUIRE(conformance.find("source_counts.{count_key} must match") !=
	        std::string::npos);
	REQUIRE(conformance.find("_meta/generated-navmesh.json must declare source_hashes") !=
	        std::string::npos);
	REQUIRE(conformance.find("source_hashes.{source_name} must match public source bytes") !=
	        std::string::npos);
	REQUIRE(conformance.find("navigation.ini must declare {description} capability support") !=
	        std::string::npos);
	REQUIRE(conformance.find("_meta/generated-navmesh.json must declare movement capabilities walk,jump,drop,wall,ceiling") !=
	        std::string::npos);
	REQUIRE(conformance.find("arena.ini must declare scenario_graph_cache") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.ini must declare scenario_graph_cache") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"texCoord\\\":0") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialUvScaleForGlb") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbAuthorUv") != std::string::npos);
	REQUIRE(arena.find("\\\"TEXCOORD_1\\\":%u") != std::string::npos);
	REQUIRE(arena.find("\\\"COLOR_0\\\":%u") != std::string::npos);
	REQUIRE(arena.find("\\\"baseColorTexture\\\":{\\\"index\\\":%d,\\\"texCoord\\\":0}") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"extras\\\":{\\\"pd2_material\\\":") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"texture_command\\\":\\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(arena.find("s_bgMaterialTextureCommandName") != std::string::npos);
	REQUIRE(arena.find("s_bgGltfWrapName") != std::string::npos);
	REQUIRE(arena.find("\\\"secondaryTexture\\\":{\\\"index\\\":%d,\\\"texCoord\\\":1}") !=
	        std::string::npos);
	REQUIRE(arena.find("s_bgGltfWrapMode") != std::string::npos);
	REQUIRE(arena.find("texture_scale_s") != std::string::npos);
	REQUIRE(arena.find("op == (u8)G_TEXTURE") != std::string::npos);
	REQUIRE(arena.find("\\\"wrapS\\\":%u,\\\"wrapT\\\":%u") != std::string::npos);
	REQUIRE(arena.find("m->shifts <= 10") != std::string::npos);
	REQUIRE(arena.find("m->shiftt <= 10") != std::string::npos);
	REQUIRE(arena.find("s_shift_scale / (f32)tex->width") != std::string::npos);
	REQUIRE(arena.find("t_shift_scale / (f32)tex->height") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbUv(m->vertices[v].u, m->vertices[v].v") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_scene_glb_texture_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_0 range") != std::string::npos);
	REQUIRE(conformance.find("bg_visual_scene_glb_v12_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_materialextras_dualtex_alphablend") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2_material extras for source renderer material parity") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be repeat, clamp, or mirror") !=
	        std::string::npos);
	REQUIRE(conformance.find("must be an integer native material field") !=
	        std::string::npos);
	REQUIRE(conformance.find("must reference a texture index") !=
	        std::string::npos);
	REQUIRE(conformance.find("renderer-parity runtime UVs") !=
	        std::string::npos);
	REQUIRE(conformance.find("declares secondary_image") != std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_1 runtime UVs") != std::string::npos);
	REQUIRE(conformance.find("COLOR_0 vertex colors") != std::string::npos);
	REQUIRE(conformance.find("DCC-authoring UV range") != std::string::npos);
	REQUIRE(conformance.find("visible textures to TEXCOORD_0") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("validate_scene_glb_texture_contract") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find(".pdscenario") != std::string::npos);
	REQUIRE(scene_texture_checker.find(".pdarena") != std::string::npos);
	REQUIRE(scene_texture_checker.find("path.is_dir()") != std::string::npos);
	REQUIRE(scene_texture_checker.find("path.rglob(\"*\")") !=
	        std::string::npos);
	REQUIRE(scene_texture_checker.find("scene.glb texture contract failed") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_generated_scenario_texture_contracts") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_ai_command_graph_coverage") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_runtime_count_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_runtime_lookup_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_scenario_player_state_access_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("SCENARIO_RUNTIME_LOOKUP_RULES") !=
	        std::string::npos);
	REQUIRE(guard.find(
		        "SCENARIO_CURRENT_PLAYER_POINTER_SOURCE_PROVEN_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("\"objFindByTagId\"") !=
	        std::string::npos);
	REQUIRE(guard.find("\"tagFindById\"") !=
	        std::string::npos);
	REQUIRE(guard.find("\"pathFindById\"") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\.currentplayer\\s*->") !=
	        std::string::npos);
	REQUIRE(guard.find("g_Vars\\.currentplayer\\b(?!\\s*->)") !=
	        std::string::npos);
	REQUIRE(guard.find("s_aiGraphRequireRuntimePlayerSlot") !=
	        std::string::npos);
	REQUIRE(guard.find("\"s_countRuntimePads\"") !=
	        std::string::npos);
	REQUIRE(guard.find("s_aiGraphTryCountRuntimeSetupChrRowsFromSource") !=
	        std::string::npos);
	REQUIRE(guard.find("s_aiGraphRuntimeSetupCharacterSourceIsActive()") !=
	        std::string::npos);
	REQUIRE(guard.find("s_setupGraphValidateBlockedPathWaypoints") !=
	        std::string::npos);
	REQUIRE(guard.find("AI_INTERPRETER_ONLY_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("AI_GRAPH_PENDING_FUNCTIONS") !=
	        std::string::npos);
	REQUIRE(guard.find("AI_GRAPH_PENDING_FUNCTIONS = set()") !=
	        std::string::npos);
	REQUIRE(guard.find("\"aiStop\"") == std::string::npos);
	REQUIRE(guard.find("\"aiKneel\"") == std::string::npos);
	REQUIRE(guard.find("\"aiSurrender\"") == std::string::npos);
	REQUIRE(guard.find("\"aiFadeOut\"") == std::string::npos);
	REQUIRE(guard.find("\"aiRemoveChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiChrDoAnimation\"") == std::string::npos);
	REQUIRE(guard.find("\"aiBeSurprisedOneHand\"") == std::string::npos);
	REQUIRE(guard.find("\"aiBeSurprisedLookAround\"") == std::string::npos);
	REQUIRE(guard.find("\"aiBeSurprisedSurrender\"") == std::string::npos);
	REQUIRE(guard.find("\"aiKillBond\"") == std::string::npos);
	REQUIRE(guard.find("\"aiFaceEntity\"") == std::string::npos);
	REQUIRE(guard.find("\"ai0019\"") == std::string::npos);
	REQUIRE(guard.find("\"aiChrDamageChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiConsiderGrenadeThrow\"") == std::string::npos);
	REQUIRE(guard.find("\"aiDropItem\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryRunFromTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryJogToTargetProp\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryWalkToTargetProp\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryRunToTargetProp\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryGoToCoverProp\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryJogToChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryWalkToChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiTryRunToChr\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfCanHearAlarm\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfPatrolling\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfAlarmActive\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfGasActive\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfHearsTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfSawInjury\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfSawDeath\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfLosToTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfLosToAttackTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfTargetNearlyInSight\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfNearlyInTargetsSight\"") == std::string::npos);
	REQUIRE(guard.find("\"aiSetPadPresetToPadOnRouteToTarget\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfSawTargetRecently\"") == std::string::npos);
	REQUIRE(guard.find("\"aiIfHeardTargetRecently\"") == std::string::npos);
	REQUIRE(guard.find("\"aiSayQuip\"") == std::string::npos);
	REQUIRE(guard.find("\"aiSayCiStaffQuip\"") == std::string::npos);
	REQUIRE(guard.find("\"aiShuffleRuinsPillars\"") == std::string::npos);
	REQUIRE(guard.find("\"aiShufflePelagicSwitches\"") == std::string::npos);
	for (const char *name : {
		     "\"aiIfLosToChr\"",
		     "\"aiIfNeverBeenOnScreen\"",
		     "\"aiIfOnScreen\"",
		     "\"aiIfChrInOnScreenRoom\"",
		     "\"aiIfRoomIsOnScreen\"",
		     "\"aiIfTargetAimingAtMe\"",
		     "\"aiIfNearMiss\"",
		     "\"aiIfSeesSuspiciousItem\"",
		     "\"aiIfCheckFovWithTarget\"",
		     "\"aiIfTargetInFovLeft\"",
		     "\"aiIfTargetOutOfFovLeft\"",
		     "\"aiIfTargetInFov\"",
		     "\"aiIfTargetOutOfFov\"",
		     "\"aiIfDistanceToTargetLessThan\"",
		     "\"aiIfDistanceToTargetGreaterThan\"",
		     "\"aiIfChrDistanceToPadLessThan\"",
		     "\"aiIfChrDistanceToPadGreaterThan\"",
		     "\"aiIfDistanceToChrLessThan\"",
		     "\"aiIfDistanceToChrGreaterThan\"",
		     "\"ai0058\"",
		     "\"aiIfDistanceFromTargetToPadLessThan\"",
		     "\"aiIfDistanceFromTargetToPadGreaterThan\"",
		     "\"aiIfChrInRoom\"",
		     "\"aiIfTargetInRoom\"",
		     "\"aiIfChrHasObject\"",
		     "\"aiIfWeaponThrown\"",
		     "\"aiIfWeaponThrownOnObject\"",
		     "\"aiIfChrHasWeaponEquipped\"",
		     "\"aiIfGunUnclaimed\"",
		     "\"aiIfObjectHealthy\"",
		     "\"ai0075\"",
		     "\"aiSetPadPresetToTargetQuadrant\"",
		     "\"ai00e9\"",
		     "\"ai00fd\"",
		     "\"ai012f\"",
		     "\"ai0184\"",
		     "\"aiEndLevel\"",
		     "\"ai00dd\"",
		     "\"aiWarpJoToPad\"",
		     "\"aiSetCameraAnimation\"",
		     "\"aiIfInCutscene\"",
		     "\"aiIfCutsceneButtonPressed\"",
		     "\"ai0175\"",
		     "\"ai00df\"",
		     "\"aiRevokeControl\"",
		     "\"aiGrantControl\"",
		     "\"ai00e3\"",
		     "\"ai00e4\"",
		     "\"aiIfColourFadeComplete\"",
		     "\"ai00f4\"",
		     "\"ai00f5\"",
		     "\"ai00f6\"",
		     "\"aiSpawnChrAtPad\"",
		     "\"aiSpawnChrAtChr\"",
		     "\"aiTryEquipWeapon\"",
		     "\"aiTryEquipHat\"",
		     "\"aiSetObjImage\"",
		     "\"aiObjectDoAnimation\"",
		     "\"aiSetDoorOpen\"",
		     "\"aiDuplicateChr\"",
		     "\"aiEnableChr\"",
		     "\"aiDisableChr\"",
		     "\"aiEnableObj\"",
		     "\"aiDisableObj\"",
		     "\"aiChrMoveToPad\"",
		     "\"aiChrSetTeam\"",
		     "\"aiDamageChrByAmount\"",
		     "\"aiDoPresetAnimation\"",
		     "\"ai01aa\"",
		     "\"ai01b4\"",
		     "\"aiIfChrNotTalking\"",
		     "\"aiIfOrders\"",
		     "\"aiIfHasOrders\"",
		     "\"aiIfChrInSquadronDoingAction\"",
		     "\"aiIfChrListening\"",
		     "\"aiIfNotListening\"",
		     "\"aiIfChrInjured\"",
		     "\"aiIfAction\"",
		     "\"aiIfChrAmmoQuantityLessThan\"",
		     "\"aiIfChrTarget\"",
		     "\"aiIfCompareChrPresetsTeam\"",
		     "\"aiIfHuman\"",
		     "\"aiIfSkedar\"",
		     "\"aiIfPropPresetIsBlockingSightToTarget\"",
		     "\"aiRemoveObjectAtPropPreset\"",
		     "\"aiIfPropPresetHeightLessThan\"",
		     "\"aiSetTarget\"",
		     "\"aiIfPresetsTargetIsNotMyTarget\"",
		     "\"aiSetChrPresetToChrNearSelf\"",
		     "\"aiSetChrPresetToChrNearPad\"",
		     "\"aiIfDangerousObjectNearby\"",
		     "\"aiIfHeliWeaponsArmed\"",
		     "\"aiIfHoverbotNextStep\"",
		     "\"aiShuffleInvestigationTerminals\"",
		     "\"aiSetPadPresetToInvestigationTerminal\"",
		     "\"aiHeliArmWeapons\"",
		     "\"aiHeliUnarmWeapons\"",
		     "\"aiIfSafety2LessThan\"",
		     "\"aiIfPlayerUsingCmpOrAr34\"",
		     "\"aiDetectEnemyOnSameFloor\"",
		     "\"aiDetectEnemy\"",
		     "\"aiIfSafetyLessThan\"",
		     "\"aiIfTargetMovingSlowly\"",
		     "\"aiIfTargetMovingCloser\"",
		     "\"aiIfTargetMovingAway\"",
		     "\"aiIfSquadronIsDead\"",
		     "\"aiIfTrue\"",
		     "\"aiIfNumChrsInSquadronGreaterThan\"",
		     "\"aiIfNaturalAnim\"",
		     "\"aiIfY\"",
		     "\"aiIfSoundTimer\"",
		     "\"aiIfTargetYDifferenceLessThan\"",
	     }) {
		REQUIRE(guard.find(name) == std::string::npos);
	}
	REQUIRE(guard.find("untracked debt") != std::string::npos);
	REQUIRE(guard.find("Build/data/ntsc-final/scenarios") !=
	        std::string::npos);
	REQUIRE(guard.find("Build/data/ntsc-final/arenas") != std::string::npos);
	REQUIRE(guard.find(".pdarena") != std::string::npos);
	REQUIRE(guard.find("validate_scene_glb_texture_contract") !=
	        std::string::npos);
	REQUIRE(arena.find("\"stagenum = %d\\n\"") == std::string::npos);
	REQUIRE(scanner.find("parseModeKeyValue") != std::string::npos);
	REQUIRE(scanner.find("parseBotTypeKeyValue") != std::string::npos);
	REQUIRE(scanner.find("parseHudElementKeyValue") != std::string::npos);
	REQUIRE(scanner.find("parsePropKeyValue") != std::string::npos);
	REQUIRE(scanner.find("parseEffectTypeKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseModeKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseHudElementKeyValue") !=
	        std::string::npos);
	REQUIRE(distrib.find("distribParsePropKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseEffectTypeKeyValue") !=
	        std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"music_file\", iniGet(ini, \"midi_file\", \"\"))") !=
	        std::string::npos);
	REQUIRE(distrib.find("assetCatalogRegisterAudioIni(slot->id") != std::string::npos);
	REQUIRE(scanner.find("catalogAudioPublicSourceParse(ini, audio_category != AUDIO_CAT_MUSIC") != std::string::npos);
	REQUIRE(scanner.find("catalogAudioPublicSourceApply(e, &audio_public)") != std::string::npos);
	const std::string audio_public_fields = readTextFile("port/src/catalog_audio_public_source.cpp");
	REQUIRE(audio_public_fields.find("COPY(file_path)") != std::string::npos);
	REQUIRE(scanner.find("strncpy(e->ext.font.font_file, pf") !=
	        std::string::npos);
	REQUIRE(scanner.find("strncpy(e->ext.font.metrics_file, mf") !=
	        std::string::npos);
	REQUIRE(distrib.find("strncpy(e->ext.font.font_file, pf") !=
	        std::string::npos);
	REQUIRE(distrib.find("strncpy(e->ext.font.metrics_file, mf") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.font.font_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.font.metrics_file") != std::string::npos);
	REQUIRE(runtime.find("s_fontSourceComplete") != std::string::npos);
	/* Slice anchors below start at the binding switch ("switch
	 * (entry->type)", the established pattern earlier in this file).
	 * Anchoring at the file's FIRST "case ASSET_FONT:" broke when the
	 * early type-support switch ("switch (type)") gained an adjacent
	 * "case ASSET_LANG:" label, shrinking the slice to two lines. */
	const size_t runtime_binding_switch = runtime.find("switch (entry->type)");
	REQUIRE(runtime_binding_switch != std::string::npos);
	{
		const size_t font_start = runtime.find("case ASSET_FONT:",
			runtime_binding_switch);
		const size_t lang_start = runtime.find("case ASSET_LANG:", font_start);
		REQUIRE(font_start != std::string::npos);
		REQUIRE(lang_start != std::string::npos);
		const std::string font_case =
			runtime.substr(font_start, lang_start - font_start);
		REQUIRE(font_case.find("entry->ext.font.font_file") !=
		        std::string::npos);
		REQUIRE(font_case.find("entry->ext.font.metrics_file") !=
		        std::string::npos);
		REQUIRE(font_case.find("s_fontSourceComplete(binding)") !=
		        std::string::npos);
		REQUIRE(font_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	{
		const size_t lang_start = runtime.find("case ASSET_LANG:",
			runtime_binding_switch);
		const size_t scenario_start = runtime.find("case ASSET_SCENARIO:",
			lang_start);
		REQUIRE(lang_start != std::string::npos);
		REQUIRE(scenario_start != std::string::npos);
		const std::string lang_case =
			runtime.substr(lang_start, scenario_start - lang_start);
		REQUIRE(lang_case.find("entry->ext.lang.strings_file") !=
		        std::string::npos);
		REQUIRE(lang_case.find("entry->ext.lang.bank_id") !=
		        std::string::npos);
		REQUIRE(lang_case.find("entry->ext.lang.bank_id >= 0") !=
		        std::string::npos);
		REQUIRE(lang_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	REQUIRE(examples.find("mode_key = combat") != std::string::npos);
	REQUIRE(examples.find("manifest_with(\"gamemode\"") != std::string::npos);
	REQUIRE(examples.find("\"rules_file\": \"rules.json\"") != std::string::npos);
	REQUIRE(examples.find("\"max_players\": 8") != std::string::npos);
	REQUIRE(examples.find("manifest_with(\"botprofile\"") != std::string::npos);
	REQUIRE(examples.find("\"target_body\": \"example:tri_body\"") !=
	        std::string::npos);
	REQUIRE(examples.find("\"profile_file\": \"profile.json\"") !=
	        std::string::npos);
	REQUIRE(examples.find("hud_key = ammo") != std::string::npos);
	REQUIRE(examples.find("prop_key = object") != std::string::npos);
	REQUIRE(examples.find("audio_wav_descriptor") != std::string::npos);
	REQUIRE(examples.find("audio_wav_manifest") != std::string::npos);
	REQUIRE(examples.find("pcm16_mono_wav") != std::string::npos);
	REQUIRE(examples.find("b\"RIFF\"") != std::string::npos);
	REQUIRE(examples.find("sfx_sample = pcm16_mono_wav()") != std::string::npos);
	REQUIRE(examples.find("update_audio_examples()") != std::string::npos);
	REQUIRE(examples.find("song_sequence_descriptor") != std::string::npos);
	REQUIRE(examples.find("song_sequence_manifest") != std::string::npos);
	REQUIRE(examples.find("update_song_examples()") != std::string::npos);
	REQUIRE(examples.find("\"events_file = sequence.json") != std::string::npos);
	REQUIRE(examples.find("\\\"event_count\\\": 1") != std::string::npos);
	REQUIRE(examples.find("font_bitmap_descriptor") != std::string::npos);
	REQUIRE(examples.find("font_bitmap_manifest") != std::string::npos);
	REQUIRE(examples.find("update_font_examples()") != std::string::npos);
	REQUIRE(examples.find("\"metrics_file = font.metrics.json") !=
	        std::string::npos);
	REQUIRE(examples.find("key_base = 60") != std::string::npos);
	REQUIRE(examples.find("\\\"attack_time_us\\\": 0") != std::string::npos);
	REQUIRE(examples.find("velocity_max = 127") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.stop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.kneel") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.surrender") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.fade_out") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.remove_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_sidestep") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_attack_stand") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_attacking") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_modify_attack") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.face_entity") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.apply_gset_damage") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.chr_damage_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.consider_grenade_throw") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.drop_item") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_run_from_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_jog_to_target_prop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_walk_to_target_prop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_run_to_target_prop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_go_to_cover_prop") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_jog_to_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_walk_to_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.try_run_to_chr") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_can_hear_alarm") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_patrolling") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_alarm_active") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_gas_active") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_hears_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_saw_injury") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_saw_death") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_los_to_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_los_to_attack_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_target_nearly_in_sight") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_nearly_in_targets_sight") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.set_pad_preset_to_pad_on_route_to_target") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_saw_target_recently") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_heard_target_recently") != std::string::npos);
	for (const char *kind : {
		     "scenario.ai.condition.if_los_to_chr",
		     "scenario.ai.condition.if_never_been_on_screen",
		     "scenario.ai.condition.if_on_screen",
		     "scenario.ai.condition.if_chr_in_on_screen_room",
		     "scenario.ai.condition.if_room_is_on_screen",
		     "scenario.ai.condition.if_target_aiming_at_me",
		     "scenario.ai.condition.if_near_miss",
		     "scenario.ai.condition.if_sees_suspicious_item",
		     "scenario.ai.condition.if_target_in_fov_left",
		     "scenario.ai.condition.if_check_fov_with_target",
		     "scenario.ai.condition.if_target_out_of_fov_left",
		     "scenario.ai.condition.if_target_in_fov",
		     "scenario.ai.condition.if_target_out_of_fov",
		     "scenario.ai.condition.if_distance_to_target_less_than",
		     "scenario.ai.condition.if_distance_to_target_greater_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_less_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_greater_than",
		     "scenario.ai.condition.if_distance_to_chr_less_than",
		     "scenario.ai.condition.if_distance_to_chr_greater_than",
		     "scenario.ai.condition.if_any_chr_near_self",
		     "scenario.ai.condition.if_distance_from_target_to_pad_less_than",
		     "scenario.ai.condition.if_distance_from_target_to_pad_greater_than",
		     "scenario.ai.condition.if_chr_in_room",
		     "scenario.ai.condition.if_target_in_room",
		     "scenario.ai.condition.if_chr_has_object",
		     "scenario.ai.condition.if_weapon_thrown",
		     "scenario.ai.condition.if_weapon_thrown_on_object",
		     "scenario.ai.condition.if_chr_has_weapon_equipped",
		     "scenario.ai.condition.if_gun_unclaimed",
		     "scenario.ai.condition.if_object_healthy",
		     "scenario.ai.condition.if_chr_activated_object",
		     "scenario.ai.action.obj_interact",
		     "scenario.ai.action.destroy_object",
		     "scenario.ai.action.drop_object_from_chr",
		     "scenario.ai.action.chr_drop_items",
		     "scenario.ai.action.chr_drop_weapon",
		     "scenario.ai.action.give_object_to_chr",
		     "scenario.ai.action.object_move_to_pad",
		     "scenario.ai.action.say_quip",
		     "scenario.ai.action.say_ci_staff_quip",
		     "scenario.ai.action.shuffle_ruins_pillars",
		     "scenario.ai.action.shuffle_pelagic_switches",
		     "scenario.ai.condition.if_waypoint_within_quadrant",
		     "scenario.ai.action.set_pad_preset_to_target_quadrant",
	     }) {
		REQUIRE(examples.find(kind) != std::string::npos);
	}
	REQUIRE(examples.find("scenario.ai.action.chr_do_animation") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.be_surprised_one_hand") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.be_surprised_look_around") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.be_surprised_surrender") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.random") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.kill_bond") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_random_less_than") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.condition.if_random_greater_than") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.print") != std::string::npos);
	REQUIRE(examples.find("scenario.ai.action.noop") != std::string::npos);
	REQUIRE(texture_extractor.find("\"empty_rom_slot = %s\\n\"") ==
	        std::string::npos);
	REQUIRE(texture_extractor.find("\\\"source_state\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(theme.find("fsDataPathFor(rel, out, out_n)") !=
	        std::string::npos);
	REQUIRE(theme.find("(void)romExtractAllPdtheme(0)") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.skin_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.swatches_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.material_archive") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.texture_archive") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.prop.prop_file") !=
	        std::string::npos);
	REQUIRE(runtime.find("entry->ext.prop.behavior_graph") !=
	        std::string::npos);
	REQUIRE(file_provider.find("FILE_PROVIDER_POOL_BYTES   (1024 * 1024)") !=
	        std::string::npos);
	REQUIRE(file_provider.find("FILE_PROVIDER_MAX_PATHS    16384") !=
	        std::string::npos);
}

TEST_CASE("typed asset extractors do not leave zip inspection artifacts",
          "[modding][pdxxx][c3842][extract][static]") {
	const std::string writer = readTextFile("port/src/asset_archive_writer.c");
	const std::string writer_h =
		readTextFile("port/include/asset_archive_writer.h");
	REQUIRE(writer.find("assetArchiveWriterAddBundledMem") !=
	        std::string::npos);
	REQUIRE(writer_h.find("assetArchiveWriterAddBundledMem") !=
	        std::string::npos);

	const std::string arena = readTextFile("port/src/romextract_pdarena.c");
	REQUIRE(arena.find("s_cleanupLegacyPdarenaZipSiblings") !=
	        std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDARENA_FAST_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(arena.find("ROMEXTRACT_PDSCENARIO_FAST_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntryPrefix") !=
	        std::string::npos);
	REQUIRE(arena.find("s_addScenarioArchiveDependency") !=
	        std::string::npos);
	REQUIRE(arena.find("\"scenario/\"") != std::string::npos);
	REQUIRE(arena.find("\"_meta/scenario/\"") != std::string::npos);
	REQUIRE(arena.find("\"_meta/_meta/\"") != std::string::npos);
	REQUIRE(arena.find("s_pdarenaOutputsCleanForFastCache") !=
	        std::string::npos);
	REQUIRE(arena.find("fast-cache blocked by stale archive") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicDisk(writer, dst_name") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddBundledMem(&asset_writer, \"_meta/generated-collision.json\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\"scenario/_meta/generated-collision.json\"") ==
	        std::string::npos);

	const std::string mesh = readTextFile("port/src/romextract_pdmesh.c");
	REQUIRE(mesh.find("s_removeLegacyZipForMesh") != std::string::npos);
	REQUIRE(mesh.find("romextract pdmesh: removed stale typed-archive zip") !=
	        std::string::npos);
}

/* ========================================================================
 * c3849 Wave 4 Slice B -- texture boot bind verification.
 *
 * texLoad's public-source path sysFatalErrors at FIRST USE of a texture
 * whose bound .pdtexture archive is missing (mod_texture_source.c), with
 * zero boot-time detection. romExtractAllPdtexture now ends with a
 * Gate-5-style verification pass (mirrors B-908): one LOG_WARNING per
 * miss (catalog id + path) plus a "TEXTURE.BIND: n missing of m" summary.
 * These pins lock the shape: actual bound handle, "::" split, stat-only
 * cost, and execution on BOTH the emit and fast-cache skip paths.
 * ======================================================================== */

TEST_CASE("c3849 Slice B: texture bind verification is loud, cheap, and runs on both paths",
          "[modding][pdxxx][c3849][texture][bind][static]") {
	const std::string texture_extractor =
		readTextFile("port/src/romextract_pdtexture.c");

	/* The pass exists and self-identifies in the log. */
	REQUIRE(texture_extractor.find("TEXTURE.BIND") != std::string::npos);

	const std::string verify_block =
		functionBlock(texture_extractor, "static void s_verifyTextureBinds(void)");
	REQUIRE(!verify_block.empty());

	/* It checks the ACTUAL bound primary handle, not a re-derived path --
	 * a registration bug that binds the wrong path must be caught too. */
	REQUIRE(verify_block.find("fileProviderPath(e->source.primary)") !=
	        std::string::npos);
	/* Split the bound path at "::" and stat only the archive half. */
	REQUIRE(verify_block.find("strstr(archive, \"::\")") !=
	        std::string::npos);
	REQUIRE(verify_block.find("fsFileSize(archive)") != std::string::npos);
	/* Cheap by contract: no ZIP opens inside the verification pass. */
	REQUIRE(verify_block.find("modArchiveOpen") == std::string::npos);
	/* One LOG_WARNING per miss with catalog id + path, plus the summary. */
	REQUIRE(verify_block.find("LOG_WARNING") != std::string::npos);
	REQUIRE(verify_block.find("has no FileProvider primary bound") !=
	        std::string::npos);
	REQUIRE(verify_block.find("missing archive") != std::string::npos);
	REQUIRE(verify_block.find("%d missing of %d") != std::string::npos);
	REQUIRE(verify_block.find("0 missing of %d") != std::string::npos);
	/* Same bundled ASSET_TEXTURE predicate as the emit loop. */
	REQUIRE(verify_block.find("e->type != ASSET_TEXTURE") !=
	        std::string::npos);

	/* Runs on BOTH paths: the fast-cache skip early-out AND the full emit
	 * path. The skip path is the one a warm boot takes every time, so
	 * skipping verification there would defeat the point of the pass. */
	const std::string extract_all =
		functionBlock(texture_extractor, "s32 romExtractAllPdtexture");
	REQUIRE(!extract_all.empty());
	REQUIRE(countOccurrences(extract_all, "s_verifyTextureBinds();") == 2);
	requireTokenOrder(extract_all, "fast-cache)",
		"s_verifyTextureBinds();");
	requireTokenOrder(extract_all, "romExtractPdFastCacheWrite",
		"\n\ts_verifyTextureBinds();");
}

/* ========================================================================
 * c3849 Wave 4 Slice B -- unified catalog filename slug.
 *
 * The emitter names the archive on disk and registration builds the bound
 * FileProvider primary path; both sides must produce the IDENTICAL slug
 * or boot completes fine and the first texLoad of the divergent texture
 * is fatal. Slice B unified the texture pair onto the shared
 * catalogIdToFilenameSlug (port/include/assetcatalog_slug.h). The
 * remaining private copy in romextract_pdmeta.c (used by the non-texture
 * meta families that assetcatalog_base_extended.c also binds) is pinned
 * byte-identical to the shared body below.
 * ======================================================================== */

TEST_CASE("c3849 Slice B: shared catalog filename slug produces stable slugs",
          "[modding][pdxxx][c3849][slug]") {
	char out[64];

	/* Separators (':' '/' '\\') map to '_'; everything else verbatim. */
	catalogIdToFilenameSlug("base:texture_0000", out, sizeof(out));
	REQUIRE(std::string(out) == "base_texture_0000");

	catalogIdToFilenameSlug("base:weapon/falcon\\mk2", out, sizeof(out));
	REQUIRE(std::string(out) == "base_weapon_falcon_mk2");

	/* Case and dots are PRESERVED -- the slug is not lowercased, and any
	 * future "improvement" is a re-emit migration for existing installs. */
	catalogIdToFilenameSlug("Base:TeX/SubDir\\Name.01", out, sizeof(out));
	REQUIRE(std::string(out) == "Base_TeX_SubDir_Name.01");

	/* All-separator input. */
	catalogIdToFilenameSlug(":/\\", out, sizeof(out));
	REQUIRE(std::string(out) == "___");

	/* Empty and NULL ids produce the empty slug, never garbage. */
	memset(out, 'x', sizeof(out));
	catalogIdToFilenameSlug("", out, sizeof(out));
	REQUIRE(out[0] == '\0');
	memset(out, 'x', sizeof(out));
	catalogIdToFilenameSlug(NULL, out, sizeof(out));
	REQUIRE(out[0] == '\0');

	/* Long ids truncate to out_n - 1 plus NUL (catalog ids are capped at
	 * CATALOG_ID_LEN 64, same as this buffer). */
	const std::string long_id =
		"base:" + std::string(90, 'a') + ":tail";
	catalogIdToFilenameSlug(long_id.c_str(), out, sizeof(out));
	REQUIRE(std::strlen(out) == sizeof(out) - 1);
	REQUIRE(std::string(out) == "base_" + std::string(58, 'a'));

	/* Degenerate buffer sizes stay safe. */
	char tiny[1];
	tiny[0] = 'x';
	catalogIdToFilenameSlug("base:texture_0000", tiny, sizeof(tiny));
	REQUIRE(tiny[0] == '\0');
	catalogIdToFilenameSlug("base:texture_0000", NULL, 0);
}

TEST_CASE("c3849 Slice B: filename slug is unified for the texture pair and pinned elsewhere",
          "[modding][pdxxx][c3849][slug][static]") {
	const std::string slug_header =
		readTextFile("port/include/assetcatalog_slug.h");
	const std::string texture_extractor =
		readTextFile("port/src/romextract_pdtexture.c");
	const std::string base =
		readTextFile("port/src/assetcatalog_base_extended.c");
	const std::string meta = readTextFile("port/src/romextract_pdmeta.c");

	/* The texture pair (emitter archive name + bound primary path) calls
	 * the ONE shared helper; the private copies are gone. */
	REQUIRE(texture_extractor.find("#include \"assetcatalog_slug.h\"") !=
	        std::string::npos);
	REQUIRE(base.find("#include \"assetcatalog_slug.h\"") !=
	        std::string::npos);
	REQUIRE(texture_extractor.find("s_idToFilenameSlug") ==
	        std::string::npos);
	REQUIRE(base.find("s_catalogIdToFilenameSlug") == std::string::npos);
	{
		const std::string rel_path_block =
			functionBlock(texture_extractor, "static void s_archiveRelPath");
		REQUIRE(rel_path_block.find(
		                "catalogIdToFilenameSlug(id, slug, sizeof(slug))") !=
		        std::string::npos);
		const std::string member_path_block =
			functionBlock(base, "static void s_buildArchiveMemberPath");
		REQUIRE(member_path_block.find(
		                "catalogIdToFilenameSlug(id, slug, sizeof(slug))") !=
		        std::string::npos);
	}

	/* romextract_pdmeta.c keeps a private s_idToFilenameSlug for the
	 * non-texture meta families (out of Slice B's allowed surface); those
	 * archives are ALSO bound by s_buildArchiveMemberPath, so its body
	 * must stay byte-identical to the shared helper. Drift here is the
	 * same boot-fine-then-fatal failure the unification removed for
	 * textures. Fix drift by re-pointing pdmeta at assetcatalog_slug.h,
	 * not by editing either body in place. */
	const auto bracedBody = [](const std::string &text, const char *name) {
		const std::string block = functionBlock(text, name);
		const size_t brace = block.find('{');
		if (brace == std::string::npos) {
			return std::string();
		}
		return block.substr(brace);
	};
	const std::string shared_body = bracedBody(slug_header,
		"static inline void catalogIdToFilenameSlug");
	const std::string meta_body = bracedBody(meta,
		"static void s_idToFilenameSlug");
	REQUIRE(!shared_body.empty());
	REQUIRE(!meta_body.empty());
	REQUIRE(shared_body == meta_body);
}

/* ------------------------------------------------------------------------
 * c3849 Wave 6a (Unit 10) -- meta-family runtime consumers.
 *
 * Native systems now require active public-source bindings for bot profiles,
 * game modes, and catalog themes. Native mirrors and provider-path fallbacks
 * are not acceptable after extraction.
 * ---------------------------------------------------------------------- */

TEST_CASE("c3849 Wave 6a: meta-family runtime consumers feed native systems",
          "[modding][pdxxx][runtime][c3849][meta][static]") {
	const std::string mplayer = readTextFile("src/game/mplayer/mplayer.c");
	const std::string setup = readTextFile("src/game/mplayer/setup.c");
	const std::string scenarios = readTextFile("src/game/mplayer/scenarios.c");
	const std::string theme_loader =
		readTextFile("port/fast3d/pdgui_theme_loader.cpp");
	const std::string mplayer_h =
		readTextFile("src/include/game/mplayer/mplayer.h");

	/* Shared helper is declared in the header both consumers include. */
	REQUIRE(mplayer_h.find(
		"const struct asset_runtime_binding *mpBotProfileRuntimeBinding(s32 profilenum)") !=
		std::string::npos);

	/* Bot-profile lookup by permanent catalog ID requires a readable binding
	 * and fails closed instead of using g_BotProfiles as authored truth. */
	const std::string helper = functionBlock(mplayer,
		"const struct asset_runtime_binding *mpBotProfileRuntimeBindingById");
	REQUIRE(!helper.empty());
	REQUIRE(helper.find("assetCatalogResolve(profile_id)") != std::string::npos);
	REQUIRE(helper.find("assetRuntimeFindByTypeAndId(ASSET_BOT_PROFILE") !=
	        std::string::npos);
	REQUIRE(helper.find("assetRuntimePrimaryFileAccessible") !=
	        std::string::npos);
	REQUIRE(helper.find("ASSET.CHAIN: bot profile") != std::string::npos);
	REQUIRE(helper.find("CATALOG.BOTPROFILE.RUNTIME_MISS") ==
	        std::string::npos);

	/* mpCreateBotFromProfileId consumes only binding values and retains ID. */
	const std::string create =
		functionBlock(mplayer, "s32 mpCreateBotFromProfileId");
	REQUIRE(!create.empty());
	REQUIRE(create.find("mpBotProfileRuntimeBindingById(profile_id)") !=
	        std::string::npos);
	REQUIRE(create.find("bot_profile_type") != std::string::npos);
	REQUIRE(create.find("bot_profile_difficulty") != std::string::npos);
	REQUIRE(create.find("profile->target_id") != std::string::npos);
	REQUIRE(create.find("g_BotConfigsArray[botnum].profile_id") !=
	        std::string::npos);
	REQUIRE(create.find("g_BotProfiles[profilenum]") == std::string::npos);

	/* Simulant menu apply site consumes the same helper without fallback. */
	REQUIRE(setup.find("mpBotProfileRuntimeBindingById(ctx.result_id)") !=
	        std::string::npos);
	REQUIRE(setup.find("profile->bot_profile_type") != std::string::npos);
	REQUIRE(setup.find("g_BotProfiles[profnum]") == std::string::npos);

	/* B-953: gamemode selection is public-source-owned. The helper requires
	 * an active, readable rules binding; the picker consumes team and
	 * participant bounds; public name/description feed real menu text. */
	const std::string binding_helper =
		functionBlock(scenarios, "static const asset_runtime_binding_t *scenarioBindingForEntry");
	REQUIRE(!binding_helper.empty());
	REQUIRE(binding_helper.find("assetRuntimeFindByTypeAndId(ASSET_GAMEMODE") !=
	        std::string::npos);
	REQUIRE(binding_helper.find("assetRuntimePrimaryFileAccessible") !=
	        std::string::npos);
	REQUIRE(binding_helper.find("ASSET.CHAIN: gamemode") !=
	        std::string::npos);
	REQUIRE(binding_helper.find("ext.gamemode") == std::string::npos);

	const std::string accepts =
		functionBlock(scenarios, "static bool scenarioCtxAccepts");
	REQUIRE(!accepts.empty());
	REQUIRE(accepts.find("scenarioBindingForEntry") != std::string::npos);
	REQUIRE(accepts.find("gamemode_team_based") != std::string::npos);
	REQUIRE(accepts.find("mpGetActiveParticipantCount") != std::string::npos);
	REQUIRE(accepts.find("gamemode_min_players") != std::string::npos);
	REQUIRE(accepts.find("gamemode_max_players") != std::string::npos);
	REQUIRE(accepts.find("ext.gamemode") == std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_name") != std::string::npos);
	REQUIRE(scenarios.find("binding->gamemode_description") !=
	        std::string::npos);
	REQUIRE(scenarios.find(
		"(uintptr_t)&mpMenuTextScenarioDescription") != std::string::npos);
	REQUIRE(scenarios.find("CATALOG.GAMEMODE.RUNTIME_MISS") ==
	        std::string::npos);

	/* theme: enabled catalog-only rows require a binding and never resolve a
	 * parallel FileProvider path directly. */
	const std::string theme_reg =
		functionBlock(theme_loader, "register_catalog_theme_entry");
	REQUIRE(!theme_reg.empty());
	REQUIRE(theme_reg.find("assetRuntimeFindByTypeAndId(ASSET_THEME") !=
	        std::string::npos);
	REQUIRE(theme_reg.find("binding->primary_path") != std::string::npos);
	REQUIRE(theme_reg.find("binding->authored_file") != std::string::npos);
	REQUIRE(theme_reg.find("ASSET.CHAIN: enabled theme") !=
	        std::string::npos);
	REQUIRE(theme_reg.find("CATALOG.THEME.RUNTIME_MISS") ==
	        std::string::npos);
	REQUIRE(theme_reg.find("fileProviderPath(entry->source.primary)") ==
	        std::string::npos);
}

TEST_CASE("B-959 HUD material skin and vehicle public source owns production behavior",
          "[modding][pdxxx][b959][static]") {
	const std::string loader = readTextFile("port/src/assetcatalog_load.c");
	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");
	const std::string bondgun = readTextFile("src/game/bondgun.c");
	const std::string player = readTextFile("src/game/player.c");
	const std::string radar = readTextFile("src/game/radar.c");
	const std::string hud = readTextFile("port/fast3d/pdgui_hud.cpp");
	const std::string propobj = readTextFile("src/game/propobj.c");
	const std::string bondbike = readTextFile("src/game/bondbike.c");
	const std::string chr = readTextFile("src/game/chr.c");
	const std::string forge = readTextFile("port/src/forge/forge_runtime.c");
	const std::string scenarios = readTextFile("src/game/mplayer/scenarios.c");
	const std::string mplayer = readTextFile("src/game/mplayer/mplayer.c");

	REQUIRE(loader.find("assetRuntimeHydrateCatalogEntry(entry)") !=
	        std::string::npos);
	REQUIRE(runtime.find("binding->active = 0") != std::string::npos);
	REQUIRE(runtime.find("if (!binding || !action) return 0;") !=
	        std::string::npos);
	REQUIRE(runtime.find("case ASSET_PROP:     ok = s_hydrateProp(binding);") !=
	        std::string::npos);
	REQUIRE(runtime.find("case ASSET_GAMEMODE: ok = s_hydrateGamemode(binding);") !=
	        std::string::npos);
	REQUIRE(runtime.find(
		"case ASSET_BOT_PROFILE: ok = s_hydrateBotProfile(binding);") !=
	        std::string::npos);

	REQUIRE(bondgun.find("assetRuntimeHudElementEnabled(HUD_ELEM_AMMO)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("assetRuntimeHudElementEnabled(HUD_ELEM_CROSSHAIR)") !=
	        std::string::npos);
	REQUIRE(player.find("assetRuntimeHudElementEnabled(HUD_ELEM_HEALTH)") !=
	        std::string::npos);
	REQUIRE(radar.find("assetRuntimeHudElementEnabled(HUD_ELEM_RADAR)") !=
	        std::string::npos);
	REQUIRE(hud.find("assetRuntimeHudElement(HUD_ELEM_SCORE)") !=
	        std::string::npos);
	REQUIRE(hud.find("assetRuntimeHudElement(HUD_ELEM_TIMER)") !=
	        std::string::npos);

	REQUIRE(propobj.find("assetRuntimeVehicleForModelnum") !=
	        std::string::npos);
	REQUIRE(propobj.find("assetRuntimeVehicleHover") != std::string::npos);
	REQUIRE(propobj.find("assetRuntimeVehicleAllows(obj->modelnum, \"mount\")") !=
	        std::string::npos);
	REQUIRE(bondbike.find(
		"assetRuntimeVehicleAllows(vehicle->modelnum, \"dismount\")") !=
	        std::string::npos);
	REQUIRE(chr.find("assetRuntimeSkinAppearance") != std::string::npos);
	REQUIRE(forge.find("propGraphHealthToMaxDamage(source->prop_health)") != std::string::npos);
	REQUIRE(forge.find("propGraphHealthToMaxDamage(health)") != std::string::npos);
	REQUIRE(forge.find("propGraphHealthIsRepresentable(source->prop_health)") != std::string::npos);
	REQUIRE(runtime.find("propGraphHealthIsRepresentable(binding->prop_health)") != std::string::npos);
	REQUIRE(forge.find("source->prop_flags") != std::string::npos);
	REQUIRE(scenarios.find("!binding || !binding->source_hydrated") !=
	        std::string::npos);
	REQUIRE(mplayer.find("!binding || !binding->source_hydrated") !=
	        std::string::npos);

	REQUIRE(conformance.find("pd2.prop.v2") != std::string::npos);
	REQUIRE(conformance.find("pd2.gamemode.rules.v2") != std::string::npos);
	REQUIRE(conformance.find("pd2.botprofile.v2") != std::string::npos);
	REQUIRE(conformance.find("validate_vehicle_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.vehicle.physics.v2") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.vehicle.behavior.v2") !=
	        std::string::npos);
	REQUIRE(conformance.find("validate_hud_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.hud.layout.v1") != std::string::npos);
	REQUIRE(conformance.find("validate_material_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.material.v1") != std::string::npos);
	REQUIRE(conformance.find("validate_skin_source_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.skin.v1") != std::string::npos);
	REQUIRE(conformance.find("pd2.skin.swatches.v1") !=
	        std::string::npos);
}

TEST_CASE("pdvoice public metadata feeds production subtitles",
          "[modding][pdxxx][voice][t-assets-011][static]") {
	const std::string walker = readTextFile("port/src/loader_walker_voice.c");
	const std::string common = readTextFile("port/src/loader_walker_common.c");
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	const std::string distrib = readTextFile("port/src/net/netdistrib.c");
	const std::string runtime = readTextFile("port/src/scenario_source_runtime.c");

	REQUIRE(walker.find("loaderWalkerArchiveTextMember(file_path, \"voice.ini\"") !=
	        std::string::npos);
	REQUIRE(walker.find("assetCatalogRegisterBaseAudioSource") != std::string::npos);
	REQUIRE(walker.find("iniParseBuffer(\"voice.ini\"") != std::string::npos);
	REQUIRE(common.find("modArchiveFindEntry(arc, member)") != std::string::npos);
	REQUIRE(scanner.find("catalogAudioPublicSourceApply") != std::string::npos);
	REQUIRE(distrib.find("assetCatalogRegisterAudioIni(slot->id") != std::string::npos);
	const std::string audio_parser = readTextFile("port/src/catalog_audio_public_source.cpp");
	REQUIRE(audio_parser.find("COPY(\"transcript\", voice_transcript, 0)") != std::string::npos);
	REQUIRE(audio_parser.find("COPY(voice_transcript)") != std::string::npos);
	REQUIRE(runtime.find("s_voiceSourceSubtitle(audio_source_id") !=
	        std::string::npos);
	REQUIRE(runtime.find("audio.voice_actor, audio.voice_transcript") !=
	        std::string::npos);
	REQUIRE(runtime.find("s_voiceLanguageMatchesActive(audio.voice_language)") !=
	        std::string::npos);
	REQUIRE(runtime.find("hudmsgCreateAsSubtitle(text") != std::string::npos);
}

TEST_CASE("T-ASSETS-023 declared weapon reticles fail closed without public UI source",
		"[modding][pdxxx][c3842][weapon][reticle][static]") {
	const std::string runtime = readTextFile("port/src/weapon_graph_runtime.c");
	const std::string theme = readTextFile("port/fast3d/pdgui_theme.cpp");
	const std::string reticle =
		readTextFile("port/fast3d/pdgui_weapon_reticle.cpp");

	REQUIRE(runtime.find("!entry || entry->type != ASSET_UI || !entry->enabled") !=
		std::string::npos);
	REQUIRE(runtime.find("fsFileLoad(entry->ext.ui.texture_file") !=
		std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeClearWeapon(weaponnum);") !=
		std::string::npos);
	REQUIRE(theme.find("never synthesize a fallback") != std::string::npos);
	REQUIRE(reticle.find("PDWEAPON.RETICLE.REJECT") != std::string::npos);
	REQUIRE(reticle.find("pdguiThemeGetTexture(s_CatalogId)") !=
		std::string::npos);
}

TEST_CASE("V-009 Needler edited tint variants reach the production renderer audit",
		"[modding][pdxxx][effect_graph][v009][needler_ab]") {
	const std::string builder = readTextFile("tools/build_needler_mod.py");
	const std::string renderer = readTextFile("src/game/effect_presentation_renderer.c");
	const std::string runner = readTextFile("tools/smoke-verify/run-needler-effect-ab.ps1");

	REQUIRE(builder.find("DEFAULT_BURST_TINT = (1.0, 0.4, 0.8, 1.0)") !=
	        std::string::npos);
	REQUIRE(builder.find("HELD_WEAPON_SCALE = 5.0") != std::string::npos);
	REQUIRE(builder.find("HELD_WEAPON_SCALE = 14.0") == std::string::npos);
	REQUIRE(builder.find("HELD_WEAPON_POS_X = 30.0") != std::string::npos);
	REQUIRE(builder.find("HELD_WEAPON_POS_Y = -18.0") != std::string::npos);
	/* B-1155: the held Needler is authored muzzle +Z now, so its view depth
	 * moved from -45 to -34 to keep the rear of the gun where it was. */
	REQUIRE(builder.find("HELD_WEAPON_POS_Z = -34.0") != std::string::npos);
	REQUIRE(builder.find("p[2] * HELD_WEAPON_SCALE))") != std::string::npos);
	REQUIRE(builder.find("f\"posx = {HELD_WEAPON_POS_X:.1f}\\n\"") !=
	        std::string::npos);
	REQUIRE(builder.find("f\"posy = {HELD_WEAPON_POS_Y:.1f}\\n\"") !=
	        std::string::npos);
	REQUIRE(builder.find("f\"posz = {HELD_WEAPON_POS_Z:.1f}\\n\"") !=
	        std::string::npos);
	REQUIRE(builder.find("--output-dir") != std::string::npos);
	REQUIRE(builder.find("--burst-tint") != std::string::npos);
	REQUIRE(builder.find("--spark-tint") != std::string::npos);
	REQUIRE(builder.find("\"tint\": list(burst_tint)") != std::string::npos);
	REQUIRE(builder.find("\"tint\": list(spark_tint)") != std::string::npos);
	REQUIRE(renderer.find("effectPresentationMaterialColor(command.rgba") !=
	        std::string::npos);
	REQUIRE(renderer.find("authored_rgba=%.3f,%.3f,%.3f,%.3f") !=
	        std::string::npos);
	REQUIRE(renderer.find("effective_rgba=%.3f,%.3f,%.3f,%.3f") !=
	        std::string::npos);
	REQUIRE(renderer.find("render_rgba=%.3f,%.3f,%.3f,%.3f") !=
	        std::string::npos);
	const std::string compiler = readTextFile("port/src/modasset_compiler.c");
	REQUIRE(compiler.find("gltfApplyPrimitiveBaseColor") != std::string::npos);
	REQUIRE(compiler.find("gltf_material_base_color_factor_invalid") !=
	        std::string::npos);
	REQUIRE(compiler.find("mesh->vertices[vertex_base + i].colour = colour") !=
	        std::string::npos);
	REQUIRE(runner.find("needler_effect_ab_baseline") == std::string::npos);
	REQUIRE(runner.find("New-NeedlerVariant -Name \"baseline\"") !=
	        std::string::npos);
	REQUIRE(runner.find("New-NeedlerVariant -Name \"cyan\"") !=
	        std::string::npos);
	REQUIRE(runner.find("needler_graph_runtime_visual_smoke.json") !=
	        std::string::npos);
	REQUIRE(runner.find("packed_fixtures[0].src") != std::string::npos);
	REQUIRE(runner.find("New-Object System.Text.UTF8Encoding($false)") !=
	        std::string::npos);
	REQUIRE(runner.find("[System.IO.File]::WriteAllText(") !=
	        std::string::npos);
	REQUIRE(runner.find("foreach ($name in @(\"baseline\", \"cyan\"))") !=
	        std::string::npos);
	REQUIRE(runner.find("\"-Test\", $name") != std::string::npos);
	REQUIRE(runner.find("Get-Process -Name \"PerfectDark\", \"Updater\"") !=
	        std::string::npos);
	REQUIRE(runner.find("authored_rgba=$ExpectedRgba effective_rgba=$ExpectedRgba render_rgba=$ExpectedRgba") !=
	        std::string::npos);
}
