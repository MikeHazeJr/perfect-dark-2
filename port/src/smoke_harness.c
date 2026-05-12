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
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <SDL.h>

#include <PR/ultratypes.h>

#include "smoke_harness.h"
#include "system.h"

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
    SMOKE_EVENT_EXIT
} SmokeEventType;

typedef struct {
    s32            at_ms;
    SmokeEventType type;
    s32            scancode;          /* for key events */
    s32            release_at_ms;     /* for tap: scheduled release time */
    s32            released;          /* tap: 0 = press pending, 1 = release pending, 2 = done */
} SmokeEvent;

typedef struct {
    s32          active;
    char         test_path[SMOKE_MAX_PATH];
    char         scenario_name[SMOKE_MAX_NAME];
    s32          timeout_ms;
    u32          channel_mask;       /* applied via sysLogSetChannelMask */
    s32          verbose;
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
        char tmp[64];
        j_tok_copy_str(t, tmp, sizeof(tmp));
        if (!strcasecmp(tmp, "all"))  return 0xFFFFu;
        if (!strcasecmp(tmp, "none")) return 0x0000u;
        return (u32)strtoul(tmp, NULL, 0);
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

static s32 smokeParseEvent(JParse *p, SmokeEvent *ev)
{
    JTok t = p->cur;
    if (t.type != JT_LBRACE) return 0;
    t = j_next(p);

    ev->at_ms = 0;
    ev->type = SMOKE_EVENT_NONE;
    ev->scancode = 0;
    ev->release_at_ms = -1;
    ev->released = 0;

    char type_str[32] = {0};
    char key_str[32]  = {0};
    char action_str[16] = {0};
    s32  scancode = 0;

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

    sysLogPrintf(LOG_ERROR, "SMOKE: unknown event type '%s' (at_ms=%d)", type_str, ev->at_ms);
    return 0;
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
        } else {
            /* assertions / description / tags / boot_args / paths_of_interest
             * are runner-side concerns; ignore them in the harness. */
            j_skip_value(&p);
        }

        t = j_next(&p);
        if (t.type == JT_COMMA) t = j_next(&p);
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

static void smokePushKey(s32 scancode, s32 down)
{
    if (scancode <= 0) return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.timestamp = SDL_GetTicks();
    ev.key.windowID  = 0;
    ev.key.state     = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.repeat    = 0;
    ev.key.keysym.scancode = (SDL_Scancode)scancode;
    ev.key.keysym.sym      = smokeScancodeToKeycode(scancode);
    ev.key.keysym.mod      = 0;
    SDL_PushEvent(&ev);
}

/* ---------------------------------------------------------------- */
/* Public API                                                       */
/* ---------------------------------------------------------------- */

int smokeHarnessIsActive(void)
{
    return s_State.active ? 1 : 0;
}

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

    return 1;
}

void smokeHarnessTick(void)
{
    if (!s_State.active || s_State.exited) return;

    u32 now = SDL_GetTicks();
    s32 elapsed = (s32)(now - s_State.start_ticks_ms);

    /* Pending-release sweep: scan all fired tap events whose release_at_ms
     * has come due.  Since events are dispatched in JSON order, we keep
     * the scan bounded to entries between [0, next_event_idx). */
    for (s32 i = 0; i < s_State.next_event_idx; i++) {
        SmokeEvent *ev = &s_State.events[i];
        if (ev->type != SMOKE_EVENT_KEY_TAP) continue;
        if (ev->released >= 2) continue;
        if (ev->released == 1 && elapsed >= ev->release_at_ms) {
            smokePushKey(ev->scancode, 0);
            sysLogPrintf(LOG_NOTE, "SMOKE: tap-release scancode=%d at_ms=%d", ev->scancode, elapsed);
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
