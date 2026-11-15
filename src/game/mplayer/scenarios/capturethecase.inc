/**
 * Capture the Case
 *
 * Each team has a base. The base is highlighted in their team colour and
 * contains a briefcase on the ground. Players must infiltrate another team's
 * base, collect their briefcase and then return it to their own.
 *
 * When a briefcase holder is killed, the briefcase is immediately returned to
 * the team's base.
 *
 * Players cannot score unless their briefcase is at the home base.
 */

extern struct menudialogdef g_ExtGameOptionsMenuDialog;

struct menuitem g_CtcOptionsMenuItems[] = {
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LOCKABLEMINOR,
		L_MPMENU_222, // "One-Hit Kills"
		MPOPTION_ONEHITKILLS,
		menuhandlerMpOneHitKills,
	},
	{
		MENUITEMTYPE_DROPDOWN,
		0,
		MENUITEMFLAG_LOCKABLEMINOR,
		L_MPMENU_223, // "Slow Motion"
		0,
		menuhandlerMpSlowMotion,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LOCKABLEMINOR,
		L_MPMENU_224, // "Fast Movement"
		MPOPTION_FASTMOVEMENT,
		menuhandlerMpCheckboxOption,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LOCKABLEMINOR,
		L_MPMENU_225, // "Display Team"
		MPOPTION_DISPLAYTEAM,
		menuhandlerMpDisplayTeam,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LOCKABLEMINOR,
		L_MPMENU_226, // "No Radar"
		MPOPTION_NORADAR,
		menuhandlerMpCheckboxOption,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LOCKABLEMINOR,
		L_MPMENU_227, // "No Auto-Aim"
		MPOPTION_NOAUTOAIM,
		menuhandlerMpCheckboxOption,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LOCKABLEMINOR,
		L_OPTIONS_493, // "Kills Score"
		MPOPTION_KILLSSCORE,
		menuhandlerMpCheckboxOption,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LOCKABLEMINOR,
		L_MPMENU_236, // "Show on Radar"
		MPOPTION_CTC_SHOWONRADAR,
		menuhandlerMpCheckboxOption,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		L_MPMENU_239, // "Back"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_CtcOptionsMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	L_MPMENU_220, // "Capture Options"
	g_CtcOptionsMenuItems,
	mpOptionsMenuDialog,
	MENUDIALOGFLAG_MPLOCKABLE,
	&g_ExtGameOptionsMenuDialog,
//	NULL,
};

struct weaponobj g_CtcTokenObj0;
struct weaponobj g_CtcTokenObj1;
struct weaponobj g_CtcTokenObj2;
struct weaponobj g_CtcTokenObj3;

void ctcInit(void)
{
	s32 i, j, k;
	g_MpSetup.options |= MPOPTION_TEAMSENABLED;

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam); i++) {
		s32 j;
		g_ScenarioData.ctc.spawnpadsperteam[i].homepad = i;
		g_ScenarioData.ctc.spawnpadsperteam[i].numspawnpads = 0;

		for (j = 0; j < ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam[i].spawnpads); j++) {
			g_ScenarioData.ctc.spawnpadsperteam[i].spawnpads[j] = -1;
		}
	}

	for (i = 0; i != ARRAYCOUNT(g_ScenarioData.ctc.playercountsperteam); i++) {
		g_ScenarioData.ctc.playercountsperteam[i] = 0;
		g_ScenarioData.ctc.teamindexes[i] = -1;
	}

	for (k = 0; k < MAX_MPCHRS; k++) {
		if (mpIsParticipantActive(k)) {
			struct mpchrconfig *mpchr = MPCHR(k);

			while (mpchr->team >= scenarioGetMaxTeams()) {
				mpchr->team -= scenarioGetMaxTeams();
			}
		}
	}
}

s32 ctcNumProps(void)
{
	return 4;
}

void ctcTick(void)
{
	// empty
}

void ctcTickChr(struct chrdata *chr)
{
	if (chr);
}

