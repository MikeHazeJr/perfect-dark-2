/**
 * netmenu.c -- Network multiplayer menus (PD native menu system).
 *
 * Architecture: Dedicated-server-only model.
 * - Clients NEVER host. All multiplayer goes through a dedicated server.
 * - Local play (splitscreen, solo) uses NETMODE_NONE and is unaffected.
 * - The "Multiplayer" menu provides: Server Browser, Direct IP, Recent Servers.
 * - Once connected, the lobby (pdgui_menu_lobby.cpp) handles game setup.
 *
 * Legacy host menus have been removed. The co-op configuration dialog is
 * retained for use by the lobby leader (runs on client, sends to server).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "data.h"
#include "types.h"
#include "game/mainmenu.h"
#include "game/menu.h"
#include "game/gamefile.h"
#include "video.h"
#include "input.h"
#include "config.h"
#include "mpsetups.h"
#include "bss.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "game/options.h"
#include "game/lang.h"
#include "game/lv.h"
#include "game/title.h"
#include "game/mplayer/mplayer.h"
#include "net/netmsg.h"
#include "system.h"
#include "lib/vi.h"
#include "romdata.h"
#include "connectcode.h"

/* Non-static so ImGui network menus can access them */
s32 g_NetMenuMaxPlayers = NET_MAX_CLIENTS;
s32 g_NetMenuPort = NET_DEFAULT_PORT;
char g_NetJoinAddr[NET_MAX_ADDR + 1];
static s32 g_NetJoinAddrPtr = 0;

/* ========================================================================
 * Co-op configuration dialog (used by lobby leader, no local server)
 *
 * In the dedicated-server model, the lobby leader opens this dialog to
 * configure a co-op mission. The settings are sent to the server via
 * network messages. The dialog does NOT start a local server.
 * ======================================================================== */

#define NUM_COOP_STAGES 21

static s32 g_NetCoopStageIndex = 0;
static s32 g_NetCoopDiffIndex = 0;

static const char *g_DifficultyNames[] = { "Agent", "Special Agent", "Perfect Agent" };

MenuItemHandlerResult menuhandlerCoopStage(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GETOPTIONCOUNT:
		data->dropdown.value = NUM_COOP_STAGES;
		break;
	case MENUOP_GETOPTIONTEXT:
		if (data->dropdown.value >= 0 && data->dropdown.value < NUM_COOP_STAGES) {
			return (uintptr_t)langGet(g_SoloStages[data->dropdown.value].name3);
		}
		return (uintptr_t)"???";
	case MENUOP_GETSELECTEDINDEX:
		data->dropdown.value = g_NetCoopStageIndex;
		break;
	case MENUOP_SET:
		if (data->dropdown.value >= 0 && data->dropdown.value < NUM_COOP_STAGES) {
			g_NetCoopStageIndex = data->dropdown.value;
			g_MissionConfig.stagenum = g_SoloStages[g_NetCoopStageIndex].stagenum;
			g_MissionConfig.stageindex = g_NetCoopStageIndex;
		}
		break;
	}
	return 0;
}

MenuItemHandlerResult menuhandlerNetCoopDifficulty(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GETOPTIONCOUNT:
		data->dropdown.value = 3;
		break;
	case MENUOP_GETOPTIONTEXT:
		if (data->dropdown.value >= 0 && data->dropdown.value < 3) {
			return (uintptr_t)g_DifficultyNames[data->dropdown.value];
		}
		return (uintptr_t)"???";
	case MENUOP_GETSELECTEDINDEX:
		data->dropdown.value = g_NetCoopDiffIndex;
		break;
	case MENUOP_SET:
		if (data->dropdown.value >= 0 && data->dropdown.value < 3) {
			g_NetCoopDiffIndex = data->dropdown.value;
			g_MissionConfig.difficulty = g_NetCoopDiffIndex;
			g_NetCoopDifficulty = g_NetCoopDiffIndex;
		}
		break;
	}
	return 0;
}

MenuItemHandlerResult menuhandlerNetCoopFriendlyFire(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GET:
		data->checkbox.value = g_NetCoopFriendlyFire;
		break;
	case MENUOP_SET:
		g_NetCoopFriendlyFire = data->checkbox.value ? 1 : 0;
		break;
	}
	return 0;
}

