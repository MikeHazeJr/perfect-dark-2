/*
 * test_right_stick_scroll.cpp -- Right-stick smooth-scroll spec (cohort 2).
 *
 * Per Mike's directive: "test scrolling with the right stick (or scroll
 * automatically as needed if navigating manually)".
 * Per flat-menu-navigation Rule 7: when a menu's content area has a
 * scrollbar, pushing right stick up/down smoothly scrolls the visible
 * region. Speed proportional to deflection (analog response). Deadzone
 * filters out resting drift.
 *
 * The runtime implementation lives in
 * port/fast3d/pdgui_backend.cpp::pdguiDriveImGuiNav. This suite is the
 * SPEC: the pure helper below is the formal definition of the expected
 * scroll math.
 *
 * s036-07 (c036, 2026-05-12): spec constants synchronized to the live
 * runtime values (the original 2026-04-25 spec had drifted from the
 * actual implementation - deadzone 0.15 vs runtime 0.18, exponent 1.7
 * vs runtime 2.0, max speed 1200 px/sec vs runtime 28 px/frame at 60 Hz
 * = 1680 px/sec). The Runtime-Spec Link test at the bottom of this file
 * is the drift detector: any future change to those magic numbers in
 * pdgui_backend.cpp will fail it. If Mike chooses to tune the feel,
 * update BOTH the runtime values AND the constants below in one commit.
 *
 * The directive marks rendering FEEL out-of-scope for headless tests
 * (Mike verifies in-game). Math is in-scope.
 *
 * @SYNC context/designs/flat-menu-navigation.md Rule 7
 * @SYNC port/fast3d/pdgui_backend.cpp::pdguiDriveImGuiNav (right-stick path)
 */

#include "catch.hpp"
#include <math.h>
#include <fstream>
#include <sstream>
#include <string>

namespace {

static std::string readTextFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/* SCROLL SPEC
 * ===========
 * Inputs:
 *   stick_y     in [-1, 1]  - right stick Y axis (positive = down convention).
 *   dt_seconds  > 0          - frame time.
 *
 * Constants (must match pdgui_backend.cpp::pdguiDriveImGuiNav):
 *   DEADZONE              = 0.18f  (axis magnitudes below this read as 0)
 *   MAX_SPEED_PX_PER_SEC  = 1680   (= 28 px/frame * 60 Hz; this is the
 *                                    runtime cap expressed per-second so
 *                                    the dt-scaling tests below work
 *                                    independently of the 60 Hz constant)
 *   CURVE_EXPONENT        = 2.0f   (squared response - small deflections
 *                                    scroll very slowly, full deflection
 *                                    hits MAX_SPEED)
 *
 * Output:
 *   delta_px_per_sec   = sign(stick_y) * pow(|adjusted|, CURVE_EXPONENT)
 *                        * MAX_SPEED_PX_PER_SEC
 *   delta_px_per_frame = delta_px_per_sec * dt_seconds
 *
 * where adjusted = (|stick_y| - DEADZONE) / (1.0 - DEADZONE), clamped
 * to >= 0 (anything in the deadzone yields 0).
 */
constexpr float kDeadzone   = 0.18f;
constexpr float kMaxSpeedPx = 1680.0f;
constexpr float kCurveExp   = 2.0f;

float scrollDelta(float stick_y, float dt_seconds)
{
    if (dt_seconds <= 0.0f) return 0.0f;
    float mag = stick_y < 0.0f ? -stick_y : stick_y;
    if (mag <= kDeadzone) return 0.0f;
    float adjusted = (mag - kDeadzone) / (1.0f - kDeadzone);
    if (adjusted < 0.0f) adjusted = 0.0f;
    if (adjusted > 1.0f) adjusted = 1.0f;
    float curved = powf(adjusted, kCurveExp);
    float sign = stick_y < 0.0f ? -1.0f : 1.0f;
    return sign * curved * kMaxSpeedPx * dt_seconds;
}

} /* namespace */

TEST_CASE("right-stick scroll: zero deflection is zero scroll", "[scroll][deadzone]")
{
    REQUIRE(scrollDelta(0.0f, 1.0f / 60.0f) == 0.0f);
}

TEST_CASE("right-stick scroll: deadzone suppresses resting drift",
          "[scroll][deadzone]")
{
    /* Anything within +/- deadzone reads as zero. Resting analog sticks
     * commonly report 0.05-0.10 magnitudes due to spring noise. */
    REQUIRE(scrollDelta(0.05f, 1.0f / 60.0f) == 0.0f);
    REQUIRE(scrollDelta(-0.10f, 1.0f / 60.0f) == 0.0f);
    REQUIRE(scrollDelta(0.179f, 1.0f / 60.0f) == 0.0f);
}