#define CTC_NUM_BASES ((s32)ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam))
#define CTC_NUM_SPAWNPADS ((s32)ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam[0].spawnpads))

/**
 * PC (B-1126): whether a base has a respawn pad its players can use.
 */
static s32 ctcBaseHasRespawnPad(s32 slot)
{
	struct ctcspawnpadsperteam *base = &g_ScenarioData.ctc.spawnpadsperteam[slot];
	s32 i;

	for (i = 0; i < base->numspawnpads && i < CTC_NUM_SPAWNPADS; i++) {
		if (scenarioPadIsInRoom(base->spawnpads[i])) {
			return true;
		}
	}

	return false;
}

/**
 * PC (B-1126): Capture the Case puts each team's case on its base's home pad
 * (intro CASE) and respawns the team on the base's respawn pads (intro
 * CASERESPAWN). Arenas extracted today carry neither, so every case was
 * created off-world and there was nothing to capture. Those arenas get the
 * missing pads from walkable pads (Mike, 2026-11-15: the same fill-in as
 * Hacker Central), for the bases of the teams in play only:
 * - a base whose home pad cannot hold the case gets the walkable pad farthest
 *   from the other bases in play (on an arena with no usable base at all the
 *   first one is random), so the bases sit apart as on the original arenas;
 * - a base with no usable respawn pad gets the walkable pads nearest its home
 *   pad that are nearer to it than to any other base, up to the six a base
 *   holds, so its players respawn at their own base.
 * Bases the arena provides are left as they are, and an arena with all its
 * pads makes no extra random calls.
 */
static void ctcFillMissingPads(void)
{
	struct ctcspawnpadsperteam *bases = g_ScenarioData.ctc.spawnpadsperteam;
	s16 inplay[ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam)];
	s16 homes[ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam)];
	s16 needhome[ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam)];
	s16 added[ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam)];
	s16 anchors[ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam)];
	s16 anchorslots[ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam)];
	s32 numinplay = 0;
	s32 numhomes = 0;
	s32 numneed = 0;
	s32 numadded = 0;
	s32 numanchors = 0;
	s32 team;
	s32 i;
	s32 j;

	for (team = 0; team < (s32)ARRAYCOUNT(g_ScenarioData.ctc.teamindexes); team++) {
		s32 slot = g_ScenarioData.ctc.teamindexes[team];

		if (g_ScenarioData.ctc.playercountsperteam[team] == 0 || slot < 0 || slot >= CTC_NUM_BASES) {
			continue;
		}

		inplay[numinplay++] = slot;

		if (scenarioPadIsInRoom(bases[slot].homepad)) {
			homes[numhomes++] = bases[slot].homepad;
		} else {
			needhome[numneed++] = slot;
		}
	}

	if (numneed > 0) {
		numadded = scenarioFallbackFarthestPads(added, numneed, homes, numhomes, "CTC");

		for (i = 0; i < numneed; i++) {
			s32 slot = needhome[i];

			if (i < numadded) {
				sysLogPrintf(LOG_NOTE, "CTC: base %d had no usable home pad (pad %d); home pad now %d (B-1126)",
					slot, bases[slot].homepad, added[i]);
				bases[slot].homepad = added[i];
			} else {
				sysLogPrintf(LOG_ERROR, "CTC: base %d has no usable home pad (pad %d) and no walkable pad is left (B-1126)",
					slot, bases[slot].homepad);
			}
		}
	}

	// Respawn pads belong to the nearest base, so every base in play is an anchor
	for (i = 0; i < numinplay; i++) {
		if (scenarioPadIsInRoom(bases[inplay[i]].homepad)) {
			anchorslots[numanchors] = inplay[i];
			anchors[numanchors] = bases[inplay[i]].homepad;
			numanchors++;
		}
	}

	for (i = 0; i < numanchors; i++) {
		s32 slot = anchorslots[i];
		s16 pads[ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam[0].spawnpads)];
		s32 numpads;

		if (ctcBaseHasRespawnPad(slot)) {
			continue;
		}

		numpads = scenarioFallbackNearestPads(pads, CTC_NUM_SPAWNPADS, i, anchors, numanchors, "CTC");

		if (numpads <= 0) {
			sysLogPrintf(LOG_ERROR, "CTC: base %d has no usable respawn pad and none was found near home pad %d (B-1126)",
				slot, bases[slot].homepad);
			continue;
		}

		for (j = 0; j < CTC_NUM_SPAWNPADS; j++) {
			bases[slot].spawnpads[j] = j < numpads ? pads[j] : -1;
		}

		bases[slot].numspawnpads = numpads;

		sysLogPrintf(LOG_NOTE, "CTC: base %d had no usable respawn pad; added %d around home pad %d (B-1126)",
			slot, numpads, bases[slot].homepad);
	}
}