MenuItemHandlerResult menuhandlerNetCoopRadar(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GET:
		data->checkbox.value = g_NetCoopRadar;
		break;
	case MENUOP_SET:
		g_NetCoopRadar = data->checkbox.value ? 1 : 0;
		break;
	}
	return 0;
}

static MenuItemHandlerResult menuhandlerCoopCharacter(s32 operation, struct menuitem *item, union handlerdata *data)
{
	/* Character body selection for co-op — uses local player config (index 0) */
	switch (operation) {
	case MENUOP_GETOPTIONCOUNT:
		data->dropdown.value = ARRAYCOUNT(g_MpBodies) + 1;
		break;
	case MENUOP_GETOPTIONTEXT:
		if (data->dropdown.value == 0) {
			return (uintptr_t)"Default (Joanna)";
		}
		if (data->dropdown.value > 0 && data->dropdown.value <= (s32)ARRAYCOUNT(g_MpBodies)) {
			return (uintptr_t)mpGetBodyName(data->dropdown.value - 1);
		}
		return (uintptr_t)"???";
	case MENUOP_GETSELECTEDINDEX: {
		u8 mpbody = g_PlayerConfigsArray[0].base.mpbodynum;
		u8 mphead = g_PlayerConfigsArray[0].base.mpheadnum;
		data->dropdown.value = (mpbody == 0 && mphead == 0) ? 0 : (mpbody + 1);
		break;
	}
	case MENUOP_SET:
		if (data->dropdown.value == 0) {
			g_PlayerConfigsArray[0].base.mpbodynum = 0;
			g_PlayerConfigsArray[0].base.mpheadnum = 0;
		} else if (data->dropdown.value > 0 && data->dropdown.value <= (s32)ARRAYCOUNT(g_MpBodies)) {
			s32 mpbodynum = data->dropdown.value - 1;
			g_PlayerConfigsArray[0].base.mpbodynum = mpbodynum;
			g_PlayerConfigsArray[0].base.mpheadnum = mpGetMpheadnumByMpbodynum(mpbodynum);
		}
		sysLogPrintf(LOG_NOTE, "NET: co-op character set: body=%u head=%u",
			g_PlayerConfigsArray[0].base.mpbodynum, g_PlayerConfigsArray[0].base.mpheadnum);
		/* Notify server of updated settings */
		if (g_NetMode == NETMODE_CLIENT) {
			netClientSettingsChanged();
		}
		break;
	}
	return 0;
}

static MenuItemHandlerResult menuhandlerCoopConfigBack(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		menuPopDialog();
	}
	return 0;
}

/* Co-op start: lobby leader sends CLC_LOBBY_START to the dedicated server.
 * The server validates the sender is the leader, then starts the mission.
 * Only enabled when connected as a client and in CLSTATE_LOBBY. */
static MenuItemHandlerResult menuhandlerCoopConfigStart(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		if (g_NetMode == NETMODE_CLIENT && g_NetLocalClient &&
		    g_NetLocalClient->state >= CLSTATE_LOBBY) {
			u8 stagenum = (u8)g_SoloStages[g_NetCoopStageIndex].stagenum;
			u8 difficulty = (u8)g_NetCoopDiffIndex;
			/* Determine game mode: co-op or counter-op.
			 * Counter-op is selected when menuhandler sets g_NetGameMode = 2.
			 * Default to co-op (1). */
			u8 gamemode = (g_NetGameMode == 2) ? 2 : 1;

			sysLogPrintf(LOG_NOTE, "NET: lobby leader requesting co-op start "
				"(stage=%d stagenum=0x%02x diff=%d mode=%d)",
				g_NetCoopStageIndex, stagenum, difficulty, gamemode);

			netmsgClcLobbyStartWrite(&g_NetLocalClient->out,
				gamemode, stagenum, difficulty);

			/* Pop the config dialog — we're done configuring */
			menuPopDialog();
		}
	}
	if (operation == MENUOP_CHECKDISABLED) {
		/* Disabled when not connected to a server */
		if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient ||
		    g_NetLocalClient->state < CLSTATE_LOBBY) {
			return true;
		}
		return false;
	}
	return 0;
}

