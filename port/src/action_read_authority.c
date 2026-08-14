#include "action_read_authority.h"

int actionReadAuthorityAllows(const ActionReadAuthorityInput *input)
{
    if (!input) {
        return 0;
    }

    if (input->aperture_decision == ACTION_READ_APERTURE_ALLOW) {
        return 1;
    }
    if (input->aperture_decision == ACTION_READ_APERTURE_DENY) {
        return 0;
    }
    if (input->aperture_decision != ACTION_READ_APERTURE_INHERIT) {
        return 0;
    }

    if (!input->gameplay_suppressed || !input->gameplay_only) {
        return 1;
    }

    return input->smoke_owned &&
        input->gameplay_context &&
        (input->focus_lost || input->focus_settle_active);
}
