#ifndef PD_MODASSET_GLTF_DOCUMENT_H
#define PD_MODASSET_GLTF_DOCUMENT_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The source/provider interface uses signed 32-bit byte counts. */
#define MODASSET_GLTF_DOCUMENT_LIMIT 2147483647u

/* Strict, complete glTF 2.0 JSON with one buffer and nonempty bounded views.
 * GLB requires an absent URI. Text glTF requires one. Outputs remain empty on
 * rejection. Embedded data URIs may require json_size + 1 output capacity. */
s32 modAssetGltfBufferDocument(const char *json, size_t json_size, s32 glb,
    char *out_uri, size_t uri_cap, u32 *out_declared_size);

/* The same strict document boundary with an optional root buffer. A valid
 * bufferless document succeeds with an empty URI and declared size zero;
 * bufferViews must then be absent or an empty array. Nested extras do not
 * declare a root buffer. A present buffer has the required API's contract. */
s32 modAssetGltfOptionalBufferDocument(const char *json, size_t json_size, s32 glb,
    char *out_uri, size_t uri_cap, u32 *out_declared_size);

/* Admission permits a bufferless native-extras document, but never dangling
 * views. Every decoded string property is visited with its decoded key;
 * only buffers[0].uri is marked as a buffer. The caller applies its file-key
 * policy to other properties. A nonzero visitor return rejects the document. */
typedef s32 (*modasset_gltf_string_visitor)(const char *key, const char *text,
    s32 is_buffer, u32 declared_size, void *userdata);
s32 modAssetGltfDocumentVisitStrings(const char *json, size_t json_size,
    modasset_gltf_string_visitor visitor, void *userdata);

#ifdef __cplusplus
}
extern "C++" {
namespace crude_json { struct value; }
/* Format-neutral complete JSON using the same strict lexical boundary. */
bool modAssetJsonReadValue(const char *json, size_t json_size,
    crude_json::value &out) noexcept;
/* Shared strict parsed tree for C++ source consumers. Requires an object with
 * asset.version 2.0; detailed buffer and scene validation remain separate. */
bool modAssetGltfReadDocument(const char *json, size_t json_size,
    crude_json::value &out) noexcept;
}
#endif
#endif
