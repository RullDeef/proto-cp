#pragma once

#include "error.h"
#include "message.h"

struct SInvite {
	int timeout;
	unsigned int nb_participants;
	char part_name[64];
};

struct SMsgInvite {
	struct SMessage message;
	unsigned int nb_participants;
	char part_name[64];
};

void sinvite_fill_defaults(struct SInvite* invite);
void sinvite_set_timeout(struct SInvite* invite, int timeout);

// transforms invite object into sendable message
struct SMessage* sinvite_alloc_message(struct SInvite* invite);
enum SError sinvite_parse_message(struct SMessage* message, struct SInvite* invite);
