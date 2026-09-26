#ifndef PD_NET_DISTRIB_QUEUE_H
#define PD_NET_DISTRIB_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resource budgets, not catalog limits: excess requests fail explicitly. */
#define DISTRIB_QUEUE_MAX_PENDING 32768
#define DISTRIB_QUEUE_MAX_PER_CONNECTION 8192

typedef struct distrib_request {
    void *client;
    void *peer;
    uint32_t connection;
    char id[64];
    uint8_t kind;
    uint8_t temporary;
} distrib_request;

typedef struct distrib_queue {
    distrib_request *entries;
    size_t count;
    size_t capacity;
} distrib_queue;

enum distrib_queue_result {
    DISTRIB_QUEUE_REJECTED = 0,
    DISTRIB_QUEUE_ADDED = 1,
    DISTRIB_QUEUE_DUPLICATE = 2
};

int distribQueuePush(distrib_queue *queue, const distrib_request *request);
int distribQueuePop(distrib_queue *queue, distrib_request *request);
void distribQueueRemoveClient(distrib_queue *queue, const void *client);
void distribQueueClear(distrib_queue *queue);
int distribRequestSameConnection(const distrib_request *request,
    const void *client, const void *peer, uint32_t connection);

#ifdef __cplusplus
}
#endif
#endif
