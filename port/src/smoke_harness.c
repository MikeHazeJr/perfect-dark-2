/*
 * Smoke verify harness (Phase 1, 2026-05-11).
 *
 * See port/include/smoke_harness.h for the API contract and
 * context/designs/engine/smoke-verify-gate.md for the architecture.
 *
 * The harness operates on module-static state. It is single-instance
 * by construction: --smoke <path> sets one test definition, the runner
 * launches one client process per test.
 *
 * Implementation summary:
 *
 *   smokeHarnessInit()
 *     - scan argv for --smoke <path>
 *     - load the JSON test file from disk
 *     - tokenise + decode into s_State (events, assertions are inert at
 *       runtime; the runner re-checks them after exit)
 *     - apply log channel mask + verbose flag
 *     - record start_ticks_ms = SDL_GetTicks()
 *
 *   smokeHarnessTick()
 *     - elapsed_ms = SDL_GetTicks() - start_ticks_ms
 *     - while next event's at_ms <= elapsed_ms: dispatch event
 *     - if elapsed_ms >= timeout_ms: smokeHarnessExit(1, "timeout")
 *
 *   smokeHarnessExit(code, reason)
 *     - log "SMOKE: result=<reason> elapsed_ms=<N> events_fired=<M>"
 *     - exit(code)
 *
 * Event schema (input_sequence[] in the JSON test file):
 *
 *   { "at_ms": N, "type": "wait" }
 *     -- inert marker; useful for sequencing comments / dwell.
 *
 *   { "at_ms": N, "type": "wait_until",
 *     "condition": "network_stage_live", "timeout_ms": 120000 }
 *     -- pause this process's script timeline until a typed production-state
 *        condition is true. Supported conditions are network_listen_ready,
 *        network_stage_live, network_reconnect_available,
 *        cutscene_skip_ready, and gameplay_ready. The real-time harness
 *        watchdog continues while the script is paused, and every wait
 *        requires a bounded timeout.
 *
 *   { "at_ms": N, "type": "wait_until", "condition": "gameplay_ready",
 *     "timeout_ms": 60000, "stable_ms": 3000,
 *     "assist_action": "ACTION_SKIP_CUTSCENE",
 *     "assist_condition": "cutscene_skip_ready", "assist_hold_ms": 900 }
 *     -- require continuous target readiness while optionally issuing exactly
 *        one bounded production action through a separate typed aperture. The
 *        assist releases on hold expiry and every terminal harness path.
 *
 *   { "at_ms": N, "type": "exit" }
 *     -- scripted clean exit; produces SMOKE: result=scripted_exit.
 *
 *   { "at_ms": N, "type": "unclean_exit" }
 *     -- validation-only process exit after flushing the log, deliberately
 *        skipping atexit so startup crash recovery sees the dirty session.
 *
 *   { "at_ms": N, "type": "network_timeout_client", "client_id": 1 }
 *   { "at_ms": N, "type": "network_reconnect" }
 *     -- deterministic smoke-only triggers around the production timeout and
 *        reconnect APIs. They do not replace ENet teardown/auth/stage flow.
 *
 *   { "at_ms": N, "type": "key", "key": "Return", "action": "tap" }
 *     -- inject SDL_KEYDOWN / KEYUP for a named key or numeric scancode.
 *        `action` may be "tap" (auto-release one frame later), "press"
 *        (no auto-release) or "release". `tap` is the default.
 *
 *   { "at_ms": N, "type": "action",
 *     "name": "ACTION_MENU_ACCEPT", "action": "tap" }
 *     -- inject an action-map press/release edge directly via
 *        actionmapInjectStateForSmoke(). Bypasses SDL events and the
 *        ImGui focus gate. Deterministic, focus-independent. Use this
 *        when the SDL window cannot reliably hold focus (firewall prompt,
 *        OS modal, alt-tab to a different process). `name` accepts the
 *        full ACTION_* enum identifier or its CamelCase short form
 *        (e.g. "Use", "MenuUp"). `action` may be "tap" / "press" /
 *        "release"; `tap` is the default.
 *
 *   { "at_ms": N, "type": "mouse",
 *     "x": 640, "y": 400, "button": "left", "action": "click" }
 *     -- synthesise an SDL_MOUSEBUTTONDOWN / UP pair at {x, y} for
 *        "left" / "right" / "middle". Needed for genuinely-mouse-only
 *        UIs (Settings -> Debug -> Test Scenarios radios, modding hub
 *        file pickers, skin editor canvas). `action` may be "tap" /
 *        "click" / "press" / "release"; `tap` and `click` are aliases
 *        and produce an auto-release on the next tick. `tap` is the
 *        default.
 *
 *   { "at_ms": N, "type": "mouse_move", "x": 640, "y": 400 }
 *     -- inject SDL_MOUSEMOTION at an absolute client coordinate. Use this
 *        before a mouse click or wheel event so ImGui's hover/focus target is
 *        established through the same SDL backend used in ordinary play.
 *
 *   { "at_ms": N, "type": "mouse_wheel", "wheel_x": 0, "wheel_y": -1 }
 *     -- inject SDL_MOUSEWHEEL through the ordinary ImGui/action-map event
 *        path. The current hover target comes from the preceding mouse_move.
 *
 *   { "at_ms": N, "type": "receive_pdca_list", "path": "social/test/list.txt" }
 *     -- deliver raw PDCA fixtures through the production network
 *        BEGIN/CHUNK/END receive handlers at a deterministic live point.
 *        The event exists only while the smoke harness is active.
 *
 *   { "at_ms": N, "type": "agent_activate", "name": "Agent" }
 *   { "at_ms": N, "type": "agent_delete", "name": "Agent" }
 *     -- invoke the production Agent Session boundary from an ordinary client
 *        so failure rollback and active-delete rules can be observed in one
 *        live process.
 *
 *   { "at_ms": N, "type": "catalog_weapon_acquire", "path": "mod:id" }
 *   { "at_ms": N, "type": "catalog_weapon_release", "path": "mod:id" }
 *     -- acquire or release one explicit owner through the production typed
 *        weapon lifecycle. These smoke-only events make overlapping-owner
 *        balance observable without replacing the lifecycle implementation.
 *
 *   Both `action` and `mouse` events use the same tap-release sweep
 *   path that the existing key-tap events use, so they share the
 *   one-frame auto-release timing (SMOKE_TAP_RELEASE_MS).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <SDL.h>

#include <PR/ultratypes.h>

#include "bss.h"
#include "data.h"
#include "smoke_fixture_schema.h"
#include "smoke_harness.h"
#include "smoke_readiness.h"
#include "smoke_transition.h"
#include "system.h"
#include "actionmap.h"   /* actionmapResolveByName, actionmapInjectStateForSmoke */
#include "assetcatalog.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_load.h"
#include "assetprovider.h"
#include "asset_runtime.h"
#include "net/netdistrib.h"
#include "net/net.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "inputctx.h"
#include "menupool.h"
#include "scene.h"
#include "agent_session.h"
#include "prefs_agent.h"

/* c115 (2026-05-14): need gfxGetSdlWindow to stamp the correct windowID
 * on synthesised SDL events. ImGui's SDL2 backend filters events whose
 * windowID does not match the window captured at ImGui_ImplSDL2_Init
 * time -- with windowID=0 the synthesised keys reach the SDL queue but
 * ImGui drops them on the floor, so menu nav never advances. */
#include "../fast3d/gfx_sdl.h"

/* Use the port-level config registration so harness-set values can be
 * queried by other subsystems if needed.  No new pd.ini keys are
 * registered -- the harness reads its config from the test JSON. */
#include "config.h"

/* ---------------------------------------------------------------- */
/* Constants                                                        */
/* ---------------------------------------------------------------- */

#define SMOKE_MAX_EVENTS         512
#define SMOKE_MAX_PATH           1024
#define SMOKE_MAX_NAME           96
#define SMOKE_DEFAULT_TIMEOUT_MS 90000
#define SMOKE_MAX_WAIT_TIMEOUT_MS 3600000
#define SMOKE_MAX_EVENT_AT_MS    3600000
#define SMOKE_TAP_RELEASE_MS     16   /* one 60Hz frame */

typedef enum {
    SMOKE_EVENT_NONE = 0,
    SMOKE_EVENT_WAIT,
    SMOKE_EVENT_WAIT_UNTIL,
    SMOKE_EVENT_KEY_PRESS,
    SMOKE_EVENT_KEY_RELEASE,
    SMOKE_EVENT_KEY_TAP,
    SMOKE_EVENT_ACTION_PRESS,
    SMOKE_EVENT_ACTION_RELEASE,
    SMOKE_EVENT_ACTION_TAP,
    SMOKE_EVENT_MOUSE_PRESS,
    SMOKE_EVENT_MOUSE_RELEASE,
    SMOKE_EVENT_MOUSE_TAP,
    SMOKE_EVENT_MOUSE_MOVE,
    SMOKE_EVENT_MOUSE_WHEEL,
    SMOKE_EVENT_RECEIVE_PDCA_LIST,
    SMOKE_EVENT_AGENT_ACTIVATE,
    SMOKE_EVENT_AGENT_DELETE,
    SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE,
    SMOKE_EVENT_CATALOG_WEAPON_RELEASE,
    SMOKE_EVENT_CATALOG_RECOVERY_PROBE,
    SMOKE_EVENT_NETWORK_TIMEOUT_CLIENT,
    SMOKE_EVENT_NETWORK_RECONNECT,
    SMOKE_EVENT_SCREENSHOT,         /* in-game glReadPixels grab -> path */
    SMOKE_EVENT_EXIT,
    SMOKE_EVENT_UNCLEAN_EXIT
} SmokeEventType;

#define SMOKE_MAX_SHOTPATH 256

typedef struct {
    s32            at_ms;
    SmokeEventType type;
    s32            scancode;          /* for key events */
    s32            action_id;         /* for action events: resolved InputAction */
    s32            mouse_x;           /* for mouse events */
    s32            mouse_y;           /* for mouse events */
    s32            mouse_button;      /* for mouse events: SDL_BUTTON_LEFT/RIGHT/MIDDLE */
    s32            mouse_wheel_x;     /* for mouse-wheel events */
    s32            mouse_wheel_y;     /* for mouse-wheel events */
    s32            client_id;         /* network_timeout_client target */
    s32            release_at_ms;     /* for tap: scheduled release time */
    s32            released;          /* tap: 0 = press pending, 1 = release pending, 2 = done */
    smoke_readiness_condition_t readiness_condition;
    u32            wait_timeout_ms;
    u32            stable_ms;
    s32            assist_action_id;
    smoke_readiness_condition_t assist_condition;
    u32            assist_hold_ms;
    char           path[SMOKE_MAX_SHOTPATH]; /* for screenshot events: output file */
} SmokeEvent;

typedef struct {
    s32          active;
    char         test_path[SMOKE_MAX_PATH];
    char         screenshot_dir[SMOKE_MAX_PATH]; /* --smoke-screenshot-dir: glReadPixels output dir */
    char         scenario_name[SMOKE_MAX_NAME];
    s32          timeout_ms;
    u32          channel_mask;       /* applied via sysLogSetChannelMask */
    s32          verbose;
    s32          jump_logging;       /* test opt-in: enable JUMP:/CAPSULE: markers */
    SmokeEvent   events[SMOKE_MAX_EVENTS];
    s32          event_count;
    s32          next_event_idx;
    u32          start_ticks_ms;
    u32          timeline_pause_ms;
    u32          wait_started_ticks_ms;
    s32          waiting_event_idx;
    smoke_transition_state_t wait_transition;
    s32          wait_assist_injected;
	s32          wait_assist_press_count;
	s32          wait_assist_release_count;
    s32          observed_stage_ready_count;
    s32          observed_stage_teardown_count;
    s32          ready_stage_num;
    u32          ready_stage_generation;
    s32          events_fired;
    s32          exited;             /* prevent re-entrant exit */
} SmokeState;

