// B-1116: every input action needs a config name.
//
// The name table in actionmap.cpp named 105 of the 121 actions in the enum.
// The 16 without a name shared the config key "(null)", so pd.ini got a line
// its own loader rejects, and the bindings file the game writes was cut off at
// the first unnamed action, which made every later start reject that file.
// Key bindings could not be saved.
#include "catch.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

// Removes block and line comments so identifiers and strings named in a
// comment are not counted.
std::string stripComments(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ) {
		if (text.compare(i, 2, "/*") == 0) {
			const size_t end = text.find("*/", i + 2);
			i = end == std::string::npos ? text.size() : end + 2;
		} else if (text.compare(i, 2, "//") == 0) {
			const size_t end = text.find('\n', i);
			i = end == std::string::npos ? text.size() : end;
		} else {
			out.push_back(text[i++]);
		}
	}
	return out;
}

}

TEST_CASE("every input action has one unique config name, in enum order",
		"[input][actionmap][persistence][b-1116]")
{
	const std::string header = readSource("port/include/actionmap.h");
	const std::string source = readSource("port/src/actionmap.cpp");

	// The actions, as the enum lists them.
	const size_t enumEnd = header.find("} InputAction;");
	REQUIRE(enumEnd != std::string::npos);
	const size_t enumBegin = header.rfind("typedef enum", enumEnd);
	REQUIRE(enumBegin != std::string::npos);
	const std::string enumBody = stripComments(header.substr(enumBegin, enumEnd - enumBegin));
	const std::regex actionId("\\bACTION_[A-Z0-9_]+\\b");
	std::vector<std::string> actions;
	for (auto it = std::sregex_iterator(enumBody.begin(), enumBody.end(), actionId);
			it != std::sregex_iterator(); ++it) {
		if (it->str() != "ACTION_COUNT") {
			actions.push_back(it->str());
		}
	}
	REQUIRE(actions.size() > 100);
	REQUIRE(std::set<std::string>(actions.begin(), actions.end()).size() == actions.size());

	// The names, as the table lists them.
	const char *const tableHead = "static const char * const s_ActionNames[] = {";
	const size_t tableBegin = source.find(tableHead);
	REQUIRE(tableBegin != std::string::npos);
	const size_t tableEnd = source.find("};", tableBegin);
	REQUIRE(tableEnd != std::string::npos);
	const std::string table = stripComments(source.substr(tableBegin, tableEnd - tableBegin));
	const std::regex quoted("\"([^\"]*)\"");
	std::vector<std::string> names;
	for (auto it = std::sregex_iterator(table.begin(), table.end(), quoted);
			it != std::sregex_iterator(); ++it) {
		names.push_back((*it)[1].str());
	}

	// One name per action, none empty, none repeated.
	REQUIRE(names.size() == actions.size());
	REQUIRE(std::set<std::string>(names.begin(), names.end()).size() == names.size());
	for (const std::string &name : names) {
		CAPTURE(name);
		REQUIRE_FALSE(name.empty());
		REQUIRE(name.find_first_not_of(
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") == std::string::npos);
	}

	// Pin the indices used by the shipped Grid bindings and the merged editor action.
	REQUIRE(actions.size() >= 127);
	REQUIRE(actions[122] == "ACTION_GUN_ROLL_RIGHT");
	REQUIRE(names[122] == "GunRollRight");
	REQUIRE(actions[123] == "ACTION_VEHICLE_BOOST");
	REQUIRE(names[123] == "VehicleBoost");
	REQUIRE(actions[124] == "ACTION_TURRET_FIRE");
	REQUIRE(names[124] == "TurretFire");
	REQUIRE(actions[125] == "ACTION_TURRET_ZOOM");
	REQUIRE(names[125] == "TurretZoom");
	REQUIRE(actions[126] == "ACTION_FORGE_EDITOR_CONTROLS");
	REQUIRE(names[126] == "ForgeEditorControls");

	// And the compiler holds the two to the same length from now on.
	REQUIRE(source.find("static_assert(sizeof(s_ActionNames) / sizeof(s_ActionNames[0]) == ACTION_COUNT,") !=
		std::string::npos);
}
