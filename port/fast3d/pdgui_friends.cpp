/**
 * pdgui_friends.cpp -- Phase 1 connectivity UI surfaces.
 *
 * Three surfaces:
 *   - Top-right status indicator     (always visible when overlay is on)
 *   - Sidebar peek                   (Tab-toggled or click-on-indicator)
 *   - Full-screen Social menu        (Open from sidebar's "More" link)
 *
 * Visual style anchors on the existing PD2 ImGui palette via
 * pdgui_style.h accessors (TitleGlow / TintSuccess / TintInfo / TintDanger).
 *
 * No drill-in/drill-out (Q16): rows are statically visible. Each friend
 * row displays its actions inline (Invite / Message). D-pad navigates
 * directly between rows and actions; A engages the focused button; B
 * closes the surface only at the top level.
 *
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"

#include "pdgui_friends.h"
#include "pdgui_nat_diagnostics.h"
#include "pdgui_style.h"

extern "C" {
#include "presence.h"
#include "social.h"
#include "chat.h"
#include "file_transfer.h"
#include "spectator.h"
#include "theater.h"
#include "listening_room.h"
#include "social_share.h"
#include "voice.h"
#include "net/p2p.h"
#include "net/group_session.h"
}

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static bool s_SidebarOpen        = false;
static bool s_SocialOpen         = false;
static bool s_AddFriendOpen      = false;
static char s_AddFriendCodeBuf[128];
static char s_AddFriendNickBuf[64];
static char s_AddFriendStatus[128];

/* Per-friend chat panel state. One panel can be open at a time. */
static u32  s_ChatPanelFriendHandle = 0;
static char s_ChatComposeBuf[CHAT_TEXT_MAX];
static char s_ChatAttachPath[400];

/* Per-friend Player Profile modal state. */
static u32  s_ProfileFriendHandle = 0;

/* Listening-room compose state for the Host tab. */
static char s_LrAddIdBuf[LR_TRACK_ID_MAX];
static char s_LrAddNameBuf[LR_TRACK_NAME_MAX];

/* Theater compose state for the Replays tab. */
static char s_TheaterRecordNameBuf[64];
static u32  s_TheaterListLastRefreshMs = 0;

/* Convert-to-mod modal state. Source path lives in s_ConvertSourcePath; the
 * modal is open while s_ConvertSourcePath[0] is non-zero. */
static char s_ConvertSourcePath[400];
static char s_ConvertNameBuf[64];
static char s_ConvertDescBuf[256];
static char s_ConvertCreatorBuf[32];
static char s_ConvertTagsBuf[64];
static char s_ConvertVersionBuf[16];
static char s_ConvertStatus[160];

extern "C" void pdguiFriendsSidebarOpen(void)   { s_SidebarOpen = true; }
extern "C" void pdguiFriendsSidebarClose(void)  { s_SidebarOpen = false; }
extern "C" void pdguiFriendsSidebarToggle(void) { s_SidebarOpen = !s_SidebarOpen; }
extern "C" s32  pdguiFriendsSidebarIsOpen(void) { return s_SidebarOpen ? 1 : 0; }

extern "C" void pdguiFriendsSocialOpen(void)   { s_SocialOpen = true; s_SidebarOpen = false; }
extern "C" void pdguiFriendsSocialClose(void)  { s_SocialOpen = false; }
extern "C" s32  pdguiFriendsSocialIsOpen(void) { return s_SocialOpen ? 1 : 0; }

extern "C" void pdguiFriendsChatOpen(u32 friend_handle) {
	s_ChatPanelFriendHandle = friend_handle;
	s_ChatComposeBuf[0] = '\0';
}
extern "C" void pdguiFriendsChatClose(void) { s_ChatPanelFriendHandle = 0; }
extern "C" s32  pdguiFriendsChatIsOpen(void) { return s_ChatPanelFriendHandle != 0 ? 1 : 0; }
extern "C" u32  pdguiFriendsChatTargetHandle(void) { return s_ChatPanelFriendHandle; }

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static const char *visibilityLabel(social_visibility_t v)
{
	switch (v) {
		case SOCIAL_VIS_PUBLIC:         return "Public";
		case SOCIAL_VIS_FRIENDS_ONLY:   return "Friends Only";
		case SOCIAL_VIS_APPEAR_OFFLINE: return "Appear Offline";
		default: return "?";
	}
}

static const char *stateLabel(presence_state_t s)
{
	switch (s) {
		case PRESENCE_OFFLINE:        return "Offline";
		case PRESENCE_BOOTSTRAP:      return "Connecting";
		case PRESENCE_ONLINE_IDLE:    return "Online";
		case PRESENCE_IN_MATCH:       return "In Match";
		case PRESENCE_IN_MISSION:     return "In Mission";
		case PRESENCE_SPECTATING:     return "Spectating";
		case PRESENCE_APPEAR_OFFLINE: return "Appear Offline";
		default: return "?";
	}
}

static ImU32 stateDot(presence_state_t s)
{
	switch (s) {
		case PRESENCE_ONLINE_IDLE:    return pdguiImU32TintSuccess(255);
		case PRESENCE_IN_MATCH:
		case PRESENCE_IN_MISSION:     return pdguiImU32TintInfo(255);
		case PRESENCE_SPECTATING:     return pdguiImU32TitleGlow(255);
		case PRESENCE_APPEAR_OFFLINE: return IM_COL32(120, 120, 120, 255);
		case PRESENCE_OFFLINE:        return IM_COL32(80, 80, 80, 255);
		case PRESENCE_BOOTSTRAP:      return IM_COL32(180, 160, 80, 255);
		default: return IM_COL32(120, 120, 120, 255);
	}
}

static void formatLastSeen(u32 last_pong_ms, char *out, size_t outsize)
{
	if (last_pong_ms == 0) { snprintf(out, outsize, "never"); return; }
	const u32 now = SDL_GetTicks();
	if (now <= last_pong_ms) { snprintf(out, outsize, "now"); return; }
	const u32 delta = (now - last_pong_ms) / 1000u;
	if (delta < 60)        snprintf(out, outsize, "%us ago", delta);
	else if (delta < 3600) snprintf(out, outsize, "%um ago", delta / 60);
	else                   snprintf(out, outsize, "%uh ago", delta / 3600);
}

static void drawDot(ImDrawList *dl, ImVec2 p, float r, ImU32 col)
{
	dl->AddCircleFilled(p, r, col, 16);
}

/* -------------------------------------------------------------------------
 * Status indicator
 *
 * Top-right pill: [DOT] [Local Agent Name] [State Label] (Connect: foo bar...)
 * Click-through opens the sidebar.
 * ------------------------------------------------------------------------- */

