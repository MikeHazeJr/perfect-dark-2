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

/* Master toggle (F8) */
static bool s_HotswapActive = true;

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
    s_HotswapActive = true;
    s_ActiveMenuName = nullptr;
    s_RenderingThisFrame = false;

    sysLogPrintf(LOG_NOTE, "pdgui_hotswap: Initialized (F8 to toggle)");
}

void pdguiHotswapShutdown(void)
{
    s_EntryCount = 0;
    s_QueueCount = 0;
    s_HotswapActive = false;
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

    /* If a dialog is registered with a NULL renderFn, it means
     * "force PD native rendering" — skip type fallback entirely.
     * This is used for dialogs that need special PD handlers (e.g.,
     * keyboard input, custom rendering) that our generic ImGui
     * type renderers can't handle. */
    if (entry && !entry->renderFn) {
        return 0;
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

    /* Determine whether to use NEW (ImGui) or OLD (PD native) */
    bool useNew = false;
    if (entry->override == 1) {
        useNew = true;
    } else if (entry->override == -1) {
        useNew = false;
    } else {
        useNew = s_HotswapActive;
    }

    if (!useNew) {
        return 0;
    }

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

    /* Check override first */
    if (entry->override == 1) {
        return 1;  /* Forced NEW */
    } else if (entry->override == -1) {
        return 0;  /* Forced OLD */
    }

    return s_HotswapActive ? 1 : 0;
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

    /* Clear the queue for next frame */
    s_QueueCount = 0;
}

void pdguiHotswapToggle(void)
{
    s_HotswapActive = !s_HotswapActive;
    sysLogPrintf(LOG_NOTE, "pdgui_hotswap: %s",
                 s_HotswapActive ? "ON (ImGui menus)" : "OFF (PD native menus)");
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
    HotswapEntry *entry = findEntry(dialogdef);
    if (entry) {
        entry->override = override;
    }
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
        if (s_HotswapActive) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "[NEW]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "[OLD]");
        }

        if (s_ActiveMenuName && s_RenderingThisFrame) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", s_ActiveMenuName);
        }

        ImGui::TextDisabled("F8 to toggle");

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
