# Settings control coverage, Sep27

Owned by T-MENUS-003/005. Source trace only unless a receipt is explicitly
named. Main Menu Settings and the CI redirect share the renderer in
`port/fast3d/pdgui_menu_mainmenu.cpp`. Runtime, scaled rendering and physical
device acceptance remain open for every row below.

## Shared behavior

Settings has Video, Interface, Audio, Controls, Game, Updates and Catalog tabs;
Debug is a development-build addition outside player-facing acceptance.
Tab actions use current action-map bindings and are gated during binding
capture. The scrolling tab body and root capture/save-departure popups have
separate ownership. Save departure offers Stay, Retry and leave, and Leave
without saving; Back chooses Stay. Save failures stop automatic retry.
Shared numeric widgets use native controller adjustment and temporary text
entry; actual Done/Cancel/typing/restart journeys remain unverified.

## Video, in render order

| Control | Condition and effect | Persistence finding |
|---|---|---|
| Fullscreen | Checkbox; videoSetFullscreen | Checked machine-save request wired; validation pending |
| Fullscreen Mode | Fullscreen only; Borderless/Exclusive | Checked machine-save request wired; validation pending |
| Resolution | Display-mode list; disabled in borderless fullscreen | Checked machine-save request wired; validation pending |
| UI Scale | Slider 50–200 percent | Checked machine-save request wired; validation pending |
| Center Window | Checkbox | Checked machine-save request wired; validation pending |
| Maximize Window | Windowed only | Checked machine-save request wired; validation pending |
| VSync | Adaptive/Off/On | Checked machine-save request wired; validation pending |
| Framerate Limit | Slider 0–480; zero means Off | Checked machine-save request wired; validation pending |
| Uncap Tickrate | Checkbox; g_TickRateDiv | Checked machine-save request wired; validation pending |
| Display FPS | Checkbox | Checked machine-save request wired; validation pending |
| Anti-aliasing | Off/2x/4x/8x/16x; restart label | Checked machine-save request; restart proof open |
| Texture Filtering | Nearest/Bilinear/Three Point | Checked machine-save request wired; validation pending |
| GUI Texture Filtering | Checkbox | Checked machine-save request wired; validation pending |
| Detail Textures | Checkbox | Checked machine-save request wired; validation pending |
| CRT Filter | Checkbox | Agent scanlines snapshot exists |
| CRT Strength | CRT enabled; 0–100 percent | Agent scanline_alpha snapshot exists |
| CRT Line Spacing | CRT enabled; 0.25–4.0 | Checked machine-save request wired; validation pending |
| HUD Centering | None/4:3/Wide | Agent snapshot (checked Agent-save poll) |
| GE64-style Muzzle Flashes | Checkbox | Agent snapshot (checked Agent-save poll) |
| Explosion Shake | Slider 0–2 | Agent snapshot (checked Agent-save poll) |
| Screen Size | Full/Wide/Cinema | Explicit Agent dirty trigger; runtime proof pending |
| 2-Player Screen Split | Horizontal/Vertical | Removed from reachable Video controls |

Video machine edits now explicitly request the shared checked machine save. Agent
visual preferences remain on the existing Agent snapshot channel. The shutdown
configSave is not used as Settings acceptance evidence. Failure, restart and
recovery journeys remain required. Screen Size and Sound Mode live in the
legacy gamefile state, outside prefsAgentSave's change-comparison snapshot;
their edits now invalidate the preference-only save cache, preserving dirty
state on writer failure and resetting it on successful save/Agent publication.
Compilation and end-to-end persistence of this follow-up remain unverified.

## Audio, in render order

| Control | Effect | Persistence finding |
|---|---|---|
| Master Volume | 0–100 percent; audioSetMasterVolume | Agent snapshot |
| Music | 0–100 percent; audioSetMusicVolume | Agent snapshot |
| Gameplay | 0–100 percent; audioSetGameplayVolume | Agent snapshot |
| UI | 0–100 percent; audioSetUiVolume | Agent snapshot |
| Sound Mode | Mono/Stereo/Headphone/Surround | Explicit Agent dirty trigger; runtime proof pending |
| Disable MP Death Music | Checkbox | Agent snapshot |

## Controls capture follow-up

All four binding slots have an existing source contract. Listening capture
previews raw input before Apply and preserves bindable keyboard Escape. A
disconnected controller could leave listening capture owning keyboard input.
Candidate now records the initial observed SDL instance and cancels if its
existing handle disappears, or if no controller/raw devices remain. Unknown
raw identities remain eligible while a device is connected. The legacy
inputControllerConnected(0) mask cannot be used: it keeps player0 connected
for keyboard input. A production-linked SDL virtual-handle regression is
passing in the frozen asset0923lang build: 235 cases / 9765 assertions,
zero drift across 2010 source hashes (.claude/menu0927/capture-broad.xml).
Ordinary unplug/reconnect remains pending; the later Video edits are not
covered by that receipt.

