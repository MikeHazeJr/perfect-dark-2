#include "catch.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "modasset_gltf_source.h"
}

TEST_CASE("glTF buffer URIs retain their source archive and directory",
		"[modding][pdxxx][c3842][gltf-source]")
{
	char path[1024];
	REQUIRE(modAssetGltfBufferPath(
		"mods/pack.pdmod::weapon.pdweapon::mesh.pdmesh::source/model.gltf",
		"buffers/Body%20Geometry.bin", path, sizeof(path)) == 1);
	REQUIRE(std::string(path) ==
		"mods/pack.pdmod::weapon.pdweapon::mesh.pdmesh::source/buffers/Body Geometry.bin");
	REQUIRE(modAssetGltfBufferPath("C:\\mods\\mesh\\model.gltf",
		"geometry.bin", path, sizeof(path)) == 1);
	REQUIRE(std::string(path) == "C:\\mods\\mesh\\geometry.bin");
	REQUIRE(modAssetGltfBufferPath("mesh.pdmesh::model.gltf",
		"geometry.bin", path, sizeof(path)) == 1);
	REQUIRE(std::string(path) == "mesh.pdmesh::geometry.bin");
	REQUIRE(modAssetGltfBufferPath("mesh.pdmesh::model.gltf",
		"g%C3%A9ometry.bin", path, sizeof(path)) == 1);
	REQUIRE(std::string(path) == "mesh.pdmesh::g\xc3\xa9ometry.bin");
	REQUIRE(modAssetGltfBufferPath("mesh.pdmesh::model.gltf",
		"geometry.bin", path, sizeof(path)) == 1);
	const std::string expected(path);
	REQUIRE(modAssetGltfBufferPath("mesh.pdmesh::model.gltf", "geometry.bin",
		path, expected.size() + 1) == 1);
	REQUIRE(modAssetGltfBufferPath("mesh.pdmesh::model.gltf", "geometry.bin",
		path, expected.size()) == 0);
	REQUIRE(path[0] == '\0');
}

TEST_CASE("glTF buffer URIs reject source escapes and Windows aliases atomically",
		"[modding][pdxxx][c3842][gltf-source]")
{
	for (const char *uri : {"", "/mesh.bin", "//host/mesh.bin", "C:/mesh.bin",
		"http://host/mesh.bin", "file:mesh.bin", "../mesh.bin", "./mesh.bin",
		"buffers/../mesh.bin", ".", "..", "buffers/.", "buffers/..",
		"%2e%2e/mesh.bin", "%2fmesh.bin", "buffer%5cmesh.bin", "mesh%00.bin",
		"mesh.bin::outside", "mesh.bin:stream", "mesh.bin?query", "mesh.bin#fragment",
		"mesh%", "mesh%2", "mesh%xx", "buffers//mesh.bin", "buffers/",
		"mesh.bin.", "mesh.bin ", "CON.bin", "aux", "buffers/NUL.bin", "COM1.bin",
		"%ff.bin", "%c0%af.bin", "%ed%a0%80.bin", "%f4%90%80%80.bin", "%e2%82.bin"}) {
		INFO(uri);
		char path[1024] = "unchanged";
		REQUIRE(modAssetGltfBufferPath("pack.pdmod::mesh.pdmesh::model.gltf",
			uri, path, sizeof(path)) == 0);
		REQUIRE(path[0] == '\0');
	}
}

TEST_CASE("glTF declared buffers bound source bytes and GLB padding",
		"[modding][pdxxx][c3842][gltf-source]")
{
	REQUIRE(modAssetGltfBufferSize(36, 36, 0) == 1);
	REQUIRE(modAssetGltfBufferSize(36, 35, 0) == 0);
	REQUIRE(modAssetGltfBufferSize(36, 37, 0) == 0);
	REQUIRE(modAssetGltfBufferSize(0, 0, 0) == 0);
	REQUIRE(modAssetGltfBufferSize(36, 39, 1) == 1);
	REQUIRE(modAssetGltfBufferSize(36, 40, 1) == 0);
	REQUIRE(modAssetGltfBufferSize(0xffffffffu, 0, 1) == 0);
}

