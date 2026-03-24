/*
 * botvariant.c -- Bot Variant creation and persistence (D3R-8)
 *
 * Saves custom bot configurations as component .ini files and registers
 * them in the Asset Catalog for immediate in-session use.
 *
 * Save path: {modsdir}/bot_variants/{slug}/bot.ini
 *
 * Unlike mod components (which live under mod_*/_components/), bot variants
 * created by the in-game customizer live directly under mods/bot_variants/.
 * The scanner picks them up at next launch via assetCatalogScanBotVariants().
 * The catalog hot-register makes them available immediately in the same session.
 *
 * Auto-discovered by CMake GLOB_RECURSE for port/src/*.c.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include <PR/ultratypes.h>
#include "types.h"
#include "botvariant.h"
#include "modmgr.h"
#include "assetcatalog.h"
#include "fs.h"
#include "system.h"

s32 botVariantSave(const char *name,
                   const char *base_type,
                   f32 accuracy,
                   f32 reaction_time,
                   f32 aggression,
                   const char *category,
                   const char *description,
                   const char *author)
{
    const char *modsdir = modmgrGetModsDir();
    if (!modsdir || !modsdir[0]) {
        sysLogPrintf(LOG_ERROR, "botVariantSave: no mods directory");
        return 0;
    }

    if (!name || !name[0]) {
        sysLogPrintf(LOG_ERROR, "botVariantSave: empty name");
        return 0;
    }

    /* Build slug: lowercase, spaces → underscores, strip non-alphanumeric */
    char slug[CATALOG_ID_LEN];
    {
        const char *src = name;
        char *dst = slug;
        char *slugEnd = slug + sizeof(slug) - 1;
        while (*src && dst < slugEnd) {
            if (isalnum((u8)*src)) {
                *dst++ = (char)tolower((u8)*src);
            } else if (*src == ' ' || *src == '_') {
                /* Collapse consecutive underscores */
                if (dst > slug && *(dst - 1) != '_') {
                    *dst++ = '_';
                }
            }
            src++;
        }
        /* Strip trailing underscore */
        while (dst > slug && *(dst - 1) == '_') {
            dst--;
        }
        *dst = '\0';
    }

    if (slug[0] == '\0') {
        sysLogPrintf(LOG_ERROR, "botVariantSave: slug empty after sanitizing name '%s'", name);
        return 0;
    }

    /* Clamp trait values to [0, 1] */
    if (accuracy     < 0.0f) accuracy     = 0.0f;
    if (accuracy     > 1.0f) accuracy     = 1.0f;
    if (reaction_time < 0.0f) reaction_time = 0.0f;
    if (reaction_time > 1.0f) reaction_time = 1.0f;
    if (aggression   < 0.0f) aggression   = 0.0f;
    if (aggression   > 1.0f) aggression   = 1.0f;

    /* Build paths */
    char bot_variants_dir[FS_MAXPATH];
    snprintf(bot_variants_dir, sizeof(bot_variants_dir), "%s/bot_variants", modsdir);

    char component_dir[FS_MAXPATH];
    snprintf(component_dir, sizeof(component_dir), "%s/%s", bot_variants_dir, slug);

    char ini_path[FS_MAXPATH];
    snprintf(ini_path, sizeof(ini_path), "%s/bot.ini", component_dir);

    /* Create directories (fsCreateDir is idempotent if they already exist) */
    fsCreateDir(bot_variants_dir);
    fsCreateDir(component_dir);

    /* Write bot.ini */
    FILE *fp = fopen(ini_path, "w");
    if (!fp) {
        sysLogPrintf(LOG_ERROR, "botVariantSave: cannot write '%s'", ini_path);
        return 0;
    }

    const char *cat  = (category    && category[0])    ? category    : "custom";
    const char *bt   = (base_type   && base_type[0])   ? base_type   : "NormalSim";
    const char *auth = (author      && author[0])      ? author      : "Player";

    fprintf(fp, "[bot_variant]\n");
    fprintf(fp, "name = %s\n", name);
    fprintf(fp, "category = %s\n", cat);
    fprintf(fp, "base_type = %s\n", bt);
    fprintf(fp, "accuracy = %.3f\n", accuracy);
    fprintf(fp, "reaction_time = %.3f\n", reaction_time);
    fprintf(fp, "aggression = %.3f\n", aggression);
    if (description && description[0]) {
        fprintf(fp, "description = %s\n", description);
    }
    fprintf(fp, "author = %s\n", auth);
    fprintf(fp, "version = 1.0.0\n");
    fclose(fp);

    /* Hot-register in catalog so the preset is immediately available this session */
    asset_entry_t *e = assetCatalogRegisterBotVariant(slug, bt, accuracy,
                                                       reaction_time, aggression);
    if (!e) {
        sysLogPrintf(LOG_ERROR, "botVariantSave: catalog registration failed for '%s'", slug);
        return 0;
    }
    e->enabled   = 1;
    e->bundled   = 0;
    e->temporary = 0;
    strncpy(e->category, cat, CATALOG_CATEGORY_LEN - 1);
    e->category[CATALOG_CATEGORY_LEN - 1] = '\0';
    strncpy(e->dirpath, component_dir, FS_MAXPATH - 1);
    e->dirpath[FS_MAXPATH - 1] = '\0';

    sysLogPrintf(LOG_NOTE,
        "botVariantSave: '%s' → '%s' (acc=%.2f rt=%.2f agg=%.2f)",
        slug, ini_path, accuracy, reaction_time, aggression);
    return 1;
}