void ctcInitProps(void)
{
	struct mpchrconfig *mpchr;
	struct weaponobj *tmp;
	s32 mpindex;
	u32 stack;
	bool teamsdone[ARRAYCOUNT(g_ScenarioData.ctc.playercountsperteam)];

	struct weaponobj template = {
		256,                    // extrascale
		0,                      // hidden2
		OBJTYPE_WEAPON,         // type
		MODEL_CHRBRIEFCASE,     // modelnum
		0,                      // pad
		OBJFLAG_FALL | OBJFLAG_INVINCIBLE | OBJFLAG_FORCENOBOUNCE,
		OBJFLAG2_IMMUNETOGUNFIRE | OBJFLAG2_IMMUNETOEXPLOSIONS,
		0,                      // flags3
		NULL,                   // prop
		NULL,                   // model
		1, 0, 0,                // realrot
		0, 1, 0,
		0, 0, 1,
		0,                      // hidden
		NULL,                   // geo
		NULL,                   // projectile
		0,                      // damage
		1000,                   // maxdamage
		0xff, 0xff, 0xff, 0x00, // shadecol
		0xff, 0xff, 0xff, 0x00, // nextcol
		0x0fff,                 // floorcol
		0,                      // tiles
		WEAPON_BRIEFCASE2,      // weaponnum
		0,                      // unk5d
		0,                      // unk5e
		FUNC_PRIMARY,           // gunfunc
		0,                      // fadeouttimer60
		-1,                     // dualweaponnum
		-1,                     // timer240
		NULL,                   // dualweapon
	};

	s32 i;
	s32 j;
	s32 k;

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam); i++) {
		for (j = 0; j < ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam[i].spawnpads); j++) {
		}
	}

	for (i = 0; i < ARRAYCOUNT(teamsdone); i++) {
		teamsdone[i] = false;
		g_ScenarioData.ctc.playercountsperteam[i] = 0;
	}

	for (i = 0; i != ARRAYCOUNT(g_ScenarioData.ctc.teamindexes); i++) {
		do {
			g_ScenarioData.ctc.teamindexes[i] = rngRandom() % 4;
		} while (teamsdone[g_ScenarioData.ctc.teamindexes[i]]);

		teamsdone[g_ScenarioData.ctc.teamindexes[i]] = true;
	}

	for (k = 0; k < MAX_MPCHRS; k++) {
		if (mpIsParticipantActive(k)) {
			mpchr = MPCHR(k);

			while (mpchr->team >= scenarioGetMaxTeams()) {
				mpchr->team -= scenarioGetMaxTeams();
			}

#if VERSION >= VERSION_NTSC_1_0
			mpindex = func0f18d0e8(k);

			if (mpindex >= 0) {
				struct chrdata *chr = mpGetChrFromPlayerIndex(mpindex);

				if (chr) {
					chr->team = 1 << mpchr->team;
				}
			}
#else
			if (func0f18d0e8(k) >= 0) {
				struct chrdata *chr = mpGetChrFromPlayerIndex(func0f18d0e8(k));

				if (chr) {
					chr->team = 1 << mpchr->team;
				}
			}
#endif

			g_ScenarioData.ctc.playercountsperteam[mpchr->team]++;
		}
	}

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.playercountsperteam); i++) {
		if (g_ScenarioData.ctc.playercountsperteam[i] == 0) {
			g_ScenarioData.ctc.teamindexes[i] = -1;
		}
	}

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.teamindexes); i++) {
		// empty
	}

	ctcFillMissingPads();

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.tokens); i++) {
		g_ScenarioData.ctc.tokens[i] = NULL;
	}

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.baserooms); i++) {
		g_ScenarioData.ctc.baserooms[i] = -1;
	}

	if (g_ScenarioData.ctc.playercountsperteam[0] != 0) {
		g_CtcTokenObj0 = template;
		tmp = &g_CtcTokenObj0;
		tmp->base.pad = g_ScenarioData.ctc.spawnpadsperteam[g_ScenarioData.ctc.teamindexes[0]].homepad;

		setupPlaceWeapon(tmp, 1000);

		g_ScenarioData.ctc.tokens[0] = tmp->base.prop;

		tmp->base.hidden2 &= ~OBJH2FLAG_CANREGEN;
		tmp->team = 0;

		g_ScenarioData.ctc.baserooms[0] = g_ScenarioData.ctc.tokens[0] ? g_ScenarioData.ctc.tokens[0]->rooms[0] : -1;
	}

	if (g_ScenarioData.ctc.playercountsperteam[1] != 0) {
		g_CtcTokenObj1 = template;
		tmp = &g_CtcTokenObj1;
		tmp->base.pad = g_ScenarioData.ctc.spawnpadsperteam[g_ScenarioData.ctc.teamindexes[1]].homepad;

		setupPlaceWeapon(tmp, 1001);

		g_ScenarioData.ctc.tokens[1] = tmp->base.prop;

		tmp->base.hidden2 &= ~OBJH2FLAG_CANREGEN;
		tmp->team = 1;

		g_ScenarioData.ctc.baserooms[1] = g_ScenarioData.ctc.tokens[1] ? g_ScenarioData.ctc.tokens[1]->rooms[0] : -1;
	}

	if (g_ScenarioData.ctc.playercountsperteam[2] != 0) {
		g_CtcTokenObj2 = template;
		tmp = &g_CtcTokenObj2;
		tmp->base.pad = g_ScenarioData.ctc.spawnpadsperteam[g_ScenarioData.ctc.teamindexes[2]].homepad;

		setupPlaceWeapon(tmp, 1002);

		g_ScenarioData.ctc.tokens[2] = tmp->base.prop;

		tmp->base.hidden2 &= ~OBJH2FLAG_CANREGEN;
		tmp->team = 2;

		g_ScenarioData.ctc.baserooms[2] = g_ScenarioData.ctc.tokens[2] ? g_ScenarioData.ctc.tokens[2]->rooms[0] : -1;
	}

	if (g_ScenarioData.ctc.playercountsperteam[3] != 0) {
		g_CtcTokenObj3 = template;
		tmp = &g_CtcTokenObj3;
		tmp->base.pad = g_ScenarioData.ctc.spawnpadsperteam[g_ScenarioData.ctc.teamindexes[3]].homepad;

		setupPlaceWeapon(tmp, 1003);

		g_ScenarioData.ctc.tokens[3] = tmp->base.prop;

		tmp->base.hidden2 &= ~OBJH2FLAG_CANREGEN;
		tmp->team = 3;

		g_ScenarioData.ctc.baserooms[3] = g_ScenarioData.ctc.tokens[3] ? g_ScenarioData.ctc.tokens[3]->rooms[0] : -1;
	}

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.playercountsperteam); i++) {
		if (g_ScenarioData.ctc.playercountsperteam[i] && g_ScenarioData.ctc.baserooms[i] != -1) {
			roomSetLightOp(g_ScenarioData.ctc.baserooms[i], LIGHTOP_HIGHLIGHT, 0, 0, 0);
		}
	}

	// PC (B-1126): where each team's case went, for logs and smokes
	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.playercountsperteam); i++) {
		s32 slot = g_ScenarioData.ctc.teamindexes[i];

		if (g_ScenarioData.ctc.playercountsperteam[i] == 0 || slot < 0 || slot >= CTC_NUM_BASES) {
			continue;
		}

		sysLogPrintf(g_ScenarioData.ctc.baserooms[i] > 0 ? LOG_NOTE : LOG_ERROR,
			"CTC: team %d case on base %d pad=%d room=%d respawnpads=%d prop=%s",
			i, slot, g_ScenarioData.ctc.spawnpadsperteam[slot].homepad, g_ScenarioData.ctc.baserooms[i],
			g_ScenarioData.ctc.spawnpadsperteam[slot].numspawnpads,
			g_ScenarioData.ctc.tokens[i] ? "ok" : "none");
	}
}

