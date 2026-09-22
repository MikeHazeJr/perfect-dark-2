#ifndef PD_GROUP_SESSION_POLICY_H
#define PD_GROUP_SESSION_POLICY_H
#include "net/group_session.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct group_session_retire_ops {
    void (*peer)(void *context, u32 handle, u32 pair_id);
    void (*route)(void *context);
    void *context;
} group_session_retire_ops;

/* Returns 1 unchanged, 2 rebound, or 0 when identity/transport prevents use.
 * A live ENet transport retains its existing owner until normal teardown. */
s32 groupSessionRefreshOwner(group_session_t *session, u32 local_handle,
    s32 transport_active, const group_session_retire_ops *retire);
s32 groupSessionPeerAccepted(const group_session_t *session,
    u32 local_handle, u32 peer_handle);

#ifdef __cplusplus
}
#endif
#endif
