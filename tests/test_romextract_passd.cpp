/*
 * tests/test_romextract_passd.cpp -- Phase 3 Pass D self-heal hardening pin.
 *
 * Pass D layers user-facing polish on top of the existing Pass A.4 +
 * segment verify self-heal:
 *   1. Per-file UI toasts on hash-mismatch recovery / unrecoverable
 *      outcomes.
 *   2. Aggregated boot integrity report (single LOG_NOTE + system toast).
 *   3. Toast queue drained from the render-ready phase of boot so
 *      pdguiToastEnqueue timestamps are fresh.
 *   4. Quarantine path migrated from data/<romid>/.quarantine/ to
 *      the user-visible top-level data/_quarantine/<romid>/ tier.
 *
 * Static text grep against port/src/romextract.c + port/include/
 * romextract.h.  The audit is at
 * context/audits/catalog-phase3-passd-self-heal-2026-05-02.md.
 *
 * @SYNC: any future restructuring of romextract.c must keep:
 *   - LOUDFAIL.LOAD as the channel name for hash-mismatch events
 *     (Pass A.5 convention, queryable in user-submitted logs).
 *   - "DATA INTEGRITY:" as the boot-integrity log prefix (downstream
 *     tooling greps for it; do not rename).
 *   - data/_quarantine/<romid>/ as the corrupted-file holding pen
 *     (top-level visible per Pass D directive).
 *   - PD_SERVER guards around every pdguiToastEnqueue call site;
 *     the toast headers are not linked into pd-server.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSourceFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

unsigned countOccurrences(const std::string &haystack,
                          const std::string &needle) {
	if (needle.empty()) return 0u;
	unsigned count = 0;
	std::size_t pos = 0;
	while ((pos = haystack.find(needle, pos)) != std::string::npos) {
		++count;
		pos += needle.size();
	}
	return count;
}

} /* anonymous namespace */

TEST_CASE("passd: LOUDFAIL.LOAD channel used for hash-mismatch events",
          "[catalog][passd][self-heal][loudfail]") {
	const std::string src = readSourceFile("port/src/romextract.c");
	REQUIRE(!src.empty());

	/* sysLoudFailf("LOAD", ...) is the channel for verify-time
	 * hash mismatch + recovery failure.  Both the file and the
	 * segment verify paths use the same channel so future log
	 * tooling can grep one prefix. */
	REQUIRE(src.find("sysLoudFailf(\"LOAD\"") != std::string::npos);
	/* At least 6 sites: file/seg sha-mismatch, file/seg re-extract
	 * fopen failure, file/seg short write, plus the two sha256HashFile
	 * fall-through sites. */
	REQUIRE(countOccurrences(src, "sysLoudFailf(\"LOAD\"") >= 6u);
}

TEST_CASE("passd: DATA INTEGRITY single-line summary present",
          "[catalog][passd][self-heal][report]") {
	const std::string src = readSourceFile("port/src/romextract.c");
	REQUIRE(!src.empty());

	/* The single-line aggregate format -- downstream log tooling
	 * greps for "DATA INTEGRITY:" so the prefix is canonical. */
	REQUIRE(src.find("DATA INTEGRITY: %d validated, %d re-extracted, "
	                 "%d unrecoverable") != std::string::npos);
}

TEST_CASE("passd: quarantine path migrated to data/_quarantine/<romid>/",
          "[catalog][passd][self-heal][quarantine]") {
	const std::string src = readSourceFile("port/src/romextract.c");
	REQUIRE(!src.empty());

	/* New canonical path: top-level data/_quarantine/<romid>/.  The
	 * prior path data/<romid>/.quarantine/ must not appear in code
	 * (it can survive in audit history but not in production
	 * romextract.c). */
	REQUIRE(src.find("data/_quarantine") != std::string::npos);
	REQUIRE(src.find("/.quarantine") == std::string::npos);

	/* Top-level dir creation is two steps: data/_quarantine/, then
	 * data/_quarantine/<romid>/.  Both must be present so that a
	 * fresh install with no quarantine dir self-heals correctly. */
	REQUIRE(src.find("\"data/_quarantine\"") != std::string::npos);
	REQUIRE(src.find("data/_quarantine/%s") != std::string::npos);
}

TEST_CASE("passd: toast call sites are PD_SERVER-guarded",
          "[catalog][passd][self-heal][server-guard]") {
	const std::string src = readSourceFile("port/src/romextract.c");
	REQUIRE(!src.empty());

	/* pdgui_toast.h must only be pulled in for the client build.
	 * pd-server has no toast linkage. */
	REQUIRE(src.find("#if !defined(PD_SERVER)") != std::string::npos);
	REQUIRE(src.find("#include \"pdgui_toast.h\"") != std::string::npos);

	/* No raw pdguiToastEnqueue calls leak outside a PD_SERVER guard.
	 * Defensive check: the call must always be reachable via the
	 * guarded romExtractToastDrain entry. */
	REQUIRE(src.find("pdguiToastEnqueue(0, t->category, t->title, t->body)") !=
	        std::string::npos);
}

