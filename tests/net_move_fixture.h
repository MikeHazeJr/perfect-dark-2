#ifndef PD_TEST_NET_MOVE_FIXTURE_H
#define PD_TEST_NET_MOVE_FIXTURE_H
#include <stdint.h>
typedef struct move_fixture_result {
    uint32_t tick, ucmd;
    float fields[12];
    int weaponnum, emptyIdentity, unchanged;
} move_fixture_result;
typedef struct move_admission_result {
    uint32_t tick, previousTick, ack, lerp, command;
    float position;
    int accepted, unchanged, ackValid, acknowledged;
} move_admission_result;
#ifdef __cplusplus
extern "C" {
#endif
int moveFixtureRead(const void *, uint32_t, move_fixture_result *);
uint32_t moveFixtureWrite(void *, uint32_t, uint32_t, uint32_t, const float *, int);
void *moveFixtureCreate(void);
void moveFixtureDestroy(void *);
void moveFixtureCommit(void *, uint32_t tick, uint32_t ack, float position,
    uint32_t command, uint32_t checkAckTick, move_admission_result *);
#ifdef __cplusplus
}
#endif
#endif
