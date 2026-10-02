#ifndef PD_INPUT_PROFILE_METADATA_H
#define PD_INPUT_PROFILE_METADATA_H

/* Shared Settings metadata contract. No game, SDL or N64 SDK types. */
#define INPUT_PROFILE_COUNT 6
#define INPUT_DEVICE_RULE_COUNT 16
#define INPUT_PROFILE_NAME_CAPACITY 32
#define INPUT_DEVICE_KEY_CAPACITY 128
#define INPUT_DEVICE_ALIAS_CAPACITY 64
#define INPUT_PROFILE_NAMES_STR_MAX (INPUT_PROFILE_COUNT * INPUT_PROFILE_NAME_CAPACITY)
/* Per row: one profile digit, two delimiters, both field payloads and one
 * separator. The final unused separator provides the terminating NUL. */
#define INPUT_DEVICE_PROFILES_STR_MAX (INPUT_DEVICE_RULE_COUNT * \
    (1 + 2 + INPUT_DEVICE_KEY_CAPACITY - 1 + INPUT_DEVICE_ALIAS_CAPACITY - 1 + 1))

#endif
