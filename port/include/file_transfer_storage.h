#ifndef PD_FILE_TRANSFER_STORAGE_H
#define PD_FILE_TRANSFER_STORAGE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FT_RECEIPT_CAPACITY 32
#define FT_RECEIPT_LIFETIME_MS 60000u
typedef struct {
    uint32_t local, peer;
    uint64_t id, bytes;
    uint32_t chunks;
    uint8_t digest[32];
    uint8_t request_digest[32];
} ft_receipt_key_t;
typedef struct {
    ft_receipt_key_t key;
    uint8_t tail_digest[32];
    uint32_t saved_at;
    int valid;
} ft_saved_receipt_t;
typedef struct {
    ft_saved_receipt_t entries[FT_RECEIPT_CAPACITY];
    uint32_t next;
} ft_receipt_store_t;
/* Caller clears the store on Agent rebind/shutdown. Every lookup also requires
 * the exact local/peer identity. A receipt describes verified data saved locally,
 * not optional package activation or protection across receiver restarts. */
const ft_saved_receipt_t *fileTransferReceiptFind(const ft_receipt_store_t *,
    uint32_t local, uint32_t peer, uint64_t id, uint32_t now);
int fileTransferReceiptMatchesInit(const ft_saved_receipt_t *, const ft_receipt_key_t *);
int fileTransferReceiptMatchesChunk(const ft_saved_receipt_t *, uint32_t chunks,
    uint32_t sequence, const uint8_t *payload, size_t available);
int fileTransferStoreCommit(ft_receipt_store_t *, const ft_receipt_key_t *,
    const char *destination, const uint8_t *bytes, size_t length, uint32_t now,
    char *error, size_t error_capacity);
/* No remote Agent string becomes a directory. Digest prefix prevents different
 * file contents with the same display name from replacing each other. */
int fileTransferStorageNames(uint32_t peer, const char *name, const uint8_t digest[32],
    char *folder, size_t folder_capacity, char *file, size_t file_capacity);
#ifdef __cplusplus
}
#endif
#endif
