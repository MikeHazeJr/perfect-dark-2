/**
 * pdgui_menu_update.cpp -- ImGui update notification and version picker (D13).
 *
 * Provides:
 *   - Update notification banner (shown when a new version is available)
 *   - Version picker dialog (accessible from Settings → About/Update)
 *   - Download progress overlay
 *   - Release channel selector (Stable / Dev)
 *
 * Renders as an ImGui overlay — not tied to the hotswap system.
 * Called from pdguiRender() every frame when update state is relevant.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Use extern "C" forward declarations for all game symbols.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_audio.h"
#include "system.h"

extern "C" {
#include "updater.h"
#include "updateversion.h"
#include "pdgui_pausemenu.h"
s32 pdguiHotswapWasActive(void);
}

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_ShowNotification = false;     /* notification banner visible */
static bool s_ShowVersionPicker = false;    /* full version picker dialog */
static bool s_NotificationDismissed = false; /* user closed the banner this session */
static float s_NotificationTimer = 0.0f;    /* seconds since notification appeared */
static int s_SelectedRelease = -1;          /* selected row in version picker */
static bool s_ConfirmDownload = false;      /* confirm download dialog */
static bool s_DownloadActive = false;       /* download in progress */
static bool s_DownloadFailed = false;       /* last download attempt failed */
static bool s_RestartPrompt = false;        /* download done, prompt restart */

/* ========================================================================
 * Update notification banner
 *
 * Rendered as a small bar at the top of the screen. Appears when an
 * update is available and the user hasn't dismissed it this session.
 * ======================================================================== */

static void renderNotificationBanner(void)
{
	if (s_NotificationDismissed || !updaterIsUpdateAvailable()) {
		s_ShowNotification = false;
		return;
	}

	const updater_release_t *latest = updaterGetLatest();
	if (!latest) return;

	char verstr[64];
	versionFormat(&latest->version, verstr, sizeof(verstr));

	ImGuiIO &io = ImGui::GetIO();
	float barHeight = 40.0f;
	ImVec2 barPos(0, 0);
	ImVec2 barSize(io.DisplaySize.x, barHeight);

	ImGui::SetNextWindowPos(barPos);
	ImGui::SetNextWindowSize(barSize);
	ImGui::SetNextWindowBgAlpha(0.92f);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.35f, 0.12f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 8));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (ImGui::Begin("##update_banner", nullptr, flags)) {
		/* Layout: [icon] text  [View] [Dismiss] */
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "UPDATE");
		ImGui::SameLine();
		ImGui::Text("Version %s is available", verstr);
		ImGui::SameLine();

		float btnWidth = 80.0f;
		float updateBtnWidth = 100.0f;
		float dismissX = io.DisplaySize.x - btnWidth - 16;
		float viewX = dismissX - btnWidth - 8;
		float updateX = viewX - updateBtnWidth - 8;

		ImGui::SetCursorPosX(updateX);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.25f, 1.0f));
		if (ImGui::SmallButton("Update Now")) {
			/* Start downloading the latest release immediately */
			const updater_release_t *latest = updaterGetLatest();
			if (latest && latest->assetUrl[0]) {
				s_DownloadFailed = false;
				updaterDownloadAsync(latest);
				s_DownloadActive = true;
				s_NotificationDismissed = true;
				s_ShowNotification = false;
			}
		}
		ImGui::PopStyleColor(2);
		ImGui::SameLine();
		ImGui::SetCursorPosX(viewX);
		if (ImGui::SmallButton("Details")) {
			s_ShowVersionPicker = true;
		}
		ImGui::SameLine();
		ImGui::SetCursorPosX(dismissX);
		if (ImGui::SmallButton("Dismiss")) {
			s_NotificationDismissed = true;
			s_ShowNotification = false;
		}
	}
	ImGui::End();

	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}

/* ========================================================================
 * Download progress overlay
 * ======================================================================== */

