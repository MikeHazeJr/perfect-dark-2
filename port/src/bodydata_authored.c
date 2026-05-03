/*
 * port/src/bodydata_authored.c -- Catalog Universality BYOR Completion
 * (2026-05-03).
 *
 * AUTHORED EXTRACTOR SOURCE-OF-TRUTH for BODY metadata.
 *
 * 68 body entries (63 named MP/SP bodies + 5 SP-fallback bodies). Reverse-
 * engineered from the historical g_HeadsAndBodies[] table (body subset
 * filtered by unk00_01 == 0 + filenum != 0). Catalog ID slugs match the
 * prior base/bodies.pdbase JSON archive so historical references survive.
 *
 * Engine-API constraint: nothing in src/ or port/ outside the catalog
 * registration code (assetcatalog_base.c, assetcatalog_base_extended.c,
 * assetcatalog_api.c) and the runtime emitter (port/src/romextract_pdbody.c,
 * plus port/src/romextract_pdmesh.c which walks body+hand mesh refs)
 * may include bodydata_authored.h. Engine reads route through the catalog.
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>
#include "data.h"
#include "types.h"
#include "constants.h"
#include "bodydata_authored.h"

const body_authored_record_t g_BodyData[] = {
	{ "base:djbond",   0, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 167, FILE_CDJBOND             ,                 1.0f,         1.044600964f, FILE_GHAND_DDSECURITY     },
	{ "base:connery",   1, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 167, FILE_CCONNERY            ,                 1.0f,          1.03004694f, FILE_GHAND_MRBLONDE       },
	{ "base:dalton",   2, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 165, FILE_CDALTON             ,                 1.0f,         1.057276964f, FILE_GHAND_DDSECURITY     },
	{ "base:moore",   3, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 167, FILE_CMOORE              ,                 1.0f,         1.039906144f, FILE_GHAND_DDSECURITY     },
	{ "base:dark_combat",  86, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARK_COMBAT        ,                 1.0f,        0.9530516267f, FILE_GCOMBATHANDSLOD      },
	{ "base:elvis1",  87, 1, 0, 0, HEADBODYTYPE_MAIAN            , 106, FILE_CELVIS1             ,                 1.0f,        0.5727699399f, FILE_GHAND_ELVIS          },
	{ "base:area51guard",  88, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 157, FILE_CAREA51GUARD        ,                 1.0f,        0.9276995659f, FILE_GHAND_A51GUARD       },
	{ "base:overall",  89, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_COVERALL            ,                 1.0f,        0.9276995659f, FILE_GHAND_A51GUARD       },
	{ "base:carrington",  90, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 154, FILE_CCARRINGTON         ,                 1.0f,        0.8591549397f, FILE_GHAND_CARRINGTON     },
	{ "base:mrblonde",  91, 1, 0, 0, HEADBODYTYPE_MRBLONDE         , 169, FILE_CMRBLONDE           ,                 1.0f,         1.103286386f, FILE_GHAND_MRBLONDE       },
	{ "base:skedar",  92, 1, 1, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CSKEDAR             ,                 1.0f,                 1.0f, 0                         },
	{ "base:trent",  93, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 161, FILE_CTRENT              ,                 1.0f,        0.9389671683f, FILE_GHAND_TRENT          },
	{ "base:ddshock",  94, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 157, FILE_CDDSHOCK            ,                 1.0f,        0.9389671683f, FILE_GHAND_DDFODDER       },
	{ "base:labtech",  95, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 157, FILE_CLABTECH            ,                 1.0f,        0.9389671683f, FILE_GHAND_MRBLONDE       },
	{ "base:stripes",  96, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 158, FILE_CSTRIPES            ,                 1.0f,        0.9276995659f, FILE_GHAND_BLACKGUARD     },
	{ "base:dark_frock",  97, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARK_FROCK         ,                 1.0f,        0.9530516267f, FILE_GHAND_JOFROCK        },
	{ "base:dark_trench",  98, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARK_TRENCH        ,                 1.0f,        0.9530516267f, FILE_GHAND_JOTRENCH       },
	{ "base:officeworker",  99, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 157, FILE_COFFICEWORKER       ,                 1.0f,        0.9389671683f, FILE_GHAND_JOFROCK        },
	{ "base:officeworker2", 100, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 157, FILE_COFFICEWORKER2      ,                 1.0f,        0.9389671683f, FILE_GHAND_JOFROCK        },
	{ "base:secretary", 101, 0, 0, 0, HEADBODYTYPE_FEMALE           , 140, FILE_CSECRETARY          ,                 1.0f,        0.8732394576f, FILE_GHAND_JOFROCK        },
	{ "base:cassandra", 102, 0, 0, 0, HEADBODYTYPE_CASS             , 167, FILE_CCASSANDRA          ,                 1.0f,        0.9859155416f, FILE_GHAND_VRIES          },
	{ "base:theking", 103, 1, 0, 0, HEADBODYTYPE_MAIAN            , 106, FILE_CTHEKING            ,                 1.0f,        0.5727699399f, FILE_GHAND_ELVIS          },
	{ "base:fem_guard", 104, 0, 0, 1, HEADBODYTYPE_FEMALEGUARD      , 160, FILE_CFEM_GUARD          ,                 1.0f,        0.9671362042f, FILE_GHAND_JOTRENCH       },
	{ "base:dd_labtech", 105, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 157, FILE_CDD_LABTECH         ,                 1.0f,        0.9389671683f, FILE_GHAND_DDLABTECH      },
	{ "base:dd_secguard", 106, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 160, FILE_CDD_SECGUARD        ,                 1.0f,        0.9342722893f, FILE_GHAND_DDSECURITY     },
	{ "base:drcaroll", 107, 1, 1, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CDRCARROLL          ,                 1.0f,                 1.0f, 0                         },
	{ "base:sp_body_108", 108, 1, 1, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CEYESPY             ,                 1.0f,                 1.0f, 0                         },
	{ "base:dark_ripped", 109, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARK_RIPPED        ,                 1.0f,        0.9530516267f, FILE_GHAND_JOFROCK        },
	{ "base:dd_guard", 110, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 160, FILE_CDD_GUARD           ,                 1.0f,        0.9389671683f, FILE_GHAND_DDSECURITY     },
	{ "base:dd_shock_inf", 111, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 157, FILE_CDD_SHOCK_INF       ,                 1.0f,        0.9389671683f, FILE_GHAND_DDSHOCK        },
	{ "base:biotech", 113, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 155, FILE_CBIOTECH            ,                 1.0f,        0.9389671683f, FILE_GHAND_DDBIO          },
	{ "base:fbiguy", 114, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CFBIGUY             ,                 1.0f,        0.9389671683f, FILE_GHAND_FBIARM         },
	{ "base:ciaguy", 115, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CCIAGUY             ,                 1.0f,        0.9389671683f, FILE_GHAND_CIA            },
	{ "base:a51trooper", 116, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CA51TROOPER         ,                 1.0f,        0.9389671683f, FILE_GHAND_JOFROCK        },
	{ "base:a51airman", 117, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 157, FILE_CA51AIRMAN          ,                 1.0f,        0.9389671683f, FILE_GHAND_A51AIRMAN      },
	{ "base:sp_body_118", 118, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CCHICROB            ,                 1.0f,                 1.0f, 0                         },
	{ "base:steward", 119, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 153, FILE_CSTEWARD            ,                 1.0f,         0.892018795f, FILE_GHAND_JOFROCK        },
	{ "base:stewardess", 120, 0, 0, 0, HEADBODYTYPE_FEMALE           , 143, FILE_CSTEWARDESS         ,                 1.0f,        0.8544600606f, FILE_GHAND_JOFROCK        },
	{ "base:president", 121, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CPRESIDENT          ,                 1.0f,        0.9389671683f, FILE_GHAND_PRESIDENT      },
	{ "base:stewardess_coat", 122, 0, 0, 0, HEADBODYTYPE_FEMALE           , 143, FILE_CSTEWARDESS_COAT    ,                 1.0f,        0.8544600606f, FILE_GHAND_STEWARDESS_COAT },
	{ "base:sp_body_123", 123, 1, 1, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CMINISKEDAR         ,                0.75f,                 0.5f, 0                         },
	{ "base:nsa_lackey", 124, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CNSA_LACKEY         ,                 1.0f,        0.9389671683f, FILE_GHAND_CARRINGTON     },
	{ "base:pres_security", 125, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CPRES_SECURITY      ,                 1.0f,        0.9389671683f, FILE_GHAND_CARRINGTON     },
	{ "base:negotiator", 126, 0, 0, 0, HEADBODYTYPE_FEMALE           , 142, FILE_CNEGOTIATOR         ,                 1.0f,        0.8544600606f, FILE_GHAND_JOFROCK        },
	{ "base:g5_guard", 127, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CG5_GUARD           ,                 1.0f,        0.9389671683f, FILE_GHAND_G5GUARD        },
	{ "base:pelagic_guard", 128, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CPELAGIC_GUARD      ,                 1.0f,        0.9389671683f, FILE_GHAND_TRAGIC_PELAGIC },
	{ "base:g5_swat_guard", 129, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 158, FILE_CG5_SWAT_GUARD      ,                 1.0f,        0.9389671683f, FILE_GHAND_G5GUARD        },
	{ "base:alaskan_guard", 130, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 158, FILE_CALASKAN_GUARD      ,                 1.0f,        0.9389671683f, FILE_GHAND_JOSNOW         },
	{ "base:maian_soldier", 131, 1, 0, 1, HEADBODYTYPE_MAIAN            , 106, FILE_CMAIAN_SOLDIER      ,                 1.0f,        0.5727699399f, FILE_GHAND_ELVIS          },
	{ "base:sp_body_132", 132, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CPRESIDENT_CLONE    ,                 1.0f,        0.9389671683f, FILE_GCOMBATHANDSLOD      },
	{ "base:president_clone2", 133, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CPRESIDENT_CLONE    ,                 1.0f,        0.9389671683f, FILE_GHAND_PRESIDENT      },
	{ "base:dark_af1", 134, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARK_AF1           ,                 1.0f,        0.9530516267f, FILE_GHAND_JOPILOT        },
	{ "base:darkwet", 135, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARKWET            ,                 1.0f,        0.9530516267f, FILE_GHAND_JOWETSUIT      },
	{ "base:darkaqualung", 136, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARKAQUALUNG       ,                 1.0f,        0.9530516267f, FILE_GHAND_JOWETSUIT      },
	{ "base:darksnow", 137, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARKSNOW           ,                 1.0f,        0.9530516267f, FILE_GHAND_JOSNOW         },
	{ "base:darklab", 138, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARKLAB            ,                 1.0f,        0.9530516267f, FILE_GHAND_MRBLONDE       },
	{ "base:femlabtech", 139, 0, 0, 1, HEADBODYTYPE_FEMALE           , 159, FILE_CFEMLABTECH         ,                 1.0f,        0.8732394576f, FILE_GHAND_MRBLONDE       },
	{ "base:ddsniper", 140, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CDDSNIPER           ,                 1.0f,        0.9389671683f, FILE_GHAND_DDSNIPER       },
	{ "base:pilotaf1", 141, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CPILOTAF1           ,                 1.0f,        0.8826290965f, FILE_GHAND_JOPILOT        },
	{ "base:cilabtech", 142, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CCILABTECH          ,                 1.0f,        0.9389671683f, FILE_GHAND_CIFEMTECH      },
	{ "base:cifemtech", 143, 0, 0, 1, HEADBODYTYPE_FEMALE           , 159, FILE_CCIFEMTECH          ,                 1.0f,        0.8685446382f, FILE_GHAND_CIFEMTECH      },
	{ "base:carreveningsuit", 144, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CCARREVENINGSUIT    ,                 1.0f,        0.8591549397f, FILE_GHAND_MRBLONDE       },
	{ "base:jonathan", 145, 1, 0, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CJONATHON           ,                 1.0f,        0.9389671683f, FILE_GHAND_A51GUARD       },
	{ "base:cisoldier", 146, 1, 0, 1, HEADBODYTYPE_DEFAULT          , 159, FILE_CCISOLDIER          ,                 1.0f,        0.9389671683f, FILE_GHAND_CISOLDIER      },
	{ "base:sp_body_147", 147, 1, 1, 0, HEADBODYTYPE_DEFAULT          , 159, FILE_CSKEDARKING         ,                 1.0f,                1.25f, 0                         },
	{ "base:elviswaistcoat", 148, 1, 0, 0, HEADBODYTYPE_MAIAN            , 106, FILE_CELVISWAISTCOAT     ,                 1.0f,        0.5727699399f, FILE_GHAND_ELVIS          },
	{ "base:dark_leather", 149, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARK_LEATHER       ,                 1.0f,        0.9530516267f, FILE_GHAND_JOFROCK        },
	{ "base:dark_negotiator", 150, 0, 0, 0, HEADBODYTYPE_FEMALE           , 159, FILE_CDARK_NEGOTIATOR    ,                 1.0f,        0.9530516267f, FILE_GHAND_JOAF1          },
};

const s32 g_BodyDataCount = (s32)(sizeof(g_BodyData) / sizeof(g_BodyData[0]));
