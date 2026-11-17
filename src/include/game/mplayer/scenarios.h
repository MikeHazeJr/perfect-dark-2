#ifndef _IN_GAME_MPLAYER_SCENARIOS_H
#define _IN_GAME_MPLAYER_SCENARIOS_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

extern struct menudialogdef g_MpScenarioMenuDialog;
extern struct menudialogdef g_MpQuickTeamScenarioMenuDialog;

struct mpscenariooverview {
	u16 name;
	u16 shortname;
	u8 requirefeature;
	u8 teamonly;
};

extern struct mpscenariooverview g_MpScenarioOverviews[6];

MenuItemHandlerResult menuhandlerMpOpenOptions(s32 operation, struct menuitem *item, union handlerdata *data);
void scenarioReadSave(struct savebuffer *buffer, u8 version);
void scenarioWriteSave(struct savebuffer *buffer);
void scenarioInit(void);
s32 scenarioNumProps(void);
void scenarioInitProps(void);
void scenarioTick(void);
void scenarioTickChr(struct chrdata *chr);
Gfx *scenarioRadarExtra(Gfx *gdl);
bool scenarioRadarChr(Gfx **gdl, struct prop *prop);
f32 scenarioChooseSpawnLocation(f32 chrradius, struct coord *pos, RoomNum *rooms, struct prop *prop);
s32 scenarioGetMaxTeams(void);
/* PC (finding 8): where a team starts the match (Capture the Case bases). */
s32 scenarioGetTeamSpawnAnchor(s32 team, struct coord *out);
void scenarioHighlightRoom(RoomNum room, s32 *arg1, s32 *arg2, s32 *arg3);

/*
 * PC (T-NETWORKING-011): host-authoritative scenario state. The host, and
 * offline play, runs the scenario rules; a networked client presents the
 * state the host sends in SVC_SCENARIO_STATE instead of deciding it itself.
 * Only scenarios with a body in net/net_scenario_state_wire.h take part; the
 * others still run their rules on every peer.
 */
struct net_scenario_state;

/* Whether this peer decides the scenario: offline play or the listen host. */
s32 scenarioNetIsAuthority(void);

/* B-1209: on the listen host, announce a scenario prop made during the match
 * (SVC_PROP_SPAWN then SVC_PROP_MOVE); no-op offline and on a client. */
void scenarioNetAnnounceProp(struct prop *prop, const char *what);

/* B-1272: the runtime weapon a scenario puts in play as its token (Hacker
 * Central's data uplink, the briefcase of Hold the Briefcase and Capture the
 * Case), or -1. The match manifest must list it (SP-93). */
s32 scenarioTokenWeaponNum(s32 scenario);

/* B-1273: the model the scenario's token prop is drawn with (its template's
 * modelnum), or -1. The match manifest must list it too (SP-93). */
s32 scenarioTokenModelNum(s32 scenario);

/* Whether the running match's scenario has network state (a wire body). */
s32 scenarioNetHasState(void);

/* Host: fills *out with the running scenario's state and returns 1, or
 * returns 0 when the scenario has no network state. *changed is set when the
 * state differs from the previous capture in anything but a running timer
 * (the revision then goes up by one). */
s32 scenarioNetCaptureState(struct net_scenario_state *out, s32 *changed);

/* Client: accepts a decoded state for the running scenario. Returns 1 when
 * it was queued for the next scenario tick, 0 when it was dropped (no match,
 * another scenario, or older than the state already shown). */
s32 scenarioNetReceiveState(const struct net_scenario_state *in);

#endif
