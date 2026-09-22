#ifndef PD_FILE_TRANSFER_WIRE_H
#define PD_FILE_TRANSFER_WIRE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FT_WIRE_MAGIC "PDFTX"
#define FT_WIRE_VERSION 2u
#define FT_WIRE_PAYLOAD_OFFSET 200u
#define FT_WIRE_PAYLOAD_LEN 1024u
#define FT_WIRE_PUBKEY_OFFSET (FT_WIRE_PAYLOAD_OFFSET + FT_WIRE_PAYLOAD_LEN)
#define FT_WIRE_PUBKEY_LEN 32u
#define FT_WIRE_SIG_OFFSET (FT_WIRE_PUBKEY_OFFSET + FT_WIRE_PUBKEY_LEN)
#define FT_WIRE_SIG_LEN 64u
#define FT_WIRE_FRAME_LEN (FT_WIRE_SIG_OFFSET + FT_WIRE_SIG_LEN)

typedef int32_t (*ft_wire_signer_t)(const void *, uint32_t, uint8_t *);
/* Exact frame length, supported version/kind, zero reserved byte and nonzero
 * sender/recipient/transfer identity. Every packet targets the current Agent. */
int fileTransferWireHeaderValid(const uint8_t *frame, size_t length, uint32_t local_handle);
int fileTransferWireSign(uint8_t *frame, size_t capacity, const uint8_t *public_key,
    ft_wire_signer_t signer);
int fileTransferWireVerify(const uint8_t *frame, size_t length, uint32_t local_handle);
int fileTransferWirePeerMatches(const uint8_t *frame, size_t length,
    uint32_t local_handle, uint32_t peer_handle, uint64_t transfer_id);
int fileTransferWireGeometry(uint64_t bytes, uint32_t chunks, uint64_t byte_limit);
int fileTransferWireChunkRange(uint64_t bytes, uint32_t chunks, uint32_t sequence,
    uint32_t echoed_chunks, uint64_t *offset, uint32_t *length);
/* An old INIT ACK or a stale/future chunk ACK cannot rewind/skip progress. */
int fileTransferWireAckProgress(uint32_t next, uint32_t chunks, uint32_t ack, uint32_t *advanced);
#ifdef __cplusplus
}
#endif
#endif
