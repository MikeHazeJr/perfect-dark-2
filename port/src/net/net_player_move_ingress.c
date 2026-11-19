/* Production CLC/SVC movement ingress, also linked by the C test fixture. */
#include "types.h"
#include "platform.h"
#include "data.h"
#include "bss.h"
#include "game/chraction.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "grid_match_runtime.h"
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

static bool netmsgAcceptMoveRoute(const struct netclient *srccl, s32 grid, u32 epoch)
{
	const u32 active_epoch = netmsgGridGameplayEpoch();
	if (!grid) return active_epoch == 0;
	return epoch != 0 && epoch == active_epoch && netmsgGridMovePeerReady(srccl);
}

static void netmsgGridResolveMoveSelection(struct netplayermove *move, u16 ref)
{
	s32 weapon;
	move->weaponnum = -1;
	if (!(move->ucmd & UCMD_SELECT)) return;
	if (ref == NET_GRID_WEAPON_REF_NO_SELECTION || !netmsgGridDecodeWeaponRef(ref, &weapon)
			|| weapon < WEAPON_NONE || weapon >= WEAPON_CUSTOM_END) {
		move->ucmd &= ~(UCMD_SELECT | UCMD_SELECT_DUAL);
		return;
	}
	move->weaponnum = (s8)weapon;
}

u32 netmsgClcMoveRead(struct netbuf *src, struct netclient *srccl, s32 grid)
{
	struct netplayermove newmove = {0};
	u16 weapon_ref = NET_GRID_WEAPON_REF_NO_SELECTION;
	const u32 epoch = grid ? netbufReadU32(src) : 0;
	const u32 outmoveack = netbufReadU32(src);
	if (grid) {
		const u32 error = netbufReadGridPlayerMove(src, &newmove, &weapon_ref);
		if (error) return error;
	} else {
		netbufReadPlayerMove(src, &newmove);
	}
	if (src->error) {
		return src->error;
	}

	if (srccl->state != CLSTATE_GAME) {
		// silently ignore
		return src->error;
	}
	if (!netmsgAcceptMoveRoute(srccl, grid, epoch)) return 0;

	if ((newmove.ucmd & UCMD_VEHICLE_PC_INTENT)
			&& !(newmove.ucmd & UCMD_ACTIVATE)) {
		sysLogPrintf(LOG_WARNING,
			"NET: rejected CLC_MOVE vehicle PC intent without activation from client %u",
			(unsigned)srccl->id);
		return 1;
	}

	if (grid) netmsgGridResolveMoveSelection(&newmove, weapon_ref);
	if ((newmove.ucmd & UCMD_SELECT) && newmove.weaponnum == WEAPON_NONE) {
		/* A dead player's gun switches to none, and every move it sends until
		 * it respawns carries that select (49 rejections in one death). The
		 * host makes the death itself; drop the select without a warning. */
		newmove.weaponnum = -1;
		newmove.ucmd &= ~(UCMD_SELECT | UCMD_SELECT_DUAL);
	} else if ((newmove.ucmd & UCMD_SELECT) && newmove.weaponnum >= 0) {
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
		if (srccl->reconnect_gameplay_witness_pending && srccl->player && srccl->player->prop) {
			const bool fiesta = grid && gridMatchRuntimeFiestaActive();
			const bool gameplay = fiesta
				? (!srccl->player->isdead && (newmove.movespeed[0] != 0 || newmove.movespeed[1] != 0))
				: (newmove.ucmd & UCMD_FIRE) != 0;
			if (gameplay) {
				srccl->reconnect_gameplay_witness_pending = false;
				sysLogPrintf(LOG_NOTE,
					"NET.RECONNECT.GAMEPLAY client=%u player=%u room=%u prop=%u move_tick=%u %s=1 server_accepted=1",
					(unsigned)srccl->id, (unsigned)srccl->playernum,
					(unsigned)srccl->room_id, (unsigned)srccl->player->prop->syncid,
					(unsigned)newmove.tick, fiesta ? "move" : "fire");
			}
		}
	}

	return src->error;
}

u32 netmsgSvcPlayerMoveRead(struct netbuf *src, struct netclient *srccl, s32 grid)
{
	u8 id = 0;
	u32 outmoveack = 0;
	struct netplayermove newmove = {0};
	RoomNum newrooms[8] = { -1 };
	u16 weapon_ref = NET_GRID_WEAPON_REF_NO_SELECTION;
	const u32 epoch = grid ? netbufReadU32(src) : 0;

	id = netbufReadU8(src);
	outmoveack = netbufReadU32(src);
	if (grid) {
		const u32 error = netbufReadGridPlayerMove(src, &newmove, &weapon_ref);
		if (error) return error;
	} else {
		netbufReadPlayerMove(src, &newmove);
	}
	if (newmove.ucmd & UCMD_FL_FORCEMASK) {
		netbufReadRooms(src, newrooms, ARRAYCOUNT(newrooms));
	}

	if (src->error || srccl->state < CLSTATE_GAME) {
		return src->error;
	}
	if (!netmsgAcceptMoveRoute(srccl, grid, epoch)) return 0;

	if (id >= NET_MAX_CLIENTS) {
		sysLogPrintf(LOG_WARNING, "NET: PlayerMove: client id %u out of range", id);
		return 1;
	}

	struct netclient *movecl = &g_NetClients[id];
	if (movecl->state < CLSTATE_GAME) {
		return src->error;
	}
	if (grid) {
		/* The authority may position a preserved peer from the reconnect
		 * snapshot before that peer returns to the game. */
		if (movecl->state != CLSTATE_GAME || !movecl->player
				|| (movecl->flags & CLFLAG_SPECTATOR)) return 0;
		netmsgGridResolveMoveSelection(&newmove, weapon_ref);
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
