/*
 * src/game/modeldata/robot.c -- skeleton + hat-position static records.
 *
 * The g_HeadsAndBodies[152] table that historically lived here was
 * RETIRED at the BYOR completion ship (2026-05-03). The data moved to
 * port/src/headdata_authored.c (head subset, 84 entries) and
 * port/src/bodydata_authored.c (body subset, 68 entries) -- see those
 * files plus port/include/headdata_authored.h /
 * port/include/bodydata_authored.h for schema and consumer rules.
 *
 * Engine code never read g_HeadsAndBodies[] directly outside the catalog
 * registration / manager layer (CLAUDE.md "Allowed read-sites" rule);
 * after the BYOR completion all those reads route through the catalog
 * managers (catalog_mgr_heads / catalog_mgr_bodies) which seed from the
 * authoring tables. The runtime per-asset emitters
 * (port/src/romextract_pdhead.c, port/src/romextract_pdbody.c,
 * port/src/romextract_pdmesh.c for head+body+hand mesh refs) read the
 * authoring tables directly and emit per-asset .pdhead / .pdbody /
 * .pdmesh files for the catalog walker.
 *
 * What stays here: the skeleton tables (g_SkelDrCaroll, g_Skel22,
 * g_SkelRobot) and the hat-position table (var8007dae4) -- they are
 * structurally part of the chr-modeling layer, not the head/body
 * registration table, and continue to be consumed directly by the
 * engine via the same C symbol surface.
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 *      "BYOR completion SHIPPED" section.
 */

#include <ultra64.h>
#include "bss.h"
#include "data.h"
#include "types.h"

u8 g_SkelDrCarollJoints[][2] = {
	{ 0, 0 },
	{ 1, 1 },
	{ 2, 2 },
	{ 3, 3 },
};

struct skeleton g_SkelDrCaroll = {
	SKEL_DRCAROLL, ARRAYCOUNT(g_SkelDrCarollJoints), g_SkelDrCarollJoints,
};

u8 g_Skel22Joints[][2] = {
	{ 0,  0  },
	{ 1,  1  },
	{ 2,  2  },
	{ 3,  3  },
	{ 4,  4  },
	{ 5,  5  },
	{ 6,  6  },
	{ 7,  7  },
	{ 8,  8  },
	{ 9,  9  },
	{ 10, 10 },
	{ 11, 11 },
	{ 12, 12 },
	{ 13, 13 },
	{ 14, 14 },
	{ 15, 15 },
	{ 16, 16 },
	{ 17, 17 },
	{ 18, 19 },
	{ 19, 18 },
	{ 20, 21 },
	{ 21, 20 },
	{ 22, 23 },
	{ 23, 22 },
	{ 24, 25 },
	{ 25, 24 },
	{ 26, 27 },
	{ 27, 26 },
	{ 28, 29 },
	{ 29, 28 },
};

struct skeleton g_Skel22 = {
	SKEL_22, ARRAYCOUNT(g_Skel22Joints), g_Skel22Joints,
};

u8 g_SkelRobotJoints[][2] = {
	{ 0, 0 },
	{ 1, 2 },
	{ 2, 1 },
};

struct skeleton g_SkelRobot = {
	SKEL_ROBOT, ARRAYCOUNT(g_SkelRobotJoints), g_SkelRobotJoints,
};

/* g_HeadsAndBodies[152] retired here -- see file header. */

/* [headnum][hattype] */
struct hatposition var8007dae4[1][6] = {
	{
		/* HEAD_SHAUN */
		{ -0.070299997925758, 0.49189999699593, -0.83359998464584, 1.072811961174,   1.0883259773254,  0.92612099647522 },
		{ -0.10000000149012,  0.42750000953674, -0.48249998688698, 1.0333679914474,  0.96552097797394, 0.92990499734879 },
		{ 0.18000000715256,   0,                0,                 1.0722140073776,  1,                1                },
		{ 0.23700000345707,   0.97699999809265, -0.43999999761581, 1.1784629821777,  1.1406099796295,  1.1434650421143  },
		{ -0.090300001204014, 0.23190000653267, 0.12639999389648,  0.99080002307892, 1.0199999809265,  0.84659999608994 },
		{ 0,                  0.14849999547005, 0.37929999828339,  1.1548000574112,  0.99190002679825, 0.95139998197556 },
	}
};
