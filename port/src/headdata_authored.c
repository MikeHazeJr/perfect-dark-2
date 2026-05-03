/*
 * port/src/headdata_authored.c -- Catalog Universality BYOR Completion
 * (2026-05-03).
 *
 * AUTHORED EXTRACTOR SOURCE-OF-TRUTH for HEAD metadata.
 *
 * 84 head entries (75 named MP/SP heads + 9 SP-fallback heads). Reverse-
 * engineered from the historical g_HeadsAndBodies[] table (head subset
 * filtered by unk00_01 == 1). Catalog ID slugs match the prior
 * base/heads.pdbase JSON archive so historical references survive the
 * pivot.
 *
 * Engine-API constraint: nothing in src/ or port/ outside the catalog
 * registration code (assetcatalog_base.c, assetcatalog_base_extended.c)
 * and the runtime emitter (port/src/romextract_pdhead.c, plus
 * port/src/romextract_pdmesh.c which walks head mesh refs) may include
 * headdata_authored.h. Engine reads route through the catalog.
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>
#include "data.h"
#include "types.h"
#include "constants.h"
#include "headdata_authored.h"

const head_authored_record_t g_HeadData[] = {
	{ "base:head_dark_combat",   4, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADDARK_COMBAT              ,                 1.0f,               1.0f },
	{ "base:head_elvis",   5, 1, 1, HEADBODYTYPE_MAIAN            ,  27, FILE_CHEADELVIS                    ,                 1.0f,               1.0f },
	{ "base:head_ross",   6, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADROSS                     ,                 1.0f,               1.0f },
	{ "base:head_carrington",   7, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADCARRINGTON               ,                 1.0f,               1.0f },
	{ "base:head_mrblonde",   8, 1, 1, HEADBODYTYPE_MRBLONDE         ,  13, FILE_CHEADMRBLONDE                 ,                 1.0f,               1.0f },
	{ "base:head_trent",   9, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADTRENT                    ,                 1.0f,               1.0f },
	{ "base:head_ddshock",  10, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADDDSHOCK                  ,                 1.0f,               1.0f },
	{ "base:head_graham",  11, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADGRAHAM                   ,                 1.0f,               1.0f },
	{ "base:head_dark_frock",  12, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADDARK_FROCK               ,                 1.0f,               1.0f },
	{ "base:head_secretary",  13, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADSECRETARY                ,                 1.0f,               1.0f },
	{ "base:head_cassandra",  14, 0, 1, HEADBODYTYPE_CASS             ,  13, FILE_CHEADCASSANDRA                ,                 1.0f,               1.0f },
	{ "base:sp_head_15",  15, 1, 1, HEADBODYTYPE_MAIAN            ,  27, FILE_CHEADTHEKING                  ,                 1.0f,               1.0f },
	{ "base:head_fem_guard",  16, 0, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADFEM_GUARD                ,                 1.0f,               1.0f },
	{ "base:head_jon",  17, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADJON                      ,                 1.0f,               1.0f },
	{ "base:head_mark2",  18, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADMARK2                    ,                 1.0f,               1.0f },
	{ "base:head_christ",  19, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADCHRIST                   ,                 1.0f,               1.0f },
	{ "base:head_russ",  20, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADRUSS                     ,                 1.0f,               1.0f },
	{ "base:sp_head_21",  21, 1, 1, HEADBODYTYPE_MAIAN            ,  13, FILE_CHEADGREY                     ,                 1.0f,               1.0f },
	{ "base:head_darling",  22, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADDARLING                  ,                 1.0f,               1.0f },
	{ "base:head_robert",  23, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADROBERT                   ,                 1.0f,               1.0f },
	{ "base:head_beau1",  24, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBEAU                     ,                 1.0f,               1.0f },
	{ "base:head_fem_guard2",  25, 0, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADFEM_GUARD2               ,                 1.0f,               1.0f },
	{ "base:head_brian",  26, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBRIAN                    ,                 1.0f,               1.0f },
	{ "base:head_jamie",  27, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADJAMIE                    ,                 1.0f,               1.0f },
	{ "base:head_duncan2",  28, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADDUNCAN2                  ,                 1.0f,               1.0f },
	{ "base:head_biotech",  29, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBIOTECH                  ,                 1.0f,               1.0f },
	{ "base:head_neil2",  30, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADNEIL2                    ,                 1.0f,               1.0f },
	{ "base:head_edmcg",  31, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADEDMCG                    ,                 1.0f,               1.0f },
	{ "base:head_anka",  32, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADANKA                     ,                 1.0f,               1.0f },
	{ "base:head_leslie_s",  33, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADLESLIE_S                 ,                 1.0f,               1.0f },
	{ "base:head_matt_c",  34, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADMATT_C                   ,                 1.0f,               1.0f },
	{ "base:head_peer_s",  35, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADPEER_S                   ,                 1.0f,               1.0f },
	{ "base:head_eileen_t",  36, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADEILEEN_T                 ,                 1.0f,               1.0f },
	{ "base:head_andy_r",  37, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADANDY_R                   ,                 1.0f,               1.0f },
	{ "base:head_ben_r",  38, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBEN_R                    ,                 1.0f,               1.0f },
	{ "base:head_steve_k",  39, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADSTEVE_K                  ,                 1.0f,               1.0f },
	{ "base:head_jonathan",  40, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADJONATHAN                 ,                 1.0f,               1.0f },
	{ "base:head_maian_s",  41, 1, 1, HEADBODYTYPE_MAIAN            ,  27, FILE_CHEADMAIAN_S                  ,                 1.0f,               1.0f },
	{ "base:head_shaun",  42, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADSHAUN                    ,                 1.0f,               1.0f },
	{ "base:sp_head_43",  43, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBEAU                     ,                 1.0f,               1.0f },
	{ "base:head_eileen_h",  44, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADEILEEN_H                 ,                 1.0f,               1.0f },
	{ "base:head_scott_h",  45, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADSCOTT_H                  ,                 1.0f,               1.0f },
	{ "base:head_sanchez",  46, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADSANCHEZ                  ,                 1.0f,               1.0f },
	{ "base:head_darkaqua",  47, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADDARKAQUA                 ,                 1.0f,               1.0f },
	{ "base:head_ddsniper",  48, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADDDSNIPER                 ,                 1.0f,               1.0f },
	{ "base:sp_head_49",  49, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBEAU                     ,                 1.0f,               1.0f },
	{ "base:sp_head_50",  50, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBEAU                     ,                 1.0f,               1.0f },
	{ "base:sp_head_51",  51, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBEAU                     ,                 1.0f,               1.0f },
	{ "base:sp_head_52",  52, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADBEAU                     ,                 1.0f,               1.0f },
	{ "base:head_griffey",  53, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADGRIFFEY                  ,                 1.0f,               1.0f },
	{ "base:head_moto",  54, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADMOTO                     ,                 1.0f,               1.0f },
	{ "base:head_keith",  55, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADKEITH                    ,                 1.0f,               1.0f },
	{ "base:head_winner",  56, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADWINNER                   ,                 1.0f,               1.0f },
	{ "base:head_a51faceplate",  57, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CA51FACEPLATE                 ,                 1.0f,               1.0f },
	{ "base:head_elvis_gogs",  58, 1, 1, HEADBODYTYPE_MAIAN            ,  27, FILE_CHEADELVIS_GOGS               ,                 1.0f,               1.0f },
	{ "base:head_stevem",  59, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADSTEVEM                   ,                 1.0f,               1.0f },
	{ "base:head_dark_snow",  60, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEADDARK_SNOW                ,                 1.0f,               1.0f },
	{ "base:head_president",  61, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADPRESIDENT                ,                 1.0f,               1.0f },
	{ "base:head_vd",  62, 0, 1, HEADBODYTYPE_FEMALE           ,  13, FILE_CHEAD_VD                      ,                 1.0f,               1.0f },
	{ "base:head_ken",  63, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADKEN                      ,                 1.0f,               1.0f },
	{ "base:head_joel",  64, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADJOEL                     ,                 1.0f,               1.0f },
	{ "base:head_tim",  65, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADTIM                      ,                 1.0f,               1.0f },
	{ "base:head_grant",  66, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADGRANT                    ,                 1.0f,               1.0f },
	{ "base:head_penny",  67, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADPENNY                    ,                 1.0f,               1.0f },
	{ "base:head_robin",  68, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADROBIN                    ,                 1.0f,               1.0f },
	{ "base:head_alex",  69, 0, 1, HEADBODYTYPE_FEMALEGUARD      ,  13, FILE_CHEADALEX                     ,                 1.0f,               1.0f },
	{ "base:head_julianne",  70, 0, 1, HEADBODYTYPE_FEMALEGUARD      ,  13, FILE_CHEADJULIANNE                 ,                 1.0f,               1.0f },
	{ "base:head_laura",  71, 0, 1, HEADBODYTYPE_FEMALEGUARD      ,  13, FILE_CHEADLAURA                    ,                 1.0f,               1.0f },
	{ "base:head_davec",  72, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADDAVEC                    ,                 1.0f,               1.0f },
	{ "base:head_cook",  73, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADCOOK                     ,                 1.0f,               1.0f },
	{ "base:head_pryce",  74, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADPRYCE                    ,                 1.0f,               1.0f },
	{ "base:head_silke",  75, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADSILKE                    ,                 1.0f,               1.0f },
	{ "base:head_smith",  76, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADSMITH                    ,                 1.0f,               1.0f },
	{ "base:head_gareth",  77, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADGARETH                   ,                 1.0f,               1.0f },
	{ "base:head_murchie",  78, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADMURCHIE                  ,                 1.0f,               1.0f },
	{ "base:head_wong",  79, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADWONG                     ,                 1.0f,               1.0f },
	{ "base:head_carter",  80, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADCARTER                   ,                 1.0f,               1.0f },
	{ "base:head_tintin",  81, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADTINTIN                   ,                 1.0f,               1.0f },
	{ "base:head_munton",  82, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADMUNTON                   ,                 1.0f,               1.0f },
	{ "base:head_stamper",  83, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADSTAMPER                  ,                 1.0f,               1.0f },
	{ "base:head_jones",  84, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADJONES                    ,                 1.0f,               1.0f },
	{ "base:head_phelps",  85, 1, 1, HEADBODYTYPE_DEFAULT          ,  13, FILE_CHEADPHELPS                   ,                 1.0f,               1.0f },
	{ "base:sp_head_107", 107, 1, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CDRCARROLL                    ,                 1.0f,               1.0f },
	{ "base:sp_head_112", 112, 1, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CTESTCHR                      ,                 1.0f,               1.0f },
};

const s32 g_HeadDataCount = (s32)(sizeof(g_HeadData) / sizeof(g_HeadData[0]));

const head_authored_record_t *headDataLookupByHeadnum(s32 headnum)
{
	for (s32 i = 0; i < g_HeadDataCount; i++) {
		if ((s32)g_HeadData[i].headnum == headnum) {
			return &g_HeadData[i];
		}
	}
	return 0;
}
