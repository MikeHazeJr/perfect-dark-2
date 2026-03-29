/**
 * modelcatalog.h -- PC port model catalog system.
 *
 * Replaces direct access to g_HeadsAndBodies with a validated, pre-scanned
 * catalog of all character models. At startup, every head/body entry is loaded,
 * validated, and metadata cached. The engine's g_HeadsAndBodies array is then
 * populated with corrected data so existing code continues to work.
 *
 * New code (lobby UI, agent select, network) should use the catalog API
 * instead of accessing g_HeadsAndBodies directly.
 */

#ifndef _IN_MODELCATALOG_H
#define _IN_MODELCATALOG_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

#define CATALOG_MAX_ENTRIES    256   /* room for base game (152) + mods */
#define CATALOG_NAME_LEN       64   /* display name */
#define CATALOG_FALLBACK_BODY   4   /* Joanna Combat (known good) */
#define CATALOG_FALLBACK_HEAD   3   /* Joanna head (known good) */

/* Model categories (superset of HEADBODYTYPE_*) */
#define MODELCAT_HEAD          0    /* entry is a head */
#define MODELCAT_BODY          1    /* entry is a body */

/* Validation status */
#define MODELSTATUS_UNKNOWN    0    /* not yet scanned */
#define MODELSTATUS_VALID      1    /* loaded and validated successfully */
#define MODELSTATUS_CLAMPED    2    /* loaded but scale was clamped */
#define MODELSTATUS_INVALID    3    /* structural failure (NULL skel, etc.) */
#define MODELSTATUS_MISSING    4    /* file not found or couldn't load */

/* ========================================================================
 * Catalog entry
 * ======================================================================== */

struct catalogentry {
    /* Identity */
    s32 index;                          /* original g_HeadsAndBodies index */
    u16 filenum;                        /* ROM file ID */
    u8  category;                       /* MODELCAT_HEAD or MODELCAT_BODY */
    u8  status;                         /* MODELSTATUS_* */

    /* Metadata from g_HeadsAndBodies */
    u8  ismale;
    u8  type;                           /* HEADBODYTYPE_* */
    u8  canvaryheight;
    u8  height;
    f32 scale;                          /* original scale (before clamping) */
    f32 correctedScale;                 /* scale after validation/clamping */
    f32 animscale;
    u16 handfilenum;

    /* Display info */
    char displayName[CATALOG_NAME_LEN]; /* localized name from langGet */

    /* Multiplayer index (-1 if not an MP-selectable model) */
    s32 mpIndex;                        /* index into g_MpBodies/g_MpHeads */

    /* Thumbnail texture (0 = not yet rendered) */
    u32 thumbnailTexId;
    u8  thumbnailReady;
};

/* ========================================================================
 * API
 * ======================================================================== */

/**
 * Initialize the model catalog (metadata only — no heap required).
 * Scans all g_HeadsAndBodies entries, caches static metadata, classifies
 * entries as heads/bodies, and builds reverse lookup maps.
 * Call once during startup, after ROM data and modmgr are loaded.
 */
void catalogInit(void);

/**
 * Validate all catalog entries by loading and checking their modeldefs.
 * Requires the game heap (g_MempHeap) to be allocated. Call once after
 * heap initialization. Models with bad scale values are clamped rather
 * than rejected.
 */
void catalogValidateAll(void);

/**
 * Get the total number of catalog entries (base + mod).
 */
s32 catalogGetCount(void);

/**
 * Get a catalog entry by its g_HeadsAndBodies index.
 * Returns NULL if index is out of range.
 */
const struct catalogentry *catalogGetEntry(s32 index);

/**
 * Get a catalog entry by multiplayer body index.
 * Returns NULL if mpIndex is out of range.
 */
const struct catalogentry *catalogGetBodyByMpIndex(s32 mpIndex);

/**
 * Get a catalog entry by multiplayer head index.
 * Returns NULL if mpIndex is out of range.
 */
const struct catalogentry *catalogGetHeadByMpIndex(s32 mpIndex);

/**
 * Get the number of valid MP-selectable bodies.
 */
s32 catalogGetNumBodies(void);

/**
 * Get the number of valid MP-selectable heads.
 */
s32 catalogGetNumHeads(void);

/**
 * Get a safe body index — returns the input if valid, or
 * CATALOG_FALLBACK_BODY if the model is invalid/missing.
 */
s32 catalogGetSafeBody(s32 bodynum);

/**
 * Get a safe body index with its default paired head.
 *
 * If bodynum is valid, returns it and leaves *out_mpheadnum unchanged (caller's
 * head choice is still respected).  If bodynum is invalid or missing, picks a
 * random base game body (mpbody index in [0, MODMGR_BASE_BODIES)) and uses
 * that body's mpbody.headnum field to derive the matched mphead index, writing
 * it into *out_mpheadnum.  Falls back to CATALOG_FALLBACK_BODY/HEAD if the
 * random pick also fails.
 *
 * Logs MOD: WARNING when fallback is triggered.
 *
 * @param bodynum       mpbody index (same domain as catalogGetSafeBody input)
 * @param out_mpheadnum updated with the paired mphead index ONLY on fallback
 * @return safe mpbody index
 */
s32 catalogGetSafeBodyPaired(s32 bodynum, s32 *out_mpheadnum);

/**
 * Get a safe head index — returns the input if valid, or
 * CATALOG_FALLBACK_HEAD if the model is invalid/missing.
 */
s32 catalogGetSafeHead(s32 headnum);

/**
 * Get the display name for a body/head by g_HeadsAndBodies index.
 */
const char *catalogGetName(s32 index);

/**
 * Check if a head is compatible with a body (type matching).
 */
s32 catalogIsHeadBodyCompatible(s32 headnum, s32 bodynum);

/**
 * Request thumbnail generation for a catalog entry.
 * Adds the entry to the render queue.  No-op if already rendered or invalid.
 * Call catalogPollThumbnails() each frame to drive the queue forward.
 */
void catalogRequestThumbnail(s32 index);

/**
 * Poll the thumbnail queue — call once per frame after the GBI render phase.
 * On completion of the previous render, bakes the result into a unique GL
 * texture stored in the entry's thumbnailTexId/thumbnailReady fields, then
 * fires the next queued request.
 */
void catalogPollThumbnails(void);

/**
 * Flush the thumbnail queue and release all baked GL textures.
 * Call on shutdown or asset reload.
 */
void catalogFlushThumbnailQueue(void);

/**
 * Get the GL texture ID for a catalog entry's thumbnail.
 * Returns 0 if not yet rendered.
 */
u32 catalogGetThumbnailTexId(s32 index);

/**
 * Print catalog summary to log (for debugging).
 */
void catalogLogSummary(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODELCATALOG_H */
