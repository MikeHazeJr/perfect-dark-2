/**
 * modelcatalog.c -- PC port model catalog system.
 *
 * Pre-scans and validates all character models at startup, caches metadata,
 * and provides safe accessors for the lobby UI and agent select screen.
 *
 * Architecture:
 *   1. catalogInit() iterates g_HeadsAndBodies[0..151] + mod entries
 *      (metadata only — no heap required, no model loading)
 *   2. catalogValidateAll() loads and validates each model after heap init
 *   3. Bad scale values are clamped (not rejected) — fixes mod_allinone models
 *   4. Metadata is cached: name, type, gender, validity, MP index
 *   5. g_HeadsAndBodies[].modeldef is populated with validated pointers
 *   6. Engine code continues reading g_HeadsAndBodies unchanged
 *   7. New code uses catalogGet*() for safe, enriched access
 *
 * Exception safety:
 *   Model loading touches ROM data and mod files which may be corrupt.
 *   On Windows, modeldefLoadToNew() is wrapped in SEH __try/__except so
 *   an access violation in a single model marks it INVALID rather than
 *   crashing the entire game. On other platforms, a setjmp/longjmp guard
 *   with SIGSEGV handler provides equivalent protection.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "system.h"
#include "modelcatalog.h"
#include "modmgr.h"
#include "game/modeldef.h"
#include "game/lang.h"
#include "lib/memp.h"
#include "romdata.h"

#include <setjmp.h>
#include "pdgui_charpreview.h"
#ifdef _WIN32
#include <windows.h>    /* VEH: AddVectoredExceptionHandler */
#else
#include <signal.h>     /* POSIX: sigaction, SIGSEGV */
#endif

/* ========================================================================
 * Static state
 * ======================================================================== */

static struct catalogentry s_Catalog[CATALOG_MAX_ENTRIES];
static s32 s_CatalogCount = 0;
static s32 s_NumValidBodies = 0;
static s32 s_NumValidHeads = 0;
static s32 s_Initialized = 0;

/* ========================================================================
 * Thumbnail queue
 *
 * One render is in flight at a time.  catalogPollThumbnails() is called each
 * frame: if the previous render completed it bakes the result into a new,
 * unique GL texture and fires the next pending request.  The queue is a
 * simple circular array of catalog indices.
 * ======================================================================== */

static s32 s_ThumbQueue[CATALOG_MAX_ENTRIES];
static s32 s_ThumbQHead  = 0;
static s32 s_ThumbQTail  = 0;
static s32 s_ThumbActive = -1;  /* catalog index of in-flight render, or -1 */

static void thumbQueuePush(s32 index)
{
	s32 next = (s_ThumbQTail + 1) % CATALOG_MAX_ENTRIES;
	if (next == s_ThumbQHead) return;  /* full — silently drop */
	s_ThumbQueue[s_ThumbQTail] = index;
	s_ThumbQTail = next;
}

static s32 thumbQueuePop(void)
{
	if (s_ThumbQHead == s_ThumbQTail) return -1;
	s32 idx = s_ThumbQueue[s_ThumbQHead];
	s_ThumbQHead = (s_ThumbQHead + 1) % CATALOG_MAX_ENTRIES;
	return idx;
}

/* Maps: mpbody index → catalog index, mphead index → catalog index */
static s32 s_BodyMpToCatalog[CATALOG_MAX_ENTRIES];
static s32 s_HeadMpToCatalog[CATALOG_MAX_ENTRIES];

/* ========================================================================
 * Internal: safe model loading with exception guard
 *
 * modeldefLoadToNew() reads ROM/mod data that could be corrupt. A bad
 * pointer dereference inside it would normally crash the entire game.
 * We wrap the call so a fault in one model is caught, logged in detail,
 * and the entry is simply marked INVALID — the game continues.
 *
 * Windows (MinGW+MSVC): Vectored Exception Handling (VEH).
 * POSIX: temporary SIGSEGV handler with sigsetjmp/siglongjmp.
 * ======================================================================== */