static SmokeState s_State;

/* ---------------------------------------------------------------- */
/* Minimal JSON tokenizer (mirrors modmgr.c style, kept local to    */
/* avoid coupling with mod loader internals).                       */
/* ---------------------------------------------------------------- */

typedef enum {
    JT_NONE = 0, JT_LBRACE, JT_RBRACE, JT_LBRACKET, JT_RBRACKET,
    JT_COLON, JT_COMMA, JT_STRING, JT_NUMBER, JT_TRUE, JT_FALSE, JT_NULL,
    JT_EOF, JT_ERROR
} JTokType;

typedef struct {
    const char *start;
    s32         len;
    JTokType    type;
} JTok;

typedef struct {
    const char *src;
    const char *pos;
    JTok        cur;
} JParse;

static void j_skip_ws(JParse *p)
{
    while (*p->pos) {
        char c = *p->pos;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
            continue;
        }
        /* skip // line comments for authoring convenience */
        if (c == '/' && p->pos[1] == '/') {
            while (*p->pos && *p->pos != '\n') p->pos++;
            continue;
        }
        break;
    }
}

static JTok j_next(JParse *p)
{
    JTok t = { NULL, 0, JT_NONE };
    j_skip_ws(p);
    if (!*p->pos) { t.type = JT_EOF; p->cur = t; return t; }
    t.start = p->pos;
    char c = *p->pos;
    switch (c) {
    case '{': t.type = JT_LBRACE;   t.len = 1; p->pos++; break;
    case '}': t.type = JT_RBRACE;   t.len = 1; p->pos++; break;
    case '[': t.type = JT_LBRACKET; t.len = 1; p->pos++; break;
    case ']': t.type = JT_RBRACKET; t.len = 1; p->pos++; break;
    case ':': t.type = JT_COLON;    t.len = 1; p->pos++; break;
    case ',': t.type = JT_COMMA;    t.len = 1; p->pos++; break;
    case '"': {
        p->pos++;
        t.start = p->pos;
        while (*p->pos && *p->pos != '"') {
            if (*p->pos == '\\' && p->pos[1]) p->pos++;
            p->pos++;
        }
        t.len = (s32)(p->pos - t.start);
        t.type = JT_STRING;
        if (*p->pos == '"') p->pos++;
        break;
    }
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            if (c == '0' && (p->pos[1] == 'x' || p->pos[1] == 'X')) {
                p->pos += 2;
                while (isxdigit((unsigned char)*p->pos)) p->pos++;
            } else {
                if (c == '-') p->pos++;
                while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
                if (*p->pos == '.') {
                    p->pos++;
                    while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
                }
				if (*p->pos == 'e' || *p->pos == 'E') {
					p->pos++;
					if (*p->pos == '+' || *p->pos == '-') p->pos++;
					while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
				}
            }
            t.len = (s32)(p->pos - t.start);
            t.type = JT_NUMBER;
        } else if (!strncmp(p->pos, "true", 4))  { t.type = JT_TRUE;  t.len = 4; p->pos += 4; }
        else if (!strncmp(p->pos, "false", 5)) { t.type = JT_FALSE; t.len = 5; p->pos += 5; }
        else if (!strncmp(p->pos, "null", 4))  { t.type = JT_NULL;  t.len = 4; p->pos += 4; }
        else { t.type = JT_ERROR; p->pos++; }
        break;
    }
    p->cur = t;
    return t;
}

static int j_tok_str_eq(const JTok *t, const char *s)
{
    if (t->type != JT_STRING) return 0;
    s32 sl = (s32)strlen(s);
    if (sl != t->len) return 0;
    return memcmp(t->start, s, sl) == 0;
}

static void j_tok_copy_str(const JTok *t, char *dst, s32 dstsz)
{
    if (t->type != JT_STRING || dstsz <= 0) { if (dstsz > 0) dst[0] = '\0'; return; }
    s32 n = t->len < (dstsz - 1) ? t->len : (dstsz - 1);
    memcpy(dst, t->start, n);
    dst[n] = '\0';
}

static s64 j_tok_int(const JTok *t)
{
    if (t->type != JT_NUMBER || !t->start) return 0;
    return (s64)strtoll(t->start, NULL, 0);
}

static int j_tok_is_integer(const JTok *t)
{
    s32 i = 0;

    if (!t || t->type != JT_NUMBER || !t->start || t->len <= 0) {
        return 0;
    }
    if (t->start[i] == '-') {
        i++;
    }
    if (i >= t->len) {
        return 0;
    }
    if (i + 2 <= t->len && t->start[i] == '0'
            && (t->start[i + 1] == 'x' || t->start[i + 1] == 'X')) {
        i += 2;
        if (i >= t->len) {
            return 0;
        }
        for (; i < t->len; i++) {
            if (!isxdigit((unsigned char)t->start[i])) {
                return 0;
            }
        }
        return 1;
    }
    for (; i < t->len; i++) {
        if (!isdigit((unsigned char)t->start[i])) {
            return 0;
        }
    }
    return 1;
}

/* Skip the value (possibly nested) at the current parse position. */
static void j_skip_value(JParse *p)
{
    JTok t = p->cur;
    if (t.type == JT_LBRACE) {
        s32 depth = 1;
        while (depth > 0) {
            t = j_next(p);
            if (t.type == JT_EOF || t.type == JT_ERROR) return;
            if (t.type == JT_LBRACE) depth++;
            if (t.type == JT_RBRACE) depth--;
        }
    } else if (t.type == JT_LBRACKET) {
        s32 depth = 1;
        while (depth > 0) {
            t = j_next(p);
            if (t.type == JT_EOF || t.type == JT_ERROR) return;
            if (t.type == JT_LBRACKET) depth++;
            if (t.type == JT_RBRACKET) depth--;
        }
    }
    /* scalars consumed already by the caller's j_next */
}

/* ---------------------------------------------------------------- */
/* Named keys -> SDL scancode                                       */
/* ---------------------------------------------------------------- */

typedef struct {
    const char *name;
    s32         scancode;
} SmokeKeyEntry;

static const SmokeKeyEntry s_KeyTable[] = {
    { "Return",      SDL_SCANCODE_RETURN     },
    { "Enter",       SDL_SCANCODE_RETURN     },
    { "Escape",      SDL_SCANCODE_ESCAPE     },
    { "Esc",         SDL_SCANCODE_ESCAPE     },
    { "Tab",         SDL_SCANCODE_TAB        },
    { "Space",       SDL_SCANCODE_SPACE      },
    { "Backspace",   SDL_SCANCODE_BACKSPACE  },
    { "Up",          SDL_SCANCODE_UP         },
    { "Down",        SDL_SCANCODE_DOWN       },
    { "Left",        SDL_SCANCODE_LEFT       },
    { "Right",       SDL_SCANCODE_RIGHT      },
    { "LShift",      SDL_SCANCODE_LSHIFT     },
    { "RShift",      SDL_SCANCODE_RSHIFT     },
    { "LCtrl",       SDL_SCANCODE_LCTRL      },
    { "RCtrl",       SDL_SCANCODE_RCTRL      },
    { "LAlt",        SDL_SCANCODE_LALT       },
    { "RAlt",        SDL_SCANCODE_RALT       },
    { "F1",          SDL_SCANCODE_F1         },
    { "F2",          SDL_SCANCODE_F2         },
    { "F3",          SDL_SCANCODE_F3         },
    { "F4",          SDL_SCANCODE_F4         },
    { "F5",          SDL_SCANCODE_F5         },
    { "F6",          SDL_SCANCODE_F6         },
    { "F7",          SDL_SCANCODE_F7         },
    { "F8",          SDL_SCANCODE_F8         },
    { "F9",          SDL_SCANCODE_F9         },
    { "F10",         SDL_SCANCODE_F10        },
    { "F11",         SDL_SCANCODE_F11        },
    { "F12",         SDL_SCANCODE_F12        },
};

static s32 s_KeyTableLen = (s32)(sizeof(s_KeyTable) / sizeof(s_KeyTable[0]));

