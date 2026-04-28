/*
 * tests/test_manifest.cpp -- Match manifest container, hash, diff, and
 * serialize/deserialize roundtrip tests.
 *
 * Code under test: tests/manifest_pure.c (a copy of the pure subset of
 * port/src/net/netmanifest.c — see manifest_pure.c header for the
 * drift audit instructions).
 *
 * The manifest is the asset list every match start broadcasts to all
 * clients (SVC_MATCH_MANIFEST), and every SP mission diffs against the
 * previous state to drive load/unload (manifestSPTransition). A bug in
 * the container or hash means clients can desync. A bug in serialize/
 * deserialize means the wire and the in-memory state disagree. A bug
 * in diff means assets get loaded twice or unloaded prematurely.
 *
 * Tests:
 *   - Clear/Free safety on zero-init structs and after use
 *   - Add: dedup, capacity growth, type/slot preservation
 *   - AddMod: SHA-256 storage, COMPONENT type
 *   - ComputeHash: deterministic, order-sensitive, drift-detectable
 *   - Serialize/Deserialize: full roundtrip via raw byte buffer
 *   - Deserialize: malformed bytes (truncated, oversized count) error gracefully
 *   - Deserialize: malformed packets roll back entries appended by that packet
 *   - Deserialize: SEC-5 zero-SHA256 COMPONENT entries are dropped
 *   - Diff: empty/full pair classifications, to_load/to_unload/to_keep correctness
 */

#include "catch.hpp"
#include "manifest_pure.h"

#include <cstring>
#include <vector>
#include <string>

namespace {
/* Helper: build a fresh zero-init manifest. */
pdtest_manifest_t make_manifest() {
    pdtest_manifest_t m{};
    return m;
}

/* Helper: build a 32-byte SHA-256-shaped buffer. */
std::vector<unsigned char> fake_sha256(unsigned char seed) {
    std::vector<unsigned char> v(32, 0);
    for (size_t i = 0; i < v.size(); i++) v[i] = (unsigned char)(seed + i);
    return v;
}

void append_u16le(std::vector<unsigned char>& v, unsigned short n) {
    v.push_back((unsigned char)(n & 0xff));
    v.push_back((unsigned char)((n >> 8) & 0xff));
}

void append_str(std::vector<unsigned char>& v, const char *s) {
    const size_t n = std::strlen(s) + 1;
    append_u16le(v, (unsigned short)n);
    v.insert(v.end(), s, s + n);
}
} /* anon */

TEST_CASE("manifest: Clear on zero-init manifest is safe", "[manifest]") {
    pdtest_manifest_t m = make_manifest();
    pdtest_manifestClear(&m);
    REQUIRE(m.num_entries == 0);
    REQUIRE(m.manifest_hash == 0);
    REQUIRE(m.entries == nullptr);
}

TEST_CASE("manifest: Free on zero-init manifest is safe", "[manifest]") {
    pdtest_manifest_t m = make_manifest();
    pdtest_manifestFree(&m);
    REQUIRE(m.entries == nullptr);
    REQUIRE(m.capacity == 0);
}

TEST_CASE("manifest: Add allocates and stores entry", "[manifest]") {
    pdtest_manifest_t m = make_manifest();
    pdtest_manifestAddEntry(&m, "base:dark_combat", PDTEST_MANIFEST_TYPE_BODY, 3);
    REQUIRE(m.num_entries == 1);
    REQUIRE(m.entries != nullptr);
    REQUIRE(std::string(m.entries[0].id) == "base:dark_combat");
    REQUIRE(m.entries[0].type == PDTEST_MANIFEST_TYPE_BODY);
    REQUIRE(m.entries[0].slot_index == 3);
    REQUIRE(m.entries[0].net_hash != 0);  /* synthetic FNV-1a fallback */
    pdtest_manifestFree(&m);
}

TEST_CASE("manifest: Add dedups by id string", "[manifest]") {
    pdtest_manifest_t m = make_manifest();
    pdtest_manifestAddEntry(&m, "base:dark_combat", PDTEST_MANIFEST_TYPE_BODY, 3);
    /* Same id, different type/slot — duplicate should be rejected. */
    pdtest_manifestAddEntry(&m, "base:dark_combat", PDTEST_MANIFEST_TYPE_HEAD, 7);
    REQUIRE(m.num_entries == 1);
    /* Original entry preserved. */
    REQUIRE(m.entries[0].type == PDTEST_MANIFEST_TYPE_BODY);
    REQUIRE(m.entries[0].slot_index == 3);
    pdtest_manifestFree(&m);
}

