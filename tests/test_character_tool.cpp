/**
 * test_character_tool.cpp -- the rules of the Modding Hub's Characters tool
 * (port/src/character_tool.c): what it lists, how a gender and a build become
 * a descriptor's ismale and type, and which descriptor values a save sends.
 * The saves themselves go through the mod author; [characters-tab] in
 * test_mod_author.cpp proves those against the game's body and head readers.
 */
#include "catch.hpp"

#include <cstring>
#include <string>

extern "C" {
#include "character_tool.h"
#include "constants.h"
}

namespace {

character_tool_template_t tmplOf(s32 kind, s32 ismale, s32 type, s32 height)
{
	character_tool_template_t t;
	std::memset(&t, 0, sizeof(t));
	t.kind = kind;
	t.ismale = ismale;
	t.type = type;
	t.height = height;
	return t;
}

/* "key=value" for each value in order, a removed key as "key=(removed)". */
std::string valuesText(const character_tool_values_t &values)
{
	std::string out;
	for (u32 i = 0; i < values.count; i++) {
		if (i) out += " ";
		out += values.values[i].key;
		out += "=";
		out += values.values[i].value ? values.values[i].value : "(removed)";
	}
	return out;
}

std::string saved(const character_tool_template_t &tmpl, const character_tool_form_t &form)
{
	character_tool_values_t values;
	REQUIRE(characterToolDescriptorValues(&form, &tmpl, &values) == 1);
	return valuesText(values);
}

} // namespace

TEST_CASE("The Characters tool lists characters, bodies and heads", "[characters-tool]")
{
	CHECK(characterToolKindForAssetType(ASSET_CHARACTER) == CHARACTER_TOOL_KIND_CHARACTER);
	CHECK(characterToolKindForAssetType(ASSET_BODY) == CHARACTER_TOOL_KIND_BODY);
	CHECK(characterToolKindForAssetType(ASSET_HEAD) == CHARACTER_TOOL_KIND_HEAD);
	CHECK(characterToolKindForAssetType(ASSET_WEAPON) == CHARACTER_TOOL_KIND_NONE);
	CHECK(characterToolKindForAssetType(ASSET_MAP) == CHARACTER_TOOL_KIND_NONE);

	// The names are the mod author's kinds, so "mod:body_<slug>" reads the same in both.
	CHECK(std::string(characterToolKindName(CHARACTER_TOOL_KIND_CHARACTER)) == "character");
	CHECK(std::string(characterToolKindName(CHARACTER_TOOL_KIND_BODY)) == "body");
	CHECK(std::string(characterToolKindName(CHARACTER_TOOL_KIND_HEAD)) == "head");
	CHECK(std::string(characterToolKindName(CHARACTER_TOOL_KIND_NONE)).empty());
	CHECK(std::string(characterToolKindLabel(CHARACTER_TOOL_KIND_BODY)) == "Body");
	CHECK(std::string(characterToolKindLabel(77)).empty());

	// Gender, build and height belong to a body or a head, not to a character.
	CHECK(characterToolKindHasBuild(CHARACTER_TOOL_KIND_BODY) == 1);
	CHECK(characterToolKindHasBuild(CHARACTER_TOOL_KIND_HEAD) == 1);
	CHECK(characterToolKindHasBuild(CHARACTER_TOOL_KIND_CHARACTER) == 0);
	CHECK(characterToolKindHasBuild(CHARACTER_TOOL_KIND_NONE) == 0);
}

