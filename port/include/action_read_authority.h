#ifndef PD_ACTION_READ_AUTHORITY_H
#define PD_ACTION_READ_AUTHORITY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pure decision seam for ActionState reads. The caller remains responsible
 * for deriving these facts from the production input layer, context stack,
 * focus authority, and per-action smoke owner. */
enum ActionReadApertureDecision {
    ACTION_READ_APERTURE_INHERIT = -1,
    ACTION_READ_APERTURE_DENY = 0,
    ACTION_READ_APERTURE_ALLOW = 1,
};

typedef struct ActionReadAuthorityInput {
    int aperture_decision;
    int gameplay_suppressed;
    int gameplay_only;
    int smoke_owned;
    int gameplay_context;
    int focus_lost;
    int focus_settle_active;
} ActionReadAuthorityInput;

/* Returns 1 only when the action read is authorized. Invalid input and
 * invalid aperture values fail closed. Explicit typed-layer decisions always
 * win. A suppressed gameplay-only read can pass only for an exact smoke owner
 * in a gameplay-capable context during focus loss/regain settling. */
int actionReadAuthorityAllows(const ActionReadAuthorityInput *input);

#ifdef __cplusplus
}
#endif

#endif /* PD_ACTION_READ_AUTHORITY_H */
