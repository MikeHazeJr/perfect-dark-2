/**
 * test_model_rodata_guard.cpp -- the readable-memory probe and one walk's span
 * (port/include/model_rodata_guard.h).
 *
 * The probe asks the operating system whether a range of memory can be read.
 * A display list walk checks thousands of ranges that sit side by side, and a
 * system call for each one held a rider of a 5,829-triangle mod vehicle near
 * 20 frames a second (B-1234). A span remembers the run of readable pages the
 * walk's last question found, so a range inside it costs two compares.
 *
 * The cases lay out their own pages, so each answer is known beforehand.
 */
#include "catch.hpp"

#include "model_rodata_guard.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

namespace {

/**
 * Five pages in a row:
 *   0 and 1  committed, readable
 *   2        committed, no access
 *   3        committed, readable
 *   4        reserved, not committed
 */
struct Pages {
	unsigned char *base = nullptr;
	size_t page = 0;

	Pages()
	{
		SYSTEM_INFO info;
		GetSystemInfo(&info);
		page = info.dwPageSize;
		base = static_cast<unsigned char *>(
			VirtualAlloc(nullptr, page * 5, MEM_RESERVE, PAGE_NOACCESS));
		if (base != nullptr) {
			const bool ok =
				VirtualAlloc(base, page * 2, MEM_COMMIT, PAGE_READWRITE) != nullptr &&
				VirtualAlloc(base + page * 2, page, MEM_COMMIT, PAGE_NOACCESS) != nullptr &&
				VirtualAlloc(base + page * 3, page, MEM_COMMIT, PAGE_READWRITE) != nullptr;
			if (!ok) {
				VirtualFree(base, 0, MEM_RELEASE);
				base = nullptr;
			}
		}
	}
	~Pages()
	{
		if (base != nullptr) {
			VirtualFree(base, 0, MEM_RELEASE);
		}
	}
	Pages(const Pages &) = delete;
	Pages &operator=(const Pages &) = delete;

	const unsigned char *at(size_t pageIndex, size_t offset = 0) const
	{
		return base + page * pageIndex + offset;
	}
	uintptr_t addr(size_t pageIndex) const
	{
		return reinterpret_cast<uintptr_t>(base) + page * pageIndex;
	}
};

} // namespace

TEST_CASE("The probe answers for the whole range it is asked about", "[model-rodata-guard]")
{
	Pages p;
	REQUIRE(p.base != nullptr);

	CHECK(modelRodataIsReadable(nullptr, 16) == 0);
	CHECK(modelRodataIsReadable(p.at(0), 0) == 0);
	CHECK(modelRodataIsReadable(p.at(0), 16) == 1);
	// Two readable pages side by side are one readable range.
	CHECK(modelRodataIsReadable(p.at(0), p.page * 2) == 1);
	CHECK(modelRodataIsReadable(p.at(1, p.page - 16), 16) == 1);
	// One byte into the page that cannot be read.
	CHECK(modelRodataIsReadable(p.at(1, p.page - 16), 17) == 0);
	CHECK(modelRodataIsReadable(p.at(2), 16) == 0);
	CHECK(modelRodataIsReadable(p.at(3), p.page) == 1);
	// Into the page that was never committed.
	CHECK(modelRodataIsReadable(p.at(3), p.page + 1) == 0);
	CHECK(modelRodataIsReadable(p.at(4), 16) == 0);
	// A range that wraps past the top of the address space is not readable.
	CHECK(modelRodataIsReadable(p.at(0), SIZE_MAX) == 0);
	CHECK(modelRodataIsReadable(reinterpret_cast<const void *>(UINTPTR_MAX - 7), 16) == 0);
}

