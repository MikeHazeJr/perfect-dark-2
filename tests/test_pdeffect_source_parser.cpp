#include "catch.hpp"

#include "pdeffect_source.h"
#include "modarchive.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

struct explosiontype {
	float rangeh, rangev, changerateh, changeratev, innersize, blastradius,
		damageradius;
	int16_t duration, propagationrate;
	float flarespeed;
	uint8_t smoketype;
	uint16_t sound;
	float damage;
};

static void parserSoundId(s32 sound_ref, char *out, size_t out_n)
{
	std::snprintf(out, out_n, "base:sfx_%d", (int)sound_ref);
}

static const char *explosionDescriptor()
{
	return "[effect]\n"
		"catalog_id = base:effect_explosion_profiles\n"
		"name = Explosion Profiles\n"
		"schema = pd.effect_graph.v2\n"
		"profile_kind = explosion\n"
		"target_policy = callsite\n"
		"attachment_policy = callsite\n"
		"priority_policy = callsite\n"
		"scorch_policy = callsite\n"
		"effect_file = effect.graph.json\n";
}

TEST_CASE("authoritative pdeffect parser accepts complete v2 rows and preserves null audio",
	"[modding][pdxxx][pdeffect][t-assets-017]")
{
	explosiontype rows[26] = {};
	rows[1].sound = 42;
	char *graph = nullptr;
	size_t graph_len = 0;
	REQUIRE(pdEffectSourceBuildExplosionGraph("base:effect_explosion_profiles",
		rows, 26, parserSoundId, &graph, &graph_len) == 0);
	pd_effect_source_info_t info = {};
	char error[256] = {};
	REQUIRE(pdEffectSourceParse(explosionDescriptor(),
		std::strlen(explosionDescriptor()), graph, graph_len,
		"base:effect_explosion_profiles", &info, error, sizeof(error)) == 1);
	CHECK(info.format == PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY);
	CHECK(info.profile_kind == PD_EFFECT_PROFILE_EXPLOSION);
	CHECK(info.profile_count == 26);
	CHECK(info.has_audio_rows == 1);
	CHECK(info.silent_audio_rows == 25);
	std::free(graph);
}

TEST_CASE("authoritative pdeffect parser rejects numeric empty missing and duplicate audio",
	"[modding][pdxxx][pdeffect][t-assets-017]")
{
	explosiontype rows[26] = {};
	char *raw = nullptr;
	size_t raw_len = 0;
	REQUIRE(pdEffectSourceBuildExplosionGraph("base:effect_explosion_profiles",
		rows, 26, parserSoundId, &raw, &raw_len) == 0);
	const std::string valid(raw, raw_len);
	std::free(raw);
	char error[256] = {};
	pd_effect_source_info_t info = {};

	for (const std::string replacement : {"123", "\"\"", "false"}) {
		std::string graph = valid;
		size_t at = graph.find("null");
		REQUIRE(at != std::string::npos);
		graph.replace(at, 4, replacement);
		CHECK(pdEffectSourceParse(explosionDescriptor(),
			std::strlen(explosionDescriptor()), graph.data(), graph.size(), nullptr,
			&info, error, sizeof(error)) == 0);
	}

	std::string duplicate = valid;
	size_t at = duplicate.find("\"audio_catalog_id\": null");
	REQUIRE(at != std::string::npos);
	duplicate.replace(at, std::strlen("\"audio_catalog_id\": null"),
		"\"audio_catalog_id\": null, \"audio_catalog_id\": null");
	CHECK(pdEffectSourceParse(explosionDescriptor(),
		std::strlen(explosionDescriptor()), duplicate.data(), duplicate.size(),
		nullptr, &info, error, sizeof(error)) == 0);
}

TEST_CASE("authoritative pdeffect parser rejects row identity omissions and unknown fields",
	"[modding][pdxxx][pdeffect][t-assets-017]")
{
	explosiontype rows[26] = {};
	char *raw = nullptr;
	size_t raw_len = 0;
	REQUIRE(pdEffectSourceBuildExplosionGraph("base:effect_explosion_profiles",
		rows, 26, parserSoundId, &raw, &raw_len) == 0);
	const std::string valid(raw, raw_len);
	std::free(raw);
	std::string graph = valid;
	char error[256] = {};
	pd_effect_source_info_t info = {};

	size_t at = graph.find("\"id\": \"none\"");
	REQUIRE(at != std::string::npos);
	graph.replace(at, std::strlen("\"id\": \"none\""), "\"id\": \"missing\"");
	CHECK(pdEffectSourceParse(explosionDescriptor(),
		std::strlen(explosionDescriptor()), graph.data(), graph.size(), nullptr,
		&info, error, sizeof(error)) == 0);

	std::string descriptor = explosionDescriptor();
	descriptor += "private_manifest_authority = forbidden\n";
	CHECK(pdEffectSourceParse(descriptor.data(), descriptor.size(), valid.data(), valid.size(),
		nullptr, &info, error, sizeof(error)) == 0);
}