static jmp_buf s_ModelLoadJmpBuf;
static volatile int s_InSafeLoad = 0;

#ifdef _WIN32
static LONG CALLBACK catalogVehHandler(PEXCEPTION_POINTERS ep)
{
	if (s_InSafeLoad
		&& ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		longjmp(s_ModelLoadJmpBuf, 1);
	}
	/* Not ours — let the next handler (crash.c's SEH) deal with it */
	return EXCEPTION_CONTINUE_SEARCH;
}
#else
static struct sigaction s_OldSigsegvAction;

static void catalogSigsegvHandler(int sig, siginfo_t *info, void *ucontext)
{
	if (s_InSafeLoad) {
		longjmp(s_ModelLoadJmpBuf, 1);
	}
	/* Not our fault — restore and re-raise so the normal crash handler runs */
	sigaction(SIGSEGV, &s_OldSigsegvAction, NULL);
	raise(SIGSEGV);
}
#endif

/**
 * Attempt to load a modeldef, catching access violations.
 * Returns the loaded modeldef on success, or NULL on fault.
 */
static struct modeldef *safeModeldefLoad(u16 filenum, s32 index)
{
	struct modeldef *result = NULL;

#ifdef _WIN32
	/* VEH: works with both MinGW and MSVC — no __try/__except needed */
	PVOID handler = AddVectoredExceptionHandler(1, catalogVehHandler);
	s_InSafeLoad = 1;
	if (setjmp(s_ModelLoadJmpBuf) == 0) {
		result = modeldefLoadToNew(filenum);
	} else {
		sysLogPrintf(LOG_WARNING,
			"CATALOG: ACCESS VIOLATION loading model index %d (file 0x%04x) — "
			"marking INVALID. This is likely a corrupt mod model.",
			index, filenum);
		result = NULL;
	}
	s_InSafeLoad = 0;
	RemoveVectoredExceptionHandler(handler);
#else
	/* POSIX: install a temporary SIGSEGV handler that longjmps back */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = catalogSigsegvHandler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, &s_OldSigsegvAction);

	s_InSafeLoad = 1;
	if (setjmp(s_ModelLoadJmpBuf) == 0) {
		result = modeldefLoadToNew(filenum);
	} else {
		sysLogPrintf(LOG_WARNING,
			"CATALOG: SIGSEGV loading model index %d (file 0x%04x) — "
			"marking INVALID. This is likely a corrupt mod model.",
			index, filenum);
		result = NULL;
	}
	s_InSafeLoad = 0;

	/* Restore the previous signal handler */
	sigaction(SIGSEGV, &s_OldSigsegvAction, NULL);
#endif

	return result;
}

/* ========================================================================
 * Internal: validate a single modeldef
 * ======================================================================== */

static u8 validateModeldef(struct modeldef *mdef, s32 index, u16 filenum, f32 *correctedScale)
{
	if (mdef == NULL) {
		sysLogPrintf(LOG_WARNING, "CATALOG: [%3d] file 0x%04x — modeldef is NULL (MISSING)", index, filenum);
		return MODELSTATUS_MISSING;
	}

	/* Structural validity checks — log which field failed */
	if (mdef->skel == NULL) {
		sysLogPrintf(LOG_WARNING, "CATALOG: [%3d] file 0x%04x — skel is NULL (INVALID)", index, filenum);
		return MODELSTATUS_INVALID;
	}
	if (mdef->rootnode == NULL) {
		sysLogPrintf(LOG_WARNING, "CATALOG: [%3d] file 0x%04x — rootnode is NULL (INVALID)", index, filenum);
		return MODELSTATUS_INVALID;
	}
	if (mdef->numparts <= 0 || mdef->numparts > 500) {
		sysLogPrintf(LOG_WARNING, "CATALOG: [%3d] file 0x%04x — numparts=%d out of range (INVALID)",
		             index, filenum, mdef->numparts);
		return MODELSTATUS_INVALID;
	}

	/* Scale check — only reject truly degenerate values (zero/negative).
	 * Values of 700-2000 are normal for AllInOneMods replacement models.
	 * The previous clamp to 1.0 for scale > 100 was destroying valid model
	 * data, causing broken hit radii and rendering. */
	*correctedScale = mdef->scale;
	if (mdef->scale <= 0.0f) {
		sysLogPrintf(LOG_WARNING, "CATALOG: [%3d] file 0x%04x — degenerate scale %.4f, setting to 1.0",
		             index, filenum, mdef->scale);
		*correctedScale = 1.0f;
		mdef->scale = 1.0f;
		return MODELSTATUS_CLAMPED;
	}

	return MODELSTATUS_VALID;
}

