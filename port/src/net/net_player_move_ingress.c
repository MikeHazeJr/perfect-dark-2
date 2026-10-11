/* Production CLC/SVC movement ingress, also linked by the C test fixture. */
#include "types.h"
#include "platform.h"
#include "data.h"
#include "bss.h"
#include "game/chraction.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "system.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/net_player_move_wire.h"

static bool netmsgClientCanSelectWeapon(struct netclient *cl, s32 weaponnum, bool dualwield)
{
	if (!cl || cl->playernum >= MAX_PLAYERS || !cl->player) {
		return false;
	}

	if (weaponnum < WEAPON_UNARMED || weaponnum >= WEAPON_CUSTOM_END) {
		return false;
	}

	const s32 prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(cl->playernum);
	const bool has_single = invHasSingleWeaponIncAllGuns(weaponnum);
	const bool has_double = !dualwield || invHasDoubleWeaponIncAllGuns(weaponnum, weaponnum);
	setCurrentPlayerNum(prevplayernum);

	return has_single && has_double;
}

u32 netmsgClcMoveRead(struct netbuf *src, struct netclient *srccl)
{
	struct netplayermove newmove = {0};
	const u32 outmoveack = netbufReadU32(src);
	netbufReadPlayerMove(src, &newmove);
	if (src->error) {
		return src->error;
	}

	if (srccl->state != CLSTATE_GAME) {
		// silently ignore
		return src->error;
	}

	if ((newmove.ucmd & UCMD_VEHICLE_PC_INTENT)
			&& !(newmove.ucmd & UCMD_ACTIVATE)) {
		sysLogPrintf(LOG_WARNING,
			"NET: rejected CLC_MOVE vehicle PC intent without activation from client %u",
			(unsigned)srccl->id);
		return 1;
	}

	if ((newmove.ucmd & UCMD_SELECT) && newmove.weaponnum >= 0) {
		const bool dualwield = (newmove.ucmd & UCMD_SELECT_DUAL) != 0;
		if (!netmsgClientCanSelectWeapon(srccl, newmove.weaponnum, dualwield)) {
			sysLogPrintf(LOG_WARNING,
				"NET: rejected CLC_MOVE weapon select from client %u (weapon=%d dual=%d)",
				srccl->id, newmove.weaponnum, dualwield ? 1 : 0);
			newmove.weaponnum = -1;
			newmove.ucmd &= ~(UCMD_SELECT | UCMD_SELECT_DUAL);
		}
	}

	if (!src->error) {
		if (!netPlayerMoveCommit(srccl, &newmove, outmoveack)) {
			/* Rejection preserves the ACK and interpolation history too. */
			return src->error;
		}

		// enforce teleports and such
		if (srccl->forcetick && srccl->player) {
			if (netPlayerMoveAcknowledged(srccl, srccl->forcetick)) {
				// client has acknowledged our last sent move, clear the force flags
				srccl->forcetick = 0;
				srccl->player->ucmd &= ~UCMD_FL_FORCEMASK;
				sysLogPrintf(LOG_NOTE, "NET: client %u successfully forcemoved", srccl->id);
			} else {
				// client hasn't teleported yet, discard the new position from the input command
				if (srccl->player->ucmd & UCMD_FL_FORCEPOS) {
					srccl->inmove[0].pos = srccl->player->prop->pos;
				}
				if (srccl->player->ucmd & UCMD_FL_FORCEANGLE) {
					srccl->inmove[0].angles[0] = srccl->player->vv_theta;
					srccl->inmove[0].angles[1] = srccl->player->vv_verta;
				}
			}
		}
		if (srccl->reconnect_gameplay_witness_pending
				&& (newmove.ucmd & UCMD_FIRE)
				&& srccl->player && srccl->player->prop) {
			srccl->reconnect_gameplay_witness_pending = false;
			sysLogPrintf(LOG_NOTE,
				"NET.RECONNECT.GAMEPLAY client=%u player=%u room=%u prop=%u move_tick=%u fire=1 server_accepted=1",
				(unsigned)srccl->id, (unsigned)srccl->playernum,
				(unsigned)srccl->room_id,
				(unsigned)srccl->player->prop->syncid,
				(unsigned)newmove.tick);
		}
	}

	return src->error;
}

u32 netmsgSvcPlayerMoveRead(struct netbuf *src, struct netclient *srccl)
{
	u8 id = 0;
	u32 outmoveack = 0;
	struct netplayermove newmove = {0};
	RoomNum newrooms[8] = { -1 };

	id = netbufReadU8(src);
	outmoveack = netbufReadU32(src);
	netbufReadPlayerMove(src, &newmove);
	if (newmove.ucmd & UCMD_FL_FORCEMASK) {
		netbufReadRooms(src, newrooms, ARRAYCOUNT(newrooms));
	}

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (id >= NET_MAX_CLIENTS) {
		sysLogPrintf(LOG_WARNING, "NET: PlayerMove: client id %u out of range", id);
		return 1;
	}

	struct netclient *movecl = &g_NetClients[id];
	if (movecl->state < CLSTATE_GAME) {
		return src->error;
	}

	if (!netPlayerMoveCommit(movecl, &newmove, outmoveack)) {
		return src->error;
	}

	if (movecl == g_NetLocalClient && (newmove.ucmd & UCMD_FL_FORCEMASK)) {
		// server wants to teleport us
		if (movecl->player && movecl->player->prop) {
			chrSetPos(movecl->player->prop->chr, &newmove.pos, newrooms, newmove.angles[0], (newmove.ucmd & UCMD_FL_FORCEGROUND) != 0);
		}
	}

	return src->error;
}