static s32 smokeKeyNameToScancode(const char *name)
{
    if (!name) return 0;

    /* single letter: A-Z (case insensitive) */
    if (name[0] && !name[1]) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') {
            return SDL_SCANCODE_A + (c - 'A');
        }
        if (c >= '1' && c <= '9') return SDL_SCANCODE_1 + (c - '1');
        if (c == '0') return SDL_SCANCODE_0;
    }

    for (s32 i = 0; i < s_KeyTableLen; i++) {
        if (!strcasecmp(s_KeyTable[i].name, name)) {
            return s_KeyTable[i].scancode;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Channel mask parser                                              */
/* ---------------------------------------------------------------- */

static u32 smokeParseChannelMask(const JTok *t)
{
	if (t->type == JT_STRING) {
		char tmp[128];
		j_tok_copy_str(t, tmp, sizeof(tmp));
		if (!strcasecmp(tmp, "all"))  return 0xFFFFu;
		if (!strcasecmp(tmp, "none")) return 0x0000u;

		char *end = NULL;
		u32 numeric = (u32)strtoul(tmp, &end, 0);
		while (end && *end && isspace((unsigned char)*end)) {
			end++;
		}
		if (end && end != tmp && *end == '\0') {
			return numeric;
		}

		u32 mask = 0;
		for (char *part = strtok(tmp, ",|+; ");
		     part;
		     part = strtok(NULL, ",|+; ")) {
			u32 bit = 0;
			for (s32 i = 0; i < LOG_CH_COUNT; i++) {
				if (!strcasecmp(part, sysLogChannelNames[i])) {
					bit = sysLogChannelBits[i];
					break;
				}
			}
			if (!bit && (!strcasecmp(part, "mod") || !strcasecmp(part, "mods"))) {
				bit = LOG_CH_MOD;
			} else if (!bit && (!strcasecmp(part, "test") || !strcasecmp(part, "testscen"))) {
				bit = LOG_CH_TESTSCEN;
			}
			if (!bit) {
				sysLogPrintf(LOG_WARNING, "SMOKE: unknown log_channel_mask token '%s'", part);
				continue;
			}
			mask |= bit;
		}
		return mask;
	}
	if (t->type == JT_NUMBER) {
		return (u32)j_tok_int(t);
	}
    return 0xFFFFu;
}

/* ---------------------------------------------------------------- */
/* JSON loader                                                      */
/* ---------------------------------------------------------------- */

static char *smokeReadFile(const char *path, s32 *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        sysLogPrintf(LOG_ERROR, "SMOKE: cannot open test file '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 2 * 1024 * 1024) {
        sysLogPrintf(LOG_ERROR, "SMOKE: test file '%s' size %ld out of range", path, sz);
        fclose(f);
        return NULL;
    }
    char *buf = (char *)sysMemAlloc((u32)sz + 1);
    if (!buf) {
        sysLogPrintf(LOG_ERROR, "SMOKE: cannot alloc %ld bytes for test file", sz);
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_size) *out_size = (s32)n;
    return buf;
}

/* Resolve a mouse button name to an SDL_BUTTON_* code. */
static s32 smokeMouseButtonNameToCode(const char *name)
{
    if (!name || !name[0]) return SDL_BUTTON_LEFT;
    if (!strcasecmp(name, "left")   || !strcasecmp(name, "lmb")) return SDL_BUTTON_LEFT;
    if (!strcasecmp(name, "right")  || !strcasecmp(name, "rmb")) return SDL_BUTTON_RIGHT;
    if (!strcasecmp(name, "middle") || !strcasecmp(name, "mmb")) return SDL_BUTTON_MIDDLE;
    if (!strcasecmp(name, "x1") || !strcasecmp(name, "side1"))   return SDL_BUTTON_X1;
    if (!strcasecmp(name, "x2") || !strcasecmp(name, "side2"))   return SDL_BUTTON_X2;
    return 0;
}

static s32 smokeParseEvent(JParse *p, SmokeEvent *ev)
{
    JTok t = p->cur;
    if (t.type != JT_LBRACE) return 0;
    t = j_next(p);

    ev->at_ms = 0;
    ev->type = SMOKE_EVENT_NONE;
    ev->scancode = 0;
    ev->action_id = -1;
    ev->mouse_x = 0;
    ev->mouse_y = 0;
    ev->mouse_button = 0;
    ev->mouse_wheel_x = 0;
    ev->mouse_wheel_y = 0;
    ev->client_id = -1;
    ev->release_at_ms = -1;
    ev->released = 0;
    ev->readiness_condition = SMOKE_READINESS_INVALID;
    ev->wait_timeout_ms = 0;
    ev->stable_ms = 0;
    ev->assist_action_id = -1;
    ev->assist_condition = SMOKE_READINESS_INVALID;
    ev->assist_hold_ms = 0;

    char type_str[32]   = {0};
    char key_str[32]    = {0};
    char action_str[16] = {0};
    char name_str[64]   = {0};
    char button_str[16] = {0};
    char condition_str[32] = {0};
    char assist_action_str[64] = {0};
    char assist_condition_str[32] = {0};
    s32  scancode = 0;
    s64  at_ms_value = 0;
    s64  wait_timeout_ms = 0;
    s64  stable_ms = 0;
    s64  assist_hold_ms = 0;
    s32  has_action_mode = 0;
    s32  has_wait_timeout = 0;
    s32  has_stable_ms = 0;
    s32  has_assist_action = 0;
    s32  has_assist_condition = 0;
    s32  has_assist_hold_ms = 0;
    s32  has_x = 0;
    s32  has_y = 0;
    s32  has_wheel_x = 0;
    s32  has_wheel_y = 0;
    s32  has_client_id = 0;
	smoke_fixture_field_mask_t field_mask = 0;
	smoke_fixture_field_mask_t unsupported_fields = 0;

    while (t.type != JT_RBRACE && t.type != JT_EOF) {
        if (t.type != JT_STRING) { t = j_next(p); continue; }
        char field[32]; j_tok_copy_str(&t, field, sizeof(field));
		smoke_fixture_field_mask_t field_bit =
			smokeFixtureEventFieldFromName(field);
		if (!field_bit) {
			sysLogPrintf(LOG_ERROR,
				"SMOKE: event has unknown field '%s'", field);
			return 0;
		}
		if (field_mask & field_bit) {
			sysLogPrintf(LOG_ERROR,
				"SMOKE: event has duplicate field '%s'", field);
			return 0;
		}
		field_mask |= field_bit;
        t = j_next(p); /* colon */
        if (t.type == JT_COLON) t = j_next(p);

        if (!strcmp(field, "at_ms")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: event at_ms must be an integer");
                return 0;
            }
            at_ms_value = j_tok_int(&t);
        } else if (!strcmp(field, "type")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event type must be a string");
				return 0;
			}
            j_tok_copy_str(&t, type_str, sizeof(type_str));
        } else if (!strcmp(field, "key")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event key must be a string");
				return 0;
			}
            j_tok_copy_str(&t, key_str, sizeof(key_str));
        } else if (!strcmp(field, "scancode")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event scancode must be an integer");
				return 0;
			}
            scancode = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "action")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event action must be a string");
				return 0;
			}
            j_tok_copy_str(&t, action_str, sizeof(action_str));
            has_action_mode = 1;
        } else if (!strcmp(field, "name")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event name must be a string");
				return 0;
			}
            j_tok_copy_str(&t, name_str, sizeof(name_str));
        } else if (!strcmp(field, "condition")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: wait_until condition must be a string");
				return 0;
            }
            j_tok_copy_str(&t, condition_str, sizeof(condition_str));
        } else if (!strcmp(field, "timeout_ms")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: wait_until timeout_ms must be an integer");
                return 0;
            }
            wait_timeout_ms = j_tok_int(&t);
            has_wait_timeout = 1;
        } else if (!strcmp(field, "stable_ms")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: wait_until stable_ms must be an integer");
                return 0;
            }
            stable_ms = j_tok_int(&t);
            has_stable_ms = 1;
        } else if (!strcmp(field, "assist_action")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: wait_until assist_action must be a string");
				return 0;
			}
            j_tok_copy_str(&t, assist_action_str, sizeof(assist_action_str));
            has_assist_action = 1;
        } else if (!strcmp(field, "assist_condition")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: wait_until assist_condition must be a string");
				return 0;
			}
            j_tok_copy_str(&t, assist_condition_str,
                sizeof(assist_condition_str));
            has_assist_condition = 1;
        } else if (!strcmp(field, "assist_hold_ms")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: wait_until assist_hold_ms must be an integer");
                return 0;
            }
            assist_hold_ms = j_tok_int(&t);
            has_assist_hold_ms = 1;
        } else if (!strcmp(field, "path")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event path must be a string");
				return 0;
			}
            j_tok_copy_str(&t, ev->path, sizeof(ev->path));
        } else if (!strcmp(field, "x")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event x must be an integer");
				return 0;
			}
            ev->mouse_x = (s32)j_tok_int(&t);
            has_x = 1;
        } else if (!strcmp(field, "y")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event y must be an integer");
				return 0;
			}
            ev->mouse_y = (s32)j_tok_int(&t);
            has_y = 1;
        } else if (!strcmp(field, "button")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event button must be a string");
				return 0;
			}
            j_tok_copy_str(&t, button_str, sizeof(button_str));
        } else if (!strcmp(field, "wheel_x")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event wheel_x must be an integer");
				return 0;
			}
            ev->mouse_wheel_x = (s32)j_tok_int(&t);
            has_wheel_x = 1;
        } else if (!strcmp(field, "wheel_y")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event wheel_y must be an integer");
				return 0;
			}
            ev->mouse_wheel_y = (s32)j_tok_int(&t);
            has_wheel_y = 1;
        } else if (!strcmp(field, "client_id")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event client_id must be an integer");
				return 0;
			}
			ev->client_id = (s32)j_tok_int(&t);
			has_client_id = 1;
        } else if (!strcmp(field, "comment")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event comment must be a string");
				return 0;
			}
            j_skip_value(p);
        }

        t = j_next(p);
        if (t.type == JT_COMMA) t = j_next(p);
    }

	if (t.type != JT_RBRACE) {
		sysLogPrintf(LOG_ERROR, "SMOKE: event object is truncated");
		return 0;
	}
	if (!smokeFixtureEventTypeKnown(type_str)) {
		sysLogPrintf(LOG_ERROR, "SMOKE: unknown event type '%s'", type_str);
		return 0;
	}
	if (!smokeFixtureEventFieldsValid(type_str, field_mask,
			&unsupported_fields)) {
		smoke_fixture_field_mask_t first = unsupported_fields
			& (0u - unsupported_fields);
		sysLogPrintf(LOG_ERROR,
			"SMOKE: field '%s' is not supported for event type '%s'",
			smokeFixtureEventFieldName(first), type_str[0] ? type_str : "wait");
		return 0;
	}

    if (at_ms_value < 0 || at_ms_value > SMOKE_MAX_EVENT_AT_MS) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE: event at_ms must be in [0,%d] (value=%lld)",
            SMOKE_MAX_EVENT_AT_MS, (long long)at_ms_value);
        return 0;
    }
    ev->at_ms = (s32)at_ms_value;

    if (!strcmp(type_str, "wait") || !type_str[0]) {
        ev->type = SMOKE_EVENT_WAIT;
        return 1;
    }
    if (!strcmp(type_str, "wait_until")) {
        ev->readiness_condition = smokeReadinessConditionFromName(condition_str);
        if (ev->readiness_condition == SMOKE_READINESS_INVALID) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: wait_until event has unknown condition '%s' (at_ms=%d)",
                condition_str, ev->at_ms);
            return 0;
        }
        if (!has_wait_timeout || wait_timeout_ms <= 0
                || wait_timeout_ms > SMOKE_MAX_WAIT_TIMEOUT_MS) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: wait_until condition='%s' requires timeout_ms in [1,%d] (at_ms=%d)",
                condition_str, SMOKE_MAX_WAIT_TIMEOUT_MS, ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_WAIT_UNTIL;
        ev->wait_timeout_ms = (u32)wait_timeout_ms;
        if (has_stable_ms
                && (stable_ms < 0 || stable_ms >= wait_timeout_ms)) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: wait_until stable_ms must be in [0,timeout_ms) (stable_ms=%lld timeout_ms=%lld at_ms=%d)",
                (long long)stable_ms, (long long)wait_timeout_ms, ev->at_ms);
            return 0;
        }
        ev->stable_ms = (u32)stable_ms;

        if (has_assist_action || has_assist_condition
                || has_assist_hold_ms) {
            if (!has_assist_action || !has_assist_condition
                    || !has_assist_hold_ms || !has_stable_ms
                    || stable_ms <= 0) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: assisted wait_until requires assist_action, assist_condition, assist_hold_ms, and stable_ms > 0 (at_ms=%d)",
                    ev->at_ms);
                return 0;
            }
            ev->assist_action_id = actionmapResolveByName(assist_action_str);
            if (ev->assist_action_id < 0) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: assisted wait_until has unknown assist_action '%s' (at_ms=%d)",
                    assist_action_str, ev->at_ms);
                return 0;
            }
            ev->assist_condition = smokeReadinessConditionFromName(
                assist_condition_str);
            if (ev->assist_condition == SMOKE_READINESS_INVALID) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: assisted wait_until has unknown assist_condition '%s' (at_ms=%d)",
                    assist_condition_str, ev->at_ms);
                return 0;
            }
            if (assist_hold_ms <= 0 || assist_hold_ms >= wait_timeout_ms) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: assisted wait_until assist_hold_ms must be in [1,timeout_ms) (assist_hold_ms=%lld timeout_ms=%lld at_ms=%d)",
                    (long long)assist_hold_ms, (long long)wait_timeout_ms,
                    ev->at_ms);
                return 0;
            }
            ev->assist_hold_ms = (u32)assist_hold_ms;
        }
        return 1;
    }
    if (!strcmp(type_str, "exit")) {
        ev->type = SMOKE_EVENT_EXIT;
        return 1;
    }
    if (!strcmp(type_str, "unclean_exit")) {
        ev->type = SMOKE_EVENT_UNCLEAN_EXIT;
        return 1;
    }
    if (!strcmp(type_str, "network_timeout_client")) {
		if (!has_client_id || ev->client_id < 0
				|| ev->client_id >= NET_MAX_CLIENTS) {
			sysLogPrintf(LOG_ERROR,
				"SMOKE: network_timeout_client requires client_id in [0,%d] (at_ms=%d)",
				NET_MAX_CLIENTS - 1, ev->at_ms);
			return 0;
		}
		ev->type = SMOKE_EVENT_NETWORK_TIMEOUT_CLIENT;
		return 1;
	}
    if (!strcmp(type_str, "network_reconnect")) {
		ev->type = SMOKE_EVENT_NETWORK_RECONNECT;
		return 1;
	}
    if (!strcmp(type_str, "screenshot")) {
        if (!ev->path[0]) {
            sysLogPrintf(LOG_ERROR, "SMOKE: screenshot event missing 'path' (at_ms=%d)", ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_SCREENSHOT;
        return 1;
    }
    if (!strcmp(type_str, "receive_pdca_list")) {
        if (!ev->path[0]) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: receive_pdca_list event missing 'path' (at_ms=%d)",
                ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_RECEIVE_PDCA_LIST;
        return 1;
    }
    if (!strcmp(type_str, "agent_activate")
            || !strcmp(type_str, "agent_delete")) {
        if (!name_str[0]) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: %s event missing 'name' (at_ms=%d)",
                type_str, ev->at_ms);
            return 0;
        }
        snprintf(ev->path, sizeof(ev->path), "%s", name_str);
        ev->type = !strcmp(type_str, "agent_activate")
            ? SMOKE_EVENT_AGENT_ACTIVATE : SMOKE_EVENT_AGENT_DELETE;
        return 1;
    }
    if (!strcmp(type_str, "catalog_recovery_probe")) {
        if (!ev->path[0]) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: catalog_recovery_probe event missing 'path' (at_ms=%d)",
                ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_CATALOG_RECOVERY_PROBE;
        return 1;
    }
    if (!strcmp(type_str, "catalog_weapon_acquire")
            || !strcmp(type_str, "catalog_weapon_release")) {
        if (!ev->path[0]) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: %s event missing 'path' (at_ms=%d)",
                type_str, ev->at_ms);
            return 0;
        }
        if (!strcmp(type_str, "catalog_weapon_acquire")) {
            ev->type = SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE;
        } else {
            ev->type = SMOKE_EVENT_CATALOG_WEAPON_RELEASE;
        }
        return 1;
    }
    if (!strcmp(type_str, "key")) {
        if (key_str[0]) scancode = smokeKeyNameToScancode(key_str);
        if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES) {
            sysLogPrintf(LOG_ERROR, "SMOKE: key event has unresolved key (key='%s', scancode=%d)",
                key_str, scancode);
            return 0;
        }
        ev->scancode = scancode;
        if (!strcmp(action_str, "press")) {
            ev->type = SMOKE_EVENT_KEY_PRESS;
        } else if (!strcmp(action_str, "release")) {
            ev->type = SMOKE_EVENT_KEY_RELEASE;
        } else if (!has_action_mode || !strcmp(action_str, "tap")) {
            ev->type = SMOKE_EVENT_KEY_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
        } else {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: key event has unknown action '%s' (at_ms=%d)",
                action_str, ev->at_ms);
            return 0;
        }
        return 1;
    }
    if (!strcmp(type_str, "action")) {
        if (!name_str[0]) {
            sysLogPrintf(LOG_ERROR, "SMOKE: action event missing 'name' (at_ms=%d)", ev->at_ms);
            return 0;
        }
        s32 aid = actionmapResolveByName(name_str);
        if (aid < 0) {
            sysLogPrintf(LOG_ERROR, "SMOKE: action event has unknown name '%s' (at_ms=%d)",
                name_str, ev->at_ms);
            return 0;
        }
        ev->action_id = aid;
        if (!strcmp(action_str, "press")) {
            ev->type = SMOKE_EVENT_ACTION_PRESS;
        } else if (!strcmp(action_str, "release")) {
            ev->type = SMOKE_EVENT_ACTION_RELEASE;
        } else if (!has_action_mode || !strcmp(action_str, "tap")) {
            ev->type = SMOKE_EVENT_ACTION_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
        } else {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: action event has unknown action '%s' (at_ms=%d)",
                action_str, ev->at_ms);
            return 0;
        }
        return 1;
    }
    if (!strcmp(type_str, "mouse")) {
        if (!has_x || !has_y) {
            sysLogPrintf(LOG_ERROR, "SMOKE: mouse event missing x/y (at_ms=%d)", ev->at_ms);
            return 0;
        }
        s32 btn = smokeMouseButtonNameToCode(button_str);
        if (btn <= 0) {
            sysLogPrintf(LOG_ERROR, "SMOKE: mouse event has unknown button '%s' (at_ms=%d)",
                button_str, ev->at_ms);
            return 0;
        }
        ev->mouse_button = btn;
        if (!strcmp(action_str, "press")) {
            ev->type = SMOKE_EVENT_MOUSE_PRESS;
        } else if (!strcmp(action_str, "release")) {
            ev->type = SMOKE_EVENT_MOUSE_RELEASE;
        } else if (!has_action_mode || !strcmp(action_str, "tap")
                || !strcmp(action_str, "click")) {
            ev->type = SMOKE_EVENT_MOUSE_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
        } else {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: mouse event has unknown action '%s' (at_ms=%d)",
                action_str, ev->at_ms);
            return 0;
        }
        return 1;
    }
    if (!strcmp(type_str, "mouse_move")) {
        if (!has_x || !has_y) {
            sysLogPrintf(LOG_ERROR, "SMOKE: mouse_move event missing x/y (at_ms=%d)", ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_MOUSE_MOVE;
        return 1;
    }
    if (!strcmp(type_str, "mouse_wheel")) {
        if (!has_wheel_x && !has_wheel_y) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: mouse_wheel event missing wheel_x/wheel_y (at_ms=%d)", ev->at_ms);
            return 0;
        }
        if (ev->mouse_wheel_x == 0 && ev->mouse_wheel_y == 0) {
            sysLogPrintf(LOG_ERROR, "SMOKE: mouse_wheel event has zero delta (at_ms=%d)", ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_MOUSE_WHEEL;
        return 1;
    }

    sysLogPrintf(LOG_ERROR, "SMOKE: unknown event type '%s' (at_ms=%d)", type_str, ev->at_ms);
    return 0;
}

/* Parse one entry of the scenario's top-level `screenshots` array ({at_ms,name})
 * into a SMOKE_EVENT_SCREENSHOT event whose path mirrors the runner's
 * `<dir>/<ordinal:03>-<sanitized-name>.bmp`. This routes the standard scenario
 * screenshots through the in-game glReadPixels capture (focus/size-independent)
 * instead of the runner's PrintWindow/BitBlt path. Requires --smoke-screenshot-dir. */
static s32 smokeBuildScreenshotEvent(JParse *p, SmokeEvent *ev, s32 ordinal)
{
    JTok t = p->cur;
    char name_str[SMOKE_MAX_NAME] = {0};
    s64 at_ms = 0;

    if (t.type != JT_LBRACE) return 0;
    t = j_next(p);
    while (t.type != JT_RBRACE && t.type != JT_EOF) {
        if (t.type != JT_STRING) { t = j_next(p); continue; }
        char field[32]; j_tok_copy_str(&t, field, sizeof(field));
        t = j_next(p);
        if (t.type == JT_COLON) t = j_next(p);
        if (!strcmp(field, "at_ms")) {
            at_ms = j_tok_int(&t);
        } else if (!strcmp(field, "name")) {
            j_tok_copy_str(&t, name_str, sizeof(name_str));
        } else {
            j_skip_value(p);
        }
        t = j_next(p);
        if (t.type == JT_COMMA) t = j_next(p);
    }

    if (at_ms < 0 || at_ms > SMOKE_MAX_EVENT_AT_MS) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE: screenshot at_ms must be in [0,%d] (value=%lld)",
            SMOKE_MAX_EVENT_AT_MS, (long long)at_ms);
        return -1;
    }
    if (!s_State.screenshot_dir[0]) return 0; /* no output dir -> nothing to do */
    if (!name_str[0]) {
        snprintf(name_str, sizeof(name_str), "shot-%d", (int)ordinal);
    }
    char safe[SMOKE_MAX_NAME];
    s32 j = 0;
    for (s32 i = 0; name_str[i] && j < (s32)sizeof(safe) - 1; i++) {
        char c = name_str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-') {
            safe[j++] = c;
        } else {
            safe[j++] = '_';
        }
    }
    safe[j] = '\0';

    ev->at_ms = (s32)at_ms;
    ev->type = SMOKE_EVENT_SCREENSHOT;
    snprintf(ev->path, sizeof(ev->path), "%s/%03d-%s.bmp",
        s_State.screenshot_dir, (int)ordinal, safe);
    return 1;
}

