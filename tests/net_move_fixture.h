#ifndef PD_TEST_NET_MOVE_FIXTURE_H
#define PD_TEST_NET_MOVE_FIXTURE_H
#include <stdint.h>
typedef struct move_fixture_result {
    uint32_t tick, ucmd;
    float fields[12];
    int weaponnum, emptyIdentity, unchanged;
} move_fixture_result;
#ifdef __cplusplus
extern "C" {
#endif
int moveFixtureRead(const void *, uint32_t, move_fixture_result *);
uint32_t moveFixtureWrite(void *, uint32_t, uint32_t, uint32_t, const float *, int);
#ifdef __cplusplus
}
#endif
#endif
