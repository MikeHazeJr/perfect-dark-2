#include "pdgui_settings_save_status.h"

#include <algorithm>

void PdguiSettingsSaveStatus::observeAgent(const char *activeAgent)
{
    const std::string next = activeAgent ? activeAgent : "";
    if (next == agent_) return;
    if (agentFailed_ && std::find(previousAgentFailures_.begin(),
            previousAgentFailures_.end(), agent_) == previousAgentFailures_.end()) {
        previousAgentFailures_.push_back(agent_);
    }
    /* The new identity's runtime document belongs to prefs_agent. Never retry
     * the old identity's failed state against it or forget that loss silently. */
    agent_ = next;
    agentFailed_ = false;
    leavePending_ = false;
}

void PdguiSettingsSaveStatus::saveMachine(const PdguiSettingsSaveCallbacks &callbacks)
{
    machineFailed_ = !callbacks.machine || !callbacks.machine(callbacks.user);
    machinePending_ = machineFailed_;
}

void PdguiSettingsSaveStatus::saveAgent(const PdguiSettingsSaveCallbacks &callbacks)
{
    agentFailed_ = !callbacks.agent || !callbacks.agent(agent_.c_str(), callbacks.user);
}

void PdguiSettingsSaveStatus::poll(const char *activeAgent,
                                 const PdguiSettingsSaveCallbacks &callbacks)
{
    observeAgent(activeAgent);
    if (leavePending_) return;
    if (machinePending_ && !machineFailed_) saveMachine(callbacks);
    /* prefsAgentSave already skips unchanged snapshots. A failure must stop
     * this frame-driven call until the user explicitly retries it. */
    if (!agent_.empty() && !agentFailed_) saveAgent(callbacks);
}

void PdguiSettingsSaveStatus::retry(const char *activeAgent,
                                  const PdguiSettingsSaveCallbacks &callbacks)
{
    observeAgent(activeAgent);
    if (machinePending_) saveMachine(callbacks);
    if (!agent_.empty() && agentFailed_) saveAgent(callbacks);
}

bool PdguiSettingsSaveStatus::requestDeparture()
{
    leavePending_ = hasUnsaved();
    return !leavePending_;
}

bool PdguiSettingsSaveStatus::chooseDeparture(PdguiSettingsLeaveChoice choice,
                                            const char *activeAgent,
                                            const PdguiSettingsSaveCallbacks &callbacks)
{
    if (!leavePending_) return false;
    if (choice == PdguiSettingsLeaveChoice::Stay) {
        leavePending_ = false;
        return false;
    }
    if (choice == PdguiSettingsLeaveChoice::LeaveWithoutSaving) {
        leavePending_ = false;
        return true; // failures remain latched across tabs and reentry
    }
    const std::string requestedAgent = agent_;
    retry(activeAgent, callbacks);
    if (agent_ != requestedAgent) return false; // identity changed under the dialog
    if (hasUnsaved()) {
        leavePending_ = true;
        return false;
    }
    leavePending_ = false;
    return true;
}
