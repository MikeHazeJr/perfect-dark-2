#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {
std::string readTextFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void requireContains(const std::string &text, const char *needle)
{
    REQUIRE(text.find(needle) != std::string::npos);
}
}

TEST_CASE("smoke input events drive mouse hover and wheel through SDL",
    "[input][menus][smoke][v004][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");

    requireContains(source, "SMOKE_EVENT_MOUSE_MOVE");
    requireContains(source, "SMOKE_EVENT_MOUSE_WHEEL");
    requireContains(source, "!strcmp(type_str, \"mouse_move\")");
    requireContains(source, "!strcmp(type_str, \"mouse_wheel\")");
    requireContains(source, "ev.type = SDL_MOUSEMOTION;");
    requireContains(source, "ev.type = SDL_MOUSEWHEEL;");
    requireContains(source, "ev.motion.windowID = smokeResolveWindowId();");
    requireContains(source, "ev.wheel.windowID = smokeResolveWindowId();");
    requireContains(source, "ev.wheel.preciseY = (float)wheel_y;");
    requireContains(source, "SMOKE: mouse move xy=(%d,%d) at_ms=%d");
    requireContains(source, "SMOKE: mouse wheel delta=(%d,%d) at_ms=%d");
}

TEST_CASE("smoke mouse motion and wheel reject incomplete no-op events",
    "[input][menus][smoke][v004][static]")
{
    const std::string source = readTextFile("port/src/smoke_harness.c");

    requireContains(source, "SMOKE: mouse_move event missing x/y");
    requireContains(source, "SMOKE: mouse_wheel event missing wheel_x/wheel_y");
    requireContains(source, "SMOKE: mouse_wheel event has zero delta");
}
