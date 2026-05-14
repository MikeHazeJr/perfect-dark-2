# Input Universality and Transitions

> Status: **APPROVED (Phase 1 design, K.1-K.9 decided 2026-04-27). Phase 2 implementation in progress.**
> Date: 2026-04-27
> Branch: `claude/clever-montalcini-4902a1`
> Predecessor docs: [input-authority-and-menu-pool-2026-04-13.md](input-authority-and-menu-pool-2026-04-13.md), [menu-stack-architecture.md](menu-stack-architecture.md), [activemenu-radial-architecture.md](activemenu-radial-architecture.md)
> Trigger: Mike, 2026-04-27 - "Our Unreal-Action-Map-inspired input system shall be the ONLY means of input we utilize, period." Plus a concrete bug: completing mission 1 objective 1, pressing Continue on the post-game screen, the intro cutscene for objective 2 flashes for ~half a second and dismisses without playing.

This doc is the architectural ledger for moving the codebase from "action map plus IMC stack with side channels" to "action map plus typed Input Layer Stack plus Scene/State Manager, with no side channels left." It also fixes the mission-2 cutscene flash and lays the foundation for per-player cutscene state in co-op.

Sections:
A. Current state audit
B. Action Map API spec
C. Input Layer Stack API spec
D. Scene/State Manager API
E. Layer type catalog
F. Menu transition graph spec
G. Cutscene-specific behavior (per-player layer, invuln + invisible)
H. Mission/objective transitions (root cause + sweep plan)
I. Migration plan
J. Test coverage
K. Decisions for Mike
L. Decisions made during execution (running log)

---

## A. Current state audit

### A.1 What we already have

The codebase is roughly 60% of the way to the target architecture. The substrate is real, not greenfield.

**Action map** lives at `port/src/actionmap.cpp` plus `port/include/actionmap.h`. It defines an `InputAction` enum (51 actions in 9 groups: Movement, Aim, Combat, Weapons, Vehicle, Menu Nav, C-Buttons, D-Pad, System), a per-player state array `s_State[MAX_PLAYERS][ACTION_COUNT]`, and an IMC (Input Mapping Context) data structure that holds `triggers[ACTION_COUNT][N]` arrays of VK codes. Public reads are `actionPressed / actionHeld / actionReleased / actionValue`. Dispatch is `actionmapDispatch(SDL_Event)` plus `actionmapPollFrame()` for analog axes.

**IMC stack** lives at `port/src/inputctx.c` plus `port/include/inputctx.h`. Today's IMCs:

| IMC | Priority | Activated by |
|-----|----------|--------------|
| `g_ImcTextInput` | 30 | Text field focused |
| `g_ImcDebugOverlay` | 20 | F12 debug open |
| `g_ImcPauseMenu` | 11 | `g_CtxPauseMenu` pushed |
| `g_ImcMenu` | 10 | `g_CtxImGuiMenu` / `g_CtxPauseMenu` / `g_CtxDebugOverlay` pushed |
| `g_ImcForge` | 7 | Forge freefly active |
| `g_ImcForgeSession` | 6 | Forge session active (NORMAL or FREEFLY) |
| `g_ImcVehicle` | 5 | `bbikeInit` (mounted hoverbike) |
| `g_ImcGameplay` | 0 | Always active (bottom of stack) |

`fireVk()` walks priority descending and the first IMC that binds the VK wins. Frame-end is `actionmapEndFrame()` plus `inputCtxEndFrame()`. `gameplayInputSuppressed()` (the predicate from input-authority-and-menu-pool-2026-04-13.md) gates gameplay-only actions at both dispatch and read sites.

**Menu pool** lives at `port/src/menupool.c` plus `port/include/menupool.h`. 43 `menu_type_t` enum values, one slot per type, structural dedup, `menupoolReleaseAll()` cascade-close, optional ctx attachment for ImGui-renderer migration.

### A.2 What is missing or wrong

**Drift from the documented dispatch order.** The 2026-04-13 ADR documents `pdguiProcessEvent` order as `hotkey -> suppress -> actionmapDispatch -> ImGui process -> inputCtxDispatch`. The code at [pdgui_backend.cpp:1356-1422](port/fast3d/pdgui_backend.cpp:1356) actually runs `ImGui_ImplSDL2_ProcessEvent` BEFORE `actionmapDispatch`, with a `WantCaptureKeyboard` gate (B-154). This drift is intentional but the doc never got updated. The new doc here owns the corrected order.

**No formal Input Layer Stack.** The IMC stack governs which keybindings win, but it does NOT govern which scene is active. There is no first-class `Layer.Boot / Cutscene / Gameplay / Menu / Vehicle.Driver / Vehicle.Turret / Observer` enum. Today the layer concept is implicit, smeared across `g_Vars.tickmode`, `bondmovemode`, the IMC stack, the menu pool, and `g_InCutscene`. The layer model in this doc unifies them.

**Raw input handlers outside the action map.** Audit findings (full list in audit report appended below):

- ~50 raw `ImGui::IsKeyPressed(ImGuiKey_Escape/Enter/Up/Down/Left/Right/PageUp/PageDown/Space/KeypadEnter)` callsites across `pdgui_menu_solomission.cpp` (~50 alone), `pdgui_menu_mainmenu.cpp` (~13), `pdgui_menu_pausemenu.cpp` (~10), `pdgui_menu_room.cpp` (~7 plus 2 GamepadFace direct reads), `pdgui_menu_training.cpp` (~12), `pdgui_menu_endscreen.cpp` (~2), and ~15 other menus.
- Raw letter/symbol keys: `pdgui_menu_agentselect.cpp:308-376` (C/D/Delete), `pdgui_menu_solomission.cpp` (Q/E for tab cycle), `pdgui_menu_network.cpp:394` (right-click paste), `pdgui_menu_theme_editor.cpp:966` (LMB outside-click dismiss), `pdgui_menu_moddinghub.cpp:2885` (LMB outside-click).
- Raw gamepad reads: `pdgui_menu_room.cpp:4242-4356` (`ImGuiKey_GamepadFaceUp/Left`), `pdgui_menu_mpsettings.cpp:871,935` (GamepadFaceLeft).
- Dev/debug surfaces with their own input pumps: `pdgui_spectator.cpp:165-185` (PageUp/Down/Tab/R/Esc/WASDQE plus raw `io.MouseDelta` for free-fly), `pdgui_skin_editor.cpp:462-557` (mouse drag, keyboard tools), `pdgui_forge_hud.cpp:151-160` (Insert/Delete/End/Home gated on `!WantCaptureKeyboard`, with a comment admitting the bypass), `pdgui_friends.cpp:813-820` (Tab and V for voice PTT, comment says "temporary placement until input scope reopens").
- Legacy gameplay mouse: `port/src/input.c:740-1090` (`SDL_GetMouseState`, `SDL_GetRelativeMouseState`, `SDL_GetKeyboardState`, `SDL_GameControllerGetButton` inside `inputKeyPressed`/`inputKeyJustPressed`/`mouseUpdate`, all gated by `pdguiIsActive()`). Source comments at line 1031-1037 mark these as on-deprecation-track; only `optionsmenu.c` rebind capture and `menu.c` legacy mouse are still calling them.
- Cursor authority leaks (active constraint says "only `inputctx.c` may call `SDL_SetRelativeMouseMode` / `SDL_ShowCursor`"): `port/src/input.c:1128` (`inputLockMouse`, gated by `pdguiIsActive()`), `port/fast3d/pdgui_backend.cpp:864` (B-92 deferred-flush hotswap close). Both are documented bypasses and both are migration targets.

**Menu transition graph is implicit.** There is no enumerable list of (source menu, trigger action, destination menu, payload). Most transitions are call-graph-discoverable via `menuPushDialog` / `menuPopDialog` / bridge functions in `pdgui_bridge.c`, but a few escape:

- `pdgui_menu_mainmenu.cpp` flat `s_MenuView` model. Settings (2), Mods (3), Online (4), Stats (5), Grid (6) are intra-menu states with no pool acquire/release per sub-view; back-out is a `s_MenuView=0` write.
- Direct `mainChangeToStage()` from `pdgui_menu_solomission.cpp:2953` (Restart Mission), `pdgui_menu_forge.cpp:73,109` (Forge entry), `pdgui_debugmenu.cpp:245` (debug stage jump), `pdgui_menu_mainmenu.cpp:4622` indirectly via `pdguiForgeStartSessionOn`.
- Direct `netStartClientWithHolePunch(addr)` from inside `pdgui_menu_mainmenu.cpp:5050+` (Online Play row click) AND from `pdgui_menu_network.cpp:446`. Two paths to the same effect, neither registered as a graph edge.
- Three pause menus share namespace: `pdgui_menu_pausemenu.cpp` is MP combat-sim pause, `pdgui_menu_solomission.cpp` has solo pause inline at line 2880+, `pdgui_menu_mppause.cpp` is in-game MP pause. None of the three lists its outgoing edges as data.

**Cutscene state is global, not per-player.** `g_InCutscene`, `g_CutsceneAnimNum`, `g_CutsceneCurAnimFrame60`, `g_CutsceneSkipRequested`, `g_CutsceneCurTotalFrame60f`, and `g_Vars.in_cutscene` are all single globals. `bmoveSetModeForAllPlayers(MOVEMODE_CUTSCENE)` flips every player in lockstep. The net protocol broadcasts `SVC_CUTSCENE active=1/0` server-authoritative; clients freeze input. There is no per-player cutscene state, no per-player skip request, no invulnerable-and-invisible flag separate from the cutscene flag.

**The flash bug has a clear root cause.** Documented in section H below. Short version: the held state of `ACTION_USE` survives the menu-accept-then-stage-transition path; the new stage's intro cutscene starts with `g_CutsceneCurTotalFrame60f = 0`, the 30-frame gate at [player.c:3058](src/game/player.c:3058) protects only the first ~0.5s, and there is no held-state flush at any point in the transition. By tick ~31 the still-held ACTION_USE sets `g_CutsceneSkipRequested = true` and the AI script's `aiIfCutsceneButtonPressed` branches out.

### A.3 Inventory of "removed constraints" we can drop

Per `context/constraints.md`, these are gone and we should not architect around them:

- N64 platform guards (Phase D1).
- 4-player local multiplayer support (single local player only since 2026-04-10).
- Legacy N64 menu system (ImGui is sole menu since P10 D5.7).
- Numeric asset lookups (catalog ID strings everywhere since 2026-04-02).

The action map is already PC-only and assumes one local human; the layer model below assumes the same. No splitscreen plumbing.

---

## B. Action Map API spec

The action map is the foundation. This section formalizes what exists and identifies the small additions needed.

### B.1 Public API (kept from current code)

```c
// Header: port/include/actionmap.h

bool actionPressed(int player, InputAction a);   // edge: just went down this frame
bool actionHeld   (int player, InputAction a);   // level: down right now
bool actionReleased(int player, InputAction a);  // edge: just went up this frame
float actionValue (int player, InputAction a);   // [-1..1] for analog
float actionAxis  (int player, InputAction axis); // alias for analog reads

void actionmapDispatch(const SDL_Event *ev);     // raw event -> s_State write (gated)
void actionmapPollFrame(void);                   // analog stick + mouse polling
void actionmapEndFrame(void);                    // clear pressed/released edges

// IMC management
void imcActivate(InputMappingContext *imc);
void imcDeactivate(InputMappingContext *imc);
bool imcIsActive(InputMappingContext *imc);
void actionmapBindAction(InputMappingContext *imc, InputAction a, int vk);
void actionmapClearAction(InputMappingContext *imc, InputAction a);
```

### B.2 Required additions (small)

**`actionmapFlushHeldState(int player)`** - already declared as `actionmapFlushGameplayState()` at [actionmap.cpp:1275](port/src/actionmap.cpp:1275). Promote to a public, layer-aware variant: `actionmapFlushLayerActions(int player, LayerType layer)`. Clears `s_State[player][a]` for every action in the named layer's declared action set. Called by the Layer Stack on every push and pop. This is the fix for the cutscene flash and for the alt-tab class of held-state survival bugs.

**`actionRegisterDevice(SDL_GameController *)` / `actionUnregisterDevice(...)`** - kept as is.

**Action set introspection:**

```c
const InputAction *actionmapLayerActionSet(LayerType layer, int *out_count);
bool actionmapLayerHasAction(LayerType layer, InputAction a);
```

These let the Layer Stack ask "what actions does this layer own" without hardcoding the answer at every callsite. Used by the flush helper above and by the Menu Graph spec at section F.

### B.3 New ACTION_* enum entries

To finish migrating the raw ImGui::IsKey* sites in section A.2, three new actions are needed. All keyboard binds map 1:1 to the keys the code currently reads raw.

```c
ACTION_TEXT_PASTE,         // Right-click on text input (pdgui_menu_network.cpp:394)
ACTION_TAB_PREV,           // Q in solomission (currently raw)
ACTION_TAB_NEXT,           // E in solomission (currently raw)
```

`ACTION_USE` and `ACTION_MENU_ACCEPT` are already aliased; that aliasing stays. A new `ACTION_OUTSIDE_DISMISS` was considered for the LMB-outside-click dismiss in theme editor / modding hub but it is fundamentally a hit-test-against-rect call, not an action; leave it as ImGui IO (the only legitimate IsMouseClicked use, since "did you click OUTSIDE this widget" is a query about ImGui geometry, not a player intent).

### B.4 Order of operations (corrected from 2026-04-13 ADR drift)

```
SDL_PollEvent  (gfx_sdl_handle_events in gfx_sdl2.cpp:315)
  +-- pdguiProcessEvent(ev)  (pdgui_backend.cpp:1241)
       +-- Global hotkey check                 (consumed early)
       +-- inputCtxShouldSuppressKey(ev)        (consumed if in grace period)
       +-- ImGui_ImplSDL2_ProcessEvent(ev)      (writes io.WantCaptureKeyboard)
       +-- if (io.WantCaptureKeyboard) drop key (B-154 gate)
       +-- actionmapDispatch(ev)                (writes s_State, gated by Layer Stack)
       +-- inputCtxDispatch(ev)                 (game vs. menu routing)

       [per-frame]
       inputCtxPollFrame()         (Layer Stack tick - sees scene transitions)
       actionmapPollFrame()        (analog sampling, gated by Layer Stack)
       <game tick - reads s_State via actionHeld/Pressed/etc.>
       actionmapEndFrame()         (clear edges)
       inputCtxEndFrame()          (process deferred pops + cursor mode)
```

The Layer Stack (section C) replaces the current `inputCtxStack` as the authority that decides which IMCs are active. The IMCs themselves stay; the activation logic moves up.

---

## C. Input Layer Stack API spec

### C.1 Concept

A typed stack of layers. Each layer declares:

- A `LayerType` (enum constant).
- An action set (which `InputAction`s are active when this layer is on top).
- An entry payload type (typed at compile time) and a result payload type.
- Optional `onPush(payload) -> int`, `onPop(result *out)`, `onAbort(reason)` callbacks.

Push and pop are explicit. Only the topmost layer fires actions. New layer types are added by a single declaration at the bottom of `port/include/inputlayer.h`.

### C.2 Public API

