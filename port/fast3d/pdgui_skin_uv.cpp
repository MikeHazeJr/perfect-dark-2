/**
 * pdgui_skin_uv.cpp -- UV wireframe extraction from PD model data.
 *
 * Walks the model node tree for a character body, extracts vertex UV
 * coordinates from MODELNODETYPE_DL (0x18) nodes, and provides them
 * as line segments for the skin editor's wireframe overlay.
 *
 * The UV data helps users see which pixels on the 2D canvas map to
 * which parts of the 3D model.
 *
 * Design doc: context/designs/skin-editor-design.md §3.6, §5 (S-8)
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "pdgui_skin_editor.h"
#include "assetcatalog.h"
#include "system.h"

/* ========================================================================
 * We need access to model structs (types.h defines bool as s32 which breaks
 * C++).  Forward-declare the minimum we need instead of including types.h.
 * ======================================================================== */

struct modelnode {
    u16 type;
    union modelrodata *rodata;
    struct modelnode *parent;
    struct modelnode *next;
    struct modelnode *prev;
    struct modelnode *child;
};

struct modelrodata_dl {
    Gfx *opagdl;
    Gfx *xlugdl;
    void *colours;
    Vtx *vertices;
    s16 numvertices;
    s16 mcount;
    u16 rwdataindex;
    u16 numcolours;
};

struct modeldef {
    struct modelnode *rootnode;
    void *skel;
    struct modelnode **parts;
    s16 numparts;
    s16 nummatrices;
    f32 scale;
};

union modelrodata {
    struct modelrodata_dl dl;
};

extern "C" {
struct modeldef *catalogGetBodyModeldef(s32 bodynum);
}

#define MODELNODETYPE_DL 0x18

/* ========================================================================
 * UV data storage
 * ======================================================================== */

#define SKIN_UV_MAX_LINES 2048

struct UvLine {
    f32 u0, v0, u1, v1;  /* normalized 0..1 */
};

static UvLine s_UvLines[SKIN_UV_MAX_LINES];
static s32    s_NumUvLines = 0;
static s32    s_UvValid    = 0;
static char   s_UvBodyId[64] = "";

/* ========================================================================
 * Model tree walk — extract UV edges from DL nodes
 * ======================================================================== */

static void extractUvFromDlNode(struct modelnode *node, s32 texW, s32 texH)
{
    if (!node || !node->rodata) return;

    struct modelrodata_dl *dl = &node->rodata->dl;
    if (!dl->vertices || dl->numvertices < 3) return;

    /* Each triangle in the display list connects 3 vertices.
     * Without parsing the actual GBI commands (complex), we approximate
     * by drawing edges between consecutive vertex pairs.  This gives a
     * reasonable wireframe approximation for UV layout visualization. */
    f32 invW = (texW > 0) ? 1.0f / (f32)(texW * 32) : 1.0f;
    f32 invH = (texH > 0) ? 1.0f / (f32)(texH * 32) : 1.0f;

    for (s32 i = 0; i < dl->numvertices - 1 && s_NumUvLines < SKIN_UV_MAX_LINES; i++) {
        Vtx *v0 = &dl->vertices[i];
        Vtx *v1 = &dl->vertices[i + 1];

        /* Vtx.s and .t are s10.5 fixed-point UV coordinates.
         * Divide by (texDim * 32) to normalize to 0..1. */
        f32 u0 = (f32)v0->s * invW;
        f32 vv0 = (f32)v0->t * invH;
        f32 u1 = (f32)v1->s * invW;
        f32 vv1 = (f32)v1->t * invH;

        /* Clamp to valid range */
        if (u0 < 0) u0 = 0; if (u0 > 1) u0 = 1;
        if (vv0 < 0) vv0 = 0; if (vv0 > 1) vv0 = 1;
        if (u1 < 0) u1 = 0; if (u1 > 1) u1 = 1;
        if (vv1 < 0) vv1 = 0; if (vv1 > 1) vv1 = 1;

        s_UvLines[s_NumUvLines].u0 = u0;
        s_UvLines[s_NumUvLines].v0 = vv0;
        s_UvLines[s_NumUvLines].u1 = u1;
        s_UvLines[s_NumUvLines].v1 = vv1;
        s_NumUvLines++;
    }

    /* Close the loop: last vertex back to first */
    if (dl->numvertices >= 3 && s_NumUvLines < SKIN_UV_MAX_LINES) {
        Vtx *vLast = &dl->vertices[dl->numvertices - 1];
        Vtx *vFirst = &dl->vertices[0];

        s_UvLines[s_NumUvLines].u0 = (f32)vLast->s * invW;
        s_UvLines[s_NumUvLines].v0 = (f32)vLast->t * invH;
        s_UvLines[s_NumUvLines].u1 = (f32)vFirst->s * invW;
        s_UvLines[s_NumUvLines].v1 = (f32)vFirst->t * invH;
        s_NumUvLines++;
    }
}

static void walkModelTree(struct modelnode *node, s32 texW, s32 texH)
{
    while (node) {
        if (node->type == MODELNODETYPE_DL) {
            extractUvFromDlNode(node, texW, texH);
        }
        if (node->child) {
            walkModelTree(node->child, texW, texH);
        }
        node = node->next;
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

extern "C" {

void skinUvExtract(const char *body_id, s32 texW, s32 texH)
{
    s_NumUvLines = 0;
    s_UvValid = 0;

    if (!body_id || !body_id[0]) return;

    /* Check if already extracted for this body */
    if (strcmp(s_UvBodyId, body_id) == 0 && s_UvValid) return;

    strncpy(s_UvBodyId, body_id, 63);
    s_UvBodyId[63] = '\0';

    /* Resolve body -> modeldef */
    const asset_entry_t *be = assetCatalogResolve(body_id);
    if (!be || be->type != ASSET_BODY || be->mp_index < 0) {
        sysLogPrintf(LOG_NOTE, "skin_uv: cannot resolve body '%s'", body_id);
        return;
    }

    struct modeldef *mdef = catalogGetBodyModeldef(be->mp_index);
    if (!mdef || !mdef->rootnode) {
        sysLogPrintf(LOG_NOTE, "skin_uv: no modeldef for body '%s'", body_id);
        return;
    }

    walkModelTree(mdef->rootnode, texW, texH);
    s_UvValid = 1;

    sysLogPrintf(LOG_NOTE, "skin_uv: extracted %d UV lines from '%s'",
                 s_NumUvLines, body_id);
}

void skinUvClear(void)
{
    s_NumUvLines = 0;
    s_UvValid = 0;
    s_UvBodyId[0] = '\0';
}

s32 skinUvGetNumLines(void)
{
    return s_NumUvLines;
}

void skinUvGetLine(s32 idx, f32 *u0, f32 *v0, f32 *u1, f32 *v1)
{
    if (idx < 0 || idx >= s_NumUvLines) {
        *u0 = *v0 = *u1 = *v1 = 0;
        return;
    }
    *u0 = s_UvLines[idx].u0;
    *v0 = s_UvLines[idx].v0;
    *u1 = s_UvLines[idx].u1;
    *v1 = s_UvLines[idx].v1;
}

} /* extern "C" */
