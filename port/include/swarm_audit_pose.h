/* Read-only pose observer. Root resolution follows the feature/sampling gates. */
#ifndef PORT_SWARM_AUDIT_POSE_H
#define PORT_SWARM_AUDIT_POSE_H
#include <PR/ultratypes.h>
struct chrdata;
#ifdef __cplusplus
extern "C" {
#endif
void swarmAuditObservePose(struct chrdata *chr, const char *path,
                          const f32 *world_to_view);
#ifdef __cplusplus
}
#endif
#endif