void ctcCalculatePlayerScore(struct mpchrconfig *mpchr, s32 mpchrnum, s32 *score, s32 *deaths)
{
	struct mpchrconfig *loopmpchr;
	s32 i;

	*score = 0;
	*score += mpchr->numpoints * 3;

	if (g_MpSetup.options & MPOPTION_KILLSSCORE) {
		for (i = 0; i != MAX_MPCHRS; i++) {
			if (i == mpchrnum) {
				*score -= mpchr->killcounts[i];
			} else {
				loopmpchr = MPCHR(i);

				if (loopmpchr->team == mpchr->team) {
					*score -= mpchr->killcounts[i];
				} else {
					*score += mpchr->killcounts[i];
				}
			}
		}
	}

	*deaths = mpchr->numdeaths;
}

Gfx *ctcRadarExtra(Gfx *gdl)
{
	if (g_MpSetup.options & MPOPTION_CTC_SHOWONRADAR) {
		s32 i;

		for (i = 0; i < scenarioGetMaxTeams(); i++) {
			if (g_ScenarioData.ctc.tokens[i] &&
					g_ScenarioData.ctc.tokens[i]->type != PROPTYPE_CHR &&
					g_ScenarioData.ctc.tokens[i]->type != PROPTYPE_PLAYER) {
				struct coord dist;
				dist.x = g_ScenarioData.ctc.tokens[i]->pos.x - g_Vars.currentplayer->prop->pos.x;
				dist.y = g_ScenarioData.ctc.tokens[i]->pos.y - g_Vars.currentplayer->prop->pos.y;
				dist.z = g_ScenarioData.ctc.tokens[i]->pos.z - g_Vars.currentplayer->prop->pos.z;
				gdl = radarDrawDot(gdl, g_ScenarioData.ctc.tokens[i], &dist, g_TeamColours[i], 0, 1);
			}
		}
	}

	return gdl;
}