TEST_CASE("A template opens with the gender, build and height its descriptor gives", "[characters-tool]")
{
	character_tool_form_t form;

	auto opens = [&](s32 ismale, s32 type, s32 height) {
		const character_tool_template_t t = tmplOf(CHARACTER_TOOL_KIND_BODY, ismale, type, height);
		characterToolFormFromTemplate(&t, &form);
		return form;
	};

	// The base game's pairs (68 base bodies: 44 DEFAULT, 17 FEMALE, 4 MAIAN,
	// one each of FEMALEGUARD, CASS and MRBLONDE).
	CHECK(opens(1, HEADBODYTYPE_DEFAULT, 159).build == CHARACTER_TOOL_BUILD_HUMAN);
	CHECK(form.ismale == 1);
	CHECK(form.height == 159);
	CHECK(opens(0, HEADBODYTYPE_FEMALE, 157).build == CHARACTER_TOOL_BUILD_HUMAN);
	CHECK(form.ismale == 0);
	CHECK(opens(0, HEADBODYTYPE_FEMALEGUARD, 158).build == CHARACTER_TOOL_BUILD_GUARD);
	CHECK(opens(1, HEADBODYTYPE_MAIAN, 106).build == CHARACTER_TOOL_BUILD_MAIAN);
	CHECK(form.height == 106);
	CHECK(opens(0, HEADBODYTYPE_MAIAN, 106).build == CHARACTER_TOOL_BUILD_MAIAN);
	CHECK(form.ismale == 0);

	// Rigs the builds cannot name stay the template's own.
	CHECK(opens(0, HEADBODYTYPE_CASS, 160).build == CHARACTER_TOOL_BUILD_TEMPLATE);
	CHECK(opens(1, HEADBODYTYPE_MRBLONDE, 167).build == CHARACTER_TOOL_BUILD_TEMPLATE);
	// So does a type that does not fit the gender, instead of being "corrected".
	CHECK(opens(0, HEADBODYTYPE_DEFAULT, 159).build == CHARACTER_TOOL_BUILD_TEMPLATE);
	CHECK(opens(1, HEADBODYTYPE_FEMALE, 159).build == CHARACTER_TOOL_BUILD_TEMPLATE);
	CHECK(opens(1, HEADBODYTYPE_FEMALEGUARD, 159).build == CHARACTER_TOOL_BUILD_TEMPLATE);
	CHECK(opens(1, 99, 159).build == CHARACTER_TOOL_BUILD_TEMPLATE);

	// ismale is a flag: any non-zero value is male.
	CHECK(opens(7, HEADBODYTYPE_DEFAULT, 159).ismale == 1);

	// No template: a form that names nothing.
	characterToolFormFromTemplate(nullptr, &form);
	CHECK(form.build == CHARACTER_TOOL_BUILD_TEMPLATE);
	characterToolFormFromTemplate(nullptr, nullptr); // tolerated
}

TEST_CASE("A gender and a build give the descriptor's type", "[characters-tool]")
{
	const character_tool_template_t cass = tmplOf(CHARACTER_TOOL_KIND_BODY, 0, HEADBODYTYPE_CASS, 160);
	character_tool_form_t form{};

	form.ismale = 1;
	form.build = CHARACTER_TOOL_BUILD_HUMAN;
	CHECK(characterToolTypeForForm(&form, &cass) == HEADBODYTYPE_DEFAULT);
	form.ismale = 0;
	CHECK(characterToolTypeForForm(&form, &cass) == HEADBODYTYPE_FEMALE);
	form.build = CHARACTER_TOOL_BUILD_GUARD;
	CHECK(characterToolTypeForForm(&form, &cass) == HEADBODYTYPE_FEMALEGUARD);
	form.build = CHARACTER_TOOL_BUILD_MAIAN;
	CHECK(characterToolTypeForForm(&form, &cass) == HEADBODYTYPE_MAIAN);
	form.ismale = 1;
	CHECK(characterToolTypeForForm(&form, &cass) == HEADBODYTYPE_MAIAN);
	form.build = CHARACTER_TOOL_BUILD_TEMPLATE;
	CHECK(characterToolTypeForForm(&form, &cass) == HEADBODYTYPE_CASS);
	CHECK(characterToolTypeForForm(&form, nullptr) == -1);
	form.build = CHARACTER_TOOL_BUILD_COUNT;
	CHECK(characterToolTypeForForm(&form, &cass) == -1);
	form.build = -1;
	CHECK(characterToolTypeForForm(&form, &cass) == -1);
	CHECK(characterToolTypeForForm(nullptr, &cass) == -1);

	// Every type the game knows has its descriptor name; nothing else does.
	CHECK(std::string(characterToolTypeName(HEADBODYTYPE_DEFAULT)) == "HEADBODYTYPE_DEFAULT");
	CHECK(std::string(characterToolTypeName(HEADBODYTYPE_FEMALE)) == "HEADBODYTYPE_FEMALE");
	CHECK(std::string(characterToolTypeName(HEADBODYTYPE_FEMALEGUARD)) == "HEADBODYTYPE_FEMALEGUARD");
	CHECK(std::string(characterToolTypeName(HEADBODYTYPE_MAIAN)) == "HEADBODYTYPE_MAIAN");
	CHECK(std::string(characterToolTypeName(HEADBODYTYPE_CASS)) == "HEADBODYTYPE_CASS");
	CHECK(std::string(characterToolTypeName(HEADBODYTYPE_MRBLONDE)) == "HEADBODYTYPE_MRBLONDE");
	CHECK(characterToolTypeName(6) == nullptr);
	CHECK(characterToolTypeName(-1) == nullptr);

	CHECK(std::string(characterToolBuildLabel(CHARACTER_TOOL_BUILD_HUMAN)) == "Human");
	CHECK(std::string(characterToolBuildLabel(CHARACTER_TOOL_BUILD_GUARD)) == "Guard");
	CHECK(std::string(characterToolBuildLabel(CHARACTER_TOOL_BUILD_MAIAN)) == "Maian");
	CHECK(std::string(characterToolBuildLabel(CHARACTER_TOOL_BUILD_TEMPLATE)) == "Template's own");
	CHECK(std::string(characterToolBuildLabel(CHARACTER_TOOL_BUILD_COUNT)).empty());
}

