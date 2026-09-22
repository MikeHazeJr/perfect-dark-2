#include "catch.hpp"
#include "modmgr_selection.h"
#include <fstream>
#include <iterator>

namespace {
using modmgr_selection::Entry;
using modmgr_selection::Selection;
struct Registry {
    std::vector<Entry> rows = {{"a", "mods/a", "1", true, true, false},
                              {"b", "mods/b", "1", false, true, false},
                              {"c", "mods/c", "1", true, true, false}};
    int mutations = 0;
    static void swap(void *opaque, int a, int b)
    {
        auto &registry = *static_cast<Registry *>(opaque);
        ++registry.mutations;
        std::swap(registry.rows[a], registry.rows[b]);
    }
    static void enable(void *opaque, int i, bool value)
    {
        auto &registry = *static_cast<Registry *>(opaque);
        ++registry.mutations;
        registry.rows[i].enabled = value;
    }
};
std::string source(const char *path)
{
    std::ifstream file(std::string(PD_SOURCE_DIR) + '/' + path, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::string section(const std::string &text, const char *first, const char *last)
{
    const auto begin = text.find(first);
    REQUIRE(begin != std::string::npos);
    const auto end = text.find(last, begin + 1);
    REQUIRE(end != std::string::npos);
    return text.substr(begin, end - begin);
}
}

TEST_CASE("Installed selection stages enable and order without mutating live registry",
          "[modmgr][selection][transaction]")
{
    Registry registry;
    Selection selection;
    std::string error;
    REQUIRE(selection.refresh(registry.rows, 10, error));
    REQUIRE(selection.setEnabled("b", true));
    REQUIRE(selection.move(2, 0));
    REQUIRE(selection.dirty());
    REQUIRE(registry.mutations == 0);
    REQUIRE(registry.rows[0].id == "a");
    REQUIRE_FALSE(registry.rows[1].enabled);
    REQUIRE(selection.entries()[0].id == "c");
    selection.discard();
    REQUIRE_FALSE(selection.dirty());
    REQUIRE(selection.entries()[0].id == "a");
    REQUIRE_FALSE(selection.enabled("b"));
    REQUIRE(registry.mutations == 0);
}

TEST_CASE("Installed Apply publishes complete stable-ID order then enabled state",
          "[modmgr][selection][transaction]")
{
    Registry registry;
    Selection selection;
    std::string error;
    REQUIRE(selection.refresh(registry.rows, 10, error));
    REQUIRE(selection.setEnabled("b", true));
    REQUIRE(selection.setEnabled("a", false));
    REQUIRE(selection.move(2, 0));
    REQUIRE(selection.move(1, 2));
    REQUIRE(selection.publish(registry.rows, 10, &registry, Registry::swap, Registry::enable, error));
    REQUIRE(registry.mutations > 0);
    REQUIRE(registry.rows.size() == selection.entries().size());
    for (size_t i = 0; i < registry.rows.size(); ++i) {
        REQUIRE(registry.rows[i].id == selection.entries()[i].id);
        REQUIRE(registry.rows[i].enabled == selection.entries()[i].enabled);
    }
    // Only the caller's post-Apply refresh marks the transaction accepted.
    REQUIRE(selection.dirty());
    REQUIRE(selection.refresh(registry.rows, 11, error));
    REQUIRE_FALSE(selection.dirty());
}

TEST_CASE("Stale registry identity and catalog generation reject every mutation before Apply",
          "[modmgr][selection][transaction]")
{
    for (int scenario = 0; scenario < 8; ++scenario) {
        Registry registry;
        Selection selection;
        std::string error;
        REQUIRE(selection.refresh(registry.rows, 10, error));
        REQUIRE(selection.setEnabled("b", true));
        REQUIRE(selection.move(0, 2));
        uint32_t generation = 10;
        switch (scenario) {
            case 0: ++generation; break;
            case 1: registry.rows.pop_back(); break;
            case 2: std::swap(registry.rows[0], registry.rows[1]); break;
            case 3: registry.rows[0].enabled = false; break;
            case 4: registry.rows[0].path = "replacement/a"; break;
            case 5: registry.rows[0].version = "2"; break;
            case 6: registry.rows[0].locked = true; break;
            case 7: registry.rows[0].valid = false; break;
        }
        CAPTURE(scenario);
        REQUIRE_FALSE(selection.publish(registry.rows, generation, &registry, Registry::swap, Registry::enable, error));
        REQUIRE(registry.mutations == 0);
        REQUIRE_FALSE(error.empty());
        REQUIRE(selection.dirty());
    }
}

TEST_CASE("Duplicate or empty installed IDs reject refresh and publication without losing pending edits",
          "[modmgr][selection][transaction]")
{
    Registry registry;
    Selection selection;
    std::string error;
    REQUIRE(selection.refresh(registry.rows, 10, error));
    REQUIRE(selection.setEnabled("b", true));
    auto duplicates = registry.rows;
    duplicates.back().id = duplicates.front().id;
    REQUIRE_FALSE(selection.refresh(duplicates, 11, error));
    REQUIRE(selection.enabled("b"));
    REQUIRE_FALSE(selection.publish(duplicates, 10, &registry, Registry::swap, Registry::enable, error));
    duplicates.back().id.clear();
    REQUIRE_FALSE(selection.publish(duplicates, 10, &registry, Registry::swap, Registry::enable, error));
    REQUIRE(registry.mutations == 0);
    REQUIRE(selection.publish(registry.rows, 10, &registry, Registry::swap, Registry::enable, error));
}

TEST_CASE("Installed selection keeps session-owned rows locked and refuses invalid enables",
          "[modmgr][selection]")
{
    Registry registry;
    registry.rows[1].locked = true;
    registry.rows[2].valid = false;
    registry.rows[2].enabled = false;
    Selection selection;
    std::string error;
    REQUIRE(selection.refresh(registry.rows, 1, error));
    REQUIRE_FALSE(selection.setEnabled("b", true));
    REQUIRE_FALSE(selection.move(0, 1));
    REQUIRE_FALSE(selection.move(1, 2));
    REQUIRE_FALSE(selection.setEnabled("c", true));
    REQUIRE_FALSE(selection.setEnabled("missing", true));
    REQUIRE_FALSE(selection.move(-1, 2));
    REQUIRE_FALSE(selection.move(0, 3));
    REQUIRE_FALSE(selection.dirty());
}

TEST_CASE("Explicit Discard stays clean when a replacement registry has duplicate IDs",
          "[modmgr][selection][transaction]")
{
    Registry registry;
    Selection selection;
    std::string error;
    REQUIRE(selection.refresh(registry.rows, 10, error));
    REQUIRE(selection.setEnabled("b", true));
    REQUIRE(selection.move(0, 2));
    selection.discard();
    auto duplicates = registry.rows;
    duplicates.back().id = duplicates.front().id;
    REQUIRE_FALSE(selection.refresh(duplicates, 11, error));
    REQUIRE_FALSE(selection.dirty());
    REQUIRE_FALSE(selection.enabled("b"));
    REQUIRE(selection.entries()[0].id == "a");
    REQUIRE_FALSE(error.empty());
    REQUIRE(registry.mutations == 0);
}

TEST_CASE("Installed selection does not impose the old UI entry limit",
          "[modmgr][selection]")
{
    std::vector<Entry> rows;
    for (int i = 0; i < 1500; ++i) rows.push_back({"id" + std::to_string(i), "", "1", false, true, false});
    Selection selection;
    std::string error;
    REQUIRE(selection.refresh(rows, 1, error));
    REQUIRE(selection.entries().size() == rows.size());
    REQUIRE(selection.setEnabled("id1499", true));
    REQUIRE(selection.enabled("id1499"));
}

TEST_CASE("Mod Manager UI stages edits and gives child popups priority",
          "[modmgr][selection][static]")
{
    const auto ui = source("port/fast3d/pdgui_menu_modmgr.cpp");
    const auto installed = section(ui, "static void renderInstalledModsTab", "static void renderInstalledModPopups");
    REQUIRE(installed.find("modmgrSetEnabled(") == std::string::npos);
    REQUIRE(installed.find("modmgrSaveConfig(") == std::string::npos);
    REQUIRE(installed.find("modmgrSwapOrder(") == std::string::npos);
    REQUIRE(installed.find("s_InstalledSelection.setEnabled(") != std::string::npos);
    REQUIRE(installed.find("s_InstalledSelection.move(") != std::string::npos);
    REQUIRE(installed.find("pdguiMenuSecondaryPressed() || pdguiMenuDeletePressed()") != std::string::npos);
    REQUIRE(installed.find("!row.locked && !hasPendingSelection()") != std::string::npos);
    REQUIRE(ui.find("if (canNavigate && !idleBack && pdguiMenuTabPrevPressed())") != std::string::npos);
    REQUIRE(ui.find("ImGui::BeginPopupModal(\"Applying Changes\"") != std::string::npos);
    const auto attempt = section(ui, "static bool runPreparedApplyAttempt", "static void renderModManagerBody");
    REQUIRE(attempt.find("modmgrPrepareApplyPlan(") < attempt.find("s_InstalledSelection.publish("));
    REQUIRE(attempt.find("s_InstalledSelection.publish(") < attempt.find("modmgrApplyPreparedChanges("));
    REQUIRE(attempt.find("s_InstalledSelection.captureAfterAttempt(") < attempt.find("modmgrApplyPreparedChanges("));
    REQUIRE(attempt.find("applyPendingSelectionToCatalog") == std::string::npos);
    REQUIRE(attempt.find("s_ApplyIncomplete = true;") != std::string::npos);
    REQUIRE(ui.find("return s_ApplyIncomplete || countPending()") != std::string::npos);
    REQUIRE(ui.find("ImGui::Button(\"Retry\")") != std::string::npos);
    REQUIRE(ui.find("MODMGR_MAX_ENTRIES") == std::string::npos);
    REQUIRE(ui.find("MODMGR_MAX_CATEGORIES") == std::string::npos);
    REQUIRE(ui.find("MODMGR_MAX_ERRORS") == std::string::npos);
    REQUIRE(ui.find("std::vector<ModMgrEntry> s_Entries") != std::string::npos);
    REQUIRE(ui.find("std::vector<ModMgrError> s_Errors") != std::string::npos);
}

TEST_CASE("Mod Manager Back beats parent actions and modal transitions establish visible focus",
          "[modmgr][selection][navigation][static]")
{
    const auto ui = source("port/fast3d/pdgui_menu_modmgr.cpp");
    const auto body = section(ui, "static void renderModManagerBody", "static void renderModManager(s32 winW");
    const auto capture = body.find("const bool idleBack = canNavigate && pdguiMenuCancelPressed();");
    const auto disable = body.find("ImGui::BeginDisabled(idleBack || s_ApplyFlowState > 0 || s_LeaveRequested);");
    const auto leave = body.find("if (closeButton || idleBack)");
    REQUIRE(capture != std::string::npos);
    REQUIRE(capture < disable);
    REQUIRE(disable < body.find("ImGui::Button(applyLabel,"));
    REQUIRE(body.find("ImGui::EndDisabled();", body.find("const bool closeButton")) < leave);
    REQUIRE(leave < body.find("requestSelectionLeave()"));
    REQUIRE(body.find("if (idleBack) pdguiNavSuppressActivation();") != std::string::npos);

    const auto large = section(ui, "static void renderInstalledModPopups", "static void renderModDetails");
    const auto unsaved = section(body, "if (s_LeaveRequested &&", "if (s_ApplyFlowState == 1 &&");
    for (const auto *popup : {&large, &unsaved}) {
        REQUIRE(popup->find("pdguiNavSuppressActivation();") < popup->find("ImGui::OpenPopup("));
        REQUIRE(popup->find("ImGui::SetKeyboardFocusHere();\n            ImGui::SetNavCursorVisible(true);") != std::string::npos);
    }
    const auto applying = body.substr(body.find("if (s_ApplyFlowState == 1 &&"));
    REQUIRE(applying.find("pdguiNavSuppressActivation();") < applying.find("ImGui::OpenPopup("));
    REQUIRE(applying.find("s_ApplyResultNeedsFocus = true;") < applying.find("if (s_ApplyResultNeedsFocus)"));
    const auto focus = section(applying, "if (s_ApplyResultNeedsFocus)", "if (ImGui::Button(\"OK\")");
    REQUIRE(focus.find("pdguiNavSuppressActivation();") != std::string::npos);
    REQUIRE(focus.find("ImGui::SetKeyboardFocusHere();") != std::string::npos);
    REQUIRE(focus.find("ImGui::SetNavCursorVisible(true);") != std::string::npos);

    const auto footerDiscard = section(body, "if (ImGui::Button(\"Discard\",", "const bool closeButton");
    REQUIRE(footerDiscard.find("s_InstalledSelection.discard();") < footerDiscard.find("refreshSnapshot(true);"));
    const auto leaveDiscard = section(unsaved, "if (ImGui::Button(\"Discard\"))", "if (ImGui::Button(\"Apply\"))");
    REQUIRE(leaveDiscard.find("s_InstalledSelection.discard();") < leaveDiscard.find("refreshSnapshot(true);"));
}

TEST_CASE("Every Hub departure uses the staged selection guard",
          "[modmgr][selection][hub][static]")
{
    const auto hub = source("port/fast3d/pdgui_menu_moddinghub.cpp");
    const auto close = section(hub, "static void moddingHubClose(const char *reason)", "static void moddingHubCloseFromUi");
    REQUIRE(close.find("if (!moddingHubCanLeave(-2)) return;") < close.find("s_Visible = false;"));
    const auto tool = section(hub, "static void moddingHubRequestTool", "static void renderModdingHub");
    REQUIRE(tool.find("moddingHubCanLeave(tool)") != std::string::npos);
    const auto external = section(hub, "void pdguiModdingHubShowTool", "void pdguiModdingHubHide");
    REQUIRE(external.find("!moddingHubCanLeave(tool)") != std::string::npos);
    REQUIRE(hub.find("moddingHubClose(\"explicit-hide\")") != std::string::npos);
    REQUIRE(hub.find("moddingHubClose(\"outside-click\")") != std::string::npos);
    REQUIRE(hub.find("pdguiModManagerConsumeLeaveResult()") != std::string::npos);
    REQUIRE(hub.find("s_ActiveTool != 0 && s_ActiveTool != 5") != std::string::npos);
    REQUIRE(hub.find("s_ActiveTool == 0 && pdguiModManagerOwnsNavigation()") != std::string::npos);
}

TEST_CASE("Installed selection retries its own partial publication without losing desired choices",
          "[modmgr][selection][retry]")
{
    Registry registry; Selection selection; std::string error;
    REQUIRE(selection.refresh(registry.rows, 1, error));
    REQUIRE(selection.setEnabled("a", false));
    REQUIRE(selection.setEnabled("b", true));
    REQUIRE(selection.move(2, 0));
    const auto desired = selection.entries();
    const auto refuse = [](void *, int, bool) {};
    REQUIRE(selection.publish(registry.rows, 1, &registry, Registry::swap, refuse, error));
    REQUIRE(selection.captureAfterAttempt(registry.rows, 2, error));
    REQUIRE(selection.entries()[0].id == desired[0].id);
    REQUIRE(selection.enabled("b"));
    REQUIRE_FALSE(selection.enabled("a"));
    REQUIRE(selection.dirty());
    REQUIRE(selection.publish(registry.rows, 2, &registry, Registry::swap, Registry::enable, error));
    REQUIRE(selection.captureAfterAttempt(registry.rows, 3, error));
    REQUIRE_FALSE(selection.dirty());
    // Matching desired registry state is not a disk/runtime Apply-success claim.
    REQUIRE(selection.entries()[0].id == "c");
}

TEST_CASE("Installed selection rejects unrelated retry identity and session changes without replacing its baseline",
          "[modmgr][selection][retry]")
{
    Registry registry; registry.rows[1].locked = true;
    Selection selection; std::string error;
    REQUIRE(selection.refresh(registry.rows, 1, error));
    REQUIRE(selection.setEnabled("a", false));
    for (int change = 0; change < 5; ++change) {
        auto other = registry.rows;
        if (change == 0) other[0].path = "replacement";
        if (change == 1) other.pop_back();
        if (change == 2) other[2].id = other[0].id;
        if (change == 3) std::swap(other[0], other[1]);
        if (change == 4) other[1].enabled = true;
        REQUIRE_FALSE(selection.captureAfterAttempt(other, 2, error));
        REQUIRE(selection.matches(registry.rows, 1, error));
        REQUIRE_FALSE(selection.enabled("a"));
        REQUIRE(selection.dirty());
    }
}