static void renderDownloadProgress(void)
{
	if (!s_DownloadActive) return;

	updater_status_t status = updaterGetStatus();

	if (status == UPDATER_DOWNLOAD_DONE) {
		s_DownloadActive = false;
		s_RestartPrompt = true;
		return;
	}

	if (status == UPDATER_DOWNLOAD_FAILED) {
		s_DownloadActive = false;
		s_DownloadFailed = true;
		return;
	}

	if (status != UPDATER_DOWNLOADING) return;

	updater_progress_t prog = updaterGetProgress();

	ImGuiIO &io = ImGui::GetIO();
	ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(400, 160));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("Downloading Update", nullptr, flags)) {
		/* Show download size */
		if (prog.bytesTotal > 0) {
			ImGui::Text("Downloading: %.1f / %.1f MB",
				(double)prog.bytesDownloaded / (1024.0 * 1024.0),
				(double)prog.bytesTotal / (1024.0 * 1024.0));
		} else {
			ImGui::Text("Downloading: %.1f MB",
				(double)prog.bytesDownloaded / (1024.0 * 1024.0));
		}

		/* Progress bar */
		ImGui::ProgressBar(prog.percent / 100.0f, ImVec2(-1, 24));

		ImGui::Spacing();

		/* Cancel button */
		float btnWidth = 100.0f;
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - btnWidth) * 0.5f);
		if (ImGui::Button("Cancel", ImVec2(btnWidth, 0))) {
			updaterDownloadCancel();
			s_DownloadActive = false;
		}
	}
	ImGui::End();
}

/* ========================================================================
 * Restart prompt
 * ======================================================================== */

static void renderRestartPrompt(void)
{
	if (!s_RestartPrompt) return;

	ImGuiIO &io = ImGui::GetIO();
	ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(380, 140));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.25f, 0.08f, 0.95f));

	if (ImGui::Begin("Update Ready", nullptr, flags)) {
		ImGui::TextWrapped("Update downloaded and verified. Restart the game to apply.");
		ImGui::Spacing();
		ImGui::Spacing();

		float btnWidth = 120.0f;
		float totalWidth = btnWidth * 2 + 16;
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

		if (ImGui::Button("Restart Now", ImVec2(btnWidth, 0))) {
			/* Re-launch the game, then exit this process.
			 * updaterApplyPending() in the new process will apply the update. */
#ifdef _WIN32
			STARTUPINFOA si;
			PROCESS_INFORMATION pi;
			memset(&si, 0, sizeof(si));
			memset(&pi, 0, sizeof(pi));
			si.cb = sizeof(si);
			char exePath[512];
			GetModuleFileNameA(NULL, exePath, sizeof(exePath));
			if (CreateProcessA(exePath, GetCommandLineA(),
					NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
			}
#endif
			SDL_Event quitEvent;
			quitEvent.type = SDL_QUIT;
			SDL_PushEvent(&quitEvent);
		}
		ImGui::SameLine(0, 16);
		if (ImGui::Button("Later", ImVec2(btnWidth, 0))) {
			s_RestartPrompt = false;
		}
	}
	ImGui::End();

	ImGui::PopStyleColor();
}

/* ========================================================================
 * Version picker dialog
 * ======================================================================== */

/**
 * Render the version picker content (header, table, buttons).
 * Shared between the floating dialog and the inline Settings tab.
 * tableH: height for the version list table (use 0 for auto-fill).
 */