bool ctcRadarChr(Gfx **gdl, struct prop *prop)
{
	s32 i;

	if (g_MpSetup.options & MPOPTION_CTC_SHOWONRADAR) {
		for (i = 0; i < scenarioGetMaxTeams(); i++) {
			if (prop == g_ScenarioData.ctc.tokens[i] &&
					(g_ScenarioData.ctc.tokens[i]->type == PROPTYPE_CHR || g_ScenarioData.ctc.tokens[i]->type == PROPTYPE_PLAYER)) {
				struct coord dist;
				s32 colour = (prop->chr != NULL)
					? g_TeamColours[radarGetTeamIndex(prop->chr->team)]
					: 0x00ff0000;
				dist.x = g_ScenarioData.ctc.tokens[i]->pos.x - g_Vars.currentplayer->prop->pos.x;
				dist.y = g_ScenarioData.ctc.tokens[i]->pos.y - g_Vars.currentplayer->prop->pos.y;
				dist.z = g_ScenarioData.ctc.tokens[i]->pos.z - g_Vars.currentplayer->prop->pos.z;
				*gdl = radarDrawDot(*gdl, g_ScenarioData.ctc.tokens[i], &dist,
						g_TeamColours[i], colour, 1);
				return true;
			}
		}
	}

	return false;
}

