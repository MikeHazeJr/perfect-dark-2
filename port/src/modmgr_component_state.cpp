#include "modmgr_component_state.h"
#include "save_atomic.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>

struct modmgr_component_state {
    std::vector<std::string> ids;
};

namespace {
int reject(char *error, size_t capacity, const char *message)
{
    if (error && capacity) std::snprintf(error, capacity, "%s", message);
    return 0;
}

bool representable(const std::string &id)
{
    return !id.empty() && id.size() < CATALOG_ID_LEN && id[0] != '#' &&
        id.find_first_of(std::string("\0\r\n", 3)) == std::string::npos;
}

bool parseDocument(const std::string &document, modmgr_component_state &state)
{
    if (document.find('\0') != std::string::npos) return false;
    std::set<std::string> seen;
    size_t start = document.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    while (start < document.size()) {
        size_t end = document.find('\n', start);
        if (end == std::string::npos) end = document.size();
        size_t length = end - start;
        if (length && document[start + length - 1] == '\r') --length;
        std::string line = document.substr(start, length);
        if (!line.empty() && line[0] != '#') {
            if (!representable(line)) return false;
            if (seen.insert(line).second) state.ids.push_back(std::move(line));
        }
        if (end == document.size()) break;
        start = end + 1;
    }
    return true;
}
}

extern "C" int modmgrReadComponentState(const char *path,
    modmgr_component_state_t **state, char *error, size_t error_cap)
{
    if (error && error_cap) error[0] = '\0';
    if (state) *state = nullptr;
    if (!state || !path || !path[0]) {
        reject(error, error_cap, "Missing component-state path or output.");
        return -1;
    }
    struct stat info;
    if (stat(path, &info) != 0) {
        if (errno == ENOENT) return 0;
        reject(error, error_cap, "Could not inspect component state.");
        return -1;
    }
    if (!S_ISREG(info.st_mode)) {
        reject(error, error_cap, "Component state is not a regular file.");
        return -1;
    }
    try {
        std::unique_ptr<FILE, int(*)(FILE*)> file(std::fopen(path, "rb"), std::fclose);
        if (!file) {
            reject(error, error_cap, "Could not open component state.");
            return -1;
        }
        std::string document;
        char buffer[4096];
        size_t bytes;
        while ((bytes = std::fread(buffer, 1, sizeof(buffer), file.get())) != 0)
            document.append(buffer, bytes);
        if (std::ferror(file.get())) {
            reject(error, error_cap, "Could not read complete component state.");
            return -1;
        }
        if (std::fclose(file.release()) != 0) {
            reject(error, error_cap, "Could not close component state.");
            return -1;
        }
        auto candidate = std::make_unique<modmgr_component_state>();
        if (!parseDocument(document, *candidate)) {
            reject(error, error_cap, "Component state contains an invalid ID or null byte.");
            return -1;
        }
        *state = candidate.release();
        return 1;
    } catch (...) {
        reject(error, error_cap, "Could not prepare component state.");
        return -1;
    }
}

extern "C" size_t modmgrComponentStateCount(const modmgr_component_state_t *state)
{
    return state ? state->ids.size() : 0;
}
extern "C" const char *modmgrComponentStateId(const modmgr_component_state_t *state, size_t index)
{
    return state && index < state->ids.size() ? state->ids[index].c_str() : nullptr;
}
extern "C" void modmgrFreeComponentState(modmgr_component_state_t *state)
{
    delete state;
}

extern "C" int modmgrSaveComponentStateFile(const char *path,
    const modmgr_component_choice_t *choices, size_t count,
    char *error, size_t error_cap)
{
    if (error && error_cap) error[0] = '\0';
    if (!path || !path[0] || (count && !choices))
        return reject(error, error_cap, "Missing component-state destination or choices.");
    try {
        modmgr_component_state_t *loaded = nullptr;
        const int read = modmgrReadComponentState(path, &loaded, error, error_cap);
        std::unique_ptr<modmgr_component_state_t> previous(loaded);
        if (read < 0) return 0;
        std::set<std::string> ids;
        if (previous) ids.insert(previous->ids.begin(), previous->ids.end());
        std::set<std::string> current;
        for (size_t i = 0; i < count; ++i) {
            if (!std::memchr(choices[i].id, '\0', sizeof(choices[i].id)) ||
                    !choices[i].id[0] || !current.insert(choices[i].id).second)
                return reject(error, error_cap, "Component choices contain an invalid or duplicate ID.");
            if (!choices[i].persistent) continue;
            const std::string id(choices[i].id);
            if (!representable(id))
                return reject(error, error_cap, "A component ID cannot be represented in the state file.");
            if (choices[i].enabled) ids.erase(id);
            else ids.insert(id);
        }
        std::string document = "# mods/.modstate -- disabled component IDs\n";
        for (const auto &id : ids) {
            document += id;
            document += '\n';
        }
        save_atomic_file_t transaction{};
        if (saveAtomicBegin(&transaction, path) != 0)
            return reject(error, error_cap, "Could not open an atomic component-state candidate.");
        FILE *stream = saveAtomicStream(&transaction);
        if (!stream || std::fwrite(document.data(), 1, document.size(), stream) != document.size()) {
            saveAtomicAbort(&transaction);
            return reject(error, error_cap, "Could not write complete component state.");
        }
        if (saveAtomicCommit(&transaction) != 0) {
            saveAtomicAbort(&transaction);
            return reject(error, error_cap, "Could not commit component state.");
        }
        return 1;
    } catch (...) {
        return reject(error, error_cap, "Could not prepare component-state choices.");
    }
}