```c
// port/include/inputlayer.h

typedef enum {
    LAYER_BOOT = 0,           // pre-stage-load, splash, copyright
    LAYER_GAMEPLAY,           // bondmove + bondgun, the main game
    LAYER_CUTSCENE,           // playerTickCutscene path, per-player
    LAYER_MENU,               // any menu/overlay/ImGui dialog
    LAYER_VEHICLE_DRIVER,     // bbikeTick, mounted hoverbike driver
    LAYER_VEHICLE_TURRET,     // future, gunner seat
    LAYER_OBSERVER,           // Dr Carroll free-fly + forge freefly
    LAYER_TYPE_COUNT,
} LayerType;

typedef struct LayerHandle LayerHandle; // opaque

typedef int  (*LayerOnPushFn)(void *payload);
typedef void (*LayerOnPopFn)(void *result_out);
typedef void (*LayerOnAbortFn)(int reason_code);

typedef struct {
    LayerType type;
    const char *name;                      // for logs: "Gameplay", "Cutscene", etc.
    const InputAction *action_set;         // owned actions
    int action_set_count;
    LayerOnPushFn on_push;                 // optional (may be NULL)
    LayerOnPopFn  on_pop;                  // optional
    LayerOnAbortFn on_abort;               // optional
    InputMappingContext *imc;              // bound IMC (e.g., g_ImcGameplay)
    SDL_bool wants_relative_mouse;         // cursor mode contract
    SDL_bool wants_visible_cursor;
} LayerDef;

LayerHandle *inputLayerPush(const LayerDef *def, void *payload);  // logs INPUT.LAYER.PUSH
int          inputLayerPop (LayerHandle *h, void *result_out);    // logs INPUT.LAYER.POP
void         inputLayerAbort(int from_top, int reason_code);      // logs INPUT.LAYER.ABORT
LayerHandle *inputLayerTop  (void);
LayerType    inputLayerTopType(void);
bool         inputLayerHas  (LayerType t);
int          inputLayerDepth(void);
```

### C.3 Semantics

**Exclusivity.** Only the topmost layer fires actions. `actionmapDispatch` and `actionPressed/Held/Released/Value` consult `inputLayerTop()` and skip writes/reads for actions not in the top layer's `action_set`. This subsumes today's `gameplayInputSuppressed()` predicate; that predicate becomes a thin wrapper that returns `inputLayerTopType() != LAYER_GAMEPLAY`.

**Push.** Calls `actionmapFlushLayerActions(0, currentTopType)` first to clear held state from the outgoing top, then activates the new layer's IMC, runs `on_push(payload)`, and emits `INPUT.LAYER.PUSH name=Cutscene depth=2`.

**Pop.** Calls `on_pop(out)`, deactivates the IMC, calls `actionmapFlushLayerActions(0, popped.type)`, and emits `INPUT.LAYER.POP name=Cutscene result=ok depth=1`.

**Abort cascade.** `inputLayerAbort(N, reason)` pops the top N layers, calling `on_abort(reason)` on each as it unwinds. Used on stage teardown, disconnect, fatal error. The bridge functions in `pdgui_bridge.c` and `netDisconnect()` migrate to call `inputLayerAbort(inputLayerDepth() - 1, ABORT_STAGE_TEARDOWN)` instead of today's ad-hoc `menupoolReleaseAll()` plus `inputCtxPopDeferred` pairs.

**Cursor mode.** Each layer declares `wants_relative_mouse` and `wants_visible_cursor`. The frame-end pass reads the top layer's preferences and calls `SDL_SetRelativeMouseMode` plus `SDL_ShowCursor` exactly once. This formalizes the existing `inputCtxApplyCursorVisibility` pattern (B-259, S468) into the layer model and closes the two known cursor-authority leaks (`input.c:1128`, `pdgui_backend.cpp:864`).

### C.4 Relationship to existing IMC stack

The existing IMC stack (`g_ImcGameplay`, `g_ImcMenu`, `g_ImcVehicle`, etc.) is preserved as the binding-resolution layer. The Layer Stack is the activation-and-action-set layer. One layer can activate one IMC. The mapping:

| LayerType | IMC | Cursor mode |
|-----------|-----|-------------|
| LAYER_BOOT | none (no input accepted) | hidden |
| LAYER_GAMEPLAY | `g_ImcGameplay` | relative + hidden |
| LAYER_CUTSCENE | (none, or a thin `g_ImcCutscene` with only ACTION_SKIP_CUTSCENE) | hidden |
| LAYER_MENU | `g_ImcMenu` plus optional `g_ImcPauseMenu` / `g_ImcDebugOverlay` / `g_ImcTextInput` | absolute + visible |
| LAYER_VEHICLE_DRIVER | `g_ImcVehicle` | relative + hidden |
| LAYER_VEHICLE_TURRET | new `g_ImcVehicleTurret` (greenfield) | relative + hidden |
| LAYER_OBSERVER | `g_ImcForge` plus `g_ImcForgeSession` | relative + hidden |

The IMC priority numbers stay where they are; the Layer Stack just decides which IMCs are active. This means the migration is additive: existing IMC code keeps working while we layer the new structure on top.

---

## D. Scene/State Manager API

The Scene Manager coordinates layer transitions with scene events. It is the layer between game logic and the input layer stack.

### D.1 Concept

A small finite-state machine that listens for scene events (mission start, cutscene start, pause, vehicle board) and pushes/pops layers in response. Game code emits events; the Scene Manager translates them into layer operations.

### D.2 Public API

```c
// port/include/scene.h

typedef enum {
    SCENE_EVENT_BOOT_COMPLETE,
    SCENE_EVENT_STAGE_LOADING,
    SCENE_EVENT_STAGE_READY,
    SCENE_EVENT_GAMEPLAY_START,
    SCENE_EVENT_CUTSCENE_START,        // payload: anim_num, player_mask
    SCENE_EVENT_CUTSCENE_END,          // payload: player_mask, end_reason
    SCENE_EVENT_PAUSE_OPEN,
    SCENE_EVENT_PAUSE_CLOSE,
    SCENE_EVENT_VEHICLE_BOARD,         // payload: vehicle_kind (driver / turret), prop *
    SCENE_EVENT_VEHICLE_DISMOUNT,      // payload: vehicle_kind
    SCENE_EVENT_OBSERVER_ENTER,        // payload: source (forge / dr_carroll / debug)
    SCENE_EVENT_OBSERVER_EXIT,
    SCENE_EVENT_STAGE_TEARDOWN,
    SCENE_EVENT_DISCONNECT,
} SceneEvent;

void sceneFire(SceneEvent ev, const void *payload);
LayerType sceneCurrentLayer(void);
```

### D.3 Wiring

The Scene Manager owns one switch statement: "given event E and current top layer L, what does the layer stack do?" Examples:

- `SCENE_EVENT_CUTSCENE_START` while top == GAMEPLAY -> push LAYER_CUTSCENE for the player_mask.
- `SCENE_EVENT_PAUSE_OPEN` while top == GAMEPLAY or top == CUTSCENE -> push LAYER_MENU with pause payload.
- `SCENE_EVENT_VEHICLE_BOARD` while top == GAMEPLAY -> push LAYER_VEHICLE_DRIVER (or _TURRET).
- `SCENE_EVENT_STAGE_TEARDOWN` -> abort cascade everything to BOOT.
- `SCENE_EVENT_DISCONNECT` -> abort cascade to MENU (main menu).

Every callsite that today does ad-hoc `mainChangeToStage` plus `menupoolReleaseAll` plus `inputCtxPopDeferred` plus `manifestClear` migrates to a single `sceneFire(SCENE_EVENT_*)` call. The Scene Manager owns the cleanup ordering invariants (constraint: `manifestClear` before `mainChangeToStage` during MP teardown, per [systemic-bugs.md](../systemic-bugs.md) SP-13) so individual callsites can no longer get the order wrong.

### D.4 Logging

Every event fires `TRANSITION.SCENE event=<name> from=<layer> to=<layer> reason=<...>`. This gives us a single grep to reconstruct the layer stack history of any session.

---

## E. Layer type catalog

Declared action sets per layer. Implementation is one table in `port/src/inputlayer.c`. Adding a layer is one struct literal.

### E.1 LAYER_BOOT

Pre-stage-load, splash, copyright. Action set: empty. Cursor: hidden. No input accepted (events drop on the floor). Used during initial boot and during stage transitions in flight.

### E.2 LAYER_GAMEPLAY

Active during normal play. Action set: all of Movement, Aim, Combat, Weapons, plus `ACTION_PAUSE`, `ACTION_USE`, `ACTION_CANCEL_USE`, `ACTION_RELOAD`, `ACTION_WEAPON_NEXT/PREV`, `ACTION_WEAPON_FUNCTION`, `ACTION_C_*`, `ACTION_D_*`, system hotkeys (`ACTION_SCREENSHOT`, `ACTION_DEBUG_TOGGLE`). Cursor: relative + hidden.

### E.3 LAYER_CUTSCENE

Active when a cutscene is playing. Action set: `ACTION_SKIP_CUTSCENE` plus `ACTION_PAUSE` only. Cursor: hidden. Per-player instance (see section G). Replaces today's `MOVEMODE_CUTSCENE` plus `g_InCutscene` global with a real layer.

### E.4 LAYER_MENU

Any menu, overlay, dialog. Action set: `ACTION_MENU_*`, `ACTION_USE` (alias `ACTION_MENU_ACCEPT`), `ACTION_CANCEL_USE` (alias `ACTION_MENU_CANCEL`), `ACTION_TAB_NEXT/PREV`, `ACTION_TEXT_PASTE`, `ACTION_PAUSE` (when in pause menu). Cursor: absolute + visible. Hosts the menu graph (section F).

### E.5 LAYER_VEHICLE_DRIVER

Active when player has mounted a driver-seat vehicle. Action set: `ACTION_VEHICLE_ACCELERATE`, `ACTION_VEHICLE_BRAKE`, `ACTION_VEHICLE_STEER_LEFT/RIGHT`, `ACTION_VEHICLE_EXIT`, `ACTION_PAUSE`, `ACTION_FIRE_PRIMARY` (if vehicle has a driver-fired weapon, otherwise omitted). Cursor: relative + hidden. Today's `g_ImcVehicle` plus `bbikeInit` lifecycle migrates here.

### E.6 LAYER_VEHICLE_TURRET

Greenfield. Reserved for future implementation. Action set: aim controls, fire, exit, pause. Cursor: relative + hidden.

### E.7 LAYER_OBSERVER

Dr Carroll free-fly, Forge freefly, debug fly-cam. Action set: 6DOF movement (`ACTION_FORGE_ASCEND/DESCEND` plus aim), `ACTION_FORGE_TOGGLE` (exit), boost, precision, `ACTION_PAUSE`. Cursor: relative + hidden. Today's two-IMC stack (`g_ImcForgeSession` plus `g_ImcForge`) collapses into one layer; if we want to preserve the "session active but not freefly" tier, that becomes LAYER_GAMEPLAY plus a pinned `g_ImcForgeSession` IMC override (still single-layer from the stack's view).

---

## F. Menu transition graph spec

### F.1 Goal

Every menu node declares its outgoing edges as data. Edges are enumerable. CI test asserts every edge has a valid destination and every interactive control fires through an edge.

### F.2 Edge declaration

```c
// port/include/menugraph.h

typedef enum {
    EDGE_DEST_PUSH_MENU,         // push another LAYER_MENU dialog
    EDGE_DEST_POP_TO_PARENT,     // pop self
    EDGE_DEST_POP_TO_ROOT,       // pop entire menu stack
    EDGE_DEST_SCENE_EVENT,       // sceneFire() with payload
    EDGE_DEST_STAGE_CHANGE,      // mainChangeToStage(stagenum) - last-resort
    EDGE_DEST_NETWORK_OP,        // netStartClient / netStartServer / netDisconnect
    EDGE_DEST_PROCESS_EXIT,      // SDL_QUIT (Quit Game)
} EdgeDestKind;

typedef struct {
    InputAction trigger;         // ACTION_USE, ACTION_CANCEL_USE, etc.
    const char *label;           // optional, for log/debug
    EdgeDestKind kind;
    union {
        menu_type_t push_target;
        SceneEvent  scene_event;
        s32         stagenum;
        const char *network_op;  // "client", "server", "disconnect"
    } payload;
    int (*payload_provider)(void *out_payload, size_t out_size); // optional
    bool (*available)(void);                                     // optional gate
} MenuEdge;

typedef struct {
    menu_type_t type;
    const char *name;
    const MenuEdge *edges;
    int edge_count;
} MenuNode;

const MenuNode *menuGraphNode(menu_type_t t);
const MenuEdge *menuGraphEdge(menu_type_t t, InputAction trigger);
bool menuGraphFire(menu_type_t source, InputAction trigger);
```

`menuGraphFire` looks up the edge and executes it: push pool slot, fire scene event, change stage, etc. Renderers stop calling `menuPushDialog` / `mainChangeToStage` directly and start calling `menuGraphFire(MENU_TYPE_SOLO_MISSION, ACTION_USE)`.

### F.3 Required nodes (initial migration scope)

The 30 ImGui menus map onto ~25 distinct `menu_type_t` values (some share, e.g. `MENU_TYPE_MP_SETUP` covers 11 dialogdefs). Each gets one `MenuNode` entry. Initial scope: the high-traffic transition nodes in the "messy" list from section A:

1. `MENU_TYPE_MAIN_MENU` (replace `s_MenuView` flat-state with sub-menu pushes; or, keep the flat state but declare each transition as an edge).
2. `MENU_TYPE_SOLO_MISSION` (Restart Mission goes through SCENE_EVENT, not direct `mainChangeToStage`).
3. `MENU_TYPE_ROOM` (Start Match emits SCENE_EVENT_GAMEPLAY_START with payload).
4. `MENU_TYPE_ENDSCREEN_SOLO`, `MENU_TYPE_ENDSCREEN_MP` (Continue / Retry / Main Menu / Disconnect all become edges).
5. `MENU_TYPE_PAUSE_MENU`, `MENU_TYPE_MP_PAUSE`, the solo pause inside `pdgui_menu_solomission.cpp` (dedup the three pauses while we are here, see Decision K.4).
6. `MENU_TYPE_SOCIAL_LOBBY` (Create Room, Disconnect).
7. `MENU_TYPE_NETWORK` (Host, Join, Back). The duplicate path in main menu's Online Play sub-view migrates to fire the same edges.
8. `MENU_TYPE_AGENT_SELECT` (Load, Create, Save default).
9. `MENU_TYPE_WARNING_MODAL` (a meta-node; its edges are dynamic per invocation, declared in the SELECTABLE handler).

### F.4 CI test (lands in same commit per methodology)

`tests/test_menu_graph.cpp` walks every `MenuNode`, asserts:

