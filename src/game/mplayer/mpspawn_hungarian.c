/*
 * mpspawn_hungarian.c -- O(n^3) min-cost assignment (Hungarian / Kuhn-Munkres).
 *
 * Algorithm after KACTL (CC0):
 * https://github.com/kth-competitive-programming/kactl/blob/main/content/graph/WeightedMatching.h
 * (via github.com/bqi343/USACO Hungarian.h)
 */

#include <ultra64.h>
#include <string.h>
#include <stdint.h>
#include "game/mplayer/mpspawn_hungarian.h"

#define HUNG_INF ((int64_t)0x2000000000000000LL)

#if SPAWNPOOL_MAX + 2 > 256
#error SPAWNPOOL_MAX too large for hungarian static buffers
#endif

int64_t mpHungarianMinSquare(s32 n, const int64_t *a_rowmajor, s32 *match_out)
{
	static int64_t u[SPAWNPOOL_MAX + 2];
	static int64_t v[SPAWNPOOL_MAX + 2];
	static int32_t p[SPAWNPOOL_MAX + 2];
	static int64_t dist[SPAWNPOOL_MAX + 2];
	static int32_t pre[SPAWNPOOL_MAX + 2];
	static unsigned char done[SPAWNPOOL_MAX + 2];
	s32 n1;
	s32 m1;
	s32 i;
	s32 j;
	s32 j0;
	s32 i0;
	s32 j1;
	int64_t delta;
	int64_t cur;

	if (n <= 0) {
		return 0;
	}

	for (i = 0; i < n; i++) {
		match_out[i] = -1;
	}

	n1 = n + 1;
	m1 = n + 1;

	memset(u, 0, sizeof(u));
	memset(v, 0, sizeof(v));
	memset(p, 0, sizeof(p));

	for (i = 1; i < n1; i++) {
		p[0] = i;
		j0 = 0;
		for (j = 0; j <= m1; j++) {
			dist[j] = HUNG_INF;
			pre[j] = -1;
			done[j] = 0;
		}
		do {
			done[j0] = 1;
			i0 = p[j0];
			j1 = j0;
			delta = HUNG_INF;
			for (j = 1; j < m1; j++) {
				if (!done[j]) {
					cur = a_rowmajor[(i0 - 1) * n + (j - 1)] - u[i0] - v[j];
					if (cur < dist[j]) {
						dist[j] = cur;
						pre[j] = j0;
					}
					if (dist[j] < delta) {
						delta = dist[j];
						j1 = j;
					}
				}
			}
			for (j = 0; j < m1; j++) {
				if (done[j]) {
					u[p[j]] += delta;
					v[j] -= delta;
				} else {
					dist[j] -= delta;
				}
			}
			j0 = j1;
		} while (p[j0]);

		while (j0) {
			j1 = pre[j0];
			p[j0] = p[j1];
			j0 = j1;
		}
	}

	for (j = 1; j < m1; j++) {
		if (p[j] != 0) {
			match_out[p[j] - 1] = j - 1;
		}
	}

	return -v[0];
}