static void smokeTrackExplicitHold(u8 *held, s32 down, s32 *held_count)
{
	if (down && !*held) {
		*held = 1;
		(*held_count)++;
	} else if (!down && *held) {
		*held = 0;
		(*held_count)--;
	}
}

/* A wait_until pauses virtual script time while real time continues. Explicit
 * press/release pairs are authored in virtual time, so crossing a wait with a
 * press held would silently stretch it to the full real-time wait. Reject the
 * fixture before launch; bounded wait assists are the only legal ownership
 * mechanism inside a paused interval. The stable event sort makes same-time
 * ordering deterministic and preserves JSON order. */
static s32 smokeValidateWaitHoldSchedule(void)
{
	u8 key_held[SDL_NUM_SCANCODES];
	u8 action_held[ACTION_COUNT];
	u8 mouse_held[SDL_BUTTON_X2 + 1];
	s32 held_count = 0;

	memset(key_held, 0, sizeof(key_held));
	memset(action_held, 0, sizeof(action_held));
	memset(mouse_held, 0, sizeof(mouse_held));

	for (s32 i = 0; i < s_State.event_count; i++) {
		const SmokeEvent *ev = &s_State.events[i];
		switch (ev->type) {
		case SMOKE_EVENT_KEY_PRESS:
			smokeTrackExplicitHold(&key_held[ev->scancode], 1, &held_count);
			break;
		case SMOKE_EVENT_KEY_RELEASE:
			smokeTrackExplicitHold(&key_held[ev->scancode], 0, &held_count);
			break;
		case SMOKE_EVENT_ACTION_PRESS:
			smokeTrackExplicitHold(&action_held[ev->action_id], 1, &held_count);
			break;
		case SMOKE_EVENT_ACTION_RELEASE:
			smokeTrackExplicitHold(&action_held[ev->action_id], 0, &held_count);
			break;
		case SMOKE_EVENT_MOUSE_PRESS:
			smokeTrackExplicitHold(&mouse_held[ev->mouse_button], 1, &held_count);
			break;
		case SMOKE_EVENT_MOUSE_RELEASE:
			smokeTrackExplicitHold(&mouse_held[ev->mouse_button], 0, &held_count);
			break;
		case SMOKE_EVENT_WAIT_UNTIL:
			if (held_count > 0) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: wait_until at_ms=%d crosses %d explicit input hold(s); use a bounded wait assist",
					ev->at_ms, held_count);
				return 0;
			}
			break;
		default:
			break;
		}
	}
	return 1;
}

