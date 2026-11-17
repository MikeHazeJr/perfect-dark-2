/**
 * test_mods_roots.cpp -- which folder is the user mods folder, and the folder
 * helpers the mod manager uses with it.
 */
#include "catch.hpp"
extern "C" {
#include "mods_roots.h"
}
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;

std::string userDir(s32 portable, const char *savedirArg, const char *saveDir,
	const char *localAppData, const char *home)
{
	char out[512];
	std::memset(out, 'x', sizeof(out));
	if (!modsRootsUserDir(portable, savedirArg, saveDir, localAppData, home, out, sizeof(out))) {
		CHECK(out[0] == '\0');
		return "<none>";
	}
	return out;
}

struct RootsTempDir {
	fs::path path;

	RootsTempDir() {
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("pd2-mods-roots-" + std::to_string(stamp));
		fs::create_directories(path);
	}

	~RootsTempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

} // namespace

TEST_CASE("the user mods folder is in LocalAppData unless the launch isolates it",
          "[modding][mods-roots]") {
	const char *const local = "C:\\Users\\player\\AppData\\Local";
	const char *const roaming = "C:/Users/player/AppData/Roaming/perfectdark/";
	const char *const save = "C:/Users/player/AppData/Roaming/perfectdark";

	/* An ordinary launch: LocalAppData, not where the saves are. */
	CHECK(userDir(0, nullptr, save, local, roaming) ==
		"C:/Users/player/AppData/Local/perfectdark/mods");
	CHECK(userDir(0, "", save, "C:/Users/player/AppData/Local/", roaming) ==
		"C:/Users/player/AppData/Local/perfectdark/mods");

	/* --portable: with the saves, which is the install itself. */
	CHECK(userDir(1, nullptr, "D:\\Games\\PD2\\", local, roaming) == "D:/Games/PD2/mods");

	/* --savedir, with or without --portable: with the saves. */
	CHECK(userDir(0, "./runtime-home", "D:/Games/PD2/runtime-home", local, roaming) ==
		"D:/Games/PD2/runtime-home/mods");
	CHECK(userDir(1, "./runtime-home", "D:/Games/PD2/runtime-home", local, roaming) ==
		"D:/Games/PD2/runtime-home/mods");

	/* No LocalAppData (not Windows): the home folder. */
	CHECK(userDir(0, nullptr, save, nullptr, "/home/player/.local/share/perfectdark") ==
		"/home/player/.local/share/perfectdark/mods");
	CHECK(userDir(0, nullptr, save, "", roaming) ==
		"C:/Users/player/AppData/Roaming/perfectdark/mods");

	/* Nothing to go on, or no room. */
	CHECK(userDir(0, nullptr, save, nullptr, nullptr) == "<none>");
	CHECK(userDir(1, nullptr, nullptr, local, roaming) == "<none>");
	char tiny[8] = "keep";
	CHECK(modsRootsUserDir(0, nullptr, save, local, roaming, tiny, sizeof(tiny)) == 0);
	CHECK(tiny[0] == '\0');
	CHECK(modsRootsUserDir(0, nullptr, save, local, roaming, nullptr, 64) == 0);
}

TEST_CASE("mods folders are created with their parents and compared as folders",
          "[modding][mods-roots]") {
	RootsTempDir tmp;
	const fs::path nested = tmp.path / "LocalAppData" / "perfectdark" / "mods";
	REQUIRE_FALSE(fs::exists(nested));
	CHECK(modsRootsEnsureDir(nested.string().c_str()) == 1);
	CHECK(fs::is_directory(nested));
	/* Again: it already exists. */
	CHECK(modsRootsEnsureDir(nested.string().c_str()) == 1);
	CHECK(modsRootsEnsureDir((nested.generic_string() + "/").c_str()) == 1);

	/* A file in the way is not a folder. */
	const fs::path file = tmp.path / "blocked";
	{
		FILE *f = std::fopen(file.string().c_str(), "wb");
		REQUIRE(f != nullptr);
		std::fclose(f);
	}
	CHECK(modsRootsEnsureDir((file / "mods").string().c_str()) == 0);
	CHECK(modsRootsEnsureDir("") == 0);
	CHECK(modsRootsEnsureDir(nullptr) == 0);

	const std::string native = nested.string();
	const std::string generic = nested.generic_string();
	CHECK(modsRootsSameDir(native.c_str(), generic.c_str()) == 1);
	CHECK(modsRootsSameDir(native.c_str(), (generic + "/").c_str()) == 1);
	CHECK(modsRootsSameDir((tmp.path / "LocalAppData" / "." / "perfectdark" / "mods").string().c_str(),
		native.c_str()) == 1);
	CHECK(modsRootsSameDir(native.c_str(), (tmp.path / "LocalAppData").string().c_str()) == 0);
	CHECK(modsRootsSameDir(native.c_str(), nullptr) == 0);
#ifdef _WIN32
	std::string upper = generic;
	for (char &c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	CHECK(modsRootsSameDir(upper.c_str(), native.c_str()) == 1);
#endif

	/* A relative path is taken against the current folder. */
	const fs::path before = fs::current_path();
	fs::current_path(tmp.path);
	CHECK(modsRootsSameDir("./LocalAppData/perfectdark/mods", native.c_str()) == 1);
	CHECK(modsRootsSameDir("LocalAppData/perfectdark/mods/", native.c_str()) == 1);
	fs::current_path(before);
}