/* ========================================================================
 * Internal: determine if an entry is a head or body
 * ======================================================================== */

static u8 classifyEntry(s32 index)
{
	/* In PD, heads and bodies share g_HeadsAndBodies. The distinction is made
	 * by whether the index appears in g_MpHeads (as headnum) or g_MpBodies
	 * (as bodynum). Some entries serve as both. We classify by checking the
	 * mphead/mpbody arrays. Default to BODY for unclassified entries. */

	s32 totalHeads = modmgrGetTotalHeads();
	if (totalHeads < 0) totalHeads = 0;
	if (totalHeads > CATALOG_MAX_ENTRIES) {
		sysLogPrintf(LOG_WARNING, "CATALOG: modmgrGetTotalHeads() returned %d, clamping to %d",
		             totalHeads, CATALOG_MAX_ENTRIES);
		totalHeads = CATALOG_MAX_ENTRIES;
	}

	for (s32 i = 0; i < totalHeads; i++) {
		struct mphead *h = modmgrGetHead(i);
		if (h && h->headnum == index) {
			return MODELCAT_HEAD;
		}
	}

	/* If not found as head, it's a body (or unused) */
	return MODELCAT_BODY;
}

/* ========================================================================
 * Internal: get display name for a body/head
 * ======================================================================== */

static void getDisplayName(struct catalogentry *entry)
{
	/* Try to find this entry in the MP body list first */
	s32 totalBodies = modmgrGetTotalBodies();
	if (totalBodies < 0) totalBodies = 0;
	if (totalBodies > CATALOG_MAX_ENTRIES) totalBodies = CATALOG_MAX_ENTRIES;

	for (s32 i = 0; i < totalBodies; i++) {
		struct mpbody *b = modmgrGetBody(i);
		if (b && b->bodynum == entry->index) {
			char *name = langGet(b->name);
			if (name && name[0]) {
				strncpy(entry->displayName, name, CATALOG_NAME_LEN - 1);
				entry->displayName[CATALOG_NAME_LEN - 1] = '\0';
				entry->mpIndex = i;
				return;
			}
		}
	}

	/* Try head list */
	s32 totalHeads = modmgrGetTotalHeads();
	if (totalHeads < 0) totalHeads = 0;
	if (totalHeads > CATALOG_MAX_ENTRIES) totalHeads = CATALOG_MAX_ENTRIES;

	for (s32 i = 0; i < totalHeads; i++) {
		struct mphead *h = modmgrGetHead(i);
		if (h && h->headnum == entry->index) {
			/* Heads don't have name fields — use body reference or generate */
			entry->mpIndex = i;
			snprintf(entry->displayName, CATALOG_NAME_LEN, "Head %d", entry->index);
			return;
		}
	}

	/* Not in any MP list */
	entry->mpIndex = -1;
	snprintf(entry->displayName, CATALOG_NAME_LEN, "Model %d", entry->index);
}

/* ========================================================================
 * Public: catalogInit
 * ======================================================================== */

