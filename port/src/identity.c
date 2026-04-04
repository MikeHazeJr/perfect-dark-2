/**
 * identity.c -- Player identity foundation (pd-identity.dat).
 *
 * Binary file layout v2 (SA-4, all fields little-endian):
 *   [4]  u32  magic      = IDENTITY_MAGIC ("PDID")
 *   [1]  u8   version    = 2
 *   [16] u8   device_uuid
 *   [1]  u8   profile_count
 *   [1]  u8   active_profile
 *   [2]  u8   _pad[2]
 *   [N * IDENTITY_PROFILE_SIZE] identity_profile_t  profiles[profile_count]
 *     where each profile is (IDENTITY_NAME_MAX + CATALOG_ID_LEN + CATALOG_ID_LEN + 4) bytes:
 *       [16] char name[IDENTITY_NAME_MAX]
 *       [64] char head_id[CATALOG_ID_LEN]
 *       [64] char body_id[CATALOG_ID_LEN]
 *       [1]  u8   flags
 *       [3]  u8   _pad[3]
 *
 * Legacy v1 format (auto-migrated on load):
 *   Each profile was 20 bytes:
 *       [16] char name[16]
 *       [1]  u8   headnum  (raw index)
 *       [1]  u8   bodynum  (raw index)
 *       [1]  u8   flags
 *       [1]  u8   _pad
 *   On load, headnum/bodynum are resolved via catalogResolveByRuntimeIndex()
 *   and the file is immediately re-saved in v2 format.
 *
 * UUID generation: seeded from SDL performance counter + time(),
 * mixed with xorshift128.  Not cryptographically secure, but
 * sufficient for stable device identity.
 */

#include "identity.h"
#include "assetcatalog.h"
#include "system.h"
#include "fs.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

/* SDL for SDL_GetPerformanceCounter() */
#include <SDL.h>

/* -------------------------------------------------------------------------
 * Internal file structure (packed, written verbatim)
 * ------------------------------------------------------------------------- */

#define IDENTITY_FILE_VERSION  2   /* SA-4: string IDs replace raw integers */
#define IDENTITY_FILE_VERSION_V1 1 /* legacy: headnum/bodynum as u8 */

/* Total header size before profiles. */
#define IDENTITY_HEADER_SIZE (4 + 1 + 16 + 1 + 1 + 2)  /* = 25 bytes */

/* Bytes per serialised profile (v2). */
#define IDENTITY_PROFILE_SIZE \
    (IDENTITY_NAME_MAX + CATALOG_ID_LEN + CATALOG_ID_LEN + 4)  /* = 148 bytes */

/* Bytes per serialised profile (v1 legacy). */
#define IDENTITY_PROFILE_SIZE_V1 20

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static pd_identity_t s_Identity;
static int           s_Loaded = 0;

/* -------------------------------------------------------------------------
 * UUID generation
 * ------------------------------------------------------------------------- */

/* Simple xorshift128 for filling UUID bytes.  State seeded from time. */
static uint32_t s_Xorstate[4];

static void xorInit(void)
{
    uint64_t t = (uint64_t)time(NULL);
    uint64_t c = SDL_GetPerformanceCounter();
    s_Xorstate[0] = (uint32_t)(t ^ 0xDEADBEEFu);
    s_Xorstate[1] = (uint32_t)(t >> 32);
    s_Xorstate[2] = (uint32_t)(c ^ 0xCAFEBABEu);
    s_Xorstate[3] = (uint32_t)(c >> 32);
    /* Discard first 8 outputs to warm up. */
    for (int i = 0; i < 8; i++) {
        uint32_t x = s_Xorstate[3];
        x ^= x << 11;
        x ^= x >> 8;
        s_Xorstate[3] = s_Xorstate[2];
        s_Xorstate[2] = s_Xorstate[1];
        s_Xorstate[1] = s_Xorstate[0];
        s_Xorstate[0] ^= s_Xorstate[0] >> 19;
        s_Xorstate[0] ^= x;
    }
}

static uint32_t xorNext(void)
{
    uint32_t x = s_Xorstate[3];
    x ^= x << 11;
    x ^= x >> 8;
    s_Xorstate[3] = s_Xorstate[2];
    s_Xorstate[2] = s_Xorstate[1];
    s_Xorstate[1] = s_Xorstate[0];
    s_Xorstate[0] ^= s_Xorstate[0] >> 19;
    s_Xorstate[0] ^= x;
    return s_Xorstate[0];
}

static void generateUUID(u8 *out)
{
    xorInit();
    for (int i = 0; i < IDENTITY_UUID_LEN; i += 4) {
        uint32_t r = xorNext();
        out[i + 0] = (u8)(r >>  0);
        out[i + 1] = (u8)(r >>  8);
        out[i + 2] = (u8)(r >> 16);
        out[i + 3] = (u8)(r >> 24);
    }
}

