#ifndef IN_GAME_LANG_H
#define IN_GAME_LANG_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

extern u8 *g_LangBuffer;

void langInit(void);
void langReset(s32 stagenum);
void langTick(void);

u32 langGetLangBankIndexFromStagenum(s32 stagenum);
/* The stage's language bank: its archive's text_bank, else the table above. */
s32 langGetStageBank(s32 stagenum, s32 prefer_mp);
struct jpncharpixels *langGetJpnCharPixels(s32 codepoint);
s32 langGetFileNumOffset(void);
s32 langGetFileId(s32 bank);
void langLoad(s32 bank);
void langLoadToAddr(s32 bank, u8 *dst, s32 size);
void langClearBank(s32 bank);
/* Returns 1 if the bank is currently loaded (g_LangBanks[bank] != NULL). */
s32 langIsBankLoaded(s32 bank);
char *langGet(s32 textid);
void langReload(void);
void langSetEuropean(u32 arg0);
void langSetJpnEnabled(bool enable);

#endif
