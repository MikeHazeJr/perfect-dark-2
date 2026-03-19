/**
 * netupnp.c -- Automatic UPnP port forwarding for the network server.
 *
 * When hosting a game, this module:
 *   1. Discovers the router via UPnP/SSDP
 *   2. Queries the external (public) IP address
 *   3. Adds a UDP port mapping so external players can connect
 *   4. Removes the mapping when the server shuts down
 *
 * This eliminates the need for manual router port forwarding in most cases.
 * Uses miniupnpc (BSD 3-Clause license) for all UPnP operations.
 *
 * If UPnP is not available on the router, the module silently falls back
 * to manual mode — the game still works, just needs manual port forwarding.
 */

#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "system.h"

#include "miniupnpc.h"
#include "upnpcommands.h"
#include "upnperrors.h"

/* State */
static struct UPNPUrls s_UpnpUrls;
static struct IGDdatas s_UpnpData;
static char s_ExternalIP[64] = {0};
static char s_MappedPort[8] = {0};
static s32 s_UpnpActive = 0;

/**
 * Attempt to set up UPnP port forwarding.
 *
 * port:     The UDP port to forward (e.g., 27100)
 * Returns:  0 on success, -1 on failure (caller should fall back to manual)
 */
s32 netUpnpSetup(u16 port)
{
    struct UPNPDev *devlist = NULL;
    int error = 0;

    sysLogPrintf(LOG_NOTE, "UPNP: Discovering UPnP devices...");

    /* Discover UPnP devices on the network (2 second timeout) */
    devlist = upnpDiscover(2000, NULL, NULL, 0, 0, 2, &error);
    if (!devlist) {
        sysLogPrintf(LOG_WARNING, "UPNP: No UPnP devices found (error %d). "
                     "Manual port forwarding required.", error);
        return -1;
    }

    sysLogPrintf(LOG_NOTE, "UPNP: Found UPnP devices, looking for IGD...");

    /* Find a valid Internet Gateway Device */
    char lanAddr[64] = {0};
    char wanAddr[64] = {0};
    int igdResult = UPNP_GetValidIGD(devlist, &s_UpnpUrls, &s_UpnpData,
                                      lanAddr, sizeof(lanAddr),
                                      wanAddr, sizeof(wanAddr));

    freeUPNPDevlist(devlist);

    if (igdResult == 0) {
        sysLogPrintf(LOG_WARNING, "UPNP: No valid IGD found. "
                     "Manual port forwarding required.");
        return -1;
    }

    sysLogPrintf(LOG_NOTE, "UPNP: IGD found (type %d), LAN address: %s", igdResult, lanAddr);

    /* Get the external (public) IP */
    int ipResult = UPNP_GetExternalIPAddress(s_UpnpUrls.controlURL,
                                              s_UpnpData.first.servicetype,
                                              s_ExternalIP);
    if (ipResult != 0 || s_ExternalIP[0] == '\0') {
        sysLogPrintf(LOG_WARNING, "UPNP: Could not get external IP (error %d)", ipResult);
        /* Continue anyway — we can still try to add the mapping */
        strncpy(s_ExternalIP, "unknown", sizeof(s_ExternalIP));
    } else {
        sysLogPrintf(LOG_NOTE, "UPNP: External IP: %s", s_ExternalIP);
    }

    /* Add port mapping: external port → LAN address:port (UDP, 1 hour lease) */
    snprintf(s_MappedPort, sizeof(s_MappedPort), "%u", port);

    int mapResult = UPNP_AddPortMapping(
        s_UpnpUrls.controlURL,
        s_UpnpData.first.servicetype,
        s_MappedPort,        /* external port */
        s_MappedPort,        /* internal port */
        lanAddr,             /* internal client (this machine's LAN IP) */
        "Perfect Dark 2",    /* description shown in router UI */
        "UDP",               /* protocol */
        NULL,                /* remote host (NULL = any) */
        "3600"               /* lease duration in seconds (1 hour, auto-renews) */
    );

    if (mapResult != 0) {
        sysLogPrintf(LOG_WARNING, "UPNP: Port mapping failed: %s (%d). "
                     "Manual port forwarding may be required.",
                     strupnperror(mapResult), mapResult);
        FreeUPNPUrls(&s_UpnpUrls);
        return -1;
    }

    sysLogPrintf(LOG_NOTE, "UPNP: Port %s/UDP mapped successfully!", s_MappedPort);
    sysLogPrintf(LOG_NOTE, "UPNP: Players can connect to %s:%s", s_ExternalIP, s_MappedPort);

    s_UpnpActive = 1;
    return 0;
}

/**
 * Remove the UPnP port mapping. Call when the server shuts down.
 */
void netUpnpTeardown(void)
{
    if (!s_UpnpActive) {
        return;
    }

    sysLogPrintf(LOG_NOTE, "UPNP: Removing port mapping %s/UDP...", s_MappedPort);

    int result = UPNP_DeletePortMapping(
        s_UpnpUrls.controlURL,
        s_UpnpData.first.servicetype,
        s_MappedPort,
        "UDP",
        NULL
    );

    if (result != 0) {
        sysLogPrintf(LOG_WARNING, "UPNP: Could not remove port mapping: %s (%d)",
                     strupnperror(result), result);
    } else {
        sysLogPrintf(LOG_NOTE, "UPNP: Port mapping removed.");
    }

    FreeUPNPUrls(&s_UpnpUrls);
    s_ExternalIP[0] = '\0';
    s_MappedPort[0] = '\0';
    s_UpnpActive = 0;
}

/**
 * Get the external (public) IP discovered via UPnP.
 * Returns empty string if UPnP is not active or IP unknown.
 */
const char *netUpnpGetExternalIP(void)
{
    return s_ExternalIP;
}

/**
 * Returns non-zero if UPnP port mapping is active.
 */
s32 netUpnpIsActive(void)
{
    return s_UpnpActive;
}