/* -------------------------------------------------------------------------
 * Default identity
 * ------------------------------------------------------------------------- */

static void buildDefault(void)
{
    memset(&s_Identity, 0, sizeof(s_Identity));
    generateUUID(s_Identity.device_uuid);
    s_Identity.profile_count  = 1;
    s_Identity.active_profile = 0;

    strncpy(s_Identity.profiles[0].name, "Agent", IDENTITY_NAME_MAX - 1);
    /* SA-4: default appearance stored as catalog IDs (empty = use catalog default) */
    s_Identity.profiles[0].head_id[0] = '\0';
    s_Identity.profiles[0].body_id[0] = '\0';
    s_Identity.profiles[0].flags      = 0;
}

/* -------------------------------------------------------------------------
 * File I/O
 * ------------------------------------------------------------------------- */

static const char *identityFilePath(void)
{
    static char s_Path[512];
    if (s_Path[0]) return s_Path;

    char home[256];
    sysGetHomePath(home, sizeof(home));
    snprintf(s_Path, sizeof(s_Path), "%s/pd-identity.dat", home);
    return s_Path;
}

static int tryLoad(void)
{
    const char *path = identityFilePath();
    int need_resave = 0;
    int i;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    /* Read magic. */
    uint32_t magic = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != IDENTITY_MAGIC) {
        fclose(f);
        return 0;
    }

    /* Read version. */
    uint8_t ver = 0;
    if (fread(&ver, 1, 1, f) != 1 ||
        (ver != IDENTITY_FILE_VERSION && ver != IDENTITY_FILE_VERSION_V1)) {
        fclose(f);
        return 0;
    }

    /* Read UUID. */
    if (fread(s_Identity.device_uuid, IDENTITY_UUID_LEN, 1, f) != 1) {
        fclose(f);
        return 0;
    }

    /* Read profile_count, active_profile, pad. */
    uint8_t hdr[4] = {0};
    if (fread(hdr, 4, 1, f) != 1) {
        fclose(f);
        return 0;
    }
    s_Identity.profile_count  = hdr[0];
    s_Identity.active_profile = hdr[1];
    /* hdr[2], hdr[3] = reserved */

    if (s_Identity.profile_count == 0 ||
        s_Identity.profile_count > IDENTITY_MAX_PROFILES) {
        fclose(f);
        return 0;
    }
    if (s_Identity.active_profile >= s_Identity.profile_count) {
        s_Identity.active_profile = 0;
    }

    if (ver == IDENTITY_FILE_VERSION_V1) {
        /* SA-4: legacy v1 format — each profile is 20 bytes with u8 headnum/bodynum. */
        sysLogPrintf(LOG_NOTE, "IDENTITY: v1 file detected — migrating to v2 (string IDs)");
        need_resave = 1;
        for (i = 0; i < (int)s_Identity.profile_count; i++) {
            uint8_t buf[IDENTITY_PROFILE_SIZE_V1];
            const char *resolved_head;
            const char *resolved_body;
            uint8_t headnum, bodynum;
            if (fread(buf, IDENTITY_PROFILE_SIZE_V1, 1, f) != 1) {
                fclose(f);
                return 0;
            }
            memcpy(s_Identity.profiles[i].name, buf, IDENTITY_NAME_MAX);
            s_Identity.profiles[i].name[IDENTITY_NAME_MAX - 1] = '\0';
            headnum = buf[16];
            bodynum = buf[17];
            s_Identity.profiles[i].flags = buf[18];
            /* FIX-20: Resolve legacy integers to catalog string IDs.
             * Old identity format stored mpheadnum/mpbodynum (g_MpHeads[]/g_MpBodies[]
             * position indices, range 0..75/0..62).  catalogResolveByRuntimeIndex uses
             * g_HeadsAndBodies[] index (bodynum/headnum, range 0..151) — a different
             * index domain.  Use the mp-index resolvers which do the conversion. */
            resolved_head = catalogResolveHeadByMpIndex((s32)headnum);
            resolved_body = catalogResolveBodyByMpIndex((s32)bodynum);
            if (resolved_head) {
                strncpy(s_Identity.profiles[i].head_id, resolved_head, CATALOG_ID_LEN - 1);
                s_Identity.profiles[i].head_id[CATALOG_ID_LEN - 1] = '\0';
            } else {
                s_Identity.profiles[i].head_id[0] = '\0';
            }
            if (resolved_body) {
                strncpy(s_Identity.profiles[i].body_id, resolved_body, CATALOG_ID_LEN - 1);
                s_Identity.profiles[i].body_id[CATALOG_ID_LEN - 1] = '\0';
            } else {
                s_Identity.profiles[i].body_id[0] = '\0';
            }
        }
    } else {
        /* v2 format — read string IDs directly. */
        for (i = 0; i < (int)s_Identity.profile_count; i++) {
            uint8_t buf[IDENTITY_PROFILE_SIZE];
            if (fread(buf, IDENTITY_PROFILE_SIZE, 1, f) != 1) {
                fclose(f);
                return 0;
            }
            memcpy(s_Identity.profiles[i].name, buf, IDENTITY_NAME_MAX);
            s_Identity.profiles[i].name[IDENTITY_NAME_MAX - 1] = '\0';
            memcpy(s_Identity.profiles[i].head_id, buf + IDENTITY_NAME_MAX, CATALOG_ID_LEN);
            s_Identity.profiles[i].head_id[CATALOG_ID_LEN - 1] = '\0';
            memcpy(s_Identity.profiles[i].body_id,
                   buf + IDENTITY_NAME_MAX + CATALOG_ID_LEN, CATALOG_ID_LEN);
            s_Identity.profiles[i].body_id[CATALOG_ID_LEN - 1] = '\0';
            s_Identity.profiles[i].flags = buf[IDENTITY_NAME_MAX + CATALOG_ID_LEN + CATALOG_ID_LEN];
        }
    }

    fclose(f);

    /* Re-save in v2 format if we migrated from v1. */
    if (need_resave) {
        identitySave();
        sysLogPrintf(LOG_NOTE, "IDENTITY: migration complete — file re-saved in v2 format");
    }

    return 1;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void identityInit(void)
{
    if (s_Loaded) return;

    if (!tryLoad()) {
        sysLogPrintf(LOG_NOTE, "IDENTITY: no valid pd-identity.dat, creating new identity");
        buildDefault();
        identitySave();
    } else {
        char uuidStr[33];
        identityFormatUUID(uuidStr, sizeof(uuidStr));
        sysLogPrintf(LOG_NOTE, "IDENTITY: loaded, UUID=%s profiles=%u active=%u",
                     uuidStr,
                     (unsigned)s_Identity.profile_count,
                     (unsigned)s_Identity.active_profile);
    }

    s_Loaded = 1;
}