Interface, full Controls inventory, Game, Updates and Catalog require further
row-level tracing. Existing node/dialog matrices remain the scope index;
this audit does not replace them or imply complete Settings coverage.

## Propagation findings and remaining persistence gap

Game FOV, crosshair sway/size/colour/health, Use Key Reloads and Jump Height
now request checked machine saves, as do global mouse sensitivity, crosshair
speed, aim lock, mouse lock and enable controls. The existing Agent snapshot
already includes Disable MP Death Music, HUD centering, GE muzzle flashes,
shake, Skip Intro and Show Dev Releases.

**Sep29 source repair, unvalidated:** indexed legacy options in Settings use config index 0, while
ordinary solo gamefile capture/apply uses config index 4 (co-op/CounterOp use
0). The runtime reads currentplayerstats->mpindex. A save trigger alone does
not correct this domain mismatch. Trace and repair the local-player option
adapter across solo and multiplayer with production-linked cases before
claiming HUD, aim or mouse inversion persistence. Do not rewrite save flags
or revive a second local player to solve it. This gap is separate from the
now-explicit global Screen Size, Sound Mode and subtitle save requests.

## Sep29 resume

The seven paused source/test hashes match the checkpoint. The persistence
mismatch is confirmed against lvReset: solo already uses runtime slot0.
Current gamefile apply/default/capture and unified Agent capture/commit now
use local slot0 for P1. The obsolete menutick co-op restoration (0/4 and1/5
swaps, without a corresponding mpInit swap) is removed. Save field layout and
retired secondary payload handling are unchanged. The existing static menu
suite pins these production callers; integrated compile, save/restart and
co-op return journeys remain required. Do not attribute the Sep27 capture
receipt to this source follow-up.

The old Agent805E2213 binary and its1916-file snapshot are hash-verified for a
separate ordinary virtual-controller retry. That run is historical-source
Agent evidence only, not acceptance of the new Settings/save changes.

## Additional current control inventory (Sep29 source trace)

All rows below retain OPEN ordinary/controller/physical/scaled acceptance.
The sequence is source submission order; native directional focus must still
be observed. Controls sections are collapsible inside the shared scroll body.

| Surface | Controls in order | Save or transition |
|---|---|---|
| Interface | Per-theme Use/Selected/Disabled; custom Actions with Enable/Disable/Delete | Theme selection requests checked machine save and Agent poll; delete confirmation/status handles Retry refresh |
| Interface | Open Color Editor; Menu Style; Open Menu Style Tool; Title Bar Style; Font; Open Font Mod Tool | Native buttons/combos; creator launch transfers ownership; style/title/font request checked save |
| Mouse | Sensitivity X/Y; Crosshair Speed X/Y; Invert Y; Aim Lock; Lock Mode; Enabled; Menu Navigation | Machine requests added; inversion marks Agent dirty; menu navigation already captured in Agent preferences |
| Sticks | Move stick; Look stick; Move sensitivity/deadzone; Look sensitivity; ADS sensitivity; Look deadzone; Invert look | Existing explicit machine-save requests; mapping/layout applies live |
| Game | Crouch Mode; Vertical FOV; Crosshair Sway/Size/Colour/Colour by Health; Skip Intro; Use Key Reloads; Jump Height; Show Dev Releases | Machine-backed controls now request checked save, including newly found Crouch Mode; Agent poll covers Skip Intro and release preference |
| Game HUD | Sight; Ammo; Gun Function; Always Show Target; Zoom Range; Mission Time; Head Roll | Native checkboxes mark Agent dirty; P1 capture/apply now uses local slot0 |
| Game subtitles/effects | In-Game Subtitles; Cutscene Subtitles; Paintball | Agent dirty request |
| Game combat | Aim Mode; Auto Aim; Look Ahead | Agent dirty request; local slot0 |

Profiles, all dynamic binding/hold rows, Updates and Catalog still require
full control-by-control reconciliation. This table does not close them.

## Sep29 validation boundary

Sep29 T-MENUS-005 Settings/input candidate passes shared isolated client/tests
builds, broad238 cases/9829 assertions and real settings-save11/123, with3405
source paths unchanged. Full suite remains red18/140987 across1853 cases;
the18 failure names match the prior receipt. Capture loss, checked Video/Game/
mouse saves, Agent dirty invalidation, retired split removal, local P1 save/load
slot0 and obsolete co-op swap removal are connected. Ordinary save/restart/
return, physical controller and scaled acceptance remain open. Artifacts:
.claude/menu0927/sep29-validation.json and sep29-full-result.json. Frozen older
Agent805E2213 virtual run now attaches player0 with process-local SDL native
driver exclusion plus explicit touchpad mapping, but A does not enter Create:
red15/29, results-20260930T015959Z.json. It overlapped the asset session's separate
resource named game, so focus isolation is unproven; screenshot inspected.
No hardware/system assignment changed. Goal remains active and partial.