void catalogInit(void)
{
	sysLogPrintf(LOG_NOTE, "CATALOG: initializing model catalog (metadata only)...");
	sysLogPrintf(LOG_NOTE, "CATALOG: g_HeadsAndBodies at %p, sizeof(headorbody)=%d",
	             (void *)g_HeadsAndBodies, (s32)sizeof(struct headorbody));

	memset(s_Catalog, 0, sizeof(s_Catalog));
	memset(s_BodyMpToCatalog, -1, sizeof(s_BodyMpToCatalog));
	memset(s_HeadMpToCatalog, -1, sizeof(s_HeadMpToCatalog));
	s_CatalogCount = 0;
	s_NumValidBodies = 0;
	s_NumValidHeads = 0;

	/* Defensive: verify g_HeadsAndBodies is not NULL */
	if (g_HeadsAndBodies == NULL) {
		sysLogPrintf(LOG_WARNING, "CATALOG: g_HeadsAndBodies is NULL — cannot initialize catalog");
		s_Initialized = 1;
		return;
	}

	/* Count entries in g_HeadsAndBodies (terminated by filenum == 0) */
	s32 numEntries = 0;
	while (numEntries < CATALOG_MAX_ENTRIES && g_HeadsAndBodies[numEntries].filenum != 0) {
		numEntries++;
	}

	if (numEntries == 0) {
		sysLogPrintf(LOG_WARNING, "CATALOG: g_HeadsAndBodies has 0 entries — is ROM data loaded?");
		s_Initialized = 1;
		return;
	}

	sysLogPrintf(LOG_NOTE, "CATALOG: found %d head/body entries", numEntries);

	/* Log MP body/head counts from modmgr for cross-reference */
	s32 totalBodiesMgr = modmgrGetTotalBodies();
	s32 totalHeadsMgr = modmgrGetTotalHeads();
	sysLogPrintf(LOG_NOTE, "CATALOG: modmgr reports %d MP bodies, %d MP heads",
	             totalBodiesMgr, totalHeadsMgr);

	for (s32 i = 0; i < numEntries; i++) {
		struct headorbody *hb = &g_HeadsAndBodies[i];
		struct catalogentry *ce = &s_Catalog[i];

		/* Copy static metadata from the compiled array — no heap needed */
		ce->index = i;
		ce->filenum = hb->filenum;
		ce->ismale = hb->ismale;
		ce->type = hb->type;
		ce->canvaryheight = hb->canvaryheight;
		ce->height = hb->height;
		ce->scale = hb->scale;
		ce->correctedScale = hb->scale;
		ce->animscale = hb->animscale;
		ce->handfilenum = hb->handfilenum;
		ce->thumbnailTexId = 0;
		ce->thumbnailReady = 0;

		/* Mark as not-yet-validated (models load lazily when heap is ready) */
		ce->status = MODELSTATUS_UNKNOWN;

		/* Classify as head or body */
		ce->category = classifyEntry(i);

		/* Get display name and MP index */
		getDisplayName(ce);

		/* Build reverse maps */
		if (ce->mpIndex >= 0) {
			if (ce->category == MODELCAT_BODY) {
				if (ce->mpIndex < CATALOG_MAX_ENTRIES) {
					s_BodyMpToCatalog[ce->mpIndex] = i;
				}
				s_NumValidBodies++; /* Assume valid until proven otherwise */
			} else {
				if (ce->mpIndex < CATALOG_MAX_ENTRIES) {
					s_HeadMpToCatalog[ce->mpIndex] = i;
				}
				s_NumValidHeads++;
			}
		}
	}

	s_CatalogCount = numEntries;
	s_Initialized = 1;

	sysLogPrintf(LOG_NOTE, "CATALOG: metadata cached — %d entries, %d bodies, %d heads (validation deferred)",
	             numEntries, s_NumValidBodies, s_NumValidHeads);
}

/* ========================================================================
 * Public: catalogValidateEntry (lazy, on-demand)
 * ======================================================================== */

