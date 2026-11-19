#ifndef PD_MODEL_RODATA_GUARD_H
#define PD_MODEL_RODATA_GUARD_H

/*
 * model_rodata_guard.h -- S483b (2026-04-27)
 *
 * Per-tick defensive validators for `node->rodata` pointers consumed by
 * the model relations-update tree (modelUpdateReorderRelations and
 * siblings).  Companion to the load-time validator in
 * port/src/modelcatalog.c.
 *
 * Background.  A 0xc0000005 access violation surfaced in the playtest
 * at LVTICK frame 1836 of stage 0x33 (Investigation), reading
 * `rodata->reorder.unk08` inside modelUpdateReorderRelations
 * (src/lib/model.c:1387).  The fault offset pattern (low offsets read
 * fine, +0x8 AVs) is consistent with the rodata struct straddling a
 * page boundary into unmapped memory.  The same frame logged
 * `DOOR.DIAG: doorGetBbox -- no bbox for modelnum=154` (count=1),
 * which suggests the door's model_009a was loaded with partially
 * populated rodata: the bbox node was discoverable but missing data,
 * the reorder node existed with a dangling rodata pointer.
 *
 * Mike's hypothesis (2026-04-27): the catalog or model-load pipeline
 * is registering the door entry without finishing its rodata pool.
 * Defensive surface here characterises which rodata reads fail and
 * skips them safely so the game keeps running while we discriminate
 * the root cause on the catalog side.
 *
 * Discipline (matches INV-1 from b6a0c280):
 *   - On miss, log a `MODEL.RODATA.MISS:` warning at LOG_WARNING with
 *     enough context (call site, model ptr, node ptr, reason) to
 *     correlate with the DOOR.DIAG / CATALOG.MISS streams.
 *   - Warnings are rate-limited per call-site to avoid log spam if a
 *     bad model ticks continuously.
 *   - Validators are pure read predicates, no side effects on the
 *     node or model tree.
 *
 * Two predicates are exposed:
 *
 *   modelRodataIsNonNull(rodata)
 *     Cheap NULL check.  All rodata-reading update functions should
 *     gate on this at entry.  Returns true iff `rodata != NULL`.
 *
 *   modelRodataIsReadable(rodata, bytes)
 *     Page-level addressability probe via VirtualQuery on Windows.
 *     Returns true iff the byte range `[rodata, rodata+bytes)` is
 *     fully committed and readable.  Catches the page-boundary AV
 *     that motivated this header.  On non-Windows hosts the probe
 *     degrades to the NULL check.
 *
 * Cost: NULL is a single compare; readable is one VirtualQuery
 * (~100 ns on modern Win10/11) per range, ~100 calls per tick on a
 * heavy stage -- rounding error in the frame budget.
 *
 * That estimate holds for a hundred calls a tick and not for a loop.
 * Measured 2026-11-17 (B-1234): the shot hit test probed every display
 * list command and every vertex batch it walked, one system call each.
 * Against the base hoverbike that was about 3 ms of every frame while the
 * player looked at it or rode it; against a compiled 5,829-triangle mod
 * vehicle (one vertex batch per triangle) about 47 ms, which held its
 * rider near 20 frames a second.  A walk that checks many ranges uses
 * modelRodataIsReadableInSpan below, which asks the operating system once
 * per readable run of memory, not once per range.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Both predicates return non-zero on validity / readable, zero on
 * NULL or unreadable.  We avoid <stdbool.h> in the public surface so
 * src/lib/model.c (which gets `bool` from types.h as an `s32` macro)
 * can include this header without a `bool` macro / typedef collision.
 */

/*
 * NULL-only validator.  Cheap, suitable for hot paths.  Returns 1 if
 * `rodata != NULL`, 0 otherwise.
 */
int modelRodataIsNonNull(const void *rodata);

/*
 * Page-level read addressability probe.  Returns 1 iff the entire
 * `[rodata, rodata + bytes)` range is committed and readable.  On
 * non-Windows hosts this falls back to a NULL check.
 */
int modelRodataIsReadable(const void *rodata, size_t bytes);

/*
 * What one walk has learned about readable memory: the bounds of the run
 * of committed, readable pages its last query found.  `hi` is one past the
 * last readable byte; zero means nothing is known yet.
 *
 * A span belongs to ONE walk.  Declare it on the walk's stack, zeroed
 * (`model_rodata_span_t span = { 0, 0 };`), and let it go when the walk
 * returns.  Never keep one in a global or across frames: the memory it
 * describes can be freed or re-protected once the walk is over, and a
 * stale span would then answer "readable" for memory that no longer is.
 */
typedef struct model_rodata_span {
	uintptr_t lo;
	uintptr_t hi;
} model_rodata_span_t;

/*
 * The same answer as modelRodataIsReadable, for a walk that checks many
 * ranges.  A range inside the span is answered with two compares and no
 * system call.  Any other range is asked of the operating system as
 * before, and the run of pages that query walked becomes the span (joined
 * to the old span when the two touch or overlap).  A range that fails
 * leaves the span as it was.
 *
 * A run starts at the page that holds the queried address, not at the
 * start of its allocation, so a walk that reads memory in rising order
 * asks once and a walk that reads it in falling order asks once per page.
 *
 * `span` may be NULL: the call is then exactly modelRodataIsReadable.
 */
int modelRodataIsReadableInSpan(model_rodata_span_t *span,
                                const void *rodata, size_t bytes);

/*
 * Logging helper.  Emits a rate-limited `MODEL.RODATA.MISS:` warning
 * at LOG_WARNING.  `site_tag` should be a short stable identifier for
 * the caller (e.g. "Reorder.unk08", "Distance.near").  The model and
 * node pointers are included verbatim so the entry can be correlated
 * with surrounding CATALOG / DOOR.DIAG streams.
 */
void modelRodataLogMiss(const char *site_tag,
                        const void *model,
                        const void *node,
                        const void *rodata,
                        size_t bytes,
                        const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* PD_MODEL_RODATA_GUARD_H */
