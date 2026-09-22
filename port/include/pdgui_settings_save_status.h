#pragma once

#include <string>
#include <vector>

/* Settings-local retry policy. Callbacks are synchronous and are never retained.
 * Both adapters return true only on success, despite their C APIs having
 * opposite success conventions. Runtime preference values stay with their
 * existing owners; this helper never caches or replays a preference snapshot. */
struct PdguiSettingsSaveCallbacks {
    bool (*machine)(void *user);
    bool (*agent)(const char *expectedAgent, void *user);
    void *user = nullptr;
};

enum class PdguiSettingsLeaveChoice { Stay, Retry, LeaveWithoutSaving };

class PdguiSettingsSaveStatus {
public:
    void requestMachineSave() { machinePending_ = true; }
    void poll(const char *activeAgent, const PdguiSettingsSaveCallbacks &callbacks);
    void retry(const char *activeAgent, const PdguiSettingsSaveCallbacks &callbacks);
    bool requestDeparture();
    bool chooseDeparture(PdguiSettingsLeaveChoice choice, const char *activeAgent,
                         const PdguiSettingsSaveCallbacks &callbacks);
    void cancelDeparture() { leavePending_ = false; }

    bool machinePending() const { return machinePending_; }
    bool machineFailed() const { return machineFailed_; }
    bool agentFailed() const { return agentFailed_; }
    bool hasUnsaved() const { return machinePending_ || agentFailed_; }
    bool leavePending() const { return leavePending_; }
    const std::string &agentName() const { return agent_; }
    const std::vector<std::string> &previousAgentFailures() const { return previousAgentFailures_; }
    void dismissPreviousAgentFailures() { previousAgentFailures_.clear(); }

private:
    void observeAgent(const char *activeAgent);
    void saveMachine(const PdguiSettingsSaveCallbacks &callbacks);
    void saveAgent(const PdguiSettingsSaveCallbacks &callbacks);
    bool machinePending_ = false;
    bool machineFailed_ = false;
    bool agentFailed_ = false;
    bool leavePending_ = false;
    std::string agent_;
    std::vector<std::string> previousAgentFailures_;
};