TEST_CASE("manifest: Add grows capacity past initial 64", "[manifest]") {
    pdtest_manifest_t m = make_manifest();
    /* Add 70 unique entries (initial capacity is 64). */
    for (int i = 0; i < 70; i++) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "base:asset_%03d", i);
        pdtest_manifestAddEntry(&m, buf, PDTEST_MANIFEST_TYPE_BODY, (unsigned char)(i & 0xFF));
    }
    REQUIRE(m.num_entries == 70);
    REQUIRE(m.capacity >= 70);
    /* Verify all entries readable. */
    for (int i = 0; i < 70; i++) {
        char expected[64];
        std::snprintf(expected, sizeof(expected), "base:asset_%03d", i);
        REQUIRE(std::string(m.entries[i].id) == expected);
    }
    pdtest_manifestFree(&m);
}

TEST_CASE("manifest: AddMod stores SHA-256 and COMPONENT type",
          "[manifest]") {
    pdtest_manifest_t m = make_manifest();
    auto sha = fake_sha256(0x42);
    pdtest_manifestAddModEntry(&m, "user:my_skin_pack",
                               PDTEST_MANIFEST_SLOT_MATCH, sha.data());
    REQUIRE(m.num_entries == 1);
    REQUIRE(m.entries[0].type == PDTEST_MANIFEST_TYPE_COMPONENT);
    REQUIRE(m.entries[0].slot_index == PDTEST_MANIFEST_SLOT_MATCH);
    REQUIRE(std::memcmp(m.entries[0].sha256, sha.data(), 32) == 0);
    pdtest_manifestFree(&m);
}

TEST_CASE("manifest: AddMod with NULL sha256 zeroes the field",
          "[manifest]") {
    pdtest_manifest_t m = make_manifest();
    pdtest_manifestAddModEntry(&m, "user:no_sha", PDTEST_MANIFEST_SLOT_MATCH, nullptr);
    REQUIRE(m.num_entries == 1);
    for (int i = 0; i < 32; i++) {
        REQUIRE(m.entries[0].sha256[i] == 0);
    }
    pdtest_manifestFree(&m);
}

TEST_CASE("manifest: ComputeHash is deterministic", "[manifest][hash]") {
    pdtest_manifest_t m1 = make_manifest();
    pdtest_manifest_t m2 = make_manifest();
    pdtest_manifestAddEntry(&m1, "base:dark_combat",  PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&m1, "base:mp_felicity",  PDTEST_MANIFEST_TYPE_STAGE, 0xFF);
    pdtest_manifestAddEntry(&m2, "base:dark_combat",  PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&m2, "base:mp_felicity",  PDTEST_MANIFEST_TYPE_STAGE, 0xFF);

    REQUIRE(pdtest_manifestComputeHash(&m1) == pdtest_manifestComputeHash(&m2));
    REQUIRE(m1.manifest_hash == m2.manifest_hash);
    REQUIRE(m1.manifest_hash != 0);
    pdtest_manifestFree(&m1);
    pdtest_manifestFree(&m2);
}

TEST_CASE("manifest: ComputeHash differs when type or slot differs",
          "[manifest][hash]") {
    pdtest_manifest_t m1 = make_manifest();
    pdtest_manifest_t m2 = make_manifest();
    pdtest_manifest_t m3 = make_manifest();
    pdtest_manifestAddEntry(&m1, "base:dark_combat", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&m2, "base:dark_combat", PDTEST_MANIFEST_TYPE_HEAD, 0);
    pdtest_manifestAddEntry(&m3, "base:dark_combat", PDTEST_MANIFEST_TYPE_BODY, 1);
    REQUIRE(pdtest_manifestComputeHash(&m1) != pdtest_manifestComputeHash(&m2));
    REQUIRE(pdtest_manifestComputeHash(&m1) != pdtest_manifestComputeHash(&m3));
    pdtest_manifestFree(&m1);
    pdtest_manifestFree(&m2);
    pdtest_manifestFree(&m3);
}

