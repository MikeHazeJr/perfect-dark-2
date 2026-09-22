#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "assetcatalog.h"
#include "fs.h"
#include "lib/meshcollision.h"
#include "modasset_compiler.h"
#include "smoke_harness.h"
#include "system.h"

static int numericWrite(const char *path, const void *data, size_t size)
{
    FILE *file = fsFileOpenWrite(path);
    int ok;
    if (!file) return 0;
    ok = fwrite(data, 1, size, file) == size && fflush(file) == 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static void numericWord(unsigned char *bytes, unsigned int word)
{
    for (int i = 0; i < 4; ++i) bytes[i] = (unsigned char)(word >> (8 * i));
}

/* Real glTF source buffer floats reach both native geometry consumers. */
int assetSourceNumericHarnessRun(void)
{
    const char *path = "$S/asset-numeric-smoke/model.gltf";
    const char *json = "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":48,\"uri\":\"geometry.bin\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,"
        "\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[2,2,0]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"_PD_ROOM\":1}}]}]}";
    const unsigned int positions[9] = {0,0,0,0x40000000u,0,0,0,0x40000000u,0};
    const unsigned int rooms[] = {0u, 0x46fffe00u, 0x7fc00000u, 0x7f800000u,
        0xff800000u, 0xbf800000u, 0x47000000u};
    unsigned char binary[48];
    int passed = 0;
    if (!smokeHarnessIsActive() || !fsCreateDir("$S/asset-numeric-smoke")) return -1;
    if (!numericWrite(path, json, strlen(json))) return -1;
    for (int i = 0; i < 9; ++i) numericWord(binary + i * 4, positions[i]);
    for (int i = 0; i < (int)(sizeof(rooms) / sizeof(rooms[0])); ++i) {
        struct colmesh collision = {0};
        struct modeldef *model = NULL;
        asset_entry_t entry = {0};
        int col, mdl, valid, expected = i < 2;
        for (int v = 0; v < 3; ++v) numericWord(binary + 36 + v * 4, rooms[i]);
        if (!numericWrite("$S/asset-numeric-smoke/geometry.bin", binary, sizeof(binary))) return -1;
        strcpy(entry.id, "smoke:numeric_room");
        entry.type = ASSET_PROP;
        col = modAssetCompilerBuildColmesh(path, &collision);
        mdl = modAssetCompilerBuildModeldef(&entry, path, &model);
        valid = expected ? col == 1 && collision.tris && mdl == 1 && model
            : col == -1 && !collision.tris && mdl == -1 && !model;
        sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.NUMERIC: room_bits=%08x expected=%d result=%s",
            rooms[i], expected, valid ? "PASS" : "FAIL");
        meshFree(&collision);
        modAssetCompilerFreeModeldef(model);
        if (!valid) return -1;
        ++passed;
    }
    sysLogPrintf(LOG_NOTE, "ASSET.SOURCE.NUMERIC: cases=%d result=PASS", passed);
    return 0;
}
