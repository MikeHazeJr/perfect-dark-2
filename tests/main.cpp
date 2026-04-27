/*
 * tests/main.cpp -- Catch2 entry point for the pd-tests binary.
 *
 * This is the ONLY translation unit that defines CATCH_CONFIG_MAIN, so
 * Catch2 generates main() exactly once. Every other test_*.cpp just
 * includes catch.hpp without that define.
 *
 * See context/designs/testing-framework-2026-04-26.md for the framework
 * design and coverage roadmap.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"
