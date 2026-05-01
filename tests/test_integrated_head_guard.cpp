/*
 * tests/test_integrated_head_guard.cpp -- H.5 universal guard (S593):
 * pin the integrated-head head-selector lock across every body+head
 * picker site.
 *
 * Bodies with `unk00_01 == 1` (Skedar, Dr Carroll, Eye Spy) carry their
 * own head model.  The renderer's request seam at
 * `pdguiCharPreviewRequestEx` already clears the requested head when a
 * body declares `complete` (catalogGetBodyIsComplete).  Without a
 * matching guard at the picker UI, the user can still cycle / pick a
 * head that the renderer silently drops -- "bad UI to leave the
 * jankiness in there" per Mike's playtest report.
 *
 * Mike's directive (S593): "The fix should be applied UNIVERSALLY
 * across all the picker sites. No half measures -- every picker that
 * lets a user select head + body together needs the guard."
 *
 * The four pickers are:
 *   - port/fast3d/pdgui_menu_agentcreate.cpp     (Agent Create / New Agent)
 *   - port/fast3d/pdgui_menu_playerconfig.cpp    (Player Config Character)
 *   - port/fast3d/pdgui_menu_botsetup.cpp        (Simulant Character)
 *   - port/fast3d/pdgui_menu_room.cpp            (Room Change Character modal)
 *
 * These tests are static / source-text checks (same shape as
 * test_catalog_checked.cpp's body0f02ce8c source pins).  They guarantee
 * that any future refactor that drops the guard from one of the four
 * pickers fails CI loud rather than silently shipping a half-measure.
 *
 * Each picker MUST:
 *   1. Read the catalog via `catalogGetBodyIsComplete` (or wrap it in a
 *      helper that does).
 *   2. Pass the resulting bool into `ImGui::BeginDisabled(...)` for the
 *      head selector so clicks are inert.
 *   3. Surface the lock state textually -- either "(integrated)" in the
 *      label or a tooltip / annotation near the disabled control.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());

	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

/* Agent Create -- carousel-style head picker.  Established the original
 * H.5 guard pattern (commit c66b02fc / B-241 + the original carousel
 * polish); subsequent pickers mirror it. */
TEST_CASE("agentcreate: integrated-head guard locks head carousel",
          "[catalog][catalog-mgr-body][s593][integrated-head][agentcreate]") {
	const std::string src =
		readTextFile("port/fast3d/pdgui_menu_agentcreate.cpp");

	/* Helper exists and reads catalogGetBodyIsComplete via the resolved
	 * runtime_index. */
	REQUIRE(src.find("s_bodyHasIntegratedHead") != std::string::npos);
	REQUIRE(src.find("catalogGetBodyIsComplete(be->runtime_index)")
	        != std::string::npos);

	/* Carousel buttons + label sit inside BeginDisabled keyed on the
	 * helper. */
	REQUIRE(src.find("bool integratedHead = s_bodyHasIntegratedHead(s_SelectedBody);")
	        != std::string::npos);
	REQUIRE(src.find("ImGui::BeginDisabled(integratedHead || s_HeadListCount <= 1);")
	        != std::string::npos);

	/* Label states the lock so the UI is unambiguous. */
	REQUIRE(src.find("\"(integrated)\"") != std::string::npos);
}

/* Player Config (Character) -- carousel-style head picker.  Same shape
 * as Agent Create's carousel; the guard MUST also apply here. */
TEST_CASE("playerconfig: integrated-head guard locks head carousel",
          "[catalog][catalog-mgr-body][s593][integrated-head][playerconfig]") {
	const std::string src =
		readTextFile("port/fast3d/pdgui_menu_playerconfig.cpp");

	/* Helper exists and reads catalogGetBodyIsComplete via the resolved
	 * runtime_index. */
	REQUIRE(src.find("s_pcBodyHasIntegratedHead") != std::string::npos);
	REQUIRE(src.find("catalogGetBodyIsComplete(be->runtime_index)")
	        != std::string::npos);

	/* Carousel arrow buttons sit inside BeginDisabled with the
	 * integratedHead flag wired in alongside the canCycle predicate. */
	REQUIRE(src.find("bool integratedHead = s_pcBodyHasIntegratedHead(committedBodyId);")
	        != std::string::npos);
	REQUIRE(src.find("ImGui::BeginDisabled(integratedHead || !canCycle);")
	        != std::string::npos);

	/* Label communicates the lock. */
	REQUIRE(src.find("\"(integrated)\"") != std::string::npos);
}

/* Bot Setup (Simulant Character) -- combo dropdown.  Same body+head
 * choice surface as the carousels but keyed on legacy mp_idx; the guard
 * resolves the catalog id via `catalogMpBodyId`. */
TEST_CASE("botsetup: integrated-head guard locks head dropdown",
          "[catalog][catalog-mgr-body][s593][integrated-head][botsetup]") {
	const std::string src =
		readTextFile("port/fast3d/pdgui_menu_botsetup.cpp");

	/* Helper exists, takes mp_idx, resolves to runtime_index via
	 * catalogMpBodyId + assetCatalogResolve. */
	REQUIRE(src.find("s_bsBodyHasIntegratedHead") != std::string::npos);
	REQUIRE(src.find("catalogMpBodyId(mp_idx)") != std::string::npos);
	REQUIRE(src.find("catalogGetBodyIsComplete(be->runtime_index)")
	        != std::string::npos);

	/* Combo dropdown sits inside BeginDisabled with the integratedHead
	 * flag computed from the bot's currently-equipped body mp_idx. */
	REQUIRE(src.find("bool integratedHead = s_bsBodyHasIntegratedHead(curBodyMpIdx);")
	        != std::string::npos);
	REQUIRE(src.find("ImGui::BeginDisabled(integratedHead);")
	        != std::string::npos);

	/* Label communicates the lock. */
	REQUIRE(src.find("\"(integrated)\"") != std::string::npos);
}