struct menuitem g_NetCoopHostMenuItems[] = {
	{
		MENUITEMTYPE_DROPDOWN,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Mission",
		0,
		menuhandlerCoopStage,
	},
	{
		MENUITEMTYPE_DROPDOWN,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Difficulty",
		0,
		menuhandlerNetCoopDifficulty,
	},
	{
		MENUITEMTYPE_DROPDOWN,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Character",
		0,
		menuhandlerCoopCharacter,
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
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Friendly Fire\n",
		0,
		menuhandlerNetCoopFriendlyFire,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Radar\n",
		0,
		menuhandlerNetCoopRadar,
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
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Start Mission\n",
		0,
		menuhandlerCoopConfigStart,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Back\n",
		0,
		menuhandlerCoopConfigBack,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_NetCoopHostMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Co-op Mission Setup",
	g_NetCoopHostMenuItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_STARTSELECTS | MENUDIALOGFLAG_IGNOREBACK,
	NULL,
};

/* ========================================================================
 * Join / Connect flow
 * ======================================================================== */

static const char *menutextJoinAddress(struct menuitem *item)
{
	static char tmp[256 + 1];
	if (item && item->flags & MENUITEMFLAG_SELECTABLE_CENTRE) {
		if (g_NetMode == NETMODE_NONE) {
			snprintf(tmp, sizeof(tmp), "%s_\n", g_NetJoinAddr);
		} else if (g_NetLocalClient->state == CLSTATE_CONNECTING) {
			snprintf(tmp, sizeof(tmp), "Connecting to %s...\n", g_NetJoinAddr);
		} else if (g_NetLocalClient->state == CLSTATE_AUTH) {
			snprintf(tmp, sizeof(tmp), "Authenticating with %s...\n", g_NetJoinAddr);
		} else if (g_NetLocalClient->state == CLSTATE_LOBBY) {
			snprintf(tmp, sizeof(tmp), "Connected to server\n");
		}
	} else {
		snprintf(tmp, sizeof(tmp), "%s\n", g_NetJoinAddr);
	}
	return tmp;
}

static MenuItemHandlerResult menuhandlerJoining(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (inputKeyPressed(VK_ESCAPE)) {
		netDisconnect();
		menuPopDialog();
		return 0;
	}

	/* Auto-close the "Connecting..." dialog once we reach the lobby.
	 * Pop the entire join dialog stack and let the lobby overlay take over. */
	if (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_LOBBY) {
		sysLogPrintf(LOG_NOTE, "NET: client reached lobby, closing join dialogs");
		menuPopDialog(); /* Pop JoiningDialog */
		menuPopDialog(); /* Pop MultiplayerMenuDialog */
		return 0;
	}

	/* Auto-close if disconnected (connection failed / timed out) */
	if (g_NetMode == NETMODE_NONE) {
		sysLogPrintf(LOG_NOTE, "NET: disconnected while joining, closing dialog");
		menuPopDialog();
		return 0;
	}

	return 0;
}

static MenuItemHandlerResult menuhandlerJoinCharacter(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GETOPTIONCOUNT:
		data->dropdown.value = ARRAYCOUNT(g_MpBodies) + 1;
		break;
	case MENUOP_GETOPTIONTEXT:
		if (data->dropdown.value == 0) {
			return (uintptr_t)"Default (Joanna)";
		}
		if (data->dropdown.value > 0 && data->dropdown.value <= (s32)ARRAYCOUNT(g_MpBodies)) {
			return (uintptr_t)mpGetBodyName(data->dropdown.value - 1);
		}
		return (uintptr_t)"???";
	case MENUOP_GETSELECTEDINDEX: {
		u8 mpbody = g_PlayerConfigsArray[0].base.mpbodynum;
		u8 mphead = g_PlayerConfigsArray[0].base.mpheadnum;
		data->dropdown.value = (mpbody == 0 && mphead == 0) ? 0 : (mpbody + 1);
		break;
	}
	case MENUOP_SET:
		if (data->dropdown.value == 0) {
			g_PlayerConfigsArray[0].base.mpbodynum = 0;
			g_PlayerConfigsArray[0].base.mpheadnum = 0;
		} else if (data->dropdown.value > 0 && data->dropdown.value <= (s32)ARRAYCOUNT(g_MpBodies)) {
			s32 mpbodynum = data->dropdown.value - 1;
			g_PlayerConfigsArray[0].base.mpbodynum = mpbodynum;
			g_PlayerConfigsArray[0].base.mpheadnum = mpGetMpheadnumByMpbodynum(mpbodynum);
		}
		sysLogPrintf(LOG_NOTE, "NET: client character set: body=%u head=%u",
			g_PlayerConfigsArray[0].base.mpbodynum, g_PlayerConfigsArray[0].base.mpheadnum);
		netClientSettingsChanged();
		break;
	case MENUOP_CHECKHIDDEN:
		if (!g_NetLocalClient || g_NetLocalClient->state < CLSTATE_LOBBY) {
			return true;
		}
		if (g_NetGameMode == NETGAMEMODE_MP) {
			return true;
		}
		return false;
	}
	return 0;
}

