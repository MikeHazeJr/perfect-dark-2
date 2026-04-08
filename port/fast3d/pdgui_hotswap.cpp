/**
 * pdgui_hotswap.cpp -- In-game OLD/NEW menu hot-swap system.
 *
 * F8 toggles between original PD menu rendering and ImGui replacements.
 * Menus register their ImGui versions at init time.  During gameplay,
 * menuRenderDialog() in menu.c calls pdguiHotswapCheck() — if a
 * replacement exists and hot-swap is active, the PD native render is
 * suppressed and the dialog is queued for ImGui rendering.  The actual
 * ImGui drawing happens later during pdguiRender() → pdguiHotswapRenderQueued().
 *
 * This two-phase approach is necessary because:
 *   1. menuRenderDialog() runs inside the GBI/display-list phase
 *   2. ImGui rendering runs AFTER the GBI phase, in its own OpenGL pass
 *   3. Calling ImGui functions from inside the GBI phase would corrupt state
 *
 * IMPORTANT: This file is C++ and must NOT include types.h (which #defines
 * bool as s32, breaking C++ bool).  Use extern "C" forward declarations
 * for any game symbols needed.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdmain.h"
#include "screenmfst.h"
#include "system.h"

/* ========================================================================
 * Forward declarations for game symbols (extern "C" to avoid types.h)
 * ======================================================================== */

extern "C" {
    void *videoGetWindowHandle(void);
}

/* ========================================================================
 * Registry
 * ======================================================================== */

#define HOTSWAP_MAX_ENTRIES 128
#define HOTSWAP_MAX_QUEUED  8  /* Max dialogs that can be hot-swapped per frame */

struct HotswapEntry {
    struct menudialogdef *dialogdef;
    PdguiMenuRenderFn     renderFn;
    const char           *name;
    s32                   override;  /* 0=follow global, 1=force NEW, -1=force OLD */
};

/* Per-frame render queue: dialogs that were suppressed during GBI phase
 * and need ImGui rendering during the overlay phase. */
struct QueuedRender {
    HotswapEntry       *entry;
    struct menudialog  *dialog;
    struct menu        *menu;
};

static HotswapEntry s_Entries[HOTSWAP_MAX_ENTRIES];
static s32          s_EntryCount = 0;

static QueuedRender s_Queue[HOTSWAP_MAX_QUEUED];
static s32          s_QueueCount = 0;

/* P10 D5.7: ImGui is the sole menu system — hotswap permanently ON.
 * F8 toggle removed; override mechanism removed. */
static const bool s_HotswapActive = true;

/* For badge display */
static const char *s_ActiveMenuName = nullptr;
static bool        s_RenderingThisFrame = false;

/* Persistent flag: true if hotswap menus were rendered last frame.
 * Used by pdguiIsActive() to suppress player movement during hot-swapped menus.
 * The queue (s_QueueCount) is cleared after each render, so it's 0 during
 * the next frame's input polling phase — this flag bridges that gap. */
static bool s_HotswapMenuWasActive = false;

/* Type-based fallback renderers.
 * When no definition-specific entry matches, we check the dialog's type field
 * against these fallbacks. Max 8 types (0-7). */
#define HOTSWAP_MAX_TYPES 8
struct TypeFallback {
    PdguiMenuRenderFn renderFn;
    const char       *name;
};
static TypeFallback s_TypeFallbacks[HOTSWAP_MAX_TYPES] = {};
static bool s_TypeFallbacksInited = false;

/* ========================================================================
 * Lookup
 * ======================================================================== */

static HotswapEntry *findEntry(struct menudialogdef *dialogdef)
{
    for (s32 i = 0; i < s_EntryCount; i++) {
        if (s_Entries[i].dialogdef == dialogdef) {
            return &s_Entries[i];
        }
    }
    return nullptr;
}

/* ========================================================================
 * C-callable API
 * ======================================================================== */

