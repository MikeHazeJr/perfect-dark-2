/**
 * netholepunch.c -- UDP hole punch and connection waterfall (Phase 3+4).
 *
 * Connection waterfall state machine:
 *
 *   CONN_PHASE_DIRECT
 *     Entry : netStartClient() called with 3s peer timeout.
 *     Win   : ENET_EVENT_CONNECT → netHolePunchOnConnect() → CONN_PHASE_DONE.
 *     Lose  : ENET_EVENT_DISCONNECT_TIMEOUT → netHolePunchHandleTimeout() →
 *               if symmetric NAT detected → CONN_PHASE_DONE (fail, show message)
 *               else                       → CONN_PHASE_PUNCH
 *
 *   CONN_PHASE_PUNCH
 *     Entry : PUNCH_REQ sent via ENet socket to server address.
 *             ENet intercept callback (netHolePunchClientIntercept) watches for
 *             PUNCH_ACK from the server.
 *     Tick  : netHolePunchClientTick() polls s_GotAck or 500ms timeout, then
 *             resets the dead peer and fires a fresh enet_host_connect() with
 *             a 5s timeout → CONN_PHASE_PUNCH_ENET.
 *
 *   CONN_PHASE_PUNCH_ENET
 *     Win   : ENET_EVENT_CONNECT → netHolePunchOnConnect() → CONN_PHASE_DONE.
 *     Lose  : ENET_EVENT_DISCONNECT_TIMEOUT → netHolePunchHandleTimeout() →
 *               returns 0 (normal disconnect proceeds) with diagnostic message.
 *
 *   CONN_PHASE_DONE — terminal (success or failure).
 *
 * Phase 4 diagnostics: when hole punch fails, stunGetNatType() provides the
 * reason so users get a targeted error message rather than a generic timeout.
 *
 * Server side: netServerHandlePunchReq() receives PUNCH_REQ connectionless
 * packets via the ENet intercept callback and replies with 3 PUNCH_ACK
 * packets at 50ms intervals using SDL_Delay (acceptable: ~150ms one-shot,
 * runs only on the pre-auth connectionless path, not in the game loop).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>
#include "types.h"
#include "system.h"
#include "net/netenet.h"
#include "net/net.h"
#include "net/netstun.h"
#include "net/netholepunch.h"

/* SDL for SDL_GetTicks() and SDL_Delay() */
#include <SDL2/SDL.h>

/* -------------------------------------------------------------------------
 * Client waterfall state
 * ---------------------------------------------------------------------- */

static s32   s_ConnPhase    = CONN_PHASE_IDLE;
static u32   s_PhaseStartMs = 0;
static s32   s_GotAck       = 0;
static char  s_ConnAddr[NET_MAX_ADDR + 1] = {0};
static char  s_ErrorMsg[256] = {0};

/* Parsed server ENetAddress — stored when waterfall begins so we can
   reconnect after the hole punch without re-parsing the address string. */
static ENetAddress s_ServerAddr;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/* Build a PUNCH_REQ datagram into buf (must be >= PUNCH_REQ_LEN bytes).
   Fills in the client's external address (from STUN if available, else 0). */
