/**
 * logview.c -- Standalone log filter tool for Perfect Dark 2.
 *
 * Reads PD2 log file format: [MM:SS.ms] PREFIX: message
 * Filters by channel, severity, and text search.
 *
 * Usage:
 *   logview [options] [file]
 *
 * Options:
 *   --channel NET,CATALOG    Comma-separated channel names to include
 *   --level   WARN,ERROR     Comma-separated level names to include
 *   --search  "desync"       Case-insensitive substring match
 *   --json                   Output structured JSON instead of plain text
 *   --follow                 Tail mode: re-read file and print new lines
 *   --help                   Print this help
 *
 * Channel names (case-insensitive):
 *   NETWORK, GAME, COMBAT, AUDIO, MENU, SAVE, MOD, SYSTEM, MATCH,
 *   CATALOG, DISTRIB, RENDER
 *
 * Level names (case-insensitive):
 *   VERBOSE, NOTE, WARN (or WARNING), ERROR, CHAT
 *
 * Examples:
 *   logview pd.log
 *   logview --channel NET,CATALOG --level ERROR,WARN pd.log
 *   logview --search "desync" --json pd.log
 *   logview --follow pd-server.log
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
/* strcasecmp is POSIX; on Windows use _stricmp */
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#ifdef _WIN32
#include <windows.h>
#define WIN32_SLEEP(ms) Sleep(ms)
#else
#include <unistd.h>
#define WIN32_SLEEP(ms) usleep((ms)*1000)
#endif

/* -----------------------------------------------------------------------
 * Portable case-insensitive substring search
 * ----------------------------------------------------------------------- */