TEST_CASE("right-stick scroll: small positive deflection scrolls slowly down",
          "[scroll][curve]")
{
    float dt = 1.0f / 60.0f;
    float small  = scrollDelta(0.30f, dt);
    float bigger = scrollDelta(0.70f, dt);
    float full   = scrollDelta(1.00f, dt);

    REQUIRE(small > 0.0f);
    REQUIRE(bigger > small);
    REQUIRE(full > bigger);
    /* Super-linear curve: doubling input MORE than doubles output. Gives
     * fine control near zero (small deflection scrolls slowly) and fast
     * traversal near full deflection. The exponent is > 1, so the
     * actual output ratio exceeds the linear ratio. */
    REQUIRE(bigger > (small * (0.70f / 0.30f)));
}

TEST_CASE("right-stick scroll: full-up deflection is bounded at max speed",
          "[scroll][cap]")
{
    /* At dt = 1 second and full deflection, output equals MAX_SPEED_PX_PER_SEC. */
    float v = scrollDelta(1.0f, 1.0f);
    REQUIRE(v == Approx(1680.0f).margin(0.01f));
}

TEST_CASE("right-stick scroll: sign flips with stick direction",
          "[scroll][direction]")
{
    float dt = 1.0f / 60.0f;
    float down = scrollDelta(0.50f, dt);
    float up   = scrollDelta(-0.50f, dt);
    REQUIRE(down > 0.0f);
    REQUIRE(up < 0.0f);
    REQUIRE(down == Approx(-up).margin(1e-5f));
}

TEST_CASE("right-stick scroll: dt scales output linearly", "[scroll][frametime]")
{
    /* Same stick, double the frame time → double the per-frame scroll
     * (so absolute scroll-per-second is invariant). */
    float oneSixtieth = scrollDelta(0.50f, 1.0f / 60.0f);
    float oneThirtieth = scrollDelta(0.50f, 1.0f / 30.0f);
    REQUIRE(oneThirtieth == Approx(oneSixtieth * 2.0f).margin(1e-5f));
}

TEST_CASE("right-stick scroll: degenerate dt is rejected", "[scroll][robustness]")
{
    REQUIRE(scrollDelta(1.0f,   0.0f) == 0.0f);
    REQUIRE(scrollDelta(1.0f,  -1.0f) == 0.0f);
}

TEST_CASE("right-stick scroll: monotonic across deflection range",
          "[scroll][monotonic]")
{
    float dt = 1.0f / 60.0f;
    float prev = -1.0f;
    for (int step = 0; step <= 100; step++) {
        float s = (float)step / 100.0f;
        float v = scrollDelta(s, dt);
        REQUIRE(v >= prev);   /* monotonic non-decreasing */
        prev = v;
    }
}

TEST_CASE("right-stick scroll: accumulator over a held stick yields a finite total",
          "[scroll][accumulator]")
{
    /* Drive the helper for 60 frames at 60 fps with a fully-deflected
     * stick. The total should be ~MAX_SPEED * 1 second = 1680 px
     * (28 px/frame * 60 frames). */
    float dt = 1.0f / 60.0f;
    float total = 0.0f;
    for (int i = 0; i < 60; i++) {
        total += scrollDelta(1.0f, dt);
    }
    REQUIRE(total == Approx(1680.0f).margin(1.0f));
}

/* s036-07 (c036, 2026-05-12): runtime-spec drift detector.
 *
 * The pure scrollDelta() helper above is the SPEC. The runtime is
 * port/fast3d/pdgui_backend.cpp::pdguiDriveImGuiNav. Until the runtime
 * is refactored to consume scrollDelta() directly (an architectural
 * change beyond this slice), this test catches drift via static text
 * inspection. Failure here means the runtime constants changed without
 * a paired update to the kDeadzone / kMaxSpeedPx / kCurveExp constants
 * above (or vice versa).
 *
 * @SYNC port/fast3d/pdgui_backend.cpp::pdguiDriveImGuiNav
 */
TEST_CASE("right-stick scroll: runtime constants match spec",
          "[scroll][static][link]")
{
    const std::string backend = readTextFile("port/fast3d/pdgui_backend.cpp");
    REQUIRE_FALSE(backend.empty());

    /* Deadzone constant (matches kDeadzone above). */
    REQUIRE(backend.find("const f32 deadzone = 0.18f") != std::string::npos);

    /* Max per-frame speed (matches kMaxSpeedPx / 60.0 above). */
    REQUIRE(backend.find("const f32 maxPxPerFrame = 28.0f") != std::string::npos);

    /* Quadratic curve (matches kCurveExp = 2.0 above; t * t == pow(t,2)). */
    REQUIRE(backend.find("dir * t * t * maxPxPerFrame") != std::string::npos);

    /* The runtime gate stays on gameplayInputSuppressed so the right
     * stick scrolls only while a menu owns input (and not during
     * gameplay aim). */
    REQUIRE(backend.find("if (gameplayInputSuppressed())") != std::string::npos);
}