static s32 smokeParseJson(const char *src)
{
    JParse p;
    p.src = src;
    p.pos = src;
    p.cur.type = JT_NONE;

	if (!smokeFixtureJsonValid(src)) {
		sysLogPrintf(LOG_ERROR,
			"SMOKE: test JSON is malformed, truncated, or has trailing data");
		return 0;
	}

    JTok t = j_next(&p);
    if (t.type != JT_LBRACE) {
        sysLogPrintf(LOG_ERROR, "SMOKE: test JSON must be an object");
        return 0;
    }
    t = j_next(&p);

    /* Sensible defaults so the harness still works for a near-empty test */
    s_State.channel_mask = 0xFFFFu;
    s_State.verbose      = 0;
    s_State.jump_logging = 0;
    s_State.timeout_ms   = SMOKE_DEFAULT_TIMEOUT_MS;
    s_State.event_count  = 0;
    strcpy(s_State.scenario_name, "(unnamed)");

    while (t.type != JT_RBRACE && t.type != JT_EOF) {
        if (t.type != JT_STRING) { t = j_next(&p); continue; }
        char field[64]; j_tok_copy_str(&t, field, sizeof(field));
        t = j_next(&p);
        if (t.type == JT_COLON) t = j_next(&p);

        if (!strcmp(field, "scenario_name")) {
            j_tok_copy_str(&t, s_State.scenario_name, sizeof(s_State.scenario_name));
        } else if (!strcmp(field, "log_channel_mask")) {
            s_State.channel_mask = smokeParseChannelMask(&t);
        } else if (!strcmp(field, "verbose")) {
            s_State.verbose = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "jump_logging")) {
            s_State.jump_logging = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "timeout_seconds")) {
            s64 secs = j_tok_int(&t);
            if (secs <= 0) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: timeout_seconds must be positive (value=%lld)",
                    (long long)secs);
                return 0;
            }
            if (secs > 3600) secs = 3600;
            s_State.timeout_ms = (s32)(secs * 1000);
        } else if (!strcmp(field, "input_sequence")) {
            if (t.type == JT_LBRACKET) {
                t = j_next(&p);
                while (t.type != JT_RBRACKET && t.type != JT_EOF) {
                    if (t.type == JT_LBRACE) {
                        if (s_State.event_count >= SMOKE_MAX_EVENTS) {
                            sysLogPrintf(LOG_ERROR, "SMOKE: event cap reached (%d); refusing a partial timeline",
                                SMOKE_MAX_EVENTS);
                            return 0;
                        } else {
                            SmokeEvent *ev = &s_State.events[s_State.event_count];
                            if (!smokeParseEvent(&p, ev)) {
                                return 0;
                            }
                            s_State.event_count++;
                        }
					} else {
						sysLogPrintf(LOG_ERROR,
							"SMOKE: input_sequence entries must be objects");
						return 0;
                    }
                    t = j_next(&p);
                    if (t.type == JT_COMMA) t = j_next(&p);
                }
			} else {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: input_sequence must be an array");
				return 0;
            }
        } else if (!strcmp(field, "screenshots")) {
            /* Standard scenario screenshots -> in-game glReadPixels events. */
            if (t.type == JT_LBRACKET) {
                s32 ordinal = 0;
                t = j_next(&p);
                while (t.type != JT_RBRACKET && t.type != JT_EOF) {
                    if (t.type == JT_LBRACE) {
                        ordinal++;
                        if (s_State.event_count < SMOKE_MAX_EVENTS) {
                            SmokeEvent *ev = &s_State.events[s_State.event_count];
                            s32 screenshot_result = smokeBuildScreenshotEvent(
                                &p, ev, ordinal);
                            if (screenshot_result < 0) {
                                return 0;
                            }
                            if (screenshot_result > 0) {
                                s_State.event_count++;
                            }
                        } else {
                            if (s_State.screenshot_dir[0]) {
                                sysLogPrintf(LOG_ERROR,
                                    "SMOKE: event cap reached (%d); refusing a partial screenshot timeline",
                                    SMOKE_MAX_EVENTS);
                                return 0;
                            }
                            j_skip_value(&p);
                        }
					} else {
						sysLogPrintf(LOG_ERROR,
							"SMOKE: screenshot entries must be objects");
						return 0;
                    }
                    t = j_next(&p);
                    if (t.type == JT_COMMA) t = j_next(&p);
                }
			} else {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: screenshots must be an array");
				return 0;
            }
        } else {
            /* assertions / description / tags / boot_args / paths_of_interest
             * are runner-side concerns; ignore them in the harness. */
            j_skip_value(&p);
        }

        t = j_next(&p);
        if (t.type == JT_COMMA) t = j_next(&p);
    }

    /* Stable-sort events by at_ms: screenshots may be appended after the
     * input_sequence yet need to interleave, and the dispatch loop stops at the
     * first future event. Insertion sort keeps equal-time ordering stable. */
    for (s32 i = 1; i < s_State.event_count; i++) {
        SmokeEvent key = s_State.events[i];
        s32 k = i - 1;
        while (k >= 0 && s_State.events[k].at_ms > key.at_ms) {
            s_State.events[k + 1] = s_State.events[k];
            k--;
        }
        s_State.events[k + 1] = key;
    }
	if (!smokeValidateWaitHoldSchedule()) {
		return 0;
	}

    /* wait_until pauses script time while the real watchdog continues. The
     * sum of every bounded pause plus the latest virtual event is therefore
     * the fixture's worst-case real-time schedule. Require strict headroom so
     * a scripted success can never share the watchdog's failure boundary. */
    u64 wait_budget_ms = 0;
    u64 latest_event_ms = 0;
    for (s32 i = 0; i < s_State.event_count; i++) {
        const SmokeEvent *ev = &s_State.events[i];
        if ((u64)ev->at_ms > latest_event_ms) {
            latest_event_ms = (u64)ev->at_ms;
        }
        if (ev->type == SMOKE_EVENT_WAIT_UNTIL) {
            wait_budget_ms += (u64)ev->wait_timeout_ms;
        }
    }
    if (wait_budget_ms > 0
            && latest_event_ms + wait_budget_ms >= (u64)s_State.timeout_ms) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE: timeout_ms=%d must exceed schedule bound=%llu (latest_event_ms=%llu wait_budget_ms=%llu)",
            s_State.timeout_ms,
            (unsigned long long)(latest_event_ms + wait_budget_ms),
            (unsigned long long)latest_event_ms,
            (unsigned long long)wait_budget_ms);
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------- */
/* SDL event injection                                              */
/* ---------------------------------------------------------------- */

static SDL_Keycode smokeScancodeToKeycode(s32 scancode)
{
    /* SDL has a public mapping function; fall through to it for
     * correctness across keyboard layouts.  The KEYDOWN event needs
     * both scancode and sym filled (some consumers use one, some the
     * other). */
    return SDL_GetKeyFromScancode((SDL_Scancode)scancode);
}

/* Resolve the active SDL window so we can stamp windowID on synthesised
 * events. ImGui's SDL2 backend (imgui_impl_sdl2.cpp:400) compares the
 * incoming event's windowID against the one captured at init; a zero
 * windowID causes ImGui to drop the event silently, which broke menu
 * nav in worker delta's iter-2 re-run. We try the public accessor
 * first, then fall back to SDL_GetKeyboardFocus / SDL_GetMouseFocus
 * for the (unlikely) case where the harness fires before gfx_sdl_init
 * has populated the window pointer. Returns 0 if no window resolves;
 * the caller still pushes the event so SDL-side debug consumers still
 * receive it. */
static SDL_Window *smokeResolveWindow(void)
{
    SDL_Window *w = gfxGetSdlWindow();
    if (!w) {
        w = SDL_GetKeyboardFocus();
    }
    if (!w) {
        w = SDL_GetMouseFocus();
    }
    if (!w) {
        return NULL;
    }
    return w;
}

static Uint32 smokeResolveWindowId(void)
{
    SDL_Window *w = smokeResolveWindow();
    return w ? SDL_GetWindowID(w) : 0;
}

/* ImGui's SDL backend may poll the OS cursor during NewFrame after consuming
 * queued SDL_MOUSEMOTION. Keep the real window cursor and the synthetic event
 * at the same client coordinate so hover/click hit-testing cannot snap back to
 * the user's prior cursor position between frames. */
static void smokeWarpMouseTo(s32 x, s32 y)
{
    SDL_Window *w = smokeResolveWindow();
    if (w) {
        SDL_WarpMouseInWindow(w, x, y);
    }
}

static void smokePushKey(s32 scancode, s32 down)
{
    if (scancode <= 0) return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.timestamp = SDL_GetTicks();
    ev.key.windowID  = smokeResolveWindowId();
    ev.key.state     = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.repeat    = 0;
    ev.key.keysym.scancode = (SDL_Scancode)scancode;
    ev.key.keysym.sym      = smokeScancodeToKeycode(scancode);
    ev.key.keysym.mod      = 0;
    SDL_PushEvent(&ev);
}

/* Synthesise an SDL_MOUSEBUTTONDOWN / UP event at {x, y} for the named
 * button. Uses SDL_PushEvent so the event flows through the same path
 * as a real user click -- this matters because ImGui's IsItemHovered /
 * IsItemActive only fire when the press / release sequence is correct.
 *
 * c115 (2026-05-14): also synthesise an SDL_MOUSEMOTION event before the
 * button event so ImGui's hover state catches up to the click position.
 * Without the motion event ImGui treats the click as happening at the
 * mouse cursor's last real position, which is usually still the title
 * bar on the freshly-launched harness window. */
static void smokePushMouse(s32 x, s32 y, s32 button, s32 down)
{
    if (button <= 0) return;
    const Uint32 ts  = SDL_GetTicks();
    const Uint32 wid = smokeResolveWindowId();
    if (down) {
        smokeWarpMouseTo(x, y);
        /* Move-then-click so ImGui's hover hit-test lands on the right
         * widget. Only needed on the press edge; the release edge fires
         * at the same coords so no extra motion is required. */
        SDL_Event mev;
        SDL_zero(mev);
        mev.type             = SDL_MOUSEMOTION;
        mev.motion.timestamp = ts;
        mev.motion.windowID  = wid;
        mev.motion.which     = 0;
        mev.motion.state     = 0;
        mev.motion.x         = (Sint32)x;
        mev.motion.y         = (Sint32)y;
        mev.motion.xrel      = 0;
        mev.motion.yrel      = 0;
        SDL_PushEvent(&mev);
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type            = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    ev.button.timestamp = ts;
    ev.button.windowID = wid;
    ev.button.which    = 0;
    ev.button.button   = (Uint8)button;
    ev.button.state    = down ? SDL_PRESSED : SDL_RELEASED;
    ev.button.clicks   = 1;
    ev.button.x        = (Sint32)x;
    ev.button.y        = (Sint32)y;
    SDL_PushEvent(&ev);
}

static void smokePushMouseMove(s32 x, s32 y)
{
    smokeWarpMouseTo(x, y);
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_MOUSEMOTION;
    ev.motion.timestamp = SDL_GetTicks();
    ev.motion.windowID = smokeResolveWindowId();
    ev.motion.which = 0;
    ev.motion.state = 0;
    ev.motion.x = x;
    ev.motion.y = y;
    ev.motion.xrel = 0;
    ev.motion.yrel = 0;
    SDL_PushEvent(&ev);
}

static void smokePushMouseWheel(s32 wheel_x, s32 wheel_y)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_MOUSEWHEEL;
    ev.wheel.timestamp = SDL_GetTicks();
    ev.wheel.windowID = smokeResolveWindowId();
    ev.wheel.which = 0;
    ev.wheel.x = wheel_x;
    ev.wheel.y = wheel_y;
    ev.wheel.preciseX = (float)wheel_x;
    ev.wheel.preciseY = (float)wheel_y;
    ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    SDL_PushEvent(&ev);
}

/* ---------------------------------------------------------------- */
/* Public API                                                       */
/* ---------------------------------------------------------------- */

int smokeHarnessIsActive(void)
{
    return s_State.active ? 1 : 0;
}

/* Debug.JumpLogging gate (defined in port/src/main.c, default 0). The JUMP: and
 * CAPSULE: physics diagnostic markers honor this flag so normal play does not
 * flood the log. Each smoke fixture owns the complete on/off assignment below
 * so persisted user settings cannot leak diagnostics into another test. */
extern s32 g_JumpLoggingEnabled;

