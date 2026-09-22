#ifndef PD_NET_TRANSFER_BUFFER_H
#define PD_NET_TRANSFER_BUFFER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void *(*net_transfer_realloc_fn)(void *, size_t);
/* Failure preserves all buffer metadata and bytes. Pass NULL for normal realloc. */
int netTransferBufferAppend(uint8_t **buffer, uint32_t *capacity, uint32_t *length,
    const uint8_t *data, uint32_t data_length, uint32_t maximum,
    net_transfer_realloc_fn resize);
/* Reserve the declared expanded size once per accepted transfer. A failed
 * reservation leaves the session budget unchanged. */
int netTransferBudgetReserve(uint32_t *reserved, uint32_t amount,
    uint32_t item_maximum, uint32_t session_maximum);
#ifdef __cplusplus
}
#endif
#endif
