/**
 * netstun.c -- STUN client for external address discovery (RFC 5389 subset).
 *
 * Sends STUN Binding Requests to public STUN servers to discover the
 * external (NAT-facing) IP and port of the server's UDP socket.
 *
 * Background thread pattern mirrors netupnp.c / upnpWorkerThread.
 *
 * RFC 5389 minimal subset:
 *   - Binding Request / Binding Success Response only (no auth, no TURN, no ICE)
 *   - XOR-MAPPED-ADDRESS (0x0020) with MAPPED-ADDRESS (0x0001) fallback
 *   - Two-probe NAT type detection: if both probes yield the same external port
 *     the NAT is cone (hole punch viable); if they differ it is symmetric.
 *
 * Socket binding note: the STUN socket binds to the same local port as ENet
 * using SO_REUSEADDR. This ensures the NAT device maps the same (internal IP,
 * internal port) pair, so the discovered external port matches the port ENet
 * clients will reach. On Windows the OS may deliver STUN responses to either
 * socket; if this causes a missed response the probe times out and falls back
 * to the next STUN server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>
#include "types.h"
#include "system.h"
#include "net/netstun.h"
#include "connectcode.h"

/* -------------------------------------------------------------------------
 * Platform socket layer
 * ---------------------------------------------------------------------- */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET      stunSock_t;
#  define STUN_BAD_SOCKET   INVALID_SOCKET
#  define stunSockClose(s)  closesocket(s)
/* Windows select() ignores the nfds argument */
#  define STUN_SELECT_NFDS(s) 0
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <pthread.h>
typedef int         stunSock_t;
#  define STUN_BAD_SOCKET   (-1)
#  define stunSockClose(s)  close(s)
#  define STUN_SELECT_NFDS(s) ((int)(s) + 1)
#endif

/* -------------------------------------------------------------------------
 * STUN server list — tried in order until STUN_MAX_PROBES succeed or all fail
 * ---------------------------------------------------------------------- */

#define STUN_MAX_PROBES  2   /* number of probes for NAT type detection */
#define STUN_TIMEOUT_SEC 3   /* per-server wait time in seconds */

static const char *const s_StunServers[] = {
    "stun.l.google.com:19302",
    "stun1.l.google.com:19302",
    "stun.cloudflare.com:3478",
    NULL
};

/* -------------------------------------------------------------------------
 * Shared state — written by worker thread, read by main thread.
 * All written fields are volatile to prevent compiler optimisation.
 * ---------------------------------------------------------------------- */

static volatile s32 s_StunStatus  = STUN_STATUS_IDLE;
static volatile s32 s_StunNatType = STUN_NAT_UNKNOWN;
static volatile s32 s_StunCancel  = 0;
static volatile u16 s_StunPort    = 0;

/* Written only by worker thread before setting SUCCESS; safe to read after. */
static char s_StunExternalIP[64] = {0};

/* Local port to bind — set before launching the thread. */
static u16  s_BindPort = 0;

/* -------------------------------------------------------------------------
 * STUN wire-format constants
 * ---------------------------------------------------------------------- */

#define STUN_MAGIC_COOKIE        0x2112A442u
#define STUN_HDR_SIZE            20          /* 2+2+4+12 */
#define STUN_ATTR_HDR_SIZE       4           /* type(2) + length(2) */

#define STUN_MSG_BIND_REQUEST    0x0001u
#define STUN_MSG_BIND_RESPONSE   0x0101u

#define STUN_ATTR_MAPPED_ADDRESS     0x0001u
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020u

#define STUN_FAMILY_IPV4 0x01u
#define STUN_FAMILY_IPV6 0x02u

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* Build a minimal STUN Binding Request (20 bytes, no attributes).
   Writes to buf (caller provides >= STUN_HDR_SIZE bytes).
   tid_out receives the 12-byte transaction ID used to match the response. */
static void stunBuildRequest(u8 *buf, u8 tid[12])
{
    s32 i;
    for (i = 0; i < 12; i++) {
        tid[i] = (u8)(rand() & 0xFF);
    }

    /* Message type: Binding Request, big-endian */
    buf[0] = 0x00; buf[1] = 0x01;
    /* Message length: 0 (no attributes) */
    buf[2] = 0x00; buf[3] = 0x00;
    /* Magic Cookie: 0x2112A442 */
    buf[4] = 0x21; buf[5] = 0x12; buf[6] = 0xA4; buf[7] = 0x42;
    /* Transaction ID */
    memcpy(buf + 8, tid, 12);
}

