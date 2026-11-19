#ifndef IN_GAME_OBJECTIVES_H
#define IN_GAME_OBJECTIVES_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

void objectivesStop(void);

void objectivesReset(void);
void tagInsert(struct tag *tag);
void briefingInsert(struct briefingobj *obj);
void objectiveInsert(struct objective *objective);
void objectiveAddRoomEnteredCriteria(struct criteria_roomentered *criteria);
void objectiveAddThrowInRoomCriteria(struct criteria_throwinroom *criteria);
void objectiveAddHolographCriteria(struct criteria_holograph *criteria);

u32 xorBaffbeff(u32 value);
u32 xorBabeffff(u32 value);
u32 xorBoobless(u32 value);
void tagsReset(void);
s32 objGetTagNum(struct defaultobj *obj);
s32 objectiveGetCount(void);
u32 objectiveGetDifficultyBits(s32 index);
s32 objectiveCheck(s32 index);
/* Campaign runner: the graph and the native answer for one objective, without
 * recording either. False when no mission graph evaluates it. */
bool objectiveGraphParity(s32 index, s32 *out_graph, s32 *out_native);
bool objectiveIsAllComplete(void);
/** Dev-only helper: mark all objectives complete until the current mission's
 *  objective state is reset. Returns the number of difficulty-active
 *  objectives in the loaded mission. */
s32 objectivesDebugCompleteCurrentMission(void);
void objectivesDisableChecking(void);
void objectivesShowHudmsg(char *buffer, s32 hudmsgtype);
/** The original "Objective N: <status>" message; `number` is 1-based among the
 *  objectives that apply at this difficulty. The mission graph module
 *  og.hud.objective_status_message runs it. */
void objectivesShowStatusMessage(s32 number, s32 status);
void objectivesCheckAll(void);
void objectiveCheckRoomEntered(s32 currentroom);
void objectiveCheckThrowInRoom(s32 arg0, RoomNum *requiredrooms);
void objectiveCheckHolograph(f32 sqdist);
void objectiveRecordObjectState(struct defaultobj *obj);
void objectiveRecordPropState(struct prop *prop);
struct prop *chopperGetTargetProp(struct chopperobj *heli);
struct defaultobj *objFindByTagId(s32 tag_id);
struct tag *tagFindById(s32 tag_id);

#endif
