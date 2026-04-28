/**
 * pdgui_backend.cpp — Dear ImGui integration backend for the PD PC port.
 *
 * This file implements the C-callable pdgui API (declared in pdgui.h) using
 * the ImGui C++ API with SDL2 + OpenGL3 backends. It lives in fast3d/ alongside
 * the other rendering code because it directly interfaces with SDL2 and OpenGL.
 *
 * The GLOB_RECURSE for port/*.cpp in CMakeLists.txt will auto-discover this file.
 *
 * Part of Sub-Phase D3.4: Menu System Modernization.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string>
#include <unordered_map>

#include "glad/glad.h"
#include <string.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_opengl3.h"

/* PD-authentic style — colors, metrics, shimmer effects */
#include "pdgui_style.h"

/* D5.0 ROM texture decode layer + theme draw functions */
#include "pdgui_theme.h"
#include "pdgui_theme_loader.h"
#include "pdgui_nineslice.h"
#include "pdgui_effects.h"
#include "pdgui_fontmgr.h"
#include "pdgui_font_mod.h"  /* S305: user font mod discovery + Video.FontId */

/* D5.1 input ownership boundary */
#include "pdmain.h"

/* Modal scrim coalescing (pdguiPopupDarken*) */
#include "pdgui_layout.h"

/* F12 debug menu */
#include "pdgui_debugmenu.h"

/* Forge level editor (F0+) */
#include "pdgui_forge.h"
#include "pdgui_interact_prompt.h"

/* F8 in-game menu hot-swap */
#include "pdgui_hotswap.h"

/* F9 menu / input-context diagnostics (read-only overlay) */
#include "pdgui_menu_stack_debug.h"

/* D5.1: Input context stack — routes events between game and ImGui */
#include "inputctx.h"
#include "pdgui_menus.h"
#include "pdgui_charpreview.h"

/* D5 Phase 2: Gamepad navigation helpers (wrap, accept/cancel, device detect) */
#include "pdgui_nav.h"

/* M0.2 Phase A: Core action map system */
#include "actionmap.h"
#include "config.h"
#include "imgui/imgui_internal.h"

/* Lobby sidebar — declared in pdgui_lobby.cpp */
extern "C" void pdguiLobbyRender(s32 winW, s32 winH);
extern "C" void pdguiUpdateRender(void);
extern "C" s32  pdguiUpdateIsActive(void);

/* D3R-7: Modding Hub standalone window — declared in pdgui_menu_moddinghub.cpp */
extern "C" void pdguiModdingHubRender(s32 winW, s32 winH);
extern "C" s32  pdguiModdingHubIsVisible(void);

/* P5: Theme Editor — declared in pdgui_menu_theme_editor.cpp */
extern "C" void pdguiThemeEditorRender(s32 winW, s32 winH);

/* Log Viewer Dev Window tab — declared in pdgui_menu_logviewer.cpp */
extern "C" void pdguiLogViewerRender(s32 winW, s32 winH);

/* Pause menu + scorecard overlay — declared in pdgui_pausemenu.h */
#include "pdgui_pausemenu.h"
#include "pdgui_friends.h"
#include "pdgui_nat_diagnostics.h"
#include "pdgui_toast.h"
#include "pdgui_spectator.h"

/* In-match HUD overlay (top scorers + timer) */
#include "pdgui_hud.h"

/* ImGui subtitle renderer — replaces legacy hudmsg subtitle path */
#include "pdgui_subtitles.h"
#include "pdgui_achievement_toast.h"

/* Mesh collision debug (F10) — declared in gfx_sdl2.cpp */
extern "C" void meshDebugToggle(void);

/* Pause-menu Debug Shortcuts registry. Single source of truth for the
 * read-only shortcuts list shown by the pause menu's Debug Shortcuts
 * modal.  Each raw SDL hotkey if-block in pdguiProcessEvent (and the
 * Alt+Enter / backtick handlers in gfx_sdl2.cpp) is paired with a
 * pdguiDebugShortcutRegister call inside registerDebugShortcuts() below.
 * Adding a new raw hotkey?  Add a paired register call there.
 *
 * Forward decl for registerDebugShortcuts() lives inside the extern "C"
 * block alongside its definition (line 238+). */
#include "pdgui_debug_shortcuts.h"

/* MP In-Game overlays: kill ticker + endscreen suppression */
extern "C" void pdguiMpIngameRender(s32 winW, s32 winH);

/* Pre-match countdown popup: 3-2-1-GO + cancel support */
extern "C" void pdguiCountdownRender(s32 winW, s32 winH);

/* Network mode query — declared in pdgui_bridge.c */
extern "C" s32 netGetMode(void);

extern "C" s32 pdguiPauseGetNormMplayerIsRunning(void);
extern "C" u8 pdguiPauseGetPaused(void);

/* Input system -- for deferred SDL mouse-lock flush (B-92 solo mission path) */
extern "C" s32 inputMouseIsLocked(void);

/* Handel Gothic — PD's original menu font, embedded as a C array */
#include "pdgui_font_handelgothic.h"

/* Resolution-independent scaling helpers */
#include "pdgui_scaling.h"
#include "pdgui_activemenu_radial.h"

/* Logging */
#include "system.h"

/* F6: freeze MP bot AI (game/bot.c) — banner + NewFrame gating */
extern "C" s32 botGetUpdatesDisabled(void);
extern "C" void botToggleUpdatesDisabled(void);

/* F7: dev invincibility toggle (game/player.c) */
extern "C" void playerToggleDevInvincibility(void);
extern "C" s32 playerDevInvincibilityHudActive(void);

/* Issue 5b (2026-04-24): Grid FREEFLY observer-controller suppression.
 * When the user flies as Dr Carroll in The Grid's edit mode, their chr
 * is a camera avatar, not a combat participant.  Gameplay paths (F7
 * invincibility cheat, interact prompt, etc.) that assume a normal
 * playing chr must short-circuit.  forgeIsFreefly() is defined in
 * src/game/forgemode.c and returns 1 while the player is in the free-
 * fly editor state. */
extern "C" s32 forgeIsFreefly(void);

namespace {
/**
 * Shared predicate for pdguiNewFrame / pdguiRender early-exit: must stay in sync
 * with whether we skip the legacy active-menu GBI path (pdguiActiveMenuIsOpen).
 */
static bool pdguiAnyStandardOverlayReason(
    bool debugOverlayActive,
    bool menuStackDiag,
    bool networkActive,
    bool pauseActive,
    bool hubActive,
    bool interactPrompt,
    bool devGameplayHud)
{
    if (debugOverlayActive || menuStackDiag || networkActive || pauseActive || hubActive || interactPrompt
            || devGameplayHud) {
        return true;
    }
    /* 2026-04-23 B-232 v2: CRT scanlines are a global post-process that must
     * render every frame while enabled (gameplay + menus). Treat the toggle
     * itself as a reason to run pdguiRender so the foreground-drawlist pass
     * in pdguiRender line 843-ish can paint scanlines regardless of whether
     * any other overlay is active. Zero cost when the toggle is off. */
    if (pdguiThemeGetScanlineEnabled()) {
        return true;
    }
    return pdguiActiveMenuIsOpen() != 0;
}

static bool s_LoggedActiveMenuBeforeInit;
} // namespace

/* Forward declaration only — game/lang.h includes data.h which #define bool s32 */
extern "C" char *langGet(s32 textid);

/* S311: src/game/prop.c — full prop.h pulls types.h (breaks C++). */
extern "C" const char *propInteractPromptLabel(void);

/* ---------------------------------------------------------------------------
 * Exported utilities
 * --------------------------------------------------------------------------- */

extern "C" const char *langSafe(s32 textid)
{
    const char *s = langGet(textid);
    return s ? s : "";
}

/* ---------------------------------------------------------------------------
 * D5.0: UI Texture bridge
 *
 * pdguiGetUiTexture() delegates to pdguiThemeGetTexture() in pdgui_theme.cpp.
 * All decode, GL upload, and catalog registration live in the theme layer.
 *
 * D5.0a test-pattern (buildTestPattern / s_UiTexCache) has been removed.
 * --------------------------------------------------------------------------- */