static void renderVersionPickerContent(float tableH)
{
	/* Header: current version + channel */
	const pdversion_t *cur = updaterGetCurrentVersion();
	char curstr[64];
	if (cur) {
		versionFormat(cur, curstr, sizeof(curstr));
	} else {
		snprintf(curstr, sizeof(curstr), "(unknown)");
	}

	ImGui::Text("Current version: %s", curstr);
	ImGui::SameLine(0, 16);

	/* Channel selector */
	update_channel_t channel = updaterGetChannel();
	const char *channelLabels[] = { "Stable", "Dev / Test" };
	ImGui::SetNextItemWidth(120);
	int channelInt = (int)channel;
	if (ImGui::Combo("Channel##upd", &channelInt, channelLabels, 2)) {
		updaterSetChannel((update_channel_t)channelInt);
		/* Re-check with new channel */
		updaterCheckAsync();
	}

	ImGui::Separator();

	/* Status line */
	updater_status_t status = updaterGetStatus();
	switch (status) {
	case UPDATER_IDLE:
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Not checked yet");
		break;
	case UPDATER_CHECKING:
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1), "Checking for updates...");
		break;
	case UPDATER_CHECK_DONE: {
		s32 count = updaterGetReleaseCount();
		if (updaterIsUpdateAvailable()) {
			const updater_release_t *lat = updaterGetLatest();
			char latstr[64];
			versionFormat(&lat->version, latstr, sizeof(latstr));
			ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1),
				"Update available: v%s (%d versions found)", latstr, count);
		} else {
			ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1),
				"Up to date (%d versions found)", count);
		}
		break;
	}
	case UPDATER_CHECK_FAILED:
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1), "Check failed: %s", updaterGetError());
		break;
	case UPDATER_DOWNLOADING:
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1), "Downloading...");
		break;
	case UPDATER_DOWNLOAD_DONE:
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1), "Download complete — restart to apply");
		break;
	case UPDATER_DOWNLOAD_FAILED:
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1), "Download failed: %s", updaterGetError());
		break;
	}

	/* Check button */
	ImGui::SameLine();
	if (status != UPDATER_CHECKING && status != UPDATER_DOWNLOADING) {
		if (ImGui::SmallButton("Check Now")) {
			updaterCheckAsync();
		}
	}

	ImGui::Spacing();

	/* Version list table */
	if (status == UPDATER_CHECK_DONE || status == UPDATER_DOWNLOAD_DONE ||
	    status == UPDATER_DOWNLOAD_FAILED) {

		s32 count = updaterGetReleaseCount();

		if (count > 0 && ImGui::BeginTable("versions", 4,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
			ImVec2(0, tableH > 0 ? tableH : 280))) {

			ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 100);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70);
			ImGui::TableHeadersRow();

			for (s32 i = 0; i < count; i++) {
				const updater_release_t *rel = updaterGetRelease(i);
				if (!rel) continue;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				/* Version with current marker */
				char verstr[64];
				versionFormat(&rel->version, verstr, sizeof(verstr));

				bool isCurrent = cur && (versionCompare(&rel->version, cur) == 0);
				bool isNewer = cur && (versionCompare(&rel->version, cur) > 0);

				bool selected = (s_SelectedRelease == i);
				char selectableId[128];
				snprintf(selectableId, sizeof(selectableId), "%s##rel_%d", verstr, i);

				if (ImGui::Selectable(selectableId, selected,
					ImGuiSelectableFlags_SpanAllColumns)) {
					s_SelectedRelease = i;
				}

				if (isCurrent) {
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "(current)");
				} else if (isNewer) {
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "*");
				}

				ImGui::TableNextColumn();
				if (rel->isPrerelease) {
					ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Dev");
				} else {
					ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Stable");
				}

				ImGui::TableNextColumn();
				ImGui::TextWrapped("%s", rel->name[0] ? rel->name : "(no title)");

				ImGui::TableNextColumn();
				if (rel->assetSize > 0) {
					ImGui::Text("%.1f MB", (double)rel->assetSize / (1024.0 * 1024.0));
				} else {
					ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "—");
				}
			}

			ImGui::EndTable();
		}

		/* Changelog for selected release */
		if (s_SelectedRelease >= 0 && s_SelectedRelease < count) {
			const updater_release_t *sel = updaterGetRelease(s_SelectedRelease);
			if (sel && sel->body[0]) {
				ImGui::Spacing();
				ImGui::Text("Changelog:");
				ImGui::BeginChild("changelog", ImVec2(0, 80), true);
				ImGui::TextWrapped("%s", sel->body);
				ImGui::EndChild();
			}
		}

		/* Action buttons */
		ImGui::Spacing();

		if (s_SelectedRelease >= 0 && s_SelectedRelease < count) {
			const updater_release_t *sel = updaterGetRelease(s_SelectedRelease);
			bool isCurrent = sel && cur && (versionCompare(&sel->version, cur) == 0);

			if (sel && !isCurrent && sel->assetUrl[0] &&
			    status != UPDATER_DOWNLOADING) {
				const char *btnLabel = s_DownloadFailed
					? "Retry Download" : "Download & Install";
				if (ImGui::Button(btnLabel, ImVec2(160, 0))) {
					s_DownloadFailed = false;
					updaterDownloadAsync(sel);
					s_DownloadActive = true;
				}

				/* Rollback warning */
				if (sel && cur && versionCompare(&sel->version, cur) < 0) {
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1),
						"(rollback — older than current)");
				}
			}

			/* Show download failure message */
			if (s_DownloadFailed) {
				const char *errMsg = updaterGetError();
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1),
					"Download failed: %s",
					(errMsg && errMsg[0]) ? errMsg : "unknown error");
			}
		}
	}
}

