/**
 * discord.c — Discord Rich Presence thin IPC client.
 *
 * Implements the Discord RPC IPC protocol directly via Windows named pipes.
 * No external library required — zero new link dependencies.
 *
 * Wire format per frame:
 *   [opcode: u32 LE][length: u32 LE][UTF-8 JSON payload]
 * Opcodes: 0=HANDSHAKE  1=FRAME  2=CLOSE  3=PING  4=PONG
 *
 * Handshake: opcode=0, payload={"v":1,"client_id":"<DISCORD_APP_ID>"}
 * Activity:  opcode=1, payload={"cmd":"SET_ACTIVITY","args":{...},"nonce":"N"}
 *
 * Fail-silent contract: every error path silently disconnects and schedules
 * a reconnect via DISCORD_RECONNECT_SECS backoff.
 *
 * The pipe is opened in PIPE_NOWAIT (non-blocking) mode so reads used for
 * draining Discord's responses return immediately without blocking.  Writes
 * of the small payloads we send (~500 bytes) always complete synchronously
 * because the pipe buffer is never near full.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "discord.h"

/* Game state headers.  These define bool as s32 via types.h — do NOT include
 * <stdbool.h> in this file. */
#include "constants.h"    /* STAGE_IS_GAMEPLAY, NETMODE_*, MPSCENARIO_* */
#include "bss.h"           /* g_MissionConfig, g_MpSetup, g_Vars */
#include "net/net.h"       /* g_NetMode, g_NetGameMode, g_NetDedicated */
#include "game/mplayer/participant.h" /* mpGetActivePlayerCount, mpGetActiveBotCount */
#include "game/forgemode.h"           /* forgeSessionIsActive */

#define DISCORD_OPCODE_HANDSHAKE  0
#define DISCORD_OPCODE_FRAME      1
#define DISCORD_TICK_SECS         15
#define DISCORD_RECONNECT_SECS    30
#define DISCORD_PAYLOAD_MAX       2048

extern s32 g_StageNum;

/* ── module state ────────────────────────────────────────────────── */

static HANDLE  s_Pipe           = INVALID_HANDLE_VALUE;
static s32     s_Ready          = 0;
static time_t  s_LastUpdate     = 0;
static time_t  s_LastConnect    = 0;
static time_t  s_ActivityStart  = 0;
static u32     s_Nonce          = 0;
static char    s_PrevDetails[128] = {0};
static char    s_PrevState[128]   = {0};

/* ── pipe helpers ────────────────────────────────────────────────── */

static void disc_disconnect(void)
{
    if (s_Pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(s_Pipe);
        s_Pipe = INVALID_HANDLE_VALUE;
    }
    s_Ready = 0;
}