/* Parse a STUN Binding Success Response.
   Returns 1 and fills *outIP (host byte order, big-endian packed) and
   *outPort (host byte order) on success; returns 0 on parse failure. */
static s32 stunParseResponse(const u8 *buf, s32 len, const u8 tid[12],
                              u32 *outIP, u16 *outPort)
{
    u16 msgType, msgLen;
    u32 magic;
    s32 off, end;
    u32 mappedIP   = 0;
    u16 mappedPort = 0;
    s32 gotXOR     = 0;

    if (len < STUN_HDR_SIZE) {
        return 0;
    }

    /* Message type */
    msgType = ((u16)buf[0] << 8) | buf[1];
    if (msgType != STUN_MSG_BIND_RESPONSE) {
        return 0;
    }

    /* Magic cookie */
    magic = ((u32)buf[4] << 24) | ((u32)buf[5] << 16) |
            ((u32)buf[6] << 8)  | buf[7];
    if (magic != STUN_MAGIC_COOKIE) {
        return 0;
    }

    /* Transaction ID match */
    if (memcmp(buf + 8, tid, 12) != 0) {
        return 0;
    }

    msgLen = ((u16)buf[2] << 8) | buf[3];
    if ((s32)(STUN_HDR_SIZE + msgLen) > len) {
        return 0;
    }

    off = STUN_HDR_SIZE;
    end = STUN_HDR_SIZE + (s32)msgLen;

    while (off + STUN_ATTR_HDR_SIZE <= end) {
        u16 atype = ((u16)buf[off] << 8)     | buf[off + 1];
        u16 alen  = ((u16)buf[off + 2] << 8) | buf[off + 3];
        off += STUN_ATTR_HDR_SIZE;

        if (off + (s32)alen > end) {
            break;
        }

        if (atype == STUN_ATTR_XOR_MAPPED_ADDRESS && alen >= 8) {
            /* buf[off] = reserved, buf[off+1] = family */
            if (buf[off + 1] == STUN_FAMILY_IPV4) {
                /* XOR port with top 16 bits of magic cookie */
                u16 rawPort = ((u16)buf[off + 2] << 8) | buf[off + 3];
                mappedPort = rawPort ^ 0x2112u;
                /* XOR IP with full magic cookie */
                u32 rawIP = ((u32)buf[off + 4] << 24) | ((u32)buf[off + 5] << 16) |
                            ((u32)buf[off + 6] << 8)  |  buf[off + 7];
                mappedIP = rawIP ^ STUN_MAGIC_COOKIE;
                gotXOR = 1;
            }
        } else if (atype == STUN_ATTR_MAPPED_ADDRESS && alen >= 8 && !gotXOR) {
            /* Plain (non-XOR) fallback for older servers */
            if (buf[off + 1] == STUN_FAMILY_IPV4) {
                mappedPort = ((u16)buf[off + 2] << 8) | buf[off + 3];
                mappedIP   = ((u32)buf[off + 4] << 24) | ((u32)buf[off + 5] << 16) |
                             ((u32)buf[off + 6] << 8)  |  buf[off + 7];
            }
        }

        /* Attributes are padded to 4-byte boundaries */
        off += (s32)((alen + 3u) & ~3u);
    }

    if (!mappedIP || !mappedPort) {
        return 0;
    }

    /* mappedIP is in big-endian (network) packed form: top byte = first octet */
    *outIP   = mappedIP;
    *outPort = mappedPort;
    return 1;
}

/* Resolve "hostname:port" string into a sockaddr_in.
   Returns 1 on success, 0 on failure. */