void identitySave(void)
{
    const char *path = identityFilePath();
    FILE *f = fopen(path, "wb");
    if (!f) {
        sysLogPrintf(LOG_WARNING, "IDENTITY: could not write %s", path);
        return;
    }

    int ok = 1;

    /* Magic */
    uint32_t magic = IDENTITY_MAGIC;
    ok &= (fwrite(&magic, 4, 1, f) == 1);

    /* Version */
    uint8_t ver = IDENTITY_FILE_VERSION;
    ok &= (fwrite(&ver, 1, 1, f) == 1);

    /* UUID */
    ok &= (fwrite(s_Identity.device_uuid, IDENTITY_UUID_LEN, 1, f) == 1);

    /* profile_count, active_profile, pad, pad */
    uint8_t hdr[4] = {
        s_Identity.profile_count,
        s_Identity.active_profile,
        0, 0
    };
    ok &= (fwrite(hdr, 4, 1, f) == 1);

    /* Profiles (v2 format: name + head_id + body_id + flags + pad) */
    for (int i = 0; i < (int)s_Identity.profile_count; i++) {
        uint8_t buf[IDENTITY_PROFILE_SIZE];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, s_Identity.profiles[i].name, IDENTITY_NAME_MAX);
        memcpy(buf + IDENTITY_NAME_MAX,
               s_Identity.profiles[i].head_id, CATALOG_ID_LEN);
        memcpy(buf + IDENTITY_NAME_MAX + CATALOG_ID_LEN,
               s_Identity.profiles[i].body_id, CATALOG_ID_LEN);
        buf[IDENTITY_NAME_MAX + CATALOG_ID_LEN + CATALOG_ID_LEN] =
               s_Identity.profiles[i].flags;
        /* remaining bytes = _pad = 0 */
        ok &= (fwrite(buf, IDENTITY_PROFILE_SIZE, 1, f) == 1);
    }

    fclose(f);
    if (!ok) {
        sysLogPrintf(LOG_WARNING, "IDENTITY: write error saving %s", path);
    }
}

pd_identity_t *identityGet(void)
{
    return &s_Identity;
}

identity_profile_t *identityGetActiveProfile(void)
{
    u8 idx = s_Identity.active_profile;
    if (idx >= s_Identity.profile_count) idx = 0;
    return &s_Identity.profiles[idx];
}

void identityFormatUUID(char *buf, u32 buflen)
{
    if (!buf || buflen < 33) return;
    const u8 *u = s_Identity.device_uuid;
    snprintf(buf, buflen,
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7],
             u[8],u[9],u[10],u[11],u[12],u[13],u[14],u[15]);
}
