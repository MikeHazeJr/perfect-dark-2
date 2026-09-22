#ifndef PD_MODMGR_SELECTION_H
#define PD_MODMGR_SELECTION_H
#include <cstdint>
#include <string>
#include <vector>

namespace modmgr_selection {
struct Entry {
    std::string id;
    std::string path;
    std::string version;
    bool enabled = false;
    bool valid = false;
    bool locked = false; // session-owned selection/order is not editable
};

class Selection {
public:
    using Swap = void (*)(void *, int, int);
    using Enable = void (*)(void *, int, bool);
    bool refresh(const std::vector<Entry> &, uint32_t generation, std::string &error);
    bool matches(const std::vector<Entry> &, uint32_t generation, std::string &error) const;
    bool setEnabled(const std::string &id, bool enabled);
    bool move(int from, int to);
    bool dirty() const { return pendingCount() != 0; }
    int pendingCount() const;
    const std::vector<Entry> &entries() const { return pending_; }
    bool enabled(const std::string &id) const;
    void discard() { pending_ = baseline_; }
    /* Capture only immediately after this UI's own attempt. Preserve desired
     * intent, reject identity/session changes, and do not acknowledge success. */
    bool captureAfterAttempt(const std::vector<Entry> &, uint32_t generation, std::string &error);
    /* Validate the entire baseline and build the complete command plan before
     * calling any mutation. Publication deliberately has no save/runtime
     * success claim: those existing production APIs currently return void. */
    bool publish(const std::vector<Entry> &live, uint32_t generation,
                 void *userdata, Swap swap, Enable enable, std::string &error) const;
private:
    std::vector<Entry> baseline_;
    std::vector<Entry> pending_;
    uint32_t generation_ = 0;
};
}
#endif
