/* Disabled by default; only observer storage is written. Native qualification
 * and numeric/provenance/compute witnesses remain separate acceptance gates. */
#include <ultra64.h>
#include <math.h>
#include <stdio.h>
#include "constants.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "game/surface_loco.h"
#include "lib/anim.h"
#include "swarm_audit_pose.h"

#define AUDIT_AGENT_LIMIT 4
#define AUDIT_INTERVAL_60 60
#define AUDIT_BYTE_LIMIT (512u * 1024u)
#define AUDIT_LINE_SIZE 2048

struct audit_agent {
    struct chrdata *chr;
    struct prop *prop;
    struct model *model;
    s32 chrnum;
    s32 last_interval;
};

static struct audit_agent s_Agents[AUDIT_AGENT_LIMIT];
static s32 s_Stage = -1, s_LastFrame = -1, s_Epoch;
static u32 s_Bytes; /* Lifetime budget never resets on a stage/frame restart. */

static void auditEmit(const char *line, s32 len)
{
    /* system.c uses timestamp[32], a <=9-byte level prefix, space and
     * newline. 64 bytes conservatively bounds this overhead per sink.
     * The cap covers observer bytes in each file/stdout sink, not the
     * unrelated full game log. No truncated record is emitted. */
    const u32 charge = (u32)len + 64u;
    if (len < 0 || len >= AUDIT_LINE_SIZE || charge > AUDIT_BYTE_LIMIT - s_Bytes)
        return;
    s_Bytes += charge;
    sysLogPrintf(LOG_NOTE | LOGFLAG_NOCON, "%s", line);
}

