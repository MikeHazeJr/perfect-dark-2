#ifndef _IN_MISSION_GRAPH_MODULES_H
#define _IN_MISSION_GRAPH_MODULES_H

#include <PR/ultratypes.h>
#include "mission_graph_runner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's native modules for mission and level graphs (og.*) and its
 * native gates (gate.*, object-events.md slice 3), the table the graph
 * runner compiles and fires against. */
s32 missionGraphModuleKnown(const char *kind);
s32 missionGraphModuleRun(const char *kind, const char *node_id,
	const mission_graph_params_t *params, const mission_graph_event_t *event, void *user);

/* object-events.md slice 4: the object a rule event is about
 * (MISSION_GRAPH_EVENT_PICKUP_TOUCHED, _DOOR_USE, _DOOR_APPROACH), and what
 * the rule's modules did with it. The native seam fills prop and user, sets
 * the rule around the fire (Begin returns the rule it nests in, End puts that
 * one back) and reads the results. The rule gates and modules run only
 * inside their own rule events. */
struct prop;
typedef struct mission_graph_object_rule {
	struct prop *prop;  /* the pickup or the door */
	struct prop *user;  /* who touches, uses or approaches it (the player, or the eyespy) */
	s32 tickop;         /* og.pickup.collect: the tick operation propPickupByPlayer gave */
	s32 collected;      /* og.pickup.collect ran */
	s32 activated;      /* og.door.activate or og.door.activate_automatic ran */
	s32 locked_message; /* og.door.locked_message ran */
} mission_graph_object_rule_t;

mission_graph_object_rule_t *missionGraphModulesRuleBegin(mission_graph_object_rule_t *rule);
void missionGraphModulesRuleEnd(mission_graph_object_rule_t *outer);

#ifdef __cplusplus
}
#endif

#endif
