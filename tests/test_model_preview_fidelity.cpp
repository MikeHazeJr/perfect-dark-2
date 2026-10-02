#include "catch.hpp"
#include <PR/mbi.h>
#include "pdgui_model_preview.h"
#include "gfx_framebuffer_coords.h"
#include <cstring>

TEST_CASE("preview viewport survives restoration before deferred GBI execution",
        "[model-preview][render-fidelity]")
{
    Vp mutableViewport = {};
    mutableViewport.vp.vscale[0] = 1024;
    mutableViewport.vp.vscale[1] = 1024;
    mutableViewport.vp.vtrans[0] = 1024;
    mutableViewport.vp.vtrans[1] = 1024;
    const Vp frameViewport = mutableViewport;
    Gfx commands[4] = {};
    gSPViewport(&commands[0], &mutableViewport);
    gSPMatrix(&commands[1], &mutableViewport, G_MTX_LOAD | G_MTX_PROJECTION);
    gSPViewport(&commands[2], &mutableViewport);
    gSPViewport(&commands[3], &mutableViewport); // next pass, outside range
    const uintptr_t matrixPointer = commands[1].words.w1;

    pdguiModelPreviewPinViewports(commands, commands + 3, &frameViewport);
    mutableViewport.vp.vscale[0] = 3840;
    mutableViewport.vp.vscale[1] = 2160;
    mutableViewport.vp.vtrans[0] = 3840;
    mutableViewport.vp.vtrans[1] = 2160;

    for (int i : {0, 2}) {
        const auto *executed = reinterpret_cast<const Vp *>(commands[i].words.w1);
        REQUIRE(executed == &frameViewport);
        CHECK(executed->vp.vscale[0] == 1024);
        CHECK(executed->vp.vscale[1] == 1024);
        CHECK(executed->vp.vtrans[0] == 1024);
    }
    CHECK(commands[1].words.w1 == matrixPointer);
    CHECK(commands[3].words.w1 == reinterpret_cast<uintptr_t>(&mutableViewport));
}

TEST_CASE("preview readiness rejects missing models and pending replacement",
        "[model-preview][render-fidelity]")
{
    const int loadedModel = 1;
    CHECK_FALSE(pdguiModelPreviewContentReady(nullptr, 0, 0));
    CHECK_FALSE(pdguiModelPreviewContentReady(nullptr, 17, 0));
    CHECK_FALSE(pdguiModelPreviewContentReady(&loadedModel, 0, 17));
    CHECK_FALSE(pdguiModelPreviewContentReady(&loadedModel, 17, 23));
    CHECK(pdguiModelPreviewContentReady(&loadedModel, 17, 0));
    CHECK(pdguiModelPreviewContentReady(&loadedModel, 17, 17));
}

TEST_CASE("fixed offscreen viewport and scissor use framebuffer dimensions",
        "[model-preview][render-fidelity]")
{
    // The entire 512-square target maps to itself, independent of a
    // 1920x1080 main window or its native viewport size/letterbox offset.
    const auto full = gfxFramebufferMapCoords({0, 512, 512, 512},
        512, 512, 512, 512);
    CHECK(full.x == 0);
    CHECK(full.y == 0);
    CHECK(full.width == 512);
    CHECK(full.height == 512);

    const auto clipped = gfxFramebufferMapCoords({20, 170, 200, 150},
        512, 512, 512, 512);
    CHECK(clipped.x == 20);
    CHECK(clipped.y == 342);
    CHECK(clipped.width == 200);
    CHECK(clipped.height == 150);

    // Optional FBO upscaling follows its own non-square applied dimensions.
    const auto scaled = gfxFramebufferMapCoords({20, 170, 200, 150},
        512, 512, 1024, 768);
    CHECK(scaled.x == 40);
    CHECK(scaled.y == 513);
    CHECK(scaled.width == 400);
    CHECK(scaled.height == 225);

    // A full main-sized target uses the same math when explicitly selected,
    // without allowing those dimensions to leak into the fixed preview.
    const auto mainSized = gfxFramebufferMapCoords({0, 240, 320, 240},
        320, 240, 1920, 1080);
    CHECK(mainSized.y == 0);
    CHECK(mainSized.width == 1920);
    CHECK(mainSized.height == 1080);
}
