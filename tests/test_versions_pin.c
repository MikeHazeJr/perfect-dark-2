/*
 * tests/test_versions_pin.c -- C TU that reads the live version constants
 * out of net/net.h and mpsetups.h and exposes them as plain externs the
 * C++ test side can compare against.
 *
 * Living in C (not C++) lets us include game headers that pull in
 * types.h without dragging the `#define bool s32` macro into a C++ TU.
 */

#include <PR/ultratypes.h>
#include "net/net.h"        /* NET_PROTOCOL_VER */
#include "mpsetups.h"       /* MPSETUP_VERSION */

const u32 g_TestLiveNetProtocolVer = NET_PROTOCOL_VER;
const u32 g_TestLiveMpsetupVersion = MPSETUP_VERSION;
