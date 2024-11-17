#pragma once

#include <stddef.h>

#include "endpoint.h"
#include "participant.h"
#include "stypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

enum SMsgType {
	// conference invite. Includes information about conference in general as well as person, who
	// sends this message. Must be accepted or rejected by receiver
	SMSG_INVITE,

	// invite accepted. Contains address of invited participant that is listening for incoming
	// connections
	SMSG_INVITE_ACCEPT,

	// invite rejected. No additional data
	SMSG_INVITE_REJECT,

	// participants list updated. Keeps list of participant with their state change (joined or
	// leaving). Send everybody this message with yourself marked as leaving to gracefully leave
	// the conference
	SMSG_PART_PRESENCE,

	// participant information updated, not including connection state
	SMSG_PART_INFO,

	// audio packet received. Contains recording timestamp and id of participants besides actual
	// audio data
	SMSG_AUDIO,

	// video packet received. Contains recording timestamp and id of participants with regions in
	// merged frames
	SMSG_VIDEO,
};

// general message interface for passing between participants.
// Can be data frame with encoded audio/video frame or
// control message.
struct SMessage {
	size_t size;  // actual message size including this field
	enum SMsgType type;
	// message-specific params here
};

struct SMsgInvite {
	struct SMessage base;
	conf_id_t conf_id;
	part_id_t part_id;
	char part_name[];
};

struct SMsgInviteAccept {
	struct SMessage base;
	part_id_t id;         // invited participant id
	struct SEndpoint ep;  // listening endpoint
	char name[];          // invited participant name (NULL-terminated)
};

struct SMsgPartPresence {
	struct SMessage base;
	size_t count;  // amount of states[]
	struct PartPresenceState {
		part_id_t id;
		struct SEndpoint ep;
		enum PresenceState {
			PART_JOIN,
			PART_LEAVE,
		} state;
	} states[];
};

#pragma pack(pop)

struct SMessage* message_alloc(size_t size);
struct SMessage* message_alloc2(size_t size, enum SMsgType type);
void message_free(struct SMessage** msg);

struct SMessage* message_invite_alloc(conf_id_t conf_id, part_id_t part_id, const char* part_name);
struct SMessage* message_invite_accept_alloc(part_id_t id, const char* name, struct SEndpoint* ep);
struct SMessage* message_invite_reject_alloc(void);

// allocates part presence message with given states count
struct SMsgPartPresence* message_part_presence_alloc(size_t count);

#ifdef __cplusplus
}  // extern "C"
#endif
