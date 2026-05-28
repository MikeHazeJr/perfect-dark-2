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
bool objectiveIsAllComplete(void);
/** Dev-only helper: mark all objectives complete until the current mission's
 *  objective state is reset. Returns the number of difficulty-active
 *  objectives in the loaded mission. */
s32 objectivesDebugCompleteCurrentMission(void);
void objectivesDisableChecking(void);
void objectivesShowHudmsg(char *buffer, s32 hudmsgtype);
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
