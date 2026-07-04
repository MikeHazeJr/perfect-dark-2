/*
 * tests/test_pure_mirror_sync.cpp -- c065 (partial): enforce that the hand-synced
 * *_pure.* mirrors' @SYNC references point at LIVE production files.
 *
 * The pure mirrors (tests/*_pure.{c,h}) hand-copy algorithm code out of production
 * TUs so the logic is unit-testable without linking globals. Each carries @SYNC
 * comments naming the production file it mirrors. That drift audit was purely
 * MANUAL -- "drift is not enforced by CI" (c065). This test enforces the FILE half
 * of the invariant: every full repo-path named in a @SYNC comment must exist, so a
 * renamed / moved / deleted production file fails HERE (prompting a mirror re-sync
 * or the @SYNC's correction) instead of leaving a silently-dead mirror.
 *
 * Deliberately NOT checked (they drift by design and would false-positive):
 *   - line-number hints (`file.c:88`, `lines 647-672`),
 *   - prose / symbol notes in parens (`(compaction step)`, `(inputCtxPush)`),
 *   - `context/` doc references (audits/designs age out to _old/ via retention.md).
 *
 * The full compile-boundary refactor that would ELIMINATE the mirrors entirely
 * (factor each algorithm into a globals-free TU linked by both production and test)
 * remains the larger c065 deliverable; this is the safe enforcement slice.
 */
#include "catch.hpp"

#include <cctype>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

bool fileExists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

std::string slurp(const std::string &p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> listPureMirrors() {
    std::vector<std::string> out;
    DIR *d = opendir("tests");
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        const std::string n = e->d_name;
        if (n.find("_pure.") == std::string::npos) continue;
        auto ends = [&](const char *suf) {
            const std::string s(suf);
            return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
        };
        if (ends("_pure.c") || ends("_pure.h") || ends("_pure.cpp")) {
            out.push_back("tests/" + n);
        }
    }
    closedir(d);
    return out;
}

/* Pull repo-relative CODE file paths out of a @SYNC line. A path token starts with
 * a known source root (port/ / src/ / include/), runs over path characters, and
 * ends in a C/C++ extension. Any `:line` suffix stops the token naturally (`:` is
 * not a path char). context/ docs are intentionally excluded. */
std::vector<std::string> extractCodePaths(const std::string &line) {
    static const char *roots[] = {"port/", "src/", "include/"};
    std::vector<std::string> paths;
    size_t i = 0;
    while (i < line.size()) {
        size_t best = std::string::npos;
        for (const char *r : roots) {
            const size_t p = line.find(r, i);
            if (p != std::string::npos && (best == std::string::npos || p < best)) best = p;
        }
        if (best == std::string::npos) break;
        size_t j = best;
        while (j < line.size()) {
            const char c = line[j];
            if (std::isalnum((unsigned char)c) || c == '/' || c == '.' || c == '_' || c == '-') j++;
            else break;
        }
        const std::string tok = line.substr(best, j - best);
        auto ends = [&](const char *suf) {
            const std::string s(suf);
            return tok.size() >= s.size() && tok.compare(tok.size() - s.size(), s.size(), s) == 0;
        };
        if (ends(".c") || ends(".cpp") || ends(".h")) {
            paths.push_back(tok);
        }
        i = j + 1;
    }
    return paths;
}

} /* anon */

TEST_CASE("c065: pure-mirror @SYNC file references are live", "[sync][static][c065]") {
    const auto mirrors = listPureMirrors();
    REQUIRE(mirrors.size() >= 6); /* sanity: the mirror set was found (CWD = repo root) */

    int checked = 0;
    for (const auto &m : mirrors) {
        const std::string content = slurp(m);
        REQUIRE_FALSE(content.empty());
        std::istringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.find("@SYNC") == std::string::npos) continue;
            for (const auto &path : extractCodePaths(line)) {
                INFO("mirror " << m << " has a @SYNC reference to a missing production file: " << path);
                REQUIRE(fileExists(path));
                ++checked;
            }
        }
    }
    INFO("validated " << checked << " @SYNC production-file references across "
         << mirrors.size() << " mirrors");
    REQUIRE(checked >= 8); /* sanity: references were actually validated, not skipped */
}