struct menuitem g_NetJoiningMenuItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		(uintptr_t)&menutextJoinAddress,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_DROPDOWN,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Character",
		0,
		menuhandlerJoinCharacter,
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
		MENUITEMFLAG_SELECTABLE_CENTRE | MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"ESC to abort\n",
		0,
		menuhandlerJoining,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_NetJoiningDialog = {
	MENUDIALOGTYPE_SUCCESS,
	(uintptr_t)"Connecting...",
	g_NetJoiningMenuItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_IGNOREBACK | MENUDIALOGFLAG_STARTSELECTS,
	NULL,
};

/* ========================================================================
 * Address entry
 * ======================================================================== */

static MenuItemHandlerResult menuhandlerEnterJoinAddress(s32 operation, struct menuitem *item, union handlerdata *data);

struct menuitem g_NetJoinAddressMenuItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		(uintptr_t)&menutextJoinAddress,
		0,
		NULL,
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
		MENUITEMFLAG_SELECTABLE_CENTRE | MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"ESC to return\n",
		0,
		menuhandlerEnterJoinAddress,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_NetJoinAddressDialog = {
	MENUDIALOGTYPE_SUCCESS,
	(uintptr_t)"Enter Address",
	g_NetJoinAddressMenuItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_IGNOREBACK | MENUDIALOGFLAG_STARTSELECTS,
	NULL,
};

static s32 g_NetJoinAddrEditing = 0;

static MenuItemHandlerResult menuhandlerEnterJoinAddress(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (!menuIsDialogOpen(&g_NetJoinAddressDialog)) {
		return 0;
	}

	if (inputTextHandler(g_NetJoinAddr, NET_MAX_ADDR, &g_NetJoinAddrPtr, false) < 0) {
		inputStopTextInput();
		menuPopDialog();
	}

	return 0;
}

static MenuItemHandlerResult menuhandlerJoinAddress(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		if (!g_NetJoinAddrEditing) {
			inputClearLastKey();
			inputClearLastTextChar();
			inputStartTextInput();
			g_NetJoinAddrPtr = strlen(g_NetJoinAddr);
			g_NetJoinAddrEditing = 1;
		}
	}

	if (g_NetJoinAddrEditing) {
		s32 result = inputTextHandler(g_NetJoinAddr, NET_MAX_ADDR, &g_NetJoinAddrPtr, false);
		if (result < 0) {
			inputStopTextInput();
			g_NetJoinAddrEditing = 0;
		}
	}

	return 0;
}

MenuItemHandlerResult menuhandlerJoinStart(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		if (g_NetJoinAddr[0] != '\0') {
			/* Detect connect code (contains alpha chars) vs raw IP */
			s32 isCode = 0;
			for (const char *ch = g_NetJoinAddr; *ch; ch++) {
				if ((*ch >= 'A' && *ch <= 'Z') || (*ch >= 'a' && *ch <= 'z')) {
					isCode = 1;
					break;
				}
			}

			if (isCode) {
				u32 ip = 0;
				u16 port = 0;
				if (connectCodeDecode(g_NetJoinAddr, &ip, &port) == 0) {
					char resolved[NET_MAX_ADDR + 1];
					snprintf(resolved, sizeof(resolved), "%u.%u.%u.%u:%u",
					         ip & 0xFF, (ip >> 8) & 0xFF,
					         (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, port);
					if (netStartClient(resolved) == 0) {
						menuPushDialog(&g_NetJoiningDialog);
					}
				}
			} else {
				if (netStartClient(g_NetJoinAddr) == 0) {
					menuPushDialog(&g_NetJoiningDialog);
				}
			}
		}
	}
	if (operation == MENUOP_CHECKDISABLED) {
		return (g_NetJoinAddr[0] == '\0') ? true : false;
	}

	return 0;
}