static const char *logview_casestr(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            size_t i;
            for (i = 1; i < nlen; i++) {
                if (!haystack[i]) return NULL;
                if (tolower((unsigned char)haystack[i]) != tolower((unsigned char)needle[i])) break;
            }
            if (i == nlen) return haystack;
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Channel definitions (must match system.h)
 * ----------------------------------------------------------------------- */

#define CH_NETWORK  0x0001
#define CH_GAME     0x0002
#define CH_COMBAT   0x0004
#define CH_AUDIO    0x0008
#define CH_MENU     0x0010
#define CH_SAVE     0x0020
#define CH_MOD      0x0040
#define CH_SYSTEM   0x0080
#define CH_MATCH    0x0100
#define CH_CATALOG  0x0200
#define CH_DISTRIB  0x0400
#define CH_RENDER   0x0800
#define CH_ALL      0xFFFF
#define CH_NONE     0x0000

typedef struct {
    const char *name;
    unsigned int bit;
} ChannelDef;

static const ChannelDef s_Channels[] = {
    { "NETWORK", CH_NETWORK },
    { "NET",     CH_NETWORK },  /* short alias */
    { "GAME",    CH_GAME    },
    { "COMBAT",  CH_COMBAT  },
    { "AUDIO",   CH_AUDIO   },
    { "MENU",    CH_MENU    },
    { "SAVE",    CH_SAVE    },
    { "MOD",     CH_MOD     },
    { "MODS",    CH_MOD     },  /* alias */
    { "SYSTEM",  CH_SYSTEM  },
    { "SYS",     CH_SYSTEM  },  /* short alias */
    { "MATCH",   CH_MATCH   },
    { "CATALOG", CH_CATALOG },
    { "CAT",     CH_CATALOG },  /* short alias */
    { "DISTRIB", CH_DISTRIB },
    { "RENDER",  CH_RENDER  },
};
#define NUM_CHANNELS (int)(sizeof(s_Channels)/sizeof(s_Channels[0]))

/* -----------------------------------------------------------------------
 * Level definitions
 * ----------------------------------------------------------------------- */

#define LVL_VERBOSE  (1<<0)
#define LVL_NOTE     (1<<1)
#define LVL_WARNING  (1<<2)
#define LVL_ERROR    (1<<3)
#define LVL_CHAT     (1<<4)
#define LVL_ALL      0x1F

typedef struct {
    const char *name;
    unsigned int bit;
    const char *filePrefix; /* prefix as it appears in the log file */
} LevelDef;

static const LevelDef s_Levels[] = {
    { "VERBOSE", LVL_VERBOSE, "VERBOSE: " },
    { "NOTE",    LVL_NOTE,    ""           },
    { "WARN",    LVL_WARNING, "WARNING: "  },
    { "WARNING", LVL_WARNING, "WARNING: "  },
    { "ERROR",   LVL_ERROR,   "ERROR: "    },
    { "CHAT",    LVL_CHAT,    "CHAT: "     },
};
#define NUM_LEVELS (int)(sizeof(s_Levels)/sizeof(s_Levels[0]))

/* -----------------------------------------------------------------------
 * Channel classifier (mirrors sysLogClassifyMessage from system.c)
 * ----------------------------------------------------------------------- */

static unsigned int classifyMessage(const char *msg)
{
    /* Strip level prefix if present */
    if (strncmp(msg, "WARNING: ", 9) == 0) msg += 9;
    else if (strncmp(msg, "ERROR: ",  7) == 0) msg += 7;
    else if (strncmp(msg, "VERBOSE: ",9) == 0) msg += 9;
    else if (strncmp(msg, "CHAT: ",   6) == 0) msg += 6;

    /* Network */
    if (strncmp(msg, "NET:",        4) == 0) return CH_NETWORK;
    if (strncmp(msg, "UPNP:",       5) == 0) return CH_NETWORK;
    if (strncmp(msg, "SERVER:",     7) == 0) return CH_NETWORK;
    if (strncmp(msg, "LOBBY:",      6) == 0) return CH_NETWORK;
    if (strncmp(msg, "MATCHSETUP:",11) == 0) return CH_NETWORK;
    /* Game */
    if (strncmp(msg, "STAGE:",      6) == 0) return CH_GAME;
    if (strncmp(msg, "INTRO:",      6) == 0) return CH_GAME;
    if (strncmp(msg, "LOAD:",       5) == 0) return CH_GAME;
    if (strncmp(msg, "PLAYER:",     7) == 0) return CH_GAME;
    if (strncmp(msg, "SIMULANT:",   9) == 0) return CH_GAME;
    if (strncmp(msg, "SETUP:",      6) == 0) return CH_GAME;
    if (strncmp(msg, "JUMP:",       5) == 0) return CH_GAME;
    /* Match */
    if (strncmp(msg, "MATCH:",      6) == 0) return CH_MATCH;
    if (strncmp(msg, "CHRSLOTS:",   9) == 0) return CH_MATCH;
    if (strncmp(msg, "BOT_ALLOC:", 10) == 0) return CH_MATCH;
    /* Combat */
    if (strncmp(msg, "COMBAT:",     7) == 0) return CH_COMBAT;
    if (strncmp(msg, "DAMAGE:",     7) == 0) return CH_COMBAT;
    if (strncmp(msg, "WEAPON:",     7) == 0) return CH_COMBAT;
    if (strncmp(msg, "AMMO:",       5) == 0) return CH_COMBAT;
    if (strncmp(msg, "HEALTH:",     7) == 0) return CH_COMBAT;
    if (strncmp(msg, "PICKUP:",     7) == 0) return CH_COMBAT;
    /* Audio */
    if (strncmp(msg, "SND:",        4) == 0) return CH_AUDIO;
    if (strncmp(msg, "AUDIO:",      6) == 0) return CH_AUDIO;
    if (strncmp(msg, "MUSIC:",      6) == 0) return CH_AUDIO;
    if (strncmp(msg, "SFX:",        4) == 0) return CH_AUDIO;
    /* Menu */
    if (strncmp(msg, "MENU:",       5) == 0) return CH_MENU;
    if (strncmp(msg, "HOTSWAP:",    8) == 0) return CH_MENU;
    if (strncmp(msg, "DIALOG:",     7) == 0) return CH_MENU;
    if (strncmp(msg, "FONT",        4) == 0) return CH_MENU;
    /* Save */
    if (strncmp(msg, "SAVE:",        5) == 0) return CH_SAVE;
    if (strncmp(msg, "SAVEMIGRATE:",12) == 0) return CH_SAVE;
    if (strncmp(msg, "CONFIG:",      7) == 0) return CH_SAVE;
    /* Mods */
    if (strncmp(msg, "MOD:",        4) == 0) return CH_MOD;
    if (strncmp(msg, "MODMGR:",     7) == 0) return CH_MOD;
    if (strncmp(msg, "MODLOAD:",    8) == 0) return CH_MOD;
    /* Catalog */
    if (strncmp(msg, "CATALOG:",    8) == 0) return CH_CATALOG;
    if (strncmp(msg, "MANIFEST:",   9) == 0) return CH_CATALOG;
    if (strncmp(msg, "ASSET:",      6) == 0) return CH_CATALOG;
    /* Distrib */
    if (strncmp(msg, "DISTRIB:",    8) == 0) return CH_DISTRIB;
    /* Render */
    if (strncmp(msg, "RENDER:",     7) == 0) return CH_RENDER;
    if (strncmp(msg, "GFX:",        4) == 0) return CH_RENDER;
    if (strncmp(msg, "TEXTURE:",    8) == 0) return CH_RENDER;
    if (strncmp(msg, "FAST3D:",     7) == 0) return CH_RENDER;
    /* System */
    if (strncmp(msg, "SYS:",        4) == 0) return CH_SYSTEM;
    if (strncmp(msg, "MEM:",        4) == 0) return CH_SYSTEM;
    if (strncmp(msg, "MEMPC:",      6) == 0) return CH_SYSTEM;
    if (strncmp(msg, "CRASH:",      6) == 0) return CH_SYSTEM;
    if (strncmp(msg, "FS:",         3) == 0) return CH_SYSTEM;
    if (strncmp(msg, "UPDATER:",    8) == 0) return CH_SYSTEM;

    return 0; /* untagged */
}

/* Detect severity level from line content */
static unsigned int classifyLevel(const char *msg)
{
    if (strncmp(msg, "VERBOSE: ", 9) == 0) return LVL_VERBOSE;
    if (strncmp(msg, "WARNING: ", 9) == 0) return LVL_WARNING;
    if (strncmp(msg, "ERROR: ",   7) == 0) return LVL_ERROR;
    if (strncmp(msg, "CHAT: ",    6) == 0) return LVL_CHAT;
    return LVL_NOTE;
}

/* -----------------------------------------------------------------------
 * Argument helpers
 * ----------------------------------------------------------------------- */

static unsigned int parseChannelList(const char *list)
{
    if (!list || !*list) return CH_NONE;

    unsigned int mask = 0;
    char buf[256];
    strncpy(buf, list, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';

    char *tok = strtok(buf, ",");
    while (tok) {
        int found = 0;
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (strcasecmp(tok, s_Channels[i].name) == 0) {
                mask |= s_Channels[i].bit;
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "logview: unknown channel '%s'\n", tok);
        }
        tok = strtok(NULL, ",");
    }
    return mask;
}

static unsigned int parseLevelList(const char *list)
{
    if (!list || !*list) return LVL_ALL;

    unsigned int mask = 0;
    char buf[256];
    strncpy(buf, list, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';

    char *tok = strtok(buf, ",");
    while (tok) {
        int found = 0;
        for (int i = 0; i < NUM_LEVELS; i++) {
            if (strcasecmp(tok, s_Levels[i].name) == 0) {
                mask |= s_Levels[i].bit;
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "logview: unknown level '%s'\n", tok);
        }
        tok = strtok(NULL, ",");
    }
    return mask;
}

/* -----------------------------------------------------------------------
 * Help
 * ----------------------------------------------------------------------- */

static void printHelp(const char *prog)
{
    fprintf(stdout,
        "Usage: %s [options] [file]\n"
        "\n"
        "Options:\n"
        "  --channel NET,CATALOG   Comma-separated channels to include\n"
        "  --level   WARN,ERROR    Comma-separated levels to include\n"
        "  --search  TEXT          Case-insensitive substring filter\n"
        "  --json                  Output JSON (one object per line)\n"
        "  --follow                Tail mode: poll for new lines\n"
        "  --help                  Show this help\n"
        "\n"
        "Channels: NETWORK GAME COMBAT AUDIO MENU SAVE MOD SYSTEM MATCH CATALOG DISTRIB RENDER\n"
        "Levels:   VERBOSE NOTE WARN ERROR CHAT\n"
        "\n"
        "If [file] is omitted, reads from stdin.\n",
        prog);
}

/* -----------------------------------------------------------------------
 * JSON string escaping
 * ----------------------------------------------------------------------- */

static void jsonEscapeString(FILE *out, const char *s)
{
    fputc('"', out);
    for (; *s; s++) {
        switch (*s) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n",  out); break;
            case '\r': fputs("\\r",  out); break;
            case '\t': fputs("\\t",  out); break;
            default:   fputc(*s,     out); break;
        }
    }
    fputc('"', out);
}

static const char *channelName(unsigned int ch)
{
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (s_Channels[i].bit == ch) return s_Channels[i].name;
    }
    return "UNKNOWN";
}