static s32 stunResolveServer(const char *hostport, struct sockaddr_in *out)
{
    char host[256];
    char portStr[8];
    u16 port = 3478; /* default STUN port */
    char *colon;
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    strncpy(host, hostport, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';

    colon = strrchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = (u16)atoi(colon + 1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    snprintf(portStr, sizeof(portStr), "%u", (unsigned)port);

    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res) {
        return 0;
    }

    memcpy(out, res->ai_addr, sizeof(*out));
    freeaddrinfo(res);
    return 1;
}

/* Send one STUN Binding Request to hostport and wait up to STUN_TIMEOUT_SEC
   seconds for a response.
   Returns 1 on success (outIP/outPort filled), 0 on timeout or parse error. */
static s32 stunProbe(stunSock_t sock, const char *hostport,
                     u32 *outIP, u16 *outPort)
{
    struct sockaddr_in serverAddr;
    u8 reqbuf[STUN_HDR_SIZE];
    u8 tid[12];
    u8 rxbuf[512];
    struct timeval tv;
    fd_set rfds;
    struct sockaddr_in fromAddr;
    int fromLen;
    s32 rxlen;
    s32 sel;

    if (!stunResolveServer(hostport, &serverAddr)) {
        sysLogPrintf(LOG_WARNING, "STUN: could not resolve %s", hostport);
        return 0;
    }

    stunBuildRequest(reqbuf, tid);

    if (sendto(sock, (const char *)reqbuf, STUN_HDR_SIZE, 0,
               (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        sysLogPrintf(LOG_WARNING, "STUN: sendto failed for %s", hostport);
        return 0;
    }

    tv.tv_sec  = STUN_TIMEOUT_SEC;
    tv.tv_usec = 0;

    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    sel = select(STUN_SELECT_NFDS(sock), &rfds, NULL, NULL, &tv);
    if (sel <= 0) {
        sysLogPrintf(LOG_WARNING, "STUN: timeout waiting for response from %s", hostport);
        return 0;
    }

    fromLen = sizeof(fromAddr);
    rxlen = (s32)recvfrom(sock, (char *)rxbuf, (int)sizeof(rxbuf), 0,
                           (struct sockaddr *)&fromAddr, &fromLen);
    if (rxlen <= 0) {
        return 0;
    }

    return stunParseResponse(rxbuf, rxlen, tid, outIP, outPort);
}

/* -------------------------------------------------------------------------
 * Worker thread
 * ---------------------------------------------------------------------- */

#ifdef _WIN32
static DWORD WINAPI stunWorkerThread(LPVOID param)
#else
static void *stunWorkerThread(void *param)
#endif
{
    stunSock_t sock;
    struct sockaddr_in local;
    s32 yes = 1;
    s32 i;
    u32 ip1 = 0, ip2 = 0;
    u16 port1 = 0, port2 = 0;
    s32 probes = 0;
    s32 succeeded = 0;

    (void)param;
    s_StunStatus = STUN_STATUS_WORKING;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == STUN_BAD_SOCKET) {
        sysLogPrintf(LOG_WARNING, "STUN: [thread] could not create socket");
        s_StunStatus = STUN_STATUS_FAILED;
        return 0;
    }

    /* SO_REUSEADDR so we can bind to the same port as ENet */
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port        = htons(s_BindPort);

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        sysLogPrintf(LOG_WARNING, "STUN: [thread] bind to port %u failed", (unsigned)s_BindPort);
        stunSockClose(sock);
        s_StunStatus = STUN_STATUS_FAILED;
        return 0;
    }

    sysLogPrintf(LOG_NOTE, "STUN: [thread] probing (bound to port %u)...", (unsigned)s_BindPort);

    for (i = 0; s_StunServers[i] && probes < STUN_MAX_PROBES; i++) {
        u32  ip   = 0;
        u16  port = 0;

        if (s_StunCancel) {
            break;
        }

        if (!stunProbe(sock, s_StunServers[i], &ip, &port)) {
            /* Try next server */
            continue;
        }

        if (probes == 0) {
            /* First successful probe — record result */
            u8 a = (u8)(ip >> 24);
            u8 b = (u8)(ip >> 16);
            u8 c = (u8)(ip >> 8);
            u8 d = (u8)(ip);

            ip1   = ip;
            port1 = port;
            snprintf(s_StunExternalIP, sizeof(s_StunExternalIP),
                     "%u.%u.%u.%u", (unsigned)a, (unsigned)b,
                     (unsigned)c, (unsigned)d);
            s_StunPort = port;
            succeeded  = 1;

            sysLogPrintf(LOG_NOTE, "STUN: [thread] probe 1 => %s:%u via %s",
                         s_StunExternalIP, (unsigned)port, s_StunServers[i]);
        } else {
            /* Second probe — compare ports for NAT type */
            ip2   = ip;
            port2 = port;

            if (port1 == port2) {
                s_StunNatType = STUN_NAT_CONE;
                sysLogPrintf(LOG_NOTE,
                    "STUN: [thread] probe 2 => %u.%u.%u.%u:%u (port match — cone NAT, hole punch viable)",
                    (unsigned)(u8)(ip >> 24), (unsigned)(u8)(ip >> 16),
                    (unsigned)(u8)(ip >> 8),  (unsigned)(u8)ip,
                    (unsigned)port2);
            } else {
                s_StunNatType = STUN_NAT_SYMMETRIC;
                sysLogPrintf(LOG_WARNING,
                    "STUN: [thread] probe 2 => port %u (different from %u) — symmetric NAT, hole punch will likely fail",
                    (unsigned)port2, (unsigned)port1);
            }
        }
        probes++;
    }

    stunSockClose(sock);

    if (succeeded) {
        /* Log connect code using sentence system.
         * Convert big-endian packed IP to the little-endian convention
         * expected by connectCodeEncodeWithPort (byte0 = first octet). */
        {
            u8 a, b, c, d;
            u32 ipForCode;
            char code[256];

            a = (u8)(ip1 >> 24);
            b = (u8)(ip1 >> 16);
            c = (u8)(ip1 >> 8);
            d = (u8)(ip1);
            ipForCode = (u32)a | ((u32)b << 8) | ((u32)c << 16) | ((u32)d << 24);
            connectCodeEncodeWithPort(ipForCode, port1, code, sizeof(code));
            sysLogPrintf(LOG_NOTE, "STUN: Connect code: %s", code);
        }
        s_StunStatus = STUN_STATUS_SUCCESS;
    } else if (!s_StunCancel) {
        sysLogPrintf(LOG_WARNING, "STUN: [thread] all servers failed — external address unknown");
        s_StunStatus = STUN_STATUS_FAILED;
    } else {
        s_StunStatus = STUN_STATUS_FAILED;
    }

    /* Suppress unused variable warnings when only one probe succeeds */
    (void)ip2;
    (void)port2;

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void stunInit(void)
{
    s_StunStatus        = STUN_STATUS_IDLE;
    s_StunNatType       = STUN_NAT_UNKNOWN;
    s_StunCancel        = 0;
    s_StunPort          = 0;
    s_StunExternalIP[0] = '\0';
}

void stunShutdown(void)
{
    /* Signal the worker thread to stop at the next cancel check.
     * The thread is detached so we cannot join it; it will exit on its own. */
    s_StunCancel = 1;
    stunInit();
}

s32 stunDiscoverAsync(u16 localport)
{
    if (s_StunStatus == STUN_STATUS_WORKING) {
        return 0; /* Already in progress */
    }

    s_BindPort          = localport;
    s_StunStatus        = STUN_STATUS_IDLE;
    s_StunNatType       = STUN_NAT_UNKNOWN;
    s_StunCancel        = 0;
    s_StunPort          = 0;
    s_StunExternalIP[0] = '\0';

    sysLogPrintf(LOG_NOTE, "STUN: starting async discovery for port %u...", (unsigned)localport);

#ifdef _WIN32
    {
        HANDLE thread = CreateThread(NULL, 0, stunWorkerThread, NULL, 0, NULL);
        if (thread) {
            CloseHandle(thread); /* Detach — we don't need to join */
        } else {
            sysLogPrintf(LOG_WARNING, "STUN: could not create worker thread");
            s_StunStatus = STUN_STATUS_FAILED;
            return -1;
        }
    }
#else
    {
        pthread_t thread;
        if (pthread_create(&thread, NULL, stunWorkerThread, NULL) == 0) {
            pthread_detach(thread);
        } else {
            sysLogPrintf(LOG_WARNING, "STUN: could not create worker thread");
            s_StunStatus = STUN_STATUS_FAILED;
            return -1;
        }
    }
#endif

    return 0;
}

s32 stunGetStatus(void)
{
    return s_StunStatus;
}

s32 stunGetNatType(void)
{
    return s_StunNatType;
}

const char *stunGetExternalIP(void)
{
    return s_StunExternalIP;
}

u16 stunGetExternalPort(void)
{
    return s_StunPort;
}

void stunCancel(void)
{
    s_StunCancel = 1;
}