void swarmAuditObservePose(struct chrdata *chr, const char *path,
                          const f32 *view)
{
    struct model *model;
    struct modelnode *node;
    struct anim *anim;
    struct audit_agent *agent = NULL;
    const f32 *root = NULL;
    const char *reason = "ok";
    char line[AUDIT_LINE_SIZE];
    f32 render_up[3];
    s32 i, interval, index = -1, count = 0, anim_count;
    s32 primary_valid = 0, secondary_valid = 0, merge_active = 0;
    s32 absolute = -1, secondary_absolute = -1, enabled, finite = 0;
    s32 parent_present = -1, root_type = -1, compare_candidate = 0, len;

    /* Cheap feature/identity/sampling/byte gates precede root resolution. */
    if (!sysArgCheck("--debug-swarm-pose-observe") || !chr || !chr->prop
            || (chr->hidden & 0x00040000) == 0 || !path || !view
            || s_Bytes >= AUDIT_BYTE_LIMIT) return;
    if (s_Stage != g_StageNum || g_Vars.lvframe60 < s_LastFrame) {
        s_Stage = g_StageNum;
        s_Epoch++;
        for (i = 0; i < AUDIT_AGENT_LIMIT; i++) s_Agents[i].chr = NULL;
    }
    s_LastFrame = g_Vars.lvframe60;
    interval = g_Vars.lvframe60 / AUDIT_INTERVAL_60;
    for (i = 0; i < AUDIT_AGENT_LIMIT; i++) {
        if (s_Agents[i].chr == chr && s_Agents[i].chrnum == chr->chrnum
                && s_Agents[i].prop == chr->prop
                && s_Agents[i].model == chr->model) {
            agent = &s_Agents[i]; break;
        }
    }
    if (!agent) {
        for (i = 0; i < AUDIT_AGENT_LIMIT; i++) {
            if (!s_Agents[i].chr) {
                agent = &s_Agents[i];
                agent->chr = chr; agent->prop = chr->prop;
                agent->model = chr->model; agent->chrnum = chr->chrnum;
                agent->last_interval = -1; break;
            }
        }
    }
    if (!agent || agent->last_interval == interval) return;
    agent->last_interval = interval; /* Deduplicate tick/fallback/passes. */

    enabled = chrSurfaceLocoIsEnabled(chr);
    chrSurfaceLocoGetRenderUp(chr, render_up); /* Read-only blended value. */
    model = chr->model;
    anim = model ? model->anim : NULL;
    anim_count = animGetTotalCount();
    if (anim && g_Anims) {
        primary_valid = anim->animnum > 0 && anim->animnum < anim_count
            && g_Anims[anim->animnum].numframes > 0;
        secondary_valid = anim->animnum2 > 0 && anim->animnum2 < anim_count
            && g_Anims[anim->animnum2].numframes > 0;
        if (primary_valid) absolute = !!(g_Anims[anim->animnum].flags & ANIMFLAG_ABSOLUTETRANSLATION);
        if (secondary_valid) secondary_absolute = !!(g_Anims[anim->animnum2].flags & ANIMFLAG_ABSOLUTETRANSLATION);
        merge_active = anim->fracmerge != 0.0f;
    }
    /* Skedar CHRINFO roots only; reject other topology without walking it. */
    if (!model || !model->definition || !model->matrices || !anim)
        reason = "missing_model_pose";
    else if (!primary_valid || (merge_active && !secondary_valid))
        reason = "invalid_animation_index";
    else {
        node = model->definition->rootnode;
        count = model->definition->nummatrices;
        if (!node || !node->rodata) reason = "missing_root";
        else {
            root_type = node->type & 0xff;
            parent_present = node->parent != NULL;
            if (root_type != MODELNODETYPE_CHRINFO) reason = "unsupported_root_type";
            else {
                index = node->rodata->chrinfo.mtxindex;
                if (count <= 0 || index < 0 || index >= count) reason = "root_index_bounds";
                else root = (const f32 *)&model->matrices[index];
            }
        }
    }
    if (root) {
        finite = 1;
        for (i = 0; i < 16; i++) if (!__builtin_isfinite(root[i]) || !__builtin_isfinite(view[i])) finite = 0;
        for (i = 0; i < 3; i++) if (!__builtin_isfinite(render_up[i])) finite = 0;
        if (!finite) reason = "nonfinite_matrix_or_up";
        compare_candidate = finite && enabled && !absolute && !merge_active && !parent_present;
    }
    if (!root || !finite) {
        len = snprintf(line, sizeof(line),
            "SWARM.AUDIT.POSE: epoch=%d stage=%d frame=%d agent=%d chrnum=%d path=%s status=%s root_index=%d matrix_count=%d enabled=%d primary_valid=%d merge_active=%d compare_candidate=0 bytes_before=%u",
            s_Epoch, g_StageNum, g_Vars.lvframe60, (s32)(agent-s_Agents),
            (s32)chr->chrnum, path, reason, index, count, enabled, primary_valid, merge_active, s_Bytes);
        auditEmit(line, len); return;
    }
    len = snprintf(line, sizeof(line),
        "SWARM.AUDIT.POSE: epoch=%d stage=%d frame=%d agent=%d chrnum=%d path=%s status=ok "
        "root_index=%d matrix_count=%d root_type=%d root_parent=%d enabled=%d context_matches=%d "
        "action=%d anim=%d frame1=%.6g speed1=%.6g loop1=%d end1=%.6g abs1=%d meta_flags1=%u frames1=%d "
        "anim2=%d frame2=%.6g abs2=%d merge_active=%d fracmerge=%.6g timemerge=%.6g "
        "compare_candidate=%d scale=%.6g blend=%d flags=%u sup=(%.6g,%.6g,%.6g) "
        "prev=(%.6g,%.6g,%.6g) render_up=(%.6g,%.6g,%.6g) bytes_before=%u "
        "root=[%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g] "
        "view=[%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g]",
        s_Epoch,g_StageNum,g_Vars.lvframe60,(s32)(agent-s_Agents),(s32)chr->chrnum,path,
        index,count,root_type,parent_present,enabled,g_SurfaceLocoActiveChr==chr,
        (s32)chr->actiontype,(s32)anim->animnum,anim->frame,anim->speed,(s32)anim->looping,anim->endframe,absolute,
        (u32)g_Anims[anim->animnum].flags,(s32)g_Anims[anim->animnum].numframes,
        (s32)anim->animnum2,anim->frame2,secondary_absolute,merge_active,anim->fracmerge,anim->timemerge,
        compare_candidate,model->scale,(s32)chr->surface_blend_frames,(u32)chr->surface_loco_flags,
        chr->surface_up[0],chr->surface_up[1],chr->surface_up[2],
        chr->surface_up_prev[0],chr->surface_up_prev[1],chr->surface_up_prev[2],
        render_up[0],render_up[1],render_up[2],s_Bytes,
        root[0],root[1],root[2],root[3],root[4],root[5],root[6],root[7],root[8],root[9],root[10],root[11],root[12],root[13],root[14],root[15],
        view[0],view[1],view[2],view[3],view[4],view[5],view[6],view[7],view[8],view[9],view[10],view[11],view[12],view[13],view[14],view[15]);
    auditEmit(line, len);
}
