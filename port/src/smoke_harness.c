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
 *   { "at_ms": N, "type": "exit" }
 *     -- scripted clean exit; produces SMOKE: result=scripted_exit.
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

#include "smoke_harness.h"
#include "system.h"
#include "actionmap.h"   /* actionmapResolveByName, actionmapInjectStateForSmoke */

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
#define SMOKE_TAP_RELEASE_MS     16   /* one 60Hz frame */

typedef enum {
    SMOKE_EVENT_NONE = 0,
    SMOKE_EVENT_WAIT,
    SMOKE_EVENT_KEY_PRESS,
    SMOKE_EVENT_KEY_RELEASE,
    SMOKE_EVENT_KEY_TAP,
    SMOKE_EVENT_ACTION_PRESS,
    SMOKE_EVENT_ACTION_RELEASE,
    SMOKE_EVENT_ACTION_TAP,
    SMOKE_EVENT_MOUSE_PRESS,
    SMOKE_EVENT_MOUSE_RELEASE,
    SMOKE_EVENT_MOUSE_TAP,
    SMOKE_EVENT_SCREENSHOT,         /* in-game glReadPixels grab -> path */
    SMOKE_EVENT_EXIT
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
    s32            release_at_ms;     /* for tap: scheduled release time */
    s32            released;          /* tap: 0 = press pending, 1 = release pending, 2 = done */
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
    ev->release_at_ms = -1;
    ev->released = 0;

    char type_str[32]   = {0};
    char key_str[32]    = {0};
    char action_str[16] = {0};
    char name_str[64]   = {0};
    char button_str[16] = {0};
    s32  scancode = 0;
    s32  has_x = 0;
    s32  has_y = 0;

    while (t.type != JT_RBRACE && t.type != JT_EOF) {
        if (t.type != JT_STRING) { t = j_next(p); continue; }
        char field[32]; j_tok_copy_str(&t, field, sizeof(field));
        t = j_next(p); /* colon */
        if (t.type == JT_COLON) t = j_next(p);

        if (!strcmp(field, "at_ms")) {
            ev->at_ms = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "type")) {
            j_tok_copy_str(&t, type_str, sizeof(type_str));
        } else if (!strcmp(field, "key")) {
            j_tok_copy_str(&t, key_str, sizeof(key_str));
        } else if (!strcmp(field, "scancode")) {
            scancode = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "action")) {
            j_tok_copy_str(&t, action_str, sizeof(action_str));
        } else if (!strcmp(field, "name")) {
            j_tok_copy_str(&t, name_str, sizeof(name_str));
        } else if (!strcmp(field, "path")) {
            j_tok_copy_str(&t, ev->path, sizeof(ev->path));
        } else if (!strcmp(field, "x")) {
            ev->mouse_x = (s32)j_tok_int(&t);
            has_x = 1;
        } else if (!strcmp(field, "y")) {
            ev->mouse_y = (s32)j_tok_int(&t);
            has_y = 1;
        } else if (!strcmp(field, "button")) {
            j_tok_copy_str(&t, button_str, sizeof(button_str));
        } else {
            j_skip_value(p);
        }

        t = j_next(p);
        if (t.type == JT_COMMA) t = j_next(p);
    }

    if (!strcmp(type_str, "wait") || !type_str[0]) {
        ev->type = SMOKE_EVENT_WAIT;
        return 1;
    }
    if (!strcmp(type_str, "exit")) {
        ev->type = SMOKE_EVENT_EXIT;
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
    if (!strcmp(type_str, "key")) {
        if (key_str[0]) scancode = smokeKeyNameToScancode(key_str);
        if (scancode <= 0) {
            sysLogPrintf(LOG_ERROR, "SMOKE: key event has unresolved key (key='%s', scancode=%d)",
                key_str, scancode);
            return 0;
        }
        ev->scancode = scancode;
        if (!strcmp(action_str, "press")) {
            ev->type = SMOKE_EVENT_KEY_PRESS;
        } else if (!strcmp(action_str, "release")) {
            ev->type = SMOKE_EVENT_KEY_RELEASE;
        } else {
            ev->type = SMOKE_EVENT_KEY_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
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
        } else {
            /* tap (default) or any other value -- treat as tap with auto-release. */
            ev->type = SMOKE_EVENT_ACTION_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
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
        } else {
            /* tap / click / default -- press now, schedule release next frame. */
            ev->type = SMOKE_EVENT_MOUSE_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
        }
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
    s32 at_ms = 0;

    if (t.type != JT_LBRACE) return 0;
    t = j_next(p);
    while (t.type != JT_RBRACE && t.type != JT_EOF) {
        if (t.type != JT_STRING) { t = j_next(p); continue; }
        char field[32]; j_tok_copy_str(&t, field, sizeof(field));
        t = j_next(p);
        if (t.type == JT_COLON) t = j_next(p);
        if (!strcmp(field, "at_ms")) {
            at_ms = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "name")) {
            j_tok_copy_str(&t, name_str, sizeof(name_str));
        } else {
            j_skip_value(p);
        }
        t = j_next(p);
        if (t.type == JT_COMMA) t = j_next(p);
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

    ev->at_ms = at_ms;
    ev->type = SMOKE_EVENT_SCREENSHOT;
    snprintf(ev->path, sizeof(ev->path), "%s/%03d-%s.bmp",
        s_State.screenshot_dir, (int)ordinal, safe);
    return 1;
}

static s32 smokeParseJson(const char *src)
{
    JParse p;
    p.src = src;
    p.pos = src;
    p.cur.type = JT_NONE;

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
            if (secs <= 0) secs = (s64)(SMOKE_DEFAULT_TIMEOUT_MS / 1000);
            if (secs > 3600) secs = 3600;
            s_State.timeout_ms = (s32)(secs * 1000);
        } else if (!strcmp(field, "input_sequence")) {
            if (t.type == JT_LBRACKET) {
                t = j_next(&p);
                while (t.type != JT_RBRACKET && t.type != JT_EOF) {
                    if (t.type == JT_LBRACE) {
                        if (s_State.event_count >= SMOKE_MAX_EVENTS) {
                            sysLogPrintf(LOG_WARNING, "SMOKE: event cap reached (%d), dropping the rest",
                                SMOKE_MAX_EVENTS);
                            j_skip_value(&p);
                        } else {
                            SmokeEvent *ev = &s_State.events[s_State.event_count];
                            if (smokeParseEvent(&p, ev)) {
                                s_State.event_count++;
                            }
                        }
                    }
                    t = j_next(&p);
                    if (t.type == JT_COMMA) t = j_next(&p);
                }
            } else {
                j_skip_value(&p);
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
                            if (smokeBuildScreenshotEvent(&p, ev, ordinal)) {
                                s_State.event_count++;
                            }
                        } else {
                            j_skip_value(&p);
                        }
                    }
                    t = j_next(&p);
                    if (t.type == JT_COMMA) t = j_next(&p);
                }
            } else {
                j_skip_value(&p);
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
static Uint32 smokeResolveWindowId(void)
{
    SDL_Window *w = gfxGetSdlWindow();
    if (!w) {
        w = SDL_GetKeyboardFocus();
    }
    if (!w) {
        w = SDL_GetMouseFocus();
    }
    if (!w) {
        return 0;
    }
    return SDL_GetWindowID(w);
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

/* ---------------------------------------------------------------- */
/* Public API                                                       */
/* ---------------------------------------------------------------- */

int smokeHarnessIsActive(void)
{
    return s_State.active ? 1 : 0;
}

/* Debug.JumpLogging gate (defined in port/src/main.c, default 0). The JUMP: and
 * CAPSULE: physics diagnostic markers honor this flag so normal play does not
 * flood the log; a running smoke is their intended consumer (see src/lib/capsule.c
 * comment), so smokeHarnessInit enables it below. */
extern s32 g_JumpLoggingEnabled;

int smokeHarnessInit(void)
{
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
    s_State.events_fired = 0;
    s_State.start_ticks_ms = SDL_GetTicks();

    /* Per-test opt-in: enable the JUMP:/CAPSULE: physics diagnostic markers,
     * which are gated behind Debug.JumpLogging (default 0 so normal play does
     * not flood the log). A test sets "jump_logging": 1 when it asserts on those
     * markers -- e.g. wall_jump_capsule_smoke (c038) needs "CAPSULE: sweep
     * enter/result", which capsule.c only emits when this flag is set. Kept
     * per-test on purpose: a swarm smoke (297 bots sweeping) would otherwise
     * flood the log. Process exits at smoke end, so no restore is needed. */
    if (s_State.jump_logging) {
        g_JumpLoggingEnabled = 1;
        sysLogPrintf(LOG_NOTE, "SMOKE: jump_logging enabled (JUMP:/CAPSULE: markers on)");
    }

    return 1;
}

void smokeHarnessTick(void)
{
    if (!s_State.active || s_State.exited) return;

    u32 now = SDL_GetTicks();
    s32 elapsed = (s32)(now - s_State.start_ticks_ms);

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
        default:
            break;
        }
        s_State.next_event_idx++;
        s_State.events_fired++;
    }

    if (elapsed >= s_State.timeout_ms) {
        smokeHarnessExit(1, "timeout");
    }
}

void smokeHarnessExit(int code, const char *reason)
{
    if (!s_State.active) {
        exit(code);
    }
    if (s_State.exited) return;
    s_State.exited = 1;

    u32 now = SDL_GetTicks();
    s32 elapsed = (s32)(now - s_State.start_ticks_ms);

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
