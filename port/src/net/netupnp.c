/**
 * netupnp.c -- Async UPnP port forwarding for the network server.
 *
 * UPnP discovery and port mapping runs on a background thread to avoid
 * blocking the game during startup (discovery can take 2-5 seconds).
 *
 * Status is polled via netUpnpGetStatus() / netUpnpGetExternalIP().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "system.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "miniupnpc.h"
#include "upnpcommands.h"
#include "upnperrors.h"
#include "connectcode.h"

/* Status values */
#define UPNP_STATUS_IDLE      0
#define UPNP_STATUS_WORKING   1
#define UPNP_STATUS_SUCCESS   2
#define UPNP_STATUS_FAILED    3

/* State — accessed from main thread, written by worker thread */
static struct UPNPUrls s_UpnpUrls;
static struct IGDdatas s_UpnpData;
static char s_ExternalIP[64] = {0};
static char s_MappedPort[8] = {0};
static char s_LanAddr[64] = {0};
static volatile s32 s_UpnpStatus = UPNP_STATUS_IDLE;
static volatile s32 s_UpnpActive = 0;
static u16 s_RequestedPort = 0;

/**
 * Worker thread function — does all UPnP I/O without blocking the game.
 */
#ifdef _WIN32
static DWORD WINAPI upnpWorkerThread(LPVOID param)
#else
static void *upnpWorkerThread(void *param)
#endif
{
    u16 port = s_RequestedPort;
    s_UpnpStatus = UPNP_STATUS_WORKING;

    sysLogPrintf(LOG_NOTE, "UPNP: [thread] Discovering devices...");

    struct UPNPDev *devlist = NULL;
    int error = 0;
    devlist = upnpDiscover(2000, NULL, NULL, 0, 0, 2, &error);
    if (!devlist) {
        sysLogPrintf(LOG_WARNING, "UPNP: [thread] No devices found (error %d)", error);
        s_UpnpStatus = UPNP_STATUS_FAILED;
        return 0;
    }

    sysLogPrintf(LOG_NOTE, "UPNP: [thread] Found devices, looking for IGD...");

    char wanAddr[64] = {0};
    int igdResult = UPNP_GetValidIGD(devlist, &s_UpnpUrls, &s_UpnpData,
                                      s_LanAddr, sizeof(s_LanAddr),
                                      wanAddr, sizeof(wanAddr));
    freeUPNPDevlist(devlist);

    if (igdResult == 0) {
        sysLogPrintf(LOG_WARNING, "UPNP: [thread] No valid IGD found");
        s_UpnpStatus = UPNP_STATUS_FAILED;
        return 0;
    }

    sysLogPrintf(LOG_NOTE, "UPNP: [thread] IGD found (type %d), LAN: %s", igdResult, s_LanAddr);

    /* Get external IP */
    int ipResult = UPNP_GetExternalIPAddress(s_UpnpUrls.controlURL,
                                              s_UpnpData.first.servicetype,
                                              s_ExternalIP);
    if (ipResult != 0 || s_ExternalIP[0] == '\0') {
        sysLogPrintf(LOG_WARNING, "UPNP: [thread] Could not get external IP (error %d)", ipResult);
        strncpy(s_ExternalIP, "unknown", sizeof(s_ExternalIP));
    } else {
        sysLogPrintf(LOG_NOTE, "UPNP: [thread] External IP: %s", s_ExternalIP);
    }

    /* Add port mapping */
    snprintf(s_MappedPort, sizeof(s_MappedPort), "%u", port);

    int mapResult = UPNP_AddPortMapping(
        s_UpnpUrls.controlURL,
        s_UpnpData.first.servicetype,
        s_MappedPort, s_MappedPort, s_LanAddr,
        "Perfect Dark 2", "UDP", NULL, "3600"
    );

    if (mapResult != 0) {
        sysLogPrintf(LOG_WARNING, "UPNP: [thread] Port mapping failed: %s (%d)",
                     strupnperror(mapResult), mapResult);
        FreeUPNPUrls(&s_UpnpUrls);
        s_UpnpStatus = UPNP_STATUS_FAILED;
        return 0;
    }

    sysLogPrintf(LOG_NOTE, "UPNP: [thread] Port %s/UDP mapped! Connect to %s:%s",
                 s_MappedPort, s_ExternalIP, s_MappedPort);

    /* Log connect code for easy sharing */
    {
        u32 a, b, c, d;
        if (sscanf(s_ExternalIP, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            u32 ipAddr = (a) | (b << 8) | (c << 16) | (d << 24);
            u16 port = (u16)atoi(s_MappedPort);
            char code[256];
            connectCodeEncode(ipAddr, port, code, sizeof(code));
            sysLogPrintf(LOG_NOTE, "UPNP: Connect code: %s", code);
        }
    }

    s_UpnpActive = 1;
    s_UpnpStatus = UPNP_STATUS_SUCCESS;
    return 0;
}

/**
 * Start async UPnP port forwarding. Returns immediately.
 * Poll netUpnpGetStatus() to check progress.
 */
s32 netUpnpSetup(u16 port)
{
    if (s_UpnpStatus == UPNP_STATUS_WORKING) {
        return 0; /* Already in progress */
    }

    s_RequestedPort = port;
    s_UpnpStatus = UPNP_STATUS_WORKING;
    s_ExternalIP[0] = '\0';

    sysLogPrintf(LOG_NOTE, "UPNP: Starting async discovery for port %u...", port);

#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, upnpWorkerThread, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread); /* Detach — we don't need to join */
    } else {
        sysLogPrintf(LOG_WARNING, "UPNP: Could not create worker thread");
        s_UpnpStatus = UPNP_STATUS_FAILED;
        return -1;
    }
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, upnpWorkerThread, NULL) == 0) {
        pthread_detach(thread);
    } else {
        sysLogPrintf(LOG_WARNING, "UPNP: Could not create worker thread");
        s_UpnpStatus = UPNP_STATUS_FAILED;
        return -1;
    }
#endif

    return 0;
}

void netUpnpTeardown(void)
{
    if (!s_UpnpActive) {
        return;
    }

    sysLogPrintf(LOG_NOTE, "UPNP: Removing port mapping %s/UDP...", s_MappedPort);

    int result = UPNP_DeletePortMapping(
        s_UpnpUrls.controlURL,
        s_UpnpData.first.servicetype,
        s_MappedPort, "UDP", NULL
    );

    if (result != 0) {
        sysLogPrintf(LOG_WARNING, "UPNP: Could not remove mapping: %s (%d)",
                     strupnperror(result), result);
    } else {
        sysLogPrintf(LOG_NOTE, "UPNP: Port mapping removed.");
    }

    FreeUPNPUrls(&s_UpnpUrls);
    s_ExternalIP[0] = '\0';
    s_MappedPort[0] = '\0';
    s_UpnpActive = 0;
    s_UpnpStatus = UPNP_STATUS_IDLE;
}

const char *netUpnpGetExternalIP(void)
{
    return s_ExternalIP;
}

s32 netUpnpIsActive(void)
{
    return s_UpnpActive;
}

s32 netUpnpGetStatus(void)
{
    return s_UpnpStatus;
}