extern "C" s32 pdguiPauseGetNormMplayerIsRunning(void);
extern "C" u8  pdguiPauseGetPaused(void);

extern "C" void pdguiFriendsStatusIndicatorRender(s32 winW, s32 winH)
{
	if (!socialIsReady()) return;

	/* Mike playtest 2026-04-25 (db905396 build): suppress the status pill
	 * during live MP gameplay -- it was painting to GetForegroundDrawList
	 * over the minimap + gameplay HUD, and the dual render-pass path
	 * (hot-swap + main render) caused per-frame scale flicker. The pill
	 * only matters when the user is in a menu surface; during active
	 * combat the dot+code+state info is not actionable.
	 *
	 * Bad-value triage matrix applied:
	 *   - Setting bug: status pill rendered unconditionally (no in-match
	 *     gate). Root cause -- fixed here.
	 *   - Multi-source flicker: two render passes (hot-swap + final)
	 *     paint the pill at different ImGui font scales. Suppression
	 *     during MP eliminates both passes for the in-match case.
	 *   - Z-order: GetForegroundDrawList sits above gameplay HUD. The
	 *     fix is suppression rather than re-layering, since the pill is
	 *     non-actionable during active play.
	 *
	 * The pill still shows on the title screen, main menu, pause menu,
	 * scorecard, and dedicated social surfaces -- everywhere it has a
	 * useful click-target. */
	if (pdguiPauseGetNormMplayerIsRunning() && pdguiPauseGetPaused() < 2) {
		return;
	}

	const presence_state_t pstate = presenceGetLocalState();
	const social_visibility_t vis = socialVisibilityGet();
	const char *agent = socialMyAgentName();
	const char *code  = socialMyConnectCode();

	char label[160];
	if (vis == SOCIAL_VIS_APPEAR_OFFLINE) {
		snprintf(label, sizeof(label), "%s | Appear Offline | %s", agent, code);
	} else {
		snprintf(label, sizeof(label), "%s | %s | %s", agent, stateLabel(pstate), code);
	}

	ImGuiIO &io = ImGui::GetIO();
	const ImVec2 textSize = ImGui::CalcTextSize(label);
	const float pad = 10.0f;
	const float dotR = 5.0f;
	const float pillW = textSize.x + pad * 3 + dotR * 2;
	const float pillH = textSize.y + pad;

	const ImVec2 origin(io.DisplaySize.x - pillW - 12.0f, 12.0f);
	const ImVec2 end(origin.x + pillW, origin.y + pillH);

	ImDrawList *dl = ImGui::GetForegroundDrawList();
	dl->AddRectFilled(origin, end, IM_COL32(8, 12, 24, 220), 6.0f);
	dl->AddRect      (origin, end, pdguiImU32TitleGlow(180), 6.0f, 0, 1.0f);

	const ImVec2 dotPos(origin.x + pad + dotR, origin.y + pillH * 0.5f);
	drawDot(dl, dotPos, dotR, stateDot(pstate));

	const ImVec2 textPos(dotPos.x + dotR + pad, origin.y + (pillH - textSize.y) * 0.5f);
	dl->AddText(textPos, IM_COL32(220, 230, 240, 255), label);

	/* Click-through: open the sidebar on left-click within the pill. */
	const ImVec2 mp = io.MousePos;
	if (io.MouseClicked[0] && mp.x >= origin.x && mp.x <= end.x &&
	    mp.y >= origin.y && mp.y <= end.y) {
		s_SidebarOpen = !s_SidebarOpen;
	}

	(void)winW; (void)winH;
}

/* -------------------------------------------------------------------------
 * Sidebar peek
 * ------------------------------------------------------------------------- */

/* Section 2.4 (UX feedback) + Section 9.2 (Q14 mismatch) + Section 2.4
 * residue UX: render an inline status row for any group_session peer
 * that is RESOLVING or FAILED so the user sees the escalation in flight
 * and the specific error string when nothing succeeds. */
static void renderGroupConnectionsSection(void)
{
	const group_session_t *gs = groupSessionGet();
	if (!gs) return;
	s32 active = 0;
	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		if (gs->peers[i].handle != 0 &&
		    (gs->peers[i].state == GROUP_PEER_RESOLVING ||
		     gs->peers[i].state == GROUP_PEER_INVITED  ||
		     gs->peers[i].state == GROUP_PEER_FAILED   ||
		     gs->peers[i].state == GROUP_PEER_CONNECTED)) {
			active++;
		}
	}
	if (active == 0) return;

	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
	ImGui::TextUnformatted("Connections");
	ImGui::PopStyleColor();
	ImGui::Separator();

	for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; i++) {
		const group_peer_t *pp = &gs->peers[i];
		if (pp->handle == 0) continue;
		const social_friend_t *f = socialFriendByHandle(pp->handle);
		const char *agent = f && f->agent_name[0] ? f->agent_name : "friend";

		switch (pp->state) {
			case GROUP_PEER_INVITED:
				ImGui::TextDisabled("%s -- invite sent, waiting...", agent);
				break;
			case GROUP_PEER_RESOLVING: {
				p2p_pair_diag_t diag; memset(&diag, 0, sizeof(diag));
				if (pp->pair_id && p2pPairDiag(pp->pair_id, &diag)) {
					ImGui::Text("%s -- %s",
					             agent,
					             p2pTierUxLabel(diag.current_tier));
				} else {
					ImGui::Text("%s -- connecting...", agent);
				}
				break;
			}
			case GROUP_PEER_CONNECTED:
				ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintSuccess(255));
				ImGui::Text("%s -- connected", agent);
				ImGui::PopStyleColor();
				break;
			case GROUP_PEER_FAILED:
				ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintDanger(255));
				if (pp->fail == GROUP_FAIL_VERSION_MISMATCH) {
					char msg[160];
					groupSessionFormatVersionMismatch(pp, msg, sizeof(msg));
					ImGui::TextWrapped("%s", msg);
				} else if (pp->fail == GROUP_FAIL_NETWORK_BLOCKED) {
					ImGui::TextWrapped(
					        "Connection failed. Your network blocks the traffic this game uses. "
					        "Contact your network admin or try a different network.");
				} else {
					ImGui::TextWrapped("%s -- %s", agent, groupFailReasonText(pp->fail));
				}
				ImGui::PopStyleColor();
				ImGui::PushID(9000 + i);
				if (ImGui::SmallButton("Dismiss")) {
					groupSessionDropPeer(pp->handle);
					ImGui::PopID();
					return;
				}
				ImGui::PopID();
				break;
			default: break;
		}
	}
}