TEST_CASE("manifest: ComputeHash is order-sensitive", "[manifest][hash]") {
    pdtest_manifest_t m1 = make_manifest();
    pdtest_manifest_t m2 = make_manifest();
    pdtest_manifestAddEntry(&m1, "a", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&m1, "b", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&m2, "b", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&m2, "a", PDTEST_MANIFEST_TYPE_BODY, 0);
    REQUIRE(pdtest_manifestComputeHash(&m1) != pdtest_manifestComputeHash(&m2));
    pdtest_manifestFree(&m1);
    pdtest_manifestFree(&m2);
}

TEST_CASE("manifest: Serialize then Deserialize roundtrip preserves entries",
          "[manifest][wire]") {
    pdtest_manifest_t src = make_manifest();
    pdtest_manifestAddEntry(&src, "base:dark_combat",  PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&src, "base:head_dark",    PDTEST_MANIFEST_TYPE_HEAD, 0);
    pdtest_manifestAddEntry(&src, "base:mp_felicity",  PDTEST_MANIFEST_TYPE_STAGE, PDTEST_MANIFEST_SLOT_MATCH);
    pdtest_manifestAddEntry(&src, "base:falcon2",      PDTEST_MANIFEST_TYPE_WEAPON, PDTEST_MANIFEST_SLOT_MATCH);
    auto sha = fake_sha256(0xAB);
    pdtest_manifestAddModEntry(&src, "user:my_mod", PDTEST_MANIFEST_SLOT_MATCH, sha.data());

    unsigned char wire[1024];
    size_t written = 0;
    REQUIRE(pdtest_manifestSerialize(wire, sizeof(wire), &src, &written) == 0);
    REQUIRE(written > 0);
    REQUIRE(written < sizeof(wire));

    pdtest_manifest_t dst = make_manifest();
    size_t consumed = 0;
    REQUIRE(pdtest_manifestDeserialize(wire, written, &dst, &consumed) == 0);
    REQUIRE(consumed == written);
    REQUIRE(dst.num_entries == src.num_entries);

    for (int i = 0; i < (int)src.num_entries; i++) {
        REQUIRE(std::string(dst.entries[i].id) == std::string(src.entries[i].id));
        REQUIRE(dst.entries[i].type == src.entries[i].type);
        REQUIRE(dst.entries[i].slot_index == src.entries[i].slot_index);
        if (src.entries[i].type == PDTEST_MANIFEST_TYPE_COMPONENT) {
            REQUIRE(std::memcmp(dst.entries[i].sha256, src.entries[i].sha256, 32) == 0);
        }
    }

    pdtest_manifestFree(&src);
    pdtest_manifestFree(&dst);
}

TEST_CASE("manifest: Deserialize of empty manifest produces zero entries",
          "[manifest][wire]") {
    unsigned char wire[2] = { 0x00, 0x00 };  /* num_entries = 0 */
    pdtest_manifest_t dst = make_manifest();
    size_t consumed = 0;
    REQUIRE(pdtest_manifestDeserialize(wire, sizeof(wire), &dst, &consumed) == 0);
    REQUIRE(dst.num_entries == 0);
    REQUIRE(consumed == 2);
    pdtest_manifestFree(&dst);
}

TEST_CASE("manifest: Deserialize of truncated input returns error",
          "[manifest][wire]") {
    /* Claim 1 entry but provide no entry bytes. */
    unsigned char wire[2] = { 0x01, 0x00 };
    pdtest_manifest_t dst = make_manifest();
    REQUIRE(pdtest_manifestDeserialize(wire, sizeof(wire), &dst, nullptr) == 1);
    pdtest_manifestFree(&dst);
}

TEST_CASE("manifest: Deserialize of oversized count returns error",
          "[manifest][wire]") {
    /* num_entries = 0xFFFF is above MANIFEST_MAX_ENTRIES (4096). */
    unsigned char wire[2] = { 0xFF, 0xFF };
    pdtest_manifest_t dst = make_manifest();
    REQUIRE(pdtest_manifestDeserialize(wire, sizeof(wire), &dst, nullptr) == 1);
    pdtest_manifestFree(&dst);
}

