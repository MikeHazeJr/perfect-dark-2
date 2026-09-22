#include "net/transfer_buffer.h"
#include <stdlib.h>
#include <string.h>

int netTransferBufferAppend(uint8_t **buffer, uint32_t *capacity, uint32_t *length,
    const uint8_t *data, uint32_t data_length, uint32_t maximum,
    net_transfer_realloc_fn resize)
{
    if (!buffer || !capacity || !length || !data || !data_length
            || *length > *capacity || *capacity > maximum
            || (*capacity && !*buffer) || data_length > maximum - *length) return 0;
    const uint32_t required = *length + data_length;
    if (required > *capacity) {
        uint32_t next = *capacity ? *capacity : 1;
        while (next < required) next = next > maximum / 2 ? maximum : next * 2;
        uint8_t *grown = (uint8_t *)(resize ? resize : realloc)(*buffer, next);
        if (!grown) return 0;
        *buffer = grown;
        *capacity = next;
    }
    memcpy(*buffer + *length, data, data_length);
    *length = required;
    return 1;
}

int netTransferBudgetReserve(uint32_t *reserved, uint32_t amount,
    uint32_t item_maximum, uint32_t session_maximum)
{
    if (!reserved || !amount || amount > item_maximum
            || *reserved > session_maximum
            || amount > session_maximum - *reserved) return 0;
    *reserved += amount;
    return 1;
}
