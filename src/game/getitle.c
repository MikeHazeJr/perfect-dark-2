#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "game/title.h"
#include "lib/vi.h"
#include "lib/dma.h"
#include "lib/lib_317f0.h"
#include "data.h"
#include "types.h"

Mtx *var8009cc80;
Mtx *var8009cc84;
Mtx *var8009cc88;
f32 var8009cc8c;
void *var8009cc90;

u32 var80062410 = 0x00000000;
u8 var80062414 = 3;
u32 var80062418 = 0x00dc0000;
u32 var8006241c = 0x00dc0000;
u32 var80062420 = 0x00ff0000;
u32 var80062424 = 0x00ff0000;
u32 var80062428 = 0x007f0000;
u32 var8006242c = 0x00000000;
u32 var80062430 = 0xdcdcdc00;
u32 var80062434 = 0xdcdcdc00;
u32 var80062438 = 0xffffff00;
u32 var8006243c = 0xffffff00;
u32 var80062440 = 0x007f0000;
u32 var80062444 = 0x00000000;
Lights1 var80062448 = gdSPDefLights1(0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0);
struct coord g_GetitleCamPos = {0, 0, 4883};
struct coord g_GetitleCamLook = {0, 0, -1};
struct coord g_GetitleCamUp = {0, 1, 0};
f32 var80062484 = 0;
struct coord var80062488 = {1, 0, 0};
u32 var80062494 = 0x00000001;
s32 var80062498 = 0;
u32 var8006249c = 0x00000000;