TEST_CASE("glTF embedded buffers require exact MIME base64 and decoded length",
		"[modding][pdxxx][c3842][gltf-source]")
{
	REQUIRE(modAssetGltfDataUriSize("data:application/octet-stream;base64,AQ==", 1) == 1);
	REQUIRE(modAssetGltfDataUriSize("data:application/gltf-buffer;base64,AQI=", 2) == 1);
	REQUIRE(modAssetGltfDataUriSize("data:application/octet-stream;base64,AQID", 3) == 1);
	REQUIRE(modAssetGltfDataUriSize("data:application/octet-stream;base64,AQID", 2) == 0);
	for (const char *uri : {"data:text/plain;base64,AQ==",
		"data:application/octet-stream;base64;other,AQ==", "data:;base64,AQ==",
		"data:application/octet-stream;base64,AQ", "data:application/octet-stream;base64,AQ===",
		"data:application/octet-stream;base64,A===", "data:application/octet-stream;base64,AB==",
		"data:application/octet-stream;base64,AQ=Q", "data:application/octet-stream;base64,AQ==garbage",
		"data:application/octet-stream;base64,A Q==", "data:application/octet-stream;base64,",
		"data:application/octet-stream;base64,AQ==\n"}) {
		INFO(uri);
		REQUIRE(modAssetGltfDataUriSize(uri, 1) == 0);
	}
	REQUIRE(modAssetGltfDataUriSize("data:application/octet-stream;base64,AQJ=", 2) == 0);
}

namespace {
void glbWord(std::vector<u8> &bytes, u32 value)
{
	for (int i = 0; i < 4; i++) bytes.push_back(static_cast<u8>(value >> (i * 8)));
}
void glbSetWord(std::vector<u8> &bytes, size_t offset, u32 value)
{
	for (int i = 0; i < 4; i++) bytes[offset + i] = static_cast<u8>(value >> (i * 8));
}
std::vector<u8> glbContainer()
{
	std::vector<u8> bytes{'g', 'l', 'T', 'F'};
	glbWord(bytes, 2); glbWord(bytes, 0);
	glbWord(bytes, 4); glbWord(bytes, 0x4e4f534a);
	bytes.insert(bytes.end(), {'{', '}', ' ', ' '});
	glbWord(bytes, 4); glbWord(bytes, 0x004e4942);
	bytes.insert(bytes.end(), {1, 2, 3, 0});
	glbSetWord(bytes, 8, static_cast<u32>(bytes.size()));
	return bytes;
}
}

TEST_CASE("GLB chunks preserve bounded source slices and allow trailing unknown chunks",
		"[modding][pdxxx][c3842][gltf-source]")
{
	auto bytes = glbContainer();
	modasset_gltf_glb_chunks_t view{};
	REQUIRE(modAssetGltfGlbChunks(bytes.data(), bytes.size(), &view) == 1);
	REQUIRE(view.json == bytes.data() + 20);
	REQUIRE(view.json_size == 4);
	REQUIRE(view.bin == bytes.data() + 32);
	REQUIRE(view.bin_size == 4);
	glbWord(bytes, 4); glbWord(bytes, 0x12345678); glbWord(bytes, 0);
	glbSetWord(bytes, 8, static_cast<u32>(bytes.size()));
	REQUIRE(modAssetGltfGlbChunks(bytes.data(), bytes.size(), &view) == 1);
	bytes.resize(24);
	glbSetWord(bytes, 8, static_cast<u32>(bytes.size()));
	REQUIRE(modAssetGltfGlbChunks(bytes.data(), bytes.size(), &view) == 1);
	REQUIRE(view.bin == nullptr);
	REQUIRE(view.bin_size == 0);
}