TEST_CASE("manifest: Deserialize parse error rolls back entries from the failed packet",
          "[manifest][wire][security]") {
    std::vector<unsigned char> wire;
    append_u16le(wire, 2); /* one valid entry, then one truncated entry */

    wire.push_back(PDTEST_MANIFEST_TYPE_BODY);
    wire.push_back(3);
    append_str(wire, "base:partial_good");

    wire.push_back(PDTEST_MANIFEST_TYPE_COMPONENT);
    wire.push_back(PDTEST_MANIFEST_SLOT_MATCH);
    append_str(wire, "user:truncated_component");
    wire.insert(wire.end(), { 0x10, 0x11, 0x12, 0x13 }); /* short SHA-256 */

    pdtest_manifest_t dst = make_manifest();
    pdtest_manifestAddEntry(&dst, "base:preexisting", PDTEST_MANIFEST_TYPE_STAGE,
                            PDTEST_MANIFEST_SLOT_MATCH);
    const unsigned short start_entries = dst.num_entries;
    const unsigned int start_hash = pdtest_manifestComputeHash(&dst);

    size_t consumed = 0;
    REQUIRE(pdtest_manifestDeserialize(wire.data(), wire.size(), &dst, &consumed) == 1);
    REQUIRE(dst.num_entries == start_entries);
    REQUIRE(dst.manifest_hash == start_hash);
    REQUIRE(std::string(dst.entries[0].id) == "base:preexisting");

    pdtest_manifestFree(&dst);
}

TEST_CASE("manifest: Deserialize drops zero-SHA256 COMPONENT entries (SEC-5)",
          "[manifest][wire][security]") {
    /* Build a manifest with a COMPONENT entry whose sha256 is all zero.
     * Per the SEC-5 / MASTER-H2 invariant in netmanifest.c:944, the
     * deserializer must drop it (zero hash = no integrity = adversarial). */
    pdtest_manifest_t src = make_manifest();
    /* Real (non-zero) entry first */
    auto good = fake_sha256(0x01);
    pdtest_manifestAddModEntry(&src, "user:good_mod", PDTEST_MANIFEST_SLOT_MATCH, good.data());
    /* Bad (zero) entry */
    unsigned char zero[32] = {0};
    pdtest_manifestAddModEntry(&src, "user:bad_mod", PDTEST_MANIFEST_SLOT_MATCH, zero);

    unsigned char wire[1024];
    size_t written = 0;
    REQUIRE(pdtest_manifestSerialize(wire, sizeof(wire), &src, &written) == 0);

    pdtest_manifest_t dst = make_manifest();
    REQUIRE(pdtest_manifestDeserialize(wire, written, &dst, nullptr) == 0);
    REQUIRE(dst.num_entries == 1);  /* bad_mod dropped */
    REQUIRE(std::string(dst.entries[0].id) == "user:good_mod");

    pdtest_manifestFree(&src);
    pdtest_manifestFree(&dst);
}

TEST_CASE("manifest diff: empty current vs populated needed -> all to_load",
          "[manifest][diff]") {
    pdtest_manifest_t cur = make_manifest();
    pdtest_manifest_t need = make_manifest();
    pdtest_manifestAddEntry(&need, "a", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&need, "b", PDTEST_MANIFEST_TYPE_HEAD, 0);
    pdtest_manifestAddEntry(&need, "c", PDTEST_MANIFEST_TYPE_STAGE, PDTEST_MANIFEST_SLOT_MATCH);

    pdtest_manifest_diff_t d{};
    pdtest_manifestDiff(&cur, &need, &d);
    REQUIRE(d.num_to_load == 3);
    REQUIRE(d.num_to_unload == 0);
    REQUIRE(d.num_to_keep == 0);

    pdtest_manifestDiffFree(&d);
    pdtest_manifestFree(&cur);
    pdtest_manifestFree(&need);
}

TEST_CASE("manifest diff: populated current vs empty needed -> all to_unload",
          "[manifest][diff]") {
    pdtest_manifest_t cur = make_manifest();
    pdtest_manifest_t need = make_manifest();
    pdtest_manifestAddEntry(&cur, "a", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&cur, "b", PDTEST_MANIFEST_TYPE_HEAD, 0);

    pdtest_manifest_diff_t d{};
    pdtest_manifestDiff(&cur, &need, &d);
    REQUIRE(d.num_to_load == 0);
    REQUIRE(d.num_to_unload == 2);
    REQUIRE(d.num_to_keep == 0);

    pdtest_manifestDiffFree(&d);
    pdtest_manifestFree(&cur);
    pdtest_manifestFree(&need);
}

