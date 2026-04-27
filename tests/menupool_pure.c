/*
 * menupool_pure.c -- pure-C subset of the menu pool.
 *
 * Mirrors port/src/menupool.c's slot machinery without the dialogdef
 * registry or InputContext coupling. Tests verify push / pop / dedup
 * invariants on the pool API.
 *
 * @SYNC port/src/menupool.c (menupoolAcquire/Release/IsActive/ReleaseAll).
 */

#include "menupool_pure.h"

typedef struct {
    s32 active;
    u32 generation;
} mp_slot_t;

static mp_slot_t s_Pool[MP_TYPE_COUNT];

static s32 inRange(mp_type_t t)
{
    return (t > MP_TYPE_NONE && t < MP_TYPE_COUNT);
}

void menupoolPureReset(void)
{
    for (s32 i = 0; i < MP_TYPE_COUNT; i++) {
        s_Pool[i].active = 0;
        s_Pool[i].generation = 0;
    }
}

s32 menupoolPureAcquire(mp_type_t type)
{
    if (!inRange(type)) return -1;
    if (s_Pool[type].active) {
        return 0;  /* duplicate */
    }
    s_Pool[type].active = 1;
    s_Pool[type].generation++;
    return 1;
}

s32 menupoolPureRelease(mp_type_t type)
{
    if (!inRange(type)) return -1;
    if (!s_Pool[type].active) {
        return 0;  /* already free, idempotent */
    }
    s_Pool[type].active = 0;
    return 1;
}

s32 menupoolPureIsActive(mp_type_t type)
{
    if (!inRange(type)) return 0;
    return s_Pool[type].active;
}

s32 menupoolPureCountActive(void)
{
    s32 n = 0;
    for (s32 i = MP_TYPE_NONE + 1; i < MP_TYPE_COUNT; i++) {
        if (s_Pool[i].active) n++;
    }
    return n;
}

void menupoolPureReleaseAll(void)
{
    for (s32 i = MP_TYPE_NONE + 1; i < MP_TYPE_COUNT; i++) {
        if (s_Pool[i].active) {
            s_Pool[i].active = 0;
        }
    }
}

u32 menupoolPureGeneration(mp_type_t type)
{
    if (!inRange(type)) return 0;
    return s_Pool[type].generation;
}
