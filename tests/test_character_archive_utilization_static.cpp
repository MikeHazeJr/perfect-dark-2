#include <fstream>
#include <sstream>
#include <string>

#include "catch.hpp"

static std::string readCharacterSource(const char *path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}
TEST_CASE("pdcharacter catalog identities feed selection and portrait consumers",
          "[modding][pdxxx][pdcharacter][c3842][runtime][static]")
{
    const std::string extractor =
        readCharacterSource("port/src/romextract_pdcharacter.c");
    const std::string scanner =
        readCharacterSource("port/src/assetcatalog_scanner.c");
    const std::string walker =
        readCharacterSource("port/src/loader_walker_meta.c");
    const std::string runtime =
        readCharacterSource("port/src/asset_runtime.c");
    const std::string portrait =
        readCharacterSource("port/fast3d/pdgui_character_portrait.cpp");
    const std::string room =
        readCharacterSource("port/fast3d/pdgui_menu_room.cpp");

    REQUIRE(extractor.find("body_asset = %s") != std::string::npos);
    REQUIRE(extractor.find("head_asset = %s") != std::string::npos);
    REQUIRE(scanner.find("iniGet(ini, \"body_asset\"") != std::string::npos);
    REQUIRE(scanner.find("iniGet(ini, \"head_asset\"") != std::string::npos);
    REQUIRE(walker.find("s_manifestStr(manifest, manifest_len, \"body\"") !=
            std::string::npos);
    REQUIRE(walker.find("s_manifestStr(manifest, manifest_len, \"head\"") !=
            std::string::npos);
    REQUIRE(runtime.find("binding->character_body_id") != std::string::npos);
    REQUIRE(runtime.find("binding->character_head_id") != std::string::npos);
    REQUIRE(runtime.find("s_hasText(binding->character_body_id)") !=
            std::string::npos);
    REQUIRE(runtime.find("s_hasText(binding->character_head_id)") !=
            std::string::npos);

    REQUIRE(portrait.find("assetCatalogFindCharacterByBodyHead") !=
            std::string::npos);
    REQUIRE(portrait.find("assetRuntimeFindByTypeAndId(ASSET_CHARACTER") !=
            std::string::npos);
    REQUIRE(portrait.find("fsFileLoad(binding->dependency_b") !=
            std::string::npos);
    REQUIRE(portrait.find("refusing generated portrait fallback") !=
            std::string::npos);
    REQUIRE(room.find("assetCatalogIterateUnlockedByType(\n                    ASSET_CHARACTER") !=
            std::string::npos);
    REQUIRE(room.find("pdguiCharacterPortraitGet") != std::string::npos);
    REQUIRE(room.find("character->ext.character.display_name") !=
            std::string::npos);
}