static const char *levelName(unsigned int lvl)
{
    switch (lvl) {
        case LVL_VERBOSE: return "VERBOSE";
        case LVL_NOTE:    return "NOTE";
        case LVL_WARNING: return "WARNING";
        case LVL_ERROR:   return "ERROR";
        case LVL_CHAT:    return "CHAT";
        default:          return "NOTE";
    }
}

/* -----------------------------------------------------------------------
 * Line processing
 * ----------------------------------------------------------------------- */

typedef struct {
    unsigned int channelMask; /* CH_* mask filter */
    unsigned int levelMask;   /* LVL_* mask filter */
    const char  *search;      /* text search (NULL = no filter) */
    int          json;        /* output mode */
} FilterOpts;

static void processLine(const char *line, const FilterOpts *opts)
{
    /* Expected format: [MM:SS.ms] CONTENT
     * Timestamp field is optional — we accept lines without it. */
    const char *content = line;
    char timestamp[32] = "";

    if (line[0] == '[') {
        const char *close = strchr(line, ']');
        if (close && close[1] == ' ') {
            size_t tlen = (size_t)(close - line + 1);
            if (tlen < sizeof(timestamp)) {
                memcpy(timestamp, line, tlen);
                timestamp[tlen] = '\0';
            }
            content = close + 2;
        }
    }

    /* Classify level and channel from content */
    unsigned int lvl = classifyLevel(content);
    unsigned int ch  = classifyMessage(content);

    /* Level filter */
    if (!(opts->levelMask & lvl)) return;

    /* Channel filter: untagged (ch==0) always passes when any channel enabled */
    if (ch != 0 && opts->channelMask != CH_ALL && !(opts->channelMask & ch)) return;

    /* Text search */
    if (opts->search && opts->search[0]) {
        if (!logview_casestr(content, opts->search)) return;
    }

    /* Output */
    if (opts->json) {
        printf("{\"timestamp\":");
        jsonEscapeString(stdout, timestamp);
        printf(",\"level\":\"%s\",\"channel\":\"%s\",\"text\":",
            levelName(lvl),
            ch ? channelName(ch) : "UNTAGGED");
        jsonEscapeString(stdout, content);
        printf("}\n");
    } else {
        if (timestamp[0]) {
            printf("%s %s\n", timestamp, content);
        } else {
            printf("%s\n", content);
        }
    }
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    FilterOpts opts;
    opts.channelMask = CH_ALL;
    opts.levelMask   = LVL_ALL;
    opts.search      = NULL;
    opts.json        = 0;

    int doFollow     = 0;
    const char *file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printHelp(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--channel") == 0 && i + 1 < argc) {
            opts.channelMask = parseChannelList(argv[++i]);
        } else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            opts.levelMask = parseLevelList(argv[++i]);
        } else if (strcmp(argv[i], "--search") == 0 && i + 1 < argc) {
            opts.search = argv[++i];
        } else if (strcmp(argv[i], "--json") == 0) {
            opts.json = 1;
        } else if (strcmp(argv[i], "--follow") == 0) {
            doFollow = 1;
        } else if (argv[i][0] != '-') {
            file = argv[i];
        } else {
            fprintf(stderr, "logview: unknown option '%s' (use --help)\n", argv[i]);
            return 1;
        }
    }

    FILE *in = file ? fopen(file, "r") : stdin;
    if (!in) {
        fprintf(stderr, "logview: cannot open '%s'\n", file);
        return 1;
    }

    char line[4096];

    if (!doFollow) {
        /* Single-pass mode */
        while (fgets(line, sizeof(line), in)) {
            /* Strip trailing newline */
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                line[--len] = '\0';
            }
            if (len > 0) processLine(line, &opts);
        }
    } else {
        /* Follow / tail mode: read all existing content, then poll for new lines */
        while (fgets(line, sizeof(line), in)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                line[--len] = '\0';
            }
            if (len > 0) processLine(line, &opts);
        }
        fflush(stdout);

        /* Poll loop */
        while (1) {
            WIN32_SLEEP(200);
            while (fgets(line, sizeof(line), in)) {
                size_t len = strlen(line);
                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                    line[--len] = '\0';
                }
                if (len > 0) {
                    processLine(line, &opts);
                    fflush(stdout);
                }
            }
            clearerr(in);
        }
    }

    if (file) fclose(in);
    return 0;
}