TEST_CASE("A save sends only what the player changed", "[characters-tool]")
{
	// The base airman: male, human, 157.
	const character_tool_template_t airman =
		tmplOf(CHARACTER_TOOL_KIND_BODY, 1, HEADBODYTYPE_DEFAULT, 157);
	character_tool_form_t form;
	characterToolFormFromTemplate(&airman, &form);

	// Saved as it is: no descriptor values, so every line of the template stays.
	CHECK(saved(airman, form).empty());

	// Only the height.
	form.height = 165;
	CHECK(saved(airman, form) == "height=165");

	// A Maian veteran, male: the type changes and the neck follows it.
	characterToolFormFromTemplate(&airman, &form);
	form.build = CHARACTER_TOOL_BUILD_MAIAN;
	form.height = 106;
	CHECK(saved(airman, form) == "type=HEADBODYTYPE_MAIAN rig_class=(removed) height=106");

	// A Maian veteran, female.
	form.ismale = 0;
	CHECK(saved(airman, form) == "ismale=0 type=HEADBODYTYPE_MAIAN rig_class=(removed) height=106");

	// A female human from a male one: the gender and the type move together.
	characterToolFormFromTemplate(&airman, &form);
	form.ismale = 0;
	CHECK(saved(airman, form) == "ismale=0 type=HEADBODYTYPE_FEMALE rig_class=(removed)");
	form.build = CHARACTER_TOOL_BUILD_GUARD;
	CHECK(saved(airman, form) == "ismale=0 type=HEADBODYTYPE_FEMALEGUARD rig_class=(removed)");

	// Between Maians only the gender changes; the type and its neck stay.
	const character_tool_template_t maian = tmplOf(CHARACTER_TOOL_KIND_BODY, 1, HEADBODYTYPE_MAIAN, 106);
	characterToolFormFromTemplate(&maian, &form);
	form.ismale = 0;
	CHECK(saved(maian, form) == "ismale=0");

	// Cassandra's rig kept as the template's own, with a new height.
	const character_tool_template_t cass = tmplOf(CHARACTER_TOOL_KIND_BODY, 0, HEADBODYTYPE_CASS, 160);
	characterToolFormFromTemplate(&cass, &form);
	form.height = 162;
	CHECK(saved(cass, form) == "height=162");
	// ... and given up for a human build.
	form.build = CHARACTER_TOOL_BUILD_HUMAN;
	CHECK(saved(cass, form) == "type=HEADBODYTYPE_FEMALE rig_class=(removed) height=162");

	// A head works the same way.
	const character_tool_template_t head = tmplOf(CHARACTER_TOOL_KIND_HEAD, 1, HEADBODYTYPE_DEFAULT, 13);
	characterToolFormFromTemplate(&head, &form);
	form.ismale = 0;
	CHECK(saved(head, form) == "ismale=0 type=HEADBODYTYPE_FEMALE rig_class=(removed)");

	// A character's own descriptor has nothing the tool edits yet.
	const character_tool_template_t character = tmplOf(CHARACTER_TOOL_KIND_CHARACTER, 0, 0, 0);
	characterToolFormFromTemplate(&character, &form);
	form.ismale = 1;
	form.build = CHARACTER_TOOL_BUILD_MAIAN;
	form.height = 300;
	CHECK(saved(character, form).empty());

	// The values point into the caller's struct, not at anything temporary.
	character_tool_values_t values;
	characterToolFormFromTemplate(&airman, &form);
	form.height = 65535;
	REQUIRE(characterToolDescriptorValues(&form, &airman, &values) == 1);
	REQUIRE(values.count == 1);
	CHECK(values.values[0].value == values.height);
	CHECK(std::string(values.height) == "65535");
}