TEST_CASE("GLB containers reject wrong totals duplicate chunks and alignment loss",
		"[modding][pdxxx][c3842][gltf-source]")
{
	const auto good = glbContainer();
	std::vector<std::vector<u8>> invalid;
	auto candidate = good; glbSetWord(candidate, 4, 3); invalid.push_back(candidate);
	candidate = good; glbSetWord(candidate, 8, good.size() - 4); invalid.push_back(candidate);
	candidate = good; glbSetWord(candidate, 8, good.size() + 4); invalid.push_back(candidate);
	candidate = good; glbSetWord(candidate, 12, 3); invalid.push_back(candidate);
	candidate = good; glbSetWord(candidate, 12, 0xfffffffc); invalid.push_back(candidate);
	candidate = good; glbSetWord(candidate, 16, 0x004e4942); invalid.push_back(candidate);
	candidate = good; glbSetWord(candidate, 28, 0x4e4f534a); invalid.push_back(candidate);
	candidate = good; glbWord(candidate, 4); glbWord(candidate, 0x004e4942); glbWord(candidate, 0);
	glbSetWord(candidate, 8, candidate.size()); invalid.push_back(candidate);
	candidate = good; candidate.push_back(0); glbSetWord(candidate, 8, candidate.size()); invalid.push_back(candidate);
	for (const auto &bytes : invalid) {
		modasset_gltf_glb_chunks_t view{};
		view.json = good.data(); view.json_size = 99;
		REQUIRE(modAssetGltfGlbChunks(bytes.data(), bytes.size(), &view) == 0);
		REQUIRE(view.json == nullptr);
		REQUIRE(view.json_size == 0);
		REQUIRE(view.bin == nullptr);
		REQUIRE(view.bin_size == 0);
	}
}

TEST_CASE("animation native tails retain every row beyond the former 512 limit",
		"[modding][animation][c3842][gltf-source]")
{
	std::string skips = "[", repeats = "[";
	for (int i = 0; i < 600; i++) {
		if (i) { skips += ','; repeats += ','; }
		skips += std::to_string(i);
		repeats += "{\"repeat_from_frame\":" + std::to_string(i + 1)
			+ ",\"repeat_to_frame\":" + std::to_string(i) + "}";
	}
	skips += ']';
	repeats += ']';
	s16 *frames = nullptr;
	modasset_gltf_repeat_range_t *ranges = nullptr;
	s32 count = 0;
	REQUIRE(modAssetGltfCutSkipFrames(skips.data(), skips.size(), &frames, &count) == 1);
	REQUIRE(count == 600);
	for (int i = 0; i < count; i++) REQUIRE(frames[i] == i);
	std::free(frames);
	REQUIRE(modAssetGltfRepeatRanges(repeats.data(), repeats.size(), &ranges, &count) == 1);
	REQUIRE(count == 600);
	for (int i = 0; i < count; i++) {
		REQUIRE(ranges[i].repeattoframe == i);
		REQUIRE(ranges[i].repeatfromframe == i + 1);
	}
	std::free(ranges);
}

TEST_CASE("glTF accessor extents cannot wrap beyond the declared buffer view",
		"[modding][pdxxx][c3842][gltf-source]")
{
	REQUIRE(modAssetGltfAccessorBounds(64, 8, 48, 0, 3, 16, 12) == 1);
	REQUIRE(modAssetGltfAccessorBounds(64, 8, 48, 8, 3, 16, 12) == 0);
	REQUIRE(modAssetGltfAccessorBounds(64, 60, 8, 0, 1, 4, 4) == 0);
	REQUIRE(modAssetGltfAccessorBounds(64, 0, 64, 0, 0x40000001, 4, 4) == 0);
	REQUIRE(modAssetGltfAccessorBounds(64, 0xfffffff0u, 32, 0, 1, 4, 4) == 0);
	REQUIRE(modAssetGltfAccessorBounds(64, 0, 64, 0, 0, 4, 4) == 0);
	REQUIRE(modAssetGltfAccessorBounds(64, 0, 64, 0, 1, 4, 8) == 0);
}