int smokeHarnessInit(void)
{
    const SceneFireCounts *scene_counts;

    memset(&s_State, 0, sizeof(s_State));

    const char *path = sysArgGetString("--smoke");
    if (!path || !path[0]) {
        return 0;
    }

    /* Force log to be unfiltered while we are setting up so the test
     * loader's own messages are visible. */
    sysLogPrintf(LOG_NOTE, "SMOKE: harness init, test=%s", path);

    strncpy(s_State.test_path, path, sizeof(s_State.test_path) - 1);

    /* Optional: route the scenario's `screenshots` array through the in-game
     * glReadPixels capture, written into this dir. Must be read before
     * smokeParseJson (the parser builds the screenshot event paths from it). */
    const char *shotdir = sysArgGetString("--smoke-screenshot-dir");
    if (shotdir && shotdir[0]) {
        strncpy(s_State.screenshot_dir, shotdir, sizeof(s_State.screenshot_dir) - 1);
        sysLogPrintf(LOG_NOTE, "SMOKE: screenshot dir=%s (in-game glReadPixels)",
            s_State.screenshot_dir);
    }

    s32 sz = 0;
    char *src = smokeReadFile(path, &sz);
    if (!src) {
        sysLogPrintf(LOG_ERROR, "SMOKE: failed to read test file");
        exit(2);
    }

    if (!smokeParseJson(src)) {
        sysLogPrintf(LOG_ERROR, "SMOKE: failed to parse test file");
        sysMemFree(src);
        exit(2);
    }
    sysMemFree(src);

    /* Apply test-declared env */
    sysLogSetChannelMask(s_State.channel_mask);
    sysLogSetVerbose(s_State.verbose);

    sysLogPrintf(LOG_NOTE, "SMOKE: scenario=%s timeout_ms=%d events=%d channel_mask=0x%04X verbose=%d",
        s_State.scenario_name,
        s_State.timeout_ms,
        s_State.event_count,
        s_State.channel_mask,
        s_State.verbose);

    s_State.active = 1;
    s_State.next_event_idx = 0;
    s_State.timeline_pause_ms = 0;
    s_State.wait_started_ticks_ms = 0;
    s_State.waiting_event_idx = -1;
    memset(&s_State.wait_transition, 0, sizeof(s_State.wait_transition));
    s_State.wait_assist_injected = 0;
    scene_counts = sceneInstrumentGet();
    s_State.observed_stage_ready_count = scene_counts
        ? scene_counts->fire_count[SCENE_EVENT_STAGE_READY] : 0;
    s_State.observed_stage_teardown_count = scene_counts
        ? scene_counts->fire_count[SCENE_EVENT_STAGE_TEARDOWN] : 0;
    s_State.ready_stage_num = -1;
    s_State.ready_stage_generation = 0;
    s_State.events_fired = 0;
    s_State.start_ticks_ms = SDL_GetTicks();

    /* Per-test ownership: assign both states so a persisted Debug.JumpLogging
     * value cannot leak into a fixture that did not opt in. */
    g_JumpLoggingEnabled = s_State.jump_logging ? 1 : 0;

    if (g_JumpLoggingEnabled) {
        sysLogPrintf(LOG_NOTE, "SMOKE: jump_logging enabled (JUMP:/CAPSULE: markers on)");
    }

    return 1;
}

static void smokeCountCatalogDependency(const char *dep_id,
        asset_type_e expected_type, void *userdata)
{
    (void)dep_id;
    (void)expected_type;
    (*(s32 *)userdata)++;
}

static void smokeCatalogRecoveryProbe(const char *asset_id, s32 at_ms)
{
    const asset_entry_t *entry = assetCatalogResolve(asset_id);
    asset_type_e type = entry ? entry->type : ASSET_NONE;
    s32 load_result = entry && type != ASSET_NONE
        ? catalogLoadTypedAsset(type, asset_id) : 0;
    entry = assetCatalogResolve(asset_id);
    const asset_runtime_binding_t *binding = entry
        ? assetRuntimeFindByTypeAndId(entry->type, asset_id) : NULL;
    s32 dep_count = 0;
    if (entry) catalogDepForEachTyped(asset_id,
        smokeCountCatalogDependency, &dep_count);
    const s32 file_provider = entry
        && entry->source.primary.provider == fileProvider();
    const char *provider_path = file_provider
        ? fileProviderPath(entry->source.primary) : NULL;
    sysLogPrintf(entry && file_provider && binding ? LOG_NOTE : LOG_WARNING,
        "SMOKE: catalog_recovery_probe id='%s' catalog=%d temporary=%d load=%d provider=%d runtime=%d deps=%d provider_path='%s' runtime_path='%s' at_ms=%d",
        asset_id, entry ? 1 : 0, entry ? entry->temporary : 0, load_result,
        file_provider, binding ? 1 : 0, dep_count,
        provider_path ? provider_path : "",
        binding ? binding->primary_path : "", at_ms);
    if (load_result) catalogReleaseTypedAsset(type, asset_id);
}

static void smokeObserveStageReadyEpoch(void)
{
    const SceneFireCounts *scene_counts = sceneInstrumentGet();
    s32 ready_count = scene_counts
        ? scene_counts->fire_count[SCENE_EVENT_STAGE_READY] : 0;
    s32 teardown_count = scene_counts
        ? scene_counts->fire_count[SCENE_EVENT_STAGE_TEARDOWN] : 0;

    if (teardown_count != s_State.observed_stage_teardown_count) {
        sysLogPrintf(teardown_count < s_State.observed_stage_teardown_count
                ? LOG_ERROR : LOG_NOTE,
            "SMOKE.READINESS: stage teardown observed old=%d new=%d; clearing epoch",
            s_State.observed_stage_teardown_count, teardown_count);
        s_State.observed_stage_teardown_count = teardown_count;
        s_State.ready_stage_num = -1;
    }

    if (ready_count < s_State.observed_stage_ready_count) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.READINESS: stage-ready counter regressed old=%d new=%d; clearing epoch",
            s_State.observed_stage_ready_count, ready_count);
        s_State.observed_stage_ready_count = ready_count;
        s_State.ready_stage_num = -1;
        return;
    }
    if (ready_count == s_State.observed_stage_ready_count) {
        return;
    }

    s_State.observed_stage_ready_count = ready_count;
    s_State.ready_stage_num = g_StageNum;
    s_State.ready_stage_generation++;
    sysLogPrintf(LOG_NOTE,
        "SMOKE.READINESS: stage-ready epoch generation=%u stage=0x%02x event_count=%d",
        s_State.ready_stage_generation, s_State.ready_stage_num, ready_count);
}

static smoke_readiness_facts_t smokeCaptureReadinessFacts(void)
{
    smoke_readiness_facts_t facts;
    const s32 local_player_num = playermgrGetLocalPlayerNum();
    struct player *local_player = local_player_num >= 0
            && local_player_num < MAX_PLAYERS
        ? g_Vars.players[local_player_num] : NULL;
    LayerType scene_layer = sceneCurrentLayer();

    memset(&facts, 0, sizeof(facts));
    facts.network_active = g_NetMode == NETMODE_SERVER
        || g_NetMode == NETMODE_CLIENT;
    facts.network_listen_ready = g_NetMode == NETMODE_SERVER
        && netGetHost() != NULL;
    facts.network_reconnect_available = netClientReconnectAvailable();
    facts.local_client_in_game = g_NetLocalClient
        && g_NetLocalClient->state == CLSTATE_GAME;
    facts.gameplay_stage = g_StageNum == g_Vars.stagenum
        && g_StageNum != STAGE_TITLE
        && g_StageNum != STAGE_CITRAINING;
    facts.stage_ready_epoch = s_State.ready_stage_generation > 0
        && s_State.ready_stage_num == g_StageNum;
    /* network_stage_live describes every shipping network game mode. Normal
     * Combat Simulator uses normmplayerisrunning; co-op and Counter-Op retain
     * their role globals instead. Requiring only the former made the typed
     * readiness barrier permanently unreachable for network missions. */
    facts.multiplayer_running = g_Vars.normmplayerisrunning != 0
        || (facts.network_active && (g_Vars.coopplayernum >= 0
            || g_Vars.antiplayernum >= 0));
    /* Local gameplay readiness is shared by ordinary offline and network
     * fixtures. Network mode still requires the authenticated endpoint to
     * own the resolved local player; offline mode has no netclient object. */
    facts.local_player_present = local_player
		&& (!facts.network_active || (g_NetLocalClient
			&& g_NetLocalClient->player == local_player));
    facts.local_player_spawned = local_player && local_player->prop;
    facts.stage_tick_active = g_Vars.tickmode == TICKMODE_CUTSCENE
        || g_Vars.tickmode == TICKMODE_NORMAL;
    facts.scene_cutscene_layer = scene_layer == LAYER_CUTSCENE;
    facts.scene_gameplay_layer = scene_layer == LAYER_GAMEPLAY;
	facts.cutscene_in_progress = local_player_num >= 0
		&& playerCutsceneInProgress(local_player_num);
	facts.cutscene_frame_ready = local_player_num >= 0
		&& playerCutsceneCurTotalFrame60f(local_player_num) > 30.0f;
	/* A network skip is not production-ready until the listen authority has
	 * published the nonzero generation that the CLC must carry. */
	facts.cutscene_authority_ready = playerCutsceneGeneration() != 0;
    facts.gameplay_tick_normal = g_Vars.tickmode == TICKMODE_NORMAL;
    facts.gameplay_updates_active = g_Vars.lvupdate240 > 0;
	facts.player_in_cutscene = local_player_num >= 0
		&& playerInCutscene(local_player_num);
	facts.player_has_control = local_player_num >= 0
		&& g_PlayersWithControl[local_player_num] != 0;
    facts.player_unpaused = local_player
        && local_player->pausemode == PAUSEMODE_UNPAUSED;
    facts.player_alive = local_player && !local_player->isdead;
    facts.player_walk_mode = local_player
        && local_player->bondmovemode == MOVEMODE_WALK;
    facts.endscreen = g_MainIsEndscreen != 0;
	facts.endscreen_menu_active = menupoolIsActive(MENU_TYPE_ENDSCREEN_MP);
	facts.menu_input_active = inputCtxIsActive(&g_CtxImGuiMenu);
    return facts;
}

static void smokeLogReadinessWait(s32 level, const char *status,
        const SmokeEvent *ev, u32 waited_ms, u32 real_elapsed_ms,
		u32 stable_elapsed_ms, const smoke_readiness_facts_t *facts)
{
    sysLogPrintf(level,
		"SMOKE.WAIT: %s condition=%s at_ms=%d timeout_ms=%u waited_ms=%u real_elapsed_ms=%u stable_ms=%u stable_elapsed_ms=%u assist_action=%d assist_condition=%s assist_hold_ms=%u facts=net:%d/listen:%d/reconnect:%d/client_game:%d/stage:%d/ready:%d/mp:%d/player:%d/spawn:%d/tick:%d/layer_cut:%d/layer_game:%d/cut:%d/cut_progress:%d/cut_frame:%d/cut_auth:%d/normal:%d/update:%d/control:%d/unpaused:%d/alive:%d/walk:%d/end:%d/end_menu:%d/menu_input:%d",
        status, smokeReadinessConditionName(ev->readiness_condition), ev->at_ms,
        ev->wait_timeout_ms, waited_ms, real_elapsed_ms, ev->stable_ms,
		stable_elapsed_ms,
        ev->assist_action_id,
        smokeReadinessConditionName(ev->assist_condition), ev->assist_hold_ms,
		facts->network_active, facts->network_listen_ready,
		facts->network_reconnect_available,
		facts->local_client_in_game,
        facts->gameplay_stage, facts->stage_ready_epoch,
        facts->multiplayer_running,
        facts->local_player_present, facts->local_player_spawned,
        facts->stage_tick_active, facts->scene_cutscene_layer,
        facts->scene_gameplay_layer, facts->player_in_cutscene,
		facts->cutscene_in_progress, facts->cutscene_frame_ready,
		facts->cutscene_authority_ready,
        facts->gameplay_tick_normal, facts->gameplay_updates_active,
        facts->player_has_control, facts->player_unpaused,
        facts->player_alive, facts->player_walk_mode,
		facts->endscreen, facts->endscreen_menu_active,
		facts->menu_input_active);
}

