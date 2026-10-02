#ifndef PD_TEXTURE_MARKER_H
#define PD_TEXTURE_MARKER_H

#include <stdint.h>
#include <string.h>

/* Private, in-memory PC display-list identity. Public assets use catalog IDs.
 * Keep the original 32-bit control word intact; legacy ROM markers retain
 * their original 12-bit payload decoding. Never persist native markers. */
#define TEXTURE_NATIVE_SLOT_LIMIT 65535
#define TEXTURE_HIT_GLASS TEXTURE_NATIVE_SLOT_LIMIT
#define TEXTURE_MARKER_NATIVE_TAG UINT64_C(0x5044545800000000)

typedef struct texture_marker_payload {
    uint16_t primary;
    uint16_t secondary;
    uint8_t minimum;
    uint8_t reserved[3];
} texture_marker_payload_t;

#ifdef __cplusplus
static_assert(sizeof(uintptr_t) == 8 && sizeof(texture_marker_payload_t) == 8,
    "native texture markers require the PC 64-bit display-list carrier");
#else
_Static_assert(sizeof(uintptr_t) == 8 && sizeof(texture_marker_payload_t) == 8,
    "native texture markers require the PC 64-bit display-list carrier");
#endif

static inline uintptr_t textureMarkerNativeControl(uint32_t control)
{
    return (uintptr_t)TEXTURE_MARKER_NATIVE_TAG | control;
}

static inline uintptr_t textureMarkerNativePayload(uint16_t primary,
        uint16_t secondary, uint8_t minimum)
{
    const texture_marker_payload_t payload = {primary, secondary, minimum, {0, 0, 0}};
    uintptr_t word;
    memcpy(&word, &payload, sizeof(word));
    return word;
}

static inline texture_marker_payload_t textureMarkerDecode(uintptr_t control,
        uintptr_t word)
{
    texture_marker_payload_t payload;
    if ((control & UINT64_C(0xffffffff00000000)) == TEXTURE_MARKER_NATIVE_TAG) {
        memcpy(&payload, &word, sizeof(payload));
    } else {
        payload.primary = word & 0xfff;
        payload.secondary = (word >> 12) & 0xfff;
        payload.minimum = (word >> 24) & 0xff;
        memset(payload.reserved, 0, sizeof(payload.reserved));
    }
    return payload;
}

#endif