extern "C" {

void pdguiHotswapInit(void)
{
    memset(s_Entries, 0, sizeof(s_Entries));
    memset(s_TypeFallbacks, 0, sizeof(s_TypeFallbacks));
    s_EntryCount = 0;
    s_QueueCount = 0;
    /* s_HotswapActive is const true — P10 D5.7 */
    s_ActiveMenuName = nullptr;
    s_RenderingThisFrame = false;

    sysLogPrintf(LOG_NOTE, "pdgui_hotswap: Initialized (ImGui sole menu system)");
}

void pdguiHotswapShutdown(void)
{
    screenManifestShutdown();
    s_EntryCount = 0;
    s_QueueCount = 0;
    /* s_HotswapActive is const true — no shutdown reset needed */
}

s32 pdguiHotswapRegister(struct menudialogdef *dialogdef,
                          PdguiMenuRenderFn renderFn,
                          const char *name)
{
    if (!dialogdef) {
        return -1;
    }
    /* renderFn may be NULL — this means "force PD native rendering"
     * for this dialog, bypassing any type-based fallback. */

    /* Check for duplicate */
    HotswapEntry *existing = findEntry(dialogdef);
    if (existing) {
        existing->renderFn = renderFn;
        existing->name = name;
        sysLogPrintf(LOG_NOTE, "pdgui_hotswap: Updated '%s'", name ? name : "?");
        return (s32)(existing - s_Entries);
    }

    if (s_EntryCount >= HOTSWAP_MAX_ENTRIES) {
        sysLogPrintf(LOG_NOTE, "pdgui_hotswap: Registry full, can't add '%s'",
                     name ? name : "?");
        return -1;
    }

    s32 idx = s_EntryCount++;
    s_Entries[idx].dialogdef = dialogdef;
    s_Entries[idx].renderFn = renderFn;
    s_Entries[idx].name = name;
    s_Entries[idx].override = 0;

    sysLogPrintf(LOG_NOTE, "pdgui_hotswap: Registered '%s' (%d/%d)",
                 name ? name : "?", s_EntryCount, HOTSWAP_MAX_ENTRIES);
    return idx;
}

s32 pdguiHotswapRegisterType(u8 dialogType,
                              PdguiMenuRenderFn renderFn,
                              const char *name)
{
    if (dialogType >= HOTSWAP_MAX_TYPES || !renderFn) {
        return -1;
    }

    s_TypeFallbacks[dialogType].renderFn = renderFn;
    s_TypeFallbacks[dialogType].name = name;

    sysLogPrintf(LOG_NOTE, "pdgui_hotswap: Registered type %d fallback '%s'",
                 dialogType, name ? name : "?");
    return 0;
}

s32 pdguiHotswapCheck(struct menudialogdef *dialogdef,
                       struct menudialog *dialog,
                       struct menu *menu)
{
    if (!dialogdef) {
        return 0;
    }

    HotswapEntry *entry = findEntry(dialogdef);

    /* P10 D5.7: NULL-renderFn forced-native pattern removed.
     * All dialogs go through ImGui. If an entry exists with a NULL renderFn,
     * fall through to type fallback. */
    if (entry && !entry->renderFn) {
        entry = nullptr; /* let type fallback handle it */
    }

    /* If no definition-specific match, try type-based fallback.
     * menudialogdef is an incomplete type here (can't include types.h),
     * but the 'type' field is a u8 at offset 0x00 of the struct. */
    if (!entry) {
        u8 dtype = *(u8 *)dialogdef;
        if (dtype < HOTSWAP_MAX_TYPES && s_TypeFallbacks[dtype].renderFn) {
            /* Create a temporary entry for the queue. Use a small pool
             * so multiple type-based dialogs can be queued per frame. */
            static HotswapEntry s_TypeEntryPool[HOTSWAP_MAX_QUEUED];
            static s32 s_TypeEntryIdx = 0;
            if (s_TypeEntryIdx >= HOTSWAP_MAX_QUEUED) s_TypeEntryIdx = 0;

            HotswapEntry *te = &s_TypeEntryPool[s_TypeEntryIdx++];
            te->dialogdef = dialogdef;
            te->renderFn = s_TypeFallbacks[dtype].renderFn;
            te->name = s_TypeFallbacks[dtype].name;
            te->override = 0;
            entry = te;
        }
    }

    if (!entry) {
        return 0;  /* No ImGui replacement — use PD native */
    }

    /* P10 D5.7: ImGui always active — no override check needed */

    /* Queue this dialog for ImGui rendering during the overlay phase.
     * Deduplicate: if this entry is already queued (same dialogdef rendered
     * twice per frame, e.g. as both "other" and "curdialog" in menuRenderDialogs),
     * update the existing queue slot instead of adding a duplicate. */
    {
        bool alreadyQueued = false;
        for (s32 qi = 0; qi < s_QueueCount; qi++) {
            if (s_Queue[qi].entry == entry) {
                /* Update with latest dialog/menu pointers */
                s_Queue[qi].dialog = dialog;
                s_Queue[qi].menu = menu;
                alreadyQueued = true;
                break;
            }
        }
        if (!alreadyQueued && s_QueueCount < HOTSWAP_MAX_QUEUED) {
            QueuedRender *qr = &s_Queue[s_QueueCount++];
            qr->entry = entry;
            qr->dialog = dialog;
            qr->menu = menu;
        }
    }

    /* Return 1 = suppress PD native render for this dialog */
    return 1;
}

s32 pdguiHotswapIsDialogSwapped(struct menudialogdef *dialogdef)
{
    if (!dialogdef) {
        return 0;
    }

    HotswapEntry *entry = findEntry(dialogdef);
    if (!entry) {
        return 0;
    }

    /* P10 D5.7: ImGui always active */
    return entry ? 1 : 0;
}

/**
 * Render all queued hot-swap dialogs.
 * Called from pdguiRender() during the ImGui overlay phase.
 */
void pdguiHotswapRenderQueued(s32 winW, s32 winH)
{
    /* Update persistent flag BEFORE clearing the queue.
     * This flag stays true until the next frame's render pass,
     * so input polling (which happens before GBI) can see it. */
    s_HotswapMenuWasActive = (s_QueueCount > 0);

    for (s32 i = 0; i < s_QueueCount; i++) {
        QueuedRender *qr = &s_Queue[i];

        s32 handled = qr->entry->renderFn(qr->dialog, qr->menu, winW, winH);

        if (handled) {
            s_ActiveMenuName = qr->entry->name;
            s_RenderingThisFrame = true;
        }
    }

    /* Phase 6: notify screen manifest system which dialogs were active.
     * Collect dialogdef pointers before clearing the queue.
     * FIX-PLAYTEST-4: Skip screenManifestTick on frame 0/1 of a new stage.
     * When a player dies/exits to CI hub, the stage transition frees menu
     * memory.  On frame 1, s_HotswapMenuWasActive is still true from the
     * last frame (keeping us in the render path), but s_Queue is empty.
     * Calling screenManifestTick with count=0 triggers "leave" events that
     * call catalogUnloadAsset while the catalog is reinitialising for the
     * new stage → crash.  Defer the tick until lvframe60 >= 2. */
    {
        s32 lvframe = pdmainGetLvFrame60();
        if (lvframe >= 2) {
            void *active_defs[HOTSWAP_MAX_QUEUED];
            s32 n = 0;
            for (s32 qi = 0; qi < s_QueueCount; qi++) {
                if (s_Queue[qi].entry && s_Queue[qi].entry->dialogdef) {
                    active_defs[n++] = (void*)s_Queue[qi].entry->dialogdef;
                }
            }
            screenManifestTick(active_defs, n);
        }
    }

    /* Clear the queue for next frame */
    s_QueueCount = 0;
}

void pdguiHotswapToggle(void)
{
    /* P10 D5.7: ImGui is the sole menu system — toggle disabled.
     * F8 now does nothing. */
    sysLogPrintf(LOG_NOTE, "pdgui_hotswap: toggle ignored (ImGui is sole menu system)");
}

s32 pdguiHotswapIsActive(void)
{
    return s_HotswapActive ? 1 : 0;
}

/** Returns 1 if there are queued dialogs awaiting ImGui render this frame. */
s32 pdguiHotswapHasQueued(void)
{
    return s_QueueCount > 0 ? 1 : 0;
}

/** Returns 1 if hot-swap menus were rendered in the most recent frame.
 * Unlike pdguiHotswapHasQueued() (which is only >0 during the GBI/render phase),
 * this flag persists through the next frame's input polling phase, making it
 * reliable for input suppression decisions. */
s32 pdguiHotswapWasActive(void)
{
    return s_HotswapMenuWasActive ? 1 : 0;
}

void pdguiHotswapSetOverride(struct menudialogdef *dialogdef, s32 override)
{
    /* P10 D5.7: override mechanism removed — ImGui always active.
     * This function is kept as a no-op for API compatibility. */
    (void)dialogdef;
    (void)override;
}

void pdguiHotswapRenderBadge(s32 winW, s32 winH)
{
    if (s_EntryCount == 0) {
        return;  /* Nothing registered — no badge needed */
    }

    /* Only show when a game menu is actually active (queued or was rendered) */
    /* Position: top-right corner */
    float padX = 12.0f;
    float padY = 12.0f;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_AlwaysAutoResize
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoNav
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowPos(ImVec2((float)winW - padX, padY),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.65f);

    if (ImGui::Begin("##hotswap_badge", nullptr, flags)) {
        /* P10 D5.7: ImGui is sole menu system — always show [NEW] */
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "[ImGui]");

        if (s_ActiveMenuName && s_RenderingThisFrame) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", s_ActiveMenuName);
        }

        if (s_EntryCount > 0) {
            ImGui::TextDisabled("%d menu%s ready",
                               s_EntryCount,
                               s_EntryCount == 1 ? "" : "s");
        }
    }
    ImGui::End();

    /* Reset per-frame state */
    s_RenderingThisFrame = false;
}

s32 pdguiHotswapRegisteredCount(void)
{
    return s_EntryCount;
}

s32 pdguiHotswapTotalMenuCount(void)
{
    return 113;
}

} /* extern "C" */
