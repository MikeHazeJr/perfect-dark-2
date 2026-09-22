#include "modmgr_selection.h"
#include <algorithm>
#include <set>
#include <utility>

namespace modmgr_selection {
namespace {
bool same(const Entry &a, const Entry &b)
{
    return a.id == b.id && a.path == b.path && a.version == b.version
        && a.enabled == b.enabled && a.valid == b.valid && a.locked == b.locked;
}
bool unique(const std::vector<Entry> &entries, std::string &error)
{
    std::set<std::string> seen;
    for (const auto &entry : entries) {
        if (entry.id.empty() || !seen.insert(entry.id).second) {
            error = "Installed mods contain an empty or duplicate ID. Changes were not applied.";
            return false;
        }
    }
    return true;
}
}

bool Selection::refresh(const std::vector<Entry> &live, uint32_t generation, std::string &error)
{
    if (!unique(live, error)) return false;
    baseline_ = live;
    pending_ = live;
    generation_ = generation;
    error.clear();
    return true;
}

bool Selection::matches(const std::vector<Entry> &live, uint32_t generation, std::string &error) const
{
    if (!unique(live, error)) return false;
    if (generation != generation_ || live.size() != baseline_.size()
        || !std::equal(live.begin(), live.end(), baseline_.begin(), same)) {
        error = "Installed mods changed elsewhere. Discard the pending selection and try again.";
        return false;
    }
    error.clear();
    return true;
}

int Selection::pendingCount() const
{
    int count = 0;
    for (size_t i = 0; i < pending_.size(); ++i)
        if (!same(pending_[i], baseline_[i])) ++count;
    return count;
}

bool Selection::setEnabled(const std::string &id, bool enabled)
{
    for (auto &entry : pending_) {
        if (entry.id != id) continue;
        if (entry.locked || (enabled && !entry.valid)) return false;
        entry.enabled = enabled;
        return true;
    }
    return false;
}

bool Selection::move(int from, int to)
{
    if (from < 0 || to < 0 || from >= static_cast<int>(pending_.size())
        || to >= static_cast<int>(pending_.size()) || pending_[from].locked || pending_[to].locked)
        return false;
    std::swap(pending_[from], pending_[to]);
    return true;
}

bool Selection::enabled(const std::string &id) const
{
    for (const auto &entry : pending_) if (entry.id == id) return entry.enabled;
    return false;
}

bool Selection::publish(const std::vector<Entry> &live, uint32_t generation,
                        void *userdata, Swap swap, Enable enable, std::string &error) const
{
    if (!matches(live, generation, error)) return false;
    if (!swap || !enable) {
        error = "The installed-mod publication callbacks are unavailable.";
        return false;
    }
    std::vector<std::pair<int, int>> swaps;
    std::vector<std::pair<int, bool>> enables;
    auto working = live;
    for (size_t i = 0; i < pending_.size(); ++i) {
        size_t match = i;
        while (match < working.size() && working[match].id != pending_[i].id) ++match;
        if (match == working.size()) {
            error = "A selected mod disappeared. Changes were not applied.";
            return false;
        }
        if (match != i) {
            swaps.emplace_back(static_cast<int>(i), static_cast<int>(match));
            std::swap(working[i], working[match]);
        }
        if (working[i].enabled != pending_[i].enabled)
            enables.emplace_back(static_cast<int>(i), pending_[i].enabled);
    }
    for (const auto &command : swaps) swap(userdata, command.first, command.second);
    for (const auto &command : enables) enable(userdata, command.first, command.second);
    error.clear();
    return true;
}

/* Call synchronously after this UI's own publication/rebuild attempt only.
 * This preserves pending intent and records the actual post-attempt baseline.
 * It does not acknowledge successful Apply or permit silent departure.
 */
bool Selection::captureAfterAttempt(const std::vector<Entry> &live,
                                    uint32_t generation, std::string &error)
{
    if (!unique(live, error)) return false;
    if (live.size() != baseline_.size()) {
        error = "Installed mod identities changed during Apply; pending choices were retained.";
        return false;
    }
    for (size_t i = 0; i < baseline_.size(); ++i) {
        const auto &before = baseline_[i];
        auto found = std::find_if(live.begin(), live.end(),
            [&before](const Entry &entry) { return entry.id == before.id; });
        if (found == live.end() || found->path != before.path ||
                found->version != before.version || found->valid != before.valid ||
                found->locked != before.locked ||
                (before.locked && (live[i].id != before.id || found->enabled != before.enabled))) {
            error = "An installed mod changed identity or ownership; pending choices were retained.";
            return false;
        }
    }
    baseline_ = live;
    generation_ = generation;
    error.clear();
    return true;
}

}
