/*====================================================================
 *
 * Copyright 1993, Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Silicon Graphics,
 * Inc.; the contents of this file may not be disclosed to third
 * parties, copied or duplicated in any form, in whole or in part,
 * without the prior written permission of Silicon Graphics, Inc.
 *
 * RESTRICTED RIGHTS LEGEND:
 * Use, duplication or disclosure by the Government is subject to
 * restrictions as set forth in subdivision (c)(1)(ii) of the Rights
 * in Technical Data and Computer Software clause at DFARS
 * 252.227-7013, and/or in similar or successor clauses in the FAR,
 * DOD or NASA FAR Supplement. Unpublished - rights reserved under the
 * Copyright Laws of the United States.
 *====================================================================*/

#ifndef __N_ABI__
#define	__N_ABI__

/*
 * BEGIN C-specific section: (typedef's)
 */

#if defined(_LANGUAGE_C) || defined(_LANGUAGE_C_PLUS_PLUS)

#include <mixer.h>

#define n_aNoop(pkt, outp, b, c) aDisable(pkt, outp, b, c);
#define n_aADPCMdec(pkt, s, f, c, a, d) aADPCMdec(pkt, s, f, c, a, d);
#define n_aPoleFilter(pkt, f, g, t, s) aPoleFilter(pkt, f, g, t, s);
#define n_aEnvMixer(pkt, f, t, s) aEnvMixer(pkt, f, t, s);
#define n_aInterleave(pkt) aInterleave(pkt);
#define n_aLoadBuffer(pkt, c, d, s) aLoadBuffer(pkt, c, d, s);
#define n_aResample(pkt, s, f, p, i, o) aResample(pkt, s, f, p, i, o);
#define n_aSaveBuffer(pkt, c, s, d) aSaveBuffer(pkt, c, s, d);
#define n_aSetVolume(pkt, f, v, t, r) aSetVolume(pkt, f, v, t, r);
#define n_aLoadADPCM(pkt, c, d) aLoadADPCM(pkt, c, d);

#endif /* _LANGUAGE_C */

#endif /* __N_ABI__ */







