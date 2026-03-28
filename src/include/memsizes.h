/**
 * memsizes.h — Named constants for all previously-magic-number allocations.
 *
 * Part of Phase D-MEM (Memory Modernization). Every mempAlloc, stack buffer,
 * and scratch offset that used bare hex/decimal literals now has a named
 * constant here. No behavioral change — just readability and auditability.
 *
 * Categories:
 *   BG_*       — Background / level geometry loading
 *   GUN_*      — Weapon model loading
 *   MENU_*     — Menu system buffers
 *   TEX_*      — Texture decompression scratch
 *   CAM_*      — Camera / draw pipeline
 *   PAK_*      — Controller pak / file I/O
 *   SND_*      — Audio scratch buffers
 *   ZBUF_*     — Z-buffer allocation
 *   RDP_*      — RDP yield data
 *
 * Entity limits:
 *   These replace bare integer literals in mempAlloc multiplier expressions.
 *   Where an existing #define already covers the value (MAX_BOTS, MAX_PLAYERS),
 *   prefer those. These are for values that had no constant at all.
 *
 * Created: 2026-03-21, Session 15 (Phase M1)
 */

#ifndef _IN_MEMSIZES_H
#define _IN_MEMSIZES_H

/* ======================================================================
 * Background / Level Geometry Loading (bg.c)
 * ====================================================================== */

/* Scratch headroom added to inflated size for BG section decompression.
 * The decompressor reads compressed data from the tail of the buffer while
 * writing inflated data from the head — the headroom prevents overlap. */
#define BG_INFLATE_SCRATCH_LARGE       0x8000   /* 32KB — primary sections */
#define BG_INFLATE_SCRATCH_HEADER      0x10     /* 16B — header prefix (section 1 adds this) */
#define BG_INFLATE_SCRATCH_SMALL       0x800    /* 2KB — beta-version fallback (section 2) */
#define BG_INFLATE_SCRATCH_MEDIUM      0x1000   /* 4KB — beta-version fallback (section 3) */

/* GFX data sizing */
#define BG_GFXDATA_HEADER_PAD          0x100    /* 256B — display list header padding */
#define BG_GFXDATA_TAIL_PAD            0x20     /* 32B — display list tail padding */

/* ======================================================================
 * Weapon Model Loading (bondgun.c)
 * ====================================================================== */

#define GUN_MODEL_LOAD_SCRATCH         0x8000   /* 32KB decompression scratch for gun models */
#define GUN_MEMLOAD_MATRIX_BATCH       50       /* Matrices loaded per frame during async gun load */

/* ======================================================================
 * Menu System (menu.c, credits.c)
 * ====================================================================== */

#define MENU_BLUR_BUFFER_SIZE          0x4b00   /* 19.2KB — fullscreen blur effect buffer */

/* Menu 3D model buffers — used by menuResetModel() for in-menu character
 * previews. Sizes differ by pointer width because the model data contains
 * embedded pointer fields that are 4 bytes on N64/32-bit and 8 bytes on
 * PC/64-bit, expanding the total allocation proportionally. */
#define MENU_MODEL_BUF_4MB             0xb400   /*  45KB — 4MB mode (IS4MB, dead path) */
#define MENU_MODEL_BUF_8MB_64BIT       0x38400  /* 230KB — 8MB mode, 64-bit pointers */
#define MENU_MODEL_BUF_8MB_32BIT       0x25800  /* 155KB — 8MB mode, 32-bit pointers */
#define CREDITS_MODEL_BUF_SIZE         0x25800  /* 155KB — credits character model */

/* ======================================================================
 * Texture Decompression (texdecompress.c)
 * ====================================================================== */

#define TEX_SCRATCH_SIZE               0x2000   /* 8KB — main decompression scratch */
#define TEX_LOOKUP_SIZE                0x1000   /* 4KB — decompression lookup table */
#define TEX_SCRATCH2_SIZE              0x800    /* 2KB — secondary scratch */

/* ======================================================================
 * Camera / Draw Pipeline (camdraw.c)
 * ====================================================================== */

#define CAM_STACK_SIZE                 0x420    /* ~1KB — camera draw stack */
#define CAM_SP44_SIZE                  0x1000   /* 4KB — camera draw scratch */

/* ======================================================================
 * Controller Pak / File I/O (pak.c, file.c)
 * ====================================================================== */

#define PAK_BUFFER_SIZE                0x4000   /* 16KB — pak read/write buffer */

/* ======================================================================
 * Audio (snd.c, rdp.c)
 * ====================================================================== */

#define SND_SCRATCH_SIZE               0x150    /* 336B — audio processing scratch */
#define RDP_YIELD_DATA_SIZE            0xb00    /* 2.75KB — RDP yield buffer */

/* ======================================================================
 * Z-Buffer (zbuf.c)
 * ====================================================================== */

#define ZBUF_ALIGN_PAD                 0x40     /* 64B — z-buffer alignment headroom */
#define ZBUF_ALIGN_MASK                0x3f     /* 63 — alignment mask for 64-byte boundary */

/* ======================================================================
 * Entity / Array Limits
 *
 * These replace bare integer literals used as array size multipliers
 * in mempAlloc calls. Where possible, prefer existing constants from
 * constants.h (MAX_PLAYERS, MAX_BOTS, MAX_MPCHRS, etc.).
 * ====================================================================== */

#define AMMO_TYPE_COUNT                36       /* Number of distinct ammo types */
#define MAX_ONSCREEN_PROPS             200      /* Max props visible in one frame */
#define CHR_MANAGER_SLOTS              15       /* Character manager array slots */

/* ======================================================================
 * Alignment Helpers (legacy DMA, ceremonial on PC)
 *
 * On N64, DMA required 16-byte-aligned addresses. On PC, dmaStart is
 * just memcpy and alignment is irrelevant. These exist for documentation
 * and will be candidates for removal in Phase M4.
 * ====================================================================== */

#define DMA_ALIGN_PAD_16               0x10     /* 16B */
#define DMA_ALIGN_PAD_64               0x40     /* 64B */

#endif /* _IN_MEMSIZES_H */