static void renderInvitationsSection(void)
{
	const s32 nin = presenceInviteCount();
	if (nin <= 0) return;

	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
	ImGui::TextUnformatted("Invitations");
	ImGui::PopStyleColor();
	ImGui::Separator();

	for (s32 i = 0; i < nin; i++) {
		const presence_invite_t *e = presenceInviteAt(i);
		if (!e) continue;
		ImGui::PushID(i + 7000);
		const social_friend_t *f = socialFriendByHandle(e->from_handle);
		const char *agent = (f && f->agent_name[0]) ? f->agent_name :
		                    (e->from_agent[0] ? e->from_agent : "Unknown");
		const char *kindLabel = e->kind == PRESENCE_INVITE_KIND_MATCH ? "match"
		                       : e->kind == PRESENCE_INVITE_KIND_GROUP ? "group"
		                       : "listening room";
		ImGui::Text("%s invited you to %s", agent, kindLabel);
		ImGui::SameLine();
		if (ImGui::SmallButton("Accept")) {
			presenceInviteAccept(i);
			ImGui::PopID();
			return; /* indices shifted */
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Decline")) {
			presenceInviteDecline(i);
			ImGui::PopID();
			return;
		}
		ImGui::PopID();
	}
}

static void renderFriendRow(s32 idx, const social_friend_t *f)
{
	if (!f) return;
	const presence_peer_t *peer = presencePeerByHandle(f->handle);
	const presence_state_t pstate = peer ? peer->state : PRESENCE_OFFLINE;

	char label[128];
	socialFormatDisplay(f, label, sizeof(label));

	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const ImVec2 dotPos(cursor.x + 8.0f, cursor.y + 10.0f);
	drawDot(dl, dotPos, 4.0f, stateDot(pstate));

	ImGui::Indent(20.0f);

	ImGui::PushID(idx);

	ImGui::TextUnformatted(label);
	if (peer && peer->status_blurb[0]) {
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", peer->status_blurb);
	} else if (pstate != PRESENCE_OFFLINE) {
		ImGui::SameLine();
		ImGui::TextDisabled("[%s]", stateLabel(pstate));
	} else {
		ImGui::SameLine();
		char seen[24];
		formatLastSeen(peer ? peer->last_pong_ms : 0, seen, sizeof(seen));
		ImGui::TextDisabled("(seen %s)", seen);
	}

	const bool can_invite = (pstate == PRESENCE_ONLINE_IDLE) ||
	                         (pstate == PRESENCE_IN_MATCH) ||
	                         (pstate == PRESENCE_IN_MISSION);
	if (can_invite) {
		if (ImGui::SmallButton("Invite")) {
			presenceSendInvite(f->handle, PRESENCE_INVITE_KIND_MATCH);
		}
		ImGui::SameLine();
	}

	const bool can_spectate = (pstate == PRESENCE_IN_MATCH) ||
	                           (pstate == PRESENCE_IN_MISSION);
	if (can_spectate) {
		if (ImGui::SmallButton("Spectate")) {
			spectatorBeginLive(f->handle);
		}
		ImGui::SameLine();
	}

	if (ImGui::SmallButton("Message")) {
		s_ChatPanelFriendHandle = f->handle;
		s_ChatComposeBuf[0] = '\0';
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Profile")) {
		s_ProfileFriendHandle = f->handle;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(f->muted ? "Unmute" : "Mute")) {
		socialFriendSetMuted(f->connect_code, f->muted ? 0 : 1);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Block")) {
		socialBlockAdd(f->connect_code, f->agent_name);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Remove")) {
		socialFriendRemove(f->connect_code);
	}

	ImGui::PopID();

	ImGui::Unindent(20.0f);
}

/* -------------------------------------------------------------------------
 * Per-friend chat panel (Phase 2 -- private 1:1 chat surface).
 * ------------------------------------------------------------------------- */

