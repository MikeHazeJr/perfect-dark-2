/**
 * pdgui_nat_diagnostics.cpp -- Phase 1 NAT verification harness UI.
 *
 * Surfaces everything the orchestrator + tier modules know about the
 * current network state. The intent is twofold:
 *
 *  1. Fill the design's NAT-traversal verification matrix without log
 *     scraping. Mike opens this pane during a real-network playtest and
 *     reads off per-tier outcomes for each NAT type encountered.
 *  2. Apply Mike's "up + down the stack" debugging principle: when a
 *     friend won't connect, the same pane shows the local STUN result,
 *     UPnP status, LAN cache, and the per-pair tier history -- so the
 *     question "why is this peer stuck?" is answerable in one read.
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"

#include "pdgui_nat_diagnostics.h"
#include "pdgui_style.h"

extern "C" {
#include "net/p2p.h"
#include "net/group_session.h"
#include "net/netstun.h"
#include "net/netupnp.h"
#include "presence.h"
#include "social.h"
}

static bool s_Open = false;

extern "C" void pdguiNatDiagnosticsOpen(void)  { s_Open = true; }
extern "C" void pdguiNatDiagnosticsClose(void) { s_Open = false; }
extern "C" s32  pdguiNatDiagnosticsIsOpen(void) { return s_Open ? 1 : 0; }

static void formatIp(u32 ipv4, char *out, size_t outsize)
{
	snprintf(out, outsize, "%u.%u.%u.%u",
	         (ipv4 >> 24) & 0xFF, (ipv4 >> 16) & 0xFF,
	         (ipv4 >>  8) & 0xFF, (ipv4 >>  0) & 0xFF);
}

static const char *natTypeName(s32 t)
{
	switch (t) {
		case STUN_NAT_CONE:      return "cone (hole punch viable)";
		case STUN_NAT_SYMMETRIC: return "symmetric (hole punch will fail)";
		default:                 return "unknown";
	}
}

static const char *upnpStatusName(s32 s)
{
	switch (s) {
		case UPNP_STATUS_IDLE:    return "idle";
		case UPNP_STATUS_WORKING: return "working";
		case UPNP_STATUS_SUCCESS: return "success";
		case UPNP_STATUS_FAILED:  return "failed";
		default:                  return "?";
	}
}

extern "C" void pdguiNatDiagnosticsRender(s32 winW, s32 winH)
{
	if (!s_Open) return;

	const float pad = 60.0f;
	ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2((float)winW - pad * 2,
	                                  (float)winH - pad * 2),
	                          ImGuiCond_Always);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.12f, 0.97f));
	if (ImGui::Begin("##pd2_nat_diagnostics",
	                 nullptr,
	                 ImGuiWindowFlags_NoCollapse |
	                 ImGuiWindowFlags_NoTitleBar |
	                 ImGuiWindowFlags_NoSavedSettings)) {

		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
		ImGui::TextUnformatted("NAT DIAGNOSTICS");
		ImGui::PopStyleColor();
		ImGui::Separator();

		ImGui::BeginChild("##pd2_natdiag_body", ImVec2(0, -50.0f));

		/* Local */
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
		ImGui::TextUnformatted("Local");
		ImGui::PopStyleColor();
		ImGui::Indent(10.0f);
		ImGui::Text("Handle:        0x%08x", (unsigned)socialMyHandle());
		ImGui::Text("Connect code:  %s", socialMyConnectCode());
		ImGui::Text("Agent:         %s", socialMyAgentName());
		ImGui::Spacing();

		ImGui::TextUnformatted("STUN reflexive (T2):");
		const char *extip = stunGetExternalIP();
		const u16   extport = stunGetExternalPort();
		s32 stunStat = stunGetStatus();
		ImGui::Indent(10.0f);
		ImGui::Text("Status:    %s",
		             stunStat == STUN_STATUS_SUCCESS ? "success" :
		             stunStat == STUN_STATUS_WORKING ? "working" :
		             stunStat == STUN_STATUS_FAILED  ? "failed"  : "idle");
		if (stunStat == STUN_STATUS_SUCCESS) {
			ImGui::Text("Endpoint:  %s:%u", extip[0] ? extip : "?", (unsigned)extport);
			ImGui::Text("NAT type:  %s", natTypeName(stunGetNatType()));
		}
		const u32 myrefipv4 = p2pMyReflexiveIpv4();
		const u16 myrefport = p2pMyReflexivePort();
		if (myrefipv4 != 0) {
			char buf[32]; formatIp(myrefipv4, buf, sizeof(buf));
			ImGui::Text("Cached:    %s:%u", buf, (unsigned)myrefport);
		}
		ImGui::Unindent(10.0f);

		ImGui::Spacing();
		ImGui::TextUnformatted("UPnP / NAT-PMP (T3):");
		ImGui::Indent(10.0f);
		s32 upnpStat = netUpnpGetStatus();
		ImGui::Text("Status:    %s", upnpStatusName(upnpStat));
		if (upnpStat == UPNP_STATUS_SUCCESS) {
			ImGui::Text("External:  %s", netUpnpGetExternalIP());
		}
		ImGui::Unindent(10.0f);

		ImGui::Spacing();
		ImGui::TextUnformatted("LAN announcer (T0):");
		ImGui::Indent(10.0f);
		ImGui::Text("Running:   %s", p2pLanIsRunning() ? "yes" : "no");
		ImGui::Unindent(10.0f);
		ImGui::Unindent(10.0f);

		ImGui::Spacing();
		ImGui::Separator();

		/* Pairs */
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
		ImGui::TextUnformatted("Active p2p pairs");
		ImGui::PopStyleColor();
		const s32 npairs = p2pPairCount();
		if (npairs == 0) {
			ImGui::Indent(10.0f);
			ImGui::TextDisabled("(none)");
			ImGui::Unindent(10.0f);
		} else {
			ImGui::BeginTable("##pd2_pairs", 6,
			                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			                   ImGuiTableFlags_SizingStretchProp);
			ImGui::TableSetupColumn("pair");
			ImGui::TableSetupColumn("peer");
			ImGui::TableSetupColumn("state");
			ImGui::TableSetupColumn("tier");
			ImGui::TableSetupColumn("ms in tier");
			ImGui::TableSetupColumn("endpoint / error");
			ImGui::TableHeadersRow();

			for (s32 i = 0; i < npairs; i++) {
				u32 pid = p2pPairIdAt(i);
				if (pid == 0) continue;
				p2p_pair_diag_t d; memset(&d, 0, sizeof(d));
				if (!p2pPairDiag(pid, &d)) continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("%u", (unsigned)pid);
				ImGui::TableNextColumn(); ImGui::Text("0x%08x", (unsigned)d.peer_handle);
				ImGui::TableNextColumn();
				const char *st = d.state == P2P_PAIR_OPEN    ? "OPEN"
				              : d.state == P2P_PAIR_FAILED  ? "FAILED"
				              : d.state == P2P_PAIR_WORKING ? "WORKING" : "IDLE";
				ImGui::TextUnformatted(st);
				ImGui::TableNextColumn();
				ImGui::Text("%s", p2pTierName(d.current_tier));
				ImGui::TableNextColumn();
				ImGui::Text("%u", (unsigned)d.ms_in_current_tier);
				ImGui::TableNextColumn();
				if (d.state == P2P_PAIR_OPEN && d.endpoint.ipv4 != 0) {
					char buf[32]; formatIp(d.endpoint.ipv4, buf, sizeof(buf));
					ImGui::Text("%s:%u", buf, (unsigned)d.endpoint.port);
				} else if (d.state == P2P_PAIR_FAILED) {
					ImGui::Text("%s", d.last_error);
				} else {
					ImGui::TextDisabled("(in flight)");
				}
			}
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::Separator();

		/* Group session */
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
		ImGui::TextUnformatted("Group session");
		ImGui::PopStyleColor();
		const group_session_t *gs = groupSessionGet();
		if (gs && gs->in_session) {
			ImGui::Indent(10.0f);
			ImGui::Text("In session:        yes");
			ImGui::Text("Authority handle:  0x%08x", (unsigned)gs->authority_handle);
			ImGui::Text("Local authority:   %s", gs->is_local_authority ? "yes" : "no");
			for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
				const group_peer_t *pp = &gs->peers[i];
				if (pp->handle == 0) continue;
				ImGui::Text("peer 0x%08x  %s  kbps=%u",
				             (unsigned)pp->handle,
				             groupPeerStateName(pp->state),
				             (unsigned)pp->last_kbps);
				if (pp->state == GROUP_PEER_FAILED) {
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintDanger(255));
					ImGui::Text("(%s)", groupFailReasonText(pp->fail));
					ImGui::PopStyleColor();
				}
			}
			ImGui::Unindent(10.0f);
		} else {
			ImGui::Indent(10.0f);
			ImGui::TextDisabled("(no group session active)");
			ImGui::Unindent(10.0f);
		}

		ImGui::Spacing();
		ImGui::Separator();

		/* Verification matrix template -- just text Mike fills out. */
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
		ImGui::TextUnformatted("Verification matrix");
		ImGui::PopStyleColor();
		ImGui::TextWrapped(
		        "When testing on a real network, walk this list:\n"
		        "  T0 LAN broadcast      -- two clients on the same subnet\n"
		        "  T1 Direct UDP         -- public IP both ways\n"
		        "  T2 STUN punch         -- one full-cone NAT\n"
		        "  T3 UPnP / NAT-PMP     -- consumer router with UPnP enabled\n"
		        "  T4 ICE pair test      -- mixed candidates / asymmetric NAT\n"
		        "  T5 TURN relay         -- symmetric NAT or corporate firewall\n"
		        "Each row's per-tier ms / outcome surfaces above.");

		ImGui::EndChild();

		ImGui::Separator();
		if (ImGui::Button("Close")) {
			s_Open = false;
		}
	}
	ImGui::End();
	ImGui::PopStyleColor();

	(void)winW; (void)winH;
}
