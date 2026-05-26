/**
 * asset_mod_utility_contract.h -- per-family Modding Hub/CLI utility contract.
 */
#ifndef _IN_ASSET_MOD_UTILITY_CONTRACT_H
#define _IN_ASSET_MOD_UTILITY_CONTRACT_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum asset_mod_utility_op {
	ASSET_MOD_UTIL_CREATE      = 1u << 0,
	ASSET_MOD_UTIL_IMPORT      = 1u << 1,
	ASSET_MOD_UTIL_CLONE       = 1u << 2,
	ASSET_MOD_UTIL_EDIT        = 1u << 3,
	ASSET_MOD_UTIL_VALIDATE    = 1u << 4,
	ASSET_MOD_UTIL_PACKAGE     = 1u << 5,
	ASSET_MOD_UTIL_PREVIEW     = 1u << 6,
	ASSET_MOD_UTIL_HOT_ENABLE  = 1u << 7,
	ASSET_MOD_UTIL_EMBED_DEPS  = 1u << 8,
	ASSET_MOD_UTIL_TEMPLATE    = 1u << 9,
	ASSET_MOD_UTIL_SECURE_TOOL = 1u << 10,
} asset_mod_utility_op_e;

typedef struct asset_mod_utility_contract {
	asset_type_e type;
	const char *family;
	const char *extension;
	const char *descriptor;
	const char *hub_tool;
	const char *cli_noun;
	u32 ops;
} asset_mod_utility_contract_t;

size_t assetModUtilityContractCount(void);
const asset_mod_utility_contract_t *assetModUtilityContractAt(size_t index);
const asset_mod_utility_contract_t *assetModUtilityContractForType(asset_type_e type);
const asset_mod_utility_contract_t *assetModUtilityContractForExtension(const char *extension);
s32 assetModUtilitySupports(const asset_mod_utility_contract_t *contract, u32 op);
const char *assetModUtilityOperationName(u32 op);

#ifdef __cplusplus
}
#endif

#endif
