#include "net/group_session_policy.h"
#include <string.h>

s32 groupSessionRefreshOwner(group_session_t *session, u32 local_handle,
        s32 transport_active, const group_session_retire_ops *retire)
{
    if (!session || !local_handle) return 0;
    if (session->local_handle == local_handle) return 1;
    if (transport_active || !retire || !retire->peer || !retire->route) return 0;

    /* Retire the old identities before removing the state needed to cancel
     * probes/relay candidates. Do not re-arm debug invites or auto-connect. */
    for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; ++i) {
        if (session->peers[i].handle)
            retire->peer(retire->context, session->peers[i].handle,
                session->peers[i].pair_id);
    }
    retire->route(retire->context);
    memset(session, 0, sizeof(*session));
    session->local_handle = local_handle;
    return 2;
}

s32 groupSessionPeerAccepted(const group_session_t *session,
        u32 local_handle, u32 peer_handle)
{
    if (!session || !local_handle || !peer_handle || peer_handle == local_handle
            || session->local_handle != local_handle || !session->in_session)
        return 0;
    for (s32 i = 0; i < GROUP_SESSION_MAX_PEERS; ++i) {
        const group_peer_t *peer = &session->peers[i];
        if (peer->handle == peer_handle)
            return peer->state == GROUP_PEER_RESOLVING
                || peer->state == GROUP_PEER_CONNECTED;
    }
    return 0;
}
