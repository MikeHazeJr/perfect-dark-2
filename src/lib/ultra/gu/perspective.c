#include <ultra64.h>
#include "system.h" /* sysLogPrintf for the B-936 degenerate-fovy guard */

void guPerspectiveF(float mf[4][4], u16 *perspNorm, float fovy, float aspect, float near, float far, float scale)
{
	float cot;
	int	i, j;

	guMtxIdentF(mf);

	/* B-936: a degenerate field-of-view (fovy <= ~0, e.g. an un-ticked menu
	 * cutscene camera) makes cot(fovy/2) blow up to +inf, and guMtxF2L then
	 * overflows the projection's x/y scale (mf[0][0]/mf[1][1]) to INT_MIN
	 * (0x80000000). fast3d reads that back as -32768, so every world vertex
	 * projects ~32768x out of frame -- giant off-screen triangles that render
	 * nothing (the CI menu chr-body + desk PC + furniture all vanish while the
	 * scene-renderer room, which uses its own VP, stays correct). The z/w terms
	 * don't depend on fovy, so only x/y explode -- matching the captured matrix
	 * (P[0][0]=-32768, z/w sane). Clamp to a sane range so the matrix stays
	 * finite. A zero/negative/NaN fovy is never a valid frustum, so this only
	 * fires on the degenerate case and is a no-op for all normal + zoom views. */
	if (!(fovy > 0.5f && fovy < 179.0f)) {
		static s32 s_degenerateFovyWarned = 0;
		if (s_degenerateFovyWarned < 8) {
			sysLogPrintf(LOG_WARNING,
				"guPerspectiveF: degenerate fovy=%f (aspect=%f near=%f far=%f) -> clamping to 60.0",
				fovy, aspect, near, far);
			s_degenerateFovyWarned++;
		}
		fovy = 60.0f;
	}

	fovy *= 3.1415926f / 180.0f;
	cot = cosf(fovy * 0.5f) / sinf(fovy * 0.5f);

	mf[0][0] = cot / aspect;
	mf[1][1] = cot;
	mf[2][2] = (near + far) / (near - far);
	mf[2][3] = -1;
	mf[3][2] = (2.0f * near * far) / (near - far);
	mf[3][3] = 0;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			mf[i][j] *= scale;
		}
	}

	if (perspNorm != (u16 *) NULL) {
		if (near + far <= 2.0f) {
			*perspNorm = (u16) 0xFFFF;
		} else {
			*perspNorm = (u16) ((2.0f * 65536.0f) / (near + far));

			if (*perspNorm <= 0) {
				*perspNorm = (u16) 0x0001;
			}
		}
	}
}

void guPerspective(Mtx *m, u16 *perspNorm, float fovy, float aspect, float near, float far, float scale)
{
	float mf[4][4];

	guPerspectiveF(mf, perspNorm, fovy, aspect, near, far, scale);

	guMtxF2L(mf, m);
}
