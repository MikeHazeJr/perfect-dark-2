#ifndef PD_WEAPON_COMMAND_SOURCE_H
#define PD_WEAPON_COMMAND_SOURCE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct weapon_command_source weapon_command_source;
typedef enum weapon_command_reference {
    WEAPON_COMMAND_NO_REFERENCE, WEAPON_COMMAND_COMMANDS,
    WEAPON_COMMAND_CLIP, WEAPON_COMMAND_SOUND
} weapon_command_reference;
typedef struct weapon_command_source_item {
    uint8_t type, selector;
    uint16_t trigger;
    intptr_t value;
    weapon_command_reference reference_kind;
    const char *reference; /* Exact catalog ID, owned by source. */
} weapon_command_source_item;
/* Strict complete public commands.json. Native scalar widths are validated;
 * numeric/bare dependency identities, unknown members, duplicate keys and
 * missing/early terminators reject. No catalog lookup or native mutation. */
weapon_command_source *weaponCommandSourceRead(const char *catalog_id,
    const char *json, size_t size, char *error, size_t cap);
void weaponCommandSourceFree(weapon_command_source *);
const char *weaponCommandSourceId(const weapon_command_source *);
const char *weaponCommandSourceName(const weapon_command_source *);
size_t weaponCommandSourceCount(const weapon_command_source *);
int weaponCommandSourceItem(const weapon_command_source *, size_t,
    weapon_command_source_item *out);
#ifdef __cplusplus
}
#endif
#endif
