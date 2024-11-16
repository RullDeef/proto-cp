#pragma once

struct SInvite {
  int timeout;
};

void sinvite_fill_defaults(struct SInvite *invite);

void sinvite_set_timeout(struct SInvite *invite, int timeout);
