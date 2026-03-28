#ifndef MESHDEBUG_H
#define MESHDEBUG_H

/* F9 cycles: 0=OFF, 1=TINT rendered geo, 2=MESH ONLY (collision mesh visible) */
void meshDebugToggle(void);
int meshDebugIsEnabled(void);
int meshDebugGetMode(void);
void meshDebugRenderCollisionMesh(float vp[4][4], int width, int height);

#endif
