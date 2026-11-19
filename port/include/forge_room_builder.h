#ifndef PD_FORGE_ROOM_BUILDER_H
#define PD_FORGE_ROOM_BUILDER_H

#include "asset_archive_clone.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct forge_room_extension forge_room_extension_t;

/* Extend room 1 or 2 away from the shared doorway. The returned overrides
 * borrow their names and bytes from the extension until it is freed. */
forge_room_extension_t *forgeRoomCreateExtension(const void *arenaBytes, u32 arenaSize,
        const char *scenarioMember, s32 room, s32 extraLength, char *err, size_t cap);
/* Apply exact brightness-only edits to the pinned room table before extending. */
forge_room_extension_t *forgeRoomCreateExtensionWithRoomBrightness(const void *arenaBytes, u32 arenaSize,
        const char *scenarioMember, s32 room, s32 extraLength, const char *draftRooms,
        size_t draftRoomsLength, char *err, size_t cap);
const asset_archive_clone_override_t *forgeRoomExtensionEntries(
        const forge_room_extension_t *extension, u32 *count);
void forgeRoomFreeExtension(forge_room_extension_t *extension);

#ifdef __cplusplus
}

#include <array>
#include <string>

inline constexpr const char *FORGE_ROOM_TEMPLATE_ID = "mod_grid_two_rooms:arena_grid_two_rooms";

struct ForgeRoomSize {
    static constexpr s32 TemplateLength = 1550;
    static constexpr s32 TemplateWidth = 1640;
    static constexpr s32 TemplateHeight = 520;
    static constexpr s32 TemplateDoorwayWidth = 440;
    static constexpr s32 TemplateDoorwayHeight = 340;

    static constexpr s32 MinLength = 1000;
    static constexpr s32 MaxLength = 4000;
    static constexpr s32 MinWidth = 1200;
    static constexpr s32 MaxWidth = 4000;
    static constexpr s32 MinHeight = 400;
    static constexpr s32 MaxHeight = 1000;
    static constexpr s32 MinDoorwayWidth = 300;
    static constexpr s32 MinDoorwayHeight = 250;
    static constexpr s32 MinSideWallWidth = 150;
    static constexpr s32 MinLintelHeight = 80;

    s32 firstLength = TemplateLength;
    s32 secondLength = TemplateLength;
    s32 width = TemplateWidth;
    s32 height = TemplateHeight;
    /* Zero preserves the template's aperture ratio on this axis. */
    s32 doorwayWidth = 0;
    s32 doorwayHeight = 0;

    double effectiveDoorwayWidth() const
    {
        return doorwayWidth ? doorwayWidth
            : width * (static_cast<double>(TemplateDoorwayWidth) / TemplateWidth);
    }

    double effectiveDoorwayHeight() const
    {
        return doorwayHeight ? doorwayHeight
            : height * (static_cast<double>(TemplateDoorwayHeight) / TemplateHeight);
    }
};

struct ForgeRoomOverrides {
    std::array<std::string, 6> paths;
    std::array<std::string, 6> source;
    std::array<asset_archive_clone_override_t, 6> entries;
};

/* Build a complete set of native source overrides. Keep it alive until the
 * archive author returns because its entries borrow the source buffers. */
bool forgeRoomBuildOverrides(const char *templatePath, const ForgeRoomSize &size,
        ForgeRoomOverrides &out, char *err, size_t errCap);

#endif /* __cplusplus */
#endif
