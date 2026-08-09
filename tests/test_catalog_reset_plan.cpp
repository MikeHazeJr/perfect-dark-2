#include "catch.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "catalog_reset_plan.h"
}

struct ResetEdges {
    std::vector<std::pair<std::string, std::string>> rows;
};

static s32 containsEdge(const char *parent, const char *dependency, void *userdata)
{
    auto *edges = static_cast<ResetEdges *>(userdata);
    for (const auto &edge : edges->rows) {
        if (edge.first == parent && edge.second == dependency) return 1;
    }
    return 0;
}

TEST_CASE("catalog reset plan covers every typed family without a hard-coded allowlist",
          "[catalog][lifecycle][t-catalog-004]")
{
    std::vector<std::string> ids;
    std::vector<catalog_reset_plan_row_t> rows;
    std::vector<size_t> order;
    ResetEdges edges;
    char error[128] = {};

    for (int type = ASSET_NONE + 1; type < ASSET_TYPE_COUNT; type++) {
        ids.emplace_back("mod:type_" + std::to_string(type));
    }
    rows.resize(ids.size());
    order.assign(ids.size(), static_cast<size_t>(-1));
    for (size_t i = 0; i < ids.size(); i++) {
        rows[i] = {ids[i].c_str(), static_cast<asset_type_e>(i + 1)};
    }

    REQUIRE(catalogResetPlanBuild(rows.data(), rows.size(), containsEdge,
        &edges, order.data(), error, sizeof(error)) == 1);
    std::vector<bool> seen(rows.size(), false);
    for (size_t index : order) {
        REQUIRE(index < rows.size());
        REQUIRE_FALSE(seen[index]);
        seen[index] = true;
    }
}

TEST_CASE("catalog reset plan is growable and retires parents before shared children",
          "[catalog][lifecycle][t-catalog-004]")
{
    constexpr size_t Count = 130;
    std::vector<std::string> ids;
    std::vector<catalog_reset_plan_row_t> rows(Count);
    std::vector<size_t> order(Count, static_cast<size_t>(-1));
    ResetEdges edges;
    char error[128] = {};

    ids.reserve(Count);
    for (size_t i = 0; i < Count; i++) {
        ids.emplace_back("mod:reset_" + std::to_string(i));
    }
    for (size_t i = 0; i < Count; i++) {
        rows[i] = {ids[i].c_str(), ASSET_TOOL};
        if (i + 1 < Count) edges.rows.emplace_back(ids[i], ids[i + 1]);
    }
    /* A diamond into row 80 proves one shared child waits for both owners. */
    edges.rows.emplace_back(ids[12], ids[80]);

    REQUIRE(catalogResetPlanBuild(rows.data(), rows.size(), containsEdge,
        &edges, order.data(), error, sizeof(error)) == 1);
    std::vector<size_t> position(Count);
    for (size_t i = 0; i < Count; i++) position[order[i]] = i;
    for (const auto &edge : edges.rows) {
        size_t parent = static_cast<size_t>(std::stoul(edge.first.substr(10)));
        size_t child = static_cast<size_t>(std::stoul(edge.second.substr(10)));
        REQUIRE(position[parent] < position[child]);
    }
}

TEST_CASE("catalog reset plan rejects cycles before publishing an order",
          "[catalog][lifecycle][t-catalog-004]")
{
    const std::array<catalog_reset_plan_row_t, 2> rows{{
        {"mod:a", ASSET_MODEL}, {"mod:b", ASSET_TEXTURE}
    }};
    std::array<size_t, 2> order{{99, 99}};
    ResetEdges edges{{{"mod:a", "mod:b"}, {"mod:b", "mod:a"}}};
    char error[128] = {};

    REQUIRE(catalogResetPlanBuild(rows.data(), rows.size(), containsEdge,
        &edges, order.data(), error, sizeof(error)) == 0);
    REQUIRE(std::strstr(error, "cycle") != nullptr);
    REQUIRE(order[0] == 99);
    REQUIRE(order[1] == 99);
}