TEST_CASE("A span gives the probe's answers", "[model-rodata-guard]")
{
	Pages p;
	REQUIRE(p.base != nullptr);

	model_rodata_span_t span = { 0, 0 };
	CHECK(modelRodataIsReadableInSpan(&span, nullptr, 16) == 0);
	CHECK(modelRodataIsReadableInSpan(&span, p.at(0), 0) == 0);
	CHECK(modelRodataIsReadableInSpan(&span, p.at(0), SIZE_MAX) == 0);
	// Nothing readable was found yet, so nothing is remembered.
	CHECK(span.lo == 0);
	CHECK(span.hi == 0);

	// The first readable range: the span becomes the run of readable pages
	// from the range's page to the page that cannot be read.
	CHECK(modelRodataIsReadableInSpan(&span, p.at(0, 64), 16) == 1);
	CHECK(span.lo == p.addr(0));
	CHECK(span.hi == p.addr(2));

	CHECK(modelRodataIsReadableInSpan(&span, p.at(1, p.page - 16), 16) == 1);
	CHECK(modelRodataIsReadableInSpan(&span, p.at(0), p.page * 2) == 1);

	// A range that leaves the span is asked about, refused, and the span
	// keeps what it knew.
	CHECK(modelRodataIsReadableInSpan(&span, p.at(1, p.page - 16), 17) == 0);
	CHECK(modelRodataIsReadableInSpan(&span, p.at(2), 16) == 0);
	CHECK(modelRodataIsReadableInSpan(&span, p.at(4), 16) == 0);
	CHECK(span.lo == p.addr(0));
	CHECK(span.hi == p.addr(2));

	// A readable run that does not touch the span replaces it.
	CHECK(modelRodataIsReadableInSpan(&span, p.at(3, 8), 16) == 1);
	CHECK(span.lo == p.addr(3));
	CHECK(span.hi == p.addr(4));
	CHECK(modelRodataIsReadableInSpan(&span, p.at(3), p.page + 1) == 0);
	// And the first run is asked about again.
	CHECK(modelRodataIsReadableInSpan(&span, p.at(0), 16) == 1);
	CHECK(span.lo == p.addr(0));
	CHECK(span.hi == p.addr(2));

	// With no span it is the plain probe.
	CHECK(modelRodataIsReadableInSpan(nullptr, p.at(0), 16) == 1);
	CHECK(modelRodataIsReadableInSpan(nullptr, p.at(2), 16) == 0);
	CHECK(modelRodataIsReadableInSpan(nullptr, nullptr, 16) == 0);
}

TEST_CASE("A walk that reads memory in falling order still ends with one span",
          "[model-rodata-guard]")
{
	Pages p;
	REQUIRE(p.base != nullptr);

	// A question reports from the asked page onward, so the higher page first
	// gives a span that starts at that page.
	model_rodata_span_t span = { 0, 0 };
	CHECK(modelRodataIsReadableInSpan(&span, p.at(1, 32), 16) == 1);
	CHECK(span.lo == p.addr(1));
	CHECK(span.hi == p.addr(2));

	// The lower page is outside it. Its run overlaps the span, and the two
	// join into one.
	CHECK(modelRodataIsReadableInSpan(&span, p.at(0, 32), 16) == 1);
	CHECK(span.lo == p.addr(0));
	CHECK(span.hi == p.addr(2));
}

TEST_CASE("A span answers from memory, which is why it lives for one walk only",
          "[model-rodata-guard]")
{
	Pages p;
	REQUIRE(p.base != nullptr);

	model_rodata_span_t span = { 0, 0 };
	REQUIRE(modelRodataIsReadableInSpan(&span, p.at(0), 16) == 1);
	REQUIRE(span.hi == p.addr(2));

	// The second page stops being readable after the span learned of it.
	DWORD old = 0;
	REQUIRE(VirtualProtect(p.base + p.page, p.page, PAGE_NOACCESS, &old) != 0);

	// The plain probe asks the operating system and sees the change.
	CHECK(modelRodataIsReadable(p.at(1), 16) == 0);
	// The span does not ask: this is the saving, and the reason a span must
	// never be kept past the walk that filled it.
	CHECK(modelRodataIsReadableInSpan(&span, p.at(1), 16) == 1);

	// A new walk starts with a new span and gets the true answer.
	model_rodata_span_t fresh = { 0, 0 };
	CHECK(modelRodataIsReadableInSpan(&fresh, p.at(1), 16) == 0);
	CHECK(modelRodataIsReadableInSpan(&fresh, p.at(0), 16) == 1);
	CHECK(fresh.lo == p.addr(0));
	CHECK(fresh.hi == p.addr(1));
}

#endif // _WIN32
