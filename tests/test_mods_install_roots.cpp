// B-1169 (Mike, 2026-11-15, option A): a player's install reads only the
// mods folder next to the game; an executable inside a source checkout also
// reads the repository-root mods folder, first. Real folders in the system
// temp folder stand in for a player install, a clone and a git worktree.
#include "catch.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
#include "mods_install_roots.h"
}

namespace {

struct TempTree {
	std::filesystem::path root;

	TempTree()
	{
		const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		root = std::filesystem::temp_directory_path() / ("pd2-b1169-" + std::to_string(stamp));
		std::filesystem::create_directories(root);
	}

	~TempTree()
	{
		std::error_code ec;
		std::filesystem::remove_all(root, ec);
	}

	std::filesystem::path dir(const std::string &rel) const
	{
		const auto path = root / rel;
		std::filesystem::create_directories(path);
		return path;
	}

	void file(const std::string &rel) const
	{
		const auto path = root / rel;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path) << "x";
	}
};

std::string slashes(const std::filesystem::path &path)
{
	std::string text = path.string();
	for (char &c : text) {
		if (c == '\\') c = '/';
	}
	return text;
}

struct Roots {
	char a[1100] = {};
	char b[1100] = {};
	char *out[2] = {a, b};
	s32 count = 0;

	Roots(const std::filesystem::path &exe, const std::filesystem::path &base, s32 room = 2)
	{
		count = modsInstallRoots(exe.string().c_str(), base.string().c_str(), out, sizeof(a), room);
	}
};

} // namespace

TEST_CASE("B-1169 a player's install reads only the mods folder next to the game",
		"[mods][b1169]")
{
	TempTree tree;
	// Another game's mods folder beside the install is never read.
	tree.file("Games/mods/othergame/mod.json");
	const auto game = tree.dir("Games/PD2");
	REQUIRE_FALSE(modsInstallIsSourceCheckout(game.string().c_str()));
	Roots roots(game, game);
	REQUIRE(roots.count == 1);
	REQUIRE(std::string(roots.a) == slashes(game / "mods"));
}

TEST_CASE("B-1169 a build inside a checkout keeps the repository-root folder first",
		"[mods][b1169]")
{
	TempTree tree;
	tree.file("repo/CMakeLists.txt");
	tree.dir("repo/.git");
	const auto build = tree.dir("repo/Build");
	REQUIRE(modsInstallIsSourceCheckout(build.string().c_str()));
	Roots roots(build, build);
	REQUIRE(roots.count == 2);
	REQUIRE(std::string(roots.a) == slashes(tree.root / "repo/mods"));
	REQUIRE(std::string(roots.b) == slashes(build / "mods"));

	// --basedir names another install: its folder is the install's own.
	const auto base = tree.dir("shared-base");
	Roots other(build, base);
	REQUIRE(other.count == 2);
	REQUIRE(std::string(other.b) == slashes(base / "mods"));

	// --basedir at the repository root: one folder, not the same one twice.
	Roots same(build, tree.root / "repo");
	REQUIRE(same.count == 1);
	REQUIRE(std::string(same.a) == slashes(tree.root / "repo/mods"));

	// Room for one folder: the install's own.
	Roots one(build, build, 1);
	REQUIRE(one.count == 1);
	REQUIRE(std::string(one.a) == slashes(build / "mods"));
}

TEST_CASE("B-1169 a git worktree counts as a checkout, a bare CMakeLists does not",
		"[mods][b1169]")
{
	TempTree tree;
	tree.file("worktree/CMakeLists.txt");
	tree.file("worktree/.git");  // a worktree's .git is a file
	REQUIRE(modsInstallIsSourceCheckout(tree.dir("worktree/Build").string().c_str()));

	tree.file("unpacked/CMakeLists.txt");
	const auto unpacked = tree.dir("unpacked/game");
	REQUIRE_FALSE(modsInstallIsSourceCheckout(unpacked.string().c_str()));
	REQUIRE(Roots(unpacked, unpacked).count == 1);

	// Trailing separators and an empty base folder.
	const std::string withSlash = unpacked.string() + "\\";
	REQUIRE_FALSE(modsInstallIsSourceCheckout(withSlash.c_str()));
	char buffer[1100] = {};
	char *out[1] = {buffer};
	REQUIRE(modsInstallRoots(withSlash.c_str(), "", out, sizeof(buffer), 1) == 0);
}
