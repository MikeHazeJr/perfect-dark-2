#ifndef IN_GAME_MODELDEF_H
#define IN_GAME_MODELDEF_H
#include <ultra64.h>
#include "data.h"
#include "types.h"
#include "assetprovider.h"

void modeldef0f1a7560(struct modeldef *modeldef, u16 filenum, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5);
void modelPromoteTypeToPointer(struct modeldef *modeldef);
struct modeldef *modeldefLoad(u16 fileid, u8 *arg1, s32 arg2, struct texpool *arg3);
struct modeldef *modeldefLoadFromHandle(asset_data_handle_t handle, s32 source_filenum, u8 *arg1, s32 arg2, struct texpool *arg3);
struct modeldef *modeldefLoadToNew(u16 fileid);
struct modeldef *modeldefLoadToNewFromHandle(asset_data_handle_t handle, s32 source_filenum);

#endif
