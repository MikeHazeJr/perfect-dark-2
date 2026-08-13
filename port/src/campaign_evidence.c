#include <stdio.h>
#include <string.h>

#include "campaign_evidence.h"
#include "save_atomic.h"

static void evidenceSetError(char *error, s32 capacity, const char *message)
{
	if (!error || capacity <= 0) {
		return;
	}
	strncpy(error, message ? message : "campaign evidence write failed",
		(size_t)capacity - 1u);
	error[capacity - 1] = '\0';
}

static void evidenceWriteJsonString(FILE *stream, const char *value)
{
	const unsigned char *cursor = (const unsigned char *)(value ? value : "");
	fputc('"', stream);
	while (*cursor) {
		switch (*cursor) {
		case '"': fputs("\\\"", stream); break;
		case '\\': fputs("\\\\", stream); break;
		case '\b': fputs("\\b", stream); break;
		case '\f': fputs("\\f", stream); break;
		case '\n': fputs("\\n", stream); break;
		case '\r': fputs("\\r", stream); break;
		case '\t': fputs("\\t", stream); break;
		default:
			if (*cursor < 0x20u) {
				fprintf(stream, "\\u%04x", (unsigned int)*cursor);
			} else {
				fputc(*cursor, stream);
			}
			break;
		}
		cursor++;
	}
	fputc('"', stream);
}

static u16 evidencePersistedBesttime(const campaign_run_t *run,
	const campaign_evidence_options_t *options,
	s32 plan_index)
{
	if (!options->persisted_besttimes
			|| plan_index < 0
			|| plan_index >= options->persisted_besttime_count
			|| plan_index >= run->mission_count) {
		return 0;
	}
	return options->persisted_besttimes[plan_index];
}

s32 campaignEvidenceWrite(const campaign_run_t *run,
	const campaign_evidence_options_t *options,
	char *error,
	s32 error_capacity)
{
	save_atomic_file_t transaction;
	FILE *stream;
	s32 i;

	if (error && error_capacity > 0) {
		error[0] = '\0';
	}
	if (!run || !options || !options->path || !options->path[0]
			|| !options->status || !options->status[0]
			|| run->mission_count < 0
			|| run->mission_count > CAMPAIGN_RUN_MAX_MISSIONS) {
		evidenceSetError(error, error_capacity, "invalid campaign evidence input");
		return -1;
	}

	if (saveAtomicBegin(&transaction, options->path) != 0) {
		evidenceSetError(error, error_capacity, "could not create evidence candidate");
		return -1;
	}
	stream = saveAtomicStream(&transaction);

	fputs("{\n  \"schema\": \"pd2.campaign.release-evidence\",\n", stream);
	fputs("  \"version\": 1,\n  \"status\": ", stream);
	evidenceWriteJsonString(stream, options->status);
	fputs(",\n  \"mode\": ", stream);
	evidenceWriteJsonString(stream, options->verify_only ? "persisted_verify" : "live_run");
	fputs(",\n  \"phase\": ", stream);
	evidenceWriteJsonString(stream,
		options->verify_only && run->failure == CAMPAIGN_RUN_FAILURE_NONE
			? "persisted_verified" : campaignRunPhaseName(run->phase));
	fputs(",\n  \"failure\": ", stream);
	evidenceWriteJsonString(stream, campaignRunFailureName(run->failure));
	fputs(",\n  \"failure_detail\": ", stream);
	evidenceWriteJsonString(stream, run->failure_detail);
	fputs(",\n  \"profile\": ", stream);
	evidenceWriteJsonString(stream, run->profile);
	fprintf(stream,
		",\n  \"difficulty\": %d,\n  \"start_solo_index\": %d,\n"
		"  \"final_solo_index\": %d,\n  \"mission_count\": %d,\n"
		"  \"completed_count\": %d,\n  \"credits_verified\": %s,\n"
		"  \"missions\": [\n",
		run->difficulty, run->start_solo_index, run->final_solo_index,
		run->mission_count, run->completed_count,
		run->credits_verified ? "true" : "false");

	for (i = 0; i < run->mission_count; i++) {
		const campaign_run_mission_t *mission = &run->missions[i];
		fprintf(stream, "    {\"plan_index\": %d, \"expected_stage_id\": ", i);
		evidenceWriteJsonString(stream, run->expected_ids[i]);
		fprintf(stream,
			", \"solo_index\": %d, \"runtime_stagenum\": %d,"
			" \"difficulty\": %d, \"loaded\": %s, \"objective_count\": %d,"
			" \"active_objective_count\": %d, \"loaded_social_in_match\": %s,"
			" \"completed\": %s, \"completed_objective_count\": %d,"
			" \"completion_social_in_match\": %s, \"save_serial\": %u,"
			" \"save_result\": %d, \"besttime\": %u,"
			" \"persisted_besttime\": %u, \"routed\": %s,"
			" \"next_unlocked\": %s, \"route_is_credits\": %s,"
			" \"route_to\": ",
			mission->solo_index, mission->runtime_stagenum, mission->difficulty,
			mission->loaded ? "true" : "false", mission->objective_count,
			mission->active_objective_count,
			mission->loaded_social_in_match ? "true" : "false",
			mission->completed ? "true" : "false",
			mission->completed_objective_count,
			mission->completion_social_in_match ? "true" : "false",
			(unsigned int)mission->save_serial, mission->save_result,
			(unsigned int)mission->besttime,
			(unsigned int)evidencePersistedBesttime(run, options, i),
			mission->routed ? "true" : "false",
			mission->next_unlocked ? "true" : "false",
			mission->route_is_credits ? "true" : "false");
		evidenceWriteJsonString(stream, mission->route_to);
		fprintf(stream, "}%s\n", i + 1 < run->mission_count ? "," : "");
	}
	fputs("  ]\n}\n", stream);

	if (ferror(stream)) {
		saveAtomicAbort(&transaction);
		evidenceSetError(error, error_capacity, "campaign evidence stream write failed");
		return -1;
	}
	if (saveAtomicCommit(&transaction) != 0) {
		evidenceSetError(error, error_capacity, "campaign evidence commit failed");
		return -1;
	}
	return 0;
}