static s32 smokeHasPendingTap(void)
{
    for (s32 i = 0; i < s_State.next_event_idx; i++) {
        const SmokeEvent *ev = &s_State.events[i];
        if ((ev->type == SMOKE_EVENT_KEY_TAP
                || ev->type == SMOKE_EVENT_ACTION_TAP
                || ev->type == SMOKE_EVENT_MOUSE_TAP)
                && ev->released == 1) {
            return 1;
        }
    }
    return 0;
}

static smoke_transition_config_t smokeWaitTransitionConfig(
        const SmokeEvent *ev)
{
    smoke_transition_config_t config;
    memset(&config, 0, sizeof(config));
    config.timeout_ms = ev->wait_timeout_ms;
    config.stable_ms = ev->stable_ms;
    config.assist_enabled = ev->assist_action_id >= 0;
    config.assist_hold_ms = ev->assist_hold_ms;
    return config;
}

static s32 smokePressWaitAssist(const SmokeEvent *ev, u32 waited_ms)
{
    if (s_State.wait_assist_injected) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.WAIT.ASSIST: action=%d condition=%s event=press rejected=already_injected at_ms=%d waited_ms=%u",
            ev->assist_action_id,
            smokeReadinessConditionName(ev->assist_condition), ev->at_ms,
            waited_ms);
        return 0;
    }
    if (!actionmapInjectStateForSmoke(0, ev->assist_action_id, 1)) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.WAIT.ASSIST: action=%d condition=%s event=press rejected=actionmap at_ms=%d waited_ms=%u",
            ev->assist_action_id,
            smokeReadinessConditionName(ev->assist_condition), ev->at_ms,
            waited_ms);
        return 0;
    }
	s_State.wait_assist_injected = 1;
	s_State.wait_assist_press_count++;
    sysLogPrintf(LOG_NOTE,
        "SMOKE.WAIT.ASSIST: action=%d condition=%s event=press at_ms=%d waited_ms=%u hold_ms=%u",
        ev->assist_action_id,
        smokeReadinessConditionName(ev->assist_condition), ev->at_ms,
        waited_ms, ev->assist_hold_ms);
    return 1;
}

static s32 smokeReleaseWaitAssist(const SmokeEvent *ev, const char *reason,
        u32 waited_ms)
{
    if (!s_State.wait_assist_injected) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.WAIT.ASSIST: action=%d condition=%s event=release rejected=not_injected reason=%s at_ms=%d waited_ms=%u",
            ev->assist_action_id,
            smokeReadinessConditionName(ev->assist_condition), reason,
            ev->at_ms, waited_ms);
        return 0;
    }
    if (!actionmapInjectStateForSmoke(0, ev->assist_action_id, 0)) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.WAIT.ASSIST: action=%d condition=%s event=release rejected=actionmap reason=%s at_ms=%d waited_ms=%u",
            ev->assist_action_id,
            smokeReadinessConditionName(ev->assist_condition), reason,
            ev->at_ms, waited_ms);
        return 0;
    }
    s_State.wait_assist_injected = 0;
	s_State.wait_assist_release_count++;
    smokeTransitionForceRelease(&s_State.wait_transition);
    sysLogPrintf(LOG_NOTE,
        "SMOKE.WAIT.ASSIST: action=%d condition=%s event=release reason=%s at_ms=%d waited_ms=%u",
        ev->assist_action_id,
        smokeReadinessConditionName(ev->assist_condition), reason,
        ev->at_ms, waited_ms);
    return 1;
}

/* Returns 1 when satisfied, 0 while waiting, and -1 after a fail-closed exit. */
static s32 smokeTickReadinessWait(SmokeEvent *ev, u32 now,
        u32 real_elapsed_ms)
{
    smoke_readiness_facts_t facts = smokeCaptureReadinessFacts();
    smoke_transition_config_t config = smokeWaitTransitionConfig(ev);
    smoke_transition_input_t input;
    smoke_transition_plan_t plan;
    s32 first_tick = s_State.waiting_event_idx != s_State.next_event_idx;

    if (first_tick) {
        if (s_State.waiting_event_idx >= 0) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE.WAIT: overlapping wait events active=%d next=%d",
                s_State.waiting_event_idx, s_State.next_event_idx);
            smokeHarnessExit(1, "wait_overlap");
            return -1;
        }
        s_State.waiting_event_idx = s_State.next_event_idx;
        s_State.wait_started_ticks_ms = now;
        smokeTransitionInit(&s_State.wait_transition, &config, now);
        s_State.wait_assist_injected = 0;
		s_State.wait_assist_press_count = 0;
		s_State.wait_assist_release_count = 0;
        smokeLogReadinessWait(LOG_NOTE, "begin", ev, 0, real_elapsed_ms,
			0, &facts);
    }

    u32 waited_ms = now - s_State.wait_started_ticks_ms;
    /* Pausing virtual time while a one-frame tap remains down would turn it
     * into an unbounded hold. Reject this even when the condition is already
     * true so a malformed fixture cannot hide the pending ownership leak. */
    if (first_tick && smokeHasPendingTap()) {
        smokeLogReadinessWait(LOG_ERROR, "pending_tap", ev, waited_ms,
			real_elapsed_ms, 0, &facts);
        smokeHarnessExit(1, "wait_pending_tap");
        return -1;
    }

    memset(&input, 0, sizeof(input));
    input.target_met = smokeReadinessConditionMet(ev->readiness_condition,
        &facts);
    if (ev->assist_action_id >= 0) {
        input.assist_condition_met = smokeReadinessConditionMet(
            ev->assist_condition, &facts);
    }
    plan = smokeTransitionTick(&s_State.wait_transition, &config, &input,
        now);
    if (plan.invalid) {
        smokeLogReadinessWait(LOG_ERROR, "invalid_policy", ev, waited_ms,
			real_elapsed_ms, 0, &facts);
        smokeHarnessExit(1, "wait_policy_invalid");
        return -1;
    }

    if (plan.stability_started) {
        sysLogPrintf(LOG_NOTE,
            "SMOKE.WAIT.STABLE: condition=%s event=begin stable_ms=%u at_ms=%d waited_ms=%u",
            smokeReadinessConditionName(ev->readiness_condition),
            ev->stable_ms, ev->at_ms, waited_ms);
    }
    if (plan.stability_reset) {
        sysLogPrintf(LOG_NOTE,
            "SMOKE.WAIT.STABLE: condition=%s event=reset stable_ms=%u at_ms=%d waited_ms=%u",
            smokeReadinessConditionName(ev->readiness_condition),
            ev->stable_ms, ev->at_ms, waited_ms);
    }

    /* The policy changes ownership state first, then the harness projects that
     * plan through the production action map. Releases always precede terminal
     * timeout/success handling, so no exit can strand a held assist. */
    if (plan.release_assist
            && !smokeReleaseWaitAssist(ev,
                plan.timed_out ? "wait_timeout" : "hold_elapsed",
                waited_ms)) {
        smokeHarnessExit(1, "wait_assist_release_failed");
        return -1;
    }
    if (plan.press_assist && !smokePressWaitAssist(ev, waited_ms)) {
        smokeHarnessExit(1, "wait_assist_press_failed");
        return -1;
    }
    if (plan.timed_out) {
        smokeLogReadinessWait(LOG_ERROR, "timeout", ev, waited_ms,
			real_elapsed_ms, plan.stable_elapsed_ms, &facts);
        smokeHarnessExit(1, "wait_condition_timeout");
        return -1;
    }

    if (plan.satisfied) {
        if (s_State.wait_assist_injected) {
            smokeLogReadinessWait(LOG_ERROR, "held_assist", ev, waited_ms,
				real_elapsed_ms, plan.stable_elapsed_ms, &facts);
            smokeHarnessExit(1, "wait_assist_held_on_success");
            return -1;
        }
        if (ev->assist_action_id >= 0) {
			s32 assist_used = smokeTransitionAssistWasUsed(
				&s_State.wait_transition);
			if ((assist_used && (s_State.wait_assist_press_count != 1
						|| s_State.wait_assist_release_count != 1))
					|| (!assist_used && (s_State.wait_assist_press_count != 0
						|| s_State.wait_assist_release_count != 0))) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE.WAIT.ASSIST: action=%d condition=%s rejected=ownership_balance press_count=%d release_count=%d at_ms=%d waited_ms=%u",
					ev->assist_action_id,
					smokeReadinessConditionName(ev->assist_condition),
					s_State.wait_assist_press_count,
					s_State.wait_assist_release_count, ev->at_ms, waited_ms);
				smokeHarnessExit(1, "wait_assist_balance_invalid");
				return -1;
			}
            sysLogPrintf(LOG_NOTE,
				"SMOKE.WAIT.ASSIST: action=%d condition=%s outcome=%s at_ms=%d waited_ms=%u press_count=%d release_count=%d",
                ev->assist_action_id,
                smokeReadinessConditionName(ev->assist_condition),
				assist_used ? "used" : "not_needed", ev->at_ms, waited_ms,
				s_State.wait_assist_press_count,
				s_State.wait_assist_release_count);
        }
        s_State.timeline_pause_ms += waited_ms;
        s_State.waiting_event_idx = -1;
        s_State.wait_started_ticks_ms = 0;
        memset(&s_State.wait_transition, 0, sizeof(s_State.wait_transition));
        smokeLogReadinessWait(LOG_NOTE, "satisfied", ev, waited_ms,
			real_elapsed_ms, plan.stable_elapsed_ms, &facts);
        return 1;
    }
    return 0;
}

