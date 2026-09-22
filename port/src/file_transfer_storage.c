#include "file_transfer_storage.h"
#include "file_transfer_wire.h"
#include "save_atomic.h"
#include "sha256.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

static int reject(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return 0;
}
const ft_saved_receipt_t *fileTransferReceiptFind(const ft_receipt_store_t *store,
    uint32_t local, uint32_t peer, uint64_t id, uint32_t now)
{
    if (!store || !local || !peer || !id) return NULL;
    for (size_t i = 0; i < FT_RECEIPT_CAPACITY; ++i) {
        const ft_saved_receipt_t *r = &store->entries[i];
        if (r->valid && r->key.local == local && r->key.peer == peer && r->key.id == id &&
                (uint32_t)(now - r->saved_at) < FT_RECEIPT_LIFETIME_MS) return r;
    }
    return NULL;
}
int fileTransferReceiptMatchesInit(const ft_saved_receipt_t *r, const ft_receipt_key_t *key)
{
    return r && key && r->valid && r->key.local == key->local && r->key.peer == key->peer &&
        r->key.id == key->id && r->key.bytes == key->bytes && r->key.chunks == key->chunks &&
        !memcmp(r->key.digest, key->digest, 32) && !memcmp(r->key.request_digest, key->request_digest, 32);
}
int fileTransferReceiptMatchesChunk(const ft_saved_receipt_t *r, uint32_t chunks,
    uint32_t sequence, const uint8_t *payload, size_t available)
{
    uint64_t offset; uint32_t length;
    if (!r || !r->valid || !payload || sequence != r->key.chunks - 1 ||
            !fileTransferWireChunkRange(r->key.bytes, r->key.chunks, sequence, chunks, &offset, &length) ||
            available < length) return 0;
    uint8_t digest[32]; sha256Hash(payload, length, digest);
    return !memcmp(digest, r->tail_digest, sizeof(digest));
}
int fileTransferStoreCommit(ft_receipt_store_t *store, const ft_receipt_key_t *key,
    const char *destination, const uint8_t *bytes, size_t length, uint32_t now,
    char *error, size_t error_capacity)
{
    if (error && error_capacity) error[0] = 0;
    if (!store || !key || !key->local || !key->peer || !key->id || !destination ||
            !destination[0] || !bytes || key->bytes != length ||
            !fileTransferWireGeometry(key->bytes, key->chunks, SIZE_MAX))
        return reject(error, error_capacity, "Invalid received file geometry or owner.");
    uint64_t tail_offset; uint32_t tail_length;
    if (!fileTransferWireChunkRange(key->bytes, key->chunks, key->chunks - 1,
            key->chunks, &tail_offset, &tail_length))
        return reject(error, error_capacity, "Could not form saved-file receipt.");
    uint8_t digest[32]; sha256Hash(bytes, length, digest);
    if (memcmp(digest, key->digest, 32)) return reject(error, error_capacity, "Received file hash mismatch.");
    struct stat info;
    int exists = stat(destination, &info) == 0;
    if (exists) {
        if (!S_ISREG(info.st_mode) || sha256HashFile(destination, digest) != 0 || memcmp(digest, key->digest, 32))
            return reject(error, error_capacity, "Inbox destination contains different or unreadable content.");
    } else {
        if (errno != ENOENT) return reject(error, error_capacity, "Could not inspect inbox destination.");
        save_atomic_file_t transaction;
        if (saveAtomicBegin(&transaction, destination) != 0)
            return reject(error, error_capacity, "Could not create inbox candidate.");
        if (fwrite(bytes, 1, length, saveAtomicStream(&transaction)) != length) {
            saveAtomicAbort(&transaction);
            return reject(error, error_capacity, "Could not write complete inbox candidate.");
        }
        if (saveAtomicCommit(&transaction) != 0)
            return reject(error, error_capacity, "Could not commit verified inbox file.");
    }
    ft_saved_receipt_t *receipt = &store->entries[store->next++ % FT_RECEIPT_CAPACITY];
    memset(receipt, 0, sizeof(*receipt)); receipt->key = *key; receipt->saved_at = now;
    sha256Hash(bytes + (size_t)tail_offset, tail_length, receipt->tail_digest);
    receipt->valid = 1;
    return 1;
}
int fileTransferStorageNames(uint32_t peer, const char *name, const uint8_t digest[32],
    char *folder, size_t folder_capacity, char *file, size_t file_capacity)
{
    if (!peer || !digest || !folder || folder_capacity < 9 || !file || file_capacity < 82) return 0;
    snprintf(folder, folder_capacity, "%08x", (unsigned)peer);
    const char *base = name && name[0] ? name : "received";
    for (const char *p = base; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    char safe[49]; size_t n = strlen(base); if (n > 48) n = 48;
    for (size_t i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)base[i];
        safe[i] = ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_') ? (char)ch : '_';
    }
    safe[n] = 0;
    const char *extension = strrchr(base, '.');
    if (strlen(base) > 48 && extension && strlen(extension) <= 10) {
        size_t ext_len = strlen(extension);
        for (size_t i = 0; i < ext_len; ++i) {
            unsigned char ch = (unsigned char)extension[i];
            safe[48 - ext_len + i] = ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '.') ? (char)ch : '_';
        }
    }
    const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) { file[i * 2] = hex[digest[i] >> 4]; file[i * 2 + 1] = hex[digest[i] & 15]; }
    snprintf(file + 32, file_capacity - 32, "_%s", n ? safe : "received");
    return 1;
}