/* ========================================================================
 * Recent servers list
 * ======================================================================== */

static char g_NetRecentServerStatusText[NET_MAX_RECENT_SERVERS][128];

static const char *menutextRecentServerEntry(struct menuitem *item)
{
	s32 idx = item->param;
	if (idx < 0 || idx >= g_NetNumRecentServers) {
		return "";
	}
	struct netrecentserver *srv = &g_NetRecentServers[idx];
	const char *status = "Offline";
	if (srv->online) {
		if (srv->flags & 1) {
			status = "In Game";
		} else {
			status = "Lobby";
		}
	}
	snprintf(g_NetRecentServerStatusText[idx], sizeof(g_NetRecentServerStatusText[idx]),
		"%s  [%s] %u/%u\n", srv->addr, status, srv->numclients, srv->maxclients);
	return g_NetRecentServerStatusText[idx];
}

static MenuItemHandlerResult menuhandlerRecentServer(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		s32 idx = item->param;
		if (idx >= 0 && idx < g_NetNumRecentServers) {
			strncpy(g_NetJoinAddr, g_NetRecentServers[idx].addr, NET_MAX_ADDR);
			g_NetJoinAddr[NET_MAX_ADDR] = '\0';
			g_NetJoinAddrPtr = strlen(g_NetJoinAddr);
		}
	}
	if (operation == MENUOP_CHECKHIDDEN) {
		return (item->param >= g_NetNumRecentServers) ? true : false;
	}
	return 0;
}

static MenuItemHandlerResult menuhandlerRecentRefresh(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		netQueryRecentServers();
	}
	if (operation == MENUOP_CHECKHIDDEN) {
		return (g_NetNumRecentServers == 0) ? true : false;
	}
	return 0;
}

static void recentServersPopulateText(void)
{
	for (s32 i = 0; i < NET_MAX_RECENT_SERVERS; ++i) {
		if (i < g_NetNumRecentServers) {
			menutextRecentServerEntry(&(struct menuitem){ .param = i });
		} else {
			g_NetRecentServerStatusText[i][0] = '\0';
		}
	}
}

static MenuItemHandlerResult menuhandlerRecentServersDialog(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_OPEN || operation == MENUOP_TICK) {
		recentServersPopulateText();
	}
	return 0;
}

struct menuitem g_NetRecentServersMenuItems[] = {
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t)g_NetRecentServerStatusText[0],
		0,
		menuhandlerRecentServer,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		1,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t)g_NetRecentServerStatusText[1],
		0,
		menuhandlerRecentServer,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		2,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t)g_NetRecentServerStatusText[2],
		0,
		menuhandlerRecentServer,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		3,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t)g_NetRecentServerStatusText[3],
		0,
		menuhandlerRecentServer,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		4,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t)g_NetRecentServerStatusText[4],
		0,
		menuhandlerRecentServer,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		5,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t)g_NetRecentServerStatusText[5],
		0,
		menuhandlerRecentServer,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		6,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t)g_NetRecentServerStatusText[6],
		0,
		menuhandlerRecentServer,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		7,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t)g_NetRecentServerStatusText[7],
		0,
		menuhandlerRecentServer,
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
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Refresh\n",
		0,
		menuhandlerRecentRefresh,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		L_OPTIONS_213, // "Back"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_NetRecentServersMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Server Browser",
	g_NetRecentServersMenuItems,
	(void *)menuhandlerRecentServersDialog,
	MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_STARTSELECTS,
	NULL,
};

/* ========================================================================
 * Main Multiplayer menu (replaces old "Network Game" Host/Join/Back)
 *
 * Layout:
 *   - Server Browser (recent/discovered servers)
 *   - Direct IP connect
 *   - Back
 * ======================================================================== */