void smokeHarnessTick(void)
{
    if (!s_State.active || s_State.exited) return;

    u32 now = SDL_GetTicks();
    u32 real_elapsed_ms = now - s_State.start_ticks_ms;

    /* The real watchdog has priority over observation, releases, readiness,
     * and scripted events. At the exact deadline, failure is the only legal
     * outcome; in particular, an exit event cannot win this tick. */
    if (real_elapsed_ms >= (u32)s_State.timeout_ms) {
        smokeHarnessExit(1, "timeout");
        return;
    }

    smokeObserveStageReadyEpoch();
    u32 script_elapsed_ms = real_elapsed_ms >= s_State.timeline_pause_ms
        ? real_elapsed_ms - s_State.timeline_pause_ms : 0;
    s32 elapsed = (s32)script_elapsed_ms;

    /* Pending-release sweep: scan all fired tap events whose release_at_ms
     * has come due.  Since events are dispatched in JSON order, we keep
     * the scan bounded to entries between [0, next_event_idx). All three
     * tap variants (key, action, mouse) share this sweep. */
    for (s32 i = 0; i < s_State.next_event_idx; i++) {
        SmokeEvent *ev = &s_State.events[i];
        if (ev->type != SMOKE_EVENT_KEY_TAP &&
            ev->type != SMOKE_EVENT_ACTION_TAP &&
            ev->type != SMOKE_EVENT_MOUSE_TAP) continue;
        if (ev->released >= 2) continue;
        if (ev->released == 1 && elapsed >= ev->release_at_ms) {
            if (ev->type == SMOKE_EVENT_KEY_TAP) {
                smokePushKey(ev->scancode, 0);
                sysLogPrintf(LOG_NOTE, "SMOKE: tap-release scancode=%d at_ms=%d",
                    ev->scancode, elapsed);
            } else if (ev->type == SMOKE_EVENT_ACTION_TAP) {
                actionmapInjectStateForSmoke(0, ev->action_id, 0);
                sysLogPrintf(LOG_NOTE, "SMOKE: action tap-release id=%d at_ms=%d",
                    ev->action_id, elapsed);
            } else { /* SMOKE_EVENT_MOUSE_TAP */
                smokePushMouse(ev->mouse_x, ev->mouse_y, ev->mouse_button, 0);
                sysLogPrintf(LOG_NOTE, "SMOKE: mouse tap-release btn=%d at_ms=%d",
                    ev->mouse_button, elapsed);
            }
            ev->released = 2;
        }
    }

    /* Dispatch any events whose at_ms has come due. */
    while (s_State.next_event_idx < s_State.event_count) {
        SmokeEvent *ev = &s_State.events[s_State.next_event_idx];
        if (ev->at_ms > elapsed) break;

        if (ev->type == SMOKE_EVENT_WAIT_UNTIL) {
            s32 wait_result = smokeTickReadinessWait(ev, now,
                real_elapsed_ms);
            if (wait_result < 0) {
                return;
            }
            if (wait_result == 0) {
                break;
            }
            s_State.next_event_idx++;
            s_State.events_fired++;
            script_elapsed_ms = real_elapsed_ms >= s_State.timeline_pause_ms
                ? real_elapsed_ms - s_State.timeline_pause_ms : 0;
            elapsed = (s32)script_elapsed_ms;
            continue;
        }

        switch (ev->type) {
        case SMOKE_EVENT_WAIT:
            sysLogPrintf(LOG_NOTE, "SMOKE: wait marker at_ms=%d", ev->at_ms);
            break;
        case SMOKE_EVENT_KEY_PRESS:
            sysLogPrintf(LOG_NOTE, "SMOKE: press scancode=%d at_ms=%d", ev->scancode, ev->at_ms);
            smokePushKey(ev->scancode, 1);
            break;
        case SMOKE_EVENT_KEY_RELEASE:
            sysLogPrintf(LOG_NOTE, "SMOKE: release scancode=%d at_ms=%d", ev->scancode, ev->at_ms);
            smokePushKey(ev->scancode, 0);
            break;
        case SMOKE_EVENT_KEY_TAP:
            sysLogPrintf(LOG_NOTE, "SMOKE: tap-press scancode=%d at_ms=%d", ev->scancode, ev->at_ms);
            smokePushKey(ev->scancode, 1);
            ev->released = 1;
            break;
        case SMOKE_EVENT_ACTION_PRESS:
            sysLogPrintf(LOG_NOTE, "SMOKE: action press id=%d at_ms=%d", ev->action_id, ev->at_ms);
            actionmapInjectStateForSmoke(0, ev->action_id, 1);
            break;
        case SMOKE_EVENT_ACTION_RELEASE:
            sysLogPrintf(LOG_NOTE, "SMOKE: action release id=%d at_ms=%d", ev->action_id, ev->at_ms);
            actionmapInjectStateForSmoke(0, ev->action_id, 0);
            break;
        case SMOKE_EVENT_ACTION_TAP:
            sysLogPrintf(LOG_NOTE, "SMOKE: action tap-press id=%d at_ms=%d", ev->action_id, ev->at_ms);
            actionmapInjectStateForSmoke(0, ev->action_id, 1);
            ev->released = 1;
            break;
        case SMOKE_EVENT_MOUSE_PRESS:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse press btn=%d xy=(%d,%d) at_ms=%d",
                ev->mouse_button, ev->mouse_x, ev->mouse_y, ev->at_ms);
            smokePushMouse(ev->mouse_x, ev->mouse_y, ev->mouse_button, 1);
            break;
        case SMOKE_EVENT_MOUSE_RELEASE:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse release btn=%d xy=(%d,%d) at_ms=%d",
                ev->mouse_button, ev->mouse_x, ev->mouse_y, ev->at_ms);
            smokePushMouse(ev->mouse_x, ev->mouse_y, ev->mouse_button, 0);
            break;
        case SMOKE_EVENT_MOUSE_TAP:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse tap-press btn=%d xy=(%d,%d) at_ms=%d",
                ev->mouse_button, ev->mouse_x, ev->mouse_y, ev->at_ms);
            smokePushMouse(ev->mouse_x, ev->mouse_y, ev->mouse_button, 1);
            ev->released = 1;
            break;
        case SMOKE_EVENT_MOUSE_MOVE:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse move xy=(%d,%d) at_ms=%d",
                ev->mouse_x, ev->mouse_y, ev->at_ms);
            smokePushMouseMove(ev->mouse_x, ev->mouse_y);
            break;
        case SMOKE_EVENT_MOUSE_WHEEL:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse wheel delta=(%d,%d) at_ms=%d",
                ev->mouse_wheel_x, ev->mouse_wheel_y, ev->at_ms);
            smokePushMouseWheel(ev->mouse_wheel_x, ev->mouse_wheel_y);
            break;
        case SMOKE_EVENT_RECEIVE_PDCA_LIST:
        {
            s32 delivered = netDistribDebugReceivePdcaListForSmoke(ev->path);
            sysLogPrintf(delivered > 0 ? LOG_NOTE : LOG_WARNING,
                "SMOKE: receive_pdca_list path='%s' delivered=%d at_ms=%d",
                ev->path, delivered, ev->at_ms);
            break;
        }
        case SMOKE_EVENT_AGENT_ACTIVATE:
        case SMOKE_EVENT_AGENT_DELETE:
        {
            char before[AGENT_PROFILE_NAME_MAX];
            const s32 activate = ev->type == SMOKE_EVENT_AGENT_ACTIVATE;
            s32 result;
            snprintf(before, sizeof(before), "%s", prefsAgentGetActive());
            result = activate
                ? agentSessionActivate(ev->path)
                : agentSessionDelete(ev->path);
            sysLogPrintf(result == 0 ? LOG_NOTE : LOG_WARNING,
                "SMOKE: agent_session op=%s name='%s' result=%d active_before='%s' active_after='%s' at_ms=%d",
                activate ? "activate" : "delete", ev->path, result,
                before, prefsAgentGetActive(), ev->at_ms);
            break;
        }
        case SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE:
        case SMOKE_EVENT_CATALOG_WEAPON_RELEASE:
        {
            const s32 acquire = ev->type == SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE;
            const asset_entry_t *entry = assetCatalogResolve(ev->path);
            s32 result = entry && entry->type == ASSET_WEAPON;

            if (acquire) {
                result = catalogLoadTypedAsset(ASSET_WEAPON, ev->path);
            } else if (result) {
                catalogReleaseTypedAsset(ASSET_WEAPON, ev->path);
            }
            entry = assetCatalogResolve(ev->path);
            sysLogPrintf(result ? LOG_NOTE : LOG_WARNING,
                "SMOKE: catalog_weapon_owner op=%s id='%s' result=%d state=%d ref=%d payload=%d at_ms=%d",
                acquire ? "acquire" : "release", ev->path, result,
                entry ? (s32)entry->load_state : -1,
                entry ? entry->ref_count : -1,
                entry ? (s32)entry->payload_kind : -1,
                ev->at_ms);
            break;
        }
        case SMOKE_EVENT_CATALOG_RECOVERY_PROBE:
            smokeCatalogRecoveryProbe(ev->path, ev->at_ms);
            break;
        case SMOKE_EVENT_NETWORK_TIMEOUT_CLIENT:
        {
			struct netclient *target = ev->client_id >= 0
				&& ev->client_id < NET_MAX_CLIENTS
				? &g_NetClients[ev->client_id] : NULL;
			if (g_NetMode != NETMODE_SERVER || !target || !target->peer
					|| target->state < CLSTATE_GAME || !target->stage_ready) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE.NETWORK: timeout_client rejected client=%d mode=%d state=%d peer=%d stage_ready=%d at_ms=%d",
					ev->client_id, g_NetMode, target ? (s32)target->state : -1,
					target && target->peer, target && target->stage_ready,
					ev->at_ms);
				smokeHarnessExit(1, "network_timeout_client_failed");
				return;
			}
			sysLogPrintf(LOG_NOTE,
				"SMOKE.NETWORK: timeout_client client=%d state=%u stage_ready=1 at_ms=%d production_path=1",
				ev->client_id, (unsigned)target->state, ev->at_ms);
			netServerKick(target, DISCONNECT_TIMEOUT);
			break;
		}
        case SMOKE_EVENT_NETWORK_RECONNECT:
		{
			const s32 rc = netClientReconnect();
			sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_ERROR,
				"SMOKE.NETWORK: reconnect rc=%d at_ms=%d production_path=1",
				rc, ev->at_ms);
			if (rc != 0) {
				smokeHarnessExit(1, "network_reconnect_failed");
				return;
			}
			break;
		}
        case SMOKE_EVENT_SCREENSHOT:
            sysLogPrintf(LOG_NOTE, "SMOKE: screenshot at_ms=%d -> %s", ev->at_ms, ev->path);
            gfxRequestSmokeScreenshot(ev->path);
            break;
        case SMOKE_EVENT_EXIT:
            sysLogPrintf(LOG_NOTE, "SMOKE: scripted exit at_ms=%d", ev->at_ms);
            s_State.next_event_idx++;
            s_State.events_fired++;
            smokeHarnessExit(0, "scripted_exit");
            return;
        case SMOKE_EVENT_UNCLEAN_EXIT:
            s_State.next_event_idx++;
            s_State.events_fired++;
            s_State.exited = 1;
            sysLogPrintf(LOG_NOTE,
                "SMOKE: result=unclean_exit scenario=%s elapsed_ms=%d events_fired=%d/%d code=0",
                s_State.scenario_name, elapsed, s_State.events_fired,
                s_State.event_count);
            fflush(NULL);
            _Exit(0);
        default:
            break;
        }
        s_State.next_event_idx++;
        s_State.events_fired++;
    }

}

void smokeHarnessExit(int code, const char *reason)
{
    u32 now;
    s32 elapsed;

    if (!s_State.active) {
        exit(code);
    }
    if (s_State.exited) return;

    now = SDL_GetTicks();
    elapsed = (s32)(now - s_State.start_ticks_ms);
    /* The real watchdog can preempt the wait policy before its next tick.
     * Release actual runtime ownership here as a final idempotent backstop. */
    if (s_State.wait_assist_injected) {
        if (s_State.waiting_event_idx < 0
                || s_State.waiting_event_idx >= s_State.event_count
                || !smokeReleaseWaitAssist(
                    &s_State.events[s_State.waiting_event_idx],
                    reason ? reason : "harness_exit",
                    now - s_State.wait_started_ticks_ms)) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE.WAIT.ASSIST: terminal cleanup failed reason=%s",
                reason ? reason : "unknown");
            code = 1;
            reason = "wait_assist_cleanup_failed";
        }
    }
    s_State.exited = 1;

    sysLogPrintf(code == 0 ? LOG_NOTE : LOG_WARNING,
        "SMOKE: result=%s scenario=%s elapsed_ms=%d events_fired=%d/%d code=%d",
        reason ? reason : "unknown",
        s_State.scenario_name,
        elapsed,
        s_State.events_fired,
        s_State.event_count,
        code);

    /* Give the log layer one last flush opportunity.  sysLogPrintf is
     * line-buffered to the file; the next call after exit() would lose
     * any in-flight buffer if we exited too eagerly. */
    fflush(NULL);

    exit(code);
}