static void catalogValidateOne(s32 index)
{
	if (index < 0 || index >= s_CatalogCount) return;

	struct catalogentry *ce = &s_Catalog[index];
	if (ce->status != MODELSTATUS_UNKNOWN) return; /* Already validated */

	struct headorbody *hb = &g_HeadsAndBodies[index];

	/* Quick pre-check: if the file doesn't exist in ROM data, mark it
	 * MISSING immediately. This avoids the overhead of VEH setup/teardown
	 * and mempAlloc for every non-existent model file. The deeper fix in
	 * fileLoadToNew also returns NULL for missing files, but catching it
	 * here produces a cleaner log and skips unnecessary work entirely. */
	if (romdataFileGetData(hb->filenum) == NULL) {
		ce->status = MODELSTATUS_MISSING;
		sysLogPrintf(LOG_WARNING, "CATALOG: [%3d] file 0x%04x — not in ROM data (MISSING)",
		             index, hb->filenum);
		return;
	}

	/* Load the modeldef if not already cached (requires heap to be ready).
	 * Use safeModeldefLoad() so a corrupt model file causes an INVALID
	 * status rather than crashing the entire game. */
	if (hb->modeldef == NULL) {
		hb->modeldef = safeModeldefLoad(hb->filenum, index);
	}

	f32 corrected = hb->scale;
	ce->status = validateModeldef(hb->modeldef, index, hb->filenum, &corrected);
	ce->correctedScale = corrected;

	/* Log each validation result at NOTE level for the summary,
	 * but only non-VALID results get WARNING-level detail above */
	if (ce->status == MODELSTATUS_VALID) {
		sysLogPrintf(LOG_NOTE, "CATALOG: [%3d] file 0x%04x — OK (scale=%.2f, parts=%d)",
		             index, hb->filenum, hb->modeldef ? hb->modeldef->scale : 0.0f,
		             hb->modeldef ? hb->modeldef->numparts : 0);
	}
}

/* ========================================================================
 * Public: catalogValidateAll — call after heap is initialized
 * ======================================================================== */

void catalogValidateAll(void)
{
	if (!s_Initialized) {
		sysLogPrintf(LOG_WARNING, "CATALOG: catalogValidateAll() called before catalogInit()");
		return;
	}

	if (s_CatalogCount == 0) {
		sysLogPrintf(LOG_WARNING, "CATALOG: no entries to validate");
		return;
	}

	/* Guard: model loading allocates via mempAlloc(MEMPOOL_STAGE), which
	 * requires mempSetHeap() to have been called. If the stage pool has
	 * zero free bytes, the pool system isn't initialized yet. */
	if (mempGetStageFree() == 0) {
		sysLogPrintf(LOG_ERROR, "CATALOG: catalogValidateAll() called before mempSetHeap() — "
		             "model loading requires the pool allocator. Skipping validation.");
		return;
	}

	sysLogPrintf(LOG_NOTE, "CATALOG: validating all %d models (heap is ready)...",
	             s_CatalogCount);

	s32 numValid = 0, numClamped = 0, numInvalid = 0, numMissing = 0;

	for (s32 i = 0; i < s_CatalogCount; i++) {
		catalogValidateOne(i);

		switch (s_Catalog[i].status) {
			case MODELSTATUS_VALID:   numValid++;   break;
			case MODELSTATUS_CLAMPED: numClamped++; break;
			case MODELSTATUS_INVALID: numInvalid++; break;
			case MODELSTATUS_MISSING: numMissing++; break;
			default: break;
		}

		/* Progress logging every 50 entries for long mod lists */
		if ((i + 1) % 50 == 0) {
			sysLogPrintf(LOG_NOTE, "CATALOG: ...validated %d/%d", i + 1, s_CatalogCount);
		}
	}

	/* Recount valid bodies/heads now that we know actual status */
	s_NumValidBodies = 0;
	s_NumValidHeads = 0;
	for (s32 i = 0; i < s_CatalogCount; i++) {
		struct catalogentry *ce = &s_Catalog[i];
		if (ce->mpIndex >= 0
			&& (ce->status == MODELSTATUS_VALID || ce->status == MODELSTATUS_CLAMPED)) {
			if (ce->category == MODELCAT_BODY) s_NumValidBodies++;
			else s_NumValidHeads++;
		}
	}

	sysLogPrintf(LOG_NOTE, "CATALOG: === Validation Summary ===");
	sysLogPrintf(LOG_NOTE, "CATALOG:   %3d VALID", numValid);
	sysLogPrintf(LOG_NOTE, "CATALOG:   %3d CLAMPED (scale corrected)", numClamped);
	if (numInvalid > 0) {
		sysLogPrintf(LOG_WARNING, "CATALOG:   %3d INVALID (structural failures — check warnings above)", numInvalid);
	} else {
		sysLogPrintf(LOG_NOTE, "CATALOG:   %3d INVALID", numInvalid);
	}
	if (numMissing > 0) {
		sysLogPrintf(LOG_WARNING, "CATALOG:   %3d MISSING (file not found)", numMissing);
	} else {
		sysLogPrintf(LOG_NOTE, "CATALOG:   %3d MISSING", numMissing);
	}
	sysLogPrintf(LOG_NOTE, "CATALOG:   %d usable bodies, %d usable heads",
	             s_NumValidBodies, s_NumValidHeads);
	sysLogPrintf(LOG_NOTE, "CATALOG: === End Validation ===");
}