bool ctcHighlightProp(struct prop *prop, s32 *colour)
{
	struct defaultobj *obj = prop->obj;

	if (prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_WEAPON || prop->type == PROPTYPE_DOOR) {
		if (obj->type == OBJTYPE_WEAPON) {
			struct weaponobj *weapon = prop->weapon;

			if (weapon->weaponnum == WEAPON_BRIEFCASE2) {
				u32 teamcolour = g_TeamColours[weapon->team];

				colour[0] = teamcolour >> 24 & 0xff;
				colour[1] = teamcolour >> 16 & 0xff;
				colour[2] = teamcolour >> 8 & 0xff;
				colour[3] = 75;

				return true;
			}
		}
	}

	return false;
}

void ctcAddPad(s32 *cmd)
{
	s32 i;

	if (cmd[0] == INTROCMD_CASE) {
		g_ScenarioData.ctc.spawnpadsperteam[cmd[1]].homepad = cmd[2];
	}

	if (cmd[0] == INTROCMD_CASERESPAWN) {
		for (i = 0; i != ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam[cmd[1]].spawnpads); i++) {
			if (g_ScenarioData.ctc.spawnpadsperteam[cmd[1]].spawnpads[i] == -1) {
				g_ScenarioData.ctc.spawnpadsperteam[cmd[1]].spawnpads[i] = cmd[2];
				g_ScenarioData.ctc.spawnpadsperteam[cmd[1]].numspawnpads++;
				return;
			}
		}
	}
}

bool ctcChooseSpawnLocation(f32 arg0, struct coord *pos, RoomNum *rooms, struct prop *prop, f32 *arg4)
{
	struct chrdata *chr = prop->chr;
	s32 index = radarGetTeamIndex(chr->team);

	// PC (B-1126): a team with no base assigned yet (before ctcInitProps, or a
	// team that had no players then) has no slot to read
	if (index < 0 || index >= (s32)ARRAYCOUNT(g_ScenarioData.ctc.teamindexes)
			|| g_ScenarioData.ctc.teamindexes[index] < 0
			|| g_ScenarioData.ctc.teamindexes[index] >= CTC_NUM_BASES) {
		return false;
	}

	if (g_ScenarioData.ctc.spawnpadsperteam[g_ScenarioData.ctc.teamindexes[index]].numspawnpads > 0) {
		*arg4 = playerChooseSpawnLocation(arg0, pos, rooms, prop,
				g_ScenarioData.ctc.spawnpadsperteam[g_ScenarioData.ctc.teamindexes[index]].spawnpads,
				g_ScenarioData.ctc.spawnpadsperteam[g_ScenarioData.ctc.teamindexes[index]].numspawnpads);
		return true;
	}

	return false;
}

s32 ctcGetMaxTeams(void)
{
	return 4;
}

bool ctcIsRoomHighlighted(RoomNum room)
{
	s32 i;

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.teamindexes); i++) {
		if (g_ScenarioData.ctc.baserooms[i] == room && g_ScenarioData.ctc.teamindexes[i] != -1) {
			return true;
		}
	}

	return false;
}

void ctcHighlightRoom(RoomNum roomnum, s32 *arg1, s32 *arg2, s32 *arg3)
{
	s32 i;

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.baserooms); i++) {
		if (g_ScenarioData.ctc.baserooms[i] == roomnum) {
			u32 colour = g_TeamColours[i];
			f32 a = *arg1;
			f32 b = *arg2;
			f32 c = *arg3;

			a *= (s32)((colour >> 24 & 0xff) + 0xff) * (1.0f / 512.0f);
			b *= (s32)((colour >> 16 & 0xff) + 0xff) * (1.0f / 512.0f);
			c *= (s32)((colour >> 8 & 0xff) + 0xff) * (1.0f / 512.0f);

			*arg1 = a;
			*arg2 = b;
			*arg3 = c;
			return;
		}
	}
}
