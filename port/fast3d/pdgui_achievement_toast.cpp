/**
 * pdgui_achievement_toast.cpp -- Slide-in toast notifications for newly
 * unlocked achievements (D6 Phase 3).
 *
 * Reads from the D6 achievements module via `achievementGetNewlyUnlocked`
 * (which clears the "new" state on each call, so one poll == one toast
 * per achievement).  Renders a stacked list of toasts in the top-right
 * corner on the ImGui foreground drawlist.  Each toast slides in,
 * lingers, then fades out.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/fast3d/*.cpp).
 */

#include <PR/ultratypes.h>
#include <cstdio>
#include <cstring>

#include "imgui/imgui.h"
#include "pdgui_achievement_toast.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"

extern "C" {

/* D6 achievements API */
typedef struct {
    const char *id;
    const char *name;
    const char *description;
    s32 condition;
    u64 threshold;
    const char *stat_key;
} achievement_def_t_cpp;
const void *achievementGetByIndex(s32 idx);
s32 achievementGetCount(void);
s32 achievementGetNewlyUnlocked(const char **ids_out, s32 max_out);
s32 achievementIsUnlocked(const char *id);

} /* extern "C" */

/* ========================================================================
 * State
 * ======================================================================== */

#define TOAST_MAX       4
#define TOAST_LIFETIME  270   /* frames @ 60Hz: 4.5 s */
#define TOAST_FADEIN    20
#define TOAST_FADEOUT   60

struct Toast {
    char name[64];
    char description[128];
    s32  age;       /* frames since push */
    s32  active;
};

static Toast s_Toasts[TOAST_MAX];
static s32   s_NumToasts = 0;

/* ========================================================================
 * Public API
 * ======================================================================== */

extern "C" void pdguiAchievementToastPush(const char *name, const char *description)
{
    if (s_NumToasts >= TOAST_MAX) return;
    Toast *t = &s_Toasts[s_NumToasts++];
    t->active = 1;
    t->age = 0;
    if (name) {
        strncpy(t->name, name, sizeof(t->name) - 1);
        t->name[sizeof(t->name) - 1] = '\0';
    } else {
        t->name[0] = '\0';
    }
    if (description) {
        strncpy(t->description, description, sizeof(t->description) - 1);
        t->description[sizeof(t->description) - 1] = '\0';
    } else {
        t->description[0] = '\0';
    }
    /* Play a bright cue; SND_FOCUS is the menu "accent" tone and is a
     * good proxy until a dedicated achievement sfx ships. */
    pdguiPlaySound(PDGUI_SND_FOCUS);
}

extern "C" void pdguiAchievementToastPollUnlocks(void)
{
    const char *ids[TOAST_MAX];
    s32 count = achievementGetNewlyUnlocked(ids, TOAST_MAX);
    if (count <= 0) return;

    s32 n = achievementGetCount();
    for (s32 i = 0; i < count; i++) {
        const char *id = ids[i];
        if (!id) continue;
        for (s32 j = 0; j < n; j++) {
            const achievement_def_t_cpp *a =
                (const achievement_def_t_cpp *)achievementGetByIndex(j);
            if (!a || !a->id) continue;
            if (strcmp(a->id, id) == 0) {
                pdguiAchievementToastPush(a->name, a->description);
                break;
            }
        }
    }
}