extern "C" void* pdguiGetUiTexture(const char *id)
{
    return pdguiThemeGetTexture(id);
}

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */

static bool g_PdguiInitialized = false;
static bool s_FontAtlasRebuildRequested = false;
/* S295 F1: Removed g_PdguiActive mirror boolean.
 * The debug overlay's visibility is now derived solely from the input context
 * stack via inputCtxIsActive(&g_CtxDebugOverlay). The mirror boolean could
 * drift from the stack (e.g. if some code path popped the context without
 * updating the bool), producing dead-input desync. See
 * context/scratch/menu-system-investigation-2026-04-16.md §5.4. */
static SDL_Window *g_PdguiWindow = nullptr;

/* Note: Mouse grab state is now owned by the input context lifecycle.
 * Each context's on_push/on_pop sets SDL relative mode and cursor visibility.
 * No manual save/restore needed — the context stack handles transitions. */

/* ---------------------------------------------------------------------------
 * C-callable API (extern "C" for linkage with video.c, main.c, etc.)
 * --------------------------------------------------------------------------- */

extern "C" {

/* Include C headers inside extern "C" block to ensure proper linkage */
#include "pdgui.h"
#include "testscenarios.h"
#include "swarm_test.h"

/* Forward decl for the Debug Shortcuts registry init function.  Defined
 * later in this same extern "C" block, called from pdguiInit().  Must
 * live inside extern "C" to match the definition's linkage. */
static void registerDebugShortcuts(void);

/* D5 Phase 2: C++ trampoline for ImGui nav wrapping.
 * Called from pdguiNavTickWrap() via function pointer to avoid
 * requiring imgui_internal.h from C code. */
static void navWrapTrampoline(void)
{
    ImGuiWindow *win = ImGui::GetCurrentWindow();
    if (win) {
        ImGui::NavMoveRequestTryWrapping(win, ImGuiNavMoveFlags_LoopY);
    }
}

/* ---- Safe area implementation (D5 Phase 2) ---- */

static float s_SafeMarginTop    = -1.0f;  /* -1 = auto-detect */
static float s_SafeMarginBottom = -1.0f;
static float s_SafeMarginLeft   = -1.0f;
static float s_SafeMarginRight  = -1.0f;

void pdguiSetSafeAreaMargins(float top, float bottom, float left, float right)
{
    s_SafeMarginTop    = top;
    s_SafeMarginBottom = bottom;
    s_SafeMarginLeft   = left;
    s_SafeMarginRight  = right;
}

PdSafeArea pdguiGetSafeArea(void)
{
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    /* Fallback display size matches pdgui_scaling.h 1080p reference. */
    float vw = (disp.x > 0.0f) ? disp.x : 1920.0f;
    float vh = (disp.y > 0.0f) ? disp.y : 1080.0f;
    float aspect = vw / vh;

    /* Default margins: ultrawide gets wider horizontal margins */
    float defH = (aspect > 2.0f) ? 0.10f : 0.05f;  /* horizontal fraction */
    float defV = 0.05f;                               /* vertical fraction */

    float mt = (s_SafeMarginTop    >= 0.0f) ? s_SafeMarginTop    : defV;
    float mb = (s_SafeMarginBottom >= 0.0f) ? s_SafeMarginBottom : defV;
    float ml = (s_SafeMarginLeft   >= 0.0f) ? s_SafeMarginLeft   : defH;
    float mr = (s_SafeMarginRight  >= 0.0f) ? s_SafeMarginRight  : defH;

    PdSafeArea sa;
    sa.x = vw * ml;
    sa.y = vh * mt;
    sa.w = vw - vw * ml - vw * mr;
    sa.h = vh - vh * mt - vh * mb;
    return sa;
}

/* Build the font atlas from the current pdguiFontMod selection.
 * Called at init time and on runtime font swap (pdguiRequestFontAtlasRebuild).
 * Caller must have already called io.Fonts->Clear() before invoking this. */
static void pdguiLoadFontsIntoAtlas(ImGuiIO &io)
{
    io.Fonts->TexGlyphPadding = 2;

    ImFont *handelFont = nullptr;
    {
        void *fontCopy = ImGui::MemAlloc(g_HandelGothicFont_size);
        memcpy(fontCopy, g_HandelGothicFont_data, g_HandelGothicFont_size);

        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = true;
        snprintf(cfg.Name, sizeof(cfg.Name), "Handel Gothic Regular");
        cfg.OversampleV = 2;

        handelFont = io.Fonts->AddFontFromMemoryTTF(
            fontCopy, (int)g_HandelGothicFont_size, 24.0f, &cfg);

        if (handelFont) {
            io.FontDefault = handelFont;
            sysLogPrintf(LOG_NOTE, "pdgui: loaded embedded Handel Gothic (%u bytes)",
                         g_HandelGothicFont_size);
        } else {
            sysLogPrintf(LOG_WARNING, "pdgui: failed to load embedded Handel Gothic");
        }
    }

    const char *userFontPath = pdguiFontModGetActivePath();
    if (userFontPath && userFontPath[0]) {
        ImFontConfig cfg;
        cfg.OversampleV = 2;
        snprintf(cfg.Name, sizeof(cfg.Name), "User font (%s)",
                 pdguiFontModGetActiveId());

        ImFont *userFont = io.Fonts->AddFontFromFileTTF(
            userFontPath, 24.0f, &cfg);

        if (userFont) {
            io.FontDefault = userFont;
            sysLogPrintf(LOG_NOTE,
                "pdgui: loaded user font mod '%s' from '%s'",
                pdguiFontModGetActiveId(), userFontPath);
        } else {
            sysLogPrintf(LOG_WARNING,
                "pdgui: failed to load user font '%s' — keeping Handel Gothic",
                userFontPath);
        }
    }
}

void pdguiInit(void *sdlWindow)
{
    if (g_PdguiInitialized) {
        return;
    }

    g_PdguiWindow = (SDL_Window *)sdlWindow;

    /* Create ImGui context */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* M0.2 Phase C: ImGui's built-in gamepad nav is disabled.
     * pdguiDriveImGuiNav() now injects nav events from actionmap each frame. */

    /* Priority K-c (2026-04-25) + B-259 (2026-04-25):
     *
     * Cursor visibility flows through ONE authority --
     * inputCtxApplyCursorVisibility(s32 want_show) in port/src/inputctx.c.
     * The helper reconciles the caller's preference with the input-context
     * stack: in gameplay the caller's intent is honoured; in any
     * non-gameplay context (menu / pause / debug / textinput) the cursor
     * is force-shown regardless of caller. Direct SDL_ShowCursor calls
     * are forbidden anywhere in the engine.
     *
     * Pre-fix authorities (now collapsed):
     *   - inputCtxSyncMouseMode (port/src/inputctx.c) -- runs at ctx
     *     push/pop.  The original "sole authority" (K-c).
     *   - inputMouseShowCursor (port/src/input.c) -- MLOCK_AUTO 3-second
     *     auto-hide timer fired SDL_ShowCursor(0) every tick once idle,
     *     hiding the cursor while a menu was still consuming clicks
     *     (Mike's playtest 21010fbd: "mouse usable but not visible").
     *     Now routes through the helper, which refuses to hide when ctx
     *     top != gameplay.
     *   - gfx_sdl_set_cursor_visibility (port/fast3d/gfx_sdl2.cpp) -- the
     *     engine gfx-API surface.  Now routes through the helper.
     *   - hotswap close (this file, B-92 deferred-mouse-capture flush) --
     *     fires on hotswap-close transitions back to gameplay.  Now
     *     routes through the helper.
     *
     * NoMouseCursorChange short-circuits ImGui_ImplSDL2_UpdateMouseCursor
     * at imgui_impl_sdl2.cpp:631-632 so the SDL backend never calls
     * SDL_ShowCursor on its own.  We lose ImGui's cursor-shape switching
     * (resize handles etc.) which we don't surface anyway.
     *
     * See context/audits/input-authority-discipline-2026-04-25.md
     * section G + context/designs/input-authority-methodology.md. */
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    /* S309: don't persist ImGui window state to imgui.ini — per-agent
     * prefs own window visibility / selection, and imgui.ini has been a
     * source of "why did the dev menu open at a weird size?" reports.
     * Setting IniFilename = NULL disables the implicit save/load; the
     * default state is rebuilt from pdgui_style.cpp each session. */
    io.IniFilename = NULL;

    /* S305: scan mods/Fonts/ for user-installed .ttf/.otf fonts BEFORE the
     * atlas gets built. Registers Video.FontId config key — if the user has
     * a font mod selected (e.g. "user.MyFont.font"), we load it as the
     * default; Handel Gothic always loads too as a fallback. */
    pdguiFontModInit();

    /* Build the initial font atlas. */
    pdguiLoadFontsIntoAtlas(io);

    /* Apply PD-authentic style (colors, sharp corners, compact metrics) */
    pdguiApplyPdStyle();

    /* Initialize SDL2 + OpenGL3 backends.
     * We use "#version 130" (GLSL 1.30 / OpenGL 3.0) which matches the port's
     * default GL 3.0 compatibility profile context. */
    SDL_GLContext glCtx = SDL_GL_GetCurrentContext();
    ImGui_ImplSDL2_InitForOpenGL(g_PdguiWindow, glCtx);
    ImGui_ImplOpenGL3_Init("#version 130");

    g_PdguiInitialized = true;

    /* Initialize the F8 in-game hot-swap system (D4) */
    pdguiHotswapInit();

    /* Register all ImGui menu replacements */
    pdguiMenusRegisterAll();

    /* Initialize the character preview FBO system */
    pdguiCharPreviewInit();

    /* D5.0: decode ROM UI textures → GL, register ASSET_UI catalog entries */
    pdguiThemeInit();

    /* Register built-in + mod themes in the theme loader registry.
     * Must run after pdguiThemeInit (which sets up the palette system)
     * and after pdguiMenusRegisterAll (which may reference theme APIs). */
    pdguiThemeLoaderInit();

    /* P4: Initialize 9-slice, effects, and font manager subsystems */
    pdguiNinesliceInit();
    pdguiEffectsInit();
    pdguiFontMgrInit();

    /* D5 Phase 2: Register the C++ wrap trampoline so pdguiNavTickWrap()
     * can call ImGui::NavMoveRequestTryWrapping from C code. */
    pdguiNavSetWrapCallback(navWrapTrampoline);

    /* D5 Phase 2: Safe area margins — persist to pd.ini.
     * -1.0 = auto-detect (default). 0.0–0.5 = manual override. */
    configRegisterFloat("UI.SafeAreaTop",    &s_SafeMarginTop,    -1.0f, 0.5f);
    configRegisterFloat("UI.SafeAreaBottom", &s_SafeMarginBottom, -1.0f, 0.5f);
    configRegisterFloat("UI.SafeAreaLeft",   &s_SafeMarginLeft,   -1.0f, 0.5f);
    configRegisterFloat("UI.SafeAreaRight",  &s_SafeMarginRight,  -1.0f, 0.5f);

    /* NOTE: actionmapInit() was previously called here, but this is WRONG.
     * It's already called in main.c:161 (before configInit/actionmapLoadBinds).
     * Calling it again here wipes all pd.ini bind customizations that were
     * loaded by actionmapLoadBinds() at main.c:164. Removed. */

    /* Pause-menu Debug Shortcuts registry: register all raw SDL hotkey
     * entries + the actionmap-bound Forge toggle.  Co-located with the
     * dispatcher in this file so drift is catchable in code review. */
    registerDebugShortcuts();
}

/* M0.2 Phase C: Translate actionmap queries into ImGui nav key events.
 * Called each frame from pdguiNewFrame() AFTER actionmapPollFrame() so
 * action states are up-to-date.  Replaces ImGui's built-in gamepad nav
 * with the unified action system.
 *
 * B-124 fix: Uses KEYBOARD nav keys (not ImGuiKey_Gamepad*) because
 * NavEnableGamepad is disabled. ImGui ignores all Gamepad* keys when
 * that flag is off. NavEnableKeyboard IS enabled, so keyboard nav keys
 * work for controller-driven navigation too. */
static void pdguiDriveImGuiNav(void)
{
    ImGuiIO &io = ImGui::GetIO();
    static bool s_MouseBackHeld = false;

    /* Pressed (edge) → ImGui addKeyEvent with value=true on press frame, false on release.
     * For D-pad we use held state since ImGui expects sustained press for repeat navigation. */
    auto drivePressed = [&](InputAction act, ImGuiKey key) {
        if (actionPressed(0, act))  io.AddKeyEvent(key, true);
        if (actionReleased(0, act)) io.AddKeyEvent(key, false);
    };

    auto driveHeld = [&](InputAction act, ImGuiKey key) {
        io.AddKeyEvent(key, actionHeld(0, act) != 0);
    };

    drivePressed(ACTION_USE,           ImGuiKey_Enter);
    drivePressed(ACTION_CANCEL_USE,    ImGuiKey_Escape);
    driveHeld(ACTION_MENU_UP,          ImGuiKey_UpArrow);
    driveHeld(ACTION_MENU_DOWN,        ImGuiKey_DownArrow);
    driveHeld(ACTION_MENU_LEFT,        ImGuiKey_LeftArrow);
    driveHeld(ACTION_MENU_RIGHT,       ImGuiKey_RightArrow);

    /* Universal mouse back/cancel: middle-click maps to the same back path
     * menus already use (Escape / ACTION_CANCEL_USE). Using middle-click avoids
     * collisions with right-click behaviors in list widgets.
     *
     * S-2: Do not inject Escape while a middle-button drag is active — tools
     * like the Skin Editor use middle-drag for canvas pan. An Escape during
     * the drag would close the tool mid-gesture. We only treat a pure
     * middle-click press (no drag) as back. */
    {
        bool mouseBackRaw = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
        bool draggingMiddle = ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        bool mouseBackHeld = mouseBackRaw && !draggingMiddle;
        if (mouseBackHeld != s_MouseBackHeld) {
            io.AddKeyEvent(ImGuiKey_Escape, mouseBackHeld);
            s_MouseBackHeld = mouseBackHeld;
        }
    }

    /* Priority L Rule 7 (2026-04-25): right-stick Y smoothly scrolls the
     * focused (NavWindow) scrollable region in any menu.  Reads the
     * analog right-stick Y axis via the actionmap, applies deadzone +
     * non-linear response, and writes directly to the NavWindow's
     * Scroll.y via the ImGui internal API.
     *
     * - Smooth: scroll delta is proportional to deflection (analog) and
     *   to the frame's lvupdate60 (frame-rate independent).
     * - System-wide: applies to any menu with a scrollable region as
     *   long as ImGui's nav has settled on that window.
     * - Stick X-axis: not consumed here (left for menu-specific
     *   horizontal nav, otherwise no-op).
     *
     * Suppressed when gameplay is the input authority (no menu open) so
     * we don't fight the gameplay aim path. */
    if (gameplayInputSuppressed()) {
        f32 axX = 0.0f, axY = 0.0f;
        actionAxis(0, ACTION_AXIS_AIM_X, &axX, &axY);
        /* Deadzone matches the actionmap stick deadzone roughly; values
         * inside [-0.18, 0.18] are noise. */
        const f32 deadzone = 0.18f;
        f32 mag = (axY < 0.0f) ? -axY : axY;
        if (mag > deadzone) {
            f32 dir = (axY < 0.0f) ? -1.0f : 1.0f;
            /* Non-linear response: square the past-deadzone fraction so
             * small deflections scroll slowly and full deflection feels
             * fast. Speed unit is "pixels per frame at 60 Hz". */
            f32 t = (mag - deadzone) / (1.0f - deadzone);
            const f32 maxPxPerFrame = 28.0f;
            f32 deltaY = dir * t * t * maxPxPerFrame;
            ImGuiContext *ctx = ImGui::GetCurrentContext();
            if (ctx && ctx->NavWindow && ctx->NavWindow->ScrollMax.y > 0.0f) {
                ImGuiWindow *w = ctx->NavWindow;
                f32 newY = w->Scroll.y + deltaY;
                if (newY < 0.0f) newY = 0.0f;
                if (newY > w->ScrollMax.y) newY = w->ScrollMax.y;
                ImGui::SetScrollY(w, newY);
            }
        }
    }
}

void pdguiRequestFontAtlasRebuild(void)
{
    s_FontAtlasRebuildRequested = true;
}

void pdguiNewFrame(void)
{
    /* Rebuild font atlas if requested (e.g., user changed font in Settings).
     * Must run between frames — after the previous Render() and before the
     * next NewFrame(). This placement (before early-return) guarantees that
     * even when the overlay is not active this frame, the rebuild still fires. */
    if (s_FontAtlasRebuildRequested && g_PdguiInitialized) {
        s_FontAtlasRebuildRequested = false;
        ImGuiIO &io = ImGui::GetIO();
        io.Fonts->Clear();
        pdguiLoadFontsIntoAtlas(io);
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
        sysLogPrintf(LOG_NOTE, "pdgui: font atlas rebuilt");
    }

    /* H-3: actionmapEndFrame() removed from here — it is called once from gfx_sdl2.cpp.
     * Calling it twice caused edge signals to be cleared before game logic could read them. */

    /* Drive ImGui nav from actionmap each frame.
     * actionmapPollFrame() already ran in pdsched so action states are current. */
    pdguiDriveImGuiNav();

    bool networkActive = (netGetMode() != 0);
    bool pauseActive = (pdguiIsPauseMenuOpen() || pdguiIsScorecardVisible());
    bool hubActive = (pdguiModdingHubIsVisible() != 0);
    bool friendsActive = (pdguiFriendsAnySurfaceIsOpen() != 0);

#if defined(PD_DEV_BUILD)
    bool debugOverlayActive = (inputCtxIsActive(&g_CtxDebugOverlay) != 0);
#else
    bool debugOverlayActive = false;
#endif
    bool menuStackDiag = (pdguiMenuStackOverlayGetOpen() != 0);
    /* S311: When walking near a door/weapon/etc., propInteractPromptLabel() is
     * non-NULL - we must run ImGui this frame. The default path skips NewFrame
     * during "clean" solo gameplay (no menus/network), which hid the prompt.
     *
     * 2026-04-23: AND with !pdguiIsActive() AND !pdguiCiIntroBlocksInteract-
     * Prompt() so the prompt is NOT treated as a reason to run ImGui when a
     * non-gameplay ctx is already on top (main menu, pause, hub) OR the
     * gameplay is transitioning (pausemode != UNPAUSED, CI camera fly-in,
     * cutscene tickmode). Fixes three symptoms seen in playtest:
     *   (a) prompt leaks from gameplay into the CI main-menu screen because
     *       g_InteractProp is still tracked while the menu is up;
     *   (b) menu renderers that call pdguiPopupDarkenBehind compound with
     *       the prompt's frame and produce an unexpected dim over gameplay;
     *   (c) after pressing Start near a prompt target in CI free-roam, the
     *       prompt renders for 1-2 frames before the menu ctx pushes,
     *       producing a brief flash.
     * pdguiInteractPromptRender already early-returns on both predicates,
     * so gating the activation boolean here is the matching half of that
     * contract. */
    /* Issue 5b (2026-04-24): suppress interact prompt during Grid
     * FREEFLY.  The Dr Carroll observer chr shouldn't display "Hold X
     * to use <thing>" pill labels -- it's not a gameplay participant
     * and the underlying interact probe would target prop collisions
     * meant for the real player. */
    bool interactPrompt = (propInteractPromptLabel() != NULL)
        && !pdguiIsActive()
        && !pdguiCiIntroBlocksInteractPrompt()
        && !forgeIsFreefly();
#if defined(PD_DEV_BUILD)
    bool devGameplayHud =
            (botGetUpdatesDisabled() != 0) || (playerDevInvincibilityHudActive() != 0);
#else
    bool devGameplayHud = false;
#endif
    /* B-222: norm MP (`g_Vars.normmplayerisrunning` via pdguiPauseGetNormMplayerIsRunning)
     * must drive ImGui NewFrame even with no other overlay — otherwise killfeed
     * has no draw context. (Full-window dim during dev HUD is a separate issue:
     * other AddRectFilled paths exist, e.g. countdown / radial dim — not gated
     * solely on the F6/F7 dev banner booleans passed into pdguiAnyStandardOverlayReason.) */
    bool mpLiveMatchHud = (pdguiPauseGetNormMplayerIsRunning() != 0)
        && (pdguiPauseGetPaused() < 2);
    if (!g_PdguiInitialized) {
        /* Invariant: gameplay cannot open the active menu before pdguiInit(). */
        if (pdguiActiveMenuIsOpen() && !s_LoggedActiveMenuBeforeInit) {
            sysLogPrintf(LOG_ERROR,
                "pdgui: active menu open while ImGui not initialized (init ordering bug)");
            s_LoggedActiveMenuBeforeInit = true;
        }
        return;
    }
    if (!pdguiAnyStandardOverlayReason(
             debugOverlayActive, menuStackDiag, networkActive, pauseActive, hubActive, interactPrompt,
             devGameplayHud)
        && !mpLiveMatchHud
        && !friendsActive
        && !pdguiHotswapHasQueued() && !pdguiHotswapWasActive()) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    /* Scale font with display height so the 24pt atlas renders proportionally
     * at all resolutions. At 720p scale=1.0 (24pt effective). At 800x600
     * scale≈0.83 → ~20pt effective, fitting within scaled button heights.
     * Subsystems that override this (debug menu, storyboard) must restore
     * to pdguiScaleFactor() — NOT 1.0f — so subsequent renderers stay correct. */
    ImGui::GetIO().FontGlobalScale = pdguiScaleFactor();

    pdguiPopupDarkenBeginFrame();
    ImGui::NewFrame();
}

/* ---- Live Console (backtick toggle) ---- */
static bool s_ConsoleVisible = false;

void pdguiConsoleToggle(void)
{
    s_ConsoleVisible = !s_ConsoleVisible;
}

static void pdguiConsoleRender(void)
{
    if (!s_ConsoleVisible || !g_PdguiInitialized) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.7f);

    /* NoFocusOnAppearing + NoNavFocus + NoNavInputs = overlay that doesn't steal input.
     * Player can still move, shoot, jump while the console is visible. */
    ImGuiWindowFlags consoleFlags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("Console", &s_ConsoleVisible, consoleFlags)) {
        s32 count = sysLogRingGetCount();
        ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (s32 i = 0; i < count; i++) {
            const char *line = sysLogRingGetLine(i);
            /* Color-code by prefix */
            if (strstr(line, "ERROR:")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            } else if (strstr(line, "WARNING:")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            } else if (strstr(line, "MESHCOL:") || strstr(line, "JUMP:") || strstr(line, "CAPSULE")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            } else if (strstr(line, "LOAD:")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            }
            ImGui::TextUnformatted(line);
            ImGui::PopStyleColor();
        }
        /* Auto-scroll to bottom */
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void pdguiRender(void)
{
    /* One-shot ROM texture extraction (--extract-ui-textures CLI flag).
     * pdguiThemeCheckExtract() checks if g_TexGeneralConfigs is populated
     * and --extract-ui-textures is set, then extracts once. */
    pdguiThemeCheckExtract();

    /* Console renders independently of the debug overlay */
    if (s_ConsoleVisible && g_PdguiInitialized) {
        pdguiConsoleRender();
    }

    bool hotswapQueued = pdguiHotswapHasQueued() != 0;
    bool hotswapWasActive = pdguiHotswapWasActive() != 0;

    bool networkActive = (netGetMode() != 0);
    bool updateActive = (pdguiUpdateIsActive() != 0);
    bool pauseActive = (pdguiIsPauseMenuOpen() || pdguiIsScorecardVisible());
    bool hubActive = (pdguiModdingHubIsVisible() != 0);
    bool friendsActive = (pdguiFriendsAnySurfaceIsOpen() != 0) ||
                          (pdguiToastIsActive() != 0) ||
                          (pdguiSpectatorOverlayActive() != 0);

    /* D13: Also render when update UI is visible (notification banner, version picker) */
#if defined(PD_DEV_BUILD)
    bool debugOverlayActive = (inputCtxIsActive(&g_CtxDebugOverlay) != 0);
#else
    bool debugOverlayActive = false;
#endif
    bool menuStackDiag = (pdguiMenuStackOverlayGetOpen() != 0);
    /* 2026-04-23: mirror the NewFrame gate exactly - do not let the interact
     * prompt keep ImGui rendering when a menu is on top or during any pause
     * transition (see rationale at the NewFrame site). */
    /* Issue 5b (2026-04-24): suppress interact prompt during Grid
     * FREEFLY.  The Dr Carroll observer chr shouldn't display "Hold X
     * to use <thing>" pill labels -- it's not a gameplay participant
     * and the underlying interact probe would target prop collisions
     * meant for the real player. */
    bool interactPrompt = (propInteractPromptLabel() != NULL)
        && !pdguiIsActive()
        && !pdguiCiIntroBlocksInteractPrompt()
        && !forgeIsFreefly();
#if defined(PD_DEV_BUILD)
    bool devGameplayHud =
            (botGetUpdatesDisabled() != 0) || (playerDevInvincibilityHudActive() != 0);
#else
    bool devGameplayHud = false;
#endif
    bool mpLiveMatchHud = (pdguiPauseGetNormMplayerIsRunning() != 0)
        && (pdguiPauseGetPaused() < 2);
    if (!g_PdguiInitialized) {
        if (pdguiActiveMenuIsOpen() && !s_LoggedActiveMenuBeforeInit) {
            sysLogPrintf(LOG_ERROR,
                "pdgui: active menu open while ImGui not initialized (init ordering bug)");
            s_LoggedActiveMenuBeforeInit = true;
        }
        return;
    }
    if (!pdguiAnyStandardOverlayReason(
             debugOverlayActive, menuStackDiag, networkActive, pauseActive, hubActive, interactPrompt,
             devGameplayHud)
        && !mpLiveMatchHud
        && !s_ConsoleVisible && !hotswapQueued && !hotswapWasActive && !updateActive
        && !friendsActive) {
        return;
    }

    /* Get window dimensions for game-relative scaling.
     * The SDL window size drives the coordinate system after
     * gfx_opengl_reset_for_overlay resets the viewport. */
    int winW = 0, winH = 0;
    if (g_PdguiWindow) {
        SDL_GetWindowSize(g_PdguiWindow, &winW, &winH);
    }
    if (winW <= 0 || winH <= 0) {
        winW = 640;
        winH = 480;
    }

#if defined(PD_DEV_BUILD)
    /* F12 debug menu — PD-styled, game-relative scaling */
    if (debugOverlayActive) {
        pdguiDebugMenuRender((s32)winW, (s32)winH);
        /* Log Viewer Dev Window — shown alongside the debug menu */
        pdguiLogViewerRender((s32)winW, (s32)winH);
    }
#endif

    /* F8 hot-swap: render any ImGui menu replacements that were queued
     * during the GBI phase by pdguiHotswapCheck() in menuRenderDialog().
     *
     * ALWAYS call pdguiHotswapRenderQueued so the persistent WasActive flag
     * stays in sync. If we skip the call (e.g., during gameplay with only
     * the lobby sidebar active), the flag stays stale from the last menu
     * frame and pdguiIsActive/pdguiWantsInput block all game input. */
    pdguiHotswapRenderQueued((s32)winW, (s32)winH);

    /* B-92 (solo mission ImGui path): pdguiHotswapRenderQueued() just updated
     * s_HotswapMenuWasActive.  When a mission is accepted from the ImGui overview
     * dialog, menuhandlerAcceptMission() calls menuStop() + inputLockMouse(1)
     * from inside the hotswap render callback.  At that point pdguiIsActive()
     * still returns true (WasActive is still set for this frame), so
     * inputLockMouse() sets mouseLocked=true but defers SDL_SetRelativeMouseMode.
     * The next frame the hotswap queue is empty, WasActive drops to false —
     * but nothing applies the SDL state unless we do it here.
     * Covers all hotswap→gameplay transitions: solo mission accept, endscreen
     * retry/next, and any future hotswap dialog that calls menuStop().
     *
     * FIX-PLAYTEST-4 (B-114): Do NOT flush during the first few ticks of a new
     * stage.  On mission→mission transitions (e.g., mission 1 endscreen →
     * mission 2 gameplay), the hotswap was active in the previous stage's menus;
     * the close fires on an early frame of the new stage while lvTickPlayer is
     * still initialising, leaving stale ImGui/input state that auto-dismisses
     * menus (B-134). Deferring to frame 5+ gives the stage time to fully
     * initialize menus, input contexts, and player state before we apply SDL
     * mouse capture. */
    {
        bool hotswapNowActive = (pdguiHotswapWasActive() != 0);
        if (hotswapWasActive && !hotswapNowActive &&
                !pdguiIsActive() &&
                pdmainGetLvFrame60() > 4) {
            if (inputMouseIsLocked()) {
                /* B-259 (2026-04-25): route through the input-ctx
                 * authority. At this point the hotswap menu has just
                 * closed, so the ctx top should already be back on
                 * gameplay; the helper agrees and applies hide. If
                 * for any reason the ctx is still on a menu (race
                 * with deferred pop), the helper keeps cursor visible
                 * which is the safer default. */
                inputCtxApplyCursorVisibility(0);
                SDL_SetRelativeMouseMode(SDL_TRUE);
                sysLogPrintf(LOG_NOTE,
                    "pdgui: hotswap closed, flushed deferred mouse capture (B-92 solo) lvframe=%d",
                    pdmainGetLvFrame60());
            }
        }
    }

    /* D3R-7: Modding Hub standalone window — renders when opened from main menu */
    pdguiModdingHubRender((s32)winW, (s32)winH);

    /* P5: Theme Editor — renders when opened from debug settings or modding hub */
    pdguiThemeEditorRender((s32)winW, (s32)winH);

    /* Network lobby player list sidebar — shows connected players when
     * in a networked session. Renders independently of hotswap state. */
    pdguiLobbyRender((s32)winW, (s32)winH);

    /* Pre-match countdown: 3-2-1-GO popup + cancel banner.
     * Renders over everything during MANIFEST_PHASE_LOADING; no-op otherwise. */
    pdguiCountdownRender((s32)winW, (s32)winH);

    /* D13: Update notification banner, version picker, download progress.
     * Renders as overlay — independent of hotswap and menu state. */
    pdguiUpdateRender();

    /* Combat sim pause menu + hold-to-show scorecard overlay.
     * Rendered independently of hotswap/menu state — active during gameplay. */
    pdguiPauseMenuRender((s32)winW, (s32)winH);
    pdguiScorecardRender((s32)winW, (s32)winH);

    /* Phase 1 connectivity surfaces: top-right status indicator,
     * Tab-toggled friend sidebar, full-screen Social menu, add-friend
     * modal. Rendered after every other overlay so the status pill is
     * always visible and the sidebar/Social menu paint above gameplay
     * windows. */
    pdguiFriendsRender((s32)winW, (s32)winH);

    /* Phase 2 toast notifications: bottom-right transient stack.
     * Painted last so popups overlay even the friends surfaces. */
    pdguiToastRender((s32)winW, (s32)winH);

    /* Phase 3 spectator overlay: top strip + scoreboard + control hints
     * during a live spectate or Theater playback. Only paints when the
     * spectator subsystem is active. */
    pdguiSpectatorRender((s32)winW, (s32)winH);

    /* In-match HUD: top 2 scorers + remaining time.
     * Only visible during normmplayerisrunning (combat sim active). */
    pdguiHudRender((s32)winW, (s32)winH);

#if defined(PD_DEV_BUILD)
    /* F6: bot AI frozen — top-center banner (NoInputs so gameplay mouse/look unchanged) */
    const float kDevHudF6Stack = 58.0f; /* vertical space for two-line F6 banner */
    float invincBannerY = 22.0f;
    if (botGetUpdatesDisabled()) {
        ImGui::SetNextWindowPos(ImVec2((float)winW * 0.5f, 22.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##botupdates_off", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Bot Update: DISABLED");
        ImGui::TextDisabled("Press F6 to resume bot AI");
        ImGui::End();
        invincBannerY = 22.0f + kDevHudF6Stack;
    }
    /* F7: player invincibility — stack below F6 banner when both active */
    if (playerDevInvincibilityHudActive()) {
        ImGui::SetNextWindowPos(ImVec2((float)winW * 0.5f, invincBannerY), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##dev_invincible", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "Player: INVINCIBLE");
        ImGui::TextDisabled("Press F7 to disable");
        ImGui::End();
    }

    /* S483: Test Scenarios swarm benchmark HUD. Renders only when a swarm
     * scenario is armed. Top-right corner so it doesn't overlap the F6 /
     * F7 banners (top-center) and stays out of the gameplay sight line. */
    if (testScenarioIsSwarmActive()) {
        const s32 cur = testScenarioGetCurrentSwarmCount();
        s32 idx = 0;
        for (s32 i = 0; i < SWARM_TEST_CYCLE_STEPS; i++) {
            if (SWARM_TEST_CYCLE[i] == cur) { idx = i; break; }
        }
        const s32 next_count = SWARM_TEST_CYCLE[(idx + 1) % SWARM_TEST_CYCLE_STEPS];

        ImGui::SetNextWindowPos(ImVec2((float)winW - 12.0f, 22.0f),
                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGui::Begin("##testscen_swarm_hud", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
                | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                "TEST SCENARIO  Swarm - %s",
                (testScenarioActiveMethod() == SWARM_METHOD_GPU)
                    ? "GPU Boids" : "CPU Bots");
        ImGui::Separator();
        ImGui::Text("Bots:    %d  (alive %d)",
                swarmTestGetActiveCount(),
                swarmTestGetActiveCount() - swarmTestGetKillCount());
        ImGui::Text("Kills:   %d", swarmTestGetKillCount());
        ImGui::Text("Next:    %d", next_count);
        ImGui::TextDisabled("[0] / D-pad-Down: cycle");
        ImGui::End();
    }
#endif

    /* In-game active menu (weapon / function / orders) -- ImGui replaces legacy GBI wheel. */
    pdguiActiveMenuRadialRender((s32)winW, (s32)winH);

    /* Subtitle overlay: bottom-center panel for HUDMSGTYPE_INGAMESUBTITLE
     * and HUDMSGTYPE_CUTSCENESUBTITLE — replaces the legacy hudmsg path
     * for these two types.  hudmsgsRender skips them so ImGui is the sole
     * renderer.  Draws on the foreground draw list so cutscene letterbox
     * bars do not occlude subtitles. */
    pdguiSubtitlesRender((s32)winW, (s32)winH);

    /* S311: contextual interact prompt ("[E] Pick up", "[E] Open" etc).
     * No-op when no interact target is tracked.  Drawn above the HUD so the
     * player sees the prompt beside the reticle. */
    pdguiInteractPromptRender((s32)winW, (s32)winH);

    /* D6 P3: achievement unlock toasts — slide in from the right edge.
     * No-op when the toast queue is empty; polled at MP/solo endscreen
     * entry via pdguiAchievementToastPollUnlocks. */
    pdguiAchievementToastRender((s32)winW, (s32)winH);

    /* Kill/score ticker overlay: slide-in notifications for score events.
     * Active during normmplayerisrunning; auto-suppressed on game over. */
    pdguiMpIngameRender((s32)winW, (s32)winH);

    /* Post-match overlay (MATCH OVER screen + "Return to Lobby" button).
     * Without this call the game freezes on match end — the N64 menu system
     * drove the post-match transition, but the PC port ImGui doesn't observe
     * prevmenuroot.  pdguiGameOverRender is a no-op unless MPPAUSEMODE_GAMEOVER
     * is active, so it is safe to call every frame. */
    pdguiGameOverRender((s32)winW, (s32)winH);

    /* Forge level editor HUD overlay (F0+).  Renders nothing unless a forge
     * session is active; safe to call every frame.  Drawn above the gameplay
     * HUD so the editor takes visual priority once the player toggles into
     * a forge session, but below the shimmer/scanline post-process. */
    pdguiForgeHudRender((s32)winW, (s32)winH);

    /* The Grid editor overlay (F1-F8 tabbed UI: catalog, properties, zones,
     * lighting, logic, gametype, mission, settings).  Gated on forge session
     * active + FREEFLY so NORMAL playtest remains uncluttered. */
    pdguiForgeEditorRender((s32)winW, (s32)winH);


    /* Add PD-style shimmer effects to all visible windows via foreground draw list.
     * This adds the animated border highlights that are PD's signature look. */
    pdguiRenderAllWindowShimmers();

    /* D5.0: CRT scanline overlay - horizontal lines painted at configured
     * alpha across the full viewport. Per Mike (2026-04-23): this should
     * render ALWAYS while enabled, gameplay and menus alike. The earlier
     * B-232 gate on `pdguiIsActive()` was wrong; reverted. Apparent
     * "darken only when prompt visible" was an artifact of `pdguiRender`
     * only running when an overlay reason was true. The proper fix is to
     * treat `pdguiThemeGetScanlineEnabled()` itself as a reason to run
     * pdguiRender (see pdguiAnyStandardOverlayReason + call sites below),
     * so scanlines paint every frame while the option is on. */
    if (pdguiThemeGetScanlineEnabled()) {
        pdguiThemeDrawScanlineFg(0, 0, (float)winW, (float)winH);
    }

    /* F9: read-only menu stack + input-context snapshot (NoInputs — does not steal focus). */
    pdguiMenuStackOverlayRender((s32)winW, (s32)winH);

    /* B-253 follow-up: top-right indicator(s) when the debug cull mode
     * or wireframe toggle is active.  Tells Mike at a glance that the
     * world render is in a non-default state, so he doesn't hunt a
     * phantom graphics bug after toggling and forgetting. */
    {
        extern int gfxDebugCullModeGet(void);
        extern const char *gfxDebugCullModeName(void);
        extern int gfxDebugWireframeGet(void);
        int cullMode = gfxDebugCullModeGet();
        int wire     = gfxDebugWireframeGet();
        if (cullMode != 0 || wire != 0) {
            ImDrawList *fg = ImGui::GetForegroundDrawList();
            char line[128];
            if (cullMode != 0 && wire != 0) {
                snprintf(line, sizeof(line),
                         "[Shift+F1] %s   [Shift+F2] Wireframe",
                         gfxDebugCullModeName());
            } else if (cullMode != 0) {
                snprintf(line, sizeof(line), "[Shift+F1] %s",
                         gfxDebugCullModeName());
            } else {
                snprintf(line, sizeof(line), "[Shift+F2] Wireframe");
            }
            ImVec2 sz = ImGui::CalcTextSize(line);
            float pad = 8.0f;
            float bx = winW - sz.x - pad * 2.0f;
            float by = pad;
            fg->AddRectFilled(ImVec2(bx, by),
                              ImVec2(bx + sz.x + pad * 2.0f, by + sz.y + pad),
                              IM_COL32(20, 20, 20, 220), 4.0f);
            fg->AddRect(ImVec2(bx, by),
                        ImVec2(bx + sz.x + pad * 2.0f, by + sz.y + pad),
                        IM_COL32(255, 200, 80, 255), 4.0f, 0, 1.5f);
            fg->AddText(ImVec2(bx + pad, by + pad * 0.5f),
                        IM_COL32(255, 220, 120, 255), line);
        }
    }

    pdguiPopupDarkenFlush();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/**
 * Server-only render frame: clear screen, draw ImGui, swap buffers.
 * Called from the dedicated server's main loop (server_main.c).
 * The game client uses gfx_run() which handles this differently.
 */
void pdguiServerFrame(void)
{
    if (!g_PdguiInitialized || !g_PdguiWindow) {
        return;
    }

    /* One-time GL setup: ensure context is current and GLAD is loaded.
     * gfx_init creates the context but the server doesn't call gfx_run
     * which normally manages GL state each frame. */
    static bool s_GlReady = false;
    if (!s_GlReady) {
        SDL_GLContext ctx = SDL_GL_GetCurrentContext();
        if (!ctx) {
            /* No GL context — can't render */
            return;
        }
        SDL_GL_MakeCurrent(g_PdguiWindow, ctx);
        /* GLAD should already be loaded by gfx_init, but verify */
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            sysLogPrintf(LOG_ERROR, "pdguiServerFrame: GLAD failed to load");
            return;
        }
        s_GlReady = true;
        sysLogPrintf(LOG_NOTE, "pdguiServerFrame: GL context ready");
    }

    int winW = 0, winH = 0;
    SDL_GetWindowSize(g_PdguiWindow, &winW, &winH);
    if (winW <= 0 || winH <= 0) return;

    /* Update window title (server doesn't call videoEndFrame) */
    {
        static u32 s_TitleCounter = 0;
        if (++s_TitleCounter >= 60) {
            s_TitleCounter = 0;
            extern s32 g_NetMode;
            extern s32 g_NetNumClients;
            extern s32 g_NetMaxClients;
            extern u32 g_NetServerPort;
            extern const char *netUpnpGetExternalIP(void);
            extern s32 netUpnpIsActive(void);

            char titleBuf[256];
            const char *ip = netUpnpIsActive() ? netUpnpGetExternalIP() : "";
            if (ip && ip[0]) {
                snprintf(titleBuf, sizeof(titleBuf),
                         "PD2 Dedicated Server - %s:%u - %d/%d connected",
                         ip, g_NetServerPort, g_NetNumClients, g_NetMaxClients);
            } else {
                snprintf(titleBuf, sizeof(titleBuf),
                         "PD2 Dedicated Server - port %u - %d/%d connected",
                         g_NetServerPort, g_NetNumClients, g_NetMaxClients);
            }
            SDL_SetWindowTitle(g_PdguiWindow, titleBuf);
        }
    }

    /* GL clear */
    glViewport(0, 0, winW, winH);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* ImGui frame */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    pdguiPopupDarkenBeginFrame();
    ImGui::NewFrame();

    /* Server GUI: render a simple test panel to confirm ImGui works,
     * plus the lobby overlay (server info, player list, log) */
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(300, 100));
    if (ImGui::Begin("Server Status", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("PD2 Dedicated Server");
        ImGui::Text("Window: %dx%d", winW, winH);
        extern s32 g_NetMode;
        extern s32 g_NetDedicated;
        ImGui::Text("NetMode: %d  Dedicated: %d", g_NetMode, g_NetDedicated);
    }
    ImGui::End();

    pdguiLobbyRender((s32)winW, (s32)winH);

    /* Always call hotswap render to keep the WasActive flag in sync */
    pdguiHotswapRenderQueued((s32)winW, (s32)winH);

    /* Shimmer effects */
    pdguiRenderAllWindowShimmers();

    /* Finalize and draw */
    pdguiPopupDarkenFlush();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(g_PdguiWindow);
}

void pdguiShutdown(void)
{
    if (!g_PdguiInitialized) {
        return;
    }

    pdguiFontMgrShutdown();
    pdguiEffectsShutdown();
    pdguiNinesliceShutdown();
    pdguiThemeShutdown();
    pdguiHotswapShutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    g_PdguiInitialized = false;
    g_PdguiWindow = nullptr;
}

/* Pause-menu Debug Shortcuts registry: single source of truth for the
 * read-only shortcuts list shown by the pause menu modal.  Co-located
 * with the dispatcher so adding a new raw SDL hotkey if-block below
 * also requires a paired pdguiDebugShortcutRegister call here -- drift
 * is catchable in code review.
 *
 * Entries cover (a) raw SDL handlers in pdguiProcessEvent below, (b) the
 * two raw handlers in gfx_sdl2.cpp (Alt+Enter, backtick), and (c) one
 * actionmap-bound debug-adjacent entry (F11 ACTION_FORGE_TOGGLE) that is
 * surfaced for completeness with a "rebindable" note.
 *
 * Called once from pdguiInit() after subsystem init. */
static void registerDebugShortcuts(void)
{
    /* Rendering (always-on, B-253 follow-up) */
    pdguiDebugShortcutRegister("Shift+F1",
        "Cycle backface cull mode (none -> back -> front)",
        DBG_SHORTCUT_CAT_RENDERING, 0);
    pdguiDebugShortcutRegister("Shift+F2",
        "Toggle wireframe overlay on world render",
        DBG_SHORTCUT_CAT_RENDERING, 0);

    /* Diagnostics */
    pdguiDebugShortcutRegister("F2",
        "Schedule one-shot test-fire pulse for player 0 (+1s delay)",
        DBG_SHORTCUT_CAT_DIAGNOSTICS, 0);
    pdguiDebugShortcutRegister("F6",
        "Freeze MP bot AI / movement (spawn-layout inspection)",
        DBG_SHORTCUT_CAT_DIAGNOSTICS, 1);
    pdguiDebugShortcutRegister("F9",
        "Toggle menu / input-context diagnostics overlay (read-only)",
        DBG_SHORTCUT_CAT_DIAGNOSTICS, 0);
    pdguiDebugShortcutRegister("F10",
        "Toggle mesh collision debug overlay",
        DBG_SHORTCUT_CAT_DIAGNOSTICS, 0);

    /* Developer */
    pdguiDebugShortcutRegister("F12",
        "Toggle dev debug overlay (push g_CtxDebugOverlay)",
        DBG_SHORTCUT_CAT_DEVELOPER, 1);
    pdguiDebugShortcutRegister("`",
        "Toggle dev console (backtick / grave)",
        DBG_SHORTCUT_CAT_DEVELOPER, 0);

    /* Tooling */
    pdguiDebugShortcutRegister("F8",
        "Hot-swap toggle (flip rendering mode for ImGui menus)",
        DBG_SHORTCUT_CAT_TOOLING, 0);
    pdguiDebugShortcutRegister("Right-stick click",
        "Hot-swap toggle (gamepad alias for F8)",
        DBG_SHORTCUT_CAT_TOOLING, 0);
    pdguiDebugShortcutRegister("Alt+Enter",
        "Toggle fullscreen window",
        DBG_SHORTCUT_CAT_TOOLING, 0);

    /* Cheats */
    pdguiDebugShortcutRegister("F7",
        "Toggle player invincibility (solo / MP debug)",
        DBG_SHORTCUT_CAT_CHEATS, 1);

    /* Forge / Grid (actionmap-bound; shown here for completeness).
     * Default keyboard binding moved from F7 to F11 (2026-04-26) to
     * split the F7 dual-bind with the invincibility cheat above. */
    pdguiDebugShortcutRegister("F11",
        "Toggle Grid / Forge mode (default; rebindable in Settings -> Controls)",
        DBG_SHORTCUT_CAT_FORGE, 0);
}

s32 pdguiProcessEvent(void *sdlEvent)
{
    if (!g_PdguiInitialized) {
        return 0;
    }

    const SDL_Event *ev = (const SDL_Event *)sdlEvent;

    /* ---- Global hotkeys: always consumed regardless of context ----
     *
     * IMPORTANT: any new raw SDL hotkey if-block added below must also
     * be added to registerDebugShortcuts() above so the pause-menu
     * Debug Shortcuts modal stays in sync.  Drift here = stale UI. */

#if defined(PD_DEV_BUILD)
    /* F6: toggle MP bot AI/movement freeze (spawn layout inspection) */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F6) {
        botToggleUpdatesDisabled();
        return 1;
    }

    /* F7: toggle player invincibility (solo/MP debug).
     * 2026-04-26: dual-bind with ACTION_FORGE_TOGGLE split.  The
     * actionmap binding for ACTION_FORGE_TOGGLE was moved to F11
     * (port/src/actionmap.cpp), so F7 is now uniquely the
     * invincibility cheat.  The earlier forgeIsFreefly() early-return
     * is removed -- there is no longer any other consumer of F7. */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F7) {
        playerToggleDevInvincibility();
        return 1;
    }
#endif

    /* F8 / RS-click: hot-swap toggle (flip rendering mode for ImGui menus) */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F8) {
        pdguiHotswapToggle();
        return 1;
    }
    if (ev->type == SDL_CONTROLLERBUTTONDOWN &&
        ev->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
        pdguiHotswapToggle();
        return 1;
    }

#if defined(PD_DEV_BUILD)
    /* F12: toggle debug overlay via context stack push/pop.
     * S295 F1: Authoritative state is the input context stack — no mirror bool. */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F12) {
        if (!inputCtxIsActive(&g_CtxDebugOverlay)) {
            inputCtxPush(&g_CtxDebugOverlay);
        } else {
            inputCtxPopDeferred(&g_CtxDebugOverlay);
        }
        return 1;
    }
#endif

    /* B-253 follow-up: Shift+F1 cycles the renderer's debug backface-cull
     * mode for the world render (none -> back -> front -> none).
     * Diagnostic — useful in The Grid for seeing back-faces of walls when
     * the freefly camera is outside a room, and for confirming whether a
     * body's triangle winding is inverted (per-asset data issue). */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F1 &&
            (ev->key.keysym.mod & KMOD_SHIFT)) {
        extern void gfxDebugCullModeCycle(void);
        extern const char *gfxDebugCullModeName(void);
        gfxDebugCullModeCycle();
        sysLogPrintf(LOG_NOTE, "DEBUG.CULL: mode -> %s", gfxDebugCullModeName());
        return 1;
    }

    /* B-246 round-9: F2 (no modifier) schedules a one-frame test-fire
     * pulse for player 0 one second after the keypress. Mike uses this
     * for remote-testing the round-8 bone-snapshot diagnostic when his
     * full keyboard / mouse rig is unavailable (phone -> RDP into
     * Windows). Multiple presses queue. Shift+F2 still falls through
     * to the wireframe toggle below; Ctrl+F2 / Alt+F2 also fall
     * through (currently unbound). */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F2 &&
            !(ev->key.keysym.mod & (KMOD_SHIFT | KMOD_CTRL | KMOD_ALT))) {
        extern void bmoveScheduleTestFire(s32 delay_ticks_60hz);
        bmoveScheduleTestFire(60);
        return 1;
    }

    /* B-253 follow-up: Shift+F2 toggles wireframe overlay on the world
     * render (glPolygonMode lines).  Useful in The Grid for an
     * editor-style "see geometry edges" view. */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F2 &&
            (ev->key.keysym.mod & KMOD_SHIFT)) {
        extern void gfxDebugWireframeToggle(void);
        extern int  gfxDebugWireframeGet(void);
        gfxDebugWireframeToggle();
        sysLogPrintf(LOG_NOTE, "DEBUG.WIREFRAME: %s",
                     gfxDebugWireframeGet() ? "on" : "off");
        return 1;
    }

    /* F9: toggle read-only menu/input diagnostics (no input-context push). */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F9) {
        pdguiMenuStackOverlayToggle();
        return 1;
    }
    /* F10: mesh collision debug (was F9 before F9 was reserved for menu diagnostics). */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F10) {
        meshDebugToggle();
        return 1;
    }

    /* ---- B-124 fix: Key suppression on context push ---- */
    /* When a context was just pushed (within grace period), suppress KEY_DOWN
     * events to prevent the triggering key from being seen by ImGui or the
     * new context. This breaks the Esc open/close race condition where the
     * same keypress opens the menu (via gameplay) and immediately closes it
     * (via ImGui's IsKeyPressed check). */
    if (inputCtxShouldSuppressKey(ev)) {
        return 1; /* consumed: don't forward to ImGui or dispatch */
    }

    /* ---- Forward to ImGui for internal state tracking (Q-Backend-Event-Order) ----
     * ImGui must see the event BEFORE we read WantCaptureKeyboard, so focus
     * changes driven by this very event (e.g. Tab-into-textbox, click-to-focus)
     * are reflected on the same frame instead of leaking one tick of keys into
     * actionmapDispatch. WantCaptureKeyboard itself is computed at NewFrame, so
     * true gain-focus-same-frame races still require the B-154 Esc/Enter carve
     * plus the imguiEatsKey early-return below as defense-in-depth. */
    ImGui_ImplSDL2_ProcessEvent(ev);

    /* ---- B-154 fix: Textbox keystroke leak to action map ---- */
    /* When ImGui has captured the keyboard (InputText active, or a nav-focused
     * window claims keys), keystrokes must NOT reach actionmapDispatch — else
     * typing "E" in a mod-name box still fires ACTION_USE, etc. Esc and Enter
     * still pass through so dialogs can close/submit via their normal action
     * bindings. Everything else is handed to ImGui only (and consumed so the
     * game never sees it). */
    s32 isKeyEv = (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP);
    s32 imguiEatsKey = 0;
    if (isKeyEv && ImGui::GetIO().WantCaptureKeyboard) {
        SDL_Keycode sym = ev->key.keysym.sym;
        /* Allow Esc (cancel/close) and Enter (submit) through to the action
         * map — those are the only keys that must continue to drive menu-
         * level actions while a textbox has focus. */
        if (sym != SDLK_ESCAPE && sym != SDLK_RETURN && sym != SDLK_KP_ENTER) {
            imguiEatsKey = 1;
        }
        /* B-195 leak-class diagnostic: if the input-ctx stack is at
         * gameplay (no menus open) but ImGui still reports it wants the
         * keyboard, some widget kept focus past its window's Begin/End
         * teardown. Log the sym once per event so future instances of
         * this class surface in the log instead of silently swallowing
         * WASD. Fix path: imguiMenuOnPop clears focus + active-id; this
         * warning catches any remaining leak. Rate-limited to 1 per sec
         * since a stuck ImGui would otherwise spam. */
        static u32 s_LastB195WarnMs = 0;
        if (imguiEatsKey && !pdguiIsActive()) {
            u32 now = SDL_GetTicks();
            if (now - s_LastB195WarnMs > 1000) {
                s_LastB195WarnMs = now;
                sysLogPrintf(LOG_WARNING,
                    "B-195: ImGui WantCaptureKeyboard=1 while top ctx is gameplay — swallowing sym=0x%04x (widget focus leaked past menu close?)",
                    (unsigned)sym);
            }
        }
    }

    /* ---- M0.2 Phase A: Update action map state ---- */
    if (!imguiEatsKey) {
        actionmapDispatch(ev);
    }

    /* If ImGui is eating this keystroke (textbox focused), don't let the
     * input-context stack forward it to legacy paths either — defense-in-depth
     * since actionmapDispatch already skipped it above. */
    if (imguiEatsKey) {
        return 1;
    }

    /* ---- Dispatch through the input context stack ---- */
    /* The stack walks top-to-bottom. If any context's can_consume() returns
     * true, the event is consumed (game doesn't see it). If nothing consumes
     * it (or only g_CtxGameplay matches), the game processes it normally. */
    return inputCtxDispatch(ev);
}

