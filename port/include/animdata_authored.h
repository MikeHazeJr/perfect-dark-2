#ifndef PD_ANIMDATA_AUTHORED_H
#define PD_ANIMDATA_AUTHORED_H

/*
 * port/include/animdata_authored.h -- Catalog Universality BYOR
 * Completion (2026-05-03).
 *
 * Public surface for the weapon-animation AUTHORING source-of-truth.
 *
 * THIS HEADER IS FOR THE RUNTIME EMITTER ONLY.
 * Engine code does not include this header. Animations are referenced
 * by struct weapon fields (equip_animation, fire_animation, etc.) which
 * point at the invanim_*[] arrays defined alongside the weapons in
 * port/src/weapondata_authored.c. The .pdanim emitter walks the
 * iteration table here to produce one .pdanim file per animation; the
 * walker registers them in the catalog; the catalog is the engine's
 * read source.
 *
 * Allowed includers:
 *   - port/src/animdata_authored.c    (the iteration table)
 *   - port/src/romextract_pdanim.c    (the emitter)
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>

struct guncmd;

typedef struct {
	const char *name;        /* "invanim_falcon2_equip" -- maps to "base:invanim_falcon2_equip" catalog ID */
	const struct guncmd *cmds; /* opcode array; walked until GUNCMD_END */
} animdata_record_t;

extern const animdata_record_t g_AnimData[];
extern const s32 g_AnimDataCount;

#endif /* PD_ANIMDATA_AUTHORED_H */
