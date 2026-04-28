/*
 * menugraph.h -- Action-map menu transition graph.
 *
 * The graph describes high-level menu transitions independently from
 * renderer-local button code. Initial consumers use it for logging,
 * validation, and narrow call-site migration.
 */

#ifndef _IN_PORT_MENUGRAPH_H
#define _IN_PORT_MENUGRAPH_H

#include <PR/ultratypes.h>
#include "actionmap.h"
#include "menupool.h"

#ifdef __cplusplus
extern "C" {
#endif

struct menudialogdef;

typedef enum MenuGraphDestKind {
    MENU_GRAPH_DEST_NONE = 0,
    MENU_GRAPH_DEST_PUSH_MENU,
    MENU_GRAPH_DEST_POP_TO_PARENT,
    MENU_GRAPH_DEST_POP_TO_ROOT,
    MENU_GRAPH_DEST_SWITCH_SIBLING,
    MENU_GRAPH_DEST_SCENE_EVENT,
    MENU_GRAPH_DEST_STAGE_CHANGE,
    MENU_GRAPH_DEST_NETWORK_OP,
    MENU_GRAPH_DEST_PROCESS_EXIT,
    MENU_GRAPH_DEST_LOCAL_OP,
    MENU_GRAPH_DEST_COUNT
} MenuGraphDestKind;

typedef struct MenuGraphEdge {
    const char *id;
    InputAction trigger;
    const char *label;
    MenuGraphDestKind kind;
    union {
        menu_type_t push_target;
        menu_type_t sibling_target;
        s32 scene_event;
        s32 stagenum;
        const char *network_op;
        const char *local_op;
    } payload;
} MenuGraphEdge;

typedef struct MenuGraphNode {
    menu_type_t type;
    const char *name;
    const MenuGraphEdge *edges;
    s32 edge_count;
} MenuGraphNode;

typedef s32 (*MenuGraphNetworkOpFn)(void *userdata);
typedef s32 (*MenuGraphSceneOpFn)(void *userdata);
typedef s32 (*MenuGraphPushOpFn)(void *userdata);
typedef s32 (*MenuGraphProcessOpFn)(void *userdata);
typedef s32 (*MenuGraphPopOpFn)(void *userdata);
typedef s32 (*MenuGraphLocalOpFn)(void *userdata);

const MenuGraphNode *menuGraphNode(menu_type_t type);
const MenuGraphNode *menuGraphNodeAt(s32 index);
s32 menuGraphNodeCount(void);
const MenuGraphEdge *menuGraphEdge(menu_type_t source, const char *edge_id);
const char *menuGraphDestKindName(MenuGraphDestKind kind);

/* Fire a dialog-push edge where the renderer supplies the legacy dialogdef
 * for the destination. The graph validates the edge and target type before
 * delegating to menuPushDialog(). */
s32 menuGraphFirePushDialog(menu_type_t source,
                            const char *edge_id,
                            struct menudialogdef *dialogdef);
s32 menuGraphFirePushOp(menu_type_t source,
                        const char *edge_id,
                        MenuGraphPushOpFn op,
                        void *userdata);
s32 menuGraphFireSwitchSibling(menu_type_t source,
                               const char *edge_id,
                               struct menudialogdef *dialogdef);
s32 menuGraphFirePop(menu_type_t source, const char *edge_id);
s32 menuGraphFirePopOp(menu_type_t source,
                       const char *edge_id,
                       MenuGraphPopOpFn op,
                       void *userdata);
s32 menuGraphFireSceneOp(menu_type_t source,
                         const char *edge_id,
                         MenuGraphSceneOpFn op,
                         void *userdata);
s32 menuGraphFireNetworkOp(menu_type_t source,
                           const char *edge_id,
                           MenuGraphNetworkOpFn op,
                           void *userdata);
s32 menuGraphFireProcessOp(menu_type_t source,
                           const char *edge_id,
                           MenuGraphProcessOpFn op,
                           void *userdata);
s32 menuGraphFireLocalOp(menu_type_t source,
                         const char *edge_id,
                         MenuGraphLocalOpFn op,
                         void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PORT_MENUGRAPH_H */