TEST_CASE("A form the game could not read is refused with the reason", "[characters-tool]")
{
	const character_tool_template_t airman =
		tmplOf(CHARACTER_TOOL_KIND_BODY, 1, HEADBODYTYPE_DEFAULT, 157);
	character_tool_form_t form;
	character_tool_values_t values;
	char why[96];

	characterToolFormFromTemplate(&airman, &form);
	CHECK(characterToolFormValid(&form, &airman, why, sizeof(why)) == 1);
	CHECK(why[0] == '\0');

	// A male guard: the guard build is the female guards' rig.
	form.build = CHARACTER_TOOL_BUILD_GUARD;
	CHECK(characterToolFormValid(&form, &airman, why, sizeof(why)) == 0);
	CHECK(std::string(why) == "the guard build is female");
	CHECK(characterToolDescriptorValues(&form, &airman, &values) == 0);
	CHECK(values.count == 0);
	form.ismale = 0;
	CHECK(characterToolFormValid(&form, &airman, why, sizeof(why)) == 1);

	// Heights the descriptor cannot hold.
	characterToolFormFromTemplate(&airman, &form);
	form.height = 0;
	CHECK(characterToolFormValid(&form, &airman, why, sizeof(why)) == 0);
	CHECK(std::string(why) == "the height must be from 1 to 65535");
	form.height = 65536;
	CHECK(characterToolFormValid(&form, &airman, why, sizeof(why)) == 0);
	form.height = -5;
	CHECK(characterToolFormValid(&form, &airman, why, sizeof(why)) == 0);
	form.height = 1;
	CHECK(characterToolFormValid(&form, &airman, why, sizeof(why)) == 1);

	// A template with no height (the example body) may keep it, and saves no height.
	const character_tool_template_t noHeight =
		tmplOf(CHARACTER_TOOL_KIND_BODY, 1, HEADBODYTYPE_DEFAULT, 0);
	characterToolFormFromTemplate(&noHeight, &form);
	CHECK(form.height == 0);
	CHECK(characterToolFormValid(&form, &noHeight, why, sizeof(why)) == 1);
	CHECK(saved(noHeight, form).empty());

	// A build out of range, and the template's own type when the game does not know it.
	characterToolFormFromTemplate(&airman, &form);
	form.build = CHARACTER_TOOL_BUILD_COUNT;
	CHECK(characterToolFormValid(&form, &airman, why, sizeof(why)) == 0);
	CHECK(std::string(why) == "pick a build");
	const character_tool_template_t odd = tmplOf(CHARACTER_TOOL_KIND_BODY, 1, 42, 150);
	characterToolFormFromTemplate(&odd, &form);
	CHECK(characterToolFormValid(&form, &odd, why, sizeof(why)) == 0);
	CHECK(std::string(why) == "the template's own type is not one the game knows");
	// ... which a real build fixes.
	form.build = CHARACTER_TOOL_BUILD_HUMAN;
	CHECK(characterToolFormValid(&form, &odd, why, sizeof(why)) == 1);
	CHECK(saved(odd, form) == "type=HEADBODYTYPE_DEFAULT rig_class=(removed)");

	// Nothing open, or something the tool does not make.
	CHECK(characterToolFormValid(&form, nullptr, why, sizeof(why)) == 0);
	CHECK(std::string(why) == "no template is open");
	CHECK(characterToolFormValid(nullptr, &airman, nullptr, 0) == 0);
	const character_tool_template_t weapon = tmplOf(CHARACTER_TOOL_KIND_NONE, 1, 0, 0);
	CHECK(characterToolFormValid(&form, &weapon, why, sizeof(why)) == 0);
	CHECK(characterToolDescriptorValues(&form, &airman, nullptr) == 0);

	// A character is always savable: its form is not read.
	const character_tool_template_t character = tmplOf(CHARACTER_TOOL_KIND_CHARACTER, 0, 0, 0);
	form.build = 99;
	form.height = -1;
	CHECK(characterToolFormValid(&form, &character, why, sizeof(why)) == 1);
}

TEST_CASE("The Characters tool's debug flags take plain words", "[characters-tool]")
{
	s32 ismale = -1;
	s32 build = -1;

	CHECK(characterToolParseGender("male", &ismale) == 1);
	CHECK(ismale == 1);
	CHECK(characterToolParseGender("female", &ismale) == 1);
	CHECK(ismale == 0);
	CHECK(characterToolParseGender("Male", &ismale) == 0);
	CHECK(characterToolParseGender("", &ismale) == 0);
	CHECK(characterToolParseGender(nullptr, &ismale) == 0);
	CHECK(characterToolParseGender("male", nullptr) == 0);
	CHECK(ismale == 0); // a refused word changes nothing

	CHECK(characterToolParseBuild("human", &build) == 1);
	CHECK(build == CHARACTER_TOOL_BUILD_HUMAN);
	CHECK(characterToolParseBuild("guard", &build) == 1);
	CHECK(build == CHARACTER_TOOL_BUILD_GUARD);
	CHECK(characterToolParseBuild("maian", &build) == 1);
	CHECK(build == CHARACTER_TOOL_BUILD_MAIAN);
	CHECK(characterToolParseBuild("template", &build) == 1);
	CHECK(build == CHARACTER_TOOL_BUILD_TEMPLATE);
	CHECK(characterToolParseBuild("skedar", &build) == 0);
	CHECK(characterToolParseBuild(nullptr, &build) == 0);
	CHECK(characterToolParseBuild("maian", nullptr) == 0);
	CHECK(build == CHARACTER_TOOL_BUILD_TEMPLATE);
}
