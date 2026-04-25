/**
 * pdgui_nat_diagnostics.h -- Phase 1 real-NAT verification harness.
 *
 * Renders an in-build debug overlay that surfaces every active p2p pair
 * with its current tier, attempt count, time-in-tier, last error, and
 * resolved endpoint. The pane lets Mike walk the design's verification
 * matrix (open / full-cone / restricted-cone / port-restricted /
 * symmetric / carrier-grade / corporate-firewall) and record per-tier
 * outcomes against real network conditions, without needing log scrapes.
 *
 * The pane also exposes:
 *  - local STUN reflexive endpoint
 *  - local UPnP mapping status
 *  - LAN announce cache contents
 *  - registered TURN relay candidates
 *
 * Toggle: opened from the Social menu's Settings tab "Open NAT
 * diagnostics" button (no actionmap entry -- Session B owns those).
 */

#ifndef _IN_PDGUI_NAT_DIAGNOSTICS_H
#define _IN_PDGUI_NAT_DIAGNOSTICS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void pdguiNatDiagnosticsOpen(void);
void pdguiNatDiagnosticsClose(void);
s32  pdguiNatDiagnosticsIsOpen(void);
void pdguiNatDiagnosticsRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_NAT_DIAGNOSTICS_H */
