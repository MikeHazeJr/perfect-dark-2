#ifndef _IN_SCENARIO_SOURCE_RUNTIME_H
#define _IN_SCENARIO_SOURCE_RUNTIME_H

#include <PR/ultratypes.h>

#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

struct objective;

typedef struct scenario_source_objective_operand {
	u8 type;
	u32 stage_flag_mask;
	s32 tag_id;
	s32 pad;
	s32 match_value;
	s32 initial_status;
	s32 runtime_status;
	s32 runtime_status_valid;
	s32 runtime_object_state_valid;
	s32 runtime_object_present;
	s32 runtime_object_healthy;
	s32 runtime_object_held_by_player;
} scenario_source_objective_operand_t;

const asset_entry_t *scenarioSourceFindEntryForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp);
s32 scenarioSourceActivateGraphsForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp);
s32 scenarioSourceLevelGraphCheckPadRoom(s32 pad, s32 room,
	const char *reason, s32 *out_matches);
s32 scenarioSourceSetupGraphRecordBehaviorLink(u8 type, s32 record_index,
	s32 target0, s32 target1, s32 target2, s32 aux0, s32 aux1);
s32 scenarioSourceObjectiveGraphIsActive(void);
s32 scenarioSourceObjectiveGraphRecordInsert(const struct objective *objective);
s32 scenarioSourceObjectiveGraphRecordCriterionStatus(const void *criteria,
	s32 status);
s32 scenarioSourceObjectiveGraphRecordObjectState(s32 tag_id,
	s32 present, s32 healthy, s32 held_by_player);
s32 scenarioSourceObjectiveGraphRecordStageFlags(u32 flags);
s32 scenarioSourceObjectiveGraphHasStageFlag(u32 flag, s32 *out_has_flag);
s32 scenarioSourceMissionGraphRecordPhase(const char *phase,
	const char *reason);
s32 scenarioSourceObjectiveGraphGetCriterionCount(s32 index);
s32 scenarioSourceObjectiveGraphGetCriterionType(s32 index,
	s32 criterion_index, u8 *out_type);
s32 scenarioSourceObjectiveGraphGetCriterionOperand(s32 index,
	s32 criterion_index, scenario_source_objective_operand_t *out_operand);
s32 scenarioSourceObjectiveGraphReportRuntimeMismatch(s32 index,
	const char *action, const char *reason);
s32 scenarioSourceObjectiveGraphRecordEvaluate(s32 index, s32 criteria_count);
s32 scenarioSourceObjectiveGraphRecordCheck(s32 index, s32 status);

u8 *scenarioSourceLoadPadsForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size);
u8 *scenarioSourceLoadSetupForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size);
u8 *scenarioSourceLoadTilesForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size, s32 *out_rooms, s32 *out_tiles);

#ifdef __cplusplus
}
#endif

#endif
