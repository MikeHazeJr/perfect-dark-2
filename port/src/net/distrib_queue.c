#include "net/distrib_queue.h"
#include <stdlib.h>
#include <string.h>

int distribRequestSameConnection(const distrib_request *request,
    const void *client, const void *peer, uint32_t connection)
{
    return request && client && peer && request->client == client
        && request->peer == peer && request->connection == connection;
}

int distribQueuePush(distrib_queue *queue, const distrib_request *request)
{
    if (!queue || !request || !request->client || !request->peer
            || !request->id[0] || !memchr(request->id, 0, sizeof(request->id)))
        return DISTRIB_QUEUE_REJECTED;

    size_t connection_count = 0;
    for (size_t i = 0; i < queue->count; ++i) {
        const distrib_request *pending = &queue->entries[i];
        if (!distribRequestSameConnection(pending, request->client,
                request->peer, request->connection)) continue;
        ++connection_count;
        /* Preserve the originally admitted storage choice on retransmission. */
        if (pending->kind == request->kind && strcmp(pending->id, request->id) == 0)
            return DISTRIB_QUEUE_DUPLICATE;
    }
    if (connection_count >= DISTRIB_QUEUE_MAX_PER_CONNECTION
            || queue->count >= DISTRIB_QUEUE_MAX_PENDING)
        return DISTRIB_QUEUE_REJECTED;
    if (queue->count == queue->capacity) {
        size_t capacity = queue->capacity ? queue->capacity * 2 : 128;
        if (capacity > DISTRIB_QUEUE_MAX_PENDING) capacity = DISTRIB_QUEUE_MAX_PENDING;
        distrib_request *entries = realloc(queue->entries, capacity * sizeof(*entries));
        if (!entries) return DISTRIB_QUEUE_REJECTED;
        queue->entries = entries;
        queue->capacity = capacity;
    }
    queue->entries[queue->count++] = *request;
    return DISTRIB_QUEUE_ADDED;
}

int distribQueuePop(distrib_queue *queue, distrib_request *request)
{
    if (!queue || !request || !queue->count) return 0;
    *request = queue->entries[0];
    --queue->count;
    memmove(queue->entries, queue->entries + 1, queue->count * sizeof(*queue->entries));
    return 1;
}

void distribQueueRemoveClient(distrib_queue *queue, const void *client)
{
    if (!queue || !client) return;
    size_t keep = 0;
    for (size_t i = 0; i < queue->count; ++i) {
        if (queue->entries[i].client != client)
            queue->entries[keep++] = queue->entries[i];
    }
    queue->count = keep;
}

void distribQueueClear(distrib_queue *queue)
{
    if (!queue) return;
    free(queue->entries);
    memset(queue, 0, sizeof(*queue));
}
