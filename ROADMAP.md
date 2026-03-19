# Perfect Dark 2 — Development Roadmap

## Phase 1: Stabilization (Current)
**Status: In Progress**

### 1A. Bug Fixes (code written, needs testing)
- Jump queuing on ramps — `jumpconsumed` flag + revised grounded check
- Ceiling penetration on angled roofs — pre-move ceiling check via `cdFindCeilingRoomYColourFlagsAtPos`
- Desk collision (Carrington Institute) — auto-floor generation extended to props with existing AABB collision
- Jump on spawn — `wantsjump`/`jumpconsumed` initialization at player allocation
- Font rendering on combat sim endscreen — fast3d TLUT TMEM fix + integrity diagnostics

### 1B. Memory Architecture (PC modernization)
- Move font allocations out of `MEMPOOL_STAGE` into dedicated persistent storage (`malloc` or `MEMPOOL_PERSISTENT`)
- Add debug canary/guard bytes around pool allocations in debug builds
- Separate read-only resources (fonts, palettes, UI textures) from mutable gameplay data
- Goal: eliminate silent cross-allocation corruption

---

## Phase 2: Dedicated Server + ImGui Framework
**Priority: High (enables testing for all subsequent work)**

### 2A. Server Core
- Headless game loop (no rendering, no audio)
- Network message processing (leverage existing netplay code)
- Authoritative game state: player positions, hit detection, score tracking
- Server-side tick rate configuration
- Graceful client connect/disconnect handling

### 2B. ImGui Server GUI
- Server control panel: start/stop match, map selection, game mode, bot config
- Live player list with stats (ping, score, kills, deaths)
- Console/log viewer for server events
- Settings persistence (server config file)
- Foundation for future tool GUIs (collider editor, level editor)

### 2C. Client Improvements
- Client-side prediction refinement
- Net-aware jump/movement (sync `jumpconsumed` state)
- Server browser / direct connect UI

---

## Phase 3: Collider System
**Dependency: Phase 2 (needs ImGui framework and server for testing)**

### 3A. Collider Data Model
- Define `collider_t` struct supporting: Box (oriented), Cylinder, Convex Hull, Mesh
- Per-collider properties: friction, bounciness, material type, collision layers/masks
- Serialization format (binary + JSON for editor interchange)
- Collider attachment to props: each prop can reference a `collider_t` by ID
- Runtime: compile `collider_t` into existing geo format for backward compatibility

### 3B. Collider Runtime
- New collision path for oriented boxes and convex hulls (GJK/SAT)
- Broadphase spatial index (grid or BVH) for prop colliders
- Integration with existing `cdTestVolume` / `cdFindGroundInfoAtCyl` — colliders participate alongside geo tiles
- Debug visualization: render collider wireframes in-game (toggle via console)

### 3C. Collider Editor (ImGui)
- Visual editor panel: select object, choose collider type, adjust parameters
- Show original N64 collision geometry (geo tiles) as reference overlay
- Show new custom collider alongside original — constrain-to-original toggle
- Real-time preview: changes visible immediately in-game
- Import/export collider definitions per-object
- Batch operations: apply collider template to all instances of a prop type

---

## Phase 4: Memory & Allocator Modernization
**Can run in parallel with Phases 2-3**

### 4A. Pool System Overhaul
- Replace linear bump allocator with tracked allocator (size, type, owner per allocation)
- Debug mode: guard pages, canary values, double-free detection
- `MEMPOOL_PERSISTENT` for cross-stage data (fonts, UI, config, collider defs)
- `MEMPOOL_STAGE` retained for per-level geometry and transient data
- `MEMPOOL_FRAME` for per-frame scratch (display lists, temp calculations)

### 4B. Resource Lifecycle
- Fonts: load once at startup, persist across stage transitions
- Collider definitions: load from files, persist independently of stage pool
- Audio banks: separate pool, not competing with gameplay memory
- Goal: no more "unrelated allocation corrupts critical data" class of bugs

---

## Phase 5: Level Editor
**Dependency: Phases 2, 3, 4**

### 5A. Editor Core
- ImGui-based editor UI (built on Phase 2B framework)
- Room/sector editing: create, resize, connect rooms
- Object placement: drag-and-drop props with real-time collision preview
- Material/texture assignment per surface
- Lighting placement and preview

### 5B. Editor-Collider Integration
- Collider editor (Phase 3C) embedded in level editor workflow
- Per-object and per-surface collision properties
- Walkable surface painting (FLOOR1/FLOOR2 flags)
- Collision layer visualization and testing

### 5C. Level Format
- New level format that compiles to PD's room/portal system
- Export to both runtime format (fast loading) and editor format (full fidelity)
- Backward compatible: can load and edit existing PD levels
- Support for custom textures, models, and sound

---

## Execution Order & Dependencies

```
Phase 1 (now) ──► Phase 2 (dedicated server) ──► Phase 3 (colliders) ──► Phase 5 (level editor)
                         │                              │
                         ▼                              ▼
                    Phase 4 (memory)              Phase 3C (collider editor)
                    (parallel track)              (builds on 2B ImGui)
```

**Critical path:** 1 → 2 → 3 → 5
**Parallel track:** Phase 4 memory work can happen alongside Phases 2-3

## Testing Strategy

Each phase produces testable deliverables:
- Phase 1: build, run, verify fixes manually (jump on ramps, ceiling, desk, font)
- Phase 2: dedicated server running with connected clients, basic match flow
- Phase 3: objects with custom colliders, player walks on/collides with them correctly
- Phase 4: debug build catches memory corruption that release build would miss
- Phase 5: create a simple custom room, place objects, play in it

The dedicated server's priority reflects that multiplayer testing is essential for validating all subsequent work — every collision fix, every memory change, every new feature needs to be tested under real gameplay conditions with real network latency.