/* Try to connect to \\.\pipe\discord-ipc-{0..9}.  Returns 1 on success. */
static s32 disc_try_connect(void)
{
    char  name[64];
    DWORD mode;
    s32   i;

    for (i = 0; i < 10; i++) {
        _snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
        s_Pipe = CreateFileA(name,
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        if (s_Pipe != INVALID_HANDLE_VALUE) {
            /* Switch to non-blocking mode so drain reads return immediately. */
            mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            if (!SetNamedPipeHandleState(s_Pipe, &mode, NULL, NULL)) {
                disc_disconnect();
                continue;
            }
            return 1;
        }
    }
    return 0;
}

/* Write a framed message synchronously.  Safe because we write at most ~500
 * bytes every 15 seconds and the pipe buffer is never near full. */
static s32 disc_write(u32 opcode, const char *payload, DWORD plen)
{
    u32   header[2];
    DWORD written;

    header[0] = opcode;
    header[1] = plen;

    if (!WriteFile(s_Pipe, header, 8, &written, NULL) || written != 8) return 0;
    if (!WriteFile(s_Pipe, payload, plen, &written, NULL) || written != plen) return 0;
    return 1;
}

/* Discard any pending data Discord sent (READY event, SET_ACTIVITY ACKs).
 * With PIPE_NOWAIT, ReadFile returns immediately if no data is available. */
static void disc_drain(void)
{
    char  buf[512];
    DWORD nread;
    s32   i;

    for (i = 0; i < 16; i++) {
        if (!ReadFile(s_Pipe, buf, sizeof(buf), &nread, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_NO_DATA) break; /* no more data — done */
            disc_disconnect();
            return;
        }
        if (nread == 0) break;
    }
}

static s32 disc_send_handshake(void)
{
    char buf[128];
    s32  len;

    len = _snprintf(buf, sizeof(buf),
        "{\"v\":1,\"client_id\":\"%s\"}", DISCORD_APP_ID);
    if (len <= 0 || len >= (s32)sizeof(buf)) return 0;
    return disc_write(DISCORD_OPCODE_HANDSHAKE, buf, (DWORD)len);
}

/* ── game-state helpers ──────────────────────────────────────────── */

static const char *k_DifficultyNames[] = {
    "Agent", "Special Agent", "Perfect Agent", "Dark Agent"
};

static const char *k_ScenarioNames[] = {
    "Combat",            /* MPSCENARIO_COMBAT           0 */
    "Hold the Case",     /* MPSCENARIO_HOLDTHEBRIEFCASE 1 */
    "Hacker Central",    /* MPSCENARIO_HACKERCENTRAL    2 */
    "Pop a Cap",         /* MPSCENARIO_POPACAP          3 */
    "King of the Hill",  /* MPSCENARIO_KINGOFTHEHILL    4 */
    "Capture the Case",  /* MPSCENARIO_CAPTURETHECASE   5 */
};

/* Convert a catalog stage ID ("base:ci_training") to a display name
 * ("Ci Training").  Writes into out[outsz]. */
static void stage_id_to_name(const char *stage_id, char *out, size_t outsz)
{
    const char *colon = stage_id ? strchr(stage_id, ':') : NULL;
    const char *slug  = colon ? colon + 1 : (stage_id ? stage_id : "");
    size_t i, n;
    s32    cap;

    n   = strlen(slug);
    if (n >= outsz) n = outsz - 1;
    cap = 1; /* capitalize first letter of each word */
    for (i = 0; i < n; i++) {
        char c = slug[i];
        if (c == '_') { out[i] = ' '; cap = 1; }
        else if (cap)  { out[i] = (char)toupper((unsigned char)c); cap = 0; }
        else           { out[i] = c; }
    }
    out[n] = '\0';
}

/* ── presence build ──────────────────────────────────────────────── */

typedef struct {
    char        details[128]; /* top line   */
    char        state[128];   /* bottom line */
    const char *small_icon;
    const char *small_text;
} disc_activity_t;

static void disc_build_activity(disc_activity_t *a)
{
    s32  playerCount;
    s32  botCount;
    u32  diff;
    u32  scenario;
    char stageName[64];

    memset(a, 0, sizeof(*a));

    if (g_NetDedicated) {
        _snprintf(a->details, sizeof(a->details), "Running Dedicated Server");
        a->small_icon = "";
        a->small_text  = "";
        return;
    }

    if (STAGE_IS_GAMEPLAY(g_StageNum) && forgeSessionIsActive()) {
        stage_id_to_name(g_MissionConfig.stage_id, stageName, sizeof(stageName));
        _snprintf(a->details, sizeof(a->details), "Editing in The Grid");
        _snprintf(a->state,   sizeof(a->state),   "%s", stageName);
        a->small_icon = "icon_forge";
        a->small_text  = "The Grid";
        return;
    }

    if (STAGE_IS_GAMEPLAY(g_StageNum)) {
        stage_id_to_name(g_MissionConfig.stage_id, stageName, sizeof(stageName));
        playerCount = mpGetActivePlayerCount();
        botCount    = mpGetActiveBotCount();

        if (g_Vars.normmplayerisrunning || g_NetMode != NETMODE_NONE) {
            if (g_NetGameMode == NETGAMEMODE_COOP) {
                _snprintf(a->details, sizeof(a->details), "Co-op: %s", stageName);
                _snprintf(a->state,   sizeof(a->state),   "%dP", playerCount);
                a->small_icon = "icon_coop";
                a->small_text  = "Co-op";
            } else if (g_NetGameMode == NETGAMEMODE_ANTI) {
                _snprintf(a->details, sizeof(a->details), "Counter-Op: %s", stageName);
                _snprintf(a->state,   sizeof(a->state),   "%dP", playerCount);
                a->small_icon = "icon_counterop";
                a->small_text  = "Counter-Op";
            } else {
                const char *scenName;
                scenario = (u32)g_MpSetup.scenario;
                scenName = (scenario < 6) ? k_ScenarioNames[scenario] : "Combat";
                _snprintf(a->details, sizeof(a->details), "Combat Simulator: %s", stageName);
                if (botCount > 0) {
                    _snprintf(a->state, sizeof(a->state), "%s [%dP + %d Bots]",
                        scenName, playerCount, botCount);
                } else {
                    _snprintf(a->state, sizeof(a->state), "%s [%dP]",
                        scenName, playerCount);
                }
                a->small_icon = "icon_combat";
                a->small_text  = "Combat Simulator";
            }
        } else {
            /* Solo mission. */
            diff = (u32)(g_MissionConfig.difficulty & 0x7fu);
            _snprintf(a->details, sizeof(a->details), "%s", stageName);
            _snprintf(a->state,   sizeof(a->state),
                "%s", (diff < 4) ? k_DifficultyNames[diff] : "Agent");
            a->small_icon = "icon_solo";
            a->small_text  = "Solo Mission";
        }
        return;
    }

    /* Not in a gameplay stage: lobby or main menu. */
    if (g_NetMode != NETMODE_NONE) {
        playerCount = mpGetActivePlayerCount();
        _snprintf(a->details, sizeof(a->details), "In Lobby");
        if (playerCount > 0) {
            _snprintf(a->state, sizeof(a->state),
                "Waiting for Players [%d/8]", playerCount);
        } else {
            _snprintf(a->state, sizeof(a->state), "Browsing");
        }
        a->small_icon = "icon_combat";
        a->small_text  = "Multiplayer";
        return;
    }

    _snprintf(a->details, sizeof(a->details), "In Main Menu");
    a->state[0]   = '\0';
    a->small_icon = "";
    a->small_text  = "";
}

/* ── SET_ACTIVITY send ───────────────────────────────────────────── */

/* Copy src into dst as a JSON string value — escape \ and " so that mod
 * stage slugs with unusual characters cannot corrupt the JSON frame. */
static void disc_json_str(const char *src, char *dst, size_t dstsz)
{
    size_t di = 0;
    if (!src) { dst[0] = '\0'; return; }
    while (*src && di + 2 < dstsz) {
        if (*src == '\\' || *src == '"') {
            if (di + 3 > dstsz) break;
            dst[di++] = '\\';
        }
        dst[di++] = (unsigned char)*src++;
    }
    dst[di] = '\0';
}

static s32 disc_send_activity(const disc_activity_t *a)
{
    char buf[DISCORD_PAYLOAD_MAX];
    char esc_details[128];
    char esc_state[128];
    s32  len;

    disc_json_str(a->details, esc_details, sizeof(esc_details));
    disc_json_str(a->state,   esc_state,   sizeof(esc_state));

    s_Nonce++;

    if (a->small_icon && a->small_icon[0]) {
        len = _snprintf(buf, sizeof(buf),
            "{"
              "\"cmd\":\"SET_ACTIVITY\","
              "\"args\":{"
                "\"pid\":%lu,"
                "\"activity\":{"
                  "\"details\":\"%s\","
                  "\"state\":\"%s\","
                  "\"timestamps\":{\"start\":%lld},"
                  "\"assets\":{"
                    "\"large_image\":\"pd2_logo\","
                    "\"large_text\":\"Perfect Dark 2 PC Port\","
                    "\"small_image\":\"%s\","
                    "\"small_text\":\"%s\""
                  "}"
                "}"
              "},"
              "\"nonce\":\"%u\""
            "}",
            (unsigned long)GetCurrentProcessId(),
            esc_details, esc_state,
            (long long)s_ActivityStart,
            a->small_icon, a->small_text ? a->small_text : "",
            s_Nonce);
    } else {
        len = _snprintf(buf, sizeof(buf),
            "{"
              "\"cmd\":\"SET_ACTIVITY\","
              "\"args\":{"
                "\"pid\":%lu,"
                "\"activity\":{"
                  "\"details\":\"%s\","
                  "\"timestamps\":{\"start\":%lld},"
                  "\"assets\":{"
                    "\"large_image\":\"pd2_logo\","
                    "\"large_text\":\"Perfect Dark 2 PC Port\""
                  "}"
                "}"
              "},"
              "\"nonce\":\"%u\""
            "}",
            (unsigned long)GetCurrentProcessId(),
            esc_details,
            (long long)s_ActivityStart,
            s_Nonce);
    }

    if (len <= 0 || len >= DISCORD_PAYLOAD_MAX) return 0;
    return disc_write(DISCORD_OPCODE_FRAME, buf, (DWORD)len);
}

/* ── public API ──────────────────────────────────────────────────── */

void discordInit(void)
{
    time_t now = time(NULL);

    if (s_Pipe != INVALID_HANDLE_VALUE) return;

    if (!disc_try_connect()) {
        s_LastConnect = now;
        return;
    }
    if (!disc_send_handshake()) {
        disc_disconnect();
        s_LastConnect = now;
        return;
    }

    s_Ready         = 1;
    s_LastConnect   = now;
    s_ActivityStart = now;
}

void discordShutdown(void)
{
    disc_disconnect();
}

void discordTick(void)
{
    disc_activity_t a;
    time_t          now = time(NULL);
    s32             changed;

    if (s_Pipe == INVALID_HANDLE_VALUE || !s_Ready) {
        if (now - s_LastConnect >= DISCORD_RECONNECT_SECS) {
            discordInit();
        }
        return;
    }

    disc_build_activity(&a);

    changed = (strcmp(a.details, s_PrevDetails) != 0 ||
               strcmp(a.state,   s_PrevState)   != 0);
    if (changed) s_ActivityStart = now;

    if (!changed && now - s_LastUpdate < DISCORD_TICK_SECS) return;

    disc_drain();
    if (s_Pipe == INVALID_HANDLE_VALUE) return;

    if (!disc_send_activity(&a)) {
        disc_disconnect();
        s_LastConnect = now;
        return;
    }

    strncpy(s_PrevDetails, a.details, sizeof(s_PrevDetails) - 1);
    strncpy(s_PrevState,   a.state,   sizeof(s_PrevState) - 1);
    s_PrevDetails[sizeof(s_PrevDetails) - 1] = '\0';
    s_PrevState[sizeof(s_PrevState) - 1]     = '\0';
    s_LastUpdate = now;
}