/* Room Change Character modal -- Selectable list inside a modal popup.
 * Same body+head choice surface; the guard locks the entire head list
 * and clears the pending head id when an integrated body is picked so
 * the wire / save side never carries a stale head. */
TEST_CASE("room: integrated-head guard locks change-character head list",
          "[catalog][catalog-mgr-body][s593][integrated-head][room]") {
	const std::string src =
		readTextFile("port/fast3d/pdgui_menu_room.cpp");

	/* Forward declaration of catalogGetBodyIsComplete. */
	REQUIRE(src.find("s32 catalogGetBodyIsComplete(s32 bodynum);")
	        != std::string::npos);

	/* Body Selectable handler clears s_PendingCharHeadId when the picked
	 * body declares an integrated head (no stale head id on the wire). */
	REQUIRE(src.find("if (integrated) {\n"
	                 "                        s_PendingCharHeadId[0] = '\\0';")
	        != std::string::npos);

	/* Head list pre-pass resolves the integratedHead flag. */
	REQUIRE(src.find("bool integratedHead = false;")
	        != std::string::npos);

	/* Header annotation surfaces the lock. */
	REQUIRE(src.find("\"Head  (integrated)\"")
	        != std::string::npos);

	/* Selectable list sits inside BeginDisabled. */
	REQUIRE(src.find("ImGui::BeginDisabled(integratedHead);")
	        != std::string::npos);
}

/* The renderer's request seam already clears the head when the body
 * declares `complete`.  This test pins that gate so the picker UI guards
 * stay belt-and-braces -- if a future refactor accidentally removes
 * this seam, the picker guards alone are not enough. */
TEST_CASE("charpreview: request seam clears head for integrated body",
          "[catalog][catalog-mgr-body][s593][integrated-head][seam]") {
	const std::string src =
		readTextFile("port/fast3d/pdgui_charpreview.c");

	REQUIRE(src.find("if (catalogGetBodyIsComplete(bodynum)) {")
	        != std::string::npos);
	REQUIRE(src.find("Integrated-head body (Skedar, Dr Carroll)")
	        != std::string::npos);
	/* Comment captures the design intent so search-by-symptom finds it. */
	REQUIRE(src.find("drop the requested head")
	        != std::string::npos);
}

/* B-297 LOUDFAIL channel -- a silently-black preview FBO is a class of
 * "user reports preview is black on screen X" bugs that the warning
 * channels in `menu.c::menuRenderModel` already log per cause but that
 * required cross-referencing.  The `PREVIEW.FBO.BLACK:` channel surfaces
 * the symptom directly at the FBO render seam. */
TEST_CASE("charpreview: LOUDFAIL channel for silent black FBO",
          "[catalog][catalog-mgr-body][s593][b-291][loudfail]") {
	const std::string src =
		readTextFile("port/fast3d/pdgui_charpreview.c");

	REQUIRE(src.find("PREVIEW.FBO.BLACK:") != std::string::npos);
	/* Gated by bodymodeldef==NULL so routine loading frames stay quiet. */
	REQUIRE(src.find("if (mm->bodymodeldef == NULL) {") != std::string::npos);
	REQUIRE(src.find("model render skipped, FBO will display black")
	        != std::string::npos);
}

/* Agent Create initialization -- B-297 root-cause fix.  Seed the
 * carousel from the player's currently-saved body/head pair so the
 * opening selection is always rig-compatible (same baseline Player
 * Config uses).  The previous default (alphabetically-first body +
 * alphabetically-first head, picked independently) could land on a
 * pair that the renderer's rig-fallback handled but that hit a body
 * load issue downstream -- silent WARNING + black FBO. */
TEST_CASE("agentcreate: opening selection seeded from player config",
          "[catalog][catalog-mgr-body][s593][b-291][agentcreate-seed]") {
	const std::string src =
		readTextFile("port/fast3d/pdgui_menu_agentcreate.cpp");

	/* Forward decls for the player-config getters that supply the seed. */
	REQUIRE(src.find("const char *mpPlayerConfigGetBodyId(s32 playernum);")
	        != std::string::npos);
	REQUIRE(src.find("const char *mpPlayerConfigGetHeadId(s32 playernum);")
	        != std::string::npos);

	/* IsWindowAppearing branch reads them and seeds the carousel
	 * positions via the existing findBodyIndexById / findHeadIndexById
	 * helpers. */
	REQUIRE(src.find("playerBodyId = mpPlayerConfigGetBodyId(pnum)")
	        != std::string::npos);
	REQUIRE(src.find("playerHeadId = mpPlayerConfigGetHeadId(pnum)")
	        != std::string::npos);
	REQUIRE(src.find("s_SelectedBody = findBodyIndexById(playerBodyId);")
	        != std::string::npos);
	REQUIRE(src.find("s_SelectedHead = findHeadIndexById(playerHeadId);")
	        != std::string::npos);
}
