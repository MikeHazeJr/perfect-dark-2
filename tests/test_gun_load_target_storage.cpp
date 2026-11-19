// B-1109: the gun loader's queue targets must have the width of the fields they
// point at. On 64-bit they used to be uintptr_t*, so a write through
// loadmemremaining covered masterloadstate, gunloadstate and loadfilenum too.
#include "catch.hpp"
#include <type_traits>
// Both game headers are included with the same (C++) linkage on purpose.
// game/bondgun.h pulls in data.h, which re-declares globals from the PR headers
// that types.h includes, so the two must agree: an extern "C" block around only
// one of them fails to compile, and one around both collides with the C library's
// own sprintf declaration. Nothing here is called or linked; the test only checks
// declared types and writes through a local struct.
#include "types.h"
#include "game/bondgun.h"
#undef bool

// Queue targets must use the actual object widths, not the host pointer width.
static_assert(std::is_same_v<decltype(gunctrl::loadmemptr), u8 **>);
static_assert(std::is_same_v<decltype(gunctrl::loadmemremaining), u32 *>);
static_assert(sizeof(gunctrl::handmemloadremaining) == sizeof(u32));
static_assert(sizeof(gunctrl::memloadremaining) == sizeof(u32));
using BodyHandQueue = s32 (*)(player *, s32, modeldef **, u8 **, u32 *);
static_assert(std::is_same_v<decltype(&bgunQueueBodyHandModelLoad), BodyHandQueue>);

TEST_CASE("Gun load capacity target writes preserve adjacent load state", "[gun_load][storage_width]")
{
    gunctrl ctrl{};
    ctrl.masterloadstate = 0xa1;
    ctrl.gunloadstate = 0xb2;
    ctrl.loadfilenum = 0xc3d4;
    ctrl.memloadremaining = 0x11223344u;
    ctrl.loadmemremaining = &ctrl.memloadremaining;
    REQUIRE(*ctrl.loadmemremaining == 0x11223344u);
    *ctrl.loadmemremaining = 0x55667788u;
    REQUIRE(ctrl.memloadremaining == 0x55667788u);
    REQUIRE(ctrl.masterloadstate == 0xa1);
    REQUIRE(ctrl.gunloadstate == 0xb2);
    REQUIRE(ctrl.loadfilenum == 0xc3d4);
}

TEST_CASE("Hand capacity and cursor targets preserve neighboring native fields", "[gun_load][storage_width]")
{
    gunctrl ctrl{};
    u8 bytes[64]{};
    ctrl.handmemloadptr = bytes;
    ctrl.memloadptr = bytes + 32;
    ctrl.handmemloadremaining = 47;
    ctrl.memloadremaining = 29;
    // C permits aliasing a signed integer through its corresponding unsigned type.
    ctrl.loadmemremaining = reinterpret_cast<u32 *>(&ctrl.handmemloadremaining);
    REQUIRE(*ctrl.loadmemremaining == 47u);
    *ctrl.loadmemremaining = 31;
    REQUIRE(ctrl.handmemloadremaining == 31);
    REQUIRE(ctrl.memloadptr == bytes + 32);
    REQUIRE(ctrl.memloadremaining == 29u);

    ctrl.loadmemptr = &ctrl.handmemloadptr;
    *ctrl.loadmemptr = bytes + 16;
    REQUIRE(ctrl.handmemloadptr == bytes + 16);
    REQUIRE(ctrl.handmemloadremaining == 31);
    REQUIRE(ctrl.memloadptr == bytes + 32);
    ctrl.loadmemptr = &ctrl.memloadptr;
    *ctrl.loadmemptr = bytes + 48;
    REQUIRE(ctrl.memloadptr == bytes + 48);
    REQUIRE(ctrl.memloadremaining == 29u);
}