static void renderVersionPicker(void)
{
	if (!s_ShowVersionPicker) return;

	ImGuiIO &io = ImGui::GetIO();
	ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Appearing);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
	bool open = true;

	if (ImGui::Begin("Update Manager", &open, flags)) {
		renderVersionPickerContent(280);
	}
	ImGui::End();

	if (!open) {
		s_ShowVersionPicker = false;
		s_SelectedRelease = -1;
	}
}

/* ========================================================================
 * Version watermark — bottom-right corner, always visible
 * ======================================================================== */

static void renderVersionWatermark(void)
{
	/* Only show on main menu (hotswap active) or when paused */
	if (!pdguiHotswapWasActive() && !pdguiIsPauseMenuOpen()) return;

	const char *verStr = updaterGetVersionString();
	if (!verStr || !verStr[0]) return;

	char label[96];
	update_channel_t ch = updaterGetChannel();
	if (ch == UPDATE_CHANNEL_DEV) {
		snprintf(label, sizeof(label), "v%s (dev)", verStr);
	} else {
		snprintf(label, sizeof(label), "v%s", verStr);
	}

	ImGuiIO &io = ImGui::GetIO();
	ImVec2 textSize = ImGui::CalcTextSize(label);
	float padding = 8.0f;
	float x = io.DisplaySize.x - textSize.x - padding;
	float y = io.DisplaySize.y - textSize.y - padding;

	ImGui::SetNextWindowPos(ImVec2(x - padding, y - padding * 0.5f));
	ImGui::SetNextWindowSize(ImVec2(textSize.x + padding * 2, textSize.y + padding));
	ImGui::SetNextWindowBgAlpha(0.35f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoNav;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding * 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

	if (ImGui::Begin("##version_watermark", nullptr, flags)) {
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 0.6f), "%s", label);
	}
	ImGui::End();

	ImGui::PopStyleVar(2);
}

/* ========================================================================
 * Public API — called from pdguiRender() every frame
 * ======================================================================== */

extern "C" {

/**
 * Render all update UI elements. Called every frame from the ImGui render loop.
 */
void pdguiUpdateRender(void)
{
	updater_status_t status = updaterGetStatus();

	/* Version watermark — always visible in bottom-right corner */
	renderVersionWatermark();

	/* Show notification banner when update available */
	if (status == UPDATER_CHECK_DONE && updaterIsUpdateAvailable() && !s_NotificationDismissed) {
		s_ShowNotification = true;
	}

	if (s_ShowNotification) {
		renderNotificationBanner();
	}

	if (s_ShowVersionPicker) {
		renderVersionPicker();
	}

	if (s_DownloadActive) {
		renderDownloadProgress();
	}

	if (s_RestartPrompt) {
		renderRestartPrompt();
	}
}

/**
 * Open the version picker as a floating dialog (from notification banner).
 */
void pdguiUpdateShowPicker(void)
{
	s_ShowVersionPicker = true;

	/* Trigger a check if we haven't done one */
	updater_status_t status = updaterGetStatus();
	if (status == UPDATER_IDLE || status == UPDATER_CHECK_FAILED) {
		updaterCheckAsync();
	}
}

/**
 * Render update UI inline inside the Settings tab.
 * Called every frame while the "Updates" tab is active.
 * Unlike pdguiUpdateShowPicker(), this does NOT open a floating window —
 * it renders directly into the current ImGui context (the tab's child).
 */
void pdguiUpdateRenderSettingsTab(void)
{
	/* Auto-trigger a version check the first time the tab is viewed */
	static bool s_TabCheckTriggered = false;
	if (!s_TabCheckTriggered) {
		updater_status_t status = updaterGetStatus();
		if (status == UPDATER_IDLE || status == UPDATER_CHECK_FAILED) {
			updaterCheckAsync();
		}
		s_TabCheckTriggered = true;
	}

	/* Use 0 for table height — let it fill available space */
	renderVersionPickerContent(0);
}

/**
 * Check if any update UI is currently visible.
 */
s32 pdguiUpdateIsActive(void)
{
	return (s_ShowVersionPicker || s_DownloadActive || s_RestartPrompt) ? 1 : 0;
}

} /* extern "C" */
