#include "invite.h"

#include <stddef.h>
#include <string.h>

void sinvite_fill_defaults(struct SInvite* invite) {
	if (invite != NULL) {
		invite->timeout = 5000;
	}
}

void sinvite_set_timeout(struct SInvite* invite, int timeout) {
	if (invite != NULL) {
		invite->timeout = timeout;
	}
}

struct SMessage* sinvite_alloc_message(struct SInvite* invite) {
	struct SMsgInvite* message = (struct SMsgInvite*)message_alloc(sizeof(struct SMsgInvite));
	message->message.type      = SMSG_INVITE;
	message->nb_participants   = invite->nb_participants;
	memcpy(message->part_name, invite->part_name, sizeof(invite->part_name));
	return (struct SMessage*)message;
}

enum SError sinvite_parse_message(struct SMessage* message, struct SInvite* invite) {
	if (message == NULL || invite == NULL)
		return SELECON_INVALID_ARG;
	if (message->type != SMSG_INVITE)
		return SELECON_UNEXPECTED_MSG_TYPE;
	struct SMsgInvite* msg  = (struct SMsgInvite*)message;
	invite->timeout         = 0;
	invite->nb_participants = msg->nb_participants;
	memcpy(invite->part_name, msg->part_name, sizeof(msg->part_name));
	return SELECON_OK;
}
