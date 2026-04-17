# Forge — In-Game Level Editor

> Design document for Perfect Dark 2's level/map editor, inspired by Halo's Forge.
> Players edit maps from inside the game, switching between first-person play and a
> free-fly editor mode. Maps save as mods and integrate with multiplayer, co-op,
> counter-op, and eventually full custom missions.

**Status**: Design — not yet implemented
**Author**: Mike + Claude (S304 design session, 2026-04-16)
**Depends on**: Catalog system, mod infrastructure, ImGui menu system, spawn pool,
nine-slice chrome, input authority

---

## Table of Contents

1. [Vision](#1-vision)
2. [Player Modes](#2-player-modes)
3. [Object Catalog](#3-object-catalog)
4. [Placement & Manipulation](#4-placement--manipulation)
5. [Property Editor](#5-property-editor)
6. [Zones & Volumes](#6-zones--volumes)
7. [Logic System](#7-logic-system)
8. [Lighting & Atmosphere](#8-lighting--atmosphere)
9. [Level Settings](#9-level-settings)
10. [Save / Load / Share](#10-save--load--share)
11. [Multiplayer Integration](#11-multiplayer-integration)
12. [Mission Authoring](#12-mission-authoring)
13. [Custom Game Types](#13-custom-game-types)
14. [Stretch Goals & Differentiators](#14-stretch-goals--differentiators)
15. [Implementation Phases](#15-implementation-phases)
16. [File Inventory](#16-file-inventory)
17. [Open Questions](#17-open-questions)

---

## 1. Vision

Forge is a built-in level editor that lets players create and share multiplayer
arenas, custom missions, and sandbox experiences without leaving the game. The
editor lives inside the game world — the player walks through their creation in
first person, toggles into a free-fly ghost mode (Dr. Carroll) to place and
configure objects, then drops back into first person to playtest instantly.

Maps are first-class mods. They appear in the map selection for Combat Simulator,
can be shared online, and integrate with the full mod pipeline (catalog, manifest,
network sync). The goal is not a developer tool — it's a player-facing creative
platform that feels like playing, not working.

**Core principles:**
- **Immediate**: toggle between edit and play in under a second
- **Visual**: everything is WYSIWYG — no abstract data entry
- **Shareable**: every map is a mod that syncs over netplay
- **Layered**: simple placement for casual creators, deep logic for power users
- **Authentic**: outputs feel like native PD2 content, not user-generated afterthoughts

---

## 2. Player Modes

### 2.1 Normal Mode (First-Person Play)

Standard PD2 first-person controls. The player can walk, run, jump, shoot, pick
up items, and interact with placed objects. This is the "playtest" mode — the map
works exactly as it would in a real match. HUD displays normally. All gameplay
systems active.

Input to enter Forge mode: **Hold LB+RB (controller) / Hold Tab (keyboard)** for
0.5s. A brief radial wipe transition plays. The player's body disappears and is
replaced by the Dr. Carroll model.

### 2.2 Forge Mode (Free-Fly Editor)

The player becomes Dr. Carroll — a floating, glowing entity that can pass through
collision geometry. Movement uses 6DOF flight controls:

- **Left stick / WASD**: horizontal movement (world-relative)
- **Right stick / Mouse**: look direction
- **LT/RT / Q,E**: descend / ascend
- **Left stick click / Shift**: speed boost (3x)
- **Right stick click / Ctrl**: precision mode (0.25x speed, fine placement)

The Dr. Carroll model is visible to other players in co-op forge sessions. It
emits a soft glow and a subtle hum audio cue so collaborators can find each other.

**Forge HUD overlay** replaces the normal HUD:
- Crosshair becomes a placement reticle (shows snap grid when active)
- Bottom bar: current tool, selected object name, grid size
- Object count / budget display (top-right)
- Minimap with placed object dots

**Exiting Forge mode**: same input (Hold LB+RB / Tab). Dr. Carroll flies to the
nearest valid player-sized ground position, transitions back to first-person. If
no valid ground exists nearby, spawns at the nearest spawn point.

### 2.3 Mode Permissions

- **Solo Forge**: unrestricted — the player is always the editor
- **Co-op Forge** (stretch goal): multiple Dr. Carrolls; edit lock per-object
  (first to grab owns it until released)
- **Playtest mode**: all editors drop to Normal Mode simultaneously for a timed
  test round, then return to Forge
- **Published maps**: Forge mode disabled unless the player owns the map or the
  host enables editing

---

## 3. Object Catalog

All placeable objects organized into a hierarchical, searchable catalog. Opens as
a full-screen ImGui overlay in Forge mode (docked left panel or centered modal).

### 3.1 Top-Level Categories

```
Structures
├── Floors & Platforms
│   ├── Floor Tile (1x1, 2x2, 4x4, 8x8)
│   ├── Platform (thin, thick, glass)
│   ├── Ramp (15°, 30°, 45°)
│   ├── Stairs (straight, spiral, L-shaped)
│   └── Bridge (narrow, wide, suspension)
├── Walls
│   ├── Wall Panel (1x1, 2x1, 4x2)
│   ├── Wall with Window
│   ├── Wall with Door Frame
│   ├── Half Wall / Railing
│   └── Corner Piece (90°, 45°)
├── Columns & Beams
│   ├── Column (round, square)
│   ├── I-Beam
│   └── Support Strut
├── Ceilings & Roofs
│   ├── Ceiling Tile
│   ├── Skylight Panel
│   └── Angled Roof Section
└── Prefabs (user-saved groups)

Props
├── Furniture
│   ├── Desk, Table, Chair, Sofa
│   ├── Filing Cabinet, Locker
│   └── Computer Terminal, Monitor
├── Industrial
│   ├── Crate (small, medium, large)
│   ├── Barrel (standard, explosive)
│   ├── Pipe Section, Vent
│   └── Generator, Control Panel
├── Natural
│   ├── Rock (various sizes)
│   ├── Tree, Bush, Grass Patch
│   └── Water Plane
├── Decorative
│   ├── Poster, Sign, Banner
│   ├── Light Fixture (ceiling, wall, floor)
│   ├── Railing Section
│   └── Caution Tape, Barrier
└── dataDyne / Carrington / Skedar themed sets

Weapons & Pickups
├── Weapons (all PD2 weapons — each becomes a weapon pad)
├── Ammo (per-weapon-type ammo crate)
├── Equipment
│   ├── Shield
│   ├── Cloaking Device
│   ├── Night Vision
│   ├── Combat Boost
│   └── Custom Equipment (mod-defined)
└── Pickup Pads (configurable: weapon choice, respawn time, ammo count)

Spawn Points
├── Player Spawn (generic — any mode)
├── Team Spawn Zone (team-colored volume — spawns within zone)
├── Initial Spawn (match-start only, used once)
├── Respawn Point (post-death, weighted by distance from killer)
├── Vehicle Spawn (future — hover crate, etc.)
└── Bot Patrol Point (AI waypoint node)

Characters & AI
├── Guard (by type — dataDyne, Carrington, Skedar, Maian)
├── Civilian
├── Boss Character
├── Custom Chr (from Skin Editor mods)
└── AI Behavior Preset (patrol, guard, aggressive, passive, scripted)

Interactables
├── Doors
│   ├── Sliding Door (single, double)
│   ├── Blast Door (heavy, slow)
│   ├── Keycard Door (requires key item)
│   └── Destructible Door / Wall
├── Elevators & Lifts
│   ├── Platform Elevator (two-stop, multi-stop)
│   ├── Gravity Lift (one-way vertical)
│   └── Launch Pad (ballistic arc)
├── Switches & Buttons
│   ├── Wall Switch
│   ├── Floor Pressure Plate
│   ├── Terminal / Hackable Console
│   └── Timed Button (held activation)
├── Computers & Terminals
│   ├── Hackable Terminal (objective target)
│   ├── Camera System
│   ├── Alarm Panel
│   └── Display Screen (customizable text/image)
└── Destructibles
    ├── Breakable Glass
    ├── Explosive Container
    └── Structural Support (destroy to collapse section)

Zones & Volumes
├── Trigger Zone (fires event when player enters)
├── Radiation Zone (damage-over-time)
├── Low/High/Zero Gravity Zone
├── Teleporter (entrance → exit pair)
├── Kill Zone (instant death — out-of-bounds)
├── Water Volume (swimming physics)
├── Sound Zone (ambient audio region)
├── Fog Volume (local fog override)
└── No-Weapon Zone (forces holster)

Lighting
├── Point Light (color, intensity, radius, falloff)
├── Spotlight (color, intensity, cone angle, range)
├── Area Light (soft fill — rectangle or disc)
├── Emissive Surface (glow panel — decorative)
└── Light Probe (for GI approximation in clusters)

Effects
├── Particle Emitter (fire, smoke, sparks, steam, rain, snow)
├── Sound Emitter (ambient loop — selectable from audio catalog)
├── Decal Projector (blood, scorch, graffiti, arrow marking)
├── Screen Shake Zone (explosion aftershock area)
└── Post-Process Volume (color grade, bloom, vignette override)
```

### 3.2 Catalog UI

- **Left panel** in Forge mode: collapsible tree + search bar at top
- **Hover preview**: 3D thumbnail of selected object rotates in a preview pane
- **Recent**: top section shows last 10 placed object types
- **Favorites**: star any item to pin it
- **Search**: fuzzy text search across all categories and tags
- **Mod objects**: objects from installed mods appear under their mod name as a
  top-level category; tagged with mod icon
- **Budget bar**: shows object count / triangle count / memory against the map budget

### 3.3 Object Budget

Every map has a budget to ensure playable performance:

| Metric | Soft Limit | Hard Limit | Notes |
|--------|-----------|------------|-------|
| Total objects | 500 | 1024 | Warning at soft, blocked at hard |
| Triangles | 500K | 1M | From placed geometry only |
| Lights | 32 | 64 | Point + spot combined |
| Particle emitters | 16 | 32 | Active simultaneously |
| Logic nodes | 128 | 256 | Triggers + conditions + actions |
| Audio emitters | 16 | 32 | Simultaneous voices |

Budget is displayed live. Approaching soft limit turns the counter yellow;
at hard limit, placement is blocked with a clear message.

---

## 4. Placement & Manipulation

### 4.1 Placement Flow

1. Select object from catalog (click or press A/Enter)
2. Object appears attached to the placement reticle (center of screen), floating
   at the camera's focal distance
3. Object previews as semi-transparent with a green tint (valid) or red tint
   (invalid — intersecting geometry, out of bounds, over budget)
4. Move look direction to position; scroll wheel / D-pad up/down adjusts distance
5. Press A / Left-Click to place; press B / Right-Click to cancel
6. After placing, the same object type stays selected for rapid repeat placement
7. Press Y / Middle-Click to switch to Select tool

### 4.2 Select & Manipulate

- **Click** on a placed object to select it (highlight outline, corner gizmo handles)
- **Box select**: hold and drag to select multiple objects
- **Selected object shows**: 3-axis gizmo (translate, rotate, scale — cycle with
  bumpers / R key)

**Gizmo modes:**
- **Translate** (default): drag along X/Y/Z axis or free-move on the plane facing camera
- **Rotate**: drag around axis rings; snaps to 15° by default (hold Shift for free)
- **Scale**: drag axis handles for per-axis scale, drag center for uniform scale

**Snap:**
- Grid snap: 0.25 / 0.5 / 1.0 / 2.0 / 4.0 unit increments (cycle with [ ] keys)
- Surface snap: object bottom aligns to the surface under it (toggle with S key)
- Edge snap: approaching another object's edge/face snaps flush (magnetic)
- Rotation snap: 15° / 45° / 90° (cycle with < > keys)

### 4.3 Multi-Select Operations

- **Group move**: all selected objects translate/rotate together around their centroid
- **Duplicate**: Ctrl+D / hold LT+A — places copies offset by one grid unit
- **Delete**: Delete key / X button — with undo
- **Align**: context menu options — align to floor, align tops, distribute evenly
- **Save as Prefab**: selected group → named prefab in the catalog's Prefab section

### 4.4 Undo / Redo

Ring buffer of 256 operations. Every placement, deletion, property change, and
transform is an undoable operation.

- **Undo**: Ctrl+Z / LB+B
- **Redo**: Ctrl+Y / LB+A
- Undo stack persists across mode transitions (Normal ↔ Forge) but clears on
  save (save is the new baseline)

---

## 5. Property Editor

Right-click / press X on a selected object opens a property panel (right-side
ImGui panel or centered popup for small objects).

### 5.1 Universal Properties (all objects)

| Property | Type | Notes |
|----------|------|-------|
| Position | vec3 | world coordinates, editable numerically |
| Rotation | vec3 | euler degrees |
| Scale | vec3 | per-axis or uniform toggle |
| Name / Label | string | optional, for logic references |
| Visible | bool | hide without deleting (for testing) |
| Collision | enum | solid / passthrough / projectile-only |
| Team | enum | neutral / team 1-8 (for team-colored objects) |

### 5.2 Visual Properties

| Property | Type | Notes |
|----------|------|-------|
| Material / Texture | catalog ref | browse from texture catalog; preview swatch |
| Tint Color | color picker | multiplied over base texture |
| Emissive | float 0-1 | self-illumination intensity |
| Cast Shadows | bool | performance toggle |
| LOD Bias | enum | force high/medium/low detail |

### 5.3 Weapon Pad Properties

| Property | Type | Notes |
|----------|------|-------|
| Weapon | catalog ref | which weapon spawns here |
| Ammo Count | int | starting ammo (default = weapon default) |
| Dual Wield | bool | spawn paired weapons |
| Respawn Time | float seconds | 0 = one-time pickup |
| Respawn Effect | enum | none / glow / hologram |
| Team Locked | enum | any / specific team only |

When saved, each weapon pad becomes an `INTROCMD_WEAPON` + pad entry in the
generated setup data.

### 5.4 Spawn Point Properties

| Property | Type | Notes |
|----------|------|-------|
| Type | enum | initial / respawn / both |
| Team | enum | any / team 1-8 |
| Facing | angle | initial look direction (auto from wall-probe, overridable) |
| Priority | int | higher = preferred for initial spawns |
| Radius | float | for team spawn zones — random within radius |

When saved, each spawn becomes a pad + spawn pool entry. Team spawn zones
generate multiple pads within their volume.

### 5.5 Interactable Properties

**Doors:**
| Property | Notes |
|----------|-------|
| Open Direction | slide-left / slide-right / slide-up / swing |
| Open Speed | seconds to fully open |
| Auto-Close | bool + delay |
| Locked | bool — requires key/switch/logic to unlock |
| Key Item | catalog ref (if locked) |
| Sound | open/close audio from catalog |

**Elevators:**
| Property | Notes |
|----------|-------|
| Stops[] | list of Y-positions (minimum 2) |
| Speed | units/second |
| Wait Time | seconds at each stop |
| Call Button | bool — requires button press at each stop |
| Loop | bool — continuous cycle vs. call-only |

**Switches:**
| Property | Notes |
|----------|-------|
| Type | toggle / momentary / hold |
| Target | linked object(s) — door, elevator, event node |
| Activation | interact / shoot / proximity |
| Team Lock | any / specific team |
| Cooldown | seconds before re-activation |

### 5.6 Chr / AI Properties

| Property | Notes |
|----------|-------|
| Body / Head | catalog refs (from chr catalog) |
| Weapon | starting weapon (catalog ref) |
| Health | multiplier (0.5x–4x) |
| AI Behavior | patrol / guard / aggressive / passive / scripted |
| Patrol Path | linked patrol point sequence |
| Alert Radius | distance to detect player |
| Faction | friendly / hostile / neutral |
| Respawn | bool + delay |
| Dialog | bark set on alert / combat / death |

---

## 6. Zones & Volumes

Zones are invisible volumes (shown as tinted wireframe boxes/spheres in Forge
mode) that apply effects to players or objects within them.

### 6.1 Zone Types

**Trigger Zone**: fires an event when a player enters/exits/stays. The event
connects to the logic system (§7). Configurable: once vs. repeating, team filter,
delay before trigger.

**Radiation Zone**: DOT (damage over time) to players inside. Configurable:
damage per second, damage type (radiation/fire/cold/poison), visual overlay
tint, entry warning sound.

**Gravity Zone**: overrides gravity within the volume. Settings: gravity
multiplier (0 = zero-G, 0.25 = moon, 2.0 = heavy), direction vector (default
down, but can be sideways/up for disorienting arenas).

**Teleporter**: entrance volume → exit pad. Configurable: one-way vs. bidirectional,
preserve velocity (portals) vs. reset velocity, visual/audio effects on entry/exit,
team filter.

**Kill Zone**: instant death boundary. Typical use: out-of-bounds under the map.
Shows as red wireframe. Configurable: death message override ("fell to their
death" / "was consumed by the void" / custom).

**Water Volume**: enables swimming physics within. Surface plane renders water
shader. Configurable: depth, current direction and speed, visibility (murky vs.
clear), damage (toxic water).

**Sound Zone**: ambient audio region. Configurable: audio loop (from catalog),
volume, fade distance (crossfade between zones).

**No-Weapon Zone**: forces weapon holster on entry. Used for social areas,
puzzle rooms, or non-combat mission segments.

**Fog Volume**: local atmosphere override. Configurable: fog color, density,
height, blend distance at boundaries.

### 6.2 Zone Visualization

In Forge mode, zones render as:
- Tinted semi-transparent boxes/spheres (color = zone type: red=kill, green=trigger,
  blue=gravity, yellow=radiation, cyan=teleporter, etc.)
- Wireframe outline always visible
- Type icon floating at center
- Arrow showing direction (gravity, teleporter exit, current flow)
- Toggle all zone viz on/off with V key

---

## 7. Logic System

A visual node-based system for creating gameplay rules without code. Exposed as
a separate "Logic" tab in the Forge menu, or as connections between objects
in the 3D view (shown as colored wires in Forge mode).

### 7.1 Core Concepts

**Events** — things that happen (player enters zone, switch activated, enemy
killed, timer expires, item picked up).

**Conditions** — gates that check state before allowing an action (player has
key, N enemies killed, team score >= X, specific player alive).

**Actions** — things the system does (open door, spawn enemies, play sound,
show message, teleport player, change gravity, end mission, award score).

**Channels** — named signals that connect events to actions across the map.
A switch sets channel "alpha" to ON; a door listens to channel "alpha" and
opens when it's ON. Multiple inputs can OR/AND into a channel.

### 7.2 Node Types

**Event Nodes:**
| Node | Inputs | Outputs |
|------|--------|---------|
| On Player Enter Zone | zone ref, team filter | fire signal |
| On Player Exit Zone | zone ref | fire signal |
| On Object Destroyed | object ref | fire signal |
| On Switch Activated | switch ref | fire signal |
| On Kill | victim filter (any/specific/team) | fire signal + killer ref |
| On Kill Count | target count, victim filter | fire signal when reached |
| On Item Pickup | item ref | fire signal + player ref |
| On Timer | duration, repeat | fire signal |
| On Round Start | — | fire signal |
| On Round End | — | fire signal |
| On Channel | channel name | fire when channel goes ON/OFF |

**Condition Nodes:**
| Node | Check |
|------|-------|
| Has Item | player has specific item in inventory |
| Kill Count >= N | team or individual kill threshold |
| All Enemies Dead | all hostile AI in zone/map eliminated |
| Switch State | specific switch ON/OFF |
| Channel State | named channel ON/OFF |
| Team Score >= N | scoreboard check |
| Player Count | alive players >= N |
| Timer Elapsed | seconds since match/round start |
| Random | probability 0-100% (for variable encounters) |

**Action Nodes:**
| Node | Effect |
|------|--------|
| Open / Close Door | target door ref |
| Activate Elevator | send to stop N |
| Spawn Object | catalog ref at position |
| Spawn AI | chr preset at position or pad |
| Destroy Object | remove from world |
| Play Sound | audio ref, position or global |
| Show Message | HUD text, duration, recipient (all/team/player) |
| Set Channel | channel name ON/OFF/TOGGLE |
| Teleport Player | destination pad |
| Change Zone | modify zone property (gravity, damage, etc.) |
| Set Timer | start/stop/reset named timer |
| Award Score | points to player/team |
| End Mission | success/failure + message |
| Lock / Unlock Door | change door locked state |
| Enable / Disable Object | toggle object active state |
| Camera Event | cutscene camera move (mission authoring) |

### 7.3 Logic UI

**Wiring view**: in Forge mode, pressing L toggles logic wire visualization.
Selected object shows its connections as colored lines to linked objects.
Click an output port, then click a target input port to create a wire.

**Logic editor panel**: full-screen node graph (ImGui node editor). Nodes
are draggable boxes with typed input/output ports. Wires connect ports.
Color-coded by type: white=event, yellow=condition, green=action, blue=channel.

**Quick-link**: right-click an object in 3D → "Link to..." → click target object.
Auto-creates the most common connection (switch→door, trigger→spawn, etc.).

### 7.4 Logic Execution

- Events evaluate every tick (60Hz) but most are interrupt-driven (enter zone =
  collision callback, not polling)
- Conditions are lazy-evaluated: only checked when their parent event fires
- Actions execute immediately or with configurable delay
- Channels are global state booleans; changes propagate to all listeners same tick
- Circular dependencies are detected at save time and flagged as warnings
- Logic state resets on round restart (configurable: some state persists across rounds)

---

## 8. Lighting & Atmosphere

### 8.1 Sky System

Each map selects a sky from the sky catalog:

- **dataDyne Night**: dark blue, city skyline, light pollution glow
- **Carrington Day**: bright blue, scattered clouds, warm sun
- **Skedar Homeworld**: alien purple, twin moons, aurora
- **Space Station**: starfield, Earth below, no atmosphere
- **Underground**: no sky (ceiling forced), ambient-only lighting
- **Storm**: dark grey, lightning flashes, rain particles
- **Sunset**: orange/pink gradient, long shadows
- **Custom**: modded sky textures from catalog

Sky selection affects the ambient light color and intensity automatically but
can be overridden.

### 8.2 Skylight (Directional Light)

The primary scene light. Properties:

| Property | Type | Notes |
|----------|------|-------|
| Direction | vec2 (yaw, pitch) | sun/moon position |
| Color | RGB | warm yellow, cool blue, alien green, etc. |
| Intensity | float 0-4 | 0 = pitch black, 1 = default |
| Shadow | bool | cast directional shadow map |
| Shadow Softness | float | hard ↔ soft shadow edges |

### 8.3 Placed Lights

**Point Light**: omnidirectional light source. Properties: position, color,
intensity, radius, falloff curve (linear/quadratic/none). Visual: shows as a
glowing orb in Forge mode, invisible in play.

**Spotlight**: directional cone light. Properties: position, direction, color,
intensity, inner/outer cone angle, range, shadow (bool), cookie texture (optional
— projected pattern like window bars, industrial grate). Visual: shows as a cone
wireframe in Forge mode.

**Area Light**: soft rectangular light for fill. Lower performance cost than
multiple point lights for the same coverage. Properties: position, orientation,
width, height, color, intensity.

**Emissive Surface**: not a true light source — makes a surface glow. Applied
via the property editor on any object's material. Cheap visual-only effect.

### 8.4 Atmosphere

| Setting | Type | Notes |
|---------|------|-------|
| Fog Enable | bool | global distance fog |
| Fog Color | RGB | matches sky or custom |
| Fog Near/Far | float pair | fade start/end distance |
| Fog Height | float | height-based fog density |
| Ambient Color | RGB | fills shadow areas |
| Ambient Intensity | float | shadow darkness |
| Exposure | float | overall brightness |
| Bloom | float 0-1 | glow around bright objects |
| Color Grade | enum | neutral/warm/cool/noir/alien |

### 8.5 Time of Day (Stretch Goal)

Optional dynamic lighting: skylight rotates over a configurable cycle (e.g.,
10-minute day/night). Affects sky color, ambient, fog, and placed light enable
states (some lights tagged "night only"). Would require light map updates or
fully dynamic lighting.

---

## 9. Level Settings

Accessible from the Forge menu top bar: **File | Edit | View | Level Settings | Test**

### 9.1 Map Properties

| Setting | Notes |
|---------|-------|
| Map Name | displayed in map selection |
| Map Description | shown in browser, max 256 chars |
| Author | auto-filled from player profile |
| Thumbnail | auto-captured from a camera position, or manually set |
| Game Modes | checkboxes: Deathmatch, Team DM, CTF, King of Hill, Oddball, etc. |
| Max Players | 2–32 (affects spawn point requirements) |
| Recommended Players | "best with 4-8" hint |
| Map Size | small/medium/large (cosmetic tag) |

### 9.2 Spawn Configuration

| Setting | Notes |
|---------|-------|
| Min Spawn Points | warning if below threshold for max players |
| Team Spawn Mode | scattered / zoned (team areas) / symmetric |
| Initial Spawn | use Initial Spawn points only, or any |
| Respawn Delay | default override (0 = instant) |
| Spawn Protection | seconds of invulnerability after respawn |

Validation: on save, the editor checks spawn point count vs. max players and
warns if insufficient. The S302 tiered spawn system handles runtime degradation,
but the editor should encourage proper coverage.

### 9.3 Game Rules (Per-Map Defaults)

Maps can set default game rules that hosts can override:

| Rule | Notes |
|------|-------|
| Time Limit | default match duration |
| Score Limit | default score target |
| Weapon Set | all / map-placed only / specific set |
| Health | normal / 200% / 50% |
| Radar | on / off / proximity |
| Auto-Aim | on / off |
| One-Hit Kills | bool |
| Friendly Fire | on / off |

### 9.4 Bounds & Out-of-Bounds

- **Map Bounds**: a large invisible box that defines the playable area. Anything
  outside is automatically a kill zone.
- **Soft Bounds**: warning zone around the edge — "Return to the combat zone"
  with a timer before kill.
- **Editor Grid Floor**: infinite grid plane at Y=0 for reference. Toggleable.
- **Skybox Volume**: defines where the sky renders vs. geometry ceiling.

---

## 10. Save / Load / Share

### 10.1 File Format

Maps save as a mod in the `mods/Forge Maps/<map-name>/` directory:

```
mods/Forge Maps/my-arena/
├── mod.json              # mod manifest (type: "forge-map")
├── map.json              # full map data (objects, logic, settings)
├── thumbnail.tga         # auto-captured or manual screenshot
├── lightmap.bin          # baked lighting data (optional)
├── navmesh.bin           # AI navigation mesh (auto-generated)
└── assets/               # custom textures/models imported by user
    ├── custom_wall.tga
    └── ...
```

**map.json** structure:
```json
{
  "format_version": 1,
  "name": "My Arena",
  "author": "PlayerName",
  "created": "2026-04-16T12:00:00Z",
  "modified": "2026-04-16T14:30:00Z",
  "base_stage": "base:area52_rig",
  "settings": { /* §9 level settings */ },
  "sky": { "type": "carrington_day", "overrides": {} },
  "lighting": { "skylight": {}, "atmosphere": {} },
  "objects": [
    {
      "id": "obj_001",
      "catalog_id": "base:prop_crate_large",
      "position": [100.0, 0.0, -200.0],
      "rotation": [0.0, 45.0, 0.0],
      "scale": [1.0, 1.0, 1.0],
      "properties": { /* type-specific */ },
      "label": "Crate by ramp"
    }
  ],
  "spawn_points": [ /* §5.4 entries */ ],
  "weapon_pads": [ /* §5.3 entries */ ],
  "zones": [ /* §6.1 entries */ ],
  "logic": {
    "channels": ["alpha", "beta", "door_power"],
    "nodes": [ /* §7.2 entries with wiring */ ]
  },
  "lights": [ /* §8.3 entries */ ],
  "prefabs_used": [ /* references to prefab mods */ ]
}
```

### 10.2 Base Stage

Every Forge map starts from a **base stage** — an existing map that provides the
base geometry, collision, and room structure. The Forge objects are layered on top.

Options:
- **Existing MP maps**: start from any Combat Simulator map and add/modify objects
- **Paradox template** (stage 0x5e): minimal geometry — a large flat platform ideal
  for building from scratch. (See project memory: paradox-template-map.)
- **Blank canvas**: procedurally generated flat arena at configurable size
  (small 64x64, medium 128x128, large 256x256 units)
- **Campaign stages**: start from a campaign map for mission authoring

### 10.3 Save Operations

- **Quick Save**: Ctrl+S / Start+A — overwrites current slot
- **Save As**: new name, new mod folder
- **Auto-Save**: every 2 minutes during editing (separate slot, doesn't overwrite
  manual save)
- **Export as Mod**: packages for sharing — includes all custom assets, generates
  catalog entries, ready for network sync via the mod pipeline

### 10.4 Load Operations

- **Recent Maps**: last 5 edited maps at the top of the Forge menu
- **Browse Maps**: full mod catalog browser filtered to type "forge-map"
- **Import**: load a shared .zip mod package into the mods directory

### 10.5 Network Sharing

When a host selects a Forge map for a match:
1. Map mod enters the match manifest (same as any mod)
2. Clients without the map receive it via SHA-256 verified mod transfer
3. Map loads from the transferred mod on all clients
4. The mod pipeline handles all of this — Forge maps are just mods

---

## 11. Multiplayer Integration

### 11.1 Map Selection

Forge maps appear alongside stock maps in Combat Simulator map selection.
Tagged with a Forge icon and the author name. Can be filtered:
- All maps / Stock only / Forge only
- By author / by game mode support / by player count

### 11.2 Runtime Behavior

At match load:
1. Base stage loads normally (geometry, collision, rooms, pads)
2. Forge overlay applies: objects spawned, weapon pads registered, spawn points
   added to pool, zones activated, logic initialized, lights placed
3. Manifest includes Forge objects' catalog assets (weapon models, chr bodies, etc.)
4. AI nav mesh loaded (pre-baked at save time)
5. Match runs normally — Forge objects are indistinguishable from native geometry

### 11.3 Collision

Placed structure objects register dynamic collision meshes. The collision system
already handles runtime props (weapons, crates) — Forge structures use the same
mechanism but are flagged as static (no physics tick, just collision response).

Objects flagged `collision: passthrough` don't register collision — decorative only.
Objects flagged `collision: projectile-only` block bullets but not player movement.

### 11.4 Game Mode Support

Each Forge map declares supported game modes. Mode-specific objects:
- **CTF**: flag stands (2, one per team, placed by editor)
- **King of the Hill**: hill zone (uses trigger zone with "hill" tag)
- **Oddball**: ball spawn point
- **Territories**: territory zones with capture progress

These are just specialized zone/spawn objects in the catalog.

### 11.5 Custom Game Types

Beyond selecting from stock modes, Forge enables **authoring entirely new game
types** using the logic system + a Game Type Definition layer. A custom game type
is a reusable rule set that can be applied to any compatible Forge map.

#### Game Type Definition

A game type is a named, saveable configuration that defines:

| Element | Description |
|---------|-------------|
| **Win Condition** | logic expression: score >= N, last alive, timer expires, boss killed, objectives complete |
| **Score Rules** | what awards points: kills, headshots, objective captures, survival time, damage dealt |
| **Round Structure** | single round / best-of-N / wave-based / phase-based (transitions on logic trigger) |
| **Player Setup** | starting weapons, health multiplier, abilities, team assignment rules |
| **Enemy Waves** (PvE) | wave definitions: enemy type, count, scale, health multiplier, spawn delay, spawn zone |
| **Boss Encounters** | special enemy with boss health bar, scaled model, custom AI behavior, loot drop on kill |
| **Modifiers** | global mutators: low gravity, one-hit kills, infinite ammo, radar off, etc. |
| **HUD Elements** | wave counter, boss health bar, survival timer, custom score display |

#### Wave-Based PvE Example: "Skedar Onslaught"

```
Game Type: Skedar Onslaught
Structure: Wave-based (N waves + boss)

Wave 1-3:  8 Skedar Warriors, scale 0.5x, health 0.5x, aggressive AI
Wave 4-6:  12 Skedar Warriors, scale 0.5x, health 0.75x + 2 Skedar Elites (1.0x)
Wave 7-9:  16 mixed Skedar, scale 0.5x-0.75x, health 1.0x, drop ammo on kill
Wave 10:   BOSS — Skedar Brood Mother, scale 3.0x, health 20x
           Boss health bar displayed (full-width HUD element)
           Phase transitions at 75% / 50% / 25% health (spawns reinforcements)
           Custom death event: slow-motion kill cam + victory fanfare + score bonus

Between waves: 10s intermission, ammo resupply pads activate, wave counter increments
Player death: respawn with starter loadout, lose score streak multiplier
Win: kill boss → end screen with stats (kills, damage, deaths, time survived)
Lose: all players dead simultaneously → failure screen with wave reached
```

#### Boss Health Bar System

Any chr can be tagged as a **Boss** in the property editor:
- **Boss Name**: displayed above the health bar ("Skedar Brood Mother")
- **Health Bar**: full-width HUD bar, segmented by phase thresholds
- **Phase Thresholds**: at each percentage, fires a logic event (trigger reinforcements,
  change behavior, play sound, enter rage mode)
- **Death Event**: slow-motion window, custom sound, screen flash, score award
- **Model Scale**: bosses can be scaled up (1x–5x) independent of other properties
- **Damage Resistance**: optional phase-based armor (e.g., immune to body shots until
  helmet destroyed)

#### Wave Spawner Node

A specialized logic node for PvE game types:

| Property | Notes |
|----------|-------|
| Enemy Type | chr preset from catalog (with scale/health override per wave) |
| Count | enemies per wave |
| Spawn Zone | trigger zone(s) where enemies materialize |
| Spawn Rate | all-at-once / staggered (N per second) / random within window |
| Wave Trigger | auto (timer) / manual (all enemies dead) / logic event |
| Intermission | seconds between waves, events to fire (resupply, dialog, etc.) |
| Escalation | per-wave multipliers: count × 1.5, health × 1.2, speed × 1.1 |
| Final Wave | boss flag — spawns boss chr instead of regular enemies |

#### Other Custom Game Type Examples

**Infection**: one player starts "infected" (Skedar skin, melee only). Killed
players switch teams. Last human alive wins. Uses team-swap logic + custom
kill event.

**Gun Game**: every kill advances your weapon to the next in a defined sequence.
Final weapon = Slayer rocket launcher. First to get a kill with every weapon wins.
Uses per-player score tracking + weapon-swap action on kill event.

**VIP**: one player per team is the VIP (random, marked on HUD). VIP death ends
the round. VIP has 2x health but can't pick up weapons. Uses role assignment +
custom win condition.

**Juggernaut**: one player gets 4x health + all weapons. Everyone else hunts them.
Killing the Juggernaut makes you the new Juggernaut. Uses role-transfer logic +
stat modification.

**Horde Defense**: players defend a central objective (computer terminal with health
bar) against waves. Enemies path toward the objective. Uses wave spawner + object
health + custom loss condition.

**Speedrun**: solo mission with a visible speedrun timer. Leaderboard-tracked
completion time. Ghost replay of best run (stretch). Uses timer display + objective
completion + per-map leaderboard.

#### Game Type as a Mod

Custom game types save as mods (type: "forge-gametype") and can be shared
independently of maps. A game type mod includes:
- `gametype.json`: rule definitions, wave data, score rules, HUD config
- Optional custom HUD elements (boss bar layout, wave counter style)
- Can reference any catalog assets for enemy types, weapons, etc.
- Applied to any compatible map at match setup time

Game types appear in Combat Simulator setup alongside stock modes.

---

## 12. Mission Authoring

Forge maps with game mode "Mission" become playable in solo campaign or co-op.
Mission authoring adds:

### 12.1 Objectives

Objectives are logic graph outputs. An objective node has:
- **Description**: text shown in the mission briefing and HUD
- **Type**: primary (must complete) / secondary (optional) / bonus (hidden until triggered)
- **Completion Condition**: connected to logic (all enemies killed, item retrieved,
  terminal hacked, zone reached, etc.)
- **Failure Condition**: optional (timer expired, civilian killed, detected, etc.)
- **Order**: sequential (must complete in order) or parallel (any order)

### 12.2 Briefing

- **Briefing text**: mission description shown pre-load (editable in Forge)
- **Briefing camera**: saved camera path for the pre-mission flythrough
- **Difficulty scaling**: per-difficulty overrides (more guards, less ammo, etc.)
  defined as logic condition branches

### 12.3 Cinematics (Stretch Goal)

Camera nodes with keyframed positions define in-engine cutscenes:
- Triggered by logic events
- Player loses control during playback
- AI can be scripted to move/act during cinematics
- Simple timeline UI in the logic editor

---

## 13. Custom Game Types

The Game Type system is elevated from §11.5 — it's a core feature, not a
sub-item. See §11.5 for the full specification including wave spawner nodes,
boss health bar system, game type definition schema, and examples (Skedar
Onslaught, Infection, Gun Game, VIP, Juggernaut, Horde Defense, Speedrun).

Custom game types are independent mods that can be applied to any compatible
Forge map. They extend the logic system with high-level game rule primitives
(win conditions, score rules, wave structure, boss encounters, player role
assignment, and custom HUD elements).

The implementation phase for this is **F5b** (see §15), built on top of F5's
logic system foundation.

---

## 14. Stretch Goals & Differentiators

These are the features that could make Forge truly special — beyond what Halo's
Forge offered.

### 13.1 Terrain Brushes

Instead of only placing prefab floors, a terrain brush lets players paint
heightmap terrain in real-time:
- Raise / lower / smooth / flatten brushes
- Texture painting (grass, rock, sand, snow, metal) with blend at edges
- Generates collision mesh and nav mesh in real-time
- Would replace the "blank canvas" base stage with something organic

### 13.2 Co-Op Editing

Multiple players in Forge mode simultaneously:
- Each player is a Dr. Carroll with a unique color
- Object lock: first to select owns it until released (prevents conflicts)
- Real-time sync: placed objects appear for all editors instantly
- Voice/text chat for coordination
- "Spectate editor": watch what another player is building in real-time

### 13.3 Blueprint Sharing

Save any selection as a "blueprint" — a prefab mod:
- Blueprints appear in a community section of the catalog
- Other players can drop entire room setups, arena layouts, or logic circuits
  into their maps
- Version control: blueprints track which version they were created in
- Rating / favorites for popular blueprints

### 13.4 AI Director

Instead of hand-placing every guard, define encounter zones:
- "Spawn 4-6 dataDyne guards in this room"
- "Patrol between these waypoints"
- "Reinforce from this door when alerted"
- AI director handles specific placement, patrol timing, and difficulty scaling

### 13.5 Physics Objects

Objects with full physics simulation:
- Barrels that roll when shot
- Crates that stack and topple
- Ragdoll props for environmental storytelling
- Physics-based traps (rolling boulder, swinging blade, falling platform)

### 13.6 Custom Game Modes

The logic system extended with score rules:
- Define scoring events (kill = 1 point, headshot = 2, objective capture = 5)
- Custom win conditions (first to 50, most points in 10 minutes, last alive)
- Round-based rules (best of 5 rounds, elimination, waves)
- This turns Forge into a game-within-a-game platform

### 13.7 Sound & Music Authoring

- Import custom audio files as sound emitters
- Music layer system: ambient base + action layer + boss layer with crossfade
  triggers tied to logic events
- Footstep material override per surface (metal, wood, grass, water)

### 13.8 Weather System

Dynamic weather tied to zones or logic:
- Rain (with wet surface shader)
- Snow (with accumulation on horizontal surfaces)
- Sandstorm (reduced visibility + movement penalty)
- Triggered weather changes (starts clear, storm rolls in at 5-minute mark)

### 13.9 Minimap Auto-Generation

On save, the editor renders a top-down view of the map at multiple floors and
generates a minimap texture. Players see it in-game on the radar.

---

## 15. Implementation Phases

### Phase F0 — Foundation (estimated: 3-4 sessions)

**Goal**: Forge game mode exists. Player can toggle between Normal and Free-Fly.

- `GAMEMODE_FORGE` registered alongside existing modes
- Player state machine: NORMAL ↔ FREEFLY with transition animation
- Dr. Carroll model swap on mode change (use existing Carroll model from campaign)
- Free-fly 6DOF camera controller with speed/precision modifiers
- Collision bypass for Dr. Carroll (ghost mode)
- Basic Forge HUD overlay (mode indicator, crosshair change)
- Input bindings registered in actionmap
- Entry point: new "Forge" option in main menu → base stage select → load

**Files**: `src/game/forgemode.c`, `src/include/game/forgemode.h`,
`port/fast3d/pdgui_menu_forge.cpp`, `port/fast3d/pdgui_forge_hud.cpp`

### Phase F1 — Object Catalog & Placement (estimated: 4-5 sessions)

**Goal**: player can browse objects and place them in the world.

- Object catalog data structure (categories, entries, metadata)
- Catalog UI (left panel, collapsible tree, search, preview)
- Placement reticle (ghost object attached to camera look direction)
- Valid/invalid placement visualization (green/red tint)
- Place on confirm (A / Left-Click)
- Object spawning: create prop + collision at placement position
- Select tool: click to select, highlight outline
- Delete selected (with undo entry)
- Basic undo/redo ring buffer
- Object budget tracking and display

**Files**: `port/src/forge/forge_catalog.c`, `port/src/forge/forge_placement.c`,
`port/src/forge/forge_undo.c`, `port/fast3d/pdgui_forge_catalog.cpp`

### Phase F2 — Gizmo & Properties (estimated: 3-4 sessions)

**Goal**: placed objects can be moved, rotated, scaled, and configured.

- 3-axis gizmo renderer (translate/rotate/scale handles)
- Gizmo interaction (drag on axis, snap to grid)
- Multi-select and group operations
- Property editor panel (right-side ImGui)
- Universal properties (position, rotation, scale, name, collision mode)
- Visual properties (material, tint, emissive)
- Per-type properties (weapon pad, spawn point, interactable)
- Duplicate, align, distribute operations
- Prefab save/load (selected group → catalog entry)

**Files**: `port/src/forge/forge_gizmo.c`, `port/fast3d/pdgui_forge_props.cpp`,
`port/src/forge/forge_prefab.c`

### Phase F3 — Save / Load / Base Stage (estimated: 2-3 sessions)

**Goal**: maps save as mods and can be loaded, shared, and selected for matches.

- `map.json` serialization (write + read)
- Save to `mods/Forge Maps/<name>/` with mod.json manifest
- Auto-save on timer
- Load from mod catalog
- Base stage selection (existing maps + Paradox template + blank canvas)
- Forge overlay apply: spawn objects from map.json onto loaded base stage
- Thumbnail capture (render to texture → save as TGA)
- Forge maps appear in Combat Simulator map selection
- Network sharing via existing mod transfer pipeline

**Files**: `port/src/forge/forge_serialize.c`, `port/src/forge/forge_mapload.c`,
integration with `port/src/modmgr.c`

### Phase F4 — Zones & Lighting (estimated: 3-4 sessions)

**Goal**: trigger volumes, gameplay zones, and placed lights work.

- Zone volume rendering (wireframe + tinted fill in Forge mode)
- Zone types: trigger, radiation, gravity, teleporter, kill, water, sound, fog
- Zone properties in the property editor
- Zone runtime: enter/exit callbacks, per-frame effects
- Point light, spotlight, area light placement
- Light property editor (color, intensity, range, cone, shadow, cookie)
- Skylight adjustment (direction, color, intensity)
- Atmosphere settings panel
- Sky selection from catalog

**Files**: `port/src/forge/forge_zones.c`, `port/src/forge/forge_lighting.c`,
`port/fast3d/pdgui_forge_lighting.cpp`

### Phase F5 — Logic System (estimated: 4-5 sessions)

**Goal**: events, conditions, and actions can be wired to create gameplay rules.

- Logic graph data structure (nodes, ports, wires, channels)
- Event nodes (zone enter, switch, kill, timer, round start, etc.)
- Condition nodes (has item, kill count, switch state, etc.)
- Action nodes (open door, spawn, sound, message, teleport, etc.)
- Channel system (named global booleans)
- Wire visualization in 3D (Forge mode)
- Node editor panel (draggable boxes, typed ports, colored wires)
- Quick-link (right-click → "Link to..." → click target)
- Logic execution runtime (event → condition → action pipeline)
- Circular dependency detection and warning on save
- State reset on round restart

**Files**: `port/src/forge/forge_logic.c`, `port/src/forge/forge_logic_exec.c`,
`port/fast3d/pdgui_forge_logic.cpp`

### Phase F5b — Custom Game Types (estimated: 3-4 sessions)

**Goal**: players can author and share custom game types (wave PvE, boss fights,
custom competitive modes).

- Game type definition schema (`gametype.json`)
- Wave spawner logic node (enemy type, count, scale, health, spawn zone, escalation)
- Boss chr tag: boss health bar HUD, phase thresholds, death event
- Role assignment system (VIP, Juggernaut, Infected — per-player stat modifiers)
- Score rule configuration (custom scoring events + win conditions)
- Round/phase structure (sequential phases with transition triggers)
- Custom HUD elements (wave counter, boss bar, survival timer, role indicator)
- Game type as independent mod (type: "forge-gametype", shareable separately from maps)
- Game type selection in Combat Simulator setup alongside stock modes
- Playtest: instant-play a game type on the current Forge map

**Files**: `port/src/forge/forge_gametype.c`, `port/src/forge/forge_wavespawner.c`,
`port/src/forge/forge_boss.c`, `port/fast3d/pdgui_forge_gametype.cpp`,
`port/fast3d/pdgui_forge_bossbar.cpp`

### Phase F6 — Interactables & AI (estimated: 3-4 sessions)

**Goal**: doors, elevators, switches, and AI characters work in Forge maps.

- Door objects with open/close animation, locked state, key requirement
- Elevator objects with multi-stop movement
- Switch objects with activation types
- Destructible objects (health, break effect, on-destroy event)
- Chr placement with AI behavior presets
- Patrol path: linked waypoint nodes
- AI navigation mesh generation (auto from collision geometry)
- AI runtime in Forge maps (same as campaign AI, just data-driven placement)

**Files**: `port/src/forge/forge_interactables.c`, `port/src/forge/forge_ai.c`,
`port/src/forge/forge_navmesh.c`

### Phase F7 — Mission Authoring (estimated: 3-4 sessions)

**Goal**: Forge maps can define playable missions with objectives.

- Objective nodes in logic editor (primary/secondary/bonus)
- Objective HUD display (reuse existing mission HUD)
- Briefing text editor
- Difficulty scaling via logic branches
- Mission save as campaign-compatible mod
- Solo and co-op mission flow (start → objectives → complete/fail → endscreen)

**Files**: `port/src/forge/forge_mission.c`, `port/fast3d/pdgui_forge_mission.cpp`

### Phase F8 — Polish & Stretch Goals (ongoing)

- Terrain brushes
- Co-op editing
- Blueprint sharing
- Custom game modes
- Weather, physics, sound authoring
- Community features (browse, rate, featured maps)

---

## 16. File Inventory

New files this feature will create:

```
src/game/forgemode.c          — Forge game mode state machine
src/include/game/forgemode.h  — public API

port/src/forge/                — Forge subsystem directory
  forge_catalog.c             — object catalog data + queries
  forge_placement.c           — placement reticle + spawning
  forge_gizmo.c               — translate/rotate/scale gizmo
  forge_undo.c                — undo/redo ring buffer
  forge_prefab.c              — prefab save/load
  forge_serialize.c           — map.json read/write
  forge_mapload.c             — overlay apply (base stage + forge objects)
  forge_zones.c               — zone volumes + runtime effects
  forge_lighting.c            — placed lights + sky + atmosphere
  forge_logic.c               — logic graph data structure
  forge_logic_exec.c          — logic execution runtime
  forge_interactables.c       — doors, elevators, switches
  forge_ai.c                  — AI placement + patrol paths
  forge_navmesh.c             — nav mesh generation
  forge_mission.c             — mission/objective authoring
  forge_gametype.c            — custom game type definitions + runtime
  forge_wavespawner.c         — wave-based PvE spawner
  forge_boss.c                — boss chr management + phase transitions

port/include/forge/            — Forge headers
  forge_catalog.h
  forge_placement.h
  forge_gizmo.h
  forge_undo.h
  forge_serialize.h
  forge_zones.h
  forge_lighting.h
  forge_logic.h
  forge_interactables.h
  forge_ai.h
  forge_navmesh.h
  forge_mission.h
  forge_gametype.h
  forge_wavespawner.h
  forge_boss.h

port/fast3d/                   — ImGui renderers
  pdgui_menu_forge.cpp        — Forge main menu + base stage select
  pdgui_forge_hud.cpp         — Forge mode HUD overlay
  pdgui_forge_catalog.cpp     — object catalog browser UI
  pdgui_forge_props.cpp       — property editor panel
  pdgui_forge_lighting.cpp    — lighting/atmosphere settings UI
  pdgui_forge_logic.cpp       — node editor + wire visualization
  pdgui_forge_mission.cpp     — mission/objective editor UI
  pdgui_forge_gametype.cpp    — game type editor UI
  pdgui_forge_bossbar.cpp     — boss health bar HUD renderer
```

---

## 17. Open Questions

1. **Base geometry editing**: should Forge allow modifying the base stage's
   geometry (moving walls, deleting rooms)? Or strictly additive (place objects
   on top of existing geometry)? Additive is dramatically simpler and avoids
   BSP/room structure issues.

2. **Room structure**: PD2 uses a room-based visibility system. Forge-placed
   structures that create enclosed spaces would ideally define new rooms for
   performance. How deep do we go? Options: ignore rooms (rely on distance
   culling), auto-detect enclosures, or manual room tagging.

3. **Collision mesh generation**: placed structures need collision. Use the
   visual mesh as collision (simple but expensive)? Pre-built collision hulls
   per catalog object (faster but more authoring)? Probably pre-built hulls
   with the visual mesh as fallback.

4. **Lighting model**: the current renderer is display-list-based (N64 GBI →
   OpenGL). Placed dynamic lights would need a forward-lighting pass bolted on.
   How much lighting fidelity is worth the engineering cost? Options: vertex
   lighting (cheap, chunky), per-pixel forward (moderate), deferred (expensive
   but scalable).

5. **Logic complexity ceiling**: how powerful should the logic system be before
   we say "write a mod in C"? The node graph should be approachable for casual
   creators but expressive enough for complex missions. The channel + condition
   model is a good middle ground — it's not a general programming language but
   it covers most game design patterns.

6. **Mod interaction**: can Forge maps use objects from other mods (custom
   weapons, custom chrs from the Skin Editor)? The catalog system supports this
   natively — Forge just needs to browse the full catalog including mod entries.

7. **Performance budget**: with 1024 objects, 64 lights, and a logic graph, what's
   the floor hardware spec? The N64-era base geometry is trivially cheap on modern
   hardware, leaving plenty of headroom for Forge objects.

---

*This document is the starting plan. Each phase will get its own detailed design
as implementation begins. The phases are ordered by dependency — each builds on
the previous — but some can be parallelized (F4 lighting doesn't depend on F3
save/load being polished).*