static void renderChatPanel(s32 winW, s32 winH)
{
	if (s_ChatPanelFriendHandle == 0) return;

	const social_friend_t *f = socialFriendByHandle(s_ChatPanelFriendHandle);
	if (!f) {
		s_ChatPanelFriendHandle = 0;
		return;
	}

	const float w = 480.0f;
	const float h = (float)winH * 0.72f;
	ImGui::SetNextWindowPos(ImVec2(40.0f, (float)winH * 0.14f), ImGuiCond_Appearing);
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.12f, 0.95f));
	bool open = true;
	if (ImGui::Begin("##pd2_chat_panel",
	                 &open,
	                 ImGuiWindowFlags_NoCollapse |
	                 ImGuiWindowFlags_NoSavedSettings)) {

		char title[128];
		socialFormatDisplay(f, title, sizeof(title));
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
		ImGui::Text("Chat with %s", title);
		ImGui::PopStyleColor();

		const presence_peer_t *peer = presencePeerByHandle(f->handle);
		const char *state_label = peer ? stateLabel(peer->state) : "Offline";
		ImGui::TextDisabled("[%s]", state_label);
		ImGui::Separator();

		const float footer_h = 84.0f;
		ImGui::BeginChild("##pd2_chat_history", ImVec2(0, -footer_h), false);
		const s32 nm = chatHistoryCount(f->handle);
		for (s32 i = 0; i < nm; i++) {
			const chat_message_t *m = chatHistoryAt(f->handle, i);
			if (!m) continue;
			ImGui::PushID(11000 + i);
			if (m->direction == CHAT_DIR_SYS) {
				ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
				ImGui::TextWrapped("-- %s --", m->text);
				ImGui::PopStyleColor();
			} else if (m->direction == CHAT_DIR_OUT) {
				ImGui::Text("you:");
				ImGui::SameLine();
				ImGui::TextWrapped("%s", m->text);
			} else {
				ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
				ImGui::Text("%s:", f->agent_name[0] ? f->agent_name : "friend");
				ImGui::PopStyleColor();
				ImGui::SameLine();
				ImGui::TextWrapped("%s", m->text);
			}
			if (m->attachment_kind != 0) {
				ImGui::Indent(20.0f);
				const char *kind_name = fileTransferKindName((s32)m->attachment_kind);
				ImGui::Text("[%s] %s (%llu bytes)",
				             kind_name,
				             m->attachment_name[0] ? m->attachment_name : "(unnamed)",
				             (unsigned long long)m->attachment_size);
				if (m->attachment_path[0]) {
					if (ImGui::SmallButton("Open file location")) {
#ifdef _WIN32
						char cmd[600];
						snprintf(cmd, sizeof(cmd),
						          "explorer.exe /select,\"%s\"", m->attachment_path);
						(void)system(cmd);
#elif defined(__APPLE__)
						char cmd[600];
						snprintf(cmd, sizeof(cmd), "open -R \"%s\"", m->attachment_path);
						(void)system(cmd);
#else
						/* xdg-open against the directory; not all file
						 * managers highlight, but it opens the folder. */
						char cmd[600];
						char dir[400];
						strncpy(dir, m->attachment_path, sizeof(dir) - 1);
						dir[sizeof(dir) - 1] = '\0';
						char *slash = strrchr(dir, '/');
						if (slash) *slash = '\0';
						snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", dir);
						(void)system(cmd);
#endif
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Copy path")) {
						SDL_SetClipboardText(m->attachment_path);
					}
					/* Type-aware actions per Q18 amendment. */
					if (m->attachment_kind == FT_KIND_MUSIC) {
						ImGui::SameLine();
						if (ImGui::SmallButton("Convert to mod...")) {
							strncpy(s_ConvertSourcePath, m->attachment_path,
							        sizeof(s_ConvertSourcePath) - 1);
							s_ConvertSourcePath[sizeof(s_ConvertSourcePath) - 1] = '\0';
							strncpy(s_ConvertNameBuf,
							        m->attachment_name[0] ? m->attachment_name : "Untitled",
							        sizeof(s_ConvertNameBuf) - 1);
							s_ConvertNameBuf[sizeof(s_ConvertNameBuf) - 1] = '\0';
							/* Strip extension from default name. */
							char *dot = strrchr(s_ConvertNameBuf, '.');
							if (dot) *dot = '\0';
							s_ConvertDescBuf[0] = '\0';
							strncpy(s_ConvertCreatorBuf, socialMyAgentName(),
							        sizeof(s_ConvertCreatorBuf) - 1);
							s_ConvertCreatorBuf[sizeof(s_ConvertCreatorBuf) - 1] = '\0';
							strncpy(s_ConvertTagsBuf, "music,user-converted",
							        sizeof(s_ConvertTagsBuf) - 1);
							strncpy(s_ConvertVersionBuf, "1.0.0",
							        sizeof(s_ConvertVersionBuf) - 1);
							s_ConvertStatus[0] = '\0';
						}
					}
				}
				ImGui::Unindent(20.0f);
			}
			ImGui::PopID();
		}
		/* Auto-scroll if user is near the bottom. */
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f) {
			ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();

		ImGui::Separator();
		ImGui::SetNextItemWidth(-100.0f);
		bool submitted = ImGui::InputText("##pd2_chat_compose",
		                                    s_ChatComposeBuf,
		                                    sizeof(s_ChatComposeBuf),
		                                    ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if (ImGui::Button("Send", ImVec2(80, 0)) || submitted) {
			if (s_ChatComposeBuf[0] != '\0') {
				if (chatSendText(f->handle, s_ChatComposeBuf) == 0) {
					s_ChatComposeBuf[0] = '\0';
				}
			}
		}

		ImGui::SetNextItemWidth(-180.0f);
		ImGui::InputTextWithHint("##pd2_chat_attach", "absolute path to attach...",
		                          s_ChatAttachPath, sizeof(s_ChatAttachPath));
		ImGui::SameLine();
		if (ImGui::Button("Send file", ImVec2(120, 0))) {
			if (s_ChatAttachPath[0] != '\0') {
				s32 rc = fileTransferSendFile(f->handle, s_ChatAttachPath);
				if (rc == 0) {
					s_ChatAttachPath[0] = '\0';
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Paste path", ImVec2(120, 0))) {
			char *clip = SDL_GetClipboardText();
			if (clip) {
				strncpy(s_ChatAttachPath, clip, sizeof(s_ChatAttachPath) - 1);
				s_ChatAttachPath[sizeof(s_ChatAttachPath) - 1] = '\0';
				SDL_free(clip);
			}
		}

		if (peer && peer->state == PRESENCE_OFFLINE) {
			ImGui::TextDisabled("Offline -- message will not be delivered until "
			                      "%s is online again.", f->agent_name);
		}
	}
	ImGui::End();
	ImGui::PopStyleColor();

	if (!open) {
		s_ChatPanelFriendHandle = 0;
	}

	(void)winW;
}

/* Player Profile modal -- per-friend page (Q11 Halo 3 File Share lineage).
 * Shows agent + connect code + nickname + last-seen, links to subscribe
 * to their listening room, and reserves panels for stats + character
 * preview + public mods list. The character render box ships from
 * Priority Q (`03f44cb0` already in dev); this modal will pull that
 * widget in a follow-up wiring commit. */
static void renderProfileModal(void)
{
	if (s_ProfileFriendHandle == 0) return;
	const social_friend_t *f = socialFriendByHandle(s_ProfileFriendHandle);
	if (!f) { s_ProfileFriendHandle = 0; return; }

	ImGui::OpenPopup("Player Profile");
	if (ImGui::BeginPopupModal("Player Profile", nullptr,
	                            ImGuiWindowFlags_AlwaysAutoResize |
	                            ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
		ImGui::Text("%s", f->agent_name[0] ? f->agent_name : "?");
		ImGui::PopStyleColor();
		if (f->nickname[0]) ImGui::Text("\"%s\"", f->nickname);
		ImGui::TextDisabled("Connect code: %s", f->connect_code);
		ImGui::Separator();

		const presence_peer_t *p = presencePeerByHandle(f->handle);
		ImGui::Text("State:    %s", p ? stateLabel(p->state) : "Offline");
		if (p && p->status_blurb[0]) {
			ImGui::Text("Activity: %s", p->status_blurb);
		}
		if (p && p->last_pong_ms) {
			char seen[24];
			formatLastSeen(p->last_pong_ms, seen, sizeof(seen));
			ImGui::Text("Last seen: %s", seen);
		}

		ImGui::Spacing();
		ImGui::Separator();

		/* Section 7 placeholders -- character render + stats + mods.
		 * Each pulls a widget that lives elsewhere (Priority Q render,
		 * playerstats.h totals, the Public Mods Page aggregator). The
		 * follow-up wiring commit replaces these placeholders. */
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
		ImGui::TextUnformatted("Character preview");
		ImGui::PopStyleColor();
		ImGui::Indent(12.0f);
		ImGui::TextDisabled("(3D head + body render -- pulls Priority Q render box "
		                    "via charpreview when the wiring commit lands.)");
		ImGui::Unindent(12.0f);

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
		ImGui::TextUnformatted("Stats");
		ImGui::PopStyleColor();
		ImGui::Indent(12.0f);
		const share_profile_t *prof = shareProfileFor(f->handle);
		if (prof) {
			ImGui::Text("Kills:    %u", (unsigned)prof->kills);
			ImGui::Text("Deaths:   %u", (unsigned)prof->deaths);
			ImGui::Text("Missions: %u", (unsigned)prof->missions_completed);
		} else {
			ImGui::TextDisabled("(Friend has not broadcast stats yet -- arrives within ~60s of contact.)");
		}
		ImGui::Unindent(12.0f);

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
		ImGui::TextUnformatted("Public mods");
		ImGui::PopStyleColor();
		ImGui::Indent(12.0f);
		const s32 ntot = shareAggregateModCount();
		s32 own_count = 0;
		for (s32 i = 0; i < ntot; i++) {
			const share_mod_entry_t *e = shareAggregateModAt(i);
			if (!e || e->owner_handle != f->handle) continue;
			ImGui::PushID(18000 + i);
			ImGui::Text("%s v%s",
			             e->display_name[0] ? e->display_name : e->mod_id,
			             e->version[0] ? e->version : "?");
			ImGui::SameLine();
			if (ImGui::SmallButton("Download")) {
				shareSendModRequest(f->handle, e->mod_id);
			}
			ImGui::PopID();
			own_count++;
		}
		if (own_count == 0) {
			ImGui::TextDisabled("(No public mods received yet.)");
		}
		ImGui::Unindent(12.0f);

		ImGui::Spacing();
		ImGui::Separator();

		const presence_state_t pstate = p ? p->state : PRESENCE_OFFLINE;
		const bool can_invite = (pstate == PRESENCE_ONLINE_IDLE) ||
		                         (pstate == PRESENCE_IN_MATCH) ||
		                         (pstate == PRESENCE_IN_MISSION);
		if (can_invite) {
			if (ImGui::Button("Invite to play", ImVec2(180, 0))) {
				presenceSendInvite(f->handle, PRESENCE_INVITE_KIND_MATCH);
			}
			ImGui::SameLine();
		}
		if (ImGui::Button("Subscribe to listening room", ImVec2(220, 0))) {
			listeningRoomSubscribe(f->handle);
		}
		ImGui::SameLine();
		if (ImGui::Button("Close", ImVec2(120, 0))) {
			s_ProfileFriendHandle = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

/* Section 8.5.3 -- Convert-to-mod modal. Drives
 * fileTransferConvertMusicToMod and surfaces a confirmation toast on
 * success. */
static void renderConvertToModModal(void)
{
	if (s_ConvertSourcePath[0] == '\0') return;
	ImGui::OpenPopup("Convert to Mod");
	if (ImGui::BeginPopupModal("Convert to Mod", nullptr,
	                            ImGuiWindowFlags_AlwaysAutoResize |
	                            ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::Text("Source file: %s", s_ConvertSourcePath);
		ImGui::Text("Type:        Music mod");
		ImGui::Separator();

		ImGui::SetNextItemWidth(380.0f);
		ImGui::InputText("Name", s_ConvertNameBuf, sizeof(s_ConvertNameBuf));

		ImGui::SetNextItemWidth(380.0f);
		ImGui::InputTextMultiline("Description",
		                            s_ConvertDescBuf, sizeof(s_ConvertDescBuf),
		                            ImVec2(380.0f, 60.0f));

		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputText("Creator", s_ConvertCreatorBuf, sizeof(s_ConvertCreatorBuf));

		ImGui::SetNextItemWidth(380.0f);
		ImGui::InputText("Tags", s_ConvertTagsBuf, sizeof(s_ConvertTagsBuf));

		ImGui::SetNextItemWidth(140.0f);
		ImGui::InputText("Version", s_ConvertVersionBuf, sizeof(s_ConvertVersionBuf));

		const bool valid = (s_ConvertNameBuf[0] != '\0') &&
		                    (s_ConvertCreatorBuf[0] != '\0');
		if (s_ConvertStatus[0]) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
			ImGui::TextWrapped("%s", s_ConvertStatus);
			ImGui::PopStyleColor();
		}

		ImGui::Spacing();
		if (!valid) ImGui::BeginDisabled();
		if (ImGui::Button("Convert", ImVec2(140, 0))) {
			char outpath[512];
			s32 rc = fileTransferConvertMusicToMod(
			            s_ConvertSourcePath,
			            s_ConvertNameBuf,
			            s_ConvertDescBuf,
			            s_ConvertCreatorBuf,
			            s_ConvertTagsBuf,
			            s_ConvertVersionBuf,
			            outpath, sizeof(outpath));
			if (rc == 0) {
				snprintf(s_ConvertStatus, sizeof(s_ConvertStatus),
				          "Converted to mod at %s", outpath);
				/* Reset source so the user can close the modal without
				 * re-confirming -- the success message will linger one frame. */
				s_ConvertSourcePath[0] = '\0';
				ImGui::CloseCurrentPopup();
			} else {
				snprintf(s_ConvertStatus, sizeof(s_ConvertStatus),
				          "Conversion failed (check name + creator are non-empty).");
			}
		}
		if (!valid) ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(140, 0))) {
			s_ConvertSourcePath[0] = '\0';
			s_ConvertStatus[0] = '\0';
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

extern "C" void pdguiFriendsRender(s32 winW, s32 winH)
{
	pdguiFriendsStatusIndicatorRender(winW, winH);

	/* ImGui hotkey: Tab opens / closes the sidebar when no text input has
	 * focus. Avoids reaching into the actionmap layer. */
	if (!ImGui::GetIO().WantCaptureKeyboard) {
		if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
			s_SidebarOpen = !s_SidebarOpen;
		}
		/* Phase 5 PTT: V key. Codec follow-up will move this to an
		 * actionmap binding once Session B's input scope reopens. */
		if (voiceEnabled() && voiceGetCaptureMode() == VOICE_CAPTURE_PUSH_TO_TALK) {
			if (ImGui::IsKeyPressed(ImGuiKey_V, false))   voicePttBegin();
			if (ImGui::IsKeyReleased(ImGuiKey_V))          voicePttEnd();
		}
	}

	if (s_SidebarOpen) {
		const float w = 360.0f;
		const float h = (float)winH * 0.7f;
		ImGui::SetNextWindowPos(ImVec2((float)winW - w - 12.0f, 60.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.12f, 0.94f));
		if (ImGui::Begin("##pd2_friends_sidebar",
		                 nullptr,
		                 ImGuiWindowFlags_NoCollapse |
		                 ImGuiWindowFlags_NoScrollbar |
		                 ImGuiWindowFlags_NoTitleBar |
		                 ImGuiWindowFlags_NoSavedSettings)) {

			ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
			ImGui::TextUnformatted("Friends");
			ImGui::PopStyleColor();
			ImGui::SameLine();
			s32 online = 0;
			const s32 nf = socialFriendCount();
			for (s32 i = 0; i < nf; i++) {
				const social_friend_t *f = socialFriendAt(i);
				if (f && presencePeerIsOnline(f->handle)) online++;
			}
			ImGui::TextDisabled("(%d / %d online)", (int)online, (int)nf);
			ImGui::Separator();

			ImGui::BeginChild("##pd2_sidebar_body", ImVec2(0, h - 110.0f), false,
			                  ImGuiWindowFlags_HorizontalScrollbar);

			if (nf == 0) {
				ImGui::TextDisabled("No friends added yet.");
				ImGui::TextDisabled("Use \"Add by code\" to add a friend.");
			} else {
				for (s32 i = 0; i < nf; i++) {
					renderFriendRow(i, socialFriendAt(i));
				}
			}

			renderGroupConnectionsSection();
			renderInvitationsSection();

			ImGui::EndChild();

			ImGui::Separator();

			if (ImGui::Button("Add by code")) {
				s_AddFriendOpen = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Open Social menu")) {
				pdguiFriendsSocialOpen();
			}
			ImGui::SameLine();
			if (ImGui::Button("Close")) {
				s_SidebarOpen = false;
			}
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}

	if (s_SocialOpen) {
		const float pad = 32.0f;
		ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2((float)winW - pad * 2, (float)winH - pad * 2),
		                          ImGuiCond_Always);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.12f, 0.97f));
		if (ImGui::Begin("##pd2_social_menu",
		                 nullptr,
		                 ImGuiWindowFlags_NoCollapse |
		                 ImGuiWindowFlags_NoTitleBar |
		                 ImGuiWindowFlags_NoSavedSettings)) {

			ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
			ImGui::TextUnformatted("SOCIAL");
			ImGui::PopStyleColor();
			ImGui::TextDisabled("Connect code: %s", socialMyConnectCode());
			ImGui::Separator();

			if (ImGui::BeginTabBar("##pd2_social_tabs")) {

				if (ImGui::BeginTabItem("Friends")) {
					ImGui::BeginChild("##pd2_friends_full", ImVec2(0, -60.0f));
					const s32 nf = socialFriendCount();
					if (nf == 0) {
						ImGui::TextDisabled("No friends yet. Use \"Add by code\".");
					} else {
						for (s32 i = 0; i < nf; i++) {
							renderFriendRow(i, socialFriendAt(i));
							ImGui::Separator();
						}
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Block list")) {
					ImGui::BeginChild("##pd2_blocks_full", ImVec2(0, -60.0f));
					const s32 nb = socialBlockCount();
					if (nb == 0) {
						ImGui::TextDisabled("No one is blocked.");
					} else {
						for (s32 i = 0; i < nb; i++) {
							const social_block_t *b = socialBlockAt(i);
							if (!b) continue;
							ImGui::PushID(8000 + i);
							ImGui::Text("%s  (%s)", b->agent_name[0] ? b->agent_name : "?",
							                          b->connect_code);
							ImGui::SameLine();
							if (ImGui::SmallButton("Unblock")) {
								socialBlockRemove(b->connect_code);
								ImGui::PopID();
								break;
							}
							ImGui::PopID();
						}
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Replays")) {
					ImGui::BeginChild("##pd2_theater_body", ImVec2(0, -60.0f));

					ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
					ImGui::TextUnformatted("Recorder");
					ImGui::PopStyleColor();
					ImGui::Separator();
					if (theaterIsRecording()) {
						ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintDanger(255));
						ImGui::TextUnformatted("Recording in progress.");
						ImGui::PopStyleColor();
						if (ImGui::Button("Stop recording")) {
							theaterStopRecording();
						}
					} else {
						ImGui::SetNextItemWidth(280.0f);
						ImGui::InputTextWithHint("filename", "my_match.pdth",
						                          s_TheaterRecordNameBuf,
						                          sizeof(s_TheaterRecordNameBuf));
						ImGui::SameLine();
						if (ImGui::Button("Start recording")) {
							const char *fn = s_TheaterRecordNameBuf[0]
							                  ? s_TheaterRecordNameBuf : "untitled.pdth";
							theaterStartRecording(fn);
						}
					}

					ImGui::Spacing();
					ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
					ImGui::TextUnformatted("Saved replays");
					ImGui::PopStyleColor();
					ImGui::Separator();

					const u32 now_ms = SDL_GetTicks();
					if (s_TheaterListLastRefreshMs == 0 ||
					    (now_ms - s_TheaterListLastRefreshMs) > 5000u) {
						theaterRefreshList();
						s_TheaterListLastRefreshMs = now_ms;
					}

					ImGui::SameLine();
					if (ImGui::SmallButton("Refresh")) {
						theaterRefreshList();
						s_TheaterListLastRefreshMs = now_ms;
					}

					/* Iterate replays. */
					{
						s32 found = 0;
						for (s32 i = 0; i < THEATER_REPLAY_LIST_MAX; i++) {
							const theater_replay_entry_t *e = theaterListAt(i);
							if (!e) break;
							if (!e->filename[0]) break;
							found++;
							ImGui::PushID(15000 + i);
							ImGui::Text("%s -- %u frames, %u KB",
							             e->filename,
							             (unsigned)e->frame_count,
							             (unsigned)(e->size_bytes / 1024));
							ImGui::SameLine();
							if (theaterIsReplaying()) {
								if (ImGui::SmallButton("Stop")) {
									theaterStopReplay();
								}
							} else {
								if (ImGui::SmallButton("Play")) {
									theaterStartReplay(e->filename);
								}
							}
							ImGui::PopID();
						}
						if (found == 0) {
							ImGui::TextDisabled("No replays yet. Start recording to create one.");
						}
					}

					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Listening room")) {
					ImGui::BeginChild("##pd2_lr_body", ImVec2(0, -60.0f));

					const listening_room_state_t st = listeningRoomState();
					switch (st) {
						case LR_STATE_OFF:
							ImGui::TextDisabled("Not in a listening room.");
							ImGui::Spacing();
							if (ImGui::Button("Host a room")) {
								listeningRoomHostBegin();
							}
							ImGui::SameLine();
							ImGui::TextDisabled("Subscribe to a friend's room from their profile.");
							break;
						case LR_STATE_HOST: {
							ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
							ImGui::TextUnformatted("You are hosting");
							ImGui::PopStyleColor();
							ImGui::Separator();
							const s32 ntr = listeningRoomTrackCount();
							const s32 cur = listeningRoomCurrentIdx();
							for (s32 i = 0; i < ntr; i++) {
								const lr_track_t *t = listeningRoomTrackAt(i);
								if (!t) continue;
								ImGui::PushID(13000 + i);
								if (i == cur) {
									ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
									ImGui::Text("> %s", t->display_name);
									ImGui::PopStyleColor();
								} else {
									ImGui::Text("  %s", t->display_name);
								}
								ImGui::SameLine();
								if (ImGui::SmallButton("Play")) {
									listeningRoomHostPlayTrack(i);
								}
								ImGui::SameLine();
								if (ImGui::SmallButton("Remove")) {
									listeningRoomHostRemoveTrack(t->track_id);
									ImGui::PopID();
									break;
								}
								ImGui::PopID();
							}
							ImGui::Spacing();
							ImGui::TextUnformatted("Add track");
							ImGui::SetNextItemWidth(220.0f);
							ImGui::InputTextWithHint("track id", "mod-music id",
							                          s_LrAddIdBuf, sizeof(s_LrAddIdBuf));
							ImGui::SameLine();
							ImGui::SetNextItemWidth(180.0f);
							ImGui::InputTextWithHint("name", "display name",
							                          s_LrAddNameBuf, sizeof(s_LrAddNameBuf));
							ImGui::SameLine();
							if (ImGui::Button("Add##lradd")) {
								if (s_LrAddIdBuf[0]) {
									if (listeningRoomHostAddTrack(s_LrAddIdBuf, s_LrAddNameBuf) == 0) {
										s_LrAddIdBuf[0] = '\0';
										s_LrAddNameBuf[0] = '\0';
									}
								}
							}
							ImGui::Spacing();
							if (ImGui::Button("Stop hosting")) {
								listeningRoomLeave();
							}
							break;
						}
						case LR_STATE_LISTENER: {
							const u32 hh = listeningRoomHostHandle();
							const social_friend_t *hf = socialFriendByHandle(hh);
							ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
							ImGui::Text("Listening to %s",
							             hf && hf->agent_name[0] ? hf->agent_name : "host");
							ImGui::PopStyleColor();
							ImGui::Separator();

							const s32 ntr = listeningRoomTrackCount();
							const s32 cur = listeningRoomCurrentIdx();
							if (ntr == 0) {
								ImGui::TextDisabled("Waiting for the host to share their playlist...");
							}
							for (s32 i = 0; i < ntr; i++) {
								const lr_track_t *t = listeningRoomTrackAt(i);
								if (!t) continue;
								if (i == cur) {
									ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(255));
									ImGui::Text("> %s%s", t->display_name,
									             t->has_local_copy ? "" : " (downloading...)");
									ImGui::PopStyleColor();
								} else {
									ImGui::Text("  %s%s", t->display_name,
									             t->has_local_copy ? "" : " (queued)");
								}
							}
							ImGui::Spacing();
							if (cur >= 0) {
								if (ImGui::Button("Save current track permanently")) {
									listeningRoomPromoteCurrentTrack();
								}
							}
							ImGui::SameLine();
							if (ImGui::Button("Leave room")) {
								listeningRoomLeave();
							}
							break;
						}
						case LR_STATE_MUTED_BY_MATCH:
							ImGui::TextDisabled("Match track active -- listening-room track will resume after the match ends.");
							break;
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Public mods")) {
					ImGui::BeginChild("##pd2_pubmods_body", ImVec2(0, -60.0f));
					ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
					ImGui::TextUnformatted("My public mods");
					ImGui::PopStyleColor();
					ImGui::Separator();
					ImGui::TextWrapped(
					        "Mods listed here are visible on your profile and broadcast "
					        "to friends every minute. Trust pipeline: sha256 + friend "
					        "graph + manual install (Q11). The toggle lives in the social "
					        "layer (mod-public.json) so the mod loader stays untouched.");

					/* Local public mods list with Remove button. */
					ImGui::Spacing();
					const s32 npub = shareModPublicCount();
					if (npub == 0) {
						ImGui::TextDisabled("None registered. Use the form below to flag a mod public.");
					}
					for (s32 i = 0; i < npub; i++) {
						const char *id = shareModPublicIdAt(i);
						const char *nm = shareModPublicNameAt(i);
						if (!id) continue;
						ImGui::PushID(16000 + i);
						ImGui::Text("%s -- %s", nm && *nm ? nm : id, id);
						ImGui::SameLine();
						if (ImGui::SmallButton("Remove")) {
							shareModPublicRemove(id);
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}

					/* Inline add form. The mod_id is the internal id from
					 * Priority M's mod registry; the user types it (no
					 * dropdown to avoid pulling the mod loader's public
					 * surface into this layer). */
					static char s_AddPublicId[64];
					static char s_AddPublicName[64];
					static char s_AddPublicVer[16] = "1.0.0";
					ImGui::Spacing();
					ImGui::SetNextItemWidth(220.0f);
					ImGui::InputTextWithHint("mod id", "creator.modname",
					                          s_AddPublicId, sizeof(s_AddPublicId));
					ImGui::SameLine();
					ImGui::SetNextItemWidth(180.0f);
					ImGui::InputTextWithHint("display name", "Display name",
					                          s_AddPublicName, sizeof(s_AddPublicName));
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80.0f);
					ImGui::InputTextWithHint("ver", "1.0.0",
					                          s_AddPublicVer, sizeof(s_AddPublicVer));
					ImGui::SameLine();
					if (ImGui::Button("Add##pubmodadd")) {
						if (s_AddPublicId[0]) {
							if (shareModPublicAdd(s_AddPublicId, s_AddPublicName,
							                       s_AddPublicVer, 0) >= 0) {
								s_AddPublicId[0] = '\0';
								s_AddPublicName[0] = '\0';
							}
						}
					}

					ImGui::Spacing();
					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
					ImGui::TextUnformatted("Public mods from peers in this session");
					ImGui::PopStyleColor();
					ImGui::Separator();

					const s32 nag = shareAggregateModCount();
					if (nag == 0) {
						ImGui::TextDisabled("Empty -- friends broadcast their public mods "
						                    "every minute. Wait or refresh by re-entering the tab.");
					}
					for (s32 i = 0; i < nag; i++) {
						const share_mod_entry_t *e = shareAggregateModAt(i);
						if (!e) continue;
						const social_friend_t *of = socialFriendByHandle(e->owner_handle);
						const char *owner = of && of->agent_name[0] ? of->agent_name : "friend";
						ImGui::PushID(17000 + i);
						ImGui::Text("%s v%s by %s (%u KB)",
						             e->display_name[0] ? e->display_name : e->mod_id,
						             e->version[0] ? e->version : "?",
						             owner,
						             (unsigned)(e->size_bytes / 1024));
						ImGui::SameLine();
						if (ImGui::SmallButton("Download")) {
							if (shareSendModRequest(e->owner_handle, e->mod_id) == 0) {
								/* Send succeeded; the file_transfer pipe takes over
								 * on the responder side. The receiver will see the
								 * download appear in the chat panel's attachment
								 * row (file_transfer's existing UX). */
							}
						}
						ImGui::PopID();
					}

					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Settings")) {
					ImGui::TextUnformatted("My connect code");
					ImGui::Indent(12.0f);
					ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow(255));
					ImGui::TextUnformatted(socialMyConnectCode());
					ImGui::PopStyleColor();
					if (ImGui::SmallButton("Copy to clipboard")) {
						SDL_SetClipboardText(socialMyConnectCode());
					}
					ImGui::SameLine();
					ImGui::TextDisabled("Share this with a friend so they can add you.");
					ImGui::Unindent(12.0f);
					ImGui::Spacing();

					social_visibility_t v = socialVisibilityGet();
					ImGui::TextUnformatted("Visibility");
					if (ImGui::RadioButton("Public", v == SOCIAL_VIS_PUBLIC)) {
						socialVisibilitySet(SOCIAL_VIS_PUBLIC);
					}
					if (ImGui::RadioButton("Friends Only", v == SOCIAL_VIS_FRIENDS_ONLY)) {
						socialVisibilitySet(SOCIAL_VIS_FRIENDS_ONLY);
					}
					if (ImGui::RadioButton("Appear Offline", v == SOCIAL_VIS_APPEAR_OFFLINE)) {
						socialVisibilitySet(SOCIAL_VIS_APPEAR_OFFLINE);
					}

					ImGui::Spacing();
					ImGui::TextUnformatted("Notifications");
					u32 mask = socialNotifMaskGet();
					bool soc = (mask & SOCIAL_NOTIF_SOCIAL) != 0;
					bool inv = (mask & SOCIAL_NOTIF_INVITES) != 0;
					if (ImGui::Checkbox("Social / achievements", &soc)) {
						mask = soc ? (mask | SOCIAL_NOTIF_SOCIAL) : (mask & ~SOCIAL_NOTIF_SOCIAL);
						socialNotifMaskSet(mask);
					}
					if (ImGui::Checkbox("Invitations / messages", &inv)) {
						mask = inv ? (mask | SOCIAL_NOTIF_INVITES) : (mask & ~SOCIAL_NOTIF_INVITES);
						socialNotifMaskSet(mask);
					}

					ImGui::Spacing();
					ImGui::TextUnformatted("Voice");
					ImGui::Indent(12.0f);
					bool voice_on = voiceEnabled() != 0;
					if (ImGui::Checkbox("Enable voice chat", &voice_on)) {
						voiceSetEnabled(voice_on ? 1 : 0);
					}
					if (voice_on) {
						const voice_capture_mode_t cm = voiceGetCaptureMode();
						if (ImGui::RadioButton("Push-to-talk (default)", cm == VOICE_CAPTURE_PUSH_TO_TALK)) {
							voiceSetCaptureMode(VOICE_CAPTURE_PUSH_TO_TALK);
						}
						if (ImGui::RadioButton("Voice-activated", cm == VOICE_CAPTURE_VOICE_ACTIVE)) {
							voiceSetCaptureMode(VOICE_CAPTURE_VOICE_ACTIVE);
						}
						ImGui::TextDisabled(
						        "Default off; opt-in. PTT key = V (hold to talk). "
						        "Codec = Opus (low-latency, BSD-licensed). Per-friend mute "
						        "applies to voice the same way it applies to chat / toasts.");
					}
					ImGui::Unindent(12.0f);

					ImGui::Spacing();
					ImGui::TextUnformatted("Diagnostics");
					ImGui::TextDisabled("My handle: 0x%08x", (unsigned)socialMyHandle());
					ImGui::TextDisabled("My code:   %s",      socialMyConnectCode());
					ImGui::TextDisabled("Visibility: %s",     visibilityLabel(v));
					ImGui::TextDisabled("Pairs:     %d",      (int)p2pPairCount());
					if (ImGui::Button("Open NAT diagnostics")) {
						pdguiNatDiagnosticsOpen();
					}
					ImGui::EndTabItem();
				}

				ImGui::EndTabBar();
			}

			ImGui::Separator();
			if (ImGui::Button("Add by code")) {
				s_AddFriendOpen = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Close")) {
				pdguiFriendsSocialClose();
			}
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}

	pdguiNatDiagnosticsRender(winW, winH);
	renderChatPanel(winW, winH);
	renderProfileModal();
	renderConvertToModModal();

	if (s_AddFriendOpen) {
		ImGui::OpenPopup("Add Friend");
	}

	if (ImGui::BeginPopupModal("Add Friend", nullptr,
	                            ImGuiWindowFlags_AlwaysAutoResize |
	                            ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted("Enter a friend's connect code (4 words)");
		ImGui::SetNextItemWidth(420.0f);
		ImGui::InputText("Code", s_AddFriendCodeBuf, sizeof(s_AddFriendCodeBuf));
		ImGui::SameLine();
		if (ImGui::SmallButton("Paste")) {
			char *clip = SDL_GetClipboardText();
			if (clip) {
				strncpy(s_AddFriendCodeBuf, clip, sizeof(s_AddFriendCodeBuf) - 1);
				s_AddFriendCodeBuf[sizeof(s_AddFriendCodeBuf) - 1] = '\0';
				SDL_free(clip);
			}
		}

		ImGui::TextUnformatted("Optional nickname (local only)");
		ImGui::SetNextItemWidth(420.0f);
		ImGui::InputText("Nickname", s_AddFriendNickBuf, sizeof(s_AddFriendNickBuf));

		if (s_AddFriendStatus[0]) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning(255));
			ImGui::TextUnformatted(s_AddFriendStatus);
			ImGui::PopStyleColor();
		}

		ImGui::Spacing();
		if (ImGui::Button("Add", ImVec2(140, 0))) {
			s32 added = socialFriendAdd(s_AddFriendCodeBuf, "");
			if (added < 0) {
				snprintf(s_AddFriendStatus, sizeof(s_AddFriendStatus),
				          "Could not add: invalid code or list is full.");
			} else {
				if (s_AddFriendNickBuf[0]) {
					socialFriendSetNickname(s_AddFriendCodeBuf, s_AddFriendNickBuf);
				}
				/* Allow inbound presence pings from the new friend even before
				 * they pong us, so the address-book bootstrap works. */
				u32 h = 0;
				if (socialDecodeHandle(s_AddFriendCodeBuf, &h) == 0) {
					presencePendingInviteAdd(h);
				}
				s_AddFriendCodeBuf[0] = '\0';
				s_AddFriendNickBuf[0] = '\0';
				s_AddFriendStatus[0]  = '\0';
				s_AddFriendOpen = false;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(140, 0))) {
			s_AddFriendCodeBuf[0] = '\0';
			s_AddFriendNickBuf[0] = '\0';
			s_AddFriendStatus[0]  = '\0';
			s_AddFriendOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