- Every edge has a valid `kind` and a non-null payload (per kind).
- Every `EDGE_DEST_PUSH_MENU` target is a registered `menu_type_t`.
- Every `EDGE_DEST_SCENE_EVENT` is in the `SceneEvent` enum.
- Every interactive widget in the renderer (visited via a synthetic walk like cohort 2's `test_menu_reachability.cpp`) has a corresponding edge by `trigger`.
- No two edges in the same node share the same `trigger`.

Logs: `MENU.GRAPH.FIRE source=<type> trigger=<action> dest=<kind> ok=<0/1>`.

---

## G. Cutscene-specific behavior

This section is the longest because the cutscene flash bug intersects the layer model AND co-op cutscene state AND the invuln-and-invisible rule.

### G.1 Layer push and pop

`SCENE_EVENT_CUTSCENE_START` payload: `{ s16 anim_num, u8 player_mask, u8 trigger_source }`. The Scene Manager pushes `LAYER_CUTSCENE` for each player in `player_mask`. In single-player and listen-host solo, mask = 0x01. In co-op MP, mask is whatever players are in the cutscene-watching set (typically all players in the same stage).

`SCENE_EVENT_CUTSCENE_END` payload: `{ u8 player_mask, u8 reason }`. Reasons: `END_REASON_NATURAL_FINISH`, `END_REASON_SKIP`, `END_REASON_ABORT`. Pops `LAYER_CUTSCENE` for each player in mask.

### G.2 Per-player cutscene state

Today's globals move to `struct player`:

```c
// src/include/types.h additions to struct player
struct {
    s16  anim_num;
    s32  cur_anim_frame_60;
    s32  cur_anim_frame_240;
    f32  cur_total_frame_60f;
    bool skip_requested;
    bool active;
} cutscene;
```

`g_InCutscene` becomes `playerInCutscene(int playernum)`. `bmoveSetModeForAllPlayers(MOVEMODE_CUTSCENE)` is replaced by the layer push for the player_mask; players outside the mask stay in their previous mode.

Net protocol: `SVC_CUTSCENE` (0x53) gains a `u8 player_mask` field. Wire bump from current `NET_PROTOCOL_VER 44` -> v45. New CLC message `CLC_CUTSCENE_SKIP { u8 playernum }` for client-driven skip in co-op.

### G.3 Invulnerable + invisible while in cutscene layer

Mike's directive: "Co-op should allow for 1 player to skip the cutscene while others may be watching it still. In that case, all players should be invisible to enemies and invulnerable until they are out of the cutscene, with skip or end cutscene making them be treated normally at that point."

Rule: while ANY player is in `LAYER_CUTSCENE`, ALL players in the same `player_mask` (the cutscene-watching set) get the invulnerable + invisible-to-enemies flag. The flag is `chr->cutscene_protect = 1` (new field on `struct chr`). Read sites:

- `chrDamage` family: if target's `cutscene_protect` is set, ignore damage (log `CUTSCENE.DAMAGE.IGNORED chr=<n> attacker=<n>`).
- `chrCanSeeChr` / `chrIsHostileTowards` / AI target selection: if target's `cutscene_protect` is set, return false / skip.
- Existing scattered `g_InCutscene` checks at [prop.c:2713,2725](src/game/prop.c:2713), [chr.c:2006](src/game/chr.c:2006), and bondgun multiple sites: replace with `chr->cutscene_protect`.

Set on layer push (per-player), cleared on layer pop. The "pop" can be triggered by:
1. Natural cutscene finish (AI script `playerEndCutscene`).
2. Local skip request (`ACTION_SKIP_CUTSCENE` press during `LAYER_CUTSCENE`).
3. Net-driven skip (server broadcasts `SVC_CUTSCENE_END mask` after consensus, see G.4).

The 30-frame initial-skip lockout from [player.c:3058](src/game/player.c:3058) stays. It is the second line of defense after the held-state flush.

### G.4 Co-op skip semantics

Three policy options (Decision K.5):

1. **Any-player-skips-all.** First player to press SkipCutscene ends the cutscene for everyone. Simple but defeats the "others may be watching" intent.
2. **Per-player-skip.** Each player independently pops their own LAYER_CUTSCENE. Watchers continue. Matches Mike's directive directly.
3. **Majority-skip.** Need >50% to skip. Compromise.

This doc proposes (2). Implementation: server tracks which players are still in LAYER_CUTSCENE (a `g_NetCutsceneActiveMask`); each `CLC_CUTSCENE_SKIP` clears the bit; only when the mask hits 0 (or the cutscene's natural duration elapses) does the AI script's `aiIfCutsceneButtonPressed` receive `true`. Players who pop early go back to gameplay tickmode but RETAIN `cutscene_protect=1` until the final mask bit clears.

### G.5 Held-state flush (the flash bug fix)

`LAYER_CUTSCENE` push calls `actionmapFlushLayerActions(playernum, LAYER_GAMEPLAY)` for that player BEFORE the cutscene tick begins. This clears any held `ACTION_USE` / `ACTION_FIRE_*` / `ACTION_PAUSE` carrying over from the menu accept. Combined with the existing 30-frame lockout, the flash bug becomes structurally impossible: there is no path from "menu accept" to "cutscene skip" without releasing and re-pressing a skip key.

Test: `tests/test_cutscene_layer.cpp`:
- Held `ACTION_USE` at LAYER_GAMEPLAY -> push LAYER_CUTSCENE -> assert `actionHeld(0, ACTION_USE) == false` after push.
- Hold + push + tick to frame 31 + assert `g_CutsceneSkipRequested == false`.
- Press fresh ACTION_SKIP_CUTSCENE at frame 35 -> assert `g_CutsceneSkipRequested == true`.

---

## H. Mission/objective transitions

### H.1 The flash bug, line by line

Repro: solo, mission 1 obj 1 complete -> endscreen -> Continue button.

1. Player presses `ACTION_USE` (mapped to keyboard E or controller A) on the endscreen Continue button. The press is consumed by ImGui via `pdgui_menu_endscreen.cpp` (S385 confirm-modal pattern). `s_State[0][ACTION_USE].held = 1` and the ImGui consumed the event.
2. `endscreenHandleContinueMission` -> `endscreenContinue(2)` -> `endscreenAdvance` -> menuPushDialog briefing.
3. Briefing dialog: player presses Start (legacy `inputs->start` path). `menuhandlerAcceptMission` -> `menuStop()` -> `mainChangeToStage(stagenum)`.
4. Stage swap on next mainLoop tick. `g_StageSetup` populated. AI script runs, hits `aiSetCameraAnimation(anim)`, calls `playerStartCutscene(anim)`.
5. `playerStartCutscene` (`src/game/player.c:2843`) clears `g_CutsceneSkipRequested = false` and `g_CutsceneCurTotalFrame60f = 0` (lines 2851-2852). Calls `playerStartCutscene2` which does `bmoveSetModeForAllPlayers(MOVEMODE_CUTSCENE)`.
6. Frame 1 of cutscene: `playerTickCutscene` runs. Polls `actionHeld(0, ACTION_USE)` and friends at lines 3059, 3072, 3084. The `> 30` gate at line 3058 protects: `g_CutsceneCurTotalFrame60f` is 0, so the flag is NOT set this tick.
7. Frames 2-30: same. Gate holds. But ACTION_USE is STILL held from step 1, because nothing flushed it.
8. Frame 31: `g_CutsceneCurTotalFrame60f = 31.0f`, the gate opens. `actionHeld(0, ACTION_USE) == true`. `g_CutsceneSkipRequested = true`.
9. AI script's next `aiIfCutsceneButtonPressed` (opcode 0174) check fires, branches to the post-cutscene label. `playerEndCutscene` runs.

Net visible effect: the cutscene played for ~31 frames (~0.5s) and dismissed. Matches "appeared and immediately went away."

### H.2 The fix

The Layer Stack's push contract (section C.3) calls `actionmapFlushLayerActions(0, LAYER_GAMEPLAY)` on every push. The `SCENE_EVENT_GAMEPLAY_START` (after stage load) and the `SCENE_EVENT_CUTSCENE_START` (when AI script triggers) both push layers and both flush. The held ACTION_USE gets cleared at step 5, before the cutscene tick begins. Step 8 reads `actionHeld == false`. Skip never fires from a stale held state.

Belt-and-braces: Decision K.6 proposes also requiring `actionPressed` (edge), not `actionHeld` (level), at [player.c:3059,3072,3084](src/game/player.c:3059) for the skip detection. Edge requires a fresh press during the cutscene; even if the flush were ever skipped, a held key from before would not register as a press.

### H.3 Sweep of all transition paths

Every objective-end -> objective-start path becomes a `sceneFire(SCENE_EVENT_*)` callsite. Audit deliverable in Phase 2: enumerate every `mainChangeToStage` callsite in the codebase and classify:

- Stage-end via natural objective completion: emit `SCENE_EVENT_STAGE_TEARDOWN` then `SCENE_EVENT_STAGE_LOADING(next)`.
- Stage-end via abort or main menu return: emit `SCENE_EVENT_DISCONNECT` (for MP) or `SCENE_EVENT_STAGE_TEARDOWN` (solo).
- Stage-start via mission select: emit `SCENE_EVENT_STAGE_LOADING(target)`.
- Stage-start via Continue from endscreen: same.
- Stage-start via Restart Mission: same.

Today's known callsites of `mainChangeToStage`: `endscreen.c`, `mainmenu.c:825`, `pdgui_menu_solomission.cpp:2953`, `pdgui_menu_forge.cpp:73,109`, `pdgui_debugmenu.cpp:245`, plus indirect via bridge. The Phase 2 sweep finds the rest and migrates them to `sceneFire`.

### H.4 Cutscene-flash regression test

`tests/test_objective_transition.cpp`:
- Synthetic scenario: hold ACTION_USE, fire `SCENE_EVENT_STAGE_LOADING(stage_with_intro_cutscene)`, advance ticks, verify the cutscene plays its full duration and is NOT skipped.
- Reverse: fire SCENE_EVENT_CUTSCENE_START, advance to frame 35, fire `actionPressed(ACTION_SKIP_CUTSCENE)`, verify cutscene ends.

---

## I. Migration plan

### I.1 Cohorts (each is one or more commits, sequential, bisectable)

**Cohort 1: Action map additions.** Add `ACTION_TAB_NEXT/PREV`, `ACTION_TEXT_PASTE` enums. Add `actionmapFlushLayerActions(int player, LayerType layer)` (renamed from `actionmapFlushGameplayState`). Add `actionmapLayerActionSet` introspection. Tests: `tests/test_actionmap_flush.cpp` for the layer-aware flush. Update [input-authority-and-menu-pool-2026-04-13.md](input-authority-and-menu-pool-2026-04-13.md) §2.1 to match the corrected dispatch order.

**Cohort 2: Input Layer Stack scaffolding.** New `port/include/inputlayer.h` and `port/src/inputlayer.c`. `LayerType` enum, `LayerDef` table, push/pop/abort API. `inputCtx`'s push/pop becomes the implementation under the hood; the IMC stack stays. Default LAYER_BOOT pushed at process init. `inputLayerTopType` exposed to existing `gameplayInputSuppressed` (which becomes a one-line wrapper). Tests: `tests/test_input_layer_stack.cpp` for push/pop/abort/exclusivity.

**Cohort 3: Scene Manager scaffolding.** `port/include/scene.h` and `port/src/scene.c`. `SceneEvent` enum, `sceneFire` dispatcher. Initial wiring: `mainChangeToStage` callsites stay as-is for now, but `mainTickStage`'s "stage loaded" callback fires `SCENE_EVENT_STAGE_READY` -> push LAYER_GAMEPLAY. `playerStartCutscene` fires `SCENE_EVENT_CUTSCENE_START` -> push LAYER_CUTSCENE. `playerEndCutscene` fires `SCENE_EVENT_CUTSCENE_END` -> pop LAYER_CUTSCENE. Existing `g_InCutscene` still set as a transitional shim. Tests: `tests/test_scene_dispatch.cpp` invariants on layer transitions per event.

**Cohort 4: Cutscene per-player state and the flash fix.** Move `g_InCutscene`, `g_CutsceneAnimNum`, `g_CutsceneSkipRequested`, `g_CutsceneCurTotalFrame60f`, `g_CutsceneCurAnimFrame60` from globals to `struct player.cutscene`. `g_Vars.in_cutscene` becomes `playerInCutscene(playernum)`. Net protocol bump v44 -> v45 (`SVC_CUTSCENE` gains `player_mask`, new `CLC_CUTSCENE_SKIP`). Per-player skip semantics. `chr->cutscene_protect` for invuln-and-invisible. Read-site sweep: ~30 sites (prop.c, chr.c, bondgun.c, AI target selection). Cutscene flash fixed by Cohort 2's flush at LAYER_GAMEPLAY exit. Tests: `tests/test_cutscene_layer.cpp` (the held-state assertion), `tests/test_objective_transition.cpp` (the flash repro).

**Cohort 5: Vehicle and Observer layers formalized.** `LAYER_VEHICLE_DRIVER` declared; `bbikeInit` and `bbikeExit` migrate from direct `imcVehicleMount/Dismount` to `sceneFire(SCENE_EVENT_VEHICLE_BOARD/_DISMOUNT)`. `LAYER_OBSERVER` declared; forge mode's MOVEMODE_CUTSCENE hijack stays (it works) but the layer push lives alongside it. `LAYER_VEHICLE_TURRET` declared but unwired (no code today); reserved for future. Tests: `tests/test_vehicle_layer.cpp` for mount/dismount, held-state flush across the transition.

**Cohort 6: Menu graph migration.** Declare `MenuNode` for each of the 9 priority menus in section F.3. Replace direct `menuPushDialog`/`mainChangeToStage`/`netStartClient*` calls inside menu render functions with `menuGraphFire`. `s_MenuView` flat-state in mainmenu.cpp gets per-sub-view pool acquires (or is rewritten to push real sub-menu types; Decision K.7). Tests: `tests/test_menu_graph.cpp` for edge validity, reachability, and trigger uniqueness.

**Cohort 7: Raw input handler migration.** Walk the audit punch list from section A.2. Convert every `ImGui::IsKeyPressed(ImGuiKey_Escape)` to `actionPressed(0, ACTION_CANCEL_USE)`, every `IsKeyPressed(ImGuiKey_Enter)` to `actionPressed(0, ACTION_USE)`, etc. Dev tools (spectator, skin editor, forge HUD, friends PTT) get their own IMC tier or join LAYER_OBSERVER's IMC. Two cursor-authority leaks (`input.c:1128`, `pdgui_backend.cpp:864`) routed through `inputCtxApplyRelativeMode`. Tests: `tests/test_no_raw_input.cpp` (a grep-style assertion that scans the source tree for `IsKeyPressed(ImGuiKey_*)` outside whitelisted files).

**Cohort 8: Retire transitional shims.** Remove `g_InCutscene` global (reads were already migrated in Cohort 4). Remove `gameplayInputSuppressed()` (replaced by `inputLayerTopType() != LAYER_GAMEPLAY`). Remove ad-hoc `manifestClear` plus `mainChangeToStage` plus `menupoolReleaseAll` triplets (Scene Manager owns them now).

### I.2 Phasing decision required from Mike

Eight cohorts is over the "~6 cohort" stop condition in the brief. Two phasing options:

**Option A: ship all 8 in one branch, sequentially.** Roughly 2-3 weeks of dev cadence (~3 days per cohort). Single auto-merge per cohort. Default per the existing `auto-merge-by-default-sequentially` rule.

**Option B: split into two branches.** Branch 1 = Cohorts 1-4 (action map additions, layer stack, scene manager, cutscene fix). Lands the user-visible flash fix sooner. Branch 2 = Cohorts 5-8 (vehicle / observer / menu graph / raw input migration / shim retirement). Lands the architectural completion later.

Surfaced as Decision K.1.

### I.3 Roll-back strategy

Cohorts 1-3 are pure additions; reverting either is a single-commit revert. Cohort 4 (cutscene per-player state) is the protocol-bump cohort and is the hardest to roll back; we revert by un-bumping wire and restoring globals. Cohorts 5-8 are mechanical migrations; bisectable per-file.

---

## J. Test coverage

All tests land in `tests/` alongside the cohort that introduces the invariant. pd-tests is the harness; the existing 155 cases / 1881 assertions stay green throughout.

### J.1 New test files

| File | Cohort | Cases (target) | Asserts (target) | What it checks |
|------|--------|---------------:|-----------------:|----------------|
| `test_actionmap_flush.cpp` | 1 | 8 | 60 | `actionmapFlushLayerActions` clears only the named layer's actions; other layers' state preserved |
| `test_input_layer_stack.cpp` | 2 | 14 | 180 | push/pop/abort/depth/top exclusivity; cursor mode contract; only-topmost-fires invariant |
| `test_scene_dispatch.cpp` | 3 | 10 | 120 | every SceneEvent in every legal current-top combination produces the documented layer transition; illegal combos return error |
| `test_cutscene_layer.cpp` | 4 | 12 | 150 | held ACTION_USE flushed on LAYER_CUTSCENE push; per-player state isolation; `cutscene_protect` flag set/cleared; net protocol field round-trip |
| `test_objective_transition.cpp` | 4 | 6 | 70 | the flash bug repro, fixed; every objective-end -> objective-start path produces the right layer sequence |
| `test_vehicle_layer.cpp` | 5 | 8 | 90 | bike mount push, dismount pop, held-state flush across transition; gameplay actions inactive while LAYER_VEHICLE_DRIVER on top |
| `test_menu_graph.cpp` | 6 | 15 | 200 | every node has well-formed edges; every edge has valid destination; trigger uniqueness per node; synthetic walk reaches every interactive widget |
| `test_no_raw_input.cpp` | 7 | 1 | (variable) | source-tree grep: `IsKeyPressed(ImGuiKey_*)` outside whitelisted files = fail |

Total target: ~74 cases / ~870 assertions on top of cohort 2's existing 39 / 418. Combined: 229 cases / ~2300 assertions when migration is complete.

### J.2 Logging assertions

Tests also assert log-channel emissions where load-bearing:

- `INPUT.LAYER.PUSH` and `INPUT.LAYER.POP` fire on every push/pop with name and depth.
- `INPUT.ACTION.SUPPRESSED` fires the first time per frame an action is gated by the layer stack.
- `MENU.GRAPH.FIRE` fires on every `menuGraphFire`.
- `CUTSCENE.LAYER.PUSH` includes player_mask.
- `CUTSCENE.DAMAGE.IGNORED` fires when invuln blocks damage.
- `TRANSITION.SCENE` fires on every `sceneFire` with from/to layer.

Hierarchical log channels match Mike's directive. Each is grep-friendly and reconstructs a session's transition history.

---

## K. Decisions for Mike

These are architecturally significant choices where the doc does not commit until Mike picks. Default proposal listed first; alternatives below.

### K.1 Phasing: one branch or two? (section I.2)

Default proposal: **Option B (two branches).** Lands flash fix sooner, smaller blast radius per merge.

Alternative: Option A (one branch, eight cohorts sequential). Cleaner final state, longer time to first user-visible win.

### K.2 LAYER_CUTSCENE IMC choice (section C.4)

Default proposal: **A thin new `g_ImcCutscene` IMC** with only `ACTION_SKIP_CUTSCENE` and `ACTION_PAUSE` bound. Keeps the layer-IMC mapping symmetrical.

Alternative: no IMC for LAYER_CUTSCENE; instead, special-case the cutscene-tick to read `actionPressed(0, ACTION_SKIP_CUTSCENE)` directly. Less code, more special case.

### K.3 Cutscene-protect rule scope (section G.3)

Default proposal: **invuln + invisible-to-enemies for ALL damage classes.** No exceptions. Matches Mike's directive verbatim ("treated normally" only after exit).

Alternative: invuln to AI damage but NOT to scripted environmental damage (e.g., a scripted explosion the cutscene is depicting). This requires per-damage-source classification which we do not have today; would cost a damage-class enum.

### K.4 Pause menu dedup (section F.3 item 5)

Default proposal: **leave the three pause renderers (`pdgui_menu_pausemenu.cpp` MP, solo pause inline in `pdgui_menu_solomission.cpp`, `pdgui_menu_mppause.cpp` in-game MP) alone.** Each handles a sufficiently different scene. Just register them under distinct `menu_type_t` values in the graph.

Alternative: collapse into a single `pdgui_menu_pause.cpp` with mode-switched body. Larger refactor, not required by this design.

### K.5 Co-op skip semantics (section G.4)

Default proposal: **per-player-skip.** Matches Mike's directive ("1 player to skip the cutscene while others may be watching it still"). Watchers retain `cutscene_protect` until last player exits.

Alternatives: any-player-skips-all (simpler, defeats the directive) or majority-skip (compromise). Both rejected.

### K.6 Skip detection: held vs pressed (section H.2)

Default proposal: **change cutscene skip detection from `actionHeld` to `actionPressed`** at [player.c:3059,3072,3084](src/game/player.c:3059). Belt + braces: requires fresh press during cutscene window, even if held-state flush ever fails.

Alternative: rely on flush only. Pure architectural fix, but less defense in depth.

### K.7 Main menu sub-view migration (section F.3 item 1)

Default proposal: **convert each `s_MenuView` sub-view to a real `MENU_TYPE_*` push.** Larger touch but uniform with the rest of the graph.

Alternative: keep `s_MenuView` as intra-menu state but declare each sub-view-change as a graph edge of kind `EDGE_DEST_INTERNAL_STATE`. Less code change but introduces an additional edge kind that is essentially invisible.

### K.8 Net protocol bump scheduling (section G.2)

Default proposal: **bump `NET_PROTOCOL_VER 45 -> 46` in Cohort 4** for the per-player cutscene state. (v45 was taken on 2026-04-27 by the Random / Fiesta session bump; this cohort takes the next slot.) Coordinated with `MPSETUP_VERSION` if any (none required by this change).

Alternative: defer protocol bump until Cohort 5 to combine with vehicle layer net work. Cohort 4 ships with shimmed wire format that broadcasts mask=0xFF unconditionally for back-compat.

### K.9 Forge / Observer collapse (section E.7)

Default proposal: **keep the two-IMC stack (`g_ImcForgeSession` plus `g_ImcForge`) but pin both to LAYER_OBSERVER.** "Session active but not freefly" stays expressible by IMC priority within the layer.

Alternative: split into LAYER_FORGE_SESSION and LAYER_OBSERVER (where LAYER_OBSERVER is freefly only). Adds a layer for a small benefit.

---

## L. Decisions made during execution

This section is a running log filled in during Phase 2 implementation. Entries land alongside the code change that motivated them.

### L.1 K.1 through K.9 resolved (2026-04-27, decided by parent session per delegated authority)

- **K.1 Phasing.** Two branches. Cohorts 1-4 land the user-visible flash fix first; Cohorts 5-8 finish the architecture in a follow-up branch.
- **K.2 LAYER_CUTSCENE IMC.** New thin `g_ImcCutscene` IMC bound to `ACTION_SKIP_CUTSCENE` plus `ACTION_PAUSE`. Symmetric with the rest of the layer-IMC mapping.
- **K.3 Cutscene-protect rule scope.** All damage classes covered, no carve-out for scripted environmental damage. Matches Mike's directive verbatim ("treated normally" only after exit).
- **K.4 Pause menu dedup.** Deferred. The three pause renderers (`pdgui_menu_pausemenu.cpp`, `pdgui_menu_solomission.cpp` inline, `pdgui_menu_mppause.cpp`) stay separate and just get distinct `menu_type_t` registrations in the graph.
- **K.5 Co-op skip semantics.** Per-player. First skip pops that player's `LAYER_CUTSCENE` and routes them back to gameplay tickmode; watchers continue. `cutscene_protect` retained on early-skippers until the last player exits the layer, so nobody is exposed during partial-watch.
- **K.6 Skip detection: held vs pressed.** `actionPressed` (edge), not `actionHeld`. Belt-and-braces alongside the layer-push held-state flush. The flush is the structural fix; the edge requirement is the second line of defense.
- **K.7 Main menu sub-views.** Convert `s_MenuView` flat-state to real `MENU_TYPE_*` pushes. Larger touch but uniform with the rest of the graph.
- **K.8 Net protocol bump.** v45 -> v46 in Cohort 4 (v45 was taken on 2026-04-27 by the Random / Fiesta session). `SVC_CUTSCENE` gains `u8 player_mask`; new `CLC_CUTSCENE_SKIP { u8 playernum }`.
- **K.9 Forge / Observer.** Keep two-IMC stack (`g_ImcForgeSession` priority 6 plus `g_ImcForge` priority 7) pinned to LAYER_OBSERVER. "Session active but not freefly" remains expressible by IMC priority within a single layer.

Phase 2 begins with Cohort 1 immediately following.

### L.2 Section B.3 correction (Cohort 1, 2026-04-27)

The Phase 1 doc proposed three new ACTION_* enum entries (`ACTION_TEXT_PASTE`, `ACTION_TAB_PREV`, `ACTION_TAB_NEXT`). On implementation: `ACTION_MENU_TAB_PREV` (id 49) and `ACTION_MENU_TAB_NEXT` (id 50) already exist in [port/include/actionmap.h:122-123](port/include/actionmap.h:122). Cohort 1 adds only `ACTION_TEXT_PASTE`. Cohort 7 will migrate the raw `ImGui::IsKeyPressed(ImGuiKey_Q/E)` reads in `pdgui_menu_solomission.cpp` to `actionPressed(0, ACTION_MENU_TAB_PREV/NEXT)` and add Q/E binds to `g_ImcMenu` defaults if not already covered.

### L.3 Cohort 1 shipped (2026-04-27)

- `port/include/actionmap.h`: added `ACTION_TEXT_PASTE` at id 71 (post-rebase onto S483b/S483c which claim ids 69/70 respectively); `ACTION_COUNT` advances 71 -> 72.
- `port/src/actionmap.cpp`: `actionIsGameplayOnly` returns 0 for `ACTION_TEXT_PASTE` (text-input ownership). `setupTextInputDefaults` adds `addBind(g_ImcTextInput, ACTION_TEXT_PASTE, VK_MOUSE_RIGHT)`. `s_ActionNames` array gets `"TextPaste"` at index 71.
- `tests/actionmap_pure.{c,h}` + `tests/test_actionmap_flush.cpp`: 8 cases / ~70 assertions locking down the existing flush + classifier semantics that Cohort 4's flash fix depends on. Includes a bug-named "actionmap flush: bug invariant (the Mission 1 obj 2 cutscene flash)" case asserting that gameplay-only siblings of held ACTION_USE clear under flush.
- [input-authority-and-menu-pool-2026-04-13.md](input-authority-and-menu-pool-2026-04-13.md) §2.1: dispatch-order documentation corrected to match the actual pdgui_backend.cpp ordering (ImGui-first with `WantCaptureKeyboard` gate, B-154 lineage).
- `CMakeLists.txt` SRC_TESTS: registered new files.

### L.4 Rebase note (Cohort 1, 2026-04-27)

Cohort 1 was committed at id 69 then rebased onto dev's S483b (id 69 = ACTION_SOCIAL_TOGGLE) and S483c (id 70 = ACTION_TESTSCEN_CYCLE_COUNT). Final id for ACTION_TEXT_PASTE is 71. No semantic change to Cohort 1; the pure-C test mirror (`tests/actionmap_pure.{c,h}`) does not track production ids and stays at its own internal ordering since the flush + classifier behavior under test does not depend on the absolute id.

### L.5 Cohort 2 shipped (2026-04-27)

- New module `port/include/inputlayer.h` + `port/src/inputlayer.c` -- typed Input Layer Stack. `LayerType` enum with 7 first-class scenes (Boot / Gameplay / Cutscene / Menu / VehicleDriver / VehicleTurret / Observer). Capacity 16. Handle-based push/pop with generation guard prevents use-after-pop. Abort cascade unwinds N layers in LIFO order. Canonical singletons declared (`g_LayerBoot`, `g_LayerGameplay`, etc.) with placeholder IMC + action-set fields that Cohort 3 will populate.
- Cohort 2 deliberately does NOT yet wire the Layer Stack into actual push sites (gameplay start / menu open / cutscene tick); that work is Cohort 3's Scene Manager dispatch. `gameplayInputSuppressed()` is unchanged. The IMC stack is unchanged. This cohort is pure scaffolding plus invariant tests.
- Tests: `tests/inputlayer_pure.{c,h}` pure-C mirror with per-type instrumentation (push_count / pop_count / abort_count / last_abort_reason) so callback ordering and reason-code propagation are observable. `tests/test_input_layer_stack.cpp` adds 15 cases / ~80 assertions covering: empty stack, init pushes BOOT, init idempotency, push/pop topology, mismatched-handle rejection, NULL-handle rejection, use-after-pop rejection, abort cascade with reason propagation, abort with from_top > depth clamps, abort with from_top <= 0 is no-op, capacity overflow, NULL def rejection, same-type stacking allowed (menu pool enforces uniqueness at higher level), payload threading into on_push, top-type sentinel when empty, shutdown aborts everything remaining.
- `CMakeLists.txt` SRC_TESTS: registered new files. `port/src/inputlayer.c` gets auto-discovered into pd via GLOB_RECURSE.

### L.6 Cohort 3 shipped (2026-04-27)

- New module `port/include/scene.h` + `port/src/scene.c` -- Scene / State Manager. `SceneEvent` enum with 14 events (BOOT_COMPLETE / STAGE_LOADING / STAGE_READY / GAMEPLAY_START / CUTSCENE_START / CUTSCENE_END / PAUSE_OPEN / PAUSE_CLOSE / VEHICLE_BOARD / VEHICLE_DISMOUNT / OBSERVER_ENTER / OBSERVER_EXIT / STAGE_TEARDOWN / DISCONNECT). `sceneFire(SceneEvent, payload)` is the dispatcher; payload structs declared for cutscene / vehicle / observer events.
- Dispatcher behavior: each push event caches a `LayerHandle` so the matching close event pops the exact instance. Idempotency: pushing CUTSCENE_START twice does not double-push; closing without a prior open is a no-op. STAGE_TEARDOWN and DISCONNECT clear cached handles and abort everything down to BOOT. Cohort 3 single-instance per event family is sufficient for current test coverage; Cohort 4 will move CUTSCENE handle storage into `struct player` for per-player cutscene state.
- Cohort 3 still does NOT wire any real callsite. `playerStartCutscene` / `playerEndCutscene` / `mainTickStage` / pause / vehicle continue to use their pre-existing mechanisms. Cohort 4 wires CUTSCENE_START / CUTSCENE_END at the cutscene start / end as part of the flash fix; later cohorts wire the rest.
- Tests: `tests/scene_pure.{c,h}` layered on top of `inputlayer_pure` so the dispatcher invariants are asserted without SDL or actionmap. `tests/test_scene_dispatch.cpp` adds 16 cases / ~70 assertions covering: BOOT_COMPLETE replaces BOOT with GAMEPLAY, STAGE_READY pushes GAMEPLAY when not already top, STAGE_READY idempotent when already on GAMEPLAY, STAGE_LOADING is advisory, CUTSCENE_START/END round-trip, duplicate CUTSCENE_START is idempotent, CUTSCENE_END without START is idempotent, PAUSE_OPEN/CLOSE round-trip, VEHICLE_BOARD for driver/turret variants, OBSERVER_ENTER/EXIT round-trip, STAGE_TEARDOWN unwinds everything to BOOT and clears cached handles, DISCONNECT same, nested layers preserve ancestry on pop, out-of-range event ids rejected, instrumentation tallies, sceneCurrentLayer mirrors inputLayerTopType.
- `CMakeLists.txt` SRC_TESTS: registered new files. `port/src/scene.c` auto-discovered into pd via GLOB_RECURSE.

### L.7 Cohort 4 shipped (2026-04-27) - the cutscene flash fix

**The user-visible deliverable: Mission 1 objective 2 cutscene flash is fixed.**

Two layers of defense:

1. **Belt: held-state flush at LAYER_CUTSCENE push.** `port/src/inputlayer.c` adds `onCutscenePush` / `onCutscenePop` hooks; the LayerDef `g_LayerCutscene` is wired with `on_push = onCutscenePush` (calls `imcCutsceneEnter()`, `actionmapFlushGameplayState()`, then `actionmapFlushActionSet(g_LayerCutscene.action_set, ...)`) and `on_pop = onCutscenePop` (calls `imcCutsceneExit()`). On every cutscene start the gameplay-only action state for all players clears, and the declared cutscene action set clears shared skip actions such as ACTION_USE / ACTION_MENU_ACCEPT, ACTION_CANCEL_USE, ACTION_PAUSE, and ACTION_SKIP_CUTSCENE.

2. **Braces: K.6 actionPressed (edge) skip detection.** `src/game/player.c:2944-2953` switched from `actionHeld` to `actionPressed` for every cutscene-skip check (USE / CANCEL_USE / FIRE_PRIMARY / FIRE_SECONDARY / FIRE_MODE / RELOAD / WEAPON_NEXT / PAUSE) plus the new dedicated `ACTION_SKIP_CUTSCENE`. A key held across the menu-accept-then-stage-load transition is now also cleared at layer push; if any stale state reaches the tick path, `pressed = 0` unless there is a fresh keydown during the cutscene. The legacy 30-frame gate at line 3080 stays as a third line of defense.

K.2 cutscene IMC: new `g_ImcCutscene` IMC at priority 4 (between gameplay 0 / mission 1 / combat-sim 1 below and vehicle 5 / forge 6+7 / menu 10+11 / debug 20 / text-input 30 above). Bound to `ACTION_SKIP_CUTSCENE` (Space + Gamepad A) and `ACTION_PAUSE` (Escape + Start). Activated/deactivated by the layer push/pop hooks. The IMC is registered in `s_AllImcs[]` so its bindings persist across save/load.

ACTION_SKIP_CUTSCENE is the new dedicated action at id 72, ACTION_COUNT advances from 73 to 73. (Cohort 4 numbering: id 71 = TEXT_PASTE from Cohort 1, id 72 = SKIP_CUTSCENE.) Exempt from `actionIsGameplayOnly` so it routes properly through the cutscene IMC.

Wiring: `src/game/player.c::playerStartCutscene2` fires `sceneFire(SCENE_EVENT_CUTSCENE_START, NULL)` immediately before the tickmode flip. `src/game/player.c::playerEndCutscene` fires `sceneFire(SCENE_EVENT_CUTSCENE_END, NULL)` at the end of the regular (non-autocut) branch. The autocut "play all" debug path is intentionally not wired in Cohort 4 since it has its own state machine and is not the bug repro path.

Tests: `tests/test_cutscene_layer.cpp` adds 6 cases / ~50 assertions covering: belt (gameplay-only state cleared at cutscene push, shared ACTION_USE / menu accept cleared by the cutscene action-set flush, unrelated menu actions preserved), braces (held-since-before-cutscene actionPressed is 0 throughout), the bug invariant (the Mission 1 obj 2 repro mirrored step by step), positive-path (fresh press DOES register as skip), round-trip cleanup for back-to-back cutscenes, STAGE_TEARDOWN unwinds cleanly while a cutscene is active. `tests/test_actionmap_flush.cpp` also locks the lower-level invariant that flushing one declared action set does not erase unrelated actions.

**Deferred to Cohorts 5-8 (per K.1's two-branch phasing):**

- **Per-player cutscene state migration.** `g_InCutscene`, `g_CutsceneSkipRequested`, `g_CutsceneAnimNum`, `g_CutsceneCurAnimFrame60`, `g_CutsceneCurTotalFrame60f`, and `g_Vars.in_cutscene` remain global in this cohort. The layer push/pop is global (single-instance per design Section G.1). Cohort 5+ moves these into `struct player.cutscene` and updates the ~30 read sites.
- **chr->cutscene_protect (invuln + invisible to enemies, K.3).** Field not yet added; damage and AI hostility checks remain global on `g_InCutscene`. Cohort 5+ adds the per-chr field, wires it set/cleared at cutscene push/pop, and updates the 5 canonical hostility/damage gates.
- **Net protocol bump (K.8).** `NET_PROTOCOL_VER` stays at 45. `SVC_CUTSCENE 0x53` retains its existing single-bool body. New `CLC_CUTSCENE_SKIP` not yet added. Cohort 5+ bumps to v46 with `player_mask` on `SVC_CUTSCENE` and the new client message.
- **Cohort 7 raw input migration sweep.** The ~75 raw `ImGui::IsKey*` callsites in `pdgui_menu_*.cpp` (full inventory in Section A.2) remain. The new ACTION_TEXT_PASTE bind is in place but no caller has migrated to it yet.

The flash fix lands in this branch (per K.1 Option B: "Cohorts 1-4 land flash fix first"). The architectural completion lands in the second branch.

### L.8 Shared accept/action-set flush follow-up (2026-04-28)

The first Cohort 4 implementation intentionally leaned on the K.6 edge-only check for shared ACTION_USE / ACTION_MENU_ACCEPT, leaving `actionmapFlushGameplayState()` as gameplay-only. The 2026-04-28 infrastructure slice closes that remaining held-state gap directly:

- `port/include/actionmap.h` / `port/src/actionmap.cpp` add public `actionmapFlushActionSet(const InputAction *actions, s32 action_count)`.
- `port/src/inputlayer.c` declares `s_CutsceneActionSet` and assigns it to `g_LayerCutscene.action_set`. The set includes ACTION_SKIP_CUTSCENE, ACTION_USE / ACTION_MENU_ACCEPT, ACTION_CANCEL_USE, ACTION_FIRE_PRIMARY, ACTION_FIRE_SECONDARY, ACTION_PAUSE, ACTION_FIRE_MODE, ACTION_RELOAD, and ACTION_WEAPON_NEXT.
- `onCutscenePush` now runs both flushes: gameplay-only state first, then the declared cutscene action set. This preserves the existing "clear all gameplay-only held state at cutscene entry" behavior while also clearing shared accept/cancel/skip state.
- Tests were tightened so the cutscene transition invariant now asserts held ACTION_USE is cleared, and the low-level action-set test asserts unrelated actions survive when they are not declared in the flushed set.

Playtest follow-up, same day: Mike held A through the Mission 1 objective 1 -> objective 2 transition. `Build/pd-client.log` shows the held menu accept at `[03:11.66]`, the objective 2 intro reaching frame 30 at `[03:12.26]`, release at `[03:19.05]`, and a later fresh A press mapping to `ACTION_SKIP_CUTSCENE` at `[03:19.65]`. That confirms the held-transition flash class is fixed.

The same log exposed B-267: after a deliberate skip at `[03:08.14]`, the endscreen opened without a matching cutscene layer pop, so the cutscene IMC stayed active beneath the endscreen and next mission until `[03:19.66]`. The next safe slice should not broaden raw input migration; it should make cutscene exit, skip-to-endscreen, and stage teardown unwind the cached cutscene layer handle consistently.

### L.9 B-267 cutscene lifecycle cleanup (2026-04-28)

The B-267 slice kept to the transition substrate and did not broaden menu/raw input migration.

- `port/src/main.c` now initializes `inputLayerInit()` and `sceneInit()` after action-map binds load, and shuts both down on exit. This gives production a real BOOT root instead of first using the stack from cutscene push.
- `port/src/pdmain.c::mainEndStage()` fires `SCENE_EVENT_CUTSCENE_END` before `endscreenPrepare()`. This covers the skip-to-endstage path that can bypass `playerEndCutscene()`.
- `port/src/pdmain.c` fires `SCENE_EVENT_STAGE_READY` when a gameplay stage is live and `SCENE_EVENT_STAGE_TEARDOWN` when leaving gameplay/system stages. Teardown remains the backstop for any active layer that missed a normal close.
- `inputLayerHandleDistanceFromTop()` is the small public handle-order helper scene cleanup needs without exposing `struct LayerHandle`.
- `scenePopTrackedLayer()` now handles nested close correctly: if the cached handle is below the top layer, scene aborts from top through that handle and clears all cached scene handles in the aborted range.
- Regression tests cover skip-to-endstage cleanup before the next mission intro, active-cutscene stage teardown, and nested tracked close with a menu layer above cutscene.

Verification: prescribed MSYS2/Ninja flow passed for `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`; 262 test cases / 10883 assertions passed. B-267 remains pending Mike playtest.

### L.10 B-267 propagation audit (2026-04-28)

Mike asked whether the B-267 fix was applied everywhere it needed to be. The audit found the first lifecycle cleanup was central but incomplete: the solo/local endstage and stage ready/teardown roots were covered, but network cutscene sync, disconnect, and tickmode exit needed to feed the same scene/layer authority.

Additional wiring:

- `port/src/net/netmsg.c::netmsgSvcCutsceneRead()` fires `SCENE_EVENT_CUTSCENE_START` / `SCENE_EVENT_CUTSCENE_END` on client builds after reading the server's `SVC_CUTSCENE active` bit.
- `port/src/net/net.c::netDisconnect()` fires `SCENE_EVENT_DISCONNECT` before menu-pool teardown and in-game return-to-title cleanup.
- `src/game/player.c::playerSetTickMode()` records the previous tickmode and fires `SCENE_EVENT_CUTSCENE_END` whenever a transition leaves `TICKMODE_CUTSCENE`.
- `tests/test_cutscene_layer.cpp` now has a static lifecycle wiring test that checks the active roots and also verifies `src/lib/main.c` is not part of the CMake client build path. The active PC lifecycle is `port/src/pdmain.c`; stale legacy duplicates should not receive parallel fixes unless they re-enter the build.

Verification: `git diff --check` passed; prescribed MSYS2/Ninja flow passed for `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`; 267 test cases / 10926 assertions passed. B-267 remains pending Mike playtest.

### L.11 Per-player cutscene state migration (2026-04-28)

The next infrastructure slice moved cutscene runtime state behind `struct player.cutscene` accessors without changing wire semantics yet.

- Added `struct playercutscenestate` to `types.h` and embedded it in `struct player`.
- Added `playerSet*`, `playerCurrent*`, `playerAny*`, and reset/sync accessors in `player.c` / `player.h` for the active flag, in-progress flag, skip-requested flag, cutscene anim id, current anim frame, and total cutscene frame time.
- Kept the legacy globals as compatibility shims synced from the current player's cutscene state. This preserves existing consumers while migrated call sites move to the accessor API.
- Migrated active gameplay/render/audio/script call sites in `player.c`, `lv.c`, `chraction.c`, `chraicommands.c`, `chr.c`, `hudmsg.c`, `vi.c`, `model.c`, `prop.c`, `propobj.c`, `mplayer.c`, `menu.c`, `sky.c`, `bondgun.c`, and `nbomb.c`.
- Network `SVC_CUTSCENE` client handling now sets player 0 through the accessor and still fires scene events. The server-only stub/global path remains a compatibility bridge until the v46 player-mask slice.
- Transitional direct globals remain only in declarations, `playerSyncCutsceneGlobalsToCurrent()`, `varsinit.c`, `server_stubs.c`, the PD_SERVER branch in `netmsg.c`, and the `USINGDEVICE` macro. Those are tracked for the shim-retirement step.
- Added static pd-tests that keep migrated gameplay paths off the old globals.

Verification: `git diff --check` passed for the input-state migration files; prescribed MSYS2/Ninja flow passed for `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`; 281 test cases / 15294 assertions passed.

### L.12 Cutscene protection gates (2026-04-28)

The next slice implemented K.3's protection flag without starting the v46 network mask work yet.

- Added `chr->cutscene_protect` to `struct chrdata` and initialized it in `chrInit()`.
- Added `playerRefreshCutsceneProtect()` so per-player cutscene state changes set the flag on all allocated player chrs while any player is in a cutscene. This is the pre-v46 behavior; the player-mask slice will narrow the protected set to the mask.
- `chrDamage()` now ignores protected targets and logs `CUTSCENE.DAMAGE.IGNORED chr=<n> attacker=<n>`.
- `chrCompareTeams(..., COMPARE_ENEMIES)` no longer classifies protected targets as enemies, preventing canonical enemy target selection from choosing them.
- `chrHasLosToChr()` and `botIsTargetInvisible()` treat protected targets as invisible, covering the direct line-of-sight and bot visibility gates.
- Static pd-tests now guard the protection field, refresh path, damage gate, enemy-classification gate, LOS gate, and bot invisibility gate.

Verification: `git diff --check` passed for the protection slice; prescribed MSYS2/Ninja flow passed for `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`; 286 test cases / 16727 assertions passed.

### L.13 Cutscene network semantics (2026-04-28)

The next slice completed K.8 on the existing v46 protocol. v46 was already claimed by S507's mandatory mod-transfer digest, so this slice appended the cutscene semantics to v46 instead of bumping again.

- `SVC_CUTSCENE` now carries `active` plus `player_mask`; clients set per-player cutscene state from the mask and fire cutscene scene events from the server-authoritative state.
- `CLC_CUTSCENE_SKIP` (0x17) lets clients request a cutscene skip after the 30-frame gate. The server binds the request to `srccl->playernum` and ignores untrusted player numbers from the payload.
- Net clients no longer locally end a cutscene on skip input. They send the skip request and wait for the host/server path to end the cutscene.
- Cutscene protection now narrows to `playerInCutscene(i)` for each player chr instead of protecting every player chr while any player is in cutscene.
- AI script skip checks now use any server-validated cutscene skip request so a remote client's accepted skip can drive the existing cutscene branch.
- Static pd-tests guard the v46 message constants, SVC payload shape, CLC dispatch path, server authority check, active-mask handling, protection narrowing, and client skip request write.

Verification: isolated build session `ix46` built `pd` and `pd-server`; isolated `pd-tests.exe` passed 289 test cases / 16786 assertions.

### L.14 Vehicle and observer layer wiring (2026-04-28)

The next slice completed Cohort 5's first production wiring without starting a vehicle turret layer or broad raw-input migration.

- `LAYER_VEHICLE_DRIVER` now declares its action set and owns transition flushing for accelerate, brake, steer left/right, exit, and pause. Push/pop/abort callbacks own `imcVehicleMount()` / `imcVehicleDismount()`.
- `bbikeInit()` and `bbikeExit()` now fire `SCENE_EVENT_VEHICLE_BOARD` / `SCENE_EVENT_VEHICLE_DISMOUNT` with a `SceneVehiclePayload` instead of directly mounting or dismounting the vehicle IMC.
- `LAYER_OBSERVER` now declares the observer/Forge action set and flushes it on push/pop/abort. Pop/abort also deactivate `g_ImcForge` and `g_ImcForgeSession` as a cleanup guard.
- Forge session and freefly transitions now enter the observer layer through scene events; Forge inactive exit closes the observer layer while preserving the existing Forge IMC activation behavior.
- Spectator live and theater sessions now enter and exit the observer layer through scene events.
- The scene manager tracks the current observer source so a spectator stop cannot pop a Forge-owned observer layer, and a Forge inactive transition cannot pop a spectator-owned observer layer.
- Static pd-tests guard the vehicle and observer action sets, callbacks, bike scene-event wiring, Forge observer helpers, spectator observer helpers, and observer source guard.

Verification: isolated build session `ix46` built `pd` and `pd-server`; isolated `pd-tests.exe` passed 291 test cases / 16843 assertions.

### L.15 Main-menu subview pool ownership (2026-04-28)

The first menu graph migration slice implemented K.7's safe substrate for inline main-menu subviews without replacing the whole renderer or starting the broader priority-node graph in one jump.

- Added dedicated pure-ImGui menu-pool identities for main-menu Solo, Settings, Modding, Online, and Stats subviews. The existing `MENU_TYPE_GRID_SUBMENU` remains the Grid subview identity.
- Added `pdguiMainMenuViewPoolType()` and `pdguiMainMenuSetView()` so every `s_MenuView` change acquires/releases exactly one subview pool slot.
- Added render-time subview sync: if a bulk teardown released pool state while the main-menu renderer retained its local subview, the renderer reacquires the matching subview slot.
- Removed the old Grid-only pool transition branch so Grid follows the same ownership path as every other inline subview.
- Static pd-tests guard the subview identities, mapping helper, acquire/release calls, render-sync call, and the rule that raw `s_MenuView` assignments live only in the helper and declaration.

Verification: isolated build session `ix46` built `pd` and `pd-server`; isolated `pd-tests.exe` passed 293 test cases / 16881 assertions.

### L.16 Menu graph edge substrate (2026-04-28)

The next menu graph slice added the graph descriptor module and migrated the first safe direct dialog pushes.

- Added `port/include/menugraph.h` and `port/src/menugraph.c`.
- The graph now declares priority-node descriptors for main menu, main-menu subviews, solo mission, room, solo/MP endscreen, pause variants, social lobby, network, agent select, and warning modal.
- Added edge lookup and destination-kind name APIs for tests and diagnostics.
- Added `menuGraphFirePushDialog()`, which validates that a dialog-push edge exists and that the supplied legacy dialogdef maps to the declared menu-pool destination before calling `menuPushDialog()`.
- Migrated main-menu Change Agent and Cheats pushes through `menuGraphFirePushDialog()`.
- Static pd-tests guard graph substrate presence, priority-node coverage, destination validation, and the first migrated direct push sites.

Verification: isolated build session `ix46` built `pd` and `pd-server`; isolated `pd-tests.exe` passed 294 test cases / 16925 assertions.

### L.17 Network menu graph migration (2026-04-28)

The next menu graph slice migrated the Network priority node and the duplicate main-menu Online connect path.

- Added `menuGraphFireNetworkOp()` and `menuGraphFirePop()`.
- Added `MENU_TYPE_NETWORK_JOINING` and registered `g_NetJoiningDialog` so the Joining dialog has a typed graph and menu-pool destination.
- Network menu Stop Hosting, pre-host disconnect, Host, host-success pop, Join, Joining dialog push, and Back now route through graph helpers.
- The main-menu Online subview's direct connect and recent-server connect paths now route through `MENU_TYPE_MAIN_ONLINE_VIEW` graph network edges.
- Static pd-tests guard the Network menu and main-menu Online renderers against reintroducing direct `netStart*`, `netDisconnect`, `menuPushDialog(&g_NetJoiningDialog)`, or `menuPopDialog()` calls in those paths.

Verification: isolated build session `ix46` built `pd` and `pd-server`; isolated `pd-tests.exe` passed 296 test cases / 16953 assertions.

### L.18 MP endscreen disconnect graph migration (2026-04-28)

The next menu graph slice migrated the smallest MP endscreen direct transition.

- MP endscreen Disconnect confirmation now fires the `MENU_TYPE_ENDSCREEN_MP` `disconnect` graph network edge before returning to the existing `pdguiEndscreenExitToMainMenu()` path.
- The graph callback preserves the existing `netDisconnect()` behavior and returns success to the graph diagnostics.
- Static pd-tests guard the MP endscreen renderer against direct `netDisconnect()` reintroduction.

Verification: isolated build session `ix46` built `pd` and `pd-server`; isolated `pd-tests.exe` passed 298 test cases / 16970 assertions.

### L.19 Agent Select graph migration (2026-04-28)

The next menu graph slice migrated the Agent Select priority node's simple dialog transitions.

- Registered `g_FilemgrEnterNameMenuDialog` as `MENU_TYPE_AGENT_CREATE`.
- Agent Select New Agent now fires the `create` graph dialog-push edge before opening the enter-name dialog.
- Agent Select Back now fires the `back` graph pop edge.
- Static pd-tests guard Agent Select against direct `menuPushDialog(&g_FilemgrEnterNameMenuDialog)` and `menuPopDialog()` reintroduction in the renderer.

Verification: isolated build session `ix46` built `pd` and `pd-server`; isolated `pd-tests.exe` passed 299 test cases / 16981 assertions.

### L.20 MP pause graph migration (2026-04-28)

The next menu graph slice migrated the MP pause priority node's simple dialog transitions.

- MP pause Resume/Back now fires the `MENU_TYPE_MP_PAUSE` `resume` graph pop edge.
- Added a `MENU_TYPE_MP_PAUSE` `end_game` edge targeting `MENU_TYPE_WARNING_MODAL`.
- MP pause End Game now fires the graph dialog-push edge before opening `g_MpEndGameMenuDialog`.
- Static pd-tests guard the MP pause close and End Game helpers against direct `menuPopDialog()` or `menuPushDialog(target)` reintroduction.

Verification: isolated build session `ix46` built `pd` and `pd-server`; isolated `pd-tests.exe` passed 300 test cases / 16993 assertions.

### L.21 Solo pause graph migration, first slice (2026-04-28)

The next menu graph slice migrated the safe solo in-mission pause transitions without changing the legacy sibling-stack behavior.

- `MENU_TYPE_SOLO_MISSION_PAUSE` now has its own graph edge set instead of reusing the generic pause edge set.
- Solo pause Resume/Back now fires the `MENU_TYPE_SOLO_MISSION_PAUSE` `resume` graph pop edge.
- Solo pause Abort now fires the `MENU_TYPE_SOLO_MISSION_PAUSE` `abort` warning-modal push edge.
- Static pd-tests guard the solo pause renderer against reintroducing direct `menuPopDialog()` for Resume/Back or direct `menuPushDialog(&g_MissionAbortMenuDialog)` for Abort.
- Inventory and Settings remain direct for the next slice because they are legacy next-sibling dialogs, not ordinary child pushes. The correct follow-up is a small graph helper for sibling transitions, not a broad menu rewrite.

Verification: isolated build session `ix46` was reused. The session wrapper was invoked first but stalled in client compile with idle CMake/Ninja children after the command timeout; direct isolated Ninja in `.claude/session-builds/ix46` then built `pd`, `pd-server`, and `pd-tests`, and isolated `pd-tests.exe` passed 303 test cases / 17031 assertions.

### L.22 Solo pause sibling graph migration (2026-04-28)

The follow-up solo pause slice handled Inventory and Settings correctly as legacy sibling transitions instead of ordinary child pushes.

- Added `menuSwitchToDialog(struct menudialogdef *dialogdef)` in `src/game/menu.c` / `src/include/game/menu.h` so callers can switch directly to an already-open sibling by dialogdef with one transition.
- Added `MENU_GRAPH_DEST_SWITCH_SIBLING` and `menuGraphFireSwitchSibling()`, with the same graph-edge and menu-pool target validation pattern as dialog pushes.
- Added `MENU_TYPE_SOLO_INVENTORY` and registered `g_SoloMissionInventoryMenuDialog`; also registered `g_SoloMissionOptionsMenuDialog` as `MENU_TYPE_SOLO_OPTIONS`.
- Added solo Inventory and solo Options graph nodes with Back edges to `MENU_TYPE_SOLO_MISSION_PAUSE`.
- Solo pause Inventory and Settings buttons now fire sibling graph edges; Inventory and Options Back paths return to solo pause through sibling graph edges instead of popping the whole pause layer.
- Static pd-tests guard the sibling helper, destination kind, menu-pool registrations, graph edges, renderer calls, and removal of the direct Inventory/Settings pushes.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, and isolated `pd-tests.exe` passed 303 test cases / 17061 assertions.

### L.23 Social Lobby graph migration (2026-04-28)

The next menu graph slice migrated the Social Lobby node's declared network operations.

- Social Lobby Create Room now fires the `MENU_TYPE_SOCIAL_LOBBY` `create_room` graph network edge.
- Social Lobby Disconnect confirmation now fires the `MENU_TYPE_SOCIAL_LOBBY` `disconnect` graph network edge.
- The graph callbacks preserve the existing create-room packet send and `netDisconnect()` behavior.
- Static pd-tests guard the Social Lobby renderer against reintroducing direct `netmsgClcRoomCreateWrite()` or direct `netDisconnect()` in the render path.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 304 test cases / 17070 assertions.

### L.24 Warning modal graph pop migration (2026-04-28)

The next menu graph slice migrated the Warning Modal node's declared confirm and cancel exits.

- Generic typed dialog fallback OK now fires `MENU_TYPE_WARNING_MODAL` `confirm`.
- Generic typed dialog Escape now fires `MENU_TYPE_WARNING_MODAL` `cancel`.
- MP End Game popup external dismiss and Cancel now fire `cancel`; End Match now fires `confirm` after preserving the legacy selectable handler call.
- PC filemgr placeholder OK and Escape now fire the warning-modal `confirm` and `cancel` graph pop edges.
- Static pd-tests guard the warning renderer against direct `menuPopDialog()` reintroduction.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 305 test cases / 17083 assertions.

### L.25 Room setup subdialog graph migration (2026-04-28)

The next menu graph slice migrated Room setup child pushes that do not start or leave the room.

- Added `team_setup` and `select_music` push edges to `MENU_TYPE_ROOM`.
- Room Team Setup now fires `MENU_TYPE_ROOM` `team_setup` and validates the destination as `MENU_TYPE_MP_TEAM_SETUP`.
- Room Select Music now fires `MENU_TYPE_ROOM` `select_music` and validates the destination as `MENU_TYPE_MP_TUNES`.
- Start Match and Leave Room remain direct for a later scene/network slice.
- Static pd-tests guard the Room renderer against direct Team Setup / Select Music `menuPushDialog()` reintroduction.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 306 test cases / 17092 assertions.

### L.26 Main-menu inline subview graph firing (2026-04-28)

The next menu graph slice connected already-declared main-menu inline edges to the subview pool helper.

- Added `pdguiMainMenuFireSubviewEdge()` to validate `MENU_TYPE_MAIN_MENU` graph edges before changing inline views.
- Solo Play, Online Play, Settings, Mods, Stats, and The Grid now fire their declared graph edges before delegating to `pdguiMainMenuSetView()`.
- The existing subview pool ownership behavior is unchanged; the graph helper validates the destination menu-pool type first.
- Static pd-tests guard the edge lookup, destination validation, and absence of direct top-level `pdguiMainMenuSetView(1..6, "open-*")` calls.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 306 test cases / 17103 assertions.

### L.27 Main-menu inline back-edge graph firing (2026-04-28)

The next menu graph slice connected already-declared inline subview `back` edges to top-level view return.

- Added `pdguiMainMenuFireSubviewBackEdge()` to validate the current inline subview's `back` edge before returning to view 0.
- Shared subview close, Grid Back, Modding Back, and Stats auto-close now fire the validated back edge before delegating to `pdguiMainMenuSetView(0, ...)`.
- External reset, initial menu open, and Grid Enter remain direct because they are not user back edges.
- Static pd-tests guard the back-edge lookup, destination kind validation, and removal of direct `pdguiMainMenuSetView(0, "close-subview" / "grid-back" / "modding-back" / "stats-closed")` calls.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 306 test cases / 17114 assertions.

### L.28 Scene-operation graph helper and pause End Match (2026-04-28)

The next slice added the smallest helper needed for scene/stage-like graph edges without replacing stage logic.

- Added `menuGraphFireSceneOp()` to validate `MENU_GRAPH_DEST_SCENE_EVENT` edges, log the declared scene event payload, run a behavior-preserving callback, and log the result.
- Combat-sim pause End Match now fires `MENU_TYPE_PAUSE_MENU` `end_mission` through the scene-op helper.
- The actual previous behavior stays inside `pauseGraphEndMission()`: set the player-aborted flag, then call `mainEndStage()`.
- Removed unused pause-menu forward declarations for direct stage/network helpers that the file no longer calls.
- Static pd-tests guard the helper and keep the pause renderer from directly calling `pdguiPauseSetPlayerAborted()` or `mainEndStage()`.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 307 test cases / 17128 assertions.

### L.29 The Grid enter scene edge (2026-04-28)

The next scene-op slice migrated the already-declared Grid enter edge.

- Added `pdguiMainMenuGraphEnterGrid()` as a behavior-preserving callback around `gridCommitEnter()`.
- The Grid Enter button now fires `MENU_TYPE_GRID_SUBMENU` `enter` through `menuGraphFireSceneOp()`.
- Success and failure behavior is unchanged: success plays the open-dialog sound and returns the main menu to view 0; failure stays on the Grid submenu and plays cancel.
- Static pd-tests guard that `renderGridSubmenu()` uses the scene-op helper and no longer calls `gridCommitEnter()` directly.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 307 test cases / 17133 assertions.

### L.30 Room Start Match scene edge (2026-04-28)

The next scene-op slice migrated the already-declared Room start edge.

- Extracted the existing Room Start Match switch into `roomGraphStartMatch()`.
- The callback preserves Combat Sim solo start, Combat Sim online start, Campaign start, and Counter-Op start behavior.
- The Start Match button now fires `MENU_TYPE_ROOM` `start_match` through `menuGraphFireSceneOp()`.
- Static pd-tests guard that `pdguiRoomScreenRender()` fires the scene-op helper and no longer directly calls `matchStart()` / `netLobbyRequestStart*()`.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 308 test cases / 17144 assertions.

### L.31 Room Leave graph edge (2026-04-28)

The next Room slice migrated the remaining declared Room edge.

- Changed `MENU_TYPE_ROOM` `leave_room` to a graph operation edge so the renderer can preserve solo and online leave behavior behind one validated edge.
- Added `roomGraphLeaveRoom()` to reset room setup state, release `MENU_TYPE_ROOM`, close the solo room path, send client leave packets, run listen-host local leave, and return online clients to the social lobby as appropriate.
- The leave-confirm modal now fires `MENU_TYPE_ROOM` `leave_room` through `menuGraphFireNetworkOp()`.
- Static pd-tests guard that `pdguiRoomScreenRender()` no longer directly sends leave packets, calls listen-host leave, or returns to the lobby.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built the affected targets and isolated `pd-tests.exe` passed 309 test cases / 17158 assertions.

### L.32 Endscreen scene graph edges (2026-04-28)

The next priority-node slice completed the remaining endscreen exits without changing the bridge functions that actually perform stage/menu teardown.

- Solo endscreen Continue, Retry, and Main Menu now fire `MENU_TYPE_ENDSCREEN_SOLO` scene graph edges.
- `main_menu` is now a scene edge with `SCENE_EVENT_STAGE_TEARDOWN` instead of a bare graph pop-root, because the real behavior must still go through `pdguiEndscreenExitToMainMenu()`.
- MP endscreen Return to Room and Play Again now share the `MENU_TYPE_ENDSCREEN_MP` `continue` scene edge, with the existing networked/local branch preserved inside `endscreenGraphMpContinue()`.
- MP Quit now fires a scene graph edge, and MP Disconnect now owns both `netDisconnect()` and the existing endscreen exit path inside the graph-dispatched callback.
- Static pd-tests guard solo/MP endscreen renderers against direct `pdguiEndscreen*`, `netDisconnect()`, `pdguiSetInRoom(1)`, or `pdguiSoloRoomReturn()` calls returning to the render paths.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 310 test cases / 17193 assertions.

### L.33 Solo Mission start/back/restart graph edges (2026-04-28)

The next priority-node slice migrated already-declared Solo Mission edges without changing the surrounding difficulty, PD Mode, or raw ImGui key handling.

- Added `soloMissionGraphStart()` to preserve the existing `menuhandlerAcceptMission(MENUOP_SET, ...)` bridge and the post-start ImGui-menu context pop.
- Added `soloMissionGraphRestart()` to preserve the existing catalog-backed restart stage resolution before `mainChangeToStage()`.
- Mission Select list-level Back now fires `MENU_TYPE_SOLO_MISSION` `back` through `menuGraphFirePop()`.
- Mission Select Start and Accept Mission Accept now fire `MENU_TYPE_SOLO_MISSION` `start` through `menuGraphFireSceneOp()`.
- Accept Mission Decline now fires the same `back` edge instead of directly popping the dialog.
- Solo pause Restart confirmation now fires `MENU_TYPE_SOLO_MISSION_PAUSE` `restart` through `menuGraphFireSceneOp()`.
- Static pd-tests guard those render paths against direct `menuhandlerAcceptMission()`, `mainChangeToStage()`, and `menuPopDialog()` reintroduction where the graph now owns the edge.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 311 test cases / 17218 assertions.

### L.34 Main-menu Solo view push-op graph edges (2026-04-28)

The next menu graph slice handled push edges whose destination open is not a simple `menuPushDialog()` call.

- Added `MenuGraphPushOpFn` and `menuGraphFirePushOp()` for `MENU_GRAPH_DEST_PUSH_MENU` edges that must preserve an existing state-setting handler.
- Main-menu Solo Missions now fires `MENU_TYPE_MAIN_SOLO_VIEW` `solo_missions` through the push-op helper. The callback preserves `pdguiSoloMissionReset()` and `menuhandlerMainMenuSoloMissions(MENUOP_SET, ...)`.
- Main-menu Combat Simulator now fires `MENU_TYPE_MAIN_SOLO_VIEW` `combat_simulator` through the push-op helper. The callback preserves `menuhandlerMainMenuCombatSimulator(MENUOP_SET, ...)`, including the legacy setup and `pdguiSoloRoomOpen()` path inside that handler.
- Static pd-tests guard the push-op helper and the main-menu Solo view render path.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 312 test cases / 17238 assertions.

### L.35 Main-menu Modding hub graph edge (2026-04-28)

The next main-menu slice reused `menuGraphFirePushOp()` for the Modding hub overlay.

- Added `pdguiMainMenuGraphOpenModdingHub()` as a behavior-preserving callback around `pdguiModdingHubShow()`.
- The top-level Mods shortcut now opens the Modding subview, then fires the `MENU_TYPE_MAIN_MODDING_VIEW` `open_hub` edge through the push-op helper.
- The Modding subview's closed-hub `Open Modding Hub` button now fires the same graph edge instead of calling the hub opener directly.
- Static pd-tests guard the declared `open_hub` edge and the render path.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 313 test cases / 17246 assertions.

### L.36 Main-menu Stats panel graph edge (2026-04-28)

The next main-menu slice added the missing explicit graph edge for the Stats panel open path.

- Added `MENU_TYPE_MAIN_STATS_VIEW` `open_panel` as a push edge targeting `MENU_TYPE_STATS_PANEL`.
- Added `pdguiMainMenuGraphOpenStatsPanel()` as a behavior-preserving callback around `pdguiMenuStatsShow()`.
- The top-level Stats shortcut now opens the Stats subview, then fires `open_panel` through `menuGraphFirePushOp()`.
- Static pd-tests guard the edge and prevent the render path from calling `pdguiMenuStatsShow()` directly.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 314 test cases / 17254 assertions.

### L.37 Main-menu Quit process graph edge (2026-04-28)

The next main-menu slice migrated the existing Quit graph edge.

- Added `MenuGraphProcessOpFn` and `menuGraphFireProcessOp()` for `MENU_GRAPH_DEST_PROCESS_EXIT` edges.
- Added `pdguiMainMenuGraphQuit()` as a behavior-preserving callback that posts the existing `SDL_QUIT` event.
- The Quit confirmation modal now fires `MENU_TYPE_MAIN_MENU` `quit` through the process-op helper instead of posting the event directly from the renderer.
- Static pd-tests guard the process-op helper and main-menu Quit render path.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 315 test cases / 17267 assertions.

### L.38 Main-menu Close pop graph edge (2026-04-28)

The next main-menu slice migrated the top-level close path while preserving its extra game-state restoration.

- Added `MenuGraphPopOpFn` and `menuGraphFirePopOp()` for pop edges whose behavior must run a state-restoring callback.
- Added `pdguiMainMenuGraphClose()` to preserve the existing close behavior: unpause the level, call `playerUnpause()`, restore player control, pop the legacy dialog, and defensively pop `g_CtxImGuiMenu` if it survived.
- The top-level Escape/B/title-close path now fires `MENU_TYPE_MAIN_MENU` `close` through the pop-op helper.
- Static pd-tests guard the helper and render path.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 316 test cases / 17285 assertions.

### L.39 Agent Select load local graph edge (2026-04-28)

The next graph audit slice corrected Agent Select `load` semantics.

- Added `MENU_GRAPH_DEST_LOCAL_OP`, `MenuGraphLocalOpFn`, and `menuGraphFireLocalOp()` for graph edges that mutate local menu/game state without pushing, popping, or changing scenes.
- Changed `MENU_TYPE_AGENT_SELECT` `load` from a pop edge to a local-op edge named `load_agent`.
- Added `agentSelectGraphLoad()` to preserve the existing load behavior: optional pool release for the Enter path, `g_GameFileGuid` update, `filemgrSaveOrLoad()`, and `prefsLoadForFile()`.
- Routed the Enter/selected-agent and mouse/selectable load paths through the local-op edge. Auto-load and copy-confirm load paths remain direct because they are not the user `load` graph edge.
- Static pd-tests guard the local-op helper and Agent Select load/create/back graph usage.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 316 test cases / 17299 assertions.

### L.40 Raw menu-action helper and first priority exits (2026-04-28)

The next recursive input slice started raw action input migration without sweeping every ImGui key site at once.

- Added `pdguiMenuActionPressed()`, `pdguiMenuActionHeld()`, `pdguiMenuActionRepeat()`, and named accept/cancel/nav helpers in `pdgui_nav`.
- Added Space and keypad Enter to menu/pause/debug `ACTION_USE` defaults so existing confirm-modal shortcuts remain available through action-map authority.
- Migrated `pdguiActionBarButton()` and `pdguiRenderConfirmModal()` off raw Enter/Space/Escape polling.
- Migrated graph-owned endscreen cancel paths, Network menu Back, Social Lobby disconnect confirm open, MP pause Back helper, and Bot Setup Back helper off raw Escape polling.
- Added static pd-tests guarding the helper API, the menu accept bindings, and the confirm modal/action bar migration.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 317 test cases / 17319 assertions.

### L.41 Priority confirm and exit raw-input migration (2026-04-28)

The next raw-input slice finished the high-risk confirm/cancel sites that are already graph-owned or shared modal surfaces.

- Warning-modal typed dialogs, MP End Game, and PC file-manager placeholder now use `pdguiMenuAcceptPressed()` / `pdguiMenuCancelPressed()` instead of raw Enter, Space, keypad Enter, or Escape polling.
- Combat-sim pause End Match, Debug Shortcuts close, and the parent pause close path now use the same menu-action helpers.
- Room Leave arm, scenario delete confirm, and Leave Room confirm now use menu-action helpers while preserving the existing debounce and destructive-action confirmation behavior.
- Static pd-tests now guard these priority sites against raw menu shortcut polling returning.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 317 test cases / 17350 assertions.

### L.42 Priority navigation and tab raw-input migration (2026-04-28)

The next raw-input slice moved priority list navigation and tab switching behind action-map authority.

- Added PageUp/PageDown as keyboard defaults for `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` in menu and pause contexts. LB/RB remain the gamepad defaults.
- Agent Select accept/cancel/list up/down now use `pdgui_nav` helpers.
- Main-menu Settings tab cycle and Cinema close/select/up/down now use `pdgui_nav` helpers.
- Room tab cycle and Stats tab/close now use `pdgui_nav` helpers.
- Two raw PageUp/PageDown reads remain in `renderMainMenu()` as a transitional ImGui queue drain outside Settings. Remove them when the remaining tab sites and backend PageUp injection are retired together.
- Static pd-tests now guard the migrated priority navigation and tab sites.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 318 test cases / 17423 assertions.

### L.43 Simple legacy menu back/nav raw-input migration (2026-04-28)

The next raw-input slice handled simple legacy menu replacement screens whose remaining raw reads were Back, Done, or list up/down.

- Countdown cancel and the shared file browser parent navigation now use `pdguiMenuCancelPressed()`.
- Agent Create cancel, Challenges list/back, Control Diagram back/up/down, MP Advanced back, MP Settings back/Done, MP Setup back, Player Config back, and Team Setup Done now use `pdgui_nav` helpers.
- Static pd-tests now guard these files against raw menu shortcut/navigation polling returning.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 319 test cases / 17543 assertions.

### L.44 Cheats and modding panel raw-input migration (2026-04-28)

The next raw-input slice migrated cheats/modding panel shortcuts that behave like normal menu actions.

- Cheats hub close, tab cycling, warning close, and Unlock Everything confirm/cancel now use `pdgui_nav` helpers.
- Mod Manager tab cycling and close now use `pdgui_nav` helpers.
- Modding Hub tool cycling and close now use `pdgui_nav` helpers.
- Static pd-tests now guard these files against raw menu shortcut/navigation polling returning.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 320 test cases / 17579 assertions.

### L.45 Training menu raw-input migration (2026-04-28)

The next raw-input slice migrated Training menu shortcuts that behave like normal menu actions.

- `pdgui_menu_training.cpp` now uses `pdgui_nav` helpers for Back, Continue, weapon/list up/down, and firing range confirm shortcuts.
- Static pd-tests now guard Training against raw menu shortcut/navigation polling returning.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 321 test cases / 17591 assertions.

### L.46 Solo Mission raw-input migration (2026-04-28)

The next raw-input slice migrated the remaining Solo Mission menu-owned shortcuts.

- Mission select, difficulty selection, co-op/anti difficulty, co-op/anti options, briefing, inventory, accept mission, solo pause, abort mission, and solo options now use `pdgui_nav` helpers for Back, Accept, directional navigation, and tab switching.
- Added Q/E as additional menu/pause defaults for `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` so Solo Options keeps its keyboard tab shortcuts behind action-map authority.
- Static pd-tests now guard Solo Mission against raw menu shortcut/navigation polling returning.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 322 test cases / 17647 assertions.

### L.47 Secondary menu command raw-input migration (2026-04-28)

The next raw-input slice migrated secondary menu commands that were still plain raw-key shortcuts, while preserving their existing menu behavior.

- Added `ACTION_MENU_SECONDARY`, `ACTION_MENU_TERTIARY`, and `ACTION_MENU_DELETE` to the action map, with C / gamepad X, D / gamepad Y, and Delete defaults for menu and pause contexts.
- Added named `pdgui_nav` helpers for secondary, tertiary, delete, and text paste actions.
- Agent Select secondary copy, delete, and directory-open commands now use action-map helpers instead of raw C, Delete, and D reads.
- Room bot-row secondary and tertiary commands now use action-map helpers instead of raw gamepad face-button reads.
- MP Settings preview commands now use the secondary action helper instead of raw gamepad face-button reads.
- Static pd-tests guard the secondary command defaults, helper API, and migrated command sites.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 323 test cases / 17687 assertions.

### L.48 Spectator observer raw-input migration (2026-04-28)

The next raw-input slice moved spectator live/theater controls behind a dedicated observer action map while preserving Forge observer behavior.

- Added `g_ImcObserver` and observer actions for subset/member navigation, camera toggle, freefly, stop, ascend, and descend.
- `LAYER_OBSERVER` activates `g_ImcObserver` only for `SCENE_OBSERVER_SOURCE_SPECTATOR`; Forge observer entry continues to use the existing Forge IMCs.
- Scene observer payload storage is now stable inside `scene.c` so layer callbacks do not retain a caller stack pointer.
- `pdgui_spectator.cpp` no longer polls raw ImGui keys for observer controls. Freefly uses the gameplay move axis for lateral/forward movement and observer ascend/descend actions for vertical movement.
- Glyph lookup and the Controls UI now include observer bindings.
- Static pd-tests guard the observer action set, source-specific activation, stable scene payload, spectator raw-key migration, and observer binding visibility.

Verification: the isolated build wrapper was invoked for session `ix46` but stalled during client compile and left only a dead lock. Direct isolated Ninja in `.claude/session-builds/ix46` then built `pd`, `pd-server`, and `pd-tests`, and isolated `pd-tests.exe` passed 324 test cases / 17729 assertions.

### L.49 Social voice PTT raw-input migration (2026-04-28)

The next raw-input slice moved voice push-to-talk behind action-map authority.

- Added `ACTION_VOICE_PTT` with V as the default binding.
- Bound `ACTION_VOICE_PTT` in gameplay, cutscene, vehicle, observer, Forge session, menu, pause-menu, and debug overlay IMCs so it preserves the old raw hotkey's broad availability.
- Kept the existing `ImGui::GetIO().WantCaptureKeyboard` guard at the read site so typing in UI fields does not start voice transmission.
- `pdgui_friends.cpp` now calls `actionPressed/Released(0, ACTION_VOICE_PTT)` instead of raw `ImGuiKey_V` polling.
- Controls UI exposes Voice Push-to-Talk under System Hotkeys.
- Static pd-tests guard the action id, binding, shared-action classification, raw V polling removal, and Controls UI visibility.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 327 test cases / 17748 assertions.

### L.50 Editor/tool hotkey raw-input migration (2026-04-28)

The next raw-input slice moved editor/tool command shortcuts behind action-map authority without changing text-entry or geometry behavior.

- Added synthetic chord VKs for Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, and Ctrl+S. Chord keyup releases use keydown-tracked synthetic VKs so the action edge is not lost if modifiers release first.
- Added Forge placement/bot actions and Skin Editor brush/tool/grid/UV/undo/redo/save actions.
- Bound Forge session commands in `g_ImcForgeSession`, placement/sidebar commands in `g_ImcForge`, and Skin Editor commands in `g_ImcMenu`.
- Migrated Forge HUD bot commands, Forge placement cancel, Forge Ctrl+Tab sidebar cycling, and Skin Editor shortcuts from raw ImGui polling to action-map reads.
- Exposed the new Forge and Skin Editor actions in the Controls UI.
- Static pd-tests guard action ids, binding visibility, synthetic chord declarations, raw polling removal in Forge HUD/Forge Editor/Skin Editor, and Controls UI rows.
- First-party raw-key audit now leaves only the documented main-menu PageUp/PageDown queue drain plus comments; third-party ImGui internals are ignored.

Verification: direct isolated Ninja in `.claude/session-builds/ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 327 test cases / 17751 assertions.

### L.51 Cutscene compatibility global retirement (2026-04-28)

The next shim-retirement slice removed cutscene globals that were no longer read by production gameplay paths after the per-player cutscene state migration.

- Removed `g_InCutscene`, `g_CutsceneSkipRequested`, `g_CutsceneAnimNum`, `g_CutsceneCurAnimFrame60`, and `g_CutsceneCurTotalFrame60f`.
- Removed `playerSyncCutsceneGlobalsToCurrent()` and its call sites.
- `USINGDEVICE(device)` now checks `playerCurrentInCutscene()` instead of reading `g_InCutscene`.
- `SVC_CUTSCENE` handling now calls `playerSetCutsceneActiveMask(...)` for both client and pd-server builds.
- pd-server stubs keep a local cutscene active mask instead of a fake `g_InCutscene`.
- Static pd-tests guard the retired globals and wrapper against returning.

Verification: isolated build session `ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 328 test cases / 17784 assertions. `git diff --check` passed with only existing LF-to-CRLF warnings for two devtools scripts.

### L.52 PageUp/PageDown backend injection retirement (2026-04-28)

The next shim-retirement slice removed the backend PageUp/PageDown bridge that had been used as a generic ImGui tab-bar side channel.

- Social menu tabs now cycle through `pdguiMenuTabPrevPressed()` / `pdguiMenuTabNextPressed()` with explicit selected-tab state.
- `pdguiDriveImGuiNav()` no longer injects `ACTION_MENU_TAB_PREV/NEXT` as `ImGuiKey_PageUp/PageDown`.
- The main-menu PageUp/PageDown queue drain was removed because the backend no longer injects those keys every frame.
- Static pd-tests guard Social tab action ownership and prevent the PageUp/PageDown injection or queue drain from returning.
- First-party raw-key audit now shows no command reads; remaining `IsKey*` hits are comments or third-party ImGui internals.

Verification: isolated build session `ix46` built `pd`, `pd-server`, and `pd-tests`, then isolated `pd-tests.exe` passed 329 test cases / 17795 assertions.

### L.53 Inputctx to menu-layer bridge (2026-04-28)

The next infrastructure slice adds the bridge needed before `gameplayInputSuppressed()` and other remaining inputctx authority shims can safely retire.

- `port/src/inputctx.c` now mirrors effective non-gameplay input ownership into the typed input layer as a single `LAYER_MENU` handle.
- The bridge syncs on init, shutdown, push, resurrect, deferred pop, immediate pop, and end-frame compaction, so every existing menu-pool/inputctx owner gets the same layer signal without per-menu edits.
- Out-of-order bridge teardown uses `inputLayerHandleDistanceFromTop()` plus `inputLayerAbort()` to unwind any layer stacked above the mirrored menu handle before clearing it.
- Pure pd-tests model the bridge state transitions for nested menu contexts, deferred pop, and resurrected menus. A source guard pins the production bridge to real input-layer push/pop/abort calls.

Verification: attempted in isolated build session `ml53`, but client compilation stalled before a result. Mike directed to skip tests this time; `ml53` was stopped and removed. Build and `pd-tests` remain pending.

### L.54 Stage-transition cleanup helper (2026-04-28)

The next narrow infrastructure slice centralizes ordering-sensitive stage cleanup without introducing the full Scene Manager rewrite.

- Added `port/include/scene_transition.h` and `port/src/scene_transition.c`.
- `sceneStageTransitionPrepare(flags, reason)` owns the shared cleanup primitives: clear `g_ClientManifest`, fire disconnect scene teardown, and release the menu pool. Server builds compile the helper; client-only scene/menu cleanup is gated behind `!PD_SERVER`.
- `sceneStageChangeTo(stagenum, flags, reason)` runs the cleanup helper before `mainChangeToStage(stagenum)`, pinning the SP-13 invariant that MP client manifests are cleared before stage changes that must not consume them.
- Migrated priority cleanup sites that were open-coding the same primitives: solo endscreen retry/next/main-menu exit, `netDisconnect`, client `SVC_STAGE_START` menu teardown, client `SVC_STAGE_END` manifest cleanup, local `matchStart` / `matchStartFromChallenge`, and legacy `menutick.c` MP/coop manifest-clear-before-stage-change exits.
- Left non-stage manifest parse/rollback cleanup in `netmsg.c` alone. Those clears are data-validation cleanup, not transition handoff ordering.
- Added static pd-tests in `tests/test_scene_dispatch.cpp` guarding the helper API, server source inclusion, clear-before-stage-change ordering, and migrated priority callsites.

Verification: `git diff --check` passed. Build and `pd-tests` were skipped by Mike for this slice.

### L.55 Conservative layer-aware action read gate (2026-04-28)

The next shim-retirement substrate is a narrow read-side aperture in `actionmap.cpp`, not the full `gameplayInputSuppressed()` replacement.

- Added `actionLayerAllows(InputAction a)`.
- If the top input layer declares an action set, gameplay-only action reads must appear in that set. This lets cutscene, vehicle, and observer layers start constraining gameplay reads through declared ownership.
- Shared/system actions keep current behavior for now. This preserves existing menu, social, Forge, Skin Editor, and cutscene skip behavior while the remaining layer ownership is audited.
- Layers without a declared action set still fall back to the old `gameplayInputSuppressed() && actionIsGameplayOnly(...)` predicate.
- Query APIs now use the helper: pressed, held, released, scalar value, axis pair, hold threshold, tap, last gesture hold, hold progress, and hold start timestamp.
- Added a static pd-test in `tests/test_input_layer_stack.cpp` to prevent query reads from drifting back to the old inputctx-only gate.

Verification: `git diff --check` passed for the touched production/test/context files. Build and `pd-tests` were skipped by Mike for this slice.

### L.56 Conservative layer-aware action dispatch gate (2026-04-28)

The next follow-up extends the same aperture to digital VK dispatch writes without changing IMC priority order.

- `fireVk()` still walks active IMCs highest priority first and chooses one winning action per context.
- After the winning action is found, `fireVk()` calls `actionLayerAllows((InputAction)best_a)` before mutating `s_State`.
- If the top layer's declared action set does not allow that gameplay-only action, the event is consumed. It does not fall through to lower-priority contexts, preserving the established first-match-wins dispatch contract.
- The legacy `gameplayInputSuppressed()` context-level skip remains in place for gameplay, vehicle, and observer IMCs while the remaining non-digital action writers are audited.
- Added a static pd-test in `tests/test_input_layer_stack.cpp` to pin the gate before the `ActionState` write.

Verification: `git diff --check` passed for the touched production/test/context files. Build and `pd-tests` were skipped by Mike for this slice.

### L.57 Conservative layer-aware analog axis gate (2026-04-28)

The remaining same-class writer was per-frame analog axis polling, which bypasses `fireVk()`.

- Added `actionmapZeroGameplayAxes(player, zero_move, zero_aim)` so blocked axis pairs are actively cleared.
- `actionmapPollFrame()` now computes `moveAxesAllowed` from `ACTION_AXIS_MOVE_X/Y` and `aimAxesAllowed` from `ACTION_AXIS_AIM_X/Y` through `actionLayerAllows(...)`.
- Controller polling writes generic move and aim axes only for pairs allowed by the current top layer.
- Blocked pairs are zeroed for controller input and for player 0 keyboard-move synthesis. This preserves observer/freefly axis ownership while preventing cutscene/menu/vehicle layers from carrying stale generic gameplay axes.
- The existing vehicle-specific digital actions still flow through `fireVk()` and the vehicle action set; this slice does not add a generic vehicle axis model.
- Added a static pd-test in `tests/test_input_layer_stack.cpp` to pin analog aperture and zeroing.

Verification: `git diff --check` passed for the touched production/test/context files. Build and `pd-tests` were skipped by Mike for this slice.

### L.58 Hold bookkeeping and legacy pad axis bridge (2026-04-28)

The follow-up source audit covered the remaining direct action state readers/writers.

- `actionConsumeHold()` and `actionHoldConsumed()` now call `actionLayerAllows(...)` and the freefly block before touching hold-consumed state.
- `inputReadController()` no longer samples SDL controller axes directly for legacy `OSContPad` stick fields. It mirrors `ACTION_AXIS_MOVE_X/Y` and `ACTION_AXIS_AIM_X/Y` through `actionValue(...)`, then converts normalized action values to N64-style stick bytes.
- This keeps legacy pad samples under the same layer authority and zeroing as action-map reads.
- Raw `inputKeyPressed()` remains as documented key-capture/mouse plumbing. It was not swept in this slice.
- Added static pd-tests in `tests/test_input_layer_stack.cpp` to pin hold bookkeeping gates and to ensure `inputReadController()` axes come from `actionValue(...)`, not `SDL_GameControllerGetAxis(...)`.

Verification: `git diff --check` passed for the touched production/test/context files. Build and `pd-tests` were skipped by Mike for this slice.

### L.59 Current recursive boundary (2026-04-28)

Source audit after L.58:

- `rg "s_State\\[" port src --glob "!port/src/actionmap.cpp"` finds only comments outside `actionmap.cpp`.
- First-party gameplay/menu callers are behind public action query APIs.
- Remaining raw SDL axis reads are either the canonical action-map poller or documented deprecated key-capture paths in `input.c`.
- Therefore the next step is verification, not another code slice: run the isolated build/test flow, then playtest mission transitions, cutscene skip/continue, menus, vehicle, observer/freefly, and focus loss/regain.
- Keep `gameplayInputSuppressed()` as a transitional wrapper until that verification passes. Do not convert it to pure `inputLayerTopType() != LAYER_GAMEPLAY` in this unverified slice.

### L.60 s036-08 slice: four small priority-node migrations (2026-05-13)

The next menu graph slice migrated four small priority nodes whose only remaining raw menu-stack call was a single Back/Done/Close `menuPopDialog()`.

- Added four nodes to `port/src/menugraph.c`: `MENU_TYPE_AGENT_CREATE` (save / cancel pops), `MENU_TYPE_CHALLENGES` (back pop), `MENU_TYPE_MP_TEAM_SETUP` (done pop), `MENU_TYPE_MP_PLAYER_CONFIG` (close pop). Five `EDGE_POP` declarations total.
- Migrated five raw `menuPopDialog()` call sites to `menuGraphFirePop(MENU_TYPE_*, "edge_id")` across `pdgui_menu_agentcreate.cpp` (save + cancel), `pdgui_menu_challenges.cpp` (back), `pdgui_menu_teamsetup.cpp` (done), and `pdgui_menu_playerconfig.cpp` (close).
- Added four static pd-test cases to `tests/test_menu_graph.cpp` ([input][menu_graph][agent/challenges/teamsetup/playerconfig][static]) pinning the include, the edge declarations, the node registration, the new `menuGraphFirePop` call sites, and the absence of raw `menuPopDialog()` in the migrated function blocks.
- Build verify clean three-target via `build-session.ps1 -Session c036b8 / c036b8s / c036b8t`: client 55.6 MB, server 22.4 MB, tests 24.7 MB. Pd-tests `[input][menu_graph]` PASS 472 assertions / 27 cases. Pre-existing source-grep failures in `test_pdbase_retired_audit`, `test_catalog_provider_static`, and `test_uichrome_paths_pin` unchanged (not introduced by this slice).
- Remaining s036-08 surface after this slice: ~30 raw menu push/pop sites across the larger files (training.cpp 11 pops, solomission.cpp leftover pops, cheats.cpp pops, mpadvanced/mpsetup/mpsettings/botsetup/controldiagram/mainmenu push residue). Multi-session continuation per LF-1 in `audits/2026-05-13-followup-and-migration-sweep.md`.

---

## Appendix A: Audit raw findings

The three Phase-1 audit reports (raw SDL handlers, cutscene + objective transitions, menu graph + vehicle layers) live verbatim in [scratch/2026-04-27-input-universality-audits.md](../../scratch/2026-04-27-input-universality-audits.md) so the file references and counts can be re-checked when implementing each cohort. (Created Phase 2 day 1.)

## Appendix B: Cross-references

- [input-authority-and-menu-pool-2026-04-13.md](input-authority-and-menu-pool-2026-04-13.md) - prior ADR; this doc supersedes its layer model and corrects its dispatch-order documentation.
- [menu-stack-architecture.md](menu-stack-architecture.md) - menu strict-tree-stack rules; this doc subsumes them under the menu graph spec.
- [activemenu-radial-architecture.md](activemenu-radial-architecture.md) - radial-as-active-menu rule; preserved unchanged.
- [hud-layer-order.md](hud-layer-order.md) - HUD render ordering; not affected.
- [forge-level-editor-2026-04-16.md](forge-level-editor-2026-04-16.md) - forge layer wiring; LAYER_OBSERVER cleanup mentioned in K.9.
- [systemic-bugs.md](../systemic-bugs.md) SP-13 - manifestClear before mainChangeToStage; Scene Manager owns this invariant going forward.
- [constraints.md](../constraints.md) - active constraint "ImGui is the sole menu system" stays; "Mouse capture is driven by input context stack" upgrades to "Mouse capture is driven by Layer Stack."
