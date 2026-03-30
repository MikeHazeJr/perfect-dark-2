#ifndef _IN_NETENET_H
#define _IN_NETENET_H

#define ENET_NO_PRAGMA_LINK 1
#include "external/enet.h"
#undef bool
#undef near
#undef far
/* Restore project-wide bool=s32 after enet.h/windows.h may have clobbered it */
#define bool s32

#endif
