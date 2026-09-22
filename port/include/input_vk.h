#ifndef PD_INPUT_VK_H
#define PD_INPUT_VK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum input_vk_control_kind {
    INPUT_VK_BUTTON = 1,
    INPUT_VK_AXIS,
    INPUT_VK_LEGACY_OVERLAP
};
typedef struct input_vk_control {
    int player;
    enum input_vk_control_kind kind;
    int index; /* button index, or axis index for AXIS/LEGACY_OVERLAP */
    int direction; /* 0 for a button; -1/+1 for an axis */
} input_vk_control_t;

/* Zero denotes invalid; all four old 32-slot player ranges stay unchanged. */
uint32_t inputVkRawButton(int player, int button);
uint32_t inputVkDigitalAxis(int player, int axis, int direction);
uint32_t inputVkAxisByOrdinal(int player, int ordinal);
uint32_t inputVkLegacyAlias(uint32_t vk);
int inputVkDecodeController(uint32_t vk, input_vk_control_t *control);
/* Controller names only. No partial output on failure; parser returns0 for
 * an unknown name. Callers retain their existing MKB/UNKNOWN name handling. */
int inputVkControllerName(uint32_t vk, char *out, size_t capacity);
uint32_t inputVkControllerByName(const char *name);
#ifdef __cplusplus
}
#endif
#endif