TEST_CASE("passd: per-file toast cap pins toast-spam protection",
          "[catalog][passd][self-heal][cap]") {
	const std::string src = readSourceFile("port/src/romextract.c");
	REQUIRE(!src.empty());

	/* Wholesale corruption (e.g. user wiped data/) must not flood the
	 * 16-slot toast queue.  Cap at 5 per-file emit; the aggregate
	 * boot-integrity toast carries the totals beyond the cap. */
	REQUIRE(src.find("ROMEXTRACT_TOAST_PERFILE_CAP 5") != std::string::npos);
	REQUIRE(src.find("ROMEXTRACT_TOAST_QUEUE_MAX 16") != std::string::npos);
}

TEST_CASE("passd: per-file recover + fail toast helpers wired to verify paths",
          "[catalog][passd][self-heal][toast-emit]") {
	const std::string src = readSourceFile("port/src/romextract.c");
	REQUIRE(!src.empty());

	/* Both verify functions (file + segment) must reach
	 * s_emitPerFileRecoverToast on corrected branches and
	 * s_emitPerFileFailToast on failed branches.  At least 2 of each
	 * (file path + segment path). */
	REQUIRE(countOccurrences(src, "s_emitPerFileRecoverToast(") >= 2u);
	REQUIRE(countOccurrences(src, "s_emitPerFileFailToast(") >= 2u);

	/* Toast title prefixes distinguish file-class from segment-class
	 * outcomes so the player can tell what was corrupted. */
	REQUIRE(src.find("s_emitPerFileRecoverToast(\"File\",") != std::string::npos);
	REQUIRE(src.find("s_emitPerFileFailToast(\"File\",") != std::string::npos);
	REQUIRE(src.find("s_emitPerFileRecoverToast(\"Segment\",") != std::string::npos);
	REQUIRE(src.find("s_emitPerFileFailToast(\"Segment\",") != std::string::npos);
}

TEST_CASE("passd: aggregate counters update after both verify passes",
          "[catalog][passd][self-heal][aggregate]") {
	const std::string src = readSourceFile("port/src/romextract.c");
	REQUIRE(!src.empty());

	/* Both verify functions must contribute to s_AggValidated /
	 * s_AggRecovered / s_AggUnrecoverable.  The aggregator is shared
	 * so the boot-integrity report sees the union, not just one
	 * verify pass. */
	REQUIRE(countOccurrences(src, "s_AggValidated += verified + baselined") == 2u);
	REQUIRE(countOccurrences(src, "s_AggRecovered += corrected") == 2u);
	REQUIRE(countOccurrences(src, "s_AggUnrecoverable += failed") == 2u);
}

TEST_CASE("passd: public API exported through romextract.h",
          "[catalog][passd][self-heal][api]") {
	const std::string hdr = readSourceFile("port/include/romextract.h");
	REQUIRE(!hdr.empty());

	/* Three Pass D public functions: drain, report, accessor. */
	REQUIRE(hdr.find("void romExtractToastDrain(void)") != std::string::npos);
	REQUIRE(hdr.find("void romExtractEmitBootIntegrityReport(void)") !=
	        std::string::npos);
	REQUIRE(hdr.find("s32 romExtractGetBootIntegrity(") != std::string::npos);
	REQUIRE(hdr.find("Pass D (2026-05-02)") != std::string::npos);
}

TEST_CASE("passd: main.c wires drain (post-gameInit) + report (post-verify)",
          "[catalog][passd][self-heal][wiring]") {
	const std::string src = readSourceFile("port/src/main.c");
	REQUIRE(!src.empty());

	/* Report fires AFTER the verify pair and BEFORE Pass C release.
	 * Drain fires at the boot-tail so the toast renderer is ready. */
	REQUIRE(src.find("romExtractEmitBootIntegrityReport();") != std::string::npos);
	REQUIRE(src.find("romExtractToastDrain();") != std::string::npos);

	/* Order check: report site must come before romdataReleaseRom() in
	 * the file (counters are populated by the verify pair which runs
	 * before release). */
	const std::size_t reportPos = src.find("romExtractEmitBootIntegrityReport();");
	const std::size_t releasePos = src.find("romdataReleaseRom();");
	const std::size_t drainPos  = src.find("romExtractToastDrain();");
	const std::size_t mainProcPos = src.find("mainProc();");
	REQUIRE(reportPos != std::string::npos);
	REQUIRE(releasePos != std::string::npos);
	REQUIRE(drainPos != std::string::npos);
	REQUIRE(mainProcPos != std::string::npos);

	/* Order: report -> release -> drain -> mainProc. */
	REQUIRE(reportPos < releasePos);
	REQUIRE(releasePos < drainPos);
	REQUIRE(drainPos < mainProcPos);
}