/* ========================================================================
 * Public: accessors
 * ======================================================================== */

s32 catalogGetCount(void)
{
	return s_CatalogCount;
}

const struct catalogentry *catalogGetEntry(s32 index)
{
	if (index < 0 || index >= s_CatalogCount) {
		return NULL;
	}
	return &s_Catalog[index];
}

const struct catalogentry *catalogGetBodyByMpIndex(s32 mpIndex)
{
	if (mpIndex < 0 || mpIndex >= CATALOG_MAX_ENTRIES) {
		return NULL;
	}
	s32 catIdx = s_BodyMpToCatalog[mpIndex];
	if (catIdx < 0 || catIdx >= s_CatalogCount) {
		return NULL;
	}
	return &s_Catalog[catIdx];
}

const struct catalogentry *catalogGetHeadByMpIndex(s32 mpIndex)
{
	if (mpIndex < 0 || mpIndex >= CATALOG_MAX_ENTRIES) {
		return NULL;
	}
	s32 catIdx = s_HeadMpToCatalog[mpIndex];
	if (catIdx < 0 || catIdx >= s_CatalogCount) {
		return NULL;
	}
	return &s_Catalog[catIdx];
}

s32 catalogGetNumBodies(void)
{
	return s_NumValidBodies;
}

s32 catalogGetNumHeads(void)
{
	return s_NumValidHeads;
}

s32 catalogGetSafeBody(s32 bodynum)
{
	if (bodynum < 0 || bodynum >= s_CatalogCount) {
		sysLogPrintf(LOG_WARNING, "CATALOG: FALLBACK: body %d out of range [0,%d), using basegame body %d",
		             bodynum, s_CatalogCount, CATALOG_FALLBACK_BODY);
		return CATALOG_FALLBACK_BODY;
	}

	/* Trigger lazy validation if not yet done */
	if (s_Catalog[bodynum].status == MODELSTATUS_UNKNOWN) {
		catalogValidateOne(bodynum);
	}

	if (s_Catalog[bodynum].status == MODELSTATUS_INVALID
		|| s_Catalog[bodynum].status == MODELSTATUS_MISSING) {
		sysLogPrintf(LOG_WARNING, "CATALOG: FALLBACK: body %d is %s, using basegame body %d",
		             bodynum,
		             s_Catalog[bodynum].status == MODELSTATUS_INVALID ? "INVALID" : "MISSING",
		             CATALOG_FALLBACK_BODY);
		return CATALOG_FALLBACK_BODY;
	}
	return bodynum;
}

/* Find the mphead index (index into g_MpHeads) whose headnum field (g_HeadsAndBodies
 * index) matches the given value.  Used to convert mpbody.headnum → mphead index
 * so the caller can store it in mpchrconfig.mpheadnum.
 * Returns CATALOG_FALLBACK_HEAD if no matching head is found. */
static s32 catalogFindMpHeadByHeadnum(s16 headnum)
{
	s32 total = modmgrGetTotalHeads();
	for (s32 i = 0; i < total; i++) {
		struct mphead *h = modmgrGetHead(i);
		if (h && h->headnum == headnum) {
			return i;
		}
	}
	return CATALOG_FALLBACK_HEAD;
}

