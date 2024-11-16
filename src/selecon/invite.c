#include "invite.h"
#include <stddef.h>

void sinvite_fill_defaults(struct SInvite *invite) {
    if (invite != NULL) {
        invite->timeout = 5000;
    }
}

void sinvite_set_timeout(struct SInvite *invite, int timeout) {
    if (invite != NULL) {
        invite->timeout = timeout;
    }
}