TEST_CASE("legacy v1 pdeffect validates exact public identity and safe members",
	"[modding][pdxxx][pdeffect][t-assets-017]")
{
	const char *descriptor =
		"[effect]\n"
		"catalog_id = example:tri_effect\n"
		"name = Triangle Glow\n"
		"effect_key = glow\n"
		"target_key = weapon\n"
		"effect_file = effect.graph.json\n"
		"timeline_file = timeline.json\n"
		"shader_id = classic_glow\n"
		"intensity = 0.75\n";
	const char *graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":"
		"\"example:tri_effect\",\"nodes\":[],\"edges\":[]}";
	pd_effect_source_info_t info = {};
	char error[256] = {};
	REQUIRE(pdEffectSourceParse(descriptor, std::strlen(descriptor), graph,
		std::strlen(graph), "example:tri_effect", &info, error, sizeof(error)) == 1);
	CHECK(info.format == PD_EFFECT_SOURCE_FORMAT_LEGACY_GRAPH);
	CHECK(info.intensity == 0.75f);

	std::string mismatch = graph;
	mismatch.replace(mismatch.find("example:tri_effect"),
		std::strlen("example:tri_effect"), "example:other_effect");
	CHECK(pdEffectSourceParse(descriptor, std::strlen(descriptor), mismatch.data(),
		mismatch.size(), nullptr, &info, error, sizeof(error)) == 0);

	std::string unsafe = descriptor;
	unsafe.replace(unsafe.find("timeline.json"), std::strlen("timeline.json"),
		"../timeline.json");
	CHECK(pdEffectSourceParse(unsafe.data(), unsafe.size(), graph,
		std::strlen(graph), nullptr, &info, error, sizeof(error)) == 0);
}

TEST_CASE("private pdeffect manifest aliases cannot override public descriptor authority",
	"[modding][pdxxx][pdeffect][t-assets-017]")
{
	explosiontype rows[26] = {};
	char *graph = nullptr;
	size_t graph_len = 0;
	REQUIRE(pdEffectSourceBuildExplosionGraph("base:effect_explosion_profiles",
		rows, 26, parserSoundId, &graph, &graph_len) == 0);
	const std::filesystem::path path = std::filesystem::temp_directory_path() /
		"pd2_t017_manifest_disagreement.pdeffect";
	std::error_code ec;
	std::filesystem::remove(path, ec);
	mod_archive_writer_t *writer = modArchiveBegin(path.string().c_str());
	REQUIRE(writer != nullptr);
	REQUIRE(modArchiveAddFileMem(writer, "effect.ini", explosionDescriptor(),
		(u32)std::strlen(explosionDescriptor())) == MODARCHIVE_OK);
	REQUIRE(modArchiveAddFileMem(writer, "effect.graph.json", graph,
		(u32)graph_len) == MODARCHIVE_OK);
	const char *private_manifest =
		"{\"catalog_id\":\"base:effect_explosion_profiles\","
		"\"effect_file\":\"private-wrong.json\","
		"\"behavior_graph\":\"also-private-wrong.json\","
		"\"timeline_file\":\"private-wrong-timeline.json\"}";
	REQUIRE(modArchiveAddFileMem(writer, "_meta/manifest.json", private_manifest,
		(u32)std::strlen(private_manifest)) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);

	pd_effect_source_info_t info = {};
	char error[256] = {};
	INFO(error);
	REQUIRE(pdEffectSourceParseArchiveFile(path.string().c_str(),
		"base:effect_explosion_profiles", &info, error, sizeof(error)) == 1);
	CHECK(std::string(info.effect_file) == "effect.graph.json");
	CHECK(info.timeline_file[0] == '\0');
	std::free(graph);
	std::filesystem::remove(path, ec);
}

TEST_CASE("B-1047 pdeffect archive parser preserves a maximum-contract member name",
	"[modding][pdxxx][pdeffect][t-assets-017][t-catalog-003][b1047]")
{
	/* The persistent mirror must carry every member name accepted by the
	 * catalog/provider path contract. This name is intentionally far beyond
	 * the retired 256-byte field while remaining below FS_MAXPATH. */
	std::string member(900, 'r');
	member += ".graph.json";
	REQUIRE(member.size() < FS_MAXPATH);
	const std::string descriptor =
		"[effect]\n"
		"catalog_id = test:long_effect_member\n"
		"name = Long Effect Member\n"
		"effect_key = glow\n"
		"target_key = weapon\n"
		"effect_file = " + member + "\n"
		"shader_id = classic_glow\n"
		"intensity = 0.75\n";
	const char *graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":"
		"\"test:long_effect_member\",\"nodes\":[],\"edges\":[]}";
	const std::filesystem::path path = std::filesystem::temp_directory_path() /
		"pd2_b1047_long_member.pdeffect";
	std::error_code ec;
	std::filesystem::remove(path, ec);
	mod_archive_writer_t *writer = modArchiveBegin(path.string().c_str());
	REQUIRE(writer != nullptr);
	REQUIRE(modArchiveAddFileMem(writer, "effect.ini", descriptor.data(),
		(u32)descriptor.size()) == MODARCHIVE_OK);
	REQUIRE(modArchiveAddFileMem(writer, member.c_str(), graph,
		(u32)std::strlen(graph)) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);

	pd_effect_source_info_t info = {};
	char error[256] = {};
	INFO(error);
	REQUIRE(sizeof(info.effect_file) == FS_MAXPATH);
	REQUIRE(sizeof(info.timeline_file) == FS_MAXPATH);
	REQUIRE(pdEffectSourceParseArchiveFile(path.string().c_str(),
		"test:long_effect_member", &info, error, sizeof(error)) == 1);
	CHECK(std::string(info.effect_file) == member);
	std::filesystem::remove(path, ec);
}
