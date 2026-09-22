#include "file_transfer_wire.h"
#include "ed25519.h"
#include <string.h>

#define FT_WIRE_DOMAIN "pd-ft-v2"
#define FT_WIRE_DOMAIN_LEN 8u
#define FT_WIRE_SIGNED_LEN (FT_WIRE_SIG_OFFSET + FT_WIRE_DOMAIN_LEN)

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t read64(const uint8_t *p)
{
    return (uint64_t)read32(p) | (uint64_t)read32(p + 4) << 32;
}
int fileTransferWireHeaderValid(const uint8_t *frame, size_t length, uint32_t local_handle)
{
    return frame && length == FT_WIRE_FRAME_LEN && local_handle &&
        !memcmp(frame, FT_WIRE_MAGIC, 5) && frame[5] == FT_WIRE_VERSION &&
        frame[6] <= 4 && !frame[7] && read32(frame + 8) &&
        read32(frame + 12) == local_handle && read64(frame + 16);
}
static void signingMessage(const uint8_t *frame, uint8_t *message)
{
    memcpy(message, frame, FT_WIRE_SIG_OFFSET);
    memcpy(message + FT_WIRE_SIG_OFFSET, FT_WIRE_DOMAIN, FT_WIRE_DOMAIN_LEN);
}
int fileTransferWireSign(uint8_t *frame, size_t capacity, const uint8_t *public_key,
    ft_wire_signer_t signer)
{
    if (!frame || capacity != FT_WIRE_FRAME_LEN || !public_key || !signer ||
            !fileTransferWireHeaderValid(frame, capacity, read32(frame + 12))) return 0;
    uint8_t message[FT_WIRE_SIGNED_LEN], signature[FT_WIRE_SIG_LEN];
    memcpy(frame + FT_WIRE_PUBKEY_OFFSET, public_key, FT_WIRE_PUBKEY_LEN);
    signingMessage(frame, message);
    if (signer(message, sizeof(message), signature) != 1) return 0;
    memcpy(frame + FT_WIRE_SIG_OFFSET, signature, sizeof(signature));
    return 1;
}
int fileTransferWireVerify(const uint8_t *frame, size_t length, uint32_t local_handle)
{
    if (!fileTransferWireHeaderValid(frame, length, local_handle)) return 0;
    uint8_t message[FT_WIRE_SIGNED_LEN];
    signingMessage(frame, message);
    return ed25519Verify(frame + FT_WIRE_SIG_OFFSET, message, sizeof(message),
        frame + FT_WIRE_PUBKEY_OFFSET) == 1;
}
int fileTransferWirePeerMatches(const uint8_t *frame, size_t length,
    uint32_t local_handle, uint32_t peer_handle, uint64_t transfer_id)
{
    return fileTransferWireHeaderValid(frame, length, local_handle) && peer_handle &&
        read32(frame + 8) == peer_handle && read64(frame + 16) == transfer_id;
}
int fileTransferWireGeometry(uint64_t bytes, uint32_t chunks, uint64_t byte_limit)
{
    return bytes && bytes <= byte_limit && chunks &&
        (bytes - 1) / FT_WIRE_PAYLOAD_LEN + 1 == chunks;
}
int fileTransferWireChunkRange(uint64_t bytes, uint32_t chunks, uint32_t sequence,
    uint32_t echoed_chunks, uint64_t *offset, uint32_t *length)
{
    if (offset) *offset = 0;
    if (length) *length = 0;
    if (!offset || !length || !fileTransferWireGeometry(bytes, chunks, UINT64_MAX) ||
            echoed_chunks != chunks || sequence >= chunks) return 0;
    const uint64_t start = (uint64_t)sequence * FT_WIRE_PAYLOAD_LEN;
    if (start >= bytes) return 0;
    const uint64_t remaining = bytes - start;
    *offset = start;
    *length = (uint32_t)(remaining < FT_WIRE_PAYLOAD_LEN ? remaining : FT_WIRE_PAYLOAD_LEN);
    return 1;
}
int fileTransferWireAckProgress(uint32_t next, uint32_t chunks, uint32_t ack, uint32_t *advanced)
{
    if (!advanced || !chunks) return 0;
    *advanced = next;
    if (ack == UINT32_MAX) {
        if (next != UINT32_MAX) return 0;
        *advanced = 0; return 1;
    }
    if (next >= chunks || ack != next) return 0;
    *advanced = next + 1; return 1;
}