TEST_CASE("glTF cache identity changes when only a relative buffer changes",
		"[modding][pdxxx][c3842][gltf-source]")
{
	const char json[] = "{\"buffers\":[{\"uri\":\"mesh.bin\",\"byteLength\":4}]}";
	const u8 initial[] = {1, 2, 3, 4};
	const u8 edited[] = {1, 2, 9, 4};
	u8 first[32], same[32], changed[32];
	REQUIRE(modAssetGltfSourceHash(json, sizeof(json) - 1, initial, 4, first) == 1);
	REQUIRE(modAssetGltfSourceHash(json, sizeof(json) - 1, initial, 4, same) == 1);
	REQUIRE(std::memcmp(first, same, sizeof(first)) == 0);
	REQUIRE(modAssetGltfSourceHash(json, sizeof(json) - 1, edited, 4, changed) == 1);
	REQUIRE(std::memcmp(first, changed, sizeof(first)) != 0);
	REQUIRE(modAssetGltfSourceHash("a", 1, "bc", 2, first) == 1);
	REQUIRE(modAssetGltfSourceHash("ab", 2, "c", 1, changed) == 1);
	REQUIRE(std::memcmp(first, changed, sizeof(first)) != 0);
	REQUIRE(modAssetGltfSourceHash(json, sizeof(json) - 1, nullptr, 4, changed) == 0);
}

TEST_CASE("animation tail parse failures cannot return a truncated accepted prefix",
		"[modding][animation][c3842][gltf-source]")
{
	for (const std::string json : {"[1,-1]", "[1,32768]", "[1,2147483648]",
		"[1,2.5]", "[1,true]", "[1,\"2\"]", "[1,]", "[,1]", "[1]junk",
		"[01]", "[+1]", "[1e2]", "[1 2]", "[1,{\"frame\":2}]"}) {
		INFO(json);
		s16 *frames = nullptr;
		s32 count = 99;
		REQUIRE(modAssetGltfCutSkipFrames(json.data(), json.size(), &frames, &count) == 0);
		REQUIRE(frames == nullptr);
		REQUIRE(count == 0);
	}
	const std::string good = "{\"repeat_to_frame\":0,\"repeat_from_frame\":1}";
	for (const std::string bad : {"{}", "false", "{\"repeat_to_frame\":2}",
		"{\"repeat_to_frame\":2,\"repeat_to_frame\":3}",
		"{\"repeat_to_frame\":2,\"repeat_from_frame\":32768}",
		"{\"repeat_to_frame\":2.5,\"repeat_from_frame\":3}",
		"{\"repeat_to_frame\":2,\"repeat_from_frame\":3,\"unknown\":0}"}) {
		const std::string json = "[" + good + "," + bad + "]";
		INFO(json);
		modasset_gltf_repeat_range_t *ranges = nullptr;
		s32 count = 99;
		REQUIRE(modAssetGltfRepeatRanges(json.data(), json.size(), &ranges, &count) == 0);
		REQUIRE(ranges == nullptr);
		REQUIRE(count == 0);
	}
}

TEST_CASE("animation tail values preserve the nonnegative signed 16 bit boundary",
		"[modding][animation][c3842][gltf-source]")
{
	s16 *frames = nullptr;
	s32 count = 0;
	const std::string json = " [0,32767] \n";
	REQUIRE(modAssetGltfCutSkipFrames(json.data(), json.size(), &frames, &count) == 1);
	REQUIRE(count == 2);
	REQUIRE(frames[0] == 0);
	REQUIRE(frames[1] == 32767);
	std::free(frames);
	REQUIRE(modAssetGltfCutSkipFrames("[]", 2, &frames, &count) == 1);
	REQUIRE(frames == nullptr);
	REQUIRE(count == 0);
}
