#pragma once

#include <stddef.h>

struct SFrameMessage;

#pragma pack(push, 1)

enum SMsgType {
	// conference invite. Includes information about conference in general as well as person, who
	// sends this message. Must be accepted or rejected by receiver
	SMSG_INVITE,

	// invite accepted. No additional data
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

	// video packet received. Contains recording timestamp and id of participants with regions in merged frames
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

struct SMsgPartPresence {
	struct SMessage base;
	size_t count;
	struct PresenseState {
		unsigned long long part_id;
		enum PrState {
			PART_JOIN,
			PART_LEAVE,
		} state;
	} states[];
};

#pragma pack(pop)

struct SMessage* message_alloc(size_t size);
void message_free(struct SMessage** msg);