TEST_CASE("manifest diff: identical manifests -> all to_keep",
          "[manifest][diff]") {
    pdtest_manifest_t cur = make_manifest();
    pdtest_manifest_t need = make_manifest();
    pdtest_manifestAddEntry(&cur, "a", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&cur, "b", PDTEST_MANIFEST_TYPE_HEAD, 0);
    pdtest_manifestAddEntry(&need, "a", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&need, "b", PDTEST_MANIFEST_TYPE_HEAD, 0);

    pdtest_manifest_diff_t d{};
    pdtest_manifestDiff(&cur, &need, &d);
    REQUIRE(d.num_to_load == 0);
    REQUIRE(d.num_to_unload == 0);
    REQUIRE(d.num_to_keep == 2);

    pdtest_manifestDiffFree(&d);
    pdtest_manifestFree(&cur);
    pdtest_manifestFree(&need);
}

TEST_CASE("manifest diff: overlapping current/needed correctly partitions",
          "[manifest][diff]") {
    /* Stage transition A -> B: assets unique to A unload, unique to B load,
     * shared keep. */
    pdtest_manifest_t cur = make_manifest();
    pdtest_manifest_t need = make_manifest();
    /* Stage A loaded: a, b, c, d */
    pdtest_manifestAddEntry(&cur, "a", PDTEST_MANIFEST_TYPE_BODY, 0);
    pdtest_manifestAddEntry(&cur, "b", PDTEST_MANIFEST_TYPE_HEAD, 0);
    pdtest_manifestAddEntry(&cur, "c", PDTEST_MANIFEST_TYPE_BODY, 1);
    pdtest_manifestAddEntry(&cur, "d", PDTEST_MANIFEST_TYPE_HEAD, 1);
    /* Stage B needs: b, c, e, f (keeps b+c, unloads a+d, loads e+f) */
    pdtest_manifestAddEntry(&need, "b", PDTEST_MANIFEST_TYPE_HEAD, 0);
    pdtest_manifestAddEntry(&need, "c", PDTEST_MANIFEST_TYPE_BODY, 1);
    pdtest_manifestAddEntry(&need, "e", PDTEST_MANIFEST_TYPE_BODY, 2);
    pdtest_manifestAddEntry(&need, "f", PDTEST_MANIFEST_TYPE_HEAD, 2);

    pdtest_manifest_diff_t d{};
    pdtest_manifestDiff(&cur, &need, &d);
    REQUIRE(d.num_to_load == 2);    /* e, f */
    REQUIRE(d.num_to_unload == 2);  /* a, d */
    REQUIRE(d.num_to_keep == 2);    /* b, c */

    /* Verify exact ids in each bucket. */
    auto contains = [](pdtest_manifest_diff_entry_t *arr, int n, const char *id) {
        for (int i = 0; i < n; i++) {
            if (std::string(arr[i].id) == id) return true;
        }
        return false;
    };
    REQUIRE(contains(d.to_load,   d.num_to_load,   "e"));
    REQUIRE(contains(d.to_load,   d.num_to_load,   "f"));
    REQUIRE(contains(d.to_unload, d.num_to_unload, "a"));
    REQUIRE(contains(d.to_unload, d.num_to_unload, "d"));
    REQUIRE(contains(d.to_keep,   d.num_to_keep,   "b"));
    REQUIRE(contains(d.to_keep,   d.num_to_keep,   "c"));

    pdtest_manifestDiffFree(&d);
    pdtest_manifestFree(&cur);
    pdtest_manifestFree(&need);
}

TEST_CASE("manifest diff: DiffFree on zero-init diff is safe",
          "[manifest][diff]") {
    pdtest_manifest_diff_t d{};
    pdtest_manifestDiffFree(&d);
    REQUIRE(d.to_load == nullptr);
    REQUIRE(d.to_unload == nullptr);
    REQUIRE(d.to_keep == nullptr);
}