static void buildPunchReq(u8 *buf)
{
    memcpy(buf, PUNCH_REQ_MAGIC, PUNCH_MAGIC_LEN);

    /* Client external IP (network byte order).
       Use STUN-discovered address when available; otherwise send 0 and let
       the server use the socket-level source address instead. */
    u32 extIP   = 0;
    u16 extPort = 0;
    if (stunGetStatus() == STUN_STATUS_SUCCESS) {
        const char *ipStr = stunGetExternalIP();
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (sscanf(ipStr, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            /* Store in network byte order (big-endian, first octet in byte 0) */
            extIP = htonl(((u32)a << 24) | ((u32)b << 16) | ((u32)c << 8) | (u32)d);
        }
        extPort = htons(stunGetExternalPort());
    }

    memcpy(buf + 5, &extIP,   4);
    memcpy(buf + 9, &extPort, 2);

    u32 protoVer = htonl((u32)NET_PROTOCOL_VER);
    memcpy(buf + 11, &protoVer, 4);
}

/* -------------------------------------------------------------------------
 * Server-side handler
 * ---------------------------------------------------------------------- */

void netServerHandlePunchReq(struct _ENetHost *host, const struct _ENetAddress *addr,
                              const u8 *rxdata, s32 rxlen)
{
    char addrStr[128] = {0};
    u8   ack[PUNCH_ACK_LEN];

    (void)rxdata;
    (void)rxlen;

    /* Build PUNCH_ACK.
     * We use the socket-level source address (addr) as the destination —
     * this is the client's NAT-rewritten external address, which is what
     * we need to punch through.  The payload ext_ip field from PUNCH_REQ is
     * a hint but the OS-filled source is authoritative. */
    memcpy(ack, PUNCH_ACK_MAGIC, PUNCH_MAGIC_LEN);

    /* server_lan_ip: 0 (client can determine from the ENet source later) */
    ack[5] = 0; ack[6] = 0; ack[7] = 0; ack[8] = 0;

    /* server_port (network byte order) */
    u16 srvPort = htons((u16)host->address.port);
    memcpy(ack + 9, &srvPort, 2);

    /* nat_viable = 1 (we can reach client, or at least we're trying) */
    ack[11] = 1;

    /* Log before sending so we have a record even if send fails */
    enet_address_get_ip(addr, addrStr, sizeof(addrStr) - 1);
    sysLogPrintf(LOG_NOTE, "NET: PUNCH_REQ from %s:%u — sending PUNCH_ACK x3",
                 addrStr, (unsigned)addr->port);

    /* Send 3 ACKs at 50ms intervals to punch through the client's NAT.
     * SDL_Delay here is acceptable: ~150ms total, one-shot per new client,
     * runs on the connectionless pre-auth path, not the game tick. */
    ENetBuffer ebuf;
    ebuf.data       = ack;
    ebuf.dataLength = PUNCH_ACK_LEN;

    s32 i;
    for (i = 0; i < 3; i++) {
        enet_socket_send(host->socket, addr, &ebuf, 1);
        if (i < 2) {
            SDL_Delay(50);
        }
    }
}

/* -------------------------------------------------------------------------
 * Client: ENet intercept callback
 * ---------------------------------------------------------------------- */

s32 netHolePunchClientIntercept(struct _ENetEvent *event, struct _ENetAddress *addr,
                                 u8 *rxdata, s32 rxlen)
{
    (void)event;
    (void)addr;

    if (rxlen >= PUNCH_MAGIC_LEN &&
        memcmp(rxdata, PUNCH_ACK_MAGIC, PUNCH_MAGIC_LEN) == 0) {
        /* PUNCH_ACK received — the server punched back through our NAT */
        if (s_ConnPhase == CONN_PHASE_PUNCH) {
            sysLogPrintf(LOG_NOTE, "NET: PUNCH_ACK received — NAT hole open");
            s_GotAck = 1;
        }
        return 1; /* consume packet — don't pass to ENet */
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Client: start connection waterfall
 * ---------------------------------------------------------------------- */

s32 netStartClientWithHolePunch(const char *addr)
{
    /* Store address for reconnect after punch phase */
    strncpy(s_ConnAddr, addr, NET_MAX_ADDR);
    s_ConnAddr[NET_MAX_ADDR] = '\0';

    /* Pre-parse the address — we need it in ENetAddress form for reconnect */
    if (!netParseAddr(&s_ServerAddr, addr)) {
        sysLogPrintf(LOG_ERROR, "NET: hole punch: invalid address '%s'", addr);
        return -2;
    }

    /* Start normal client connection */
    s32 ret = netStartClient(addr);
    if (ret != 0) {
        return ret;
    }

    /* Override the intercept callback so we can receive PUNCH_ACK */
    enet_host_set_intercept_callback(netGetHost(), netHolePunchClientIntercept);

    /* Shorten the ENet peer timeout to 3s so the waterfall can proceed faster.
     * Default ENet timeout is ~30s which is far too long for a waterfall. */
    if (g_NetLocalClient && g_NetLocalClient->peer) {
        enet_peer_timeout(g_NetLocalClient->peer,
                          ENET_PEER_TIMEOUT_LIMIT, 300, PUNCH_DIRECT_TIMEOUT_MS);
    }

    s_ConnPhase    = CONN_PHASE_DIRECT;
    s_PhaseStartMs = SDL_GetTicks();
    s_GotAck       = 0;
    s_ErrorMsg[0]  = '\0';

    sysLogPrintf(LOG_NOTE, "NET: starting connection waterfall to %s", addr);
    return 0;
}

/* -------------------------------------------------------------------------
 * Client: intercept disconnect timeout events
 * ---------------------------------------------------------------------- */

s32 netHolePunchHandleTimeout(struct _ENetHost *host)
{
    if (s_ConnPhase == CONN_PHASE_DIRECT) {
        /* Direct connection timed out. Check whether hole punch is viable. */
        s32 natType = stunGetNatType();

        if (natType == STUN_NAT_SYMMETRIC) {
            /* Phase 4: symmetric NAT detected — hole punch will fail */
            sysLogPrintf(LOG_WARNING,
                "NET: direct connect timed out. NAT type is symmetric — "
                "hole punch not viable. Try UPnP or manual port forwarding.");
            snprintf(s_ErrorMsg, sizeof(s_ErrorMsg),
                "Your NAT type (symmetric) doesn't support hole punching. "
                "Try UPnP or manual port forwarding.");
            s_ConnPhase = CONN_PHASE_DONE;
            return 0; /* let normal disconnect proceed */
        }

        /* Cone NAT or unknown — attempt hole punch */
        sysLogPrintf(LOG_NOTE,
            "NET: direct connect to %s timed out (NAT type: %s) — "
            "trying hole punch",
            s_ConnAddr,
            natType == STUN_NAT_CONE ? "cone" : "unknown");

        /* Send PUNCH_REQ to server via the ENet socket.
         * The ENet peer is dead at this point (we got a timeout), but the
         * underlying ENet socket is still open and can send connectionless
         * datagrams. */
        u8 req[PUNCH_REQ_LEN];
        buildPunchReq(req);

        ENetBuffer ebuf;
        ebuf.data       = req;
        ebuf.dataLength = PUNCH_REQ_LEN;
        enet_socket_send(host->socket, &s_ServerAddr, &ebuf, 1);

        sysLogPrintf(LOG_NOTE, "NET: PUNCH_REQ sent to %s, waiting up to %dms for ACK",
                     s_ConnAddr, PUNCH_ACK_TIMEOUT_MS);

        s_ConnPhase    = CONN_PHASE_PUNCH;
        s_PhaseStartMs = SDL_GetTicks();
        s_GotAck       = 0;

        return 1; /* consumed — do NOT call netClientEvDisconnect() */
    }

    if (s_ConnPhase == CONN_PHASE_PUNCH_ENET) {
        /* Phase 4: post-punch ENet connect also failed */
        s32 natType = stunGetNatType();

        if (natType == STUN_NAT_CONE) {
            sysLogPrintf(LOG_WARNING,
                "NET: hole punch failed (cone NAT). "
                "The server may be behind a symmetric NAT or firewall.");
            snprintf(s_ErrorMsg, sizeof(s_ErrorMsg),
                "Hole punch failed. The server may be behind a symmetric NAT "
                "or firewall. Ask the server host to enable UPnP or port forward.");
        } else {
            sysLogPrintf(LOG_WARNING,
                "NET: hole punch failed. Could not connect to %s.", s_ConnAddr);
            snprintf(s_ErrorMsg, sizeof(s_ErrorMsg),
                "Could not connect to server. Check that the server is running "
                "and reachable, or try manual port forwarding.");
        }

        s_ConnPhase = CONN_PHASE_DONE;
        return 0; /* let normal disconnect proceed */
    }

    /* Not in a waterfall phase — don't intercept */
    return 0;
}

/* -------------------------------------------------------------------------
 * Client: connection established notification
 * ---------------------------------------------------------------------- */

void netHolePunchOnConnect(void)
{
    if (s_ConnPhase == CONN_PHASE_DIRECT || s_ConnPhase == CONN_PHASE_PUNCH_ENET) {
        const char *phase = (s_ConnPhase == CONN_PHASE_DIRECT) ? "direct" : "hole punch";
        sysLogPrintf(LOG_NOTE, "NET: connected via %s", phase);
        s_ConnPhase = CONN_PHASE_DONE;
    }
}

/* -------------------------------------------------------------------------
 * Client: per-frame state machine tick
 * ---------------------------------------------------------------------- */

void netHolePunchClientTick(struct _ENetHost *host)
{
    if (s_ConnPhase != CONN_PHASE_PUNCH) {
        return; /* nothing to do in other phases */
    }

    u32 now     = SDL_GetTicks();
    u32 elapsed = now - s_PhaseStartMs;

    if (!s_GotAck && elapsed < PUNCH_ACK_TIMEOUT_MS) {
        return; /* still waiting */
    }

    /* ACK received or 500ms timeout — either way, proceed with ENet connect.
     * The NAT hole may be open (if ACK arrived) or we're trying optimistically. */
    if (s_GotAck) {
        sysLogPrintf(LOG_NOTE, "NET: hole punch confirmed, retrying ENet connect...");
    } else {
        sysLogPrintf(LOG_NOTE,
            "NET: PUNCH_ACK timeout — retrying ENet connect optimistically");
    }

    /* The ENet peer from the direct phase is dead (timeout expired).
     * Reset it so the peer slot is freed, then create a new connection. */
    if (g_NetLocalClient && g_NetLocalClient->peer) {
        enet_peer_reset(g_NetLocalClient->peer);
        g_NetLocalClient->peer = NULL;
    }

    /* Fire a fresh ENet connection to the same server address */
    ENetPeer *peer = enet_host_connect(host, &s_ServerAddr, NETCHAN_COUNT, NET_PROTOCOL_VER);
    if (!peer) {
        sysLogPrintf(LOG_WARNING, "NET: hole punch: enet_host_connect failed");
        snprintf(s_ErrorMsg, sizeof(s_ErrorMsg),
            "Could not start post-punch connection. Internal error.");
        s_ConnPhase = CONN_PHASE_DONE;
        return;
    }

    /* Apply longer timeout: NAT holes need a moment to stabilize */
    enet_peer_timeout(peer, ENET_PEER_TIMEOUT_LIMIT, 300, PUNCH_ENET_TIMEOUT_MS);

    g_NetLocalClient->peer  = peer;
    g_NetLocalClient->state = CLSTATE_CONNECTING;

    s_ConnPhase    = CONN_PHASE_PUNCH_ENET;
    s_PhaseStartMs = SDL_GetTicks();

    sysLogPrintf(LOG_NOTE, "NET: ENet connect fired (post-punch, %dms timeout)",
                 PUNCH_ENET_TIMEOUT_MS);
}

/* -------------------------------------------------------------------------
 * Accessors
 * ---------------------------------------------------------------------- */

s32 netGetConnectionPhase(void)
{
    return s_ConnPhase;
}

const char *netHolePunchGetError(void)
{
    return s_ErrorMsg;
}

void netHolePunchReset(void)
{
    s_ConnPhase    = CONN_PHASE_IDLE;
    s_PhaseStartMs = 0;
    s_GotAck       = 0;
    s_ConnAddr[0]  = '\0';
    s_ErrorMsg[0]  = '\0';
    memset(&s_ServerAddr, 0, sizeof(s_ServerAddr));
}
