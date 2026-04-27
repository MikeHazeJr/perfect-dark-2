# Input Universality and Transitions

> Status: **PROPOSED (Phase 1 design)**
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

Default proposal: **bump `NET_PROTOCOL_VER 44 -> 45` in Cohort 4** for the per-player cutscene state. Coordinated with `MPSETUP_VERSION` if any (none required by this change).

Alternative: defer protocol bump until Cohort 5 to combine with vehicle layer net work. Cohort 4 ships with shimmed wire format that broadcasts mask=0xFF unconditionally for back-compat.

### K.9 Forge / Observer collapse (section E.7)

Default proposal: **keep the two-IMC stack (`g_ImcForgeSession` plus `g_ImcForge`) but pin both to LAYER_OBSERVER.** "Session active but not freefly" stays expressible by IMC priority within the layer.

Alternative: split into LAYER_FORGE_SESSION and LAYER_OBSERVER (where LAYER_OBSERVER is freefly only). Adds a layer for a small benefit.

---

## L. Decisions made during execution

This section is a running log filled in during Phase 2 implementation. Entries land alongside the code change that motivated them.

(Empty at Phase 1 surface.)

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
