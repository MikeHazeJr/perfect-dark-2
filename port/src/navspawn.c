#include <stdlib.h>
#include "navspawn.h"

/*
 * Navmesh-based spawn point fallback for SP maps used in MP.
 *
 * SP maps have waypoints (AI navigation graph) but no INTROCMD_SPAWN pads.
 * This module picks random waypoint positions with rejection sampling to
 * spread them across the map, then faces each spawn away from the nearest wall.
 *
 * The implementation uses game code (padUnpack, cdExamCylMove01, etc.) that
 * is only available in the client build.  The server build gets a stub that
 * returns 0, which is correct: the server defers spawn decisions to clients.
 */

#if !defined(PD_SERVER)

#include <ultra64.h>
#include "types.h"
#include "constants.h"
#include "bss.h"
#include "game/pad.h"
#include "game/atan2f.h"
#include "lib/collision.h"

#define NAVSPAWN_MIN_DIST_SQ (500.0f * 500.0f)
#define NAVSPAWN_MAX_ATTEMPTS 100

s32 navspawnGeneratePoints(s32 max_count, struct coord *out_pos, f32 *out_yaw)
{
    /* 4 cardinal directions for wall probing */
    static const f32 dirX[4] = { 0.0f,  1.0f, 0.0f, -1.0f };
    static const f32 dirZ[4] = { 1.0f,  0.0f, -1.0f, 0.0f };

    struct waypoint *waypoints;
    s32 numwaypoints;
    s32 count;
    s32 i;

    if (!g_StageSetup.waypoints || max_count < 1) {
        return 0;
    }

    waypoints = g_StageSetup.waypoints;

    /* Count waypoints — array is terminated by padnum < 0 */
    numwaypoints = 0;
    while (waypoints[numwaypoints].padnum >= 0) {
        numwaypoints++;
    }

    if (numwaypoints < 1) {
        return 0;
    }

    if (max_count > 24) {
        max_count = 24;
    }

    count = 0;

    for (i = 0; i < max_count; i++) {
        s32 attempt;
        s32 found = 0;

        for (attempt = 0; attempt < NAVSPAWN_MAX_ATTEMPTS; attempt++) {
            s32 idx = (s32)((u32)rand() % (u32)numwaypoints);
            struct pad p;
            s32 too_close;
            s32 j;

            padUnpack(waypoints[idx].padnum, PADFIELD_POS | PADFIELD_ROOM | PADFIELD_FLAGS, &p);

            /* Skip pads with invalid rooms or AI-drop-only flags */
            if (p.room < 0) {
                continue;
            }
            if (p.flags & PADFLAG_AIDROP) {
                continue;
            }

            /* Reject if within minimum distance of an already-placed point */
            too_close = 0;
            for (j = 0; j < count; j++) {
                f32 dx = p.pos.x - out_pos[j].x;
                f32 dz = p.pos.z - out_pos[j].z;
                if (dx * dx + dz * dz < NAVSPAWN_MIN_DIST_SQ) {
                    too_close = 1;
                    break;
                }
            }

            if (!too_close) {
                RoomNum rooms[2];
                f32 wallX;
                f32 wallZ;
                s32 wallCount;
                s32 dir;

                out_pos[count] = p.pos;

                rooms[0] = p.room;
                rooms[1] = -1;

                /* Probe 4 cardinal directions to find walls */
                wallX = 0.0f;
                wallZ = 0.0f;
                wallCount = 0;

                for (dir = 0; dir < 4; dir++) {
                    struct coord probe;
                    probe.x = p.pos.x + dirX[dir] * 200.0f;
                    probe.y = p.pos.y;
                    probe.z = p.pos.z + dirZ[dir] * 200.0f;

                    if (cdExamCylMove01(&out_pos[count], &probe, 30, rooms,
                            CDTYPE_BG, false, 0, 0) == CDRESULT_COLLISION) {
                        wallX += dirX[dir];
                        wallZ += dirZ[dir];
                        wallCount++;
                    }
                }

                if (wallCount > 0) {
                    /* Face away from the average wall direction */
                    out_yaw[count] = atan2f(-wallX, -wallZ);
                } else {
                    /* No walls found — use random yaw */
                    out_yaw[count] = ((f32)rand() / (f32)RAND_MAX) * M_BADTAU;
                }

                count++;
                found = 1;
                break;
            }
        }

        if (!found) {
            /* Couldn't spread any further.  If we have at least one point,
             * stop here.  If we have none, try once without spacing to
             * guarantee at least one usable spawn. */
            if (count == 0) {
                struct pad p;
                s32 idx = (s32)((u32)rand() % (u32)numwaypoints);
                padUnpack(waypoints[idx].padnum, PADFIELD_POS | PADFIELD_ROOM, &p);
                if (p.room >= 0) {
                    out_pos[count] = p.pos;
                    out_yaw[count] = ((f32)rand() / (f32)RAND_MAX) * M_BADTAU;
                    count++;
                }
            }
            break;
        }
    }

    return count;
}

#else /* PD_SERVER */

/* Server build: spawn decisions are handled client-side. */
s32 navspawnGeneratePoints(s32 max_count, struct coord *out_pos, f32 *out_yaw)
{
    (void)max_count;
    (void)out_pos;
    (void)out_yaw;
    return 0;
}

#endif /* !PD_SERVER */
