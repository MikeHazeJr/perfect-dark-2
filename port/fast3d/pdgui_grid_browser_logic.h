#ifndef PDGUI_GRID_BROWSER_LOGIC_H
#define PDGUI_GRID_BROWSER_LOGIC_H

#include <algorithm>
#include <string_view>

namespace gridBrowserLogic {

inline constexpr int NoParent = -1;
inline constexpr int ParentUnavailable = -2;

enum class FamilyState { Root, MissingParent, Cycle };

struct Family {
    FamilyState state;
    int anchor;
};

// The caller resolves native identity hashes to rows before traversal.
// A missing node retains the row that names the unavailable parent.
constexpr Family resolveFamily(const int *parents, int count, int start)
{
    if (!parents || start < 0 || start >= count) return {FamilyState::Cycle, start};
    int row = start;
    for (int hop = 0; hop < count; ++hop) {
        const int parent = parents[row];
        if (parent == NoParent) return {FamilyState::Root, row};
        if (parent < 0 || parent >= count) return {FamilyState::MissingParent, row};
        row = parent;
    }
    return {FamilyState::Cycle, start};
}

template <typename Arena>
constexpr int findArenaById(const Arena *rows, int count, std::string_view id)
{
    for (int index = 0; index < count; ++index) {
        if (std::string_view(rows[index].id) == id) return index;
    }
    return -1;
}

constexpr bool useColumns(float width, float scale, float fontSize)
{
    return width >= std::max(820.0f * scale, 41.0f * fontSize);
}

constexpr bool useActionRow(float width, float gap, float requiredButtonWidth)
{
    return (width - gap) * 0.5f >= requiredButtonWidth;
}

constexpr float bodyHeight(float available, float reserved)
{
    return std::max(1.0f, available - reserved);
}

namespace contract {

inline constexpr int familyChain[] = {NoParent, 0, 1, ParentUnavailable, 3, 6, 5, 7};
static_assert(resolveFamily(familyChain, 8, 0).anchor == 0);
static_assert(resolveFamily(familyChain, 8, 2).state == FamilyState::Root);
static_assert(resolveFamily(familyChain, 8, 2).anchor == 0);
static_assert(resolveFamily(familyChain, 8, 4).state == FamilyState::MissingParent);
static_assert(resolveFamily(familyChain, 8, 4).anchor == 3);
static_assert(resolveFamily(familyChain, 8, 5).state == FamilyState::Cycle);
static_assert(resolveFamily(familyChain, 8, 7).state == FamilyState::Cycle);

struct ArenaIdentity { std::string_view id; };
inline constexpr ArenaIdentity reordered[] = {{"mod:variant"}, {"base:arena"}, {""}};
static_assert(findArenaById(reordered, 3, "base:arena") == 1);
static_assert(findArenaById(reordered, 3, "") == 2);
static_assert(findArenaById(reordered, 3, "mod:missing") == -1);
static_assert(useColumns(1120.0f, 1.0f, 18.0f));
static_assert(!useColumns(600.0f, 1.0f, 18.0f));
static_assert(!useColumns(1120.0f, 1.5f, 27.0f));
static_assert(!useActionRow(250.0f, 10.0f, 130.0f));
static_assert(useActionRow(290.0f, 10.0f, 130.0f));
static_assert(bodyHeight(190.0f, 100.0f) == 90.0f);
static_assert(bodyHeight(80.0f, 100.0f) == 1.0f);

} // namespace contract
} // namespace gridBrowserLogic

#endif