s32 catalogGetSafeBodyPaired(s32 bodynum, s32 *out_mpheadnum)
{
	s32 safeBody = catalogGetSafeBody(bodynum);

	if (safeBody == bodynum) {
		/* Body is valid — leave caller's head choice (*out_mpheadnum) unchanged */
		return safeBody;
	}

	/* Body is invalid or missing — pick a random base game body and use its
	 * built-in head pairing so we always get a matched body+head pair. */
	s32 randMpBodyIdx = rand() % MODMGR_BASE_BODIES;
	struct mpbody *b = modmgrGetBody(randMpBodyIdx);
	s32 pairedHead = b ? catalogFindMpHeadByHeadnum(b->headnum) : CATALOG_FALLBACK_HEAD;

	sysLogPrintf(LOG_WARNING,
	             "CATALOG: FALLBACK: body %d unavailable, using basegame body %d (head %d)",
	             bodynum, randMpBodyIdx, pairedHead);

	if (out_mpheadnum) {
		*out_mpheadnum = pairedHead;
	}
	return randMpBodyIdx;
}

s32 catalogGetSafeHead(s32 headnum)
{
	if (headnum < 0 || headnum >= s_CatalogCount) {
		sysLogPrintf(LOG_WARNING, "CATALOG: FALLBACK: head %d out of range [0,%d), using basegame head %d",
		             headnum, s_CatalogCount, CATALOG_FALLBACK_HEAD);
		return CATALOG_FALLBACK_HEAD;
	}

	/* Trigger lazy validation if not yet done */
	if (s_Catalog[headnum].status == MODELSTATUS_UNKNOWN) {
		catalogValidateOne(headnum);
	}

	if (s_Catalog[headnum].status == MODELSTATUS_INVALID
		|| s_Catalog[headnum].status == MODELSTATUS_MISSING) {
		sysLogPrintf(LOG_WARNING, "CATALOG: FALLBACK: head %d is %s, using basegame head %d",
		             headnum,
		             s_Catalog[headnum].status == MODELSTATUS_INVALID ? "INVALID" : "MISSING",
		             CATALOG_FALLBACK_HEAD);
		return CATALOG_FALLBACK_HEAD;
	}
	return headnum;
}

const char *catalogGetName(s32 index)
{
	if (index < 0 || index >= s_CatalogCount) {
		return "Unknown";
	}
	return s_Catalog[index].displayName;
}

s32 catalogIsHeadBodyCompatible(s32 headnum, s32 bodynum)
{
	if (headnum < 0 || headnum >= s_CatalogCount
		|| bodynum < 0 || bodynum >= s_CatalogCount) {
		return 0;
	}

	/* PD compatibility rules based on HEADBODYTYPE_*:
	 * - DEFAULT heads work with DEFAULT bodies
	 * - FEMALE heads work with FEMALE and FEMALEGUARD bodies
	 * - MAIAN heads work with MAIAN bodies
	 * - CASS heads work with CASS bodies
	 * - MRBLONDE heads work with MRBLONDE bodies
	 * Simplified: types must match, or both be DEFAULT */
	u8 headType = s_Catalog[headnum].type;
	u8 bodyType = s_Catalog[bodynum].type;

	if (headType == bodyType) {
		return 1;
	}
	if (headType == HEADBODYTYPE_DEFAULT && bodyType == HEADBODYTYPE_DEFAULT) {
		return 1;
	}
	/* FEMALE and FEMALEGUARD are cross-compatible */
	if ((headType == HEADBODYTYPE_FEMALE || headType == HEADBODYTYPE_FEMALEGUARD)
		&& (bodyType == HEADBODYTYPE_FEMALE || bodyType == HEADBODYTYPE_FEMALEGUARD)) {
		return 1;
	}

	return 0;
}

