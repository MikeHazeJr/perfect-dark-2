/**
 * pdgui_toast.cpp -- bottom-right transient notification stack.
 *
 * Each toast is a 3-line popup: title (TitleGlow), body (default),
 * relative timestamp. Fade in over 200 ms, hold for ~5 s, fade out
 * over 400 ms. Up to TOAST_MAX_VISIBLE shown at once; older toasts
 * scroll off the top of the stack. The queue holds up to TOAST_MAX
 * waiting entries; overflow drops oldest pending.
 *
 * Per-category mute via social.h notification mask. Per-friend mute
 * via socialFriend.muted. Block list gates upstream (the presence
 * layer never delivers blocked-handle pongs, so they cannot reach
 * the toast enqueue path).
 */

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"

#include "pdgui_toast.h"
#include "pdgui_style.h"

extern "C" {
#include "social.h"
#include "system.h"
}

#define TOAST_MAX           16
#define TOAST_MAX_VISIBLE    4
#define TOAST_FADE_IN_MS    200u
#define TOAST_HOLD_MS      5000u
#define TOAST_FADE_OUT_MS   400u
#define TOAST_TITLE_LEN      96
#define TOAST_BODY_LEN      192

typedef struct {
	u8   in_use;
	u32  category;
	u32  friend_handle;
	u32  enqueued_ms;
	char title[TOAST_TITLE_LEN];
	char body[TOAST_BODY_LEN];
} toast_t;

static toast_t s_Toasts[TOAST_MAX];
static s32     s_NumToasts;

extern "C" void pdguiToastInit(void)
{
	memset(s_Toasts, 0, sizeof(s_Toasts));
	s_NumToasts = 0;
}

extern "C" void pdguiToastShutdown(void)
{
	memset(s_Toasts, 0, sizeof(s_Toasts));
	s_NumToasts = 0;
}

extern "C" s32 pdguiToastIsActive(void)
{
	return s_NumToasts > 0 ? 1 : 0;
}

static u32 lifetimeMs(void)
{
	return TOAST_FADE_IN_MS + TOAST_HOLD_MS + TOAST_FADE_OUT_MS;
}

extern "C" void pdguiToastTick(void)
{
	if (s_NumToasts == 0) return;
	const u32 now = SDL_GetTicks();
	const u32 life = lifetimeMs();
	s32 w = 0;
	for (s32 r = 0; r < s_NumToasts; r++) {
		if (now - s_Toasts[r].enqueued_ms < life) {
			if (w != r) s_Toasts[w] = s_Toasts[r];
			w++;
		}
	}
	for (s32 i = w; i < s_NumToasts; i++) {
		memset(&s_Toasts[i], 0, sizeof(s_Toasts[i]));
	}
	s_NumToasts = w;
}

extern "C" s32 pdguiToastEnqueue(u32 friend_handle, u32 category,
                                  const char *title, const char *body)
{
	if (!title || !*title) return 0;

	if (category != TOAST_CATEGORY_SYSTEM) {
		const u32 mask = socialNotifMaskGet();
		if ((mask & category) == 0) return 0;
	}
	if (friend_handle != 0) {
		const social_friend_t *f = socialFriendByHandle(friend_handle);
		if (f && f->muted) return 0;
		if (socialBlockIsHandle(friend_handle)) return 0;
	}

	if (s_NumToasts >= TOAST_MAX) {
		/* Drop oldest. */
		memmove(&s_Toasts[0], &s_Toasts[1],
		        (size_t)(TOAST_MAX - 1) * sizeof(toast_t));
		s_NumToasts = TOAST_MAX - 1;
	}
	toast_t *t = &s_Toasts[s_NumToasts++];
	memset(t, 0, sizeof(*t));
	t->in_use = 1;
	t->category = category;
	t->friend_handle = friend_handle;
	t->enqueued_ms = SDL_GetTicks();
	strncpy(t->title, title, TOAST_TITLE_LEN - 1);
	if (body) strncpy(t->body, body, TOAST_BODY_LEN - 1);
	return 1;
}

extern "C" void pdguiToastRender(s32 winW, s32 winH)
{
	if (s_NumToasts == 0) return;

	const u32 now = SDL_GetTicks();
	const float pad = 14.0f;
	const float toast_w = 360.0f;
	const float toast_h = 78.0f;
	const float gap = 6.0f;

	ImDrawList *dl = ImGui::GetForegroundDrawList();

	const s32 visible_count = s_NumToasts < TOAST_MAX_VISIBLE
	                          ? s_NumToasts : TOAST_MAX_VISIBLE;

	for (s32 i = 0; i < visible_count; i++) {
		const s32 idx = s_NumToasts - 1 - i; /* newest at the bottom */
		const toast_t *t = &s_Toasts[idx];
		if (!t->in_use) continue;

		const u32 age = now - t->enqueued_ms;
		float alpha = 1.0f;
		if (age < TOAST_FADE_IN_MS) {
			alpha = (float)age / (float)TOAST_FADE_IN_MS;
		} else if (age > TOAST_FADE_IN_MS + TOAST_HOLD_MS) {
			const u32 fade = age - (TOAST_FADE_IN_MS + TOAST_HOLD_MS);
			alpha = 1.0f - ((float)fade / (float)TOAST_FADE_OUT_MS);
			if (alpha < 0.0f) alpha = 0.0f;
		}
		const u8 a = (u8)(alpha * 255.0f);

		const float x1 = (float)winW - toast_w - pad;
		const float y1 = (float)winH - pad - (toast_h + gap) * (i + 1);
		const float x2 = x1 + toast_w;
		const float y2 = y1 + toast_h;

		dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2),
		                   IM_COL32(8, 12, 24, (u8)(220 * alpha)),
		                   8.0f);
		dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2),
		             pdguiImU32TitleGlow(a),
		             8.0f, 0, 1.5f);

		const ImU32 dotCol = (t->category == TOAST_CATEGORY_INVITES)
		                       ? pdguiImU32TintInfo(a)
		                       : (t->category == TOAST_CATEGORY_SYSTEM)
		                         ? pdguiImU32TintDanger(a)
		                         : pdguiImU32TintSuccess(a);
		dl->AddCircleFilled(ImVec2(x1 + 14.0f, y1 + 16.0f), 5.0f, dotCol, 16);

		dl->AddText(ImVec2(x1 + 30.0f, y1 + 8.0f),
		             IM_COL32(220, 230, 240, a),
		             t->title);
		if (t->body[0]) {
			dl->AddText(ImVec2(x1 + 30.0f, y1 + 28.0f),
			             IM_COL32(180, 190, 210, a),
			             t->body);
		}

		const u32 age_secs = age / 1000u;
		char ts[24];
		if (age_secs == 0)        snprintf(ts, sizeof(ts), "now");
		else if (age_secs < 60)   snprintf(ts, sizeof(ts), "%us ago", (unsigned)age_secs);
		else                       snprintf(ts, sizeof(ts), "%um ago", (unsigned)(age_secs / 60));
		dl->AddText(ImVec2(x1 + 30.0f, y1 + 50.0f),
		             IM_COL32(140, 150, 170, a), ts);
	}
}
