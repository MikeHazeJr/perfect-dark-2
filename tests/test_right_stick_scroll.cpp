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
 * scroll math. If the runtime code drifts from the spec, the next
 * follow-up wires it through this helper.
 *
 * The directive marks rendering FEEL out-of-scope for headless tests
 * (Mike verifies in-game). Math is in-scope.
 *
 * @SYNC context/designs/flat-menu-navigation.md Rule 7
 * @SYNC port/fast3d/pdgui_backend.cpp::pdguiDriveImGuiNav (right-stick path)
 */

#include "catch.hpp"
#include <math.h>

namespace {

/* SCROLL SPEC
 * ===========
 * Inputs:
 *   stick_y     in [-1, 1]  - right stick Y axis (positive = down convention).
 *   dt_seconds  > 0          - frame time.
 *
 * Constants:
 *   DEADZONE              = 0.15f  (axis magnitudes below this read as 0)
 *   MAX_SPEED_PX_PER_SEC  = 1200   (cap so a fully deflected stick scrolls
 *                                    at a predictable maximum, regardless
 *                                    of frame time)
 *   CURVE_EXPONENT        = 1.7f   (mild non-linear response - small
 *                                    deflections are slower, full deflection
 *                                    hits MAX_SPEED)
 *
 * Output:
 *   delta_px_per_frame = sign(stick_y) * pow(|adjusted|, CURVE_EXPONENT)
 *                        * MAX_SPEED_PX_PER_SEC * dt_seconds
 *
 * where adjusted = (|stick_y| - DEADZONE) / (1.0 - DEADZONE), clamped
 * to >= 0 (anything in the deadzone yields 0).
 */
constexpr float kDeadzone   = 0.15f;
constexpr float kMaxSpeedPx = 1200.0f;
constexpr float kCurveExp   = 1.7f;

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
    REQUIRE(scrollDelta(0.149f, 1.0f / 60.0f) == 0.0f);
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
    REQUIRE(v == Approx(1200.0f).margin(0.01f));
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
     * stick. The total should be ~MAX_SPEED * 1 second = 1200 px. */
    float dt = 1.0f / 60.0f;
    float total = 0.0f;
    for (int i = 0; i < 60; i++) {
        total += scrollDelta(1.0f, dt);
    }
    REQUIRE(total == Approx(1200.0f).margin(1.0f));
}