s32 pdguiWantsInput(void)
{
    if (!g_PdguiInitialized) {
        return 0;
    }

    /* If the top context is anything other than gameplay, ImGui wants input.
     * The context stack knows what's active — no manual mode checks needed. */
    InputContext *top = inputCtxGetTop();
    return (top && top != &g_CtxGameplay) ? 1 : 0;
}

s32 pdguiIsActive(void)
{
    /* Any non-gameplay context on the stack means ImGui is active */
    InputContext *top = inputCtxGetTop();
    return (top && top != &g_CtxGameplay) ? 1 : 0;
}

void pdguiToggle(void)
{
#if defined(PD_DEV_BUILD)
    /* S295 F1: Authoritative state is the input context stack — no mirror bool. */
    if (!inputCtxIsActive(&g_CtxDebugOverlay)) {
        inputCtxPush(&g_CtxDebugOverlay);
    } else {
        inputCtxPopDeferred(&g_CtxDebugOverlay);
    }
#endif
}

void pdguiClearImGuiFocusAndNav(void)
{
    /* B-195: clear ImGui's nav/focus/active-id state so WantCaptureKeyboard
     * drops back to false after the last menu closes. Declared in pdgui.h;
     * called from imguiMenuOnPop in inputctx.c.
     *
     * We DON'T close popups here because the caller path (imguiMenuOnPop)
     * fires *after* the menu's Begin/End pair has already been torn down —
     * popups are already gone by this point. The lingering state is
     * purely NavWindow/NavId/ActiveId pointing at the recently-closed
     * window's widgets. Focusing NULL forces ImGui to drop NavWindow
     * on the next frame; ClearActiveID releases whatever widget was
     * holding the active id (typically a textbox or a drag-edit). */
    if (!g_PdguiInitialized) {
        return;
    }
    ImGui::FocusWindow(nullptr);
    ImGui::ClearActiveID();
}

} /* extern "C" */
