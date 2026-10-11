#include <float.h>
#include <stddef.h>
#include <string.h>
#include "net/net.h"
#include "net/netbuf.h"
#include "net/net_player_move_wire.h"

static bool moveTickAfter(u32 candidate, u32 current)
{
    const u32 distance = candidate - current;
    /* Half-range jumps are ambiguous; never treat them as newer. */
    return distance != 0 && distance < 0x80000000u;
}

bool netPlayerMoveAcknowledged(const struct netclient *client, u32 tick)
{
    return client && client->outmoveack_valid
        && (client->outmoveack == tick || moveTickAfter(client->outmoveack, tick));
}

bool netPlayerMoveCommit(struct netclient *client,
        const struct netplayermove *move, u32 ack)
{
    if (!client || !move || (client->inmove_valid
            && !moveTickAfter(move->tick, client->inmove[0].tick))) return false;

    memmove(client->inmove + 1, client->inmove,
        sizeof(client->inmove) - sizeof(*client->inmove));
    client->inmove[0] = *move;
    client->inmove_valid = true;
    client->lerpticks = 0;
    if ((!client->outmoveack_valid && ack != 0)
            || (client->outmoveack_valid && moveTickAfter(ack, client->outmoveack))) {
        client->outmoveack = ack;
        client->outmoveack_valid = true;
    }
    return true;
}

u32 netbufWritePlayerMove(struct netbuf *buf, const struct netplayermove *in)
{
	netbufWriteU32(buf, in->tick);
	netbufWriteU32(buf, in->ucmd);
	netbufWriteF32(buf, in->leanofs);
	netbufWriteF32(buf, in->crouchofs);
	netbufWriteF32(buf, in->movespeed[0]);
	netbufWriteF32(buf, in->movespeed[1]);
	netbufWriteF32(buf, in->angles[0]);
	netbufWriteF32(buf, in->angles[1]);
	netbufWriteF32(buf, in->crosspos[0]);
	netbufWriteF32(buf, in->crosspos[1]);
	netbufWriteS8(buf, in->weaponnum);
	netbufWriteCoord(buf, &in->pos);
	if (in->ucmd & UCMD_AIMMODE) {
		netbufWriteF32(buf, in->zoomfov);
	}
	return buf->error;
}

u32 netbufReadPlayerMove(struct netbuf *buf, struct netplayermove *out)
{
    struct netplayermove candidate;
    struct netplayermove *in = &candidate;
    if (!buf || !out) return 1;
    memset(&candidate, 0, sizeof(candidate));
	in->tick = netbufReadU32(buf);
	in->ucmd = netbufReadU32(buf);
	in->leanofs = netbufReadF32(buf);
	in->crouchofs = netbufReadF32(buf);
	in->movespeed[0] = netbufReadF32(buf);
	in->movespeed[1] = netbufReadF32(buf);
	in->angles[0] = netbufReadF32(buf);
	in->angles[1] = netbufReadF32(buf);
	in->crosspos[0] = netbufReadF32(buf);
	in->crosspos[1] = netbufReadF32(buf);
	in->weaponnum = netbufReadS8(buf);
	/* M-6: Clamp weaponnum to valid range to prevent OOB from malicious packets. */
	if (in->weaponnum < WEAPON_NONE || in->weaponnum >= WEAPON_CUSTOM_END) {
		in->weaponnum = WEAPON_UNARMED;
	}
	netbufReadCoord(buf, &in->pos);
	if (in->ucmd & UCMD_AIMMODE) {
		in->zoomfov = netbufReadF32(buf);
	} else {
		in->zoomfov = 0.f;
	}
    if (buf->error) return buf->error;
    /* Wire fields are untrusted on both ingress paths. Validate the complete
     * candidate before publishing any bytes, including the unwired identity.
     * Physical speed/position authority is a separate simulation boundary. */
    const f32 values[] = {in->leanofs, in->crouchofs,
        in->movespeed[0], in->movespeed[1], in->angles[0], in->angles[1],
        in->crosspos[0], in->crosspos[1], in->pos.x, in->pos.y, in->pos.z,
        in->zoomfov};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if (!(values[i] >= -FLT_MAX && values[i] <= FLT_MAX)) {
            buf->error = 1;
            return buf->error;
        }
    }
    *out = candidate;
    return 0;
}