void catalogRequestThumbnail(s32 index)
{
	if (index < 0 || index >= s_CatalogCount) return;
	struct catalogentry *ce = &s_Catalog[index];

	/* Only MP-selectable models have a meaningful mpIndex to render. */
	if (ce->mpIndex < 0) return;
	if (ce->thumbnailReady || ce->thumbnailTexId != 0) return;  /* already done */
	if (ce->status == MODELSTATUS_INVALID || ce->status == MODELSTATUS_MISSING) return;

	thumbQueuePush(index);
}

void catalogPollThumbnails(void)
{
	/* Step 1: if the in-flight render just completed, bake a unique texture. */
	if (s_ThumbActive >= 0 && pdguiCharPreviewIsReady()) {
		u32 texId = pdguiCharPreviewBakeToTexture();
		if (texId != 0 && s_ThumbActive < s_CatalogCount) {
			struct catalogentry *ce = &s_Catalog[s_ThumbActive];
			ce->thumbnailTexId = texId;
			ce->thumbnailReady = 1;
			sysLogPrintf(LOG_NOTE, "CATALOG: thumbnail baked [%d] '%s' texId=%u",
			             s_ThumbActive, ce->displayName, texId);
		}
		s_ThumbActive = -1;
	}

	/* Step 2: fire the next pending request (one per frame). */
	if (s_ThumbActive < 0) {
		s32 idx = thumbQueuePop();
		/* Skip entries that were already rendered (duplicate queue entries). */
		while (idx >= 0 && idx < s_CatalogCount && s_Catalog[idx].thumbnailReady) {
			idx = thumbQueuePop();
		}
		if (idx >= 0 && idx < s_CatalogCount) {
			struct catalogentry *ce = &s_Catalog[idx];
			u8 headnum = (u8)CATALOG_FALLBACK_HEAD;
			u8 bodynum = (u8)CATALOG_FALLBACK_BODY;
			if (ce->category == MODELCAT_HEAD) {
				headnum = (u8)ce->mpIndex;
			} else {
				bodynum = (u8)ce->mpIndex;
			}
			pdguiCharPreviewRequest(headnum, bodynum);
			s_ThumbActive = idx;
		}
	}
}

void catalogFlushThumbnailQueue(void)
{
	s_ThumbQHead  = 0;
	s_ThumbQTail  = 0;
	s_ThumbActive = -1;

	for (s32 i = 0; i < s_CatalogCount; i++) {
		if (s_Catalog[i].thumbnailTexId != 0) {
			pdguiCharPreviewFreeTexture(s_Catalog[i].thumbnailTexId);
			s_Catalog[i].thumbnailTexId = 0;
			s_Catalog[i].thumbnailReady  = 0;
		}
	}
}

u32 catalogGetThumbnailTexId(s32 index)
{
	if (index < 0 || index >= s_CatalogCount) {
		return 0;
	}
	return s_Catalog[index].thumbnailTexId;
}

void catalogLogSummary(void)
{
	sysLogPrintf(LOG_NOTE, "CATALOG: === Model Catalog Summary ===");
	sysLogPrintf(LOG_NOTE, "CATALOG: Total entries: %d", s_CatalogCount);

	for (s32 i = 0; i < s_CatalogCount; i++) {
		struct catalogentry *ce = &s_Catalog[i];
		const char *statusStr = "?";
		switch (ce->status) {
			case MODELSTATUS_VALID:   statusStr = "OK";      break;
			case MODELSTATUS_CLAMPED: statusStr = "CLAMPED"; break;
			case MODELSTATUS_INVALID: statusStr = "INVALID"; break;
			case MODELSTATUS_MISSING: statusStr = "MISSING"; break;
		}
		sysLogPrintf(LOG_NOTE, "  [%3d] %-12s file=0x%04x scale=%.2f→%.2f %s mp=%d '%s'",
		             i,
		             ce->category == MODELCAT_HEAD ? "HEAD" : "BODY",
		             ce->filenum,
		             ce->scale,
		             ce->correctedScale,
		             statusStr,
		             ce->mpIndex,
		             ce->displayName);
	}

	sysLogPrintf(LOG_NOTE, "CATALOG: === End Summary ===");
}