extern "C" void pdguiAchievementToastRender(s32 winW, s32 winH)
{
    if (s_NumToasts == 0) return;

    float scale = pdguiScaleFactor();
    float toastW = 340.0f * scale;
    float toastH = 60.0f * scale;
    float pad    = 10.0f * scale;

    ImDrawList *dl = ImGui::GetForegroundDrawList();

    /* Reap expired toasts in-place before rendering so the visible stack
     * compresses as earlier toasts time out. */
    s32 w = 0;
    for (s32 r = 0; r < s_NumToasts; r++) {
        if (s_Toasts[r].active && s_Toasts[r].age < TOAST_LIFETIME) {
            if (w != r) s_Toasts[w] = s_Toasts[r];
            w++;
        }
    }
    s_NumToasts = w;

    float y = pad + 80.0f * scale;  /* clear of title bar region */
    for (s32 i = 0; i < s_NumToasts; i++) {
        Toast *t = &s_Toasts[i];
        t->age++;

        /* Alpha envelope: fade in -> hold -> fade out. */
        f32 a = 1.0f;
        if (t->age < TOAST_FADEIN) {
            a = (f32)t->age / (f32)TOAST_FADEIN;
        } else if (t->age > TOAST_LIFETIME - TOAST_FADEOUT) {
            s32 remaining = TOAST_LIFETIME - t->age;
            if (remaining <= 0) { a = 0.0f; }
            else                { a = (f32)remaining / (f32)TOAST_FADEOUT; }
        }
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;

        /* Slide-in offset so toasts glide in from the right edge. */
        f32 slide = 0.0f;
        if (t->age < TOAST_FADEIN) {
            f32 p = 1.0f - (f32)t->age / (f32)TOAST_FADEIN;
            slide = p * toastW * 0.4f;
        }

        float x = (float)winW - toastW - pad + slide;
        ImVec2 tl(x, y);
        ImVec2 br(x + toastW, y + toastH);

        u32 bgCol     = pdguiPalImU32(PDPAL_TITLEBG,   (s32)(210.0f * a));
        u32 borderCol = pdguiImU32TintSuccess((s32)(240.0f * a));
        u32 titleCol  = pdguiImU32TitleGlow((s32)(255.0f * a));
        u32 descCol   = IM_COL32(210, 220, 230, (s32)(210.0f * a));
        u32 tagCol    = pdguiImU32TintSuccess((s32)(255.0f * a));

        /* Shadow. */
        dl->AddRectFilled(ImVec2(tl.x + 3.0f * scale, tl.y + 3.0f * scale),
                          ImVec2(br.x + 3.0f * scale, br.y + 3.0f * scale),
                          IM_COL32(0, 0, 0, (s32)(140.0f * a)),
                          4.0f * scale);
        dl->AddRectFilled(tl, br, bgCol, 4.0f * scale);
        dl->AddRect(tl, br, borderCol, 4.0f * scale, 0, 2.0f * scale);

        /* Accent strip on the left edge. */
        dl->AddRectFilled(ImVec2(tl.x, tl.y),
                          ImVec2(tl.x + 4.0f * scale, br.y),
                          tagCol, 4.0f * scale);

        /* Row 1: "ACHIEVEMENT UNLOCKED" tag + name. */
        const char *tagTxt = "ACHIEVEMENT UNLOCKED";
        ImVec2 tagSz = ImGui::CalcTextSize(tagTxt);
        dl->AddText(ImVec2(tl.x + 12.0f * scale, tl.y + 6.0f * scale),
                    tagCol, tagTxt);

        /* Row 2: achievement name (bold-ish via glow color). */
        dl->AddText(ImVec2(tl.x + 12.0f * scale, tl.y + 6.0f * scale + tagSz.y + 2.0f * scale),
                    titleCol, t->name);

        /* Row 3: description, truncated with ellipsis if overflow. */
        char descBuf[132];
        snprintf(descBuf, sizeof(descBuf), "%s", t->description);
        float avail = toastW - 24.0f * scale;
        ImVec2 dSz = ImGui::CalcTextSize(descBuf);
        if (dSz.x > avail) {
            /* rough char-clip by shaving the tail; good enough for toast. */
            s32 len = (s32)strlen(descBuf);
            while (len > 4 && ImGui::CalcTextSize(descBuf).x > avail) {
                descBuf[--len] = '\0';
            }
            if (len > 4) {
                descBuf[len - 1] = '.';
                descBuf[len - 2] = '.';
                descBuf[len - 3] = '.';
            }
        }
        dl->AddText(ImVec2(tl.x + 12.0f * scale, br.y - 18.0f * scale),
                    descCol, descBuf);

        y += toastH + pad;
    }
}