extern struct menudialogdef g_NetRecentServersMenuDialog;

MenuItemHandlerResult menuhandlerMultiplayerConnect(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		if (g_NetJoinAddr[0] == '\0') {
			strncpy(g_NetJoinAddr, g_NetLastJoinAddr, NET_MAX_ADDR);
			g_NetJoinAddr[NET_MAX_ADDR] = '\0';
			g_NetJoinAddrPtr = strlen(g_NetJoinAddr);
		}
	}

	return 0;
}

struct menuitem g_NetMenuItems[] = {
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Address:   \n",
		(uintptr_t)&menutextJoinAddress,
		menuhandlerJoinAddress,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Connect\n",
		0,
		menuhandlerJoinStart,
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
		MENUITEMFLAG_SELECTABLE_OPENSDIALOG | MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Server Browser\n",
		0,
		(void *)&g_NetRecentServersMenuDialog,
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
		L_OPTIONS_213, // "Back"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_NetMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Multiplayer",
	g_NetMenuItems,
	NULL,
	MENUDIALOGFLAG_MPLOCKABLE | MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_STARTSELECTS,
	NULL,
};

/* ========================================================================
 * In-match controls dialog (pause menu during network games)
 * ======================================================================== */

static MenuItemHandlerResult menuhandlerNetReversePitch(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GET:
		data->checkbox.value = (g_PlayerConfigsArray[0].options & OPTION_FORWARDPITCH) ? 0 : 1;
		break;
	case MENUOP_SET:
		if (data->checkbox.value) {
			g_PlayerConfigsArray[0].options &= ~OPTION_FORWARDPITCH;
		} else {
			g_PlayerConfigsArray[0].options |= OPTION_FORWARDPITCH;
		}
		if (g_NetLocalClient) {
			g_NetLocalClient->settings.options = g_PlayerConfigsArray[0].options;
			netClientSettingsChanged();
		}
		break;
	}
	return 0;
}

static MenuItemHandlerResult menuhandlerNetMouseSpeedX(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GETSLIDER:
		data->slider.value = (s32)(g_PlayerExtCfg[0].mouseaimspeedx * 100.0f);
		break;
	case MENUOP_SET:
		g_PlayerExtCfg[0].mouseaimspeedx = (f32)data->slider.value / 100.0f;
		break;
	}
	return 0;
}

static MenuItemHandlerResult menuhandlerNetMouseSpeedY(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GETSLIDER:
		data->slider.value = (s32)(g_PlayerExtCfg[0].mouseaimspeedy * 100.0f);
		break;
	case MENUOP_SET:
		g_PlayerExtCfg[0].mouseaimspeedy = (f32)data->slider.value / 100.0f;
		break;
	}
	return 0;
}

static MenuItemHandlerResult menuhandlerNetLookAhead(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GET:
		data->checkbox.value = (g_PlayerConfigsArray[0].options & OPTION_LOOKAHEAD) ? 1 : 0;
		break;
	case MENUOP_SET:
		if (data->checkbox.value) {
			g_PlayerConfigsArray[0].options |= OPTION_LOOKAHEAD;
		} else {
			g_PlayerConfigsArray[0].options &= ~OPTION_LOOKAHEAD;
		}
		if (g_NetLocalClient) {
			g_NetLocalClient->settings.options = g_PlayerConfigsArray[0].options;
			netClientSettingsChanged();
		}
		break;
	}
	return 0;
}

struct menuitem g_NetPauseControlsMenuItems[] = {
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Reverse Pitch\n",
		0,
		menuhandlerNetReversePitch,
	},
	{
		MENUITEMTYPE_CHECKBOX,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Look Ahead\n",
		0,
		menuhandlerNetLookAhead,
	},
	{
		MENUITEMTYPE_SLIDER,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Mouse Speed X",
		1000,
		menuhandlerNetMouseSpeedX,
	},
	{
		MENUITEMTYPE_SLIDER,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Mouse Speed Y",
		1000,
		menuhandlerNetMouseSpeedY,
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
		L_OPTIONS_213, // "Back"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_NetPauseControlsMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Controls",
	g_NetPauseControlsMenuItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_STARTSELECTS,
	NULL,
};
