#include "catch.hpp"
#include "modmgr_save_status.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Save {
    std::string active = "Joanna";
    int fail = -1, change = -1;
    std::vector<int> calls;
    static const char *agent(void *data) { return static_cast<Save *>(data)->active.c_str(); }
    static int write(modmgr_save_phase phase, const char *expected,
        char *error, size_t capacity, void *data) {
        auto &self = *static_cast<Save *>(data);
        REQUIRE(self.active == expected);
        self.calls.push_back(phase);
        if (self.change == phase) self.active = "Elvis";
        if (self.fail == phase) {
            std::snprintf(error, capacity, "Injected destination failure");
            return 0;
        }
        return 1;
    }
    modmgr_save_callbacks_t callbacks() { return {agent, write, this}; }
};
}
TEST_CASE("Mod config save confirms all required destinations", "[modmgr][save-status]") {
    Save save; auto callbacks = save.callbacks(); modmgr_save_result_t result{};
    REQUIRE(modmgrRunConfigSave(nullptr, &callbacks, &result) == 1);
    REQUIRE(result.phase == MODMGR_SAVE_COMPLETE);
    REQUIRE(result.saved == 7);
    REQUIRE(std::string(result.agent) == "Joanna");
    REQUIRE(save.calls == std::vector<int>{0,1,2,3});
}
TEST_CASE("Mod config save treats absent Agent as inapplicable", "[modmgr][save-status]") {
    Save save; save.active.clear(); auto callbacks = save.callbacks(); modmgr_save_result_t result{};
    REQUIRE(modmgrRunConfigSave(nullptr, &callbacks, &result) == 1);
    REQUIRE(result.saved == 3);
    REQUIRE(save.calls == std::vector<int>{0,1,2});
}
TEST_CASE("Mod config save stops at each failure with truthful earlier destinations", "[modmgr][save-status]") {
    for (int failure = 0; failure < 4; ++failure) {
        INFO(failure);
        Save save; save.fail = failure; auto callbacks = save.callbacks(); modmgr_save_result_t result{};
        REQUIRE(modmgrRunConfigSave(nullptr, &callbacks, &result) == 0);
        REQUIRE(result.phase == failure);
        REQUIRE(result.saved == (failure > 0 ? (1u << (failure - 1)) - 1 : 0));
        REQUIRE(save.calls.size() == static_cast<size_t>(failure + 1));
        REQUIRE(std::string(result.error) == "Injected destination failure");
    }
}
TEST_CASE("Mod config save retry retains owned Agent identity and retries failed destinations", "[modmgr][save-status]") {
    Save save; save.fail = MODMGR_SAVE_AGENT; auto callbacks = save.callbacks(); modmgr_save_result_t result{};
    REQUIRE(modmgrRunConfigSave(nullptr, &callbacks, &result) == 0);
    save.fail = -1; save.calls.clear();
    REQUIRE(modmgrRunConfigSave(result.agent, &callbacks, &result) == 1);
    REQUIRE(result.saved == 7);
    REQUIRE(save.calls == std::vector<int>{0,1,2,3});
    save.active = "Elvis"; save.calls.clear();
    REQUIRE(modmgrRunConfigSave(result.agent, &callbacks, &result) == 0);
    REQUIRE(save.calls.empty());
    REQUIRE(result.saved == 0);
    REQUIRE(std::string(result.agent) == "Joanna");
}
TEST_CASE("Mod config save detects identity change between destination writes", "[modmgr][save-status]") {
    Save save; save.change = MODMGR_SAVE_ENABLED_JSON; auto callbacks = save.callbacks(); modmgr_save_result_t result{};
    REQUIRE(modmgrRunConfigSave(nullptr, &callbacks, &result) == 0);
    REQUIRE(result.phase == MODMGR_SAVE_MACHINE);
    REQUIRE(result.saved == MODMGR_SAVED_ENABLED_JSON);
    REQUIRE(save.calls == std::vector<int>{0,1});
    Save finalSave; finalSave.change = MODMGR_SAVE_AGENT;
    callbacks = finalSave.callbacks();
    REQUIRE(modmgrRunConfigSave(nullptr, &callbacks, &result) == 0);
    REQUIRE(result.saved == 7);
    REQUIRE(result.phase == MODMGR_SAVE_AGENT);
}
TEST_CASE("Mod config save rejects invalid adapters and identity before writes", "[modmgr][save-status]") {
    Save save; auto callbacks = save.callbacks(); modmgr_save_result_t result{};
    REQUIRE(modmgrRunConfigSave(nullptr, nullptr, &result) == 0);
    REQUIRE(modmgrRunConfigSave(nullptr, &callbacks, nullptr) == 0);
    std::string huge(AGENT_PROFILE_NAME_MAX, 'x');
    REQUIRE(modmgrRunConfigSave(huge.c_str(), &callbacks, &result) == 0);
    save.active = huge;
    REQUIRE(modmgrRunConfigSave(nullptr, &callbacks, &result) == 0);
    REQUIRE(save.calls.empty());
}
